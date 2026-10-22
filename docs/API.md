# API Reference

## Core API

### Initialization

```c
#include "iolinki/iolink.h"

int iolink_init(const iolink_phy_api_t *phy, const iolink_config_t *config);
```

Initialize the IO-Link stack with a PHY implementation and protocol configuration.

**Parameters**:
- `phy`: Pointer to PHY API structure
- `config`: Pointer to `iolink_config_t` structure (copied internally)

**Returns**: 0 on success, negative error code otherwise

#### Stack Configuration

```c
typedef struct {
    iolink_m_seq_type_t m_seq_type; /**< M-sequence type support */
    uint8_t min_cycle_time;         /**< Minimum cycle time (encoded per spec) */
    uint8_t pd_in_len;              /**< Process Data Input length (bytes) */
    uint8_t pd_out_len;             /**< Process Data Output length (bytes) */
} iolink_config_t;
```

**M-Sequence Types**:
- `IOLINK_M_SEQ_TYPE_0`: 1 OD octet, no PD.
- `IOLINK_M_SEQ_TYPE_1_1`: 2 PD octets, no OD (legacy interleave; rejected by
  a V1.1.5 Device, Table A.10 NOTE 3).
- `IOLINK_M_SEQ_TYPE_1_2`: 2 OD octets, no PD (Figure A.7).
- `IOLINK_M_SEQ_TYPE_1_V`: 8 or 32 OD octets, no PD (Figure A.8).
- `IOLINK_M_SEQ_TYPE_2_1`: 1 PD-in + 1 OD, no PD-out (Figure A.9).
- `IOLINK_M_SEQ_TYPE_2_2`: 2 PD-in + 1 OD, no PD-out (Figure A.10).
- `IOLINK_M_SEQ_TYPE_2_3`: 1 PD-out + 1 OD, no PD-in (Figure A.11).
- `IOLINK_M_SEQ_TYPE_2_4`: 2 PD-out + 1 OD, no PD-in (Figure A.12).
- `IOLINK_M_SEQ_TYPE_2_5`: 1 PD-in + 1 PD-out + 1 OD (Figure A.13).
- `IOLINK_M_SEQ_TYPE_2_V`: variable PD (0..32) + 1/2/8/32 OD (Figure A.14).

The fixed PD/OD layout is enforced at `iolink_device_init()` in the ISR build;
a mismatch returns `IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT`.

**Example**:
```c
extern const iolink_phy_api_t g_phy_virtual;

int main(void) {
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_2,
        .pd_in_len = 2,
        .pd_out_len = 2
    };

    if (iolink_init(&g_phy_virtual, &config) != 0) {
        // Handle error
        return -1;
    }

    while (1) {
        iolink_process();
        // Sleep 1ms
    }
}
```

### Main Loop

```c
void iolink_process(void);
```

Process IO-Link stack logic. Must be called periodically (e.g., every 1ms).

## PHY Layer API

### PHY API Structure

Copy `iolink_phy_api_t` from `include/iolinki/phy.h`. Every callback takes `void* user` first. `init`, `set_mode`, and `set_baudrate` are required. `send` returns the number of bytes sent, or a negative error. `recv_byte` returns 1 when a byte was read, 0 when none is waiting, or a negative error. `detect_wakeup`, `set_cq_line`, `get_voltage_mv`, and `is_short_circuit` may be NULL.

`IOLINK_BAUDRATE_COM1`, `COM2`, and `COM3` are 0, 1, and 2. They select 4.8, 38.4, and 230.4 kbit/s. They are not the baud number itself.

```c
static int my_phy_send(void* user, const uint8_t* data, size_t len) {
    (void)user;
    (void)data;
    return (int)len;
}

static int my_phy_recv_byte(void* user, uint8_t* byte) {
    (void)user;
    (void)byte;
    return 0;
}
```

## Application Layer API

### Callbacks

```c
typedef struct {
    void (*on_startup)(void);
    void (*on_preoperate)(void);
    void (*on_operate)(void);
    bool (*on_pd_input)(uint8_t *data, uint8_t len); /* fill PD-in; false = invalid */
    void (*on_pd_output)(uint8_t *data, uint8_t len);
} iolink_app_callbacks_t;
```

### Registration

```c
void iolink_app_register(const iolink_app_callbacks_t *callbacks);
```

