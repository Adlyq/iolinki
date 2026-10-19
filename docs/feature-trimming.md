# Feature trimming

`include/iolinki/config.h` exposes a small set of compile-time feature switches.
Every switch defaults to `1` (full stack) and can be overridden with `-D...=0`
or a private configuration header.

The exact behavior matters: a trimmed feature can either remove code entirely,
replace it with an inert stub, or answer a negative ISDU response. The table
below is the contract.

| Macro | Default | Trimmed to 0 |
| --- | --- | --- |
| `IOLINK_FEATURE_DATA_STORAGE` | 1 | `src/data_storage.c` body and the `ds` context field are removed; index `0x0003` and the DS SystemCommands answer `FUNC_NOTAVAIL`/`IDX_NOTAVAIL` |
| `IOLINK_FEATURE_SYSTEM_COMMAND` | 1 | `handle_system_command()` is compiled to reject every command with `FUNC_NOTAVAIL` |
| `IOLINK_FEATURE_LEGACY_PARAMS` | 1 | the process-wide `g_legacy_*` parameter set and the context-less `iolink_params_*` API are removed; the ISDU fallback returns an error |
| `IOLINK_FEATURE_EVENTS` | 1 | `src/events.c` is replaced by inert stubs (no queue, no classification); DetailedDeviceStatus reports no events |
| `IOLINK_PHY_FEATURE_WAKEUP` | 1 | `detect_wakeup` is removed from the PHY vtable; the DLL treats the PHY as owning the line state (starts in SDCI) |
| `IOLINK_PHY_FEATURE_SIO_CQ` | 1 | `set_cq_line` is removed from the PHY vtable |
| `IOLINK_PHY_FEATURE_LEGACY_DIAG` | 1 | `get_voltage_mv` / `is_short_circuit` and the DLL diagnostic polling are removed |

`IOLINK_FEATURE_MASTER_IDENT` was removed: the MasterCommand ident subset was
never implemented, so the switch had no effect.

## IOLINK_MINIMAL_PROFILE

Defining `IOLINK_MINIMAL_PROFILE` selects the trimmed defaults (only when the
individual macro is not already defined):

- `IOLINK_FEATURE_DATA_STORAGE = 0`
- `IOLINK_FEATURE_SYSTEM_COMMAND = 0`
- `IOLINK_FEATURE_LEGACY_PARAMS = 0`
- `IOLINK_PHY_FEATURE_WAKEUP = 0`
- `IOLINK_PHY_FEATURE_SIO_CQ = 0`
- `IOLINK_PHY_FEATURE_LEGACY_DIAG = 0`

`IOLINK_FEATURE_EVENTS` is intentionally **not** part of the profile (events are
kept by default); define it explicitly to remove the event engine as well.

CMake:

```bash
cmake -B build_min -DIOLINK_MINIMAL_PROFILE=ON -DBUILD_TESTING=OFF
cmake --build build_min --target iolinki
```

Individual features can be overridden in the same build:

```bash
cmake -B build_min -DIOLINK_MINIMAL_PROFILE=ON \
  -DCMAKE_C_FLAGS="-DIOLINK_FEATURE_EVENTS=0" -DBUILD_TESTING=OFF
```

## Capacity overrides

These are not feature switches; they size the static buffers and are validated by
`tests/test_config_trim.c`:

- `IOLINK_ISDU_BUFFER_SIZE` (default 256)
- `IOLINK_EVENT_QUEUE_SIZE` (default 4)
- `IOLINK_PD_IN_MAX_SIZE` / `IOLINK_PD_OUT_MAX_SIZE` (default 32)
- `IOLINK_OD_MAX_SIZE` (default 32)
- `IOLINK_DS_IMAGE_MAX` (default 256)

Note that the ISR two-pump transport adds a per-context ISDU response snapshot
(`isr_isdu_resp[IOLINK_ISDU_BUFFER_SIZE]`); lowering `IOLINK_ISDU_BUFFER_SIZE`
also lowers that RAM cost.

## Caveats

- Trimming SystemCommand or Data Storage makes the device non-conformant to the
  corresponding mandatory/optional IO-Link objects. The library answers a
  negative response rather than silently accepting the request.
- `IOLINK_PHY_FEATURE_*` must match the PHY implementation you compile: a PHY
  that still initializes a removed vtable field does not build.
- The minimal profile is verified by building the `iolinki` target with
  `-DIOLINK_MINIMAL_PROFILE=ON`. The full unit suite runs against the default
  (all features enabled) build.
