#!/usr/bin/env bash
#
# Grouped quality gates: a change only pays for the checks it can break.
#
# Each group is one topic (formatting, compilation, static analysis, ...). Name
# the groups a change touches; `all` runs every group and is the local
# equivalent of the CI gate set, which GH Actions runs split across parallel
# jobs (see docs/TESTING.md).
#
# The first failing group stops the run unless --keep-going is given.

set -euo pipefail

# A running bash reads its script incrementally: editing the checkout while a
# run is in progress makes the parser fail halfway with a bogus "unexpected
# token". Re-exec once from a snapshot so an invocation always executes one
# consistent version of this file.
if [ -z "${IOLINK_GATES_SNAPSHOT:-}" ]; then
    IOLINK_GATES_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
    snapshot="$(mktemp "${TMPDIR:-/tmp}/iolink-gates.XXXXXX")"
    cp -- "${BASH_SOURCE[0]}" "$snapshot"
    IOLINK_GATES_SNAPSHOT="$snapshot" IOLINK_GATES_ROOT="$IOLINK_GATES_ROOT" \
        exec bash "$snapshot" "$@"
fi
trap 'rm -f "$IOLINK_GATES_SNAPSHOT"' EXIT

ROOT="${IOLINK_GATES_ROOT:?gates: snapshot re-exec lost the repository root}"
cd "$ROOT"

GATE_GROUPS=(quick build test static docs matrix perf package)

STRICT_CFLAGS="-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow"

BUILD_DIR="${IOLINKI_BUILD_DIR:-$ROOT/build/quality}"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$ROOT/${BUILD_DIR}" ;;
esac
RELEASE_DIR="${BUILD_DIR}_release"

usage() {
    cat >&2 <<'EOF'
Usage: tools/gates.sh [--keep-going] <group> [group ...]

Groups:
  quick    clang-format, per-file size, shellcheck, DLL ownership and the test
           conventions                                    (seconds, no build)
  build    strict -Werror compilation, Debug (-O0) and Release (-O3)
  test     Debug build plus the unit suite (ctest)
  static   cppcheck and the MISRA addon (report-only unless
           IOLINKI_MISRA_ENFORCE=1)
  docs     Doxygen warnings
  matrix   compile matrix plus the differential frame gate
  perf     coverage floor, WCET path proxy and size baseline
  package  install the exported CMake package and build a consumer against it
  all      every group above

Groups compose: tools/gates.sh quick static
EOF
}

have() {
    command -v "$1" >/dev/null 2>&1
}

skip() {
    printf '   SKIP: %s\n' "$*"
}

step() {
    local label="$1"
    shift
    printf '   ... %s\n' "$label"
    if "$@"; then
        return 0
    fi
    printf '   FAIL: %s\n' "$label" >&2
    return 1
}

# --- quick ------------------------------------------------------------------

check_format() {
    if ! have clang-format; then
        skip "clang-format missing (pip install clang-format==23.1.1)"
        return 0
    fi
    local version
    version=$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)
    if [ "$version" != "23.1.1" ]; then
        printf '   WARN: clang-format %s != pinned 23.1.1 (results may differ)\n' \
            "${version:-unknown}"
    fi
    find src include tests examples -type f \( -name '*.c' -o -name '*.h' \) -print0 |
        xargs -0 clang-format --dry-run --Werror
}

check_file_size() {
    if ! have python3; then
        skip "python3 missing"
        return 0
    fi
    python3 tools/check_file_size.py --selftest && python3 tools/check_file_size.py
}

check_shellcheck() {
    if ! have shellcheck; then
        skip "shellcheck missing (Arch: pacman -S shellcheck-static / Debian: apt-get install shellcheck)"
        return 0
    fi
    # Maintained scripts only; tools/add_copyright_headers.sh is a legacy
    # one-shot helper with a known SC2218 false positive.
    local scripts=(tools/gates.sh tools/build_matrix.sh tools/coverage.sh
                   tools/diff_matrix.sh tools/check_ownership.sh
                   tools/check_test_whitebox.sh tools/check_test_gates.sh
                   tools/clean_builds.sh tools/test_install_consumer.sh
                   tools/wcet_run.sh tools/check_profile_abi.sh
                   tools/matrix_configs.sh
                   test_all.sh .docker_test_entrypoint.sh)
    shellcheck -S warning "${scripts[@]}"
}

check_test_conventions() {
    ./tools/check_test_whitebox.sh --selftest &&
        ./tools/check_test_whitebox.sh &&
        ./tools/check_test_gates.sh
}

group_quick() {
    step "clang-format" check_format || return 1
    step "file size" check_file_size || return 1
    step "shellcheck" check_shellcheck || return 1
    step "DLL ownership isolation" ./tools/check_ownership.sh || return 1
    step "test conventions (white-box, teardown)" check_test_conventions
}

# --- build / test -----------------------------------------------------------

configure_debug() {
    if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
        local cached_src
        cached_src=$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$BUILD_DIR/CMakeCache.txt" |
            cut -d= -f2- || true)
        if [ -n "$cached_src" ] && [ "$cached_src" != "$ROOT" ]; then
            rm -rf "$BUILD_DIR"
        fi
    fi
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="$STRICT_CFLAGS"
}

build_debug() {
    configure_debug && cmake --build "$BUILD_DIR" --parallel "$(nproc)"
}

# Debug (-O0) does not emit some -O2/-O3-only diagnostics (e.g.
# -Wmaybe-uninitialized), hence the second strict build at -O3.
build_release() {
    rm -rf "$RELEASE_DIR"
    cmake -S "$ROOT" -B "$RELEASE_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$STRICT_CFLAGS" &&
        cmake --build "$RELEASE_DIR" --parallel "$(nproc)"
}

