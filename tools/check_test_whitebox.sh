#!/usr/bin/env bash
#
# Reject direct access to a device's embedded DLL context from tests.
#
# Behaviour and conformance tests must use the public API
# (`iolink_device_get_state`, `iolink_device_get_dll_stats`,
# `iolink_device_get_events_ctx`, ...) or the accessors in
# `tests/dll_access.h`. Only that header may name the DLL layout, so a change to
# `iolink_device_ctx_t` touches one file instead of every test.
#
# Tests whose subject is the DLL internals declare `iolink_dll_ctx_t` or
# `iolink_dll_main_t` themselves and keep using `ctx.main...`; that form is not
# matched here and stays allowed.
#
# Run `--selftest` to exercise the matchers and the source scanner against
# known-good and known-bad samples.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ACCESSOR="tests/dll_access.h"

# `&ctx->dll`, `ctx->dll.main`, `offsetof(iolink_device_ctx_t, dll)` and a
# macro wrapping the member are the shapes that couple a test to the layout.
# A bare field named `dll` on an unrelated struct would also match; no test
# does that, and rewording is cheaper than a parser here.
ACCESS_RE='(\.|->)[[:space:]]*dll([^[:alnum:]_]|$)|offsetof\([^,]*,[[:space:]]*dll[[:space:]]*\)'

# Only a direct include is rejected; naming the header in prose is fine.
INCLUDE_RE='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]iolinki/dll_ctx\.h[>"]'

# Scan C source the way the compiler sees it: comments are not code, and (for
# the access rule) string contents are not identifiers either.
#
#   $1 = 1  keep string contents (the include path lives inside quotes)
#   $1 = 0  blank string and character literals
#
# Line structure is preserved so reported line numbers stay correct.
scan_source() {
    awk -v keep_strings="${1:-1}" '
        BEGIN { in_block = 0; in_string = 0; in_char = 0 }
        {
            out = ""
            n = length($0)
            i = 1
            while (i <= n) {
                c = substr($0, i, 1)
                nxt = substr($0, i + 1, 1)
                if (in_block) {
                    if (c == "*" && nxt == "/") {
                        in_block = 0
                        i += 2
                    } else {
                        i++
                    }
                    continue
                }
                if (in_string) {
                    out = out ((keep_strings == 1) ? c : "")
                    if (c == "\\") {
                        out = out ((keep_strings == 1) ? nxt : "")
                        i += 2
                    } else {
                        if (c == "\"") {
                            in_string = 0
                        }
                        i++
                    }
                    continue
                }
                if (in_char) {
                    out = out ((keep_strings == 1) ? c : "")
                    if (c == "\\") {
                        out = out ((keep_strings == 1) ? nxt : "")
                        i += 2
                    } else {
                        if (c == "\047") {
                            in_char = 0
                        }
                        i++
                    }
                    continue
                }
                if (c == "\"") {
                    in_string = 1
                    out = out c
                    i++
                    continue
                }
                if (c == "\047") {
                    in_char = 1
                    out = out c
                    i++
                    continue
                }
                if (c == "/" && nxt == "*") {
                    in_block = 1
                    i += 2
                    continue
                }
                if (c == "/" && nxt == "/") {
                    i = n + 1
                    continue
                }
                out = out c
                i++
            }
            print out
        }'
}

matches_access() {
    local text="$1"
    printf '%s\n' "$text" | grep -qE "$ACCESS_RE"
}

matches_include() {
    local text="$1"
    printf '%s\n' "$text" | grep -qE "$INCLUDE_RE"
}

