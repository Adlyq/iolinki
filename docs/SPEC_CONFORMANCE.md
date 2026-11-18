# IO-Link V1.1.5 Specification Conformance Matrix

Single source of truth for **what is implemented**, **where**, and **which test
proves it**, ordered by the bundled specification.

> **Authoritative reference:** `docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/IOL-Interface-Spec_10002_V1.1.5_Oct2025.pdf`.
> The V1.1.3 PDF is historical. When a doc/comment disagrees with V1.1.5, the
> PDF wins.

Status: **PASS** (implemented + tested), **PARTIAL** (subset covered), **N/A**
(conditional on a profile). All in-scope gaps are closed; the full suite
(including `test_spec_conformance`) is green: `ctest --output-on-failure`.

Final audit disposition, counted from the `status` column of
[`tests/conformance_audit.csv`](../tests/conformance_audit.csv) over its 142 case
IDs: **116 direct / 19 n/a / 7 out-of-scope**. `direct` is the CSV's own status,
not a claim that every row is covered by a C unit test: the `scope` column marks
which rows are `library-core`, `library-optional`, `reference-integration` or
external (the CSV records the evidence per row). The out-of-scope set is
physical/HIL or product-IODD work, and each such row carries a residual reason.
The CSV is authoritative for these counts; the detailed 2026-10-28 audit report
is kept in the git history.

## Traceability anchors

Two identifier sets join this document to machine-readable data. Neither is
cosmetic and neither may be renamed:

- **Case IDs** — `SDCI_TC_*` (and the audit IDs written as
  `IOLINK_CONFORMANCE_CASE("...")` in `tests/*.c`). The `case_id` column of
  [`tests/conformance_audit.csv`](../tests/conformance_audit.csv) is
  authoritative; an annotation without a CSV row fails
  `tools/check_conformance_traceability.py`. Case IDs quoted in this document
  must exist in `conformance_audit.csv`; the checker also accepts an identifier
  that exists in `tests/spec_constants.csv`, so a single anchor never has to be
  maintained in two files.
- **Clause labels** — the specification's own numbering (`A.1.6`, `10.10.2`,
  `B.2.22`, `B.3`, `B.8`, `B.9`, `B.13`, `C.1`, `D.1`, `Table 10`, ...). These
  labels are stable anchors: the constants table in
  [`tests/spec_constants.csv`](../tests/spec_constants.csv) joins its `clause`
  column on them, so change a label only when the referenced clause itself
  changes.

## Spec constants

[`tests/spec_constants.csv`](../tests/spec_constants.csv) maps a specification
clause to one implementation symbol and its expected value. `test_spec_constants`
compiles the same symbols into a C lookup table and enforces both directions:
a CSV row without a C entry fails, a C entry without a CSV row fails, and a
value mismatch prints the clause, symbol, expected and actual values. Both
sides therefore change together; the CSV is the reviewable data, the C table is
the compiled oracle.

Every `clause` label must come from the hand-maintained list in
`tools/check_conformance_traceability.py` (the checker does not parse the PDF),
and every symbol below must exist in the CSV. `direct` rows of the audit CSV
must be reachable from this document by case ID, test file or test function.

