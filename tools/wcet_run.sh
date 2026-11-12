#!/usr/bin/env bash
# Copyright (C) 2026 Andrii Shylenko
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 构建并运行主机侧 WCET 路径长度代理基准（tools/wcet）。
#
#   ./tools/wcet_run.sh                 构建 + 运行，打印指标表
#   ./tools/wcet_run.sh --check         与 tools/wcet_baseline.json 比较（增长即失败）
#   ./tools/wcet_run.sh --update        用本次实测结果覆盖基线（显式操作）
#   ./tools/wcet_run.sh --build-dir DIR 指定构建目录（默认 build_wcet/）
#
# --check 还会用 --repeat 2 运行每个场景，验证同一场景重复运行指标完全一致。
# 指标是调用次数（路径长度代理），不是 CPU 周期 WCET；周期上界见
# tools/wcet/README.md 与 docs/EVOLUTION_ROADMAP.md D8。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${WCET_BUILD_DIR:-${REPO_ROOT}/build_wcet}"
BASELINE="${SCRIPT_DIR}/wcet_baseline.json"
MODE="run"

usage() {
    cat <<'EOF'
用法：./tools/wcet_run.sh [--check | --update] [--build-dir DIR]

  （无参数）      构建 + 运行，打印各场景指标表
  --check        与 tools/wcet_baseline.json 比较；任一指标增长即失败
  --update       用本次实测结果覆盖基线（显式操作）
  --build-dir    指定构建目录（默认 build_wcet/）

指标是路径长度代理（调用次数），不是 CPU 周期 WCET；
周期上界见 tools/wcet/README.md 与 docs/EVOLUTION_ROADMAP.md D8。
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check)
            MODE="check"
            shift
            ;;
        --update)
            MODE="update"
            shift
            ;;
        --build-dir)
            if [[ $# -lt 2 ]]; then
                echo "错误：--build-dir 需要参数" >&2
                exit 2
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "错误：未知参数 $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS="4"
fi

mkdir -p "${BUILD_DIR}"
BUILD_LOG="${BUILD_DIR}/wcet_build.log"
METRICS="${BUILD_DIR}/wcet_metrics.json"

echo "[wcet] 构建 ${BUILD_DIR}"
if ! cmake -S "${SCRIPT_DIR}/wcet" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
    >"${BUILD_LOG}" 2>&1 ||
    ! cmake --build "${BUILD_DIR}" --parallel "${JOBS}" >>"${BUILD_LOG}" 2>&1; then
    cat "${BUILD_LOG}" >&2
    echo "[wcet] 构建失败" >&2
    exit 1
fi

echo "[wcet] 运行全部场景"
"${BUILD_DIR}/wcet_harness" --repeat 2 --json "${METRICS}"

case "${MODE}" in
    run)
        echo
        echo "[wcet] 指标已写入 ${METRICS}"
        echo "[wcet] 与基线比较：./tools/wcet_run.sh --check"
        ;;
    update)
        cp "${METRICS}" "${BASELINE}"
        echo
        echo "[wcet] 基线已更新：${BASELINE}"
        ;;
    check)
        if [[ ! -f "${BASELINE}" ]]; then
            echo "[wcet] 基线不存在：${BASELINE}" >&2
            echo "[wcet] 先运行 ./tools/wcet_run.sh --update 并提交基线" >&2
            exit 1
        fi
        python3 - "${BASELINE}" "${METRICS}" <<'PY'
import json
import sys

baseline_path, current_path = sys.argv[1], sys.argv[2]

with open(baseline_path, encoding="utf-8") as handle:
    baseline = json.load(handle)["scenarios"]
with open(current_path, encoding="utf-8") as handle:
    current = json.load(handle)["scenarios"]

failures = []
warnings = []

for scenario, base_metrics in baseline.items():
    if scenario not in current:
        failures.append("%s: 当前运行缺少该场景" % scenario)
        continue
    current_metrics = current[scenario]
    for metric, base_value in base_metrics.items():
        if metric not in current_metrics:
            failures.append("%s.%s: 当前运行缺少该指标" % (scenario, metric))
            continue
        current_value = current_metrics[metric]
        if current_value > base_value:
            failures.append(
                "%s.%s: %d > 基线 %d (+%d)"
                % (scenario, metric, current_value, base_value, current_value - base_value)
            )
        elif current_value < base_value:
            warnings.append(
                "%s.%s: %d < 基线 %d（允许，可考虑 --update）"
                % (scenario, metric, current_value, base_value)
            )

for scenario in current:
    if scenario not in baseline:
        warnings.append("%s: 新场景未纳入基线（允许，可考虑 --update）" % scenario)

for message in warnings:
    print("[wcet] 提示: " + message)

if failures:
    for message in failures:
        print("[wcet] 失败: " + message, file=sys.stderr)
    print("[wcet] WCET 路径长度代理门未通过（只允许不增长）", file=sys.stderr)
    sys.exit(1)

print("[wcet] WCET 路径长度代理门通过：所有指标均未超过基线")
PY
        ;;
esac
