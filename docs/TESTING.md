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

### 2. Run Quality Checks
Verify code against warnings and static analysis tools.
```bash
./check_quality.sh
```

### 3. Run the Compile Matrix
Every supported structural configuration must build. The matrix is defined once
in `tools/matrix_configs.sh`: four structural configurations (`host_default`,
its Release variant `host_default_release`, `host_frame` and
`target_minimal`, plus `host_smp_split`), seven syntax-only combination probes,
the link-time profile probes and the bare-metal platform. The retired
MINIMAL/TINY bundles are gone; feature trimming is expressed with the individual
`IOLINK_FEATURE_*` switches. Configurations marked tests=yes (`host_default`,
`host_default_release`, `host_smp_split`) also run the unit suite; `host_frame`
is build-only because the host tests drive BYTE-only PHY vtables, and demoted
runtime switches are covered by expected-frame tests in the default suite.

```bash
./tools/build_matrix.sh                    # everything (~1-2 min)
./tools/build_matrix.sh --filter host      # names containing "host"
./tools/build_matrix.sh --list             # configuration names
./tools/build_matrix.sh --probes-only      # compile-only probes
```

**Delivery rule:** a change that touches any `#if`, a `IOLINK_*` switch, or a
public header is not ready while `tools/build_matrix.sh` is red. Add a
configuration to the script when you introduce a new build switch.

### 4. Docker Environment
To ensure a reproducible environment matching CI, run tests inside Docker:

```bash
./run_all_tests_docker.sh
```

It builds `Dockerfile.test` and runs `check_quality.sh` plus
`.docker_test_entrypoint.sh` (Linux and BAREMETAL builds, `ctest`, and the
virtual-master tests) inside the container.

## ✅ Continuous Integration (CI)

The CI workflow (`.github/workflows/ci.yml`) runs on every push and PR to
`own_dev`/`develop`:

1.  **docker-validation**: `check_quality.sh` + Linux/BAREMETAL builds + `ctest`
    + the virtual-master suite
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