selftest() {
    local rc=0
    local sample scanned

    local bad_access=(
        'ctx.dll.main.pd_valid = true;'
        'dev.ctx->dll.main.shared.state'
        'iolink_dll_ctx_t* c = &ctx->dll;'
        'iolink_dll_ctx_t* c = &device->dll;'
        'size_t off = offsetof(iolink_device_ctx_t, dll);'
        '#define DLLP(p) (&(p)->dll)'
        'ctx->dll; /* trailing comment */'
        'const char* u = "http://example.test"; (void) ctx->dll;'
        'const char* open = "/*"; (void) ctx->dll;'
        'const char* quote = "\""; (void) ctx->dll;'
    )
    local good_access=(
        'test_dll(&ctx)->pd_valid = true;'
        'iolink_dll_ctx_t dll;'
        'dll.main.shared.state = 0;'
        'iolink_device_get_dll_stats(&ctx, &stats);'
        'iolink_device_get_pd_in_len(&ctx);'
        'offsetof(iolink_device_ctx_t, dll_ctx)'
        '/* ctx->dll is not allowed here */'
        'int x = 0; // ctx->dll would be wrong'
        '/* multi
           ctx->dll
           line comment */'
        'const char* s = "frame.dll.stats";'
        'const char* u = "http://example.test";'
        'const char* open = "/*";'
        'char c = 0x27;'
    )
    local bad_include=(
        '#include "iolinki/dll_ctx.h"'
        '   #include <iolinki/dll_ctx.h>'
    )
    local good_include=(
        '// see include/iolinki/dll_ctx.h'
        '/* layout lives in iolinki/dll_ctx.h */'
        '#include "dll_access.h"'
        '// #include "iolinki/dll_ctx.h"'
        '/* #include "iolinki/dll_ctx.h" */'
        'const char* p = "iolinki/dll_ctx.h";'
    )

    for sample in "${bad_access[@]}"; do
        scanned="$(scan_source 0 <<< "$sample")"
        if ! matches_access "$scanned"; then
            echo "SELFTEST FAIL: access rule misses: $sample" >&2
            rc=1
        fi
    done
    for sample in "${good_access[@]}"; do
        scanned="$(scan_source 0 <<< "$sample")"
        if matches_access "$scanned"; then
            echo "SELFTEST FAIL: access rule false positive: $sample" >&2
            rc=1
        fi
    done
    for sample in "${bad_include[@]}"; do
        scanned="$(scan_source 1 <<< "$sample")"
        if ! matches_include "$scanned"; then
            echo "SELFTEST FAIL: include rule misses: $sample" >&2
            rc=1
        fi
    done
    for sample in "${good_include[@]}"; do
        scanned="$(scan_source 1 <<< "$sample")"
        if matches_include "$scanned"; then
            echo "SELFTEST FAIL: include rule false positive: $sample" >&2
            rc=1
        fi
    done

    [ "$rc" -eq 0 ] && echo "white-box gate: selftest OK"
    return "$rc"
}

if [ "${1:-}" = "--selftest" ]; then
    selftest
    exit $?
fi

if ! command -v grep > /dev/null; then
    echo "FAIL: grep is required" >&2
    exit 1
fi
if ! command -v awk > /dev/null; then
    echo "FAIL: awk is required" >&2
    exit 1
fi
if [ ! -d tests ]; then
    echo "FAIL: tests/ not found (run from the repository root)" >&2
    exit 1
fi

failed=0

check_includes() {
    local file scanned line
    while IFS= read -r file; do
        [ -z "$file" ] && continue
        [ "$file" = "$ACCESSOR" ] && continue
        # shellcheck disable=SC2218  # defined above; shellcheck 0.11 mis-tracks it
        scanned="$(scan_source 1 < "$file")"
        while IFS=: read -r line _; do
            [ -z "$line" ] && continue
            echo "FAIL: $file:$line includes iolinki/dll_ctx.h; use tests/dll_access.h" >&2
            failed=1
        done <<< "$(printf '%s\n' "$scanned" | grep -nE "$INCLUDE_RE" || true)"
    done <<< "$(grep -rlE "$INCLUDE_RE" tests/ 2>/dev/null || true)"
    return 0
}

check_access() {
    local file scanned line
    while IFS= read -r file; do
        [ -z "$file" ] && continue
        [ "$file" = "$ACCESSOR" ] && continue
        # shellcheck disable=SC2218  # defined above; shellcheck 0.11 mis-tracks it
        scanned="$(scan_source 0 < "$file")"
        while IFS=: read -r line _; do
            [ -z "$line" ] && continue
            echo "FAIL: $file:$line accesses the device's DLL context directly" >&2
            failed=1
        done <<< "$(printf '%s\n' "$scanned" | grep -nE "$ACCESS_RE" || true)"
    done <<< "$(grep -rlE "$ACCESS_RE" tests/ 2>/dev/null || true)"
    return 0
}

check_includes
check_access

if [ "$failed" -eq 0 ]; then
    echo "white-box access: OK (DLL layout reached only through tests/dll_access.h)"
fi
exit "$failed"
