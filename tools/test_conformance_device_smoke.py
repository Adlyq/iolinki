#!/usr/bin/env python3
"""Smoke-test the Linux conformance reference device over a PTY.

This is not a Test Spec runner. It proves that the reference integration starts,
opens its virtual PHY/NVM, accepts a wake-up and remains in a valid startup
state. The full Test Spec runner is a separate follow-up.
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


def read_available(fd: int, timeout: float) -> bytes:
    ready, _, _ = select.select([fd], [], [], timeout)
    if not ready:
        return b""
    try:
        return os.read(fd, 4096)
    except OSError:
        return b""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_conformance_device_smoke.py <binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1])
    if not binary.exists():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 2

    master, slave = pty.openpty()
    # Unique per run: a fixed path collides with parallel build trees/jobs.
    fd, path = tempfile.mkstemp(prefix="iolink-conformance-smoke-", suffix=".nvm")
    os.close(fd)
    nvm = Path(path)
    nvm.unlink(missing_ok=True)
    proc = subprocess.Popen(
        [str(binary), "--profile", "full", "--phy", os.ttyname(slave), "--nvm", str(nvm)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=False,
    )
    output = bytearray()
    try:
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            chunk = read_available(proc.stdout.fileno(), 0.1)
            if chunk:
                output.extend(chunk)
                if b"conformance device ready" in output:
                    break
        if b"conformance device ready" not in output:
            print(output.decode(errors="replace"), file=sys.stderr)
            return 1

        # 0x55 is the SIO wake-up marker consumed by the virtual PHY.
        os.write(master, b"\x55")
        time.sleep(0.1)
        assert proc.stdin is not None
        proc.stdin.write(b"dump\n")
        proc.stdin.flush()
        time.sleep(0.1)
        proc.stdin.write(b"quit\n")
        proc.stdin.flush()
        out, _ = proc.communicate(timeout=5)
        output.extend(out)
        text = output.decode(errors="replace")
        if "state=" not in text:
            print(text, file=sys.stderr)
            return 1
        if proc.returncode != 0:
            print(text, file=sys.stderr)
            return 1
        print("PASS: conformance reference device started and accepted wake-up")
        return 0
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=2)
        os.close(master)
        os.close(slave)
        nvm.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
