# iolinki Conformance Audit — 2026-10-28

Independent, clean-context audit of the iolinki IO-Link Device stack against
IO-Link Test Specification 10032 V1.1.4 (June 2024) and the V1.1.5 Interface
Specification. Existing traceability and conformance annotations were treated
as claims to be verified, not as evidence.

Artifacts:

- This report: `docs/CONFORMANCE_AUDIT_2026-10-28.md`
- Machine-readable per-case audit: `tests/conformance_audit.csv` (142 rows)
- Temporary probe harness (not committed): `/tmp/audit/probe_states.c`

---

## 1. Scope and sources

| Source | Use |
|---|---|
| `IO-Link_Test-Spec_10032_V1.1.4_Jun24.pdf` | Normative conformance oracle; extracted with `pdftotext -layout` to `/tmp/test_spec.txt` |
| `IOL-Interface-Spec_10002_V1.1.5_Oct2025.pdf` | Normative implementation reference |
| `host/conformance/spec_cases.py`, `spec_checks.py`, `test_spec_all.py` | HIL case classification and the 72 executable checks |
| `docs/IO-Link测试项汇总分类.csv` | The project's 142-item device case list used as the audit index |
| `tests/*.c` | Unit-test evidence; annotations via `IOLINK_CONFORMANCE_CASE` |
| `tests/conformance_traceability.csv` | Existing traceability, audited as suspect |

The audit index is the 142-row HIL classification CSV. It is **not** the full
Test Spec: Annex C of the supplied PDF lists 313 unique case IDs (Master, PL,
Device protocol, IODD). The 142 rows are the device-oriented subset used by this
project plus four obsolete IDs, six `CP_TC_*` profile cases, `SDCI_TC_priv`,
`TC_Device_Identification`, and the PL cases 0304/0305. Device protocol text was
taken from PDF section 6; IODD functional/parameter text from section 7. Cases
not present in this PDF edition (`CP_TC_*`, `priv`, `TC_Device_Identification`,
obsolete 0042/0048/0100/0130) were judged from the CSV description and are
flagged as such in the CSV.

### What was executed

```
cmake -B build && cmake --build build -j4     # success
(cd build && ctest --output-on-failure)       # 50/50 test binaries pass
```

Passing `ctest` is not conformance evidence: `test_conformance_gaps` reports
6 skips and `test_isdu_wire` reports 1 skip. The skipped assertions are exactly
some of the known non-conformances below.

---

## 2. Method and status definitions

For each of the 142 cases I derived, independently from the Test Spec text:

1. EUT state before the action (STARTUP/AWAITING_COMM, PREOPERATE, OPERATE).
2. Exact index/subindex, request length, response bytes, ErrorType.
3. The layer actually exercised: wire M-sequence, DLL/ISR, ISDU API, DS API,
   parameter API.
4. Whether the assertion has an independent oracle or is vacuous/tautological
   (e.g. expected value produced by the same function under test).
5. Whether power-cycle/communication-interruption/NVM requirements are met or
   only simulated by a context re-init.

### Final disposition (authoritative)

`tests/conformance_audit.csv` is the authoritative per-case state after the
scope split:

| Status | Count |
|---|---:|
| direct | 111 |
| n/a | 19 |
| out-of-scope | 12 |
| **Total** | **142** |

There are no in-scope `partial`, `gap`, `known-gap` or `conflict` rows. The 12
out-of-scope rows are physical/HIL or product-IODD responsibilities and carry a
`residual` reason (`external-physical`, `external-power`, `external-reset`,
`external-timing` or `external-product-iodd`).

Current status vocabulary: `direct`, `n/a`, `out-of-scope`. The earlier audit
labels (`partial`, `gap`, `known-gap`, `conflict`, `HIL-only`, `IODD-oracle`)
are retained only in the historical sections below.

### Initial baseline (historical)

The first pass over the unchanged tree produced 20 direct / 53 partial / 29 gap
/ 5 known-gap / 1 conflict / 18 n/a / 5 HIL-only / 11 IODD-oracle. Those counts
describe the starting point, not the current state. The HIL project has 72
`CHECKS` entries; several are weaker than the Test Spec (see §7.3).

> Sections 3-7 preserve the initial audit findings for chronology. They are
> **historical**; use `tests/conformance_audit.csv` for the final per-case
> status.

---

## 3. Initial state-transition findings (0034–0052, 0089–0104)

