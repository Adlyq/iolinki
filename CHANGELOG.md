# Changelog

All notable changes to the `iolinki` project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed (MAJOR: device API, DLL surface and context boundary)
- **Breaking:** the application trait owns reset handling. `iolink_device_app_t`
  gains `on_reset(self, type)`; `iolink_device_set_reset_handler()`,
  `iolink_reset_handler_t` and `iolink_device_ctx_t::reset_handler` are gone.
- **Breaking:** the PD callbacks no longer take a length:
  `on_pd_input(self, data)` and `on_pd_output(self, const uint8_t*)`. The
  length is the configured `pd_in_len` / `pd_out_len_current` and cannot change
  at runtime.
- **Breaking:** `iolink_device_pd_input_update()` returns
  `iolink_device_pd_result_t` (`OK`/`ERR_ARG`/`ERR_MODE`/`ERR_LEN`/`ERR_BUSY`).
  `IOLINK_DEVICE_INIT_ERR_PD_INPUT` is removed; initialisation never returned it.
- **Breaking:** `iolink_dll_get_baudrate()` takes no argument. The DLL
  introspection entry points `iolink_dll_expected_req_len()`,
  `iolink_dll_timer_deadline()`, `iolink_dll_timer_armed()` and
  `iolink_dll_request_back_to_box()` moved to `include/iolinki/internal/wiring.h`.
- **Breaking:** the data-ISR entry points (`iolink_dll_process_isr`,
  `iolink_dll_on_rx_timeout`, `iolink_dll_on_stack_tick`) take
  `iolink_dll_shared_t*`; the ISR translation units no longer include
  `dll_main.h`. The shared slice is sectioned by writer/reader and every field
  carries its category prefix (`c_`, `m_`, `i_`, `b_`), enforced by
  `tools/check_dead_fields.py`. `IOLINK_STAT_ISR`/`IOLINK_STAT_TOTAL` take the
  shared and ISR-stat field names separately, so both stats modes stay valid.
- **Breaking:** the non-standard `0x0F` transition command is removed.
  `IOLINK_MC_TRANSITION_COMMAND`, `IOLINK_REPLY_ACT_ENTER_ESTAB_COM`,
  `IOLINK_REPLY_FLAG_RESTART_TIMER_B`, `DLL_EV_TRANSITION` and
  `dll_handle_preoperate()` are gone; V1.1.5 Table B.1 defines `0x0F` only as
  the DPP1 SystemCommand address, and Table 45 reaches ESTAB_COM through
  `0x99 DeviceOperate`. A 2-byte `MC = 0x0F` frame is now an ordinary Type-0 write: it gets a CKS
  reply and has the same state effect as any other Type-0 frame in that state
  (it no longer jumps to ESTAB_COM). Type-1/2 process-channel writes at
  addresses 0x0F..0x1F now use the generic write plan instead of being
  discarded as unplanned frames.
- **Breaking:** non-null public entry points no longer accept a NULL context and
  return a default. A missing required context is a contract violation, not a
  silent fallback.
- `iolink_dll_stats_t::total_retries` is now `invalid_frames`: it counts CRC
  failures and 0x97 DeviceStartup frames, not retransmissions (the library has no
  retransmission path). The stats gain `isr_eager_rejects` (moved into the
  shared slice) and `pd_in_skips`.
- Dead DLL state is removed: `wakeup_seen`, `retry_count`, `max_retries`,
  `isr_tx_slot`, `last_valid_frame_ms` and `pd_out_seen`. The data ISR no longer
  writes the master-activity fields; the main pump refreshes them in
  `dll_take_isr_master_activity()`. An ISR-serviced `0x97 DeviceStartup` now
  transitions on the next main-pump pass (the acknowledgement is unchanged).
- `dll_master_cycle_us()` returns 0 instead of an invented 2 ms when both cycle
  times are unknown; master-lost detection waits for a known cycle time.
- `examples/freertos_app` is removed (it was not part of any build and needs the
  FreeRTOS sources to compile).

### Changed (spec alignment)
- The byte-gap timeout uses the Table 47 `MaxUARTFrameTime` of **12 T_BIT**
  (11 + t1(max)), not the previous 16 T_BIT receive window. The COM1/COM2/COM3
  byte-gap limits are now 2500/313/53 us (rounded up to whole microseconds).
- The unused `IOLINK_OD_LEN_8BIT`, `IOLINK_OD_LEN_16BIT` and
  `IOLINK_OD_LEN_32BIT` macros are removed. Table A.10 defines the OD width in
  octets (1/2/8/32); `od_width` is rejected otherwise
  (`IOLINK_DEVICE_INIT_ERR_OD_WIDTH`) and above `IOLINK_OD_MAX_SIZE`
  (`IOLINK_DEVICE_INIT_ERR_OD_CAPACITY`). The macros had no users.

### Tooling and tests
- `tests/spec_constants.csv` pins 111 specification constants (e.g. t_A, the A.3.3-A.3.6 bit
  costs, T_FBD (Table 43), the Annex A layout, Table 10/42, Table B.3,
  B.8/B.9/B.12/B.13 and the Table C.1 ISDU codes)
  and `test_spec_constants` fails both ways when the CSV and the code drift; the
  timing literals in `src/` are named `IOLINK_*` constants in `protocol.h`.
- `tools/gen_docs.py` generates the error-code enum, the DLL stats table, the
  public-API table and the shared-ownership table (`--check`/`--write`/
  `--selftest`); `tools/check_docs_refs.py` checks symbols, links, script paths,
  shared-field names and error-code numbers.
- `tools/check_dead_fields.py`, `tools/check_public_api.py`,
  `tools/check_context.py` and `tools/stack_report.py` are enforced (the context
  gate reports zero violations). `tools/gates.sh` gained the `context` group;
  `quick` checks docs references, dead fields and the public API; `docs` checks
  the generated blocks; `perf` checks the target stack baseline. CI gained
  `static-analysis` and `docs-drift` jobs.
- `tools/isr_latency` now binds a device identity and builds the DPP1 image, so
  the page-read scenario actually receives a reply; it checks the first reply
  byte and the reply CKS.

### Changed (the log sink is locked when ISR logging is compiled in)
- With `IOLINK_LOG_ENABLE_ISR=1`, `iolink_device_set_log()` is a no-op once
  `iolink_device_init()` returns: the data ISR reads `shared.c_log.write`/
  `self`/`level`, so a runtime rebind raced with it. `iolink_dll_main_t` gained
  a `log_locked` flag (it fits the existing padding, so no context size
  changed) that re-initialisation clears. In the default build there is no ISR
  reader, so the main context may rebind the sink at runtime. The initial sink
  comes from `config.app->log_write` and `config.log_level` only.

### Tests (the event queue drain ordering is pinned)
- `iolink_events_pop()` documents why it drains the queue without a lock
  (`count` → `flag` → empty image) and `tests/test_events.c` pins that draining
  to empty withdraws the published Table 58 image and keeps
  `flag == (count > 0)` after every pop.

### Changed (the error-counter reset only locks the SPLIT counters)
- `iolink_dll_reset_error_counters()` no longer takes a critical section in the
  default `IOLINK_STATS_MODE=SHARED` build: clearing the counters while the ISR
  increments one can lose that update, which is the contract the counters
  already declare (diagnostics only). Under `SPLIT` the ISR-owned `isr_stat` is
  still cleared inside `IOLINK_CRITICAL_ENTER/EXIT()`, so its single writer
  holds.

### Changed (the event queue and pending table need no lock)
- `iolink_event_clear()`, `iolink_events_peek()`, `iolink_events_get_all()`,
  `iolink_diag_device_status()` and `iolink_diag_dds_snapshot()` no longer take
  `IOLINK_CRITICAL_ENTER/EXIT()`. They read or write `pending[]` and `queue[]`,
  which only the main context touches; the data ISR reads `flag`, `count` and
  the published Table 58 image. The event-side critical sections that remain
  are the table-wide clear in `iolink_events_reset()` (it must not lose an ISR
  one-shot pulse) and the pulse consumption in `iolink_diag_service()`.
- `iolink_events_peek()` and `iolink_events_get_all()` are main-context only:
  they read the queue. `iolink_events_flag()`, `iolink_events_pending()`,
  `iolink_events_memory_read()` and `iolink_events_get_dropped_count()` stay
  readable from either side.
- `tests/test_locking.c` pins zero critical-section entries for these paths;
  they previously asserted one entry for `iolink_event_clear()`.

### Changed (MAJOR: PD-in publication is one single-writer word)
- `iolink_dll_main_t::pd_valid` and `iolink_dll_shared_t::pd_in_active` are
  gone. Process input is published through `iolink_dll_shared_t::pd_pub`
  (`{active[7:0], valid[8], gen[23:16]}`, main-context writer) and the
  data-ISR writer `pd_valid_cmd` (`{value[0], base_gen[15:8], valid[16]}`).
  This is a layout change of both structs: `iolink_dll_ctx_t` and
  `iolink_device_ctx_t` grow by 8 bytes (4944 -> 4952 and 5136 -> 5144 in the
  64-bit default build, FRAME/FRAME 4936 -> 4944 and 5128 -> 5136).
- `src/device.c` no longer takes `IOLINK_CRITICAL_ENTER/EXIT()` for the PD-in
  publish. The callback and the polling API fill a buffer outside any lock and
  then write `pd_pub` with `IOLINK_FENCE()` plus one atomic store; the data ISR
  loads that word once per reply, so the buffer it captures and the CKS
  PD-valid bit it reports always belong to the same generation.
- 0x98 `PROCESS_DATA_OPERATE` / 0x99 `DEVICE_OPERATE` still override the valid
  bit, but only for the generation in which they were applied: the data ISR
  records the command value together with the current `pd_pub` generation in
  `pd_valid_cmd`, and every reader falls back to the application value as soon
  as the generation moves on. A main-context command application (STARTUP probe
  / verbose OPERATE path) publishes a new generation instead. The ISR retires a
  stale command word on its next entry, so the 8-bit generation cannot wrap
  back onto an expired command.
- `tests/test_reply_plan.c` pins the same-generation capture and the
  generation-scoped 0x98/0x99 override including the CKS bit; the context-size
  contract and `docs/MEMORY_GUIDE.md` record the +8 bytes.

### Changed (BEHAVIOUR CHANGE: the data ISR owns the PD-in lease release)
- TX FRAME: `dll_process_body()` no longer polls `tx_busy()`, and the
  `IOLINK_CRITICAL_ENTER/EXIT()` pair that guarded the poll and the
  `pd_in_captured` reset is gone with it. The data ISR releases the lease at
  entry (`dll_isr_process()`, before any new capture) when `tx_busy()` reports
  the line idle, so `pd_in_captured` has a single writer.
- **Behaviour change:** during master silence no data interrupt arrives, so the
  lease of the last reply is held until the next ISR entry instead of being
  returned by the next main-pump pass. The main pump can still fill the other
  buffer; only two PD-in updates during the same silence make the second one
  fail and count `pd_in_skips` (the released lease used to allow it). An
  in-flight buffer is still never overwritten: the release requires `tx_busy()`
  to report an idle line.
- A TX FRAME PHY without `tx_busy()` is unchanged: the transfer end stays
  unobservable, the lease is kept until the next capture and
  `IOLINK_PD_IN_BUFFERS` stays at three. TX BYTE keeps releasing at frame end.
