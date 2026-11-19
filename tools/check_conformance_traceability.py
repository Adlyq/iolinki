#!/usr/bin/env python3
"""Validate the conformance traceability matrix and test annotations."""
from __future__ import annotations

import contextlib
import csv
import io
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

# A clause label as the specification writes it: a table reference
# ("Table B.13"), an annex/table sub-clause ("A.1.6", "B.3", "C.1") or a
# numbered section ("10.10.2"). The checker does not parse the bundled PDF, so
# what it verifies is the label grammar plus the agreement between
# tests/spec_constants.csv and the generated table: the CSV is the only label
# registry, a new clause needs no edit here.
CLAUSE_LABEL_RE = re.compile(
    r"(?:Table\s+(?:[A-D]\.)?\d+(?:\.\d+)*|[A-D](?:\.\d+)+|\d+(?:\.\d+)+)"
)
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
        if not CLAUSE_LABEL_RE.fullmatch(clause):
            fail(
                f"spec_constants.csv:{index}: {symbol} uses clause {clause!r}, which is not a "
                "specification label ('Table N', 'Annex.section' or 'chapter.section')"
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


def spec_clauses(rows: list[dict[str, str]]) -> set[str]:
    """The clause labels the CSV uses; there is no second registry to maintain."""
    return {(row["clause"] or "").strip() for row in rows}


def spec_doc_section(text: str) -> str:
    match = re.search(r"^## Spec constants\s*$", text, re.MULTILINE)
    if match is None:
        fail("SPEC_CONFORMANCE.md has no '## Spec constants' section")
    rest = text[match.end() :]
    end = re.search(r"^## ", rest, re.MULTILINE)
    return rest if end is None else rest[: end.start()]


def spec_doc_clauses(text: str) -> set[str]:
    """Clause labels of the generated table in SPEC_CONFORMANCE.md."""
    section = spec_doc_section(text)
    clauses: set[str] = set()
    for line in section.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) < 2 or cells[0] in {"Clause", "---"} or set(cells[0]) <= {"-", ":"}:
            continue
        clause = cells[0].strip("` ")
        if not CLAUSE_LABEL_RE.fullmatch(clause):
            fail(f"SPEC_CONFORMANCE.md spec table uses unknown clause {clause!r}")
        clauses.add(clause)
    if not clauses:
        fail("SPEC_CONFORMANCE.md spec table has no clause rows")
    return clauses


def check_spec_doc(
    text: str, symbols: set[str], csv_clauses: set[str], doc_clauses: set[str]
) -> None:
    section = spec_doc_section(text)
    documented: set[str] = set()
    for line in section.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) < 2 or cells[0] in {"Clause", "---"} or set(cells[0]) <= {"-", ":"}:
            continue
        documented.update(t for t in TICK_RE.findall(line) if SPEC_SYMBOL_RE.match(t))

    # The doc table is generated from the CSV, so a label the CSV does not use
    # is a real error: the row would document a constant the data source never
    # declares. The reverse direction keeps a CSV-only clause visible.
    unknown = sorted(doc_clauses - csv_clauses)
    if unknown:
        fail(
            "SPEC_CONFORMANCE.md spec table uses unknown clause(s) "
            + ", ".join(unknown)
            + ": add the constants to tests/spec_constants.csv"
        )
    missing_clauses = sorted(csv_clauses - doc_clauses)
    if missing_clauses:
        fail(
            "clause(s) in spec_constants.csv missing from the SPEC_CONFORMANCE.md table: "
            + ", ".join(missing_clauses)
        )

    # Documented constants must exist in the CSV, and the CSV must be fully
    # documented: a new row without a visible clause entry fails here.
    unknown = sorted(documented - symbols)
    if unknown:
        fail(
            "SPEC_CONFORMANCE.md names constants missing from spec_constants.csv: "
            + ", ".join(unknown)
        )
    undocumented = sorted(symbols - documented)
    if undocumented:
        fail(
            "spec_constants.csv symbols missing from SPEC_CONFORMANCE.md: "
            + ", ".join(undocumented)
        )


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


