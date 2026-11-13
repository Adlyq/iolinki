# API Reference

The stack is **context-based** (no singletons): you allocate an
`iolink_device_ctx_t` and pass it to every call. Multiple device instances can
coexist as long as each has its own context. See `include/iolinki/device.h`,
`include/iolinki/application.h`, `include/iolinki/events.h`,
`include/iolinki/isdu.h`, `include/iolinki/data_storage.h`,
`include/iolinki/workspace.h`, `include/iolinki/capability.h`,
`include/iolinki/od.h`, `include/iolinki/diag.h` and
`include/iolinki/storage.h` for the authoritative declarations.

## Core API

### Initialization

```c
#include "iolinki/device.h"

int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);
iolink_workspace_t* iolink_device_get_workspace(const iolink_device_ctx_t* ctx);
```

Initializes the stack from `config`. `config` (and everything it points at)
must stay valid for the lifetime of `ctx`.

**Returns**: `0` on success, a negative `iolink_device_init_result_t`
(`IOLINK_DEVICE_INIT_ERR_*`), or the negative error returned by the PHY `init`
callback.

#### Workspace

`iolink_workspace_t` (see `include/iolinki/workspace.h`) holds the ISDU
request/payload/response/frame scratch and, with Data Storage compiled in, the
DS serialization staging area. It never holds long-lived state, so it may live
in DTCM or external RAM and does not need to survive a transaction.

- `IOLINK_WORKSPACE_EMBEDDED=1` (default): `config.workspace` is optional. When
  it is `NULL`, `iolink_device_init()` keeps the scratch buffers embedded in the
  context and `iolink_device_get_workspace()` returns `NULL`.
- `IOLINK_WORKSPACE_EMBEDDED=0`: no fallback exists. `config.workspace` must be
  non-`NULL`, otherwise `iolink_device_init()` returns
  `IOLINK_DEVICE_INIT_ERR_WORKSPACE`.

`iolink_device_get_workspace()` returns the workspace currently bound to the
instance, or `NULL` when the embedded fallback is in use.
`IOLINK_WORKSPACE_DECLARE(name)` declares a static instance.

#### Device configuration

```c
typedef struct {
    iolink_phy_api_t phy;                        /* PHY driver vtable */
    iolink_stack_config_t stack;                 /* protocol settings */
    const iolink_app_callbacks_t* app_callbacks; /* may be NULL; a non-NULL
                                                    on_pd_input selects the PD-in
                                                    callback path */
    const iolink_device_info_t* device_info;     /* may be NULL */
#if IOLINK_FEATURE_DATA_STORAGE
    const iolink_storage_api_t* storage;         /* required: read+write callbacks */
#endif
    iolink_device_time_us_fn time_us;        /* required microsecond clock (ISR-safe); NULL rejects init */
    void* user;                              /* opaque pointer for callbacks */
    iolink_log_write_fn log_write;           /* optional runtime log sink; NULL is silent */
    iolink_log_level_t log_level;            /* runtime log threshold; SILENT (0) disables */
    const iolink_od_app_t* od;               /* optional application object dictionary */
    iolink_workspace_t* workspace;           /* optional scratch; NULL = embedded fallback */
} iolink_device_config_t;
```

#### Stack configuration

```c
typedef struct {
    iolink_m_seq_type_t m_seq_type; /* Type 0/1_2/1_V/2_1..2_5/2_V */
    uint8_t min_cycle_time;         /* minimum cycle time, 0.1 ms units */
    uint8_t pd_in_len;              /* Process Data Input length (bytes) */
    uint8_t pd_out_len;             /* Process Data Output length (bytes) */
    uint32_t t_pd_us;               /* power-on delay (t_pd), microseconds */
    uint8_t od_width;               /* OD octets; 0 = derive from m_seq_type */
    iolink_baudrate_t baudrate;     /* operating COM when baudrate_set is true */
    bool baudrate_set;              /* false keeps the COM2 default */
    uint8_t master_lost_cycles;     /* missed cycles before master_lost; 0 = off */
    uint8_t compat_flags;           /* IOLINK_STACK_COMPAT_* bits, OR-ed with the build default */
    uint8_t flags;                  /* IOLINK_STACK_FLAG_* bits */
    uint8_t timing_enforce;         /* IOLINK_STACK_TIMING_*; DEFAULT = build profile */
    uint8_t reserved;               /* must be 0 */
    uint16_t event_min_appear_ms;   /* event min-appear window (ms); 0 = build default */
    uint16_t event_min_repeat_ms;   /* same-code repeat window (ms); 0 = build default */
} iolink_stack_config_t;
```

