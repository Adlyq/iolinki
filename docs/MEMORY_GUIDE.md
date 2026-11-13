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
| **`iolink_device_ctx_t`** | **4072** | The object the application allocates (includes everything below) |
| `iolink_dll_ctx_t` | 3760 | DLL state, timers, reply-plan table (10×24 B), ISR staging; embeds ISDU, Events and DS |
| `iolink_isdu_ctx_t` | 1328 | Transport state, buffer pointers and the embedded ISDU scratch (1033 B) |
| `iolink_events_ctx_t` | 504 | Event queue plus lifecycle records |
| `iolink_ds_ctx_t` | 392 | DS state, buffer pointers and the embedded download stage |
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
> sizes at build time (`IOLINK_STATS_SHARED` and `SPLIT` variants) plus every
> workspace buffer capacity; a layout or buffer-default change fails CI until
> the test and this table are updated together. Use that test as the authority
> instead of trusting arithmetic.

### Workspace (P8)

Transport scratch is no longer mixed into long-lived state. The optional
`iolink_workspace_t` (`include/iolinki/workspace.h`) holds:

| Region | Bytes (default) | Lifetime |
| :--- | ---: | :--- |
| `isdu.request` | 262 | one ISDU request |
| `isdu.payload` | 256 | one decoded ISDU service |
| `isdu.response` | 256 | one application ISDU response |
| `isdu.frame` | 259 | one framed ISDU response (single buffer, lease model) |
| `ds_stage` | 256 | DS upload/download serialization scratch |
| **Total** | **1289** | |

Applications can supply the workspace (for example in DTCM or external RAM):

```c
static iolink_workspace_t g_ws;                 /* or IOLINK_WORKSPACE_DECLARE */

iolink_device_config_t cfg = { ... };
cfg.workspace = &g_ws;
iolink_device_init(&ctx, &cfg);                 /* or iolink_device_init_with_workspace() */
```

Passing `NULL` (or leaving the field zero) keeps the historical behaviour: each
context uses its embedded fallback scratch, so the API stays compatible.
`iolink_device_get_workspace()` returns the bound pointer or `NULL` for the
embedded case.

To move the scratch **out of the context entirely**, build with
`-DIOLINK_WORKSPACE_EMBEDDED=0`. The context then keeps only pointers and
`iolink_device_init()` rejects a missing workspace with
`IOLINK_DEVICE_INIT_ERR_WORKSPACE` (-16). Measured on the same host:

| Component | Embedded (default) | `IOLINK_WORKSPACE_EMBEDDED=0` |
| :--- | ---: | ---: |
| `iolink_device_ctx_t` | 4072 | **2776** |
| `iolink_isdu_ctx_t` | 1328 | **280** |
| `iolink_ds_ctx_t` | 392 | **136** |
| `iolink_workspace_t` (separate allocation) | — | 1289 |

The trimmed build is the small-MCU path: place the 1289-byte workspace in
DTCM/CCM (or size it down with `IOLINK_ISDU_BUFFER_SIZE` / `IOLINK_DS_IMAGE_MAX`)
instead of carrying it inside the main context. One measured example (`IOLINK_ISDU_BUFFER_SIZE=64`, `IOLINK_DS_IMAGE_MAX=64`):

| Component | Embedded | External workspace |
| :--- | ---: | ---: |
| `iolink_device_ctx_t` | 3008 | **2672** |
| `iolink_workspace_t` | — | 329 |

> **Lease rule (ISDU response).** There is exactly one framed-response buffer.
> Before writing it, the main pump publishes `{len=0, busy=1}` through the P5
> publish word (`dll_isdu_sync_snapshot`); the data ISR reacts to Busy/No
> Service and never reads the buffer while it is being rebuilt. A completed
> response is published together with its generation bit, so a "write then
> read" of the same length still interrupts the ISR's segment sequence and can
> never replay stale bytes. `resp_store[0/1]` remain as two view indices, but
> both point at the same buffer; the double copy is gone. Application code must
> not keep a pointer to the framed buffer across calls.

> **DS streaming rule.** `iolink_ds_ctx_t` no longer carries a resident 256-byte
> image. Uploads, `Data_Storage_Size` and `Parameter_Checksum` serialize the
> current parameter set on demand (`iolink_ds_serialize()` /
> `iolink_ds_refresh()`); the only persistent use of `ds_stage` is the atomic
> download staging (`ParamDownload*`) and NVM persistence. `iolink_ds_get_image()`
> still works when a workspace/embedded stage is bound and returns `NULL` in the
> streaming-only configuration.

### Configurable Memory (via `include/iolinki/config.h`)

You can tune these values to fit your MCU.

| Macro | Default | Bytes Used | Description |
| :--- | :--- | :--- | :--- |
| `IOLINK_ISDU_BUFFER_SIZE` | 256 | **1033** bytes | `request`(262) + `payload`(256) + `response`(256) + `frame`(259) |
| `IOLINK_WORKSPACE_EMBEDDED` | 1 | 0 or 1289 | 1 = keep fallback scratch in the contexts; 0 = require an external workspace |
| `IOLINK_EVENT_QUEUE_SIZE` | 4 | scales with queue | `iolink_event_t` queue plus `IOLINK_EVENT_PENDING_MAX` (8) lifecycle records, a 19-byte Table 58 memory and a 6-entry frozen snapshot |
| `IOLINK_PD_IN_MAX_SIZE` | 32 | 195 bytes | Three reply buffers of `od_max + pd_in + 1` (65 each) |
| `IOLINK_PD_OUT_MAX_SIZE` | 32 | 32 bytes | Output Process Data buffer (also sizes the ISR RX buffer) |
| `IOLINK_DS_IMAGE_MAX` | 256 | 256 bytes | DS serialization/download stage |

### Total RAM

The default `iolink_device_ctx_t` is **4072 bytes** (64-bit host, default
buffers); buffer arrays dominate it. On a 32-bit target the same layout is
smaller. To measure your own build instead of relying on the table:

```c
#include <stdio.h>
#include "iolinki/device.h"
#include "iolinki/workspace.h"
int main(void) {
    printf("device ctx: %zu\n", sizeof(iolink_device_ctx_t));
    printf("dll ctx:    %zu\n", sizeof(iolink_dll_ctx_t));
    printf("workspace:  %zu\n", sizeof(iolink_workspace_t));
    return 0;
}
```

**Trimming** is the only reliable way to fit a tiny MCU: reduce
`IOLINK_ISDU_BUFFER_SIZE` (e.g. 64) and `IOLINK_DS_IMAGE_MAX`, set PD sizes to
what the device actually uses, build with `-DIOLINK_FEATURE_DATA_STORAGE=OFF`
(or keep DS and shrink `IOLINK_DS_IMAGE_MAX`), and use
`IOLINK_WORKSPACE_EMBEDDED=0` plus an application-provided workspace.

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