This is the highest-risk area: several tests assert the right state name while
bypassing the state-specific frame layout, and two production behaviours
contradict the Test Spec.

| Case | Status | Evidence / verdict |
|---|---|---|
| 0034 | gap | No test varies CycTime or reads DPP1 0x02–0x06 in STARTUP (Table 52). |
| 0035 | gap | No test. **Probe shows non-conformance**: from AWAITING_COMM/STARTUP, 0x99 is ACKed (`tx=75`), state becomes ESTAB_COM, and the following OPERATE-type ISDU idle is answered (`state=4`, `tx=00 00 00 00 75`). Table 53 requires a non-V1.0 device not to answer. Root cause: `src/dll.c:1062-1067` promotes the startup frame to PREOPERATE, `:1098-1107` dispatches 0x99, `:1146-1165` then answers OPERATE traffic. |
| 0036 | gap | No test. Current `!type_ok` path outside OPERATE (`src/dll.c:1187-1206`) should drop the illegal OPERATE-type frame, but this is unverified. |
| 0037 | gap | No test. 0x97 is implemented (`src/dll.c:446-448`); sequence is unverified. |
| 0038 | gap | No test. **Probe shows non-conformance**: in OPERATE, a TYPE_0 page read of 0x02 is answered (`state=4`, `tx=1E 28`) and the device stays in OPERATE on the second read. Table 57 requires no reply to the first read and OPERATE→STARTUP. `src/dll.c:160-164` explicitly allows TYPE_0 page reads in OPERATE; `:602-632` answers them. |
| 0039 | partial | `test_dll_startup_to_preoperate_read_dpp1` reaches PREOPERATE via 0x9A and reads DPP1 0x03, but the expected value is produced by `iolink_isdu_direct_param_page1_octet()` — the function under test (`tests/test_dll.c:488,518`). Only one octet, no saved PDIn/PDOut/M-sequence check, no no-PD assertion. |
| 0040 | partial | `test_dll_startup_to_preoperate_write_dpp1` writes DPP1 0x01=0x4F and asserts only the CKS ack and PREOPERATE (`tests/test_dll.c:522-562`). No read-back/mirror, no write-length check. |
| 0041 | gap | **Mislabeled**: `test_dll_receive_error_discards_partial_frame` is tagged 0041 but injects a PHY receive error, not a one-octet-short DPP1 VendorID write (Table 60) (`tests/test_dll.c:657-687`). |
| 0042 | n/a | Removed by V1.1.4. |
| 0043 | n/a | Spec procedure step a explicitly stops the test when PREOPERATE uses TYPE_0; this stack does. |
| 0044 | gap | No test executes the Table 62 type matrix. Logic exists at `src/dll.c:1187-1206`. |
| 0045 | partial | `test_dll_preoperate_to_operate_read_dpp1` reaches OPERATE via 0x9A/0x99/0x98 and reads DPP1 0x03, but the expected octet is again the production helper (`tests/test_dll.c:596,653`). Only one octet, no PD check. |
| 0046 | gap | No OPERATE DPP1 write test (Table 65). |
| 0047 | gap | No short-write negative test in OPERATE (Table 66). |
| 0048 | n/a | Removed by V1.1.4. |
| 0049 | gap | Same TYPE_0-in-OPERATE conflict as 0038 (Table 67); no test. |
| 0052 | partial | `test_isdu_direct_parameters_page1` reads the DPP1 object through the ISDU API and checks bit0; it is not a DPP1 M-sequence read, and PREOPERATE/OPERATE is not established (`tests/test_isdu.c:682-723`). The ISR page-read test compares against the same production helper (`tests/test_isr_observability.c:397-399`). |
| 0089 | known-gap | `test_0089_master_cycle_time_readback` skips because `build_direct_param_page1()` never copies `master_cycle_time` into page[0x01]; it remains 0. Runtime confirms SKIPPED (`tests/test_conformance_gaps.c:131-156`, `src/isdu.c:1178-1205`). |
| 0090 | partial | Tests only the Table B.3 encode/decode helpers and the worst-case computation. It never reads DPP1.MinCycleTime and never compares with the IODD `minCycleTime` (`tests/test_protocol_tables.c:27-95`, `tests/test_dll.c:689-712`). |
| 0091 | partial | Reads DPP1 via the ISDU object; checks bit0 but not the IODD value or bits6/7 (`tests/test_isdu.c:682-723,824-837`). |
| 0092 | IODD-oracle | No test. Implementation hardcodes `page[0x04] = 0x11`, ignoring `info->revision_id`; the default info revision is `0x0001` (`src/isdu.c:1189`, `src/device_info.c:40`). This is also an implementation concern. |
| 0093 | IODD-oracle | No test reads DPP1 0x05; 0132 tests PD data, not the descriptor (Table 117). |
| 0094 | IODD-oracle | No test reads DPP1 0x06; 0133 tests PD data, not the descriptor (Table 118). |
| 0095 | partial | DPP1 VendorID matches the fixture `vendor_id`, not the IODD/assigned VendorID (`tests/test_isdu.c:700-701`). |
| 0096 | partial | DPP1 DeviceID matches the fixture `device_id`, not the IODD (`tests/test_isdu.c:702-704`). |
| 0097 | direct | Reads DPP1 0x0C/0x0D through subindices 0x0D/0x0E and asserts 0x00 (`tests/test_conformance_gaps.c:162-178`). OPERATE precondition not established, but the value assertion is real. |
| 0101 | direct | In OPERATE, writes 0x00–0xFF to DPP1 0x0E and asserts state stays OPERATE and no event after each byte (`tests/test_conformance_gaps.c:454-486`). Exhaustive and meaningful. |
| 0104 | partial | Samples 0x07/0x3F/0x84/0x9F and 0xFF; asserts 0x8035. It does not enumerate the capability-dependent ranges required by Table 124 (`tests/test_system_commands.c:78-97`, `tests/test_isdu_wire.c:360-376`). |

