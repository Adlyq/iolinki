#!/usr/bin/env python3
"""Measure the target stack budget and static RAM, with a monotonic baseline.

Every ``src/*.c`` is compiled with ``arm-none-eabi-gcc`` (or the GCC-compatible
cross compiler named by ``IOLINKI_STACK_CC``) using
``-fstack-usage -fcallgraph-info=su``. The call graph and the per-function
frames come from that target build, never from host numbers.

Reported values:

  * ``main_max``  -- worst path from the main-context entry points;
  * ``isr_max``   -- worst path from the four ISR entry points;
  * ``combined``  -- ``main_max + isr_max`` (the ISR can preempt any point);
  * ``static_ram``-- .bss + .data bytes summed from ``nm -S`` per object.

Functions that contain an indirect call (``__indirect_call`` in the callgraph
file) cannot be resolved statically; each such function adds a fixed
conservative margin (``UNKNOWN_CALL_MARGIN``) and is listed in the report.
Recursion is reported as a cycle and cut at the back edge.

``--check`` fails when a value grows past ``tools/stack_baseline.json`` plus a
small tolerance; ``--update`` rewrites the baseline. A toolchain or flag
change also fails ``--check`` -- rerun ``--update`` and review the diff.

Usage:
    tools/stack_report.py               # print the current numbers
    tools/stack_report.py --check       # compare with the committed baseline
    tools/stack_report.py --update      # refresh the baseline
    tools/stack_report.py --selftest    # verify the analysis on a fixture
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = Path("tools/stack_baseline.json")
FIXTURE = Path("tools/fixtures/gates/stack")
HOST_ONLY = {"src/phy_virtual.c"}
CFLAGS = ["-std=c99", "-Os", "-fstack-usage", "-fcallgraph-info=su"]
MAIN_ROOTS = (
    "iolink_device_init",
    "iolink_device_process",
    "iolink_dll_init",
    "iolink_dll_process",
    "iolink_dll_publish_reply_plans",
)
ISR_ROOTS = (
    "iolink_device_process_isr",
    "iolink_dll_process_isr",
    "iolink_dll_on_rx_timeout",
    "iolink_dll_on_stack_tick",
)
UNKNOWN_CALL_MARGIN = 64
TOLERANCE = 8
INDIRECT = "__indirect_call"
RAM_TYPES = set("bBdDgG")
ALLOCATORS = {"malloc", "calloc", "realloc", "free", "aligned_alloc", "posix_memalign"}

NODE_RE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"\s*label:\s*"([^"]*)"')
RECURSIVE_RE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"[^}]*shape\s*:\s*triangle')
SIZE_RE = re.compile(r"(\d+) bytes")
EDGE_RE = re.compile(
    r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s*targetname:\s*"([^"]+)"'
)
NM_RE = re.compile(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)$")


class StackError(Exception):
    """The target build cannot be analysed; the message says why."""


def find_toolchain() -> tuple[str, str, str]:
    """Return (cc, nm, description). Prefer the cross GCC used for firmware."""
    override = os.environ.get("IOLINKI_STACK_CC")
    if override:
        cc = shutil.which(override) or override
        nm = os.environ.get("IOLINKI_STACK_NM") or shutil.which("arm-none-eabi-nm")
    else:
        cc = shutil.which("arm-none-eabi-gcc")
        nm = shutil.which("arm-none-eabi-nm")
    if cc is None:
        raise StackError(
            "no target compiler found (install arm-none-eabi-gcc or set IOLINKI_STACK_CC "
            "to a GCC-compatible cross compiler)"
        )
    if nm is None:
        raise StackError("no target nm found (needed for the static RAM report)")
    try:
        version = subprocess.run(
            [cc, "--version"], capture_output=True, text=True, errors="replace"
        ).stdout.splitlines()[0]
    except (OSError, IndexError) as exc:
        raise StackError(f"cannot run {cc}: {exc}") from exc
    return cc, nm, version.strip()


def compile_unit(cc: str, root: Path, relative: str, output: Path) -> tuple[bool, str]:
    path = root / relative
    if not path.is_file():
        return False, f"{relative}: missing"
    command = [cc] + CFLAGS + [
        f"-I{root / 'include'}",
        f"-I{root / 'src'}",
        "-c",
        str(path),
        "-o",
        str(output),
    ]
    proc = subprocess.run(command, capture_output=True, text=True, errors="replace")
    return proc.returncode == 0, proc.stderr.strip()


def build(root: Path, work: Path) -> list[Path]:
    cc, nm, version = find_toolchain()
    source = root / "src"
    units = sorted(path.name for path in source.glob("*.c"))
    if not units:
        raise StackError("no translation units in src/")
    objects: list[Path] = []
    for unit in units:
        relative = f"src/{unit}"
        output = work / f"{Path(unit).stem}.o"
        ok, message = compile_unit(cc, root, relative, output)
        if not ok:
            if relative in HOST_ONLY:
                continue
            raise StackError(f"target build of {relative} failed:\n{message}")
        objects.append(output)
    if not objects:
        raise StackError("no objects produced")
    return objects


def parse_callgraph(
    path: Path,
) -> tuple[dict[str, int], list[tuple[str, str]], set[str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    sizes: dict[str, int] = {}
    for match in NODE_RE.finditer(text):
        title, label = match.group(1), match.group(2)
        size = SIZE_RE.search(label)
        value = int(size.group(1)) if size else 0
        sizes[title] = max(sizes.get(title, 0), value)
    edges = [(source, target) for source, target in EDGE_RE.findall(text)]
    recursive = {match.group(1) for match in RECURSIVE_RE.finditer(text)}
    return sizes, edges, recursive


def static_ram(nm: str, objects: list[Path]) -> tuple[int, list[str]]:
    total = 0
    proc = subprocess.run(
        [nm, "-S", "--size-sort", *[str(path) for path in objects]],
        capture_output=True,
        text=True,
        errors="replace",
    )
    if proc.returncode != 0:
        raise StackError(f"{nm} failed: {proc.stderr.strip()}")
    for line in proc.stdout.splitlines():
        match = NM_RE.match(line.strip())
        if match is None:
            continue
        size, kind = int(match.group(2), 16), match.group(3)
        if kind in RAM_TYPES:
            total += size
    unresolved = subprocess.run(
        [nm, "-u", *[str(path) for path in objects]],
        capture_output=True,
        text=True,
        errors="replace",
    )
    if unresolved.returncode != 0:
        raise StackError(f"{nm} -u failed: {unresolved.stderr.strip()}")
    undefined = {
        line.split()[-1] for line in unresolved.stdout.splitlines() if line.split()
    }
    return total, sorted(undefined & ALLOCATORS)


def relativize(title: str, root: Path) -> str:
    prefix = root.resolve().as_posix() + "/"
    return title[len(prefix) :] if title.startswith(prefix) else title


def analyse(root: Path) -> dict:
    cc, nm, version = find_toolchain()
    with tempfile.TemporaryDirectory(prefix="iolinki-stack-") as temp:
        work = Path(temp)
        objects = build(root, work)
        sizes: dict[str, int] = {}
        edges: list[tuple[str, str]] = []
        recursive_nodes: set[str] = set()
        for obj in objects:
            callgraph = obj.with_suffix(".ci")
            if not callgraph.is_file():
                raise StackError(f"{obj.name}: callgraph file missing")
            unit_sizes, unit_edges, unit_recursive = parse_callgraph(callgraph)
            for title, size in unit_sizes.items():
                name = relativize(title, root)
                sizes[name] = max(sizes.get(name, 0), size)
            edges.extend(
                (relativize(source, root), relativize(target, root))
                for source, target in unit_edges
            )
            recursive_nodes |= {relativize(name, root) for name in unit_recursive}
        ram, allocators = static_ram(nm, objects)

    graph: dict[str, set[str]] = {name: set() for name in sizes}
    indirect: set[str] = set()
    for source, target in edges:
        if target == INDIRECT:
            indirect.add(source)
            continue
        if source in graph:
            graph[source].add(target)

    def effective(name: str) -> int:
        return sizes.get(name, 0) + (UNKNOWN_CALL_MARGIN if name in indirect else 0)

    cycles: list[list[str]] = []

    def longest(name: str, stack: list[str], memo: dict[str, tuple[int, list[str]]]) -> tuple[int, list[str]]:
        if name in memo:
            return memo[name]
        if name in stack:
            cycles.append(stack[stack.index(name) :] + [name])
            return 0, [name]
        best_total = 0
        best_path: list[str] = []
        for target in sorted(graph.get(name, ())):
            if target not in sizes:
                continue
            total, path = longest(target, stack + [name], memo)
            if total > best_total:
                best_total, best_path = total, path
        result = (effective(name) + best_total, [name] + best_path)
        memo[name] = result
        return result

    memo: dict[str, tuple[int, list[str]]] = {}
    main_max, main_path = 0, []
    for root_name in MAIN_ROOTS:
        if root_name in graph:
            total, path = longest(root_name, [], memo)
            if total > main_max:
                main_max, main_path = total, path
    isr_max, isr_path = 0, []
    for root_name in ISR_ROOTS:
        if root_name in graph:
            total, path = longest(root_name, [], memo)
            if total > isr_max:
                isr_max, isr_path = total, path

    if main_max == 0:
        raise StackError("no main-context stack root found in the callgraph")
    if isr_max == 0:
        raise StackError("no ISR stack root found in the callgraph")

    # Cycles may live outside the root paths; scan every node so a recursive
    # component cannot hide behind an unused entry point. GCC also folds some
    # recursive components into a loop and marks the node with a triangle
    # instead of emitting the back edge, so both markers are collected.
    for name in sorted(graph):
        longest(name, [], memo)
    recursion = sorted(recursive_nodes | {cycle[-1] for cycle in cycles})

    return {
        "toolchain": version,
        "flags": " ".join(CFLAGS),
        "main_roots": list(MAIN_ROOTS),
        "isr_roots": list(ISR_ROOTS),
        "main_max": main_max,
        "isr_max": isr_max,
        "combined": main_max + isr_max,
        "static_ram_bytes": ram,
        "unknown_call_margin_bytes": UNKNOWN_CALL_MARGIN,
        "indirect_call_functions": sorted(indirect),
        "recursion": [[name] for name in recursion],
        "main_path": main_path,
        "isr_path": isr_path,
        "allocators": allocators,
        "tolerance_bytes": TOLERANCE,
    }


def baseline_path(root: Path, override: Path | None) -> Path:
    return override if override is not None else root / BASELINE


def compare(current: dict, baseline: dict) -> list[str]:
    problems: list[str] = []
    for key in ("toolchain", "flags"):
        if current.get(key) != baseline.get(key):
            print(
                f"warning: {key} changed ({baseline.get(key)!r} -> {current.get(key)!r}); "
                "numeric comparison only, refresh the baseline deliberately",
                file=sys.stderr,
            )
    tolerance = int(baseline.get("tolerance_bytes", TOLERANCE))
    for key in ("main_max", "isr_max", "combined", "static_ram_bytes"):
        old = int(baseline.get(key, 0))
        new = int(current.get(key, 0))
        if new > old + tolerance:
            problems.append(
                f"{key} grew {new - old} bytes ({old} -> {new}, tolerance {tolerance})"
            )
    baseline_recursion = {tuple(item) for item in baseline.get("recursion", [])}
    for cycle in current.get("recursion", []):
        if tuple(cycle) not in baseline_recursion:
            problems.append("recursion: " + " -> ".join(cycle))
    return problems


def print_report(data: dict) -> None:
    print(f"stack: main_max={data['main_max']} isr_max={data['isr_max']} "
          f"combined={data['combined']} static_ram={data['static_ram_bytes']}")
    print(
        "  static RAM counts module .data/.bss only; the protocol contexts are "
        "allocated by the caller"
    )
    print("  main path: " + " -> ".join(data["main_path"]))
    print("  isr path:  " + " -> ".join(data["isr_path"]))
    print(
        f"  toolchain: {data['toolchain']} [{data['flags']}], "
        f"indirect-call margin {data['unknown_call_margin_bytes']} B per function"
    )
    if data["indirect_call_functions"]:
        print("  indirect calls: " + ", ".join(data["indirect_call_functions"]))
    for cycle in data["recursion"]:
        print("  recursion: " + " -> ".join(cycle))
    if data["allocators"]:
        print("  dynamic allocation referenced: " + ", ".join(data["allocators"]))


def run(root: Path, baseline_override: Path | None, update: bool, check: bool) -> int:
    try:
        current = analyse(root)
    except StackError as exc:
        print(f"stack-report: {exc}", file=sys.stderr)
        return 1
    print_report(current)
    path = baseline_path(root, baseline_override)
    if update:
        path.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"stack-report: baseline written to {path}")
        return 0
    if not check:
        return 0
    if not path.is_file():
        print(f"stack-report: {path} is missing; run --update", file=sys.stderr)
        return 1
    baseline = json.loads(path.read_text(encoding="utf-8"))
    problems = compare(current, baseline)
    if problems:
        for problem in problems:
            print(f"stack-report: {problem}", file=sys.stderr)
        return 1
    print("stack-report: within baseline")
    return 0


def selftest() -> int:
    failures = 0
    try:
        data = analyse(ROOT / FIXTURE)
    except StackError as exc:
        print(f"selftest FAIL: fixture build: {exc}", file=sys.stderr)
        return 1
    if data["main_max"] < 200:
        failures += 1
        print(
            f"selftest FAIL: deep fixture chain measured {data['main_max']} bytes",
            file=sys.stderr,
        )
    if not data["recursion"]:
        failures += 1
        print("selftest FAIL: recursion in the fixture was not detected", file=sys.stderr)

    baseline = dict(data)
    baseline["main_max"] = data["main_max"] - 1000
    baseline["combined"] = data["combined"] - 1000
    problems = compare(data, baseline)
    if not any("main_max grew" in problem for problem in problems):
        failures += 1
        print(
            "selftest FAIL: forged shallow baseline did not fail the gate",
            file=sys.stderr,
        )

    exact = compare(data, data)
    if exact:
        failures += 1
        print(f"selftest FAIL: identical baseline reported {exact}", file=sys.stderr)

    if failures:
        return 1
    print(
        "stack selftest: OK (deep chain measured, recursion detected, "
        "shallower baseline rejected)"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="tree to analyse")
    parser.add_argument("--baseline", type=Path, default=None, help="override the baseline path")
    parser.add_argument("--check", action="store_true", help="compare with the baseline")
    parser.add_argument("--update", action="store_true", help="rewrite the baseline")
    parser.add_argument("--selftest", action="store_true", help="verify the analysis itself")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if args.check and args.update:
        parser.error("--check and --update are exclusive")
    return run(args.root.resolve(), args.baseline, args.update, args.check)


if __name__ == "__main__":
    sys.exit(main())
