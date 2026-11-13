#!/usr/bin/env bash
#
# Link-time profile gate: the compile-time layout switches must
# make a mismatched application fail to link, with the direction visible in the
# missing symbol name (iolink_profile_events_off, ...).
#
# A gate that is never proven to fire is decoration, so this script runs a
# positive probe (same profile links) and one negative probe per switch (the
# opposite profile must fail to link). It is called by tools/build_matrix.sh.
#
# Usage:
#   ./tools/check_profile_abi.sh
#
# Environment:
#   CC   C compiler (default: cc)

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CC_BIN="${CC:-cc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CFLAGS_BASE=(-std=c99 -I"$ROOT/include" -Wall -Wextra -Werror -Wpedantic)

cat >"$TMP/app.c" <<'PROBE'
#include "iolinki/profile.h"

IOLINK_PROFILE_ANCHOR(iolink_app_profile);

int main(void)
{
    return (iolink_app_profile[0] == NULL) ? 1 : 0;
}
PROBE

# Library side: only the profile translation unit is needed to resolve the
# anchor symbols, and it is built with the default (full) profile.
# shellcheck disable=SC2086 # word splitting of the flag list is intentional
if ! "$CC_BIN" "${CFLAGS_BASE[@]}" -c "$ROOT/src/profile.c" -o "$TMP/profile.o" \
    >"$TMP/profile.log" 2>&1; then
    echo "profile gate: FAIL (cannot compile src/profile.c)" >&2
    cat "$TMP/profile.log" >&2
    exit 1
fi

if ! "$CC_BIN" "${CFLAGS_BASE[@]}" -c "$TMP/app.c" -o "$TMP/app.o" >"$TMP/app.log" 2>&1; then
    echo "profile gate: FAIL (cannot compile the anchor probe)" >&2
    cat "$TMP/app.log" >&2
    exit 1
fi

# Positive probe: the same profile must link.
if ! "$CC_BIN" "$TMP/app.o" "$TMP/profile.o" -o "$TMP/app" >"$TMP/link.log" 2>&1; then
    echo "profile gate: FAIL (matching profile did not link)" >&2
    cat "$TMP/link.log" >&2
    exit 1
fi
echo "profile gate         matching profile links"

fail=0
# name|extra cflags|symbol expected to be undefined
NEGATIVE_CASES=(
    "events_off|-DIOLINK_FEATURE_EVENTS=0|iolink_profile_events_off"
    "no_data_storage|-DIOLINK_FEATURE_DATA_STORAGE=0|iolink_profile_data_storage_off"
    "isr_rx_frame|-DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME|iolink_profile_isr_rx_frame"
    "isr_tx_frame|-DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME|iolink_profile_isr_tx_frame"
    "stats_split|-DIOLINK_STATS_MODE=IOLINK_STATS_SPLIT|iolink_profile_stats_split"
    "concurrency_single|-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_SINGLE|iolink_profile_concurrency_single"
    "tx_busy|-DIOLINK_PHY_FEATURE_TX_BUSY=1|iolink_profile_tx_busy_on"
    "workspace_external|-DIOLINK_WORKSPACE_EMBEDDED=0|iolink_profile_workspace_embedded_off"
    "pd_in_buffers_3|-DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME|iolink_profile_pd_in_buffers_3"
)

for entry in "${NEGATIVE_CASES[@]}"; do
    IFS='|' read -r name cflags symbol <<<"$entry"
    # shellcheck disable=SC2086 # intentional word splitting of the -D list
    # 反向探针只引用本用例的档位符号：链接器只报告第一个未定义符号，
    # 用完整锚点会被其它缺失符号（例如 FRAME 同时改变 ISR_TX）掩盖。
    cat >"$TMP/one.c" <<PROBE
extern const unsigned char $symbol;
int main(void)
{
    const void* const volatile anchor = (const void*) &$symbol;
    return (anchor == 0) ? 1 : 0;
}
PROBE
    if ! "$CC_BIN" "${CFLAGS_BASE[@]}" $cflags -c "$TMP/one.c" -o "$TMP/app_$name.o" \
        >"$TMP/app_$name.log" 2>&1; then
        echo "profile gate: FAIL ($name: probe did not compile)" >&2
        cat "$TMP/app_$name.log" >&2
        fail=1
        continue
    fi
    if "$CC_BIN" "$TMP/app_$name.o" "$TMP/profile.o" -o "$TMP/app_$name" \
        >"$TMP/link_$name.log" 2>&1; then
        echo "profile gate: FAIL ($name: mismatched profile linked silently)" >&2
        fail=1
        continue
    fi
    if ! grep -q "$symbol" "$TMP/link_$name.log"; then
        echo "profile gate: FAIL ($name: link failed without naming $symbol)" >&2
        sed -n '1,5p' "$TMP/link_$name.log" | sed 's/^/    /' >&2
        fail=1
        continue
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "profile gate: FAIL" >&2
    exit 1
fi
echo "profile gate         PASS (${#NEGATIVE_CASES[@]} mismatched profiles rejected)"
