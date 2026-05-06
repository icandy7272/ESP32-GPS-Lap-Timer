"""Regression tests for the UDP transport added 2026-05-06.

Scope: confirm `udp_reader()` binds, drains a [gps-live] datagram, and
feeds the line through `parse_line()` so the same shared `state` dict
that the serial path populates is updated.

Standalone runner (matches tests_host/test_live_map_parse.py).  Exit
code 0 on pass, non-zero on failure.

Run with:
    python3 tests_host/test_live_map_udp.py
"""

from __future__ import annotations

import importlib.util
import pathlib
import socket
import sys
import threading
import time
import types
from typing import Any


def _load_live_map() -> Any:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    module_path = repo_root / "tools" / "live_map.py"
    spec = importlib.util.spec_from_file_location("live_map", module_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)

    # Same fake-pyserial trick as test_live_map_parse.py: the module
    # imports `serial` at top level.  We never exercise the serial
    # path in this test, but the import must succeed.
    fake_serial = types.ModuleType("serial")

    class _FakeSerial:
        def __init__(self) -> None:
            self.port = None
            self.baudrate = None
            self.timeout = None

        def read(self, _n: int = 0) -> bytes:
            return b""

        def write(self, _data: bytes) -> int:
            return 0

        def flush(self) -> None:
            return None

    fake_serial.Serial = _FakeSerial
    prev_serial = sys.modules.get("serial")
    sys.modules["serial"] = fake_serial
    try:
        spec.loader.exec_module(module)
    finally:
        if prev_serial is None:
            del sys.modules["serial"]
        else:
            sys.modules["serial"] = prev_serial
    return module


FAIL_COUNT = 0


def check(description: str, got: Any, want: Any) -> None:
    global FAIL_COUNT
    if got != want:
        sys.stderr.write(f"FAIL: {description} — got {got!r}, want {want!r}\n")
        FAIL_COUNT += 1


def _pick_free_udp_port() -> int:
    """Bind a transient UDP socket on port 0, learn the OS-assigned
    port, close, return it.  Tiny race window between close and the
    test's bind, but vanishingly small in practice and avoids
    hardcoding 5555 (which collides with a real firmware on the
    LAN if the test runs on the same machine that's also doing live
    development)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    finally:
        s.close()
    return port


def main() -> int:
    live_map = _load_live_map()

    # Use a fresh ephemeral port so this test never collides with a
    # firmware-driven 5555 broadcast on the same host.
    test_port = _pick_free_udp_port()
    live_map.UDP_PORT = test_port

    # Reset shared state so any prior import-time defaults don't bleed.
    with live_map.state_lock:
        live_map.state["current"] = None
        live_map.state["sats"] = 0
        live_map.state["hdop"] = -1.0

    reader = threading.Thread(target=live_map.udp_reader, daemon=True)
    reader.start()

    # Wait for the reader to bind + mark the source connected.  Bind
    # is synchronous inside udp_reader; the connected flag flips
    # immediately after.  We poll briefly to avoid a fixed sleep.
    deadline = time.time() + 2.0
    while time.time() < deadline:
        health = live_map._read_serial_health()
        if health.get("connected"):
            break
        time.sleep(0.02)
    health = live_map._read_serial_health()
    check("udp_reader marks source connected after bind",
          health.get("connected"), True)

    # Send a synthetic [gps-live] line via UDP.  Format must match the
    # firmware's snprintf in src/gps/gps_fix.cpp byte-for-byte; if it
    # ever drifts the parser gets the same problem either way.
    payload = (
        "[gps-live] lat=37.4220000 lon=-122.0840000 sats=11 fix_3d=1 "
        "speed=0.32 head=12.3 hdop=0.8 q=85 tier=2 t_us=1234567890\n"
    ).encode("utf-8")
    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sender.sendto(payload, ("127.0.0.1", test_port))
    finally:
        sender.close()

    # parse_line() runs synchronously inside udp_reader; the next
    # recvfrom() blocks until the next datagram, so we just need to
    # give the reader a moment to dispatch ours.
    deadline = time.time() + 2.0
    cur = None
    while time.time() < deadline:
        with live_map.state_lock:
            cur = live_map.state.get("current")
        if cur is not None:
            break
        time.sleep(0.02)

    check("udp_reader populated state.current after datagram",
          cur is not None and len(cur) == 2, True)
    if cur is not None and len(cur) == 2:
        check("udp parsed lat is in range",
              abs(cur[0] - 37.4220000) < 1e-6, True)
        check("udp parsed lon is in range",
              abs(cur[1] - (-122.0840000)) < 1e-6, True)

    with live_map.state_lock:
        sats = live_map.state.get("sats")
        hdop = live_map.state.get("hdop")
    check("udp parsed sats", sats, 11)
    check("udp parsed hdop", hdop, 0.8)

    if FAIL_COUNT == 0:
        print("test_live_map_udp: OK")
        return 0
    return FAIL_COUNT


if __name__ == "__main__":
    sys.exit(main())
