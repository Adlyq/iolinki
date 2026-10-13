# STM32G070 + L6364 reference material and this fork's local modifications

This fork (`Adlyq/iolinki`) carries a set of local changes needed to run the
iolinki device stack on an **STM32G070CBTx with an ST L6364** single-octet
transceiver (COM3, 28 PD-in / 28 PD-out), driven from a Keil MDK firmware
project. This directory holds the material behind those changes and the
rationale, so the delta against `w1ne/iolinki@develop` is reviewable in one place.

> The delta touches `src/dll.c`, `src/device.c`, `src/isdu.c`, `src/crc.c`,
> `include/iolinki/{phy,dll,config,device,isdu,protocol,iolink}.h`, this fork's
> own `tools/virtual_master/` and `examples/reference_device/` (which had shipped
> the non-conformant reply order), and this document.
>
> Everything device-specific (M-sequence type, asymmetric OD widths, baudrate) is
> **not** in the library: it lives in the consuming application. See section 6.

## Contents

| File | What it is |
|---|---|
| [`ST_ministack_singlebyte_RE.md`](ST_ministack_singlebyte_RE.md) | Reverse-engineered facts of ST's own IO-Link device "ministack" in single-octet mode: frame layout, CKT type bits, checksum, timing. Captured with a logic analyser against an ST reference device. |
| `iol_device_ministack_stm32g0_wc16_ot.a` | ST's precompiled device ministack for STM32G0 (ARMCC, little-endian). **Reference only** - this repository does not build or link it. All rights remain with STMicroelectronics. |
| IO-Link Interface & System Specification V1.1.3 | Bundled in [`../../IOL-Interface-Spec_10002_V1.1.3_Jun2019/`](../../IOL-Interface-Spec_10002_V1.1.3_Jun2019/). This is the revision the changes below were checked against. |

## 1. Specification-conformance fixes

### 1.1 Device-to-master reply layout: OD first (`src/dll.c`)

The specification draws the device reply as `[OD][PD-In]CKS` - the on-request
data octet(s) come **first**, then process data in, then the CKS octet
(Figures A.9 / A.10 / A.13 / A.14). iolinki emitted `[PD-In][OD]CKS`.

Independent device stacks agree with the specification: IOLinky's
`iol_mtype_2_2_t` is `{ MC, CKT, OD, PD, CKS }`, and lwIOLink's `SetResponse()`
also writes the OD octet at offset 0. Fixed: `dll_handle_operate_type1_2()` and
`iolink_dll_prepare_cyclic_reply()` now emit `[OD...][PD-In...]CKS`.

Note that the reference master polls the cyclic OD channel with FlowCTRL IDLE and
does its ISDU work acyclically (Type-0), so the cyclic OD octet carries no ISDU
data (`0x00`) - the byte is still present as the specification requires.

This fork's own virtual master and reference example had the same defect and were
corrected in the same change: `tools/virtual_master/virtual_master/protocol.py`
(`DeviceResponse` now slices `body[:od_len]` as the OD octets, `body[od_len:]` as
PD-In), the `A.1.5` comments in `master.py` / the tests, and
`examples/reference_device/host_main.c` (expects `[OD 2][PD-In 3]CKS`).

### 1.2 ISDU device-specific index space (`src/isdu.c`, `src/device.c`)

Figure B.6 / Table B.8 define two device-specific ISDU index ranges:
`0x0040`-`0x00FE` ("Preferred Index", device specific) and `0x0100`-`0x3FFF`
("Extended Index", device specific). iolinki routed the application's
`vendor_service` callback only for `index >= 0x0100`, so device-specific indices
below `0x0100` were unreachable.

Fixed: both ranges are routed to `vendor_service`. An unsupported vendor index
now answers `0x11` (Table C.1 `IDX_NOTAVAIL`) instead of `0x23` (`AccessDenied`).

> The `>= 0x0100` half is exercised by the reference master. The
> `0x0040`-`0x00FE` half has **not** been verified on the wire yet (no master-side
> ISDU access to such an index has been issued). Annotated in `src/isdu.c`.

### 1.3 Known deviations left annotate-only

- **D3 - SystemCommand coding** (`protocol.h`, `isdu.c`). The
  `IOLINK_CMD_PARAM_*` values (`0x05`-`0x09`) do not match Table B.9 (where
  `0x03` = ParamDownloadStart ... `0x05` = ParamDownloadStore, `0x06` =
  ParamBreak), and the `0x83` / `0x84` / `0x95`-`0x97` "legacy" codes reuse
  reserved ranges. Left as-is: this port does not use Data Storage
  (`ds_storage == NULL`) and its master sends no SystemCommand. Annotated in
  place for a future conformant fix.
- **D4 - MasterCommands** (`dll.c`). Table B.2 defines `0x5A` (FALLBACK),
  `0x97` (STARTUP), `0x98` (ProcessDataOutputOperate), `0x99` (DeviceOperate) and
  `0x9A` (PREOPERATE); only `0x5A` and `0x99` are implemented. Left as-is: the
  reference master only sends `0x99`. Annotated in place.

