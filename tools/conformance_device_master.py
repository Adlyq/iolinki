#!/usr/bin/env python3
"""Minimal Type-0 master for the Linux conformance reference device.

This is a first end-to-end runner for the reference integration. It exercises
startup, DPP1 access and a subset of ISDU/Config cases. It is intentionally not
a replacement for the full Test Spec runner.
"""
from __future__ import annotations

import os
import pty
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TYPE_0 = 0x00
RW = 0x80
PAGE = 0x20
ISDU = 0x60
DIAG = 0x40


def ck6_from_acc(acc: int) -> int:
    b = [(acc >> i) & 1 for i in range(8)]
    return (
        ((b[7] ^ b[5] ^ b[3] ^ b[1]) << 5)
        | ((b[6] ^ b[4] ^ b[2] ^ b[0]) << 4)
        | ((b[7] ^ b[6]) << 3)
        | ((b[5] ^ b[4]) << 2)
        | ((b[3] ^ b[2]) << 1)
        | (b[1] ^ b[0])
    )


def checksum6(octets: bytes) -> int:
    acc = 0x52
    for i, value in enumerate(octets):
        acc ^= value & 0x3F if i == 1 else value
    return ck6_from_acc(acc)


class DeviceLink:
    def __init__(self, binary: Path, profile: str = "type0"):
        self.master, self.slave = pty.openpty()
        # A fixed path collides with other build trees / CI jobs running this
        # test in parallel. Reserve a unique file and let the device recreate it.
        fd, path = tempfile.mkstemp(prefix="iolink-conformance-master-", suffix=".nvm")
        os.close(fd)
        self.nvm = Path(path)
        self.nvm.unlink(missing_ok=True)
        self.proc = subprocess.Popen(
            [str(binary), "--profile", profile, "--phy", os.ttyname(self.slave),
             "--nvm", str(self.nvm)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=False,
        )
        self.log = bytearray()
        self._wait_for("conformance device ready")

    def _wait_for(self, marker: str, timeout: float = 5.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.proc.stdout.fileno()], [], [], 0.1)
            if ready:
                chunk = os.read(self.proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                self.log.extend(chunk)
                if marker.encode() in self.log:
                    return
        raise RuntimeError(f"timeout waiting for {marker}: {self.log.decode(errors='replace')}")

    def command(self, command: str) -> str:
        self.log.clear()
        assert self.proc.stdin is not None
        self.proc.stdin.write((command + "\n").encode())
        self.proc.stdin.flush()
        self._wait_for("state=")
        return self.log.decode(errors="replace")

    def query(self, command: str, marker: str, timeout: float = 3.0) -> str:
        self.log.clear()
        assert self.proc.stdin is not None
        self.proc.stdin.write((command + "\n").encode())
        self.proc.stdin.flush()
        self._wait_for(marker, timeout=timeout)
        return self.log.decode(errors="replace")

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                assert self.proc.stdin is not None
                self.proc.stdin.write(b"quit\n")
                self.proc.stdin.flush()
            except OSError:
                pass
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=2)
        os.close(self.master)
        os.close(self.slave)
        self.nvm.unlink(missing_ok=True)

    def _read_exact(self, count: int, timeout: float = 2.0) -> bytes:
        out = bytearray()
        deadline = time.monotonic() + timeout
        while len(out) < count and time.monotonic() < deadline:
            ready, _, _ = select.select([self.master], [], [], 0.1)
            if not ready:
                continue
            chunk = os.read(self.master, count - len(out))
            if not chunk:
                break
            out.extend(chunk)
        if len(out) != count:
            raise RuntimeError(f"short read: got {out.hex()}, wanted {count}")
        return bytes(out)

    def frame(self, mc: int, payload: bytes = b"", ckt_type: int = TYPE_0) -> bytes:
        header = bytes([mc, ckt_type])
        return bytes([mc, ckt_type | checksum6(header + payload)]) + payload

    def send(self, data: bytes) -> None:
        os.write(self.master, data)

    def page_read(self, address: int) -> int:
        self.send(self.frame(PAGE | RW | address))
        return self._read_exact(2)[0]

    def page_write(self, address: int, value: int) -> int:
        self.send(self.frame(PAGE | address, bytes([value & 0xFF])))
        return self._read_exact(1)[0]

    def control(self, text: str, wait: float = 0.05) -> None:
        assert self.proc.stdin is not None
        self.proc.stdin.write((text + "\n").encode())
        self.proc.stdin.flush()
        time.sleep(wait)

    def read_diag(self, address: int) -> int:
        self.send(self.frame(DIAG | RW | address))
        return self._read_exact(2)[0]

    def write_diag(self, address: int, value: int) -> int:
        self.send(self.frame(DIAG | address, bytes([value & 0xFF])))
        return self._read_exact(1)[0]

    def event_memory(self) -> tuple[int, int, int]:
        status = self.read_diag(0x00)
        qualifier = self.read_diag(0x01)
        code = (self.read_diag(0x02) << 8) | self.read_diag(0x03)
        return status, qualifier, code

    def ack_event(self) -> None:
        self.write_diag(0x00, 0x00)
        time.sleep(0.05)

    def write_octet(self, flowctrl: int, value: int) -> int:
        self.send(self.frame(ISDU | flowctrl, bytes([value & 0xFF])))
        return self._read_exact(1)[0]

    def read_od(self, flowctrl: int) -> int:
        self.send(self.frame(ISDU | RW | flowctrl))
        return self._read_exact(2)[0]

    def startup(self) -> None:
        self.send(b"\x55")
        time.sleep(0.05)
        # First valid Type-0 read enters PREOPERATE.
        assert self.page_read(0x03) >= 0
        self.page_write(0x00, 0x9A)
        self.page_write(0x00, 0x99)
        self.page_write(0x00, 0x98)
        time.sleep(0.02)

    def startup_preoperate(self) -> None:
        self.send(b"\x55")
        time.sleep(0.05)
        assert self.page_read(0x03) >= 0
        time.sleep(0.02)

    def _request_pdu(self, index: int, subindex: int) -> bytes:
        if index < 0x100:
            service = 0x09 if subindex == 0 else 0x0A
            overhead = 3 if subindex == 0 else 4
            pdu = [(service << 4) | overhead, index & 0xFF]
            if subindex:
                pdu.append(subindex & 0xFF)
        else:
            pdu = [0xB5, (index >> 8) & 0xFF, index & 0xFF, subindex & 0xFF]
        pdu.append(0)
        pdu[-1] = 0
        chk = 0
        for value in pdu[:-1]:
            chk ^= value
        pdu[-1] = chk
        return bytes(pdu)

    def _write_pdu(self, pdu: bytes) -> None:
        for position, value in enumerate(pdu):
            flowctrl = 0x10 if position == 0 else (position & 0x0F)
            self.write_octet(flowctrl, value)

    def _read_response(self) -> bytes:
        first = self.read_od(0x10)
        length = first & 0x0F
        if length == 0:
            return bytes([first])
        if length == 1:
            ext = self.read_od(0x01)
            total = ext
            out = bytearray([first, ext])
            start = 2
        else:
            total = length
            out = bytearray([first])
            start = 1
        for i in range(start, total):
            out.append(self.read_od(i & 0x0F))
        self.send(self.frame(ISDU | RW | 0x11))
        self._read_exact(2)
        return bytes(out)

    def isdu_read(self, index: int, subindex: int = 0) -> bytes:
        self._write_pdu(self._request_pdu(index, subindex))
        return self._read_response()

    def isdu_write(self, index: int, subindex: int, data: bytes) -> bytes:
        overhead = 1 + (3 if index > 0xFF else (2 if subindex else 1)) + 1
        total = len(data) + overhead
        ext = total > 15
        if ext:
            total += 1
        base = 0x30 if index > 0xFF else (0x20 if subindex else 0x10)
        pdu = bytearray([base | (0x01 if ext else total)])
        if ext:
            pdu.append(total)
        if index > 0xFF:
            pdu += bytes([(index >> 8) & 0xFF, index & 0xFF, subindex])
        else:
            pdu.append(index & 0xFF)
            if subindex:
                pdu.append(subindex)
        pdu += data
        chk = 0
        for value in pdu:
            chk ^= value
        pdu.append(chk)
        self._write_pdu(bytes(pdu))
        return self._read_response()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


