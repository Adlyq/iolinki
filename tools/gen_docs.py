#!/usr/bin/env python3
"""Generate the derivable tables in docs/ from their single source of truth.

Four blocks are generated. Each lives between the markers

    <!-- BEGIN GENERATED: <name> (tools/gen_docs.py) -->
    ...
    <!-- END GENERATED -->

and each has exactly one source:

  * ``device_error_codes`` -- the ``IOLINK_DEVICE_*`` enums in
    ``include/iolinki/device.h`` (verbatim C, so item order and comments match).
  * ``dll_stats`` -- ``iolink_dll_stats_t`` in ``include/iolinki/dll.h``; the
    meaning column is the member's trailing ``/**< ... */`` comment.
  * ``public_api`` -- every function declared in ``include/iolinki/*.h``
    (``internal/`` excluded), including ``static inline`` definitions, which
    are part of the public API as well. Typedefs, macro constants, struct
    members and function-pointer types are not functions and are skipped.
  * ``shared_ownership`` -- ``tools/shared_fields.csv``
    (``struct,field,category,writer,reader,note``). The CSV is required: a
    missing file is an error, because an empty ownership table would read as
    "nothing is shared" instead of "the data is missing".

Prose is never generated. The block itself must already exist in the document:
``--write`` rewrites its contents, ``--check`` compares them byte for byte. A
missing block, a duplicate BEGIN/END pair or an unknown block name is a hard
failure -- there is no fallback that silently appends or drops a table.

Usage:
    tools/gen_docs.py --check              # verify every committed block
    tools/gen_docs.py --write              # regenerate every block
    tools/gen_docs.py --selftest           # verify the check itself
    tools/gen_docs.py --check --block dll_stats
"""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tools" / "fixtures" / "docs" / "gen"
DEVICE_HEADER = Path("include/iolinki/device.h")
DLL_HEADER = Path("include/iolinki/dll.h")
SHARED_FIELDS = Path("tools/shared_fields.csv")


class DocsError(Exception):
    """A generated block cannot be produced; the message says why."""


@dataclass(frozen=True)
class BlockLocation:
    """Where a block belongs in the prose (used for reports and --check)."""

    path: str
    anchor: str


BLOCKS = {
    "device_error_codes": BlockLocation(
        "docs/API.md",
        "「## 错误码」小节，替换现有的 ```c 枚举块",
    ),
    "dll_stats": BlockLocation(
        "docs/API.md",
        "「## 错误码」之后新增「## DLL 统计」小节，表体",
    ),
    "public_api": BlockLocation(
        "docs/API_INTEGRATION.md",
        "「## 11. 其他公开头文件与适配器」小节，清单之前",
    ),
    "shared_ownership": BlockLocation(
        "docs/CODE_FLOW.md",
        "「## 3. 上下文拓扑与所有权」末尾，生命周期段落之后",
    ),
}

BLOCK_NAMES = tuple(BLOCKS)

BEGIN_RE = re.compile(r"<!-- BEGIN GENERATED: (\S+) \(tools/gen_docs\.py\) -->")
END_MARKER = "<!-- END GENERATED -->"
MARKER_RE = re.compile(r"<!-- BEGIN GENERATED: .*? -->|" + re.escape(END_MARKER))

CATEGORIES = {"c_", "m_", "i_", "b_"}
SHARED_HEADER = ["struct", "field", "category", "writer", "reader", "note"]
STATS_MEMBER_RE = re.compile(
    r"([A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)*\s*\*?)\s+([A-Za-z_]\w*)\s*;"
    r"\s*/\*\*<(?P<doc>.*?)\*/",
    re.DOTALL,
)
ENUM_RE = re.compile(r"typedef\s+enum\s*\{(?P<body>[^{}]*)\}\s*(?P<name>\w+)\s*;")
STATS_END_RE = re.compile(r"\}\s*iolink_dll_stats_t\s*;")
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
}


# --- C source scanning ------------------------------------------------------