## 2. Layering / portability changes (`phy.h`, `dll.c`, `dll.h`)

Goal: keep the library free of MCU / transceiver detail so it can be moved to
another PHY or MCU unchanged.

- **`recv_frame()` (new optional PHY hook).** A single-octet transceiver delimits
  the M-sequence in hardware (octet count + CKT type, or a frame-end time-out),
  so the stack can receive a **complete frame** instead of reassembling bytes with
  a `t_byte` timer. `dll_drain_rx()` and `iolink_dll_process()` use it; the
  byte-wise `recv_byte()` path remains as a fallback.
- **`stage_reply()` (new optional PHY hook).** When the cyclic reply is
  deterministic (TYPE_2_V) the DLL builds the whole reply image and hands it to
  the PHY, which transmits it verbatim at master-frame end from its own
  interrupt. The PHY no longer needs to know the reply layout or compute the
  checksum. The image is **double buffered** in `iolink_dll_ctx_t`
  (`cyclic_stage[2]` + `cyclic_stage_active`) so the DLL may rebuild it while the
  PHY still holds the previous one; the library still needs no critical section,
  because the DLL is main-context only.
- **`set_cyclic_frame_len()` (new optional PHY hook).** The DLL is the single
  source for the cyclic M-sequence length (`MC + CKT + PD-out + OD`) and pushes it
  to the PHY, which needs it only to delimit the incoming frame. Removes a second
  derivation of the same length.
- **`detect_wakeup` is a capability flag.** When it is NULL the PHY owns the
  SIO / wake-up handshake, so `dll_recover_to_idle()` keeps the DLL in SDCI
  (switching to SIO would latch, because a NULL PHY never reports a wake-up).
- **Removed the `__disable_irq()` fallback** in `iolink_device_pd_input_update()`
  and `iolink_device_pd_output_read()`. The DLL never runs in an ISR, so the
  per-iteration fallback critical section only blocked the transceiver interrupt.
  An application-provided lock hook is still honoured when supplied.
- **PHY diagnostics report *physical* faults; the EventCodes are device data.**
  `iolink_phy_api_t.get_faults()` returns a bitmask of `IOLINK_PHY_FAULT_*`
  (under-voltage / over-temperature / short-circuit) and the DLL raises events
  through `iolink_dll_fault_map_t` (EventCode + EventType), installed per device
  with `iolink_dll_set_fault_map()`. Previously `dll_poll_diagnostics()`
  hard-coded 0x1805/0x1806, so a device with its own diagnostics (or an IODD with
  vendor-specific codes) could not be expressed. Two defects in the same path are
  fixed: a *negative* `get_voltage_mv()` means "measurement unavailable" and is no
  longer a fault (`phy_generic`'s template returns -1, so every generic-PHY device
  raised a voltage event on every cycle), and a fault that persists now raises its
  event once instead of on every cycle - the Event flag already tells the master
  to re-read. The legacy `get_voltage_mv()`/`is_short_circuit()` hooks still work
  when `get_faults()` is not provided.
- **Events support appears *and* disappears (A.6.4).** `iolink_event_mode_t`
  (`IOLINK_EVENT_MODE_APPEARS` / `_DISAPPEARS` / `_SINGLE_SHOT`) is carried in the
  `iolink_event_t` and encoded in the EventQualifier MODE field, so the DLL can
  announce a fault and later withdraw it: an "appears" is issued on the 0->1 edge,
  the matching "disappears" on the 1->0 edge (never within
  `IOLINK_EVENT_MIN_APPEAR_MS`, 50 ms, of the appearance - a shorter glitch keeps
  the fault announced until the window elapses, as the dispatcher rules require).
  A disappearance removes the entry of the same EventCode from the event memory
  instead of adding one, per "Upon occurrence of an Event with MODE Event
  disappears, the corresponding entry shall be removed".
- **EventQualifier INSTANCE field corrected.** It used `0x02`, a *reserved*
  INSTANCE value (Table A.17); it is now `0x04` = "Application". For an
  error-class event this yields `0xF4`, exactly the value ST's ministack emits for
  `StackEventsQualifier(APPLICATION, DEVICE, EVENT_ERROR, APPEARS)`. The
  StatusCode octet is now `0x00` (instead of `0x80`) when no event is active, so a
  disappearance leaves a clean "no events" image.

## 3. Runtime behaviour changes (`dll.c`, `device.c`, `include/iolinki/*.h`)

- **OPERATE supervision** (`IOLINK_OPERATE_TIMEOUT_MS`, default 200 ms). If no
  checksum/type-valid frame arrives while in OPERATE, the master is treated as
  gone: the device returns to STARTUP / SIO and refreshes the PHY, so a master
  restart (WURQ) is handled cleanly instead of leaving the device stuck showing
  "connected".
