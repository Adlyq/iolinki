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


def mask_source(text: str, mask_literals: bool) -> str:
    """Blank comments (and optionally string/char literals), keeping offsets.

    An annotation that only exists in a comment or in a string is not a real
    annotation, so the scan runs on masked text. Every consumed character maps
    to one space (newlines are kept) so line numbers stay correct.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        c = text[i]
        if state == "block":
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                out.append("  ")
                i += 2
                state = "code"
            else:
                out.append("\n" if c == "\n" else " ")
                i += 1
            continue
        if state == "line":
            if c == "\n":
                out.append("\n")
                i += 1
                state = "code"
            else:
                out.append(" ")
                i += 1
            continue
        if state in {"string", "char"}:
            if c == "\\" and i + 1 < n:
                out.append("  ")
                i += 2
                continue
            if (state == "string" and c == '"') or (state == "char" and c == "'"):
                out.append(c if not mask_literals else " ")
                i += 1
                state = "code"
                continue
            out.append("\n" if c == "\n" else (" " if mask_literals else c))
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            out.append("  ")
            i += 2
            state = "block"
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            out.append("  ")
            i += 2
            state = "line"
            continue
        if c == '"':
            out.append('"')
            i += 1
            state = "string"
            continue
        if c == "'":
            out.append("'")
            i += 1
            state = "char"
            continue
        out.append(c)
        i += 1
    return "".join(out)


def mask_disabled_blocks(text: str) -> str:
    """Blank `#if 0` regions: code inside them is not compiled."""
    lines = text.splitlines(keepends=True)
    depth = 0
    in_disabled = False
    out: list[str] = []
    for line in lines:
        stripped = line.strip()
        if re.match(r"#\s*if\s+0\s*$", stripped):
            depth += 1
            in_disabled = True
            out.append("\n" if line.endswith("\n") else "")
            continue
        if re.match(r"#\s*if(n?def)?\b", stripped) or re.match(r"#\s*else\b", stripped):
            if in_disabled:
                depth += 1
            out.append("\n" if line.endswith("\n") else "")
            continue
        if re.match(r"#\s*endif\b", stripped):
            if in_disabled:
                depth -= 1
                if depth == 0:
                    in_disabled = False
            out.append("\n" if line.endswith("\n") else "")
            continue
        if in_disabled:
            out.append("\n" if line.endswith("\n") else "")
        else:
            out.append(line)
    return "".join(out)


def function_body(text: str, name: str) -> tuple[int, int, int] | None:
    """Return (start, end, line) of the first definition of `name`."""
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
        start = i + 1
        while start < len(text) and text[start] in " \t\r\n":
            start += 1
        if start < len(text) and text[start] == "{":
            depth = 0
            for k in range(start, len(text)):
                if text[k] == "{":
                    depth += 1
                elif text[k] == "}":
                    depth -= 1
                    if depth == 0:
                        line = text.count("\n", 0, match.start()) + 1
                        return start, k + 1, line
    return None


def check_c_evidence(row: dict[str, str]) -> None:
    """A C test row must name functions that exist and each carry the case id."""
    case_id = row["case_id"]
    path = ROOT / row["unit_test_file"]
    text = path.read_text(errors="replace")
    code = mask_disabled_blocks(mask_source(text, mask_literals=True))
    code_with_literals = mask_disabled_blocks(mask_source(text, mask_literals=False))
    marker = f'IOLINK_CONFORMANCE_CASE("{case_id}")'
    for name in (n.strip() for n in row["unit_test_function"].split(";") if n.strip()):
        span = function_body(code, name)
        if span is None:
            fail(f"{case_id}: {row['unit_test_file']} has no function {name!r}")
        start, end, line = span
        if marker not in code_with_literals[start:end]:
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
        text = mask_disabled_blocks(mask_source(path.read_text(errors="replace"),
                                                mask_literals=False))
        for match in re.finditer(r'IOLINK_CONFORMANCE_CASE\("SDCI_TC_([0-9]+)"\)', text):
            annotations.add(f"SDCI_TC_{match.group(1)}")
    unknown = sorted(annotations - set(ids))
    if unknown:
        fail("annotation(s) missing from audit CSV: " + ", ".join(unknown))

    print(f"PASS: {len(rows)} audit rows; {len(annotations)} annotated IDs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