- `tests/test_tx_busy.c` and `tests/test_pd_lease.c` assert the new release
  point; the new `tests/test_pd_lease_frame.c` pins the silent-period skip and
  the intact in-flight data.
### Fixed (documentation matches the lock-free hand-off model)
- `include/iolinki/port.h`, `docs/PORTING.md` and `docs/API_INTEGRATION.md`
  claimed the critical section protects the frame mailbox, the ISDU snapshot,
  reply staging or the event-queue write path. None of those take a section:
  they are slot rings, publish words and leases. They now list the operations
  that do take one — diagnostic condition table, event queue read/clear,
  counter reset, the SIO-mode reply-slot cleanup and the ISR RX phase reset —
  and drop the removed "a DLL state transition resets the event queue under the
  lock" example from the nesting requirement.
- `docs/CODE_FLOW.md`: the remaining-critical-section audit no longer names the
  removed `isr_tx_has_cks` field (the SIO-mode entry clears four fields) or a
  "log sink" section that does not exist, and the hook table states that
  `iolink_dll_publish_reply_plans()` rejects a call from the data ISR.
- `docs/API.md` documents the quiescent-entry contract shared by
  `iolink_dll_init()`, `iolink_dll_publish_reply_plans()` and
  `iolink_isdu_bind_workspace()`, including which of them can refuse at run
  time.
- `include/iolinki/diag.h` names both halves of the `isr_pulse[]` one-shot: the
  ISR only stores 1, `iolink_diag_service()` reads and clears it inside one
  short section.

### Changed (MAJOR: `iolink_dll_publish_reply_plans()` reports misuse)
- The reply-plan publisher returns `int` (`iolink_dll_publish_result_t`)
  instead of `void`: `IOLINK_DLL_PUBLISH_OK` (0) when the table was published,
  otherwise `IOLINK_DLL_PUBLISH_ERR_NULL` (`ctx == NULL`),
  `IOLINK_DLL_PUBLISH_ERR_UNINIT` (the context never completed
  `iolink_dll_init()`, detected by the unbound `shared.isr`) or
  `IOLINK_DLL_PUBLISH_ERR_ISR` (`shared.in_isr != 0`). A rejected call writes
  no plan slot, leaves `reply_plans_ready` alone and, for the ISR case, logs an
  ISR error. Rebuilding the table from the data ISR used to rewrite the table
  the pump was scanning, with no counter and no error.
- The two in-library call sites (`iolink_dll_init()`, `iolink_device_init()`)
  keep publishing at init time and ignore the result of the unreachable
  refusal paths.
- `include/iolinki/dll.h` and `include/iolinki/isdu.h` now state one
  precondition for `iolink_dll_init()`, `iolink_dll_publish_reply_plans()` and
  `iolink_isdu_bind_workspace()`: main context, data pump quiescent. The latter
  two refuse a call from the data ISR at run time; `iolink_dll_init()` cannot,
  because it clears the object that holds `shared.in_isr`.
- Tests: `tests/test_reply_plan.c` pins the NULL, uninitialised and in-ISR
  outcomes and checks that a refused call leaves the previous table and the
  ready flag unchanged.

### Fixed (CONSUME frame timestamp handoff publishes with a fence)
- `dll_isr_complete_frame()` now writes `isr_consumed_us`, passes
  `IOLINK_FENCE()` and only then bumps `isr_consumed_seq`, so the sequence
  number is a release edge rather than a second plain store.
  `dll_take_isr_consumed_frame()` reads `seq -> IOLINK_FENCE() -> us ->
  IOLINK_FENCE() -> seq`, which makes the double-read acceptance test hold
  under `IOLINK_CONCURRENCY=SMP` (two cores or two threads) instead of relying
  on "the writer's interrupt return is visible to the reader". The 8-bit
  free-running sequence, the "exactly 256 frames between two main passes miss
  one update" behaviour and the context layout are unchanged.
  `tests/test_isr_consumed_timing.c` pins the publish order with a stepping
  clock and covers the accept, no-new-frame, multi-publish and 8-bit wrap
  paths.
### Changed (MAJOR: ISDU write entry is two request slots + a publication word)
- **`iolink_isdu_scratch_t::request` is now two slots** of
  `IOLINK_ISDU_BUFFER_SIZE + 6` bytes (one being accumulated by the data ISR,
  one published for the main pump). This is a layout change of a public
  struct: `iolink_workspace_t` grows by `IOLINK_ISDU_BUFFER_SIZE + 6` bytes
  (1289 -> 1551 at the default 256-byte buffer); `iolink_isdu_ctx_t` grows by
  the same amount when the scratch is embedded (1336 -> 1616), plus 18 bytes
  of slot bookkeeping (24 bytes with `IOLINK_WORKSPACE_EMBEDDED=0`).
  `IOLINK_ISDU_STAGE_ABORT` is gone: the ISR settles an ABORT itself.
- The data ISR owns the write entry end to end (slot selection, accumulation
  state, `stage_state`) and publishes `{len, sel, gen, receiving}` in a single
  natural-atomic word (`iolink_isdu_ctx_t::req_pub`); the main pump only loads
  that word. It claims the published slot (`req_busy_sel`), validates the
  CHKPDU, parses the header and copies the payload **outside any critical
  section**, then records `req_ack_gen` and releases the slot before running
  the service and the application callbacks. `iolink_isdu_process()` no longer
  takes a critical section and never re-reads `stage_state`.
- While the main pump works on one request the ISR can complete the next one in
  the other slot. A START never writes the slot being parsed; it replaces a
  published but unclaimed request instead, which is withdrawn (`len = 0` with
  a fresh generation) and counted in `iolink_isdu_ctx_t::req_overruns`,
  exported unchanged as `iolink_dll_get_stats().isr_od_overruns`. A replaced
  request is never executed.
