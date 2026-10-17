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
| 0x0003 | DataStorageIndex | PARTIAL | `test_ds` |
| 0x0004-0x000B | Reserved (VendorID/DeviceID are DPP1) | PASS | `test_vendor_device_id_indices_reserved` |
| 0x000C | DeviceAccessLocks | PASS | `test_ident_indices`, `test_ds`, `test_spec_conformance` |
| 0x000D | ProfileCharacteristic (contains FunctionID) | PASS | `test_ident_indices` |
| 0x0010-0x0017 | Identification strings | PASS | `test_ident_indices`, `test_spec_conformance` |
| 0x0018-0x001A | Application/Function/Location tags | PASS | `test_isdu` |
| 0x001B | ProductURI (conditional) | PASS | `test_product_uri_index` |
| 0x0020 | ErrorCount (optional) | PASS | `test_error_count_index` |
| 0x0024 | DeviceStatus | PASS | `test_isdu`, `test_events` |
| 0x0025 | DetailedDeviceStatus | PASS | `test_isdu` |
| 0x0028 | ProcessDataInput (last valid PD-in) | PASS | `test_process_data_input_returns_pd`, `test_isdu` |
| 0x0029 | ProcessDataOutput (last valid PD-out) | PASS | `test_process_data_output_returns_pd` |
| read-only objects reject WRITE (0x80 0x23) | PASS | `test_read_only_identification_rejects_write`, `test_isdu_wire` |

## Events, Data Storage, Timing, Locks

| Area | Status | Test |
|---|---|---|
| Event queue, qualifier, classify (Annex D), memory | PASS | `test_events` |
| DetailedDeviceStatus filtering/order | PASS | `test_isdu` |
| Data Storage serialize/apply/checksum/locks/recovery | PARTIAL | `test_ds`, `test_system_commands` |
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

## Deferred / known limitations

- **ErrorCount reset semantics** (B.2.20): the stack sums the DLL communication
  counters (`crc`, `framing`, `timeout`, `timing`), which are cleared on DLL
  initialization (power-on). A Device Reset (`0x80`) only raises
  `reset_pending`; the application owns re-initialization, so the "since reset"
  part depends on the application. B.2.20 explicitly allows vendor/device
  specific usage.
- **`IOLINK_ISDU_ERROR_BUSY` (0x30)**: this legacy symbol is used for busy
  responses in the Data Storage handler, but Table C.1 defines `0x30` as
  `PAR_VALOUTOFRNG`. Kept for wire compatibility; tracked as a separate
  cleanup outside the ISDU conformance batch.

## Deliberate divergences from upstream (PDF wins)

- SystemCommand `Param*` codes use Table B.9 `0x01-0x06` (upstream used
  `0x05-0x09`/`0x97`); `0x84` is reserved (upstream treated it as
  SetCommunicationMode). Pinned by `test_protocol_constants` /
  `test_system_commands`.
- `PAR{M}_SETINVALID=0x40` / `PAR_SETINCONSIST=0x41` per Table C.1.

## Related

- `FRAMES.md` (A.1/A.3/A.10), `TESTING.md`, `CONFORMANCE.md` (virtual master),
  `security/THREAT_MODEL.md`, `ROADMAP.md`.
