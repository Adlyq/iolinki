# Testing & Quality Assurance

This document describes the testing strategy, quality standards, and CI/CD workflow for the `iolinki` project.

## 🏆 Philosophy
We adhere to **Strict Coding Standards** to ensure safety, portability, and reliability in embedded environments.
- **Zero Warnings**: Code must compile with
  `-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow`.
- **Static Analysis**: `cppcheck` must pass; the MISRA C:2012 addon is
  report-only unless `IOLINKI_MISRA_ENFORCE=1`.
- **Portable C**: C99, context-based API, no dynamic memory, no global state.
- **Automated Verification**: every push and PR runs the quality gate, unit
  tests, virtual-master conformance and the compile matrix.

## 🛠️ Local Testing

### 1. Run All Tests
Runs the Linux build, the virtual-master tests and the bare-metal library build.
```bash
./test_all.sh
```

### 2. Quality gates

`tools/gates.sh` is the single gate entry point. It runs only the groups you
name, prints each group's wall time, and stops at the first failure
(`--keep-going` runs the rest and summarises):

| Group | Checks |
| --- | --- |
| `quick` | clang-format, per-file size (800 code lines, comments excluded), shellcheck, DLL ownership isolation, nullability regions (`check_nullability.sh`), test conventions (`check_test_whitebox.sh`, `check_test_gates.sh`), documentation references (`check_docs_refs.py`), dead fields (`check_dead_fields.py`) and the public-API surface (`check_public_api.py`); the three tree-wide checks run concurrently so the group stays at a few seconds |
| `build` | strict `-Werror` compilation with clang, Debug (`-O0`) and Release (`-O3`) |
| `context` | field ownership between the ISR and main contexts: the call graph of `src/` built from the clang AST and classified against `tools/shared_fields.csv` (`check_context.py`, ~3 s) |
| `test` | strict Debug build plus `ctest` |
| `gcc` | the gcc-only strict diagnostics (`-DIOLINK_STRICT_GCC=ON`, library target) and `gcc -fanalyzer` as a report-only step |
| `sanitize` | the unit suite under AddressSanitizer, UndefinedBehaviorSanitizer and `implicit-conversion` (clang only; a sanitizer report fails the run) |
| `memcheck` | the unit binaries under valgrind memcheck (uninitialised reads, invalid accesses); the PTY conformance examples are covered by `test` |
| `analyzer` | the clang static analyzer over `src/` (`clang --analyze`); a finding fails the group |
| `static` | cppcheck and the MISRA addon (report-only unless `IOLINKI_MISRA_ENFORCE=1`) |
| `docs` | the generated documentation blocks (`gen_docs.py --check`: error codes, stats fields, public API, field ownership) and Doxygen warnings |
| `matrix` | `tools/build_matrix.sh` (clang) and `tools/diff_matrix.sh` |
| `perf` | `tools/coverage.sh --check` (clang), `tools/wcet_run.sh --check`, `tools/size_matrix.py --check --selftest` (ATFE; the ROM/RAM matrix is built with `-DIOLINK_LOG_COMPILE_LEVEL=0`, so the budget covers protocol code, not diagnostics) and `tools/stack_report.py --selftest --check` against `tools/stack_baseline.json` (target stack frames, Arm gcc or ATFE) |
| `package` | the exported package is installed into a scratch prefix and a two-file consumer is built against it |
| `fuzz` | libFuzzer over the three parsers that take untrusted bytes (received octets, ISDU payloads, OD indices/values); 60 s per target (`FUZZ_SECONDS` overrides), a crash, timeout or leak fails the group |
| `all` | every group above |

Groups compose (`tools/gates.sh quick static`). Four cumulative levels cover the
usual points of a change; each level adds groups to the one above it:

| Level | When | Command | Cost |
| --- | --- | --- | --- |
| L0 | after every save | `./tools/gates.sh quick` | ~4 s, no build |
| L1 | before every commit | `./tools/gates.sh quick build test gcc` | ~45 s |
| L2 | before a push | `./tools/gates.sh quick build test gcc static docs package --keep-going` | ~3 min |
| L3 | before a release, or a full local run | `./tools/gates.sh all` | ~9 min |

