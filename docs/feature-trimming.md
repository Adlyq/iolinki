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
   `iolink_device_capabilities()`. Capabilities are defined **once, at
   initialisation**: there is no runtime override, because the reply-plan table
   is compiled from them and never rebuilt. Ask for different switches through
   `iolink_stack_config_t` (`compat_flags`, `flags`) and re-initialise.
   The four DeviceAccessLocks bits are not configurable: the library implements
   all four, so `isdu_locks_implemented()` is always the full mask.
   Hardware bits (RX/TX, wake-up, SIO C/Q, `request_service`) come from the
   vtable only. A capability the image was not built with is masked out
   (the bit reads 0) instead of failing init; the reachable init checks are
   `REQUIRE_ISR_TX` without `request_service()`, a TX-capable PHY without
   `request_service()` under the `ISR`/`SMP` profiles, and TX-FRAME+TX_BUSY
   without the hook, all `IOLINK_DEVICE_INIT_ERR_PHY`.

## Compile-time switches (retained)

| Macro | Default | Trimmed to 0 |
| --- | --- | --- |
| `IOLINK_FEATURE_DATA_STORAGE` | 1 | `src/data_storage.c` body and the `ds` context field are removed; index `0x0003` and the DS SystemCommands answer `FUNC_NOTAVAIL`/`IDX_NOTAVAIL`. The `iolink_ds_*` symbols stay exported: the reduction is a **link-time contract** (negative link probes in `tools/check_profile_abi.sh`) |
| `IOLINK_FEATURE_EVENTS` | 1 | `src/events.c` is replaced by inert stubs (no queue, no classification); `0x0025` DetailedDeviceStatus answers `IDX_NOTAVAIL`. Same link-time contract |
| `IOLINK_FEATURE_VALIDATE` | 1 | Compiles the init-time OD self-check (`od_validate_app`); `IOLINK_DEVICE_INIT_ERR_OD` is only produced when enabled. A `.text`-only switch, no link-time contract |
| `IOLINK_WORKSPACE_EMBEDDED` | 1 | 0 removes the embedded ISDU/DS scratch; the application must provide a workspace, otherwise initialisation returns `IOLINK_DEVICE_INIT_ERR_WORKSPACE` |
| `IOLINK_ISR_RX_MODE` / `IOLINK_ISR_TX_MODE` | BYTE/BYTE | FRAME transports: different PHY vtable and ISR buffering |
| `IOLINK_PHY_FEATURE_TX_BUSY` | 0 | TX-FRAME line-state hook (two vs three PD-in slots, so the main pump still has a slot to fill) |
| `IOLINK_PHY_FEATURE_ISR_TX_ONLY` | 0 | 只提供 `IOLINK_STACK_FLAG_REQUIRE_ISR_TX` 的构建期默认值；实际作用点是该运行期位 |
| `IOLINK_FEATURE_SYSTEM_COMMAND` | 1 | 只提供 `IOLINK_CAP_SYSTEM_COMMAND` 的构建期默认值；实例可用 `iolink_stack_config_t::flags` 关闭 |
| `IOLINK_CONCURRENCY` | ISR | SINGLE / SMP synchronisation primitives |
| `IOLINK_STATS_MODE` | `IOLINK_STATS_SHARED` | `IOLINK_STATS_SPLIT`: per-context counter sets. `iolink_dll_shared_t` grows by 12 B (three `uint32_t`), while `sizeof(iolink_device_ctx_t)` grows by 8 B because existing padding absorbs the rest (host and Cortex-M0+ layouts; see `tests/test_ctx_sizes.c`) |

The matrix uses five structural configurations, defined once in
`tools/matrix_configs.sh`, one per axis that can break the library:

