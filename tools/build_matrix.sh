#!/usr/bin/env bash
#
# Compile matrix: build every supported compile-time configuration.
#
# This is the delivery gate for changes that touch IOLINK_FEATURE_*,
# IOLINK_PHY_FEATURE_*, IOLINK_ISR_*_MODE, IOLINK_COMPAT_*, IOLINK_PLATFORM or
# any #if in src/ and include/. A change is not ready while the matrix is red.
#
# Usage:
#   ./tools/build_matrix.sh                 # build everything; run the suite for
#                                           # the configurations that support it
#   ./tools/build_matrix.sh --list          # print the configuration names
#   ./tools/build_matrix.sh --filter isr_   # only names containing "isr_"
#   ./tools/build_matrix.sh --fail-fast     # stop at the first failure
#   ./tools/build_matrix.sh --keep          # keep the logs of passing configs
#
# Environment:
#   IOLINKI_MATRIX_DIR   build root (default: <repo>/build_matrix)
#   JOBS                 parallel build jobs (default: nproc)

set -uo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${IOLINKI_MATRIX_DIR:-$ROOT/build_matrix}"
if [ -z "${JOBS:-}" ]; then
    JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi

STRICT_FLAGS="-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow"

FAIL_FAST=0
KEEP=0
LIST=0
FILTER=""
STOP=0
RESULTS=()

usage() {
    sed -n '3,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list) LIST=1 ;;
        --fail-fast) FAIL_FAST=1 ;;
        --keep) KEEP=1 ;;
        --filter) shift; FILTER="${1:-}" ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 2 ;;
    esac
    shift
done

# run_config <name> <mode> <build-type> <run-tests> <cmake-extra> <cflags-extra>
run_config() {
    local name="$1"
    local mode="$2"
    local build_type="$3"
    local run_tests="$4"
    local cmake_extra="$5"
    local cflags_extra="$6"

    [ "$STOP" -eq 1 ] && return 0
    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        return 0
    fi
    if [ "$LIST" -eq 1 ]; then
        printf '%-22s %-12s %-8s tests=%s\n' "$name" "$mode" "$build_type" "$run_tests"
        return 0
    fi

    local dir="$BUILD_ROOT/$name"
    local cflags="$STRICT_FLAGS $cflags_extra"
    case "$mode" in
        BYTE_BYTE) ;;
        FRAME_FRAME) cflags+=" -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME" ;;
        FRAME_BYTE) cflags+=" -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_BYTE" ;;
        BYTE_FRAME) cflags+=" -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_BYTE -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME" ;;
        *) echo "internal: bad mode $mode" >&2; exit 2 ;;
    esac

    # Tests drive the BYTE-only virtual PHY and BYTE mock vtables, so they are
    # only meaningful on the BYTE/BYTE layout.
    local tests_flag=ON
    if [ "$mode" != "BYTE_BYTE" ] || [ "$run_tests" != "yes" ]; then
        tests_flag=OFF
    fi

    printf '%-22s ' "$name"

    rm -rf "$dir"
    mkdir -p "$dir"
    # shellcheck disable=SC2086 # intentional word splitting of the option list
    if ! cmake -S "$ROOT" -B "$dir" \
            "-DCMAKE_BUILD_TYPE=$build_type" \
            "-DCMAKE_C_FLAGS=$cflags" \
            "-DBUILD_TESTING=$tests_flag" \
            $cmake_extra >"$dir/configure.log" 2>&1; then
        echo "CONFIGURE FAILED"
        sed -n '1,40p' "$dir/configure.log" | sed 's/^/    /'
        RESULTS+=("$name|configure|fail")
        [ "$FAIL_FAST" -eq 1 ] && STOP=1
        return 1
    fi

    if ! cmake --build "$dir" --parallel "$JOBS" >"$dir/build.log" 2>&1; then
        echo "BUILD FAILED"
        grep -nE 'error:|undefined reference' "$dir/build.log" | head -20 | sed 's/^/    /'
        RESULTS+=("$name|build|fail")
        [ "$FAIL_FAST" -eq 1 ] && STOP=1
        return 1
    fi

    if [ "$tests_flag" = "ON" ]; then
        if ! (cd "$dir" && ctest --output-on-failure >"$dir/test.log" 2>&1); then
            echo "TEST FAILED"
            tail -30 "$dir/test.log" | sed 's/^/    /'
            RESULTS+=("$name|test|fail")
            [ "$FAIL_FAST" -eq 1 ] && STOP=1
            return 1
        fi
        echo "ok (built + tested)"
    else
        echo "ok (built)"
    fi
    RESULTS+=("$name|ok")

    if [ "$KEEP" -eq 0 ]; then
        rm -f "$dir/configure.log" "$dir/build.log" "$dir/test.log"
    fi
    return 0
}

