# iolinki Documentation

Comprehensive documentation for the iolinki IO-Link Device Stack.

The companion master stack is [iolinki-master](https://github.com/w1ne/iolinki-master).

## Getting Started

- **[README](../README.md)** - Project overview and quick start
- **[INSTALL](../INSTALL.md)** - Installation instructions
- **[VISION](VISION.md)** - Project vision and goals

## Conformance & testing

- **[SPEC_CONFORMANCE](SPEC_CONFORMANCE.md)** - **start here:** V1.1.5 table ->
  implementation -> test matrix plus the known-gap backlog.
- **[Testing Guide](TESTING.md)** - Unit tests, integration tests, Virtual Master.
- **[CONFORMANCE](CONFORMANCE.md)** - Virtual-Master conformance suites.

## Development

- **[ROADMAP](ROADMAP.md)** - Development roadmap and V1.1.5 compliance checklist
- **[ARCHITECTURE](ARCHITECTURE.md)** - System architecture and design
- **[CODING_STANDARDS](CODING_STANDARDS.md)** - Code style and conventions
- **[RELEASE_STRATEGY](RELEASE_STRATEGY.md)** - Release process and versioning

## Technical Guides

- **[API Reference](API.md)** - Complete API documentation
- **[Frame Support](FRAMES.md)** - Supported M-sequences and frame structures
- **[Testing Guide](TESTING.md)** - Unit tests, integration tests, and Virtual Master
- **[Platform Porting](PORTING.md)** - How to port to new platforms and RTOSes

## Tools

- **[Virtual Master](../tools/virtual_master/README.md)** - Python Virtual IO-Link Master
  - [Services](../tools/virtual_master/SERVICES.md) - Supported services and API

## Specification (source of truth)

- **[IO-Link Interface Spec V1.1.5](IOL-Interface-Spec_10002_V1.1.5_Oct2025/)** -
  **authoritative**. When code, comments or docs disagree with this PDF, the PDF
  wins.
- **[superpowers/](superpowers/)** - historical plans/specs; not maintained.

## Transport note

The default DLL transport is upstream's streaming path (`phy->send/recv_byte`,
`src/dll.c`). The two-pump / ISR transport is opt-in:
`cmake -DIOLINK_ISR_PUMP=ON` (enables the `#if` ISR block in `src/dll.c`). See
[SPEC_CONFORMANCE](SPEC_CONFORMANCE.md).

## API Documentation (Doxygen)

## Continuous Integration

| Platform | Status |
|----------|--------|
| **Linux** | ✅ Verified |
| **Zephyr** | ✅ Verified (Native Sim) |
| **Bare Metal** | ✅ Verified (Build) |
| **Code Quality** | ✅ Pass (MISRA/Cppcheck) |

## Quick Start

### Run Tests (Docker)
```bash
docker build -t iolinki-test .
docker run --rm iolinki-test
```

Output: `docs/html/index.html`

## Contributing

See [CODING_STANDARDS](CODING_STANDARDS.md) for contribution guidelines.
