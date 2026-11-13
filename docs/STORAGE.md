# Persistent Storage and Data Storage Parameters

This note explains how application data is persisted and how the Data Storage
(DS) parameter set is read/written.

## 1. One storage backend

When `IOLINK_FEATURE_DATA_STORAGE` is enabled (the default), the stack talks to
NVM through exactly one callback-only backend supplied in
`iolink_device_config_t::storage`. The member is only declared for that feature
and is required by `iolink_device_init()`; NULL or missing `read`/`write`
callbacks are rejected with `IOLINK_DEVICE_INIT_ERR_STORAGE`. With Data Storage
compiled out, the member is absent and parameters are RAM-only.

```c
typedef struct {
    void* user;
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
} iolink_storage_api_t;
```

- `offset` is **relative** to the application's storage area; add the physical
  base inside the callbacks (`eeprom_read(BASE + offset, ...)`).
- Return `0` on success, negative on failure.
- A `write` must complete synchronously or copy the buffer before returning.

The region layout is fixed at compile time (`storage.h`):

| Region | Offset | Size |
|---|---|---|
| params | `IOLINK_STORAGE_PARAMS_BASE` (0) | `IOLINK_STORAGE_PARAMS_SIZE` |
| DS image | `IOLINK_STORAGE_DS_BASE` | `IOLINK_STORAGE_DS_SIZE` |
| DS flag | `IOLINK_STORAGE_FLAG_BASE` | `IOLINK_STORAGE_FLAG_SIZE` |

Use `IOLINK_STORAGE_TOTAL_SIZE` for a static assertion in the application:

```c
_Static_assert(sizeof(nvm_region) >= IOLINK_STORAGE_TOTAL_SIZE, "NVM too small");
```

Each record carries `magic + version` (layout) and a Fletcher-32 checksum
(content integrity). A mismatch/corruption falls back to defaults.

## 2. How DS parameters are read

The device reads and writes DS records through the object dictionary:

1. standard parameters (0x0018/0x0019/0x001A) through the params context;
2. device-specific parameters (0x0040+ / 0x0100+) through the table entry's
   `STORAGE` variable or `HANDLER` callbacks.

So **if a parameter is declared in the application object dictionary with
`IOLINK_OD_STORAGE_DS`, DS reads and writes it without any extra callback.**

## 3. Customising the DS parameter set

The DS set is derived from the object dictionary: every standard or
application entry whose `storage` is `IOLINK_OD_STORAGE_DS` is serialized, in
standard-table order followed by application-table order. Declaring the
parameter is the only step:

```c
static uint8_t g_vendor_param[8];
static uint8_t g_vendor_param_len;

static const iolink_od_entry_t my_entries[] = {
    IOLINK_OD_STRING_STORAGE(0x0050, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_DS, 8, "VendorParam", g_vendor_param,
                             &g_vendor_param_len),
};
```

- `block_len` bounds the value stored in the image for that record (0 =
  unbounded).
- Values are read and written through the same table-entry backend that serves
  ISDU, so DS needs no separate accessor pair.
- A restore runs the table entry's `validate` hook for every known record
  before writing any of them; a rejected record aborts the whole restore.
- The three tags are always members because the standard table declares them
  `IOLINK_OD_STORAGE_DS`. To publish an empty `Index_List` a device needs a
  build without `IOLINK_FEATURE_DATA_STORAGE` (see `docs/feature-trimming.md`).

## 4. What is persisted by the stack

| Trigger | Region written | Notes |
|---|---|---|
| Tag write (0x0018/0x0019/0x001A, `WRITE_STORE`) | params record | A failed write is reported as `IOLINK_RESULT_IO` and leaves RAM/read-back unchanged |
| Factory reset / Back-to-box | params record | Unconditional; a storage failure is ignored because the RAM reset already happened |
| DS download finished | params record per stored parameter, then the DS image | Every `WRITE_STORE` parameter (including the three tags) is persisted once by the OD write and once more inside the image |
| DS upload requested | DS_UPLOAD_FLAG (`1`), then the DS image | Flag survives reboot so the master sees a pending upload |
| DS upload/download finished | DS_UPLOAD_FLAG (`0`) | Also cleared on factory reset |
| Boot | reads the params record and the DS image | `magic + version + Fletcher-32` mismatch falls back to defaults / an invalid image |

The params record and the DS image can both hold the same tag values; that
double write is a known redundancy and will be removed when both are serialized
from the object dictionary.

The DS image layout is `magic(4) + version(2) + length(2) + checksum(4) +
records`, where a record is `[Index(2)][Subindex(1)][Length(1)][Data]`.
