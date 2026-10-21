# Persistent Storage and Data Storage Parameters

This note explains how application data is persisted and how the Data Storage
(DS) parameter set is read/written.

## 1. One storage backend

The stack talks to NVM through exactly one callback-only backend supplied in
`iolink_device_config_t::storage`:

```c
typedef struct {
    void* user;
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
    int (*erase)(void* user, uint32_t offset, size_t len);   /* may be NULL */
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

The device binds a default accessor pair:

1. standard parameters (0x0018/0x0019/0x001A) through the params context;
2. everything else (vendor 0x0040+ / 0x0100+) through `vendor_service`.

So **if a parameter is already served by the params context or
`vendor_service`, DS can read/write it without any extra callback.**

## 3. Customising the DS parameter set

Only the index list usually needs to change:

```c
static const iolink_ds_param_desc_t my_ds[] = {
    { IOLINK_IDX_APPLICATION_TAG, 0, 0 },  /* max_len 0 = unbounded */
    { 0x0050, 0, 8 },                      /* vendor param, handled by vendor_service */
};

iolink_ds_set_param_desc(iolink_device_get_ds_ctx(dev), my_ds,
                         sizeof(my_ds) / sizeof(my_ds[0]));
```

- `iolink_ds_set_param_desc()` replaces only the list and keeps the current
  accessors and user pointer.
- `iolink_ds_bind_param_access(read, write, user, desc, count)` also installs
  custom accessors. Passing `NULL` for `read`/`write` keeps the current ones and
  their `user`.
- `iolink_ds_param_desc_t::max_len` (0 = unbounded) bounds the value stored in
  the image for that record.

Provide custom accessors only when a parameter does not live in the params
context or `vendor_service` (e.g. read directly from a sensor):

```c
int my_ds_read(void* user, uint16_t index, uint8_t subindex, uint8_t* out, size_t cap);
int my_ds_write(void* user, uint16_t index, uint8_t subindex,
                const uint8_t* data, size_t len, bool persist);
```

## 4. What is persisted by the stack

- the params record (three tags) on `persist` writes and parameter changes;
- the DS image (magic + version + length + checksum + data) after a download or
  a parameter change;
- the DS_UPLOAD_FLAG (one byte, `1` = pending).
