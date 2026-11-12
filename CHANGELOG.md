# Changelog

All notable changes to the `iolinki` project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Runtime log sink: `iolink_log_sink_t` (callback + user pointer + level) is
  carried by the DLL, ISDU, DS and event contexts, bound from
  `iolink_device_config_t::log_write`/`log_level` and changeable with
  `iolink_device_set_log()`. Print points cover device/DLL/ISDU/DS/event code;
  the default is silent. ISR print points use the separate
  `IOLINK_LOG_ISR_*` family, enabled with `IOLINK_LOG_ENABLE_ISR`. See
  `include/iolinki/log.h` and `docs/API.md`.

### Changed (documentation language)
- Comments under `src/`, `include/iolinki/` and `tests/` are written in
  Simplified Chinese. Code, identifiers, string literals, Doxygen commands and
  spec references are unchanged; the Doxygen output now renders Chinese prose.

### Changed (readability and API cleanup)
- Naming: `frame_eager` -> `frame_replied_by_isr`,
  `dll_isr_reply_allowed[_acc]` -> `dll_isr_frame_valid[_acc]`,
  `dll_isr_note_activity` -> `dll_isr_touch_activity`,
  `dll_master_activity` -> `dll_mark_master_activity`,
  `dll_master_activity_ts` -> `dll_mark_master_activity_ts`,
  `iolink_dll_can_run_user_code()` -> `iolink_dll_take_service_window()`,
  `isr_fallback_clear` -> `fallback_clear_request`,
  `dll_publish_isr_frame` -> `dll_isr_publish_frame`,
  `dll_mc_is_isdu_write` -> `dll_isr_is_isdu_write`; short locals expanded.
  Spec abbreviations (`mc`, `od`, `ckt`, `flowctrl`) are intentionally kept.
- Public types: `iolink_config_t` -> `iolink_stack_config_t`; the
  `iolink_device_phy_t` alias is removed; `iolink_m_seq_type_t` declarations are
  ordered by value.
- `iolink_app_callbacks_t`: every hook takes `void* user`
  (`iolink_device_config_t.user`), so callbacks need no global state.
- `IOLINK_APP_PD_INPUT_CALLBACK` is a compile-time API selector
  (`-DIOLINK_APP_PD_INPUT_CALLBACK=ON`): the polling function and the callback
  member are mutually exclusive at compile time. Init rejects a callback
  profile without `on_pd_input` (`IOLINK_DEVICE_INIT_ERR_PD_INPUT`, -14) and a
  `IOLINK_PHY_FEATURE_TX_BUSY=1` build without `tx_busy`
  (`IOLINK_DEVICE_INIT_ERR_PHY`, -15).
- `phy->get_faults()` is polled: edge-triggered Table D.1 events
  (under/overvoltage, over-temperature, short circuit); the legacy
  `get_voltage_mv`/`is_short_circuit` pair is the fallback.
- Documentation: pump call maps, a device integration checklist and an
  ownership/lifetime contract (`dll.c`, `dll_isr.c`, `device.h`,
  `CODING_STANDARDS.md`).

### Removed
- `iolink_dll_prepare_cyclic_reply()` (no-op), `cyclic_reply_precached`,
  `iolink_dll_reply_variant()`, `iolink_reply_variant_t`,
  `IOLINK_OD_EVENT_MODE`, `IOLINK_OPERATE_TIMEOUT_MS`,
  `IOLINK_PHY_OWNS_LINE_STATE`, `IOLINK_DS_STATE_LOCKED`,
  `IOLINK_REPLY_VARIANT_COUNT`, `iolink_device_ctx_size()` and the unused ISDU
  transport states.
- Legacy layers (no deprecation path): `IOLINK_PHY_FEATURE_LEGACY_DIAG` with the
  `get_voltage_mv`/`is_short_circuit` PHY hooks (diagnostics now use
  `get_faults()` only), `IOLINK_FEATURE_LEGACY_PARAMS` with the context-less
  `iolink_params_*` API and `g_legacy_*` state, the `iolink_device_info_*`
  process-wide wrappers and `g_legacy_device_info_ctx`, and the
  `IOLINK_CONCURRENCY_RTOS` marker. Identity and parameters are context-only;
  a script that calls the removed APIs must switch to the `_ctx` forms and bind
  them through `iolink_device_init()`.

### Fixed (Type-0 reset and PHY frame contract)
- A MasterCommand (Table B.2, e.g. `0x97` DeviceStartup) written to Direct
  Parameter page 1 address 0 is now applied when it is carried by the OPERATE
  M-sequence instead of a TYPE_0 frame, so Table 45 T6/T7/T9 hold for every
  M-sequence type. It was previously ignored on the Type-1/2 path (only the
  TYPE_0 frame paths handled it).
- Kept the V1.1.5 behavior for non-TYPE_0 OPERATE devices: a valid TYPE_0
  request is consumed as the simulated restart, returns the link to STARTUP,
  and is not answered. An ISR-triggered STARTUP transition is now reported by
  the main-context state callback even if the next startup probe reaches
  PREOPERATE before the callback flush.
- Documented that Direct Parameter page 2 addresses `0x10-0x1F` are valid;
  unimplemented reads return zero and writes are ignored. Address filtering is
  not used as protection against truncated frames.
- Clarified that RX FRAME PHY adapters must deliver complete M-sequences and
  must not submit a two-octet header or another truncated prefix as a frame.

### Fixed (V1.1.5 review follow-up)
- `IOLINK_STATS_MODE=SPLIT` compiles again: the diagnostics reset only clears
  the ISR-owned counters that exist (`timeout_errors`, `framing_errors`), not
  the nonexistent `isr_stat.crc_errors`/`isr_stat.timing_errors`.
- `iolink_cycle_time_encode(0)` now encodes 0x00 ("no MinCycleTime", B.1.3)
  instead of clamping to 0.4 ms; 1..3 still clamp to 0x04 and the 317..320
  range no longer relies on unsigned wraparound.
- An unconfigured optional ProductURI (0x001B) now answers `0x80 0x11`
  instead of an empty positive response.
- ProcessDataInput (0x0028) and ProcessDataOutput (0x0029) serve the current
  Process Data buffer with the runtime channel length (B.2.23/B.2.24). PD
  validity is carried by the CKS PD-status bit (A.1.5); the clauses do not
  define another answer while it is invalid or before the first PD cycle, so
  the buffer is served as-is (ST reference behaviour). A device without Process
  Data still answers with an empty positive response.
- Device diagnosis and status follow Table 101: Device reset (0x80) and
  Back-to-box (0x83) set them to "0" (ErrorCount and the event memory behind
  DeviceStatus (0x0024) / DetailedDeviceStatus (0x0025) are cleared), while
  Restore factory settings (0x82) resets ErrorCount and withdraws every active
  Error/Warning with "event disappears" ("Clear"); Application reset (0x81)
  leaves them untouched.
- Event lifecycle (A.6.4/10.10.2): a re-raise inside the 1 s window is
  deferred by `iolink_events_process()` instead of being dropped, timestamps no
  longer use 0 as a "never sent" sentinel (a first event at t=0 works), events
  lost to a full queue or `pending[]` table increments
  `iolink_events_get_dropped_count()`, and slots are cleared completely when
  released. A cooled-down slot is reused only after both the appears and
  disappears windows elapsed, so the spacing history is not erased. DetailedDeviceStatus (0x0025) lists only events whose "appears" has
  actually been reported, and keeps a cleared event listed until its
  disappearance is reported (B.2.22). DeviceStatus (0x0024) is derived from the currently
  asserted conditions (Table B.13), so it drops with the cause and is not held
  by a queued disappearance or raised by a Notification.

### Added (TX FRAME line state)
- `iolink_phy_api_t::tx_busy` (TX FRAME only, optional) reports whether the PHY
  still owns the C/Q line. With it the library defers an eager ISR reply while
  the previous frame is on the wire, releases the PD-in reply buffer as soon as
  the transfer ends, and `IOLINK_PHY_FEATURE_TX_BUSY=1` drops the PD-in buffer
  count from three to two. Without the hook the previous behaviour is unchanged.
- `tests/test_tx_busy.c` plus a FRAME/FRAME `tx_busy` fixture and a matrix
  configuration cover the deferral and the lease release.

### Changed (PHY API)
- `iolink_phy_api_t::tx_done` is declared only for TX BYTE builds. TX FRAME
  builds have no such member: the library never called it in that mode (the PHY
  ends the transfer from its own completion interrupt), and carrying a NULL
  member implied otherwise. A BYTE-RX/FRAME-TX port must now release its
  receiver itself instead of relying on the gap handler's `tx_done()` call.

### Changed (concurrency model)
- The stack is now explicitly **single-main-caller**: the library no longer
  takes an application thread lock. `iolink_device_config_t::lock/unlock`,
  `iolink_device_lock_fn`/`unlock_fn`, the DLL context's lock fields and the
  `IOLINK_DEVICE_INIT_ERR_LOCK_PAIR`/`_LOCK` codes are removed (the -8 and -13
  values stay reserved). `dll_send()` stages the reply main-owned and publishes
  it last behind a barrier, so only the legacy direct-send fallback excludes
  the data interrupt; PD-in publishes and the event engine use the platform
  critical section, and `pd_out` reads need no lock.
