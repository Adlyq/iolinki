#!/usr/bin/env python3
"""Validate the conformance traceability matrix and test annotations."""
from __future__ import annotations

import csv
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CSV = ROOT / "tests" / "conformance_audit.csv"
SPEC_CSV = ROOT / "tests" / "spec_constants.csv"
DOC = ROOT / "docs" / "SPEC_CONFORMANCE.md"
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

# Clause labels a spec_constants.csv row may use. This is a hand-maintained
# list of labels that exist in the bundled V1.1.5 PDF; the checker deliberately
# does not parse the PDF, so a new label is an explicit edit here.
SPEC_CLAUSES = {
    "A.1.2",
    "A.1.3",
    "A.1.6",
    "A.3.3",
    "A.3.4",
    "A.3.5",
    "A.3.6",
    "A.6.4",
    "7.3.3.2",
    "7.3.6.2",
    "10.7.5",
    "10.10.2",
    "B.1.5",
    "B.2.22",
    "B.3",
    "B.8",
    "B.9",
    "B.13",
    "C.1",
    "D.1",
    "Table 10",
    "Table 42",
    "Table 43",
    "Table 47",
    "Table 52",
    "Table A.1",
    "Table A.3",
    "Table A.6",
    "Table A.10",
    "Table A.12",
    "Table B.1",
    "Table B.2",
    "Table B.3",
    "Table B.8",
    "Table B.9",
    "Table B.12",
    "Table B.13",
    "Table C.1",
}
SPEC_FIELDS = ["clause", "symbol", "expected", "note"]
CASE_ID_RE = re.compile(r"\b(SDCI_TC_\d+|TC_[A-Za-z][A-Za-z0-9_]*)\b")
SPEC_SYMBOL_RE = re.compile(r"^IOLINK_[A-Z0-9_]+$")
TICK_RE = re.compile(r"`([^`]+)`")


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


def parse_expected(text: str) -> int:
    """Parse a C integer literal: decimal/0x hex, optional u/l suffix."""
    value = text.strip()
    if not value:
        raise ValueError("empty")
    stripped = value.rstrip("uUlL")
    if not stripped:
        raise ValueError("no digits")
    return int(stripped, 0)


def load_spec_rows() -> list[dict[str, str]]:
    if not SPEC_CSV.exists():
        fail(f"missing spec constants CSV: {SPEC_CSV.relative_to(ROOT)}")
    with SPEC_CSV.open(newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != SPEC_FIELDS:
            fail(
                "spec_constants.csv header must be "
                f"{','.join(SPEC_FIELDS)} (got {','.join(reader.fieldnames or [])})"
            )
        rows = list(reader)
    if not rows:
        fail("spec_constants.csv has no data rows")
    return rows


def check_spec_csv(rows: list[dict[str, str]]) -> set[str]:
    symbols: set[str] = set()
    for index, row in enumerate(rows, start=2):
        clause = (row["clause"] or "").strip()
        symbol = (row["symbol"] or "").strip()
        expected = (row["expected"] or "").strip()
        note = (row["note"] or "").strip()
        if not clause or not symbol or not expected or not note:
            fail(f"spec_constants.csv:{index}: empty field")
        if clause not in SPEC_CLAUSES:
            fail(
                f"spec_constants.csv:{index}: {symbol} uses clause {clause!r}, "
                "which is not in the hand-maintained clause list"
            )
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", symbol):
            fail(f"spec_constants.csv:{index}: {symbol!r} is not a C symbol name")
        if symbol in symbols:
            fail(f"spec_constants.csv:{index}: duplicate symbol {symbol}")
        try:
            parse_expected(expected)
        except ValueError:
            fail(f"spec_constants.csv:{index}: {symbol} has invalid expected value {expected!r}")
        symbols.add(symbol)
    return symbols


def spec_doc_section(text: str) -> str:
    match = re.search(r"^## Spec constants\s*$", text, re.MULTILINE)
    if match is None:
        fail("SPEC_CONFORMANCE.md has no '## Spec constants' section")
    rest = text[match.end():]
    end = re.search(r"^## ", rest, re.MULTILINE)
    return rest if end is None else rest[: end.start()]


def check_spec_doc(symbols: set[str]) -> None:
    doc = DOC.read_text(errors="replace")
    section = spec_doc_section(doc)

    documented: set[str] = set()
    clause_rows = 0
    for line in section.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) < 2 or cells[0] in {"Clause", "---"} or set(cells[0]) <= {"-", ":"}:
            continue
        clause = cells[0].strip("` ")
        if clause not in SPEC_CLAUSES:
            fail(f"SPEC_CONFORMANCE.md spec table uses unknown clause {clause!r}")
        clause_rows += 1
        documented.update(t for t in TICK_RE.findall(line) if SPEC_SYMBOL_RE.match(t))
    if clause_rows == 0:
        fail("SPEC_CONFORMANCE.md spec table has no clause rows")

    # Documented constants must exist in the CSV, and the CSV must be fully
    # documented: a new row without a visible clause entry fails here.
    unknown = sorted(documented - symbols)
    if unknown:
        fail("SPEC_CONFORMANCE.md names constants missing from spec_constants.csv: "
             + ", ".join(unknown))
    undocumented = sorted(symbols - documented)
    if undocumented:
        fail("spec_constants.csv symbols missing from SPEC_CONFORMANCE.md: "
             + ", ".join(undocumented))


def check_doc_case_ids(doc: str, audit_ids: set[str], spec_symbols: set[str]) -> None:
    known = audit_ids | spec_symbols
    unknown = sorted(set(CASE_ID_RE.findall(doc)) - known)
    if unknown:
        fail("case id(s) quoted in SPEC_CONFORMANCE.md but absent from "
             "conformance_audit.csv/spec_constants.csv: " + ", ".join(unknown))


def check_direct_visibility(rows: list[dict[str, str]]) -> None:
    doc = DOC.read_text(errors="replace")
    missing: list[str] = []
    for row in rows:
        if row["status"] != "direct":
            continue
        target = os.path.splitext(os.path.basename(row["unit_test_file"]))[0]
        evidence = {
            row["case_id"],
            row["unit_test_file"],
            row["unit_test_function"],
            target,
        }
        if not any(item and item in doc for item in evidence):
            missing.append(row["case_id"])
    if missing:
        fail(
            "direct row(s) have no test annotation and no SPEC_CONFORMANCE.md "
            "visibility: " + ", ".join(sorted(missing))
        )


def check_audit(rows: list[dict[str, str]]) -> int:
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
    return len(annotations)


def main(argv: list[str]) -> int:
    unknown_args = [a for a in argv if a not in {"--spec-constants"}]
    if unknown_args:
        print(f"FAIL: unknown argument(s): {' '.join(unknown_args)}", file=sys.stderr)
        return 2
    spec_only = "--spec-constants" in argv

    rows = list(csv.DictReader(CSV.open()))
    if not rows:
        fail("empty audit CSV")

    if not spec_only:
        annotations = check_audit(rows)
    else:
        annotations = 0

    spec_rows = load_spec_rows()
    spec_symbols = check_spec_csv(spec_rows)
    check_spec_doc(spec_symbols)
    check_doc_case_ids(DOC.read_text(errors="replace"), {r["case_id"] for r in rows},
                       spec_symbols)
    check_direct_visibility(rows)

    if not spec_only:
        print(f"PASS: {len(rows)} audit rows; {annotations} annotated IDs")
    print(f"PASS: {len(spec_rows)} spec constants; clause labels checked; "
          "doc case IDs and direct rows visible")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
