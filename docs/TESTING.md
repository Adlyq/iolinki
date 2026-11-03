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
Every supported compile-time configuration must build. The matrix covers the
feature toggles (`IOLINK_FEATURE_*`, `IOLINK_PHY_FEATURE_*`), all four ISR
granularity combinations, the minimal/tiny profiles, the compatibility switches
and the bare-metal platform. Configurations marked tests=yes (default, release,
single_profile, smp_profile and stats_split) also run the unit suite; the others
are build-only because the host tests drive the BYTE-only virtual PHY.

```bash
./tools/build_matrix.sh                 # everything (~1-2 min)
./tools/build_matrix.sh --filter isr_   # one group
./tools/build_matrix.sh --list          # configuration names
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
3.  **build-matrix**: every compile-time configuration builds
    (`tools/build_matrix.sh`).
4.  **stm32g0-example**: STM32G0/TIOL112 firmware built with `arm-none-eabi`.
5.  **lint-scripts** and **sbom-tools**: shellcheck and Python tool tests.

The release workflow (tag push) builds Debug with coverage, runs `ctest` with
JUnit output, validates the IODDs and the IAR project, generates SBOMs, packages
the artifacts, and executes the released firmware in both LabWired engine modes.
Tag only after CI on `own_dev` is green.

## 📊 Code Standards

See [CODING_STANDARDS.md](CODING_STANDARDS.md) for naming, C standard,
formatting and static-analysis rules.