- `IOLINK_CONCURRENCY_RTOS` is removed; an RTOS build uses `ISR` (or `SMP`
  across cores). The removed name stays defined out of range so a stale build
  flag is rejected instead of silently evaluating to `SINGLE`. `SMP` now has
  the value 2.
- The CPU-level `iolink_critical_*` and the (future) per-instance thread lock
  are separate layers: if thread-level serialisation is ever needed, the
  library will take a per-instance lock at the public entry points, and the
  application must not wrap those entry points itself.

### Fixed (code-review follow-up)
- The ISR no longer writes the 64-bit `fallback_deadline_ms`: a non-FALLBACK
  MasterCommand stages `fallback_clear_request` and the main pump applies it.
- Skipped PD-in publishes (every spare buffer leased) are counted in
  `pd_in_skips`. The lease is released only when the library can see the
  transfer end: a transceiver that stalls mid-frame keeps it, because rewriting
  a buffer that may still be on the wire would tear the reply. The counter lets
  the application detect the stall and reset the PHY.
- `isr_tx_dropped` is attributed to the running context, RX FRAME still answers
  and counts a frame when the mailbox is full, and a rejected short TX FRAME is
  counted.
- The framed ISDU response only alternates buffers after the previous response
  was published, so a new request can never overwrite the buffer the ISR reads.
- Documentation: state-callback re-entrancy, buffer/slot counts, counter
  locking, FreeRTOS tick wrap and the bare-metal example's clock resolution.

### Added (concurrency primitives)
- `include/iolinki/port.h` declares the graded asynchronous primitives and
  derives what a port must provide from the new concurrency profile
  (`IOLINK_CONCURRENCY`: `SINGLE`/`ISR`/`SMP`). The stack publishes
  `IOLINK_NEED_FENCE` and `IOLINK_NEED_CRITICAL`, so a port implements only the
  grades its profile requires. A `SINGLE` build does
  not reference `iolink_critical_*` at all; `tools/build_matrix.sh` proves it
  with `nm -u`.
- `IOLINK_FENCE_LEVEL` grades the barrier (`COMPILER`/`DEVICE`/`SMP`); a port
  supplies `IOLINK_FENCE_DEVICE_IMPL()` / `IOLINK_FENCE_SMP_IMPL()` for the
  higher grades, and a mismatched profile/level combination is a compile error.
- Publish-word helpers `iolink_atomic_load()` / `iolink_atomic_store()` and
  `IOLINK_CRITICAL_ENTER/EXIT()`; every library call site now uses the latter.
- `IOLINK_STATS_MODE=SPLIT` gives the data ISR its own counter set
  (`ctx->isr_stat`); `iolink_dll_get_stats()` reports the sum, so a preempted
  increment can no longer be lost. `SHARED` stays the default.
- `iolink_dll_stats_t::isr_tx_dropped` exposes the dropped-reply counter that
  both contexts increment (main + ISR, summed in `SPLIT` mode), so a transport
  that cannot transmit replies is observable from the public statistics. The
  member is appended, so `sizeof(iolink_dll_stats_t)` changes (rebuild against
  the new header; the out-pointer call itself is unchanged).
- `tests/test_port_capabilities.c` exercises the derivation, the barrier and
  the publish-word helpers under every profile; `test_stats_split` covers both
  statistics modes; `test_isr_cutpoints` injects the ISR publish at every
  consumer step (peek/copy/release) and inside a main-pump pass.

### Changed (state notification, P3c)
- State-change notification is now **level-based** and the deferred transition
  queue is gone: `dll_set_state()` only writes `ctx->state` (no critical
  section), and `iolink_dll_flush_state_callbacks()` calls the hook once when
  the current state differs from the last delivered one. Main-context
  transitions are delivered immediately (a PREOPERATE -> ESTAB_COM sequence
  inside one frame still reports both); transitions taken in the data ISR are
  delivered by the next flush and are coalesced into the final state. The hook
  never runs in interrupt context. Applications that need every intermediate
  transition must sample `iolink_device_get_state()` themselves.

### Changed (ISR transport concurrency)
- The cross-context handoffs are lock-free: the frame mailbox and the staged
  ISDU write segments use two-slot SPSC rings (`include/iolinki/ring.h`), the
  framed ISDU response is served zero-copy from a double buffer published as one
  atomic word, and the PD-in lease is released at reply end (two buffers in
  TX BYTE, three in TX FRAME). The main pump no longer disables interrupts for
  a bulk copy; only scalar bookkeeping and the event engine take the platform
  critical section.
- 64-bit deadlines are main-owned: the ISR flags a T_DSIO clear / T_FBD arm
  request instead of writing the 64-bit fields, and `last_byte_us` stores the
  low 32 bits compared with a wrap-safe delta (`dll_read_u64()` is gone).
- `request_service()` runs outside the reply-staging critical section; the ISDU
  view publish and the staged-write drain are lock-free when nothing changed;
  the DetailedDeviceStatus formatting runs outside the gate.
- Reply staging has three statically owned slots: 0/1 alternate in the main
  pump, slot 2 is reserved for ISR-generated replies, so no slot counter is
  shared between the contexts.
- `iolink_dll_ctx_t` and `iolink_isdu_ctx_t` internal layout changed accordingly
  (all fields remain private stack state; no API change).

### Changed
- `iolink_device_config_t::time_us` is now required: `iolink_device_init()`
  rejects NULL with the new `IOLINK_DEVICE_INIT_ERR_TIME`. When supplied (the
  only accepted configuration), the DLL, ISR pump and event engine take every
  timestamp from it; the callback must be monotonic, non-blocking and ISR-safe.
  Remove `include/iolinki/time_utils.h` and the platform
  `iolink_time_get_us()/get_ms()` sources; direct DLL users must bind
  `ctx->time_us` after `iolink_dll_init()`.
- `iolink_device_init()` now probes the platform critical-section hooks and
  returns the new `IOLINK_DEVICE_INIT_ERR_CRITICAL` when the weak no-op default
  is still active.
- Removed the legacy `iolink_nvm_read()`/`iolink_nvm_write()` API entirely
  (declarations, weak stubs, the Linux file mock and its test).
- `iolink_device_config_t::storage` is only declared with
  `IOLINK_FEATURE_DATA_STORAGE=1` and is now required then:
  `iolink_device_init()` rejects NULL or a backend missing `read`/`write` with
  `IOLINK_DEVICE_INIT_ERR_STORAGE`.

### Changed (maintenance model)
- The repository is maintained independently: `own_dev` is the mainline and
  `upstream` (`w1ne/iolinki`) is a read-only reference. Upstream is no longer
  merged or rebased; changes are adopted by re-implementing them against this
  codebase and adding a regression test. See `docs/MAINTENANCE.md`.
- Release flow now tags from `own_dev`; there is no `main` branch, promotion or
  back-merge step.
- Documentation aligned with the current code: removed `src/frame.c` and
  `IOLINK_PHY_FEATURE_ISR_PUMP` references, corrected the memory guide (ISDU
  buffer sizing, no CRC tables) and the porting examples (storage API, time
  conversion).
- The `upstream-sync` agent skill was replaced by a `maintenance` skill.

### Added (build verification)
- `tools/build_matrix.sh`: compiles every supported configuration (all ISR
  byte/frame combinations, minimal/tiny profiles, every feature toggle,
  compatibility switches, bare-metal, plus the `SINGLE` and `SMP` concurrency
  profiles and `STATS_MODE=SPLIT`) and runs the unit suite for the BYTE/BYTE
  ones. It also asserts that the `SINGLE` archive has no
  `iolink_critical_*` reference. Wired into CI as the delivery gate for
  compile-time changes.
- `src/phy_virtual.c` now compiles in FRAME builds as an unusable stub instead
  of failing with `#error`, so every ISR granularity can be compiled.
- Fixed the trimmed/no-events configurations: guarded SystemCommand-only ISDU
  helpers, the disabled-event macro arguments and the examples that referenced
  event/DS members unconditionally.

### Fixed (build and CI)
- `examples/stm32g0_tiol112/build.sh` no longer references the removed
  `src/frame.c` (the CI firmware job failed on it), and the IAR project dropped
  the same entry.
- `test_all.sh` / `.docker_test_entrypoint.sh` use `IOLINK_PLATFORM` (the old
  `-DPLATFORM=` was ignored, so the "bare metal" step silently built Linux).
- Release workflow: dropped the deleted ESP32/Zephyr example jobs and the
  `main`/`develop` back-merge job, removed the non-existent `samples` archive
  path, packaged only binaries the host build produces, and derives the
  verification version from `CMakeLists.txt` instead of a hardcoded RC.
- CI now triggers on `own_dev`, and a new Release (`-O3`, `-Werror`) job plus a
  Release build in `check_quality.sh` cover optimisation-only warnings.
- The unit suite compiles with `-UNDEBUG` so `assert()`-based tests keep testing
  in Release instead of becoming no-ops.

### Changed (tooling)
- clang-format is pinned to 23.1.1 and enforced by `check_quality.sh` instead of
  being skipped on a version mismatch; the Docker images install the pinned
  version and the tree was reformatted with it.