`min_cycle_time` is stored raw in 0.1 ms units and encoded per Table B.3 only
when Direct Parameter Page 1 is built.

`compat_flags` / `flags` / `timing_enforce` are the runtime half of the
capability table: they are merged with the build-time defaults when
`iolink_caps_resolve()` runs during `iolink_device_init()`. `event_min_appear_ms`
and `event_min_repeat_ms` override the build-time A.6.4 windows; `0` keeps the
build default.

**M-Sequence types**:
- `IOLINK_M_SEQ_TYPE_0`: 1 OD octet, no PD.
- `IOLINK_M_SEQ_TYPE_1_1`: legacy interleave; rejected by a V1.1.5 Device
  (Table A.10 NOTE 3) with `IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE`.
- `IOLINK_M_SEQ_TYPE_1_2`: 2 OD octets, no PD.
- `IOLINK_M_SEQ_TYPE_1_V`: 8 or 32 OD octets, no PD.
- `IOLINK_M_SEQ_TYPE_2_1` / `2_2`: 1 / 2 PD-in + 1 OD.
- `IOLINK_M_SEQ_TYPE_2_3` / `2_4`: 1 / 2 PD-out + 1 OD.
- `IOLINK_M_SEQ_TYPE_2_5`: 1 PD-in + 1 PD-out + 1 OD.
- `IOLINK_M_SEQ_TYPE_2_V`: variable PD (0..32) + 1/2/8/32 OD.

Fixed PD/OD layouts are enforced at `iolink_device_init()`; a mismatch returns
`IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT`.

#### Example

```c
#include "iolinki/device.h"

extern const iolink_phy_api_t g_phy_virtual;
extern const iolink_device_info_t g_identity;

static iolink_device_ctx_t g_ctx;
static uint64_t g_time_us; /* advanced by your microsecond timer */

static uint64_t board_time_us(void* user) {
    (void) user;
    return g_time_us;
}

static const iolink_device_config_t g_cfg = {
    .phy = g_phy_virtual,
    .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 3, .pd_out_len = 1},
    .time_us = board_time_us,
    .device_info = &g_identity,
};

int main(void) {
    if (iolink_device_init(&g_ctx, &g_cfg) != 0) {
        return -1;
    }
    for (;;) {
        iolink_device_process(&g_ctx);
        /* sleep ~1 ms */
    }
}
```

### Main loop and ISR pump

```c
void iolink_device_process(iolink_device_ctx_t* ctx);      /* main loop */
void iolink_device_process_isr(iolink_device_ctx_t* ctx);  /* data ISR */
```

The stack uses a two-pump transport: `iolink_device_process()` runs the
protocol logic and must be called periodically (e.g. every 1 ms), while
`iolink_device_process_isr()` is called from the PHY data interrupt to frame
requests and transmit replies.

### Process Data

```c
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data,
                                  size_t len, bool valid);
int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx, uint8_t* data, size_t len);
```

`pd_input_update` publishes Device-to-Master process data (`valid` drives the
CKS PD-invalid bit). Both PD-in paths exist in the same build: a non-NULL
`on_pd_input` selects the callback path at init and sets the instance bit
`IOLINK_CAP_PD_IN_CALLBACK`; otherwise the application polls with this function.
Once the callback path is selected, `pd_input_update` returns
`IOLINK_DEVICE_INIT_ERR_PD_INPUT` instead of silently overwriting the callback
output. `pd_output_read` returns the latest Master-to-Device data.

### Capabilities

