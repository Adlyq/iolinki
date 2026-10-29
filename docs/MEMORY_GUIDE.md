# iolinki Memory Usage Guide

This guide helps embedded developers estimate the RAM and Flash (ROM) requirements for the `iolinki` stack and provides tips for optimization.

## 1. RAM Usage (Static)

The stack avoids dynamic memory allocation (`malloc`). All state is held in context structures.

### Base Requirements

The core memory footprint is determined by `iolink_device_ctx_t`, which embeds
`iolink_dll_ctx_t` (including ISDU, Events and Data Storage).

| Component | Size (approx) | Description |
| :--- | :--- | :--- |
| **DLL context** | ~1.3 KB | Core state, timers, counters, ISR staging and the ISDU snapshot |
| **ISDU context** | ~1.1 KB | Four payload/transport buffers (see below) |
| **PHY API** | 20-30 bytes | Function pointers (usually const/flash, but pointer storage in RAM) |
| **Buffers** | *Configurable* | Dependent on `include/iolinki/config.h` |

### Configurable Memory (via `include/iolinki/config.h`)

You can tune these values to fit your MCU.

| Macro | Default | Bytes Used | Description |
| :--- | :--- | :--- | :--- |
| `IOLINK_ISDU_BUFFER_SIZE` | 256 | **1289** bytes | `buffer`(256) + `response_buf`(256) + `req_buf`(262) + `resp_buf`(259), plus the ISR snapshot `isr_isdu_resp_buf`(256) |
| `IOLINK_EVENT_QUEUE_SIZE` | 4 | scales with queue | `iolink_event_t` queue plus `IOLINK_EVENT_PENDING_MAX` (8) lifecycle records, a 19-byte Table 58 memory and a 6-entry frozen snapshot |
| `IOLINK_PD_IN_MAX_SIZE` | 32 | 195 bytes | Three reply buffers of `od_max + pd_in + 1` (65 each) |
| `IOLINK_PD_OUT_MAX_SIZE` | 32 | 32 bytes | Output Process Data buffer (also sizes the ISR RX buffer) |
| `IOLINK_DS_IMAGE_MAX` | 256 | 256 bytes | Data Storage image |

### Total RAM

Buffer arrays alone exceed **2 KB** at the default settings (ISDU 1289 + ISR
staging/PD ~520 + DS image 256 + events), before scalar state and alignment. Do
not trust an arithmetic estimate:

```c
#include <stdio.h>
#include "iolinki/device.h"
int main(void) {
    printf("device ctx: %zu\n", sizeof(iolink_device_ctx_t));
    printf("dll ctx:    %zu\n", sizeof(iolink_dll_ctx_t));
    return 0;
}
```

**Trimming** is the only reliable way to fit a tiny MCU: reduce
`IOLINK_ISDU_BUFFER_SIZE` (e.g. 64) and `IOLINK_DS_IMAGE_MAX`, set PD sizes to
what the device actually uses, and select `IOLINK_TINY_PROFILE`.

## 2. Stack Usage (Call Depth)

The stack is designed to be shallow. The deepest call path typically occurs during ISDU processing or Event triggering.

*   `iolink_device_process` -> `iolink_dll_process` -> `iolink_isdu_process` -> `handle_standard_commands`
*   **Estimated Max Stack Depth**: ~256 bytes (standard frame)

> **Recommendation**: Allocate at least **512 bytes** for the stack to be safe.

### RTOS Stack Guidelines

When using an RTOS (e.g., FreeRTOS), `iolink_device_process` typically runs in its own thread.

- **Stack Size**: 1024 bytes recommended (allow overhead for context switching and platform API calls).
- **Critical Sections**: The stack uses `iolink_critical_enter/exit`. These should map to fast IRQ-disable routines to minimize latency.
- **Queue Storage**: Events are stored in `iolink_events_ctx_t` (RAM), not on the task stack.

Code size varies heavily by architecture (ARM Cortex-M vs AVR vs RISC-V) and compiler optimization (`-Os`, `-O3`).

These are ARM Cortex-M `-Os` estimates, not measurements of the default host
build. The default CMake build sets no `CMAKE_BUILD_TYPE` (no optimisation) and
`check_quality.sh` builds `Debug` (`-O0`), so both are much larger than `-Os`.

**Estimated Sizes (ARM Cortex-M, -Os):**

| Module | Size (approx) |
| :--- | :--- |
| **Core (DLL/ISDU/Events)** | 4 - 6 KB |
| **Data Storage** | ~1 KB |
| **Total** | **~5 - 7 KB** |

The checksum in `src/crc.c` is computed on the fly (XOR + 6-bit compression);
there are no lookup tables to size.

## 4. Optimization Tips

### Reducing RAM
1.  **Reduce ISDU Buffer**: Use `64` or `32` bytes if you don't need large parameter transfers.
    *   *Define* `IOLINK_ISDU_BUFFER_SIZE` in your build system.
2.  **Shrink Event Queue**: standard IO-Link devices often only need a queue of 1 or 2 events.
3.  **Process Data**: Set `IOLINK_PD_IN_MAX_SIZE` to exactly what your device needs (e.g., 2 bytes).

### Reducing Flash
1.  **Compiler Flags**: Always use `-Os` or `-Oz` (for Clang).
2.  **LTO**: Enable Link Time Optimization (`-flto`).
3.  **Remove Unused Features**: The linker will garbage-collect unused functions if you compile with `-ffunction-sections -fdata-sections` and link with `--gc-sections`.

## 5. Verification

To verify memory usage on GCC-based toolchains:

```bash
size -A libiolinki.a
```
