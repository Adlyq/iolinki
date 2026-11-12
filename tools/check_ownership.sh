#!/usr/bin/env bash
#
# Ownership gate (roadmap P2): the main-context translation unit must not be
# able to reference data-ISR-exclusive fields.
#
# Two independent properties are checked:
#   1. src/dll.c's full preprocessor dependency set must not pull in
#      dll_isr.h, the packed container (dll_ctx.h) or device.h;
#   2. a main-only translation unit cannot dereference the opaque ISR handle,
#      while the packed container can.
#
# This turns the ownership boundary into a CI gate instead of a review habit.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

cc_bin="${CC:-cc}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

deps="$tmp/dll.d"
if ! "$cc_bin" -std=c99 -Iinclude -Isrc -M src/dll.c >"$deps" 2>"$tmp/deps.log"; then
    echo "FAIL: cannot preprocess src/dll.c (toolchain or headers broken)" >&2
    cat "$tmp/deps.log" >&2
    exit 1
fi

if grep -Eq 'dll_isr\.h|dll_ctx\.h|device\.h' "$deps"; then
    echo "FAIL: src/dll.c must not transitively include dll_isr.h/dll_ctx.h/device.h" >&2
    grep -E 'dll_isr\.h|dll_ctx\.h|device\.h' "$deps" >&2
    exit 1
fi

cat >"$tmp/probe_ctx.c" <<'PROBE'
#include "iolinki/dll_ctx.h"
void probe(iolink_dll_ctx_t* ctx)
{
    ctx->isr.isr_tx_flags = 1U;
}
PROBE

if ! "$cc_bin" -std=c99 -Iinclude -c "$tmp/probe_ctx.c" -o "$tmp/probe_ctx.o" \
    >"$tmp/probe_ctx.log" 2>&1; then
    echo "FAIL: packed container cannot access ISR fields (toolchain or headers broken)" >&2
    cat "$tmp/probe_ctx.log" >&2
    exit 1
fi

cat >"$tmp/probe_main.c" <<'PROBE'
#include "iolinki/dll_main.h"
void probe(iolink_dll_main_t* ctx)
{
    ctx->shared.isr->isr_tx_flags = 1U;
}
PROBE

if "$cc_bin" -std=c99 -Iinclude -c "$tmp/probe_main.c" -o "$tmp/probe_main.o" \
    >"$tmp/probe_main.log" 2>&1; then
    echo "FAIL: main context can dereference the ISR handle (isolation lost)" >&2
    exit 1
fi

if ! grep -Eq 'isr_tx_flags|iolink_dll_isr|incomplete|不完全' "$tmp/probe_main.log"; then
    echo "FAIL: negative probe failed for an unexpected reason" >&2
    cat "$tmp/probe_main.log" >&2
    exit 1
fi

echo "ownership check: PASS (dll.c cannot reference ISR-exclusive fields)"
