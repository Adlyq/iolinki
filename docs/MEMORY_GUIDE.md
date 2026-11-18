# iolinki Memory Usage Guide

This guide helps embedded developers estimate the RAM and Flash (ROM) requirements for the `iolinki` stack and provides tips for optimization.

## 1. RAM Usage (Static)

The stack avoids dynamic memory allocation (`malloc`). All state is held in context structures.

### Base Requirements

The application allocates one `iolink_device_ctx_t`. It embeds `iolink_dll_ctx_t`,
which in turn embeds the ISDU, Events and Data Storage contexts, so the numbers
below are nested—do not add them together.

Measured on a 64-bit host with the default buffers in
`include/iolinki/config.h` (`sizeof` of the public types; the default, SPLIT and
FRAME variants are pinned by `tests/test_ctx_sizes.c`):

| Component | Size (bytes) | Description |
| :--- | ---: | :--- |
| **`iolink_device_ctx_t`** | **5120** | The object the application allocates (includes everything below) |
| `iolink_device_ctx_t` (FRAME/FRAME) | 5120 | `IOLINK_ISR_*_MODE` = FRAME without `tx_busy()` (three PD-in buffers instead of two); `iolink_dll_ctx_t` is 4936 |
| `iolink_dll_ctx_t` | 4936 | DLL state, timers, reply-plan table (48×24 B, four state slices), frame mailbox; embeds ISDU, Events and DS |
| `iolink_isdu_ctx_t` | 1616 | Transport state, the two request-slot publication words, the DPP1 RAM image (16 B + seqlock) and the embedded ISDU scratch (1295 B) |
| `iolink_events_ctx_t` | 504 | Event queue, lifecycle records, the double event-memory image and the ISR reset-request handshake |
| `iolink_ds_ctx_t` | 352 | DS state, buffer pointers, the OD set and the embedded download stage |
| `iolink_phy_api_t` | 112 (usually `.rodata`) | Driver vtable, not part of the device context; only the pointer in `iolink_device_config_t` costs RAM (8 B on 64-bit) |

`IOLINK_STATS_MODE=IOLINK_STATS_SPLIT` adds 8 bytes to `iolink_device_ctx_t` on
both the 64-bit host (5128 total) and the Cortex-M0+ target (4288 → 4304 B with
arm-none-eabi-gcc, 4416 → 4424 B with clang): `iolink_dll_shared_t` gains a
12-byte ISR counter set, partly absorbed by existing padding. 32-bit targets are
smaller because pointers are 4 bytes. One exception: `iolink_device_ctx_t::phy_error`
(an `int`) can add 4 bytes there, because the 64-bit layout absorbs it in existing
padding.

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

### Workspace

Transport scratch is kept out of long-lived state. The optional
`iolink_workspace_t` (`include/iolinki/workspace.h`) holds:

| Region | Bytes (default) | Lifetime |
| :--- | ---: | :--- |
| `isdu.request` | 524 | **two** ISDU request slots (262 B each: one being accumulated by the data ISR, one published for the main pump) |
| `isdu.payload` | 256 | one decoded ISDU service |
| `isdu.response` | 256 | one application ISDU response |
| `isdu.frame` | 259 | one framed ISDU response (single buffer, lease model) |
| `ds_stage` | 256 | DS upload/download serialization scratch |
| **Total** | **1551** | |

Applications can supply the workspace (for example in DTCM or external RAM):

```c
static iolink_workspace_t g_ws;                 /* or IOLINK_WORKSPACE_DECLARE */

iolink_device_config_t cfg = { ... };
cfg.workspace = &g_ws;
iolink_device_init(&ctx, &cfg);                 /* cfg.workspace binds the scratch */
```

Passing `NULL` (or leaving the field zero) means "use the scratch embedded in the
context": each context then carries its own fallback buffers and
`iolink_device_get_workspace()` returns `NULL`. With
`IOLINK_WORKSPACE_EMBEDDED=0` there is no embedded fallback.

To move the scratch **out of the context entirely**, build with
`-DIOLINK_WORKSPACE_EMBEDDED=0`. The context then keeps only pointers and
`iolink_device_init()` rejects a missing workspace with
`IOLINK_DEVICE_INIT_ERR_WORKSPACE` (-12). Measured on the same host (this
configuration is compiled by the matrix but not pinned by `test_ctx_sizes.c`):

