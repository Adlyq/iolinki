#!/usr/bin/env python3
"""Check documentation references against the tree they describe.

Four rules are enforced for the Markdown documents (docs/**/*.md and
README.md):

  1. Every ``iolink_*`` / ``IOLINK_*`` symbol inside an inline code span exists
     in include/, src/, tests/ or examples/. A macro that only exists after
     configuration is a legitimate exception, but it needs an allowlist entry
     with a reason -- never a silently skipped symbol.
  2. Relative links and anchors resolve: ``#anchor`` inside the same document,
     relative ``.md`` paths (and their anchors). External URLs, images and
     non-Markdown targets are out of scope for this rule.
  3. Repository paths named in Dockerfile*, run_*.sh, test_all.sh,
     .github/workflows/*.yml and tools/*.sh exist. Variables, URLs, absolute
     paths and build/ outputs are skipped: they are not repository paths.
  4. ``SYMBOL（-N）`` and ``SYMBOL = -N`` error codes match the enumeration in
     include/iolinki/device.h.

Allowlist file: ``tools/docs_refs_allowlist.txt`` with one
``path-glob:symbol-glob # reason`` entry per line. A reason is mandatory and a
stale entry (it suppresses nothing) is a hard failure, the same rule as
tools/diff_allowlist.txt.

Usage:
    tools/check_docs_refs.py             # report violations, exit 1 if any
    tools/check_docs_refs.py --selftest  # verify the checker itself
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import shutil
import sys
import tempfile
import urllib.parse
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tools" / "fixtures" / "docs" / "refs"
ALLOWLIST = Path("tools/docs_refs_allowlist.txt")
DEVICE_HEADER = Path("include/iolinki/device.h")

SYMBOL_RE = re.compile(r"\b(?:iolink_[A-Za-z0-9_]+|IOLINK_[A-Za-z0-9_]+)\b")
INLINE_CODE_RE = re.compile(r"`([^`\n]+)`")
LINK_RE = re.compile(r"\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
HTML_LINK_RE = re.compile(r"<a\s+[^>]*href=\"([^\"]+)\"", re.IGNORECASE)
HTML_ANCHOR_RE = re.compile(r"<a\s+[^>]*(?:name|id)=\"([^\"]+)\"", re.IGNORECASE)
ID_ATTR_RE = re.compile(r"\bid=\"([^\"]+)\"")
ERROR_CODE_RE = re.compile(
    r"(?P<symbol>IOLINK_DEVICE_[A-Z0-9_]+)`?\s*"
    r"(?:[（(]\s*(?P<paren>-?\d+)\s*[）)]|=\s*(?P<equals>-?\d+))"
)
SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp", ".inc"}
SOURCE_DIRS = ("include", "src", "tests", "examples")
REPO_ROOTS = (
    "include",
    "src",
    "tests",
    "examples",
    "tools",
    "docs",
    ".github",
    "cmake",
    "scripts",
)
REPO_PATH_RE = re.compile(
    r"(?<![\w./$)])"
    r"((?:\./)?(?:"
    + "|".join(re.escape(entry) for entry in REPO_ROOTS)
    + r")/[A-Za-z0-9_.\-/]+)"
)
ROOT_FILE_RE = re.compile(
    r"(?<![\w./-])(CMakeLists\.txt|CMakePresets\.json|CHANGELOG\.md|LICENSE|"
    r"README\.md|Dockerfile(?:\.[A-Za-z0-9_.-]+)?)"
)
SCRIPT_GLOBS = (
    "Dockerfile*",
    "run_*.sh",
    "test_all.sh",
    ".github/workflows/*.yml",
    ".github/workflows/*.yaml",
    "tools/*.sh",
)
SKIP_DIRS = {".git", "build", "node_modules", ".venv", "__pycache__"}


@dataclass(frozen=True)
class Violation:
    kind: str
    path: str
    line: int
    message: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.message}"


class RefsError(Exception):
    """The checker cannot run (malformed allowlist, unreadable input)."""


# --- Document scanning ------------------------------------------------------


def iter_docs(root: Path) -> list[Path]:
    docs_dir = root / "docs"
    paths = sorted(docs_dir.rglob("*.md")) if docs_dir.is_dir() else []
    readme = root / "README.md"
    if readme.is_file():
        paths.append(readme)
    return paths


def relative(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def iter_unfenced_lines(text: str):
    """Yield (line number, line) outside ``` / ~~~ fenced code blocks."""
    fence: str | None = None
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.lstrip()
        marker = stripped[:3]
        if marker in {"```", "~~~"}:
            if fence is None:
                fence = marker
            elif fence == marker:
                fence = None
            continue
        if fence is None:
            yield number, line