@dataclass(frozen=True)
class Comment:
    start: int
    end: int
    text: str


def mask_comments(text: str) -> tuple[str, list[Comment]]:
    """Blank C comments while preserving every offset, and collect them."""
    out: list[str] = []
    comments: list[Comment] = []
    index = 0
    length = len(text)
    state = "code"
    comment_start = 0
    while index < length:
        char = text[index]
        following = text[index + 1] if index + 1 < length else ""
        if state == "line":
            if char == "\\" and following == "\n":
                out.append("  ")
                index += 2
                continue
            if char == "\n":
                out.append("\n")
                comments.append(Comment(comment_start, index, text[comment_start:index]))
                state = "code"
            else:
                out.append(" ")
            index += 1
            continue
        if state == "block":
            if char == "*" and following == "/":
                out.append("  ")
                index += 2
                comments.append(Comment(comment_start, index, text[comment_start:index]))
                state = "code"
                continue
            out.append("\n" if char == "\n" else " ")
            index += 1
            continue
        if char == "/" and following == "/":
            comment_start = index
            out.append("  ")
            index += 2
            state = "line"
            continue
        if char == "/" and following == "*":
            comment_start = index
            out.append("  ")
            index += 2
            state = "block"
            continue
        if char in {'"', "'"}:
            # Literals are data, never declarations: blank them so a parenthesis
            # or brace inside a string cannot corrupt statement scanning.
            quote = char
            out.append(" ")
            index += 1
            while index < length:
                current = text[index]
                if current == "\\" and index + 1 < length:
                    out.append("\n" if current == "\n" else " ")
                    out.append("\n" if text[index + 1] == "\n" else " ")
                    index += 2
                    continue
                out.append("\n" if current == "\n" else " ")
                index += 1
                if current == quote:
                    break
            continue
        out.append(char)
        index += 1
    if state in {"line", "block"}:
        comments.append(Comment(comment_start, length, text[comment_start:length]))
    return "".join(out), comments


def mask_preprocessor(text: str) -> str:
    """Blank preprocessor directives (including continuation lines)."""
    lines = text.split("\n")
    out: list[str] = []
    in_directive = False
    for line in lines:
        if in_directive or line.lstrip().startswith("#"):
            out.append(" " * len(line))
            in_directive = line.rstrip().endswith("\\")
        else:
            out.append(line)
    return "\n".join(out)


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


def normalize_comment(comment: str) -> str:
    """Strip comment delimiters and leading asterisks; keep the prose."""
    body = comment.strip()
    body = re.sub(r"^/\*+!?<?", "", body)
    body = re.sub(r"\*+/\s*$", "", body)
    lines = [re.sub(r"^\s*\*+ ?", "", line).strip() for line in body.split("\n")]
    return "\n".join(lines).strip()


def is_cjk(char: str) -> bool:
    return "\u3000" <= char <= "\u9fff" or "\uff00" <= char <= "\uffef"


def flatten(text: str) -> str:
    """Join wrapped comment lines, without a space next to CJK characters."""
    joined = ""
    for line in text.split("\n"):
        line = line.strip()
        if not line:
            continue
        if joined and not is_cjk(joined[-1]) and not is_cjk(line[0]):
            joined += " "
        joined += line
    return joined


def first_sentence(text: str) -> str:
    text = flatten(text)
    match = re.match(r"(.+?[。.!?])(?:\s|$)", text)
    return match.group(1) if match else text


def strip_inline_tags(text: str) -> str:
    return re.sub(r"@(?:p|ref|c|a|b|e)\s+", "", text)