| Component | Embedded (default) | `IOLINK_WORKSPACE_EMBEDDED=0` |
| :--- | ---: | ---: |
| `iolink_device_ctx_t` | 5120 | **3568** |
| `iolink_isdu_ctx_t` | 1616 | **320** |
| `iolink_ds_ctx_t` | 352 | **96** |
| `iolink_workspace_t` (separate allocation) | — | 1551 |

On Cortex-M0+ the same pair measures 4288 → 2736 B (`iolink_device_ctx_t`, with
arm-none-eabi-gcc) and 4416 → 2864 B (clang).

The trimmed build is the small-MCU path: place the 1551-byte workspace in
DTCM/CCM (or size it down with `IOLINK_ISDU_BUFFER_SIZE` / `IOLINK_DS_IMAGE_MAX`)
instead of carrying it inside the main context. One measured example
(`IOLINK_ISDU_BUFFER_SIZE=64`, `IOLINK_DS_IMAGE_MAX=64`, same `sizeof` probe;
this buffer configuration is not part of the build matrix and its sizes are not
asserted by `test_ctx_sizes.c`):

| Component | Embedded | External workspace |
| :--- | ---: | ---: |
| `iolink_device_ctx_t` | 3968 | **3568** |
| `iolink_workspace_t` | — | 399 |

> **Lease rule (ISDU response).** There is exactly one framed-response buffer.
> Before writing it, the main pump publishes `{len=0, busy=1}` through the reply-plan
> publish word (`dll_isdu_sync_snapshot`); the data ISR reacts to Busy/No
> Service and never reads the buffer while it is being rebuilt. A completed
> response is published together with its generation bit, so a "write then
> read" of the same length still interrupts the ISR's segment sequence and can
> never replay stale bytes. `resp_store[0/1]` are two view indices into that
> same buffer. Application code must not keep a pointer to the framed buffer
> across calls.

> **DS streaming rule.** `iolink_ds_ctx_t` does not carry a resident 256-byte
> image. Uploads, `Data_Storage_Size` and `Parameter_Checksum` serialize the
> current parameter set on demand (`iolink_ds_serialize()` /
> `iolink_ds_refresh()`); the only persistent use of `ds_stage` is the atomic
> download staging (`ParamDownload*`) and NVM persistence. `iolink_ds_get_image()`
> builds and returns the image when a workspace (or the embedded fallback) is
> bound, and returns `NULL` when no staging buffer is available
> (`IOLINK_WORKSPACE_EMBEDDED=0` without an external workspace).

### Configurable Memory (via `include/iolinki/config.h`)

You can tune these values to fit your MCU.

| Macro | Default | Bytes Used | Description |
| :--- | :--- | :--- | :--- |
| `IOLINK_ISDU_BUFFER_SIZE` | 256 | **1295** bytes | `request`(2 × 262) + `payload`(256) + `response`(256) + `frame`(259) |
| `IOLINK_WORKSPACE_EMBEDDED` | 1 | 0 or 1551 | 1 = keep fallback scratch in the contexts; 0 = require an external workspace |
| `IOLINK_EVENT_QUEUE_SIZE` | 4 | scales with queue | `iolink_event_t` queue plus `IOLINK_EVENT_PENDING_MAX` (8) lifecycle records, two 19-byte Table 58 memory images and a 6-entry frozen snapshot |
| `IOLINK_PD_IN_MAX_SIZE` | 32 | 130 bytes | `IOLINK_PD_IN_BUFFERS` PD-in reply buffers of `od_max + pd_in + 1` (65 each); the default BYTE profile uses 2, FRAME/FRAME without `tx_busy()` uses 3 (195) |
| `IOLINK_PD_OUT_MAX_SIZE` | 32 | 32 bytes direct | `pd_out`; it also sets `IOLINK_M_SEQ_MAX_LEN`, which sizes the three ISR payload buffers, the ISR RX buffer, `frame_buf` and each mailbox record (4 × 68 B by default) |
| `IOLINK_DS_IMAGE_MAX` | 256 | 256 bytes | DS serialization/download stage |
| `IOLINK_MBX_SLOTS` | 4 | 272 bytes | Receive-frame mailbox depth; power of two in [2, 128], each record is 68 B |
| `IOLINK_REPLY_PLAN_SLOTS` | 48 | 1152 bytes | Reply-plan entries published to the ISR (48 × 24 B, four state slices) |