| Clause | Constants |
|---|---|
| `Table 10` | `IOLINK_T_WU_US`, `IOLINK_T_REN_US` |
| `Table 42` | `IOLINK_T_DSIO_MS` |
| `10.10.2` | `IOLINK_EVENT_MIN_APPEAR_MS`, `IOLINK_EVENT_MIN_REPEAT_MS` |
| `A.3.3` | `IOLINK_T1_MAX_BITS` |
| `A.3.4` | `IOLINK_T2_MAX_BITS` |
| `A.3.5` | `IOLINK_T_A_MAX_BITS` |
| `A.3.6` | `IOLINK_UART_FRAME_BITS`, `IOLINK_MSEQ_MASTER_OCTET_BITS`, `IOLINK_MSEQ_DEVICE_OCTET_BITS`, `IOLINK_MSEQ_TIMING_FIXED_BITS` |
| `Table 43` | `IOLINK_T_FBD_MASTER_CYCLES`, `IOLINK_T_FBD_MAX_MS` |
| `Table 47` | `IOLINK_T_BYTE_WINDOW_BITS` |
| `Table B.3` | `IOLINK_CYCLE_TIME_UNIT_US`, `IOLINK_MIN_CYCLE_TIME_UNITS` |
| `7.3.3.2` | `IOLINK_M_SEQ_HEADER_LEN`, `IOLINK_M_SEQ_TYPE0_LEN`, `IOLINK_M_SEQ_MIN_LEN` |
| `A.1.2` | `IOLINK_MC_RW_MASK`, `IOLINK_MC_COMM_CHANNEL_MASK`, `IOLINK_MC_ADDR_MASK` |
| `Table A.1` | `IOLINK_MC_CHANNEL_PROCESS`, `IOLINK_MC_CHANNEL_PAGE`, `IOLINK_MC_CHANNEL_DIAGNOSIS`, `IOLINK_MC_CHANNEL_ISDU` |
| `A.1.3` | `IOLINK_MSEQ_TYPE_MASK`, `IOLINK_MSEQ_CHECKSUM_MASK` |
| `Table A.3` | `IOLINK_MSEQ_TYPE_0`, `IOLINK_MSEQ_TYPE_1`, `IOLINK_MSEQ_TYPE_2`, `IOLINK_MSEQ_TYPE_RESERVED` |
| `Table A.6` | `IOLINK_EVENT_BIT_STATUS` |
| `Table 52` | `IOLINK_FLOWCTRL_START`, `IOLINK_FLOWCTRL_IDLE`, `IOLINK_FLOWCTRL_IDLE2`, `IOLINK_FLOWCTRL_ABORT`, `IOLINK_FLOWCTRL_COUNT_MASK` |
| `Table B.2` | `IOLINK_CMD_FALLBACK`, `IOLINK_CMD_MASTER_IDENT`, `IOLINK_CMD_DEVICE_IDENT`, `IOLINK_CMD_DEVICE_STARTUP`, `IOLINK_CMD_PROCESS_DATA_OPERATE`, `IOLINK_CMD_DEVICE_OPERATE`, `IOLINK_CMD_DEVICE_PREOPERATE` |
| `Table A.12` | `IOLINK_ISDU_SERVICE_READ`, `IOLINK_ISDU_SERVICE_WRITE` |
| `Table B.8` | `IOLINK_IDX_DIRECT_PARAMETERS_1`, `IOLINK_IDX_DIRECT_PARAMETERS_2`, `IOLINK_IDX_SYSTEM_COMMAND`, `IOLINK_IDX_DATA_STORAGE`, `IOLINK_IDX_DEVICE_ACCESS_LOCKS`, `IOLINK_IDX_PROFILE_CHARACTERISTIC`, `IOLINK_IDX_VENDOR_NAME`, `IOLINK_IDX_VENDOR_TEXT`, `IOLINK_IDX_PRODUCT_NAME`, `IOLINK_IDX_PRODUCT_ID`, `IOLINK_IDX_PRODUCT_TEXT`, `IOLINK_IDX_SERIAL_NUMBER`, `IOLINK_IDX_HARDWARE_REVISION`, `IOLINK_IDX_FIRMWARE_REVISION`, `IOLINK_IDX_APPLICATION_TAG`, `IOLINK_IDX_FUNCTION_TAG`, `IOLINK_IDX_LOCATION_TAG`, `IOLINK_IDX_PRODUCT_URI`, `IOLINK_IDX_ERROR_COUNT`, `IOLINK_IDX_DEVICE_STATUS`, `IOLINK_IDX_DETAILED_DEVICE_STATUS`, `IOLINK_IDX_PROCESS_DATA_INPUT`, `IOLINK_IDX_PROCESS_DATA_OUTPUT`, `IOLINK_IDX_OFFSET_TIME`, `IOLINK_IDX_ERROR_STATS` |
| `Table B.9` | `IOLINK_CMD_PARAM_UPLOAD_START`, `IOLINK_CMD_PARAM_UPLOAD_END`, `IOLINK_CMD_PARAM_DOWNLOAD_START`, `IOLINK_CMD_PARAM_DOWNLOAD_END`, `IOLINK_CMD_PARAM_DOWNLOAD_STORE`, `IOLINK_CMD_PARAM_BREAK`, `IOLINK_CMD_DEVICE_RESET`, `IOLINK_CMD_APPLICATION_RESET`, `IOLINK_CMD_RESTORE_FACTORY_SETTINGS`, `IOLINK_CMD_BACK_TO_BOX` |
| `Table B.12` | `IOLINK_LOCK_PARAM`, `IOLINK_LOCK_DS`, `IOLINK_LOCK_LOCAL_PARAM`, `IOLINK_LOCK_LOCAL_UI` |
| `Table C.1` | `IOLINK_ISDU_ERROR_IDX_NOT_AVAIL`, `IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL`, `IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL`, `IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_LOCAL_CTRL`, `IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL`, `IOLINK_ISDU_ERROR_NOT_ACCESSIBLE`, `IOLINK_ISDU_ERROR_WRITE_PROTECTED`, `IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG`, `IOLINK_ISDU_ERROR_PAR_VAL_GT_LIM`, `IOLINK_ISDU_ERROR_PAR_VAL_LT_LIM`, `IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN`, `IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN`, `IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL`, `IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL`, `IOLINK_ISDU_ERROR_PARAM_SET_INVALID`, `IOLINK_ISDU_ERROR_PARAM_INCONSISTENT`, `IOLINK_ISDU_ERROR_APP_DEV_NOT_READY` |
| `Table B.13` | `IOLINK_DEVICE_STATUS_OK`, `IOLINK_DEVICE_STATUS_MAINTENANCE`, `IOLINK_DEVICE_STATUS_OUT_OF_SPEC`, `IOLINK_DEVICE_STATUS_FUNCTIONAL_CHECK`, `IOLINK_DEVICE_STATUS_FAILURE` |
| `B.1.5` | `IOLINK_REVISION_ID_1_0`, `IOLINK_REVISION_ID_1_1` |

