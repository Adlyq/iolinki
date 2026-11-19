#!/usr/bin/env python3
"""Generate the SPEC_ENTRY symbol list of tests/test_spec_constants.c.

``tests/spec_constants.csv`` is the data source and ``include/iolinki/protocol.h``
the implementation. The C test has to name every CSV symbol so it can compare
the macro's value with the CSV ``expected`` column, and that list used to be a
third hand-copied source. This script emits the symbols only: the generated
header carries no values, so the CSV and the macros stay independent -- a wrong
value still fails the runtime comparison, while a row whose macro does not
exist fails to compile.

The header is committed (``tests/gen/spec_constants_table.h``) so configuring
the test suite does not need Python; ``--check`` is the gate that keeps it in
sync and runs in ``tools/gates.sh docs``. It is included inside the ``k_spec[]``
initializer, so every entry ends with a comma.

Usage:
    tools/gen_spec_constants.py            # rewrite tests/gen/spec_constants_table.h
    tools/gen_spec_constants.py --check    # fail if the committed header is stale
    tools/gen_spec_constants.py --selftest # exercise the parser on throwaway data
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CSV = Path("tests") / "spec_constants.csv"
TABLE = Path("tests") / "gen" / "spec_constants_table.h"
FIELDS = ["clause", "symbol", "expected", "note"]
SYMBOL_RE = re.compile(r"IOLINK_[A-Z0-9_]+")
HEADER = """/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AUTO-GENERATED from tests/spec_constants.csv by tools/gen_spec_constants.py
 * -- do not edit. Source of truth: tests/spec_constants.csv (symbols only; the
 * expected values live in include/iolinki/protocol.h and the CSV).
 */"""


class GenError(Exception):
    """The CSV cannot produce a symbol list; the message says why."""


def read_symbols(csv_path: Path) -> list[str]:
    """Return the symbols of one CSV, skipping blank and ``#`` comment lines."""
    name = csv_path.as_posix()
    if not csv_path.is_file():
        raise GenError(f"{name} is missing")
    data_lines = [
        line
        for line in csv_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if not data_lines:
        raise GenError(f"{name}: no data rows")
    reader = csv.DictReader(data_lines)
    if reader.fieldnames != FIELDS:
        raise GenError(
            f"{name}: header must be {','.join(FIELDS)} "
            f"(got {','.join(reader.fieldnames or [])})"
        )
    symbols: list[str] = []
    for line_number, row in enumerate(reader, start=2):
        if any(value is None for value in row.values()):
            raise GenError(f"{name}:{line_number}: expected {len(FIELDS)} columns")
        symbol = (row["symbol"] or "").strip()
        if not symbol:
            raise GenError(f"{name}:{line_number}: empty symbol")
        if not SYMBOL_RE.fullmatch(symbol):
            raise GenError(f"{name}:{line_number}: {symbol!r} is not an IOLINK_ macro name")
        if symbol in symbols:
            raise GenError(f"{name}:{line_number}: duplicate symbol {symbol}")
        symbols.append(symbol)
    if not symbols:
        raise GenError(f"{name}: no data rows")
    return symbols


def render(symbols: list[str]) -> str:
    lines = [
        HEADER,
        "",
        "/* One symbol per line, so a CSV edit changes exactly one line of this file. */",
        "// clang-format off",
    ]
    lines += [f"SPEC_ENTRY({symbol})," for symbol in symbols]
    return "\n".join(lines) + "\n"


def selftest() -> int:
    """Exercise every rejection path on throwaway data."""
    cases = {
        "valid": (
            "clause,symbol,expected,note\n"
            "Table 47,IOLINK_OK,1,a\n"
            "# a comment line\n"
            "\n"
            "Table 47,IOLINK_TWO,2,b\n",
            2,
        ),
        "missing_column": ("clause,symbol,expected,note\nTable 47,IOLINK_OK,1\n", None),
        "duplicate": (
            "clause,symbol,expected,note\n"
            "Table 47,IOLINK_OK,1,a\n"
            "Table 47,IOLINK_OK,2,b\n",
            None,
        ),
        "bad_prefix": ("clause,symbol,expected,note\nTable 47,NOT_A_MACRO,1,a\n", None),
        "empty_symbol": ("clause,symbol,expected,note\nTable 47,,1,a\n", None),
        "header": ("clause,name,expected,note\nTable 47,IOLINK_OK,1,a\n", None),
        "no_rows": ("clause,symbol,expected,note\n", None),
    }
    with tempfile.TemporaryDirectory() as tmp:
        for label, (content, expected) in cases.items():
            path = Path(tmp) / f"{label}.csv"
            path.write_text(content, encoding="utf-8")
            try:
                symbols = read_symbols(path)
            except GenError as exc:
                if expected is not None:
                    print(f"gen-spec-constants selftest: {label} was rejected: {exc}", file=sys.stderr)
                    return 1
                continue
            if expected is None or len(symbols) != expected:
                print(f"gen-spec-constants selftest: {label} did not behave as expected", file=sys.stderr)
                return 1
            if render(symbols).count("SPEC_ENTRY(") != expected:
                print(f"gen-spec-constants selftest: {label} rendered the wrong count", file=sys.stderr)
                return 1
        missing = Path(tmp) / "missing.csv"
        try:
            read_symbols(missing)
        except GenError:
            pass
        else:
            print("gen-spec-constants selftest: a missing CSV was accepted", file=sys.stderr)
            return 1
    print("gen-spec-constants selftest: OK (accepted/rejected as expected)")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="verify the committed header; write nothing"
    )
    parser.add_argument("--selftest", action="store_true", help="exercise the parser on temp data")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    table = ROOT / TABLE
    try:
        content = render(read_symbols(ROOT / CSV))
    except GenError as exc:
        print(f"gen-spec-constants: {exc}", file=sys.stderr)
        return 1

    if args.check:
        try:
            current = table.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"gen-spec-constants: cannot read {TABLE.as_posix()}: {exc}", file=sys.stderr)
            return 1
        if current != content:
            print(
                f"gen-spec-constants: {TABLE.as_posix()} is stale; "
                "rerun tools/gen_spec_constants.py",
                file=sys.stderr,
            )
            return 1
        print(f"gen-spec-constants: OK ({TABLE.as_posix()} up to date)")
        return 0

    table.parent.mkdir(parents=True, exist_ok=True)
    try:
        if table.read_text(encoding="utf-8") == content:
            print(f"gen-spec-constants: {TABLE.as_posix()} up to date")
            return 0
    except OSError:
        pass
    table.write_text(content, encoding="utf-8")
    print(f"gen-spec-constants: wrote {TABLE.as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