### Probe evidence (temporary, not committed)

The probe was compiled from `/tmp/audit/probe_states.c` against
`build/libiolinki.a` and `tests/test_helpers.c`. Relevant output:

```
after wake state=1
after 0x99 from STARTUP: state=3 txlen=1 tx=75
after OPERATE-type ISDU idle: state=4 txlen=5 tx=00 00 00 00 75
0038 start state=4
after TYPE_0 page read in OPERATE: state=4 txlen=2 tx=1E 28
after second TYPE_0 page read: state=4 txlen=2 tx=1E 28
```

State values: 1 = AWAITING_COMM, 3 = ESTAB_COM, 4 = OPERATE. This is direct
evidence that 0035/0038/0049 are implementation gaps, not only test gaps.

---

## 4. Mislabeled tests

These tests carry a case ID but do not execute that case's scenario.

| Annotation | Actual test | Why it is mislabeled | Evidence |
|---|---|---|---|
| `SDCI_TC_0041` | `test_dll_receive_error_discards_partial_frame` | Injects a PHY receive error; Table 60 requires a one-octet-short DPP1 VendorID write and subsequent correct write. | `tests/test_dll.c:657-687` |
| `SDCI_TC_0082` | `test_ds_download_end_error_classification` | Tests `-5`/`-4` error classification of malformed/rejected DS images; Table 104 requires download after parameter modification. | `tests/test_ds.c:381-404` |
| `SDCI_TC_0090` | `test_min_cycle_time_from_m_sequence` | Tests the internal worst-case compute helper, not DPP1.MinCycleTime vs IODD. | `tests/test_dll.c:689-712` |
| `SDCI_TC_0107` | `test_isdu_data_storage_subindices` | Reads sub2–sub5 only; never reads sub0, which is the measured object. | `tests/test_isdu.c:557-619` |
| `SDCI_TC_0108` | `test_isdu_data_storage_subindices` | Does not read sub1 and does not range-check sub3; Table 126 requires sub1..sub5. | `tests/test_isdu.c:557-619` |
| `SDCI_TC_0373` | `test_event_helpers` | Only checks event severity/get_all ordering; never reads DeviceStatus or DetailedDeviceStatus. | `tests/test_events.c:123-154` |
| `SDCI_TC_0070` | `test_0070_single_event_preoperate` | Never establishes PREOPERATE; only triggers an event and checks a flag. | `tests/test_conformance_gaps.c:670-681` |
| `SDCI_TC_0080` | `test_0080_local_change_requests_upload` | Calls `iolink_ds_request_upload()` directly; no local parameter change, no upload. | `tests/test_conformance_gaps.c:348-363` |
| `SDCI_TC_0052` | `test_type0_page_read_fast_path` | Oracle is `iolink_isdu_direct_param_page1_octet()` — the function under test. | `tests/test_isr_observability.c:397-399` |
| `SDCI_TC_0039`/`0045` | `test_dll_startup_to_preoperate_read_dpp1`, `test_dll_preoperate_to_operate_read_dpp1` | Expected DPP1 octet is generated by the same production function; not an independent oracle. | `tests/test_dll.c:488,596,518,653` |

