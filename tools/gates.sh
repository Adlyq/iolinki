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
PARALLEL_DIR=""
cleanup() {
    rm -f "${IOLINK_GATES_SNAPSHOT:-}"
    if [ -n "$PARALLEL_DIR" ]; then
        rm -rf "$PARALLEL_DIR"
    fi
}
trap cleanup EXIT

ROOT="${IOLINK_GATES_ROOT:?gates: snapshot re-exec lost the repository root}"
cd "$ROOT"

GATE_GROUPS=(quick build context test gcc sanitize memcheck analyzer static docs matrix perf package fuzz)

STRICT_CFLAGS="-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow"
# -Wnullable-to-nonnull-conversion rejects passing a nullable expression to a
# non-null parameter. It is purely type-based with no data flow, so a nullable
# struct field that was checked one line earlier still has to be copied into an
# unqualified local before every call. The sites that remain in src/ are of two
# shapes: a field that the surrounding `if` or condition already tested, and the
# ISDU buffer pair (`buffer`/`response_buf`), which is attached and cleared
# together and is never NULL once `iolink_isdu_process()` gets past its early
# return. Copying those fields into locals silences the warning but the
# assignment itself is not checked, so the flag would stop enforcing the guard
# at the sites that need it. It stays off; the nullability oracle is `-Wnonnull`
# (literal NULL), `clang --analyze` (data flow) and `-fsanitize=null` (runtime,
# group `sanitize`). A nullable parameter that is genuinely meaningful is
# declared as such instead — see `iolink_diag_device_status()` taking a NULL
# event registry, pinned by a test that passes the literal NULL.
NULLABILITY_FLAGS=""

# clang is the primary compiler: the shipping toolchains are clang-based (ATFE
# is clang, AC6 armclang is LLVM) and the pointer annotations only exist under
# clang. gcc runs afterwards with the diagnostics clang does not offer.
CC_PRIMARY="${IOLINKI_CC:-clang}"
CXX_PRIMARY="${IOLINKI_CXX:-clang++}"
CC_SECONDARY="${IOLINKI_CC_SECONDARY:-gcc}"

BUILD_DIR="${IOLINKI_BUILD_DIR:-$ROOT/build/quality}"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$ROOT/${BUILD_DIR}" ;;
esac
RELEASE_DIR="${BUILD_DIR}_release"
SECONDARY_DIR="${BUILD_DIR}_${CC_SECONDARY}"
SANITIZE_DIR="${BUILD_DIR}_sanitize"
MEMCHECK_DIR="${BUILD_DIR}_memcheck"

