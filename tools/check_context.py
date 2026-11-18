#!/usr/bin/env python3
"""Check that ISR-reachable code cannot write main-owned state (and vice versa).

The gate builds a call graph from the clang AST of every ``src/*.c``
translation unit, merges it across TUs (static functions are keyed by
``file:function``), and computes which contexts reach each function. A
compile-time dispatch table (``g_dll_sm``) is not a call edge, so the functions
it references are seeded as MAIN roots; the indirect call itself is reported by
``stack_report.py``, not guessed here.

  * ``ISR``  -- ``iolink_dll_process_isr``, ``iolink_dll_on_rx_timeout``,
    ``iolink_dll_on_stack_tick``, ``iolink_device_process_isr`` and the
    documented ISR-side public entries;
  * ``MAIN`` -- ``iolink_dll_process``, ``iolink_device_process``,
    ``iolink_dll_init`` and every other public declaration.

Writes are direct scalar stores, compound assignments and increments on a
struct member. Writes performed through a pointer handed to another function
(``memcpy(&s.field, ...)``, ring bookkeeping on ``&s.ring``) are *not* judged;
the ownership rules below therefore cover the scalar handoff fields, which are
the ones a context can corrupt silently.

``tools/shared_fields.csv`` classifies every field of ``iolink_dll_shared_t``
(and any cross-context field of the main/ISR structs):

  * ``c_`` -- written only while the ISR is quiescent (init closure);
  * ``m_`` -- main writes, ISR reads;
  * ``i_`` -- ISR writes, main reads (a literal ``0`` store is the documented
    consume/clear operation);
  * ``b_`` -- both contexts write.

Violations: ISR writing ``m_``/``c_``, MAIN writing ``i_``/``c_``, a function
reachable from both contexts writing ``m_``/``i_``, ISR touching a
main-exclusive field, MAIN touching an ISR-exclusive field, a ``dll_isr_*``
function reachable from MAIN and a ``dll_main_*`` function reachable from ISR.
A shared field with no CSV row is a hard failure (the table is incomplete),
and so is a row for a field the header no longer declares.

Usage:
    tools/check_context.py                 # report-only (exit 0)
    tools/check_context.py --enforce       # exit 1 on violations
    tools/check_context.py --dump-fields   # print observed field contexts
    tools/check_context.py --selftest      # verify the gate on fixtures
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = "src"
SHARED_HEADER = "include/iolinki/internal/dll_shared.h"
MAIN_HEADER = "include/iolinki/internal/dll_main.h"
ISR_HEADER = "include/iolinki/internal/dll_isr.h"
DECLARING_HEADERS = (SHARED_HEADER, MAIN_HEADER, ISR_HEADER)
CSV_HEADER = ["struct", "field", "category", "writer", "reader", "note"]
CATEGORIES = {"c_", "m_", "i_", "b_"}
WRITERS = {"main", "isr", "both", "init"}
ALLOWED_STRUCTS = {"iolink_dll_shared_t", "iolink_dll_main_t", "iolink_dll_isr_t"}
CSV_PATH = Path("tools/shared_fields.csv")
FIXTURE = Path("tools/fixtures/gates/context")

ISR_ROOTS = {
    "iolink_dll_process_isr",
    "iolink_dll_on_rx_timeout",
    "iolink_dll_on_stack_tick",
    "iolink_device_process_isr",
}
ISR_SIDE_PUBLIC = ISR_ROOTS | {
    "iolink_isdu_od_write",
    "iolink_diag_pulse_isr",
}
MAIN_FIXED_ROOTS = {"iolink_dll_process", "iolink_device_process", "iolink_dll_init"}
INIT_ROOTS = {
    "iolink_dll_init",
    "iolink_device_init",
    "iolink_dll_publish_reply_plans",
}
ASSIGN_OPS = {
    "=",
    "+=",
    "-=",
    "*=",
    "/=",
    "%=",
    "&=",
    "|=",
    "^=",
    "<<=",
    ">>=",
}
STRUCT_OPEN_RE = re.compile(r"(?:typedef\s+)?struct\s*([A-Za-z_]\w*)?\s*\{")
TAG_ALIAS_RE = re.compile(r"typedef\s+struct\s+([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*;")
TRAILING_ALIAS_RE = re.compile(r"\s*([A-Za-z_]\w*)\s*;")
FUNCTION_POINTER_RE = re.compile(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)")
IDENT_RE = re.compile(r"[A-Za-z_]\w*")
FUNCTION_RE = re.compile(r"(?<![*\w])([a-z_]\w*)\s*\(")
STRUCT_WRAP_RE = re.compile(
    r"\b(?:const|volatile|struct|enum|union|_Nonnull|_Nullable)\b"
)


class ContextError(Exception):
    """The tree cannot be analysed; the message says why."""


@dataclass
class FieldDecl:
    struct: str
    field: str


@dataclass
class FunctionData:
    name: str
    file: str
    static: bool
    edges: set[str] = field(default_factory=set)
    writes: list[tuple[str, str, bool]] = field(default_factory=list)
    reads: set[tuple[str, str]] = field(default_factory=set)
    indirect: bool = False


@dataclass
class Violation:
    rule: str
    function: str
    detail: str
    context: str
    chain: str


def mask_source(text: str) -> str:
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
    masked = "".join(out)
    lines = masked.split("\n")
    return "\n".join(
        " " * len(line) if line.lstrip().startswith("#") else line for line in lines
    )


def matching_brace(text: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ContextError(f"unbalanced brace at offset {open_index}")


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
    return names[-1].group(0), start + names[-1].start()


def parse_struct_fields(
    text: str, path: str, aliases: dict[str, str] | None = None
) -> list[FieldDecl]:
    masked = mask_source(text)
    local = dict(aliases or {})
    local.update({tag: alias for tag, alias in TAG_ALIAS_RE.findall(masked)})
    fields: list[FieldDecl] = []
    for match in STRUCT_OPEN_RE.finditer(masked):
        tag = match.group(1)
        close = matching_brace(masked, match.end() - 1)
        body = masked[match.end() : close]
        trailing = TRAILING_ALIAS_RE.match(masked, close + 1)
        name = trailing.group(1) if trailing else local.get(tag, tag)
        if not name:
            continue
        for statement in split_statements(body):
            field_name, _ = statement_field(statement)
            if field_name is not None:
                fields.append(FieldDecl(name, field_name))
    return fields


def header_alias_map(root: Path) -> dict[str, str]:
    aliases: dict[str, str] = {}
    for path in sorted((root / "include").rglob("*.h")):
        aliases.update(
            {
                tag: alias
                for tag, alias in TAG_ALIAS_RE.findall(
                    mask_source(path.read_text(encoding="utf-8"))
                )
            }
        )
    return aliases


def declared_fields(root: Path) -> dict[str, set[str]]:
    aliases = header_alias_map(root)
    fields: dict[str, set[str]] = {}
    for relative in DECLARING_HEADERS:
        path = root / relative
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise ContextError(f"cannot read {relative}: {exc}") from exc
        for decl in parse_struct_fields(text, relative, aliases):
            fields.setdefault(decl.struct, set()).add(decl.field)
    for required in ALLOWED_STRUCTS:
        if required not in fields:
            raise ContextError(f"{relative}: struct {required} not found")
    return fields


def load_shared_fields(root: Path, declared: dict[str, set[str]]) -> dict[tuple[str, str], str]:
    path = root / CSV_PATH
    if not path.is_file():
        raise ContextError(f"{CSV_PATH} is missing")
    categories: dict[tuple[str, str], str] = {}
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream)
        try:
            header = next(reader)
        except StopIteration as exc:
            raise ContextError(f"{CSV_PATH}: empty file") from exc
        if header != CSV_HEADER:
            raise ContextError(
                f"{CSV_PATH}: expected header {','.join(CSV_HEADER)}, "
                f"got {','.join(header)}"
            )
        for number, cells in enumerate(reader, start=2):
            if not cells or all(not cell.strip() for cell in cells):
                continue
            if len(cells) != len(CSV_HEADER):
                raise ContextError(
                    f"{CSV_PATH}:{number}: expected {len(CSV_HEADER)} columns, "
                    f"got {len(cells)}"
                )
            row = dict(zip(CSV_HEADER, (cell.strip() for cell in cells)))
            struct, name = row["struct"], row["field"]
            if struct not in ALLOWED_STRUCTS:
                raise ContextError(f"{CSV_PATH}:{number}: unknown struct {struct!r}")
            if row["category"] not in CATEGORIES:
                raise ContextError(
                    f"{CSV_PATH}:{number}: category {row['category']!r} is invalid"
                )
            if row["writer"] not in WRITERS or row["reader"] not in WRITERS:
                raise ContextError(
                    f"{CSV_PATH}:{number}: writer/reader must be one of "
                    f"{', '.join(sorted(WRITERS))}"
                )
            if name not in declared.get(struct, set()):
                raise ContextError(
                    f"{CSV_PATH}:{number}: {struct} has no field {name!r}"
                )
            key = (struct, name)
            if key in categories:
                raise ContextError(f"{CSV_PATH}:{number}: duplicate row {struct}.{name}")
            categories[key] = row["category"]
    shared_missing = sorted(
        name
        for name in declared["iolink_dll_shared_t"]
        if ("iolink_dll_shared_t", name) not in categories
    )
    if shared_missing:
        raise ContextError(
            f"{CSV_PATH}: unclassified field(s) in iolink_dll_shared_t: "
            + ", ".join(shared_missing)
        )
    return categories


def parse_public_declarations(root: Path) -> dict[str, bool]:
    """Return {function: is_static} for include/iolinki/*.h."""
    functions: dict[str, bool] = {}
    for path in sorted((root / "include" / "iolinki").glob("*.h")):
        masked = mask_source(path.read_text(encoding="utf-8"))
        masked = re.sub(r"\bextern\s*\{", lambda match: " " * len(match.group(0)), masked)
        start = 0
        brace = 0
        paren = 0
        for index, char in enumerate(masked):
            if char == "{":
                brace += 1
            elif char == "}":
                brace = max(0, brace - 1)
            elif char == "(":
                paren += 1
            elif char == ")":
                paren = max(0, paren - 1)
            elif char == ";" and brace == 0 and paren == 0:
                chunk = masked[start:index]
                start = index + 1
                stripped = chunk.lstrip()
                if not stripped or stripped.startswith("typedef"):
                    continue
                match = FUNCTION_RE.search(stripped)
                if match is None:
                    continue
                name = match.group(1)
                if name in {"if", "for", "while", "switch", "return", "sizeof"}:
                    continue
                if name.startswith("__"):
                    continue
                words = re.findall(r"[A-Za-z_]\w*", stripped)
                functions[name] = "static" in words
    return functions


def normalize_type(qual_type: str) -> str:
    text = STRUCT_WRAP_RE.sub(" ", qual_type)
    text = re.sub(r"[\s*]+", "", text)
    return text


def base_struct_name(node: dict) -> str:
    inner = node.get("inner") or []
    if not inner:
        return ""
    qual_type = inner[0].get("type", {}).get("qualType", "")
    return normalize_type(qual_type)


def iterator(root: dict):
    stack = [root]
    while stack:
        node = stack.pop()
        yield node
        children = node.get("inner") or []
        stack.extend(reversed(children))


def literal_zero(node: dict) -> bool:
    stack = [node]
    while stack:
        current = stack.pop()
        kind = current.get("kind")
        if kind == "IntegerLiteral":
            return current.get("value") == "0"
        if kind in {
            "ImplicitCastExpr",
            "ParenExpr",
            "CStyleCastExpr",
            "ConstantExpr",
            "ExprWithCleanups",
        }:
            stack.extend(current.get("inner") or [])
    return False


def referenced_key(node: dict, id_info: dict[str, tuple[str, bool, str]]) -> str | None:
    if node.get("kind") != "DeclRefExpr":
        return None
    ref = node.get("referencedDecl") or {}
    if ref.get("kind") != "FunctionDecl":
        return None
    info = id_info.get(ref.get("id", ""))
    if info is None:
        name = ref.get("name")
        return name
    name, is_static, file = info
    return f"{file}:{name}" if is_static else name


def callee_key(call: dict, id_info: dict[str, tuple[str, bool, str]]) -> tuple[str | None, bool]:
    """Return (callee key, indirect)."""
    inner = call.get("inner") or []
    if not inner:
        return None, True
    stack = [inner[0]]
    while stack:
        node = stack.pop()
        kind = node.get("kind")
        if kind == "DeclRefExpr":
            return referenced_key(node, id_info), False
        if kind == "MemberExpr":
            return None, True
        if kind in {
            "ImplicitCastExpr",
            "ParenExpr",
            "UnaryOperator",
            "CStyleCastExpr",
            "ConstantExpr",
        }:
            stack.extend(node.get("inner") or [])
        else:
            break
    return None, True


def normalize_path(root: Path, path: str | None, fallback: str) -> str:
    if not path:
        return fallback
    candidate = Path(path)
    if candidate.is_absolute():
        try:
            return candidate.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            return path
    return path


def walk_function(
    function: dict,
    body: dict,
    tu: str,
    id_info: dict[str, tuple[str, bool, str]],
    tracked: set[tuple[str, str]],
) -> FunctionData:
    name = function.get("name", "?")
    loc = function.get("loc") or {}
    file = loc.get("file") or (loc.get("includedFrom") or {}).get("file") or tu
    is_static = function.get("storageClass") == "static"
    data = FunctionData(name=name, file=file, static=is_static)

    # (node, write, zero)
    stack: list[tuple[dict, bool, bool]] = [(body, False, False)]
    while stack:
        node, write, zero = stack.pop()
        kind = node.get("kind")
        if kind == "FunctionDecl":
            continue
        if kind in {"BinaryOperator", "CompoundAssignOperator"}:
            opcode = node.get("opcode", "")
            inner = node.get("inner") or []
            if opcode in ASSIGN_OPS and len(inner) >= 2:
                stack.append((inner[1], False, False))
                stack.append((inner[0], True, literal_zero(inner[1])))
                continue
            stack.extend((child, False, False) for child in inner)
            continue
        if kind == "UnaryOperator" and node.get("opcode") in {"++", "--"}:
            for child in node.get("inner") or []:
                stack.append((child, True, False))
            continue
        if kind == "CallExpr":
            key, indirect = callee_key(node, id_info)
            if indirect:
                data.indirect = True
            elif key:
                data.edges.add(key)
            stack.extend((child, False, False) for child in node.get("inner") or [])
            continue
        if kind == "MemberExpr":
            field_name = node.get("name", "")
            struct = base_struct_name(node)
            if (struct, field_name) in tracked:
                if write:
                    data.writes.append((struct, field_name, zero))
                else:
                    data.reads.add((struct, field_name))
            stack.extend((child, False, False) for child in node.get("inner") or [])
            continue
        stack.extend((child, write, zero) for child in node.get("inner") or [])

    return data


def analyse_unit(
    root: Path, tu: str, tracked: set[tuple[str, str]]
) -> tuple[dict[str, FunctionData], set[str]]:
    path = root / tu
    command = [
        "clang",
        "-std=c99",
        f"-I{root / 'include'}",
        f"-I{root / 'src'}",
        "-Xclang",
        "-ast-dump=json",
        "-fsyntax-only",
        str(path),
    ]
    try:
        proc = subprocess.run(
            command, capture_output=True, text=True, errors="replace"
        )
    except OSError as exc:
        raise ContextError(f"cannot run clang: {exc}") from exc
    if proc.returncode != 0:
        raise ContextError(f"clang cannot parse {tu}: " + proc.stderr[-500:])
    try:
        ast = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise ContextError(f"cannot decode the clang AST of {tu}: {exc}") from exc

    id_info: dict[str, tuple[str, bool, str]] = {}
    functions: list[dict] = []
    for node in iterator(ast):
        if node.get("kind") == "FunctionDecl" and node.get("name"):
            loc = node.get("loc") or {}
            file = loc.get("file") or (loc.get("includedFrom") or {}).get("file") or tu
            is_static = node.get("storageClass") == "static"
            if node.get("id"):
                id_info[node["id"]] = (
                    node["name"],
                    is_static,
                    normalize_path(root, file, tu),
                )
            if any(child.get("kind") == "CompoundStmt" for child in node.get("inner") or []):
                functions.append(node)

    unit: dict[str, FunctionData] = {}
    for function in functions:
        body = next(
            child
            for child in function.get("inner") or []
            if child.get("kind") == "CompoundStmt"
        )
        data = walk_function(function, body, tu, id_info, tracked)
        data.file = normalize_path(root, data.file, tu)
        key = f"{data.file}:{data.name}" if data.static else data.name
        if key in unit:
            existing = unit[key]
            existing.edges |= data.edges
            existing.writes.extend(data.writes)
            existing.reads |= data.reads
            existing.indirect = existing.indirect or data.indirect
        else:
            unit[key] = data

    # Compile-time dispatch tables (g_dll_sm and friends) reference actions
    # outside any function body; those targets are dispatched indirectly at
    # runtime, so seed them as MAIN roots instead of pretending the table is a
    # call edge.
    table_targets: set[str] = set()
    for node in ast.get("inner") or []:
        if node.get("kind") == "FunctionDecl":
            continue
        for child in iterator(node):
            if child.get("kind") == "DeclRefExpr":
                key = referenced_key(child, id_info)
                if key:
                    table_targets.add(key)
    return unit, table_targets


def build_graph(
    root: Path, tracked: set[tuple[str, str]], jobs: int
) -> tuple[dict[str, FunctionData], set[str]]:
    source = root / SOURCE_DIR
    units = sorted(path.name for path in source.glob("*.c"))
    if not units:
        raise ContextError(f"no translation units in {SOURCE_DIR}/")
    graph: dict[str, FunctionData] = {}
    table_targets: set[str] = set()
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [
            pool.submit(analyse_unit, root, f"{SOURCE_DIR}/{unit}", tracked)
            for unit in units
        ]
        for future in futures:
            unit, targets = future.result()
            table_targets |= targets
            for key, data in unit.items():
                if key in graph:
                    graph[key].edges |= data.edges
                    graph[key].writes.extend(data.writes)
                    graph[key].reads |= data.reads
                    graph[key].indirect = graph[key].indirect or data.indirect
                else:
                    graph[key] = data
    return graph, table_targets


def reachable(
    graph: dict[str, FunctionData], roots: set[str]
) -> tuple[set[str], dict[str, str | None]]:
    parents: dict[str, str | None] = {}
    queue: list[str] = []
    for root in sorted(roots):
        if root in graph and root not in parents:
            parents[root] = None
            queue.append(root)
    index = 0
    while index < len(queue):
        key = queue[index]
        index += 1
        for target in sorted(graph[key].edges):
            if target in graph and target not in parents:
                parents[target] = key
                queue.append(target)
    return set(parents), parents


def chain(key: str, parents: dict[str, str | None]) -> str:
    names: list[str] = []
    current: str | None = key
    while current is not None:
        names.append(current.split(":")[-1] if ":" in current else current)
        current = parents.get(current)
    names.reverse()
    return " -> ".join(names)


def analyse(
    root: Path, jobs: int
) -> tuple[
    list[Violation],
    dict[str, FunctionData],
    dict[tuple[str, str], str],
    set[str],
]:
    declared = declared_fields(root)
    categories = load_shared_fields(root, declared)
    public = parse_public_declarations(root)
    main_roots = set(MAIN_FIXED_ROOTS)
    main_roots.update(
        name
        for name, is_static in public.items()
        if not is_static and name not in ISR_SIDE_PUBLIC
    )
    isr_roots = set(ISR_ROOTS)
    init_roots = set(INIT_ROOTS)

    tracked: set[tuple[str, str]] = set(categories)
    for struct in ("iolink_dll_main_t", "iolink_dll_isr_t"):
        tracked.update((struct, name) for name in declared[struct])
    tracked.discard(("iolink_dll_main_t", "shared"))
    graph, table_targets = build_graph(root, tracked, jobs)
    main_roots |= table_targets

    isr_contexts, isr_parents = reachable(graph, isr_roots)
    main_contexts, main_parents = reachable(graph, main_roots)
    init_contexts, _ = reachable(graph, init_roots)
    contexts = {
        key: {
            label
            for label, members in (("ISR", isr_contexts), ("MAIN", main_contexts))
            if key in members
        }
        for key in set(isr_contexts | main_contexts)
    }

    violations: list[Violation] = []
    seen: set[tuple[str, str, str]] = set()

    def report(rule: str, key: str, detail: str) -> None:
        dedupe = (rule, key, detail)
        if dedupe in seen:
            return
        seen.add(dedupe)
        context = "ISR" if "ISR" in contexts.get(key, set()) else "MAIN"
        parents = isr_parents if context == "ISR" else main_parents
        leaf = key.split(":")[-1]
        violations.append(Violation(rule, leaf, detail, context, chain(key, parents)))

    for key, data in sorted(graph.items()):
        members = contexts.get(key, set())
        name = data.name
        if name.startswith("dll_isr_") and "MAIN" in members and key not in init_contexts:
            report("isr-helper-in-main", key, f"{name} is dll_isr_* but reachable from MAIN")
        if name.startswith("dll_main_") and "ISR" in members:
            report("main-helper-in-isr", key, f"{name} is dll_main_* but reachable from ISR")

        for struct, field_name, zero in data.writes:
            category = categories.get((struct, field_name))
            if struct == "iolink_dll_shared_t" and category is None:
                report(
                    "unclassified",
                    key,
                    f"{struct}::{field_name} is written but has no CSV row",
                )
                continue
            if category is not None:
                if category == "c_":
                    if key not in init_contexts:
                        report(
                            "non-init-writes-c",
                            key,
                            f"{struct}::{field_name} (c_) written outside the init closure",
                        )
                elif category == "m_" and "ISR" in members:
                    rule = "dual-writes-m" if members == {"ISR", "MAIN"} else "isr-writes-m"
                    report(rule, key, f"{struct}::{field_name} (m_) written from the ISR")
                elif category == "i_" and "ISR" in members and "MAIN" in members:
                    report(
                        "dual-writes-i",
                        key,
                        f"{struct}::{field_name} (i_) written by a dual-context function",
                    )
                elif category == "i_" and "MAIN" in members and not zero and key not in init_contexts:
                    report(
                        "main-writes-i",
                        key,
                        f"{struct}::{field_name} (i_) written by MAIN",
                    )
                continue
            if struct == "iolink_dll_main_t" and "ISR" in members and field_name != "shared":
                report(
                    "isr-touches-main-field",
                    key,
                    f"{struct}::{field_name} is main-exclusive but written from the ISR",
                )
            if struct == "iolink_dll_isr_t" and "MAIN" in members:
                report(
                    "main-touches-isr-field",
                    key,
                    f"{struct}::{field_name} is ISR-exclusive but written from MAIN",
                )

        for struct, field_name in data.reads:
            if (struct, field_name) in categories:
                continue
            if struct == "iolink_dll_main_t" and "ISR" in members and field_name != "shared":
                report(
                    "isr-touches-main-field",
                    key,
                    f"{struct}::{field_name} is main-exclusive but read from the ISR",
                )
            if struct == "iolink_dll_isr_t" and "MAIN" in members:
                report(
                    "main-touches-isr-field",
                    key,
                    f"{struct}::{field_name} is ISR-exclusive but read from MAIN",
                )

    return violations, graph, categories, table_targets


def dump_fields(
    graph: dict[str, FunctionData],
    categories: dict[tuple[str, str], str],
    root: Path,
    table_targets: set[str],
) -> None:
    isr_contexts, _ = reachable(graph, set(ISR_ROOTS))
    main_roots = set(MAIN_FIXED_ROOTS) | table_targets
    main_roots.update(
        name
        for name, is_static in parse_public_declarations(root).items()
        if not is_static and name not in ISR_SIDE_PUBLIC
    )
    main_contexts, _ = reachable(graph, main_roots)
    summary: dict[tuple[str, str], dict[str, set[str]]] = {}
    for key, data in graph.items():
        members = set()
        if key in isr_contexts:
            members.add("ISR")
        if key in main_contexts:
            members.add("MAIN")
        for struct, field_name, _ in data.writes:
            summary.setdefault((struct, field_name), {"write": set(), "read": set()})[
                "write"
            ] |= members
        for struct, field_name in data.reads:
            summary.setdefault((struct, field_name), {"write": set(), "read": set()})[
                "read"
            ] |= members
    for (struct, field_name), groups in sorted(summary.items()):
        category = categories.get((struct, field_name), "-")
        print(
            f"{struct}.{field_name} [{category}] "
            f"write={','.join(sorted(groups['write'])) or '-'} "
            f"read={','.join(sorted(groups['read'])) or '-'}"
        )


def run(root: Path, jobs: int, enforce: bool, dump: bool) -> int:
    try:
        violations, graph, categories, table_targets = analyse(root, jobs)
    except ContextError as exc:
        print(f"check-context: {exc}", file=sys.stderr)
        return 1
    if dump:
        dump_fields(graph, categories, root, table_targets)
    for violation in violations:
        print(
            f"context: [{violation.rule}] {violation.function} -- {violation.detail}",
            file=sys.stderr,
        )
        print(f"  {violation.context} chain: {violation.chain}", file=sys.stderr)
    edges = sum(len(data.edges) for data in graph.values())
    if violations:
        print(
            f"check-context: {len(violations)} violation(s); {len(graph)} functions, "
            f"{edges} call edges" + ("" if enforce else " (report-only)"),
            file=sys.stderr,
        )
        return 1 if enforce else 0
    print(f"check-context: OK ({len(graph)} functions, {edges} call edges)")
    return 0


def selftest(jobs: int) -> int:
    failures = 0
    expected_bad = {
        ("isr-writes-m", "isr_helper", "m_scalar"),
        ("main-writes-i", "main_helper", "i_scalar"),
        ("dual-writes-m", "dual_writes", "m_scalar"),
    }
    try:
        violations, _, _, _ = analyse(ROOT / FIXTURE / "bad", jobs)
    except ContextError as exc:
        failures += 1
        print(f"selftest FAIL: bad fixture: {exc}", file=sys.stderr)
        violations = []
    actual = set()
    for violation in violations:
        name = violation.function
        field_name = re.search(r"::(\w+)", violation.detail)
        actual.add((violation.rule, name, field_name.group(1) if field_name else ""))
    if actual != expected_bad:
        failures += 1
        print(
            f"selftest FAIL: bad fixture: expected {sorted(expected_bad)}, "
            f"got {sorted(actual)}",
            file=sys.stderr,
        )

    try:
        clean, _, _, _ = analyse(ROOT / FIXTURE / "clean", jobs)
    except ContextError as exc:
        failures += 1
        print(f"selftest FAIL: clean fixture: {exc}", file=sys.stderr)
        clean = [Violation("error", "", str(exc), "", "")]
    if clean:
        failures += 1
        print(
            f"selftest FAIL: clean fixture reported {len(clean)} violation(s): "
            f"{[violation.rule for violation in clean]}",
            file=sys.stderr,
        )

    try:
        analyse(ROOT / FIXTURE / "bad_csv", jobs)
    except ContextError:
        pass
    else:
        failures += 1
        print("selftest FAIL: incomplete CSV was accepted", file=sys.stderr)

    if failures:
        return 1
    print("context selftest: OK (ISR->m_, MAIN->i_, dual->m_, missing CSV row)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="tree to analyse")
    parser.add_argument("--enforce", action="store_true", help="exit 1 on violations")
    parser.add_argument("--dump-fields", action="store_true", help="print field contexts")
    parser.add_argument("--selftest", action="store_true", help="verify the gate itself")
    parser.add_argument(
        "--jobs",
        type=int,
        default=min(8, os.cpu_count() or 1),
        help="parallel translation units",
    )
    args = parser.parse_args()

    if args.selftest:
        return selftest(args.jobs)
    return run(args.root.resolve(), args.jobs, args.enforce, args.dump_fields)


if __name__ == "__main__":
    sys.exit(main())
