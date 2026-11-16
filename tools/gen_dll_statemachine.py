#!/usr/bin/env python3
"""Generate DLL state-machine artifacts from include/iolinki/internal/dll_sm.h.

The X-macro ``DLL_SM_ROWS`` in ``include/iolinki/internal/dll_sm.h`` is the single
source of truth for the state x event table. This script parses those rows
(no external dependencies) and emits:

  * ``build/statemachine/dll_state_machine.mmd``  -- mermaid state diagram
  * ``build/statemachine/dll_state_machine.csv``  -- full state x event grid
  * ``tests/test_dll_statemachine_gen.c``         -- generated coverage test
  * ``docs/CODE_FLOW.md``                          -- non-frame transitions table

``--check`` validates the grid (no holes, no duplicates) and verifies that the
committed generated test and the documentation table are up to date. It is the machine gate for the table
shape; runtime behaviour is covered by tests/test_dll_statemachine.c and the
differential gate.

Usage:
    tools/gen_dll_statemachine.py            # write artifacts
    tools/gen_dll_statemachine.py --check    # validate + verify generated test
    tools/gen_dll_statemachine.py --out DIR  # override artifact directory
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "iolinki" / "internal" / "dll_sm.h"
GENERATED_TEST = ROOT / "tests" / "test_dll_statemachine_gen.c"
DOC = ROOT / "docs" / "CODE_FLOW.md"
ADMIN_BEGIN = "<!-- BEGIN GENERATED: admin transitions (tools/gen_dll_statemachine.py) -->"
ADMIN_END = "<!-- END GENERATED -->"


EXPECTED_STATES = [
    "IOLINK_DLL_STATE_STARTUP",
    "IOLINK_DLL_STATE_AWAITING_COMM",
    "IOLINK_DLL_STATE_PREOPERATE",
    "IOLINK_DLL_STATE_ESTAB_COM",
    "IOLINK_DLL_STATE_OPERATE",
    "IOLINK_DLL_STATE_FALLBACK",
    "IOLINK_DLL_STATE_COMM_DISABLED",
]

EXPECTED_EVENTS = [
    "DLL_EV_TRANSITION",
    "DLL_EV_FALLBACK_CMD",
    "DLL_EV_PAGE_READ_T0",
    "DLL_EV_PAGE_WRITE_ADDR0",
    "DLL_EV_PAGE_WRITE",
    "DLL_EV_OD_WRITE_T0",
    "DLL_EV_T0_SHORT",
    "DLL_EV_PD_EXCHANGE",
]

ROW_RE = re.compile(
    r"X\(\s*(IOLINK_DLL_STATE_[A-Z_]+)\s*,\s*(DLL_EV_[A-Z0-9_]+)\s*,\s*"
    r"(dll_sm_[A-Za-z0-9_]+)\s*,\s*"
    r"(IOLINK_DLL_STATE_[A-Z_]+|DLL_SM_STATE_KEEP)\s*,\s*"
    r'"([^"]*)"\s*\)',
    re.MULTILINE,
)


ADMIN_ROW_RE = re.compile(r'X\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)')


class Row:
    def __init__(self, state: str, event: str, action: str, enter: str, clause: str):
        self.state = state
        self.event = event
        self.action = action
        self.enter = enter
        self.clause = clause


def parse_rows() -> list[Row]:
    text = HEADER.read_text(encoding="utf-8")
    # 宏行用反斜杠续行；先拼成一行，解析器就不必理解 C 续行。
    text = text.replace("\\\n", " ")
    rows = [Row(*m.groups()) for m in ROW_RE.finditer(text)]
    return rows


class AdminRowError(Exception):
    """DLL_SM_ADMIN_ROWS cannot be read; the message says why."""


def parse_admin_rows() -> list[tuple[str, str, str]]:
    """Rows of DLL_SM_ADMIN_ROWS: transitions the frame grid cannot express."""
    text = HEADER.read_text(encoding="utf-8").replace("\\\n", " ")
    start = text.find("#define DLL_SM_ADMIN_ROWS")
    if start < 0:
        raise AdminRowError("DLL_SM_ADMIN_ROWS block not found")
    end = text.find("/* clang-format on */", start)
    if end < 0:
        raise AdminRowError("DLL_SM_ADMIN_ROWS block is not closed with /* clang-format on */")
    block = text[start:end]
    rows = [tuple(m.groups()) for m in ADMIN_ROW_RE.finditer(block)]
    # Every X( line in the block must parse; a typo must not shrink the table.
    if block.count("X(") != len(rows):
        raise AdminRowError(
            f"{block.count('X(') - len(rows)} DLL_SM_ADMIN_ROWS row(s) do not match "
            'X("a", "b", "c")'
        )
    return rows


def render_admin_table(rows: list[tuple[str, str, str]]) -> str:
    lines = [ADMIN_BEGIN, "| 迁移 | 触发 | 代码 |", "| :--- | :--- | :--- |"]
    lines += [f"| {a} | {b} | {c} |" for a, b, c in rows]
    lines.append(ADMIN_END)
    return "\n".join(lines) + "\n"


def sync_admin_doc(rows: list[tuple[str, str, str]], check_only: bool) -> bool:
    """Write or verify the generated table block in docs/CODE_FLOW.md."""
    expected = render_admin_table(rows)
    text = DOC.read_text(encoding="utf-8")
    begin = text.find(ADMIN_BEGIN)
    end = text.find(ADMIN_END, begin if begin >= 0 else 0)
    if begin < 0 or end < 0:
        print(f"statemachine: {DOC} has no generated admin block", file=sys.stderr)
        return False
    end += len(ADMIN_END) + 1
    if check_only:
        if text[begin:end] != expected:
            print(
                "statemachine: docs/CODE_FLOW.md admin table is stale; "
                "rerun tools/gen_dll_statemachine.py",
                file=sys.stderr,
            )
            return False
        return True
    DOC.write_text(text[:begin] + expected + text[end:], encoding="utf-8")
    return True


def validate(rows: list[Row]) -> list[str]:
    errors: list[str] = []
    seen: dict[tuple[str, str], Row] = {}
    for row in rows:
        if row.state not in EXPECTED_STATES:
            errors.append(f"unknown state {row.state}")
        if row.event not in EXPECTED_EVENTS:
            errors.append(f"unknown event {row.event}")
        if not row.action or not row.clause:
            errors.append(f"empty action/clause for {row.state}/{row.event}")
        key = (row.state, row.event)
        if key in seen:
            errors.append(f"duplicate cell {row.state}/{row.event}")
        seen[key] = row

    for state in EXPECTED_STATES:
        for event in EXPECTED_EVENTS:
            if (state, event) not in seen:
                errors.append(f"missing cell {state}/{event}")

    for state in EXPECTED_STATES:
        if not any(r.state == state for r in rows):
            errors.append(f"state without rows: {state}")
    return errors


def enter_name(enter: str) -> str:
    return enter


def render_mermaid(rows: list[Row]) -> str:
    lines = [
        "%% 由 tools/gen_dll_statemachine.py 从 include/iolinki/internal/dll_sm.h 生成",
        "%% 只画状态迁移边；完整 state x event 网格见同名 CSV",
        "stateDiagram-v2",
        "    [*] --> IOLINK_DLL_STATE_STARTUP",
    ]
    for row in rows:
        if row.enter == "DLL_SM_STATE_KEEP":
            continue
        lines.append(
            f"    {row.state} --> {row.enter}: {row.event} / {row.action}"
        )
    return "\n".join(lines) + "\n"


def render_csv(rows: list[Row]) -> str:
    import io

    buf = io.StringIO()
    writer = csv.writer(buf, lineterminator="\n")
    writer.writerow(["state", "event", "action", "enter_state", "clause"])
    for row in rows:
        writer.writerow([row.state, row.event, row.action, row.enter, row.clause])
    return buf.getvalue()


def render_test(rows: list[Row]) -> str:
    cells = "\n".join(
        f'    {{{row.state}, {row.event}, {row.enter}}},' for row in rows
    )
    return f"""/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AUTO-GENERATED by tools/gen_dll_statemachine.py -- do not edit.
 * Source of truth: include/iolinki/internal/dll_sm.h (DLL_SM_ROWS).
 */

