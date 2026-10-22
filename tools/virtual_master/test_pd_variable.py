"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import subprocess
import os
import time
import sys
import unittest
from virtual_master.master import VirtualMaster
from virtual_master.protocol import MSequenceType


class TestVariablePD(unittest.TestCase):
    """TYPE_2_V is the only M-sequence with variable Process Data (A.14)."""

    def setUp(self):
        self.master = VirtualMaster()
        self.device_tty = self.master.get_device_tty()
        self.env = os.environ.copy()
        self.env["IOLINK_DEVICE_PATH"] = os.environ.get(
            "IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo"
        )

    def tearDown(self):
        self.master.close()

    def _run_2v_case(self, pd_len):
        process = subprocess.Popen(
            [
                self.env["IOLINK_DEVICE_PATH"],
                self.device_tty,
                str(int(MSequenceType.TYPE_2_V)),
                str(pd_len),
            ],
            stdout=sys.stdout,
            stderr=sys.stderr,
            env=self.env,
        )
        try:
            time.sleep(1)
            self.master.m_seq_type = MSequenceType.TYPE_2_V
            self.master.set_pd_length(pd_len, pd_len)
            self.assertTrue(self.master.run_startup_sequence())
            self.master.go_to_operate()

            test_data = bytes((0x10 + i) & 0xFF for i in range(pd_len))
            expected = bytes((0x11 + i) & 0xFF for i in range(pd_len))
            resp = None
            for _ in range(3):
                resp = self.master.run_cycle(pd_out=test_data)

            self.assertIsNotNone(resp)
            self.assertTrue(resp.valid)
            self.assertEqual(resp.pd, expected)
            print(f"   ✅ PD {pd_len}-byte exchange successful")
        finally:
            process.terminate()

    def test_type2_v_pd4(self):
        """Test Type 2_V with 4-byte PD."""
        print("\n[TEST] Type 2_V with PD Len 4")
        self._run_2v_case(4)

    def test_type2_v_pd32(self):
        """Test Type 2_V with 32-byte PD (maximum)."""
        print("\n[TEST] Type 2_V with PD Len 32")
        self._run_2v_case(32)

    def test_persistence_application_tag(self):
        """Test if Application Tag persists across reboots (simulated)."""
        print("\n[TEST] Application Tag Persistence")

        process = subprocess.Popen(
            [
                self.env["IOLINK_DEVICE_PATH"],
                self.device_tty,
                str(int(MSequenceType.TYPE_2_V)),
                "2",
            ],
            stdout=sys.stdout,
            stderr=sys.stderr,
            env=self.env,
        )
        try:
            time.sleep(1)
            self.master.m_seq_type = MSequenceType.TYPE_2_V
            self.master.set_pd_length(2, 2)
            self.master.run_startup_sequence()
            self.master.go_to_operate()

            new_tag = "PersistTest"
            self.assertTrue(self.master.write_isdu(0x0018, 0, new_tag.encode()))
            print(f"   ✅ Tag '{new_tag}' written")

            process.terminate()
            process.wait()

            process = subprocess.Popen(
                [
                    self.env["IOLINK_DEVICE_PATH"],
                    self.device_tty,
                    str(int(MSequenceType.TYPE_2_V)),
                    "2",
                ],
                stdout=sys.stdout,
                stderr=sys.stderr,
                env=self.env,
            )
            time.sleep(1)
            self.master.m_seq_type = MSequenceType.TYPE_2_V
            self.master.set_pd_length(2, 2)
            self.master.run_startup_sequence()
            self.master.go_to_operate()

            read_tag = self.master.read_isdu(0x0018, 0)
            print(f"   🔍 Tag after reboot: {read_tag}")
            self.assertIsNotNone(read_tag)
        finally:
            process.terminate()


if __name__ == "__main__":
    unittest.main()