```c
iolink_caps_t iolink_device_capabilities(const iolink_device_ctx_t* ctx);
iolink_caps_t iolink_device_set_capabilities(iolink_device_ctx_t* ctx, iolink_caps_t caps);

const iolink_build_caps_t* iolink_caps_build(void);
iolink_caps_t iolink_caps_resolve(const iolink_phy_api_t* phy, const iolink_stack_config_t* stack,
                                  const iolink_app_callbacks_t* app,
                                  const iolink_build_caps_t* build);
bool iolink_caps_has(iolink_caps_t caps, iolink_caps_t bit);
bool iolink_caps_built(const iolink_build_caps_t* caps, uint32_t mask);
```

The capability table has two layers. `iolink_build_caps_t` (`iolink_caps_build()`,
`const` in `.rodata`) describes what this image was compiled with: ISR RX/TX
granularity (`IOLINK_CAP_ISR_RX_*` / `IOLINK_CAP_ISR_TX_*`), concurrency/statistics
profiles, PD-in buffer count and the `IOLINK_CAPB_*` built-feature bits
(`DATA_STORAGE`, `EVENTS`, `SYSTEM_COMMAND`, `ISR_LOG`, `ISR_TX_ONLY`).
`iolink_caps_t` is the per-instance mask resolved once by
`iolink_caps_resolve()` after the PHY is bound, and reachable at runtime through
`iolink_device_capabilities()`.

Instance bits: `IOLINK_CAP_RX`, `_TX`, `_WAKEUP`, `_SIO_CQ`, `_TX_BUSY`,
`_REQUEST_SERVICE`, `_PHY_FAULTS` (probed from the PHY vtable),
`_PD_IN_CALLBACK`, `_SYSTEM_COMMAND`, `_EVENTS`, `_COMPAT_V10_OP`,
`_COMPAT_T0_READ`, `_DS_LEGACY_SUB0`, `_REQUIRE_ISR_TX`, and the four
DeviceAccessLocks bits `_LOCK_PARAM`, `_LOCK_DS`, `_LOCK_LOCAL_PARAM`,
`_LOCK_LOCAL_UI` (grouped as `IOLINK_CAP_LOCKS_ALL`).

`iolink_device_set_capabilities()` only accepts changes to runtime switches
(compatibility bits, SystemCommand, access locks). Bits tied to the PHY vtable,
the build profile or `on_pd_input` are re-derived from the current configuration
and cannot be forged; `IOLINK_CAP_EVENTS` and `IOLINK_CAP_SYSTEM_COMMAND` stay
cleared when those subsystems were not compiled in. It returns the mask that
actually took effect.

`iolink_device_init()` rejects two inconsistent combinations with
`IOLINK_DEVICE_INIT_ERR_PHY`: `IOLINK_CAP_REQUIRE_ISR_TX` without a PHY
`request_service()` hook, and a TX FRAME build with
`IOLINK_PHY_FEATURE_TX_BUSY=1` but no `tx_busy()` hook.

### Reset handler

```c
typedef void (*iolink_reset_handler_t)(iolink_reset_type_t type);
void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx, iolink_reset_handler_t handler);
```

Invoked on SystemCommand `0x80` (`IOLINK_RESET_DEVICE`) / `0x81`
(`IOLINK_RESET_APPLICATION`). The stack does not reboot on its own.

## PHY Layer API

Copy `iolink_phy_api_t` from `include/iolinki/phy.h`. Every callback takes
`void* user` first. `init`, `set_mode`, and `set_baudrate` are required. The
transport hooks depend on the configured granularity: `rx_octet`/`tx_octet` in
BYTE mode or `rx_frame`/`tx_frame` in FRAME mode, plus `int_active`.
`tx_done` (TX BYTE) is an optional completion notification: TX capability is
resolved from `tx_octet`, and the library completes the frame itself when the
hook is absent. `tx_busy` (TX FRAME), `detect_wakeup`, `set_cq_line` and
`get_faults` may be NULL.

`IOLINK_BAUDRATE_COM1`, `COM2`, and `COM3` are 0, 1, and 2. They select 4.8,
38.4, and 230.4 kbit/s. They are not the baud number itself.

```c
static int my_phy_rx_octet(void* user, uint8_t* data, uint8_t* status) {
    (void)user;
    (void)data;
    (void)status;
    return 0;
}

static int my_phy_tx_octet(void* user, uint8_t data) {
    (void)user;
    (void)data;
    return 0;
}
```

## Application Layer API

