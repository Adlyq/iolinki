"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import os
import sys
import pytest
from virtual_master.crc import checksum6
from virtual_master.protocol import DeviceResponse

"""
Test the Device reply layout of the multi-octet M-sequence types
(Figures A.9/A.10/A.13/A.14): a READ reply is [OD...][PD-in...] CKS and a WRITE
reply is [PD-in...] CKS with no OD.
"""


sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def test_read_reply_od_first():
    """A READ reply carries the OD octets before the PD-in octets."""
    od = 0x11
    pd = 0xA5
    ck = checksum6(bytes([od, pd, 0x00]))
    resp = DeviceResponse(bytes([od, pd, ck]), od_len=1, pd_in_len=1)

    assert resp.checksum_ok is True
    assert resp.pd_valid is True
    assert resp.od == od
    assert resp.payload == bytes([od])
    assert resp.pd == bytes([pd])


def test_write_reply_has_no_od():
    """A WRITE reply is [PD-in] CKS with no OD octet."""
    pd = 0xA5
    ck = checksum6(bytes([pd, 0x00]))
    resp = DeviceResponse(bytes([pd, ck]), od_len=0, pd_in_len=1)

    assert resp.checksum_ok is True
    assert resp.pd_valid is True
    assert resp.od == 0
    assert resp.payload == b""
    assert resp.pd == bytes([pd])


def test_read_reply_two_octet_od():
    """TYPE_2_V READ replies carry two OD octets before the PD-in octets."""
    od0 = 0x01
    od1 = 0x02
    pd = 0x99
    ck = checksum6(bytes([od0, od1, pd, 0x00]))
    resp = DeviceResponse(bytes([od0, od1, pd, ck]), od_len=2, pd_in_len=1)

    assert resp.checksum_ok is True
    assert resp.od == od0
    assert resp.od2 == od1
    assert resp.pd == bytes([pd])


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
