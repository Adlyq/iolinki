#!/usr/bin/env bash
#
# Nullability gate: the header invariants that the compiler cannot state.
#
# Every header declares "non-null by default" by opening an
# IOLINK_ASSUME_NONNULL_BEGIN region. That declaration only holds if:
#
#   1. the macros are defined there: the header includes "iolinki/types.h"
#      directly. Without it the macro name stays an ordinary identifier and
#      the header does not compile at all;
#   2. the region is unconditional: BEGIN/END sit at include-guard level, not
#      inside an #if block. A region under #if disappears silently in every
#      build where the switch is off, and clang only diagnoses a missing pair
#      when the preprocessor reaches both ends.
#
# Both were real defects when the model was rolled out (18 headers used the
# macro without the include, and port.h opened the region inside
# #ifdef IOLINK_PORT_CONFIG_FILE), so they are checked here instead of being
# caught by review or by one particular build configuration.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BEGIN=IOLINK_ASSUME_NONNULL_BEGIN
END=IOLINK_ASSUME_NONNULL_END

# scan <dir>: one line per header that uses the macros:
#   file begin_count end_count begin_depth has_types_include begin_line end_line
scan() {
    find "$1" -name '*.h' -print0 | sort -z | xargs -0 -r awk -v begin="$BEGIN" -v end="$END" '
        function flush() {
            if (started && (nb + ne) > 0) {
                printf "%s %d %d %d %d %d %d\n", file, nb, ne, bdepth, has_types, bline, eline
            }
        }
        FNR == 1 {
            flush()
            file = FILENAME
            started = 1
            depth = 0
            nb = 0
            ne = 0
            has_types = 0
            bdepth = -1
            bline = 0
            eline = 0
        }
        /#include[[:space:]]+"iolinki\/types\.h"/ { has_types = 1 }
        /^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef)([[:space:]]|$)/ { depth++; next }
        /^[[:space:]]*#[[:space:]]*endif([[:space:]]|$)/ { if (depth > 0) depth--; next }
        # The markers must be whole lines: a mention inside a comment is not a
        # region.
        $0 ~ "^[[:space:]]*" begin "[[:space:]]*$" {
            nb++
            if (bdepth < 0) {
                bdepth = depth
                bline = FNR
            }
            next
        }
        $0 ~ "^[[:space:]]*" end "[[:space:]]*$" {
            ne++
            if (eline == 0) {
                eline = FNR
            }
            next
        }
        END { flush() }
    '
}

# check_dir <dir>: fail on any violation, otherwise stay quiet.
check_dir() {
    local dir="$1"
    local listing
    local status=0
    local file nb ne bdepth has_types bline eline
    listing="$(scan "$dir")"
    while read -r file nb ne bdepth has_types bline eline; do
        [ -n "$file" ] || continue
        if [ "$has_types" -ne 1 ]; then
            echo "FAIL: $file uses $BEGIN but does not include \"iolinki/types.h\"" >&2
            status=1
        fi
        if [ "$nb" -ne 1 ] || [ "$ne" -ne 1 ]; then
            echo "FAIL: $file needs exactly one $BEGIN and one $END (found $nb/$ne)" >&2
            status=1
        elif [ "$bdepth" -gt 1 ]; then
            echo "FAIL: $file opens the region inside a conditional block (depth $bdepth);" \
                 "keep it at include-guard level" >&2
            status=1
        elif [ "$bline" -gt "$eline" ]; then
            echo "FAIL: $file closes the region before opening it" >&2
            status=1
        fi
    done <<<"$listing"
    return "$status"
}

selftest() {
    # Not `local`: the EXIT trap runs after this function returns.
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/good" "$tmp/no-types" "$tmp/in-if" "$tmp/unbalanced" "$tmp/unused"

    cat >"$tmp/good/ok.h" <<'FIXTURE'
#ifndef OK_H
#define OK_H
#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN
void f(int* p);
IOLINK_ASSUME_NONNULL_END
#endif
FIXTURE

    cat >"$tmp/no-types/ok.h" <<'FIXTURE'
#ifndef OK_H
#define OK_H
IOLINK_ASSUME_NONNULL_BEGIN
void f(int* p);
IOLINK_ASSUME_NONNULL_END
#endif
FIXTURE

    cat >"$tmp/in-if/ok.h" <<'FIXTURE'
#ifndef OK_H
#define OK_H
#include "iolinki/types.h"
#ifdef OPTIONAL_FEATURE
IOLINK_ASSUME_NONNULL_BEGIN
#endif
void f(int* p);
IOLINK_ASSUME_NONNULL_END
#endif
FIXTURE

    cat >"$tmp/unbalanced/ok.h" <<'FIXTURE'
#ifndef OK_H
#define OK_H
#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN
void f(int* p);
#endif
FIXTURE

    printf '#ifndef PLAIN_H\n#define PLAIN_H\n#endif\n' >"$tmp/unused/ok.h"

    local failures=0
    if ! check_dir "$tmp/good" 2>/dev/null; then
        echo "selftest FAIL: accepted a well-formed header" >&2
        failures=1
    fi
    local bad
    for bad in no-types in-if unbalanced; do
        if check_dir "$tmp/$bad" 2>/dev/null; then
            echo "selftest FAIL: accepted the $bad fixture" >&2
            failures=1
        fi
    done
    if ! check_dir "$tmp/unused" 2>/dev/null; then
        echo "selftest FAIL: rejected a header that does not use the macros" >&2
        failures=1
    fi
    if [ "$failures" -ne 0 ]; then
        return 1
    fi
    echo "selftest: scan detects all four fixtures"
}

main() {
    cd "$root"
    if [ "${1:-}" = "--selftest" ]; then
        selftest
        return
    fi
    check_dir include/iolinki
    echo "nullability: header regions and macro includes are consistent"
}

main "$@"
