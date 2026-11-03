# API Reference

The stack is **context-based** (no singletons): you allocate an
`iolink_device_ctx_t` and pass it to every call. Multiple device instances can
coexist as long as each has its own context. See `include/iolinki/device.h`,
`include/iolinki/application.h`, `include/iolinki/events.h`,
`include/iolinki/isdu.h`, `include/iolinki/data_storage.h` and
`include/iolinki/storage.h` for the authoritative declarations.

## Core API

### Initialization

```c
#include "iolinki/device.h"

int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);
```

Initializes the stack from `config`. `config` (and everything it points at)
must stay valid for the lifetime of `ctx`.

**Returns**: `0` on success, a negative `iolink_device_init_result_t`
(`IOLINK_DEVICE_INIT_ERR_*`), or the negative error returned by the PHY `init`
callback.

#### Device configuration

```c
typedef struct {
    iolink_device_phy_t phy;                 /* PHY driver (copy of iolink_phy_api_t) */
    iolink_config_t stack;                   /* protocol settings */
    const iolink_app_callbacks_t* app_callbacks; /* may be NULL */
    const iolink_device_info_t* device_info;     /* may be NULL */
#if IOLINK_FEATURE_DATA_STORAGE
    const iolink_storage_api_t* storage;         /* required: read+write callbacks */
#endif
    iolink_device_time_us_fn time_us;        /* required microsecond clock (ISR-safe); NULL rejects init */
    void* user;                              /* opaque pointer for callbacks */
    uint8_t (*vendor_service)(void* user, uint16_t index, uint8_t subindex,
                              bool write, const uint8_t* input, size_t length,
                              uint8_t* output, size_t* output_length);
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
} iolink_config_t;
```

`min_cycle_time` is stored raw in 0.1 ms units and encoded per Table B.3 only
when Direct Parameter Page 1 is built.

**M-Sequence types**:
- `IOLINK_M_SEQ_TYPE_0`: 1 OD octet, no PD.
- `IOLINK_M_SEQ_TYPE_1_1`: legacy interleave; rejected by a V1.1.5 Device
  (Table A.10 NOTE 3).
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
static const iolink_device_config_t g_cfg = {
    .phy = g_phy_virtual,
    .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 3, .pd_out_len = 1},
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
int iolink_device_pd_output_read(iolink_device_ctx_t* ctx, uint8_t* data, size_t len);
```

`pd_input_update` publishes Device-to-Master process data (`valid` drives the
CKS PD-invalid bit). `pd_output_read` returns the latest Master-to-Device data.

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
BYTE mode or `rx_frame`/`tx_frame` in FRAME mode, plus `tx_done` (TX BYTE
only), `tx_busy` (TX FRAME, optional) and `int_active`. `detect_wakeup`, `set_cq_line`, `get_voltage_mv`, and
`is_short_circuit` may be NULL.

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
    void (*on_startup)(void);
    void (*on_preoperate)(void);
    void (*on_operate)(void);
    bool (*on_pd_input)(uint8_t* data, uint8_t len);   /* IOLINK_APP_PD_INPUT_CALLBACK=1 */
    void (*on_pd_output)(uint8_t* data, uint8_t len);
    void (*on_state_change)(iolink_dll_state_t state);
    void (*on_master_lost)(void);
    void (*on_master_online)(void);
} iolink_app_callbacks_t;
```

Register them through `iolink_device_config_t.app_callbacks`; there is no
separate registration function.

**Example**:

```c
static void on_operate(void) { /* ... */ }
static void on_pd_output(uint8_t* data, uint8_t len) { (void)data; (void)len; }

static const iolink_app_callbacks_t g_app = {
    .on_operate = on_operate,
    .on_pd_output = on_pd_output,
};
/* ... .app_callbacks = &g_app in iolink_device_config_t ... */
```

## Vendor ISDU service

Device-specific indices (`0x0040-0x00FE`, `0x0100-0x3FFF`) and DS access for
non-standard parameters are routed to `iolink_device_config_t.vendor_service`:

```c
uint8_t vendor_service(void* user, uint16_t index, uint8_t subindex, bool write,
                       const uint8_t* input, size_t length,
                       uint8_t* output, size_t* output_length);
```

Return `0` on success (set `*output_length` for reads) or an IO-Link
`IOLINK_ISDU_ERROR_*` additional code on failure.

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

## Data Storage API

```c
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);

void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_storage_api_t* storage);
void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx);
void iolink_ds_process(iolink_ds_ctx_t* ctx);                  /* driven by iolink_device_process */
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum);
int  iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);
int  iolink_ds_build_image(iolink_ds_ctx_t* ctx);
int  iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);
```

The DS context is initialized by `iolink_device_init()` from the required
`config.storage` backend (declared only with `IOLINK_FEATURE_DATA_STORAGE=1`);
use `iolink_device_get_ds_ctx()` to reach it.

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
    /* -8 was the lock-pair error; retired with the application thread lock. */
    IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT = -9,
    IOLINK_DEVICE_INIT_ERR_TIME = -10,      /* time_us is NULL */
    IOLINK_DEVICE_INIT_ERR_CRITICAL = -11,  /* weak critical-section default in use */
    IOLINK_DEVICE_INIT_ERR_STORAGE = -12,   /* storage NULL or read/write missing */
    /* -13 was the RTOS lock requirement; retired with the RTOS tier. */
} iolink_device_init_result_t;
```

ISDU additional codes (`IOLINK_ISDU_ERROR_*`, e.g. `IDX_NOTAVAIL` `0x11`,
`SUBINDEX_NOT_AVAIL` `0x12`, `WRITE_PROTECTED`/access denied `0x23`,
`FUNCTION_NOT_AVAIL` `0x35`) are defined in `include/iolinki/protocol.h`.

## Platform abstraction

`iolink_device_config_t::time_us` is required by `iolink_device_init()` (NULL is
rejected with `IOLINK_DEVICE_INIT_ERR_TIME`), and the stack uses it for all DLL,
ISR and event timing. There is no separate platform time API; direct DLL users
must bind `ctx->time_us`/`time_user` after `iolink_dll_init()`. Critical sections are
`iolink_critical_enter()` / `iolink_critical_exit()` from
`include/iolinki/platform.h`; `iolink_device_init()` rejects the library's weak
no-op default with `IOLINK_DEVICE_INIT_ERR_CRITICAL`.

## Build configuration

Every compile-time switch, profile and capacity override is documented in
[feature-trimming.md](feature-trimming.md). The transport granularity is selected
with `IOLINK_ISR_RX_MODE` / `IOLINK_ISR_TX_MODE` (BYTE or FRAME).

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