def comment_description(comment: Comment) -> str:
    body = normalize_comment(comment.text)
    if not body:
        return ""
    if body.startswith(("@file", "@defgroup", "@name", "@{", "@}")):
        return ""
    for paragraph in re.split(r"\n\s*\n", body):
        brief = re.match(r"\s*@brief\s+(.*)", paragraph, re.DOTALL)
        if brief:
            text = re.split(r"\n\s*@[A-Za-z]+", brief.group(1), maxsplit=1)[0]
            return strip_inline_tags(flatten(text))
    if body.startswith("@"):
        return ""
    prose = re.split(r"\n\s*@[A-Za-z]+", body, maxsplit=1)[0]
    lowered = prose.lower()
    if lowered.lstrip().startswith(
        ("clang-format", "nolint", "cppcheck", "end of", "begin ", "end ")
    ):
        return ""
    prose = re.sub(r"^-+$", "", prose).strip()
    if not prose or not re.search(r"[A-Za-z0-9\u4e00-\u9fff]", prose):
        return ""
    return strip_inline_tags(first_sentence(prose))


def preceding_comment(
    comments: list[Comment], masked: str, original: str, position: int
) -> Comment | None:
    for comment in reversed(comments):
        if comment.end > position:
            continue
        if masked[comment.end:position].strip():
            return None
        # A comment immediately above a macro directive documents the macro,
        # not the declaration below. Conditional wrappers (#if/#endif) are
        # allowed: a feature-gated declaration keeps its documentation.
        tail = original[comment.end:position].lstrip()
        if re.match(r"#\s*(?:define|undef|include)\b", tail):
            return None
        return comment
    return None


def public_functions(source: str) -> list[tuple[str, str]]:
    """Return (function name, description) for public declarations in `source`."""
    masked, comments = mask_comments(source)
    masked = mask_preprocessor(masked)
    # `extern "C" {` is a linkage wrapper, not a scope that hides declarations.
    masked = re.sub(
        r"\bextern\s*\{", lambda match: " " * len(match.group(0)), masked
    )
    found: list[tuple[str, str]] = []
    for start, end in iter_statements(masked):
        chunk = masked[start:end]
        stripped = chunk.lstrip()
        if not stripped:
            continue
        offset = start + (len(chunk) - len(stripped))
        # A bare macro line (IOLINK_ASSUME_NONNULL_BEGIN, ...) can precede the
        # first declaration without a semicolon; it is not part of the type.
        glue = re.match(r"(?:[ \t]*[A-Z_][A-Z0-9_]*[ \t]*\n)+", stripped)
        if glue:
            stripped = stripped[glue.end() :].lstrip()
            offset = start + (len(chunk) - len(stripped))
        if stripped.startswith("typedef"):
            continue
        words = re.findall(r"[A-Za-z_]\w*", stripped)
        if not words:
            continue
        # `static inline` in a public header is public API. A plain `static`
        # definition (data, or a file-local helper) is not.
        if "static" in words and "inline" not in words:
            continue
        name = None
        pointer_match = FUNCTION_POINTER_RETURN_RE.search(stripped)
        match = pointer_match if pointer_match else FUNCTION_RE.search(stripped)
        if match:
            name = match.group(1)
        if not name or name in FUNCTION_KEYWORDS:
            continue
        comment = preceding_comment(comments, masked, source, offset)
        found.append((name, comment_description(comment) if comment else ""))
    return found


# --- Markdown helpers -------------------------------------------------------


def md_cell(text: str) -> str:
    return " ".join(text.replace("|", "\\|").split())


def code(text: str) -> str:
    return f"`{text}`"


# --- Renderers --------------------------------------------------------------


def render_device_error_codes(root: Path) -> str:
    path = root / DEVICE_HEADER
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise DocsError(f"cannot read {DEVICE_HEADER}: {exc}") from exc
    enums: list[str] = []
    matches = list(ENUM_RE.finditer(text))
    declared = len(re.findall(r"\btypedef\s+enum\b", text))
    if len(matches) != declared:
        raise DocsError(
            f"{DEVICE_HEADER}: {declared} typedef enum(s) but {len(matches)} parsed; "
            "an enum body contains a brace in a comment or is not a plain typedef"
        )
    for match in matches:
        body = match.group("body")
        if not re.search(r"^\s*IOLINK_DEVICE_[A-Z0-9_]+\s*=", body, re.MULTILINE):
            continue
        enums.append(text[match.start() : match.end()])
    if not enums:
        raise DocsError(f"{DEVICE_HEADER}: no enum with IOLINK_DEVICE_* values")
    return "```c\n" + "\n\n".join(enums) + "\n```"


