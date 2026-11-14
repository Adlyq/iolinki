# iolinki Documentation

Documentation for the `iolinki` IO-Link Device Stack. The companion **master**
stack is [iolinki-master](https://github.com/w1ne/iolinki-master).

## Start here

- **[README](../README.md)** — project overview, installation and quick start
- **[SPEC_CONFORMANCE](SPEC_CONFORMANCE.md)** — spec clause → implementation →
  test matrix; the single source of truth for what is implemented
- **[API](API.md)** and **[API_INTEGRATION](API_INTEGRATION.md)** — public API
  reference and integration workflow (lifetimes, ISR/PHY wiring, limitations)

## Integration

- **[PORTING](PORTING.md)** — platform, bare-IDE and RTOS porting contract
- **[STORAGE](STORAGE.md)** — application-owned persistence, the tag entries and Data Storage
- **[MEMORY_GUIDE](MEMORY_GUIDE.md)** — RAM/ROM sizing and workspace placement
- **[feature-trimming](feature-trimming.md)** — structural switches, runtime
  capability bits and capacity overrides
- **[hardware/TIOL112](hardware/TIOL112.md)** — portable TIOL112 transceiver driver

## Development

- **[CODE_FLOW](CODE_FLOW.md)** — main-loop/ISR call flow and ownership (中文)
- **[CODING_STANDARDS](CODING_STANDARDS.md)** — style, naming and quality gates
- **[TESTING](TESTING.md)** — unit, differential, matrix and virtual-master gates
- **[CONTRIBUTING](CONTRIBUTING.md)** — workflow and commit conventions
- **[MAINTENANCE](MAINTENANCE.md)** — branch model, releases and upstream adoption
- **[UPSTREAM_DIVERGENCE](UPSTREAM_DIVERGENCE.md)** — divergence inventory against
  `upstream/develop` (reference only, not a sync plan)

## Security and evidence

- **[security/THREAT_MODEL](security/THREAT_MODEL.md)** — STRIDE analysis aligned to
  the IO-Link Security Design and Development Guideline
- **[security/CRA](security/CRA.md)** — EU Cyber Resilience Act scope
- **[physical/BENCH_PROOF](physical/BENCH_PROOF.md)** — benchmark evidence

## Specification (source of truth)

- **[IO-Link Interface Spec V1.1.5](IOL-Interface-Spec_10002_V1.1.5_Oct2025/)** —
  **authoritative**. When code, comments or docs disagree with this PDF, the PDF
  wins.
- The Test Spec PDF in the same directory is the reference for the conformance
  matrix; the machine-readable per-case status is
  [`tests/conformance_audit.csv`](../tests/conformance_audit.csv).
