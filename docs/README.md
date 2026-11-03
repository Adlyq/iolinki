# iolinki Documentation

Documentation for the `iolinki` IO-Link Device Stack. The companion **master**
stack is [iolinki-master](https://github.com/w1ne/iolinki-master).

## Start here

- **[README](../README.md)** — project overview, installation and quick start
- **[SPEC_CONFORMANCE](SPEC_CONFORMANCE.md)** — spec table -> implementation ->
  test matrix; the single source of truth for what is implemented
- **[FAQ](FAQ.md)** — evaluation, licensing and integration questions

## Development

- **[ARCHITECTURE](ARCHITECTURE.md)** — layers, processing model, design rules
- **[CODE_FLOW](CODE_FLOW.md)** — main-loop/ISR call flow and integration notes (中文)
- **[CODING_STANDARDS](CODING_STANDARDS.md)** — style, naming, quality gates
- **[TESTING](TESTING.md)** — unit, integration and virtual-master tests; CI gates
- **[RELEASE_STRATEGY](RELEASE_STRATEGY.md)** — versioning and release process
- **[MAINTENANCE](MAINTENANCE.md)** — branch model and upstream-adoption rules
- **[UPSTREAM_DIVERGENCE](UPSTREAM_DIVERGENCE.md)** — historical divergence
  inventory against `upstream/develop`

## Integration guides

- **[API](API.md)** — public API reference
- **[API_INTEGRATION](API_INTEGRATION.md)** — integration workflow, configuration
  lifetimes, ISR/PHY porting and current limitations (中文)
- **[PORTING](PORTING.md)** — platform and RTOS porting contract
- **[STORAGE](STORAGE.md)** — NVM backend and Data Storage parameters
- **[MEMORY_GUIDE](MEMORY_GUIDE.md)** — RAM/ROM sizing and trimming
- **[FRAMES](FRAMES.md)** — M-sequences, frame layouts and checksums
- **[feature-trimming](feature-trimming.md)** — compile-time feature switches
- **[EXAMPLE_COVERAGE](EXAMPLE_COVERAGE.md)** — examples and evidence checklist

## Conformance records

- **[CONFORMANCE_AUDIT_2026-10-28](CONFORMANCE_AUDIT_2026-10-28.md)** —
  independent 142-case audit
- **[HIL_AUDIT_2026-10-28](HIL_AUDIT_2026-10-28.md)** and
  **[HIL_CORRECTION_GUIDE_2026-10-28](HIL_CORRECTION_GUIDE_2026-10-28.md)** —
  review of the external HIL project

## Tools

- **[Virtual Master](../tools/virtual_master/README.md)** — Python virtual
  IO-Link master; [services](../tools/virtual_master/SERVICES.md)
- **[IODD tools](../tools/IODD_GEN.md)** — generate, validate and pack IODDs

## Specification (source of truth)

- **[IO-Link Interface Spec V1.1.5](IOL-Interface-Spec_10002_V1.1.5_Oct2025/)** —
  **authoritative**. When code, comments or docs disagree with this PDF, the PDF
  wins.