- Test NVM files are per-run/per-process, so parallel ctest runs in different
  build trees no longer corrupt each other (SDCI_TC_0316 flake).

### Added (tooling)
- `tools/coverage.sh`: gcovr line/branch summary (and optional HTML) for `src/`.
  Local only; no coverage threshold in CI yet.
- `shellcheck` now lints the maintained CI/test scripts in `check_quality.sh`
  and in a dedicated CI job.

### Tests
- Characterization tests for the byte RX CHK/gap frame end: a short or
  incomplete M-sequence is discarded and only a real truncation is counted as a
  framing error; the gap never publishes, answers or runs the eager validation.

### Fixed (conformance tooling)
- `tests/conformance_audit.csv` had been overwritten with the 387-row Chinese
  test-spec summary, so `conformance_traceability` failed on the missing
  `case_id` column. Restored the 142-row machine-readable audit and moved the
  summary back to `docs/IO-Link测试项全量汇总_387.csv`.

### Fixed (Test Spec conformance)
- DLL: reject the V1.0 STARTUP -> OPERATE shortcut (`0x99`/`0x98`) by default;
  a TYPE_0 page read while OPERATE is active now returns STARTUP without a
  reply for non-TYPE_0 devices. Both behaviours are available only through the
  `IOLINK_COMPAT_*` build switches.
- ISDU: an invalid CHKPDU discards the request and the next Read returns
  No Service (`0x00`), matching Test Spec 10032 SDCI_TC_0065.
- DPP1: `MasterCycleTime` (0x01) now mirrors the value written by the Master;
  `RevisionID` (0x04) now comes from the configured identity instead of a
  hardcoded `0x11`.
- DeviceAccessLocks: reserved bits 4..15 are rejected with `0x8030` and do not
  change the stored lock value.
- DataStorageIndex: sub0 now exposes the concatenated `12 + 3n` record and
  sub1 returns one octet as required by SDCI_TC_0107/0108. The legacy
  whole-image sub0 payload is available through `IOLINK_DS_LEGACY_IMAGE_SUB0`.
- DS: `DS_UploadEnd`/`DS_DownloadEnd` clear and persist `DS_UPLOAD_FLAG`.
- DS reset commands: Restore factory settings (0x82) and Back-to-box (0x83)
  clear and persist `DS_UPLOAD_FLAG = 0` without raising `DS_UPLOAD_REQ`,
  matching Tables 177/178 (SDCI_TC_0155/0318).
- CKS: a Device with output Process Data but no input Process Data now always
  reports "Process Data valid" (bit 6 = 0), including before `0x98`.
- DS state machine: unexpected ending commands in Idle return `0x8036`;
  UploadStart/DownloadStart restart or interrupt the opposite operation instead
  of reporting Busy; UploadStart/UploadEnd during Download discard the staged
  parameter values.
- ISDU: parameter writes are locked (`0x8022`) during an active Upload and
  parameter reads during an active Download, while SystemCommand and
  DataStorageIndex stay accessible.
- Test Spec 1.1.5: `ParamDownloadStore` is now valid from `DSIdle_2` and
  during Upload, and `DS_UploadEnd`/`DS_DownloadEnd` clear `DS_UPLOAD_FLAG`
  directly in `DSIdle_2` (SDCI_TC_0321/0322/0327).

### Added (conformance reference integration)
- `examples/conformance_device`: selectable full/config/PDOut-only/minimal
  profiles, Config1/2/3/4 test objects, DS descriptors, file-backed NVM, event
  injection, PD-valid control and local-change hook.
- Synthetic `device.xml`/`iodd_expected.json` for the reference profile.
- `tests/test_conformance_device` profile/parameter/storage tests.
- `tools/iodd_gen.py`: support the `features.dataStorage` and
  `features.blockParameter` flags and 16-bit Config1/3 test indices.
- `tools/conformance_device_master.py`: minimal TYPE_0 master covering startup,
  DPP1, Config/ISDU, DS Upload/Download and ParameterManager subset end-to-end
  over a PTY.
- DS state machine: Uploading/Downloading no longer auto-complete between main
  loop iterations; they stay active until UploadEnd/DownloadEnd/ParamBreak.
- Unit test for `0315`: incompatible DeviceID writes are rejected and the
  configured DID survives a restart.
- ISDU identification/access-lock handling now uses the Device's bound
  `device_info` context instead of the legacy global identity, allowing multiple
  device instances and the conformance reference profile.
- `tools/test_conformance_iodd_oracle.py`: synthetic IODD oracle checks for
  identity, DPP1 RevisionID/PD lengths, Config1/2/3/4 and fixed-length errors.
- `0123/0146` length error chain: ApplicationTag overrun -> `0x8033`, invalid
  Download parameter -> `0x8033`, DownloadEnd -> `0x8041`.
- Event memory acknowledgement now removes exactly the events presented in the
  frozen snapshot, not the first N entries of a possibly reordered queue.
- Conformance master runner now covers the event flows `0069/0070/0071/0072/
  0073/0074/0075/0076`, including PREOPERATE, 2 s communication pause, power
  cycle and short-event disappear.
- DataStorageIndex sub0 now builds the image before reading Data_Storage_Size,
  so the first read reports the current size instead of a stale value.
- Parallel conformance hardening raised the audit to 104 direct, 8 partial,
  19 n/a and 11 HIL-only; the remaining partials are
  `0034/0083/0155/0157/0316/0317/0318/0322`.
- Project scope now separates external HIL/physical/product-IODD work:
  final in-scope audit is 111 direct / 19 n/a / 12 out-of-scope, with
  `residual` reasons for external timing, reset, power and physical checks.
- Added `tools/check_conformance_traceability.py` and a CTest gate for the
  audit matrix and conformance annotations.
- Fixed the pre-existing `src/dll.c` `-Wconversion` issue; `check_quality.sh`
  now passes.
- `tests/conformance_audit.csv` and `tests/conformance_traceability.csv`:
  `scope` and `profile` columns separate library, reference-integration,
  physical-HIL, IODD-oracle and product-integration work.

### Changed (documentation)
- Removed the stale undated `docs/CONFORMANCE.md`; its material overlapped with
  `README.md`, `docs/TESTING.md` and `docs/SPEC_CONFORMANCE.md`, while its
  49-test/100% claims no longer matched the audit.
- Reconciled `CONFORMANCE_AUDIT_2026-10-28.md`, `SPEC_CONFORMANCE.md`,
  `ROADMAP.md` and the README with the final 111 direct / 19 n/a /
  12 out-of-scope disposition.
- Marked the HIL audit and correction guide as external HIL-project artifacts.

### Added (conformance audit)
- `docs/CONFORMANCE_AUDIT_2026-10-28.md`, `tests/conformance_audit.csv`,
  `docs/HIL_AUDIT_2026-10-28.md`, `tests/hil_audit.csv` and
  `docs/HIL_CORRECTION_GUIDE_2026-10-28.md`.
- Unit conformance annotations and the full 142-case traceability matrix.

### Added (remaining Test Spec cases)
- `tests/test_conformance_untested.c` covers the cases that were still marked
  untested: SDCI_TC_0063/0064 (Config1 length errors preserve the value),
  0122/0123 (ApplicationSpecificTag write/read, overlong rejection and
  persistence), 0124 (ErrorCount increment/reset), 0128 (DeviceStatus in
  OPERATE), 0131 (active-event DetailedDeviceStatus after restart), 0145/0329
  (block Download/Upload interrupted by reset) and the IODD reset sequences
  0155/0316/0317/0318. SDCI_TC_0035/0044/0097/0065 are extended in the existing
  DLL/ISDU wire tests.
- Restore factory settings and Back-to-box now clear `DS_UPLOAD_FLAG`
  (Tables 177/178) instead of requesting an upload, so SDCI_TC_0155/0318 run
  without a known-gap skip.

### Fixed (events / Data Storage)
- Events: the Event flag now satisfies `flag == (count > 0)` at every setter
  (publish, event-memory acknowledge, pop), so a Master can no longer see a flag
  with an empty queue and loop on read/acknowledge.
- Notifications: the same unacknowledged EventCode is queued only once, so a
  retrying Master cannot turn repeated requests into an event storm.
- `iolink_device_process()` now services `iolink_events_process()` and
  `iolink_ds_process()` while the ISDU transport is idle, so the DS/event state
  machines advance without the application having to pump them.
- The DS_UPLOAD_REQ notification is published only after the DS-command ISDU
  response has been read (ISDU idle), instead of inside the response, so the
  Master no longer interleaves a diagnosis read with the ISDU transfer.
- Parameter_Checksum (0x0003/4) is rebuilt on read when not downloading, so a
  direct parameter write is reflected before and after ParamDownloadStore.

### Removed (legacy API and constants)
- Removed non-standard `IOLINK_IDX_VENDOR_ID` / `IOLINK_IDX_DEVICE_ID`;
  VendorID/DeviceID are only DPP1 fields.
- Removed the unused `IOLINK_ISDU_CTRL_*` control-octet constants.
- Removed `IOLINK_CMD_RESTORE_APP_DEFAULTS` (use `IOLINK_CMD_BACK_TO_BOX`).
- Removed `IOLINK_CMD_PARAM_UPLOAD` / `IOLINK_CMD_PARAM_DOWNLOAD` (0x95/0x96)
  and their legacy SystemCommand mapping; those values are reserved and now
  return `0x8035`.