# name                   mode          type     tests  cmake-extra                  cflags-extra
run_config default             BYTE_BYTE   Debug   yes   ""                            ""
run_config release             BYTE_BYTE   Release yes   ""                            ""
run_config isr_frame_frame     FRAME_FRAME Debug   no    ""                            ""
run_config isr_frame_byte      FRAME_BYTE  Debug   no    ""                            ""
run_config isr_byte_frame      BYTE_FRAME  Debug   no    ""                            ""
run_config isr_tx_only         BYTE_BYTE   Debug   no    ""                            "-DIOLINK_PHY_FEATURE_ISR_TX_ONLY=1"
run_config minimal_profile     BYTE_BYTE   Debug   no    "-DIOLINK_MINIMAL_PROFILE=ON" ""
run_config tiny_profile        BYTE_BYTE   Debug   no    "-DIOLINK_TINY_PROFILE=ON"    ""
run_config no_data_storage     BYTE_BYTE   Debug   no    "-DIOLINK_FEATURE_DATA_STORAGE=OFF" ""
run_config no_system_command   BYTE_BYTE   Debug   no    "-DIOLINK_FEATURE_SYSTEM_COMMAND=OFF" ""
run_config no_events           BYTE_BYTE   Debug   no    "-DIOLINK_FEATURE_EVENTS=OFF" ""
run_config no_legacy_params    BYTE_BYTE   Debug   no    "-DIOLINK_FEATURE_LEGACY_PARAMS=OFF" ""
run_config no_wakeup           BYTE_BYTE   Debug   no    "-DIOLINK_PHY_FEATURE_WAKEUP=OFF" ""
run_config no_sio_cq           BYTE_BYTE   Debug   no    "-DIOLINK_PHY_FEATURE_SIO_CQ=OFF" ""
run_config no_legacy_diag      BYTE_BYTE   Debug   no    "-DIOLINK_PHY_FEATURE_LEGACY_DIAG=OFF" ""
run_config phy_owns_line_state BYTE_BYTE   Debug   no    ""                            "-DIOLINK_PHY_OWNS_LINE_STATE=1"
run_config compat_v10          BYTE_BYTE   Debug   no    ""                            "-DIOLINK_COMPAT_V10_STARTUP_OPERATE=1 -DIOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE=1"
run_config ds_legacy_sub0      BYTE_BYTE   Debug   no    ""                            "-DIOLINK_DS_LEGACY_IMAGE_SUB0=1"
run_config baremetal           BYTE_BYTE   Debug   no    "-DIOLINK_PLATFORM=BAREMETAL" ""
run_config single_profile      BYTE_BYTE   Debug   yes   ""                            "-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_SINGLE"
# SMP is compile-only: IOLINK_CONCURRENCY>=RTOS requires config.lock/unlock and
# the test harness does not supply one, so the device suite cannot run here.
run_config smp_profile         BYTE_BYTE   Debug   no    ""                            "-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_SMP -DIOLINK_FENCE_LEVEL=IOLINK_FENCE_SMP -DIOLINK_FENCE_SMP_IMPL=__sync_synchronize"
run_config stats_split         BYTE_BYTE   Debug   yes   ""                            "-DIOLINK_STATS_MODE=IOLINK_STATS_SPLIT"

# Minimum-dependency proof: a SINGLE profile never references the
# critical-section hooks, so a port is allowed to omit them entirely.
if [ "$LIST" -eq 0 ] && [ -f "$BUILD_ROOT/single_profile/libiolinki.a" ]; then
    undef_syms="$(nm -u "$BUILD_ROOT/single_profile/libiolinki.a" 2>/dev/null || true)"
    if printf '%s\n' "$undef_syms" | grep -q 'iolink_critical_'; then
        echo "single_profile         critical-section dependency detected"
        RESULTS+=("single_profile|dependency|fail")
    else
        echo "single_profile         no critical-section dependency"
    fi
fi

if [ "$LIST" -eq 1 ]; then
    exit 0
fi

FAILED=0
for entry in ${RESULTS[@]+"${RESULTS[@]}"}; do
    case "$entry" in
        *"|ok") ;;
        *) FAILED=$((FAILED + 1)) ;;
    esac
done

echo
if [ "${#RESULTS[@]}" -eq 0 ]; then
    echo "compile matrix: no configuration matched '${FILTER}'"
    exit 1
fi
if [ "$FAILED" -eq 0 ]; then
    echo "compile matrix: PASS (${#RESULTS[@]} configurations)"
    exit 0
fi

echo "compile matrix: FAIL ($FAILED of ${#RESULTS[@]} configurations)"
for entry in ${RESULTS[@]+"${RESULTS[@]}"}; do
    case "$entry" in
        *"|ok") ;;
        *) echo "  FAILED: $entry" ;;
    esac
done
exit 1