Related overstatements (not strictly mislabels):

- `SDCI_TC_0054` → `test_isdu_vendor_name_read` asserts the default fixture
  `"iolinki"`, not the registered vendor/IODD name.
- `SDCI_TC_0104` → `test_system_cmd_invalid` checks only 0xFF, not the ranges.
- `SDCI_TC_0114` → `test_isdu_vendor_name_read` does not validate UTF-8 or the
  vendor registration.
- `SDCI_TC_0156` → lock tests never perform the required 0x0000 unlock followed
  by a successful parameter write.

---

## 5. Implementation gaps found during the audit

These are code-level findings, not test-gap findings. No production code was
modified.

### P0 — direct Test Spec contradictions

1. **0035 / 0038 / 0049 — illegal state transitions accepted.**
   `src/dll.c:160-164` allows TYPE_0 page reads in OPERATE, and `:602-632`
   answers them; `src/dll.c:1062-1067` + `:1098-1107` accept 0x99 from the
   startup phase. Probe evidence in §3. Test Spec 10032 Tables 53/57/67 require
   the opposite for a non-V1.0 device.

2. **0065 — corrupt CHKPDU answered with 0xC4 instead of No Service.**
   `test_isdu_wire_chkpdu_corrupt` explicitly skips on 0xC4
   (`tests/test_isdu_wire.c:333-357`). Table 84 requires I-Service 0.

3. **0107 — DataStorageIndex sub0 is the wrong object.**
   `src/isdu.c:1352-1365` returns the legacy DS image for Index 3 sub0. Table
   125 requires a 12-octet header + 3-octet index-list entries; the test never
   reads sub0 (`tests/test_isdu.c:557-619`).

4. **0108 — DataStorageIndex sub1 returns zero octets.**
   `src/isdu.c:1367-1370` sets `response_len = 0`. Table 126 requires 1 octet
   with value 0..5. The test omits sub1.

5. **0089 — DPP1.MasterCycleTime never populated.**
   `build_direct_param_page1()` zeroes page[0x01] and never writes
   `master_cycle_time` (`src/isdu.c:1178-1205`). The test skips (`SKIPPED`).

6. **0092 — DPP1.RevisionID hardcoded.**
   `src/isdu.c:1189` writes 0x11 unconditionally; `info->revision_id` (default
   0x0001, `src/device_info.c:40`) is ignored. The IODD comparison cannot pass
   for a configured revision.

7. **0123 — overlong ApplicationSpecificTag returns the wrong ErrorType.**
   `src/params.c:186-190` rejects length ≥33; `src/isdu.c:621-635` maps the
   failure to 0x8020 (SERVICE_NOT_AVAIL). Table 142 requires 0x8033
   (VAL_LENOVRRUN). The test skips (`SKIPPED`).

8. **0110 — reserved DeviceAccessLocks bits accepted.**
   `src/isdu.c:997-1003` stores any 16-bit value without validating bits 4..15.
   Table 129 requires 0x8030 and a zero readback. The test skips (`SKIPPED`).

### P1 — likely contradictions requiring a decision

9. **0072 — events cleared on communication interruption.**
   `src/dll.c:86-91` clears the event queue on every entry to STARTUP. Test
   Spec Table 92 requires the event flag to survive a communication
   interruption. Whether the master-reset/T_DSIO path reaches STARTUP must be
   confirmed on hardware, but the policy is suspicious.

10. **0374 — system-command UploadEnd does not clear DS_UPLOAD_FLAG.**
    `src/isdu.c:868-906` calls `iolink_ds_handle_command()` directly for
    SystemCommand 0x02/0x04; only `iolink_ds_handle_ds_command()` clears the
    flag (`src/data_storage.c:573-575`). Table 109 requires the flag cleared
    after UploadStart/UploadEnd via the system command. The unit test clears it
    with an API call instead.

11. **0376 — CKS PD-valid for PD-out-only devices.**
    If configured with `pd_in_len = 0`, 0x99 still sets `pd_valid = false`
    (`src/dll.c:459-467`) and replies set CKS bit 6 (`:613-615`). Table 70
    requires "Process Data valid" always for output-only devices. Conditional
    on the configuration.