- Removed deprecated ISDU error aliases `IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL`
  and `IOLINK_ISDU_ERROR_BUSY`.
- Fixed the protocol header's spec version from V1.1.2 to V1.1.5.

### Removed (Zephyr / ESP32 support)
- Dropped the Zephyr module (`zephyr/`), the Zephyr/ESP-IDF examples and
  samples, the Zephyr UART PHY, the Zephyr test harness/Dockerfiles, the
  `west.yml` manifest and the related docs. The fork targets bare-metal and
  Linux only. The Zephyr-specific `zephyr-validation` CI stage was removed.

### Removed (streaming transport)
- The upstream streaming transport (`phy->send()`/`phy->recv_byte()`, the
  `#if IOLINK_PHY_FEATURE_ISR_PUMP` dual-path in `src/dll.c`, and
  `src/frame.c`) has been removed. The ISR two-pump transport is now the only
  transport: `iolink_device_process_isr()` frames requests and transmits
  replies, and `phy->send`/`phy->recv_byte` are gone from `iolink_phy_api_t`.
  `-DIOLINK_ISR_PUMP=ON` and the streaming-only host demos were dropped.

### Fixed (feature trimming)
- `IOLINK_PHY_FEATURE_WAKEUP=0`: a Fallback MasterCommand is acknowledged but
  no longer switches the DLL to SIO. Without `detect_wakeup` the device could
  not leave SIO again, so the unsupported transition is ignored. Regression
  coverage added to `test_sio_fallback`.

### Fixed (ISR transport concurrency)
- `dll_send()` now falls back to the platform critical section when the
  application configured no lock, so main-context reply staging
  (`isr_tx_next`/payload slot/`isr_tx_len`) is atomic against the data ISR.
  Previously an unlocked device could pick the same payload slot from both
  contexts.
- The data ISR records master activity for every validated received frame
  (not only the Type-0 fast path), so a busy main window no longer measures
  the master-loss window from the last main-pump run. This refresh is
  timestamp-only; the Back-to-box cycle count still advances exactly once per
  served request (regression test added).
- Deferred state callbacks use an ordered queue instead of a single slot: two
  ISR transitions before the next main window no longer lose the earlier one,
  and the queue keeps ISR and main-context transitions in order.
- `iolink_dll_process_isr()` defers a nested invocation (pump bound to several
  vectors on boards without BASEPRI) and re-runs once at the outer level,
  preventing double allocation of the reply slot and DMA teardown.
- `phy_l6362a_raw`: `tx_frame()` refuses a reply while the previous DMA is
  still enabled (returns a negative error) instead of reconfiguring the DMA
  mid-transfer; a hardware-completed transfer is finished inline. The eager
  path abandons its pre-send and lets the main pump own the frame, and the
  staging paths count the drop in `isr_tx_dropped`.
- `phy_l6362a_raw`: a frame that completes while interrupts are masked in
  `set_mode()`/`set_baudrate()` is published there, because `UartConfigure()`
  clears RTOCF before the deferred vector runs.
- Docs: the L6362A example requires the ISR pump in the USART2 vector only;
  the other feeds call `phy_l6362a_raw_request_pump()`.
- ISR_PUMP: removed the unused `state_cb_pending` field from
  `iolink_dll_ctx_t`; all state transitions now go through the ordered queue
  drained by `iolink_dll_flush_state_callbacks()` (source/ABI change for code
  that touched the field directly).

### Added (ISR transport concurrency)
- `test_isr_atomicity`: platform-critical fallback for unlocked staging,
  deferred nested pump, ordered state queue and busy-PHY reply handling.
- `test_phy_l6362a_raw`: TX busy refusal / pending hardware completion / TC
  timeout, plus a hardware-faithful mock that clears RTOCF in the critical
  section and delivers RX bytes before the vector.

### Changed (BREAKING, ISR transport)
- **M-sequence fixed types now follow Annex A byte-for-byte** (Figures A.7-A.13):
  TYPE_1_2 (2 OD, no PD), TYPE_1_V (8/32 OD, no PD), TYPE_2_1 (1 PD-in),
  TYPE_2_2 (2 PD-in), TYPE_2_3 (1 PD-out), TYPE_2_4 (2 PD-out) and TYPE_2_5
  (1 PD-in + 1 PD-out). Added `IOLINK_M_SEQ_TYPE_2_3/2_4/2_5`.
- **TYPE_1_1 is rejected at init** in the ISR build
  (`IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE`): it forces legacy interleave, which a
  V1.1.5 Device shall not implement (Table A.10 NOTE 3).
- **Fixed-type PD/OD lengths are validated** at `iolink_device_init()`; a
  mismatch returns the new `IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT`. Only TYPE_2_V
  accepts `iolink_device_set_pd_length()` in the ISR build.
- **2-octet fixed-type READ requests** were misrouted to the Type-0 handler;
  dispatch now uses the CKT type bits instead of `req_len`.
- `iolink_dll_compute_min_cycle_time()` now uses the actual READ request width
  (`MC+CKT+PD-out`, no OD) instead of assuming an OD octet.
- `virtual_master` is now a spec-conformant oracle (per-type request layouts,
  canonical `MSequenceType` values, TYPE_1_1 rejection) and
  `examples/host_demo` accepts the C enum values.

### Added
- `phy_virtual`: ISR BYTE hooks (`rx_octet`/`tx_octet`/`tx_done`/`int_active`/
  `request_service`) plus SDCI-gated `int_active`, so `host_demo` can drive the
  two-pump ISR transport over a PTY. ISR FRAME mode remains unimplemented.
- `host_demo`: pumps `iolink_device_process_isr()` when built with
  `IOLINK_ISR_PUMP`, enabling end-to-end fixed-type tests against the ISR build.
- `test_phy_l6362a_raw`: host-side coverage for the STM32G0 + L6362A raw
  PHY example using a mock CubeMX/HAL surface; exercises RX FRAME (normal,
  dropped, error and wrap), TX FRAME (ISR contract, completion and
  mode/baud-change abort), IRQ feeds, platform hooks and deterministic
  ISR/main interleaving (RX during `set_mode`, deferred RX/TX vectors during
  a baud-rate change).
- `test_pd_variable`: byte-exact Type 1_V/Type 2_V round-trips at both PD
  length bounds (1 and 32 octets, including asymmetric PDin/PDout widths such
  as the 28-in/2-out product configuration) and independent rejection one
  octet past either configured maximum.
- ISR_PUMP: Timer A/B hook wiring (`response_timer_start/stop`,
  `stack_timer_start`) plus `iolink_dll_on_rx_timeout()`,
  `iolink_dll_on_stack_tick()` and `iolink_dll_take_service_window()`.
  With a `stack_timer_start` hook installed, `iolink_device_process()` only
  runs after a Timer B tick authorises the main service window.
- ISR_PUMP: `isr_mailbox_drops` / `isr_eager_rejects` counters so mailbox
  back-pressure and eager validation failures are no longer silent.
- ISR_PUMP: optional PHY timer hooks (`response_timer_start/stop`,
  `stack_timer_start/stop`) for the upcoming ST-style Timer A/B interlock.
- `test_isr_observability`: mailbox drop / eager reject / ISDU-write-in-ISR
  coverage; `test_isr_isdu_snapshot` now covers repeated and out-of-order
  FlowCTRL.
- Data Storage: bindable parameter set (`iolink_ds_param_desc_t`) with
  vendor-parameter support via a unified read/write accessor; DataStorageIndex
  subindices 01-05 (DS_Command, State_Property, Data_Storage_Size,
  Parameter_Checksum, Index_List); Fletcher-32 `Parameter_Checksum`;
  block-parameter download buffering with atomic commit and `ParamDownloadEnd`
  `0x8041` rollback; `ParamDownloadStore` (0x05); Back-to-box (0x83)
  communication stop after 3 MasterCycles.
- Unified NVM backend (`iolink_storage_api_t`, callback-only, with a `user`
  pointer) replacing the separate `iolink_nvm_*`/`ds_storage` entry points; a
  fixed compile-time region layout (`IOLINK_STORAGE_TOTAL_SIZE`) with
  magic/version and a Fletcher-32 record checksum.
- Event appears/disappears lifecycle for Error/Warning (A.6.4): 50 ms minimum
  appearance window, 1 s per-code repeat limit, severity-ordered queue and a
  queue reset when communication is lost.
- `iolink_ds_set_param_desc()` for replacing only the DS index list, and
  `iolink_ds_param_desc_t::max_len` to bound a record's value.
- `docs/STORAGE.md` describing the storage backend and DS parameter access.

### Changed
- `iolink_device_init()` validates `config->stack` before touching the context
  or PHY: M-sequence type, PD lengths against the compile-time buffers, OD
  width/capacity, `baudrate_set` COM value and the `lock`/`unlock` pairing.
  Invalid configurations now return distinct `IOLINK_DEVICE_INIT_ERR_*` codes.
- ISR_PUMP: generate OPERATE replies directly from the shared `pd_in` /
  `isdu.resp_buf` state and remove the pre-staged cyclic double buffer,
  the ISDU response snapshot and their metadata. The main pump only keeps
  the ISDU Busy/length view in sync.
