# Feature trimming

`iolinki` has **two independent trimming layers**. Mixing them up is the usual
source of "why did my build not behave as configured" reports.

1. **Compile-time structural switches** change struct layout, ISR granularity,
   concurrency, statistics layout or whether a subsystem is linked at all.
   They cannot be changed at run time.
2. **Runtime capability bits** select behaviour (SystemCommand, wake-up
   detection, SIO C/Q, PD-input callback, ISR-TX-only, protocol compatibility,
   event timing, timing enforcement, access locks). They are resolved once per
   instance from three inputs:
   - `iolink_build_caps_t` — what the image was compiled with,
   - `iolink_stack_config_t` — per-instance overrides
     (`compat_flags`, `flags`, `timing_enforce`,
     `event_min_appear_ms`, `event_min_repeat_ms`),
   - the bound PHY vtable — hardware facts.

   `iolink_caps_resolve()` is the single resolution point
   (`include/iolinki/capability.h`). Read the result with
   `iolink_device_capabilities()`; override the mutable bits with
   `iolink_device_set_capabilities()`. Hardware bits (RX/TX, wake-up, SIO C/Q,
   `request_service`) cannot be forged: `resolve()` re-probes the vtable on
   every override. A capability the image was not built with is masked out
   (the bit reads 0) instead of failing init; the reachable init checks are
   `REQUIRE_ISR_TX` without `request_service()` and TX-FRAME+TX_BUSY without
   the hook, both `IOLINK_DEVICE_INIT_ERR_PHY`.

## Compile-time switches (retained)

| Macro | Default | Trimmed to 0 |
| --- | --- | --- |
| `IOLINK_FEATURE_DATA_STORAGE` | 1 | `src/data_storage.c` body and the `ds` context field are removed; index `0x0003` and the DS SystemCommands answer `FUNC_NOTAVAIL`/`IDX_NOTAVAIL`. The `iolink_ds_*` symbols stay exported: the reduction is a **link-time contract** (negative link probes in `tools/check_profile_abi.sh`) |
| `IOLINK_FEATURE_EVENTS` | 1 | `src/events.c` is replaced by inert stubs (no queue, no classification); `0x0025` DetailedDeviceStatus answers `IDX_NOTAVAIL`. Same link-time contract |
| `IOLINK_FEATURE_VALIDATE` | 1 | Compiles the init-time OD self-check (`od_validate_app`); `IOLINK_DEVICE_INIT_ERR_OD` is only produced when enabled. A `.text`-only switch, no link-time contract |
| `IOLINK_ISR_RX_MODE` / `IOLINK_ISR_TX_MODE` | BYTE/BYTE | FRAME transports: different PHY vtable and ISR buffering |
| `IOLINK_PHY_FEATURE_TX_BUSY` | 0 | TX-FRAME line-state hook (two vs three PD-in slots) |
| `IOLINK_CONCURRENCY` | ISR | SINGLE / SMP synchronisation primitives |
| `IOLINK_STATS_MODE` | `IOLINK_STATS_SHARED` | `IOLINK_STATS_SPLIT`: per-context counter sets (+16 B) |

The matrix uses six structural configurations, defined once in
`tools/matrix_configs.sh`:

| # | Name | ISR | Concurrency/stats | Trim | Platform |
|---|---|---|---|---|---|
| C1 | `host_default` | BYTE/BYTE | ISR+COMPILER/SHARED | full | LINUX Debug |
| C1r | `host_default_release` | BYTE/BYTE | ISR/SHARED | full | LINUX Release |
| C2 | `host_frame` | FRAME/FRAME | ISR/SHARED | full | LINUX Debug |
| C3 | `host_no_ds` | BYTE/BYTE | ISR/SHARED | no DS | LINUX Debug |
| C4 | `host_no_events` | BYTE/BYTE | ISR/SHARED | no events | LINUX Debug |
| C5 | `target_minimal` | BYTE/BYTE | SINGLE/SHARED | no DS, no events | BAREMETAL Release |
| C6 | `host_smp_split` | BYTE/BYTE | SMP/SMP barrier/SPLIT | full | LINUX Debug |

The full unit suite runs on C1, C1r, C2 and C6. C3/C4 run
`tests/test_trimmed_contract.c`, which asserts the replies a trimmed stack still
owes a master (missing DS index/commands, `0x0025` -> `IDX_NOTAVAIL`); C5 is
compile-only and the remaining cross-combinations are syntax-only probes.

## Runtime capability bits