Times are the sum of the group wall times on a 16-core host; `build`, `test`,
`static`, `matrix` and `fuzz` scale with the machine, and `static` (cppcheck
plus the MISRA addon) is about 100 s of L2 while `fuzz` is 3 × what
`FUZZ_SECONDS` says.

L2 is a superset of the old `check_quality.sh`, not an equivalent: all ten of
its steps map onto `quick` (clang-format, per-file size, shellcheck, DLL
ownership, nullability regions, test conventions), `build` (strict Debug *and*
Release), `static` (cppcheck, MISRA), `docs` and `package`, and L2 adds `test`,
which the old script never ran.

Two compilers run, and they are not interchangeable:

- **clang is primary.** It is the family the firmware is built with (ATFE is
  clang, AC6 armclang is LLVM), and the pointer annotations
  (`IOLINK_NONNULL`/`IOLINK_NULLABLE`) only exist under clang — on gcc they
  expand to nothing. `IOLINKI_CC` selects it for the gates, the matrix, the
  coverage tree and the WCET harness.
- **gcc is the supplement.** It owns the diagnostics clang does not implement:
  `-Wduplicated-branches`, `-Wlogical-op`, `-Wmaybe-uninitialized` (only at
  `-O3`), `-Wstringop-overflow` and `-fanalyzer`. The flag set lives in
  `CMakeLists.txt` (`IOLINK_STRICT_GCC`) and applies to the library target
  only, because `-Wstack-usage`/`-Wframe-larger-than` describe the library, not
  the host examples.

Baselines record the toolchain that produced them and are never compared across
compilers:
- `tools/size_baseline.atfe.csv` is measured with ATFE (the
  `tools/size_matrix.py` default); `tools/size_baseline.csv` keeps the
  GNU-toolchain numbers and `IOLINKI_SIZE_TOOLCHAIN=gcc` reproduces them.
- `tools/coverage_baseline.txt` is calibrated on clang's gcov output, which
  counts 5496 executable lines on this tree where gcc counts 4809. gcc reports
  91.0% and clang 90.7% on the same sources; the file records both denominators
  so the difference is not read as a regression.

`sanitize` runs the same suite with AddressSanitizer and UndefinedBehaviorSanitizer
plus `implicit-conversion`, which covers what `-Wconversion` cannot see (an
`x++` on a narrow type, for example). `unsigned-integer-overflow` is left out on
purpose: the CRC and checksum arithmetic relies on wrapping. A sanitizer report
fails the group; there is no report-only mode.

`analyzer` runs `clang --analyze` over `src/` and fails on any finding. It does
not go through `scan-build`: that tool intercepts compilation by putting a `cc`
wrapper on `PATH`, while `configure_tree()` pins `CMAKE_C_COMPILER` to an
absolute path, so the wrapper never sees an invocation and the run reports "no
bugs" whatever the code says (a deliberate null-dereference probe confirmed it).

`memcheck` runs the unit binaries under valgrind. On hosts where valgrind cannot
redirect loader symbols (Arch with a recent glibc) the group reports `SKIP` with
the reason instead of one startup failure per binary; the Docker image installs
`valgrind` and `libc6-dbg` and runs the group for real, with `-gdwarf-4` because
valgrind 3.18 cannot parse DWARF 5.

Three checks are enforced: `check_context.py` (fields written across the
ISR/main boundary), `check_dead_fields.py` (fields production never reads) and
`check_public_api.py` (public functions referenced only from tests or missing
from the generated API table). The field migration removed the last known
findings; a new one now fails `quick` or `context` and therefore CI. Findings
that are intentional must be justified in the matching allowlist file.

No level up to L2 builds a structural configuration or re-measures size,
coverage or the WCET proxy, so a green L2 says nothing about those. Choose the
groups from what the change can break, not from the level:

#### Change type → required gates

