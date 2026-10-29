# iolinki: Open-Source IO-Link Device Stack

**Hardware-Agnostic IO-Link Protocol Stack for Bare-Metal and Host Systems**

The companion master stack is [iolinki-master](https://github.com/w1ne/iolinki-master).

## Overview

`iolinki` is a professional-grade, open-source IO-Link Device Stack (implements IO-Link V1.1.5, Package 2025 — the latest published specification package) designed with complete hardware independence and comprehensive test coverage. Built from the ground up using test-driven development, every component is testable without hardware.

### Key Features

- **Hardware-Agnostic**: Runs on any platform via clean PHY abstraction
- **Test-Driven**: 100% mock-based unit testing from day one
- **Portable**: bare-metal and host (Linux) compatible
- **Virtual Testing**: Conformance verification against virtual IO-Link Master
- **Open Source**: Transparent, vendor-agnostic implementation

### Licensing

**Dual-licensed: GPLv3 or Commercial.** Pick the one that matches your product.

- **GPLv3 (free, open source)** — use, modify, and ship iolinki at no cost, including in production, provided your own work that includes it is also released under the GPLv3.
- **Commercial license** — **required only if you ship a closed-source / proprietary product** and do not want the GPLv3 copyleft obligations. One-time, royalty-free:
  - **Indie**: €1,399 — issued to a named individual for their own independent product family; two onboarding hours for build/setup, MCU/PHY wiring and callback review, and a written bring-up checklist; 48-hour email response target
  - **Company**: €4,699 — held by the named legal company for one agreed product family, with the same source; eight total scoped engineering hours, 24-hour priority email response target and quarterly technical reviews during the first year within those hours
  - **Enterprise**: custom for multiple families, subsidiaries or broader SDK redistribution
  - Indie is for independent developers, including sole traders, and does not grant product rights to an employer or separate company. Company permits unlimited authorized employees and contractors working on the licensed family; company projects require company-held rights.
  - Includes 12 months of updates and bug fixes. Agree board/compiler/PHY/tasks and booking window before scheduling. Company assistance deliverables within the eight-hour allowance include build/configuration review, MCU/PHY callback and wiring review, a written checklist and findings from the agreed build/test review. Larger ports are quoted separately.
  - New quotes cover one named commercial product range, including variants, successors and hardware revisions within that range: unlimited manufactured units and customers, royalty-free. An unrelated line needs a separate license. MCU/PHY changes within the agreed family keep the license valid; porting and validation are separately quoted. Already granted or separately agreed rights remain governed by their accepted agreement.
  - Both packages receive the same source, documentation and reference release. Reproducible stack bug corrections do not consume custom engineering hours. Company contractors receive access for the licensee's project; independent reuse, subsidiaries and broader redistribution require agreed additional scope.

**Shipping a closed-source product?** A commercial license removes the GPLv3 obligations — email **andrii@shylenko.com** for terms (fast, no-friction). See [LICENSE](LICENSE) and [LICENSE.COMMERCIAL](LICENSE.COMMERCIAL).

## Quick Start

For a runnable counter/button/LED software example, see
[examples/reference_device](examples/reference_device/README.md). For a
board-specific ISR two-pump PHY reference (STM32G0 + L6362A), see
[examples/stm32g0_l6362a_raw](examples/stm32g0_l6362a_raw/README.md). For
practical evaluation questions and integration status, see
[docs/FAQ.md](docs/FAQ.md) and
[the portable TIOL112 driver](docs/hardware/TIOL112.md). Physical board ports and
commercial-master validation are separate from the host simulation.

### Installation

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential libcmocka-dev clang-format cppcheck
```

#### macOS
```bash
brew install cmake cmocka clang-format cppcheck
```

### Building

```bash
cmake -B build
cmake --build build
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

### Development Tools (pre-commit)

To ensure code quality, `iolinki` uses `pre-commit` hooks. These hooks run `clang-format`, `cppcheck`, `ruff`, and `shellcheck` automatically before each commit.

1. **Install pre-commit**:
   ```bash
   pip install pre-commit
   ```

2. **Install the git hooks**:
   ```bash
   pre-commit install
   ```

3. **(Optional) Run on all files**:
   ```bash
   pre-commit run --all-files
   ```

## Running Tests

### Docker (Primary & Recommended)
Docker is the primary and recommended environment for running all `iolinki` tests. This ensures a consistent environment with all tools (CMocka, Cppcheck, Doxygen, Clang-Format) pre-configured.

```bash
./run_all_tests_docker.sh
```

### Local (Requires Manual Dependencies)
To run tests locally, ensure you have `libcmocka-dev` installed:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

### Full Validation Suite (All Platforms + Conformance)
```bash
./test_all.sh
```

## IO-Link V1.1.5 Conformance

Conformance status is tracked case by case, not by a headline test count:

- **[Spec conformance matrix](docs/SPEC_CONFORMANCE.md)** — feature -> implementation -> test.
- **[Conformance audit](docs/CONFORMANCE_AUDIT_2026-10-28.md)** — independent 142-case
  audit, scope split and residual risks.
- **[Machine-readable audit](tests/conformance_audit.csv)** — authoritative per-case status.

Current disposition: **111 direct / 19 n/a / 12 out-of-scope** of 142 Test Spec
case IDs. The 12 out-of-scope items are physical/HIL or product-IODD work; they
are not claimed as library conformance. There are no in-scope `partial` or `gap`
entries.

Run the host suite and the traceability gate with:

```bash
cmake -B build && cmake --build build
(cd build && ctest --output-on-failure)
```

### Building for Bare Metal

```bash
cmake -B build_bare -DIOLINK_PLATFORM=BAREMETAL
cmake --build build_bare
./build_bare/examples/bare_metal_app/bare_metal_app
```

## Project Status

**Phase 3: Ecosystem & Verification (Current)**

The stack is feature-complete for IO-Link V1.1.5, including Process Data, ISDU, Events, and Data Storage. We have achieved 100% core test coverage and established automated IODD generation and strict coding standards (MISRA-oriented).

See [ROADMAP.md](./docs/ROADMAP.md) for detailed development phases.

## Documentation

- **[ROADMAP.md](./docs/ROADMAP.md)** - Development phases and milestones
- **[VISION.md](./docs/VISION.md)** - Project mission and approach
- **[RELEASE_STRATEGY.md](./docs/RELEASE_STRATEGY.md)** - Release workflow and versioning
- **[INSTALL.md](./INSTALL.md)** - Detailed installation instructions
- **[PUPPETEER.md](./docs/PUPPETEER.md)** - Agent task mutex workflow (submodule)

## Security

- **[SECURITY.md](./SECURITY.md)** — coordinated disclosure policy and CRA-ready
  vulnerability handling
- **[Threat model](./docs/security/THREAT_MODEL.md)** — STRIDE analysis aligned to
  the IO-Link Security Design and Development Guideline (Order No. 10.512), every
  claim anchored to code and tests
- **[CRA overview](./docs/security/CRA.md)** — what the EU Cyber Resilience Act
  means for devices built on this stack; free vs. commercial deliverables
- **SBOMs** — CycloneDX 1.6 + SPDX 2.3 attached to every tagged release

## Related Projects

- **[iolinki-master](https://github.com/w1ne/iolinki-master)** — the companion
  standalone IO-Link **master** stack (multi-port controller). Split from this
  repository on purpose; it reuses only the narrow shared CRC/frame/PHY pieces
  and is CI-validated against this device stack over a simulated wire.

## Releases

Releases for this branch are published from `origin`
([Adlyq/iolinki](https://github.com/Adlyq/iolinki/releases)). Upstream releases
at `w1ne/iolinki` are a separate line and are not the source of this branch.

Each release includes:
- **Test Results**: Complete test suite validation
- **Build Artifacts**: Pre-compiled examples and test binaries
- **Documentation**: Updated docs and guides

To create a new release (see [docs/MAINTENANCE.md](docs/MAINTENANCE.md)):
```bash
git switch own_dev
git tag -a vX.Y.Z -m "Release version X.Y.Z"
git push origin vX.Y.Z
```

## Development Philosophy

**Test-Driven from Ground Zero**: All development is built on mocks and abstractions. Every component is testable without hardware. Conformance verification runs against a virtual IO-Link Master on each release.

## License

This project is dual-licensed under the **GPLv3** (free, for open-source/GPLv3 use)
and a **commercial license** (for closed-source / proprietary products that cannot
accept the GPLv3 copyleft). Shipping a proprietary product? Email
**andrii@shylenko.com**. See [LICENSE](LICENSE) and [LICENSE.COMMERCIAL](LICENSE.COMMERCIAL).

## Contributing

Contributions are welcome! Please see [ROADMAP.md](./docs/ROADMAP.md) for areas where help is needed.