**Example**:
```c
static void on_operate(void) {
    printf("Device entered OPERATE state\n");
}

static bool on_pd_input(uint8_t *data, uint8_t len) {
    // Fill the input process data (Device -> Master)
    (void)data;
    (void)len;
    return true;
}

const iolink_app_callbacks_t app_callbacks = {
    .on_operate = on_operate,
    .on_pd_input = on_pd_input
};

int main(void) {
    iolink_app_register(&app_callbacks);
    iolink_init(&g_phy_virtual, NULL); /* NULL = default configuration */
    // ...
}
```

## ISDU API

### Reading ISDU

```c
int iolink_isdu_read(uint16_t index, uint8_t subindex,
                     uint8_t *data, uint8_t *len);
```

**Parameters**:
- `index`: ISDU index (0-65535)
- `subindex`: ISDU subindex (0-255)
- `data`: Buffer for data
- `len`: Input: buffer size, Output: actual data length

**Returns**: 0 on success, negative error code otherwise

### Writing ISDU

```c
int iolink_isdu_write(uint16_t index, uint8_t subindex,
                      const uint8_t *data, uint8_t len);
```

### ISDU Handler Registration

```c
typedef int (*iolink_isdu_handler_t)(uint16_t index, uint8_t subindex,
                                     uint8_t *data, uint8_t *len,
                                     bool is_write);

void iolink_isdu_register_handler(uint16_t index,
                                   iolink_isdu_handler_t handler);
```

**Example**:
```c
static int vendor_name_handler(uint16_t index, uint8_t subindex,
                                uint8_t *data, uint8_t *len,
                                bool is_write) {
    if (is_write) {
        return -1; // Read-only
    }

    const char *vendor = "MyCompany";
    *len = strlen(vendor);
    memcpy(data, vendor, *len);
    return 0;
}

void app_init(void) {
    iolink_isdu_register_handler(0x0010, vendor_name_handler);
}
```

## Event API

### Triggering Events

```c
void iolink_event_trigger(uint16_t event_code, iolink_event_type_t type);
```

**Event Types**:
```c
typedef enum {
    IOLINK_EVENT_TYPE_NOTIFICATION = 0,
    IOLINK_EVENT_TYPE_WARNING = 1,
    IOLINK_EVENT_TYPE_ERROR = 2
} iolink_event_type_t;
```

**Example**:
```c
// Trigger temperature warning
iolink_event_trigger(0x5012, IOLINK_EVENT_TYPE_WARNING);
```

### Checking Event Status

```c
bool iolink_events_pending(void);
```

## Data Storage API

### Initialization

```c
typedef struct {
    int (*load)(uint8_t *data, uint16_t *len);
    int (*store)(const uint8_t *data, uint16_t len);
} iolink_ds_storage_api_t;

void iolink_ds_init(const iolink_ds_storage_api_t *storage);
```

### Checksum Validation

```c
void iolink_ds_check(uint16_t master_checksum);
```

## Error Codes

```c
#define IOLINK_ERR_INVALID_PARAM    -1
#define IOLINK_ERR_NOT_READY        -2
#define IOLINK_ERR_TIMEOUT          -3
#define IOLINK_ERR_CRC              -4
#define IOLINK_ERR_BUSY             -5
#define IOLINK_ERR_NOT_SUPPORTED    -6
```

## Platform Abstraction

### Time API

```c
uint32_t iolink_time_get_ms(void);
uint64_t iolink_time_get_us(void);
```

Implement these functions for your platform in `src/platform/<platform>/time_utils.c`.

## Build Configuration

### CMake Options

```cmake
option(IOLINK_PLATFORM "Target platform" "LINUX")
option(IOLINK_ENABLE_TESTS "Enable unit tests" ON)
option(IOLINK_ENABLE_EXAMPLES "Enable examples" ON)
```

### Compile-Time Configuration

Compile-time buffer sizes are defined in `include/iolinki/config.h` and may be
overridden by defining them before the header is included (each is guarded with
`#ifndef`):
```c
#define IOLINK_PD_IN_MAX_SIZE   32   /* Max input Process Data bytes */
#define IOLINK_PD_OUT_MAX_SIZE  32   /* Max output Process Data bytes */
#define IOLINK_ISDU_BUFFER_SIZE 256  /* ISDU request/response buffer */
#define IOLINK_EVENT_QUEUE_SIZE 4    /* Event FIFO depth */
```

## Thread Safety

**Current Status**: Not thread-safe. All API calls must be from the same thread or protected by mutex.

**Future**: Context-based API will enable multi-instance support.
