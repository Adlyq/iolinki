#!/usr/bin/env bash
# Ownership gate: the main-context translation unit must not be able to
# reference data-ISR-exclusive fields. dll.c only includes dll_main.h; a
# reference to an ISR field is a compile error by construction. This script
# makes that property enforceable in CI instead of relying on review.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if grep -q 'dll_isr\.h' "$root/src/dll.c"; then
    echo "FAIL: src/dll.c must not include dll_isr.h" >&2
    exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/probe_main.c" <<'PROBE'
#include "iolinki/dll_main.h"
void probe(iolink_dll_main_t* ctx)
{
    ctx->isr_rx_cnt = 0U;
}
PROBE

cat > "$tmp/probe_ctx.c" <<'PROBE'
#include "iolinki/dll_ctx.h"
void probe(iolink_dll_ctx_t* ctx)
{
    ctx->isr.isr_rx_cnt = 0U;
}
PROBE

cc_bin="${CC:-cc}"

if "$cc_bin" -std=c99 -I"$root/include" -c "$tmp/probe_main.c" -o "$tmp/probe_main.o" \
    >/dev/null 2>&1; then
    echo "FAIL: main context can reference ISR-exclusive fields (isr_rx_cnt)" >&2
    exit 1
fi

if ! "$cc_bin" -std=c99 -I"$root/include" -c "$tmp/probe_ctx.c" -o "$tmp/probe_ctx.o" \
    >/dev/null 2>&1; then
    echo "FAIL: packed container cannot access ISR fields (isr_rx_cnt)" >&2
    exit 1
fi

echo "ownership check: PASS (dll.c cannot reference ISR fields)"
