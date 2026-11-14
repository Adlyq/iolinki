#!/usr/bin/env bash
#
# Line/branch coverage for the core library.
#
# Requirements: gcovr (pacman -S gcovr, or apt-get install gcovr) and CMocka for
# the unit suite. The report covers the compiled src/ sources only.
#
# Usage:
#   ./tools/coverage.sh                       # BYTE transport: text report + summary
#   ./tools/coverage.sh --transport FRAME     # FRAME transport only
#   ./tools/coverage.sh --transport both      # BYTE + FRAME + the merged report
#   ./tools/coverage.sh --html                # also write build_coverage/coverage/
#   ./tools/coverage.sh --check               # fail when line coverage < tools/coverage_baseline.txt
#   ./tools/coverage.sh --filter dll          # only files whose path contains "dll"
#
# Transports:
#   The ISR code in src/ is compiled per transport, so a BYTE build holds
#   neither the FRAME-only branches nor the other way round. Each transport
#   therefore gets its own build tree (BYTE: $IOLINKI_COVERAGE_DIR, FRAME:
#   $IOLINKI_COVERAGE_DIR/frame) and its own gcovr JSON tracefile, and
#   --transport both merges the two with gcovr --add-tracefile. --check always
#   compares the BYTE result, because tools/coverage_baseline.txt is calibrated
#   on the BYTE build; the merged number is reported but never gated.
#
# Environment:
#   IOLINKI_COVERAGE_DIR   build root (default: <repo>/build_coverage)
#   JOBS                   parallel build jobs (default: nproc)
#
# JSON tracefiles: <build root>/coverage-byte.json,
# <build root>/frame/coverage.json and, for --transport both,
# <build root>/coverage-merged.json.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${IOLINKI_COVERAGE_DIR:-$ROOT/build_coverage}"
if [ -z "${JOBS:-}" ]; then
    JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi

BYTE_DIR="$BUILD_DIR"
FRAME_DIR="$BUILD_DIR/frame"
BYTE_JSON="$BUILD_DIR/coverage-byte.json"
FRAME_JSON="$FRAME_DIR/coverage.json"
MERGED_JSON="$BUILD_DIR/coverage-merged.json"

HTML=0
CHECK=0
FILTER=""
TRANSPORT="BYTE"

usage() {
    awk '/^# Usage:/ { p = 1 } p && !/^#/ { exit } p' "${BASH_SOURCE[0]}" |
        sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --html) HTML=1 ;;
        --check) CHECK=1 ;;
        --filter) shift; FILTER="${1:-}" ;;
        --transport) shift; TRANSPORT="${1:-}" ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 2 ;;
    esac
    shift
done

wants_byte=0
wants_frame=0
case "$TRANSPORT" in
    BYTE) wants_byte=1 ;;
    FRAME) wants_frame=1 ;;
    both) wants_byte=1; wants_frame=1 ;;
    *)
        echo "invalid --transport: '$TRANSPORT' (expected BYTE, FRAME or both)" >&2
        exit 2
        ;;
esac

if ! command -v gcovr >/dev/null 2>&1; then
    echo "gcovr is not installed" >&2
    echo "  Arch:  sudo pacman -S gcovr" >&2
    echo "  Debian: sudo apt-get install gcovr" >&2
    exit 2
fi

# The baseline is calibrated on the BYTE build, so gating a FRAME-only run would
# compare unrelated numbers.
if [ "$CHECK" -eq 1 ] && [ "$wants_byte" -eq 0 ]; then
    echo "--check compares the BYTE result; use --transport BYTE or both" >&2
    exit 2
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

filter="$ROOT/src/"
if [ -n "$FILTER" ]; then
    filter="$ROOT/src/.*$FILTER"
fi

# A reused tree keeps .gcno/.gcda from older source layouts; gcovr then
# attributes lines to the wrong file and reports false regressions. Always
# start from a clean coverage tree. FRAME lives inside the BYTE tree, so a
# FRAME-only run only has to drop its own subtree, and a BYTE run (or `both`)
# wipes the shared root before anything is built.
if [ "$wants_byte" -eq 1 ]; then
    rm -rf "${BUILD_DIR:?}"
else
    rm -rf "${FRAME_DIR:?}"
fi

