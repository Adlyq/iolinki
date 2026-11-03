# AGENTS.md

`iolinki` is an IO-Link **Device** stack (spec V1.1.5, Package 2025), C99,
hardware-agnostic, dual-licensed (GPLv3 or commercial). The companion **master**
stack is `iolinki-master`. The bundled V1.1.5 PDF under
`docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/` is the source of truth: when code,
comments or docs disagree with it, the PDF wins.

## Commands

```bash
# Build + unit tests
cmake -B build && cmake --build build && (cd build && ctest --output-on-failure)

# One test
ctest --test-dir build -R test_dll

# Strict -Werror builds (Debug + Release), cppcheck, clang-format, shellcheck,
# Doxygen; MISRA is report-only unless IOLINKI_MISRA_ENFORCE=1.
./check_quality.sh

# Compile matrix: every feature toggle, ISR mode and platform. Required for any
# change touching a compile-time switch or an #if in src/ and include/.
./tools/build_matrix.sh

# Coverage of src/ (gcovr; local only, --html writes the report).
./tools/coverage.sh
```

## Architecture

- Layers: Application (`include/iolinki/application.h`) -> DLL
  (`src/dll.c`, `src/dll_isr.c`) -> PHY vtable (`include/iolinki/phy.h`); the
  ISR/main-context boundary is declared in `src/dll_internal.h`.
- ISR two-pump transport: the data ISR frames requests and transmits staged
  replies (`iolink_device_process_isr()`), while the main loop runs the protocol
  logic. Main-context replies are staged and signalled through the optional
  `phy->request_service()` hook.
- No dynamic memory; context-based (no globals); `<stdint.h>` types; negative-int
  error codes; MISRA C:2012 subset.
- Compile-time trimming lives in `include/iolinki/config.h`
  (`IOLINK_FEATURE_*`, `IOLINK_PHY_FEATURE_*`, `IOLINK_MINIMAL_PROFILE`,
  `IOLINK_TINY_PROFILE`, `IOLINK_ISR_RX_MODE`/`IOLINK_ISR_TX_MODE`,
  `IOLINK_COMPAT_*`). `IOLINK_PLATFORM` selects `LINUX` (default) or `BAREMETAL`.

## Spec constraints

- Table B.8: `0x0004-0x000B` and `0x001C-0x001F` are reserved indices.
- VendorID/DeviceID are Direct Parameter page 1 fields (`0x07-0x0B`), not ISDU
  indices.
- SystemCommand `Param*` codes are Table B.9 `0x01-0x06`; `0x84` is reserved.

## Conventions

- Public symbols `iolink_` prefix, `snake_case`, `_t` suffix, `UPPER_SNAKE`
  macros; Doxygen on public API in `include/`.
- `clang-format` pinned to 23.1.1 (Google-based, 4 spaces, 100 cols), enforced
  by `check_quality.sh`.
- Conventional Commits (`feat(dll): ...`); add `[Unreleased]` to `CHANGELOG.md`.
- Topic branches `fix/*`/`feature/*` cut from `own_dev`; PRs target `own_dev`.
  `no-commit-to-branch` blocks `main` and `develop`.

## Maintenance model

- `origin` (`Adlyq/iolinki`) is authoritative; **`own_dev` is the mainline**.
  `develop` is a historical snapshot and `main` does not exist.
- `upstream` (`w1ne/iolinki`) is read-only: **never merge or rebase onto
  `upstream/develop`**. Adopt changes by re-implementing them here with a
  regression test. See `docs/MAINTENANCE.md` and `docs/UPSTREAM_DIVERGENCE.md`.
