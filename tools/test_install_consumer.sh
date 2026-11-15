#!/usr/bin/env bash
#
# Install-and-consume gate: the exported package must be usable the way an
# integrator uses it — find_package() or add_subdirectory(), a link line, and
# the same struct layout as the library.
#
# Three scenarios:
#   1. default install + find_package consumer;
#   2. add_subdirectory consumer (same iolinki::iolinki spelling);
#   3. install with non-default layout macros, consumer asserts they arrived
#      (a mismatch would change sizeof(iolink_device_ctx_t)).

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

build_install() { # <build-dir> <prefix> [extra cmake args...]
    local dir="$1" prefix="$2"
    shift 2
    cmake -S "$ROOT" -B "$dir" -DBUILD_TESTING=OFF -DIOLINK_PLATFORM=LINUX \
        -DCMAKE_INSTALL_PREFIX="$prefix" "$@" > "$dir.log" 2>&1 \
        || { cat "$dir.log" >&2; exit 1; }
    cmake --build "$dir" --parallel "$(nproc)" >> "$dir.log" 2>&1 \
        || { cat "$dir.log" >&2; exit 1; }
    cmake --install "$dir" >> "$dir.log" 2>&1 \
        || { cat "$dir.log" >&2; exit 1; }
}

check_install_tree() {
    local prefix="$1"
    if [ ! -f "$prefix/include/iolinki/device.h" ]; then
        echo "FAIL: installed tree has no iolinki/device.h" >&2
        exit 1
    fi
    if [ ! -f "$prefix/include/iolinki/internal/dll_ctx.h" ]; then
        echo "FAIL: device.h embeds the DLL layout, but internal/dll_ctx.h was not installed" >&2
        exit 1
    fi
    if [ -d "$prefix/include/src" ] || [ -d "$prefix/src" ]; then
        echo "FAIL: the install tree must not contain the source tree" >&2
        exit 1
    fi
}

# --- 1. default install + find_package -------------------------------------
build_install "$work/build" "$work/prefix"
check_install_tree "$work/prefix"

mkdir -p "$work/pkg"
cat > "$work/pkg/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.12)
project(iolinki_pkg_consumer C)
find_package(iolinki 4 CONFIG REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE iolinki::iolinki)
EOF
cat > "$work/pkg/main.c" <<'EOF'
#include <stddef.h>
#include <stdio.h>

#include "iolinki/crc.h"
#include "iolinki/device.h"

int main(void)
{
    /* A.1.6 seed vector (tests/test_crc_vectors.c pins the same value). */
    const unsigned char cks = iolink_checksum6_from_acc(0x52U);

    printf("device ctx: %zu bytes, cks: 0x%02X\n", sizeof(iolink_device_ctx_t), cks);
    return (sizeof(iolink_device_ctx_t) > 0U) && (cks == 0x2DU) ? 0 : 1;
}
EOF

cmake -S "$work/pkg" -B "$work/pkg/build" \
    -DCMAKE_PREFIX_PATH="$work/prefix" > "$work/pkg.log" 2>&1 \
    || { cat "$work/pkg.log" >&2; exit 1; }
cmake --build "$work/pkg/build" >> "$work/pkg.log" 2>&1 \
    || { cat "$work/pkg.log" >&2; exit 1; }
"$work/pkg/build/consumer" > "$work/pkg.run" 2>&1 \
    || { cat "$work/pkg.run" >&2; exit 1; }

# --- 2. add_subdirectory ---------------------------------------------------
mkdir -p "$work/sub"
cat > "$work/sub/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.12)
project(iolinki_sub_consumer C)
add_subdirectory("${IOLINKI_SRC}" iolinki_build EXCLUDE_FROM_ALL)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE iolinki::iolinki)
EOF
cp "$work/pkg/main.c" "$work/sub/main.c"
cmake -S "$work/sub" -B "$work/sub/build" -DIOLINKI_SRC="$ROOT" -DBUILD_TESTING=OFF \
    > "$work/sub.log" 2>&1 || { cat "$work/sub.log" >&2; exit 1; }
cmake --build "$work/sub/build" >> "$work/sub.log" 2>&1 \
    || { cat "$work/sub.log" >&2; exit 1; }
"$work/sub/build/consumer" > "$work/sub.run" 2>&1 \
    || { cat "$work/sub.run" >&2; exit 1; }

# --- 3. non-default layout must be exported --------------------------------
build_install "$work/build-layout" "$work/prefix-layout" \
    -DIOLINK_STATS_MODE=IOLINK_STATS_SPLIT \
    -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME \
    -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME \
    -DIOLINK_PHY_FEATURE_TX_BUSY=1

mkdir -p "$work/layout"
cat > "$work/layout/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.12)
project(iolinki_layout_consumer C)
find_package(iolinki 4 CONFIG REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE iolinki::iolinki)
EOF
cat > "$work/layout/main.c" <<'EOF'
#include "iolinki/device.h"

/* The exported target must carry the library's layout macros, otherwise the
 * consumer allocates a context of the wrong size. */
#ifndef IOLINK_STATS_MODE
#error "IOLINK_STATS_MODE was not exported"
#endif
#if IOLINK_STATS_MODE != IOLINK_STATS_SPLIT
#error "IOLINK_STATS_MODE did not reach the consumer"
#endif
#ifndef IOLINK_ISR_RX_MODE
#error "IOLINK_ISR_RX_MODE was not exported"
#endif
#if IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_FRAME
#error "IOLINK_ISR_RX_MODE did not reach the consumer"
#endif
#if IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_FRAME
#error "IOLINK_ISR_TX_MODE did not reach the consumer"
#endif
#if IOLINK_PHY_FEATURE_TX_BUSY != 1
#error "IOLINK_PHY_FEATURE_TX_BUSY did not reach the consumer"
#endif

int main(void)
{
    return sizeof(iolink_device_ctx_t) > 0U ? 0 : 1;
}
EOF
cmake -S "$work/layout" -B "$work/layout/build" \
    -DCMAKE_PREFIX_PATH="$work/prefix-layout" > "$work/layout.log" 2>&1 \
    || { cat "$work/layout.log" >&2; exit 1; }
cmake --build "$work/layout/build" >> "$work/layout.log" 2>&1 \
    || { cat "$work/layout.log" >&2; exit 1; }
"$work/layout/build/consumer" > "$work/layout.run" 2>&1 \
    || { cat "$work/layout.run" >&2; exit 1; }

echo "install/consume gate: PASS ($(cat "$work/pkg.run"); add_subdirectory ok; split/frame layout ok)"
