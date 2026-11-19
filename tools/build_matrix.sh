#!/usr/bin/env bash
#
# Compile matrix: build every structural configuration.
#
# The matrix is structure x platform, not one entry per switch. The table lives
# in tools/matrix_configs.sh, shared with tools/diff_matrix.sh so the two gates
# cannot drift apart. This is the delivery gate for changes that touch
# IOLINK_FEATURE_*, IOLINK_PHY_FEATURE_*, IOLINK_ISR_*_MODE, IOLINK_COMPAT_*,
# IOLINK_PLATFORM or any #if in src/ and include/.
#
# Besides the structural configurations it runs:
#   - compile-only combination probes for legal but uninstantiated layouts;
#   - the SINGLE profile's "no iolink_critical_*" nm -u assertion;
#   - the link-time profile gate (tools/check_profile_abi.sh).
#
# --reuse-tree keeps an existing build tree instead of recreating it: cmake runs
# again with the row's declared arguments, so the cache and the compile commands
# match tools/matrix_configs.sh, and the tree is then rebuilt incrementally.
# ctest is not run in this mode, whether the tree existed or not: the caller owns
# the suite (tools/diff_matrix.sh executes every tests/test_* binary itself).
#
# Usage:
#   ./tools/build_matrix.sh                 # build everything; run the suite for
#                                           # the configurations that support it
#   ./tools/build_matrix.sh --list          # print the configuration names
#   ./tools/build_matrix.sh --filter host   # only names containing "host"
#   ./tools/build_matrix.sh --only host_frame  # exactly this configuration
#   ./tools/build_matrix.sh --probes-only   # only the compile-only probes
#   ./tools/build_matrix.sh --reuse-tree    # configure and build the existing
#                                           # trees in place; ctest is not re-run
#   ./tools/build_matrix.sh --fail-fast     # stop at the first failure
#   ./tools/build_matrix.sh --keep          # keep the logs of passing configs
#
# Environment:
#   IOLINKI_MATRIX_DIR   build root (default: <repo>/build/matrix)
#   JOBS                 parallel build jobs (default: nproc)
#   CC                   C compiler for the probes (default: IOLINKI_CC, else clang)
#   IOLINKI_CC           C compiler for the matrix rows (default: clang)

set -uo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=tools/matrix_configs.sh
. "$ROOT/tools/matrix_configs.sh"
BUILD_ROOT="${IOLINKI_MATRIX_DIR:-$ROOT/build/matrix}"
CC_BIN="${CC:-${IOLINKI_CC:-clang}}"
if [ -z "${JOBS:-}" ]; then
    JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi

STRICT_FLAGS="-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wundef"

FAIL_FAST=0
KEEP=0
LIST=0
FILTER=""
ONLY=""
PROBES_ONLY=0
REUSE_TREE=0
STOP=0
RESULTS=()

