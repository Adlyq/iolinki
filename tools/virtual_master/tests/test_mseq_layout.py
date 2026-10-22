"""
Annex A M-sequence request/reply layout oracle (hardware independent).

These tests pin the frame the Virtual Master emits for every fixed type. They
run without a Device or a PTY, so CI always validates the Master side even when
the ISR-capable hardware is unavailable.
"""

import pytest

from virtual_master.protocol import (
    DeviceResponse,
    MSequenceGenerator,
    MSequenceType,
)
from virtual_master.crc import checksum6


# (type, od_width, pd_in_len, pd_out_len)
CASES = [
    (MSequenceType.TYPE_1_2, 2, 0, 0),
    (MSequenceType.TYPE_1_V, 8, 0, 0),
    (MSequenceType.TYPE_1_V, 32, 0, 0),
    (MSequenceType.TYPE_2_1, 1, 1, 0),
    (MSequenceType.TYPE_2_2, 1, 2, 0),
    (MSequenceType.TYPE_2_3, 1, 0, 1),
    (MSequenceType.TYPE_2_4, 1, 0, 2),
    (MSequenceType.TYPE_2_5, 1, 1, 1),
    (MSequenceType.TYPE_2_V, 2, 2, 2),
]


def _assert_ckt(frame, m_type):
    assert frame[1] & 0xC0 == MSequenceType.ckt_bits(m_type)
    body = bytearray(frame)
    body[1] &= 0xC0
    assert checksum6(bytes(body)) == (frame[1] & 0x3F)


@pytest.mark.parametrize("m_type, od, pd_in, pd_out", CASES)
def test_read_request_layout(m_type, od, pd_in, pd_out):
    del pd_in
    gen = MSequenceGenerator(od_len=od, m_seq_type=m_type, pd_out_len=pd_out)
    pd = bytes((0x10 + i) & 0xFF for i in range(pd_out))
    od_bytes = bytes([0xAA]) * od

    frame = gen.build_request(0x80, pd, od_bytes)

    expected_len = 2 + (pd_out if MSequenceType.read_request_has_pd_out(m_type) else 0)
    assert len(frame) == expected_len
    assert frame[0] == 0x80
    _assert_ckt(frame, m_type)
    if MSequenceType.read_request_has_pd_out(m_type):
        assert frame[2:] == pd


@pytest.mark.parametrize("m_type, od, pd_in, pd_out", CASES)
def test_write_request_layout(m_type, od, pd_in, pd_out):
    del pd_in
    gen = MSequenceGenerator(od_len=od, m_seq_type=m_type, pd_out_len=pd_out)
    pd = bytes((0x10 + i) & 0xFF for i in range(pd_out))
    od_bytes = bytes([0xAA]) * od

    frame = gen.build_request(0x00, pd, od_bytes)

    expected_len = 2 + pd_out + (od if MSequenceType.write_request_has_od(m_type) else 0)
    assert len(frame) == expected_len
    assert frame[0] == 0x00
    _assert_ckt(frame, m_type)
    assert frame[2 : 2 + pd_out] == pd
    if MSequenceType.write_request_has_od(m_type):
        assert frame[2 + pd_out :] == od_bytes


@pytest.mark.parametrize("m_type, od, pd_in, pd_out", CASES)
def test_reply_parsing(m_type, od, pd_in, pd_out):
    del pd_out
    od_bytes = bytes((0x40 + i) & 0xFF for i in range(od))
    pd = bytes((0x80 + i) & 0xFF for i in range(pd_in))

    read_body = od_bytes + pd
    read_cks = checksum6(read_body)
    read = DeviceResponse(read_body + bytes([read_cks]), od_len=od, pd_in_len=pd_in)
    assert read.payload == od_bytes
    assert read.pd == pd
    assert read.checksum_ok

    write_body = pd
    write_cks = checksum6(write_body)
    write = DeviceResponse(write_body + bytes([write_cks]), od_len=0, pd_in_len=pd_in)
    assert write.payload == b""
    assert write.pd == pd
    assert write.checksum_ok


def test_type_1_1_is_rejected():
    from virtual_master.master import VirtualMaster

    with pytest.raises(ValueError):
        VirtualMaster(m_seq_type=MSequenceType.TYPE_1_1)