build_transport() {
    local transport="$1" dir="$2"
    local cmake_args=(-S "$ROOT" -B "$dir"
        -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON)
    if [ "$transport" = "FRAME" ]; then
        # The transport is a compile-time ISR granularity: the FRAME code only
        # exists in the library when these definitions are set.
        cmake_args+=(
            -DIOLINK_PLATFORM=LINUX
            -DIOLINK_TEST_TRANSPORT=FRAME
            -DCMAKE_C_FLAGS="-DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME"
        )
    fi
    echo "== building and testing the ${transport} transport in ${dir}" >&2
    cmake "${cmake_args[@]}"
    cmake --build "$dir" --parallel "$JOBS"
    (cd "$dir" && ctest --output-on-failure)
}

# Report from one build tree. gcovr collects from the working directory, so it
# runs inside the tree: with both trees present a BYTE run from the repository
# root would silently absorb the FRAME objects.
report_transport() {
    local dir="$1" json="$2"
    shift 2
    (cd "$dir" && gcovr -r "$ROOT" --filter "$filter" "${gcovr_opts[@]}" \
        --json "$json" "$@" --print-summary)
}

byte_out=""
frame_out=""

if [ "$wants_byte" -eq 1 ]; then
    build_transport BYTE "$BYTE_DIR" || {
        echo "BYTE transport build or tests failed" >&2
        exit 1
    }
    if [ "$wants_frame" -eq 1 ]; then
        byte_out="$(report_transport "$BYTE_DIR" "$BYTE_JSON")"
        echo "== BYTE transport ($BYTE_DIR) =="
    else
        byte_out="$(report_transport "$BYTE_DIR" "$BYTE_JSON" --txt)"
    fi
    printf '%s\n' "${byte_out%$'\n'}"
fi

if [ "$wants_frame" -eq 1 ]; then
    build_transport FRAME "$FRAME_DIR" || {
        echo "FRAME transport build or tests failed" >&2
        exit 1
    }
    if [ "$wants_byte" -eq 1 ]; then
        frame_out="$(report_transport "$FRAME_DIR" "$FRAME_JSON")"
        echo "== FRAME transport ($FRAME_DIR) =="
        printf '%s\n' "${frame_out%$'\n'}"
    else
        frame_out="$(report_transport "$FRAME_DIR" "$FRAME_JSON" --txt)"
        printf '%s\n' "${frame_out%$'\n'}"
    fi
fi

if [ "$wants_byte" -eq 1 ] && [ "$wants_frame" -eq 1 ]; then
    echo "== merged BYTE + FRAME =="
    gcovr -r "$ROOT" --filter "$filter" "${gcovr_opts[@]}" \
        --add-tracefile "$BYTE_JSON" --add-tracefile "$FRAME_JSON" \
        --json "$MERGED_JSON" --txt --print-summary
fi

if [ "$CHECK" -eq 1 ]; then
    baseline_file="$ROOT/tools/coverage_baseline.txt"
    if [ ! -f "$baseline_file" ]; then
        echo "missing baseline file: $baseline_file" >&2
        exit 2
    fi
    actual="$(printf '%s\n' "$byte_out" | sed -nE 's/^lines: ([0-9]+\.[0-9]+)%.*/\1/p')"
    baseline="$(sed -nE 's/^lines=([0-9]+\.[0-9]+).*/\1/p' "$baseline_file")"
    if [ -z "$actual" ] || [ -z "$baseline" ]; then
        echo "could not parse the coverage summary or $baseline_file" >&2
        exit 2
    fi
    if ! awk -v actual="$actual" -v baseline="$baseline" \
        'BEGIN { exit (actual + 0 >= baseline + 0) ? 0 : 1 }'; then
        echo "coverage regression: ${actual}% < ${baseline}% baseline" >&2
        exit 1
    fi
    suffix=""
    if [ "$wants_frame" -eq 1 ]; then
        suffix=" (BYTE transport, the merged number is not gated)"
    fi
    echo "coverage check: ${actual}% >= ${baseline}% baseline${suffix}"
fi

if [ "$HTML" -eq 1 ]; then
    mkdir -p "$BUILD_DIR/coverage"
    tracefiles=()
    if [ "$wants_byte" -eq 1 ]; then
        tracefiles+=(--add-tracefile "$BYTE_JSON")
    fi
    if [ "$wants_frame" -eq 1 ]; then
        tracefiles+=(--add-tracefile "$FRAME_JSON")
    fi
    gcovr -r "$ROOT" --filter "$filter" "${gcovr_opts[@]}" "${tracefiles[@]}" \
        --html-details "$BUILD_DIR/coverage/index.html"
    echo "HTML report: $BUILD_DIR/coverage/index.html"
fi
