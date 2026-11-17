#!/usr/bin/env python3
#
# Copyright (C) 2026 Andrii Shylenko
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This file is part of iolinki.
# See LICENSE for details.
"""ROM/RAM measurement matrix for the structural configurations.

Measures a minimal device application for two delivery forms, two
garbage-collection settings and LTO on/off:

  source   all objects on the link line (what the examples do)
  archive  the same objects packed into libiolinki.a, application links the .a

  gc on    -Wl,--gc-sections
  gc off   no section GC

  lto off  one translation unit per source file (the default build)
  lto on   -flto at compile and link (docs/MEMORY_GUIDE.md suggests it)

Facts this tool is built around (measured, not assumed):

  * the library build does not enable -ffunction-sections/-fdata-sections,
    so --gc-sections works at object granularity inside libiolinki.a;
  * the constant capability table is a GC root: every function pointer in it
    pins its implementation, so GC can only reclaim implementations the table
    does not list, never a listed branch that is unused at run time;
  * a non-LTO .a is extracted object by object, so "source" and "archive" are
    close when the application references most of the API;
  * the probe application stays a non-LTO object even in the LTO rows: its
    memcpy/memset definitions are what -nostdlib links against, and LTO drops
    them after deciding they are unused while later re-introducing the call.
    Firmware with libc never hits this.

Usage:
    ./tools/size_matrix.py --update   # rewrite tools/size_baseline.csv
    ./tools/size_matrix.py --check    # rebuild and compare against the baseline
    ./tools/size_matrix.py --table    # human-readable table (CD artefact)
    ./tools/size_matrix.py --check --selftest
                                      # inject forged text/.rodata entries and
                                      # require --check to return non-zero

Environment:
    IOLINKI_SIZE_TOOLCHAIN  target toolchain: `atfe` (default) or `gcc`
    ARM_ATFE                ATFE install prefix (default: /opt/atfe/bin)
    ARM_GCC                 gcc toolchain prefix, `gcc` mode (default: arm-none-eabi-)
    IOLINKI_SIZE_DIR  build root (default: <repo>/build/size)

The baseline records the toolchain that produced it. With a matching toolchain
the per-row 0.5% budget applies. A different toolchain is compared with the
wider cross-toolchain budget (see CROSS_TOOLCHAIN_*_PERMILLE), because register
allocation and string merging differ between compilers; regenerate the
baseline with `--update` after an intentional change. Each toolchain has its
own baseline file, so the shipped numbers are measured with the compiler the
firmware is built with.
"""

from __future__ import annotations

import argparse
import csv
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(os.environ.get("IOLINKI_SIZE_DIR", ROOT / "build" / "size"))

# The size numbers are meaningful only per toolchain: ATFE (clang + llvm) is
# what the firmware is built with, gcc stays available for comparison and for
# hosts without the Arm toolchain installed. Each mode keeps its own baseline
# so a measurement is never compared against another compiler's baseline.
TOOLCHAIN = os.environ.get("IOLINKI_SIZE_TOOLCHAIN", "atfe")
if TOOLCHAIN == "gcc":
    _gcc_prefix = os.environ.get("ARM_GCC", "arm-none-eabi-")
    CC = [_gcc_prefix + "gcc"]
    AR = [_gcc_prefix + "ar"]
    SIZE = [_gcc_prefix + "size"]
    # gcc needs its runtime for __aeabi_* helpers; its linker also accepts the
    # RWX warning switch that lld rejects.
    LINK_RUNTIME = ["-lgcc"]
    LINK_EXTRA = ["-Wl,--no-warn-rwx-segments"]
    BASELINE = ROOT / "tools" / "size_baseline.csv"
elif TOOLCHAIN == "atfe":
    _atfe = Path(os.environ.get("ARM_ATFE", "/opt/atfe/bin"))
    CC = [str(_atfe / "clang"), "--target=arm-none-eabi"]
    AR = [str(_atfe / "llvm-ar")]
    SIZE = [str(_atfe / "llvm-size")]
    LINK_RUNTIME = ["-lclang_rt.builtins"]
    LINK_EXTRA = []
    BASELINE = ROOT / "tools" / "size_baseline.atfe.csv"
else:
    sys.stderr.write(f"unknown IOLINKI_SIZE_TOOLCHAIN: {TOOLCHAIN} (atfe|gcc)\n")
    raise SystemExit(2)