def github_slug(heading: str) -> str:
    text = heading.strip().lower()
    # Backticks, emphasis markers and link syntax are formatting, not text.
    # Underscores stay: GitHub only treats them as emphasis around words.
    text = re.sub(r"[`*~]", "", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    return re.sub(r"\s+", "-", text.strip())


def anchors_of(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    anchors: set[str] = set()
    seen: dict[str, int] = {}
    for _, line in iter_unfenced_lines(text):
        match = re.match(r"^\s{0,3}(#{1,6})\s+(.*?)\s*$", line)
        if match:
            slug = github_slug(match.group(2))
            count = seen.get(slug, 0)
            seen[slug] = count + 1
            anchors.add(slug if count == 0 else f"{slug}-{count}")
        for explicit in HTML_ANCHOR_RE.findall(line):
            anchors.add(explicit)
    for explicit in ID_ATTR_RE.findall(text):
        anchors.add(explicit)
    return anchors


def build_symbol_index(root: Path) -> set[str]:
    symbols: set[str] = set()
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                symbols.update(SYMBOL_RE.findall(path.read_text(encoding="utf-8", errors="replace")))
    return symbols


def extract_error_codes(root: Path) -> dict[str, int]:
    path = root / DEVICE_HEADER
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RefsError(f"cannot read {DEVICE_HEADER}: {exc}") from exc
    codes: dict[str, int] = {}
    for match in re.finditer(r"(IOLINK_DEVICE_[A-Z0-9_]+)\s*=\s*(-?\d+)", text):
        codes[match.group(1)] = int(match.group(2))
    if not codes:
        raise RefsError(f"{DEVICE_HEADER}: no IOLINK_DEVICE_* error codes found")
    return codes


# --- Allowlist --------------------------------------------------------------


@dataclass
class AllowEntry:
    line: int
    path_pattern: str
    symbol_pattern: str
    reason: str
    used: int = 0


def read_allowlist(root: Path) -> list[AllowEntry]:
    path = root / ALLOWLIST
    if not path.is_file():
        return []
    entries: list[AllowEntry] = []
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        pattern, separator, reason = line.partition("#")
        if not separator or not reason.strip():
            raise RefsError(f"{ALLOWLIST}:{number}: allowlist entry needs '# reason'")
        target, separator, symbol = pattern.strip().partition(":")
        if not separator or not target.strip() or not symbol.strip():
            raise RefsError(
                f"{ALLOWLIST}:{number}: expected 'path-glob:symbol-glob # reason'"
            )
        entries.append(AllowEntry(number, target.strip(), symbol.strip(), reason.strip()))
    return entries


def is_allowed(entries: list[AllowEntry], path: str, symbol: str) -> bool:
    allowed = False
    for entry in entries:
        if fnmatch.fnmatchcase(path, entry.path_pattern) and fnmatch.fnmatchcase(
            symbol, entry.symbol_pattern
        ):
            entry.used += 1
            allowed = True
    return allowed


# --- Rules ------------------------------------------------------------------


def check_symbols(path: Path, rel: str, text: str, symbols: set[str], allow: list[AllowEntry]):
    violations: list[Violation] = []
    for number, line in iter_unfenced_lines(text):
        for span in INLINE_CODE_RE.findall(line):
            for match in SYMBOL_RE.finditer(span):
                symbol = match.group(0)
                following = span[match.end() : match.end() + 1]
                # `IOLINK_CAP_*` is a prefix, and `IOLINK_PLATFORM=LINUX` is a
                # build-system variable: neither claims to name a C symbol.
                if following in {"*", "="}:
                    continue
                if symbol in symbols:
                    continue
                if is_allowed(allow, rel, symbol):
                    continue
                violations.append(
                    Violation(
                        "symbol",
                        rel,
                        number,
                        f"unknown symbol {symbol!r} in inline code "
                        "(not found in include/, src/, tests/ or examples/)",
                    )
                )
    return violations


def check_links(path: Path, rel: str, text: str):
    violations: list[Violation] = []
    anchor_cache: dict[Path, set[str]] = {}

    def anchors(target: Path) -> set[str]:
        if target not in anchor_cache:
            anchor_cache[target] = anchors_of(target)
        return anchor_cache[target]

    for number, line in iter_unfenced_lines(text):
        targets = LINK_RE.findall(line) + HTML_LINK_RE.findall(line)
        for target in targets:
            target = target.strip()
            if not target or "://" in target or target.startswith(("//", "mailto:", "tel:")):
                continue
            target = urllib.parse.unquote(target)
            path_part, separator, fragment = target.partition("#")
            if not path_part:
                if separator and fragment and fragment not in anchors(path):
                    violations.append(
                        Violation(
                            "link",
                            rel,
                            number,
                            f"unresolved anchor #{fragment!r} in {rel}",
                        )
                    )
                continue
            if not path_part.lower().endswith(".md"):
                continue
            resolved = (path.parent / path_part).resolve()
            if not resolved.is_file():
                violations.append(
                    Violation("link", rel, number, f"missing link target {path_part!r}")
                )
                continue
            if separator and fragment and fragment not in anchors(resolved):
                violations.append(
                    Violation(
                        "link",
                        rel,
                        number,
                        f"unresolved anchor #{fragment!r} in {path_part}",
                    )
                )
    return violations


def iter_scripts(root: Path) -> list[Path]:
    seen: set[Path] = set()
    for pattern in SCRIPT_GLOBS:
        for path in root.glob(pattern):
            if path.is_file() and not any(part in SKIP_DIRS for part in path.parts):
                seen.add(path)
    for pattern in ("run_*.sh", "test_all.sh"):
        for path in root.rglob(pattern):
            if path.is_file() and not any(part in SKIP_DIRS for part in path.parts):
                seen.add(path)
    return sorted(seen)


def path_tokens(line: str) -> list[str]:
    tokens: list[str] = []
    for match in REPO_PATH_RE.finditer(line):
        token = match.group(1)
        # A glob is a pattern, not a path that must exist. `src/dll*.c`
        # leaves the literal prefix `src/dll` in the match.
        if line[match.end() : match.end() + 1] in {"*", "?"}:
            continue
        tokens.append(token)
    tokens += ROOT_FILE_RE.findall(line)
    cleaned: list[str] = []
    for token in tokens:
        token = token.strip("'\"`()[]<>,;")
        token = token.rstrip(".,;:")
        if token.startswith("./"):
            token = token[2:]
        if not token or token.endswith("/"):
            continue
        if any(marker in token for marker in ("$", "{", "}", "*", "?", "@", ":", "%", "\\", "|")):
            continue
        if token.startswith(("build/", "/", "~")):
            continue
        if "/build/" in token:
            continue
        # A path without a suffix is a build output (examples/x/x,
        # tests/test_init): it exists only after a build, like build/.
        if "." not in token.rsplit("/", 1)[-1]:
            continue
        cleaned.append(token)
    return cleaned


def check_script_paths(root: Path) -> list[Violation]:
    violations: list[Violation] = []
    for script in iter_scripts(root):
        rel = relative(root, script)
        text = script.read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), start=1):
            for token in path_tokens(line):
                if not (root / token).exists():
                    violations.append(
                        Violation("path", rel, number, f"references missing path {token!r}")
                    )
    return violations