- ISR_PUMP: an eager frame that fails CRC/type validation is now published
  to the main pump (counted in `isr_eager_rejects`) so the existing fallback
  accounting still runs; the transition command is not answered by the
  data fast path.
- ISR_PUMP: Type-0 state transitions (startup probe, transition command,
  DeviceOperate 0x99 and Fallback 0x5A) are handled in the ISR; `state_cb` is
  deferred to the main service window so application callbacks never run in
  the data ISR.
- ISR_PUMP: Type-0 data frames in PREOPERATE/OPERATE (ISDU/page/process reads
  and ISDU writes) are answered directly from the ISR, including the CKS-only
  write reply. The startup probe, page writes (MasterCommand/DeviceOperate/
  Fallback) and diagnosis event memory still run in the main pump.
- ISR_PUMP: ISDU write requests are validated (A.1.6/type) and buffered by the
  ISR (collection, FlowCTRL, CHKPDU) before execution by the main window; the
  main pump skips the already-buffered octets via `isdu_isr_consumed`. This
  removes the "ACK before the request is buffered" window, the mailbox
  back-pressure loss for ISDU writes, and rejects corrupt writes before they
  reach the transport.
- ISR_PUMP: the ISR ISDU read path now enforces FlowCTRL START/COUNT,
  repeats the previous segment on a repeated COUNT, and answers No Service on
  out-of-order COUNTs.
- DPP1 `MinCycleTime` (0x02) is encoded per Table B.3 (time base | multiplier)
  and clamped to the 0.4 ms minimum.
- `0x83` is treated as Back-to-box (restore delivery values + stop
  communication), not "restore application defaults".
- Event memory is frozen when the EventFlag is raised (Table 60 T3), not on the
  first master read.
- DetailedDeviceStatus is served per subindex (0 = whole list, i = i-th active
  entry) and drops disappeared events.
- `iolink_ds_bind_param_access()` keeps the bound accessors/user when only the
  descriptor set is changed.

### Fixed
- ISR_PUMP: three PD-in reply buffers plus an ISR-published captured index, so
  the main pump can publish at any rate without overwriting the copy the ISR is
  streaming; replies use the published `pd_in_len` instead of the configured
  `pd_in_len_current`.
- ISR_PUMP: `state`, master-loss fields and timestamps are `volatile`; 64-bit
  deadlines are snapshotted under the critical section and
  `iolink_dll_set_pd_length()` publishes its pair atomically.
- ISR_PUMP RX FRAME: copy each completed frame into the mailbox buffer after the
  eager reply starts, so a main-loop stall longer than the DMA ring can no longer
  tear the frame the main pump copies (the copy is outside t_A).
- `test_isr_observability`: the Type-0 ISDU write fast-path test now checks
  the ISR staging contract (`isr_od_stage`) and the main-pump drain instead of
  the pre-staging direct `isdu.req_idx` update.
- ISR_PUMP: keep two TX payload slots so a TX-FRAME DMA keeps reading its
  buffer while the next reply is prepared; the single buffer could be
  overwritten by a back-to-back reply.
- ISR_PUMP: `isr_eager_replied` is snapshotted into `frame_replied_by_isr` when the
  mailbox is consumed, removing the cross-frame race on the eager flag.
- ISR_PUMP RX FRAME: validate the request length, do not call `rx_frame()`
  while the mailbox still holds the zero-copy pointer, and fill the TX CKS
  before handing the buffer to `tx_frame()`.
- ISDU: busy answers use `SERV_NOTAVAIL` 0x20 instead of 0x30
  (`PAR_VALOUTOFRNG`); a write-channel FlowCTRL `0x1F` (ABORT) resets the
  transport; the remaining Table C.1 additional codes are defined.
- Events: DeviceStatus maps Error → 4 (Failure) and Notifications no longer
  raise it; DetailedDeviceStatus excludes Notifications (B.2.22); a StatusCode
  confirmation acknowledges only the slots presented while frozen.
- Removed conflicting `APP_*`/`HW_*` event-code duplicates and the stale
  0x18xx `COMM_ERR_*` aliases (0x1800-0x18FF is vendor-specific, Table D.1).
- Device: TYPE_1_2 defaults to a 2-octet OD width, matching the advertised
  M-sequence capability code 1.
- Data Storage: raise DS_UPLOAD_REQ/DS_UPLOAD_FLAG on parameter-changing
  SystemCommands (Application Reset, restore commands, ParamDownloadStore).
- ISDU: single-element objects reject a non-zero subindex; 0x000C only accepts
  subindex 0; 0x000D is a single-element ArrayT.
- DPP1 0x0F SystemCommand is executed without an ISDU response (7.3.5.3).
- Data Storage: ParamDownloadEnd distinguishes a malformed image
  (`PAR_SETINVALID`, 0x8040) from a rejected known parameter
  (`PAR_SETINCONSIST`, 0x8041).

### Documentation
- Removed stale planning artifacts: the executed agent task board
  (`docs/AGENT_TASKS.md`, `docs/AGENT_REPORTS.md`,
  `docs/AGENT_INSTRUCTIONS_TEMPLATE.md`, `docs/PUPPETEER.md`, `docs/claims/`),
  the completed `docs/superpowers/` plans/specs, `docs/ROADMAP.md` (superseded by
  `docs/SPEC_CONFORMANCE.md`) and the empty `MISRA_DEVIATIONS.md`. The
  `tools/thepuppeteer` submodule and its wrapper scripts are gone with them.
- Merged duplicate content: `INSTALL.md` folded into `README.md`;
  `docs/examples/reference-device.md` removed (it only pointed at the example
  README); storage backend and memory sizing in `docs/PORTING.md` reduced to
  pointers at `docs/STORAGE.md` / `docs/MEMORY_GUIDE.md`; storage and build
  configuration in `docs/API.md` reduced to pointers; commercial tier terms in
  `README.md`, `docs/FAQ.md` and `docs/RELEASE_STRATEGY.md` now defer to
  `LICENSE.COMMERCIAL`; the branch model is stated once in
  `docs/MAINTENANCE.md` and `docs/RELEASE_STRATEGY.md` links to it.
- Corrected stale statements in `docs/CODING_STANDARDS.md`, `docs/TESTING.md`,
  `docs/RELEASE_STRATEGY.md`, `docs/security/THREAT_MODEL.md`, `SECURITY.md` and
  `docs/security/CRA.md` (quality-gate flags, coverage/MISRA policy, release
  artifacts and links, test file names, supported branch).

## [2.2.0] - 2026-10-17

### Changed
- **OPERATE M-sequence reply layout corrected (wire change).** The
  reply to a READ M-sequence is `[OD][PD-in] CKS` and the reply to a WRITE
  M-sequence is `[PD-in] CKS` with no OD octet, matching Figures
  A.9/A.10/A.13/A.14. `iolink_frame_decode_operate_response`, the Python
  virtual master and the reference example follow the same layout. Device and
  `iolinki-master` 1.1.0 must be upgraded together for the multi-octet OPERATE
  M-sequence types, because a mixed old/new pairing frames replies with the
  wrong layout and produces checksum errors.
- **API change:** the mislabelled `IOLINK_EVENT_PHY_VOLTAGE_FAULT` (0x1805) and
  `IOLINK_EVENT_PHY_SHORT_CIRCUIT` (0x1806) macros are removed. Those values are
  Table D.2 Master/Port codes ("Overtemperature", "Short circuit at L+"), not
  Device codes. `dll_poll_diagnostics()` now reports the standardized Table D.1
  Device codes `IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN` (0x5111) for a reading
  below 18 V, `IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN` (0x5110) for a reading
  above 30 V, and `IOLINK_EVENTCODE_SHORT_CIRCUIT` (0x7710). Callers that
  referenced the removed macros must switch to the `IOLINK_EVENTCODE_*` names.
- The public GPLv3 grant is no longer limited: the evaluation, non-commercial
  and 90-day conditions are dropped from `LICENSE`. The commercial license
  remains the alternative for proprietary products. The 12-month support
  period is removed from the public evidence bundle and security documents.
- Registering a vendor handler takes ownership of the Preferred Index range
  `0x0040`-`0x00FE`: requests to `0x0040` go to the handler, shadowing the
  built-in error-statistics object while it is registered.

### Added
- `tools/bench_run.py`, a one-command physical bench runner: it builds and
  flashes the firmware, captures the master's serial output, stores the raw
  capture with its SHA-256 and records the result; it records nothing for an
  empty capture or a dirty checkout.
- `tools/evidence_bundle.py`, a per-firmware-hash evidence bundle with the gap
  list the caller cannot drop.

### Fixed
- EventQualifier INSTANCE now encodes `0x04` (Application, Table A.17) instead of
  the reserved `0x02` in both the Table 58 event memory and the
  DetailedDeviceStatus (Index 0x0025) builder; an error-class device event is
  qualifier `0xF4`.
- `dll_poll_diagnostics()` no longer treats a negative `get_voltage_mv()` as an
  under/over-voltage fault: a negative reading means "measurement unavailable"
  (the generic PHY template returns `-1`), so generic-PHY devices no longer raise
  a voltage event on every poll.
