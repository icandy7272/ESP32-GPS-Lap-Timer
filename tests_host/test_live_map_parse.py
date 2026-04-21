"""Regression tests for `tools/live_map.py` parse hardening.

Codex P1 finding (2026-04-19): the previous `[0-9.\\-]+` regexes admitted
malformed tokens like `..` or `1..2`, which `float()` then raised
`ValueError` on, killing the serial_reader thread and silently freezing
the live map on its last frame.  These tests pin down that:

  1. Tightened regexes reject the malformed cases cleanly.
  2. parse_line() is crash-proof against unexpected input because
     serial_reader wraps it in try/except — but the first line of
     defence must remain the regexes themselves.

Run with: python3 tests_host/test_live_map_parse.py
Exit code: 0 on pass, non-zero on failure (pytest-free on purpose so the
existing tools/run_host_tests.sh loop can pick it up if extended).
"""

from __future__ import annotations

import importlib.util
import pathlib
import types
import sys
from typing import Any


def _load_live_map() -> Any:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    module_path = repo_root / "tools" / "live_map.py"
    spec = importlib.util.spec_from_file_location("live_map", module_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    # live_map imports pyserial at module import time.  The test only
    # exercises parsing / HTML assembly, so install a tiny fake module
    # ahead of exec_module() and avoid an environment-dependent skip.
    fake_serial = types.ModuleType("serial")

    class _FakeSerial:
        def __init__(self) -> None:
            self.port = None
            self.baudrate = None
            self.timeout = None
            self.dtr = False
            self.rts = False

        def open(self) -> None:
            return None

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


def main() -> int:
    live_map = _load_live_map()

    # --- Tightened regexes reject malformed numbers --------------------

    re_xing = live_map._XING_CANDIDATE_RE
    re_spread = live_map._DRAFT_SPREAD_RE
    re_latlon = live_map._LATLON_RE
    re_hdop = live_map._HDOP_RE

    # Well-formed baseline: every regex matches a valid example.
    check(
        "candidate: valid input matches",
        bool(re_xing.search(
            "[xing] L0 candidate u=0.475 overshoot=0.00 hdiff=+3.1 "
            "result=PASS reason=segment")),
        True,
    )
    check(
        "spread: valid input matches",
        bool(re_spread.search("spread=0.85m tier=High samples=47")),
        True,
    )
    check(
        "latlon: valid input matches",
        bool(re_latlon.search("lat=40.0059425 lon=116.4584473")),
        True,
    )

    # Malformed cases that killed the reader before: the regex must NOT
    # match, so float() is never called on garbage.
    malformed_candidates = [
        # Two dots in u value.
        "[xing] L0 candidate u=1..2 overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
        # Lone dot.
        "[xing] L0 candidate u=. overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
        # Empty number after `=`.
        "[xing] L0 candidate u= overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
        # Sign-only.
        "[xing] L0 candidate u=- overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",
    ]
    for idx, bad in enumerate(malformed_candidates):
        check(
            f"candidate: malformed case {idx} rejected",
            bool(re_xing.search(bad)),
            False,
        )

    check(
        "spread: malformed `spread=..m` rejected",
        bool(re_spread.search("spread=..m tier=High samples=5")),
        False,
    )
    check(
        "latlon: lone-dot lat rejected",
        bool(re_latlon.search("lat=. lon=116.0")),
        False,
    )
    check(
        "hdop: empty value rejected",
        bool(re_hdop.search("hdop=")),
        False,
    )

    # --- parse_line is crash-proof even on surprise input --------------
    #
    # If a future firmware emits something neither the old nor the new
    # regex anticipated, parse_line must not raise — the reader thread
    # depends on that invariant.  We test parse_line directly (the
    # try/except wrapper in serial_reader is a second line of defence,
    # not a replacement).

    surprise_inputs = [
        "",                                      # blank
        "\x00\x01\x02 garbage",                  # partial frame bytes
        "[gps] lat=.. lon=..",                   # malformed numeric fields
        "[draft] p1 = (not_a_number, also_bad)", # wrong shape
        "[xing] L0 candidate u=NaN overshoot=0.0 hdiff=+3 "
        "result=PASS reason=segment",           # float-ish token that our
                                                 # regex rejects
        "random unstructured log line",
        # Truncation mid-line:
        "[xing] L0 candidate u=0.5",
    ]
    for text in surprise_inputs:
        try:
            live_map.parse_line(text)
        except Exception as exc:  # pragma: no cover — test fails below
            check(f"parse_line({text!r}) must not raise", f"raised {exc!r}", "no raise")

    # --- high-rate gps-live lines update the same state as 1 Hz gps ----
    with live_map.state_lock:
        live_map.state["current"] = None
        live_map.state["trail"] = []
    live_map.parse_line(
        "[gps-live] lat=40.0059425 lon=116.4584473 sats=12 "
        "fix_3d=1 speed=4.50 head=185.0 hdop=0.8 q=92 tier=3"
    )
    with live_map.state_lock:
        check("gps-live updates current point",
              live_map.state["current"], [40.0059425, 116.4584473])
        check("gps-live appends trail point",
              live_map.state["trail"], [[40.0059425, 116.4584473]])
        check("gps-live updates sats", live_map.state["sats"], 12)
        check("gps-live updates fix_3d", live_map.state["fix_3d"], True)

    # --- live_map layout keeps custom panels away from Leaflet corners --
    html = live_map.HTML
    check(
        "HTML offsets info panel below top-left Leaflet controls",
        "#info{position:absolute;top:56px;left:8px" in html,
        True,
    )
    check(
        "HTML offsets right column below top-right Leaflet controls",
        "#right-col{position:absolute;top:56px;right:8px" in html,
        True,
    )

    # --- serial command ACK matching stays terminal-only and specific ---
    ack = live_map._match_command_ack
    check(
        "recording stop success ack",
        ack("recording stop", "[recording] stopped and saved"),
        (True, "stopped and saved"),
    )
    check(
        "recording stop not-recording is a failure ack",
        ack("recording stop", "[recording] (not recording — nothing to stop)"),
        (False, "not recording — nothing to stop"),
    )
    check(
        "recording start error ack",
        ack("recording start",
            "[recording] ERR: session_start_recording refused (wrong phase or SD failure)"),
        (False, "session_start_recording refused (wrong phase or SD failure)"),
    )
    check(
        "track save success ack",
        ack("track save", "[draft] saved: track_007 (home) length=12.5m heading=181.0"),
        (True, "saved: track_007"),
    )
    check(
        "sampling progress is not a terminal ACK",
        ack("mark p1", "[draft] sampling P1 for 5.0 s..."),
        None,
    )

    # --- recording-stop ACK matcher covers the firmware ERR variants --
    # Without these, a partial-save or refused-stop silently times out
    # and the operator sees a generic "firmware did not acknowledge"
    # message 5 seconds later with no actionable guidance.
    stop_refused = ack(
        "recording stop",
        "[recording] ERR: stop refused — recording still active")
    check(
        "stop refused is a terminal failure ACK",
        stop_refused is not None and stop_refused[0] is False,
        True,
    )
    check(
        "stop refused advises operator to retry",
        stop_refused is not None and "press Save again" in stop_refused[1],
        True,
    )
    partial = ack(
        "recording stop",
        "[recording] ERR: stopped but final session file was not committed")
    check(
        "stopped-but-not-committed is a terminal failure ACK",
        partial is not None and partial[0] is False,
        True,
    )
    check(
        "stopped-but-not-committed advises checking SD",
        partial is not None and "SD card" in partial[1],
        True,
    )

    # --- reboot detector debounces + kicks a re-bootstrap ----------
    # The boot banner prints many lines fast at reset; only the first
    # [BOOT-EARLY] SETUP_ENTRY should trigger the re-bootstrap, and
    # subsequent banners during the debounce window must be ignored
    # so the serial writer isn't flooded with duplicate commands.
    live_map._reboot_watch["last_bootstrap_ts"] = 0.0
    live_map._reboot_watch["bootstrap_in_flight"] = False
    live_map._maybe_handle_reboot("[BOOT-EARLY] SETUP_ENTRY")
    check(
        "first SETUP_ENTRY marks bootstrap in flight",
        live_map._reboot_watch["bootstrap_in_flight"],
        True,
    )
    # Another banner within debounce window must NOT spawn a second
    # thread — the existing one is still running.
    prev_ts = live_map._reboot_watch["last_bootstrap_ts"]
    live_map._maybe_handle_reboot("[BOOT-EARLY] SETUP_ENTRY")
    check(
        "second SETUP_ENTRY during in-flight window is ignored",
        live_map._reboot_watch["last_bootstrap_ts"],
        prev_ts,
    )
    # Non-banner lines never trigger the reboot path.
    live_map._reboot_watch["bootstrap_in_flight"] = False
    live_map._reboot_watch["last_bootstrap_ts"] = 0.0
    live_map._maybe_handle_reboot(
        "[gps] lat=40.0059425 lon=116.4584473 sats=12")
    check(
        "non-banner line never triggers bootstrap",
        live_map._reboot_watch["bootstrap_in_flight"],
        False,
    )

    # --- serial health surface + disconnect banner in HTML ---------
    # Default at test startup: reader never ran, so connected=False.
    health = live_map._read_serial_health()
    check(
        "serial health exposes a `connected` bool",
        isinstance(health.get("connected"), bool),
        True,
    )
    live_map._mark_serial_connected()
    check(
        "_mark_serial_connected flips the flag",
        live_map._read_serial_health()["connected"],
        True,
    )
    live_map._mark_serial_disconnected("Device not configured")
    h = live_map._read_serial_health()
    check(
        "_mark_serial_disconnected flips flag + records error",
        h["connected"] is False and h["last_error"] == "Device not configured",
        True,
    )
    check(
        "HTML includes the serial-disconnect banner element",
        '<div id="serial-banner"' in live_map.HTML,
        True,
    )
    check(
        "HTML update path paints the banner from state.serial",
        "renderSerialBanner(state.serial)" in live_map.HTML,
        True,
    )

    if FAIL_COUNT == 0:
        print("test_live_map_parse: OK")
        return 0
    return FAIL_COUNT


if __name__ == "__main__":
    sys.exit(main())
