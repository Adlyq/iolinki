# HIL Conformance Check Correction Guide

Date: 2026-10-28
Audience: maintainers of the host HIL conformance project
Scope: correction guidance only. This document does **not** modify HIL code.

Sources:

- IO-Link Test Specification 10032 V1.1.4 (conformance oracle)
- IO-Link Interface and System Specification V1.1.5
- `tests/hil_audit.csv` (72 implemented checks, strictness classification)
- `docs/HIL_AUDIT_2026-10-28.md` (full audit)

---

## 1. Why this guide exists

The current HIL run reports 62 passes, but only 5 of the 72 implemented checks
are exact Test-Spec checks. The remaining checks are partial, shallow or run a
different scenario. A pass from those checks is not conformance evidence.

Do not change the reported pass count by loosening assertions. The goal is to
make each check assert the Test Spec's precondition, procedure and evaluation.

Strictness vocabulary:

| Value | Meaning |
|---|---|
| exact | Precondition, procedure and evaluation are all implemented |
| partial | Correct scenario, but at least one required step or assertion is missing |
| shallow | Only a weak proxy is asserted (`OK`/`ERR`, length only, no exception) |
| wrong-scenario | Wrong state, object, command or interruption; a pass does not exercise the case |
| not-applicable | Case is outside the DUT scope, or the check is only a skip stub |
| blocked-oracle | Required IODD/vendor reference data is unavailable |

---

## 2. Priority

| Priority | Count | Work |
|---|---:|---|
| P0 | 11 | Fix wrong-scenario checks |
| P1 | 28 | Replace shallow assertions with exact criteria |
| P2 | 6 | Re-evaluate n/a decisions |
| P3 | 1 | IODD-blocked check |
| P4 | 44 | Cases not present in `CHECKS` (classify: HIL-only, IODD, unit-only, n/a) |

---

## 3. P0: wrong-scenario checks

### 3.1 `SDCI_TC_0060` — 16-bit write

Current: `check_0060` calls `check_0059`, which reads reserved index `0x7F32`.

Required:

1. Use a 16-bit writable object (Config2 or an equivalent writable 16-bit
   object declared by the DUT).
2. Send a 16-bit ISDU write request with a valid payload.
3. Check busy handling and the positive write response.
4. Read the object back and compare with the written value; if the object is not
   available, accept only the specified `0x8011` response.

Do not reuse the reserved-index read check.

### 3.2 `SDCI_TC_0063` — Config1 length overrun

Current: writes 33 bytes to ApplicationTag `0x0018` and only checks `ERR`.

Required:

1. Use the Config1 object with a 64-octet maximum (or the DUT's declared
   equivalent).
2. Save the current value as `temp1`.
3. Write 70 octets (maximum + overrun).
4. Assert `0x8033` (VAL_LENOVRRUN).
5. Read the object back as `temp2` and assert `temp1 == temp2`.

`0x0018` is ApplicationTag (max 32 octets) and belongs to `0123`, not `0063`.

### 3.3 `SDCI_TC_0157` — complex record consistency

Current: reads VendorName `0x0010` sub0/sub1.

Required:

1. Use a complex IODD parameter with `subindexAccessSupported = true`.
2. Write all fields through sub0.
3. Read the same fields through sub-n and compare all four values.
4. Verify that partial writes do not leave inconsistent records.

VendorName is a StringT, not the required complex record type.

### 3.4 `SDCI_TC_0069` — event in OPERATE

Current: `_event_check` injects one vendor-hook event and waits for it.

Required:

1. Precondition: EUT in OPERATE.
2. Use Config7 to inject Event A and Event B.
3. Read StatusCode `SCa`, EventQualifier `SCc`, EventCode `SCd`.
4. Assert EventQualifier = appears and EventCode values.
5. Read StatusCode `SCe`, acknowledge, and verify the event is cleared.

### 3.5 `SDCI_TC_0070` — event in PREOPERATE

Current: runs after `ensure_operate`, i.e. in OPERATE.

Required:

1. Put the fixture in PREOPERATE, not OPERATE.
2. Run the same StatusCode/qualifier/code sequence as `0069`.
3. Do not call `ensure_operate` for this case.