group_build() {
    step "strict Debug build" build_debug || return 1
    step "strict Release (-O3) build" build_release
}

group_test() {
    step "strict Debug build" build_debug || return 1
    step "unit suite (ctest)" ctest --test-dir "$BUILD_DIR" --output-on-failure
}

# --- static ----------------------------------------------------------------

cppcheck_run() {
    cppcheck --enable=warning,style,performance,portability \
        --suppress=missingIncludeSystem \
        --suppress=constParameterPointer \
        --suppress=knownConditionTrueFalse \
        --inline-suppr \
        --quiet \
        -I include \
        src/ examples/ "$@"
}

check_cppcheck() {
    if ! have cppcheck; then
        skip "cppcheck missing (apt-get install cppcheck)"
        return 0
    fi
    cppcheck_run --error-exitcode=1
}

check_misra() {
    if ! have cppcheck; then
        skip "cppcheck missing (apt-get install cppcheck)"
        return 0
    fi
    # The MISRA addon is very noisy on this C99 codebase (~1500 pre-existing
    # findings), so it is report-only unless enforcement is requested.
    local addon=""
    local path
    for path in /usr/share/cppcheck/addons/misra.py /usr/lib/cppcheck/addons/misra.py; do
        if [ -f "$path" ]; then
            addon="$path"
            break
        fi
    done
    if [ -z "$addon" ]; then
        if [ "${IOLINKI_MISRA_ENFORCE:-}" = "1" ]; then
            echo "   FAIL: MISRA addon not available and IOLINKI_MISRA_ENFORCE=1" >&2
            return 1
        fi
        skip "cppcheck MISRA addon not available"
        return 0
    fi
    if [ "${IOLINKI_MISRA_ENFORCE:-}" = "1" ]; then
        cppcheck_run --addon=misra --error-exitcode=1
        return
    fi
    cppcheck_run --addon=misra || true
    echo "   MISRA report generated (non-enforcing; IOLINKI_MISRA_ENFORCE=1 makes it fatal)"
}

group_static() {
    step "cppcheck" check_cppcheck || return 1
    step "MISRA C:2012 (cppcheck addon)" check_misra
}

# --- docs ------------------------------------------------------------------

check_doxygen() {
    if ! have doxygen; then
        skip "doxygen missing"
        return 0
    fi
    local log="$ROOT/doxygen.log"
    doxygen Doxyfile >/dev/null 2>"$log" || true
    if grep -q "warning:" "$log"; then
        echo "   Doxygen warnings:" >&2
        grep "warning:" "$log" >&2
        return 1
    fi
    rm -f "$log"
}

group_docs() {
    step "Doxygen" check_doxygen
}

# --- matrix ----------------------------------------------------------------

group_matrix() {
    step "compile matrix" ./tools/build_matrix.sh || return 1
    step "differential frame gate" ./tools/diff_matrix.sh
}

# --- perf ------------------------------------------------------------------

check_size_baseline() {
    if ! have arm-none-eabi-gcc; then
        skip "arm-none-eabi-gcc missing (size baseline needs the Arm toolchain)"
        return 0
    fi
    python3 tools/size_matrix.py --check --selftest
}

group_perf() {
    step "coverage floor" ./tools/coverage.sh --check || return 1
    step "WCET path proxy" ./tools/wcet_run.sh --check || return 1
    step "ROM/RAM size baseline" check_size_baseline
}

# --- package ---------------------------------------------------------------

group_package() {
    step "install and consume" ./tools/test_install_consumer.sh
}

# --- dispatch --------------------------------------------------------------

run_group() {
    case "$1" in
        quick) group_quick ;;
        build) group_build ;;
        test) group_test ;;
        static) group_static ;;
        docs) group_docs ;;
        matrix) group_matrix ;;
        perf) group_perf ;;
        package) group_package ;;
        *)
            echo "internal error: unknown group $1" >&2
            return 2
            ;;
    esac
}

if [ "$#" -eq 0 ]; then
    usage
    exit 2
fi

keep_going=0
requested=()
for arg in "$@"; do
    case "$arg" in
        -h | --help)
            usage
            exit 0
            ;;
        --keep-going)
            keep_going=1
            ;;
        all)
            requested+=("${GATE_GROUPS[@]}")
            ;;
        quick | build | test | static | docs | matrix | perf | package)
            requested+=("$arg")
            ;;
        *)
            echo "unknown argument: $arg" >&2
            usage
            exit 2
            ;;
    esac
done

selected=()
if [ "${#requested[@]}" -eq 0 ]; then
    usage
    exit 2
fi
for group in "${requested[@]}"; do
    case " ${selected[*]-} " in
        *" $group "*) ;;
        *) selected+=("$group") ;;
    esac
done

failed=()
for group in "${selected[@]}"; do
    start=$SECONDS
    printf '\n=== [%s] ===\n' "$group"
    if run_group "$group"; then
        printf '=== [%s] PASS (%ss) ===\n' "$group" "$((SECONDS - start))"
    else
        printf '=== [%s] FAIL (%ss) ===\n' "$group" "$((SECONDS - start))" >&2
        failed+=("$group")
        if [ "$keep_going" -eq 0 ]; then
            break
        fi
    fi
done

echo
if [ "${#failed[@]}" -eq 0 ]; then
    echo "gates: PASS (${selected[*]})"
    exit 0
fi
echo "gates: FAIL (${failed[*]})" >&2
exit 1
