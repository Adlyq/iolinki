#!/usr/bin/env bash
#
# Nullability gate: the header invariants that the compiler cannot state.
#
# Every header declares "non-null by default" by opening an
# IOLINK_ASSUME_NONNULL_BEGIN region. That declaration only holds if:
#
#   1. the macros are defined there: the header includes "iolinki/types.h"
#      directly, at file scope. Without the include the macro name stays an
#      ordinary identifier and the header does not compile at all;
#   2. the region is unconditional: BEGIN/END sit at file scope or inside the
#      include guard, never inside any other `#if`. A region under `#if`
#      disappears silently in every build where the switch is off, and clang
#      only diagnoses a missing pair when the preprocessor reaches both ends;
#   3. each marker is on a line of its own, so a trailing comment cannot make
#      the pair invisible to this scan (a silent no-op is the failure mode).
#
# All three broke while the model was being rolled out (18 headers used the
# macro without the include, port.h opened the region inside
# #ifdef IOLINK_PORT_CONFIG_FILE, and the include itself sat under an `#if`), so
# they are checked here instead of being caught by review or by one particular
# build configuration.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BEGIN=IOLINK_ASSUME_NONNULL_BEGIN
END=IOLINK_ASSUME_NONNULL_END

# scan <dir>: one line per header that uses the macros:
#   file begin end begin_depth include_depth begin_line end_line loose mark_in_guard
scan() {
    find "$1" -name '*.h' -print0 | sort -z | xargs -0 -r awk -v begin="$BEGIN" -v end="$END" '
        function flush() {
            if (started && (nb + ne + loose) > 0) {
                printf "%s %d %d %d %d %d %d %d %d\n", file, nb, ne, bdepth, idepth, \
                    bline, eline, loose, guard
            }
        }
        FNR == 1 {
            flush()
            file = FILENAME
            started = 1
            depth = 0
            nb = 0
            ne = 0
            loose = 0
            idepth = -1
            bdepth = -1
            bline = 0
            eline = 0
            guard = 0
        }
        /^[[:space:]]*#include[[:space:]]*[<"]iolinki\/types\.h[>"]/ {
            if (idepth < 0) {
                idepth = depth
            }
            next
        }
        # Only the first `#ifndef` of a file is an include guard; any other
        # conditional makes the region conditional and therefore invalid.
        /^[[:space:]]*#[[:space:]]*ifndef([[:space:]]|$)/ {
            depth++
            if (depth == 1) {
                guard = 1
            }
            next
        }
        /^[[:space:]]*#[[:space:]]*(if|ifdef|elif)([[:space:]]|$)/ { depth++; next }
        /^[[:space:]]*#[[:space:]]*endif([[:space:]]|$)/ { if (depth > 0) depth--; next }
        # A marker must be the whole line: anything after it means this scan
        # would not see the pair, which would silently disable every check.
        $0 ~ "^[[:space:]]*" begin {
            if ($0 ~ "^[[:space:]]*" begin "[[:space:]]*$") {
                nb++
                if (bdepth < 0) {
                    bdepth = depth
                    bline = FNR
                }
            }
            else {
                loose++
            }
            next
        }
        $0 ~ "^[[:space:]]*" end {
            if ($0 ~ "^[[:space:]]*" end "[[:space:]]*$") {
                ne++
                if (eline == 0) {
                    eline = FNR
                }
            }
            else {
                loose++
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
    local file nb ne bdepth idepth bline eline loose guard
    listing="$(scan "$dir")"
    while read -r file nb ne bdepth idepth bline eline loose guard; do
        [ -n "${file:-}" ] || continue
        if [ "${loose:-0}" -ne 0 ]; then
            echo "FAIL: $file has a marker with trailing text; keep $BEGIN / $END on" \
                 "a line of their own" >&2
            status=1
        fi
        if [ "$nb" -eq 0 ] && [ "$ne" -eq 0 ]; then
            continue
        fi
        if [ "$idepth" -lt 0 ]; then
            echo "FAIL: $file uses $BEGIN but does not include \"iolinki/types.h\"" >&2
            status=1
        elif [ "$idepth" -gt 1 ]; then
            echo "FAIL: $file includes \"iolinki/types.h\" inside a conditional block;" \
                 "move it to file scope" >&2
            status=1
        fi
        if [ "$nb" -ne 1 ] || [ "$ne" -ne 1 ]; then
            echo "FAIL: $file needs exactly one $BEGIN and one $END (found $nb/$ne)" >&2
            status=1
        elif [ "$bdepth" -gt 1 ] || { [ "$bdepth" -eq 1 ] && [ "${guard:-0}" -ne 1 ]; }; then
            echo "FAIL: $file opens the region inside a conditional block (depth $bdepth);" \
                 "keep it at file scope or in the include guard" >&2
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
    mkdir -p "$tmp/good" "$tmp/no-types" "$tmp/include-in-if" "$tmp/in-if" \
        "$tmp/guardless-if" "$tmp/unbalanced" "$tmp/reversed" "$tmp/trailing" "$tmp/unused"

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

    cat >"$tmp/include-in-if/ok.h" <<'FIXTURE'
#ifndef OK_H
#define OK_H
#ifdef OPTIONAL_FEATURE
#include <iolinki/types.h>
#endif
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

    cat >"$tmp/guardless-if/ok.h" <<'FIXTURE'
#include "iolinki/types.h"
#if OPTIONAL_FEATURE
IOLINK_ASSUME_NONNULL_BEGIN
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

    cat >"$tmp/reversed/ok.h" <<'FIXTURE'
#ifndef OK_H
#define OK_H
#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_END
void f(int* p);
IOLINK_ASSUME_NONNULL_BEGIN
#endif
FIXTURE

    cat >"$tmp/trailing/ok.h" <<'FIXTURE'
#ifndef OK_H
#define OK_H
#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN /* region */
void f(int* p);
IOLINK_ASSUME_NONNULL_END /* region */
#endif
FIXTURE

    printf '#ifndef PLAIN_H\n#define PLAIN_H\n#endif\n' >"$tmp/unused/ok.h"

    local failures=0
    if ! check_dir "$tmp/good" 2>/dev/null; then
        echo "selftest FAIL: rejected a well-formed header" >&2
        failures=1
    fi
    local bad
    for bad in no-types include-in-if in-if guardless-if unbalanced reversed trailing; do
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
    echo "selftest: scan accepts a well-formed header and rejects all seven defects"
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