usage() {
    sed -n '3,38p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list) LIST=1 ;;
        --fail-fast) FAIL_FAST=1 ;;
        --keep) KEEP=1 ;;
        --filter) shift; FILTER="${1:-}" ;;
        --only) shift; ONLY="${1:-}" ;;
        --probes-only) PROBES_ONLY=1 ;;
        --reuse-tree) REUSE_TREE=1 ;;
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
    if [ -n "$ONLY" ] && [ "$name" != "$ONLY" ]; then
        return 0
    fi
    if [ "$PROBES_ONLY" -eq 1 ]; then
        return 0
    fi
    if [ "$LIST" -eq 1 ]; then
        printf '%-22s %-12s %-8s tests=%s\n' "$name" "$mode" "$build_type" "$run_tests"
        return 0
    fi

    local dir="$BUILD_ROOT/$name"
    local cflags="$STRICT_FLAGS $cflags_extra"
    local transport=""
    case "$mode" in
        BYTE_BYTE) transport=BYTE ;;
        FRAME_FRAME) transport=FRAME ;;
        FRAME_BYTE|BYTE_FRAME) transport="" ;; # 混合粒度只有编译探针
        *) echo "internal: bad mode $mode" >&2; exit 2 ;;
    esac
    case "$mode" in
        BYTE_BYTE) ;;
        FRAME_FRAME) cflags+=" -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME" ;;
        FRAME_BYTE) cflags+=" -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_BYTE" ;;
        BYTE_FRAME) cflags+=" -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_BYTE -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME" ;;
    esac

    # The unit suite runs on every transport that has a test contract
    # (BYTE/BYTE and FRAME/FRAME); the mock PHY implements both. Mixed-granularity
    # layouts stay compile-only. IOLINK_TEST_TRANSPORT lets tests/CMakeLists.txt
    # drop the targets that pin a BYTE-only contract.
    local tests_flag=ON
    if [ "$run_tests" != "yes" ] || [ -z "$transport" ]; then
        tests_flag=OFF
    fi

    printf '%-22s ' "$name"

    # Everything this row decides; a reuse run compares it against the value the
    # tree was built with. cmake re-applies the -D arguments, but a variable the
    # row no longer declares would survive in the cache, so a changed row starts
    # from an empty tree instead.
    local row_args
    row_args="$build_type|${IOLINKI_CC:-clang}|$cflags|$tests_flag|${transport:-BYTE}|$cmake_extra"
    if [ "$REUSE_TREE" -eq 1 ]; then
        if [ -f "$dir/.matrix_row_args" ] && [ "$(cat "$dir/.matrix_row_args")" != "$row_args" ]; then
            rm -rf "$dir"
        fi
        mkdir -p "$dir"
    else
        rm -rf "$dir"
        mkdir -p "$dir"
    fi
    printf '%s\n' "$row_args" >"$dir/.matrix_row_args"
    # shellcheck disable=SC2086 # intentional word splitting of the option list
    if ! cmake -S "$ROOT" -B "$dir" \
            "-DCMAKE_BUILD_TYPE=$build_type" \
            "-DCMAKE_C_COMPILER=${IOLINKI_CC:-clang}" \
            "-DCMAKE_C_FLAGS=$cflags" \
            "-DBUILD_TESTING=$tests_flag" \
            "-DIOLINK_TEST_TRANSPORT=${transport:-BYTE}" \
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

    if [ "$tests_flag" = "ON" ] && [ "$REUSE_TREE" -eq 0 ]; then
        if ! (cd "$dir" && ctest --output-on-failure >"$dir/test.log" 2>&1); then
            echo "TEST FAILED"
            tail -30 "$dir/test.log" | sed 's/^/    /'
            RESULTS+=("$name|test|fail")
            [ "$FAIL_FAST" -eq 1 ] && STOP=1
            return 1
        fi
        echo "ok (built + tested)"
    elif [ "$tests_flag" = "ON" ]; then
        echo "ok (built, suite not run)"
    else
        echo "ok (built)"
    fi
    RESULTS+=("$name|ok")

    if [ "$KEEP" -eq 0 ]; then
        rm -f "$dir/configure.log" "$dir/build.log" "$dir/test.log"
    fi
    return 0
}

# run_probe <name> <cflags>: syntax-only compile of the core sources.
#
# The probe does not link, so it cannot catch a missing definition; its job is
# to keep legal, uninstantiated preprocessor combinations compiling. A failure
# is red.
PROBE_SOURCES=(
    src/dll.c
    src/dll_frame.c
    src/dll_sm.c
    src/dll_reply.c
    src/dll_api.c
    src/dll_isr.c
    src/dll_isr_rx.c
    src/isdu.c
    src/isdu_cmd.c
    src/isdu_ds.c
    src/device.c
    src/capability.c
    src/profile.c
    src/data_storage.c
    src/events.c
)