usage() {
    cat >&2 <<'EOF'
Usage: tools/gates.sh [--keep-going] <group> [group ...]

Groups:
  quick    clang-format, per-file size, shellcheck, DLL ownership, nullability
           regions, test conventions, documentation references, dead fields and
           the public-API surface                       (seconds, no build)
  build    strict -Werror compilation with clang, Debug (-O0) and Release (-O3)
  context  field ownership between the ISR and main contexts, from the clang
           AST (~3 s)
  test     strict Debug build plus the unit suite (ctest)
  gcc      the gcc-only strict diagnostic set, plus -fanalyzer as a report
  sanitize the unit suite under AddressSanitizer + UndefinedBehaviorSanitizer
           (clang only; a sanitizer report fails the run)
  memcheck the unit binaries under valgrind memcheck (uninitialised reads,
           invalid accesses)
  analyzer the clang static analyzer over the library (clang --analyze)
  static   cppcheck (warning, style, performance, portability)
  docs     Doxygen warnings and the generated documentation blocks
  matrix   compile matrix plus the differential frame gate
  perf     coverage floor, WCET path proxy, size baseline and the stack-frame
           baseline
  package  install the exported CMake package and build a consumer against it
  fuzz     libFuzzer over the frame, ISDU and OD parsers (60 s per target;
           clang + libFuzzer runtime required)
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

# Three of the quick checks parse the whole tree (documentation references,
# struct fields, public API surface) and cost a few seconds each, more than
# every other quick step combined. They read nothing another step produces, so
# they start first, write to a private log and are joined at the end of the
# group: the group costs the slowest check instead of the sum. A failure still
# fails the group, and the log of the failing check is printed with it.
PARALLEL_LABELS=()
PARALLEL_PIDS=()
PARALLEL_LOGS=()

parallel_start() {
    local label="$1"
    shift
    if [ -z "$PARALLEL_DIR" ]; then
        PARALLEL_DIR="$(mktemp -d "${TMPDIR:-/tmp}/iolink-gates-parallel.XXXXXX")"
    fi
    local log="$PARALLEL_DIR/${#PARALLEL_PIDS[@]}.log"
    "$@" >"$log" 2>&1 &
    PARALLEL_PIDS+=("$!")
    PARALLEL_LABELS+=("$label")
    PARALLEL_LOGS+=("$log")
}

parallel_join() {
    local failed=0 index
    for index in "${!PARALLEL_PIDS[@]}"; do
        if wait "${PARALLEL_PIDS[$index]}"; then
            printf '   ... %s\n' "${PARALLEL_LABELS[$index]}"
            cat "${PARALLEL_LOGS[$index]}"
        else
            printf '   FAIL: %s\n' "${PARALLEL_LABELS[$index]}" >&2
            cat "${PARALLEL_LOGS[$index]}" >&2
            failed=1
        fi
    done
    PARALLEL_LABELS=()
    PARALLEL_PIDS=()
    PARALLEL_LOGS=()
    return "$failed"
}

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
    find src include tests examples fuzz -type f \( -name '*.c' -o -name '*.h' \) -print0 |
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
                   tools/check_nullability.sh tools/fuzz.sh
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

check_nullability() {
    ./tools/check_nullability.sh --selftest && ./tools/check_nullability.sh
}

check_docs_refs() {
    python3 tools/check_docs_refs.py --selftest && python3 tools/check_docs_refs.py
}

# The field and API-surface gates fail on any finding. The field migration
# removed the last known ones; a new finding is a regression.
check_dead_fields() {
    python3 tools/check_dead_fields.py --selftest && python3 tools/check_dead_fields.py --enforce
}

check_public_api() {
    if ! have clang; then
        skip "clang missing (the public-API scan needs the preprocessor)"
        return 0
    fi
    python3 tools/check_public_api.py --selftest && python3 tools/check_public_api.py --enforce
}

group_quick() {
    if ! have python3; then
        skip "python3 missing: documentation references, dead fields and public API not checked"
    else
        parallel_start "documentation references" check_docs_refs
        parallel_start "dead fields" check_dead_fields
        parallel_start "public API surface" check_public_api
    fi

    # Every step runs even when an earlier one failed: the group's verdict is
    # the conjunction, and one pass over the tree reports every problem it has.
    local status=0
    step "clang-format" check_format || status=1
    step "file size" check_file_size || status=1
    step "shellcheck" check_shellcheck || status=1
    step "DLL ownership isolation" ./tools/check_ownership.sh || status=1
    step "nullability regions" check_nullability || status=1
    step "test conventions (white-box, teardown)" check_test_conventions || status=1
    parallel_join || status=1
    return "$status"
}

# --- build / test -----------------------------------------------------------

# same_compiler <a> <b>: do two compiler names resolve to the same binary?
same_compiler() {
    local a b
    a=$(command -v "$1" 2>/dev/null || printf '%s' "$1")
    b=$(command -v "$2" 2>/dev/null || printf '%s' "$2")
    [ "$(readlink -f "$a" 2>/dev/null || printf '%s' "$a")" = \
      "$(readlink -f "$b" 2>/dev/null || printf '%s' "$b")" ]
}

# configure_tree <dir> <build-type> <compiler> [cmake args...]
#
# CMake refuses to reuse a cache that was generated by another compiler, and a
# cache from another source tree is just as unusable. Both cases delete the
# directory instead of failing the gate with a CMake error.
configure_tree() {
    local dir="$1" type="$2" cc="$3"
    shift 3
    local flags="$STRICT_CFLAGS $NULLABILITY_FLAGS"
    if [ -f "$dir/CMakeCache.txt" ]; then
        local cached_src cached_cc
        cached_src=$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$dir/CMakeCache.txt" |
            cut -d= -f2- || true)
        cached_cc=$(sed -n 's/^CMAKE_C_COMPILER:[A-Z_]*=//p' "$dir/CMakeCache.txt" | head -1)
        if [ -n "$cached_src" ] && [ "$cached_src" != "$ROOT" ]; then
            rm -rf "$dir"
        elif [ -n "$cached_cc" ] && ! same_compiler "$cached_cc" "$cc"; then
            rm -rf "$dir"
        fi
    fi
    cmake -S "$ROOT" -B "$dir" -DCMAKE_BUILD_TYPE="$type" \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_C_FLAGS="$flags" "$@"
}

