#!/usr/bin/env python3
"""File-size gate for the product sources.

A translation unit that grows past a screenful per function is fine; a
translation unit that reaches 800 lines of code is not. This gate counts
*code* lines -- blank lines and comments are removed first, string and
character literals are preserved -- so that heavy documentation does not
inflate a file and comment-only padding does not hide one.

Scope: src/ (the library), include/ (the installed headers) and examples/
(the shipped applications). Tests and tools are not product code and are out
of scope.

Usage:
    tools/check_file_size.py            # gate (exit 1 on an oversized file)
    tools/check_file_size.py --list     # print every file, largest first
    tools/check_file_size.py --selftest # verify the counter itself
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAX_CODE_LINES = 800

SCOPE = [
    (ROOT / "src", (".c", ".h")),
    (ROOT / "include", (".h",)),
    (ROOT / "examples", (".c", ".h")),
]


def strip_comments(text: str) -> str:
    """Remove C comments, keeping line structure and literal contents."""
    if text.startswith("\ufeff"):
        text = text[1:]
    out: list[str] = []
    index = 0
    length = len(text)
    state = None  # None | "line" | "block" | "string" | "char"

    while index < length:
        char = text[index]
        following = text[index + 1] if index + 1 < length else ""

        if state is None:
            if char == "/" and following == "/":
                state = "line"
                index += 2
                continue
            if char == "/" and following == "*":
                state = "block"
                index += 2
                out.append(" ")
                continue
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            out.append(char)
            index += 1
            continue

        if state == "line":
            if char == "\\" and following == "\n":
                # Line splicing happens before comment removal: the comment
                # continues on the next physical line.
                index += 2
                continue
            if char == "\n":
                state = None
                out.append(char)
            index += 1
            continue

        if state == "block":
            if char == "*" and following == "/":
                state = None
                index += 2
                continue
            if char == "\n":
                out.append(char)
            index += 1
            continue

        # Inside a string or character literal: a comment marker is data.
        out.append(char)
        if char == "\\":
            if index + 1 < length:
                out.append(text[index + 1])
                index += 2
                continue
        elif (state == "string" and char == '"') or (state == "char" and char == "'"):
            state = None
        index += 1

    return "".join(out)


def count_code_lines(text: str) -> int:
    body = strip_comments(text)
    return sum(1 for line in body.split("\n") if line.strip())


class FileSizeError(Exception):
    """A source file could not be read; the message says which."""


def read_source(path: Path) -> str:
    """Read a source file; a BOM must not turn the first line into code."""
    try:
        return path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError as exc:
        raise FileSizeError(f"cannot read {path}: {exc}") from exc


def scan() -> list[tuple[int, Path]]:
    rows: list[tuple[int, Path]] = []
    for base, suffixes in SCOPE:
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix.lower() in suffixes:
                rows.append((count_code_lines(read_source(path)), path))
    rows.sort(key=lambda row: (-row[0], str(row[1])))
    return rows


def selftest() -> int:
    cases = [
        ("empty", "", 0),
        ("blank lines only", "\n\n  \n\t\n", 0),
        ("comment only", "/* a\n b\n c */\n", 0),
        ("line comment only", "// a\n// b\n", 0),
        ("code with comment", "int a; /* x */\nint b; // y\n", 2),
        ("comment marker in string", 'const char* s = "/* not a comment";\n', 1),
        ("line marker in char", "char c = '/'; int x; // tail\n", 1),
        ("block comment spans code line", "int a; /* x\ny */ int b;\n", 2),
        ("escaped quote", 'const char* s = "a\\"b"; int x;\n', 1),
        ("preprocessor counts", "#define A 1\n#undef A\n", 2),
        ("utf-8 bom", "\ufeff/* a\n b\n */\nint a;\n", 1),
        ("line continuation in comment", "// a \\\nint b;\n", 0),
        ("escaped char quote", "char c = '\\''; int x;\n", 1),
        ("unclosed block comment", "int a; /* open\nint b;\n", 1),
        ("ascii art in comment", "/**\n * +--+\n * |  |\n * +--+\n */\nint a;\n", 1),
    ]
    failures = 0
    for name, source, expected in cases:
        actual = count_code_lines(source)
        if actual != expected:
            failures += 1
            print(f"selftest FAIL: {name}: expected {expected}, got {actual}", file=sys.stderr)
    if failures:
        return 1
    print(f"file-size selftest: OK ({len(cases)} cases)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="print every file, largest first")
    parser.add_argument("--selftest", action="store_true", help="verify the counter itself")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    try:
        rows = scan()
    except FileSizeError as exc:
        print(f"file-size: {exc}", file=sys.stderr)
        return 1
    if not rows:
        print("file-size: no source files found", file=sys.stderr)
        return 1

    oversized = [(count, path) for count, path in rows if count >= MAX_CODE_LINES]
    if args.list:
        for count, path in rows:
            print(f"{count:6d}  {path.relative_to(ROOT)}")
        return 0
    if oversized:
        for count, path in oversized:
            print(
                f"file-size: {path.relative_to(ROOT)} has {count} code lines "
                f"(limit {MAX_CODE_LINES}); split it by responsibility",
                file=sys.stderr,
            )
        return 1

    largest_count, largest_path = rows[0]
    print(
        f"file-size: OK ({len(rows)} files, largest {largest_path.relative_to(ROOT)} "
        f"at {largest_count} < {MAX_CODE_LINES} code lines)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