def check_error_codes(path: Path, rel: str, text: str, codes: dict[str, int]):
    violations: list[Violation] = []
    for number, line in enumerate(text.splitlines(), start=1):
        for match in ERROR_CODE_RE.finditer(line):
            symbol = match.group("symbol")
            if symbol not in codes:
                continue
            value = int(match.group("paren") or match.group("equals"))
            if value != codes[symbol]:
                violations.append(
                    Violation(
                        "number",
                        rel,
                        number,
                        f"{symbol} is written as {value}, "
                        f"include/iolinki/device.h has {codes[symbol]}",
                    )
                )
    return violations


def collect(root: Path) -> tuple[list[Violation], list[AllowEntry]]:
    allow = read_allowlist(root)
    symbols = build_symbol_index(root)
    codes = extract_error_codes(root)
    violations: list[Violation] = []
    for path in iter_docs(root):
        rel = relative(root, path)
        text = path.read_text(encoding="utf-8", errors="replace")
        violations += check_symbols(path, rel, text, symbols, allow)
        violations += check_links(path, rel, text)
        violations += check_error_codes(path, rel, text, codes)
    violations += check_script_paths(root)
    for entry in allow:
        if entry.used == 0:
            violations.append(
                Violation(
                    "allowlist",
                    ALLOWLIST.as_posix(),
                    entry.line,
                    f"stale allowlist entry {entry.path_pattern}:{entry.symbol_pattern} "
                    f"(reason: {entry.reason})",
                )
            )
    return list(dict.fromkeys(violations)), allow


# --- Selftest ---------------------------------------------------------------