- Vendor-specific ISDU indices in the Preferred Index range `0x0040`-`0x00FE`
  are now routed to the application vendor service (B.2.27), alongside the
  Extended Index range from `0x0100`; unsupported vendor indices keep answering
  `0x11` (Service not available). The built-in error-statistics object at
  `0x0040` remains the fallback when no vendor handler is registered.
- The bench record tooling accepts the gateway line the master prints and
  rejects a pass whose line names a different vendor or device.

### Validation scope
- Host suites pass: 36/36 CMocka tests, the Python virtual-master suites, and
  the corrected wire layout is exercised against `iolinki-master` in the
  cross-repo test. Physical-master re-validation of the corrected reply layout
  remains separate pending work.

## [2.1.1] - 2026-10-02

### Fixed
- Unsupported SystemCommand values now return FunctionNotAvailable (`0x8035`)
  as required by IO-Link Interface & System Specification V1.1.5, section C.2.14.
  Unknown ISDU indices continue to return IndexNotAvailable (`0x8011`).
- Released counter and switching-sensor IODDs now include required standard
  variable references and ISDU test configurations; the teach singleton uses
  SingleValue. Release ZIPs use canonical IODD filenames. Regenerated examples
  pass current official XSD validation and genuine IODD Checker 1.1.4 business rules.

## [2.1.0] - 2026-10-02

### Added
- Bare-metal STM32G0B1RE/TIOL112 GCC and native IAR project files, native
  ESP-IDF ESP32-C3/L6362A and Zephyr Nucleo-U575ZI-Q/TIOL112 references.
- Counter/button/LED and switching-sensor examples with matching schema/CLI-validated IODD
  maps. The sensor demonstrates threshold/hysteresis/inversion, wire ISDU teach
  and readback, PD publication and a vendor notification.
- Optional per-device vendor ISDU service for indices >= 0x0100, with bounded
  responses and independent application state. Mandatory indices stay reserved.
- Reproducible LabWired actual-firmware startup/interrupt recipes and exact pins.

### Changed
- New commercial quotes distinguish Indie person-held and Company company-held
  product-family rights; accepted earlier agreements retain their terms.
- Release gating requires passing, non-skipped JUnit tests. Source, exact-tag
  firmware, test evidence and SBOM assets are checksummed; RC tags are prereleases.

### Validation scope
- Native IAR project compilation, embedded flash persistence, complete analog
  transceiver Twins and physical-master validation are separate pending work.
- IODD maps pass schema/CLI checks; official IODD Checker approval is separate. The switching
  example does not claim standardized Smart Sensor Profile conformance.

## [2.0.0] - 2026-09-25

### Changed
- **BREAKING wire change: spec-conformant IO-Link V1.1.5 M-sequences.** The
  device stack now speaks the wire defined by the IO-Link Interface &
  System specification V1.1.5:
  - **C1 Checksum (A.1.6)**: the message checksum is the XOR of every octet
    seeded with `0x52`, compressed 8->6 bits by (A.1). The old CRC-6
    (poly `0x1D`, seed `0x15`) and the trailing checksum octet are gone.
  - **Reply layout (A.1.5)**: replies are `[PD-in][OD] CKS` with no leading
    status octet and no PD toggle bit; the Event flag is CKS bit 7 and the
    PD-invalid flag is CKS bit 6. A Type-0 read replies `OD CKS`; a Type-0
    write replies `CKS` only.
  - **C2 Channel dispatch**: OD is dispatched by the MC communication channel
    (process / page / diagnosis / ISDU); MasterCommands are page-channel writes
    to address 0.
  - **C3 ISDU transport**: requests and responses are carried by OD messages on
    the ISDU channel with FlowCTRL in the MC address bits and CHKPDU (A.5.6);
    the invented interleaved START/LAST/SEQ control bytes are removed.
  - **C4 Diagnosis channel**: the event memory is exposed per Table 58 and the
    Event flag is acknowledged by writing StatusCode at address 0.
  - **C5 Indices**: standard indices corrected (`0x0024` DeviceStatus,
    `0x0025` DetailedDeviceStatus, `0x0020` ErrorCount, vendor objects at
    `0x0040+`), 3-octet DeviceID and Table A.10 M-sequence capability codes.
  - **C6 Timing**: a single device `T_REN` of 500 us (Table 10), `T_WU` pulse
    (80 us), `T_DSIO` (300 ms) fallback to SIO, and `T_FBD` (3 MasterCycleTimes,
    max 500 ms) for the FALLBACK MasterCommand.
- The Python virtual master and the 49-test conformance suite were updated to
  the same wire contract.

### Fixed
- **Table A.10 OD width**: TYPE_2_1 and TYPE_2_2 carry one On-request Data octet.
  TYPE_2_V stays at two octets.
- **Type-0 OD write**: a write is three octets in PREOPERATE and in OPERATE,
  independent of the OPERATE OD width.
- **ISDU errors**: a read of write-only SystemCommand (index 0x0002) returns
  ErrorType `0x80 0x23` (IDX_NOT_ACCESSIBLE, Table C.1). Access denied uses the
  same ErrorType.

### Verified
- CI on `develop` is green, including the virtual-master conformance suite.
- This tag is not an official IO-Link conformance certificate. It has not been
  run against a third-party master.

## [1.2.0] - 2026-07-02
### Added
- **Per-release SBOMs**: every tagged release now ships CycloneDX 1.6 and SPDX 2.3
  SBOMs (`iolinki-<version>.cdx.json` / `.spdx.json`) generated by
  `tools/generate_sbom.py` and attached as release assets.
- **Security documentation package**: CRA-ready coordinated-disclosure policy
  (`SECURITY.md`), STRIDE threat model aligned to IO-Link Security Design and
  Development Guideline 10.512 (`docs/security/THREAT_MODEL.md`), and a Cyber
  Resilience Act overview for device makers (`docs/security/CRA.md`).

### Fixed
- **Zephyr module build**: `zephyr/CMakeLists.txt` still referenced
  `src/iolink_core.c`, deleted by the singleton-API removal; the module now
  builds against `src/device.c`. The breakage was invisible because
  `zephyr-validation` was not a required check.

### Changed
- **CI gates are now real**: branch protection on `develop` and `main` requires
  `docker-validation`, `zephyr-validation`, and `sbom-tools` directly; the
  echo-only proxy jobs (`quality-gate`, `build-and-test`, `build-bare-metal`,
  `build-example`) are removed.

## [1.1.2] - 2026-06-15
### Fixed
- **Device communication over the UART PHY**: the DLL booted in SIO mode and only left it upon electrical wake-up detection. A plain UART PHY cannot observe the C/Q wake-up pulse (`detect_wakeup == NULL`), so a device on real UART hardware stayed in SIO forever and never serviced M-sequences. The DLL now starts in SDCI when the PHY cannot detect wake-up (the already-established-COM contract of the UART PHY), so the device responds to the Master immediately.
- **Real-hardware build out of the box**: the Zephyr module defaulted to the virtual PHY, which is implemented with POSIX `termios` and therefore failed to compile for any real board (ARM/RISC-V). The PHY choice now tracks the target — virtual on `native_sim`/POSIX, UART on real hardware — and the virtual PHY is no longer offered (cannot be mis-selected) off POSIX.

### Tools
- `nucleo_master.py` gains `--no-wakeup` (drive an already-established-COM UART link without the `0x55` wake-up artifact, which would otherwise desynchronise framing) and `--hold-lines` (hold DTR/RTS low for boards whose USB-serial resets on control-line changes, e.g. the ESP32-C3 native USB-Serial-JTAG).

### Validated
- End-to-end IO-Link Master↔Device exchange on an ESP32-C3 (RISC-V) over the native USB-Serial-JTAG: PHY `send`/`recv_byte` round-trip, and a full startup → PREOPERATE → OPERATE sequence reading the device's live Process Data with valid checksums.

## [1.1.1] - 2026-06-15
### Fixed
- **Zephyr UART PHY build**: `uart_irq_update()` returns `void` in current Zephyr, but the PHY ISR used its result in a boolean (`if (!uart_irq_update(dev))`), so the UART PHY failed to compile when targeting real hardware. (`native_sim` builds use the virtual PHY, so this path was never exercised in CI.) Call it as a statement per the Zephyr API. Validated on a Nucleo-F103RB (Cortex-M3): builds (FLASH 25% / RAM 54% of 20 KB), flashes, boots, and runs on silicon.

### Added
- Board overlay for `nucleo_f103rb` (IO-Link UART on USART1).

## [1.1.0] - 2026-06-15
### Added
- **Data Storage parameter server** (ISDU index 0x0003): real serialization and restore of device parameters, with Access-Lock integration and checksum-mismatch recovery.
- **Direct Parameter pages**: page 1 read (index 0x0000) and page 2 read/write (index 0x0001).
- **Standard EventCodes**: V1.1.5 Annex D event-code constants and `iolink_event_classify()`.
- **Variable Process Data negotiation**: runtime PD length changes for Type 1_V/2_V via `iolink_set_pd_length()`.
- **Application callbacks API** (`iolink_app_register`): lifecycle (startup/preoperate/operate) and process-data callbacks.
- **Application reset handler** (`iolink_set_reset_handler`) for System Commands 0x80/0x81.
- **Zephyr module**: west-installable manifest, Kconfig-driven configuration, devicetree-bound UART PHY, and a `native_sim` sample.

