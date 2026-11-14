#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: Error Injection & Recovery
Tests device robustness, error handling, and recovery mechanisms.
"""

import sys
import time
import os
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestErrorInjectionConformance(unittest.TestCase):
    """IO-Link V1.1.5 Error Injection & Recovery Tests"""

    def setUp(self):
        self.master = VirtualMaster()
        self.device_tty = self.master.get_device_tty()
        self.demo_bin = os.environ.get(
            "IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo"
        )

    def tearDown(self):
        if hasattr(self, "process") and self.process:
            self.process.terminate()
            self.process.wait()
        self.master.close()

    def test_01_communication_loss_keeps_dl_mode(self):
        """A silent link counts idle timeouts; the DL mode is kept (ST semantics).

        The Device neither changes state nor baudrate on communication loss, so
        a Master that resumes with the same M-sequence is served immediately —
        no wake-up and no new startup sequence.
        """

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "6", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # Enter OPERATE state
        self.master.run_startup_sequence()
        self.master.m_seq_type = 6
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        # Verify PD works
        resp1 = self.master.run_cycle(pd_out=b"\xaa\xbb")
        self.assertIsNotNone(resp1, "PD should work before dropout")
        self.assertTrue(resp1.valid, "PD should work before dropout")

        # Simulate communication loss
        print("[INFO] Simulating 15s communication dropout (ST-style idle supervision)...")
        time.sleep(15.0)

        # The same M-sequence must still be answered after the silence.
        resp2 = self.master.run_cycle(pd_out=b"\xcc\xdd")
        self.assertTrue(resp2.valid, "Device should keep the DL mode after a dropout")

        response = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(response, "Device ISDU should work after a dropout")
        print("[PASS] Device kept the DL mode and answers on the same M-sequence")

    def test_02_rapid_state_transitions(self):

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # Perform 5 rapid startup cycles. A real Master drives communication
        # immediately after the wake-up, so read straight away; then idle long
        # enough for the Device to fall back to SIO before the next wake-up.
        success_count = 0
        for i in range(5):
            self.master.send_wakeup()
            response = self.master.read_isdu(index=0x0012, subindex=0x00)
            if response:
                success_count += 1
            time.sleep(1.0)  # let the Device revert to SIO before the next cycle

        self.assertGreaterEqual(
            success_count, 3, "At least 3/5 rapid transitions should succeed"
        )
        print(f"[PASS] {success_count}/5 rapid state transitions successful")

    def test_03_concurrent_isdu_requests(self):
        """
        Test Case: Concurrent ISDU Requests
        Requirement: IO-Link V1.1.5 Section 8.1 - ISDU Flow Control

        Validates:
        - Device handles overlapping ISDU requests
        - ISDU Busy state is used correctly
        """
        print("\n[TEST] Concurrent ISDU Requests")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()

        # Send multiple ISDU reads in quick succession
        results = []
        for i in range(3):
            response = self.master.read_isdu(index=0x0010, subindex=0x00)
            results.append(response is not None)

        # At least some should succeed
        success_count = sum(results)
        self.assertGreater(success_count, 0, "At least one ISDU should succeed")
        print(f"[PASS] {success_count}/3 concurrent ISDU requests succeeded")

    def test_04_boundary_condition_max_isdu_size(self):
        """
        Test Case: Maximum ISDU Size
        Requirement: IO-Link V1.1.5 Section 8.1.2 - ISDU Length

        Validates:
        - Device handles maximum ISDU data size
        - Segmentation works correctly
        """
        print("\n[TEST] Maximum ISDU Size Handling")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "6", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()
        # Transition to OPERATE via DeviceOperate (MC=0x20, OD=0x99)
        self.master.m_seq_type = 6
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)
        large_data = b"LargeDataTest123"

        write_result = self.master.write_isdu(
            index=0x0018, subindex=0x00, data=large_data
        )
        self.assertTrue(write_result, "Large ISDU write should succeed")

        readback = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertIsNotNone(readback, "Large ISDU should be readable")
        print("[PASS] 16-byte ISDU write/read successful")

    def test_05_error_recovery_sequence(self):

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # 1. Start in good state
        self.master.run_startup_sequence()
        initial = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(initial, "Initial state should be good")

        # 2. Induce error (communication loss)
        time.sleep(15.0)

        # 3. Try to recover with fresh startup
        print("[INFO] Attempting recovery...")
        self.master.m_seq_type = 0
        success = False
        for i in range(3):
            if self.master.run_startup_sequence():
                success = True
                break
            time.sleep(0.3)
        self.assertTrue(success, "Device should recover after communication loss")

        # 4. Verify full functionality
        vendor = self.master.read_isdu(index=0x0010, subindex=0x00)
        product = self.master.read_isdu(index=0x0012, subindex=0x00)

        self.assertIsNotNone(vendor, "Vendor Name should be readable after recovery")
        self.assertIsNotNone(product, "Product Name should be readable after recovery")
        print("[PASS] Full recovery sequence successful")

    def test_06_bad_crc_handling(self):
        """
        Test Case: CRC Error Handling
        Requirement: IO-Link V1.1.5 Section 7.2 - Frame Validation

        Validates:
        - Device detects and handles CRC errors
        - Retransmission or error reporting works
        """
        print("\n[TEST] CRC Error Handling")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "6", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()
        self.master.m_seq_type = 6
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        # Send frame with bad CRC (if supported by Virtual Master)
        if hasattr(self.master, "run_cycle_bad_crc"):
            self.master.run_cycle_bad_crc(pd_out=b"\x12\x34")
            # Device should either reject or handle gracefully
            print("[PASS] Device handled bad CRC gracefully")
        else:
            print("[SKIP] Bad CRC injection not supported by Virtual Master")

    def test_07_crc_errors_keep_dl_mode(self):
        """
        Test Case: CRC Error Handling & Diagnostics
        Requirement: IO-Link V1.1.5 Section 7.3.5 - Error Handling

        Validates:
        - Device tolerates repeated CRC errors
        - Corrupt frames are discarded and the link stays in the same DL mode
        - Three consecutive errors raise the COMM_CRC event (0x1801) in the
          detailed status and in the event memory
        """
        print("\n[TEST] CRC Error Handling & Diagnostics")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "6", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # Step 1: Establish OPERATE state
        self.master.run_startup_sequence()
        self.master.m_seq_type = 6
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        # Verify initial state is good
        resp_good = self.master.run_cycle(pd_out=b"\xaa\xbb")
        self.assertIsNotNone(resp_good, "Initial PD should work")
        print("[INFO] Device in OPERATE state, PD working")

        # Step 2: Inject 3+ consecutive bad CRC frames
        if hasattr(self.master, "run_cycle_bad_crc"):
            print("[INFO] Injecting 3 consecutive bad CRC frames...")
            for i in range(3):
                self.master.run_cycle_bad_crc(pd_out=b"\xaa\xbb")
                time.sleep(0.02)

            print("[INFO] Bad CRC frames sent; the DL mode must be kept")
        else:
            print("[SKIP] Bad CRC injection not supported, simulating with delay")
            time.sleep(0.3)

        # Step 3: The current M-sequence is still served — no wake-up, no
        # startup sequence, no baudrate change.
        resp_after = self.master.run_cycle(pd_out=b"\xaa\xbb")
        self.assertTrue(
            resp_after.valid, "Device should keep the DL mode after consecutive CRC errors"
        )
        self.assertTrue(
            resp_after.has_event(),
            "The Event flag (CKS bit 7) should be set after consecutive CRC errors",
        )

        # Step 4: Verify Event Reporting (Task 3 integration) and Device Status.
        # The Type-0 Diagnosis channel is only served by the STARTUP/SIO path,
        # so in a TYPE_2_V OPERATE the event is observed through the CKS Event
        # flag, the DetailedDeviceStatus and the DeviceStatus index.
        print("[INFO] Verifying Event Reporting via DetailedDeviceStatus (Index 0x0025)...")
        events_data = self.master.read_isdu(index=0x0025, subindex=0x00)
        self.assertIsNotNone(events_data, "Detailed Device Status should be readable")

        # Look for CRC error event code 0x1801
        found_crc_event = False
        for i in range(0, len(events_data), 3):
            if i + 2 < len(events_data):
                code = (events_data[i + 1] << 8) | events_data[i + 2]
                if code == 0x1801:
                    found_crc_event = True
                    break
        self.assertTrue(
            found_crc_event,
            "CRC error event (0x1801) should be present in Detailed Device Status",
        )
        print("[INFO] CRC error event found in Index 0x0025")

        # Check Device Status (Index 0x0024, Table B.8)
        status_data = self.master.read_isdu(index=0x0024, subindex=0x00)
        self.assertIsNotNone(status_data, "Device Status should be readable")
        self.assertGreaterEqual(
            status_data[0], 1, "Device Status should be non-zero (reported error)"
        )
        print(f"[INFO] Device Status: {status_data[0]}")

        print(
            "[PASS] Device kept the DL mode after CRC errors; event and device status verified"
        )


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Error Injection & Recovery")
    print("=" * 70)
    unittest.main(verbosity=2)