| Capability | Resolved from | Override |
| --- | --- | --- |
| `IOLINK_CAP_SYSTEM_COMMAND` | build profile | `stack.flags` / `iolink_device_set_capabilities()` |
| `IOLINK_CAP_WAKEUP` | PHY vtable (`detect_wakeup`) | hardware bound |
| `IOLINK_CAP_SIO_CQ` | PHY vtable (`set_cq_line`) | hardware bound |
| `IOLINK_CAP_PD_IN_CALLBACK` | `app->on_pd_input` | application |
| `IOLINK_CAP_REQUIRE_ISR_TX` | build profile | `stack.flags` + `phy->request_service` |
| `IOLINK_CAP_COMPAT_V10_OP` | `compat_defaults` | `stack.compat_flags` |
| `IOLINK_CAP_COMPAT_T0_READ` | `compat_defaults` | `stack.compat_flags` |
| `IOLINK_CAP_DS_LEGACY_SUB0` | `compat_defaults` | `stack.compat_flags` |
| event timing | build profile | `stack.event_min_appear_ms` / `_repeat_ms` (0 = build default) |
| timing enforcement | build profile | `stack.timing_enforce` (`DEFAULT` = build profile) |
| access locks | manifest | `IOLINK_CAP_LOCK_*` |

All of them are resolved once per instance by `iolink_caps_resolve()`; no
compile-time switch selects a behaviour.

## CMake options

| Option | Effect |
| --- | --- |
| `-DIOLINK_FEATURE_DATA_STORAGE=OFF` | Structural removal of DS (link-time contract) |
| `-DIOLINK_FEATURE_EVENTS=OFF` | Structural removal of the event engine (link-time contract) |
| `-DIOLINK_ISR_RX_MODE=...` / `-DIOLINK_ISR_TX_MODE=...` | Transport granularity (CMake cache variables; exported to consumers because they change struct layout) |
| `-DIOLINK_CONCURRENCY=...` | ISR / SINGLE / SMP |
| `-DIOLINK_STATS_MODE=...` | `IOLINK_STATS_SHARED` / `IOLINK_STATS_SPLIT` |
| `-DIOLINK_PHY_FEATURE_TX_BUSY=ON` | TX-FRAME `tx_busy()` hook |

## Capacity overrides

These are not feature switches; they size the static buffers and are validated by
`tests/test_config_trim.c`:

- `IOLINK_ISDU_BUFFER_SIZE` (default 256)
- `IOLINK_EVENT_QUEUE_SIZE` (default 4)
- `IOLINK_PD_IN_MAX_SIZE` / `IOLINK_PD_OUT_MAX_SIZE` (default 32)
- `IOLINK_OD_MAX_SIZE` (default 32)
- `IOLINK_DS_IMAGE_MAX` (default 256)
- `IOLINK_MBX_SLOTS` (default 4; power of two in `[2, 128]`) — frames the data
  ISR may queue for the main pump
- `IOLINK_REPLY_PLAN_SLOTS` (default 48) — reply-plan entries the main pump
  may publish to the ISR (four state slices share one table); overflow is
  counted in `reply_plan_overflows`

`IOLINK_TX_SLOTS` is derived (`IOLINK_TX_ISR_SLOT + 1`) and must not be
overridden on its own. The `capacity_overrides` probe in
`tools/matrix_configs.sh` compiles a build with the slot counts changed, so the
`#ifndef` guards are exercised outside the unit test too.

Since P8 the ISDU buffers live in `iolink_workspace_t` (1033 B at the default
256-byte buffer); lowering `IOLINK_ISDU_BUFFER_SIZE` shrinks the workspace, not
the context (see docs/MEMORY_GUIDE.md).

## Caveats

- `DataStorageIndex` (index `0x0003`) is mandatory (Table B.8). Trimming Data
  Storage removes it, so the device is non-conformant. The DS set always
  contains the three standard tags; an empty `Index_List` is only available by
  omitting `IOLINK_FEATURE_DATA_STORAGE` entirely, which has the same
  non-conformance caveat.
- `SystemCommand` (index `0x0002`) is conditional (Table B.8, B.2.2) and is now
  a runtime capability. Disabling it is conformant only when no conveyed
  functionality requires it: block parameterization needs `0x01`-`0x06` (all or
  none), and a non-empty DS `Index_List` requires `0x05`
  (ParamDownloadStore). `Device reset` and `Restore factory settings` are
  optional and `Application reset` is highly recommended (H), not mandatory.
- `IOLINK_PHY_FEATURE_*` macros that became runtime bits are no-ops; the PHY
  vtable is the source of truth.
- Verification: structural configurations are built by
  `tools/build_matrix.sh`; the byte-exact differential gate
  (`tools/diff_matrix.sh`) compares `host_default` against
  `host_default_release`; demoted switches are covered by expected-frame tests
  in the default suite (`tests/test_capability.c`,
  `tests/test_system_commands.c`, ...).