ERROR_PREFIX = 0x80


def isdu_payload(response: bytes) -> bytes:
    """Strip the ISDU response framing (service octet and CHKPDU)."""
    if not response:
        return b""
    length = response[0] & 0x0F
    if length == 0:
        return b""
    if length == 1:
        return response[2:-1]
    return response[1:-1]


def isdu_error_code(response: bytes) -> int | None:
    if (len(response) >= 3 and response[1] == ERROR_PREFIX and
            (response[0] & 0xF0) in (0x40, 0xC0)):
        return response[2]
    return None


def require_ok(case_id: str, response: bytes) -> bytes:
    code = isdu_error_code(response)
    if code is not None:
        raise AssertionError(f"{case_id}: ISDU error 0x{code:02X} ({response.hex()})")
    return isdu_payload(response)


def read_payload(link: DeviceLink, index: int, subindex: int, case_id: str) -> bytes:
    return require_ok(case_id, link.isdu_read(index, subindex))


def write_ok(link: DeviceLink, index: int, subindex: int, data: bytes, case_id: str) -> None:
    require_ok(case_id, link.isdu_write(index, subindex, data))


def ds_command(link: DeviceLink, command: int, case_id: str) -> None:
    write_ok(link, 0x0003, 0x01, bytes([command & 0xFF]), case_id)


def ds_state(link: DeviceLink, case_id: str = "DS") -> int:
    data = read_payload(link, 0x0003, 0x02, case_id)
    require(len(data) == 1, f"{case_id}: State_Property length {len(data)}")
    return data[0]


def ds_flag(link: DeviceLink, case_id: str = "DS") -> int:
    return 1 if (ds_state(link, case_id) & 0x80) else 0


def ds_checksum(link: DeviceLink, case_id: str = "DS") -> int:
    data = read_payload(link, 0x0003, 0x04, case_id)
    require(len(data) == 4, f"{case_id}: Parameter_Checksum length {len(data)}")
    return int.from_bytes(data, "big")


def ds_size(link: DeviceLink, case_id: str = "DS") -> int:
    data = read_payload(link, 0x0003, 0x03, case_id)
    require(len(data) == 4, f"{case_id}: Data_Storage_Size length {len(data)}")
    return int.from_bytes(data, "big")


def ds_index_list(link: DeviceLink, case_id: str = "DS") -> bytes:
    data = read_payload(link, 0x0003, 0x05, case_id)
    require(len(data) >= 2 and data[-2:] == b"\x00\x00",
            f"{case_id}: Index_List terminator {data.hex()}")
    require((len(data) - 2) % 3 == 0, f"{case_id}: Index_List length {len(data)}")
    return data


def ds_record(link: DeviceLink, case_id: str = "DS") -> tuple[int, int, int, bytes]:
    """Read DataStorageIndex sub0 and split the Table B.11 record."""
    data = read_payload(link, 0x0003, 0x00, case_id)
    require(len(data) >= 12, f"{case_id}: DataStorageIndex record length {len(data)}")
    state = data[1]
    size = int.from_bytes(data[2:6], "big")
    checksum = int.from_bytes(data[6:10], "big")
    return state, size, checksum, data[10:]


def read_device_status(link: DeviceLink, case_id: str = "0373") -> int:
    data = read_payload(link, 0x0024, 0x00, case_id)
    require(len(data) == 1, f"{case_id}: DeviceStatus length {len(data)}")
    require(0 <= data[0] <= 4, f"{case_id}: DeviceStatus value {data[0]}")
    return data[0]


def read_dds(link: DeviceLink, case_id: str = "0373") -> list[tuple[int, int]]:
    data = read_payload(link, 0x0025, 0x00, case_id)
    require(len(data) % 3 == 0, f"{case_id}: DDS length {len(data)} is not a multiple of 3")
    require(len(data) <= 64 * 3, f"{case_id}: DDS length {len(data)} exceeds 64 records")
    return [(data[i], (data[i + 1] << 8) | data[i + 2]) for i in range(0, len(data), 3)]