run_probe() {
    local name="$1"
    local cflags_extra="$2"
    local failed=0
    local src

    printf 'probe:%-16s ' "$name"
    for src in "${PROBE_SOURCES[@]}"; do
        # shellcheck disable=SC2086 # intentional word splitting of the flag list
        if ! "$CC_BIN" -std=c99 -I"$ROOT/include" $STRICT_FLAGS $cflags_extra \
                -fsyntax-only "$ROOT/$src" >"$BUILD_ROOT/probe_$name.log" 2>&1; then
            failed=1
            break
        fi
    done
    if [ "$failed" -ne 0 ]; then
        echo "PROBE FAILED ($src)"
        sed -n '1,20p' "$BUILD_ROOT/probe_$name.log" | sed 's/^/    /'
        RESULTS+=("probe:$name|compile|fail")
        [ "$FAIL_FAST" -eq 1 ] && STOP=1
        return 1
    fi
    echo "ok"
    RESULTS+=("probe:$name|ok")
    rm -f "$BUILD_ROOT/probe_$name.log"
    return 0
}

mkdir -p "$BUILD_ROOT"

iolink_matrix_emit run_config

RUN_PROBES=0
RUN_GATES=0
if [ "$LIST" -eq 0 ]; then
    if [ "$PROBES_ONLY" -eq 1 ]; then
        RUN_PROBES=1
    elif [ -z "$ONLY" ] && [ -z "$FILTER" ]; then
        RUN_PROBES=1
        RUN_GATES=1
    fi
fi

if [ "$RUN_PROBES" -eq 1 ]; then
    iolink_matrix_emit_probes run_probe
fi

if [ "$RUN_GATES" -eq 1 ]; then
    # Minimum-dependency proof: a SINGLE profile never references the
    # critical-section hooks, so a port is allowed to omit them entirely.
    crit_cfg="$(iolink_matrix_no_critical_config)"
    if [ -f "$BUILD_ROOT/$crit_cfg/libiolinki.a" ]; then
        undef_syms="$(nm -u "$BUILD_ROOT/$crit_cfg/libiolinki.a" 2>/dev/null || true)"
        if printf '%s\n' "$undef_syms" | grep -q 'iolink_critical_'; then
            echo "$crit_cfg   critical-section dependency detected"
            RESULTS+=("$crit_cfg|dependency|fail")
        else
            echo "$crit_cfg   no critical-section dependency"
        fi
    elif [ "$STOP" -eq 0 ]; then
        # 归档缺失说明该配置没有真正构建，跳过会把门禁变成空操作。
        # --fail-fast 下前序失败已经让配置没有产出，不重复计一次失败。
        echo "$crit_cfg   missing libiolinki.a (critical-section gate not run)"
        RESULTS+=("$crit_cfg|dependency|fail")
    fi

    # Link-time profile gate: a mismatched application must fail to link.
    if [ "$STOP" -eq 0 ]; then
        printf '%-22s ' "profile_abi"
        if "$ROOT/tools/check_profile_abi.sh" >"$BUILD_ROOT/profile_abi.log" 2>&1; then
            echo "ok"
            RESULTS+=("profile_abi|ok")
            rm -f "$BUILD_ROOT/profile_abi.log"
        else
            echo "FAILED"
            sed -n '1,20p' "$BUILD_ROOT/profile_abi.log" | sed 's/^/    /'
            RESULTS+=("profile_abi|link|fail")
            [ "$FAIL_FAST" -eq 1 ] && STOP=1
        fi
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
    echo "compile matrix: no configuration matched '${FILTER}${ONLY}'"
    exit 1
fi
if [ "$FAILED" -eq 0 ]; then
    echo "compile matrix: PASS (${#RESULTS[@]} jobs, ${#IOLINK_MATRIX_STRUCTURAL_CONFIGS[@]} structural configurations)"
    exit 0
fi

echo "compile matrix: FAIL ($FAILED of ${#RESULTS[@]} jobs)"
for entry in ${RESULTS[@]+"${RESULTS[@]}"}; do
    case "$entry" in
        *"|ok") ;;
        *) echo "  FAILED: $entry" ;;
    esac
done
exit 1