build_debug() {
    configure_tree "$BUILD_DIR" Debug "$CC_PRIMARY" &&
        cmake --build "$BUILD_DIR" --parallel "$(nproc)"
}

# Debug (-O0) does not emit some -O2/-O3-only diagnostics (e.g.
# -Wmaybe-uninitialized), hence the second strict build at -O3.
build_release() {
    rm -rf "$RELEASE_DIR"
    configure_tree "$RELEASE_DIR" Release "$CC_PRIMARY" &&
        cmake --build "$RELEASE_DIR" --parallel "$(nproc)"
}

group_build() {
    step "strict Debug build ($CC_PRIMARY)" build_debug || return 1
    step "strict Release (-O3) build ($CC_PRIMARY)" build_release
}

group_test() {
    step "strict Debug build ($CC_PRIMARY)" build_debug || return 1
    step "unit suite (ctest)" ctest --test-dir "$BUILD_DIR" --output-on-failure
}

# --- gcc supplement ---------------------------------------------------------

# The flag set lives in CMakeLists.txt (IOLINK_STRICT_GCC) because the
# stack-usage bounds apply to the library, not to the host examples that
# allocate multi-kilobyte contexts.
build_secondary() {
    configure_tree "$SECONDARY_DIR" Release "$CC_SECONDARY" -DIOLINK_STRICT_GCC=ON &&
        cmake --build "$SECONDARY_DIR" --parallel "$(nproc)"
}

# -fanalyzer is interprocedural and slow, and its findings need triage, so the
# step reports a count and the first findings instead of failing the gate.
gcc_analyzer() {
    if ! have "$CC_SECONDARY"; then
        skip "$CC_SECONDARY missing"
        return 0
    fi
    local out
    out=$(for f in "$ROOT"/src/*.c; do
        "$CC_SECONDARY" -std=c99 -fanalyzer -Wall -Wextra \
            -I"$ROOT/include" -I"$ROOT/src" -c "$f" -o /dev/null 2>&1 || true
    done)
    # A compile error means the step did not analyse anything; that is a gate
    # failure, not a finding to triage.
    if printf '%s\n' "$out" | grep -q 'error:'; then
        printf '   %s -fanalyzer could not compile the library:\n' "$CC_SECONDARY"
        printf '%s\n' "$out" | grep 'error:' | head -5
        return 1
    fi
    out=$(printf '%s\n' "$out" | grep -E 'warning:' || true)
    if [ -z "$out" ]; then
        printf '   -fanalyzer: clean\n'
        return 0
    fi
    printf '   -fanalyzer: %s warning(s), report-only; first 10:\n' "$(printf '%s\n' "$out" | wc -l)"
    printf '%s\n' "$out" | head -10
}

group_gcc() {
    if ! have "$CC_SECONDARY"; then
        skip "$CC_SECONDARY missing"
        return 0
    fi
    step "strict build with $CC_SECONDARY extras" build_secondary || return 1
    step "$CC_SECONDARY -fanalyzer (report-only)" gcc_analyzer
}

# --- sanitize ---------------------------------------------------------------

# UndefinedBehaviorSanitizer catches the integer-conversion and shift mistakes
# the compiler only warns about, AddressSanitizer the out-of-bounds accesses on
# the fixed-size frame/OD/page buffers. `unsigned-integer-overflow` is not
# included: the CRC and checksum arithmetic relies on wrapping by design.
SANITIZE_FLAGS="-fsanitize=address,undefined,implicit-conversion,bounds,null -fno-sanitize-recover=all -fno-omit-frame-pointer -g"

build_sanitize() {
    # configure_tree() sets CMAKE_C_FLAGS first; the later -D wins, which is how
    # the sanitizer flags replace the plain strict set. The example subprojects
    # also enable C++, so the C++ compiler has to match: `implicit-conversion`
    # is a clang sanitizer and the linker flags reach the C++ link as well.
    configure_tree "$SANITIZE_DIR" Debug "$CC_PRIMARY" \
        "-DCMAKE_CXX_COMPILER=$CXX_PRIMARY" \
        "-DCMAKE_C_FLAGS=$STRICT_CFLAGS $NULLABILITY_FLAGS $SANITIZE_FLAGS" \
        "-DCMAKE_EXE_LINKER_FLAGS=$SANITIZE_FLAGS" &&
        cmake --build "$SANITIZE_DIR" --parallel "$(nproc)"
}

run_sanitize_tests() {
    ASAN_OPTIONS="detect_leaks=0" \
        UBSAN_OPTIONS="print_stacktrace=1" \
        ctest --test-dir "$SANITIZE_DIR" --output-on-failure
}

group_sanitize() {
    case "$CC_PRIMARY" in
        *clang*) ;;
        *)
            skip "sanitizers need clang (IOLINKI_CC=$CC_PRIMARY)"
            return 0
            ;;
    esac
    step "sanitizer build (asan + ubsan + implicit-conversion)" build_sanitize || return 1
    step "unit suite under sanitizers" run_sanitize_tests
}

# --- memcheck ---------------------------------------------------------------

build_memcheck() {
    # -gdwarf-4: valgrind releases in older distributions (3.18 on Ubuntu 22.04)
    # cannot parse the DWARF 5 that current clang emits by default and abort
    # with "ensure_valid failed" on every binary.
    configure_tree "$MEMCHECK_DIR" Debug "$CC_PRIMARY" \
        "-DCMAKE_C_FLAGS=$STRICT_CFLAGS $NULLABILITY_FLAGS -gdwarf-4" &&
        cmake --build "$MEMCHECK_DIR" --parallel "$(nproc)"
}

# valgrind memcheck is the only tool here that reports reads of uninitialised
# memory, which is the defect class the empty-context bugs belong to. Only the
# unit binaries run: the PTY conformance examples spawn a device process and are
# covered by the plain `test` group.
run_memcheck() {
    local failed=0
    local err
    err="$(mktemp "${TMPDIR:-/tmp}/iolink-memcheck.XXXXXX")"
    for test_bin in "$MEMCHECK_DIR"/tests/test_*; do
        [ -x "$test_bin" ] || continue
        if ! valgrind --error-exitcode=1 --leak-check=no --quiet \
            --track-origins=yes "$test_bin" >/dev/null 2>"$err"; then
            printf '   FAIL: %s\n' "${test_bin##*/}" >&2
            sed -n '1,12p' "$err" >&2
            failed=1
        fi
    done
    rm -f "$err"
    return "$failed"
}

