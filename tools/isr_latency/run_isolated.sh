#!/usr/bin/env bash
# Copyright (C) 2026 Andrii Shylenko
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This file is part of iolinki.
# See LICENSE for details.
#
# 在宿主上降低 isr_latency 的干扰（不重启）：
#   - 钉住目标核的频率（scaling_min = scaling_max）
#   - 关闭 intel_pstate 睿频
#   - 把 IRQ 移出目标核
#   - 禁用目标核的深 C-state
#   - 以 root 运行（获得 SCHED_FIFO）
# 所有设置都在退出时（含失败和 Ctrl-C）逐项恢复。
#
# 真正的独占（无 tick、无 SMT 兄弟竞争）需要在内核命令行加入并重启：
#   isolcpus=managed_irq,0,1 nohz_full=0,1 rcu_nocbs=0,1 irqaffinity=2-15
#
# 用法：tools/isr_latency/run_isolated.sh [--core N] [--iters N] [--warmup N] [--check]

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL="$ROOT/build_isr_latency/isr_latency"
BASELINE="$ROOT/tools/isr_latency_baseline.json"
CORE=0
FREQ_KHZ="${FREQ_KHZ:-2500000}"
MODE="write"
TOOL_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --core)
            CORE="${2:?missing value for --core}"
            shift 2
            ;;
        --iters)
            TOOL_ARGS+=("--iters" "${2:?missing value for --iters}")
            shift 2
            ;;
        --warmup)
            TOOL_ARGS+=("--warmup" "${2:?missing value for --warmup}")
            shift 2
            ;;
        --check)
            MODE="check"
            shift
            ;;
        --no-rt)
            TOOL_ARGS+=("--no-rt")
            shift
            ;;
        -h | --help)
            sed -n '2,25p' "$0"
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            exit 2
            ;;
    esac
done

if [ ! -x "$TOOL" ]; then
    echo "harness not built: cmake -S tools/isr_latency -B build_isr_latency" \
        "&& cmake --build build_isr_latency" >&2
    exit 1
fi
if ! sudo -v; then
    echo "sudo is required to pin frequency, move IRQs and disable C-states" >&2
    exit 1
fi

RESTORE_FILE="$(mktemp)"
RESTORE_ERRORS=0

restore_add()
{
    printf '%s|%s\n' "$1" "$2" >> "$RESTORE_FILE"
}

sudo_write()
{
    printf '%s' "$2" | sudo tee "$1" >/dev/null 2>&1
}

# shellcheck disable=SC2329  # invoked from the EXIT trap
do_restore()
{
    [ -f "$RESTORE_FILE" ] || return 0
    while IFS='|' read -r path value; do
        [ -n "$path" ] || continue
        if ! printf '%s' "$value" | sudo tee "$path" >/dev/null 2>&1; then
            echo "warning: failed to restore $path=$value" >&2
            RESTORE_ERRORS=$((RESTORE_ERRORS + 1))
        fi
    done < "$RESTORE_FILE"
    rm -f "$RESTORE_FILE"
    if [ "$RESTORE_ERRORS" -ne 0 ]; then
        echo "warning: $RESTORE_ERRORS setting(s) were not restored" >&2
    fi
}

trap 'do_restore' EXIT
trap 'exit 130' INT TERM

# --- 目标核之外的 CPU 列表（用于 IRQ 亲和性） ---
TARGET_CPUS=""
if [ "$CORE" -eq 0 ]; then
    TARGET_CPUS="1-$(( $(nproc) - 1 ))"
else
    LAST=$(( $(nproc) - 1 ))
    if [ "$CORE" -eq "$LAST" ]; then
        TARGET_CPUS="0-$(( CORE - 1 ))"
    else
        TARGET_CPUS="0-$(( CORE - 1 )),$(( CORE + 1 ))-$LAST"
    fi
fi

POLICY="/sys/devices/system/cpu/cpu$CORE/cpufreq"
if [ -d "$POLICY" ]; then
    MAX_FREQ="$(cat "$POLICY/cpuinfo_max_freq" 2>/dev/null || echo 0)"
    MIN_FREQ="$(cat "$POLICY/cpuinfo_min_freq" 2>/dev/null || echo 0)"
    if [ "$MAX_FREQ" -gt 0 ] && [ "$FREQ_KHZ" -gt "$MAX_FREQ" ]; then
        FREQ_KHZ="$MAX_FREQ"
    fi
    if [ "$MIN_FREQ" -gt 0 ] && [ "$FREQ_KHZ" -lt "$MIN_FREQ" ]; then
        FREQ_KHZ="$MIN_FREQ"
    fi
    restore_add "$POLICY/scaling_max_freq" "$(cat "$POLICY/scaling_max_freq")"
    restore_add "$POLICY/scaling_min_freq" "$(cat "$POLICY/scaling_min_freq")"
    sudo_write "$POLICY/scaling_max_freq" "$FREQ_KHZ"
    sudo_write "$POLICY/scaling_min_freq" "$FREQ_KHZ"
    echo "freq: cpu$CORE pinned to $FREQ_KHZ kHz"
else
    echo "note: cpufreq unavailable for cpu$CORE; frequency not pinned" >&2
fi

TURBO="/sys/devices/system/cpu/intel_pstate/no_turbo"
if [ -f "$TURBO" ]; then
    restore_add "$TURBO" "$(cat "$TURBO")"
    sudo_write "$TURBO" 1
    echo "turbo: disabled (no_turbo=1)"
fi

IRQ_MOVED=0
IRQ_BLOCKED=0
for f in /proc/irq/*/smp_affinity_list; do
    [ -e "$f" ] || continue
    old="$(cat "$f" 2>/dev/null)" || continue
    # 只记录/回滚真正移动成功的 IRQ：per-CPU 托管 IRQ 与级联 IRQ 不可改。
    if printf '%s' "$TARGET_CPUS" | sudo tee "$f" >/dev/null 2>&1; then
        restore_add "$f" "$old"
        IRQ_MOVED=$((IRQ_MOVED + 1))
    else
        IRQ_BLOCKED=$((IRQ_BLOCKED + 1))
    fi
done
echo "irq: moved $IRQ_MOVED handler(s) away from cpu$CORE (list $TARGET_CPUS), $IRQ_BLOCKED non-migratable"

for st in /sys/devices/system/cpu/cpu"$CORE"/cpuidle/state*; do
    [ -e "$st/disable" ] || continue
    name="$(cat "$st/name" 2>/dev/null || echo '?')"
    [ "$name" = "POLL" ] && continue
    restore_add "$st/disable" "$(cat "$st/disable")"
    sudo_write "$st/disable" 1
done
echo "cpuidle: deep C-states disabled on cpu$CORE"

# --- 运行 ---
if [ "$MODE" = "check" ]; then
    echo "run: check against $BASELINE"
    sudo "$TOOL" --core "$CORE" ${TOOL_ARGS[@]+"${TOOL_ARGS[@]}"} --check "$BASELINE"
    rc=$?
else
    # root 自己的临时文件：fs.protected_regular 会拒绝 root 写入用户在 /tmp 的 0600 文件。
    TMP_JSON="$(sudo mktemp)"
    echo "run: baseline -> $BASELINE"
    sudo "$TOOL" --core "$CORE" ${TOOL_ARGS[@]+"${TOOL_ARGS[@]}"} --json "$TMP_JSON"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        sudo chown "$(id -u):$(id -g)" "$TMP_JSON"
        mv "$TMP_JSON" "$BASELINE"
    else
        rm -f "$TMP_JSON"
    fi
fi

exit "$rc"
