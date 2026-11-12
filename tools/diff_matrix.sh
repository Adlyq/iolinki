#!/usr/bin/env bash
#
# Differential frame gate (roadmap P1 / H₀).
#
# Rebuilds the protocol test suite under every runnable BYTE/BYTE
# configuration (the same table as tools/build_matrix.sh), records every device
# reply frame emitted through the mock PHY, and compares the transcripts byte
# for byte. A refactor that changes protocol behaviour fails here even when all
# assertions still pass.
#
# Only BYTE/BYTE configurations build the suite (build_matrix.sh keeps the tests
# off for FRAME transports, which are thin adapters covered by the compile
# matrix). Tests that emit no frames produce empty transcripts and compare
# trivially.
#
# Usage:
#   ./tools/diff_matrix.sh            # run the gate
#   ./tools/diff_matrix.sh --keep     # keep per-config matrix logs
#
# Environment:
#   IOLINKI_DIFF_DIR   build root (default: <repo>/build_diff)
#   JOBS               parallel build jobs (default: nproc)

set -uo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${IOLINKI_DIFF_DIR:-$ROOT/build_diff}"
TRACE_ROOT="$BUILD_ROOT/traces"
if [ -z "${JOBS:-}" ]; then
    JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi

KEEP=0

usage() {
    sed -n '3,21p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --keep) KEEP=1 ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 2 ;;
    esac
    shift
done

# Configurations whose suite runs (BYTE/BYTE + tests=yes in build_matrix.sh).
#
# P5 尝试把 FRAME 配置（isr_frame_frame / isr_frame_byte / isr_byte_frame）
# 也纳入本门禁，结论是当前**不可运行**，因此 CONFIGS 保持 BYTE/BYTE：
#  1. tools/build_matrix.sh 只对 BYTE/BYTE 打开 BUILD_TESTING（非 BYTE 传输
#     视为薄适配层，只做编译覆盖）；
#  2. 手工对 FRAME/FRAME 打开测试后，tests/test_ctx_sizes.c 的精确布局契约
#     失败：RX BYTE 专属的 isr_rx_buf/cnt/acc/expect（约 72 字节）在 FRAME
#     模式下不存在，所以 64 位尺寸断言需要按传输模式参数化；
#  3. 即使补上尺寸断言，tests/test_helpers.c 的 g_phy_mock 在 FRAME 模式下
#     把 rx_frame/tx_frame 置为 NULL（只有 raw-octet 钩子），依赖字节泵的
#     ISR 用例无法驱动帧。
# 差分门因此继续使用 BYTE/BYTE 的 6 个配置；FRAME 的语义等价目前由
# ./tools/build_matrix.sh 的编译矩阵覆盖。详见 docs/EVOLUTION_ROADMAP.md P5。
CONFIGS=(default release compat_v10 single_profile smp_profile stats_split)

mkdir -p "$BUILD_ROOT"
STATUS=0

for cfg in "${CONFIGS[@]}"; do
    printf '%-18s ' "$cfg"
    keep_flag=()
    [ "$KEEP" -eq 1 ] && keep_flag=(--keep)
    if ! IOLINKI_MATRIX_DIR="$BUILD_ROOT" JOBS="$JOBS" \
        "$ROOT/tools/build_matrix.sh" --filter "$cfg" "${keep_flag[@]}" \
        >"$BUILD_ROOT/$cfg.matrix.log" 2>&1; then
        echo "BUILD/TEST FAILED"
        tail -20 "$BUILD_ROOT/$cfg.matrix.log" | sed 's/^/    /'
        STATUS=1
        continue
    fi

    rm -rf "${TRACE_ROOT:?}/$cfg"
    mkdir -p "$TRACE_ROOT/$cfg"
    (
        cd "$BUILD_ROOT/$cfg/tests" || exit 1
        for test_bin in test_*; do
            [ -x "$test_bin" ] || continue
            IOLINK_DIFF_TRACE="$TRACE_ROOT/$cfg/$test_bin.txt" "./$test_bin" >/dev/null 2>&1 || true
        done
    )
    count="$(find "$TRACE_ROOT/$cfg" -name '*.txt' | wc -l)"
    echo "ok (${count} transcripts)"
done

BASE="$TRACE_ROOT/default"
if [ ! -d "$BASE" ]; then
    echo "diff gate: no baseline transcripts from the default configuration" >&2
    exit 1
fi

ALLOWLIST="$ROOT/tools/diff_allowlist.txt"

is_allowed() {
    local cfg="$1"
    local name="$2"
    local allowed_cfg allowed_test reason

    [ -f "$ALLOWLIST" ] || return 1
    while IFS='|' read -r allowed_cfg allowed_test reason; do
        case "$allowed_cfg" in
            ''|'#'*) continue ;;
        esac
        if [ "$allowed_cfg" = "$cfg" ] && [ "$allowed_test" = "$name" ]; then
            echo "    allowed: $cfg/$name ($reason)"
            return 0
        fi
    done <"$ALLOWLIST"
    return 1
}

for cfg in "${CONFIGS[@]:1}"; do
    for base_file in "$BASE"/*.txt; do
        name="$(basename "$base_file")"
        if is_allowed "$cfg" "$name"; then
            continue
        fi
        other="$TRACE_ROOT/$cfg/$name"
        if [ ! -f "$other" ]; then
            echo "DIFF $cfg/$name: transcript missing" >&2
            STATUS=1
            continue
        fi
        if ! cmp -s "$base_file" "$other"; then
            echo "DIFF $cfg/$name: reply frames differ from the default configuration" >&2
            cmp -l "$base_file" "$other" | head -3 | sed 's/^/    /' >&2
            STATUS=1
        fi
    done
done

if [ "$STATUS" -eq 0 ]; then
    echo "diff gate: PASS (all transcripts byte-identical across ${#CONFIGS[@]} configurations)"
else
    echo "diff gate: FAIL" >&2
fi
exit "$STATUS"