12. **0315 — no incompatible-DeviceID rejection mechanism.**
    No alternate-DID/overwrite code was found in `src/`. Table 179 is mandatory
    for devices that support the mechanism; the case cannot pass without it.

13. **0324–0330 — ParameterManager states not implemented.**
    `src/data_storage.c:502-525` models only Idle/Downloading and a break; there
    is no upload-in-progress state, no read lock during download, and no
    unexpected-command handling. Most of Table 161–167 cannot pass.

---

## 6. Fixture and oracle gaps

| Gap | Cases affected | Impact |
|---|---|---|
| No machine-readable IODD is loaded by the unit suite | 0093, 0094, 0141, 0142, 0149–0152, 0157, `CP_TC_*`, `TC_Device_Identification` | Content/DPP1/parameter verification cannot be judged. `examples/*/device.xml` exist but are unused as oracles. |
| No 64-octet Config1 writable object | 0063 | Test is intentionally skipped; 0123 covers tag overrun only. |
| No fixed-length non-StringT writable parameter | 0064, 0141, 0142 | Length overrun/underrun for non-string standard parameters cannot be tested. |
| No real power-cycle/NVM fixture | 0072, 0073, 0122, 0124, 0131, 0316–0318, 0374 | Tests use context re-init/storage mocks, not a power cycle. |
| No actuator/local-UI fixture | 0080, 0148, 0312, 0313 | Failsafe and local-locking behaviour cannot be observed. |
| No reference master / scope | 0304, 0305, `SDCI_TC_priv` | Physical-layer timing/wake-up cannot be measured. |
| No V1.0 reference master / alternate DID fixture | 0085–0087, 0306, 0314 | Conditional legacy/DID cases are not applicable or not executable. |

The HIL project partially fills some of these (it has master control, DS, event
and power-cycle helpers), but its 72 checks are not uniformly stronger than the
unit tests. Examples:

- `check_0107` only asserts `"OK" in od get 0x0003 0` (`spec_checks.py:241-244`),
  not the 12+3n length.
- `check_0108` only asserts `"OK" or "ERR"` for sub1..5
  (`spec_checks.py:265-269`).
- `check_0110` only reads the object (`spec_checks.py:259-263`).
- `check_0104` only checks one reserved command returns `ERR`
  (`spec_checks.py:235-238`).

---

## 7. Existing traceability: what is wrong

`tests/conformance_traceability.csv` has 74 rows and uses the statuses
`direct/partial/gap/known-gap/conflict/covered`; 79 case IDs are annotated in
the test sources. Specific problems:

### 7.1 Clearly incorrect mappings

| Case | Existing claim | Audit finding |
|---|---|---|
| 0107 | `test_isdu_data_storage_subindices`, `direct` | The test never reads sub0. Implementation returns the DS image for sub0. Should be `gap` + P0 implementation issue. |
| 0108 | same test, `direct` | The test does not read sub1 and does not range-check sub3. Should be `partial`, and sub1 is an implementation gap. |
| 0090 | `test_min_cycle_time_from_m_sequence`, `direct` | The test does not read DPP1.MinCycleTime or use the IODD. Should be `partial`. |
| 0104 | `test_system_cmd_invalid`, `direct` | Only 0xFF is tested; Table 124 requires ranges. Should be `partial`. |
| 0039/0040 | `direct` | The DPP1 expected value is produced by the same function under test; only one octet is read/written. Should be `partial`. |
| 0156 | `direct` | No unlock (0x0000) + successful rewrite. Should be `partial`. |
| 0054 | `direct` | Content oracle is the fixture `"iolinki"`, not the registered vendor/IODD. Should be `partial`. |
| 0114 | `direct` | Same content/oracle issue. Should be `partial`. |

### 7.2 Missing rows for existing annotations

These IDs are annotated in tests but absent from the traceability CSV:

`SDCI_TC_0041`, `SDCI_TC_0045`, `SDCI_TC_0082`, `SDCI_TC_0090`,
`SDCI_TC_0143`, `SDCI_TC_0144`, `SDCI_TC_0375`.

### 7.3 Invalid status vocabulary / empty rows

- `covered` is used for 0112, 0113, 0134 but is not one of the project's
  defined statuses.
- 0072 and 0157 are listed as `gap` with no test file; that is honest, but
  neither points to the HIL check that exists.
- 0122 points to function `main` in `test_params_persistence.c`, which is the
  test binary entry point, not a named test function.

