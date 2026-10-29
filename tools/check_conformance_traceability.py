#!/usr/bin/env python3
"""Validate the conformance traceability matrix and test annotations."""
from __future__ import annotations

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CSV = ROOT / "tests" / "conformance_audit.csv"
VALID_STATUS = {"direct", "n/a", "out-of-scope"}
VALID_SCOPE = {
    "library-core",
    "library-optional",
    "reference-integration",
    "product-integration",
    "not-applicable",
    "external-hil",
    "external-physical",
    "external-iodd",
}
VALID_RESIDUAL = {
    "none",
    "external-timing",
    "external-reset",
    "external-real-reset",
    "external-power",
    "external-physical",
    "external-product-iodd",
}


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    rows = list(csv.DictReader(CSV.open()))
    if not rows:
        fail("empty audit CSV")
    ids = [r["case_id"] for r in rows]
    if len(ids) != len(set(ids)):
        fail("duplicate case_id in audit CSV")

    for r in rows:
        cid = r["case_id"]
        if r["status"] not in VALID_STATUS:
            fail(f"{cid}: invalid status {r['status']!r}")
        if r["scope"] not in VALID_SCOPE:
            fail(f"{cid}: invalid scope {r['scope']!r}")
        if r["residual"] not in VALID_RESIDUAL:
            fail(f"{cid}: invalid residual {r['residual']!r}")
        if r["status"] == "direct":
            if not r["unit_test_file"] or not r["unit_test_function"]:
                fail(f"{cid}: direct row lacks test file/function")
            if not (ROOT / r["unit_test_file"]).exists():
                fail(f"{cid}: test file missing: {r['unit_test_file']}")
        if r["status"] == "out-of-scope" and r["residual"] == "none":
            fail(f"{cid}: out-of-scope row needs a residual reason")
        if r["status"] in {"partial", "gap", "known-gap"}:
            fail(f"{cid}: in-scope work is not closed ({r['status']})")

    annotations = set()
    for path in (ROOT / "tests").glob("*.c"):
        for match in re.finditer(r'IOLINK_CONFORMANCE_CASE\("SDCI_TC_([0-9]+)"\)',
                                 path.read_text(errors="replace")):
            annotations.add(f"SDCI_TC_{match.group(1)}")
    unknown = sorted(annotations - set(ids))
    if unknown:
        fail("annotation(s) missing from audit CSV: " + ", ".join(unknown))

    print(f"PASS: {len(rows)} audit rows; {len(annotations)} annotated IDs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
