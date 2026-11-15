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
# Run `--selftest` to exercise the matchers and the comment stripper against
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

# Prose such as "do not name ctx->dll here" is not a violation, so strip C
# comments before matching. Line numbers are preserved.
strip_comments() {
    awk '
        BEGIN { inblock = 0 }
        {
            out = ""
            n = length($0)
            i = 1
            while (i <= n) {
                if (inblock) {
                    j = index(substr($0, i), "*/")
                    if (j == 0) {
                        i = n + 1
                        continue
                    }
                    inblock = 0
                    i = i + j + 1
                    continue
                }
                rest = substr($0, i)
                open_at = index(rest, "/*")
                line_at = index(rest, "//")
                if ((line_at > 0) && ((open_at == 0) || (line_at < open_at))) {
                    out = out substr(rest, 1, line_at - 1)
                    i = n + 1
                } else if (open_at == 0) {
                    out = out rest
                    i = n + 1
                } else {
                    out = out substr(rest, 1, open_at - 1)
                    inblock = 1
                    i = i + open_at + 1
                }
            }
            print out
        }'
}

selftest() {
    local rc=0
    local sample

    local bad_access=(
        'ctx.dll.main.pd_valid = true;'
        'dev.ctx->dll.main.shared.state'
        'iolink_dll_ctx_t* c = &ctx->dll;'
        'iolink_dll_ctx_t* c = &device->dll;'
        'size_t off = offsetof(iolink_device_ctx_t, dll);'
        '#define DLLP(p) (&(p)->dll)'
        'ctx->dll; /* trailing comment */'
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
    )
    local bad_include=(
        '#include "iolinki/dll_ctx.h"'
        '   #include <iolinki/dll_ctx.h>'
    )
    local good_include=(
        '// see include/iolinki/dll_ctx.h'
        '/* layout lives in iolinki/dll_ctx.h */'
        '#include "dll_access.h"'
    )

    for sample in "${bad_access[@]}"; do
        local stripped
        stripped="$(strip_comments <<< "$sample")"
        if ! printf '%s\n' "$stripped" | grep -qE "$ACCESS_RE"; then
            echo "SELFTEST FAIL: access rule misses: $sample" >&2
            rc=1
        fi
    done
    for sample in "${good_access[@]}"; do
        local stripped
        stripped="$(strip_comments <<< "$sample")"
        if printf '%s\n' "$stripped" | grep -qE "$ACCESS_RE"; then
            echo "SELFTEST FAIL: access rule false positive: $sample" >&2
            rc=1
        fi
    done
    for sample in "${bad_include[@]}"; do
        local stripped
        stripped="$(strip_comments <<< "$sample")"
        if ! printf '%s\n' "$stripped" | grep -qE "$INCLUDE_RE"; then
            echo "SELFTEST FAIL: include rule misses: $sample" >&2
            rc=1
        fi
    done
    for sample in "${good_include[@]}"; do
        local stripped
        stripped="$(strip_comments <<< "$sample")"
        if printf '%s\n' "$stripped" | grep -qE "$INCLUDE_RE"; then
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

while IFS= read -r file; do
    [ -z "$file" ] && continue
    [ "$file" = "$ACCESSOR" ] && continue
    while IFS=: read -r line _; do
        [ -z "$line" ] && continue
        echo "FAIL: $file:$line includes iolinki/dll_ctx.h; use tests/dll_access.h" >&2
        failed=1
    done <<< "$(grep -nE "$INCLUDE_RE" "$file" 2>/dev/null || true)"
done <<< "$(grep -rlE "$INCLUDE_RE" tests/ 2>/dev/null || true)"

while IFS= read -r file; do
    [ -z "$file" ] && continue
    [ "$file" = "$ACCESSOR" ] && continue
    while IFS=: read -r line _; do
        [ -z "$line" ] && continue
        echo "FAIL: $file:$line accesses the device's DLL context directly" >&2
        failed=1
    done <<< "$(stripped="$(strip_comments < "$file")"; printf '%s\\n' "$stripped" | grep -nE "$ACCESS_RE" || true)"
done <<< "$(grep -rlE "$ACCESS_RE" tests/ 2>/dev/null || true)"

if [ "$failed" -eq 0 ]; then
    echo "white-box access: OK (DLL layout reached only through tests/dll_access.h)"
fi
exit "$failed"
