#!/usr/bin/env bash
#
# Ownership gate: every main-context translation unit must not be
# able to reference data-ISR-exclusive fields, and every data-ISR
# translation unit must not be able to name main-context state.
#
# Three independent properties are checked:
#   1. the full preprocessor dependency set of each main-context TU
#      (src/dll*.c except dll_isr*.c) must not pull in the ISR state
#      (internal/dll_isr.h), the packed container (internal/dll_ctx.h)
#      or device.h;
#   2. the reverse: a data-ISR TU (src/dll_isr*.c) must not pull in
#      internal/dll_main.h (its interface is iolink_dll_shared_t);
#   3. a main-only translation unit cannot dereference the opaque ISR handle,
#      while the packed container can.
#
# This turns the ownership boundary into a CI gate instead of a review habit.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

cc_bin="${CC:-cc}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Derive the list instead of hard-coding it: a new main-context file must not
# be able to join the tree without being checked.
MAIN_TUS=()
for tu in src/dll*.c; do
    case "$tu" in
        src/dll_isr*) ;;
        *) MAIN_TUS+=("$tu") ;;
    esac
done
if [ "${#MAIN_TUS[@]}" -eq 0 ]; then
    echo "FAIL: no main-context translation units found in src/" >&2
    exit 1
fi

for tu in "${MAIN_TUS[@]}"; do
    deps="$tmp/$(basename "$tu" .c).d"
    if ! "$cc_bin" -std=c99 -Iinclude -Isrc -M "$tu" >"$deps" 2>"$tmp/deps.log"; then
        echo "FAIL: cannot preprocess $tu (toolchain or headers broken)" >&2
        cat "$tmp/deps.log" >&2
        exit 1
    fi

    if grep -Eq '/iolinki/(internal/(dll_isr|dll_ctx)\.h|device\.h)' "$deps"; then
        echo "FAIL: $tu must not transitively include internal/dll_isr.h, internal/dll_ctx.h or device.h" >&2
        grep -E '/iolinki/(internal/(dll_isr|dll_ctx)\.h|device\.h)' "$deps" >&2
        exit 1
    fi
done

# The reverse direction: a data-ISR translation unit only sees the shared
# slice, so it must not be able to name main-context state at all.
for tu in src/dll_isr*.c; do
    deps="$tmp/$(basename "$tu" .c).d"
    if ! "$cc_bin" -std=c99 -Iinclude -Isrc -M "$tu" >"$deps" 2>"$tmp/deps.log"; then
        echo "FAIL: cannot preprocess $tu (toolchain or headers broken)" >&2
        cat "$tmp/deps.log" >&2
        exit 1
    fi
    if grep -Eq '/iolinki/internal/dll_main\.h' "$deps"; then
        echo "FAIL: $tu must not transitively include internal/dll_main.h" >&2
        grep -E '/iolinki/internal/dll_main\.h' "$deps" >&2
        exit 1
    fi
done

cat >"$tmp/probe_ctx.c" <<'PROBE'
#include "iolinki/internal/dll_ctx.h"
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
#include "iolinki/internal/dll_main.h"
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

echo "ownership check: PASS (main-context TUs cannot reference ISR-exclusive fields)"
