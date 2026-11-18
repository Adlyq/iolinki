#!/usr/bin/env python3
"""Find struct fields that production code never uses.

The gate parses the field lists of the DLL state structs (``dll_shared.h``,
``dll_main.h``, ``dll_isr.h``, ``dll_ctx.h``) and the device structs
(``device.h``), then counts references in ``src/``, ``include/`` and ``tests/``.
A field is reported when it falls into one of three classes:

  * ``no-use``     -- no reference outside its own declaration;
  * ``write-only`` -- written, read by nobody;
  * ``test-only``  -- read only by tests, so production cannot observe it.

References are counted per *field name* on comment- and literal-masked source.
That deliberately over-counts when two structs share a field name (``state``):
a shared name hides a dead field instead of inventing one. Writes are detected
only through direct assignment/compound-assignment/increment on the name; a
write performed through a pointer (``memcpy(&s.field, ...)``) is counted as a
read, again erring on the quiet side.

A separate, non-allowlistable check enforces the ownership prefix: every
``iolink_dll_shared_t`` field must have a row in ``tools/shared_fields.csv``
and its name must start with that row's category (``c_``/``m_``/``i_``/``b_``).
A missing row or a mismatched prefix fails the gate; there is no allowlist for
it, because the prefix is the classification made visible at every use site.

Every finding must be covered by ``tools/dead_fields_allowlist.txt`` with a
``# reason``; an entry that no longer matches a finding is a hard failure, so
the list cannot quietly outlive the code it excuses.

Usage:
    tools/check_dead_fields.py               # report-only (exit 0)
    tools/check_dead_fields.py --enforce     # exit 1 on an unexcused finding
    tools/check_dead_fields.py --selftest    # verify the gate on fixtures
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECLARING_HEADERS = (
    "include/iolinki/internal/dll_shared.h",
    "include/iolinki/internal/dll_main.h",
    "include/iolinki/internal/dll_isr.h",
    "include/iolinki/internal/dll_ctx.h",
    "include/iolinki/device.h",
)
SCAN_TREES = ("src", "include", "tests")
ALLOWLIST = Path("tools/dead_fields_allowlist.txt")
CLASSES = ("no-use", "write-only", "test-only")
FIXTURE = Path("tools/fixtures/gates/dead_fields")

STRUCT_OPEN_RE = re.compile(r"(?:typedef\s+)?struct\s*([A-Za-z_]\w*)?\s*\{")
TAG_ALIAS_RE = re.compile(r"typedef\s+struct\s+([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*;")
TRAILING_ALIAS_RE = re.compile(r"\s*([A-Za-z_]\w*)\s*;")
FUNCTION_POINTER_RE = re.compile(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)")
IDENT_RE = re.compile(r"[A-Za-z_]\w*")


class DeadFieldError(Exception):
    """The input tree cannot be analysed; the message says why."""


@dataclass(frozen=True)
class FieldDecl:
    struct: str
    field: str
    path: str
    offset: int


@dataclass(frozen=True)
class Finding:
    struct: str
    field: str
    kind: str
    detail: str

    @property
    def key(self) -> str:
        return f"{self.struct}.{self.field}"


def mask_source(text: str) -> str:
    """Blank comments and string/char literals, preserving offsets and lines."""
    out: list[str] = []
    index = 0
    length = len(text)
    state = "code"
    while index < length:
        char = text[index]
        following = text[index + 1] if index + 1 < length else ""
        if state == "block":
            if char == "*" and following == "/":
                out.append("  ")
                index += 2
                state = "code"
                continue
            out.append("\n" if char == "\n" else " ")
            index += 1
            continue
        if state == "line":
            if char == "\n":
                out.append("\n")
                index += 1
                state = "code"
            else:
                out.append(" ")
                index += 1
            continue
        if state in {"string", "char"}:
            if char == "\\" and index + 1 < length:
                out.append("\n" if char == "\n" else " ")
                out.append("\n" if text[index + 1] == "\n" else " ")
                index += 2
                continue
            if (state == "string" and char == '"') or (state == "char" and char == "'"):
                out.append(" ")
                index += 1
                state = "code"
                continue
            out.append("\n" if char == "\n" else " ")
            index += 1
            continue
        if char == "/" and following == "*":
            out.append("  ")
            index += 2
            state = "block"
            continue
        if char == "/" and following == "/":
            out.append("  ")
            index += 2
            state = "line"
            continue
        if char in {'"', "'"}:
            out.append(" ")
            index += 1
            state = "string" if char == '"' else "char"
            continue
        out.append(char)
        index += 1
    return "".join(out)


def matching_brace(text: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise DeadFieldError(f"unbalanced brace at offset {open_index}")


def parse_struct_fields(
    text: str, path: str, aliases: dict[str, str] | None = None
) -> list[FieldDecl]:
    """Return every field of every struct definition in one header."""
    masked = mask_source(text)
    local_aliases = dict(aliases or {})
    local_aliases.update({tag: alias for tag, alias in TAG_ALIAS_RE.findall(masked)})
    fields: list[FieldDecl] = []
    for match in STRUCT_OPEN_RE.finditer(masked):
        tag = match.group(1)
        close = matching_brace(masked, match.end() - 1)
        body_start = match.end()
        body = masked[body_start:close]
        trailing = TRAILING_ALIAS_RE.match(masked, close + 1)
        name = None
        if trailing is not None:
            name = trailing.group(1)
        elif tag:
            name = local_aliases.get(tag, tag)
        if not name:
            name = tag
        if not name:
            continue
        for statement in split_statements(body):
            field, offset = statement_field(statement)
            if field is None:
                continue
            fields.append(FieldDecl(name, field, path, body_start + offset))
    return fields


def split_statements(body: str) -> list[tuple[int, str]]:
    statements: list[tuple[int, str]] = []
    start = 0
    depth = 0
    for index, char in enumerate(body):
        if char == "{":
            depth += 1
        elif char == "}":
            depth = max(0, depth - 1)
        elif char == ";" and depth == 0:
            statements.append((start, body[start:index]))
            start = index + 1
    return statements


def statement_field(statement: tuple[int, str]) -> tuple[str | None, int]:
    start, text = statement
    # Keep offsets stable: blank preprocessor lines and bracket groups with
    # spaces instead of deleting them.
    lines = [
        " " * len(line) if line.lstrip().startswith("#") else line
        for line in text.split("\n")
    ]
    code = "\n".join(lines)
    pointer = FUNCTION_POINTER_RE.search(code)
    if pointer is not None:
        return pointer.group(1), start + pointer.start(1)
    code = re.sub(r"\[[^\]]*\]", lambda match: " " * len(match.group(0)), code)
    names = list(IDENT_RE.finditer(code))
    if not names:
        return None, 0
    last = names[-1]
    return last.group(0), start + last.start()


def collect_decls(root: Path) -> list[FieldDecl]:
    aliases: dict[str, str] = {}
    for path in sorted((root / "include").rglob("*.h")):
        aliases.update(
            {tag: alias for tag, alias in TAG_ALIAS_RE.findall(
                mask_source(path.read_text(encoding="utf-8")))}
        )
    decls: list[FieldDecl] = []
    for relative in DECLARING_HEADERS:
        path = root / relative
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise DeadFieldError(f"cannot read {relative}: {exc}") from exc
        decls.extend(parse_struct_fields(text, relative, aliases))
    if not decls:
        raise DeadFieldError("no struct fields parsed from the declaring headers")
    return decls


def scan_files(root: Path) -> list[tuple[str, str]]:
    files: list[tuple[str, str]] = []
    for tree in SCAN_TREES:
        base = root / tree
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".c", ".h"}:
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except OSError as exc:
                raise DeadFieldError(f"cannot read {path}: {exc}") from exc
            files.append((path.relative_to(root).as_posix(), mask_source(text)))
    if not files:
        raise DeadFieldError("no C sources found under src/, include/ or tests/")
    return files


WRITE_AHEAD_RE = re.compile(r"\s*(\+\+|--|<<=|>>=|[-+*/%&|^]=|=(?!=))")
WRITE_BEHIND_RE = re.compile(r"(\+\+|--)\s*$")
WRITE_AFTER_CALL_RE = re.compile(r"\s*\)\s*(\+\+|--)")


def compile_findings(root: Path) -> tuple[list[Finding], list[FieldDecl]]:
    decls = collect_decls(root)
    files = scan_files(root)
    declaration_sites = {(decl.path, decl.offset) for decl in decls}
    findings: list[Finding] = []
    for decl in decls:
        pattern = re.compile(r"\b" + re.escape(decl.field) + r"\b")
        total = 0
        prod_reads = 0
        test_reads = 0
        writes = 0
        for relative, text in files:
            is_test = relative.startswith("tests/")
            for match in pattern.finditer(text):
                if (relative, match.start()) in declaration_sites:
                    continue
                total += 1
                write = bool(
                    WRITE_AHEAD_RE.match(text, match.end())
                    or WRITE_BEHIND_RE.search(text[: match.start()])
                    or WRITE_AFTER_CALL_RE.match(text, match.end())
                )
                if write:
                    writes += 1
                elif is_test:
                    test_reads += 1
                else:
                    prod_reads += 1
        if total == 0:
            findings.append(Finding(decl.struct, decl.field, "no-use", "no reference"))
        elif prod_reads == 0 and test_reads > 0:
            findings.append(
                Finding(decl.struct, decl.field, "test-only", "read only by tests")
            )
        elif writes == total:
            findings.append(
                Finding(decl.struct, decl.field, "write-only", "written, never read")
            )
    findings.sort(key=lambda finding: (finding.kind, finding.struct, finding.field))
    return findings, decls


def load_shared_fields(root: Path) -> list[dict[str, str]]:
    """Read the ownership table that also defines the shared-field prefixes."""
    path = root / "tools/shared_fields.csv"
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cells = [cell.strip() for cell in line.split(",")]
        if len(cells) < 3 or cells[0] == "struct":
            continue
        rows.append({"struct": cells[0], "field": cells[1], "category": cells[2]})
    return rows


def compile_prefix_findings(root: Path, decls: list[FieldDecl]) -> list[Finding]:
    """Every shared field must be categorised in the CSV and carry that prefix."""
    rows = load_shared_fields(root)
    if not rows:
        return []
    findings: list[Finding] = []
    categorised = {(row["struct"], row["field"]): row["category"] for row in rows}
    for row in rows:
        if row["struct"] != "iolink_dll_shared_t":
            continue
        if not row["field"].startswith(row["category"]):
            findings.append(
                Finding(
                    row["struct"],
                    row["field"],
                    "prefix",
                    f"tools/shared_fields.csv classifies it as {row['category']}",
                )
            )
    for decl in decls:
        if decl.struct != "iolink_dll_shared_t":
            continue
        category = categorised.get((decl.struct, decl.field))
        if category is None:
            findings.append(
                Finding(decl.struct, decl.field, "prefix", "missing from tools/shared_fields.csv")
            )
        elif not decl.field.startswith(category):
            findings.append(
                Finding(
                    decl.struct,
                    decl.field,
                    "prefix",
                    f"name does not start with {category}",
                )
            )
    findings.sort(key=lambda finding: (finding.struct, finding.field))
    return findings


def load_allowlist(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise DeadFieldError(f"{path} is missing")
    entries: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "#" not in line:
            raise DeadFieldError(f"{path}:{number}: entry needs a trailing '# reason'")
        key, reason = line.split("#", 1)
        key = key.strip()
        reason = reason.strip()
        if not key or not reason:
            raise DeadFieldError(f"{path}:{number}: entry needs a key and a reason")
        if key in entries:
            raise DeadFieldError(f"{path}:{number}: duplicate entry {key!r}")
        entries[key] = reason
    return entries


def apply_allowlist(findings: list[Finding], allow: dict[str, str]) -> list[Finding]:
    keys = {finding.key for finding in findings}
    stale = sorted(set(allow) - keys)
    if stale:
        raise DeadFieldError(
            "stale dead-field allowlist entr(y/ies) (fix or delete): " + ", ".join(stale)
        )
    return [finding for finding in findings if finding.key not in allow]


def run(root: Path, allowlist: Path, enforce: bool) -> int:
    findings, decls = compile_findings(root)
    allow = load_allowlist(allowlist)
    prefix_findings = compile_prefix_findings(root, decls)
    for finding in prefix_findings:
        print(
            f"dead-field: {finding.key} has an invalid category prefix "
            f"({finding.detail})",
            file=sys.stderr,
        )
    remaining = apply_allowlist(findings, allow) + prefix_findings
    for finding in remaining:
        print(
            f"dead-field: {finding.key} is {finding.kind} ({finding.detail})",
            file=sys.stderr,
        )
    for finding in findings:
        if finding.key in allow:
            print(f"allowed: {finding.key} ({finding.kind}): {allow[finding.key]}")
    if remaining:
        print(
            f"check-dead-fields: {len(remaining)} unexcused finding(s) "
            f"of {len(findings)} total, {len(decls)} fields scanned"
            + ("" if enforce else " (report-only)"),
            file=sys.stderr,
        )
        if enforce:
            return 1
    else:
        print(
            f"check-dead-fields: OK ({len(decls)} fields, "
            f"{len(allow)} allowed finding(s))"
        )
    return 0


def selftest() -> int:
    failures = 0
    cases = [
        (
            FIXTURE / "bad",
            {
                ("iolink_dll_shared_t", "ghost_field", "no-use"),
                ("iolink_dll_shared_t", "sink_field", "write-only"),
                ("iolink_dll_shared_t", "probe_field", "test-only"),
            },
        ),
        (FIXTURE / "clean", set()),
    ]
    for base, expected in cases:
        root = ROOT / base
        try:
            findings, _ = compile_findings(root)
        except DeadFieldError as exc:
            failures += 1
            print(f"selftest FAIL: {base}: {exc}", file=sys.stderr)
            continue
        actual = {(finding.struct, finding.field, finding.kind) for finding in findings}
        if actual != expected:
            failures += 1
            print(
                f"selftest FAIL: {base}: expected {sorted(expected)}, "
                f"got {sorted(actual)}",
                file=sys.stderr,
            )

    prefix_root = ROOT / FIXTURE / "prefix"
    try:
        prefix_text = (prefix_root / "include/iolinki/internal/dll_shared.h").read_text(
            encoding="utf-8"
        )
        prefix_decls = parse_struct_fields(
            prefix_text, "include/iolinki/internal/dll_shared.h", {}
        )
        prefix = compile_prefix_findings(prefix_root, prefix_decls)
        prefix_actual = {(finding.field, finding.kind) for finding in prefix}
    except (OSError, DeadFieldError) as exc:
        failures += 1
        print(f"selftest FAIL: prefix fixture: {exc}", file=sys.stderr)
    else:
        prefix_expected = {("bad_name", "prefix"), ("b_missing", "prefix")}
        if prefix_actual != prefix_expected:
            failures += 1
            print(
                f"selftest FAIL: prefix fixture: expected {sorted(prefix_expected)}, "
                f"got {sorted(prefix_actual)}",
                file=sys.stderr,
            )

    with tempfile.TemporaryDirectory() as temp:
        stale = Path(temp) / "allow.txt"
        stale.write_text("s_shared.ghost_field  # stale on purpose\n", encoding="utf-8")
        try:
            findings, _ = compile_findings(ROOT / FIXTURE / "clean")
            apply_allowlist(findings, load_allowlist(stale))
        except DeadFieldError:
            pass
        else:
            failures += 1
            print("selftest FAIL: stale allowlist entry was accepted", file=sys.stderr)

        unreasoned = Path(temp) / "bare.txt"
        unreasoned.write_text("s_shared.ghost_field\n", encoding="utf-8")
        try:
            load_allowlist(unreasoned)
        except DeadFieldError:
            pass
        else:
            failures += 1
            print("selftest FAIL: allowlist entry without a reason was accepted", file=sys.stderr)

    if failures:
        return 1
    print("dead-fields selftest: OK (3 classes, prefix categories, clean tree, stale/bare allowlist)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="tree to analyse")
    parser.add_argument(
        "--allowlist", type=Path, default=None, help="override the allowlist path"
    )
    parser.add_argument("--enforce", action="store_true", help="exit 1 on findings")
    parser.add_argument("--selftest", action="store_true", help="verify the gate itself")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    root = args.root.resolve()
    allowlist = args.allowlist or (root / ALLOWLIST)
    try:
        return run(root, allowlist, args.enforce)
    except DeadFieldError as exc:
        print(f"check-dead-fields: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