```c
typedef struct {
    void (*on_startup)(void* user);
    void (*on_preoperate)(void* user);
    void (*on_operate)(void* user);
    bool (*on_pd_input)(void* user, uint8_t* data, uint8_t len); /* non-NULL selects the callback path */
    void (*on_pd_output)(void* user, uint8_t* data, uint8_t len);
    void (*on_state_change)(void* user, iolink_dll_state_t state);
    void (*on_master_lost)(void* user);
    void (*on_master_online)(void* user);
} iolink_app_callbacks_t;
```

Every callback receives `iolink_device_config_t.user`, so the application does
not need a global to reach its own state. Register them through
`iolink_device_config_t.app_callbacks`; there is no separate registration
function. `on_pd_input` fills an idle PD-in buffer and its return value becomes
the PD-valid flag; leaving it `NULL` means the application publishes PD-in with
`iolink_device_pd_input_update()` instead.

**Example**:

```c
static void on_operate(void* user) { /* ... */ }
static void on_pd_output(void* user, uint8_t* data, uint8_t len) { (void)user; (void)data; (void)len; }

static const iolink_app_callbacks_t g_app = {
    .on_operate = on_operate,
    .on_pd_output = on_pd_output,
};
/* ... .app_callbacks = &g_app and .user = &app_state in iolink_device_config_t ... */
```

## Object Dictionary API

The object dictionary (`include/iolinki/od.h`) is the single description of
every parameter: index/subindex, datatype codec, access rights, write semantics,
storage class, defaults and optional `validate`/`on_change` hooks. ISDU
dispatching, Data Storage membership and IODD export all derive from it.

```c
typedef struct {
    uint16_t index;
    uint8_t subindex;
    uint8_t max_subindex;
    uint8_t datatype;         /* IOLINK_OD_DT_* */
    uint8_t access;           /* IOLINK_OD_ACCESS_* */
    uint8_t write_semantics;  /* IOLINK_OD_WRITE_STORE / _VOLATILE / _TRIGGER */
    uint8_t storage;          /* IOLINK_OD_STORAGE_VOLATILE / _DS / _NVM */
    uint8_t flags;            /* IOLINK_OD_FLAG_* */
    uint8_t handler;          /* IOLINK_OD_HANDLER_* */
    uint8_t block_len;
    uint32_t range_min;
    uint32_t range_max;
    uint32_t scalar_default;
    const void* block_default;
    const char* name;
    iolink_od_validate_fn validate;
    iolink_od_change_fn on_change;
} iolink_od_entry_t;

typedef struct {
    const iolink_od_entry_t* entries;
    size_t count;
    iolink_od_read_fn read;
    iolink_od_write_fn write;
    void* user;
} iolink_od_app_t;

const iolink_od_entry_t* iolink_od_find(const iolink_od_app_t* app, uint16_t index,
                                        uint8_t subindex);
const iolink_od_entry_t* iolink_od_find_app(const iolink_od_app_t* app, uint16_t index,
                                            uint8_t subindex);
iolink_result_t iolink_od_read_value(const iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                     uint8_t subindex, uint8_t* out, size_t cap);
iolink_result_t iolink_od_write_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                      const uint8_t* data, size_t len);
```

Standard objects (`0x0000-0x003F` plus the vendor error statistics at `0x0040`)
live in a built-in `const` table served by stack handlers; applications add
their own parameters through `config.od`. Table entries are plain values so the
whole table can stay `const` in ROM; runtime values come from the `read`/`write`
backend (or the parameter manager for tag objects).

**Adding a parameter** takes one table row plus one branch in your backend:

```c
static const iolink_od_entry_t g_od_entries[] = {
    IOLINK_OD_SCALAR(0x0050, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_STORE,
                     IOLINK_OD_STORAGE_NVM, IOLINK_OD_HANDLER_BACKEND, 0U, 1000U, 500U,
                     "Threshold"),
};

static const iolink_od_app_t g_od = {
    .entries = g_od_entries,
    .count = sizeof(g_od_entries) / sizeof(g_od_entries[0]),
    .read = my_od_read,
    .write = my_od_write,
    .user = &app_state,
};
/* ... .od = &g_od in iolink_device_config_t ... */
```