### 3.6 `SDCI_TC_0071` — event clear

Current: injects an event and reads it.

Required:

1. Inject Event A (appear), acknowledge with StatusCode `0x00`.
2. Wait 50 ms.
3. Inject Event A disappear, acknowledge with StatusCode `0xAA`.
4. Wait 1 s.
5. Inject Event A appear again.
6. Read/acknowledge and verify the Event flag is cleared at the end.

### 3.7 `SDCI_TC_0072` — communication interruption

Current: `_event_check` only; no reset, no pause.

Required:

1. Inject Event A and read StatusCode/EventQualifier/EventCode.
2. Reset the DTU and pause 2 s.
3. Wake the Device back to OPERATE.
4. Read and acknowledge events until the expected event is found.
5. Assert EventQualifier/EventCode match the saved values.

### 3.8 `SDCI_TC_0073` — power interruption

Current: `_event_check` only; no power-down.

Required:

1. Inject Event A and read StatusCode/qualifier/code.
2. Power down the Device for 15 s.
3. Reconnect and wake to OPERATE.
4. Read events.
5. Assert Event A is not reported with mode `disappear`; the first event after
   reconnection must be `appear`.

### 3.9 `SDCI_TC_0074` — appear/disappear

Current: only one injected event.

Required:

1. Inject Event A appear; read qualifier/code.
2. Write StatusCode `0xFF`.
3. Wait 50 ms.
4. Inject Event A disappear.
5. Read qualifier/code and assert `disappear`.
6. Acknowledge and verify both modes.

### 3.10 `SDCI_TC_0075` — overlapping events

Current: injects one event.

Required:

1. Inject Event A and Event B.
2. Overlap their appear/disappear sequences.
3. After each acknowledgement, read qualifier/code and assert ordering and
   values.
4. Verify both events can coexist.

### 3.11 `SDCI_TC_0076` — short event

Current: injects only an appear.

Required:

1. Inject Event A appear.
2. Inject Event A disappear before the acknowledgement.
3. Write StatusCode `0xFF`.
4. Wait 50 ms or two MasterCycleTimes, whichever is longer.
5. Write StatusCode `0xFF` again.
6. Assert the Device reports `disappear` after the acknowledgement and the Event
   flag is cleared correctly.

---

## 4. P1: shallow checks

These checks may keep their scenario but must assert the exact Test-Spec value.

| Case | Current weak assertion | Required assertion |
|---|---|---|
| `0054` | VendorName read/OK | I-Service, Length, CHKPDU, Content equals IODD VendorName |
| `0057` | OK/ERR | Negative read, `0x80 0x11` |
| `0058` | OK/ERR | Negative read, `0x80 0x12` |
| `0059` | OK/ERR | Positive response **or** `0x8011`, with correct I-Service/Length/CHKPDU |
| `0061` | OK/ERR | Negative read, `0x80 0x11` |
| `0066` | OK/ERR | Negative write, `0x80 0x23` |
| `0104` | A few command values | Enumerate all reserved SystemCommand ranges; assert `0x8035` |
| `0107` | Length only | Length == 12 + 3n; n in 0..70; header fields consistent |
| `0108` | `OK` or `ERR` | Sub1..5 lengths and values from Table 126 |
| `0109` | Read only | Write implemented bits, read back, write 0, read back |
| `0110` | Read only | Write each reserved bit, assert `0x8030` and zero read-back |
| `0111` | OK/ERR | Negative read, `0x8011` |
| `0112` | OK/ERR | Negative read, `0x8011` |
| `0113` | OK/ERR | Negative read, `0x8011` |
| `0114` | Read only | UTF-8, length <= 64, content matches registered VendorName |
| `0115` | Read only | UTF-8, length <= 64, vendor-specific content |
| `0116` | Read only | UTF-8, length <= 64, content |
| `0117` | Read only | UTF-8, length <= 64, content |
| `0118` | Read only | UTF-8, length <= 64, content |
| `0119` | Read only | UTF-8, length <= 16, content |
| `0120` | Read only | UTF-8, length <= 64, content |
| `0121` | Read only | UTF-8, length <= 64, content |
| `0123` | Read/write length check | Read before, write fixed_length+1, assert `0x8033`, read after, values unchanged |
| `0124` | Length == 2 | Stimulate error, counter increments by one, power cycle, counter resets to 0 |
| `0136` | `0x8011` only | Each profile index `0x0031..0x003F` returns `0x8011` |
| `0137` | `0x8011` only | Each profile index write returns `0x8011` |
| `0140` | `0x8023` only | Read length then write read-only diagnostic, assert `0x8023` |
| `0373` | DeviceStatus OK | DeviceStatus 1..4, DDS contains EventCode, disappear restores status and removes DDS entry |

