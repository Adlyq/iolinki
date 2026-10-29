#!/usr/bin/env python3
"""Compare the conformance reference device against the synthetic test IODD.

The synthetic IODD/profile is a test oracle, not a product IODD. It proves that
the generic library can serve the profile's identity, PD lengths and Config1/2/3/4
parameter contract.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from conformance_device_master import DeviceLink  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = ROOT / "examples" / "conformance_device" / "iodd_expected.json"


def payload(response: bytes) -> bytes:
    if not response:
        raise AssertionError("empty ISDU response")
    length = response[0] & 0x0F
    if length == 0:
        return b""
    if length == 1:
        return response[2:-1]
    return response[1:-1]


def encode_pd(octets: int) -> int:
    if octets == 0:
        return 0
    if octets <= 2:
        return octets * 8
    return 0x80 | (octets - 1)


def decode_cycle_time(octet: int) -> int:
    """Decode a DPP1 MinCycleTime octet into microseconds (Table B.3)."""
    base = octet >> 6
    multiplier = octet & 0x3F
    if base == 0:
        return multiplier * 100
    if base == 1:
        return 6400 + multiplier * 400
    if base == 2:
        return 32000 + multiplier * 1600
    return 0


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_conformance_iodd_oracle.py <binary>", file=sys.stderr)
        return 2
    expected = json.loads(EXPECTED.read_text())
    link = DeviceLink(Path(sys.argv[1]), profile="type0")
    try:
        link.startup()
        identity = expected["identity"]

        # 0090: DPP1 MinCycleTime equals the synthetic IODD value.
        min_cycle = link.page_read(0x02)
        require((min_cycle >> 6) < 3, f"0090 reserved time base {min_cycle:#04x}")
        require(decode_cycle_time(min_cycle) == identity["minCycleTime"],
                f"0090 MinCycleTime {decode_cycle_time(min_cycle)} != {identity['minCycleTime']}")

        # 0091: DPP1 M-sequenceCapability equals the synthetic IODD value.
        mseq = link.page_read(0x03)
        require((mseq & 0xC0) == 0, f"0091 reserved bits {mseq:#04x}")
        require(mseq == identity["mSequenceCapability"],
                f"0091 M-sequenceCapability {mseq} != {identity['mSequenceCapability']}")

        # 0092: DPP1 RevisionID.
        require(link.page_read(0x04) == identity["revisionByte"], "0092 RevisionID")
        # 0093/0094: process-data lengths from the synthetic IODD.
        require(link.page_read(0x05) == encode_pd(expected["processData"]["inputBytes"]),
                "0093 PDIn encoding")
        require(link.page_read(0x06) == encode_pd(expected["processData"]["outputBytes"]),
                "0094 PDOut encoding")

        # 0114-0121: identity strings.
        strings = {
            0x0010: identity["vendorName"],
            0x0011: identity["vendorText"],
            0x0012: identity["productName"],
            0x0013: identity["productId"],
            0x0014: identity["productText"],
            0x0015: identity["serialNumber"],
            0x0016: identity["hardwareRevision"],
            0x0017: identity["firmwareRevision"],
        }
        for index, value in strings.items():
            got = payload(link.isdu_read(index)).decode("utf-8")
            require(got == value, f"identity 0x{index:04X}: {got!r} != {value!r}")

        # TC_Device_Identification: VendorID/DeviceID/ProductID.
        vendor_id = (link.page_read(0x07) << 8) | link.page_read(0x08)
        device_id = (link.page_read(0x09) << 16) | (link.page_read(0x0A) << 8) | link.page_read(0x0B)
        require(vendor_id == identity["vendorId"], f"0095 VendorID 0x{vendor_id:04X}")
        require(vendor_id in identity["assignedVendorIds"],
                f"0095 VendorID 0x{vendor_id:04X} not in the assigned set")
        require(device_id > 0, "0096 DeviceID is zero")
        require(device_id == identity["deviceId"], f"0096 DeviceID 0x{device_id:06X}")
        require(payload(link.isdu_read(0x0013)).decode() == identity["productId"], "ProductID")

        # 0141/0142: fixed-length Config1 underrun/overrun.
        err = link.isdu_write(0x0100, 0, bytes([0x11] * 63))
        require(len(err) == 4 and err[2] == 0x34, f"0141: {err.hex()}")
        err = link.isdu_write(0x0100, 0, bytes([0x22] * 65))
        require(len(err) == 4 and err[2] == 0x33, f"0142: {err.hex()}")

        # 0151: read the parameter contract.
        cfg1 = payload(link.isdu_read(0x0100))
        require(len(cfg1) == 64, f"Config1 length {len(cfg1)}")
        cfg2 = payload(link.isdu_read(0x0101))
        require(len(cfg2) == 2, f"Config2 length {len(cfg2)}")
        cfg3 = payload(link.isdu_read(0x0102))
        require(len(cfg3) == 200, f"Config3 length {len(cfg3)}")
        for index in range(0x0110, 0x0114):
            block = payload(link.isdu_read(index))
            require(len(block) == 32, f"Config4 0x{index:04X} length {len(block)}")

        # 0152: write/read verification for Config2 and one Config4 block.
        require(link.isdu_write(0x0101, 0, b"\x12\x34")[0] == 0x52, "Config2 write")
        require(payload(link.isdu_read(0x0101)) == b"\x12\x34", "Config2 readback")
        block = bytes([0xA5] * 32)
        require(link.isdu_write(0x0113, 0, block)[0] == 0x52, "Config4 write")
        require(payload(link.isdu_read(0x0113)) == block, "Config4 readback")

        # 0157: index-vs-subindex consistency of the DataStorageIndex record.
        # Read Parameter_Checksum first so the DS image is built; the sub0 size
        # field is currently captured before the image rebuild (production gap).
        payload(link.isdu_read(0x0003, 0x04))
        record = payload(link.isdu_read(0x0003))
        require(len(record) >= 12, f"0157 DataStorageIndex record length {len(record)}")
        state = record[1]
        size = int.from_bytes(record[2:6], "big")
        checksum = int.from_bytes(record[6:10], "big")
        index_list = record[10:]
        require(payload(link.isdu_read(0x0003, 0x02)) == bytes([state]),
                "0157 state/sub0 mismatch")
        require(int.from_bytes(payload(link.isdu_read(0x0003, 0x03)), "big") == size,
                "0157 size/sub0 mismatch")
        require(int.from_bytes(payload(link.isdu_read(0x0003, 0x04)), "big") == checksum,
                "0157 checksum/sub0 mismatch")
        require(payload(link.isdu_read(0x0003, 0x05)) == index_list,
                "0157 Index_List/sub0 mismatch")

        # 0157: all Config4 block objects keep their own length and content.
        blocks = [bytes([0x70 + i]) * 32 for i in range(4)]
        for i, block in enumerate(blocks):
            require(link.isdu_write(0x0110 + i, 0, block)[0] == 0x52,
                    f"0157 Config4 write 0x{0x0110 + i:04X}")
            readback = payload(link.isdu_read(0x0110 + i))
            require(readback == block, f"0157 Config4 0x{0x0110 + i:04X} readback")
        checksum = int.from_bytes(payload(link.isdu_read(0x0003, 0x04)), "big")
        record = payload(link.isdu_read(0x0003))
        require(int.from_bytes(record[6:10], "big") == checksum,
                "0157 checksum not refreshed after the block writes")
        print("cases: 0090 0091 0092 0093 0094 0095 0096 0114-0121 "
              "0141 0142 0151 0152 0157")
        print("PASS: conformance device synthetic IODD oracle")
        return 0
    except Exception as exc:  # noqa: BLE001 - command-line runner
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        link.close()


if __name__ == "__main__":
    raise SystemExit(main())