`IOLINK_OD_SCALAR` / `IOLINK_OD_SCALAR_EX` / `IOLINK_OD_STRING` build the rows;
`IOLINK_OD_SCALAR_EX` adds the hooks. Index ownership follows the index domain:
normative indices (below `0x0040`) are served from the standard table and cannot
be overridden, while device-specific indices (`0x0040-0x00FE`, `0x0100+`) try
the application table first and fall back to the built-in standard entry
(`0x0040` error statistics) when undeclared. `iolink_od_write_value()` receives
the requested subindex, enforces `max_subindex`, and rejects OctetString payloads
above `block_len` with `0x33`. See
`examples/switching_sensor/switching_sensor_od.c` for a complete table with
validation and trigger semantics.

## Event API

```c
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx);

void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type);
void iolink_event_clear(iolink_events_ctx_t* ctx, uint16_t code);
void iolink_events_process(iolink_events_ctx_t* ctx);   /* driven by iolink_device_process */
bool iolink_events_pending(const iolink_events_ctx_t* ctx);
```

`iolink_device_process()` calls `iolink_events_process()` each cycle while the
ISDU transport is idle, emitting deferred "Event disappears" entries. Call it
yourself only if you drive the event context directly; see `events.h`.
`iolink_event_trigger()` normalises the event type through the diagnostic
registry (`diag.h`), which is also what fills DeviceStatus and
DetailedDeviceStatus. The A.6.4 appear/repeat windows default to
`IOLINK_EVENT_MIN_APPEAR_MS` / `IOLINK_EVENT_MIN_REPEAT_MS` and can be overridden
per instance with `iolink_stack_config_t::event_min_appear_ms` /
`event_min_repeat_ms` (`0` keeps the build default).

**Event types**:

```c
typedef enum {
    IOLINK_EVENT_TYPE_NOTIFICATION = 0,
    IOLINK_EVENT_TYPE_WARNING = 1,
    IOLINK_EVENT_TYPE_ERROR = 2
} iolink_event_type_t;
```

**Example**:

```c
iolink_event_trigger(iolink_device_get_events_ctx(&g_ctx), IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN,
                     IOLINK_EVENT_TYPE_WARNING);
```

## Diagnostics API

`include/iolinki/diag.h` is the single derivation point for EventCode,
DeviceStatus (`0x0024`) and DetailedDeviceStatus (`0x0025`). A `const` ROM
registry describes each diagnostic source (spec EventCode and EventType, the
Table D.1 preferred DeviceStatus, level/pulse behavior, hold window); a small
RAM cache keeps the source-side facts (ISR one-shots, last-occurrence time,
edge counts). The event memory remains the authority for condition lifetime, so
DeviceStatus, DDS and the `0x1800-0x18FF` communication events are all
projections of the same conditions.

```c
bool iolink_diag_set_level(iolink_events_ctx_t* events, iolink_diag_src_t src, bool level);
void iolink_diag_pulse(iolink_events_ctx_t* events, iolink_diag_src_t src);
void iolink_diag_pulse_isr(iolink_events_ctx_t* events, iolink_diag_src_t src);
void iolink_diag_service(iolink_events_ctx_t* events, uint32_t now_ms);
void iolink_diag_raise_code(iolink_events_ctx_t* events, uint16_t code);
void iolink_diag_clear_code(iolink_events_ctx_t* events, uint16_t code);
uint8_t iolink_diag_device_status(iolink_events_ctx_t* events);
uint8_t iolink_diag_dds_snapshot(iolink_events_ctx_t* events, iolink_diag_dds_entry_t* out,
                                 uint8_t max);
```

Sources are `IOLINK_DIAG_SRC_PHY_*` (undervoltage, overvoltage, overtemperature,
short circuit) and `IOLINK_DIAG_SRC_COMM_*` (CRC, timeout, framing, timing).
`set_level` is for level sources sampled once per cycle, `pulse`/`pulse_isr` for
pulse sources; `pulse_isr` only writes a one-shot byte and is safe from the data
ISR, while `service` consumes it in the main context (the DLL calls it from
`iolink_dll_process()`, so applications do not have to). `raise_code`/`clear_code`
are the application-level wrappers around `iolink_event_trigger()` /
`iolink_event_clear()` and use the registry to normalise the event type.