## Transport

| Item | Status | Implementation | Test |
|---|---|---|---|
| Two-pump ISR transport (RX/TX BYTE and RX/TX FRAME) | PASS | `src/device.c` (`iolink_device_process_isr()`), `src/dll_isr.c` (`iolink_dll_process_isr()`) | `test_dll_isr`, `test_isr_*`, plus the full protocol suite via the test warp |
| M-sequence fixed types 0/1_2/1_V/2_1/2_2/2_3/2_4/2_5 | PASS | `src/device.c`, `src/dll.c` | `test_m_sequence_types`, `tools/virtual_master/tests/test_mseq_layout.py` |
| TYPE_1_1 (legacy interleave) rejected by V1.1.5 Device | PASS | `src/device.c` | `test_m_sequence_types` |
| TYPE_2_V (variable PD/OD) | PASS | `src/dll.c` | `test_pd_variable`, `test_dll` |
| OD width / OPERATE codes 0-7 (Table A.10) | PASS | `include/iolinki/iolink.h` | `test_protocol_tables` |
| Checksum A.1.6 (incl. ISR accumulator) | PASS | `src/crc.c` | `test_crc`, `test_crc_vectors` |
| Protocol constants (A.1/A.3/A.12/B.2/B.9/C.1/D.1) | PASS | `include/iolinki/protocol.h` | `test_protocol_constants` |
| OPERATE TYPE_0 restart for non-TYPE_0 devices (Table 45 T11; SDCI_TC_0038/0049) | PASS | `dll_handle_operate_type0()` via `dll_sm_type0_read()` | `test_dll_operate_type0_read_resets`, `test_dll_operate_type0_min_cycle_time_read` |
| ISR OPERATE → STARTUP notification remains observable before PREOPERATE | PASS | `startup_notification_pending`, `iolink_dll_flush_state_callbacks()` | `test_isr_startup_notification_is_not_coalesced` |
| RX FRAME receives complete frames only; truncated prefixes stay in PHY | PASS | `iolink_phy_api_t::rx_frame()` contract | `test_dll_isr` / PHY integration tests |

