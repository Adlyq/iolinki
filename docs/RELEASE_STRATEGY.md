# iolinki Release Strategy

This document outlines the development workflow, versioning, and release procedures for the `iolinki` project.

## 1. Workflow

We use a simplified **Gitflow** model.

### Branches
- **`own_dev`**: Maintained mainline. All development lands here and **all official version tags (`vX.Y.Z`) are pushed from this branch**.
- **`fix/*` / `feature/*`**: Topic branches, created from and merged back to `own_dev` via PR.
- **`release/*`**: Release preparation, branched from `own_dev`. Source for `vX.Y.Z-rcN` tags.
- **`develop`**: Historical snapshot from before independent maintenance. Not a release branch and not a merge target.
- **`main`**: Does not exist on `origin`. Do not create it without an explicit policy change.

> This repository is maintained independently of `upstream` (`w1ne/iolinki`).
> There is no upstream sync, promotion or back-merge step. See
> [MAINTENANCE.md](MAINTENANCE.md).

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
1. **Code Formatting**: `clang-format` check (enforces `.clang-format` style).
2. **Static Analysis**: `cppcheck` with all checks enabled.
3. **MISRA Check**: Run in Docker via `cppcheck --addon=misra` (MISRA C:2012 subset). Report-only unless `IOLINKI_MISRA_ENFORCE=1` is set.
4. **Unit Tests**: All CMocka tests must pass.
5. **Integration Tests**: Enforced - Virtual IO-Link Master conformance (V1.1.5).
6. **Release build**: strict `-Werror` build at `-O3` (Debug builds cannot emit
   some optimisation-only warnings).

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
   - Update `ROADMAP.md` milestones

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
- **Test Results**: Test count and pass/fail summary
- **Build Artifacts**:
  - `simple_device` - Example executable
  - `test_init` - Unit test executable
- **Documentation**: Links to all project docs
- **Source Code**: Automatic archive by GitHub

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
- **Pricing** (see `LICENSE.COMMERCIAL`):
  - **Indie**: €1,399 (one-time, royalty-free; named individual, own independent product family)
  - **Company**: €4,699 (one-time, royalty-free; named legal company, one agreed product family, with scoped engineering assistance)
  - **Enterprise**: Custom pricing

New quotes allow unlimited manufactured units and customers, including variants,
successors and hardware revisions within the agreed named commercial range.
Unrelated lines need a separate license. MCU/PHY changes within that range retain
the license; porting and validation are separately quoted. Enterprise scope for
multiple families, subsidiaries or broader SDK redistribution is quoted. Already
granted or separately agreed rights retain their accepted scope.
Indie is issued to a named individual developing and distributing their own
independent product family, including as a sole trader. It does not grant an
employer or separate company product rights. Company is held by the named legal
company and allows unlimited authorized employees and contractors working on the
family. Separate legal entities and independent contractor reuse require agreed
additional scope.

### Support Policy
- **Included Support**: 12 months of updates and support are included in the commercial price.
- **Assistance**: Indie includes two onboarding hours for build/setup, MCU/PHY wiring and callback review, and a written bring-up checklist, with a 48-hour email response target. Company includes eight total scoped engineering hours, a 24-hour priority response target and quarterly technical reviews during the first year within that allowance. Deliverables within the hours include build/configuration review, MCU/PHY callback and wiring review, a written checklist and findings from the agreed build/test review. Agree board/compiler/PHY/tasks and the booking window before scheduling. Larger ports are quoted separately; reproducible stack bug corrections do not consume custom engineering hours. See LICENSE.COMMERCIAL for holder and support terms.

### Provenance
- **Signing**: Release artifacts are signed or accompanied by checksums.
