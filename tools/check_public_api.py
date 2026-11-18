#!/usr/bin/env python3
"""Keep the public API referenced, documented and free of test-only helpers.

For every function in ``include/iolinki/*.h`` (``internal/`` excluded) this
gate requires:

  * at least one reference in ``src/``, ``examples/``, ``tools/`` or
    ``tests/``, or an entry in ``tools/public_api_allowlist.txt`` explaining
    why the symbol exists for integrators only. References are counted on the
    preprocessed translation units, so a call hidden behind a macro still
    counts; a file that cannot be preprocessed in-tree falls back to its
    masked source (direct calls only);
  * an entry in the generated ``public_api`` table of ``docs/API.md`` or
    ``docs/API_INTEGRATION.md`` (``tools/gen_docs.py`` renders that block);
  * no function whose only references live in ``tests/`` -- that is a
    test-only helper masquerading as API.

``static inline`` definitions are enumerated and must be referenced, but
``tools/gen_docs.py`` deliberately omits them from the generated table
(they are implementations, not declarations), so they are reported as an
``info`` line when absent instead of failing the gate.

Every allowlist entry carries a ``# reason``; an entry that no longer matches
an unexcused symbol is a hard failure.

Usage:
    tools/check_public_api.py               # report-only (exit 0)
    tools/check_public_api.py --enforce     # exit 1 on findings
    tools/check_public_api.py --selftest    # verify the gate on fixtures
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_HEADERS = "include/iolinki/*.h"
REFERENCE_TREES = ("src", "examples", "tools", "tests")
DOC_FILES = ("docs/API.md", "docs/API_INTEGRATION.md")
BLOCK_BEGIN_RE = re.compile(
    r"<!-- BEGIN GENERATED: public_api \(tools/gen_docs\.py\) -->"
)
BLOCK_END = "<!-- END GENERATED -->"
ALLOWLIST = Path("tools/public_api_allowlist.txt")
FIXTURE = Path("tools/fixtures/gates/public_api")

FUNCTION_RE = re.compile(r"(?<![*\w])([a-z_]\w*)\s*\(")
FUNCTION_POINTER_RETURN_RE = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_ \t*]*\(\s*\*\s*([a-z_]\w*)\s*\("
)
FUNCTION_KEYWORDS = {
    "void",
    "const",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "signed",
    "unsigned",
    "struct",
    "enum",
    "union",
    "bool",
    "size_t",
    "if",
    "for",
    "while",
    "switch",
    "return",
    "sizeof",
    "static",
    "inline",
}


class PublicApiError(Exception):
    """The tree cannot be analysed; the message says why."""


@dataclass(frozen=True)
class PublicFunction:
    header: str
    name: str
    static_inline: bool


def mask_c_source(text: str) -> str:
    """Blank comments, literals and preprocessor directives, keeping offsets."""
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
    return _blank_directives("".join(out))


def _blank_directives(text: str) -> str:
    lines = text.split("\n")
    return "\n".join(
        " " * len(line) if line.lstrip().startswith("#") else line for line in lines
    )


def iter_statements(masked: str):
    """Yield (start, end) spans of top-level statements and brace blocks."""
    start = 0
    brace = 0
    paren = 0
    bracket = 0
    for index, char in enumerate(masked):
        if char == "{":
            brace += 1
        elif char == "}":
            brace = max(0, brace - 1)
            if brace == 0:
                yield start, index + 1
                start = index + 1
        elif char == "(":
            paren += 1
        elif char == ")":
            paren = max(0, paren - 1)
        elif char == "[":
            bracket += 1
        elif char == "]":
            bracket = max(0, bracket - 1)
        elif char == ";" and brace == 0 and paren == 0 and bracket == 0:
            yield start, index + 1
            start = index + 1


def header_functions(root: Path) -> list[PublicFunction]:
    headers = sorted((root / "include" / "iolinki").glob("*.h"))
    if not headers:
        raise PublicApiError(f"{PUBLIC_HEADERS}: no public headers found")
    functions: list[PublicFunction] = []
    seen: set[str] = set()
    for path in headers:
        try:
            source = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise PublicApiError(f"cannot read {path}: {exc}") from exc
        masked = mask_c_source(source)
        masked = re.sub(
            r"\bextern\s*\{", lambda match: " " * len(match.group(0)), masked
        )
        for start, end in iter_statements(masked):
            chunk = masked[start:end]
            stripped = chunk.lstrip()
            if not stripped:
                continue
            glue = re.match(r"(?:[ \t]*[A-Z_][A-Z0-9_]*[ \t]*\n)+", stripped)
            if glue:
                stripped = stripped[glue.end() :].lstrip()
            if stripped.startswith("typedef"):
                continue
            words = re.findall(r"[A-Za-z_]\w*", stripped)
            if not words:
                continue
            pointer = FUNCTION_POINTER_RETURN_RE.search(stripped)
            match = pointer if pointer else FUNCTION_RE.search(stripped)
            if match is None:
                continue
            name = match.group(1)
            if name in FUNCTION_KEYWORDS or name.startswith("__"):
                continue
            if name in seen:
                continue
            seen.add(name)
            functions.append(
                PublicFunction(
                    path.relative_to(root).as_posix(),
                    name,
                    static_inline="static" in words,
                )
            )
    if not functions:
        raise PublicApiError(f"{PUBLIC_HEADERS}: no public functions found")
    return functions


def reference_files(root: Path) -> list[tuple[str, str]]:
    """Return (relative path, preprocessed text) for every scanned C file.

    Preprocessing is what makes macro-mediated calls visible (``DLL_DIAG_PULSE``
    expands to ``iolink_diag_pulse``); a file that cannot be preprocessed in
    this tree (vendor headers missing) falls back to its masked source, which
    still sees direct calls.
    """
    clang = shutil.which("clang")
    if clang is None:
        raise PublicApiError("clang is required to resolve macro-mediated references")
    files: list[tuple[str, str]] = []
    for tree in REFERENCE_TREES:
        base = root / tree
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.c")):
            relative = path.relative_to(root).as_posix()
            command = [
                clang,
                "-E",
                "-std=c99",
                f"-I{root / 'include'}",
                f"-I{root / 'src'}",
                f"-I{root / 'tests'}",
                f"-I{path.parent}",
                str(path),
            ]
            proc = subprocess.run(
                command, capture_output=True, text=True, errors="replace"
            )
            if proc.returncode == 0:
                text = proc.stdout
            else:
                text = mask_c_source(path.read_text(encoding="utf-8", errors="replace"))
            files.append((relative, text))
    if not files:
        raise PublicApiError("no C sources found under the reference trees")
    return files


def is_definition(text: str, offset: int) -> bool:
    """A line that starts at column 1 carries a definition/prototype, not a call."""
    line_start = text.rfind("\n", 0, offset) + 1
    return line_start < len(text) and not text[line_start].isspace()


def classify_references(
    functions: list[PublicFunction], files: list[tuple[str, str]]
) -> dict[str, dict[str, int]]:
    counts: dict[str, dict[str, int]] = {
        function.name: {"prod": 0, "test": 0} for function in functions
    }
    names = sorted({function.name for function in functions}, key=len, reverse=True)
    pattern = re.compile(r"\b(?:" + "|".join(re.escape(name) for name in names) + r")\b")
    for relative, text in files:
        is_test = relative.startswith("tests/")
        for match in pattern.finditer(text):
            if is_definition(text, match.start()):
                continue
            counts[match.group(0)]["test" if is_test else "prod"] += 1
    return counts


def documented_functions(root: Path) -> set[str]:
    documented: set[str] = set()
    found_block = False
    for relative in DOC_FILES:
        path = root / relative
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        match = BLOCK_BEGIN_RE.search(text)
        if match is None:
            continue
        end = text.find(BLOCK_END, match.end())
        if end < 0:
            raise PublicApiError(f"{relative}: unterminated public_api block")
        found_block = True
        for line in text[match.end() : end].splitlines():
            line = line.strip()
            if not line.startswith("|"):
                continue
            cells = [cell.strip() for cell in line.strip("|").split("|")]
            if len(cells) < 2:
                continue
            cell = cells[1].replace("`", "").strip()
            if not cell or cell in {"公开函数", "—", "-"} or set(cell) <= {":", "-"}:
                continue
            documented.add(cell.removesuffix("()").strip())
    if not found_block:
        raise PublicApiError("no public_api generated block found in docs/API*.md")
    return documented


def load_allowlist(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise PublicApiError(f"{path} is missing")
    entries: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "#" not in line:
            raise PublicApiError(f"{path}:{number}: entry needs a trailing '# reason'")
        key, reason = line.split("#", 1)
        key = key.strip()
        reason = reason.strip()
        if not key or not reason:
            raise PublicApiError(f"{path}:{number}: entry needs a key and a reason")
        if key in entries:
            raise PublicApiError(f"{path}:{number}: duplicate entry {key!r}")
        entries[key] = reason
    return entries


@dataclass(frozen=True)
class Report:
    findings: list[tuple[str, str, str]]
    infos: list[tuple[str, str]]


def analyse(root: Path, allowlist: Path) -> Report:
    functions = header_functions(root)
    counts = classify_references(functions, reference_files(root))
    documented = documented_functions(root)
    allow = load_allowlist(allowlist)

    findings: list[tuple[str, str, str]] = []
    infos: list[tuple[str, str]] = []
    unexcused: set[str] = set()
    for function in functions:
        bucket = counts[function.name]
        if bucket["prod"] == 0 and bucket["test"] == 0:
            unexcused.add(function.name)
            findings.append(
                (
                    "unreferenced",
                    function.name,
                    f"{function.header}: no reference in src/, examples/, tools/ or tests/",
                )
            )
        elif bucket["prod"] == 0 and function.name.startswith("iolink_dll_"):
            unexcused.add(function.name)
            findings.append(
                (
                    "test-only",
                    function.name,
                    f"{function.header}: iolink_dll_* entry point referenced only from tests/",
                )
            )
        if function.name not in documented:
            if function.static_inline:
                infos.append(
                    (
                        function.name,
                        "static inline; tools/gen_docs.py omits implementations from the table",
                    )
                )
            else:
                unexcused.add(function.name)
                findings.append(
                    (
                        "undocumented",
                        function.name,
                        f"{function.header}: missing from the generated public_api table",
                    )
                )

    stale = sorted(set(allow) - unexcused)
    if stale:
        raise PublicApiError(
            "stale public-api allowlist entr(y/ies) (fix or delete): " + ", ".join(stale)
        )
    excused = [finding for finding in findings if finding[1] in allow]
    remaining = [finding for finding in findings if finding[1] not in allow]
    report_infos = list(infos)
    for kind, name, detail in excused:
        report_infos.append((name, f"allowed as {kind}: {detail}"))
    return Report(remaining, report_infos)


def run(root: Path, allowlist: Path, enforce: bool) -> int:
    report = analyse(root, allowlist)
    allow = load_allowlist(allowlist)
    for kind, name, detail in report.findings:
        print(f"public-api: {name} is {kind} ({detail})", file=sys.stderr)
    for name, detail in report.infos:
        print(f"info: {name}: {detail}")
    if report.findings:
        print(
            f"check-public-api: {len(report.findings)} unexcused finding(s)"
            + ("" if enforce else " (report-only)"),
            file=sys.stderr,
        )
        return 1 if enforce else 0
    print("check-public-api: OK")
    return 0


def selftest() -> int:
    failures = 0
    cases = [
        (
            FIXTURE / "bad",
            {
                ("unreferenced", "iolink_demo_orphan"),
                ("undocumented", "iolink_demo_undocumented"),
            },
        ),
        (FIXTURE / "clean", set()),
    ]
    for base, expected in cases:
        root = ROOT / base
        allowlist = root / "tools" / "public_api_allowlist.txt"
        try:
            report = analyse(root, allowlist)
        except PublicApiError as exc:
            failures += 1
            print(f"selftest FAIL: {base}: {exc}", file=sys.stderr)
            continue
        actual = {(kind, name) for kind, name, _ in report.findings}
        if actual != expected:
            failures += 1
            print(
                f"selftest FAIL: {base}: expected {sorted(expected)}, "
                f"got {sorted(actual)}",
                file=sys.stderr,
            )

    with tempfile.TemporaryDirectory() as temp:
        stale = Path(temp) / "allow.txt"
        stale.write_text("iolink_demo_orphan  # stale on purpose\n", encoding="utf-8")
        try:
            analyse(ROOT / FIXTURE / "clean", stale)
        except PublicApiError:
            pass
        else:
            failures += 1
            print("selftest FAIL: stale allowlist entry was accepted", file=sys.stderr)

        bare = Path(temp) / "bare.txt"
        bare.write_text("iolink_demo_orphan\n", encoding="utf-8")
        try:
            load_allowlist(bare)
        except PublicApiError:
            pass
        else:
            failures += 1
            print("selftest FAIL: allowlist entry without a reason was accepted", file=sys.stderr)

    if failures:
        return 1
    print("public-api selftest: OK (unreferenced, undocumented, clean, stale allowlist)")
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
    except PublicApiError as exc:
        print(f"check-public-api: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