### Total RAM

The default `iolink_device_ctx_t` is **5120 bytes** (64-bit host, default
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

> **These are engineering estimates, not a repository baseline.** The library
> code has no checked-in stack measurement; measure the target build with
> `-fstack-usage` (or the toolchain's equivalent) before sizing a task stack.

> **Recommendation**: Allocate at least **512 bytes** for the stack to be safe.

### RTOS Stack Guidelines

When using an RTOS (e.g., FreeRTOS), `iolink_device_process` typically runs in its own thread.
That thread must be the only main-context caller of the context; the data ISR may preempt it,
and no other task may call the IO-Link API (tasks that need to signal the stack should post a
request flag or queue item for the IO-Link task to serve).

- **Stack Size**: 1024 bytes recommended (allow overhead for context switching and platform API calls).
- **Critical Sections**: The stack uses `iolink_critical_enter/exit`. These should map to fast IRQ-disable routines to minimize latency.
- **Queue Storage**: Events are stored in `iolink_events_ctx_t` (RAM), not on the task stack.

### Code size (ROM)

Code size is measured, not estimated: `tools/size_matrix.py` links a minimal
device application against every library source and records the section sizes
in `tools/size_baseline.csv` (arm-none-eabi-gcc) and
`tools/size_baseline.atfe.csv` (clang). The `c1_full` row is the full stack
(DLL, ISDU, Data Storage, events, diagnostics, OD, device and the `phy_generic`
template listed in `tools/size_matrix.py`) as reached by the probe application,
plus that application's `memcpy`/`memset`/`strlen` stubs; no transceiver driver
is linked. Section GC is on, so the row counts what the minimal application and
the constant capability table keep, not every function of every source file.

Basis for the numbers below: `c1_full`, `source` delivery,
`-Wl,--gc-sections`, LTO off, `-Os`, Cortex-M0+, `-nostdlib`, and
`-DIOLINK_LOG_COMPILE_LEVEL=0`, so log format strings are **not** included.
ROM total = `text` + `rodata` + `data`:

| Toolchain | CSV row | text | rodata | data | ROM total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| arm-none-eabi-gcc 15.3.1 | `tools/size_baseline.csv` | 22952 | 3057 | 0 | **26009 B ≈ 25.4 KiB** |
| clang 23.1.0 (ATFE) | `tools/size_baseline.atfe.csv` | 25468 | 3173 | 0 | **28641 B ≈ 28.0 KiB** |

The same row with LTO on is 24276 B (gcc) and 27833 B (clang), i.e. LTO saves
about 1.7 KiB and 0.8 KiB respectively. Host debug builds (`-O0`, the default
CMake build type) are not comparable and are not used for these rows;
`tools/size_matrix.py --check` enforces the baselines and `--table` prints the
full matrix.

The checksum in `src/crc.c` is computed on the fly (XOR + 6-bit compression);
there are no lookup tables to size.

## 3. Optimization Tips

### Reducing RAM
1.  **Reduce ISDU Buffer**: Use `64` or `32` bytes if you don't need large parameter transfers.
    *   *Define* `IOLINK_ISDU_BUFFER_SIZE` in your build system.
2.  **Shrink Event Queue**: standard IO-Link devices often only need a queue of 1 or 2 events.
3.  **Process Data**: Set `IOLINK_PD_IN_MAX_SIZE` to exactly what your device needs (e.g., 2 bytes).

### Reducing Flash
1.  **Compiler Flags**: Always use `-Os` or `-Oz` (for Clang).
2.  **LTO**: Enable Link Time Optimization (`-flto`). It also recovers most of the
    cross-file inlining lost when a large translation unit is split: the size gate
    measures both modes, and the split costs ~24 B under `-flto` instead of ~750 B.
3.  **Remove Unused Features**: The linker will garbage-collect unused functions if you compile with `-ffunction-sections -fdata-sections` and link with `--gc-sections`.

## 4. Verification

To verify memory usage on GCC-based toolchains:

```bash
size -A libiolinki.a
```