def selftest() -> int:
    """Exercise clause derivation and the doc-table checks on synthetic input."""
    failures: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    def rejected(action) -> bool:
        sink = io.StringIO()
        try:
            with contextlib.redirect_stderr(sink):
                action()
        except SystemExit:
            return True
        return False

    for label in (
        "Table 10",
        "Table B.13",
        "Table A.12",
        "A.1.6",
        "B.3",
        "C.1",
        "10.10.2",
        "7.3.3.2",
    ):
        expect(bool(CLAUSE_LABEL_RE.fullmatch(label)), f"clause label {label!r} was rejected")
    for label in ("Table", "Table Q.1", "Table A.", "A.", "10.", "7.3.3.2a", ""):
        expect(CLAUSE_LABEL_RE.fullmatch(label) is None, f"clause label {label!r} was accepted")

    def rows_of(*pairs: tuple[str, str]) -> list[dict[str, str]]:
        return [
            {"clause": clause, "symbol": symbol, "expected": "1", "note": "fixture"}
            for clause, symbol in pairs
        ]

    csv_rows = rows_of(("A.1.6", "IOLINK_FIXTURE_A"))
    expect(spec_clauses(csv_rows) == {"A.1.6"}, "spec_clauses does not mirror the CSV column")

    # The CSV is the registry: a new, syntactically valid label needs no code
    # change. Accuracy against the PDF is a review duty, not a machine check.
    new_rows = rows_of(("Table 99", "IOLINK_FIXTURE_NEW"))
    expect(
        not rejected(lambda: check_spec_csv(new_rows)),
        "a clause label present in the CSV was rejected",
    )

    def doc_with(pairs: tuple[tuple[str, str], ...]) -> str:
        lines = [
            "# Fixture",
            "",
            "## Spec constants",
            "",
            "| Clause | Constants |",
            "|---|:---|",
        ]
        lines += [f"| `{clause}` | `{symbol}` |" for clause, symbol in pairs]
        lines += ["", "## Next", ""]
        return "\n".join(lines)

    good = doc_with((("A.1.6", "IOLINK_FIXTURE_A"),))
    doc_clauses = spec_doc_clauses(good)
    expect(doc_clauses == {"A.1.6"}, "spec_doc_clauses does not read the table")
    expect(
        not rejected(
            lambda: check_spec_doc(good, {"IOLINK_FIXTURE_A"}, {"A.1.6"}, doc_clauses)
        ),
        "a consistent CSV and table was rejected",
    )

    unknown_clause = doc_with((("Z.9", "IOLINK_FIXTURE_A"),))
    expect(
        rejected(
            lambda: check_spec_doc(
                unknown_clause,
                {"IOLINK_FIXTURE_A"},
                {"A.1.6"},
                spec_doc_clauses(unknown_clause),
            )
        ),
        "a doc-table clause absent from the CSV was accepted",
    )
    expect(
        rejected(
            lambda: check_spec_doc(good, {"IOLINK_FIXTURE_A"}, {"A.1.6", "B.3"}, doc_clauses)
        ),
        "a CSV clause missing from the doc table was accepted",
    )
    unknown_symbol = doc_with((("A.1.6", "IOLINK_FIXTURE_B"),))
    expect(
        rejected(
            lambda: check_spec_doc(
                unknown_symbol,
                {"IOLINK_FIXTURE_A"},
                {"A.1.6"},
                spec_doc_clauses(unknown_symbol),
            )
        ),
        "a doc-table symbol absent from the CSV was accepted",
    )
    expect(
        rejected(lambda: check_spec_csv(rows_of(("Table", "IOLINK_FIXTURE_A")))),
        "a malformed clause label was accepted",
    )
    expect(
        rejected(
            lambda: check_spec_csv(
                rows_of(("A.1.6", "IOLINK_FIXTURE_A"), ("A.1.6", "IOLINK_FIXTURE_A"))
            )
        ),
        "a duplicate symbol was accepted",
    )

    if failures:
        for failure in failures:
            print(f"traceability selftest FAIL: {failure}", file=sys.stderr)
        return 1
    print("traceability selftest: OK (clause derivation and table checks)")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["--selftest"]:
        return selftest()
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
    doc_text = DOC.read_text(errors="replace")
    csv_clauses = spec_clauses(spec_rows)
    doc_clauses = spec_doc_clauses(doc_text)
    spec_symbols = check_spec_csv(spec_rows)
    check_spec_doc(doc_text, spec_symbols, csv_clauses, doc_clauses)
    check_doc_case_ids(doc_text, {r["case_id"] for r in rows}, spec_symbols)
    check_direct_visibility(rows)

    if not spec_only:
        print(f"PASS: {len(rows)} audit rows; {annotations} annotated IDs")
    print(f"PASS: {len(spec_rows)} spec constants; clause labels derived from the CSV; "
          "doc case IDs and direct rows visible")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