Neither DeviceStatus nor DDS is cached: both are recomputed from the condition
table on every read, which is why `iolink_device_process()` does not need to
maintain a parallel status mirror. With `IOLINK_FEATURE_EVENTS=0` the whole
module disappears: `0x0025` reports index-not-available and `0x0024` returns 0.

## Data Storage API

```c
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);

void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_storage_api_t* storage);
void iolink_ds_bind_workspace(iolink_ds_ctx_t* ctx, iolink_workspace_t* ws);
void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx);
void iolink_ds_bind_od(iolink_ds_ctx_t* ctx, const iolink_od_app_t* app);
void iolink_ds_bind_param_access(iolink_ds_ctx_t* ctx,
                                 iolink_ds_param_read_fn read, iolink_ds_param_write_fn write,
                                 iolink_ds_param_check_fn check, void* user,
                                 const iolink_ds_param_desc_t* params, size_t count);
void iolink_ds_process(iolink_ds_ctx_t* ctx);                  /* driven by iolink_device_process */
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum);
int  iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);
int  iolink_ds_serialize(iolink_ds_ctx_t* ctx, uint8_t* out, size_t cap);
void iolink_ds_refresh(iolink_ds_ctx_t* ctx);
int  iolink_ds_build_image(iolink_ds_ctx_t* ctx);
int  iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);
```

The DS context is initialized by `iolink_device_init()` from the required
`config.storage` backend (declared only with `IOLINK_FEATURE_DATA_STORAGE=1`);
use `iolink_device_get_ds_ctx()` to reach it.

The DS parameter set is derived from the standard OD table plus the application
table bound with `iolink_ds_bind_od()` (the device layer binds
`iolink_device_config_t.od`): entries marked `IOLINK_OD_STORAGE_DS` are
serialized, `IOLINK_OD_STORAGE_VOLATILE` entries are not. `iolink_ds_apply_image()`
validates every known record through the optional check callback before it
writes any of them; a rejected record leaves the previous parameter set intact.
This is not a cross-parameter transaction: an NVM write failure during the apply
phase can still leave a prefix of the records stored.

The serialized image is **not** resident state: the context only holds a pointer
to a staging buffer (`iolink_workspace_t::ds_stage`, or the embedded fallback).
Uploads and `Parameter_Checksum` reads stream through `iolink_ds_serialize()`,
which can write into the ISDU response buffer or with `out == NULL` just compute
length and checksum. `iolink_ds_refresh()` updates only
`Data_Storage_Size`/`Parameter_Checksum` without materialising the image; the
full staging buffer is used by downloads and by `iolink_ds_build_image()`.

## Storage backend

`iolink_device_config_t::storage` is the single NVM backend (interface,
region layout, sizing and an example are in [STORAGE.md](STORAGE.md)). The
persistent region layout and its magic/version/checksum are defined in
`include/iolinki/storage.h`.

## Error codes

```c
typedef enum {
    IOLINK_DEVICE_INIT_OK = 0,
    IOLINK_DEVICE_INIT_ERR_NULL = -1,
    IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE = -2,
    IOLINK_DEVICE_INIT_ERR_PD_IN_LEN = -3,
    IOLINK_DEVICE_INIT_ERR_PD_OUT_LEN = -4,
    IOLINK_DEVICE_INIT_ERR_OD_WIDTH = -5,
    IOLINK_DEVICE_INIT_ERR_OD_CAPACITY = -6,
    IOLINK_DEVICE_INIT_ERR_BAUDRATE = -7,
    /* -8 is unused (numbering kept stable). */
    IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT = -9,
    IOLINK_DEVICE_INIT_ERR_TIME = -10,      /* time_us is NULL */
    IOLINK_DEVICE_INIT_ERR_CRITICAL = -11,  /* weak critical-section default in use */
    IOLINK_DEVICE_INIT_ERR_STORAGE = -12,   /* storage NULL or read/write missing */
    /* -13 was the RTOS lock requirement; retired with the RTOS tier. */
    IOLINK_DEVICE_INIT_ERR_PD_INPUT = -14,  /* polling update on the on_pd_input path */
    IOLINK_DEVICE_INIT_ERR_PHY = -15,       /* required PHY hook is NULL */
    IOLINK_DEVICE_INIT_ERR_WORKSPACE = -16, /* IOLINK_WORKSPACE_EMBEDDED=0 without a workspace */
} iolink_device_init_result_t;
```