def render_dll_stats(root: Path) -> str:
    path = root / DLL_HEADER
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise DocsError(f"cannot read {DLL_HEADER}: {exc}") from exc
    struct = STATS_END_RE.search(text)
    if struct is None:
        raise DocsError(f"{DLL_HEADER}: iolink_dll_stats_t struct not found")
    open_brace = text.rfind("{", 0, struct.start())
    typedef = text.rfind("typedef struct", 0, open_brace) if open_brace >= 0 else -1
    if open_brace < 0 or typedef < 0:
        raise DocsError(f"{DLL_HEADER}: iolink_dll_stats_t is not a plain typedef struct")
    if text[typedef:open_brace].strip() != "typedef struct":
        raise DocsError(f"{DLL_HEADER}: iolink_dll_stats_t is not a plain typedef struct")
    body = text[open_brace + 1 : struct.start()]
    if "{" in body or "}" in body:
        raise DocsError(f"{DLL_HEADER}: iolink_dll_stats_t must not contain nested braces")
    rows: list[tuple[str, str, str]] = []
    consumed = 0
    for match in STATS_MEMBER_RE.finditer(body):
        leftover = body[consumed : match.start()].strip()
        if leftover:
            raise DocsError(
                f"{DLL_HEADER}: cannot parse iolink_dll_stats_t member near {leftover!r}"
            )
        member_type = " ".join(match.group(1).split())
        member_name = match.group(2)
        meaning = md_cell(
            strip_inline_tags(flatten(normalize_comment("/**<" + match.group("doc") + "*/")))
        )
        if not meaning:
            raise DocsError(
                f"{DLL_HEADER}: iolink_dll_stats_t::{member_name} has no /**< ... */ comment"
            )
        rows.append((member_name, member_type, meaning))
        consumed = match.end()
    leftover = body[consumed:].strip()
    if leftover:
        raise DocsError(f"{DLL_HEADER}: cannot parse iolink_dll_stats_t member near {leftover!r}")
    if not rows:
        raise DocsError(f"{DLL_HEADER}: iolink_dll_stats_t has no members")
    lines = ["| 字段 | 类型 | 含义 |", "| :--- | :--- | :--- |"]
    lines += [
        f"| {code(name)} | {code(member_type)} | {meaning} |"
        for name, member_type, meaning in rows
    ]
    return "\n".join(lines)


def render_public_api(root: Path) -> str:
    include = root / "include" / "iolinki"
    headers = sorted(include.glob("*.h"))
    if not headers:
        raise DocsError("include/iolinki/*.h: no public headers found")
    rows: list[tuple[str, str, str]] = []
    for header in headers:
        try:
            source = header.read_text(encoding="utf-8")
        except OSError as exc:
            raise DocsError(f"cannot read {header.relative_to(root)}: {exc}") from exc
        for name, description in public_functions(source):
            rows.append((header.relative_to(root).as_posix(), name, description))
    if not rows:
        raise DocsError("include/iolinki/*.h: no public function declarations found")
    lines = ["| 头文件 | 公开函数 | 说明 |", "| :--- | :--- | :--- |"]
    lines += [
        f"| {code(header)} | {code(name + '()')} | {md_cell(description)} |"
        for header, name, description in rows
    ]
    return "\n".join(lines)


