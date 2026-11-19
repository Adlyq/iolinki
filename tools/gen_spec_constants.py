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
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
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
    if not csv_path.is_file():
        raise GenError(f"{CSV.as_posix()} is missing")
    with csv_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != FIELDS:
            raise GenError(
                f"{CSV.as_posix()}: header must be {','.join(FIELDS)} "
                f"(got {','.join(reader.fieldnames or [])})"
            )
        symbols: list[str] = []
        for line_number, row in enumerate(reader, start=2):
            symbol = (row["symbol"] or "").strip()
            if not symbol:
                raise GenError(f"{CSV.as_posix()}:{line_number}: empty symbol")
            if not SYMBOL_RE.fullmatch(symbol):
                raise GenError(
                    f"{CSV.as_posix()}:{line_number}: {symbol!r} is not an IOLINK_ macro name"
                )
            if symbol in symbols:
                raise GenError(f"{CSV.as_posix()}:{line_number}: duplicate symbol {symbol}")
            symbols.append(symbol)
    if not symbols:
        raise GenError(f"{CSV.as_posix()}: no data rows")
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


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="verify the committed header; write nothing"
    )
    args = parser.parse_args(argv)

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