| The change touches | Run before it is ready |
| --- | --- |
| A public header (`include/iolinki/*.h`), the `iolink_phy_api_t` vtable, or a field of a public struct | `./tools/gates.sh quick build test gcc context fuzz` |
| A compile-time switch, an `#if` in `src/`/`include/`, or an `IOLINK_*` configuration macro | `./tools/build_matrix.sh`, plus `./tools/gates.sh quick` (delivery rule in §3) |
| How the ISR and main contexts share state (`dll_shared.h`, `dll_main.h`, `dll_isr.h`, `tools/shared_fields.csv`) | `./tools/gates.sh quick context test`, and `context` must stay at zero findings (`--enforce` once the split lands) |
| A specification constant, a state-machine transition, or ISDU/event/DS behaviour | `./tools/gates.sh test` |
| Documentation only (`*.md`) | `./tools/gates.sh docs` (the generated blocks are compared, not regenerated) |
| A tool, a CI workflow or a shell script | `./tools/gates.sh quick` |
| A hot path, or anything that can move ROM/RAM, stack depth or coverage | `./tools/gates.sh perf` |
| A PHY driver or an example firmware | nothing locally (`package` is already in L2); the `stm32g0-example` arm build exists only in CI |

`gcc`, `context` and `fuzz` are part of the public-header row because a header
change reaches the second compiler's diagnostics, the ISR/main field ownership
gate and the parser harnesses. `sanitize`, `memcheck`, `analyzer` and `static`
are acceptance gates rather than per-commit gates: the combined run before
delivery includes them, and CI runs `sanitize` and `analyzer` in
`static-analysis`.

`all` is not the release gate either: it does not build the
`examples/stm32g0_l6362a_raw` firmware with `arm-none-eabi`, run
`.docker_test_entrypoint.sh` (Linux and BAREMETAL builds and the virtual-master
suite), validate the IODDs or generate SBOMs. A release is "CI green on
`own_dev`, then tag"; locally that is `./tools/gates.sh all` followed by
`./run_all_tests_docker.sh`.

A missing optional tool (clang-format, cppcheck, doxygen, shellcheck, the Arm
toolchain for the size and stack-frame baselines) reports `SKIP`. A skipped
group checks nothing, so a level only counts when its groups actually ran: CI
installs the tools in the Docker image and the job setup, and
`./run_all_tests_docker.sh` reproduces that environment locally.

### 3. Run the Compile Matrix
Every supported structural configuration must build. The matrix is defined once
in `tools/matrix_configs.sh`: six structural configurations (`host_default`,
`host_frame`, `host_no_ds`, `host_no_events`, `target_minimal` and
`host_smp_split`, plus the Release variant `host_default_release`), the syntax-only
combination probes, the link-time profile probes and the bare-metal platform.
Feature trimming is expressed with the individual `IOLINK_FEATURE_*` switches;
`host_no_ds` / `host_no_events` run `tests/test_trimmed_contract.c` so a
trimmed build proves what the master observes, not only that it compiles.

Configurations marked tests=yes (`host_default`, `host_default_release`,
`host_frame`, `host_no_ds`, `host_no_events`, `host_smp_split`) also run their
registered tests (the full suite except on the two trimmed rows). The suite is transport
neutral: `tests/test_helpers.c` implements both PHY contracts — the BYTE hooks
(`rx_octet`/`tx_octet`/`tx_done`) and the FRAME hooks
(`rx_frame`/`tx_frame`/`tx_busy`) — over the same request queue, and
`tools/build_matrix.sh` passes `-DIOLINK_TEST_TRANSPORT=<BYTE|FRAME>` to CMake
so the suite is built and run against the layout of that row.

`iolink_test_transport_probe` compiles the suite's transport label against the
compiled-in `IOLINK_ISR_*_MODE`, so a build that claims FRAME without the FRAME
definitions fails instead of silently running BYTE tests.

FRAME coverage has two explicit gaps, both BYTE-contract facts rather than
untested code:

- Individual cases that need a half-frame or a receive error from the PHY (a
  BYTE intermediate state that a FRAME PHY never delivers), the optional
  `tx_done` hook, timer B re-arm on frame end (the PHY's DMA completion in TX
  FRAME), or the 2-buffer PD-in lease are guarded with `#if IOLINK_ISR_*_MODE`.
  `test_pd_lease` becomes a whole-file skip, reported by ctest as `Skipped`
  through `SKIP_RETURN_CODE`; the in-binary cases print `SKIPPED` in the cmocka
  output.
- The PTY host examples (`conformance_device_smoke`, `conformance_device_master`,
  `conformance_device_iodd_oracle`) are registered only for the BYTE transport:
  `iolink_phy_virtual_get()` returns NULL in a FRAME build because the virtual
  PHY is a byte stream, so the example cannot initialise.