def read_shared_fields(root: Path) -> list[dict[str, str]]:
    path = root / SHARED_FIELDS
    if not path.is_file():
        raise DocsError(
            f"{SHARED_FIELDS} is missing; the shared_ownership table has no source of "
            "truth (create the CSV, do not hand-write the table)"
        )
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream)
        try:
            header = next(reader)
        except StopIteration as exc:
            raise DocsError(f"{SHARED_FIELDS}: empty file") from exc
        if header != SHARED_HEADER:
            raise DocsError(
                f"{SHARED_FIELDS}: expected header {','.join(SHARED_HEADER)}, "
                f"got {','.join(header)}"
            )
        rows: list[dict[str, str]] = []
        for line_number, fields in enumerate(reader, start=2):
            if not fields or all(not field.strip() for field in fields):
                continue
            if len(fields) != len(SHARED_HEADER):
                raise DocsError(
                    f"{SHARED_FIELDS}:{line_number}: expected {len(SHARED_HEADER)} columns, "
                    f"got {len(fields)}"
                )
            row = dict(zip(SHARED_HEADER, (field.strip() for field in fields)))
            if not row["struct"] or not row["field"]:
                raise DocsError(f"{SHARED_FIELDS}:{line_number}: struct and field are required")
            if row["category"] not in CATEGORIES:
                raise DocsError(
                    f"{SHARED_FIELDS}:{line_number}: category {row['category']!r} "
                    f"is not one of {', '.join(sorted(CATEGORIES))}"
                )
            rows.append(row)
    if not rows:
        raise DocsError(f"{SHARED_FIELDS}: no field rows")
    return rows


def render_shared_ownership(root: Path) -> str:
    rows = read_shared_fields(root)
    lines = ["| 字段 | 类别 | 写者 | 读者 | 说明 |", "| :--- | :--- | :--- | :--- | :--- |"]
    for row in rows:
        field = code(f"{row['struct']}.{row['field']}")
        lines.append(
            f"| {field} | {code(row['category'])} | {md_cell(row['writer'])} | "
            f"{md_cell(row['reader'])} | {md_cell(row['note'])} |"
        )
    return "\n".join(lines)


RENDERERS = {
    "device_error_codes": render_device_error_codes,
    "dll_stats": render_dll_stats,
    "public_api": render_public_api,
    "shared_ownership": render_shared_ownership,
}


# --- Block bookkeeping ------------------------------------------------------


def block_marker(name: str) -> str:
    return f"<!-- BEGIN GENERATED: {name} (tools/gen_docs.py) -->"


def validate_markers(text: str, path: str) -> None:
    """Pair every BEGIN with one END, including blocks owned by other tools."""
    depth = 0
    for match in MARKER_RE.finditer(text):
        if match.group(0) == END_MARKER:
            if depth == 0:
                raise DocsError(f"{path}: END marker without a preceding BEGIN")
            depth -= 1
        else:
            depth += 1
    if depth:
        raise DocsError(f"{path}: {depth} generated block(s) without an END marker")


def locate_blocks(text: str, path: str) -> dict[str, tuple[int, int, int, int]]:
    """Map name -> (block start, block end, content start, content end)."""
    validate_markers(text, path)
    found: dict[str, tuple[int, int, int, int]] = {}
    for match in BEGIN_RE.finditer(text):
        name = match.group(1)
        if name not in BLOCKS:
            raise DocsError(f"{path}: unknown generated block {name!r}")
        if name in found:
            raise DocsError(f"{path}: duplicate generated block {name!r}")
        if text[match.end() : match.end() + 1] != "\n":
            raise DocsError(f"{path}: BEGIN marker for {name!r} must end its line")
        end = text.find(END_MARKER, match.end())
        if end < 0:
            raise DocsError(f"{path}: generated block {name!r} has no END marker")
        nested = text.find("<!-- BEGIN GENERATED:", match.end(), end)
        if nested >= 0:
            raise DocsError(f"{path}: generated block {name!r} contains another BEGIN marker")
        if text[max(0, end - 1) : end] != "\n":
            raise DocsError(f"{path}: END marker for {name!r} must start its line")
        if text[end + len(END_MARKER) : end + len(END_MARKER) + 1] not in {"", "\n"}:
            raise DocsError(f"{path}: END marker for {name!r} must end its line")
        content_end = end
        found[name] = (match.start(), end + len(END_MARKER), match.end() + 1, content_end)
    return found


