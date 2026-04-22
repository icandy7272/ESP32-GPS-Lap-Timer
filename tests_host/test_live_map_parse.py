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
    with live_map._reboot_watch_lock:
        live_map._reboot_watch["last_bootstrap_ts"] = 0.0
        live_map._reboot_watch["bootstrap_in_flight"] = False
        live_map._reboot_watch["stream_ack_seen"] = False
    live_map._maybe_handle_reboot("[BOOT-EARLY] SETUP_ENTRY")
    with live_map._reboot_watch_lock:
        check(
            "first SETUP_ENTRY marks bootstrap in flight",
            live_map._reboot_watch["bootstrap_in_flight"],
            True,
        )
    # Another banner within debounce window must NOT spawn a second
    # thread — the existing one is still running.
    with live_map._reboot_watch_lock:
        prev_ts = live_map._reboot_watch["last_bootstrap_ts"]
    live_map._maybe_handle_reboot("[BOOT-EARLY] SETUP_ENTRY")
    with live_map._reboot_watch_lock:
        check(
            "second SETUP_ENTRY during in-flight window is ignored",
            live_map._reboot_watch["last_bootstrap_ts"],
            prev_ts,
        )
    # Non-banner lines never trigger the reboot path.
    with live_map._reboot_watch_lock:
        live_map._reboot_watch["bootstrap_in_flight"] = False
        live_map._reboot_watch["last_bootstrap_ts"] = 0.0
    live_map._maybe_handle_reboot(
        "[gps] lat=40.0059425 lon=116.4584473 sats=12")
    with live_map._reboot_watch_lock:
        check(
            "non-banner line never triggers bootstrap",
            live_map._reboot_watch["bootstrap_in_flight"],
            False,
        )

    # --- stream ACK from firmware flips stream_ack_seen ------------
    # The retry loop in _kick_reboot_bootstrap polls this flag and
    # exits early when set; parse_line() sets it on any [gps-live]
    # stream status line.
    with live_map._reboot_watch_lock:
        live_map._reboot_watch["stream_ack_seen"] = False
    live_map.parse_line("[gps-live] stream=10Hz")
    with live_map._reboot_watch_lock:
        check(
            "stream=NHz ACK flips stream_ack_seen",
            live_map._reboot_watch["stream_ack_seen"],
            True,
        )
    with live_map._reboot_watch_lock:
        live_map._reboot_watch["stream_ack_seen"] = False
    live_map.parse_line("[gps-live] stream=off")
    with live_map._reboot_watch_lock:
        check(
            "stream=off ACK also flips stream_ack_seen",
            live_map._reboot_watch["stream_ack_seen"],
            True,
        )

    # --- track_query_bootstrap() regression for codex finding #1 ---
    # Before the fix, the final `send(f"cat tracks/{track_name}")`
    # inside track_query_bootstrap() raised NameError because the
    # nested helper had been extracted into _send_runtime_bootstrap_
    # commands().  This test walks the exact crash path codex found:
    # serial already open, ls reply already seen, first_track known.
    live_map._ser_ref[0] = object()  # fake truthy handle
    live_map._track_discovery["ls_reply_seen"] = True
    live_map._track_discovery["first_track"] = "track_001.json"
    live_map._track_discovery["requested_cat"] = False
    try:
        live_map.track_query_bootstrap()
        check(
            "track_query_bootstrap completes without NameError",
            True,
            True,
        )
        check(
            "track_query_bootstrap sets requested_cat",
            live_map._track_discovery["requested_cat"],
            True,
        )
    except NameError as exc:  # pragma: no cover
        check(
            "track_query_bootstrap must not raise NameError",
            f"raised {exc!r}",
            "no raise",
        )
    except Exception:
        # Other exceptions (write on fake handle) are expected and
        # harmless for this test — we only care NameError is gone.
        pass
    finally:
        live_map._ser_ref[0] = None
        live_map._track_discovery["ls_reply_seen"] = False
        live_map._track_discovery["first_track"] = None
        live_map._track_discovery["requested_cat"] = False

    # --- GPS diagnostics stream parsing -----------------------------
    # The three new firmware log streams must all land in state.gps_diag.
    with live_map.state_lock:
        live_map.state["gps_diag"] = {
            "ubx_events": [],
            "nak_count": 0,
            "constellations": None,
            "verify": None,
        }
    live_map.parse_line("[gps-ubx] ACK CFG-NAV5 (cls=0x06 id=0x24)")
    live_map.parse_line("[gps-ubx] ACK CFG-SBAS (cls=0x06 id=0x16)")
    live_map.parse_line(
        "[gps-ubx] NAK CFG-GNSS (cls=0x06 id=0x3E) "
        "- receiver rejected this configuration; nak_count=1")
    with live_map.state_lock:
        evs = live_map.state["gps_diag"]["ubx_events"]
        check("ubx_events length after 3 lines", len(evs), 3)
        check("first event kind", evs[0]["kind"], "ACK")
        check("first event name", evs[0]["name"], "CFG-NAV5")
        check("first event cls", evs[0]["cls"], 0x06)
        check("first event id", evs[0]["id"], 0x24)
        check("nak event kind", evs[2]["kind"], "NAK")
        check("nak event name", evs[2]["name"], "CFG-GNSS")
        check("nak_count running total", live_map.state["gps_diag"]["nak_count"], 1)

    live_map.parse_line("[gps-const] GPS=12 GAL=6 BDS=10 GLO=7 QZS=0")
    with live_map.state_lock:
        c = live_map.state["gps_diag"]["constellations"]
        check("constellations dict present", c is not None, True)
        check("constellations GPS count", c["gps"], 12)
        check("constellations Galileo count", c["galileo"], 6)
        check("constellations BeiDou count", c["beidou"], 10)
        check("constellations GLONASS count", c["glonass"], 7)
        check("constellations QZSS count", c["qzss"], 0)

    live_map.parse_line("[gps-verify] active: GPS=Y SBAS=Y GAL=N BDS=N QZS=N GLO=Y")
    with live_map.state_lock:
        v = live_map.state["gps_diag"]["verify"]
        check("verify dict present", v is not None, True)
        check("verify GPS true", v["gps"], True)
        check("verify SBAS true", v["sbas"], True)
        check("verify Galileo false (NAK revert)", v["galileo"], False)
        check("verify BeiDou false (NAK revert)", v["beidou"], False)
        check("verify GLONASS true", v["glonass"], True)
        check("verify QZSS false", v["qzss"], False)

    # Malformed / partial diag lines must not crash the parser.
    for junk in [
        "[gps-ubx] something weird (no cls field)",
        "[gps-const] malformed counts",
        "[gps-verify] active: GPS=yes SBAS=no",  # non-Y/N values
        "[gps-const] GPS=12",                    # not all fields present
    ]:
        try:
            live_map.parse_line(junk)
        except Exception as exc:  # pragma: no cover
            check(
                f"parse_line({junk!r}) must not raise",
                f"raised {exc!r}",
                "no raise",
            )

    check(
        "HTML has the GPS receiver diagnostics panel",
        '<div id="gps-diag-panel">' in live_map.HTML,
        True,
    )
    check(
        "HTML update path calls renderGpsDiag",
        "renderGpsDiag(state.gps_diag)" in live_map.HTML,
        True,
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

    # --- codex follow-up review 2026-04-22 regressions ------------------
    # Five bugs that were found in the fix-pass commit itself, now
    # pinned so a revert or refactor regression trips a host test
    # instead of a walking test.
    ack_matcher = live_map._match_command_ack

    # Generic [draft] ERR: must NOT hijack a recording-command wait.
    # Before the cmd-gated fallback was added, an unrelated draft
    # error could falsely terminate the wrong pending command.
    check(
        "draft ERR does not attribute to recording start",
        ack_matcher("recording start", "[draft] ERR: no active draft"),
        None,
    )
    # Conversely, a recording ERR must not hijack a draft-command wait.
    check(
        "recording ERR does not attribute to mark p1",
        ack_matcher("mark p1", "[recording] ERR: stop refused — still active"),
        None,
    )
    # But the family-matched ones still work: [draft] ERR on mark p1
    # should still surface to mark-family commands.
    draft_err_on_draft = ack_matcher(
        "mark p1", "[draft] ERR: no draft in progress")
    check(
        "draft ERR still attributes to mark p1 (same family)",
        draft_err_on_draft is not None and draft_err_on_draft[0] is False,
        True,
    )
    rec_err_on_rec = ack_matcher(
        "recording start",
        "[recording] ERR: session_start_recording refused (wrong phase)")
    check(
        "recording ERR still attributes to recording start (same family)",
        rec_err_on_rec is not None and rec_err_on_rec[0] is False,
        True,
    )

    # _track_discovery JSON capture state is cleared by the
    # reconnect helper even when ls_reply_seen / first_track survive.
    live_map._track_discovery["in_json"] = True
    live_map._track_discovery["brace_depth"] = 2
    live_map._track_discovery["buf"] = ["{", "  \"name\": \"x\","]
    live_map._track_discovery["ls_reply_seen"] = True
    live_map._track_discovery["first_track"] = "track_042.json"
    live_map._reset_track_discovery_json_capture()
    check(
        "json reset clears in_json",
        live_map._track_discovery["in_json"],
        False,
    )
    check(
        "json reset clears brace_depth",
        live_map._track_discovery["brace_depth"],
        0,
    )
    check(
        "json reset clears buf",
        live_map._track_discovery["buf"],
        [],
    )
    check(
        "json reset preserves ls_reply_seen",
        live_map._track_discovery["ls_reply_seen"],
        True,
    )
    check(
        "json reset preserves first_track",
        live_map._track_discovery["first_track"],
        "track_042.json",
    )

    # --- [stop-trace] must surface to the events feed ---------------
    # Codex review 2026-04-22 High: before this filter hit,
    # parse_line() dropped [stop-trace] on the floor, so the diagnostic
    # markers added in the stop path were invisible in the tool the
    # operator was actually using during the walking test.  Pinning
    # here so a future refactor doesn't quietly remove the filter.
    with live_map.state_lock:
        live_map.state["events"] = []
        live_map.state["event_seq"] = 0
    live_map.parse_line(
        "[stop-trace] session_stop: entry t=12345")
    live_map.parse_line(
        "[stop-trace] end_session: synced took_ms=47")
    with live_map.state_lock:
        evs = live_map.state["events"]
        stop_evs = [
            e for e in evs
            if "[stop-trace]" in (e.get("text", "") if isinstance(e, dict) else str(e))
        ]
        check(
            "stop-trace lines surface in events feed",
            len(stop_evs),
            2,
        )

    # --- Track picker: [tracks-list] entries build the catalog ---
    # Firmware emits a `begin` marker, one line per track, then an
    # `end` terminator.  Parser accumulates into a pending buffer
    # and atomically swaps on `end` so a refresh (e.g. after a
    # track_delete from the phone UI) cleanly replaces stale entries.
    with live_map.state_lock:
        live_map.state["tracks_catalog"] = []
        live_map.state["_tracks_catalog_pending"] = None
        live_map.state["active_track_id"] = None
        live_map.state["active_track_name"] = None
    live_map.parse_line('[tracks-list] begin')
    live_map.parse_line('[tracks-list] track_001 "Home Loop"')
    live_map.parse_line('[tracks-list] track_002 "Test Track"')
    live_map.parse_line('[tracks-list] track_042 "With Spaces And 數字"')
    # Before `end` the pending list is populated but the public
    # catalog is still empty — UI must never see a half-rebuilt list.
    with live_map.state_lock:
        check("tracks_catalog still empty mid-refresh",
              live_map.state["tracks_catalog"], [])
        check("pending has 3 entries mid-refresh",
              len(live_map.state["_tracks_catalog_pending"]), 3)
    live_map.parse_line('[tracks-list] end')
    with live_map.state_lock:
        cat = live_map.state["tracks_catalog"]
        check("tracks_catalog length after end", len(cat), 3)
        check("catalog first id", cat[0]["id"], "track_001")
        check("catalog first name", cat[0]["name"], "Home Loop")
        check("catalog second id", cat[1]["id"], "track_002")
        check("catalog last id", cat[2]["id"], "track_042")
        check("pending cleared after end",
              live_map.state["_tracks_catalog_pending"], None)

    # Refresh with FEWER tracks (track_042 was deleted on firmware).
    # A second begin/end cycle must REPLACE the catalog, not merge —
    # this is the codex 2026-04-22 Medium fix for the stale-entry
    # bug.
    live_map.parse_line('[tracks-list] begin')
    live_map.parse_line('[tracks-list] track_001 "Home Loop Renamed"')
    live_map.parse_line('[tracks-list] track_002 "Test Track"')
    live_map.parse_line('[tracks-list] end')
    with live_map.state_lock:
        cat = live_map.state["tracks_catalog"]
        check("catalog shrunk on refresh (track_042 gone)", len(cat), 2)
        check("catalog first name updated on refresh",
              cat[0]["name"], "Home Loop Renamed")
        check("track_042 no longer in catalog",
              [t["id"] for t in cat if t["id"] == "track_042"], [])

    # [track] selected: and [track] auto-detected: both update active.
    live_map.parse_line("[track] selected: track_002 (Test Track)")
    with live_map.state_lock:
        check("active id after selected", live_map.state["active_track_id"], "track_002")
        check("active name after selected",
              live_map.state["active_track_name"], "Test Track")
    live_map.parse_line("[track] auto-detected: track_042 (With Spaces And 數字)")
    with live_map.state_lock:
        check("active id after auto-detect",
              live_map.state["active_track_id"], "track_042")
        check("active name after auto-detect",
              live_map.state["active_track_name"], "With Spaces And 數字")

    # --- Track picker ACK matcher ---
    # track select → requires [track] selected: <matching id>.  A
    # selected line for a DIFFERENT id must not ACK (codex review
    # 2026-04-22 Medium: two rapid dropdown changes could have the
    # second request ACK on the first request's echo).
    ack_pick = live_map._match_command_ack
    sel_ack = ack_pick("track select track_002",
                       "[track] selected: track_002 (Test Track)")
    check("track select success ACK is terminal True",
          sel_ack is not None and sel_ack[0] is True, True)
    # Wrong-id emit must NOT terminate our wait.
    wrong_id_ack = ack_pick(
        "track select track_002",
        "[track] selected: track_003 (Some Other)")
    check("track select ignores wrong-id emit", wrong_id_ack, None)
    # Autodetect emit must NOT terminate a manual select wait.
    auto_on_select = ack_pick(
        "track select track_002",
        "[track] auto-detected: track_002 (Test Track)")
    check("track select ignores auto-detected line", auto_on_select, None)
    sel_err = ack_pick("track select track_999",
                       "[track] ERR: track track_999 not found")
    check("track select error ACK is terminal False",
          sel_err is not None and sel_err[0] is False, True)
    check("track select error carries message",
          sel_err is not None and "not found" in sel_err[1], True)

    # track autodetect accepts ONLY auto-detected: lines, not selected:.
    auto_ack = ack_pick(
        "track autodetect",
        "[track] auto-detected: track_042 (My Track)")
    check("track autodetect success ACK",
          auto_ack is not None and auto_ack[0] is True, True)
    selected_on_auto = ack_pick(
        "track autodetect",
        "[track] selected: track_042 (My Track)")
    check("track autodetect ignores selected line",
          selected_on_auto, None)
    auto_err = ack_pick(
        "track autodetect",
        "[track] ERR: autodetect requires a 3D fix")
    check("track autodetect error ACK",
          auto_err is not None and auto_err[0] is False, True)

    # `tracks list` ACKs on the `end` terminator.
    list_ack = ack_pick("tracks list", "[tracks-list] end")
    check("tracks list end is terminal ACK",
          list_ack is not None and list_ack[0] is True, True)
    # A catalog entry line alone is NOT the terminal ACK.
    mid_ack = ack_pick("tracks list", '[tracks-list] track_003 "Foo"')
    check("tracks list mid-line is not terminal", mid_ack, None)

    # --- Allowed-commands gate: track select <id> must match id regex
    # but block path traversal and wrong prefix.
    is_allowed = live_map._is_allowed_command
    check("track select track_003 allowed",
          is_allowed("track select track_003"), True)
    check("track select track_42 allowed (2 digits)",
          is_allowed("track select track_42"), True)
    check("track select session_001 rejected (wrong prefix)",
          is_allowed("track select session_001"), False)
    check("track select track_ rejected (no digits)",
          is_allowed("track select track_"), False)
    check("track select track_12345 rejected (too many digits)",
          is_allowed("track select track_12345"), False)
    check("track select ../etc rejected (path traversal)",
          is_allowed("track select ../etc"), False)
    # Non-ASCII digits must be rejected even though Python's
    # str.isdigit() considers them digits — the firmware uses C
    # isdigit() and will reject them.  Forwarding them would
    # desync the ACK wait from the firmware (codex review
    # 2026-04-22 Low).
    check("track select with fullwidth digit rejected",
          is_allowed("track select track_\uff11"), False)
    check("track select with devanagari digit rejected",
          is_allowed("track select track_\u0966"), False)
    check("track autodetect allowed", is_allowed("track autodetect"), True)
    check("tracks list allowed", is_allowed("tracks list"), True)

    # --- Draft-saved must update picker state (codex Finding 2) ---
    # After the firmware emits `[draft] saved: track_NNN (Name) ...`,
    # the new track is active on the firmware side.  live_map must
    # immediately reflect this: catalog gets the entry (idempotent),
    # active_track_id + active_track_name flip.  Previously the
    # picker stayed stale until next reboot.
    with live_map.state_lock:
        live_map.state["tracks_catalog"] = [
            {"id": "track_001", "name": "Existing"},
        ]
        live_map.state["_tracks_catalog_pending"] = None
        live_map.state["active_track_id"] = "track_001"
        live_map.state["active_track_name"] = "Existing"
    live_map.parse_line(
        "[draft] saved: track_007 (Home Loop) length=12.50m heading=181.0")
    with live_map.state_lock:
        cat = live_map.state["tracks_catalog"]
        ids = [t["id"] for t in cat]
        check("catalog gains track_007 after draft saved",
              "track_007" in ids, True)
        t7 = next(t for t in cat if t["id"] == "track_007")
        check("catalog track_007 name captured", t7["name"], "Home Loop")
        check("active id flips to newly saved",
              live_map.state["active_track_id"], "track_007")
        check("active name flips to newly saved",
              live_map.state["active_track_name"], "Home Loop")
    # Re-emitting the same [draft] saved line is idempotent — no duplicate.
    live_map.parse_line(
        "[draft] saved: track_007 (Home Loop) length=12.50m heading=181.0")
    with live_map.state_lock:
        cat = live_map.state["tracks_catalog"]
        count_007 = sum(1 for t in cat if t["id"] == "track_007")
        check("re-emit draft saved doesn't duplicate catalog entry",
              count_007, 1)

    # Codex review 2026-04-22 Medium: names with parens must parse
    # correctly — firmware's is_track_name_valid accepts `(` and `)`,
    # and the emit path just interpolates them literally, so a valid
    # name like `Home (North)` produces
    # `[draft] saved: track_013 (Home (North)) length=...`.
    # Previous regex `([^)]+)` stopped at the first `)` and silently
    # dropped the catalog update.  Pin the fix here.
    with live_map.state_lock:
        live_map.state["tracks_catalog"] = []
        live_map.state["_tracks_catalog_pending"] = None
        live_map.state["active_track_id"] = None
        live_map.state["active_track_name"] = None
    live_map.parse_line(
        "[draft] saved: track_013 (Home (North)) length=8.33m heading=90.0")
    with live_map.state_lock:
        cat = live_map.state["tracks_catalog"]
        check("draft saved with parens-in-name lands in catalog",
              len(cat), 1)
        if cat:
            check("paren-in-name catalog id", cat[0]["id"], "track_013")
            check("paren-in-name catalog name captured verbatim",
                  cat[0]["name"], "Home (North)")
        check("paren-in-name active id flipped",
              live_map.state["active_track_id"], "track_013")
        check("paren-in-name active name captured verbatim",
              live_map.state["active_track_name"], "Home (North)")

    # Codex review 2026-04-22 third round Medium:
    # _TRACK_SELECTED_RE had the SAME parens bug as _DRAFT_SAVED_RE.
    # `[track] selected:` / `auto-detected:` lines carry the track
    # name verbatim; anchored greedy capture should handle embedded
    # parens correctly.
    with live_map.state_lock:
        live_map.state["active_track_id"] = None
        live_map.state["active_track_name"] = None
    live_map.parse_line("[track] selected: track_013 (Home (North))")
    with live_map.state_lock:
        check("[track] selected with parens captures full name",
              live_map.state["active_track_name"], "Home (North)")
        check("[track] selected with parens captures id",
              live_map.state["active_track_id"], "track_013")
    live_map.parse_line("[track] auto-detected: track_014 (Outer Loop (E))")
    with live_map.state_lock:
        check("[track] auto-detected with parens captures full name",
              live_map.state["active_track_name"], "Outer Loop (E)")
        check("[track] auto-detected with parens captures id",
              live_map.state["active_track_id"], "track_014")

    # Codex review 2026-04-22 third round Medium:
    # `is_track_name_valid` accepts characters like `)`, ` `, `=`,
    # digits, `.`, `m`, and letters, which means a valid name could
    # contain a fake `) length=<float>m heading=<float>` suffix and
    # spoof the parser.  The anchored-greedy regex + `$` end-of-line
    # forces the FINAL length/heading to come from the firmware
    # suffix, so the spoofed interior just becomes part of the name.
    with live_map.state_lock:
        live_map.state["tracks_catalog"] = []
        live_map.state["_tracks_catalog_pending"] = None
        live_map.state["active_track_id"] = None
        live_map.state["active_track_name"] = None
    spoof = ("[draft] saved: track_015 (Foo) length=1.0m heading=2.0) "
             "length=8.33m heading=90.0")
    live_map.parse_line(spoof)
    with live_map.state_lock:
        cat = live_map.state["tracks_catalog"]
        # The outer length/heading (the REAL firmware suffix) must
        # be the ones captured.  Name should include the entire
        # spoofed interior.
        check("delimiter-in-name spoof: catalog has one entry",
              len(cat), 1)
        if cat:
            check("spoof id",
                  cat[0]["id"], "track_015")
            check("spoof name absorbs spoofed interior",
                  cat[0]["name"], "Foo) length=1.0m heading=2.0")

    # Codex review 2026-04-22 third round Low:
    # Garbage-prefixed lines must not slip past search() with the
    # loose (\S+) id.  Anchoring `^[draft] saved:` + id constraint
    # `track_\d{1,3}` blocks these.
    with live_map.state_lock:
        live_map.state["tracks_catalog"] = []
        live_map.state["active_track_id"] = None
        live_map.state["active_track_name"] = None
    live_map.parse_line(
        "garbage [draft] saved: ../../evil (Injected) "
        "length=1.0m heading=2.0")
    with live_map.state_lock:
        check("garbage-prefixed [draft] saved ignored",
              live_map.state["tracks_catalog"], [])
        check("garbage-prefixed line does not flip active id",
              live_map.state["active_track_id"], None)
    live_map.parse_line(
        "[draft] saved: ../../evil (Injected) length=1.0m heading=2.0")
    with live_map.state_lock:
        check("bad-id [draft] saved ignored",
              live_map.state["tracks_catalog"], [])

    # Codex review 2026-04-22 third round Low:
    # Exact-match [tracks-list] begin/end, not startswith.
    # `[tracks-list] beginning something` or `[tracks-list] endjunk`
    # must not false-trigger.
    with live_map.state_lock:
        live_map.state["_tracks_catalog_pending"] = None
    live_map.parse_line("[tracks-list] beginning of time")
    with live_map.state_lock:
        check("[tracks-list] beginning does not open pending",
              live_map.state["_tracks_catalog_pending"], None)

    # Codex review 2026-04-22 third round Low:
    # If a draft saves WHILE a tracks-list refresh is mid-stream
    # (begin received, end not yet), the new entry must be upserted
    # into BOTH the public catalog AND the pending accumulator so
    # the `end` atomic swap doesn't silently drop it.
    with live_map.state_lock:
        live_map.state["tracks_catalog"] = [
            {"id": "track_001", "name": "Old"},
        ]
        live_map.state["_tracks_catalog_pending"] = None
        live_map.state["active_track_id"] = "track_001"
        live_map.state["active_track_name"] = "Old"
    live_map.parse_line("[tracks-list] begin")
    live_map.parse_line('[tracks-list] track_001 "Old"')
    # Mid-stream draft save.
    live_map.parse_line(
        "[draft] saved: track_042 (Fresh) length=10.0m heading=180.0")
    # End closes the swap.  Without the dual-upsert, track_042 would
    # be lost because pending has no record of it.
    live_map.parse_line("[tracks-list] end")
    with live_map.state_lock:
        cat = live_map.state["tracks_catalog"]
        ids = sorted(t["id"] for t in cat)
        check("mid-refresh draft save survives atomic swap",
              "track_042" in ids, True)
        check("active id after mid-refresh save",
              live_map.state["active_track_id"], "track_042")

    # --- Finding 3 narrowing: only begin/end in events, not entries ---
    # Previously the filter included every [tracks-list] entry, so a
    # 15-track catalog spammed 17 events per `tracks list`.  Now the
    # markers alone satisfy _wait_for_command_ack without noising up
    # the UI.
    with live_map.state_lock:
        live_map.state["events"] = []
        live_map.state["event_seq"] = 0
    live_map.parse_line("[tracks-list] begin")
    live_map.parse_line('[tracks-list] track_001 "Alpha"')
    live_map.parse_line('[tracks-list] track_002 "Beta"')
    live_map.parse_line("[tracks-list] end")
    with live_map.state_lock:
        evs = live_map.state["events"]
        list_evs = [
            e for e in evs
            if "[tracks-list]" in (e.get("text", "") if isinstance(e, dict) else str(e))
        ]
        # Expect exactly 2 (begin + end), not 4 (begin + 2 entries + end).
        check("only begin+end surface in events (not entries)",
              len(list_evs), 2)
        # Codex review 2026-04-22 Low: pin the exact text too — a
        # broken filter that emitted two entries and dropped `end`
        # would pass the count-only assertion but cause
        # _wait_for_command_ack("tracks list", ...) to time out
        # because it explicitly scans for `[tracks-list] end`.
        evs_texts = [e.get("text", "") for e in list_evs]
        check("first surfaced event is [tracks-list] begin",
              "[tracks-list] begin" in evs_texts[0], True)
        check("last surfaced event is [tracks-list] end",
              "[tracks-list] end" in evs_texts[-1], True)
        # And the entries are still parsed into the catalog.
        cat = live_map.state["tracks_catalog"]
        ids_after = [t["id"] for t in cat]
        check("entries still populate catalog despite filter",
              sorted(ids_after), ["track_001", "track_002"])

    if FAIL_COUNT == 0:
        print("test_live_map_parse: OK")
        return 0
    return FAIL_COUNT


if __name__ == "__main__":
    sys.exit(main())