The FRAME counterpart of the PD-in lease is covered rather than skipped:
`tests/test_pd_lease_frame.c` links the `iolink_tx_busy_fixture` (TX FRAME with
`tx_busy()`), so it runs under every suite transport and pins the lease release
at the ISR entry together with the silent-period `pd_in_skips`.

`test_ctx_sizes` pins the layout: 4952/5136 bytes on BYTE and 4944/5128 bytes on
FRAME/FRAME for the DLL and device contexts. The transport changes both the
PD-in buffer count (`IOLINK_PD_IN_BUFFERS`) and the ISR RX assembly buffer, so
do not derive one pair from the other; see [MEMORY_GUIDE.md](MEMORY_GUIDE.md) for
the component breakdown and the `IOLINK_STATS_MODE=IOLINK_STATS_SPLIT` variant.

Mixed-granularity layouts (`FRAME_BYTE`, `BYTE_FRAME`) remain compile-only: they
have no test transport contract, so their rows stay tests=no.

Non-default COM rates are compile-only as well. `tools/matrix_configs.sh` emits
`com1` (`-DIOLINK_COM=1`) and `com3` (`-DIOLINK_COM=3`) as combination probes,
which run `cc -fsyntax-only` over the core sources; neither rate has a runnable
configuration, a ctest target or a test transport. **Only compilation is
verified** — the `#if IOLINK_COM` branches are never executed by the suite, so a
COM1/COM3 build passing the matrix says nothing about its runtime behaviour.
This paragraph and the probe list change together if a rate ever becomes
runnable.

`IOLINK_STATS_MODE` is a CMake cache variable, not a `CMAKE_C_FLAGS` macro:
leave it empty for the `config.h` default (`IOLINK_STATS_SHARED`), or set
`-DIOLINK_STATS_MODE=IOLINK_STATS_SPLIT` to build the per-context counters that
`test_stats_split` exercises. The value is applied build-wide, so the library and
the test always agree, and the configure log prints the effective mode.

A single FRAME row locally:

```bash
cmake -B build/frame -DIOLINK_PLATFORM=LINUX -DIOLINK_TEST_TRANSPORT=FRAME \
      -DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME -DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME
cmake --build build/frame
ctest --test-dir build/frame
```

Coverage sees both transports the same way: `tools/coverage.sh --transport both`
builds and tests BYTE and FRAME in separate trees (`build/coverage/` and
`build/coverage/frame/`), writes one gcovr JSON tracefile per transport and
merges them into a combined report. `--check` still compares only the BYTE
number, because `tools/coverage_baseline.txt` is calibrated on the BYTE build.

```bash
./tools/build_matrix.sh                    # everything (~1-2 min)
./tools/build_matrix.sh --filter host      # names containing "host"
./tools/build_matrix.sh --list             # configuration names
./tools/build_matrix.sh --probes-only      # compile-only probes
```

**Delivery rule:** a change that touches any `#if`, a `IOLINK_*` switch, or a
public header is not ready while `tools/build_matrix.sh` is red. Add a
configuration to the script when you introduce a new build switch.

### 4. Test conventions

Every registered C test binary is a cmocka program:

- one `static void test_x(void** state)` per case, cmocka assertion macros, and
  `cmocka_run_group_tests()` as the entry point. A failing case aborts only
  itself, so one broken expectation does not hide the rest of the binary;
- test bodies that report the first failing step as an integer keep that shape
  and are exposed through `IOLINK_TEST_RC(fn)` (`tests/test_case.h`), which
  turns `int fn(void)` into a case asserting `fn() == 0`. The macro is a
  definition, so it takes no trailing semicolon;
- `assert()` from `<assert.h>` is not used, so the suite does not depend on
  `-UNDEBUG` to keep its checks alive.

Two `ctest` entries are not C binaries: `dll_statemachine_gen_check` re-runs
`tools/gen_dll_statemachine.py --check`, so the state-machine table and its
documentation stay generated, and `od_iodd_export` runs
`tests/test_od_iodd_export.py`, which checks that the IODD export comes from the
C object-dictionary table rather than from JSON variables.