def read_doc(root: Path, location: BlockLocation) -> Path:
    path = root / location.path
    if not path.is_file():
        raise DocsError(f"{location.path} does not exist")
    return path


def verify(root: Path, names: tuple[str, ...]) -> list[str]:
    errors: list[str] = []
    for name in names:
        location = BLOCKS[name]
        try:
            content = RENDERERS[name](root)
            path = read_doc(root, location)
            text = path.read_text(encoding="utf-8")
            blocks = locate_blocks(text, location.path)
        except DocsError as exc:
            errors.append(str(exc))
            continue
        if name not in blocks:
            errors.append(
                f"{location.path}: missing generated block {name!r} "
                f"(expected in {location.anchor}); insert the BEGIN/END markers first"
            )
            continue
        _, _, content_start, content_end = blocks[name]
        if text[content_start:content_end] != content + "\n":
            errors.append(
                f"{location.path}: generated block {name!r} is stale; "
                "rerun tools/gen_docs.py --write"
            )
    return errors


def rewrite(root: Path, names: tuple[str, ...]) -> list[str]:
    errors: list[str] = []
    for name in names:
        location = BLOCKS[name]
        try:
            content = RENDERERS[name](root)
            path = read_doc(root, location)
            text = path.read_text(encoding="utf-8")
            blocks = locate_blocks(text, location.path)
        except DocsError as exc:
            errors.append(str(exc))
            continue
        if name not in blocks:
            errors.append(
                f"{location.path}: missing generated block {name!r} "
                f"(expected in {location.anchor}); insert the BEGIN/END markers first"
            )
            continue
        block_start, block_end, content_start, _ = blocks[name]
        replacement = text[block_start:content_start] + content + "\n" + END_MARKER
        text = text[:block_start] + replacement + text[block_end:]
        path.write_text(text, encoding="utf-8")
    return errors


# --- Selftest ---------------------------------------------------------------


