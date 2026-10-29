# iolinki Documentation

Comprehensive documentation for the iolinki IO-Link Device Stack.

The companion master stack is [iolinki-master](https://github.com/w1ne/iolinki-master).

## Getting Started

- **[README](../README.md)** - Project overview and quick start
- **[INSTALL](../INSTALL.md)** - Installation instructions
- **[VISION](VISION.md)** - Project vision and goals

## Conformance & testing

- **[SPEC_CONFORMANCE](SPEC_CONFORMANCE.md)** - **start here:** V1.1.5 table ->
  implementation -> test matrix plus external residuals.
- **[Conformance Audit](CONFORMANCE_AUDIT_2026-10-28.md)** - 142-case audit,
  final scope split and evidence status.
- **[Testing Guide](TESTING.md)** - Unit tests, integration tests, Virtual Master.

## Development

- **[ROADMAP](ROADMAP.md)** - Development roadmap and V1.1.5 compliance checklist
- **[ARCHITECTURE](ARCHITECTURE.md)** - System architecture and design
- **[CODING_STANDARDS](CODING_STANDARDS.md)** - Code style and conventions
- **[RELEASE_STRATEGY](RELEASE_STRATEGY.md)** - Release process and versioning

## Technical Guides

- **[API Reference](API.md)** - Complete API documentation
- **[API and Integration Guide](API_INTEGRATION.md)** - Integration workflow,
  configuration lifetimes, ISR/PHY porting and current limitations
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

The stack uses a single ISR two-pump transport: the data interrupt frames
requests and transmits replies (`src/dll_isr.c`, `iolink_device_process_isr()`),
while the main loop runs the protocol logic. See
[SPEC_CONFORMANCE](SPEC_CONFORMANCE.md).

## API Documentation (Doxygen)

## Continuous Integration

| Platform | Status |
|----------|--------|
| **Linux** | ✅ Verified |
| **Bare Metal** | ✅ Verified (Build) |
| **Code Quality** | ✅ Cppcheck enforced; MISRA report-only by default |

## Quick Start

### Run Tests (Docker)
```bash
docker build -t iolinki-test .
docker run --rm iolinki-test
```

Output: `docs/html/index.html`

## Contributing

See [CODING_STANDARDS](CODING_STANDARDS.md) for contribution guidelines.
