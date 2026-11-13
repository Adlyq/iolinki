# Persistent Storage and Data Storage Parameters

This note explains how application data is persisted and how the Data Storage
(DS) parameter set is read/written.

## 1. One storage backend, two regions

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
- A `write` must complete before returning, or copy the buffer if it queues.

The region layout is fixed at compile time (`storage.h`):

| Region | Offset | Size |
|---|---|---|
| parameter image | `IOLINK_STORAGE_PARAMS_BASE` (0) | `IOLINK_STORAGE_PARAMS_SIZE` |
| DS upload flag | `IOLINK_STORAGE_FLAG_BASE` | `IOLINK_STORAGE_FLAG_SIZE` |

Use `IOLINK_STORAGE_TOTAL_SIZE` for a static assertion in the application:

```c
_Static_assert(sizeof(nvm_region) >= IOLINK_STORAGE_TOTAL_SIZE, "NVM too small");
```

The parameter image carries `magic + version + length + Fletcher-32`, followed
by the same `[Index(2)][Subindex(1)][Length(1)][Data]` records the DS set uses.
A mismatch or corruption falls back to table defaults.

There is no stored Data Storage image: `Data_Storage_Size`,
`Parameter_Checksum` and the upload payload are regenerated from the current
values. Only values and the `DS_UPLOAD_FLAG` occupy NVM.

## 2. One flag controls persistence

Each object dictionary entry has a `storage` bitmask:

| Bit | Meaning |
|---|---|
| `IOLINK_OD_STORAGE_VOLATILE` (0) | never persisted, not in the DS set |
| `IOLINK_OD_STORAGE_DS` | member of the Data Storage parameter set |
| `IOLINK_OD_STORAGE_PERSIST` | value is written to the parameter image |

The two bits combine freely:

- `DS | PERSIST` — normal parameter (master backup + local NVM).
- `DS` — RAM-only parameter; the master restores it after a reboot (a device
  without NVM uses this for everything by leaving `config.storage` NULL).
- `PERSIST` — locally stored but not part of the master's backup set.

A `WRITE_STORE` write to a `PERSIST` entry serializes the whole persisted set
once and writes it through the backend. If that write fails, the ISDU write
answers with an error and a STORAGE scalar is rolled back to its previous value
(OctetString content is not rolled back; reject those in `validate`).

## 3. Customising the DS / persistence set

Declare the parameter; the bits do the rest:

```c
static uint8_t g_vendor_param[8];
static uint8_t g_vendor_param_len;

static const iolink_od_entry_t my_entries[] = {
    IOLINK_OD_BYTES_STORAGE(0x0050, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_STORE,
                             IOLINK_OD_STORAGE_DS | IOLINK_OD_STORAGE_PERSIST, 8,
                             "VendorParam", g_vendor_param, &g_vendor_param_len),
};
```

- `block_len` bounds the value stored in the image for that record (0 =
  unbounded).
- Values are read and written through the same table-entry backend that serves
  ISDU, so DS and persistence need no separate accessor pair.
- A restore runs the table entry's `validate` hook for every known record
  before writing any of them; a rejected record aborts the whole restore.
- The three tags are always DS members because the standard table declares them
  `DS | PERSIST`. To publish an empty `Index_List` a device needs a build
  without `IOLINK_FEATURE_DATA_STORAGE` (see `docs/feature-trimming.md`).

## 4. When NVM is written

| Trigger | Region written | Notes |
|---|---|---|
| Any `WRITE_STORE` write to a `PERSIST` entry | parameter image | One image write for the whole persisted set; a failure answers with an error and rolls back a STORAGE scalar |
| Factory reset / Back-to-box | parameter image | Tags are cleared, then every `PERSIST` entry is restored from its table default; one image write |
| DS download finished | parameter image | Every applied value is persisted once after the batch, not per record |
| DS upload requested | DS_UPLOAD_FLAG (`1`) | Flag survives reboot so the master sees a pending upload |
| DS upload/download finished | DS_UPLOAD_FLAG (`0`) | Also cleared on factory reset |
| Boot | reads the parameter image and the flag | `magic + version + Fletcher-32` mismatch means "use defaults"; values are restored without running hooks |

Application-local value changes (writes to a STORAGE variable outside ISDU) are
not observed by the stack. Call `iolink_ds_save_persistent()` after such a
change if it must survive a reboot.