## Identification objects (Table B.8)

| Index | Object | Status | Test |
|---|---|---|---|
| 0x0000/0x0001 | Direct Parameter pages 1/2; DPP1 `0x00-0x0F`, DPP2 `0x10-0x1F`; unimplemented reads return 0 | PASS | `test_dll_type0_dpp2_unimplemented_read_returns_zero`, `test_isdu` |
| 0x0002 | SystemCommand (0x01-0x06, 0x80-0x83; 0x84-0x9F reserved and rejected) | PASS | `test_system_commands`, `test_isdu`, `test_isdu_wire` |
| 0x0003 | DataStorageIndex — sub 0 concatenated record (read-only); sub 01 DS_Command, 02 State_Property, 03 Size (UInt32), 04 Parameter_Checksum (UInt32), 05 Index_List. The pre-V1.1 whole-image sub 0 exists only under `IOLINK_CAP_DS_LEGACY_SUB0` | PASS | `test_ds`, `test_isdu`, `test_system_commands` |
| 0x0004-0x000B | Reserved (VendorID/DeviceID are DPP1) | PASS | `test_vendor_device_id_indices_reserved` |
| 0x000C | DeviceAccessLocks (RecordT; only subindex 0 is accessible, B.2.4) | PASS | `test_ident_indices`, `test_ds`, `test_spec_conformance`, `test_isdu` |
| 0x000D | ProfileCharacteristic (ArrayT of UIntegerT16, profile-conditional; subindex 0/1) | PASS | `test_ident_indices`, `test_isdu` |
| 0x0010-0x0017 | Identification strings | PASS | `test_ident_indices`, `test_spec_conformance` |
| 0x0018-0x001A | Application/Function/Location tags (optional / profile-conditional; default DS set) | PASS | `test_isdu`, `test_system_commands` |
| 0x001B | ProductURI (conditional) | PASS | `test_product_uri_index` |
| 0x0020 | ErrorCount (optional) | PASS | `test_error_count_index` |
| 0x0024 | DeviceStatus (Table B.13: 0..4) | PASS | `test_isdu`, `test_events` |
| 0x0025 | DetailedDeviceStatus (ArrayT of 3-octet entries, subindex 0..n; Error/Warning only, B.2.22) | PASS | `test_isdu` |
| 0x0028 | ProcessDataInput (last valid PD-in) | PASS | `test_process_data_input_returns_pd`, `test_isdu` |
| 0x0029 | ProcessDataOutput (last valid PD-out) | PASS | `test_process_data_output_returns_pd` |
| — | read-only objects reject WRITE (0x80 0x23) | PASS | `test_read_only_identification_rejects_write`, `test_isdu_wire` |

## Events, Data Storage, Timing, Locks

| Area | Status | Test |
|---|---|---|
| Event queue, qualifier, classify (Annex D), memory | PASS | `test_events` |
| Event appears/disappears lifecycle (10.10.2: pairing, 50 ms appear window, 1 s repeat limit, severity order, clear on comm loss) | PASS | `test_events` |
| Event memory frozen when the EventFlag is raised (Table 60 T3) | PASS | `test_events` |
| DPP1 0x0F SystemCommand (executed, no response, 7.3.5.3) | PASS | `test_isdu` |
| DeviceStatus mapping (B.13) + DetailedDeviceStatus filtering/order (B.2.22) | PASS | `test_isdu`, `test_events` |
| DS parameter serialization/apply, Fletcher-32 checksum, locks, recovery | PASS | `test_ds`, `test_system_commands` |
| DS block-download error classification (0x8040 malformed vs 0x8041 rejected parameter) | PASS | `test_ds` |
| DS parameter access: OD-derived set (`IOLINK_OD_STORAGE_DS`), entry backend, `block_len` | PASS | `test_ds`, `test_ds_app_od` |
| DS bindable parameter set incl. device-specific parameters (0x0040+/0x0100+) | PASS | `test_ds` |
| DS Index_List / Size / State_Property (incl. DS_UPLOAD_FLAG) / Checksum subindices | PASS | `test_isdu` |
| DS_Command (0x0003 sub 01) upload/download/break sequence | PASS | `test_isdu` |
| Block-parameter download buffering + atomic commit + ParamBreak rollback | PASS | `test_system_commands` |
| ParamDownloadStore (0x05) → DS_UPLOAD_REQ (event + State_Property flag) | PASS | `test_system_commands` |
| Back-to-box (0x83): restore delivery values + stop communication after 3 MasterCycles | PASS | `test_dll`, `test_system_commands` |
| t_ren / t_cycle / t_pd / OPERATE supervision / T_DSIO / T_FBD | PASS | `test_timing` |
| Access Locks read/write, DS lock | PASS | `test_ident_indices`, `test_ds` |
| Parameter lock (bit 0) enforcement | PASS | `test_parameter_lock_blocks_tag_write` |