Notes:

- `assert_ok` in `spec_checks.py` must not be used for negative ISDU cases.
- Every negative ISDU response must assert both `ErrorCode` and
  `AdditionalCode` (`0x80` + `0x11`, `0x12`, `0x23`, `0x30`, `0x33`, `0x35`).
- Every positive ISDU response must assert I-Service, Length/ExtLength, CHKPDU
  and the exact payload.

---

## 5. P2: n/a decisions to re-evaluate

### `SDCI_TC_0323`

Current: unconditionally skipped with "DUT DS is enabled; no-DS case is N/A".

Problem: 0323 is the DataStorageIndex **termination marker** case. It is
applicable to a DS-enabled device and requires:

- Sub3 value 0
- Sub5 length 2 and value 0

Remove the skip and implement the marker check.

### `SDCI_TC_0155` / `0316` / `0317` / `0318`

Current: marked n/a and never called. The check bodies only send the
SystemCommand and wait for OPERATE; they do not assert the Test-Spec evaluation.

Required before keeping n/a:

1. Confirm the DUT build configuration actually excludes the feature.
2. Provide that evidence in the report.
3. If the feature is compiled in, implement the full evaluation:
   - `0155`: events, 5 s wait, startup, DS flag, DID/RID defaults, status,
     parameter defaults
   - `0316`: exactly one startup, RID, DID unchanged, event/status, parameter
     set unchanged
   - `0317`: no startup, IDs unchanged, tags unchanged, `DS_UPLOAD_REQ` and flag,
     params positive
   - `0318`: communication interruption, exactly one startup, defaults, DS flag,
     event/status, parameter defaults

---

## 6. IODD unavailable

IODD-dependent checks must not use invented expected values. Mark them
`blocked-oracle` and keep the scenario code ready for later.

Blocked items:

- `TC_Device_Identification`
- `0092` RevisionID
- `0093` PDIn descriptor
- `0094` PDout descriptor
- `0141` write too short
- `0142` write too long
- `0149` identity parameters
- `0150` communication profile
- `0151` read verification
- `0152` write verification
- `0157` complex record consistency

When an IODD becomes available, add a single oracle loader and use it for all
eleven checks. Do not copy IODD values into the check source.

---

## 7. General implementation rules for the HIL project

1. One function per Test Spec case. Do not implement six event cases with one
   helper.
2. Assert the exact response:
   - I-Service
   - Length / ExtLength
   - CHKPDU
   - ErrorCode + AdditionalCode
   - payload length and value
3. Assert the state before and after the action:
   - STARTUP / PREOPERATE / OPERATE
4. Implement the specified interruption:
   - communication reset + 2 s
   - power-down 15 s
   - 50 ms / two-cycle short-event window
5. For DS and block checks, verify:
   - state transitions
   - `DS_UPLOAD_FLAG`
   - checksum changes
   - parameter-set atomicity
6. Do not use `OK` or `ERR` as the only assertion.
7. Do not let a category-level skip hide an applicable check.

---

## 8. Acceptance criteria

A check may be reported as conformance evidence only when:

- Strictness is `exact`.
- The report records the Test Spec ID and the asserted values.
- The precondition is the one required by the Test Spec.
- Negative cases assert the exact ErrorType.
- IODD-dependent values come from the IODD oracle, or the case is marked
  `blocked-oracle`.

After correction, the report should distinguish:

```text
exact pass
partial / shallow / wrong-scenario (not conformance evidence)
n/a with evidence
blocked-oracle
```

The current run's 62 passes must not be reused as a conformance result.
