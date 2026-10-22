#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
End-to-end check of the fixed Annex A M-sequence types against the Device.

TYPE_1_1 is legacy interleave and is rejected by a V1.1.5 Device, so it is not
exercised here. Run against the ISR build for spec-conformant framing:

    IOLINK_DEVICE_PATH=build_isr/examples/host_demo/host_demo \
        python3 tools/virtual_master/test_m_sequence_types.py
"""

import os
import subprocess
import sys
import time

from virtual_master import VirtualMaster, MSequenceType


def _launch(master, m_type, pd_len=2):
    device_path = os.environ.get(
        "IOLINK_DEVICE_PATH", "build_isr/examples/host_demo/host_demo"
    )
    return subprocess.Popen(
        [device_path, master.get_device_tty(), str(int(m_type)), str(pd_len)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=os.environ.copy(),
    )


def _run_case(m_type, pd_out):
    master = VirtualMaster(m_seq_type=m_type)
    proc = _launch(master, m_type)
    try:
        time.sleep(0.8)
        if not master.run_startup_sequence():
            print(f"❌ Type {m_type.name}: startup failed")
            return False
        master.go_to_operate()
        time.sleep(0.1)

        # The Device publishes PD-in for the NEXT cycle: run two identical
        # cycles and inspect the second (PD-in reflects the first PD-out).
        master.run_cycle(pd_out=pd_out)
        resp = master.run_cycle(pd_out=pd_out)
        if int(master.pd_in_len) == 0:
            expected_pd = b""
        elif int(master.pd_out_len) > 0:
            # The demo echoes PD-out + 1 into PD-in.
            expected_pd = bytes((b + 1) & 0xFF for b in pd_out)
        else:
            # PD-in exists but the type has no PD-out to echo.
            expected_pd = bytes(int(master.pd_in_len))
        ok = resp.valid and resp.pd == expected_pd
        print(
            f"{'✅' if ok else '❌'} Type {m_type.name}: "
            f"pd_out={pd_out.hex()} pd_in={resp.pd.hex()} "
            f"(od={master.od_len}, pd_in_len={master.pd_in_len})"
        )
        return ok
    finally:
        proc.terminate()
        proc.wait()
        master.close()


def test_fixed_types():
    cases = [
        # (type, PD-out sent in the WRITE request)
        (MSequenceType.TYPE_1_2, b""),
        (MSequenceType.TYPE_1_V, b""),
        (MSequenceType.TYPE_2_1, b""),
        (MSequenceType.TYPE_2_2, b""),
        (MSequenceType.TYPE_2_3, b"\x10"),
        (MSequenceType.TYPE_2_4, b"\x10\x20"),
        (MSequenceType.TYPE_2_5, b"\x10"),
    ]
    results = [(t.name, _run_case(t, pd)) for t, pd in cases]
    failed = [name for name, ok in results if not ok]
    print(f"\n{'✅ All fixed types passed' if not failed else '❌ Failed: ' + ', '.join(failed)}")
    return not failed


def test_type_1_1_rejected():
    try:
        VirtualMaster(m_seq_type=MSequenceType.TYPE_1_1)
    except ValueError:
        print("✅ TYPE_1_1 rejected by the Master")
        return True
    print("❌ TYPE_1_1 was accepted")
    return False


def main():
    print("=" * 60)
    print("Fixed M-Sequence Types Test Suite (ISR build)")
    print("=" * 60)
    ok = test_fixed_types() and test_type_1_1_rejected()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