## Conformance closures

`tests/test_spec_conformance.c` now passes; it pins the behaviour that was
previously missing:

- 0x000A/0x000B are reserved (VendorID/DeviceID live in DPP1).
- ErrorCount (0x0020) returns the summed DLL error counters.
- ProductURI (0x001B) is served read-only.
- ProcessDataInput/Output (0x0028/0x0029) return the last valid PD data: PDin
  answers `0x80 0x11` while the DLL flags it invalid, PDout answers `0x80 0x11`
  until a frame has been received; a device without PD answers with an empty
  positive response.
- A WRITE to a known read-only object answers `0x80 0x23`; an unknown index
  keeps `0x80 0x11`.
- DeviceAccessLocks bit 0 blocks parameter writes (tags and DPP2), not reads or
  Data Storage downloads.
- Identification strings are clamped to their object fixedLength (64, SerialNumber 16,
  ProductURI up to 100, tags 32).
- DeviceStatus (0x0024) follows Table B.13: Error → 4 (Failure), Warning → 2
  (Out-of-Spec) or 1 (Maintenance), Notification → 0; priority order is
  Failure > Functional-Check > Maintenance > Out-of-Spec.
- DetailedDeviceStatus (0x0025) lists only Error/Warning events (no
  Notifications), per B.2.22. Subindex 0 returns the whole list; element access
  through subindex 1..n is a device extension (B.2.22 describes subindex 0
  only).
- A StatusCode write confirms only the event slots that were presented while
  frozen; events queued during the readout survive to the next round.
- A re-raise inside the 1 s repeat window is deferred (not dropped), and a full
  pending table is observable through `iolink_events_get_dropped_count()`.
- An ISDU write-channel FlowCTRL `0x1F` (ABORT) resets the transport to Idle
  (Table 52), like the read channel.

## Deferred / known limitations

- **ErrorCount reset semantics** (B.2.20): the stack sums the DLL communication
  counters (`crc`, `framing`, `timeout`, `timing`), which are cleared on DLL
  initialization (power-on). A Device Reset (`0x80`) only raises
  `reset_pending`; the application owns re-initialization, so the "since reset"
  part depends on the application. B.2.20 explicitly allows vendor/device
  specific usage.
- **DS_UPLOAD_FLAG persistence** (Table B.10, State property): the flag is
  exposed through State_Property bit 7 and
  `iolink_device_config_t::ds_flag_persist` is called when it changes. The
  application owns the non-volatile copy; a `NULL` hook keeps the flag in RAM
  only. The library has no NVM backend and never reads storage — see
  [STORAGE.md](STORAGE.md).
- **DS parameter set defaults to the three tags.** `ApplicationSpecificTag`
  (0x0018, optional), `FunctionTag` (0x0019) and `LocationTag` (0x001A) are the
  profile-conditional tags declared `IOLINK_OD_STORAGE_DS` in the standard
  table, so they are always serialized. A build that must publish an empty
  `Index_List` (10.4.5) omits `IOLINK_FEATURE_DATA_STORAGE`, which also removes
  the mandatory 0x0003 object and is therefore non-conformant.