---

## 8. Prioritized remediation plan

No production code or existing tests were changed by this audit.

### 8.1 Test gaps (highest value first)

1. **State machine 0034–0049**: add one table-driven mock-PHY test that,
   for each case, establishes the exact state, sends the exact frame, and
   asserts reply/no-reply plus the next state. Do not compare against
   `iolink_isdu_direct_param_page1_octet()`; use literal expected bytes.
2. **0035/0038/0049**: write the failing tests first; they should expose the
   current implementation behaviour in §5.
3. **0107/0108**: add sub0 and sub1 reads with exact length/value assertions.
4. **0104**: enumerate all capability-dependent reserved command ranges.
5. **0156**: add lock → rejected write → 0x0000 → accepted write.
6. **0072/0073/0374**: add a power-cycle/communication-interruption fixture or
   explicitly mark these as HIL-only.
7. **0316–0318/0155**: add the full functional sequences, not only the command
   flag.
8. **0321/0322/0324–0330**: add ParameterManager state tests once the
   implementation exists.
9. **0039/0040/0045**: replace the tautological oracle with literal DPP1
   vectors and all saved communication parameters.

### 8.2 Implementation gaps

1. Fix 0035/0038/0049 state transitions (`src/dll.c`).
2. Fix 0065 CHKPDU rejection (`src/isdu.c`).
3. Implement 0107/0108 DataStorageIndex sub0/sub1 (`src/isdu.c`).
4. Populate DPP1 0x01 from `master_cycle_time` and DPP1 0x04 from
   `info->revision_id` (`src/isdu.c`).
5. Map tag length errors to 0x8033/0x8034 (`src/isdu.c`/`src/params.c`).
6. Validate reserved DeviceAccessLocks bits (`src/isdu.c`).
7. Clear `ds_upload_flag` on system-command UploadEnd/DownloadEnd
   (`src/isdu.c`).
8. Review event clearing on STARTUP against 0072 (`src/dll.c`).
9. Implement ParameterManager states 0324–0330 (`src/data_storage.c`).
10. Review PD-out-only CKS validity 0376 and the DID mechanism 0315.

### 8.3 Fixture / oracle gaps

1. Vendor a canonical `device.xml` (or use `examples/reference_device/device.xml`)
   and add an IODD parser/test helper for 0093/0094/0149–0152/0157/CP_TC.
2. Add a manufacturer 64-octet writable object for 0063 and a fixed-length
   non-StringT writable object for 0064/0141/0142.
3. Add a real power-cycle fixture (or a documented NVM + reset harness) for
   0072/0073/0122/0124/0131/0316–0318/0374.
4. Add an actuator/local-UI fixture for 0080/0148/0312/0313.
5. Keep the PL cases 0304/0305/`priv` in the HIL/scope run; do not claim unit
   coverage.
6. Replace the weak HIL checks (0107/0108/0110/0104) with assertions on the
   actual Table values, not just `OK`/`ERR`.

---

## 9. Validation performed and remaining uncertainty

### Validation

- All 142 rows were matched to a Test Spec case where the case exists in the
  supplied PDF; source text was extracted with `pdftotext -layout`.
- All 79 `IOLINK_CONFORMANCE_CASE` annotations were enumerated and compared
  with the 74-row traceability CSV.
- The unit suite was built and run: 50/50 binaries pass; the known-gap and
  conflict skips were observed directly.
- A temporary CMocka probe was compiled against `build/libiolinki.a` to confirm
  the 0035/0038 state behaviour. It is not committed and its teardown reported
  leftover mock expectations; the printed state/tx values are from the action
  path and are consistent with the static analysis.

### Remaining uncertainty

1. **Scope mismatch.** The supplied 10032 V1.1.4 PDF Annex C has 313 case IDs;
   the project's 142-row list is a device subset that also contains cases not in
   this PDF edition (`CP_TC_*`, `priv`, `TC_Device_Identification`, obsolete
   IDs). Their audit entries are based on the project CSV text, not the PDF.
2. **Conditional applicability.** `n/a` decisions use the current defaults:
   no profile (`profile_characteristic = 0`), no V1.0 compatibility, no
   additional DID, DS enabled, no OffsetTime. A different build/configuration
   can make these applicable.
3. **HIL not executed.** The `hil_check` column records the presence/category
   of a HIL check from `spec_checks.py`; it does not claim the check passed.
   No `HIL=1` run was made.