/* clang-format off */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/internal/dll_sm.h"

/** @brief 生成行：状态、事件、进入状态。 */
typedef struct
{{
    uint8_t state;
    uint8_t event;
    uint8_t enter_state;
}} gen_cell_t;

static const gen_cell_t g_expected[] = {{
{cells}
}};

static void test_grid_dimensions(void** state)
{{
    (void) state;
    const uint8_t states = (uint8_t) IOLINK_DLL_STATE_COUNT;
    const uint8_t events = (uint8_t) DLL_EV_COUNT;
    assert_int_equal(states, {len(EXPECTED_STATES)}U);
    assert_int_equal(events, {len(EXPECTED_EVENTS)}U);
    assert_int_equal(sizeof(g_expected) / sizeof(g_expected[0]), (size_t) states * (size_t) events);
}}

/* 每个生成行都必须在运行时常量表中找到非 NULL 动作，且进入状态合法。 */
static void test_rows_match_runtime_table(void** state)
{{
    (void) state;
    for (size_t i = 0U; i < sizeof(g_expected) / sizeof(g_expected[0]); i++) {{
        const iolink_dll_sm_cell_t* cell =
            iolink_dll_sm_cell((iolink_dll_state_t) g_expected[i].state,
                               (iolink_dll_event_t) g_expected[i].event);
        assert_non_null(cell);
        assert_true(cell->action != NULL);
        assert_int_equal(cell->enter_state, g_expected[i].enter_state);
        assert_true((cell->enter_state == DLL_SM_STATE_KEEP) ||
                    (cell->enter_state < (uint8_t) IOLINK_DLL_STATE_COUNT));
    }}
}}