def selftest() -> int:
    failures: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    def kinds(violations: list[Violation]) -> set[str]:
        return {violation.kind for violation in violations}

    with tempfile.TemporaryDirectory(prefix="check_docs_refs_selftest_") as tmp:
        workspace = Path(tmp) / "refs"
        shutil.copytree(FIXTURE, workspace)
        good = workspace / "docs" / "good.md"
        baseline = good.read_text(encoding="utf-8")
        script = workspace / "tools" / "gates.sh"
        script_text = script.read_text(encoding="utf-8")
        allowlist = workspace / ALLOWLIST
        allowlist_text = allowlist.read_text(encoding="utf-8")

        violations, allow = collect(workspace)
        expect(not violations, "fixture baseline fails: " + "; ".join(v.render() for v in violations))
        expect(any(entry.used for entry in allow), "fixture allowlist entry is not exercised")

        good.write_text(
            baseline + "\n`iolink_fixture_missing_symbol`\n", encoding="utf-8"
        )
        violations, _ = collect(workspace)
        expect(
            any(v.kind == "symbol" and "iolink_fixture_missing_symbol" in v.message for v in violations),
            "missing symbol was not reported",
        )
        good.write_text(baseline, encoding="utf-8")

        good.write_text(baseline + "\n[坏链接](missing.md)\n", encoding="utf-8")
        violations, _ = collect(workspace)
        expect(
            any(v.kind == "link" and "missing.md" in v.message for v in violations),
            "broken link was not reported",
        )
        good.write_text(baseline, encoding="utf-8")

        good.write_text(baseline + "\n[坏锚点](good.md#no-such-heading)\n", encoding="utf-8")
        violations, _ = collect(workspace)
        expect(
            any(v.kind == "link" and "anchor" in v.message for v in violations),
            "broken anchor was not reported",
        )
        good.write_text(baseline, encoding="utf-8")

        script.write_text(
            script_text + "\nif [ -f tools/deleted_helper.sh ]; then :; fi\n",
            encoding="utf-8",
        )
        violations, _ = collect(workspace)
        expect(
            any(v.kind == "path" and "tools/deleted_helper.sh" in v.message for v in violations),
            "deleted script path was not reported",
        )
        script.write_text(script_text, encoding="utf-8")

        good.write_text(
            baseline + "\nIOLINK_DEVICE_INIT_ERR_NULL（-9）\n", encoding="utf-8"
        )
        violations, _ = collect(workspace)
        expect(
            any(v.kind == "number" and "IOLINK_DEVICE_INIT_ERR_NULL" in v.message for v in violations),
            "wrong error code number was not reported",
        )
        good.write_text(baseline, encoding="utf-8")

        # The allowlist is the only way to accept a configuration-generated macro.
        allowlist.unlink()
        violations, _ = collect(workspace)
        expect(
            any(v.kind == "symbol" and "IOLINK_FIXTURE_GENERATED" in v.message for v in violations),
            "allowlist removal did not expose the generated macro",
        )
        allowlist.write_text(allowlist_text, encoding="utf-8")

        allowlist.write_text(
            allowlist_text + "docs/good.md:iolink_never_used # 过期条目\n",
            encoding="utf-8",
        )
        violations, _ = collect(workspace)
        expect(
            any(v.kind == "allowlist" and "stale" in v.message for v in violations),
            "stale allowlist entry was not rejected",
        )
        allowlist.write_text(allowlist_text, encoding="utf-8")

        allowlist.write_text(allowlist_text + "docs/good.md:iolink_never_used\n", encoding="utf-8")
        try:
            collect(workspace)
        except RefsError:
            pass
        else:
            expect(False, "allowlist entry without a reason was accepted")
        allowlist.write_text(allowlist_text, encoding="utf-8")

        violations, _ = collect(workspace)
        expect(not violations, "fixture is dirty after selftest: " + "; ".join(v.render() for v in violations))

    if failures:
        for failure in failures:
            print(f"docs-refs selftest FAIL: {failure}", file=sys.stderr)
        return 1
    print("docs-refs selftest: OK (symbol, link, path, number, allowlist)")
    return 0


# --- CLI --------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify references (default)")
    parser.add_argument("--selftest", action="store_true", help="verify the checker itself")
    parser.add_argument("--root", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    root = args.root.resolve() if args.root else ROOT
    try:
        violations, allow = collect(root)
    except RefsError as exc:
        print(f"docs-refs: {exc}", file=sys.stderr)
        return 1
    if violations:
        for violation in violations:
            print(violation.render(), file=sys.stderr)
        print(f"docs-refs: {len(violations)} violation(s)", file=sys.stderr)
        return 1
    suffix = f", {len(allow)} allowlist entr{'y' if len(allow) == 1 else 'ies'}" if allow else ""
    print(f"docs-refs: OK ({len(iter_docs(root))} documents{suffix})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