group_memcheck() {
    if ! have valgrind; then
        skip "valgrind missing (apt-get install valgrind)"
        return 0
    fi
    # valgrind cannot redirect loader symbols on some distribution/glibc
    # combinations; it then fails at startup on every binary, which is a host
    # problem, not a finding. Probe once and say so instead of reporting 80
    # identical failures.
    if ! valgrind --quiet --error-exitcode=1 /bin/true >/dev/null 2>&1; then
        skip "valgrind cannot start on this host (needs glibc debug info: 'debuginfod' on Arch, 'libc6-dbg' on Debian)"
        return 0
    fi
    step "build for memcheck" build_memcheck || return 1
    step "unit binaries under valgrind" run_memcheck
}

# --- analyzer ---------------------------------------------------------------

# The clang static analyzer follows the data flow the compiler warnings cannot
# see (a pointer that is only NULL on one path, a store read after a reset).
#
# It runs as `clang --analyze` per translation unit rather than through
# scan-build: scan-build intercepts compilation by putting a `cc` wrapper on
# PATH, and `configure_tree` pins CMAKE_C_COMPILER to an absolute path, so the
# wrapper never sees a single invocation and the run reports "no bugs" whatever
# the code says.
run_analyzer() {
    local out
    out=$(for f in "$ROOT"/src/*.c; do
        "$CC_PRIMARY" --analyze -std=c99 -I"$ROOT/include" -I"$ROOT/src" \
            -Xanalyzer -analyzer-output=text "$f" 2>&1 || true
    done)
    # A compile error means the file was not analysed at all; that is a gate
    # failure, not a finding.
    if printf '%s\n' "$out" | grep -q 'error:'; then
        printf '   the analyzer could not compile the library:\n'
        printf '%s\n' "$out" | grep 'error:' | head -5
        return 1
    fi
    out=$(printf '%s\n' "$out" | grep -E 'warning:' || true)
    if [ -z "$out" ]; then
        printf '   analyzer: clean\n'
        return 0
    fi
    printf '   analyzer: %s finding(s); first 10:\n' "$(printf '%s\n' "$out" | wc -l)"
    printf '%s\n' "$out" | head -10
    return 1
}

group_analyzer() {
    case "$CC_PRIMARY" in
        *clang*) ;;
        *)
            skip "the clang static analyzer needs clang (IOLINKI_CC=$CC_PRIMARY)"
            return 0
            ;;
    esac
    step "clang static analyzer (clang --analyze)" run_analyzer
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

group_static() {
    step "cppcheck" check_cppcheck
}

# --- context ---------------------------------------------------------------

# The context gate builds a call graph from the clang AST of every src/ unit
# and fails on each write the ISR performs on main-owned state (and the reverse),
# classified against tools/shared_fields.csv. A new field or call edge that
# crosses the boundary without a category is a hard failure.
check_context() {
    python3 tools/check_context.py --selftest && python3 tools/check_context.py --enforce
}

group_context() {
    if ! have python3; then
        skip "python3 missing"
        return 0
    fi
    if ! have clang; then
        skip "clang missing (the gate reads the clang AST)"
        return 0
    fi
    step "ISR/main field ownership" check_context
}

# --- docs ------------------------------------------------------------------

check_docs_generated() {
    if ! have python3; then
        skip "python3 missing"
        return 0
    fi
    python3 tools/gen_docs.py --selftest && python3 tools/gen_docs.py --check &&
        python3 tools/gen_spec_constants.py --check
}

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
    step "generated documentation blocks" check_docs_generated || return 1
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

# The stack report compiles every src/ unit for the target, so it needs the same
# cross toolchain as the size baseline; without one it reports SKIP instead of a
# measurement, exactly like the size gate.
check_stack_baseline() {
    if [ -z "${IOLINKI_STACK_CC:-}" ] && ! have arm-none-eabi-gcc; then
        skip "no target compiler for the stack report (install arm-none-eabi-gcc or set IOLINKI_STACK_CC)"
        return 0
    fi
    python3 tools/stack_report.py --selftest && python3 tools/stack_report.py --check
}

group_perf() {
    step "coverage floor" ./tools/coverage.sh --check || return 1
    step "WCET path proxy" ./tools/wcet_run.sh --check || return 1
    step "ROM/RAM size baseline" check_size_baseline || return 1
    step "stack-frame baseline" check_stack_baseline
}

# --- fuzz -------------------------------------------------------------------

# libFuzzer over the three parsers that take untrusted bytes. The harnesses live
# in fuzz/ and are built in their own tree (tools/fuzz.sh), because the library
# has to carry sanitizer coverage for the fuzzer to make progress.
group_fuzz() {
    if ! have "$CC_PRIMARY"; then
        skip "$CC_PRIMARY missing (the harnesses use libFuzzer)"
        return 0
    fi
    step "libFuzzer harnesses" ./tools/fuzz.sh
}

# --- package ---------------------------------------------------------------

group_package() {
    step "install and consume" ./tools/test_install_consumer.sh
}

# --- dispatch --------------------------------------------------------------

# known_group <name>: is this a group the dispatch below can run? The argument
# parser derives from GATE_GROUPS instead of repeating the list, so adding a
# group cannot leave the command line behind (that is how `fuzz` was once
# unreachable through `tools/gates.sh` while `all` already ran it).
known_group() {
    local candidate="$1" known
    for known in "${GATE_GROUPS[@]}"; do
        [ "$candidate" = "$known" ] && return 0
    done
    return 1
}

run_group() {
    case "$1" in
        quick) group_quick ;;
        build) group_build ;;
        context) group_context ;;
        test) group_test ;;
        gcc) group_gcc ;;
        sanitize) group_sanitize ;;
        memcheck) group_memcheck ;;
        analyzer) group_analyzer ;;
        static) group_static ;;
        docs) group_docs ;;
        matrix) group_matrix ;;
        perf) group_perf ;;
        fuzz) group_fuzz ;;
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
        *)
            if known_group "$arg"; then
                requested+=("$arg")
            else
                echo "unknown argument: $arg" >&2
                usage
                exit 2
            fi
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
