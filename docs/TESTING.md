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
| `quick` | clang-format, per-file size (800 code lines, comments excluded), shellcheck, DLL ownership isolation, test conventions (`check_test_whitebox.sh`, `check_test_gates.sh`) |
| `build` | strict `-Werror` compilation, Debug (`-O0`) and Release (`-O3`) |
| `test` | strict Debug build plus `ctest` |
| `static` | cppcheck and the MISRA addon (report-only unless `IOLINKI_MISRA_ENFORCE=1`) |
| `docs` | Doxygen warnings |
| `matrix` | `tools/build_matrix.sh` and `tools/diff_matrix.sh` |
| `perf` | `tools/coverage.sh --check`, `tools/wcet_run.sh --check`, `tools/size_matrix.py --check --selftest` |
| `package` | the exported package is installed into a scratch prefix and a two-file consumer is built against it |
| `all` | every group above |

Groups compose (`tools/gates.sh quick static`). Pick them by what the change
can break:

| When | Command |
| --- | --- |
| after an edit | `./tools/gates.sh quick` |
| before a commit | `./tools/gates.sh quick build test` |
| before a push | `./tools/gates.sh quick build test static docs package` |
| before a release, or a full local run | `./tools/gates.sh all` |

A missing optional tool (clang-format, cppcheck, doxygen, shellcheck, the Arm
toolchain for the size gate) reports `SKIP`; CI installs them in the Docker
image and the job setup, so a skip only happens locally.

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
  `iolink_phy_virtual` is a byte stream and its `get()` returns NULL in a FRAME
  build, so the example cannot initialise.

`test_ctx_sizes` pins both layouts: 4672/4864 bytes on BYTE and 4664/4856 bytes
on FRAME/FRAME for the DLL and device contexts.

Mixed-granularity layouts (`FRAME_BYTE`, `BYTE_FRAME`) remain compile-only: they
have no test transport contract, so their rows stay tests=no.

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

Every registered test binary is a cmocka program:

- one `static void test_x(void** state)` per case, cmocka assertion macros, and
  `cmocka_run_group_tests()` as the entry point. A failing case aborts only
  itself, so one broken expectation does not hide the rest of the binary;
- test bodies that report the first failing step as an integer keep that shape
  and are exposed through `IOLINK_TEST_RC(fn)` (`tests/test_case.h`), which
  turns `int fn(void)` into a case asserting `fn() == 0`. The macro is a
  definition, so it takes no trailing semicolon;
- `assert()` from `<assert.h>` is not used, so the suite does not depend on
  `-UNDEBUG` to keep its checks alive.

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
package` plus `.docker_test_entrypoint.sh` (Linux and BAREMETAL builds, `ctest`,
and the virtual-master tests) inside the container.

## ✅ Continuous Integration (CI)

The CI workflow (`.github/workflows/ci.yml`) runs on every push and PR to
`own_dev`/`develop`:

1.  **docker-validation**: `tools/gates.sh quick build static docs package`
    + Linux/BAREMETAL builds + `ctest` + the virtual-master suite
    (`test_automated_mandatory`, `test_pd_variable`,
    `test_conformance_error_injection`, `test_conformance_isdu`,
    `test_conformance_state_machine`, `test_conformance_timing`).
2.  **release-build**: strict `-Werror` build and `ctest` at `-O3`.
3.  **build-matrix**: every structural configuration builds, plus the
    syntax-only probes and the link-time profile gate
    (`tools/build_matrix.sh`).
4.  **differential gate**: `tools/diff_matrix.sh` compares the frame transcripts
    of the runnable configurations byte for byte.
5.  **coverage / size / WCET**: `tools/coverage.sh --check`,
    `tools/size_matrix.py --check --selftest` and `tools/wcet_run.sh --check`
    enforce the floors and the host-side path-length proxy.
6.  **stm32g0-example**: STM32G0/TIOL112 firmware built with `arm-none-eabi`.
7.  **lint-scripts** and **sbom-tools**: shellcheck and Python tool tests.

The release workflow (tag push) builds Debug with coverage, runs `ctest` with
JUnit output, validates the IODDs and the IAR project, generates SBOMs, packages
the artifacts, and executes the released firmware in both LabWired engine modes.
Tag only after CI on `own_dev` is green.

## 📊 Code Standards

See [CODING_STANDARDS.md](CODING_STANDARDS.md) for naming, C standard,
formatting and static-analysis rules.