### Fixed
- EventQualifier encoding (index 0x1C) corrected to the V1.1.5 layout.
- PD Input Descriptor (index 0x1D) reports the actual configured PD length.
- DLL resets the inactivity window on wake-up, making re-establishment after an idle period reliable.
- FALLBACK state is now observable; stray ISDU debug output removed; `timeout_errors` counter wired through.
- CI pipeline restored to green (strict compile, clang-format, Doxygen, and Zephyr build stage).

### Changed
- Documentation reconciled with the implementation (conformance test count, architecture, API reference).
- Clarified dual-license terms: GPLv3 is free for open-source use (including production) under copyleft; a commercial license is required only for closed-source/proprietary products. Replaces the prior, unenforceable "90-day evaluation" framing.

## [1.0.0] - 2026-02-06
### Added
- **Final V1.0.0 Stability**: Completed all mandatory ISDU index implementations and protocol state machine hardening.
- **Interleaved Mode Support**: Full compliance with IO-Link V1.1.5 interleaved Control/Data format for all acyclic transfers.

### Fixed
- **ISDU Protocol Alignment**: Corrected service ID mapping (8 for Read, 9 for Write) and standardized state transitions across all handlers.
- **Timer and Jitter Resilience**: Optimized cycle timing and timeout handling for robust performance in virtualized environments.
- **Unit Test Coverage**: Achieved 100% pass rate on 21 core unit tests and synchronized all test helpers with the protocol spec.

## [1.0.0-rc5] - 2026-02-06
### Fixed
- **ISDU Service Protocol**: Fixed critical bug in `HEADER_INITIAL` where Service Codes `0x9` (Read) and `0xA` (Write) were compared against internal Enum values `0/1` instead of protocol literals, causing all requests to fail or misbehave.
- **Unit Test Expectation**: Corrected `test_dll.c` expectation for Preoperate-to-Operate transition to 4 bytes, matching Type 1 environment defaults.

## [1.0.0-rc4] - 2026-02-06
### Fixed
- **Test Stability**: Fixed `test_timing.c` by explicitly configuring Type 0 mode to match test frames, avoiding default Type 1 conflict in CI.
- **ISDU Compliance**: Updated `test_isdu_segmented.c` to use standard sequence number logic (starting at 0), resolving potential protocol mismatches.

## [1.0.0-rc3] - 2026-02-06
### Fixed
- **Build System**: Fixed invalid CMake version string (removed `-rc` suffix from `project()` call) to resolve configuration error.

## [1.0.0-rc2] - 2026-02-06
### Fixed
- **ISDU Helpers**: Fixed `test_helpers.c` to correctly handle `Start/Last` bits in V1.1.5 Interleaved Mode (preventing `BUSY` errors).
- **Unit Tests**: Updated `test_dll.c` expectations to match Type 1 (4-byte) default configuration in CI.

## [1.0.0-rc1] - 2026-02-06

### Added
- **Quality Gate**: Integrated Docker-based validation strategy for consistent testing.
- **Pre-commit Hooks**: Enforced automated unit test execution before commits to prevent regressions.
- **ISDU Helpers**: Added test helper functions (`isdu_send_read_request`, etc.) for V1.1.5 interleaved format validation.

### Fixed
- **ISDU Protocol Compliance**: Updated `src/isdu.c` to enforcing V1.1.5 interleaved Control/Data format for **all** transfers (previously only segmented).
- **Type 0 Framing**: Corrected DLL implementation and tests to properly handle 2-byte idle frames (MC+CK).
- **Error Recovery**: Fixed inactivity timeout (200ms) in `src/dll.c` for robust communication recovery.
- **Stability**: Resolved PD toggle timing issues and ISDU stress test failures.

## [0.12.13] - 2026-02-05

### Fixed
- **CI/CD**: Final global formatting pass to ensure 100% compliance with `clang-format`.

## [0.12.12] - 2026-02-05

### Fixed
- **MISRA**: Eliminated all suppressions for Rule 14.3 by using non-constant stubs in `src/platform_stubs.c`.
- **CI/CD**: Final stabilization of static analysis for professional deployment baseline.

## [0.12.11] - 2026-02-05

### Fixed
- **MISRA**: Resolved `knownConditionTrueFalse` violation (MISRA 14.3) in `src/params.c` without using suppressions.
- **Architectural**: Refactored NVM weak stubs to a dedicated platform file (`src/platform_stubs.c`).

## [0.12.10] - 2026-02-05

### Fixed
- **CI/CD**: Fixed all static analysis warnings from `cppcheck` (style, const usage, and false positive in params).
- **CI/CD**: Resolved persistent `clang-format` issues by replacing macro with inline function.

## [0.12.9] - 2026-02-05

### Fixed
- **CI/CD**: Replaced `IOLINK_US_FROM_MS` macro with `iolink_us_from_ms` function to resolve persistent `clang-format` ambiguity.
- **CI/CD**: Verified formatting compliance across all platform ports.

## [0.12.8] - 2026-02-05

### Fixed
- **CI/CD**: Final fix for `clang-format` ambiguity in `time_utils.h`.
- **CI/CD**: Enforced formatting check on all project files.

## [0.12.7] - 2026-02-05

### Fixed
- **CI/CD**: Fixed unit test regressions in `test_dll` and `test_isdu_segmented`.
- **CI/CD**: Fixed `clang-format` violations in `time_utils.h`.

## [0.12.6] - 2026-02-05

### Fixed
- **PHY**: Modified `phy_virtual` to skip `tcgetattr` if the device is not a TTY (allowing `/dev/null` usage in CI).

## [0.12.5] - 2026-02-05

### Fixed
- **Bare Metal**: Fixed runtime crash in `examples/bare_metal_app` by initializing dummy port `/dev/null` for virtual PHY.
- **Unit Tests**: Fixed compilation error in `tests/test_dll.c` (undeclared variable `bad_frame` scope).

## [0.12.4] - 2026-02-05

### Fixed
- **Bare Metal**: Fixed API mismatch in `examples/bare_metal_app` (`iolink_init` arguments).
- **Unit Tests**:
  - Fixed `test_dll` failure caused by mock underflow in `recv_byte` during recovery.
  - Fixed `test_isdu` assertion logic to match flow control response timing.

## [0.12.3] - 2026-02-05

### Fixed
- **CI/CD**: Fixed `clang-format` violations across the codebase.
- **CI/CD**: Updated `cppcheck` configuration to suppress `unusedFunction` false positives for library APIs.

## [0.12.2] - 2026-02-05

### Fixed
- **Conformance**: Fixed critical State Machine and Error Injection conformance failures.
  - Resolved **False Wakeup Detection** in `OPERATE` state by using invalid MC byte `0x55`.
  - Fixed **ISDU State Machine** bug in Index `0x001C` (Detailed Device Status) read.
  - Fixed **Type 0 Protocol Collision** in test suite by enforcing Type 2 for large ISDU payloads.
  - Updated Virtual Master tool to support ISDU writes > 15 bytes.

## [0.12.1] - 2026-02-04

### Changed
- Updated LICENSE: Added dual-licensing terms (GPLv3 + Commercial).
- Updated project version to 0.12.1.

## [0.12.0] - 2026-02-04

### Changed
- **Major Codebase Refactoring and Quality Improvement**:
  - Centralized all IO-Link protocol constants in `include/iolinki/protocol.h`.
  - Refactored `src/dll.c` to improve readability and reduce complexity (modularized `handle_operate`).

## [0.11.0] — 2026-02-04

### Changed
- **Major Codebase Refactoring and Quality Improvement**:
  - Centralized all IO-Link protocol constants in `include/iolinki/protocol.h`.
  - Refactored `src/dll.c` to improve readability and reduce complexity (modularized `handle_operate`).
  - Refactored `src/isdu.c` to simplify the segmentation engine and standardize index handling.
  - Standardized code style across the project (MISRA-C style NULL checks and unsigned literals).
  - Resolved build warnings and improved build integrity.
- Updated `CMakeLists.txt`, `Doxyfile`, and documentation to version 0.11.0.

## [0.10.0] — 2026-02-03

### Added
- **SIO Mode Support**: Single-wire bidirectional communication mode
  - `iolink_dll_set_sio_mode()` and `iolink_dll_set_sdci_mode()` API functions
  - `iolink_dll_get_phy_mode()` to query current PHY mode
  - Virtual Master `set_sio_mode()` and `set_sdci_mode()` methods
  - Mode switching only allowed in OPERATE state
- **Virtual Master Enhancements**:
  - Added `verify_checksum()` for proper CRC validation
  - Added `checksum_ok` and `pd_valid` fields to `DeviceResponse`
  - Enhanced `run_cycle()` with `od_req2` and `ckt` parameters for better control
  - Improved `run_cycle_bad_crc()` with same parameters
- **Test Suites**:
  - `test_sio_mode.py` - SIO mode switching and restrictions tests

### Changed
- Fixed event detection in `DeviceResponse` (bit 0x80 instead of 0x01)
- Initialize PHY mode to SDCI in `iolink_dll_init()`
- Updated ROADMAP to mark SIO mode as complete

## [0.9.0] — 2026-02-03

