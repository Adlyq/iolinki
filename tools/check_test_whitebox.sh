#!/usr/bin/env bash
#
# Reject direct access to the device's embedded DLL context from tests.
#
# Behaviour and conformance tests must use the public API
# (`iolink_device_get_state`, `iolink_device_get_dll_stats`,
# `iolink_device_get_events_ctx`, ...) or the accessors in
# `tests/dll_access.h`. Only that header (and the shared harness) may name the
# DLL layout, so a change to `iolink_device_ctx_t` touches one file instead of
# every test.
#
# Tests whose subject *is* the DLL internals declare `iolink_dll_ctx_t`
# directly and use `ctx.main...`; that is not matched here and stays allowed.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

failed=0

# 1) The layout header is included in exactly one test header (plus the harness).
while IFS= read -r file; do
    case "$file" in
        tests/dll_access.h | tests/test_helpers.c) ;;
        *)
            echo "FAIL: $file includes iolinki/dll_ctx.h; use tests/dll_access.h" >&2
            failed=1
            ;;
    esac
done < <(grep -rl 'iolinki/dll_ctx.h' tests/ 2>/dev/null || true)

# 2) No test may reach through the device context (`ctx->dll...`).
while IFS=: read -r file line _; do
    case "$file" in
        tests/dll_access.h | tests/test_helpers.c) ;;
        *)
            echo "FAIL: $file:$line accesses the device's DLL context directly" >&2
            failed=1
            ;;
    esac
done < <(grep -rnE '(\.|->)[[:space:]]*dll[[:space:]]*\.' tests/ 2>/dev/null || true)

if [ "$failed" -eq 0 ]; then
    echo "white-box access: OK (DLL layout reached only through tests/dll_access.h)"
fi
exit "$failed"
