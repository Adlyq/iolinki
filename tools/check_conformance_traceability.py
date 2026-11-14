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


def skip_ws_and_comments(text: str, index: int) -> int:
    """Skip whitespace and comments starting at `index`."""
    while index < len(text):
        if text[index] in " \t\r\n":
            index += 1
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                return len(text)
            index = end + 2
        elif text.startswith("//", index):
            end = text.find("\n", index + 2)
            if end < 0:
                return len(text)
            index = end + 1
        else:
            break
    return index


def function_body(text: str, name: str) -> tuple[str | None, int]:
    """Return (body, line) of the first definition of `name`."""
    for match in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        i = match.end() - 1
        depth = 0
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        start = skip_ws_and_comments(text, i + 1)
        if start < len(text) and text[start] == "{":
            depth = 0
            for k in range(start, len(text)):
                if text[k] == "{":
                    depth += 1
                elif text[k] == "}":
                    depth -= 1
                    if depth == 0:
                        line = text.count("\n", 0, match.start()) + 1
                        return text[start : k + 1], line
    return None, 0


def check_c_evidence(row: dict[str, str]) -> None:
    """A C test row must name functions that exist and each carry the case id."""
    case_id = row["case_id"]
    path = ROOT / row["unit_test_file"]
    text = path.read_text(errors="replace")
    marker = f'IOLINK_CONFORMANCE_CASE("{case_id}")'
    for name in (n.strip() for n in row["unit_test_function"].split(";") if n.strip()):
        body, line = function_body(text, name)
        if body is None:
            where = f"{row['unit_test_file']}:{line}" if line else row["unit_test_file"]
            fail(f"{case_id}: {where} has no function {name!r}")
        if marker not in body:
            fail(
                f"{case_id}: {row['unit_test_file']}:{line} function {name!r} "
                f"does not carry {marker}"
            )


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
            # C tests must point at a function that really carries the case ID.
            # Python/HIL evidence has no annotation macro; the file check above
            # is all that can be verified there.
            if r["unit_test_file"].startswith("tests/"):
                check_c_evidence(r)
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
