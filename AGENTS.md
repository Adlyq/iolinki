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

## Code constraints

- No dynamic memory; context-based (no globals); `<stdint.h>` types; negative-int
  error codes; MISRA C:2012 subset.
- **No fallback paths for required inputs.** A missing required input, an error
  return or an unexpected state ends in an explicit failure (negative code,
  `IOLINK_DEVICE_INIT_ERR_*`, ISDU error response, `false`/`NULL` for a
  documented borrowed view). Never substitute a default value, a second
  implementation or a silently degraded behaviour ("if it is missing, use X
  instead"). Absence that means "unsupported" or "not needed" is a legitimate
  outcome, but it must be declared at the interface (nullable pointer, optional
  hook, or a spec-defined `0` = "none"); it is not a fallback.
- Pointer contracts are explicit: `IOLINK_NONNULL`/`IOLINK_NULLABLE` for
  required/optional pointers, a named opaque type instead of `void *`, and
  `IOLINK_OPAQUE` (with a reason) only where the interface cannot name a type.

## Spec constraints

- Table B.8: `0x0004-0x000B` and `0x001C-0x001F` are reserved indices.
- VendorID/DeviceID are Direct Parameter page 1 fields (`0x07-0x0B`), not ISDU
  indices.
- SystemCommand `Param*` codes are Table B.9 `0x01-0x06`; `0x84` is reserved.

## Compatibility policy

- **No backward compatibility.** There is no deprecation window, no alias, no
  shim and no "keep it for one version" rule. Replace the old interface,
  document, macro or data format with the new one in the same change and delete
  the old one; do not leave a second path behind.
- Never add a second entry point that duplicates an existing one. Extend the
  single implementation instead (see the OD vs `vendor_service` removal).
- A removal covers everything: public header, build option, internal wiring,
  tests, examples, tools, data files and documentation.
- Documentation describes the current model only. Do not keep historical
  design/plan documents, compatibility notes or migration shims in `docs/`; git
  history is the archive and `CHANGELOG.md` is the only place that records a
  removal.
- Version rule: any public-interface change bumps `MAJOR`; `MINOR` adds
  functionality without changing existing declarations; `PATCH` fixes behaviour.

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
