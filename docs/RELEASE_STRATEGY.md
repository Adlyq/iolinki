# iolinki Release Strategy

This document outlines the development workflow, versioning, and release procedures for the `iolinki` project.

## 1. Workflow

The branch model is defined in [MAINTENANCE.md](MAINTENANCE.md). Official tags
(`vX.Y.Z`) are pushed from `own_dev`; `release/*` branches cut from `own_dev` are
the source for `vX.Y.Z-rcN` tags. There is no upstream sync, promotion or
back-merge step.

## 2. Versioning

We use **Semantic Versioning 2.0.0** (SemVer).

Format: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible API changes or significant protocol version updates.
- **MINOR**: Backward-compatible new functionality (e.g., new standard definitions).
- **PATCH**: Backward-compatible bug fixes.

## 3. CI/CD

Automated pipelines run on every push to `own_dev` (the maintained branch) and
`develop` (historical).

### Quality Gate (runs on all pushes/PRs)
1. **Code Formatting**: `clang-format` check (pinned 23.1.1).
2. **Static Analysis**: `cppcheck` (`warning,style,performance,portability`).
3. **MISRA Check**: `cppcheck --addon=misra` (MISRA C:2012 subset). Report-only unless `IOLINKI_MISRA_ENFORCE=1` is set.
4. **Unit Tests**: All CMocka tests must pass.
5. **Integration Tests**: Virtual IO-Link Master conformance (V1.1.5), run by the
   CI `docker-validation` job.
6. **Release build**: strict `-Werror` build at `-O3` (Debug builds cannot emit
   some optimisation-only warnings).
7. **Compile matrix**: every feature toggle, ISR mode and platform builds
   (`tools/build_matrix.sh`).

### Build Stages
1. **Build**: CMake build for host (Linux).
2. **Test**: Run all unit tests via `ctest`.
3. **Example Build**: Verify example projects compile.

## 4. Release Process

### Automated Release (own_dev)

Official releases are tagged from the `own_dev` branch once its checks are
green. There is no `main` promotion and no fast-forward/back-merge step.

1. **Tag the maintained branch**:
   ```bash
   git fetch origin
   git switch own_dev
   git pull --ff-only
   git tag -a vX.Y.Z -m "Release version X.Y.Z"
   git push origin vX.Y.Z
   ```

2. **Workflow**: The GitHub Action automatically:
   - Builds with **Debug + Code Coverage** (`-DCMAKE_BUILD_TYPE=Debug
     -DENABLE_COVERAGE=ON`).
   - Generates **Automated Release Notes**.
   - Packages binaries, **SBOMs (CycloneDX + SPDX)**, and creates a GitHub Release.

### Release Candidates (RC)

RCs are used to verify the release workflow and quality before finalizing.

1. **RC Tagging**: Push an RC tag from a `release/*` branch cut from `own_dev`:
   ```bash
   git checkout own_dev
   git checkout -b release/X.Y.Z
   git tag -a vX.Y.Z-rc1 -m "Release Candidate 1"
   git push origin vX.Y.Z-rc1
   ```
2. **Verification**: RC tags trigger the same build/test pipeline but do NOT
   create the final release.

> [!TIP]
> Use RC tags to "dry-run" the release notes and binary packaging without affecting the production baseline.

### Manual Release (PR-Based Flow)

1. **Prepare Release Branch**:
   ```bash
   git checkout own_dev
   git checkout -b release/x.y.z
   ```

2. **Update Documentation**:
   - Update version in `CMakeLists.txt`
   - Update `CHANGELOG.md`

3. **Verify Quality Locally**:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   cd build && ctest --output-on-failure
   ```
   The default CMake build sets no `CMAKE_BUILD_TYPE` (no optimisation), and the
   release workflow above builds `Debug` with coverage. The `Release` build here
   is an optimisation sanity check, not the configuration used for artifacts.

4. **Open the PR**:
   - Push the `release/x.y.z` branch to remote.
   - Open a PR from `release/x.y.z` to **`own_dev`**.
   - **Wait for CI to pass** and get approval.
   - Merge the PR.

5. **Tag the Release**:
   - On the updated `own_dev` branch locally:
     ```bash
     git pull origin own_dev
     git tag -a vx.y.z -m "Release vx.y.z"
     git push origin vx.y.z
     ```

6. **Troubleshooting & Retries**:
   - **Failed Release**: If the CI fails on a tag, delete the tag on remote and local, fix the issue on `own_dev`, and re-tag:
     ```bash
     git tag -d v1.0.0
     git push origin :v1.0.0
     # Fix issue on own_dev...
     git tag v1.0.0
     git push origin v1.0.0
     ```
   - **Pre-commit Blocks**: If hooks (e.g., branch name checks) block release housekeeping commits, use `--no-verify`:
     ```bash
     git commit -m "chore(release): bump version" --no-verify
     ```

## 5. Release Artifacts

Each GitHub Release includes:
- **Test Results**: JUnit XML, test-count summary, coverage summary and
  per-release SBOMs (CycloneDX 1.6 + SPDX 2.3)
- **Source**: full-source zip plus an examples zip
- **Build Artifacts**: host demos (`host_demo`, `simple_device`, `test_init`),
  STM32G0/TIOL112 firmware (ELF/HEX/MAP), the native IAR project, IODD packages
  and the LabWired recipe archive
- **Checksums**: `SHA256SUMS` over every published file

## 6. Distribution

### Open-Source Evaluation (GPLv3)
- **Source**: GitHub releases provide the full source archive (automatic).
- **Artifacts**: Example/test binaries are provided for evaluation and verification.
- **Checksums**: Each release includes SHA256 checksums for all published artifacts.

### Commercial Distribution
- **Access**: Commercial customers receive a licensed SDK bundle via a private download link after purchase.
- **Contents**:
  - Headers + libraries (platform-specific)
  - Example binaries
  - Test bundle (optional)
  - Documentation snapshot
- **License Delivery**: License file and terms are delivered with the SDK bundle.
- **Lead Time**: Typical fulfillment within 2 business days.
- **Pricing and scope**: Indie, Company and Enterprise tiers, their product-family
  scope and support terms are defined in `LICENSE.COMMERCIAL`. New quotes cover
  unlimited units and customers within the agreed family; separate product lines,
  subsidiaries and broader redistribution need agreed additional scope.

### Support Policy
- **Included Support**: 12 months of updates and support are included in the commercial price.
- **Assistance**: the per-tier hours, response targets and deliverables are defined
  in `LICENSE.COMMERCIAL`; agree the board, compiler, PHY and validation tasks
  before scheduling.

### Provenance
- **Signing**: Release artifacts are signed or accompanied by checksums.