| # | Name | ISR | Concurrency/stats | Trim | Platform |
|---|---|---|---|---|---|
| C1 | `host_default` | BYTE/BYTE | ISR+COMPILER/SHARED | full | LINUX Debug |
| C2 | `host_frame` | FRAME/FRAME | ISR/SHARED | full | LINUX Debug |
| C3 | `host_smp_split` | BYTE/BYTE | SMP/SMP barrier/SPLIT | full | LINUX Debug, `IOLINK_LOG_ENABLE_ISR=1` |
| C4 | `host_trimmed` | BYTE/BYTE | ISR/SHARED | no DS, no events | LINUX Debug |
| C5 | `target_minimal` | BYTE/BYTE | SINGLE/SHARED | no DS, no events | BAREMETAL Release |

The full unit suite runs on C1, C2 and C3. C4 runs
`tests/test_trimmed_contract.c`, which asserts the replies a trimmed stack still
owes a master (missing DS index/commands, `0x0025` -> `IDX_NOTAVAIL`); C5 is
compile-only and the remaining cross-combinations are syntax-only probes.
Trimming both subsystems in one configuration keeps the row count at five; the
individual `IOLINK_FEATURE_*` switches are still compiled by their probes.

## Runtime capability bits

| Capability | Resolved from | Override |
| --- | --- | --- |
| `IOLINK_CAP_SYSTEM_COMMAND` | build profile | `stack.flags` (init only) |
| `IOLINK_CAP_WAKEUP` | PHY vtable (`detect_wakeup`) | hardware bound |
| `IOLINK_CAP_SIO_CQ` | PHY vtable (`set_cq_line`) | hardware bound |
| `IOLINK_CAP_PD_IN_CALLBACK` | `app->on_pd_input` | application |
| `IOLINK_CAP_REQUIRE_ISR_TX` | build profile | `stack.flags` + `phy->request_service` |
| `IOLINK_CAP_COMPAT_V10_OP` | `compat_defaults` | `stack.compat_flags` |
| `IOLINK_CAP_COMPAT_T0_READ` | `compat_defaults` | `stack.compat_flags` |
| `IOLINK_CAP_DS_LEGACY_SUB0` | `compat_defaults` | `stack.compat_flags` |
| event timing | build profile | `stack.event_min_appear_ms` / `_repeat_ms` (0 = build default) |
| timing enforcement | build profile | `stack.timing_enforce` (`DEFAULT` = build profile) |
| access locks | manifest | fixed: all four are implemented |

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

- `IOLINK_ISDU_BUFFER_SIZE` (default 256) — must hold the largest string plus
  framing margin: raw octet transport adds 6 bytes to a request and 3 bytes to
  a response. ISDU write requests reuse this buffer, so there is no separate
  segment cap.
- `IOLINK_EVENT_QUEUE_SIZE` (default 4)
- `IOLINK_EVENT_PENDING_MAX` (default 8) — concurrent event lifecycle records
- `IOLINK_PD_IN_MAX_SIZE` / `IOLINK_PD_OUT_MAX_SIZE` (default 32)
- `IOLINK_OD_MAX_SIZE` (default 32)
- `IOLINK_DS_IMAGE_MAX` (default 256)
- `IOLINK_MBX_SLOTS` (default 4; power of two in `[2, 128]`) — frames the data
  ISR may queue for the main pump; a full mailbox drops the frame and counts
  `isr_mailbox_drops`
- `IOLINK_REPLY_PLAN_SLOTS` (default 48) — reply-plan entries the main pump
  may publish to the ISR (four state slices share one table); overflow is
  counted in `reply_plan_overflows`

`IOLINK_TX_SLOTS` is derived (`IOLINK_TX_ISR_SLOT + 1`) and must not be
overridden on its own. The `capacity_overrides` probe in
`tools/matrix_configs.sh` compiles a build with the slot counts changed, so the
`#ifndef` guards are exercised outside the unit test too.

Since P8 the ISDU buffers live in `iolink_workspace_t` (1295 B at the default
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
  (`tools/diff_matrix.sh`) compares `host_smp_split` against
  `host_default`; demoted switches are covered by expected-frame tests
  in the default suite (`tests/test_capability.c`,
  `tests/test_system_commands.c`, ...).
