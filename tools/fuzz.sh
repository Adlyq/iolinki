#!/usr/bin/env bash
#
# Fuzz gate: libFuzzer harnesses for the three parsers that consume untrusted
# bytes — received M-sequences, ISDU payloads, and OD indices/values.
#
# A separate tree is required, not a convenience: the library itself has to be
# built with `-fsanitize=fuzzer-no-link` so the fuzzer gets coverage feedback
# from the code under test. Reusing build/ would slow every other gate down and
# mix instrumentation into binaries the memcheck/size gates measure.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

FUZZ_DIR="${FUZZ_DIR:-build/fuzz}"
FUZZ_SECONDS="${FUZZ_SECONDS:-${IOLINKI_FUZZ_SECONDS:-60}}"
cc="${IOLINKI_CC:-clang}"

# libFuzzer reads 0 as "run forever", which would hang the whole `all` run; a
# typo has to fail here instead.
case "$FUZZ_SECONDS" in
    '' | *[!0-9]* | 0)
        echo "FUZZ_SECONDS must be a positive integer, got '$FUZZ_SECONDS'" >&2
        exit 2
        ;;
esac

if ! command -v "$cc" >/dev/null 2>&1; then
    echo "skip: $cc not found (the harnesses need libFuzzer)" >&2
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# A clang without the libFuzzer runtime must skip, not fail: the compiler is
# the primary one for the other gates, the runtime is a separate package.
cat >"$tmp/probe.c" <<'PROBE'
int LLVMFuzzerTestOneInput(const unsigned char* data, unsigned long size)
{
    (void) data;
    (void) size;
    return 0;
}
PROBE
if ! "$cc" -fsanitize=fuzzer "$tmp/probe.c" -o "$tmp/probe" >/dev/null 2>&1; then
    echo "skip: $cc cannot link -fsanitize=fuzzer (libFuzzer runtime missing)" >&2
    exit 0
fi

# CMake refuses a cache from another source tree or compiler, and the fuzz tree
# carries different instrumentation than every other gate. Same comparison as
# gates.sh: resolve the paths, so two names for one binary do not trigger a
# rebuild and two different binaries with one name cannot slip through.
same_compiler() {
    local a b
    a=$(command -v "$1" 2>/dev/null || printf '%s' "$1")
    b=$(command -v "$2" 2>/dev/null || printf '%s' "$2")
    [ "$(readlink -f "$a" 2>/dev/null || printf '%s' "$a")" = \
        "$(readlink -f "$b" 2>/dev/null || printf '%s' "$b")" ]
}
if [ -f "$FUZZ_DIR/CMakeCache.txt" ]; then
    cached_src="$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$FUZZ_DIR/CMakeCache.txt" |
        cut -d= -f2- || true)"
    cached_cc="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z_]*=//p' "$FUZZ_DIR/CMakeCache.txt" | head -1)"
    if [ -n "$cached_src" ] && [ "$cached_src" != "$root" ]; then
        rm -rf "$FUZZ_DIR"
    elif [ -n "$cached_cc" ] && ! same_compiler "$cached_cc" "$cc"; then
        rm -rf "$FUZZ_DIR"
    fi
fi

cmake -S "$root" -B "$FUZZ_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="$cc" \
    -DIOLINK_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF >/dev/null
# Only the harnesses: the examples in the same tree would link the instrumented
# library without the sanitizer runtime.
cmake --build "$FUZZ_DIR" --target fuzz_frame fuzz_isdu fuzz_od --parallel "$(nproc)"

corpus="$FUZZ_DIR/corpus"
artifacts="$FUZZ_DIR/artifacts"
mkdir -p "$corpus/frame" "$corpus/isdu" "$corpus/od" "$artifacts"

# Seeds: byte patterns that reach past the first length/type check, plus the
# checksum vectors from tests/test_crc.c (MC/CKT shapes the parser recognises).
# The fuzzer grows the corpus from here; files are only written once so a
# previous run's discoveries survive.
seed() {
    local dir="$1" name="$2" hex="$3"
    [ -s "$dir/$name" ] && return 0
    # `sed` below turns every byte pair into \xHH; an odd-length or non-hex
    # string would silently become ASCII instead of the intended byte, so
    # reject it here.
    case "$hex" in
        '' | *[!0-9a-fA-F]*)
            echo "seed $name: '$hex' is not a hex string" >&2
            exit 2
            ;;
    esac
    if [ $((${#hex} % 2)) -ne 0 ]; then
        echo "seed $name: '$hex' is not an even-length hex string" >&2
        exit 2
    fi
    printf '%b' "$(printf '%s' "$hex" | sed 's/../\\x&/g')" >"$dir/$name"
}

seed "$corpus/frame" 00 0000
seed "$corpus/frame" 01 a200
seed "$corpus/frame" 02 200099
seed "$corpus/frame" 03 0040a55a
seed "$corpus/frame" 04 8080
seed "$corpus/frame" 05 600000
seed "$corpus/frame" 06 5a
seed "$corpus/frame" 07 ff
seed "$corpus/frame" 08 0000000000

seed "$corpus/isdu" 00 001000
seed "$corpus/isdu" 01 004000
seed "$corpus/isdu" 02 000300
seed "$corpus/isdu" 03 0002006300
seed "$corpus/isdu" 04 4000015a
seed "$corpus/isdu" 05 0000000000

seed "$corpus/od" 00 00100000
seed "$corpus/od" 01 00400005
seed "$corpus/od" 02 004200
seed "$corpus/od" 03 00000000
seed "$corpus/od" 04 ffff0001

failed=0
for target in frame isdu od; do
    exe="$FUZZ_DIR/fuzz/fuzz_$target"
    log="$FUZZ_DIR/$target.log"
    printf '   fuzz_%s: %s s\n' "$target" "$FUZZ_SECONDS"
    # -timeout turns a parser that stops making progress into a finding instead
    # of a hung gate; ASan's leak check runs at exit. -use_value_profile lets
    # the fuzzer learn the checksum bytes and the 16-bit OD indices instead of
    # waiting for a lucky mutation.
    if ! "$exe" -max_total_time="$FUZZ_SECONDS" -timeout=10 -rss_limit_mb=2048 \
        -max_len=256 -use_value_profile=1 -artifact_prefix="$artifacts/" \
        "$corpus/$target" >"$log" 2>&1; then
        printf '   fuzz_%s: FAIL (log %s, artifacts in %s)\n' "$target" "$log" "$artifacts" >&2
        tail -n 20 "$log" >&2
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo "fuzz: PASS (3 targets, ${FUZZ_SECONDS} s each)"