- **`t_byte` delimits only the byte-wise fallback.** `dll_get_t_byte_limit_us()`
  returns 0 when the bound PHY exposes `recv_frame()`, because such a PHY resolves
  the M-sequence boundary itself (octet count + CKT type, or the transceiver's
  frame-end time-out). This matters because the polled main-loop DLL is slower
  than 13 * Tbit: applying the inter-octet timer to a frame the transceiver has
  already delimited would reset it mid-assembly. A PHY without `recv_frame()`
  keeps the upstream byte-wise delimiting and error semantics unchanged.
- **Operating baudrate.** `iolink_config_t.baudrate` (new field) is honoured and
  the recovery paths return to the configured baudrate instead of hard-coding
  COM1.
- **Direct Parameter page 2 write notification.**
  `iolink_device_config_t.on_direct_param_write` and
  `iolink_isdu_ctx_t.direct_param_write_cb` (new) fire after a page-2 write is
  committed, so an application can persist the change.
- **TYPE_2_V OPERATE capability code is derived from the OD width.** Table A.10
  codes the variable M-sequence types by their On-request Data width, so
  `direct_param_mseq_capability()` reports `4` for a one-OD channel and `5` for a
  two-OD channel instead of hard-coding `5`. The generic derivation for TYPE_2_V
  is the symmetric two-octet case (code 5); a one-octet device overrides the
  widths after `iolink_device_init()` (section 6).
- `src/crc.c` factors the 8-to-6-bit compression into a file-local
  `iolink_checksum6_from_acc()` used by `iolink_checksum6()`.

## 4. Verification

**Host test suite.** `cmake -S . -B build -DPLATFORM=LINUX -DCMAKE_BUILD_TYPE=Debug`
followed by `ctest`: **36/36 pass** (cmocka unit tests, `reference_device_demo`,
`switching_sensor_demo`). The virtual-master integration tests that do not assume
the private `build/` layout also pass: `test_type1`, `test_pd_variable`,
`test_conformance_state_machine`, `test_conformance_isdu`, `test_sio_mode`,
`test_automated_mandatory`. `test_m_sequence_types` fails identically on
unmodified `develop`, so that failure is pre-existing and unrelated.

**Hardware.** Every specification-conformance and layering step was flashed to the
STM32G070 + L6364 target (`openocd ... program build/Release/... verify`) and then
observed over SEGGER RTT for ~12 s, requiring `STARTUP -> PREOP -> ESTAB ->
OPERATE`, a **constant** checksum-error counter in OPERATE (i.e. frames delimited
correctly), `timeout = 0`, a reply length matching the configured PD/OD widths
(30 octets here) and no "master lost" event. The cyclic reply bytes were
cross-checked against an ST reference device capture (see the RE document).

> The last revision of this library - which additionally carries the byte-wise
> `recv_byte()` fallback and the capability-gated `t_byte`, both inert for a
> `recv_frame()` PHY - was built and flashed, but the RTT observation above was
> not repeated for that exact revision.

## 5. Divergence from this repository's own documentation

`docs/FRAMES.md` and
`docs/superpowers/specs/2026-09-18-spec-conformant-wire-design.md` describe the
device reply as `[PD-In][OD]CKS`. The specification figures cited in section 1.1
show the opposite order, and this fork follows the specification. Those documents
were not rewritten here.

## 6. What is intentionally left to the application

The library stays device-agnostic. The following belong to the consuming
application (the STM32G070 + L6364 firmware project), not to this repository:

- **M-sequence type and PD widths**: `IOLINK_M_SEQ_TYPE_2_V` with 28 PD-In and 28
  PD-Out octets.
- **Asymmetric OD widths.** The generic derivation (unchanged from upstream) is
  `od_len = (m_seq_type == TYPE_2_V) ? 2 : 1`, with `od_resp_len` defaulting to
  `od_len`. The L6364 device runs the *one*-octet TYPE_2_V variant (OPERATE code
  4): the master's cyclic message carries **no** OD octet while the reply still
  leads with one. After `iolink_device_init()` the application therefore sets
  `dll.od_len = 0`, `dll.od_resp_len = 1` and re-publishes the derived cyclic
  frame length via `iolink_dll_apply_cyclic_frame_len()`. Because
  `od_resp_len` is what the reply builder and the ISDU capability page read, the
  override also yields capability code `4` rather than `5`.
- **Operating baudrate**: COM3.
- **Diagnostic events**: the transceiver's physical faults
  (`L6364 STATUS:UV` / `:SOT` / `:SSC`, exposed through `get_faults()`) are mapped
  to the codes and severities ST's ministack application used -
  `0x1800` under-voltage (Error), `0x1801` over-temperature (Warning), `0x7710`
  short circuit (Error). The two 0x18xx codes are ST's vendor-specific IODD codes,
  chosen so this device is indistinguishable from the ST reference device under
  the same master; a device with its own IODD replaces them by changing the
  `st_fault_map` it passes to `iolink_dll_set_fault_map()`.
- **The PHY**: the `recv_frame()`, `stage_reply()` and `set_cyclic_frame_len()`
  implementations, plus `detect_wakeup == NULL` (the L6364 owns the SIO / wake-up
  handshake), per the contracts in section 2.