ISDU additional codes (`IOLINK_ISDU_ERROR_*`, e.g. `IDX_NOTAVAIL` `0x11`,
`SUBINDEX_NOT_AVAIL` `0x12`, `WRITE_PROTECTED`/access denied `0x23`,
`FUNCTION_NOT_AVAIL` `0x35`) are defined in `include/iolinki/protocol.h`.

## Platform abstraction

`iolink_device_config_t::time_us` is required by `iolink_device_init()` (NULL is
rejected with `IOLINK_DEVICE_INIT_ERR_TIME`), and the stack uses it for all DLL,
ISR and event timing. There is no separate platform time API; direct DLL users
must bind `ctx->shared.time_us`/`shared.time_user` after `iolink_dll_init()`
(the DLL context is split into a main view and an ISR view). Critical sections are
`iolink_critical_enter()` / `iolink_critical_exit()` from
`include/iolinki/port.h`; `iolink_device_init()` rejects the library's weak
no-op default with `IOLINK_DEVICE_INIT_ERR_CRITICAL`.

## Build configuration

Every compile-time switch, profile and capacity override is documented in
[feature-trimming.md](feature-trimming.md). The transport granularity is selected
with `IOLINK_ISR_RX_MODE` / `IOLINK_ISR_TX_MODE` (BYTE or FRAME).

The protocol-compatibility switches `IOLINK_COMPAT_V10_STARTUP_OPERATE`,
`IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE` and `IOLINK_DS_LEGACY_IMAGE_SUB0` are
build-time defaults for runtime bits: the behaviour is selected per instance
through `iolink_stack_config_t::compat_flags`. `IOLINK_FEATURE_*`,
`IOLINK_PHY_FEATURE_ISR_TX_ONLY` and `IOLINK_PHY_FEATURE_TX_BUSY` act at compile
time. Wake-up detection, SIO C/Q and the PD-input path have no build switch:
the PHY vtable and the presence of `on_pd_input` are the only selectors.

## Logging

The stack contains print points but no output implementation. Every context
carries an `iolink_log_sink_t` (callback + user pointer + runtime level), so the
application decides where and at which verbosity the lines go:

```c
#include <stdarg.h>
#include <stdio.h>

static void my_log(void* user, iolink_log_level_t level, const char* format, ...)
{
    (void) user;
    (void) level;
    va_list args;
    va_start(args, format);
    (void) vfprintf(stderr, format, args);   /* or an RTOS/UART logger */
    va_end(args);
}

config.user = my_context;
config.log_write = my_log;
config.log_level = IOLINK_LOG_LEVEL_INFO;
```

`IOLINK_LOG_LEVEL_SILENT` (the default) or a NULL `config.log_write` keeps the
stack silent. `iolink_device_set_log(ctx, write, level)` changes the sink at
runtime; the `user` argument stays `config.user`. The sink is copied into the
DLL, ISDU, DS and event contexts, so direct users of those modules bind
`ctx->log` themselves with `iolink_log_sink_init()`.

The library does not format or buffer anything: the callback receives a
printf-style format string and its arguments, and only evaluates them when the
line passes the runtime threshold. The ISR print points use a separate
`IOLINK_LOG_ISR_*` family and stay compiled out unless `IOLINK_LOG_ENABLE_ISR`
is 1; a sink used from the ISR must be non-blocking and ISR-safe.


## Thread safety

The stack has a single main-context caller per context: the data ISR
(`iolink_device_process_isr`) may preempt that caller, but no other thread or
task may enter the API. The library does not take an application thread lock;
the CPU-level `iolink_critical_*` (IRQ mask) is the only mutual exclusion it
uses. Do not wrap the entry points in your own lock either: the library owns the
granularity of its critical sections. `iolink_device_init()` verifies that the
platform critical section is real. Multiple device instances are supported by
using one `iolink_device_ctx_t` per instance, each with its own main-context
caller and data ISR. If thread-level serialisation is ever added, the library
will take a per-instance lock at the public entry points.