### Added
- **Variable PD Types (1_V and 2_V)**: Support for dynamic Process Data length changes (2-32 bytes)
  - `iolink_dll_set_pd_length()` and `iolink_dll_get_pd_length()` API functions
  - Virtual Master `set_pd_length()` method for dynamic PD length changes
  - Frame length calculation for variable PD in `get_req_len()`
- **Integration Testing Infrastructure**:
  - `test_device_connection.py` - Automated device connectivity tests for Types 0, 1_2, 2_2
  - `test_integration.sh` - Bash integration test runner
  - `run_conformance_all_types.sh` - Multi-type conformance test runner
- **Test Suites**:
  - `test_pd_variable.py` - Variable PD type tests with dynamic length changes
  - Updated `test_conformance_isdu.py` with M-sequence type parametrization via environment variables

### Changed
- Updated Virtual Master README with Variable PD examples and comprehensive M-sequence type table
- Enhanced conformance tests to support multiple M-sequence types (0, 12, 22)
- Improved DLL initialization for variable PD field setup (pd_in_len_current, pd_out_len_current)

### Fixed
- OD length calculation for Type 2_V (now correctly uses 2-byte OD)

## [0.8.0] — 2026-02-03

### Added
- **M-Sequence Type Support**: Full implementation of Types 1_1, 1_2, 2_1, and 2_2 for comprehensive protocol coverage.
- **Variable OD Length**: Dynamic On-request Data sizing (1 or 2 bytes) based on M-sequence type.
- **Mandatory ISDU Indices**: Implemented all remaining mandatory indices per IO-Link V1.1.5:
  - 0x000A: Vendor ID (16-bit)
  - 0x000B: Device ID (32-bit)
  - 0x000D: Profile Characteristic (16-bit)
  - 0x001B: Device Status (8-bit)
- **PHY API Extensions**: Added optional hardware feature support:
  - Wake-up pulse detection (80μs pulse on C/Q line)
  - C/Q line state control
  - L+ voltage monitoring (18-30V range)
  - Short circuit detection
- **Error Handling**: Comprehensive error tracking and retry logic:
  - CRC error counter
  - Timeout error counter
  - Framing error counter
  - Automatic retry mechanism (max 3 attempts)
  - Timing statistics (response time tracking)
- **Unit Tests**: New `test_m_sequence_types.c` for validating M-sequence frame handling.

### Changed
- **DLL Context**: Added `od_len` field for dynamic OD sizing.
- **Device Info**: Moved `access_locks` from global variable to `device_info` structure for better encapsulation.
- **ISDU Write Support**: Enhanced Index 0x000C (Access Locks) to support write operations.

### Fixed
- **Global Variables**: Eliminated all global state variables (removed `g_access_locks`).
- **Multi-Instance Support**: All APIs now properly use context pointers for full reentrancy.

## [0.7.0] — 2026-02-03

### Added
- **Parametrization Manager**: New module (`src/params.c`) for persistent storage of ISDU parameters (e.g., Application Tag).
- **NVM Abstraction**: Added Non-Volatile Memory read/write hooks to the platform layer for cross-reboot persistence.
- **Dockerized Validation Suite**: Integrated optimized, two-stage Docker testing infrastructure for both Linux and Zephyr targets.
- **GitHub Actions Optimization**: Enhanced CI with Docker image caching for 5GB+ Zephyr environments.
- **Variable PD Verification**: Fully verified Type 1_V and 2_V frames using the updated Virtual Master automation.

### Changed
- **ISDU Response Logic**: Improved state machine robustness by resetting response buffers and flags on a per-request basis.

### Fixed
- **Platform Headers**: Corrected missing `size_t` definitions and header guards in `platform.h`.
- **Zephyr CI Paths**: Fixed `west` workspace initialization issues in automated testing scripts.

## [0.6.0] — 2026-02-02

### Added
- **Mandatory ISDU Indices**: Full support for IO-Link V1.1.5 mandatory indices (0x0010-0x0018, 0x001E, 0x0024).
- **V1.1.5 Compliance**: Implemented strict Control Byte interleaving/multiplexing for 1-byte OD systems.
- **Automated Integration Testing**: New Python Virtual Master test suite for verifying mandatory indices and protocol conformance.
- **CI Enforcement**: Integrated Virtual Master tests into the project validation suite (`test_all.sh`).

### Fixed
- **DLL State Machine**: Fixed missing `isdu_process` calls in Type 0 and Preoperate states that caused acyclic messaging hangs.
- **ISDU Response State**: Fixed a bug preventing the reset of response control flags between transactions.

## [0.5.0] — 2026-02-02

### Added
- **ISDU Completion**: Full support for IO-Link V1.1.5 segmented transfers (interleaved Control/Data bytes for 1-byte OD).
- **Flow Control**: Added ISDU Busy state handling for better acyclic messaging management.
- **16-bit Indexing**: Enhanced range for ISDU parameter access (16-bit Index, 8-bit Subindex).
- **New Tests**: Added `tests/test_isdu_segmented.c` for multi-frame ISDU validation.

## [0.4.0] — 2026-02-02

### Added
- **Variable Process Data**: Full support for IO-Link V1.1.5 frame types (Type 1_V, 2_V) supporting 2-32 bytes.
- **Data Validity**: Added mandatory `valid` flag to `iolink_pd_input_update` and `PDStatus` bit propagation in status byte.
- **Thread Safety**: Wrapped all Process Data input/output in platform critical sections.
- **Extended M-Sequences**: Supported Type 1_V and Type 2_V in DLL.
- **New Tests**: Added `tests/test_pd_variable.c` for multi-byte PD exchange validation.

### Changed
- **API (BREAKING)**: `iolink_pd_input_update` now requires a third `bool valid` argument.

## [0.3.0] — 2026-02-02

### Added
- **Core Portability**: Refactored entire stack to use Context-Based API (no global state).
- **Embedded Config**: Centralized `iolink_config.h` for tuning buffer sizes and memory usage.
- **RTOS Integration**: Added `platform.h` with critical section hooks (`iolink_critical_enter/exit`) for thread safety.
- **FreeRTOS Support**: Added `examples/freertos_app` demonstrating multi-task usage.
- **Zephyr Support**: Native simulation integration (`test_zephyr.sh`) and module manifest (`west.yml`).
- **Docker Environment**: Unified build/test container for Linux, Bare Metal, and Zephyr.
- **Strict Quality**: Enforced `-Werror`, `-Wconversion` across codebase.
- **Documentation**: Added `MEMORY_GUIDE.md` and `SECURITY.md`.

### Changed
- **Logging**: Removed `printf` from core logic; introduced logging abstraction.
- **CI/CD**: Added locking verification tests and static analysis enhancements.

## [0.2.0] — 2026-02-01

### Added
- **Application Layer API**: Introduced `iolink_pd_input_update` and `iolink_pd_output_read`.
- **Process Data (PD)**: Cyclic exchange support in `OPERATE` state.
- **ISDU Support**: Acyclic messaging engine with segmentation/reassembly.
- **Standard Services**: Implementation of Vendor Name (Index 0x10) reading.
- **Events**: Diagnostic signaling mechanism with FIFO queue and ISDU Index 2 integration.
- **Data Storage (DS)**: Parameter backup/restore state machine with storage abstraction.
- **Architecture Documentation**: Added `docs/ARCHITECTURE.md` detailing the layered design.
- **Unit Tests**: Added `test_pd.c` and `test_isdu.c`.

### Changed
- **MISRA Audit**: Improved code quality, fixed unused parameters, and ensured strict typing.
- **DLL State Machine**: Transition logic from Preoperate to Operate based on protocol events.

## [0.1.0] — 2026-02-01

### Added
- **Foundational Architecture**: Layered design with hardware abstraction.
- **PHY Abstraction**: `iolink_phy_api_t` interface for portability.
- **DLL Core**: Initial state machine (Startup, Preoperate) and CRC6 calculation.
- **Simulation Drivers**: `phy_virtual` for host-side execution and `phy_mock` for TDD.
- **Automated CI/CD**: GitHub Actions for linting, testing, and releases.
- **Host Demo**: `examples/host_demo` showing stack execution without hardware.
- **Documentation**: Initial README, ROADMAP, VISION, and RELEASE_STRATEGY.

[Unreleased]: https://github.com/Adlyq/iolinki/compare/v2.2.0...HEAD
[2.2.0]: https://github.com/Adlyq/iolinki/compare/v2.1.1...v2.2.0
[2.1.1]: https://github.com/w1ne/iolinki/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/w1ne/iolinki/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/w1ne/iolinki/compare/v1.2.0...v2.0.0
[1.2.0]: https://github.com/w1ne/iolinki/compare/v1.1.3...v1.2.0
[0.11.0]: https://github.com/w1ne/iolinki/compare/v0.10.0...v0.11.0
[0.10.0]: https://github.com/w1ne/iolinki/compare/v0.9.0...v0.10.0
[0.9.0]: https://github.com/w1ne/iolinki/compare/v0.8.0...v0.9.0
[0.5.0]: https://github.com/w1ne/iolinki/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/w1ne/iolinki/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/w1ne/iolinki/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/w1ne/iolinki/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/w1ne/iolinki/releases/tag/v0.1.0
