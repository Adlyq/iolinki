# iolinki Memory Usage Guide

This guide helps embedded developers estimate the RAM and Flash (ROM) requirements for the `iolinki` stack and provides tips for optimization.

## 1. RAM Usage (Static)

The stack avoids dynamic memory allocation (`malloc`). All state is held in context structures.

### Base Requirements

The application allocates one `iolink_device_ctx_t`. It embeds `iolink_dll_ctx_t`,
which in turn embeds the ISDU, Events and Data Storage contexts, so the numbers
below are nested—do not add them together.

Measured on a 64-bit host with the default buffers in
`include/iolinki/config.h`:

| Component | Size (bytes) | Description |
| :--- | ---: | :--- |
| **`iolink_device_ctx_t`** | **3712** | The object the application allocates (includes everything below) |
| `iolink_dll_ctx_t` | 3400 | DLL state, timers, counters, ISR staging; embeds ISDU, Events and DS |
| `iolink_isdu_ctx_t` | 1488 | Transport state plus the payload/transport buffers (see below) |
| `iolink_events_ctx_t` | 392 | Event queue plus lifecycle records |
| `iolink_ds_ctx_t` | 376 | DS state and the `IOLINK_DS_IMAGE_MAX` image |
| **PHY API** | 20-30 bytes | Function pointers (usually const/flash, but pointer storage in RAM) |

`IOLINK_STATS_MODE=IOLINK_STATS_SPLIT` adds 16 bytes (two counter sets plus padding).
32-bit targets are smaller because pointers are 4 bytes.

The DLL state is owned by two sibling structures inside `iolink_dll_ctx_t`:
`iolink_dll_main_t` (which nests the main/ISR shared region) and
`iolink_dll_isr_t`. Both live in the same single allocation; the split only
changes type visibility, not the allocation strategy. `tools/check_ownership.sh`
fails the build if the main-context translation unit can reference
ISR-exclusive fields.

> **These numbers are a contract.** `tests/test_ctx_sizes.c` asserts the exact
> sizes at build time (`IOLINK_STATS_SHARED` and `SPLIT` variants); a layout or
> buffer-default change fails CI until the test and this table are updated
> together. Use that test as the authority instead of trusting arithmetic.

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

The default `iolink_device_ctx_t` is **3712 bytes** (64-bit host, default
buffers); buffer arrays dominate it. On a 32-bit target the same layout is
smaller. To measure your own build instead of relying on the table:

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
That thread must be the only main-context caller of the context; the data ISR may preempt it,
and no other task may call the IO-Link API (tasks that need to signal the stack should post a
request flag or queue item for the IO-Link task to serve).

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
