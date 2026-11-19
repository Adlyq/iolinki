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

# Grouped gates: pick the groups a change can break (see docs/TESTING.md).
./tools/gates.sh quick                  # seconds: format, size, shellcheck, ownership
./tools/gates.sh quick build test gcc   # strict -Werror builds + ctest + gcc extras
./tools/gates.sh all                    # everything; CI runs the same groups in jobs

# Compile matrix: every feature toggle, ISR mode and platform. Required for any
# change touching a compile-time switch or an #if in src/ and include/.
./tools/build_matrix.sh

# Coverage of src/ (gcovr; local only, --html writes the report).
./tools/coverage.sh
```

clang is the primary compiler — it is the family the firmware is built with and
the only one that checks `IOLINK_NONNULL`/`IOLINK_NULLABLE`; `gcc` is the
supplement for its own diagnostics and `-fanalyzer`. `IOLINKI_CC` overrides the
compiler for the gates, the matrix, coverage and WCET.

## Code navigation

Two MCP servers index this repository; look symbols up there before reading
whole files. Neither replaces reading the code you are about to change.

`serena` (clangd, project `iolinki`) answers precise symbol questions and needs
a current `build/compile_commands.json`:

- `get_symbols_overview(relative_path="src/dll_sm.c")`
- `find_referencing_symbols(name_path="dll_sm_classify", relative_path="src/dll_sm.c")`
- also `find_declaration`, `find_implementations`, `safe_delete_symbol`, `rename_symbol`
- line numbers are 0-based; symbols inside disabled `#if` branches are invisible

`codebase-memory` (project `iolinki`) answers whole-repository questions:

- `search_graph(project="iolinki", query="dll_sm_classify")` → definition site
- `get_code_snippet(qualified_name="iolinki.src.dll_sm.dll_sm_classify")`,
  `get_file_outline(project="iolinki", file_path="src/dll_sm.c")`
- `trace_path(project="iolinki", function_name="<qn>", direction="inbound")`
- `detect_changes(project="iolinki", scope="impact")` for a change's blast radius
- `get_architecture(project="iolinki", aspects=["overview"])`

Subagents do not inherit either server; they see the usual shell/read/grep plus
the `codebase-memory` CLI, which reaches the same graph:
`codebase-memory-mcp cli search_graph --project iolinki --query dll_sm_classify`,
`codebase-memory-mcp cli trace_path --project iolinki --function_name <qn>`.
`trace_path` and `get_code_snippet` need the qualified name. The graph is
best-effort (`parse_partial` covers `include/iolinki/application.h` and some
`examples/`) and is refreshed with
`index_repository(repo_path="/home/adlyq/CLionProjects/iolinki", name="iolinki")`
(about 3 s); confirm a symbol before citing it.

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
  by `tools/gates.sh quick`.
- Conventional Commits (`feat(dll): ...`). A `[Unreleased]` entry in
  `CHANGELOG.md` is required only when behaviour, a public interface or a build
  option changes; internal refactors, tests and tooling do not need one.
- Topic branches `fix/*`/`feature/*` cut from `own_dev`; PRs target `own_dev`.
  `no-commit-to-branch` blocks `main` and `develop`.

## Maintenance model

- `origin` (`Adlyq/iolinki`) is authoritative; **`own_dev` is the mainline**.
  `develop` is a historical snapshot and `main` does not exist.
- `upstream` (`w1ne/iolinki`) is read-only: **never merge or rebase onto
  `upstream/develop`**. Adopt changes by re-implementing them here with a
  regression test. See `docs/MAINTENANCE.md`.
