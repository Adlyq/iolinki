# IO-Link V1.1.5 Specification Conformance Matrix

Single source of truth for **what is implemented**, **where**, and **which test
proves it**, ordered by the bundled specification.

> **Authoritative reference:** `docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/IOL-Interface-Spec_10002_V1.1.5_Oct2025.pdf`.
> The V1.1.3 PDF is historical. When a doc/comment disagrees with V1.1.5, the
> PDF wins.

Status: **PASS** (implemented + tested), **PARTIAL** (subset covered), **N/A**
(conditional on a profile). All previously tracked gaps are closed, so the full
suite (including `test_spec_conformance`) is green: `ctest --output-on-failure`.

## Transport

| Item | Status | Implementation | Test |
|---|---|---|---|
| Streaming transport (`send`/`recv_byte`), default | PASS | `src/dll.c`, `src/frame.c` | `test_dll`, `test_frame` |
| Opt-in two-pump ISR transport (`-DIOLINK_ISR_PUMP=ON`) | PASS | `src/dll.c` (`#if` block) | `test_dll_isr` + the full protocol suite via the test warp |
| M-sequence Types 0/1_1/1_2/1_V/2_1/2_2/2_V | PASS | `src/dll.c`, `src/frame.c` | `test_m_sequence_types`, `test_dll` |
| OD width / OPERATE codes 0-7 (Table A.10) | PASS | `include/iolinki/iolink.h` | `test_protocol_tables` |
| Checksum A.1.6 (incl. streaming accumulator) | PASS | `src/crc.c` | `test_crc`, `test_crc_vectors` |
| Protocol constants (A.1/A.3/A.12/B.2/B.9/C.1/D.1) | PASS | `include/iolinki/protocol.h` | `test_protocol_constants` |

## Identification objects (Table B.8)

| Index | Object | Status | Test |
|---|---|---|---|
| 0x0000/0x0001 | Direct Parameter pages 1/2 | PASS | `test_dll`, `test_isdu` |
| 0x0002 | SystemCommand (0x01-0x06, 0x80-0x83; 0x84-0x9F reserved) | PASS | `test_system_commands`, `test_isdu`, `test_isdu_wire` |
| 0x0003 | DataStorageIndex — sub 01 DS_Command, 02 State_Property, 03 Size (UInt32), 04 Parameter_Checksum (UInt32), 05 Index_List; sub 0 whole-image (legacy) | PASS | `test_ds`, `test_isdu`, `test_system_commands` |
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
| read-only objects reject WRITE (0x80 0x23) | PASS | `test_read_only_identification_rejects_write`, `test_isdu_wire` |

## Events, Data Storage, Timing, Locks

| Area | Status | Test |
|---|---|---|
| Event queue, qualifier, classify (Annex D), memory | PASS | `test_events` |
| Event appears/disappears lifecycle (A.6.4: pairing, 50 ms window, 1 s repeat limit, severity order, clear on comm loss) | PASS | `test_events` |
| Event memory frozen when the EventFlag is raised (Table 60 T3) | PASS | `test_events` |
| DPP1 0x0F SystemCommand (executed, no response, 7.3.5.3) | PASS | `test_isdu` |
| DeviceStatus mapping (B.13) + DetailedDeviceStatus filtering/order (B.2.22) | PASS | `test_isdu`, `test_events` |
| DS parameter serialization/apply, Fletcher-32 checksum, locks, recovery | PASS | `test_ds`, `test_system_commands` |
| DS block-download error classification (0x8040 malformed vs 0x8041 rejected parameter) | PASS | `test_ds` |
| Unified NVM backend (callback-only), fixed region layout, magic/version, record checksum | PASS | `test_ds`, `test_params_persistence` |
| DS parameter access: default params+vendor_service, `set_param_desc`, `max_len` | PASS | `test_ds` |
| DS bindable parameter set incl. vendor parameters (0x0040+/0x0100+) | PASS | `test_ds` |
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
- ProcessDataInput/Output (0x0028/0x0029) return the last valid PD data.
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
  Notifications), per B.2.22.
- A StatusCode write confirms only the event slots that were presented while
  frozen; events queued during the readout survive to the next round.
- An ISDU write-channel FlowCTRL `0x1F` (ABORT) resets the transport to Idle
  (Table 52), like the read channel.

## Deferred / known limitations

- **ErrorCount reset semantics** (B.2.20): the stack sums the DLL communication
  counters (`crc`, `framing`, `timeout`, `timing`), which are cleared on DLL
  initialization (power-on). A Device Reset (`0x80`) only raises
  `reset_pending`; the application owns re-initialization, so the "since reset"
  part depends on the application. B.2.20 explicitly allows vendor/device
  specific usage.
- **DS_UPLOAD_FLAG persistence** (B.2.10): the flag is persisted through the
  unified NVM backend (flag region) and restored on init, in addition to being
  exposed through State_Property bit 7.
- **DS parameter set defaults to the three tags.** `ApplicationSpecificTag`
  (0x0018, optional), `FunctionTag` (0x0019) and `LocationTag` (0x001A) are the
  profile-conditional tags. Applications that do not want Data Storage can bind
  an empty descriptor set via `iolink_ds_set_param_desc()` / `iolink_ds_bind_param_access()`, which makes
  Index_List report the 0x0000 terminator (10.4.5).

## Deliberate divergences from upstream (PDF wins)

- SystemCommand `Param*` codes use Table B.9 `0x01-0x06` (upstream used
  `0x05-0x09`/`0x97`); `0x84` is reserved (upstream treated it as
  SetCommunicationMode). Pinned by `test_protocol_constants` /
  `test_system_commands`.
- `PAR{M}_SETINVALID=0x40` / `PAR_SETINCONSIST=0x41` per Table C.1.
- DPP1 `MinCycleTime` (0x02) is encoded as Table B.3 `time base | multiplier`
  and clamped to the 0.4 ms minimum; raw 0.1 ms units are only valid inside
  0x04-0x3F. Pinned by `test_protocol_tables`.
- `0x83` is **Back-to-box** (10.7.5): restore delivery values and stop
  communication after the ISDU response, not a plain "restore app defaults".
- Event codes: 0x1800-0x18FF are vendor-specific (Table D.1); the conflicting
  `APP_*`/`HW_*` duplicates and the stale `COMM_ERR_*` 0x18xx aliases are gone.
  `IOLINK_EVENT_COMM_*` is the single vendor communication-event set.
- ISDU "busy" answers use `SERV_NOTAVAIL` 0x20, not 0x30 (`PAR_VALOUTOFRNG`);
  `IOLINK_ISDU_ERROR_BUSY` is a deprecated alias of 0x20.

## Related

- `FRAMES.md` (A.1/A.3/A.10), `TESTING.md`, `CONFORMANCE.md` (virtual master),
  `security/THREAT_MODEL.md`, `ROADMAP.md`.