/* 越界查询必须安全返回 NULL。 */
static void test_out_of_range_query_returns_null(void** state)
{{
    (void) state;
    assert_null(iolink_dll_sm_cell((iolink_dll_state_t) IOLINK_DLL_STATE_COUNT, DLL_EV_TRANSITION));
    assert_null(iolink_dll_sm_cell(IOLINK_DLL_STATE_STARTUP, (iolink_dll_event_t) DLL_EV_COUNT));
}}

int main(void)
{{
    const struct CMUnitTest tests[] = {{
        cmocka_unit_test(test_grid_dimensions),
        cmocka_unit_test(test_rows_match_runtime_table),
        cmocka_unit_test(test_out_of_range_query_returns_null),
    }};
    return cmocka_run_group_tests(tests, NULL, NULL);
}}

/* clang-format on */
"""


def write_artifacts(rows: list[Row], out_dir: Path, update_test: bool) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "dll_state_machine.mmd").write_text(render_mermaid(rows), encoding="utf-8")
    (out_dir / "dll_state_machine.csv").write_text(render_csv(rows), encoding="utf-8")
    if update_test:
        GENERATED_TEST.write_text(render_test(rows), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate only")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "statemachine")
    args = parser.parse_args()

    rows = parse_rows()
    try:
        admin_rows = parse_admin_rows()
    except AdminRowError as exc:
        print(f"statemachine: {HEADER.relative_to(ROOT)}: {exc}", file=sys.stderr)
        return 1
    if not admin_rows:
        print(f"statemachine: {HEADER.relative_to(ROOT)}: DLL_SM_ADMIN_ROWS has no rows",
              file=sys.stderr)
        return 1
    errors = validate(rows)
    if errors:
        for error in errors:
            print(f"statemachine: {error}", file=sys.stderr)
        return 1

    expected_test = render_test(rows)
    if args.check:
        if not GENERATED_TEST.exists():
            print(f"statemachine: missing {GENERATED_TEST}", file=sys.stderr)
            return 1
        if GENERATED_TEST.read_text(encoding="utf-8") != expected_test:
            print(
                "statemachine: tests/test_dll_statemachine_gen.c is stale; "
                "rerun tools/gen_dll_statemachine.py",
                file=sys.stderr,
            )
            return 1
        if not sync_admin_doc(admin_rows, check_only=True):
            return 1
        print(
            f"statemachine: OK ({len(rows)} cells = "
            f"{len(EXPECTED_STATES)} states x {len(EXPECTED_EVENTS)} events, "
            f"{len(admin_rows)} admin transitions)"
        )
        return 0

    write_artifacts(rows, args.out, update_test=True)
    if not sync_admin_doc(admin_rows, check_only=False):
        return 1
    print(
        f"statemachine: wrote {len(rows)} cells to {args.out}, {GENERATED_TEST} "
        f"and {len(admin_rows)} admin transitions to {DOC}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
