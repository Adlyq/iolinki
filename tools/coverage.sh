#!/usr/bin/env bash
#
# Line/branch coverage for the core library.
#
# Requirements: gcovr (pacman -S gcovr, or apt-get install gcovr) and CMocka for
# the unit suite. The report covers the compiled src/ sources only.
#
# Usage:
#   ./tools/coverage.sh              # text report + summary for src/
#   ./tools/coverage.sh --html       # also write build_coverage/coverage/
#   ./tools/coverage.sh --filter dll # only files whose path contains "dll"
#
# Environment:
#   IOLINKI_COVERAGE_DIR   build root (default: <repo>/build_coverage)
#   JOBS                   parallel build jobs (default: nproc)

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${IOLINKI_COVERAGE_DIR:-$ROOT/build_coverage}"
if [ -z "${JOBS:-}" ]; then
    JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi

HTML=0
FILTER=""

usage() {
    sed -n '3,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --html) HTML=1 ;;
        --filter) shift; FILTER="${1:-}" ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 2 ;;
    esac
    shift
done

if ! command -v gcovr >/dev/null 2>&1; then
    echo "gcovr is not installed" >&2
    echo "  Arch:  sudo pacman -S gcovr" >&2
    echo "  Debian: sudo apt-get install gcovr" >&2
    exit 2
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build "$BUILD_DIR" --parallel "$JOBS"
(cd "$BUILD_DIR" && ctest --output-on-failure)

filter="$ROOT/src/"
if [ -n "$FILTER" ]; then
    filter="$ROOT/src/.*$FILTER"
fi

# gcovr changed its options over time: --branch existed before 8.7 (where branch
# coverage is on by default) and --merge-mode-functions only exists in newer
# releases.
gcovr_opts=()
help="$(gcovr --help 2>&1)"
if grep -qE '^  --branch(,| |$)' <<<"$help"; then
    gcovr_opts+=(--branch)
fi
if grep -q -- '--merge-mode-functions' <<<"$help"; then
    gcovr_opts+=(--merge-mode-functions=merge-use-line-min)
fi

gcovr -r "$ROOT" --filter "$filter" "${gcovr_opts[@]}" --print-summary

if [ "$HTML" -eq 1 ]; then
    mkdir -p "$BUILD_DIR/coverage"
    gcovr -r "$ROOT" --filter "$filter" "${gcovr_opts[@]}" \
        --html-details "$BUILD_DIR/coverage/index.html"
    echo "HTML report: $BUILD_DIR/coverage/index.html"
fi