- `iolink_isdu_od_write()` publishes a `receiving` generation as soon as a
  write START is accepted, so the main pump keeps the published read view
  empty for the whole transfer without reading any ISR phase field. The
  write-abort path also asks the main context to reset the read transport
  (`isr_isdu_read_reset`, Table 52), which the main pump now applies
  unconditionally: `isdu_read_reset()` touches only read-transport fields, so
  the write-entry phase guard is gone ("read ABORT immediately followed by
  write START" can no longer drop the write). The flag itself is still set by
  the ISR and cleared by the pump, so the read-clear stays inside one short
  critical section (the fast path takes no lock when nothing is pending). `isdu_transport_reset()` is removed; `isdu_read_reset()` is the only
  read-side reset.
- `iolink_isdu_execute_system_command()` (Direct Parameter page 1) still drops
  a staged read response but no longer discards an already received ISDU write:
  every segment of that write was acknowledged, and the write entry now belongs
  to the data ISR alone.
- Ordering: slot contents are written before the publication word
  (`IOLINK_FENCE()` + one atomic store) and the claiming side stores
  `req_busy_sel` before reading the slot, so under
  `IOLINK_CONCURRENCY=SMP` a reader never pairs a fresh generation with stale
  bytes, and the ISR never overwrites a slot the main pump is parsing.
- Tests: `tests/test_isdu_slots.c` drives the three cut points (new request
  during service execution, replacement of an unclaimed request, slot reuse
  after the ack);
  `tests/test_isdu_slots_small.c` repeats the flow against a
  `IOLINK_ISDU_BUFFER_SIZE=64` build.

### Changed (ISDU write invalidation uses a monotonic sequence)
- The clearable `isr_isdu_invalidated` flag is gone. The data ISR bumps
  `isr_isdu_write_gen` on every accepted write START/ABORT; the main pump records
  `isdu_write_gen_applied` after it has applied (or aborted) that write and
  published the new read view. While the write entry has not returned to IDLE,
  the ISR read path answers `0x00` and `dll_isdu_sync_snapshot()` publishes
  `len = 0`, so a read that follows a write cannot return the previous response.
  The acknowledgement is written only after the new view is published, so an ISR
  that observes equal sequence numbers always reads the new view (at worst one
  extra "no service"). `dll_isdu_sync_snapshot()` no longer takes a critical
  section. Both fields fit in alignment holes of `iolink_dll_shared_t`; context
  sizes are unchanged.
### Changed (BEHAVIOUR CHANGE: ISR/SMP reject a TX PHY without `request_service()`)
- **Behaviour change:** under `IOLINK_CONCURRENCY=ISR` (the default) and `SMP`,
  `iolink_device_init()` now returns `IOLINK_DEVICE_INIT_ERR_PHY` for a PHY that
  has TX capability (`IOLINK_CAP_TX`), has no `request_service()` and does not
  set `IOLINK_CAP_REQUIRE_ISR_TX`. Such a PHY initialised successfully before
  and silently dropped every main-context reply (`dll.isr_tx_dropped`); it now
  has to provide the hook or be built as `SINGLE`. The check lives with the
  other PHY capability checks in `device_config_validate()`.
  `IOLINK_CAP_REQUIRE_ISR_TX` keeps its own, stricter requirement (the hook is
  mandatory when the bit is set).
- The direct-send branch of `dll_send()` is now compiled only under
  `IOLINK_CONCURRENCY_SINGLE`, the only profile without a data ISR: there the
  main context is the only sender and drives the TX hooks itself. Under
  `ISR`/`SMP` a staged reply reaches the line only through the pump, so the
  remaining path counts `isr_tx_dropped` (reachable only by callers that use
  `iolink_dll_init()` directly and skip the device-layer validation).
- The `IOLINK_CRITICAL_ENTER/EXIT()` pair around that branch is gone; the
  remaining branch runs only in `SINGLE`, where the macros are empty.
- Local test PHYs with TX gained a no-op `request_service()` and their service
  loops drain `isr_tx_pending`; `tests/test_capability.c` covers the rejection,
  the accepted hook and the `REQUIRE_ISR_TX` interaction.

### Changed (PHY mocks hand main-context replies to the ISR pump)
- The shared test mock (`g_phy_mock`), the frame fuzzer, the ISR latency
  harness, the generic template PHY and both host examples now provide
  `request_service()`. The test pump (`test_io_cycle()` and the helpers built on
  it) drains the staged reply through `iolink_dll_process_isr()` instead of
  letting `dll_send()` drive the PHY directly, so the harness models the port
  contract the ISR profiles expect. `tests/test_capability.c` asserts
  the capability bit for the mock and keeps the `IOLINK_CAP_REQUIRE_ISR_TX`
  rejection covered with a local PHY that has TX but no `request_service()`.
  The direct-send branch itself is restricted to `SINGLE` (see the entry above).

### Fixed (DPP1 image reads acquire the sequence; workspace rebinding refuses in the ISR)
- `iolink_isdu_direct_param_page1_octet()` and
  `iolink_isdu_dp_page1_read_range()` order their loads the way the writer
  orders its stores: sequence, `IOLINK_FENCE()`, image, `IOLINK_FENCE()`,
  sequence re-read. Under `IOLINK_CONCURRENCY=SMP` that is the acquire a
  seqlock reader needs; under `ISR` it is a compiler barrier with no run-time
  cost. The 3-attempt retry and the "0 / all zero" outcome are unchanged.
- `iolink_isdu_bind_workspace()` is refused (pointers and transport state
  unchanged) when it is called from the data ISR, where the request/response
  buffers it rewrites are in use. Binding stays a quiescent-pump operation;
  the public header now states it.

### Changed (ISDU read reset keeps the write entry phase)
- The read transport reset is now a separate entry, `isdu_read_reset()`, that
  touches only the main-context read fields; the write-entry phase
  (`req_idx`/`req_total`/`flowctrl_seen`/`stage_state`) belongs to the data ISR
  and is never reset from the pump. The read paths of `iolink_isdu_od_read()`
  (IDLE, IDLE2, ABORT and a drained response) use it.
  A read reset no longer clears a write request the data ISR has just started,
  so a master that aborts an idle read and immediately starts a write keeps the
  request instead of having every acknowledged COUNT dropped.
- `isdu_stage_release()` names the write-entry phase reset that
  `iolink_isdu_od_write()` and the main pump (on COMPLETE/ABORT takeover) use.
- The read path no longer writes the write entry's FlowCTRL watermark: a read
  COUNT is accepted while `resp_total != 0`, i.e. after a read START, so read
  segment sequencing cannot move the write dedupe position.
  The wire bytes of every read/write sequence are unchanged.
### Fixed (Back-to-box cycles have a single writer)
- `dll_mark_master_activity()` was called from the data ISR on the CONSUME path
  and did `back_to_box_cycles++`, a read-modify-write on a field the main
  context also advances. A lost update delayed the 10.7.5 "stop after 3
  MasterCycles" decision. The function is now timestamp-only and renamed
  `dll_mark_master_activity_ts()` (it no longer has a counting variant); ISR
  frames set the one-shot `shared.isr_cycle_served` after their bookkeeping, and
  the pump consumes it in `dll_apply_isr_deadline_requests()`: clear, then
  `dll_count_served_cycle()` once if a Back-to-box is pending. A pump pass
  therefore advances the counter at most once even when the ISR served several
  frames in between (the count errs conservative — it never stops early).
  Main-context frames keep advancing the counter directly in
  `dll_process_complete_frame()`. The SIO wakeup path no longer counts a cycle:
  no request was served.
- `iolink_dll_shared_t` gains `isr_cycle_served` (one byte, absorbed by existing
  padding: context sizes are unchanged).

### Fixed (SIO switching clears the ISR reply state under the lock)
- `iolink_dll_set_sio_mode()` cleared `isr_tx_pending`/`isr_tx_active`/
  `isr_tx_idx`/`pd_in_captured` with plain stores. The data
  ISR writes the same fields, so a switch between two byte steps left a
  half-frame with `tx_done` never called (and a staged reply silently dropped),
  with nothing counted. The cleanup now runs in one critical section; a staged
  or in-flight reply that it discards counts once as `isr_tx_dropped`
  (main-context field; `SPLIT` mode still sums it with the ISR copy in
  `iolink_dll_get_stats()`). `phy.set_mode` stays outside the lock.

### Fixed (an ISDU read reset no longer discards an in-flight write request)
- `dll_process_body()` applied the ISR's IDLE/IDLE2/ABORT read-reset request
  unconditionally. The read reset used to clear the whole transport, including
  `req_idx`/`req_total`/`flowctrl_seen`/`stage_state` — the write entry the data
  ISR may have just rebuilt in the same exchange. A master that aborted a read and immediately started a write saw
  every COUNT acknowledged while the request was dropped whole. The write entry
  now belongs to the ISR, which removes the race at the ownership level; the
  flag itself is still set by the ISR and cleared by the pump, so the read-clear
  runs in one short critical section (the fast path stays lock free when no
  request is pending).
### Added (the event memory is published, not locked)
- The Table 58 event memory is now a double image with a published index:
  `iolink_events_ctx_t::memory[2][19]` plus `volatile uint8_t memory_pub`. The
  main context writes the unpublished image, fences, then publishes the index;
  `iolink_events_memory_read()` loads the index, fences, and reads one byte. A
  reader (the data ISR) therefore always sees one complete generation instead of
  a byte-by-byte snapshot taken under the lock.
- `iolink_events_reset_request()` (`src/events_internal.h`) is the only write the
  data ISR performs on the event engine proper: it raises `volatile uint8_t
  reset_req` above `reset_applied`. (The one-shot `diag.isr_pulse[]` bytes
  remain ISR-written and are consumed by `iolink_diag_service()`.) `dll_set_state(STARTUP)` calls it when the
  transition happens in the ISR; a main-context transition still calls
  `iolink_events_reset()` directly.
- The main pump applies the request (`dll_process_body()` and
  `iolink_dll_flush_state_callbacks()`, both before any diagnostics or state
  callback) by running the full reset and setting
  `reset_applied = reset_req`. While the two counters differ,
  `iolink_events_flag()` reports `false` and `iolink_events_memory_read()`
  reports `0`, so the 0x97 DeviceStartup frame carries no pre-reset CKS Event
  bit and Table 58 reads no pre-reset content.
- The main-only write entries (trigger, clear, process, clear_diagnosis, reset,
  memory_write, pop) are rejected while the bound `isr_active` flag is set;
  `iolink_dll_init()` binds it to the DLL's own ISR-depth flag.
  **MAJOR**: `include/iolinki/events.h` gains `memory_pub`, `reset_req`,
  `reset_applied` and `isr_active`, and `memory` becomes a double image
  (`iolink_events_ctx_t` 480 → 504 B on a 64-bit host).
- `iolink_event_trigger()`, `iolink_events_process()`,
  `iolink_events_clear_diagnosis()`, `iolink_events_reset()`,
  `iolink_events_memory_write()` and `iolink_events_pop()` no longer take a
  platform critical section. The queue read paths (`iolink_events_peek()`,
  `iolink_events_get_all()`, `iolink_event_clear()`) keep theirs.

### Changed (event timestamps are sampled before the lock)
- `iolink_event_trigger()`, `iolink_events_process()` and
  `iolink_events_clear_diagnosis()` now read the bound application clock once,
  before `IOLINK_CRITICAL_ENTER()`. The callback is application code, so the
  time spent with interrupts disabled is now bounded by the event bookkeeping
  itself instead of the clock read; each call still uses that single timestamp
  throughout.

### Changed (the diagnostic source is written with the event, not after it)
- `iolink_diag_set_level()` and `iolink_diag_pulse()` used to call
  `iolink_event_trigger()` and then take a second critical section to look the
  code up in `pending[]` again just to store its source. The source is now
  passed into the trigger (`src/events_internal.h`: `iolink_event_trigger_src()`)
  and written in the same critical section that allocates the slot, so a
  diagnostic raise costs one short lock instead of two. The public
  `iolink_event_trigger()` is unchanged in signature and passes
  `IOLINK_DIAG_SRC_NONE`.

### Fixed (Timer A timeout resets RX assembly in one phase)
- `iolink_dll_on_rx_timeout()` (Timer A) cleared `isr_rx_cnt`/`isr_rx_expect`
  with two bare stores although those fields belong to the data ISR: a pump that
  ran between the two writes re-seeded a half frame, and the following byte
  stream was assembled against a stale expected length. Both stores now sit in
  one short `IOLINK_CRITICAL_ENTER/EXIT()` window (no callbacks, no waits). The
  critical section cannot protect against a timeout vector that *preempts* the
  data pump, so `docs/PORTING.md` now requires the data interrupt and Timer A/B
  to run at the same priority or in the same vector, and states how a port that
  cannot do that must serialise the entries itself. Regression:
  `test_isr_atomicity` pins the single-phase reset.

### Fixed (the staged-reply hand-off is one operation)
- `dll_send()` used to check `isr_tx_active` and publish the staging as two
  separate steps: a data interrupt between them could start an immediate reply,
  and the next ISR entry then replaced that in-flight frame from the middle. The
  slot advance, payload copy, metadata and `isr_tx_pending` now happen inside
  one short critical section, and a rejected staging no longer touches the slot
  (`isr_tx_next` only advances when the reply is accepted, so two consecutive
  rejections cannot rotate back onto the in-flight slot and rewrite its bytes).
  The ISR take reads the metadata and clears `isr_tx_pending` under the same
  lock, and drops plus counts a staging that arrives while a frame is still
  streaming. `SINGLE` closes the previous frame with `tx_done()` before starting
  the next one and counts that discard. With no TX hook in the selected mode the
  reply is counted as dropped instead of vanishing.

### Fixed (an abandoned ISDU write is retired on a communication restart)
- A write START publishes a generation whose ack only advances on completion or
  abort, so a master that started a write and went silent left the read path at
  `0x00` and the events/DS gate closed across link restarts. A main-context
  STARTUP now retires an unfinished generation and resets the read transport
  (an ISR-triggered STARTUP defers both to the pump through
  `isdu_restart_pending`); a completed request is still executed by the same
  pump pass instead of being discarded. Claiming a request with no bound
  workspace advances the ack instead of hanging.

### Fixed (staged reply length and CKS are ISR-owned)
- The staged reply's length and CKS flag were shared between the main pump and
  the data ISR: `dll_send()` wrote `isr_tx_len`/`isr_tx_has_cks` while the ISR
  re-read them for every streamed octet. A main-context `dll_send()` during an
  in-flight reply could therefore shorten it (skipping CKS, ending the frame
  early and releasing the PD-in lease) or hand a fresh staging to a stream that
  was already running. The staging metadata is now main-context only
  (`stage_len`/`stage_has_cks`, adjacent to `isr_tx_pending_slot`) and the ISR
  copies `{slot, len, has_cks}` into its own stream state
  (`iolink_dll_isr_t::tx_stream_len`/`tx_stream_has_cks`) when it takes
  `isr_tx_pending`; `dll_isr_tx_step()` reads nothing else. `dll_send()` also
  rejects a staging while `isr_tx_active != 0` and counts it in
  `isr_tx_dropped`, and `dll_isr_start_staged_tx()` takes the CKS flag as a
  parameter (TX BYTE no longer ignores the length). The 64-bit host context
  layout is unchanged; on a 32-bit target the two new ISR fields add 4 bytes to
  `iolink_dll_isr_t` in the TX BYTE configuration.
  Regression: `test_isr_atomicity` covers a staging overwritten mid-stream and a
  rejected staging during an in-flight reply.


### Removed (reply plans carry no timing field and are compiled once)
- `iolink_reply_plan_t::deadline_us` is gone. Nothing read it at run time: the
  ISR arms t_A from `10 * dll_get_t_bit_us()` when it starts a reply
  (`dll_sm.c`), and the PHY response timer is the authority. The field was
  written once per plan and asserted only by a unit test, and its "timing audit /
  WCET" comment described a use that did not exist. On a 32-bit target the plan
  entry drops from 20 to 16 bytes (alignment no longer absorbs the field), so the
  48-slot table loses 192 B of RAM; on a 64-bit host nothing changes.
  **MAJOR**: `include/iolinki/dll.h` loses a public struct field.
- Baudrate no longer affects plan content or publication. It used to enter plan
  content only through `deadline_us`; the table is compiled once at init and
  never rebuilt. (COM is now build-time fixed as well.)

### Removed (capabilities and the reply plan are fixed at init)
- `iolink_device_set_capabilities()` is gone. Capability bits are inputs to the
  reply-plan table, and the table is no longer rebuilt at run time; a runtime
  override could therefore leave the ISR executing plans that disagreed with
  `iolink_device_capabilities()`. Capabilities come from `iolink_caps_resolve()`
  once, inside init; request different switches through `iolink_stack_config_t`
  (`compat_flags` / `flags`) and re-initialise. **MAJOR**:
  `include/iolinki/device.h` loses a public function.
- Behaviour change that came with it: the four DeviceAccessLocks bits can no
  longer be narrowed per instance. `iolink_caps_resolve()` always sets
  `IOLINK_CAP_LOCKS_ALL`, so `isdu_locks_implemented()` (the 0x000C writable
  mask) is always the full four bits and a master writing an implemented lock is
  accepted. Previously an application could clear a bit through the removed
  setter; `iolink_stack_config_t` has no locks field, and none was added because
  the library implements all four.
- `iolink_dll_invalidate_reply_plans()` is gone, and so is the `reply_plan_seq`
  seqlock: `reply_plans_ready` (clear before the table is written, set after)
  replaces it, `iolink_dll_process()` no longer republishes every call, and
  `dll_isr_plan_lookup()` no longer reads a sequence number before and after
  copying the hit. `iolink_dll_publish_reply_plans()` stays public for harnesses
  that configure a context after `iolink_dll_init()`, documented as requiring a
  quiescent data ISR. The flag gates the ISR **entry** only: while it is clear,
  a frame that has not started yet is not processed at all (not counted, not
  queued, no reply), and RX is not drained; it does not make a rebuild safe
  against an ISR already inside the pump — that is what the quiescence
  precondition covers. **MAJOR**:
  `include/iolinki/dll.h` loses a public function.
- `reply_plan_overflows` now describes the current table rather than a running
  total: `iolink_dll_publish_reply_plans()` resets it before compiling, so the
  two publications inside `iolink_device_init()` (default configuration first,
  final configuration second) cannot double-count a throwaway table.
- Cost: on cortex-m0plus `-Os`, `dll_isr_plan_lookup()` drops from 119 to 97
  instructions and `dll_isr.o` `.text` shrinks by 44 B; `isr_plan_misses` now
  means only "the current state's slice has no matching plan", not "collided
  with a republish window".

### Removed (PD length is fixed at init)
- `iolink_dll_set_pd_length()` and `iolink_device_set_pd_length()` are gone
  (**MAJOR**). DPP1 0x05/0x06 are read-only fields (Table B.1), so the process
  data length is a device property fixed by `iolink_stack_config_t`; there is no
  run-time width change in any state. `iolink_device_pd_input_update()` requires
  `len` to equal the configured PD-in length and rejects anything else with -1,
  instead of silently changing the announced layout (and the DPP1 image).
- The `pd_in_len_current`, `pd_in_len_max`, `pd_out_len_max` and `pd_out_len`
  mirrors are gone. `shared.pd_in_len` and `shared.pd_out_len_current` are the
  single init-time values, read by the ISR and by the DPP1/0x0028 paths; the
  64-bit context sizes shrink by 8 B (BYTE 4656/4848, pinned in
  `test_ctx_sizes`).
- `iolink_device_pd_input_update()` takes the configured PD-in length as the
  fixed contract: a different `len` returns -1 instead of silently changing the
  announced layout. (A TYPE_0 profile normally configures 0; the library does
  not reinterpret the configured length per M-sequence.)

### Removed (test-only and init-only timing knobs)
- `iolink_dll_set_t_ren_limit_us()` and `iolink_device_set_t_ren_limit_us()` are
  gone (**MAJOR**). t_REN is the fixed Table 10 value (`IOLINK_T_REN_US`, 500 us);
  the override fields and both setters existed only for one unit test. Use the
  compile-time override in `config.h` if a test needs another bound.
- `iolink_dll_set_timing_enforcement()` and
  `iolink_device_set_timing_enforcement()` are gone (**MAJOR**): the stack
  config (`iolink_stack_config_t::timing_enforce`) is the only input, and the
  field is written directly during init.
- `sio_fallback_threshold` is now the file-local `DLL_CRC_DIAG_BATCH` constant
  (it only throttles the COMM_CRC diagnostic pulse; it never switched to SIO).
- `t_pd_delay_us` and `response_time_us` are gone: the first was an init-local
  temporary, the second had no reader.

### Changed (COM is a compile-time constant)
- The transmission rate is now the build-time `IOLINK_COM` (1/2/3, default 2) from
  `include/iolinki/config.h`; `IOLINK_COM_HZ` and the bit time fold at compile
  time. `iolink_stack_config_t::baudrate` / `baudrate_set` are gone
  (**MAJOR**), as are `iolink_dll_set_baudrate()`,
  `iolink_dll_set_operating_baudrate()`, `IOLINK_BAUDRATE_UNSET` and the COM1
  fallback/restore machinery: SIO and fallback only switch the line mode, the
  UART rate never changes.
- `iolink_dll_init()` no longer touches the UART: the PHY configures it in `init()`
  from `IOLINK_COM_HZ`. **MAJOR**: `include/iolinki/phy.h` loses the
  `set_baudrate` vtable member.
- `IOLINK_DEVICE_INIT_ERR_BAUDRATE` is gone; an invalid COM is a compile-time
  `#error`. Later init error codes shift by one.

### Changed (initialisation wiring is no longer public API)
- `iolink_dll_set_sio_mode()`,
  `iolink_dll_set_sdci_mode()`, `iolink_dll_reset_error_counters()`,
  `iolink_dll_reset_diagnostics()`, `iolink_events_set_window()`,
  `iolink_ds_set_flag_persist()` and `iolink_isdu_dp_page1_invalidate()` moved to
  `include/iolinki/internal/wiring.h` (**MAJOR**: the public declarations are
  gone). They are called by the initialisation and protocol paths; applications
  configure the same behaviour through `iolink_device_config_t` /
  `iolink_stack_config_t`.
- `iolink_device_set_log()` moved to `include/iolinki/internal/device_wiring.h`
  (a separate header so DLL translation units do not pull `device.h`).
- Logging is init-only: the sink and level come from `config.log_write` /
  `config.log_level`; the runtime switch and its critical section are gone.

### Added (compile-time log level)
- `IOLINK_LOG_COMPILE_LEVEL` (`iolinki/log.h`, default `IOLINK_LOG_COMPILE_DEBUG`)
  compiles messages above the chosen level away together with their format
  strings. The runtime `iolink_log_sink_t::level` still filters what a compiled-in
  message prints; it never removed the string from the image, so a build that
  wants a smaller flash image had no way to drop diagnostics.
- The ROM/RAM baseline is a ROM budget, not a diagnostics budget:
  `tools/size_matrix.py` now measures with `-DIOLINK_LOG_COMPILE_LEVEL=0` and both
  `tools/size_baseline.csv` and `tools/size_baseline.atfe.csv` are re-recorded
  (c1_full: about 1 KB less `.text` and 0.7 KB less `.rodata`). `docs/PORTING.md`
  says so; `build_matrix.sh` gains a `logs_off` compile probe.
- `tests/test_log_compiled_out` is built at level 0 and asserts that a sink with
  the runtime threshold wide open is never called and that an argument with a
  side effect is never evaluated: the property the budget relies on is guarded,
  not assumed.

### Changed (nullability model: non-null by default)
- Public headers declare `IOLINK_ASSUME_NONNULL_BEGIN/END` around their
  declarations (`_Pragma("clang assume_nonnull ...")` on clang, empty on gcc),
  so a pointer is non-null unless it is marked. `IOLINK_NULLABLE` now marks the
  cases where NULL is a meaningful input — an optional hook, "no application
  table", "measure only" — and the return types whose NULL means "not found".
- A function may still guard `if (ctx == NULL) return err;` internally. That
  guard is a fallback for a caller bug, and the parameter stays non-null. The
  exception is a NULL that *is* a state the interface defines rather than a value
  the caller forgot to fill in: the parameter is then `IOLINK_NULLABLE`, the
  guard implements the declared state, and the header says what NULL means.
  Marking such a parameter nullable otherwise would write "you may pass garbage"
  into the interface.
- `iolink_diag_device_status()` takes `iolink_events_ctx_t* IOLINK_NULLABLE`: a
  standalone ISDU engine (`iolink_isdu_init()` without the DLL) has no diagnosis
  registry, and "no registry" means DeviceStatus 0, not an error. The behaviour
  was already there (and asserted); the interface now declares it instead of
  leaving the internal `if (events == NULL)` looking like a fallback.
- `IOLINK_NONNULL` stays for nested pointer types (arrays of pointers), which
  `assume_nonnull` deliberately does not infer.
- The model now covers every public and internal header, not only the six that
  introduced it (`application`, `od`, `dll`, `isdu`, `events`, `data_storage`).
- PHY callbacks take `iolink_app_t* IOLINK_NULLABLE self`: the library forwards
  whatever the binding holds, and a callback that does not use the context is
  valid. Every hook in the vtable is `IOLINK_NULLABLE`: `iolink_caps_resolve()`
  probes each one and the library calls it only when the matching capability bit
  is set (`int_active` keeps its own NULL check), so a NULL hook means "this PHY
  does not offer that capability". `rx_frame` also returns
  `IOLINK_NULLABLE` — "no complete frame yet" is a normal answer, not an error.
- The driver headers follow the same reading of the code:
  `iolink_phy_l6362a_get()`/`iolink_phy_tiol112_get()` return `IOLINK_NULLABLE`
  (both implementations return NULL for an uninitialised driver), their
  `read_ol`/`read_diag`/`read_nfault` board callbacks are optional, and
  `iolink_device_info_t::product_uri` (0x001B) and `application_tag` (0x0018)
  are nullable: both are answered with an empty value when absent
  (`IDX_NOT_AVAIL` for the URI, an empty tag that Data Storage can later
  write).
- Lookup functions keep "NULL means not in the table" in the return type:
  `iolink_od_at()`, `iolink_diag_desc()`, `iolink_diag_registry_at()`,
  `iolink_diag_src_desc()`, `iolink_dll_sm_cell()`, `iolink_ring_peek()`,
  `iolink_ring_claim()`, `iolink_phy_virtual_get()` and
  `iolink_device_get_workspace()`.
- `iolink_log_sink_init(self)` and `iolink_device_set_log(write)` accept NULL:
  a sink without a context and "silence the log" are documented actions, not
  caller bugs.
- `iolink_app_t*` — the application's opaque context, i.e. the value of
  `iolink_device_app_t::self` forwarded to every hook — is `IOLINK_NULLABLE` at
  every declaration: the hook typedefs, the log sink's copy, the OD/ISDU/DS
  contexts, the PHY `self` and the test doubles. It is one type with one
  meaning, so the marker belongs on the type instead of on a hundred
  parameters, and declarations, definitions and tests cannot drift apart.
- `iolink_isdu_is_idle()` no longer claims that a NULL context is legal: that
  guard returns `false` for a caller bug, the same shape as
  `iolink_device_get_state()`'s default, and the parameter stays non-null.

### Added (nullability invariants gate)
- `tools/check_nullability.sh` (in `quick`) enforces the two properties the
  macro model rests on: a header that opens a region includes
  `"iolinki/types.h"` directly, and the region is not nested inside an `#if`.
  Both failed during the rollout — 18 headers used the macro without the
  include, and `port.h` opened the region inside
  `#ifdef IOLINK_PORT_CONFIG_FILE`, which disappears whenever that switch is
  off. `--selftest` covers a well-formed header, a missing include, a
  conditional region and an unbalanced pair.

### Fixed (Data Storage download without a staging buffer)
- `ParamDownloadEnd` decided "there is an image to commit" from `image_len > 0`,
  but `iolink_ds_serialize()` also writes `image_len` when it measures without a
  buffer (`out == NULL`) — that value is what `DataStorageIndex` reports as the
  parameter-set size — so the two are not equivalent. With
  `IOLINK_WORKSPACE_EMBEDDED=0` and no workspace bound, the commit path handed a
  NULL buffer to `iolink_ds_apply_image()` and answered PAR_SETINVALID
  ("malformed image") for a state that simply had nothing staged. It now fails
  explicitly with PAR_SETINCONSIST (0x8041) and leaves the reported size alone.

### Added (static analyzer gate)
- `tools/gates.sh analyzer` runs `clang --analyze` over `src/` and fails on any
  finding. It runs per translation unit instead of through `scan-build`: that
  tool intercepts compilation with a `cc` wrapper on `PATH`, and
  `configure_tree()` pins `CMAKE_C_COMPILER` to an absolute path, so the wrapper
  never saw a single invocation — a deliberate null-dereference probe passed
  unnoticed. The per-file form catches the same probe in 11 s.

### Changed (ATFE 23.1.0)
- The ROM/RAM baseline is re-recorded with ATFE 23.1.0, the version the
  firmware is built with, and the test image pins the same release (version and
  SHA-256 as build args) at `/opt/atfe`, which is also
  `tools/size_matrix.py`'s default. Measuring 22.1.0 numbers against a 23.1.0
  toolchain silently fell back to the wider cross-toolchain budget, so the gate
  was looser than it claimed.

### Added (memcheck gate)
- `tools/gates.sh memcheck` runs the unit binaries under valgrind memcheck, the
  only tool here that reports reads of uninitialised memory — the class the
  empty-context bugs belonged to. The PTY conformance examples stay with the
  `test` group (they spawn a device process).
- Two host/toolchain mismatches are handled instead of being reported as
  findings: valgrind cannot redirect loader symbols on Arch's glibc 2.44, and
  valgrind 3.18 (Ubuntu 22.04) cannot parse DWARF 5, so the memcheck tree is
  built with `-gdwarf-4`. The group probes valgrind once and skips with the
  reason when it cannot start at all; the container installs `valgrind` and
  `libc6-dbg` and runs the group for real.

### Added (sanitizer gate)
- `tools/gates.sh sanitize` runs the unit suite under AddressSanitizer,
  UndefinedBehaviorSanitizer and `implicit-conversion` (clang only). A report
  fails the run; `unsigned-integer-overflow` stays off because the CRC and
  checksum arithmetic wraps by design.
- The reference device's 16-bit counter now narrows explicitly: `counter++`
  promoted to `int` and truncated back, which `-Wconversion` does not diagnose
  (increment operators are exempt) but the sanitizer does. The wrap at 65535 is
  intended and asserted by `test_reference_device`.

### Added (fuzz gate)
- `tools/gates.sh fuzz` builds three libFuzzer harnesses from `fuzz/` and runs
  each for 60 s (`FUZZ_SECONDS` overrides). They cover the parsers that consume
  untrusted bytes: the received octet stream driven through the real ISR and
  main pumps (`fuzz_frame`), ISDU reassembly with a one-shot versus chunked
  differential (`fuzz_isdu`), and OD lookup plus codec/read/write/restore
  (`fuzz_od`).
- The harnesses carry their own assertions on top of the sanitizers — a reply
  never exceeds the largest legal reply, the DLL/ISDU state stays inside its
  enumeration, a lookup only ever returns an entry that belongs to one of the
  two tables, draining yields exactly the announced length, and chunking an ISDU
  payload never changes the response.
- The library is rebuilt in `build/fuzz` with
  `-fsanitize=fuzzer-no-link,address,undefined`: without instrumentation the
  fuzzer gets no coverage feedback and the sanitizers see none of the library's
  memory errors. Seeds are written once into that tree, so a previous run's
  discoveries survive; crash inputs land in `build/fuzz/artifacts/`.

### Changed (clang is the primary compiler)
- The gates, the compile matrix, the coverage tree and the WCET harness now
  build with clang (the compiler family the firmware is built with, and the
  only one that checks `IOLINK_NONNULL`/`IOLINK_NULLABLE`; on gcc they expand to
  nothing). `IOLINKI_CC` overrides the choice.
- `gcc` stays as a blocking supplement group: it runs the diagnostics clang
  does not implement (`-Wduplicated-branches`, `-Wlogical-op`,
  `-Wmaybe-uninitialized`, `-Wstringop-overflow`, ...) through
  `-DIOLINK_STRICT_GCC=ON`, applied to the library target only, plus
  `-fanalyzer` as a report-only step. `-Wswitch-enum` is part of that set: the
  six switches it flagged now list every enumerator, so a new state or timer id
  cannot be added without deciding what the switch does with it.
- The test image installs clang (the primary compiler) and the pinned Arm
  Toolchain for Embedded tarball (version and SHA-256 are build args; Arm
  publishes no container image). `run_all_tests_docker.sh` runs the size gate
  there, so the ATFE baseline is reproduced in CI rather than only on a
  developer machine.
- The public headers now compile under `clang -Wpedantic -Werror`: the
  extension diagnostics for `_Nonnull`/`_Nullable` and for the `__COUNTER__`
  fallback of `IOLINK_STATIC_ASSERT` are suppressed where they are defined (the
  C2y one behind `__has_warning`, because clang 14 does not know that group).
- The ROM/RAM baseline is measured with ATFE (clang + llvm,
  `--target=arm-none-eabi`) because that is what builds the firmware;
  `tools/size_baseline.atfe.csv` is new and `tools/size_baseline.csv` keeps the
  GNU-toolchain numbers (`IOLINKI_SIZE_TOOLCHAIN=gcc`).
- The coverage floor is re-calibrated on clang: it counts 5496 executable lines
  on this tree where gcc counts 4809, so the floor is 90.7% (gcc reports 91.0%
  on the same sources). Both denominators are recorded in
  `tools/coverage_baseline.txt`.

### Changed (event type and OD storage types)
- `iolink_diag_type_for()` returns `iolink_event_type_t` instead of `uint8_t`,
  and that enum now lives in `diag.h` (`events.h` includes it), so callers stop
  casting the result back to the enum. `gcc -Wbad-function-cast` rejected the
  cast of a call result.
- `iolink_od_entry_t.u.storage.value` is `uint8_t*`: the storage backend is
  written by the ISDU write path and by the scalar rollback, so the previous
  `const uint8_t*` was a contract the implementation could only honour by
  casting the qualifier away (`-Wcast-qual`). Read-only bindings belong in
  `block_default`, which stays `const`.

### Changed (grouped quality gates)
- `check_quality.sh` is replaced by `tools/gates.sh`, which runs the same checks
  in named groups (`quick`, `build`, `test`, `static`, `docs`, `matrix`,
  `perf`, `package`) plus `all`, so a local run only pays for what a change can
  break. CI keeps the full set, split across the same jobs as before. A group
  that needs a tool the machine does not have reports `SKIP`; CI installs them.

### Removed (od_width is not derived)
- `stack.od_width` must be given explicitly (1/2/8/32); `0` used to be turned
  into a per-M-sequence default inside `iolink_device_init()` and the DPP1
  M-sequenceCapability builder, so a config that forgot the width silently ran
  with a guessed layout. It now returns `IOLINK_DEVICE_INIT_ERR_OD_WIDTH`.
  `iolink_mseq_default_od_width()` stays as the Table A.10 lookup an
  application can use to pick its own value.

### Removed (no default device identity)
- `src/device_info.c` no longer carries a fallback identity
  (`k_default_info`: `vendor_id 0xFFFF`, `"Generic IO-Link Device"`,
  `"DefaultTag"`, protocol 1.1). A device without `config.device_info` would
  have advertised that fabricated identity on the wire, including a VendorID
  that belongs to nobody. `config.device_info` is now required:
  `iolink_device_init()` returns `IOLINK_DEVICE_INIT_ERR_DEVICE_INFO` (-16)
  when it is NULL, and `iolink_device_info_ctx_init()` returns -1 and leaves
  the context zeroed instead of substituting the fallback. DPP1 and the
  identity indices are not fabricated either: an ISDU context that never bound
  a device identity answers `0x80 0x11` (`IDX_NOT_AVAIL`), and the DLL layers
  stay silent instead of emitting an all-zero DPP1 page (ISR reply plan, the
  STARTUP page probe and the Type-0 page read). A Type-1/2 page read cannot
  stay silent because its cyclic frame must fill the OD slot; it reports `0`
  (A.1.2 "unimplemented address") instead of building a page.
- Direct Parameter page 2 is owned by the device: an ISDU context without
  `direct_param_page2` bound answers `0x80 0x11` for that index instead of
  writing into a shared static fallback buffer (which leaked one device's
  page 2 into another context).

### Changed (typed handles instead of `void *`)
- `iolink_isdu_ctx_t` back-links carry their real types: `event_ctx`, `ds_ctx`
  and `dll_ctx` are `struct iolink_events_ctx *`, `struct iolink_ds_ctx *` and
  `struct iolink_dll_main *` (opaque forward declarations in `isdu.h`),
  `direct_param_page2` is `uint8_t *`, and the DPP1 cache keys name the DLL and
  info contexts. The casts they forced in `isdu*.c` are gone.
- `iolink_od_entry_t` stores the application variable as a byte view:
  `u.storage.value` and `block_default` are `const uint8_t *`. The initialiser
  macros cast for you, so OD tables do not change.
- The TIOL112 and L6362A board adapters no longer take `void *`: the state is
  `iolink_tiol112_board_t *` / `iolink_l6362a_board_t *`, opaque types the board
  defines (the field is `io.board`). Adapters that need no state leave it NULL.
- Pointer contracts are annotated instead of implied: `IOLINK_NONNULL` /
  `IOLINK_NULLABLE` (Clang `_Nonnull` / `_Nullable`, empty elsewhere) mark the
  required inputs and the documented optional ones, and the remaining `void *`
  carry `IOLINK_OPAQUE` with the reason in their Doxygen: the byte helpers in
  `utils.h`, the profile symbol table, and the DLL state callback's context.

### Changed (config holds pointers)
- `iolink_device_config_t::phy` and `stack` are pointers now, like `app`,
  `device_info`, `od` and `workspace`. `phy` used to be copied by value (120 B
  of vtable duplicated into every config) while `device_info` (104 B) was a
  pointer; the struct is 64 B instead of 200 B on the host and 32 B instead of
  116 B on a 32-bit target (56 B / 28 B with `IOLINK_FEATURE_DATA_STORAGE=0`).
  `phy` and `stack` must outlive the context and a
  NULL in either of them is rejected with `IOLINK_DEVICE_INIT_ERR_NULL`.

### Fixed (RevisionID is one octet)
- `iolink_device_info_t::revision_id` was `uint16_t` holding the IODD value
  0x0111 while DPP1 octet 0x04 carries a single octet: Figure B.4 packs
  MajorRev in bits 4-7 and MinorRev in bits 0-3, so V1.1 is 0x11 and V1.0 is
  0x10. The field is now `uint8_t` and the two values have names
  (`IOLINK_REVISION_ID_1_0` / `IOLINK_REVISION_ID_1_1`); the DPP1 builder
  copies the field instead of masking a 16-bit value.

### Changed (one log sink, one clock)
- The DLL, ISDU, DS and events contexts used to embed their own
  `iolink_log_sink_t` (four copies of the same callback/self/level) and the
  events engine kept a second clock next to the DLL's. The device now owns a
  single sink (`shared.log`) and a single `iolink_clock_t` (`shared.clock`);
  the module contexts hold `const` pointers to them, bound once by
  `iolink_device_set_log()` and at init. `iolink_device_set_log()` updates one
  object instead of four, and a module that was never bound is silent instead
  of logging through a stale copy. Standalone users point `ctx->log` /
  `ctx->clock` at their own objects (NULL = silent / time 0).
- Saves 28 B of RAM per device on a 32-bit target and 56 B on the 64-bit host;
  the events context shrinks from 504 to 480 B (device 4920 -> 4864 on the
  host). One extra indirection costs 16 B of `.text` in the full image.

### Changed (application context is a trait)
- `iolink_device_config_t` no longer takes `user`, `time_us`, `log_write` and
  `app_callbacks`. They are one `const iolink_device_app_t* app`: a trait that
  binds `iolink_app_t* self` to the clock, the log sink and every lifecycle/PD
  callback. `iolink_app_t` is an incomplete type the application defines, so
  assigning an unrelated pointer to `self` needs an explicit cast and is a
  compile-time diagnostic instead of a silent `void*` conversion.
- The OD hooks and HANDLER backends receive the same `self`
  (`iolink_od_app_t` lost its `user` field; `iolink_od_ctx_t`, `iolink_isdu_ctx_t`
  and `iolink_ds_ctx_t` carry it), and `iolink_isdu_bind_od()` /
  `iolink_ds_bind_sources()` take it as an argument.
- The PHY vtable follows the same rule: `phy.user` is now `phy.self` typed
  `iolink_app_t*`. The library forwards that pointer unchanged and the driver
  turns it back into its own state. An application that keeps the driver
  context as the first member (offset 0) of `struct iolink_app` and sets
  `app->self = (iolink_app_t*) &app` gets one pointer per instance:
  `phy.self == app->self`. The TIOL112/L6362A `io.user` (board adapter) and
  `iolink_dll_state_cb_t` (device context) keep `void*`: they carry the board's
  and the device's private state, not the application instance.
- `iolink_log_write_fn` and `iolink_ds_flag_persist_fn` take `iolink_app_t*`;
  `iolink_log_sink_t::user` is now `self`.
- New `IOLINK_DEVICE_INIT_ERR_APP` (-15) when `config.app == NULL`; a trait
  without `time_us` still returns `IOLINK_DEVICE_INIT_ERR_TIME`.
- Layout: device context 4904 → 4920 B, ISDU 1344 → 1352 B, DS 360 → 368 B on
  the 64-bit host (each context stores the instance pointer; the config struct
  loses four pointers). The application trait itself is 44 B of `.rodata` on a
  32-bit target (11 pointers) instead of 16 B of scattered config fields, so
  `tools/size_baseline.csv` was regenerated for that intentional change.

### Changed (source layout)
- The three oversized translation units are split by responsibility, so each
  stays readable and reviewable: `dll.c` (context, deadline table, pump),
  `dll_frame.c` (frame path), `dll_sm.c` (state-machine skeleton),
  `dll_reply.c` (reply-plan publication and ISDU snapshot), `dll_api.c`
  (public getters/setters), `dll_isr.c` + `dll_isr_rx.c` (TX/plan executor
  vs. RX assembly), and `isdu.c` + `isdu_cmd.c` + `isdu_ds.c` (transport,
  standard indices/system commands, data storage and OD entries). The shared
  internals are declared in `src/dll_internal.h`, `src/dll_isr_internal.h`
  and `src/isdu_internal.h`; nothing new is installed or public. The DLL
  ownership gate now covers every main-context TU instead of only `dll.c`,
  `tools/build_matrix.sh` / `tools/size_matrix.py` list the new sources, and the
  example build scripts and the IAR project (`examples/stm32g0_tiol112/`) too.
  Splitting costs about 0.8 kB of text (+3.5%) because GCC can no longer inline
  the helpers across the former single translation unit; `tools/size_baseline.csv`
  was regenerated for that reason. rodata, data and bss are unchanged.

### Added (LTO dimension in the size gate)
- `tools/size_matrix.py` now measures every structural configuration twice, with
  and without `-flto`, so the documented "enable LTO to save flash" advice is
  covered by the gate instead of being taken on trust. The probe application
  stays a non-LTO object in the LTO rows: with `-nostdlib` its `memcpy`/`memset`
  definitions are the only ones available, and LTO would drop them after
  deciding they are unused. Firmware linked against libc is unaffected.
- With LTO the translation-unit split costs ~24 B instead of ~750 B, because
  the linker can inline across the new files again.

### Added (file-size gate)
- `tools/check_file_size.py` fails when a `src/`, `include/` or `examples/`
  file reaches 800 code lines. Comments and blank lines are removed first (a
  string literal containing `//` still counts), so documentation does not
  inflate a file and comment padding cannot hide one. It runs in
  `check_quality.sh` and in CI, and `--selftest` keeps the counter honest.

### Removed (stale traceability duplicate)
- `tests/conformance_traceability.csv`: it still pointed at renamed tests and
  no tool validated it; `tests/conformance_audit.csv` is the single matrix.

### Removed (unused root script)
- `verify_events.py` at the repository root: an unreferenced variant of
  `tools/virtual_master/verify_events.py` that could not import its own module
  (`ModuleNotFoundError: virtual_master.master`). The copy under `tools/`
  imports and runs.

### Changed (build trees)
- Every build goes below `build/`, one subdirectory per purpose
  (`build/quality`, `build/matrix`, `build/coverage`, `build/wcet`,
  `build/size`, `build/linux`, `build/docker`, ...), so the repository root
  stays clean and `tools/clean_builds.sh` removes all of it in one step. The
  `IOLINKI_*_DIR` overrides still work.
- `tools/check_test_gates.sh` rejects a group teardown that relies on a cmocka
  assertion (cmocka 2.x still exits 0 for those, so such a gate can never fail)
  and a binary that includes the shared PHY harness without wiring
  `test_tx_all_asserted`.

### Changed (init result codes)
- `iolink_device_init_result_t` is contiguous again (-1..-14). The reserved
  holes (-8/-12/-13/-18) existed only to keep numbering stable; the tree is
  unreleased, so the values moved and the "keep numbering stable" note is gone.
- A valid frame that has no reply plan is now logged at WARN in `OPERATE`
  (where it is more suspicious) and at DEBUG elsewhere (where FALLBACK has no plans
  on purpose). The counter was already there; the log line used to be DEBUG
  everywhere. Both stay behind `IOLINK_LOG_ENABLE_ISR`.

### Added (generated transition table)
- `DLL_SM_ADMIN_ROWS` declares the non-frame transitions (init, wake-up,
  timers, management commands). `tools/gen_dll_statemachine.py` now renders
  them into `docs/CODE_FLOW.md` between markers; `--check` (ctest
  `dll_statemachine_gen_check`) rejects a stale table, so the table and the
  macro cannot drift. The mermaid sketch above the table stays illustrative.

### Changed (public tree and package)
- The context-layout headers (`dll_ctx.h`, `dll_main.h`, `dll_shared.h`,
  `dll_isr.h`, `dll_sm.h`, `ring.h`) moved to `include/iolinki/internal/`.
  `device.h` still embeds the layout (the application allocates the context),
  but a direct include now reads as what it is. `tools/check_test_whitebox.sh`
  rejects the layout headers from tests, and `iolink_device_ctx_t`'s accessors
  remain the supported path.
- `iolink::iolinki` is exported: `cmake --install` writes an
  `iolinkiConfig.cmake`, the target carries its include directory, and
  `tools/test_install_consumer.sh` builds a two-file consumer against an
  installed prefix in the quality gate and CI.
- `dll.h` is documented as the advanced/low-level interface; applications start
  at `device.h`.
- `iolink_device_init()` maps a failing PHY `init` callback to
  `IOLINK_DEVICE_INIT_ERR_PHY` instead of returning the driver's negative code,
  which mixed two error domains. The driver code is still available through
  `iolink_device_get_phy_error()` (the default log is silent).

### Added (per-frame fate counters)
- `isr_frames_consumed` (the ISR completed the frame and the main pump never
  sees it, whether it replied or was silent by specification) and
  `isr_frames_handoff` (the frame was published to the main pump) in
  `iolink_dll_stats_t` split the ISR's work in two. Dropped frames stay visible
  through `isr_plan_misses`, `isr_mailbox_drops` and `isr_eager_rejects`
  (corrupt ISDU writes). The two counters cost 8 bytes of context RAM.

### Added (trimmed-build behaviour contract)
- A trimmed build (`IOLINK_FEATURE_DATA_STORAGE=OFF` or
  `IOLINK_FEATURE_EVENTS=OFF`) no longer proves only that it compiles:
  `tests/test_trimmed_contract.c` asserts what a master observes in that build
  (`0x0003` -> `IDX_NOTAVAIL`, DS SystemCommands -> `FUNC_NOTAVAIL`,
  `0x0025` -> `IDX_NOTAVAIL`) with a control assertion that a standard object is
  still readable. `host_no_ds` / `host_no_events` are matrix configurations, so
  the matrix now covers six structural configurations.

### Changed (test layout coupling)
- Behaviour and conformance tests no longer reach into the device's embedded
  DLL context. They use the public API where it exists and otherwise the
  accessors in `tests/dll_access.h` (`test_dll`, `test_dll_shared`,
  `test_dll_isdu`, `test_dll_ctx`), the only test file that includes
  `iolinki/internal/dll_ctx.h` directly. Tests whose subject is the DLL internals keep
  declaring `iolink_dll_main_t` directly.
- `tools/check_test_whitebox.sh` enforces both rules (`--selftest` checks the
  matchers) and runs from `check_quality.sh` and CI.

### Changed (test effectiveness)
- `skip_known_gap()` is gone: the conformance cases now assert the behaviour the
  stack actually produces (`0x8030` for reserved access-lock bits, `0x8033` for
  an overlong tag, the no-DS DataStorageIndex record lengths), so a regression
  fails instead of silently skipping.
- The conformance audit matrix now names functions that exist and carry the
  matching `IOLINK_CONFORMANCE_CASE` id; `check_conformance_traceability.py`
  enforces both and ignores annotations that only exist in a comment, a string
  or an `#if 0` block. The `IOLINK_DS_LEGACY_IMAGE_SUB0` round trip is now
  registered only when that option is enabled instead of skipping at runtime.
- Every test binary that uses the PHY mock fails when a captured reply was
  never asserted: the shared harness and the local PHY mocks count delivered
  and asserted replies, and the group teardown exits with `EXIT_FAILURE`
  because cmocka 2.x reports a failed teardown but still exits 0.
- Expected frames no longer compute their checksum with the code under test:
  `tests/crc_oracle.h` implements A.1.6 independently of `src/crc.c`.
- `IOLINK_STATS_MODE` is a real CMake variable, and the SPLIT statistics
  expectations are literal instead of `IOLINK_STAT_TOTAL` self-mirrors.

### Changed (test suite)
- Every unit test now runs through cmocka. 21 files that used `<assert.h>` and
  7 whose `main()` returned a step number (`IOLINK_TEST_RC` in
  `tests/test_case.h`) were converted, so one failing case no longer hides the
  rest of the binary.
- cmocka is a hard requirement for the suite: a configure without it fails
  instead of printing a warning and registering no tests. `-UNDEBUG` is gone
  with the last `assert()`.

### Added (DS_UPLOAD_FLAG persistence hook)
- `iolink_ds_flag_persist_fn`, `iolink_ds_set_flag_persist()` and
  `iolink_device_config_t::ds_flag_persist` let the application persist
  `DS_UPLOAD_FLAG` in its own NVM. The library keeps the RAM state and calls
  the hook whenever the flag changes (NULL = RAM-only).

### Removed (tag parameter manager)
- `include/iolinki/params.h` and `src/params.c`. The three writable tags
  (`0x0018`-`0x001A`) are now application-provided
  `IOLINK_OD_BYTES_STORAGE[_EX]` entries; the library routes them like any
  other entry and no longer owns a tag store or a `HANDLER_TAG` backend.
- `iolink_od_ctx_t::params`, `iolink_params_ctx_t`, the `iolink_ds_ctx_t`
  tag binding and the tag branch of the ISDU factory reset. Factory reset
  restores the DS set in RAM; the application supplies tag defaults and
  persistence.

### Removed (library-owned NVM and persistence)
- `include/iolinki/storage.h`, `iolink_storage_api_t`, the `storage` backend
  of `iolink_device_config_t`, `IOLINK_STORAGE_*`, `IOLINK_OD_STORAGE_PERSIST`,
  `iolink_od_ctx_t::flush`/`iolink_od_flush_fn`, `iolink_ds_save_persistent()`
  and `iolink_ds_load_persistent()`. The stack no longer writes NVM or owns a
  persistence format; applications persist through each entry's `on_change`.
- `IOLINK_DEVICE_INIT_ERR_STORAGE` and `IOLINK_DEVICE_INIT_ERR_PERSIST_CAPACITY`.
  `iolink_ds_init()` no longer takes a storage backend.
- `DS_UPLOAD_FLAG` is RAM-only; `iolink_ds_factory_reset()` restores the DS
  set in RAM only and never calls into a storage backend. Restored entries go
  through the normal write path, so the application receives `on_change` and
  can persist the defaults.

### Added (init-time configuration self-check)
- `iolink_device_init()` now validates the configuration once, before binding
  any subsystem, and rejects the first problem with
  `IOLINK_DEVICE_INIT_ERR_OD` instead of failing at the first ISDU access.
  Covered: a missing backend pointer, a read/write HANDLER without the
  corresponding callback, `TRIGGER` with `storage` bits, a writable STORAGE
  OctetString without `block_len`, read-only entries carrying
  `on_change`/`validate`, out-of-range defaults, duplicate
  `(index, subindex)` and application entries occupying a standard index
  (`< 0x0040`, except the tags 0x0018-0x001A).
- The check compiles behind `IOLINK_FEATURE_VALIDATE` (default on). Set it to
  `0` once the configuration is stable to drop the self-check `.text`; the
  compile-time macro checks stay active either way.
- The table macros now reject integer cross-field mistakes at compile time:
  `TRIGGER` with a `storage` bit and a writable `IOLINK_OD_BYTES_STORAGE` with
  `block_len == 0`.

### Added (slot sizing and diagnostics)
- `IOLINK_MBX_SLOTS` and `IOLINK_REPLY_PLAN_SLOTS` are overridable in
  `include/iolinki/config.h` (`#ifndef` plus power-of-two/range checks).
  Defaults: frame mailbox 4, reply-plan slots 48 (four state slices share one
  table); `IOLINK_TX_SLOTS` is derived from `IOLINK_TX_ISR_SLOT`.
- `iolink_dll_stats_t` gains `isr_od_overruns`, `isr_plan_misses`,
  `isr_mailbox_drops`, `t_pd_discards` and `reply_plan_overflows`, so
  overwritten requests, dropped plan misses, deferred frames, plan entries and
  frames discarded inside the t_PD window are all observable.
- `iolink_isdu_dp_page1_read_range()` reads a DPP1 range in one seqlock
  window; the ISR page-read plan uses it, so a multi-byte page reply is a
  single generation and needs one loop instead of per-byte accessor calls
  (the 32-byte path drops from about 2500 to about 450 Thumb instructions on
  Cortex-M0+).

### Fixed (reply ownership and drop counters)
- Frames discarded inside the t_PD protection window are counted in
  `t_pd_discards`; reply-plan slot exhaustion is counted in
  `reply_plan_overflows` instead of being silent.
- Type-0 DPP page writes (addresses 1..0x1F) and diagnostic writes now have
  ISR reply plans: the ISR acknowledges with CKS and the frame still reaches
  the main pump, which applies the write without sending a second reply. This
  removes the PREOPERATE stall where a late main pump dropped the MinCycleTime
  page-write acknowledgement and the master retried it forever.

### Changed (ISR owns the reply path)
- A valid frame that matches no reply plan is now dropped and counted in
  `isr_plan_misses`; it is no longer handed to the main pump. The main pump
  only consumes frames that do not need an immediate reply (eager copies,
  writes it must apply, deferred decisions and invalid frames). A plan hit
  whose execution fails (busy line, ISDU write not yet staged) is still
  deferred to the main pump.
- Reply plans are published once as four state slices (STARTUP/AWAITING,
  PREOPERATE, ESTAB_COM, OPERATE) under the key
  `{M-sequence type, OD width, baudrate}`; state transitions no longer require
  a main-pump republish. ESTAB_COM shares the OPERATE content but always
  publishes its first frame so the main pump can complete the transition.
- `IOLINK_REPLY_FLAG_DEFER` is added: a matching plan may deliberately leave
  the frame to the main pump (startup MasterCommand acceptance depends on the
  command byte and the compatibility mode).
- Direct Parameter Page 1 is now a main-pump-maintained RAM image
  (`iolink_isdu_ctx_t::dp_page1`, seqlock-protected) instead of being rebuilt
  on every ISR read; the page-read path is an index plus checksum.
- IDLE/IDLE2/ABORT ISDU reads get a CONSUME plan that answers `0x00` and asks
  the main pump to reset the read transport via the one-shot
  `isr_isdu_read_reset` handshake.
- Type-0 process writes are planned (ACK + deliver) in every state, and
  Type-1/2 OPERATE/ESTAB also plan Type-0 page writes, so no valid frame shape
  falls through to the main pump.
- Removed: `iolink_frame_rec_t::end_us`, `iolink_dll_stats_t::late_reply_dropped`,
  `iolink_dll_main_t::late_reply_dropped`, `frame_window_closed` and
  `dll_reply_window_closed()`. Replies are either sent by the ISR inside its
  plan or not sent at all; there is no late-reply window to gate.

### Changed (object dictionary value backends)
- `iolink_od_entry_t` now carries exactly one application backend:
  `IOLINK_OD_BACKEND_STORAGE` (`const void* value` + `uint8_t* str_len`) or
  `IOLINK_OD_BACKEND_HANDLER` (per-entry `read`/`write`). The tagged union adds
  12 bytes per entry on a 32-bit target (80 vs 56 bytes on a 64-bit host).
  Built-in entries keep the `handler` selector; the two are mutually exclusive.
- New table macros: `IOLINK_OD_SCALAR_STORAGE[_EX]`,
  `IOLINK_OD_BYTES_STORAGE[_EX]`, `IOLINK_OD_SCALAR_HANDLER[_EX]`,
  `IOLINK_OD_BYTES_HANDLER[_EX]`. The `_STORAGE` variants bind an application
  variable and check its width (or `sizeof(buf) >= block_len`) at compile time;
  the `_EX` suffix adds `validate`/`on_change` hooks.
- Completed writes: `validate` may reject before the value changes; a failing
  `on_change` rolls a STORAGE scalar back to its previous value. OctetString
  writes are not rolled back (avoid copying up to `block_len` on the main-pump
  stack), so reject those in `validate`.
- `iolink_od_app_t` is now `{entries, count, user}`; `user` goes to hooks and
  HANDLER callbacks. The built-in standard table uses internal
  `IOLINK_OD_BUILTIN_*` macros, and "not a built-in" is
  `IOLINK_OD_HANDLER_NONE`.

### Changed (single parameter image)
- Persistence now has one answer: every entry whose `storage` has
  `IOLINK_OD_STORAGE_PERSIST` is serialized into the PARAMS region, and the
  whole set is rewritten once per `WRITE_STORE` write (via
  `iolink_od_ctx_t::flush`). A failed image write answers with
  `IOLINK_RESULT_IO` and rolls a STORAGE scalar back.
- `IOLINK_OD_STORAGE_NVM` becomes the bitmask
  `IOLINK_OD_STORAGE_DS | IOLINK_OD_STORAGE_PERSIST` (independent bits:
  DS-only parameters are restored by the master, PERSIST-only ones are local).
- The separately stored Data Storage image is gone: `Data_Storage_Size`,
  `Parameter_Checksum`, uploads and the backup are regenerated from the current
  values. NVM holds only the parameter image and `DS_UPLOAD_FLAG`;
  `iolink_ds_verify()` and the `IOLINK_STORAGE_DS_*` macros are removed.
- `iolink_ds_save_persistent()` / `iolink_ds_load_persistent()` /
  `iolink_ds_factory_reset()` are the persistence entry points. Boot restores
  values through `iolink_od_restore_value()` (no hooks, no write-back); factory
  reset clears the tags and restores every PERSIST entry from its table default.
- Startup no longer builds a DS image; it loads the parameter image and calls
  `iolink_ds_refresh()`. A RAM-only context (`storage == NULL`) still serves DS
  but never persists.
- `iolink_params_ctx_t` loses its storage binding and the `persist` argument:
  the tag context is a RAM value store, not a second persistence path.
- Storage layout is now `PARAMS | DS_UPLOAD_FLAG`, with a new magic
  (`IOL3`). Existing NVM contents are discarded and parameters fall back to
  defaults (no migration by policy).

### Changed (ISDU write ingestion)
- ISDU write requests are now accumulated in the data ISR with positional
  FlowCTRL de-duplication (`iolink_isdu_od_write`): a retried segment is
  acknowledged but does not consume a new position, so a master retry storm
  cannot overrun the staging buffer. `iolink_isdu_process()` validates CHKPDU,
  parses and executes the completed request in the main context.
- `IOLINK_OD_SLOTS`, `iolink_od_rec_t` and the `isr_od_ring` staging ring are
  removed. The write request reuses the existing ISDU request buffer, which
  saves about 260 bytes and removes the ring-full overrun path. The internal
  `IOLINK_ISDU_ERROR_SEGMENTATION` constant is removed because the write path
  no longer emits it (out-of-order segments are ignored and self-heal).
- `isr_plan_misses` lives in the shared DLL region and counts only valid plan
  misses; invalid frames are counted as framing/eager rejects.

### Changed (IODD single source)
- IODD generation takes parameters only from the C object dictionary
  (`--od`); a JSON `variables` key is rejected. `iodd_cli.py generate` accepts
  `--od`, `IOLINK_OD_STRING_EX` exposes validate/on-change hooks to the table,
  and the shipped switching-sensor and conformance IODDs are regenerated from
  their OD tables (index-derived variable ids, `name` only). The illustrative
  `simple_device` IODD pair is removed; that example is a C integration demo
  without an OD table.

### Fixed (2026-11 code review)
- OD write path: `iolink_od_write_value()` takes the requested subindex and
  forwards it to the params manager and the application write hook; the value
  layer rejects `subindex > max_subindex` on its own; OctetString writes over
  `block_len` answer `0x33` instead of silently truncating tags.
- Index routing by domain: normative indices stay standard-table-first (an
  application table cannot shadow `0x0002`/`0x0024`), while device-specific
  indices (`0x0040`-`0x00FE`, `0x0100+`) try the application OD table, then the
  built-in entry. `iolink_od_find()` follows the same split, so DS access to an
  application parameter in the Preferred range resolves correctly.
- Data Storage membership includes application OD entries with
  `IOLINK_OD_STORAGE_DS` (`iolink_ds_bind_sources()`); `ParamDownloadEnd`
  validates every known record before applying any of them, and the "atomic"
  wording was corrected (NVM failures are still not a cross-parameter
  transaction).
- Unbinding an external workspace clears the ISDU/DS buffer pointers instead of
  leaving dangling references when `IOLINK_WORKSPACE_EMBEDDED=0`.
- Runtime capability overrides invalidate the published reply plans
  (`iolink_dll_invalidate_reply_plans()`), so a compat-bit change cannot leave
  the ISR executing the previous plan.
- Frames answered by the ISR under a `CONSUME` plan now report back through a
  shared timestamp and take part in T_CYCLE/T_REN bookkeeping and
  `last_response_us`.
- The profile anchor covers `IOLINK_WORKSPACE_EMBEDDED` and
  `IOLINK_PD_IN_BUFFERS`; the negative probes reference one symbol each, so a
  mismatch cannot mask the expected failure.
- `tools/diff_matrix.sh` fails when a test binary exits non-zero,
  `tools/build_matrix.sh` fails when the critical-section archive is missing,
  and `tools/size_matrix.py` gates `.rodata` as well (with `--selftest` proof).

### Removed (no compatibility layer)
- `iolink_od_app_t::read`/`write` and `IOLINK_OD_HANDLER_BACKEND`.
  `IOLINK_OD_SCALAR[_EX]`/`IOLINK_OD_STRING[_EX]` are no longer application
  macros: every application entry uses one of the `_STORAGE`/`_HANDLER`
  variants, and the standard table uses the internal `IOLINK_OD_BUILTIN_*`.
- `iolink_persist_t`, `IOLINK_PERSIST_*`, `iolink_params_bind_storage()` and
  the `persist` argument of `iolink_params_ctx_write()`. Persistence is decided
  by the table entry's `PERSIST` bit and triggered by the write path, so a
  second "should I persist?" argument is gone.
- `iolink_params_nvm_t`, the stored Data Storage image and the
  `IOLINK_STORAGE_DS_BASE`/`IOLINK_STORAGE_DS_SIZE` macros; the NVM layout is
  `PARAMS | DS_UPLOAD_FLAG`. `iolink_ds_verify()` is removed with the image it
  verified.
- The Data Storage descriptor/accessor override: `iolink_ds_param_desc_t`,
  `iolink_ds_param_read_fn`/`write_fn`/`check_fn`, `iolink_ds_bind_params()`,
  `iolink_ds_bind_od()`, `iolink_ds_bind_param_access()`,
  `iolink_ds_set_param_desc()`, `iolink_ds_param_at()` and
  `iolink_params_ctx_factory_reset`-era descriptor plumbing. The DS set is now
  derived from the object dictionary only (`IOLINK_OD_STORAGE_DS`), values come
  from the table entry backend, and the restore pre-check reuses the entry's
  `validate` hook. `iolink_ds_bind_sources()` replaces the three bind entry
  points; `iolink_ds_param_count()`/`iolink_ds_param_entry()` expose the
  derived set. `IOLINK_PERSIST_ASYNC` and
  `iolink_ds_start_upload()`/`start_download()` (duplicates of the command
  path) are gone as well.
- `iolink_device_config_t::vendor_service` / `iolink_isdu_ctx_t::vendor_service`
  and `vendor_user`. Device-specific ISDU indices and parameter values now have
  a single implementation path: `iolink_device_config_t::od` (const table +
  read/write backend). The conformance example, the WCET harness and the tests
  use OD entries; undeclared device-specific indices fall through to the
  built-in table (`0x0040`) and then answer `0x80 0x11`.
- The integer parameter wrappers (see below), the retired capability build
  switches and their CMake options, the internal T_PD hand-off (the device arms
  T_PD directly), and the example's vendor-service wrapper.

### Added
- Capability-table design for roadmap P6: `docs/CAPABILITY_DESIGN.md`
  classifies all 37 compile-time knobs (23 stay compile-time, 12 become
  runtime capability bits, 2 profile bundles retire), drafts the
  `iolink_build_caps_t` / `iolink_caps_t` model, converges the 22-config
  matrix to 4 structural configurations, and specifies how ROM is measured
  for source integration versus non-LTO static-library delivery.
- Unified result model: `iolink_result_t` (`include/iolinki/result.h`) with
  `0 = success`, negative internal codes and positive ISDU ErrorCodes.
  `iolink_params_ctx_read/write`, `iolink_ds_stage_param` and the DS parameter
  accessors now return it, so protocol errors such as `0x33` (value length
  overrun) survive the params/DS path instead of collapsing to `0x11`.
- Context size contract: `tests/test_ctx_sizes.c` asserts the exact layout
  sizes at build time; layout changes must update the test and
  `docs/MEMORY_GUIDE.md` together.
- Differential frame gate: `tools/diff_matrix.sh` replays the protocol suite
  under every runnable configuration and compares device reply frames byte for
  byte (`tools/diff_allowlist.txt` documents the one expected compat-v10
  difference).
- Coverage floor: `tools/coverage.sh --check` enforces
  `tools/coverage_baseline.txt` in CI.
- DLL ownership split headers: `include/iolinki/dll_shared.h`,
  `dll_main.h`, `dll_isr.h` and `dll_ctx.h` expose the main, shared,
  ISR-exclusive and packed-container views separately.
- Ownership gate: `tools/check_ownership.sh` proves that the main-context
  translation unit cannot reference ISR-exclusive fields, and runs in
  `check_quality.sh` and CI.

### Added (object dictionary)
- Const object dictionary (`include/iolinki/od.h`, `src/od.c`) as the single
  source for ISDU dispatch, DS membership and IODD export. Mandatory indices
  (SystemCommand, Profile, identification, tags, DeviceStatus, DPP1/2) now
  live in table entries; `isdu.c` keeps only transport and built-in handlers.
- Data Storage membership is resolved from the OD table / descriptor set and
  no longer probes `vendor_service` with a read. `examples/switching_sensor`
  is the first OD application and `tools/iodd_gen.py` exports the IODD from
  the table. ISDU context grows by 8 bytes.

### Added (reply plan)
- Reply-plan table in `iolink_dll_ctx_t` (10 slots × 24 B) published by the
  main pump under a seqlock. The ISR now only looks up a plan, sources the
  bytes, appends the checksum and feeds the PHY; unrecognised requests return
  to the main pump instead of being interpreted in the ISR. An ISDU generation
  bit plus staged-write invalidation keep write-then-read consistent, and the
  ISR-side M-sequence content constants roughly halve (27 vs 54).

### Added (diagnostics registry)
- One condition registry (`include/iolinki/diag.h`) drives DeviceStatus
  (0x0024), DetailedDeviceStatus (0x0025) and EventCode 0x1800 with latch/edge
  semantics; sources are PHY faults, the event queue, data-path errors, mapped
  local parameter writes and DS state. Missing DDS mappings report 0xFF.
  For codes registered in the table `iolink_event_trigger()` now uses the
  normative EventType; unregistered application/vendor codes keep the caller
  supplied type.

### Added (capability model)
- `iolink_build_caps_t` / `iolink_caps_t` and `iolink_caps_resolve()` are the
  single capability resolution point (`include/iolinki/capability.h`).
  Twelve compile-time switches became runtime capability bits (SystemCommand,
  wake-up, SIO C/Q, PD-input callback, ISR-TX-only, three compatibility bits,
  event timing, timing enforcement, access locks); `on_pd_input` and the
  polling API now coexist and are selected by member presence.
- Structural matrix converged to four configurations with one source of truth
  (`tools/matrix_configs.sh`), syntax-only combination probes, profile
  link-probes and a ROM size baseline (`tools/size_matrix.py`).
  Measured minimal-app cost: +972 B text (+4.5%), +20 B rodata.

### Added (timers and state machine)
- Software timeouts converge onto one deadline table (t_A/t_ren/t_cycle/t_PD/
  t_byte/T_DSIO/T_FBD/master_lost/idle) with re-arm/cancel semantics and a
  single next-deadline comparison in the main loop.
- The DLL state machine is tabulated (7 states × 8 events) with generated
  Mermaid/CSV/test artefacts; reply plans carry a state stamp so transitions
  invalidate them.

### Added (workspace)
- Optional `iolink_workspace_t` (`include/iolinki/workspace.h`) holds the ISDU
  request/payload/response/frame scratch and the DS staging buffer, so the
  application can place them in DTCM or external RAM. ISDU and DS contexts
  keep pointers and capacities only; `IOLINK_WORKSPACE_EMBEDDED=1` (default)
  keeps an embedded fallback, `=0` requires a workspace and rejects init with
  `IOLINK_DEVICE_INIT_ERR_WORKSPACE`.
- ISDU responses now use one frame buffer with a lease (published as Busy
  while being rebuilt) instead of two full copies; DS uploads and checksums
  are serialized incrementally instead of keeping a resident 256 B image.
  The device context is 4088 B on a 64-bit host (2792 B with the workspace
  externalized).

### Changed
- DLL ownership split: `iolink_dll_ctx_t` packs `iolink_dll_main_t` (with the
  nested shared region) and `iolink_dll_isr_t` as siblings in one allocation.
  `src/dll.c` includes only the main header, so referencing ISR-exclusive
  fields is a compile error. DLL public functions now take `iolink_dll_main_t*`
  and `iolink_dll_init()` takes the main and ISR objects; the packed container
  remains the only object the application allocates (device context is 4088
  bytes on a 64-bit host, same single-allocation model).
- `iolink_device_ctx_t` no longer carries a `stack_config` copy; the caller
  configuration (`ctx->config->stack`) is the single source of truth
  (device context shrinks by 24 bytes on a 64-bit host).
- Test helpers no longer keep process-wide parameter/identity singletons; each
  test file owns an explicitly initialised `iolink_test_env_t`.

### Added (runtime log sink)
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
- `on_pd_input` and the polling API coexist; the path is selected at init by
  member presence. Init still rejects a `IOLINK_PHY_FEATURE_TX_BUSY=1` build
  without `tx_busy` (`IOLINK_DEVICE_INIT_ERR_PHY`, -15).
- `phy->get_faults()` is polled: edge-triggered Table D.1 events
  (under/overvoltage, over-temperature, short circuit); a PHY without the hook
  produces no physical diagnostics.
- Documentation: pump call maps, a device integration checklist and an
  ownership/lifetime contract (`dll.c`, `dll_isr.c`, `device.h`,
  `CODING_STANDARDS.md`).

### Changed (configuration surface)
- `config.h` is now the single source of default values, grouped into
  overridable buffers, spec-fixed values, build/porting tiers and logging.
- The bundle profile switches are gone; trimming uses the individual
  `IOLINK_FEATURE_*` options.

### Removed
- `iolink_params_ctx_get/set`: the integer compatibility wrappers that collapsed
  every failure to `-1` are gone; callers must use the result-returning
  `iolink_params_ctx_read/write` and inspect `code`/`len`.
- `phy_api->stack_timer_stop` and `iolink_storage_api_t::erase`: the stack never
  called either hook, so the porting contract is now start/read/write only.
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
  `IOLINK_CONCURRENCY_RTOS` tier. Identity and parameters are context-only;
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
