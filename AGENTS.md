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
ctest --test-dir build -R test_dll

# Quality script: strict -Werror build at Debug (-O0) and Release (-O3), plus
# cppcheck. MISRA, clang-format and Doxygen run only when installed, and
# MISRA/format only fail the script with IOLINKI_MISRA_ENFORCE=1 /
# IOLINKI_FORMAT_ENFORCE=1.
./check_quality.sh

# Compile matrix (delivery gate): every feature toggle, ISR granularity and
# platform must build; BYTE/BYTE configs also run the suite. Required for any
# change touching IOLINK_* switches or an #if in src/ and include/.
./tools/build_matrix.sh
```

## Build / platform gotchas

- Platform switch is **`IOLINK_PLATFORM`** (`LINUX` default, `BAREMETAL`).
- Tests are skipped with a CMake **warning** when CMocka is absent
  (`tests/CMakeLists.txt`); install `libcmocka-dev`. Feature-trimmed builds skip
  the whole suite (`tests/CMakeLists.txt:1-10`), so use `-DBUILD_TESTING=OFF`
  for those profiles.
- The default CMake build sets no `CMAKE_BUILD_TYPE`, so it compiles at the
  toolchain default (effectively `-O0` with GCC) and `static` helpers are not
  inlined: do not assume zero-cost abstraction when judging ISR code size or
  timing. `check_quality.sh` also uses `Debug`; only `Release` / documented
  embedded flags (`-Os`, see `examples/stm32g0_tiol112/build.sh`) optimise.
- `tests/test_spec_conformance.c` pins the mandatory ISDU conformance behaviour
  (reserved indices, ErrorCount, ProductURI, ProcessDataInput/Output, read-only
  WRITE rejection, Parameter lock, string fixedLength).
- The stack uses a single ISR two-pump transport: the data interrupt frames
  requests and transmits replies (`src/dll.c`, `iolink_device_process_isr()`),
  while the main loop runs the protocol logic. The previous streaming transport
  (`src/frame.c`, `phy->send`/`phy->recv_byte`) was removed.

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
  (`IOLINK_FEATURE_*`, `IOLINK_PHY_FEATURE_*`, `IOLINK_MINIMAL_PROFILE`,
  `IOLINK_TINY_PROFILE`, `IOLINK_ISR_RX_MODE`/`IOLINK_ISR_TX_MODE`,
  `IOLINK_COMPAT_*`, `IOLINK_DS_LEGACY_IMAGE_SUB0`, `IOLINK_PHY_OWNS_LINE_STATE`).

## Spec-precision notes

- Table B.8: `0x0004-0x000B` and `0x001C-0x001F` are reserved. VendorID/DeviceID
  live in DPP1 (`0x07-0x0B`); FunctionID is a ProfileCharacteristic subfield;
  RevisionID is DPP1 `0x04`.
- VendorID/DeviceID live only in DPP1; no `IOLINK_IDX_VENDOR_ID`/`DEVICE_ID`
  constants are exposed.
- SystemCommand `Param*` codes are Table B.9 `0x01-0x06`; `0x84` is reserved.
  These values are mandated by Table B.9 and are the local source of truth.

## Conventions

- Public symbols `iolink_` prefix, `snake_case`, `_t` suffix, `UPPER_SNAKE`
  macros; Doxygen on public API in `include/`.
- `clang-format` (Google-based, 4 spaces, 100 cols). Note: the installed
  clang-format may differ from the version pinned in `.pre-commit-config.yaml`.
- Conventional Commits (`feat(dll): ...`); add `[Unreleased]` to `CHANGELOG.md`.
- `no-commit-to-branch` blocks `main`/`develop`. Work on a `fix/*`/`feature/*`
  topic branch and open the PR against `own_dev`.

## Maintenance model

- `origin` (`Adlyq/iolinki`) is the authoritative remote. **`own_dev` is the
  maintained mainline**; `develop` is kept only as a historical snapshot and
  `main` does not exist.
- `upstream` (`w1ne/iolinki`) is a read-only reference. This branch is
  maintained independently: **do not merge or rebase onto `upstream/develop`**,
  and do not optimise new work for clean merges with upstream (editing existing
  files is fine).
- Adopt upstream changes selectively: review the commit, re-implement the
  behaviour against this codebase, and add a regression test. A single-purpose
  fix may be sent upstream as a PR. See `docs/MAINTENANCE.md` and
  `docs/UPSTREAM_DIVERGENCE.md`.