# 跨工具链比较的预算（‰ of the baseline）。工具链与基线一致时使用基线里
# 逐行记录的 0.5% 容差；不一致时（CI 的 apt 工具链 vs 本地发布的
# Arm GNU Toolchain）改用这两项，见 check()。
CROSS_TOOLCHAIN_TEXT_PERMILLE = 20
CROSS_TOOLCHAIN_RODATA_PERMILLE = 80

# Cortex-M0+ without LTO: the STM32G0 examples' target and options.
CPU_FLAGS = ["-mcpu=cortex-m0plus", "-mthumb", "-std=c99", "-Os", "-g0", "-ffreestanding"]
# The budget is a ROM budget, not a diagnostics budget: log messages (and the
# format strings behind them) are compiled out here, because a device that
# keeps them pays for them by choice. `docs/MEMORY_GUIDE.md` states the same.
LOG_OFF = ["-DIOLINK_LOG_COMPILE_LEVEL=0"]
COMMON_FLAGS = (
    CPU_FLAGS
    + ["-I" + str(ROOT / "include"), "-ffunction-sections", "-fdata-sections"]
    + LOG_OFF
)

SOURCES = [
    "src/capability.c",
    "src/profile.c",
    "src/crc.c",
    "src/dll.c",
    "src/dll_frame.c",
    "src/dll_sm.c",
    "src/dll_reply.c",
    "src/dll_api.c",
    "src/dll_isr.c",
    "src/dll_isr_rx.c",
    "src/od.c",
    "src/isdu.c",
    "src/isdu_cmd.c",
    "src/isdu_ds.c",
    "src/events.c",
    "src/diag.c",
    "src/platform.c",
    "src/data_storage.c",
    "src/device_info.c",
    "src/device.c",
    "src/phy_generic.c",
]

APP = r"""
/* Minimal application: init once, then the main pump. No libc, no globals
 * except the device context (which the application owns by design). */
#include <stddef.h>
#include <stdint.h>
#include "iolinki/device.h"

static uint64_t app_time_us(iolink_app_t* self)
{
    (void) self;
    return 0U;
}

static int app_phy_init(iolink_app_t* self)
{
    (void) self;
    return 0;
}

static void app_phy_set_mode(iolink_app_t* self, iolink_phy_mode_t mode)
{
    (void) self;
    (void) mode;
}

static void app_phy_set_baudrate(iolink_app_t* self, iolink_baudrate_t baudrate)
{
    (void) self;
    (void) baudrate;
}

static iolink_device_ctx_t g_ctx;
static iolink_device_config_t g_cfg;
static iolink_phy_api_t g_phy;
static iolink_stack_config_t g_stack;
static const iolink_device_app_t g_app = {.self = NULL, .time_us = app_time_us};

static const iolink_device_info_t g_info = {
    .vendor_name = "iolinki",
    .product_name = "size probe",
    .product_id = "size-probe",
    .serial_number = "0000000001",
    .hardware_revision = "1",
    .firmware_revision = "1",
    .vendor_id = 1234U,
    .device_id = 5678U,
    .min_cycle_time = 10U,
    .revision_id = IOLINK_REVISION_ID_1_1,
};

void* memset(void* dst, int c, size_t n)
{
    uint8_t* d = dst;
    while (n-- != 0U) {
        *d++ = (uint8_t) c;
    }
    return dst;
}

size_t strlen(const char* s)
{
    size_t n = 0U;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void* memcpy(void* dst, const void* src, size_t n)
{
    uint8_t* d = dst;
    const uint8_t* s = src;
    while (n-- != 0U) {
        *d++ = *s++;
    }
    return dst;
}

volatile uint32_t g_ticks;

void reset_handler(void)
{
    g_phy.init = app_phy_init;
    g_phy.set_mode = app_phy_set_mode;
    g_phy.set_baudrate = app_phy_set_baudrate;
    g_stack.m_seq_type = IOLINK_M_SEQ_TYPE_1_V;
    g_stack.od_width = 8U;
    g_cfg.phy = &g_phy;
    g_cfg.stack = &g_stack;
    g_cfg.app = &g_app;
    g_cfg.device_info = &g_info;
    if (iolink_device_init(&g_ctx, &g_cfg) != 0) {
        for (;;) {
            g_ticks++;
        }
    }
    for (;;) {
        iolink_device_process(&g_ctx);
        g_ticks++;
    }
}
"""