def selftest() -> int:
    failures: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    with tempfile.TemporaryDirectory(prefix="gen_docs_selftest_") as tmp:
        workspace = Path(tmp) / "gen"
        shutil.copytree(FIXTURE, workspace)

        errors = rewrite(workspace, BLOCK_NAMES)
        expect(not errors, "fixture --write failed: " + "; ".join(errors))
        errors = verify(workspace, BLOCK_NAMES)
        expect(not errors, "fixture --check failed after --write: " + "; ".join(errors))

        api = (workspace / "docs" / "API.md").read_text(encoding="utf-8")
        expect(
            "IOLINK_DEVICE_INIT_ERR_BAD = -7" in api,
            "device_error_codes block is not the fixture enum",
        )
        expect("| `fixture_count` | `uint32_t` |" in api, "dll_stats row is missing")
        integration = (workspace / "docs" / "API_INTEGRATION.md").read_text(encoding="utf-8")
        for name in (
            "iolink_fixture_multiline",
            "iolink_fixture_selector",
            "iolink_fixture_inline",
        ):
            expect(f"`{name}()`" in integration, f"public_api misses {name}")
        expect(
            "内联查询（公开头文件中的实现也是 API）。" in integration,
            "public_api drops the static inline @brief",
        )
        for name in ("iolink_fixture_fn_typedef", "IOLINK_FIXTURE_MAX", "member_cb"):
            expect(name not in integration, f"public_api must not list {name}")
        code_flow = (workspace / "docs" / "CODE_FLOW.md").read_text(encoding="utf-8")
        expect("iolink_dll_shared_t.b_state" in code_flow, "shared_ownership block is empty")
        expect("FOREIGN CONTENT" in code_flow, "a foreign generated block was rewritten")

        # Tampering with one number must make --check fail.
        tampered = api.replace("IOLINK_DEVICE_INIT_ERR_BAD = -7", "IOLINK_DEVICE_INIT_ERR_BAD = -8", 1)
        (workspace / "docs" / "API.md").write_text(tampered, encoding="utf-8")
        errors = verify(workspace, ("device_error_codes",))
        expect(bool(errors), "tampered enum value passed --check")

        # Restore, then exercise the hard-failure paths.
        (workspace / "docs" / "API.md").write_text(api, encoding="utf-8")

        duplicate = workspace / "docs" / "API.md"
        original = duplicate.read_text(encoding="utf-8")
        duplicate.write_text(
            original
            + "\n"
            + block_marker("device_error_codes")
            + "\n"
            + "```c\n"
            + "```\n"
            + END_MARKER
            + "\n",
            encoding="utf-8",
        )
        errors = verify(workspace, ("device_error_codes",))
        expect(
            any("duplicate generated block" in error for error in errors),
            "duplicate BEGIN was not rejected",
        )
        duplicate.write_text(original, encoding="utf-8")

        duplicate.write_text(original + "\n" + END_MARKER + "\n", encoding="utf-8")
        errors = verify(workspace, ("device_error_codes",))
        expect(
            any("END marker" in error for error in errors),
            "stray END marker was not rejected",
        )
        duplicate.write_text(original, encoding="utf-8")

        unknown = workspace / "docs" / "CODE_FLOW.md"
        saved = unknown.read_text(encoding="utf-8")
        unknown.write_text(
            saved + "\n" + block_marker("not_a_block") + "\n" + END_MARKER + "\n",
            encoding="utf-8",
        )
        errors = verify(workspace, ("shared_ownership",))
        expect(
            any("unknown generated block" in error for error in errors),
            "unknown block name was not rejected",
        )
        unknown.write_text(saved, encoding="utf-8")

        missing_marker = workspace / "docs" / "API_INTEGRATION.md"
        saved = missing_marker.read_text(encoding="utf-8")
        missing_marker.write_text(
            re.sub(r"(?s)" + re.escape(block_marker("public_api")) + ".*?" + re.escape(END_MARKER),
                   "", saved),
            encoding="utf-8",
        )
        errors = verify(workspace, ("public_api",))
        expect(
            any("missing generated block" in error for error in errors),
            "missing block was not rejected",
        )
        missing_marker.write_text(saved, encoding="utf-8")

        csv_path = workspace / SHARED_FIELDS
        csv_text = csv_path.read_text(encoding="utf-8")
        csv_path.unlink()
        errors = verify(workspace, ("shared_ownership",))
        expect(
            any("is missing" in error for error in errors),
            "missing shared_fields.csv was not rejected",
        )
        csv_path.write_text(csv_text, encoding="utf-8")
        errors = verify(workspace, ("shared_ownership",))
        expect(not errors, "restored fixture CSV still fails: " + "; ".join(errors))

    if failures:
        for failure in failures:
            print(f"gen-docs selftest FAIL: {failure}", file=sys.stderr)
        return 1
    print(f"gen-docs selftest: OK ({len(BLOCK_NAMES)} blocks)")
    return 0


# --- CLI --------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="verify committed blocks")
    mode.add_argument("--write", action="store_true", help="rewrite committed blocks")
    mode.add_argument("--selftest", action="store_true", help="verify the checker itself")
    parser.add_argument(
        "--block",
        choices=BLOCK_NAMES,
        help="only process one block",
    )
    args = parser.parse_args()

    if args.selftest:
        if args.block:
            parser.error("--selftest does not take --block")
        return selftest()

    names: tuple[str, ...] = (args.block,) if args.block else BLOCK_NAMES
    errors = rewrite(ROOT, names) if args.write else verify(ROOT, names)
    if errors:
        for error in errors:
            print(f"gen-docs: {error}", file=sys.stderr)
        return 1
    print(f"gen-docs: OK ({len(names)} block(s) up to date)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