## Spec-mandated corrections

- SystemCommand `Param*` codes use Table B.9 `0x01-0x06`; `0x84` is reserved.
  (Historically upstream used `0x05-0x09`/`0x97`, but that comparison is no
  longer maintained; the PDF is the reference.) Pinned by
  `test_protocol_constants` / `test_system_commands`.
- `PAR{M}_SETINVALID=0x40` / `PAR_SETINCONSIST=0x41` per Table C.1.
- DPP1 `MinCycleTime` (0x02) is encoded as Table B.3 `time base | multiplier`
  and clamped to the 0.4 ms minimum; raw 0.1 ms units are only valid inside
  0x04-0x3F. Pinned by `test_protocol_tables`.
- DPP1 `RevisionID` (0x04) is one octet: bits 4-7 MajorRev, bits 0-3 MinorRev
  (Figure B.4), so V1.1 is 0x11 and V1.0 is 0x10. The IODD's 16-bit
  `iolinkRevision` (0x0111) is not the wire value. Pinned by
  `test_protocol_constants` and `test_conformance_gaps` (SDCI_TC_0092).
- `0x83` is **Back-to-box** (10.7.5): restore delivery values and stop
  communication after the ISDU response, not a plain "restore app defaults".
- Event codes: 0x1800-0x18FF are vendor-specific (Table D.1); the conflicting
  `APP_*`/`HW_*` duplicates and the stale `COMM_ERR_*` 0x18xx aliases are gone.
  `IOLINK_EVENT_COMM_*` is the single vendor communication-event set.
- ISDU "busy" answers use `SERV_NOTAVAIL` 0x20, not 0x30 (`PAR_VALOUTOFRNG`).

## Direct case visibility index

Every `direct` row of the audit CSV is reachable from this document by case ID,
test file or test function. The rows below are the ones no area table names
directly; each still carries its `IOLINK_CONFORMANCE_CASE` annotation in the
test function itself.

| Case ID | Test evidence | Pins |
|---|---|---|
| `SDCI_TC_0053` | `test_isr_observability` | Reserved FlowCTRL in the ISDU idle state answers "no service" |
| `SDCI_TC_0063` | `test_conformance_untested` | Write-length overrun keeps the previous parameter value |
| `SDCI_TC_0064` | `test_conformance_untested` | Write with a wrong length keeps the previous parameter value |
| `SDCI_TC_0068` | `test_isr_observability` | Aborted read response handling |
| `SDCI_TC_0122` | `test_conformance_untested` | Valid ApplicationSpecificTag write |
| `SDCI_TC_0123` | `test_conformance_untested` | Over-long tag write is rejected without losing the value |
| `SDCI_TC_0124` | `test_conformance_untested` | ErrorCount increments and resets |
| `SDCI_TC_0128` | `test_conformance_untested` | DeviceStatus in OPERATE |
| `SDCI_TC_0131` | `test_conformance_untested` | DetailedDeviceStatus is active after a restart |
| `SDCI_TC_0145` | `test_conformance_untested` | Download break by reset |
| `SDCI_TC_0146` | `test_conformance_device` | Illegal download breaks the block parameterization |
| `SDCI_TC_0155` | `test_conformance_untested` | Restore-factory-settings sequence |
| `SDCI_TC_0309` | `test_isr_observability` | ISDU segment repeat |
| `SDCI_TC_0316` | `test_conformance_untested` | Device reset sequence |
| `SDCI_TC_0317` | `test_conformance_untested` | Application reset sequence |
| `SDCI_TC_0318` | `test_conformance_untested` | Back-to-box sequence |
| `SDCI_TC_0329` | `test_conformance_untested` | Upload break by reset |
| `SDCI_TC_0373` | `test_conformance_device` | Event-driven DeviceStatus |

## Related

- `CODE_FLOW.md` (frame flow, A.1/A.3/A.10), `TESTING.md`,
  `security/THREAT_MODEL.md`.