# name, defines
CONFIGS = [
    ("c1_full", []),
    ("c3_minimal", ["-DIOLINK_FEATURE_DATA_STORAGE=0", "-DIOLINK_FEATURE_EVENTS=0"]),
    ("cap_events_off", ["-DIOLINK_FEATURE_EVENTS=0"]),
    ("cap_data_storage_off", ["-DIOLINK_FEATURE_DATA_STORAGE=0"]),
    ("cap_isr_frame", ["-DIOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME",
                       "-DIOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME",
                       "-DIOLINK_PHY_FEATURE_TX_BUSY=1"]),
    ("cap_stats_split", ["-DIOLINK_STATS_MODE=IOLINK_STATS_SPLIT"]),
    ("cap_single", ["-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_SINGLE"]),
]

DELIVERIES = ["source", "archive"]
GC_MODES = ["on", "off"]
LTO_MODES = ["off", "on"]


def run(cmd: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(1)
    return result.stdout


def toolchain_version() -> str:
    out = run([*CC, "--version"])
    return out.splitlines()[0].strip()


def build_config(name: str, defines: list[str], lto: str) -> dict[str, dict[str, int]]:
    flags = COMMON_FLAGS + (["-flto"] if lto == "on" else [])
    cfg_dir = BUILD / (name if lto == "off" else f"{name}_lto")
    obj_dir = cfg_dir / "obj"
    if cfg_dir.exists():
        shutil.rmtree(cfg_dir)
    obj_dir.mkdir(parents=True)

    app_c = cfg_dir / "app.c"
    app_c.write_text(APP)
    app_o = cfg_dir / "app.o"
    run([*CC, *COMMON_FLAGS, *defines, "-c", str(app_c), "-o", str(app_o)])

    objects: list[Path] = []
    for src in SOURCES:
        obj = obj_dir / (Path(src).stem + ".o")
        run([*CC, *flags, *defines, "-c", str(ROOT / src), "-o", str(obj)])
        objects.append(obj)

    lib = cfg_dir / "libiolinki.a"
    run([*AR, "rcs", str(lib), *[str(o) for o in objects]])

    rows: dict[str, dict[str, int]] = {}
    app_cfg = cfg_dir / "app_cfg.o"
    # The application uses the same profile as the library, but never -flto:
    # see the note in the module docstring.
    run([*CC, *COMMON_FLAGS, *defines, "-c", str(app_c), "-o", str(app_cfg)])

    for delivery in DELIVERIES:
        for gc in GC_MODES:
            elf = cfg_dir / f"size-{delivery}-{gc}.elf"
            link = [
                *CC,
                *CPU_FLAGS,
                "-nostdlib",
                "-nostartfiles",
                "-Wl,-e,reset_handler",
                *LINK_EXTRA,
            ]
            if gc == "on":
                link.append("-Wl,--gc-sections")
            if lto == "on":
                link.append("-flto")
            if delivery == "source":
                link += [str(app_cfg), *[str(o) for o in objects]]
            else:
                link += [str(app_cfg), str(lib)]
            link += [*LINK_RUNTIME, "-o", str(elf)]
            run(link)
            rows[f"{delivery}|{gc}"] = section_sizes(elf)
    return rows


def section_sizes(elf: Path) -> dict[str, int]:
    out = run([*SIZE, "-A", str(elf)])
    sizes = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        section, size = parts[0], parts[1]
        try:
            value = int(size)
        except ValueError:
            continue
        if section == ".text":
            sizes["text"] += value
        elif section == ".rodata":
            sizes["rodata"] += value
        elif section in (".data",):
            sizes["data"] += value
        elif section in (".bss", "COMMON"):
            sizes["bss"] += value
    return sizes


def collect() -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for name, defines in CONFIGS:
        for lto in LTO_MODES:
            sizes = build_config(name, defines, lto)
            for delivery in DELIVERIES:
                for gc in GC_MODES:
                    s = sizes[f"{delivery}|{gc}"]
                    rows.append(
                        {
                            "config": name,
                            "delivery": delivery,
                            "gc": gc,
                            "lto": lto,
                            "text": str(s["text"]),
                            "rodata": str(s["rodata"]),
                            "data": str(s["data"]),
                            "bss": str(s["bss"]),
                            # 0.5% of the full image, rounded up, as the design
                            # suggests for a single capability; the whole-image
                            # budget is 3%.
                            "text_tol": str(max(64, (sizes["source|on"]["text"] * 5) // 1000)),
                            # .rodata 里放的是表（OD/能力/诊断/状态机），同样
                            # 是 0.5% 预算：只门控 text 会放过表膨胀。
                            "rodata_tol": str(
                                max(16, (sizes["source|on"]["rodata"] * 5) // 1000)
                            ),
                        }
                    )
    return rows


def write_baseline(rows: list[dict[str, str]]) -> None:
    with BASELINE.open("w", newline="") as handle:
        handle.write("# 由 ./tools/size_matrix.py --update 生成；不要手改数字\n")
        handle.write(f"# toolchain: {toolchain_version()}\n")
        handle.write("# opt: -Os; lto: off/on; cpu: cortex-m0plus\n")
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "config",
                "delivery",
                "gc",
                "lto",
                "text",
                "rodata",
                "data",
                "bss",
                "text_tol",
                "rodata_tol",
            ],
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(rows)


def read_baseline() -> tuple[str, list[dict[str, str]]]:
    toolchain = ""
    rows: list[dict[str, str]] = []
    with BASELINE.open(newline="") as handle:
        lines = handle.readlines()
    body = []
    for line in lines:
        if line.startswith("#"):
            if "toolchain:" in line:
                toolchain = line.split("toolchain:", 1)[1].strip()
            continue
        body.append(line)
    rows = list(csv.DictReader(body))
    return toolchain, rows


def key(row: dict[str, str]) -> str:
    return f"{row['config']}|{row['delivery']}|{row['gc']}|{row.get('lto', 'off')}"


def print_table(current: list[dict[str, str]], baseline: list[dict[str, str]] | None) -> None:
    base = {key(r): r for r in baseline or []}
    print(
        f"{'config':22} {'delivery':9} {'gc':3} {'lto':3} "
        f"{'text':>7} {'rodata':>7} {'data':>6} {'bss':>7}"
    )
    for row in current:
        old = base.get(key(row))
        delta = ""
        if old is not None:
            d = int(row["text"]) - int(old["text"])
            delta = f"  ({d:+d})"
        print(
            f"{row['config']:22} {row['delivery']:9} {row['gc']:3} {row['lto']:3} "
            f"{row['text']:>7} {row['rodata']:>7} {row['data']:>6} {row['bss']:>7}{delta}"
        )


def check(current: list[dict[str, str]], baseline: list[dict[str, str]], base_toolchain: str) -> int:
    # 不同 GCC 版本的寄存器分配、内联与字符串合并策略不同，同一份源码的
    # .text 会整体偏移约 1-1.5%，.rodata 约 6-7%（实测 15.3.1 vs 14.2.1）。
    # 严格的 0.5% 门禁只在工具链与基线一致时成立；不一致时按下面的
    # 跨工具链预算比较，避免把编译器差异当成回归。
    cross = bool(base_toolchain) and base_toolchain != toolchain_version()
    if cross:
        print(
            "warning: toolchain differs from the baseline "
            f"({base_toolchain} != {toolchain_version()})",
            file=sys.stderr,
        )
        print(
            "note: cross-toolchain budget: text "
            f"{CROSS_TOOLCHAIN_TEXT_PERMILLE / 10:.1f}%, .rodata "
            f"{CROSS_TOOLCHAIN_RODATA_PERMILLE / 10:.1f}%",
            file=sys.stderr,
        )
    base = {key(r): r for r in baseline}
    failures = 0
    current_keys = {key(r) for r in current}
    for row in baseline:
        if key(row) not in current_keys:
            print(f"FAIL {key(row)}: baseline entry was not measured", file=sys.stderr)
            failures += 1
    for row in current:
        old = base.get(key(row))
        if old is None:
            print(f"FAIL {key(row)}: no baseline entry", file=sys.stderr)
            failures += 1
            continue
        delta = int(row["text"]) - int(old["text"])
        text_tol = int(old["text_tol"])
        if cross:
            text_tol = max(
                text_tol, int(old["text"]) * CROSS_TOOLCHAIN_TEXT_PERMILLE // 1000
            )
        if delta > text_tol:
            print(
                f"FAIL {key(row)}: text grew by {delta} B (tolerance {text_tol} B)",
                file=sys.stderr,
            )
            failures += 1
        # 旧基线没有 rodata_tol 列：按同一配置 source|on 行的 0.5% 规则推导，
        # 与 write_baseline() 的算法一致（逐行自身推导会差 1 B）。
        source_on = base.get(f"{row['config']}|source|on|{row['lto']}")
        fallback_src = int(source_on["rodata"]) if source_on else int(old["rodata"])
        rodata_tol = int(old.get("rodata_tol") or max(16, (fallback_src * 5) // 1000))
        if cross:
            rodata_tol = max(
                rodata_tol, int(old["rodata"]) * CROSS_TOOLCHAIN_RODATA_PERMILLE // 1000
            )
        rdelta = int(row["rodata"]) - int(old["rodata"])
        if rdelta > rodata_tol:
            print(
                f"FAIL {key(row)}: .rodata grew by {rdelta} B (tolerance {rodata_tol} B)",
                file=sys.stderr,
            )
            failures += 1
        # .bss is dominated by the application-owned device context (the probe
        # declares it as a file-scope object), so only .data is a "no globals"
        # signal here. A non-zero .data would mean an initialised global.
        if int(row["data"]) != 0:
            print(
                f"FAIL {key(row)}: .data = {int(row['data'])} B (no initialised globals allowed)",
                file=sys.stderr,
            )
            failures += 1
    if failures:
        return 1
    print(f"size gate: PASS ({len(current)} measurements within tolerance)")
    return 0


def selftest(current: list[dict[str, str]], baseline: list[dict[str, str]]) -> int:
    forged = [dict(r) for r in baseline]
    # Forge the baseline *downwards* so the real measurement looks like a
    # +1024 B regression (text and .rodata). Forging it upwards makes the
    # current value look smaller, and a smaller image always passes.
    forged[0]["text"] = str(int(forged[0]["text"]) - 1024)
    forged[0]["text_tol"] = "64"
    old = baseline[0]["text"]
    baseline[0] = forged[0]
    try:
        rc = check(current, baseline, "")
    finally:
        baseline[0]["text"] = old
    if rc == 0:
        print("selftest: FAIL (a forged +1024 B entry was accepted)", file=sys.stderr)
        return 1

    # Same proof for .rodata: forge it down so the measured value looks larger.
    # The forgery must exceed the widest budget the check can apply, i.e. the
    # cross-toolchain .rodata budget (CROSS_TOOLCHAIN_RODATA_PERMILLE), so the
    # selftest proves the gate on a matching toolchain as well as on CI.
    forged_ro = dict(baseline[0])
    forged_ro["rodata"] = str(max(0, int(forged_ro["rodata"]) - 512))
    forged_ro["rodata_tol"] = "16"
    old_ro = baseline[0]["rodata"]
    old_tol = baseline[0].get("rodata_tol")
    baseline[0] = forged_ro
    try:
        rc = check(current, baseline, "")
    finally:
        baseline[0]["rodata"] = old_ro
        if old_tol is None:
            baseline[0].pop("rodata_tol", None)
        else:
            baseline[0]["rodata_tol"] = old_tol
    if rc == 0:
        print("selftest: FAIL (a forged .rodata entry was accepted)", file=sys.stderr)
        return 1
    print("selftest: PASS (forged +1024 B text and .rodata entries rejected)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true", help="rewrite the baseline")
    parser.add_argument("--check", action="store_true", help="compare against the baseline")
    parser.add_argument("--table", action="store_true", help="print the measurement table")
    parser.add_argument("--selftest", action="store_true", help="prove --check can fail")
    args = parser.parse_args()

    if not (args.update or args.check or args.table):
        parser.print_help()
        return 2

    BUILD.mkdir(parents=True, exist_ok=True)
    current = collect()

    if args.update:
        write_baseline(current)
        print(f"wrote {BASELINE.relative_to(ROOT)} ({len(current)} measurements)")

    base_toolchain, baseline = ("", [])
    if BASELINE.exists():
        base_toolchain, baseline = read_baseline()

    if args.table:
        print_table(current, baseline)

    status = 0
    if args.check:
        if not baseline:
            print("size gate: no baseline; run --update", file=sys.stderr)
            return 1
        status = check(current, baseline, base_toolchain)
    if args.selftest:
        if not baseline:
            print("selftest: no baseline; run --update", file=sys.stderr)
            return 1
        if selftest(current, baseline) != 0:
            status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
