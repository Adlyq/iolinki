# AGENTS.md

`iolinki` is an IO-Link **Device** stack (spec V1.1.5), C99, hardware-agnostic,
dual-licensed (GPLv3 or commercial). The companion **master** stack lives in
`iolinki-master`. The bundled V1.1.5 PDF under
`docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/` is the source of truth: when code,
comments or docs disagree with it, the PDF wins.

## Commands

```bash
# Default build (ISR two-pump transport) + unit tests
cmake -B build && cmake --build build && (cd build && ctest --output-on-failure)

# One test
ctest -R test_dll

# Quality gate (strict -Werror build, cppcheck, clang-format, MISRA, Doxygen)
./check_quality.sh
```

## Build / platform gotchas

- Platform switch is **`IOLINK_PLATFORM`** (`LINUX` default, `BAREMETAL`).
- Tests are **silently skipped** when CMocka is absent (`tests/CMakeLists.txt`);
  install `libcmocka-dev`.
- `tests/test_spec_conformance.c` pins the mandatory ISDU conformance behaviour
  (reserved indices, ErrorCount, ProductURI, ProcessDataInput/Output, read-only
  WRITE rejection, Parameter lock, string fixedLength).
- The stack uses a single ISR two-pump transport: the data interrupt frames
  requests and transmits replies (`src/dll.c`, `iolink_device_process_isr()`),
  while the main loop runs the protocol logic. `src/frame.c` and the upstream
  streaming transport (`phy->send`/`phy->recv_byte`) were removed.

## Architecture

- Layered: Application (`include/iolinki/application.h`) -> DLL (`dll.c`) ->
  PHY vtable (`include/iolinki/phy.h`).
- **Transport** (`IOLINK_PHY_FEATURE_ISR_PUMP` removed; always on): the ISR
  frames requests and transmits pre-staged cyclic replies (`src/dll.c`,
  `iolink_device_process_isr()`), for low-latency t_A replies on single-octet
  transceivers. Main-context replies are staged and reported through the
  optional `phy->request_service()` hook (the TX hooks then run only in the
  ISR); `IOLINK_PHY_FEATURE_ISR_TX_ONLY` removes the direct-send fallback. ISDU
  reads are served by the ISR from a framed-response snapshot (`isr_isdu_*`)
  that the main pump refreshes when the service completes.
- **No dynamic memory**, context-based (no globals), `<stdint.h>` types,
  negative-int error codes. MISRA C:2012 subset.
- Compile-time trimming in `include/iolinki/config.h`
  (`IOLINK_FEATURE_*`, `IOLINK_PHY_FEATURE_*`, `IOLINK_MINIMAL_PROFILE`).

## Spec-precision notes

- Table B.8: `0x0004-0x000B` and `0x001C-0x001F` are reserved. VendorID/DeviceID
  live in DPP1 (`0x07-0x0B`); FunctionID is a ProfileCharacteristic subfield;
  RevisionID is DPP1 `0x04`.
- `IOLINK_IDX_VENDOR_ID`/`DEVICE_ID` are legacy non-standard constants pending
  removal.
- SystemCommand `Param*` codes are Table B.9 `0x01-0x06`; `0x84` is reserved.
  These were corrected locally even though upstream still uses older values.

## Conventions

- Public symbols `iolink_` prefix, `snake_case`, `_t` suffix, `UPPER_SNAKE`
  macros; Doxygen on public API in `include/`.
- `clang-format` (Google-based, 4 spaces, 100 cols). Note: the installed
  clang-format may differ from the version pinned in `.pre-commit-config.yaml`.
- Conventional Commits (`feat(dll): ...`); add `[Unreleased]` to `CHANGELOG.md`.
- `no-commit-to-branch` blocks `main`/`develop`; work on a topic branch.

## Multi-repo workflow

- `origin` is the personal fork, `upstream` is `w1ne/iolinki`. This branch is
  converged onto `upstream/develop` so that upstream changes merge cleanly: new
  work should be additive (prefer new files over rewriting upstream files).