def read_tag(link: DeviceLink, index: int, case_id: str = "tag") -> bytes:
    return read_payload(link, index, 0x00, case_id)


def wait_for_event(link: DeviceLink, code: int, timeout: float = 1.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status, _, current = link.event_memory()
        if (status & 0x80) and current == code:
            return
        time.sleep(0.02)
    status, _, current = link.event_memory()
    raise AssertionError(f"event 0x{code:04X} timeout (status 0x{status:02X}, code 0x{current:04X})")


def parse_status_line(text: str, prefix: str) -> dict[str, str]:
    line = None
    for candidate in text.splitlines():
        if candidate.startswith(prefix + "="):
            line = candidate
    require(line is not None, f"{prefix} marker missing: {text!r}")
    fields: dict[str, str] = {}
    for token in line[len(prefix) + 1:].split():
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    return fields


def parse_resetinfo(text: str) -> dict[str, str]:
    return parse_status_line(text, "resetinfo")


def power_cycle(link: DeviceLink) -> dict[str, str]:
    text = link.query("powercycle", "powercycle=", timeout=5.0)
    time.sleep(0.15)
    link.startup()
    return parse_status_line(text, "powercycle")


def drain_events(link: DeviceLink) -> None:
    for _ in range(16):
        status = link.read_diag(0x00)
        if (status & 0x80) == 0:
            return
        link.ack_event()
    raise AssertionError("event memory did not drain")


def run(binary: Path) -> None:
    link = DeviceLink(binary)
    try:
        link.startup()
        require((link.page_read(0x03) & 0x01) == 0x01, "SDCI_TC_0052: ISDU bit missing")
        require(link.page_read(0x0C) == 0x00, "SDCI_TC_0097: FunctionID MSB")
        require(link.page_read(0x0D) == 0x00, "SDCI_TC_0097: FunctionID LSB")

        config1 = link.isdu_read(0x0100)
        require(len(config1) > 2 and (config1[0] >> 4) != 0x8, f"Config1 read failed: {config1.hex()}")

        config3 = link.isdu_read(0x0102)
        payload3 = config3[2:-1]
        require(len(payload3) == 200, f"SDCI_TC_0055: Config3 length {len(payload3)}")

        too_long = bytes([0x33] * 70)
        err = link.isdu_write(0x0100, 0, too_long)
        require(len(err) >= 2 and (err[0] & 0x0F) == 4 and err[2] == 0x33,
                f"SDCI_TC_0063: expected 0x8033, got {err.hex()}")

        too_short = bytes([0x44] * 63)
        err = link.isdu_write(0x0100, 0, too_short)
        require(len(err) >= 2 and (err[0] & 0x0F) == 4 and err[2] == 0x34,
                f"SDCI_TC_0064: expected 0x8034, got {err.hex()}")

        ds_sub1 = link.isdu_read(0x0003, 0x01)
        require(len(ds_sub1) == 3 and ds_sub1[1] <= 5, f"SDCI_TC_0108: DS sub1 {ds_sub1.hex()}")

        # 0107: sub0 is the concatenated 12 + 3n record.
        ds_sub0 = link.isdu_read(0x0003, 0x00)
        require(len(ds_sub0) >= 3 and len(ds_sub0[2:-1]) == 42,
                f"SDCI_TC_0107: DS sub0 {ds_sub0.hex()}")

        # 0325: upload active -> parameter write rejected with 0x8022.
        ok = link.isdu_write(0x0002, 0x00, bytes([0x01]))
        require(ok and ok[0] == 0x52, f"DS UploadStart failed: {ok.hex()}")
        time.sleep(0.05)
        err = link.isdu_write(0x0018, 0x00, b"X")
        require(len(err) == 4 and err[2] == 0x22, f"SDCI_TC_0325: {err.hex()}")
        ok = link.isdu_write(0x0002, 0x00, bytes([0x02]))
        require(ok and ok[0] == 0x52, f"DS UploadEnd failed: {ok.hex()}")

        # 0324: ParamBreak in Idle -> 0x8036.
        err = link.isdu_write(0x0002, 0x00, bytes([0x06]))
        require(len(err) == 4 and err[2] == 0x36, f"SDCI_TC_0324: {err.hex()}")

        # 0326: read during download -> 0x8022.
        ok = link.isdu_write(0x0002, 0x00, bytes([0x03]))
        require(ok and ok[0] == 0x52, f"DownloadStart failed: {ok.hex()}")
        err = link.isdu_read(0x0018, 0x00)
        require(len(err) == 4 and err[2] == 0x22, f"SDCI_TC_0326: {err.hex()}")

        # Download a new Config1 value and commit it.
        value = bytes([0xA5] * 64)
        ok = link.isdu_write(0x0100, 0x00, value)
        require(ok and ok[0] == 0x52, f"Config1 download write: {ok.hex()}")
        ok = link.isdu_write(0x0002, 0x00, bytes([0x04]))
        require(ok and ok[0] == 0x52, f"DownloadEnd failed: {ok.hex()}")
        readback = link.isdu_read(0x0100)
        require(len(readback[2:-1]) == 64 and readback[2:-1] == value,
                f"downloaded Config1 mismatch: {readback.hex()}")

        # 0069/0075: two events in diagnosis memory.
        link.control("event 0x8C00 warning")
        link.control("event 0x8C01 error")
        time.sleep(0.1)
        status = link.read_diag(0x00)
        require((status & 0x80) != 0, f"event flag missing: 0x{status:02X}")
        require((status & 0x01) != 0, f"event slot: 0x{status:02X}")
        q1 = link.read_diag(0x01)
        code1 = (link.read_diag(0x02) << 8) | link.read_diag(0x03)
        require(q1 != 0, "event qualifier missing")
        link.write_diag(0x00, 0x00)
        time.sleep(0.05)
        status = link.read_diag(0x00)
        require((status & 0x80) != 0, f"second event flag missing: 0x{status:02X}")
        code2 = (link.read_diag(0x02) << 8) | link.read_diag(0x03)
        require({code1, code2} == {0x8C00, 0x8C01}, f"event codes {code1:04X}/{code2:04X}")
        link.write_diag(0x00, 0x00)
        time.sleep(0.05)
        require((link.read_diag(0x00) & 0x80) == 0, "event ack did not clear flag")

        # 0074: appear/disappear qualifier.
        link.control("event 0x8C02 warning")
        time.sleep(0.1)
        q_appear = link.read_diag(0x01)
        code_appear = (link.read_diag(0x02) << 8) | link.read_diag(0x03)
        require(code_appear == 0x8C02, f"appear code {code_appear:04X}")
        link.write_diag(0x00, 0x00)
        time.sleep(0.05)
        link.control("clearevent 0x8C02", wait=0.01)
        time.sleep(0.1)
        q_disappear = link.read_diag(0x01)
        code_disappear = (link.read_diag(0x02) << 8) | link.read_diag(0x03)
        require(code_disappear == 0x8C02, f"disappear code {code_disappear:04X}")
        require(q_disappear != q_appear, "disappear qualifier equals appear")
        link.write_diag(0x00, 0x00)

        # 0071: explicit clear, then stable idle for >1 s.
        link.control("event 0x8C10 warning")
        time.sleep(0.1)
        status, q_clear_appear, code_clear = link.event_memory()
        require(code_clear == 0x8C10 and (status & 0x80) != 0, "0071 appear missing")
        link.ack_event()
        link.control("clearevent 0x8C10", wait=0.01)
        time.sleep(0.1)
        status, q_clear_disappear, code_clear = link.event_memory()
        require(code_clear == 0x8C10 and q_clear_disappear != q_clear_appear,
                "0071 disappear missing")
        link.ack_event()
        time.sleep(1.0)
        require((link.read_diag(0x00) & 0x80) == 0, "0071 event reappeared")

        # 0072: communication interruption keeps an uncleared event.
        link.control("event 0x8C11 warning")
        time.sleep(0.1)
        time.sleep(2.0)  # no M-sequence traffic
        status, _, code = link.event_memory()
        require(code == 0x8C11 and (status & 0x80) != 0, "0072 event lost")
        link.ack_event()

        # 0073: power cycle clears stale event memory; a new event appears cleanly.
        link.control("powercycle", wait=0.2)
        link.startup()
        require((link.read_diag(0x00) & 0x80) == 0, "0073 stale event after restart")
        link.control("event 0x8C12 warning")
        time.sleep(0.1)
        status, _, code = link.event_memory()
        require(code == 0x8C12 and (status & 0x80) != 0, "0073 re-announce missing")
        link.ack_event()

        # 0076: short event; clear before first acknowledgement.
        link.control("event 0x8C13 warning")
        link.control("clearevent 0x8C13", wait=0.01)
        time.sleep(0.1)
        status, q_short_appear, code_short = link.event_memory()
        require(code_short == 0x8C13, f"0076 code {code_short:04X}")
        link.ack_event()
        time.sleep(0.1)
        status, q_short_disappear, code_short = link.event_memory()
        require((status & 0x80) != 0 and code_short == 0x8C13,
                f"0076 disappear event missing: 0x{status:02X}/0x{code_short:04X}")
        require(q_short_disappear != q_short_appear, "0076 disappear qualifier")
        link.ack_event()

        # --- SDCI_TC_0090/0091/0095/0096: DPP1 values vs synthetic profile ---
        min_cycle = link.page_read(0x02)
        require((min_cycle >> 6) < 3, f"SDCI_TC_0090: reserved time base {min_cycle:#04x}")
        require(min_cycle == 0x1E, f"SDCI_TC_0090: MinCycleTime {min_cycle:#04x} != IODD 3000us")
        mseq = link.page_read(0x03)
        require((mseq & 0xC0) == 0, f"SDCI_TC_0091: reserved M-sequence bits {mseq:#04x}")
        require(mseq == 1, f"SDCI_TC_0091: M-sequenceCapability {mseq} != IODD 1")
        vendor_id = (link.page_read(0x07) << 8) | link.page_read(0x08)
        require(vendor_id == 1234, f"SDCI_TC_0095: VendorID 0x{vendor_id:04X} != IODD 1234")
        device_id = ((link.page_read(0x09) << 16) | (link.page_read(0x0A) << 8) |
                     link.page_read(0x0B))
        require(device_id > 0, "SDCI_TC_0096: DeviceID is zero")
        require(device_id == 5680, f"SDCI_TC_0096: DeviceID 0x{device_id:06X} != IODD 5680")

        # --- SDCI_TC_0373: DeviceStatus/DDS follow an event appear/disappear ---
        drain_events(link)
        base_status = read_device_status(link, "SDCI_TC_0373")
        base_records = read_dds(link, "SDCI_TC_0373")
        link.control("event 0x8C40 warning")
        wait_for_event(link, 0x8C40)
        status = read_device_status(link, "SDCI_TC_0373")
        require(1 <= status <= 4, f"SDCI_TC_0373: DeviceStatus {status} outside 1..4")
        require(status == 1, f"SDCI_TC_0373: maintenance event status {status} != 1")
        records = read_dds(link, "SDCI_TC_0373")
        require(any(code == 0x8C40 for _, code in records),
                f"SDCI_TC_0373: DDS lacks 0x8C40: {records}")
        link.ack_event()
        link.control("clearevent 0x8C40", wait=0.01)
        time.sleep(0.2)
        records = read_dds(link, "SDCI_TC_0373")
        require(all(code != 0x8C40 for _, code in records),
                f"SDCI_TC_0373: DDS entry did not disappear: {records}")
        drain_events(link)
        require(read_device_status(link, "SDCI_TC_0373") == base_status,
                "SDCI_TC_0373: DeviceStatus did not return to baseline")
        require(read_dds(link, "SDCI_TC_0373") == base_records,
                f"SDCI_TC_0373: DDS did not return to baseline {base_records}")

        # --- SDCI_TC_0077: complete DS upload through DataStorageIndex ---
        drain_events(link)
        # Build the DS image before the composite read: the sub0 size octets are
        # captured before the image rebuild in the current production code.
        ds_checksum(link, "SDCI_TC_0077")
        state0, size0, checksum0, list0 = ds_record(link, "SDCI_TC_0077")
        require((state0 & 0x06) == 0, f"SDCI_TC_0077: baseline DS state {state0:#04x}")
        require(ds_size(link, "SDCI_TC_0077") == size0, "SDCI_TC_0077: size/sub0 mismatch")
        require(ds_checksum(link, "SDCI_TC_0077") == checksum0,
                "SDCI_TC_0077: checksum/sub0 mismatch")
        require(ds_index_list(link, "SDCI_TC_0077") == list0,
                "SDCI_TC_0077: Index_List/sub0 mismatch")
        require(ds_flag(link, "SDCI_TC_0077") == 0, "SDCI_TC_0077: flag set at baseline")
        ds_command(link, 0x01, "SDCI_TC_0077")
        require((ds_state(link, "SDCI_TC_0077") & 0x06) == 0x02,
                "SDCI_TC_0077: upload state not active")
        require(ds_checksum(link, "SDCI_TC_0077") == checksum0,
                "SDCI_TC_0077: checksum changed during upload")
        require(ds_size(link, "SDCI_TC_0077") == size0,
                "SDCI_TC_0077: size changed during upload")
        ds_command(link, 0x02, "SDCI_TC_0077")
        require((ds_state(link, "SDCI_TC_0077") & 0x06) == 0,
                "SDCI_TC_0077: DS did not return to idle")
        require(ds_flag(link, "SDCI_TC_0077") == 0, "SDCI_TC_0077: flag set after upload")

        # --- SDCI_TC_0321: a single parameter write must not request DS ---
        drain_events(link)
        write_ok(link, 0x0101, 0, b"\x12\x34", "SDCI_TC_0321")
        time.sleep(1.05)
        require(ds_flag(link, "SDCI_TC_0321") == 0,
                "SDCI_TC_0321: single write set DS_UPLOAD_FLAG")
        status, _, code = link.event_memory()
        require(not ((status & 0x80) and code == 0xFF91),
                "SDCI_TC_0321: single write raised DS_UPLOAD_REQ")
        checksum_before_store = ds_checksum(link, "SDCI_TC_0321")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0321")
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0321")
        wait_for_event(link, 0xFF91)
        require(ds_flag(link, "SDCI_TC_0321") == 1,
                "SDCI_TC_0321: ParamDownloadStore did not set DS_UPLOAD_FLAG")
        require(ds_checksum(link, "SDCI_TC_0321") == checksum_before_store,
                "SDCI_TC_0321: store changed the parameter checksum")
        link.ack_event()
        ds_command(link, 0x01, "SDCI_TC_0321")
        ds_command(link, 0x02, "SDCI_TC_0321")
        require(ds_flag(link, "SDCI_TC_0321") == 0,
                "SDCI_TC_0321: upload end did not clear DS_UPLOAD_FLAG")
        drain_events(link)

        # --- SDCI_TC_0078/0079: store after a staged parameter set 2 ---
        checksum1 = ds_checksum(link, "SDCI_TC_0078")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0078")
        write_ok(link, 0x0101, 0, b"\x56\x78", "SDCI_TC_0078")
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0078")
        wait_for_event(link, 0xFF91)
        checksum2 = ds_checksum(link, "SDCI_TC_0078")
        require(checksum2 != checksum1, "SDCI_TC_0078: set 2 checksum unchanged")
        require(ds_flag(link, "SDCI_TC_0078") == 1, "SDCI_TC_0078: flag not set")
        link.ack_event()
        ds_command(link, 0x01, "SDCI_TC_0078")
        ds_command(link, 0x02, "SDCI_TC_0078")
        require(ds_flag(link, "SDCI_TC_0078") == 0, "SDCI_TC_0078: flag not cleared")
        require(ds_checksum(link, "SDCI_TC_0078") == checksum2,
                "SDCI_TC_0078: upload changed the checksum")

        checksum1 = ds_checksum(link, "SDCI_TC_0079")
        write_ok(link, 0x0101, 0, b"\x0A\x0B", "SDCI_TC_0079")
        time.sleep(1.05)
        require(ds_flag(link, "SDCI_TC_0079") == 0,
                "SDCI_TC_0079: direct set 2 write raised the flag")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0079")
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0079")
        wait_for_event(link, 0xFF91)
        require(ds_checksum(link, "SDCI_TC_0079") != checksum1,
                "SDCI_TC_0079: checksum unchanged after store")
        link.ack_event()
        ds_command(link, 0x01, "SDCI_TC_0079")
        ds_command(link, 0x02, "SDCI_TC_0079")
        drain_events(link)

        # --- SDCI_TC_0080: local teach-in requests a DS upload ---
        checksum1 = ds_checksum(link, "SDCI_TC_0080")
        config2_before = int.from_bytes(read_payload(link, 0x0101, 0, "SDCI_TC_0080"), "big")
        link.control("localchange")
        wait_for_event(link, 0xFF91)
        require(ds_flag(link, "SDCI_TC_0080") == 1,
                "SDCI_TC_0080: local change did not set DS_UPLOAD_FLAG")
        config2_after = int.from_bytes(read_payload(link, 0x0101, 0, "SDCI_TC_0080"), "big")
        require(config2_after == ((config2_before + 1) & 0xFFFF),
                "SDCI_TC_0080: local change did not modify the parameter")
        require(ds_checksum(link, "SDCI_TC_0080") != checksum1,
                "SDCI_TC_0080: local change checksum unchanged")
        link.ack_event()
        ds_command(link, 0x01, "SDCI_TC_0080")
        ds_command(link, 0x02, "SDCI_TC_0080")
        require(ds_flag(link, "SDCI_TC_0080") == 0,
                "SDCI_TC_0080: upload end did not clear DS_UPLOAD_FLAG")
        drain_events(link)

        # --- SDCI_TC_0322: DS_UPLOAD_FLAG 1/0/1/0 sequence ---
        require(ds_flag(link, "SDCI_TC_0322") == 0, "SDCI_TC_0322: flag not clear")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0322")
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0322")
        require(ds_flag(link, "SDCI_TC_0322") == 1, "SDCI_TC_0322: first flag != 1")
        ds_command(link, 0x01, "SDCI_TC_0322")
        ds_command(link, 0x02, "SDCI_TC_0322")
        require(ds_flag(link, "SDCI_TC_0322") == 0, "SDCI_TC_0322: UploadEnd flag != 0")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0322")
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0322")
        require(ds_flag(link, "SDCI_TC_0322") == 1, "SDCI_TC_0322: second flag != 1")
        ds_command(link, 0x03, "SDCI_TC_0322")
        ds_command(link, 0x04, "SDCI_TC_0322")
        require(ds_flag(link, "SDCI_TC_0322") == 0, "SDCI_TC_0322: DownloadEnd flag != 0")
        drain_events(link)

        # --- SDCI_TC_0374: DS_UPLOAD_FLAG survives a power cycle ---
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0374")
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0374")
        require(ds_flag(link, "SDCI_TC_0374") == 1, "SDCI_TC_0374: flag not set")
        wait_for_event(link, 0xFF91)
        link.ack_event()
        power_cycle(link)
        require(ds_flag(link, "SDCI_TC_0374") == 1,
                "SDCI_TC_0374: DS_UPLOAD_FLAG lost on power cycle")
        ds_command(link, 0x01, "SDCI_TC_0374")
        ds_command(link, 0x02, "SDCI_TC_0374")
        require(ds_flag(link, "SDCI_TC_0374") == 0, "SDCI_TC_0374: flag not cleared")
        power_cycle(link)
        require(ds_flag(link, "SDCI_TC_0374") == 0,
                "SDCI_TC_0374: cleared flag reappeared after power cycle")
        drain_events(link)

        # --- SDCI_TC_0143: complete block-set download and readback ---
        blocks = [bytes([0x60 + i]) * 32 for i in range(4)]
        config1_new = bytes([0x77]) * 64
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0143")
        write_ok(link, 0x0100, 0, config1_new, "SDCI_TC_0143")
        for i, block in enumerate(blocks):
            write_ok(link, 0x0110 + i, 0, block, "SDCI_TC_0143")
        write_ok(link, 0x0002, 0, bytes([0x04]), "SDCI_TC_0143")
        require(read_payload(link, 0x0100, 0, "SDCI_TC_0143") == config1_new,
                "SDCI_TC_0143: Config1 readback mismatch")
        for i, block in enumerate(blocks):
            require(read_payload(link, 0x0110 + i, 0, "SDCI_TC_0143") == block,
                    f"SDCI_TC_0143: Config4 block {i} readback mismatch")
        require((ds_state(link, "SDCI_TC_0143") & 0x06) == 0,
                "SDCI_TC_0143: DS not idle after block download")
        require(ds_flag(link, "SDCI_TC_0143") == 0,
                "SDCI_TC_0143: DS_UPLOAD_FLAG set after block download")

        # --- SDCI_TC_0144: ParamBreak restores value1; later end -> 0x8036 ---
        value1 = read_payload(link, 0x0101, 0, "SDCI_TC_0144")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0144")
        write_ok(link, 0x0101, 0, b"\xAA\x55", "SDCI_TC_0144")
        write_ok(link, 0x0002, 0, bytes([0x06]), "SDCI_TC_0144")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0144") == value1,
                "SDCI_TC_0144: break did not restore value1")
        response = link.isdu_write(0x0002, 0x00, bytes([0x04]))
        require(isdu_error_code(response) == 0x36,
                f"SDCI_TC_0144: post-break DownloadEnd {response.hex()}")

        # --- SDCI_TC_0147: a second ParamDownloadStart restarts the download ---
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0147")
        write_ok(link, 0x0101, 0, b"\x11\x11", "SDCI_TC_0147")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0147")
        write_ok(link, 0x0101, 0, b"\x22\x22", "SDCI_TC_0147")
        write_ok(link, 0x0002, 0, bytes([0x04]), "SDCI_TC_0147")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0147") == b"\x22\x22",
                "SDCI_TC_0147: second download did not win")

        # --- SDCI_TC_0084: all three ParamBreak abort points in a download ---
        checksum_base = ds_checksum(link, "SDCI_TC_0084")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0084")
        write_ok(link, 0x0002, 0, bytes([0x06]), "SDCI_TC_0084")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0084")
        write_ok(link, 0x0100, 0, bytes([0x99]) * 64, "SDCI_TC_0084")
        write_ok(link, 0x0002, 0, bytes([0x06]), "SDCI_TC_0084")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0084")
        for i in range(4):
            write_ok(link, 0x0110 + i, 0, bytes([0x80 + i]) * 32, "SDCI_TC_0084")
        write_ok(link, 0x0002, 0, bytes([0x06]), "SDCI_TC_0084")
        require((ds_state(link, "SDCI_TC_0084") & 0x06) == 0,
                "SDCI_TC_0084: DS not inactive after break")
        require(ds_flag(link, "SDCI_TC_0084") == 0, "SDCI_TC_0084: flag set after break")
        require(ds_checksum(link, "SDCI_TC_0084") == checksum_base,
                "SDCI_TC_0084: checksum changed after break")
        response = link.isdu_write(0x0002, 0x00, bytes([0x04]))
        require(isdu_error_code(response) == 0x36,
                f"SDCI_TC_0084: post-break DownloadEnd {response.hex()}")

        # --- SDCI_TC_0081: upload abort points ---
        ds_command(link, 0x01, "SDCI_TC_0081")
        write_ok(link, 0x0002, 0, bytes([0x06]), "SDCI_TC_0081")
        require((ds_state(link, "SDCI_TC_0081") & 0x06) == 0,
                "SDCI_TC_0081: break did not abort upload")
        ds_command(link, 0x01, "SDCI_TC_0081")
        read_payload(link, 0x0101, 0, "SDCI_TC_0081")
        write_ok(link, 0x0002, 0, bytes([0x06]), "SDCI_TC_0081")
        require((ds_state(link, "SDCI_TC_0081") & 0x06) == 0,
                "SDCI_TC_0081: mid-upload break did not abort")

        # --- SDCI_TC_0082: download after a direct modification ---
        restore1 = read_payload(link, 0x0100, 0, "SDCI_TC_0082")
        write_ok(link, 0x0100, 0, bytes([0x22]) * 64, "SDCI_TC_0082")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0082")
        write_ok(link, 0x0100, 0, restore1, "SDCI_TC_0082")
        write_ok(link, 0x0002, 0, bytes([0x04]), "SDCI_TC_0082")
        require(read_payload(link, 0x0100, 0, "SDCI_TC_0082") == restore1,
                "SDCI_TC_0082: download did not restore set 1")
        require(ds_flag(link, "SDCI_TC_0082") == 0,
                "SDCI_TC_0082: download raised DS_UPLOAD_FLAG")

        # --- SDCI_TC_0083: factory reset followed by a DS download ---
        restore1 = read_payload(link, 0x0100, 0, "SDCI_TC_0083")
        write_ok(link, 0x0100, 0, bytes([0x33]) * 64, "SDCI_TC_0083")
        write_ok(link, 0x0002, 0, bytes([0x82]), "SDCI_TC_0083")
        link.query("factoryreset", "resetinfo=")
        require(read_payload(link, 0x0100, 0, "SDCI_TC_0083") == bytes([0x49]) * 64,
                "SDCI_TC_0083: factory reset did not restore the application default")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0083")
        write_ok(link, 0x0100, 0, restore1, "SDCI_TC_0083")
        write_ok(link, 0x0002, 0, bytes([0x04]), "SDCI_TC_0083")
        require(read_payload(link, 0x0100, 0, "SDCI_TC_0083") == restore1,
                "SDCI_TC_0083: DS download did not restore set 1")
        require(ds_flag(link, "SDCI_TC_0083") == 0,
                "SDCI_TC_0083: DS_UPLOAD_FLAG set after the download")
        drain_events(link)

        # --- SDCI_TC_0327: unexpected upload commands and write lock ---
        ds_command(link, 0x01, "SDCI_TC_0327")
        response = link.isdu_write(0x0101, 0, b"\x01\x02")
        require(isdu_error_code(response) == 0x22, f"SDCI_TC_0327: write lock {response.hex()}")
        ds_command(link, 0x01, "SDCI_TC_0327")
        response = link.isdu_write(0x0101, 0, b"\x03\x04")
        require(isdu_error_code(response) == 0x22, f"SDCI_TC_0327: write lock {response.hex()}")
        ds_command(link, 0x04, "SDCI_TC_0327")
        write_ok(link, 0x0101, 0, b"\x05\x06", "SDCI_TC_0327")

        # --- SDCI_TC_0328: upload/download/upload state switches ---
        ds_command(link, 0x01, "SDCI_TC_0328")
        response = link.isdu_write(0x0101, 0, b"\x01\x01")
        require(isdu_error_code(response) == 0x22, f"SDCI_TC_0328: upload write {response.hex()}")
        ds_command(link, 0x03, "SDCI_TC_0328")
        response = link.isdu_read(0x0101, 0)
        require(isdu_error_code(response) == 0x22, f"SDCI_TC_0328: download read {response.hex()}")
        ds_command(link, 0x01, "SDCI_TC_0328")
        response = link.isdu_write(0x0101, 0, b"\x02\x02")
        require(isdu_error_code(response) == 0x22, f"SDCI_TC_0328: upload write {response.hex()}")
        ds_command(link, 0x02, "SDCI_TC_0328")
        read_payload(link, 0x0101, 0, "SDCI_TC_0328")

        # --- SDCI_TC_0330: upload start discards staged download values ---
        original = read_payload(link, 0x0101, 0, "SDCI_TC_0330")
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0330")
        write_ok(link, 0x0101, 0, b"\xDE\xAD", "SDCI_TC_0330")
        ds_command(link, 0x01, "SDCI_TC_0330")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0330") == original,
                "SDCI_TC_0330: staged download value visible after UploadStart")
        ds_command(link, 0x02, "SDCI_TC_0330")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0330") == original,
                "SDCI_TC_0330: staged download value applied after UploadEnd")

        # --- SDCI_TC_0155: factory settings restore defaults and IDs ---
        drain_events(link)
        write_ok(link, 0x0018, 0, b"FACT", "SDCI_TC_0155")
        write_ok(link, 0x0101, 0, b"\x33\x44", "SDCI_TC_0155")
        status_before = read_device_status(link, "SDCI_TC_0155")
        vendor_before = (link.page_read(0x07) << 8) | link.page_read(0x08)
        device_before = ((link.page_read(0x09) << 16) | (link.page_read(0x0A) << 8) |
                         link.page_read(0x0B))
        info_before = parse_resetinfo(link.query("resetinfo", "resetinfo="))
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0155")
        require(ds_flag(link, "SDCI_TC_0155") == 1,
                "SDCI_TC_0155: precondition DS_UPLOAD_FLAG not set")
        drain_events(link)
        write_ok(link, 0x0002, 0, bytes([0x82]), "SDCI_TC_0155")
        info = link.query("factoryreset", "resetinfo=")
        fields = parse_resetinfo(info)
        require(int(fields.get("factory", "0")) == int(info_before.get("factory", "0")) + 1,
                f"SDCI_TC_0155: factory hook {fields}")
        require(fields.get("defaults") == "1", f"SDCI_TC_0155: defaults {fields}")
        require(fields.get("reason") == "factory", f"SDCI_TC_0155: reason {fields}")
        require(read_tag(link, 0x0018, "SDCI_TC_0155") == b"",
                "SDCI_TC_0155: ApplicationSpecificTag not restored")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0155") == b"\x00\x00",
                "SDCI_TC_0155: Config2 not restored to its default")
        require(read_device_status(link, "SDCI_TC_0155") == status_before,
                "SDCI_TC_0155: DeviceStatus changed unexpectedly")
        require(((link.page_read(0x07) << 8) | link.page_read(0x08)) == vendor_before,
                "SDCI_TC_0155: VendorID changed")
        require((((link.page_read(0x09) << 16) | (link.page_read(0x0A) << 8) |
                  link.page_read(0x0B))) == device_before,
                "SDCI_TC_0155: DeviceID changed")
        require(ds_flag(link, "SDCI_TC_0155") == 0,
                "SDCI_TC_0155: factory restore did not clear DS_UPLOAD_FLAG")

        # --- SDCI_TC_0316: device reset preserves parameters and IDs ---
        write_ok(link, 0x0018, 0, b"KEEP", "SDCI_TC_0316")
        write_ok(link, 0x0101, 0, b"\x55\x66", "SDCI_TC_0316")
        info_before = parse_resetinfo(link.query("resetinfo", "resetinfo="))
        write_ok(link, 0x0002, 0, bytes([0x80]), "SDCI_TC_0316")
        time.sleep(0.05)
        info_after = parse_resetinfo(link.query("resetinfo", "resetinfo="))
        require(info_after.get("device") != info_before.get("device"),
                f"SDCI_TC_0316: device reset not observed {info_after}")
        require(info_after.get("reason") == "device",
                f"SDCI_TC_0316: reason {info_after}")
        require(read_tag(link, 0x0018, "SDCI_TC_0316") == b"KEEP",
                "SDCI_TC_0316: tag lost on device reset")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0316") == b"\x55\x66",
                "SDCI_TC_0316: parameter lost on device reset")
        startup_before = int(info_after.get("startup", "0"))
        pc = power_cycle(link)
        require(int(pc.get("startup", "0")) == startup_before + 1,
                f"SDCI_TC_0316: startup count after power cycle {pc}")
        require(read_tag(link, 0x0018, "SDCI_TC_0316") == b"KEEP",
                "SDCI_TC_0316: tag lost on restart")
        require(((link.page_read(0x07) << 8) | link.page_read(0x08)) == 1234,
                "SDCI_TC_0316: VendorID changed on restart")
        require((((link.page_read(0x09) << 16) | (link.page_read(0x0A) << 8) |
                  link.page_read(0x0B))) == 5680,
                "SDCI_TC_0316: DeviceID changed on restart")

        # --- SDCI_TC_0317: application reset preserves IDs/tags, resets app data ---
        drain_events(link)
        write_ok(link, 0x0018, 0, b"TAG", "SDCI_TC_0317")
        write_ok(link, 0x0101, 0, b"\x77\x88", "SDCI_TC_0317")
        info_before = parse_resetinfo(link.query("resetinfo", "resetinfo="))
        write_ok(link, 0x0002, 0, bytes([0x81]), "SDCI_TC_0317")
        wait_for_event(link, 0xFF91)
        info_after = parse_resetinfo(link.query("resetinfo", "resetinfo="))
        require(info_after.get("application") != info_before.get("application"),
                f"SDCI_TC_0317: app reset not observed {info_after}")
        require(info_after.get("reason") == "application",
                f"SDCI_TC_0317: reason {info_after}")
        require(info_after.get("startup") == info_before.get("startup"),
                "SDCI_TC_0317: application reset restarted communication")
        require(read_tag(link, 0x0018, "SDCI_TC_0317") == b"TAG",
                "SDCI_TC_0317: tag changed on application reset")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0317") == b"\x00\x00",
                "SDCI_TC_0317: application parameter not restored")
        require(((link.page_read(0x07) << 8) | link.page_read(0x08)) == 1234,
                "SDCI_TC_0317: VendorID changed")
        require((((link.page_read(0x09) << 16) | (link.page_read(0x0A) << 8) |
                  link.page_read(0x0B))) == 5680,
                "SDCI_TC_0317: DeviceID changed")
        require(ds_flag(link, "SDCI_TC_0317") == 1,
                "SDCI_TC_0317: app reset did not set DS_UPLOAD_FLAG")
        link.ack_event()
        ds_command(link, 0x01, "SDCI_TC_0317")
        ds_command(link, 0x02, "SDCI_TC_0317")

        # --- SDCI_TC_0318: back-to-box restores defaults and restarts ---
        drain_events(link)
        write_ok(link, 0x0002, 0, bytes([0x03]), "SDCI_TC_0318")
        write_ok(link, 0x0018, 0, b"BOX", "SDCI_TC_0318")
        write_ok(link, 0x0101, 0, b"\x99\xAA", "SDCI_TC_0318")
        write_ok(link, 0x0002, 0, bytes([0x05]), "SDCI_TC_0318")
        require(ds_flag(link, "SDCI_TC_0318") == 1,
                "SDCI_TC_0318: precondition DS_UPLOAD_FLAG not set")
        drain_events(link)
        info_before = parse_resetinfo(link.query("resetinfo", "resetinfo="))
        write_ok(link, 0x0002, 0, bytes([0x83]), "SDCI_TC_0318")
        time.sleep(0.1)
        info_mid = parse_resetinfo(link.query("backtobox", "resetinfo="))
        require(info_mid.get("reason") == "backtobox", f"SDCI_TC_0318: reason {info_mid}")
        require(info_mid.get("defaults") == "1", f"SDCI_TC_0318: defaults {info_mid}")
        power_cycle(link)
        require(read_tag(link, 0x0018, "SDCI_TC_0318") == b"",
                "SDCI_TC_0318: tag not restored to default")
        require(read_payload(link, 0x0101, 0, "SDCI_TC_0318") == b"\x00\x00",
                "SDCI_TC_0318: Config2 not restored to default")
        require(((link.page_read(0x07) << 8) | link.page_read(0x08)) == 1234,
                "SDCI_TC_0318: VendorID changed")
        require((((link.page_read(0x09) << 16) | (link.page_read(0x0A) << 8) |
                  link.page_read(0x0B))) == 5680,
                "SDCI_TC_0318: DeviceID changed")
        require(ds_flag(link, "SDCI_TC_0318") == 0,
                "SDCI_TC_0318: DS_UPLOAD_FLAG set after back-to-box")


        print("cases: 0069-0076 0077-0084 0090 0091 0095 0096 0143 0144 0147 "
              "0155 0316 0317 0318 0321 0322 0324-0328 0330 0373 0374")
        print("PASS: conformance reference master subset")

    finally:
        link.close()

    # 0070: the same event engine works in PREOPERATE.
    preop = DeviceLink(binary)
    try:
        preop.startup_preoperate()
        preop.control("event 0x8C20 warning")
        time.sleep(0.1)
        status, qualifier, code = preop.event_memory()
        require(code == 0x8C20 and qualifier != 0 and (status & 0x80) != 0,
                "0070 PREOPERATE event missing")
        preop.ack_event()
    finally:
        preop.close()


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: conformance_device_master.py <binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1])
    if not binary.exists():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 2
    try:
        run(binary)
    except Exception as exc:  # noqa: BLE001 - command-line runner
        import traceback
        traceback.print_exc()
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