A reply that no case asserts is a failure, not a silent extra. The shared mock
(`tests/test_helpers.c`) counts delivered and asserted replies and every group
that uses it passes `test_tx_all_asserted` as the group teardown. The counters
are cumulative across cases (`setup_mock_phy()` no longer clears them), and a
reply may not be erased: `mock_phy_clear_tx()` counts any pending unasserted
reply as a discard. Use `mock_phy_ignore_tx()` where a reply is deliberately
irrelevant. Binaries with a local PHY mock (`test_reply_plan`, `test_isr_*`,
`test_dll_statemachine`, `test_tx_busy`) keep the same delivered/asserted
bookkeeping in their own group teardown.

A failing group teardown must fail the *process*: cmocka 2.x reports
`[ FAILED ] GROUP TEARDOWN` but still exits 0, so the gate prints the offending
frame and calls `exit(EXIT_FAILURE)` instead of relying on the return value.
`tools/check_test_gates.sh` rejects a teardown body that uses `assert_*` and a
binary that includes the shared harness without wiring the teardown, so the gate
cannot silently turn decorative again.

Tests reach the DLL layout through one header. Behaviour and conformance
tests use the public API (`iolink_device_get_state`, `iolink_device_get_dll_stats`,
`iolink_device_get_events_ctx`, `iolink_device_get_ds_ctx`, ...); where no public
accessor exists they use `tests/dll_access.h` (`test_dll`, `test_dll_shared`,
`test_dll_isdu`, `test_dll_ctx`). Nothing outside that header names the DLL
embedded in a device context (`ctx->dll...`), so changing how
`iolink_device_ctx_t` embeds it touches one file instead of every test.
`tests/dll_access.h` is also the only test file that includes
`iolinki/internal/dll_ctx.h` directly. Tests whose subject is the DLL internals declare
`iolink_dll_ctx_t` or `iolink_dll_main_t` themselves and keep using
`ctx.main...`.
`tools/check_test_whitebox.sh` enforces both rules and its `--selftest` keeps
the matchers honest; both run from `tools/gates.sh quick` and CI.

CMocka is a hard requirement for the suite: a configure without it fails
instead of silently registering no tests. Use `-DBUILD_TESTING=OFF` for a
library-only build. Trimming `IOLINK_FEATURE_DATA_STORAGE` or
`IOLINK_FEATURE_EVENTS` drops the full suite (it exercises the untrimmed stack)
but keeps `tests/test_trimmed_contract.c`, which asserts the responses such a
build still owes a master. `tests/transport_probe.c` is the one plain `main()`
left: it is a compile-time check that the suite's `IOLINK_TEST_TRANSPORT` label
matches the compiled ISR granularity, not a test.

### 5. Build trees

Every build goes below `build/`, one subdirectory per purpose:

| Directory | Created by |
| --- | --- |
| `build/quality`, `build/quality_release` | `tools/gates.sh build` (Debug and `-O3`) |
| `build/matrix`, `build/diff` | `build_matrix.sh`, `diff_matrix.sh` |
| `build/coverage` | `coverage.sh` |
| `build/wcet`, `build/size` | `wcet_run.sh`, `size_matrix.py` |
| `build/isr_latency` | `tools/isr_latency/run_isolated.sh` |
| `build/linux`, `build/baremetal` | `test_all.sh` |
| `build/docker`, `build/bare` | `.docker_test_entrypoint.sh` |

`build/` is ignored by git, so the repository root stays clean and one command
removes everything:

```bash
./tools/clean_builds.sh          # list build/ and its size
./tools/clean_builds.sh --yes    # remove it
```

Ad-hoc work should follow the same rule (`cmake -B build/scratch`), and the
`IOLINKI_*_DIR` environment variables can still point a script elsewhere.

### 6. Mutation spot checks

A small set of deliberate defects that the suite must catch. They are checked by
hand (in a `/tmp` copy of the tree, reverting each one) whenever the related
area changes; a survivor is a real coverage gap, not a warning.

| Mutant | Injected defect | Must be killed by |
|---|---|---|
| M1 | drop `__set_PRIMASK(saved)` in `examples/stm32g0_l6362a_raw/phy_l6362a_raw.c` | `test_phy_l6362a_raw::test_platform_hooks` (and four ISR cases) |
| M2 | `IOLINK_STAT_TOTAL` SPLIT branch ignores the ISR fields | `test_stats_split::test_public_stats_mirror_the_totals` in a `-DIOLINK_STATS_MODE=IOLINK_STATS_SPLIT` build |
| M3 | idle deadline constant `1001U` -> `1000U` in `src/dll.c` | `test_timer_table::test_idle_deadline_boundary` |
| M4 | `IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN` -> `VAL_LEN_UNDRUN` in `src/od.c` | `test_conformance_gaps::test_0123_application_tag_overlong` and the ISDU wire test |
| M5 | DS checksum comparison replaced by a no-op | `test_ds::test_ds_state_transitions` |