4. **Hardware-only questions.** The 0072 event-persistence conflict depends on
   whether the master-reset/T_DSIO path reaches `STARTUP`; 0376 depends on the
   configured PD-out-only build; 0312/0313 depend on the application failsafe
   behaviour.
5. **Not dynamically probed.** 0315 (no DID mechanism), 0324–0330
   (ParameterManager states), and the exact IODD-dependent values were judged
   statically from the source and the absence of a mechanism/oracle.
6. **Quality gate not run.** `check_quality.sh` (MISRA/cppcheck/Doxygen) was not
   run because this audit changed no production code; it is not evidence about
   conformance.

---

## 10. Bottom line

The repository has a substantial unit suite, but only 20 of the 142 audited
cases are directly and meaningfully covered. The most serious problems are not
missing assertions alone: 0035/0038/0049 accept state transitions the Test Spec
forbids, 0065 returns 0xC4 where No Service is required, and 0107/0108 expose
the wrong DataStorageIndex structure. The existing traceability overstates
coverage in several places (notably 0107, 0108, 0090, 0104) and omits seven
annotated cases. The HIL project's 72 checks should not be treated as a
conformance oracle until their assertions are strengthened.

---

## 11. Post-audit P0 update

After this audit, the following production changes and tests were added:

- `0035`: STARTUP `0x99`/`0x98` is rejected; negative test added.
- `0038/0049`: TYPE_0 page read in OPERATE returns STARTUP without a reply;
  reset test added.
- `0065`: invalid CHKPDU is discarded and the next read returns No Service.
- `0089`: DPP1 0x01 now carries the MasterCycleTime value.
- `0092`: DPP1 0x04 uses the configured RevisionID low octet.
- `0107/0108`: DataStorageIndex sub0 is the concatenated 12+3n record and
  sub1 returns one octet.
- `0110`: reserved DeviceAccessLocks bits are rejected with `0x8030`.
- `0374`: DS_UploadEnd clears and persists `DS_UPLOAD_FLAG`.
- Phase 2: state/DPP1 tests added for `0034/0036/0037/0041/0044/0046/0047`;
  `0376` also required the PD-status fix for PDOut-only Devices.
- Phase 3: DS tests added for `0072/0081/0082/0083/0145/0147/0324–0330`;
  the DS state machine now handles unexpected endings, opposite-action
  switching, upload/download access locks and staged-download discard.
  Remaining DS gaps: `0146/0148/0315/0377`.
- L3 reference integration added: `examples/conformance_device` with
  full/config/PDOut-only/minimal/type0 profiles, synthetic IODD/oracle,
  file-backed NVM, event/PD/local-change hooks, a Linux PTY smoke test and a
  TYPE_0 master subset. This is a reference-device capability check, not a
  product certification.
- The audit CSVs now carry `scope` and `profile` columns:
  `library-core`, `library-optional`, `reference-integration`,
  `physical-hil`, `iodd-oracle`, `product-integration`, `not-applicable`.
- `0315` closed as a library-core negative test: incompatible DeviceID writes
  are ignored and the configured DID survives a restart. No additional
  incompatible DeviceID is enabled.
- `0148` is classified `product-integration` / `n/a` for the library; local UI
  locking belongs to the product application.
- Synthetic IODD oracle added: `0063/0064/0123/0141/0142/0092/0093/0094`,
  `0114-0121`, `0149-0152` are now direct. `0157` was later moved to
  `out-of-scope` as product-IODD work; the oracle is a test profile, not a
  product IODD.
- Per-device identity/access-lock handling now uses the bound `device_info`
  context instead of the legacy global identity.
- Event wire-level checks now cover `0069-0076`; the frozen-memory
  acknowledgement bug (removing a reordered queue head) is fixed.
- Intermediate parallel-hardening checkpoint: 104 direct / 8 partial / 19 n/a /
  11 HIL-only. The eight partials were subsequently closed or reclassified.
- Final project scope separates external work: 111 direct / 19 n/a /
  12 out-of-scope. External items are HIL, physical-layer and product-IODD
  responsibilities; `residual` records the reason. No in-scope partials remain.
- `tools/check_conformance_traceability.py` enforces the matrix and annotations
  in CTest.

The per-case status in `tests/conformance_audit.csv` is updated accordingly.
All in-scope cases are closed; the only remaining validation is external HIL,
physical-layer, reset/power/timing and product-IODD work.