### 7. Docker Environment
To ensure a reproducible environment matching CI, run tests inside Docker:

```bash
./run_all_tests_docker.sh
```

It builds `Dockerfile.test` and runs `tools/gates.sh quick build static docs
package`, `tools/size_matrix.py --check --selftest` and
`.docker_test_entrypoint.sh` (Linux and BAREMETAL builds, `ctest`, and the
virtual-master tests) inside the container. The image installs clang (the
primary compiler) and the pinned Arm Toolchain for Embedded tarball, so the
ATFE size baseline is reproduced there; Arm publishes no container image, only
checksummed release tarballs.

## ✅ Continuous Integration (CI)

The CI workflow (`.github/workflows/ci.yml`) runs on every push and PR to
`own_dev`/`develop`:

1.  **docker-validation**: `./run_all_tests_docker.sh host`, i.e.
    `tools/gates.sh quick build static docs package`,
    `tools/size_matrix.py --check --selftest`, `tools/gates.sh memcheck`, then
    `.docker_test_entrypoint.sh` (Linux and BAREMETAL builds, `ctest`, and the
    virtual-master suite: `test_automated_mandatory`, `test_pd_variable`,
    `test_conformance_error_injection`, `test_conformance_isdu`,
    `test_conformance_state_machine`, `test_conformance_timing`).
2.  **release-build**: strict `-Werror` build and `ctest` at `-O3`.
3.  **build-matrix**: every structural configuration builds with clang
    (`IOLINKI_CC`), plus the syntax-only probes, the link-time profile gate
    (`tools/build_matrix.sh`), the differential gate (`tools/diff_matrix.sh`)
    and the file-size, ownership, white-box and install/consume checks.
4.  **static-analysis**: `tools/gates.sh gcc sanitize analyzer context` — the
    gcc-only diagnostics, the unit suite under the sanitizers, the clang static
    analyzer and the ISR/main field ownership gate. `context` is report-only
    until the context split removes its findings; the step then fails on a new
    violation without a workflow change. It is meant to be a required check.
5.  **docs-drift**: `tools/gates.sh docs`, i.e. the generated blocks compared
    verbatim by `tools/gen_docs.py --check` and a warning-free Doxygen run. It
    is meant to be a required check.
6.  **coverage-baseline / wcet-path-proxy / size-baseline**: three separate
    jobs. `tools/coverage.sh --check` enforces the line-coverage floor,
    `tools/wcet_run.sh --check` the host-side path-length proxy and
    `tools/size_matrix.py --check --selftest --table` the ROM/RAM budget. The
    size job installs the GNU toolchain from apt, so it pins
    `IOLINKI_SIZE_TOOLCHAIN=gcc` and gates `tools/size_baseline.csv`; the ATFE
    baseline is measured where ATFE is installed. The target stack-frame
    baseline (`tools/stack_report.py --check`) has no CI job of its own: it
    needs the cross compiler and runs in `perf` where the toolchain exists.
7.  **stm32g0-example**: STM32G0/TIOL112 firmware built with `arm-none-eabi`.
8.  **lint-scripts** and **sbom-tools**: shellcheck and Python tool tests.
9.  **fuzz**: `tools/gates.sh fuzz` builds the three libFuzzer harnesses and
    runs each for `FUZZ_SECONDS` (60 s in CI). A crash, a parser that stops
    making progress (`-timeout=10`), a sanitizer report or a leaked allocation
    fails the job; the offending input is uploaded from
    `build/fuzz/artifacts/`.

The release workflow (tag push) builds Debug with coverage, runs `ctest` with
JUnit output, validates the IODDs and the IAR project, generates SBOMs, packages
the artifacts, and executes the released firmware in both LabWired engine modes.
Tag only after CI on `own_dev` is green.

## 📊 Code Standards

See [CODING_STANDARDS.md](CODING_STANDARDS.md) for naming, C standard,
formatting and static-analysis rules.
