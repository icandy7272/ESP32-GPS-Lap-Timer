#!/usr/bin/env python3
"""Live GPS + detection-line visualisation over USB serial.

Requests the firmware's compact [gps-live] stream over USB serial for
higher-rate current position updates, still accepts the 1 Hz [gps]
diagnostic line as a fallback, reads the [track] boot-log lines for the
active P1/P2 segment, and lap / session events; serves a self-contained
canvas-based live map at http://127.0.0.1:8080.

Keeps one serial port open for the lifetime of the process — do NOT
run at the same time as capture_serial.py / pio device monitor, and
do NOT run the upload target while this is live.  The script does not
touch DTR/RTS so it will not reset the ESP32-S3 on open.

Usage:
    python3 tools/live_map.py [<port> [<baud>]]

Defaults: port = /dev/cu.usbserial-10, baud = 115200.

Drawn elements:
  - Red solid: actual P1-P2 detection segment
  - Yellow dashed: axial extension (CROSSING_END_TOLERANCE_M = 2 m
    walking mode, matches src/lap_timer/lap_timer_internal.h)
  - Green short line from midpoint: valid_heading direction
  - Blue polyline: recent GPS trail
  - Green dot: current position
  - Top-left panel: live lat/lon/speed/heading/sats/quality/hdop
  - Bottom: last 20 lap/session log lines with "X s ago" timestamps
"""

import json
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
except ImportError:
    sys.stderr.write(
        "pyserial not installed. Run:\n"
        "  ~/.platformio/penv/bin/pip install pyserial\n"
    )
    sys.exit(1)


PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-10"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
LISTEN = ("127.0.0.1", 8080)
TRAIL_MAX = 600
EVENT_MAX = 60
CANDIDATE_MAX = 40
LIVE_GPS_STREAM_HZ = 10

# Tolerance displayed on the map.  Keep in sync with the walking-test
# value of CROSSING_END_TOLERANCE_M in lap_timer_internal.h so the
# dashed extension drawn here matches the firmware's effective hitbox.
END_TOLERANCE_M = 2.0


state_lock = threading.Lock()
state = {
    "line": {},               # {p1:[lat,lon], p2:[lat,lon], heading:deg}
    "current": None,          # [lat, lon]
    "sats": 0,
    "fix_3d": False,
    "quality_score": 0,
    "quality_tier": 0,
    "hdop": -1.0,
    "trail": [],
    "events": [],
    "event_seq": 0,
    # Structured [xing] `candidate` events parsed from the firmware.  Used
    # by the Crossing Candidates panel and the Finish-Line Relative View
    # (View B) so the UI can show PASS/REJECT + reason without re-running
    # the geometry.  Each row carries line_type='active' (scored against
    # the loaded track) or line_type='draft' (dry-run against the
    # currently-marked P1/P2 during track creation).
    "candidates": [],
    # Accepted / rejected counters only for the draft validation path.
    # Live-updated by _parse_xing_draft_candidate().  Presented on the
    # candidate panel and, once Phase B ships, mirrors the state the
    # phone UI polls.
    "draft_validation": {
        "accepted": 0,
        "rejected": 0,
    },
    "started_at": time.time(),
    # Live state of the firmware's [draft] track-marking machine,
    # populated by parse_line() as serial events arrive.  Consumed by
    # the browser UI to enable/disable the Mark/Save/Cancel buttons
    # and to draw the draft line in a different colour than a saved
    # line.
    "draft": {
        "active": False,
        "name": "",
        "p1": None,          # [lat, lon] after mark p1
        "p2": None,
        "heading": None,     # deg after mark p2
        "status": "",        # last firmware message for the UI
    },
    # Mirror of the on-device TFT.  Updated at ~5 Hz by parse_line
    # from the firmware's [lcd] ... emissions.  None until the first
    # [lcd] line arrives — panel renders a "waiting" placeholder.
    "lcd": None,
    # Track picker catalog.  Populated from [tracks-list] lines the
    # firmware emits when we send `tracks list` at bootstrap or on
    # any later refresh (e.g. after a track save).  Keyed by id so
    # updates are idempotent and the UI can render a dropdown.
    # Stored as list of {id, name} for stable ordering.
    "tracks_catalog": [],
    # Transient accumulator for an in-flight refresh.  None when no
    # refresh is active; a list while we're between [tracks-list]
    # begin..end.  Atomically swapped into tracks_catalog on end so
    # the UI never sees a half-rebuilt list.
    "_tracks_catalog_pending": None,
    # Name of the track currently active on the firmware.  Sourced
    # from `[track] selected:` / `[track] auto-detected:` ACK lines
    # and from the firmware's session_state.track_name echo in
    # [lcd] lines.  None until first update.  Used by the UI to
    # highlight the selected entry in the dropdown and to render
    # a "no active track" state when empty.
    "active_track_id": None,
    "active_track_name": None,
    # GPS receiver diagnostics surfaced to the UI so an operator can
    # tell at a glance whether the last boot's UBX config writes were
    # accepted, which constellations are actually delivering satellite
    # data, and whether CFG-GNSS readback matches intent.  Populated
    # by parse_line() from three firmware log streams:
    #   - [gps-ubx] ACK|NAK CFG-XXX   → per-config accept/reject trail
    #   - [gps-const] GPS=N GAL=N ... → per-constellation sat counts
    #   - [gps-verify] active: ...    → ground-truth enable bits
    # All three are optional — older firmware won't emit them and the
    # UI just shows a "no data" state.
    "gps_diag": {
        "ubx_events": [],        # list of {ts, kind, name, cls, id}
        "nak_count": 0,          # running total of NAKs this session
        "constellations": None,  # last [gps-const] dict or None
        "verify": None,          # last [gps-verify] dict or None
    },
}

# Shared reference to the open serial port so the bootstrap thread
# below can write query commands.  Set inside serial_reader once the
# port is open.
_ser_ref: list = [None]

# Serial-link health.  `connected` is flipped to False when the
# reader catches a Device-not-configured / port-gone error, and
# back to True after a successful reopen.  Exposed via /api/state
# so the UI can render a warning banner ("USB disconnected — stop
# commands will fail") instead of silently serving stale data.
# `last_error` carries the string of the most recent exception for
# one-click debugging from the browser.
_serial_health: dict = {
    "connected": False,
    "last_error": None,
    "last_connect_ts": 0.0,
    "last_disconnect_ts": 0.0,
}
_serial_health_lock = threading.Lock()


def _mark_serial_connected() -> None:
    with _serial_health_lock:
        _serial_health["connected"] = True
        _serial_health["last_error"] = None
        _serial_health["last_connect_ts"] = time.time()


def _mark_serial_disconnected(err: str) -> None:
    with _serial_health_lock:
        was_connected = _serial_health["connected"]
        _serial_health["connected"] = False
        _serial_health["last_error"] = err
        if was_connected:
            _serial_health["last_disconnect_ts"] = time.time()


def _read_serial_health() -> dict:
    with _serial_health_lock:
        return dict(_serial_health)
_serial_write_lock = threading.Lock()


def _locked_update(**kwargs):
    with state_lock:
        for k, v in kwargs.items():
            state[k] = v


def _locked_set_line(key, *vals):
    with state_lock:
        line = state.setdefault("line", {})
        if len(vals) == 1:
            line[key] = vals[0]
        else:
            line[key] = list(vals)


def _locked_append_trail(lat, lon):
    with state_lock:
        trail = state["trail"]
        trail.append([lat, lon])
        if len(trail) > TRAIL_MAX:
            del trail[: len(trail) - TRAIL_MAX]


def _locked_append_event(text):
    with state_lock:
        events = state["events"]
        state["event_seq"] += 1
        events.append({
            "seq": state["event_seq"],
            "t": time.time(),
            "text": text,
        })
        if len(events) > EVENT_MAX:
            del events[: len(events) - EVENT_MAX]


def _serial_write_command(cmd: str) -> None:
    """Write a command to the firmware, serialised against both
    concurrent writers AND the reader's disconnect / reconnect path.

    Codex review 2026-04-22 flagged a real race: the previous version
    snapshotted _ser_ref[0] outside the lock, so the reader thread
    could close the port and null the ref after the snapshot but
    before the write, then the writer would call .write() on a dead
    handle.  Now the snapshot, the None-check, and the write itself
    all happen inside _serial_write_lock — which serial_reader() also
    takes when dropping/replacing the handle on disconnect.
    """
    with _serial_write_lock:
        ser = _ser_ref[0]
        if ser is None:
            raise RuntimeError("serial not open")
        ser.write((cmd + "\r\n").encode("utf-8"))
        ser.flush()


# Numeric regex building blocks.  The previous `[0-9.\-]+` / `[\d.]+`
# patterns accepted malformed tokens like `..`, `1..2`, or a lone `-`.
# `float()` then raised `ValueError` inside parse_line, killing the
# serial_reader thread and silently freezing the live map on the last
# frame — exactly the kind of sandbox-hostile-input failure the
# 2026-04-18 debugging roadmap warned about for "untrusted serial
# devices".  These patterns accept ONLY well-formed signed decimals.
_FLOAT = r"-?\d+(?:\.\d+)?"          # "-3.14", "42", "0", "-0.5"
_SIGNED_FLOAT = r"[+-]?\d+(?:\.\d+)?"  # also allows leading '+'

_P1_RE = re.compile(rf"p1\s*=\s*\(({_FLOAT}),\s*({_FLOAT})\)")
_P2_RE = re.compile(rf"p2\s*=\s*\(({_FLOAT}),\s*({_FLOAT})\)")
_HEAD_RE = re.compile(rf"valid_heading\s*=\s*({_FLOAT})\s*deg")
_LATLON_RE = re.compile(rf"lat=({_FLOAT})\s+lon=({_FLOAT})")
_SATS_RE = re.compile(r"sats=(\d+)(?:-(\d+))?")
_FIX3D_RE = re.compile(r"fix_3d=(\d)")
_Q_RE = re.compile(r"q=(\d+)\s+tier=(\d+)")
_HDOP_RE = re.compile(rf"hdop=({_FLOAT})")

# GPS receiver diagnostics emitted by the firmware after 2026-04-22:
#   [gps-ubx]   ACK|NAK CFG-NAV5 (cls=0x06 id=0x24)
#   [gps-const] GPS=12 GAL=8 BDS=10 GLO=7 QZS=0
#   [gps-verify] active: GPS=Y SBAS=Y GAL=Y BDS=Y QZS=N GLO=Y
_UBX_ACK_RE = re.compile(
    r"\[gps-ubx\]\s+(ACK|NAK)\s+(\S+)\s+\(cls=0x([0-9a-fA-F]+)\s+id=0x([0-9a-fA-F]+)\)"
)
_GPS_CONST_RE = re.compile(
    r"\[gps-const\]\s+"
    r"GPS=(\d+)\s+GAL=(\d+)\s+BDS=(\d+)\s+GLO=(\d+)\s+QZS=(\d+)"
)
_GPS_VERIFY_RE = re.compile(
    r"\[gps-verify\]\s+active:\s+"
    r"GPS=([YN])\s+SBAS=([YN])\s+GAL=([YN])\s+BDS=([YN])\s+QZS=([YN])\s+GLO=([YN])"
)

_TRACK_FILE_RE = re.compile(r"^(track_\d+\.json)\s*$")

# Catalog entries emitted by the firmware's `tracks list` command.
# Format: `[tracks-list] track_003 "My Home"` + terminator line
# `[tracks-list] end`.  The name can contain spaces, colons, and any
# ASCII byte that firmware's is_track_name_valid() allows; the `"`
# character itself is replaced with `_` by the firmware before emit,
# so we can safely match on the outer quotes.
_TRACKS_LIST_ENTRY_RE = re.compile(
    r'\[tracks-list\]\s+(track_\d{1,3})\s+"([^"]*)"'
)
# Active-track notifications.  The firmware emits these for:
#   - "track select" serial command (manual)
#   - "track autodetect" serial command (manual)
#   - late auto-detect on first 3D fix at boot
#   - /api/tracks/select HTTP path (browser phone UI)
# Any of them can arrive at any time, so we update state["active_track_*"]
# from both lines uniformly.
_TRACK_SELECTED_RE = re.compile(
    r'\[track\]\s+(?:selected|auto-detected):\s+(track_\d{1,3})\s+\(([^)]*)\)'
)
_SERIAL_HEADER_RE = re.compile(r"^\[serial\] --- tracks/([^ ]+) ---")
_DRAFT_STARTED_RE = re.compile(r"\[draft\] started: (\S+)")
_DRAFT_P1_RE = re.compile(
    rf"\[draft\] p1\s*=\s*\(({_FLOAT}),\s*({_FLOAT})\)"
)
_DRAFT_P2_RE = re.compile(
    rf"\[draft\] p2\s*=\s*\(({_FLOAT}),\s*({_FLOAT})\)(?:\s+heading=({_FLOAT}))?"
)
_DRAFT_SAVED_RE = re.compile(
    rf"\[draft\] saved: (\S+) \(([^)]+)\) length=({_FLOAT})m heading=({_FLOAT})"
)
# Spread + confidence tier appended to `[draft] p1/p2` lines by the
# 5-second sampling path.  Optional so old firmwares that do not emit
# these fields still yield a valid P1/P2 parse.
_DRAFT_SPREAD_RE = re.compile(
    rf"spread=({_FLOAT})m\s+tier=(\w+)(?:\s+samples=(\d+))?"
)
_RECORDING_STARTED_RE = re.compile(r"\[recording\] started:\s+(.+)$")
# Structured crossing candidate — emitted by lap_timer_crossing.cpp on
# every side-flip of a detection line (both PASS and REJECT cases).  See
# emit_candidate_event() in that file for the field contract.
_XING_CANDIDATE_RE = re.compile(
    rf"\[xing\] L(\d+) candidate u=({_FLOAT}) overshoot=({_FLOAT}) "
    rf"hdiff=({_SIGNED_FLOAT}) result=(\w+) reason=(\w+)"
)
# Dry-run candidate against the draft validation line — emitted by
# lap_timer_draft_validation.cpp while the operator is creating a
# new track.  Same fields, different prefix; no L<i> because draft is
# a single untyped line.  Stored in a separate list so the UI can
# distinguish active-track candidates (red/green) from draft
# candidates (orange).
_XING_DRAFT_CANDIDATE_RE = re.compile(
    rf"\[xing-draft\] candidate u=({_FLOAT}) overshoot=({_FLOAT}) "
    rf"hdiff=({_SIGNED_FLOAT}) result=(\w+) reason=(\w+)"
)
# LCD mirror — firmware display_task emits this at ~5 Hz carrying the
# exact state the on-device TFT is showing.  Field list is stable;
# extra fields are accepted and ignored by callers.
# Example:
#   [lcd] screen=DRIVING state=NORMAL rec=1 lap=3 cur_ms=42300 \
#       delta_ms=-123 delta_valid=1 best_ms=43500 speed=4.52 \
#       track="test2" sats=10 fix3d=1 off=0 bg=green
_LCD_RE = re.compile(r"\[lcd\]\s+(.+)$")
_LCD_KV_RE = re.compile(r'(\w+)=(?:"([^"]*)"|(\S+))')


def _parse_lcd_mirror(line: str) -> None:
    """Parse one `[lcd] k=v k=v ...` state line from the firmware.

    Populates state['lcd'] with typed fields the browser mock LCD
    needs.  Bad / missing fields leave their slot as None without
    raising — the panel renders what it can.
    """
    m = _LCD_RE.search(line)
    if not m:
        return
    body = m.group(1)
    kv: dict[str, str] = {}
    for km in _LCD_KV_RE.finditer(body):
        k = km.group(1)
        v = km.group(2) if km.group(2) is not None else km.group(3)
        kv[k] = v

    def _int(k: str, default: int = 0) -> int:
        try:
            return int(kv.get(k, default))
        except (TypeError, ValueError):
            return default

    def _float(k: str, default: float = 0.0) -> float:
        try:
            return float(kv.get(k, default))
        except (TypeError, ValueError):
            return default

    snap = {
        "screen": kv.get("screen", ""),
        "state": kv.get("state", ""),
        "rec": _int("rec"),
        "lap": _int("lap"),
        "cur_ms": _int("cur_ms"),
        "delta_ms": _int("delta_ms"),
        "delta_valid": _int("delta_valid"),
        "best_ms": _int("best_ms"),
        "speed": _float("speed"),
        "track": kv.get("track", ""),
        "sats": _int("sats"),
        "fix3d": _int("fix3d"),
        "off": _int("off"),
        "bg": kv.get("bg", "black"),
        "t": time.time(),
    }
    with state_lock:
        state["lcd"] = snap


def _extract_draft_spread(line: str):
    """Pull spread_m / tier / samples from a `[draft] p1/p2` line.

    Returns (spread_m | None, tier | None, samples | None).  Absent
    fields are tolerated so an older firmware that does not emit them
    still works.
    """
    m = _DRAFT_SPREAD_RE.search(line)
    if not m:
        return (None, None, None)
    spread = float(m.group(1))
    tier = m.group(2)
    samples = int(m.group(3)) if m.group(3) else None
    return (spread, tier, samples)


def _parse_xing_candidate(line: str) -> None:
    """Extract one `[xing] ... candidate ...` line into structured state.

    The firmware emits one candidate per side-flip of the infinite line
    through P1-P2 — whether or not the crossing is accepted.  The live
    UI renders these on the Finish-Line Relative View (View B) and in a
    Crossing Candidates panel so the operator can see exactly why the
    latest pass / miss was classified the way it was.
    """
    m = _XING_CANDIDATE_RE.search(line)
    if not m:
        return
    ev = {
        "t": time.time(),
        "line_idx": int(m.group(1)),
        "u": float(m.group(2)),
        "overshoot": float(m.group(3)),
        "hdiff": float(m.group(4)),
        "result": m.group(5),
        "reason": m.group(6),
        "line_type": "active",
    }
    with state_lock:
        cands = state["candidates"]
        cands.append(ev)
        if len(cands) > CANDIDATE_MAX:
            del cands[: len(cands) - CANDIDATE_MAX]


def _parse_xing_draft_candidate(line: str) -> None:
    """Extract one `[xing-draft] candidate ...` line.

    Emitted by the lap_timer's draft validation path while an operator
    is creating a new track and has marked both endpoints.  Landed in
    the same candidate list as active-track events but tagged
    line_type='draft' so the UI can colour them differently.

    `session_stale` suffix: the firmware tags events it evaluated
    against a session that was cleared/replaced before the counter
    write landed.  Firmware deliberately does NOT commit those to its
    own accepted/rejected totals — so we must NOT increment the local
    laptop-side counters either, or live_map counts drift from
    `/api/tracks/draft_validation`.  Still surface the event in the
    list with a distinct tag so the operator can see what happened.
    Codex P2 from 2026-04-19 round-3 review.
    """
    m = _XING_DRAFT_CANDIDATE_RE.search(line)
    if not m:
        return
    stale = "session_stale" in line
    ev = {
        "t": time.time(),
        "line_idx": -1,
        "u": float(m.group(1)),
        "overshoot": float(m.group(2)),
        "hdiff": float(m.group(3)),
        "result": m.group(4),
        "reason": m.group(5),
        "line_type": "draft",
        "stale": stale,
    }
    with state_lock:
        cands = state["candidates"]
        cands.append(ev)
        if len(cands) > CANDIDATE_MAX:
            del cands[: len(cands) - CANDIDATE_MAX]
        # Only committed events count toward the accepted/rejected
        # summary that the UI shows and that the phone web UI polls.
        if not stale:
            counts = state.setdefault("draft_validation", {
                "accepted": 0, "rejected": 0,
            })
            if ev["result"] == "PASS":
                counts["accepted"] = counts.get("accepted", 0) + 1
            else:
                counts["rejected"] = counts.get("rejected", 0) + 1


def _parse_draft_event(line: str) -> None:
    m = _DRAFT_STARTED_RE.search(line)
    if m:
        with state_lock:
            state["draft"] = {
                "active": True,
                "name": m.group(1),
                "p1": None,
                "p2": None,
                "heading": None,
                "status": f"Draft '{m.group(1)}' started. Mark P1.",
            }
        return
    m = _DRAFT_P1_RE.search(line)
    if m:
        spread, tier, samples = _extract_draft_spread(line)
        with state_lock:
            state["draft"]["p1"] = [float(m.group(1)), float(m.group(2))]
            state["draft"]["p1_spread_m"] = spread
            state["draft"]["p1_tier"] = tier
            state["draft"]["p1_samples"] = samples
            tier_note = f" ({tier}, ±{spread:.2f}m)" if spread is not None else ""
            state["draft"]["status"] = f"P1 marked{tier_note}. Walk to P2."
        return
    m = _DRAFT_P2_RE.search(line)
    if m:
        spread, tier, samples = _extract_draft_spread(line)
        with state_lock:
            state["draft"]["p2"] = [float(m.group(1)), float(m.group(2))]
            if m.group(3):
                state["draft"]["heading"] = float(m.group(3))
            state["draft"]["p2_spread_m"] = spread
            state["draft"]["p2_tier"] = tier
            state["draft"]["p2_samples"] = samples
            tier_note = f" ({tier}, ±{spread:.2f}m)" if spread is not None else ""
            state["draft"]["status"] = f"P2 marked{tier_note}. Review and Save."
        return
    m = _DRAFT_SAVED_RE.search(line)
    if m:
        saved_id = m.group(1)
        with state_lock:
            state["draft"] = {
                "active": False,
                "name": "",
                "p1": None,
                "p2": None,
                "heading": None,
                "status": (f"Saved {m.group(1)} ({m.group(2)}) — "
                           f"line {m.group(3)} m, heading {m.group(4)}°"),
            }
        # Re-read the just-saved file over serial so the red "active
        # track" overlay updates from the SD-backed JSON rather than
        # leaving the old track geometry on screen until reboot.
        try:
            _serial_write_command(f"cat tracks/{saved_id}.json")
        except RuntimeError:
            pass
        return
    if "[draft] cancelled" in line:
        with state_lock:
            state["draft"] = {
                "active": False,
                "name": "",
                "p1": None,
                "p2": None,
                "heading": None,
                "status": "Cancelled.",
            }
        return
    if "[draft] ERR:" in line:
        with state_lock:
            # Preserve current p1/p2/name on error so the operator can
            # retry the failing step without losing context.
            state["draft"]["status"] = line.split("[draft] ERR:", 1)[1].strip()
        return
    if "[draft] sampling" in line:
        with state_lock:
            state["draft"]["status"] = line.split("[draft]", 1)[1].strip()
        return

# Bootstrap state for the on-startup "cat tracks/<first>.json" query.
# The parser holds a small JSON-capture state machine that activates
# when it sees the serial-console header and deactivates once it has
# consumed a complete top-level object (matched braces).  This keeps
# the feature entirely client-side — no firmware changes needed.
_track_discovery = {
    "ls_reply_seen": False,
    "first_track": None,
    "requested_ls": False,
    "requested_cat": False,
    "in_json": False,
    "brace_depth": 0,
    "buf": [],
}


def _reset_track_discovery_json_capture() -> None:
    """Drop any in-flight JSON capture state.

    If a ``cat tracks/*.json`` reply is cut mid-stream (USB drop,
    board reboot with bytes in transit, user pressing stop between
    braces), the parser's brace-depth counter stays positive and
    ``in_json`` stays True forever, causing ``parse_line()`` to
    swallow every subsequent line as part of the same (never-ending)
    JSON.  Call this on reconnect / reboot so the next cat can start
    fresh.  ls_reply_seen / first_track are preserved because those
    are SD-card facts that don't change across a simple reconnect.
    (Codex review 2026-04-22.)
    """
    _track_discovery["in_json"] = False
    _track_discovery["brace_depth"] = 0
    _track_discovery["buf"] = []

# Reboot detection + auto re-bootstrap.  Firmware's live-stream rate
# lives in a static uint8_t that resets to 0 on every ESP32 reboot, so
# after a board reset the 10 Hz [gps-live] feed silently falls back to
# the 1 Hz [gps] diagnostic.  We catch the boot banner below and
# re-send the bootstrap commands so the operator gets smooth trails
# across reboots without touching the laptop.
#
# Codex review 2026-04-22 caught two real bugs in the earlier
# fire-and-forget approach:
#
#   1) The 2-second settle fired commands before the firmware's
#      main loop was consuming USB serial, so on a slow-fix boot
#      (up to 30 s in gps_uart_init's wait-for-fix loop) the
#      commands were dropped silently.  Now we retry every 3 s
#      and stop the moment we observe the firmware's ACK line
#      (`[gps-live] stream=XHz`), so the exact boot cadence
#      doesn't matter.
#
#   2) The dict was mutated from three places (parse_line reader
#      thread, the kicked worker, and _maybe_handle_reboot) with
#      no synchronisation, so the debounce guarantee wasn't real.
#      A lock around every access closes that race.
_reboot_watch = {
    "last_bootstrap_ts": 0.0,
    "bootstrap_in_flight": False,
    # Flipped to True by parse_line() when the firmware acknowledges
    # "gps stream N" — the retry loop stops the moment this goes
    # True.  Reset on every new kick so one reboot's ack doesn't
    # satisfy the next reboot's kick.
    "stream_ack_seen": False,
}
_reboot_watch_lock = threading.Lock()
_REBOOT_DEBOUNCE_S = 3.0   # Ignore repeat banners within 3 s of last kick.
_REBOOT_RETRY_INTERVAL_S = 3.0
_REBOOT_MAX_RETRIES = 15   # 15 * 3 s = 45 s, covers the firmware's 30 s
                           # wait-for-fix window plus ~15 s headroom.


def _try_parse_track_json(raw_lines: list[str]) -> None:
    try:
        data = json.loads("\n".join(raw_lines))
    except json.JSONDecodeError:
        return
    sf = data.get("start_finish") if isinstance(data, dict) else None
    if not isinstance(sf, dict):
        return
    lat1, lon1 = sf.get("lat1"), sf.get("lon1")
    lat2, lon2 = sf.get("lat2"), sf.get("lon2")
    heading = sf.get("heading")
    if lat1 is None or lon1 is None or lat2 is None or lon2 is None:
        return
    _locked_set_line("p1", float(lat1), float(lon1))
    _locked_set_line("p2", float(lat2), float(lon2))
    if heading is not None:
        _locked_set_line("heading", float(heading))
    print(f"[live_map] track loaded from serial: "
          f"P1=({lat1}, {lon1}) P2=({lat2}, {lon2}) heading={heading}")


def parse_line(line: str) -> None:
    # First thing: watch for a fresh-boot banner so the reboot
    # detector can auto-resend `gps stream 10`.  Placed before any
    # other parsing because it must not be short-circuited by a
    # regex miss below.
    _maybe_handle_reboot(line)

    # Boot-log track geometry (arrives once, at boot).
    m = _P1_RE.search(line)
    if m:
        _locked_set_line("p1", float(m.group(1)), float(m.group(2)))
    m = _P2_RE.search(line)
    if m:
        _locked_set_line("p2", float(m.group(1)), float(m.group(2)))
    m = _HEAD_RE.search(line)
    if m:
        _locked_set_line("heading", float(m.group(1)))

    # Serial-console "ls tracks" response: capture first track filename.
    if _track_discovery["requested_ls"] and not _track_discovery["ls_reply_seen"]:
        m = _TRACK_FILE_RE.match(line.strip())
        if m and _track_discovery["first_track"] is None:
            _track_discovery["first_track"] = m.group(1)
        if line.startswith("[serial] count="):
            _track_discovery["ls_reply_seen"] = True

    # Serial-console "cat tracks/XXX.json" response: buffer between the
    # header line and the matching closing brace, then parse as JSON.
    if _SERIAL_HEADER_RE.match(line):
        _track_discovery["in_json"] = True
        _track_discovery["brace_depth"] = 0
        _track_discovery["buf"] = []
        return
    if _track_discovery["in_json"]:
        _track_discovery["buf"].append(line)
        _track_discovery["brace_depth"] += line.count("{") - line.count("}")
        # A complete top-level object is one that reaches depth 0 after
        # at least one opening brace has been seen.
        joined = "\n".join(_track_discovery["buf"])
        if "{" in joined and _track_discovery["brace_depth"] == 0:
            _try_parse_track_json(_track_discovery["buf"])
            _track_discovery["in_json"] = False
            _track_discovery["buf"] = []
        return

    # GPS receiver diagnostics surface.  Kept ABOVE the big [gps]
    # dispatch below because these are cheap string matches and must
    # not be missed by the heavier regex below firing first.
    m = _UBX_ACK_RE.search(line)
    if m:
        kind = m.group(1)      # "ACK" or "NAK"
        name = m.group(2)      # e.g. "CFG-NAV5"
        cls_id = int(m.group(3), 16)
        msg_id = int(m.group(4), 16)
        with state_lock:
            ev_list = state["gps_diag"]["ubx_events"]
            ev_list.append({
                "ts": time.time(),
                "kind": kind,
                "name": name,
                "cls": cls_id,
                "id": msg_id,
            })
            # Cap to last 32 events — one boot generates ~8 ACKs and
            # we only expect a NAK during a pathology.
            if len(ev_list) > 32:
                del ev_list[:-32]
            if kind == "NAK":
                state["gps_diag"]["nak_count"] = (
                    state["gps_diag"].get("nak_count", 0) + 1
                )
        # Don't return — fall through so the event also lands in the
        # general [events] feed for the operator to scroll through.

    m = _GPS_CONST_RE.search(line)
    if m:
        with state_lock:
            state["gps_diag"]["constellations"] = {
                "gps":     int(m.group(1)),
                "galileo": int(m.group(2)),
                "beidou":  int(m.group(3)),
                "glonass": int(m.group(4)),
                "qzss":    int(m.group(5)),
                "ts": time.time(),
            }

    m = _GPS_VERIFY_RE.search(line)
    if m:
        with state_lock:
            state["gps_diag"]["verify"] = {
                "gps":     m.group(1) == "Y",
                "sbas":    m.group(2) == "Y",
                "galileo": m.group(3) == "Y",
                "beidou":  m.group(4) == "Y",
                "qzss":    m.group(5) == "Y",
                "glonass": m.group(6) == "Y",
                "ts": time.time(),
            }

    # Track catalog (dropdown picker).  The firmware emits a
    # [tracks-list] begin marker, one entry per track, then a
    # [tracks-list] end terminator.  The begin marker is the clean-
    # slate point: swap the pending list for the old one on `end` so
    # a refresh (e.g. after a track_delete on the phone UI) doesn't
    # leave stale entries in the dropdown (codex review 2026-04-22
    # Medium: previously append/update-only so deleted tracks stayed
    # visible forever).  Mid-session refresh is supported — a new
    # `begin` cleanly restarts the pending accumulator.
    if line.startswith("[tracks-list] begin"):
        with state_lock:
            state["_tracks_catalog_pending"] = []
    m = _TRACKS_LIST_ENTRY_RE.search(line)
    if m:
        tid, tname = m.group(1), m.group(2)
        with state_lock:
            # If we're between begin..end, accumulate into the
            # pending list.  If we missed the begin marker (older
            # firmware, or we connected mid-stream), fall back to the
            # old upsert-in-place semantics so we still collect
            # something useful.
            pending = state.get("_tracks_catalog_pending")
            if pending is not None:
                pending.append({"id": tid, "name": tname})
            else:
                cat = state["tracks_catalog"]
                for entry in cat:
                    if entry.get("id") == tid:
                        entry["name"] = tname
                        break
                else:
                    cat.append({"id": tid, "name": tname})
    if line.startswith("[tracks-list] end"):
        with state_lock:
            pending = state.get("_tracks_catalog_pending")
            if pending is not None:
                # Atomic swap — replaces any stale entries from
                # before this refresh.
                state["tracks_catalog"] = pending
                state["_tracks_catalog_pending"] = None

    # Active-track changes (manual select, autodetect, or the boot
    # auto-detect path).  Mirror into state so the dropdown can
    # render the current selection.
    m = _TRACK_SELECTED_RE.search(line)
    if m:
        tid, tname = m.group(1), m.group(2)
        with state_lock:
            state["active_track_id"] = tid
            state["active_track_name"] = tname

    if line.startswith("[gps]") or line.startswith("[gps-live]"):
        m = _LATLON_RE.search(line)
        if m:
            lat, lon = float(m.group(1)), float(m.group(2))
            _locked_update(current=[lat, lon])
            _locked_append_trail(lat, lon)
        m = _SATS_RE.search(line)
        if m:
            max_sats = int(m.group(2) or m.group(1))
            _locked_update(sats=max_sats)
        m = _FIX3D_RE.search(line)
        if m:
            _locked_update(fix_3d=(m.group(1) == "1"))
        m = _Q_RE.search(line)
        if m:
            _locked_update(
                quality_score=int(m.group(1)),
                quality_tier=int(m.group(2)),
            )
        m = _HDOP_RE.search(line)
        if m:
            _locked_update(hdop=float(m.group(1)))

    if ("[lap]" in line) or ("[session]" in line) or ("[recording]" in line) \
            or ("[xing]" in line) or ("[xing-draft]" in line) \
            or line.startswith("[gps-live] stream") \
            or line.startswith("[gps-ubx]") \
            or line.startswith("[gps-verify]") \
            or line.startswith("[stop-trace]") \
            or line.startswith("[track] selected:") \
            or line.startswith("[track] auto-detected:") \
            or line.startswith("[track] ERR:") \
            or line.startswith("[tracks-list]"):
        # [tracks-list] entries + begin/end markers need to be in the
        # events feed so _wait_for_command_ack can see the terminator
        # (codex review 2026-04-22 Medium: previously `tracks list`
        # POSTs returned 504 despite the firmware having emitted the
        # `end` line, because the event filter dropped them).
        # [stop-trace] is the firmware's per-step diagnostic emitted
        # during session_stop_recording / storage_end_session.  Must
        # be in the event feed so the operator can see which
        # milestone was the last-emitted one when a Save Recording
        # hangs — without this filter hit, the markers are parsed
        # but discarded, defeating the whole point of the commit
        # (codex review 2026-04-22 High).
        _locked_append_event(line)

    # Any firmware acknowledgement of the gps-stream command lets the
    # reboot-bootstrap retry loop exit early instead of running its
    # whole 45 s retry budget.  Matches both "=NHz" (rate enabled) and
    # "=off" (disabled) so the operator can also stop the stream
    # manually during debugging without hanging the retry loop.
    if line.startswith("[gps-live] stream"):
        _reboot_watch_note_stream_ack()

    # Structured candidate events — extracted into a typed list so the
    # UI does not need to re-parse the free-text event log.  The two
    # prefixes are disjoint: `[xing]` is the active-track path,
    # `[xing-draft]` is the dry-run validation path.
    if line.startswith("[xing]") and "candidate" in line:
        _parse_xing_candidate(line)
    elif line.startswith("[xing-draft]"):
        _parse_xing_draft_candidate(line)

    # LCD mirror — compact key=val line from display_task at ~5 Hz.
    if line.startswith("[lcd] "):
        _parse_lcd_mirror(line)

    # Firmware [draft] events — keep the browser panel in sync with
    # the on-device state machine instead of polling `track status`.
    if line.startswith("[draft]"):
        _locked_append_event(line)
        _parse_draft_event(line)


def _match_command_ack(cmd: str, line: str) -> tuple[bool, str] | None:
    cmd = cmd.strip()
    if not cmd or not line:
        return None

    # Specific ERR recognisers must run BEFORE the generic
    # "[recording] ERR:" / "[draft] ERR:" prefix fallbacks below,
    # otherwise the generic path returns a stripped raw string and
    # the specific branch never executes (it would have delivered a
    # friendlier, action-oriented message to the operator).
    if cmd == "recording stop":
        if "[recording] ERR: stop refused" in line:
            return (False,
                    "stop refused — recording still active (press Save again)")
        if "[recording] ERR: stopped but final session file was not committed" in line:
            return (False,
                    "stopped but final file not committed — check SD card")

    # Gate the generic ERR: fallbacks by command family so an
    # unrelated concurrent event can't terminate the wrong pending
    # command.  Before this gating the pattern was:
    #   caller waits for ACK of "recording start"
    #   firmware prints "[draft] ERR: no active draft" (unrelated)
    #   matcher returns (False, "no active draft") → caller sees
    #     recording-start as failed with a confusing message
    # (codex review 2026-04-22 MEDIUM).
    _DRAFT_FAMILY = {
        "mark p1", "mark p2", "track save", "track cancel",
    }
    is_draft_cmd = cmd in _DRAFT_FAMILY or cmd.startswith("track draft ")
    is_recording_cmd = cmd in {"recording start", "recording stop"}

    if is_draft_cmd and line.startswith("[draft] ERR:"):
        return (False, line.split("[draft] ERR:", 1)[1].strip())
    if is_recording_cmd and line.startswith("[recording] ERR:"):
        return (False, line.split("[recording] ERR:", 1)[1].strip())

    if cmd.startswith("track draft "):
        m = _DRAFT_STARTED_RE.search(line)
        if m:
            return (True, f"draft started: {m.group(1)}")
        return None

    if cmd == "mark p1":
        if _DRAFT_P1_RE.search(line):
            return (True, "p1 marked")
        return None

    if cmd == "mark p2":
        if _DRAFT_P2_RE.search(line):
            return (True, "p2 marked")
        return None

    if cmd == "track save":
        m = _DRAFT_SAVED_RE.search(line)
        if m:
            return (True, f"saved: {m.group(1)}")
        return None

    if cmd == "track cancel":
        if "[draft] cancelled" in line:
            return (True, "draft cancelled")
        if "[draft] (no active draft)" in line:
            return (False, "no active draft")
        return None

    if cmd == "recording start":
        m = _RECORDING_STARTED_RE.search(line)
        if m:
            return (True, f"recording started: {m.group(1)}")
        return None

    if cmd == "recording stop":
        if "[recording] stopped and saved" in line:
            return (True, "stopped and saved")
        if "[recording] (not recording" in line:
            return (False, "not recording — nothing to stop")
        # Specific ERR variants were already routed above, before the
        # generic "[recording] ERR:" prefix fallback would have
        # stripped the line.  If we reach here with a "[recording]
        # ERR:" line it means the specific matcher did not recognise
        # it; let the generic fallback catch it and surface the raw
        # firmware text rather than timing out.
        return None

    if cmd.startswith("gps stream "):
        want = cmd[len("gps stream "):].strip()
        if want in ("off", "0") and "[gps-live] stream=off" in line:
            return (True, "gps stream off")
        if f"[gps-live] stream={want}Hz" in line:
            return (True, f"gps stream {want}Hz")
        return None

    # Track picker — specific ACK shapes per subcommand.
    # Codex review 2026-04-22 Medium: the previous version accepted
    # any `[track] selected:` OR `[track] auto-detected:` for EITHER
    # command, which let a later unrelated [track] line satisfy the
    # wrong wait.  Now `track select <id>` requires a `selected:` line
    # whose id matches exactly, and `track autodetect` requires an
    # `auto-detected:` line specifically.
    if cmd.startswith("track select "):
        want_id = cmd[len("track select "):].strip()
        if "[track] ERR:" in line:
            msg = line.split("[track] ERR:", 1)[1].strip()
            return (False, msg)
        if "[track] selected:" in line:
            payload = line.split("[track] selected:", 1)[1].strip()
            # Payload looks like "track_042 (My Track)".  Require the
            # first token to match the requested id so a pending
            # select-A can't be ACKed by a later select-B emit.
            if payload.startswith(want_id + " ") or payload.startswith(want_id + "("):
                return (True, "selected: " + payload)
            # Ignore unrelated [track] selected: — keep waiting.
            return None
        return None

    if cmd == "track autodetect":
        if "[track] ERR:" in line:
            msg = line.split("[track] ERR:", 1)[1].strip()
            return (False, msg)
        if "[track] auto-detected:" in line:
            payload = line.split("[track] auto-detected:", 1)[1].strip()
            return (True, "auto-detected: " + payload)
        # A plain "[track] selected:" is from a concurrent manual
        # select, not our autodetect.  Keep waiting.
        return None

    if cmd == "tracks list":
        # End-of-list marker confirms the firmware actually processed
        # the listing request — useful as a liveness probe during
        # bootstrap without blocking on a specific entry.
        if "[tracks-list] end" in line:
            return (True, "catalog refreshed")
        return None

    return None


def _command_timeout_s(cmd: str) -> float:
    if cmd in ("mark p1", "mark p2"):
        return 8.0
    if cmd == "recording stop":
        # Long sessions (several minutes of 25 Hz fixes) produce
        # 1-2 MB VBO tmp files.  SD-card sync + rename on that size
        # can legitimately take 5-10 s on a slow / busy / fragmented
        # card.  The walking test on 2026-04-22 reproduced a 10-min
        # session hanging at save; the firmware now emits detailed
        # [stop-trace] milestones so we can tell whether the delay
        # is real SD work or a true deadlock.  Generous 15 s window
        # lets a slow-but-not-deadlocked stop complete before we
        # show the operator a red error.  If genuinely deadlocked,
        # the trace lines pinpoint the step.
        return 15.0
    if cmd == "track save":
        return 3.0
    if cmd.startswith("track select ") or cmd == "track autodetect":
        # Both paths load a track from in-memory cache, call
        # lap_timer_set_track (geometry copy, no I/O), and print the
        # ACK line.  No SD work, so a tight window is fine.  Bumped
        # above the 2.5 s default because lap_timer_task may be
        # mid-fix-processing and the version-bump + ACK emit sit
        # behind a mutex briefly.
        return 3.0
    if cmd == "tracks list":
        # Catalog emission is a tight loop over the in-memory track
        # array (max ~16 entries), each emitting one 50-100 byte
        # line over 115200-baud UART.  Even 16 entries = <5 ms of
        # wire time.  Generous 3 s covers contention.
        return 3.0
    return 2.5


def _wait_for_command_ack(cmd: str, after_seq: int,
                          timeout_s: float) -> tuple[bool, str] | None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        with state_lock:
            pending = [e for e in state["events"] if e.get("seq", 0) > after_seq]
        for ev in pending:
            ack = _match_command_ack(cmd, ev["text"])
            if ack is not None:
                return ack
        time.sleep(0.05)
    return None


def _try_open_serial() -> "serial.Serial | None":
    """Open the configured serial port or return None on failure.  Does
    NOT exit the process on failure — the reader loop retries on its
    own cadence so a USB unplug during field testing doesn't terminate
    live_map and lose the UI state.
    """
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except Exception as exc:
        _mark_serial_disconnected(str(exc))
        return None
    return ser


def serial_reader() -> None:
    # Initial open: if the port isn't there at startup we still want
    # live_map's HTTP server to come up so the operator can see the
    # error banner and fix it (wrong port, cable unplugged, etc.)
    # instead of puzzling over a process that crashed silently.
    ser = _try_open_serial()
    if ser is None:
        print(f"[live_map] cannot open {PORT} at startup — continuing "
              f"with HTTP server up, will retry in background")
    else:
        _ser_ref[0] = ser
        _mark_serial_connected()

    buf = b""
    last_reopen_attempt = 0.0
    REOPEN_INTERVAL_S = 2.0

    while True:
        # Reopen loop: triggered whenever we have no serial handle.
        # Runs at most once per REOPEN_INTERVAL_S so we don't spin
        # on a permanently-missing port.
        if _ser_ref[0] is None:
            now = time.time()
            if now - last_reopen_attempt >= REOPEN_INTERVAL_S:
                last_reopen_attempt = now
                new_ser = _try_open_serial()
                if new_ser is not None:
                    _ser_ref[0] = new_ser
                    _mark_serial_connected()
                    print(f"[live_map] serial reconnected to {PORT}")
                    # Fresh port = fresh receive buffer; drop any
                    # partial line from the previous session so we
                    # don't splice across the reconnect.
                    buf = b""
                    # Drop any mid-JSON capture state so a
                    # interrupted `cat tracks/...json` reply from
                    # the previous session doesn't permanently
                    # swallow post-reconnect lines (codex review
                    # 2026-04-22 MEDIUM: in_json/brace_depth were
                    # the only _track_discovery fields left behind
                    # on reconnect).
                    _reset_track_discovery_json_capture()
            else:
                time.sleep(0.2)
            continue

        ser = _ser_ref[0]
        try:
            data = ser.read(1024)
        except Exception as exc:
            # Errno 6 "Device not configured" (cable unplugged) or
            # any other OS-level serial failure: drop the handle,
            # update health flag, let the reopen loop above retry.
            #
            # CRITICAL: hold _serial_write_lock while we null the
            # shared handle and close the port, so any concurrent
            # _serial_write_command() sees the None transition
            # atomically.  Without this lock, a writer that already
            # passed the None check could still call .write() on a
            # half-closed file descriptor and either blow up with a
            # cryptic OSError or succeed against the next process's
            # handle if the OS reuses the fd number quickly.
            err_str = str(exc)
            print(f"[live_map] serial read error: {err_str} — will reconnect")
            _mark_serial_disconnected(err_str)
            with _serial_write_lock:
                _ser_ref[0] = None
                try:
                    ser.close()
                except Exception:
                    pass
            continue
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            nl = buf.index(b"\n")
            line = buf[:nl].decode("utf-8", errors="replace").rstrip("\r")
            buf = buf[nl + 1:]
            # Belt-and-braces: tightened regexes already reject malformed
            # numbers, but a corrupted UART frame or a new firmware log
            # format we have not seen should never kill the reader
            # thread.  Print the failure and keep going.
            try:
                if line:
                    parse_line(line)
            except Exception as exc:  # noqa: BLE001 — diagnostic catch-all
                print(f"[live_map] parse_line raised on {line!r}: {exc}")
                continue


def _bootstrap_send(cmd: str) -> None:
    """Single helper both the one-shot bootstrap path and the reboot-
    detector path use to fire a command at the firmware.  Lifted out
    of a nested scope so track_query_bootstrap() can also use it —
    codex review 2026-04-22 caught a NameError where the old nested
    send() had been deleted but a call site at the end of
    track_query_bootstrap() still referenced it.
    """
    try:
        _serial_write_command(cmd)
    except Exception as exc:
        print(f"[live_map] serial write failed: {exc}")


def _send_runtime_bootstrap_commands() -> None:
    """Emit every command we want the firmware to run when it reaches
    the serial-console runtime state: enable the 10 Hz live stream,
    list tracks, and if we already know which track is active, also
    re-request its JSON so the map geometry is always fresh after a
    reconnect or reboot.

    Track JSON request is idempotent on the firmware side — repeating
    it after a reboot is how we recover the map overlay without
    relying on the board re-emitting the [track] boot log.
    """
    # Re-arm the live stream FIRST so even if the ls/cat sequence below
    # stalls, the operator already has smooth position updates.
    _bootstrap_send(f"gps stream {LIVE_GPS_STREAM_HZ}")
    _bootstrap_send("ls tracks")
    _track_discovery["requested_ls"] = True

    # Catalog dump for the track-picker dropdown.  The firmware emits
    # one `[tracks-list] <id> "<name>"` line per track plus a
    # `[tracks-list] end` terminator; the parser keeps tracks_catalog
    # up-to-date keyed by id so a mid-session rename / save idempotently
    # updates the dropdown.  Cheaper than iterating `cat tracks/*` to
    # pull each track's JSON.
    _bootstrap_send("tracks list")

    # If we've previously discovered the active track filename (either
    # this session's ls reply or a prior cat response), re-request its
    # JSON so a reboot doesn't leave stale geometry on the UI.  First-
    # run on a cold start goes through track_query_bootstrap()'s poll
    # loop which eventually calls _bootstrap_send("cat tracks/...") on
    # its own.
    track = _track_discovery.get("first_track")
    if track:
        _bootstrap_send(f"cat tracks/{track}")


def _reboot_watch_note_stream_ack() -> None:
    """Called from parse_line() when we observe the firmware's
    `[gps-live] stream=XHz` or `=off` line.  Marks the current
    reboot's bootstrap as acknowledged so the retry loop can stop.
    """
    with _reboot_watch_lock:
        _reboot_watch["stream_ack_seen"] = True


def _kick_reboot_bootstrap() -> None:
    """Spawn a worker thread that keeps re-sending the bootstrap
    commands until the firmware ACKs the gps-stream request or we
    exhaust the retry budget.

    Why retries: firmware's main loop doesn't consume serial console
    commands until after gps_uart_init() finishes, which can take up
    to 30 s on a cold/slow-fix boot.  A single fire at T+2 s lands
    on the floor during that window.  Retrying every 3 s covers the
    whole boot spectrum; the moment the firmware echoes
    `[gps-live] stream=NHz` the reader flips stream_ack_seen and we
    exit early so we don't spam.

    Reader thread is not blocked — this always runs in a daemon
    worker.
    """
    def _run() -> None:
        # A board reboot interrupts whatever was streaming —
        # including a `cat tracks/*.json` response that the parser
        # was mid-way through.  Clear the JSON capture so the fresh
        # cat we're about to issue can be read cleanly rather than
        # being appended to a stale in-flight buffer (codex review
        # 2026-04-22 MEDIUM).
        _reset_track_discovery_json_capture()

        # Small initial settle so the first retry has a chance to
        # land if the boot is fast (fix already cached).  Longer
        # waits happen via the per-retry sleep below.
        time.sleep(1.0)
        for attempt in range(1, _REBOOT_MAX_RETRIES + 1):
            with _reboot_watch_lock:
                if _reboot_watch["stream_ack_seen"]:
                    break
            _send_runtime_bootstrap_commands()
            # Give the firmware a window to reply before deciding to
            # retry.  If the board is still in GPS init, Serial2
            # isn't being drained yet and our command sits in the
            # OS tty buffer — the next retry overwrites nothing
            # because the buffer is long enough for 3-5 queued
            # commands.
            for _ in range(int(_REBOOT_RETRY_INTERVAL_S * 10)):
                with _reboot_watch_lock:
                    if _reboot_watch["stream_ack_seen"]:
                        break
                time.sleep(0.1)
            with _reboot_watch_lock:
                if _reboot_watch["stream_ack_seen"]:
                    break
            # Keep the operator informed on slow boots so they
            # understand why the trail looks choppy for 10-30 s.
            print(f"[live_map] reboot bootstrap retry "
                  f"{attempt}/{_REBOOT_MAX_RETRIES} (firmware has not "
                  f"ACKed gps stream yet)")
        with _reboot_watch_lock:
            acked = _reboot_watch["stream_ack_seen"]
            _reboot_watch["bootstrap_in_flight"] = False
        if acked:
            print("[live_map] reboot detected — re-armed gps stream "
                  f"{LIVE_GPS_STREAM_HZ} Hz")
        else:
            print(f"[live_map] WARN: reboot bootstrap gave up after "
                  f"{_REBOOT_MAX_RETRIES} retries — firmware never "
                  f"ACKed; trail will update at 1 Hz fallback rate")

    with _reboot_watch_lock:
        _reboot_watch["last_bootstrap_ts"] = time.time()
        _reboot_watch["bootstrap_in_flight"] = True
        _reboot_watch["stream_ack_seen"] = False
    threading.Thread(target=_run, daemon=True).start()


def _maybe_handle_reboot(line: str) -> None:
    """Detect the very first line the ESP32 prints on a fresh boot and,
    if it's not still inside the debounce window, kick a re-bootstrap.
    We match on `[BOOT-EARLY] SETUP_ENTRY` because it's the only token
    guaranteed to arrive exactly once per boot (before any subsystem
    log noise).  All _reboot_watch reads/writes go through the lock
    so two banners racing through parse_line() from different threads
    (reader + future admin hooks) cannot both pass the gate.
    """
    if "[BOOT-EARLY] SETUP_ENTRY" not in line:
        return
    now = time.time()
    with _reboot_watch_lock:
        if _reboot_watch["bootstrap_in_flight"]:
            return
        if now - _reboot_watch["last_bootstrap_ts"] < _REBOOT_DEBOUNCE_S:
            return
    _kick_reboot_bootstrap()


def track_query_bootstrap() -> None:
    """Once the port is open and the board is past boot, ask the serial
    console for the first track.json so the map has geometry even if the
    script was started after boot (the boot log has already scrolled by).
    """
    # Wait for port to be ready.
    for _ in range(60):
        if _ser_ref[0] is not None:
            break
        time.sleep(0.1)
    ser = _ser_ref[0]
    if ser is None:
        return

    # Give the board a moment in case it's mid-boot.  A 4 s wait lets
    # a cold boot finish its GPS timeout and reach runtime where the
    # serial console is actually processing input.
    time.sleep(4)

    _send_runtime_bootstrap_commands()
    # Seed the reboot detector's debounce timestamp so the first live
    # boot banner after startup doesn't re-kick the bootstrap
    # immediately.  Held under _reboot_watch_lock for consistency with
    # every other write to this dict (codex follow-up review found
    # this was the only remaining unsynchronised access).
    with _reboot_watch_lock:
        _reboot_watch["last_bootstrap_ts"] = time.time()

    # Poll up to 5 s for the ls reply (reader thread populates first_track).
    for _ in range(50):
        if _track_discovery["ls_reply_seen"]:
            break
        time.sleep(0.1)

    track_name = _track_discovery["first_track"]
    if track_name is None:
        print("[live_map] no track_*.json found on SD — geometry unavailable "
              "until the board reboots and prints the [track] boot log.")
        return

    _bootstrap_send(f"cat tracks/{track_name}")
    _track_discovery["requested_cat"] = True
    print(f"[live_map] requested tracks/{track_name} over serial")


HTML = r"""<!doctype html>
<html><head>
<meta charset="utf-8"/>
<title>KartGPS Live Map</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<style>
 html,body{margin:0;height:100%;background:#0a0a0a;color:#eee;font:13px/1.4 ui-monospace,monospace}
 #map{position:absolute;inset:0}
 #info{position:absolute;top:56px;left:8px;z-index:1000;padding:8px 12px;background:rgba(0,0,0,.78);border:1px solid #444;min-width:240px;border-radius:6px}
 #events{position:absolute;bottom:8px;left:8px;right:400px;max-height:150px;overflow-y:auto;padding:6px 10px;background:rgba(0,0,0,.78);border:1px solid #444;font-size:11px;z-index:1000;border-radius:6px}
 #viewb{position:absolute;bottom:8px;right:8px;z-index:1000;padding:10px 12px;background:rgba(0,0,0,.85);border:1px solid #555;border-radius:6px;width:380px}
 #viewb h3{margin:0 0 6px 0;font-size:12px;color:#9cf;font-weight:normal}
 #viewb canvas{display:block;background:#0a0f14;border:1px solid #333;border-radius:3px}
 #viewb .legend{font-size:10px;color:#999;margin-top:4px;display:flex;gap:12px}
 #viewb .legend span.sw{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:3px;vertical-align:middle}
 /* Right column — draft + recording + LCD mirror + candidates.  Flex
    column so panels stack without magic-pixel overlap no matter how
    tall any individual panel gets.  Scrolls if the combined height
    exceeds the available viewport (minus space reserved for View B
    along the bottom). */
 #right-col{position:absolute;top:56px;right:8px;z-index:1000;width:280px;display:flex;flex-direction:column;gap:8px;max-height:calc(100vh - 320px);overflow-y:auto}
 #cands{padding:8px 10px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px;max-height:260px;overflow-y:auto;font-size:11px}
 /* GPS receiver diagnostics panel — surfaces the [gps-ubx],
    [gps-const], and [gps-verify] streams in a stable layout so the
    operator can see at a glance whether the CFG writes stuck and
    which constellations are delivering sats.  Each section has a
    muted label row and a value row; status colour (green=ok,
    orange=partial, red=bad) is driven by state.gps_diag in JS. */
 #gps-diag-panel{padding:8px 10px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px;font-size:11px}
 #gps-diag-panel h3{margin:0 0 6px 0;font-size:13px;color:#fc5;font-weight:normal}
 #gps-diag-panel .diag-section{margin-top:6px}
 #gps-diag-panel .diag-label{display:block;color:#888;font-size:10px;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:2px}
 #gps-diag-panel .diag-ok{color:#5f5}
 #gps-diag-panel .diag-warn{color:#fc5}
 #gps-diag-panel .diag-err{color:#f55}
 #gps-diag-panel .diag-muted{color:#888}
 #gps-diag-panel .diag-const-row{display:flex;justify-content:space-between;padding:1px 0}
 #gps-diag-panel .diag-const-name{color:#aaa}
 #gps-diag-panel .diag-const-zero{color:#f55}
 #gps-diag-panel .diag-const-has{color:#9f9}
 #cands h3{margin:0 0 6px 0;font-size:12px;color:#9cf;font-weight:normal}
 #cands .row{margin:2px 0;padding:2px 4px;border-left:3px solid #444}
 #cands .pass{border-left-color:#5f5;color:#cfc}
 #cands .reject{border-left-color:#f55;color:#fcc}
 /* Orange tint for draft (dry-run) candidate rows, so the operator can
    tell at a glance whether a PASS is against the active track or
    against the draft line they are creating. */
 #cands .row.draft{background:rgba(255,170,0,0.08)}
 #cands .row.draft.pass{border-left-color:#fa3}
 #cands .row.draft.reject{border-left-color:#f73}
 /* Stale — firmware rejected the write because the session was
    replaced/cleared mid-evaluation.  Dim the row so an operator can
    tell at a glance this PASS did NOT count toward the save gate. */
 #cands .row.stale{opacity:0.45;font-style:italic}
 #cands .stale-tag{color:#f80;font-weight:bold}
 #cands .reason{color:#aaa;font-size:10px}
 #cands .pass{color:#cfc}
 #cands .dv-summary{margin:2px 0 6px 0;padding:4px 6px;background:rgba(255,170,0,0.12);border:1px solid rgba(255,170,0,0.3);border-radius:3px;font-size:11px}
 #draft{padding:10px 12px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px}
 /* Recording panel — compact, drives `recording start/stop` serial
    commands via POST /command. */
 #rec-panel{padding:8px 10px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px}
 #rec-panel h3{margin:0 0 6px 0;font-size:12px;color:#9cf;font-weight:normal}
 #rec-status{padding:4px 6px;margin-bottom:6px;background:#111;border-left:3px solid #666;font-size:12px}
 #rec-status.recording{border-left-color:#f33;color:#fcc}
 #rec-status.idle{border-left-color:#666;color:#999}
 #rec-panel button{margin:2px 4px 0 0;padding:5px 10px;border:1px solid #666;background:#222;color:#eee;font:inherit;font-size:12px;border-radius:3px;cursor:pointer}
 #rec-panel button:hover:not([disabled]){background:#2a2a2a}
 #rec-panel button[disabled]{opacity:0.4;cursor:not-allowed}
 #rec-panel button.primary{background:#253;border-color:#385;color:#cfc}
 #rec-panel button.danger{background:#422;border-color:#855;color:#fcc}
 #rec-msg{margin-top:4px;font-size:11px;min-height:14px}
 #rec-msg.ok{color:#7f7}
 #rec-msg.err{color:#f77}
 /* Track picker — dropdown + auto-detect button.  Driven by the
    firmware's `tracks list` catalog (cached in state.tracks_catalog).
    Disabled while recording because the firmware blocks track switch
    during a session to protect [laptiming] integrity. */
 #track-picker{padding:8px 10px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px}
 #track-picker h3{margin:0 0 6px 0;font-size:12px;color:#9cf;font-weight:normal}
 #track-picker select{width:100%;box-sizing:border-box;padding:5px 7px;background:#111;color:#eee;border:1px solid #555;border-radius:3px;font-family:inherit;font-size:12px}
 #track-picker select:disabled{opacity:0.5;cursor:not-allowed}
 #track-picker .row{display:flex;gap:6px;margin-top:6px}
 #track-picker button{flex:1;padding:5px 8px;border:1px solid #666;background:#222;color:#eee;font:inherit;font-size:12px;border-radius:3px;cursor:pointer}
 #track-picker button:hover:not([disabled]){background:#2a2a2a}
 #track-picker button[disabled]{opacity:0.4;cursor:not-allowed}
 #track-picker-status{margin-top:4px;font-size:11px;min-height:14px;color:#9cf}
 #track-picker-status.err{color:#f77}
 #track-picker-status.ok{color:#7f7}
 /* LCD mirror panel — canvas approximates the on-device TFT layout. */
 #lcd-panel{padding:8px 10px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px}
 #lcd-panel h3{margin:0 0 6px 0;font-size:12px;color:#9cf;font-weight:normal}
 #lcd-panel canvas{display:block;background:#000;border:1px solid #333;border-radius:3px;width:260px;height:260px}
 #lcd-meta{margin-top:4px;font-size:10px;color:#888}
 #draft h3{margin:0 0 8px 0;font-size:13px;color:#fc5;font-weight:normal}
 #draft label{display:block;font-size:11px;color:#aaa;margin-top:6px}
 #draft input[type=text]{width:100%;box-sizing:border-box;padding:5px 7px;background:#111;color:#eee;border:1px solid #555;border-radius:3px;font-family:inherit;font-size:12px}
 #draft button{margin:4px 4px 0 0;padding:6px 10px;border:1px solid #666;background:#222;color:#eee;font-family:inherit;font-size:12px;border-radius:3px;cursor:pointer}
 #draft button:hover:not([disabled]){background:#2a2a2a;border-color:#999}
 #draft button[disabled]{opacity:.4;cursor:not-allowed}
 #draft button.primary{background:#253;border-color:#385;color:#cfc}
 #draft button.primary:hover:not([disabled]){background:#284}
 #draft button.danger{background:#422;border-color:#855;color:#fcc}
 #draft-status{margin-top:8px;padding:6px 8px;background:#111;border-left:3px solid #fc5;font-size:11px;min-height:14px}
 #draft-markers{display:flex;gap:14px;font-size:11px;margin-top:4px}
 #draft-markers .dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#444;margin-right:4px;vertical-align:middle}
 #draft-markers .on .dot{background:#5f5}
 /* Inline hint beneath Save — tells operator WHY save is disabled
    (need more walks across the draft line) or confirms it's ready. */
 #draft-validate-hint{margin-top:6px;padding:4px 6px;font-size:11px;background:#111;border-left:3px solid #555;color:#aaa;min-height:14px}
 #draft-validate-hint.ready{border-left-color:#5f5;color:#9f9}
 #draft-validate-hint.blocked{border-left-color:#fc5;color:#fcc}
 #draft-validate-hint.err{border-left-color:#f55;color:#f99}
 #events .lap{color:#5f5}
 #events .xing{color:#fc5}
 #events .session{color:#ff5}
 .metric{color:#aaa}
 .bold{color:#fff;font-weight:bold}
 .leaflet-container{background:#0a0a0a}
 /* Serial-disconnect banner.  Sits across the top of the window at
    a higher z-index than everything else so the operator can't miss
    it — stale state is actively dangerous (looks like a live feed
    but commands silently fail). */
 #serial-banner{position:absolute;top:0;left:0;right:0;z-index:2000;
   padding:8px 14px;background:#5a1818;color:#fff;
   font:bold 13px ui-monospace,monospace;border-bottom:2px solid #f44;
   display:none}
 #serial-banner.on{display:block}
 #serial-banner .detail{font-weight:normal;color:#fcc;margin-left:12px}
</style></head><body>
<div id="map"></div>
<div id="serial-banner"></div>
<div id="info">Waiting for serial data…</div>
<div id="right-col">
<div id="draft">
  <h3>Mark Start/Finish</h3>
  <label>Track name</label>
  <input id="draft-name" type="text" placeholder="e.g. home_west" autocomplete="off"/>
  <div><button id="btn-new" class="primary">Start Draft</button></div>
  <div id="draft-markers">
    <span id="p1-ind"><span class="dot"></span>P1</span>
    <span id="p2-ind"><span class="dot"></span>P2</span>
  </div>
  <div>
    <button id="btn-mark-p1" disabled>Mark P1</button>
    <button id="btn-mark-p2" disabled>Mark P2</button>
  </div>
  <div>
    <button id="btn-save" class="primary" disabled>Save</button>
    <button id="btn-cancel" class="danger" disabled>Cancel</button>
  </div>
  <div id="draft-validate-hint">walk across the line to enable Save</div>
  <div id="draft-status">idle — press Start Draft</div>
</div>
<div id="track-picker">
  <h3>Active Track</h3>
  <select id="track-select" disabled>
    <option value="">loading...</option>
  </select>
  <div class="row">
    <button id="btn-track-autodetect" disabled title="Pick the nearest track by GPS proximity">Auto-detect by proximity</button>
  </div>
  <div id="track-picker-status"></div>
</div>
<div id="rec-panel">
  <h3>Recording</h3>
  <div id="rec-status">—</div>
  <div>
    <button id="btn-rec-start" class="primary">Start Recording</button>
    <button id="btn-rec-stop" class="danger">Save Recording</button>
  </div>
  <div id="rec-msg" class="helper"></div>
</div>
<div id="lcd-panel">
  <h3>On-Device LCD (mirror)</h3>
  <canvas id="lcd-canvas" width="260" height="260"></canvas>
  <div id="lcd-meta">waiting for firmware...</div>
</div>
<div id="cands">
  <h3>Crossing Candidates</h3>
  <div id="cand-list">(none yet — walk toward the line)</div>
</div>
<div id="gps-diag-panel">
  <h3>GPS Receiver Diagnostics</h3>
  <div class="diag-section"><span class="diag-label">CFG ACK/NAK</span>
    <div id="gps-diag-ubx">(waiting for boot log — power-cycle the board to see CFG writes)</div>
  </div>
  <div class="diag-section"><span class="diag-label">Active constellations</span>
    <div id="gps-diag-verify">(no [gps-verify] line yet)</div>
  </div>
  <div class="diag-section"><span class="diag-label">Satellites per constellation</span>
    <div id="gps-diag-const">(no [gps-const] line yet — needs GSV enabled + open sky)</div>
  </div>
</div>
</div><!-- /right-col -->
<div id="viewb">
  <h3>Finish-Line Relative View (View B)</h3>
  <canvas id="viewb-canvas" width="356" height="180"></canvas>
  <div class="legend">
    <span><span class="sw" style="background:#1f4"></span>segment u∈[0,1]</span>
    <span><span class="sw" style="background:#552"></span>extension</span>
    <span><span class="sw" style="background:#222;border:1px solid #555"></span>outside</span>
  </div>
</div>
<div id="events"></div>
<script>
const map=L.map('map',{zoomControl:true,attributionControl:true}).setView([0,0],2);
const osm=L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{
  maxZoom:23,maxNativeZoom:19,attribution:'© OpenStreetMap',
});
const sat=L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',{
  maxZoom:23,maxNativeZoom:19,attribution:'Esri World Imagery',
});
sat.addTo(map);
L.control.layers({Satellite:sat,Street:osm},null,{position:'topright'}).addTo(map);

const END_TOL_M=2.0;
// Minimum accepted draft-validation crossings the firmware requires
// before `track save` will persist a new track.  Must match
// lap_timer_draft_validation_min_accepted() on the firmware side —
// walking-test = 1, production = 2.  live_map is the walking-test
// companion tool (laptop connected over USB), so 1 is the right
// default.  The Save button is gated on this so operators see
// "walk across once more" BEFORE clicking rather than the silent
// firmware 409 they used to get.
const MIN_ACCEPTED_CROSSINGS=1;

let lineLayer=null, extLayer=null, headingLayer=null;
let trailLayer=L.polyline([],{color:'#59f',weight:2,opacity:0.85}).addTo(map);
let currentLayer=null;
let draftLineLayer=null, draftP1Layer=null, draftP2Layer=null;
let draftP1CircleLayer=null, draftP2CircleLayer=null;
let fittedOnce=false;

const info=document.getElementById('info');
const events=document.getElementById('events');

async function poll(){
  try{
    const r=await fetch('/state');
    const state=await r.json();
    update(state);
  }catch(e){/*ignore*/}
  setTimeout(poll,100);
}

function renderLine(line){
  if(!(line&&line.p1&&line.p2))return;
  const p1=line.p1, p2=line.p2;
  if(lineLayer){lineLayer.setLatLngs([p1,p2]);}
  else{
    lineLayer=L.polyline([p1,p2],{color:'#f33',weight:5,opacity:0.95}).addTo(map);
    L.circleMarker(p1,{radius:5,color:'#f33',fillColor:'#f33',fillOpacity:1})
      .bindTooltip('P1',{permanent:true,direction:'top',offset:[0,-6]}).addTo(map);
    L.circleMarker(p2,{radius:5,color:'#f33',fillColor:'#f33',fillOpacity:1})
      .bindTooltip('P2',{permanent:true,direction:'top',offset:[0,-6]}).addTo(map);
  }
  // extension
  const dLat=p2[0]-p1[0], dLon=p2[1]-p1[1];
  const lineLenM=metersBetween(p1,p2);
  const frac=lineLenM>0?Math.min(END_TOL_M/lineLenM,1.0):0;
  const e1=[p1[0]-dLat*frac,p1[1]-dLon*frac];
  const e2=[p2[0]+dLat*frac,p2[1]+dLon*frac];
  if(extLayer){extLayer.setLatLngs([e1,e2]);}
  else{
    extLayer=L.polyline([e1,e2],{color:'#ff0',weight:3,opacity:0.75,dashArray:'8,6'}).addTo(map);
  }
  // heading arrow
  if(typeof line.heading==='number'){
    const midLat=(p1[0]+p2[0])/2, midLon=(p1[1]+p2[1])/2;
    const rad=line.heading*Math.PI/180;
    const arrowM=Math.max(3,lineLenM*0.5);
    // rough projection: 1 deg lat ≈ 111 km, 1 deg lon ≈ 111km * cos(lat)
    const dyDeg=Math.cos(rad)*arrowM/111000;
    const dxDeg=Math.sin(rad)*arrowM/(111000*Math.cos(midLat*Math.PI/180));
    const tip=[midLat+dyDeg,midLon+dxDeg];
    if(headingLayer){headingLayer.setLatLngs([[midLat,midLon],tip]);}
    else{
      headingLayer=L.polyline([[midLat,midLon],tip],{color:'#5f5',weight:3,opacity:0.9}).addTo(map);
      L.circleMarker(tip,{radius:4,color:'#5f5',fillColor:'#5f5',fillOpacity:1}).addTo(map);
    }
  }
}

function metersBetween(a,b){
  const R=6371000;
  const la1=a[0]*Math.PI/180, la2=b[0]*Math.PI/180;
  const dla=(b[0]-a[0])*Math.PI/180, dlo=(b[1]-a[1])*Math.PI/180;
  const h=Math.sin(dla/2)**2+Math.cos(la1)*Math.cos(la2)*Math.sin(dlo/2)**2;
  return R*2*Math.atan2(Math.sqrt(h),Math.sqrt(1-h));
}

function fitInitial(line,cur){
  const pts=[];
  if(line&&line.p1)pts.push(line.p1);
  if(line&&line.p2)pts.push(line.p2);
  if(cur)pts.push(cur);
  if(pts.length===0)return false;
  if(pts.length===1)map.setView(pts[0],21);
  else map.fitBounds(L.latLngBounds(pts).pad(0.6),{maxZoom:21});
  return true;
}

function renderSerialBanner(serial){
  // Stale-state is the worst failure mode — the page looks alive but
  // every control silently no-ops.  Flip a high-z banner across the
  // top of the window the moment the reader reports disconnected,
  // clear it when reconnect succeeds.
  const el=document.getElementById('serial-banner');
  if(!el)return;
  if(serial && serial.connected===false){
    const err=serial.last_error ? ` (${serial.last_error})` : '';
    el.innerHTML='⚠ USB serial disconnected — plug the board back in.'
      + '<span class="detail">Save / Start / Save-track commands '
      + 'will fail until the reader re-opens the port. The state '
      + 'shown below is stale from the last connected moment'+err+'.</span>';
    el.classList.add('on');
  } else {
    el.classList.remove('on');
    el.innerHTML='';
  }
}

// Render the GPS receiver diagnostics panel from state.gps_diag.
// Three sections:
//   - CFG ACK/NAK rollup: count of ACKs + most recent NAK (if any).
//   - Active constellations from [gps-verify] readback.
//   - Per-constellation satellite counts from [gps-const].
function renderGpsDiag(diag){
  if(!diag)return;
  const ubxEl=document.getElementById('gps-diag-ubx');
  if(ubxEl){
    const evs=diag.ubx_events||[];
    if(evs.length===0){
      ubxEl.innerHTML='<span class="diag-muted">(waiting for boot log — power-cycle the board to see CFG writes)</span>';
    } else {
      const naks=evs.filter(e=>e.kind==='NAK');
      const acks=evs.filter(e=>e.kind==='ACK');
      let html='<span class="diag-ok">'+acks.length+' ACK</span>';
      if(naks.length>0){
        html+=' · <span class="diag-err">'+naks.length+' NAK</span>';
        const last=naks[naks.length-1];
        html+=' <span class="diag-err">(last: '+last.name+')</span>';
      }
      html+='<br><span class="diag-muted">last: '+evs[evs.length-1].kind+' '+evs[evs.length-1].name+'</span>';
      ubxEl.innerHTML=html;
    }
  }
  const verifyEl=document.getElementById('gps-diag-verify');
  if(verifyEl){
    const v=diag.verify;
    if(!v){
      verifyEl.innerHTML='<span class="diag-muted">(no [gps-verify] line yet)</span>';
    } else {
      const names=[['GPS','gps'],['SBAS','sbas'],['GAL','galileo'],
                   ['BDS','beidou'],['QZS','qzss'],['GLO','glonass']];
      let parts=[];
      let missingCritical=false;
      for(const [label,key] of names){
        const on=!!v[key];
        // Critical = Galileo + BeiDou: if these are off after a SET
        // there's a real config problem that must be surfaced.
        const crit=(key==='galileo'||key==='beidou');
        if(crit && !on)missingCritical=true;
        const cls=on?'diag-ok':(crit?'diag-err':'diag-muted');
        parts.push('<span class="'+cls+'">'+label+(on?'✓':'✗')+'</span>');
      }
      let html=parts.join(' ');
      if(missingCritical){
        html+='<br><span class="diag-err">⚠ Galileo/BeiDou NOT active — sats/HDOP will not improve</span>';
      }
      verifyEl.innerHTML=html;
    }
  }
  const constEl=document.getElementById('gps-diag-const');
  if(constEl){
    const c=diag.constellations;
    if(!c){
      constEl.innerHTML='<span class="diag-muted">(no [gps-const] line yet — needs GSV enabled + open sky)</span>';
    } else {
      const names=[['GPS','gps'],['GAL','galileo'],['BDS','beidou'],
                   ['GLO','glonass'],['QZS','qzss']];
      let rows=[];
      for(const [label,key] of names){
        const n=c[key]||0;
        const cls=(n===0)?'diag-const-zero':'diag-const-has';
        rows.push('<div class="diag-const-row"><span class="diag-const-name">'+label+'</span><span class="'+cls+'">'+n+' sats</span></div>');
      }
      constEl.innerHTML=rows.join('');
    }
  }
}

// Track-picker state: remember whether we've been recording so we
// can auto-restore the dropdown's enabled state when it stops.
let lastTrackPickerSignature = "";

function renderTrackPicker(state){
  const sel = document.getElementById('track-select');
  const btnAuto = document.getElementById('btn-track-autodetect');
  if(!sel || !btnAuto) return;
  const catalog = state.tracks_catalog || [];
  const activeId = state.active_track_id || "";
  const isRecording = !!(state.lcd && state.lcd.rec);

  // Rebuild <option>s only when catalog or active id changes.
  // Avoids clobbering an in-progress user selection on every poll.
  const sig = JSON.stringify({c: catalog, a: activeId});
  if(sig !== lastTrackPickerSignature){
    lastTrackPickerSignature = sig;
    if(catalog.length === 0){
      sel.innerHTML = '<option value="">(no tracks on SD)</option>';
    } else {
      // Always lead with a disabled placeholder selected by default
      // when we don't yet know the active track — or the active id
      // points to a track not in our catalog (e.g. selected via the
      // phone app before live_map connected).  Without this the
      // browser would default visually to the first <option> while
      // the firmware has a different (or no) active track; the
      // operator sees a dropdown showing "Track A" but the map
      // still reflects Track B.  Codex review 2026-04-22 Medium.
      const activeInCatalog = !!catalog.find(t => t.id === activeId);
      const showPlaceholder = !activeId || !activeInCatalog;
      const placeholderLabel = !activeId
        ? '(no active track — pick one)'
        : '(active: ' + activeId + ' — not in catalog)';
      const placeholderHtml = showPlaceholder
        ? '<option value="" disabled selected>' + placeholderLabel + '</option>'
        : '';
      const parts = catalog.map(t => {
        const selected = (t.id === activeId && activeInCatalog) ? ' selected' : '';
        // Text content is safe (set via textContent-equivalent by
        // building innerHTML with our own escape).  Names were
        // sanitised firmware-side; double-guard here.
        const safeName = (t.name||'').replace(/[<>&"]/g, '?');
        const safeId = (t.id||'').replace(/[<>&"]/g, '?');
        return `<option value="${safeId}"${selected}>${safeName} (${safeId})</option>`;
      });
      sel.innerHTML = placeholderHtml + parts.join('');
    }
  }

  // Lock the picker while a session is active — firmware blocks the
  // switch anyway with a 409-style [track] ERR, but disabling here
  // gives immediate feedback and avoids a round-trip.
  sel.disabled = isRecording || catalog.length === 0;
  btnAuto.disabled = isRecording;
  const statusEl = document.getElementById('track-picker-status');
  if(statusEl){
    if(isRecording){
      statusEl.className = 'err';
      statusEl.textContent = 'locked during recording — Save Recording first to switch';
    } else if(catalog.length === 0){
      statusEl.className = '';
      statusEl.textContent = 'waiting for tracks list from firmware...';
    } else if(activeId){
      const entry = catalog.find(t => t.id === activeId);
      statusEl.className = 'ok';
      statusEl.textContent = 'active: ' + (entry ? (entry.name + ' (' + activeId + ')') : activeId);
    } else {
      statusEl.className = '';
      statusEl.textContent = 'no track selected';
    }
  }
}

function update(state){
  renderSerialBanner(state.serial);
  renderGpsDiag(state.gps_diag);
  renderTrackPicker(state);
  const line=state.line||{};
  const cur=state.current;
  const trail=state.trail||[];

  renderLine(line);
  trailLayer.setLatLngs(trail);

  if(cur){
    if(currentLayer){currentLayer.setLatLng(cur);}
    else{
      currentLayer=L.circleMarker(cur,{radius:9,color:'#fff',weight:2,fillColor:'#5f5',fillOpacity:1}).addTo(map);
    }
  }

  if(!fittedOnce){
    if(fitInitial(line,cur))fittedOnce=true;
  }

  // info panel
  const lat=cur?cur[0].toFixed(7):'—';
  const lon=cur?cur[1].toFixed(7):'—';
  const fix=state.fix_3d?'<span class="bold" style="color:#5f5">3D✓</span>':'<span style="color:#f55">no</span>';
  const tierNames=['Poor','Fair','Good','Excellent'];
  const tname=tierNames[state.quality_tier]||'?';
  const hdop=(state.hdop===-1||state.hdop==null)?'—':state.hdop.toFixed(1);
  let distStr='—';
  if(cur&&line.p1&&line.p2){
    const mid=[(line.p1[0]+line.p2[0])/2,(line.p1[1]+line.p2[1])/2];
    distStr=metersBetween(cur,mid).toFixed(1)+' m';
  }
  const lineLen=line.p1&&line.p2?metersBetween(line.p1,line.p2).toFixed(2)+' m':'—';
  info.innerHTML=`
    <div class="bold" style="font-size:14px;margin-bottom:4px">KartGPS Live</div>
    <span class="bold">lat</span> ${lat}<br>
    <span class="bold">lon</span> ${lon}<br>
    <span class="metric">sats</span> ${state.sats||0} · ${fix}<br>
    <span class="metric">quality</span> ${state.quality_score||0} <span class="metric">(${tname})</span> · <span class="metric">hdop</span> ${hdop}<br>
    <span class="metric">line</span> ${lineLen} · <span class="metric">dist</span> ${distStr}<br>
    <span class="metric">trail</span> ${trail.length} pts
  `;

  // events
  const now=Date.now()/1000;
  const evs=(state.events||[]).slice(-20).reverse();
  events.innerHTML=evs.map(e=>{
    let cls='';
    if(e.text.includes('[lap]'))cls='lap';
    else if(e.text.includes('[xing]'))cls='xing';
    else if(e.text.includes('[session]'))cls='session';
    const ago=Math.max(0,Math.round(now-e.t));
    return `<div><span style="color:#666">[${ago}s]</span> <span class="${cls}">${e.text.replace(/</g,'&lt;')}</span></div>`;
  }).join('');

  // draft (Save button gating reads the live draft_validation counters)
  renderDraft(state.draft||{}, state.draft_validation||{});

  // crossing candidates list + View B
  renderCandidates(state.candidates||[], state.draft_validation||{});
  renderViewB(line, trail, cur, state.candidates||[]);

  // LCD mock + recording panel
  renderRecPanel(state.lcd);
  renderLcdMirror(state.lcd);
}

// Formats "m:ss.xx" from ms, matching firmware display_format.cpp.
function fmtLapMs(ms){
  if(ms<=0)return "0:00.00";
  var total=Math.floor(ms);
  var min=Math.floor(total/60000);
  var sec=(total%60000)/1000;
  return min+":"+(sec<10?"0":"")+sec.toFixed(2);
}

function fmtDeltaMs(ms, valid){
  if(!valid)return "--.--";
  var s=(ms/1000);
  var sign=ms>=0?"+":"-";
  return sign+Math.abs(s).toFixed(2);
}

// Renders the firmware's current driving screen onto a 260x260 canvas
// at roughly the same layout as the real TFT.  The data is the
// [lcd] k=v stream decoded by _parse_lcd_mirror in the Python server.
function renderLcdMirror(lcd){
  var canvas=document.getElementById('lcd-canvas');
  var meta=document.getElementById('lcd-meta');
  if(!canvas)return;
  var ctx=canvas.getContext('2d');
  var W=canvas.width, H=canvas.height;

  // Background matches firmware delta_background_colour().
  var bgMap={black:'#000', green:'#063618', red:'#3a0a0a', yellow:'#3a3208'};
  var bg = (lcd && bgMap[lcd.bg]) || '#000';
  ctx.fillStyle=bg;
  ctx.fillRect(0,0,W,H);

  if(!lcd){
    ctx.fillStyle='#888';
    ctx.font='12px ui-monospace, monospace';
    ctx.textAlign='center';
    ctx.fillText('waiting for firmware...', W/2, H/2);
    meta && (meta.textContent='no [lcd] frames yet');
    return;
  }

  // Top bar — lap label / track name / sats badge
  ctx.fillStyle='rgba(255,255,255,0.08)';
  ctx.fillRect(0,0,W,30);
  ctx.fillStyle='#fff';
  ctx.font='bold 14px ui-monospace, monospace';
  ctx.textAlign='left';
  ctx.fillText(lcd.rec?('L'+lcd.lap):'---', 8, 22);
  // Center top: track name
  ctx.textAlign='center';
  ctx.fillStyle='#ccc';
  ctx.font='13px ui-monospace, monospace';
  var trackTxt=lcd.track||'No Track';
  if(trackTxt.length>14)trackTxt=trackTxt.slice(0,14);
  ctx.fillText(trackTxt, W/2, 21);
  // Right top: sats
  ctx.textAlign='right';
  ctx.fillStyle=lcd.fix3d?'#7f7':'#f77';
  ctx.font='bold 14px ui-monospace, monospace';
  ctx.fillText(lcd.sats+'sat'+(lcd.fix3d?'✓':'·'), W-8, 22);

  // Delta area content varies by driving state.
  ctx.textAlign='center';
  if(lcd.state==='IDLE' || !lcd.rec){
    // Big sats number + track hint
    ctx.font='bold 54px ui-monospace, monospace';
    ctx.fillStyle='#fff';
    ctx.fillText(String(lcd.sats), W/2, 100);
    ctx.font='14px ui-monospace, monospace';
    ctx.fillStyle='#9cf';
    ctx.fillText('SATS', W/2, 124);
    ctx.font='13px ui-monospace, monospace';
    ctx.fillStyle='#ccc';
    ctx.fillText(trackTxt, W/2, 148);
  } else if(lcd.state==='OUT_LAP'){
    // Show speed until first crossing.
    ctx.font='bold 54px ui-monospace, monospace';
    ctx.fillStyle='#fff';
    ctx.fillText(lcd.speed.toFixed(1), W/2, 110);
    ctx.font='14px ui-monospace, monospace';
    ctx.fillStyle='#9cf';
    ctx.fillText('km/h — out lap', W/2, 134);
  } else {
    // NORMAL: big delta in the middle.
    ctx.font='bold 72px ui-monospace, monospace';
    ctx.fillStyle='#fff';
    var dtxt = lcd.delta_valid ? fmtDeltaMs(lcd.delta_ms, true) : '--.--';
    ctx.fillText(dtxt, W/2, 130);
    ctx.font='13px ui-monospace, monospace';
    ctx.fillStyle='#ccc';
    ctx.fillText('vs best', W/2, 152);
  }

  // Bottom area — current lap time, speed, best lap
  var bottomY = H - 70;
  ctx.fillStyle='rgba(0,0,0,0.35)';
  ctx.fillRect(0, bottomY, W, 70);
  ctx.textAlign='left';
  ctx.font='11px ui-monospace, monospace';
  ctx.fillStyle='#9cf';
  ctx.fillText('CUR', 12, bottomY+16);
  ctx.fillStyle='#fff';
  ctx.font='bold 18px ui-monospace, monospace';
  ctx.fillText(fmtLapMs(lcd.cur_ms), 12, bottomY+38);

  ctx.font='11px ui-monospace, monospace';
  ctx.fillStyle='#9cf';
  ctx.textAlign='right';
  ctx.fillText('BEST', W-12, bottomY+16);
  ctx.fillStyle='#fff';
  ctx.font='bold 18px ui-monospace, monospace';
  ctx.fillText(lcd.best_ms>0?fmtLapMs(lcd.best_ms):'--:--', W-12, bottomY+38);

  ctx.textAlign='center';
  ctx.font='11px ui-monospace, monospace';
  ctx.fillStyle='#aaa';
  ctx.fillText(lcd.speed.toFixed(1)+' km/h', W/2, bottomY+60);

  if(lcd.off){
    ctx.fillStyle='#fc0';
    ctx.font='bold 12px ui-monospace, monospace';
    ctx.textAlign='center';
    ctx.fillText('OFF-TRACK', W/2, 170);
  }

  // Meta line
  if(meta){
    var age = Math.max(0, Math.round(Date.now()/1000 - (lcd.t||0)));
    meta.textContent = lcd.screen+' / '+lcd.state+' · '+age+'s ago';
  }
}

function renderRecPanel(lcd){
  var stat=document.getElementById('rec-status');
  var btnStart=document.getElementById('btn-rec-start');
  var btnStop=document.getElementById('btn-rec-stop');
  if(!stat || !btnStart || !btnStop)return;
  var recording = !!(lcd && lcd.rec);
  if(recording){
    stat.className='recording';
    stat.textContent='● RECORDING — lap '+lcd.lap+' · '+fmtLapMs(lcd.cur_ms);
  }else{
    stat.className='idle';
    stat.textContent='idle — press Start to record';
  }
  btnStart.disabled=recording;
  btnStop.disabled=!recording;
}

// ---- Finish-line-local projection (client-side, pure math). ----
//
// Returns [u, signed_d_m] where:
//   u = 0 at P1, 1 at P2, outside [0,1] means the extension.
//   signed_d_m = perpendicular metres from the infinite line through P1-P2.
// Uses a flat-earth projection with the line midpoint as the latitude
// reference, fine for detection-line-scale distances.
//
// Axis order: [lat_metres, lon_metres] — matches the firmware's
// line_geometry::project_to_line() convention, which is fed
// (lat, lon) directly in lap_timer_crossing.cpp emit_candidate_event().
// Previously this function swapped to [lon_metres, lat_metres] which
// flipped the sign of the cross-product and made "above the line" in
// View B opposite to the firmware's signed_d sign.  Codex P2 from
// 2026-04-19.
function projectToLineM(pll, p1, p2){
  const latRef=(p1[0]+p2[0])/2;
  const cosLat=Math.cos(latRef*Math.PI/180);
  const toM=(ll)=>[(ll[0]-p1[0])*111000, (ll[1]-p1[1])*111000*cosLat];
  const P=toM(pll), D=toM(p2);
  const cdx=D[0], cdy=D[1];
  const len2=cdx*cdx+cdy*cdy;
  if(len2<=0) return [0,0];
  const cpx=P[0], cpy=P[1];
  const u=(cpx*cdx+cpy*cdy)/len2;
  const cdLen=Math.sqrt(len2);
  const signedDm=(cdx*cpy-cdy*cpx)/cdLen;
  return [u, signedDm];
}

function renderCandidates(cands, dv){
  const el=document.getElementById('cand-list');
  // Draft-validation summary line, shown above the list when either
  // counter is non-zero.  dv comes from state.draft_validation which
  // is maintained by the live_map parser (Phase A) and, once Phase B
  // ships, mirrors the firmware's own counters via /api/tracks/draft_validation.
  let summary='';
  if(dv && (dv.accepted || dv.rejected)){
    summary=`<div class="dv-summary">Draft: <span class="pass">✓ ${dv.accepted} accepted</span> · <span class="reject">✗ ${dv.rejected} rejected</span></div>`;
  }
  if(!cands||cands.length===0){
    el.innerHTML=summary+'<div style="color:#888">(none yet — walk toward the line)</div>';
    return;
  }
  const now=Date.now()/1000;
  // Newest first, capped at 12 rows.
  const rows=cands.slice(-12).reverse().map(c=>{
    const ago=Math.max(0,Math.round(now-c.t));
    // Draft rows get an extra class so CSS can tint them orange, to
    // separate dry-run candidates (draft line) from real lap candidates
    // (active track).  Without this tint the operator can't tell at a
    // glance which line a PASS belongs to when both are firing.
    const passReject=c.result==='PASS'?'pass':'reject';
    const typeCls=c.line_type==='draft'?' draft':'';
    // Stale rows are firmware-side session-race losers — the event
    // was evaluated against a session that had already been cleared /
    // replaced by the time the counter write landed, so the firmware
    // dropped it.  Dim the row and append "(stale)" so the operator
    // doesn't treat it as evidence of a passing validation.  Codex
    // P2 from 2026-04-20 round-4.
    const staleCls=c.stale?' stale':'';
    const cls=passReject+typeCls+staleCls;
    const uStr=c.u.toFixed(3);
    const ovStr=c.overshoot.toFixed(2);
    // One decimal so the HEADING_WINDOW boundary (60° by default) is
    // readable — 60.3 vs 59.8 must not both render as '+60'.
    const hdStr=(c.hdiff>=0?'+':'')+c.hdiff.toFixed(1);
    const label=c.line_type==='draft'?'DRAFT':`L${c.line_idx}`;
    const staleTag=c.stale?' <span class="stale-tag">(stale)</span>':'';
    return `<div class="row ${cls}">`+
      `[${ago}s] ${label} ${c.result} `+
      `<span class="reason">u=${uStr} over=${ovStr}m hd=${hdStr}° `+
      `${c.reason}</span>${staleTag}</div>`;
  });
  el.innerHTML=summary+rows.join('');
}

function renderViewB(line, trail, cur, cands){
  const canvas=document.getElementById('viewb-canvas');
  const ctx=canvas.getContext('2d');
  const W=canvas.width, H=canvas.height;
  ctx.clearRect(0,0,W,H);
  if(!(line&&line.p1&&line.p2)){
    ctx.fillStyle='#666';
    ctx.font='11px monospace';
    ctx.fillText('waiting for line geometry...', 10, H/2);
    return;
  }

  // X axis: u ∈ [-0.5, 1.5]; Y axis: signed_d ∈ [-3 m, +3 m].
  const uMin=-0.5, uMax=1.5;
  const dMin=-3.0, dMax=3.0;
  const margin={left:36, right:10, top:10, bottom:22};
  const plotW=W-margin.left-margin.right;
  const plotH=H-margin.top-margin.bottom;
  const xAt=(u)=>margin.left+((u-uMin)/(uMax-uMin))*plotW;
  const yAt=(d)=>margin.top+((dMax-d)/(dMax-dMin))*plotH;

  // Background bands: segment (green), extension (amber), outside (default).
  const lineLenM=metersBetween(line.p1,line.p2);
  const extFrac=lineLenM>0?Math.min(END_TOL_M/lineLenM,1.0):0;
  // outside (full canvas, subtle)
  ctx.fillStyle='#1a1f26';
  ctx.fillRect(margin.left,margin.top,plotW,plotH);
  // extension band
  ctx.fillStyle='#3a2a10';
  ctx.fillRect(xAt(-extFrac),margin.top,xAt(1+extFrac)-xAt(-extFrac),plotH);
  // segment band
  ctx.fillStyle='#0e2a14';
  ctx.fillRect(xAt(0),margin.top,xAt(1)-xAt(0),plotH);

  // The line itself at signed_d = 0.
  ctx.strokeStyle='#f55';
  ctx.lineWidth=2;
  ctx.beginPath();
  ctx.moveTo(xAt(0),yAt(0));
  ctx.lineTo(xAt(1),yAt(0));
  ctx.stroke();
  // Extension (dashed yellow).
  ctx.setLineDash([4,3]);
  ctx.strokeStyle='#fd5';
  ctx.beginPath();
  ctx.moveTo(xAt(-extFrac),yAt(0));
  ctx.lineTo(xAt(0),yAt(0));
  ctx.moveTo(xAt(1),yAt(0));
  ctx.lineTo(xAt(1+extFrac),yAt(0));
  ctx.stroke();
  ctx.setLineDash([]);

  // Axes ticks and labels.
  ctx.strokeStyle='#444';
  ctx.fillStyle='#888';
  ctx.font='10px monospace';
  ctx.lineWidth=1;
  // vertical gridlines at u = -0.5, 0, 0.5, 1, 1.5
  [-0.5,0,0.5,1.0,1.5].forEach(u=>{
    ctx.beginPath();
    ctx.moveTo(xAt(u),margin.top);
    ctx.lineTo(xAt(u),margin.top+plotH);
    ctx.stroke();
    ctx.fillText(u.toFixed(1),xAt(u)-8,H-margin.bottom+12);
  });
  // horizontal gridlines at d = -2, -1, 0, 1, 2 m
  [-2,-1,0,1,2].forEach(d=>{
    ctx.beginPath();
    ctx.moveTo(margin.left,yAt(d));
    ctx.lineTo(margin.left+plotW,yAt(d));
    ctx.stroke();
    ctx.fillText(d+'m',2,yAt(d)+3);
  });

  // Trail points projected into (u, signed_d).
  const keep=Math.min(trail.length,200);
  for(let i=trail.length-keep;i<trail.length;i++){
    const p=trail[i];
    const [u,d]=projectToLineM(p,line.p1,line.p2);
    if(u<uMin||u>uMax||d<dMin||d>dMax) continue;
    // Older points fade to blue-grey, newer stay bright blue.
    const age=(trail.length-1-i)/keep;
    const alpha=1-age*0.8;
    ctx.fillStyle=`rgba(100,160,255,${alpha.toFixed(2)})`;
    ctx.beginPath();
    ctx.arc(xAt(u),yAt(d),2,0,Math.PI*2);
    ctx.fill();
  }

  // Candidate crossings: markers on the y=0 line at their u value.
  (cands||[]).slice(-10).forEach(c=>{
    const x=xAt(c.u);
    if(c.u<uMin||c.u>uMax) return;
    ctx.fillStyle=c.result==='PASS'?'#5f5':'#f55';
    ctx.beginPath();
    ctx.arc(x,yAt(0),4,0,Math.PI*2);
    ctx.fill();
  });

  // Current GPS point.
  if(cur){
    const [u,d]=projectToLineM(cur,line.p1,line.p2);
    if(u>=uMin&&u<=uMax&&d>=dMin&&d<=dMax){
      ctx.strokeStyle='#fff';
      ctx.fillStyle='#5f5';
      ctx.lineWidth=2;
      ctx.beginPath();
      ctx.arc(xAt(u),yAt(d),5,0,Math.PI*2);
      ctx.fill();
      ctx.stroke();
    }
  }

  // Title line with line length + tolerance info.
  ctx.fillStyle='#888';
  ctx.font='10px monospace';
  ctx.fillText(`line=${lineLenM.toFixed(2)}m  tol=${END_TOL_M}m  ext=±${extFrac.toFixed(2)}u`,
               margin.left+2,margin.top+10);
}

function renderDraft(d, dv){
  const active=!!d.active;
  const hasP1=!!d.p1, hasP2=!!d.p2;
  // Firmware requires MIN_ACCEPTED_CROSSINGS draft-validation PASSes
  // before `track save` will persist.  Gate the button here so the
  // operator sees the requirement up-front rather than getting a 409
  // after clicking.  dv is state.draft_validation which the event
  // parser keeps in sync with every [xing-draft] PASS/REJECT.
  const accepted=(dv && +dv.accepted)||0;
  const rejected=(dv && +dv.rejected)||0;
  const ready=active && hasP1 && hasP2;
  const validated=accepted>=MIN_ACCEPTED_CROSSINGS;
  document.getElementById('btn-mark-p1').disabled=!active;
  document.getElementById('btn-mark-p2').disabled=!active;
  document.getElementById('btn-save').disabled=!(ready && validated);
  document.getElementById('btn-cancel').disabled=!active;
  document.getElementById('btn-new').disabled=active;
  document.getElementById('draft-name').disabled=active;
  document.getElementById('p1-ind').className=hasP1?'on':'';
  document.getElementById('p2-ind').className=hasP2?'on':'';
  document.getElementById('draft-status').textContent=d.status||(active?'…':'idle — press Start Draft');

  // Inline hint under Save — replaces the silent-disabled button with
  // a specific explanation the operator can act on.
  const hint=document.getElementById('draft-validate-hint');
  if(hint){
    if(!active){
      hint.className='';
      hint.textContent='';
    } else if(!ready){
      hint.className='blocked';
      hint.textContent=hasP1 ? 'mark P2 to complete the line' : 'mark P1 first';
    } else if(validated){
      hint.className='ready';
      hint.textContent=`validated (${accepted} ✓ · ${rejected} ✗) — ready to Save`;
    } else {
      const need=MIN_ACCEPTED_CROSSINGS-accepted;
      hint.className='blocked';
      hint.textContent=`walk across the line ${need} more time${need===1?'':'s'} (now ${accepted} ✓ · ${rejected} ✗)`;
    }
  }

  // draft markers + line (orange, distinct from saved red).
  // Optional error circle (radius = firmware-reported sample spread in
  // metres) visualizes the confidence tier straight on the map: if the
  // circle at P1 overlaps the circle at P2, the line is too short for
  // this GPS accuracy to mark reliably.
  function tierColor(t){ return t==='High'?'#5f5':t==='Medium'?'#fd5':'#f55'; }

  if(hasP1){
    if(!draftP1Layer)draftP1Layer=L.circleMarker(d.p1,{radius:6,color:'#fa0',fillColor:'#fa0',fillOpacity:1}).bindTooltip('P1 draft',{permanent:false}).addTo(map);
    else draftP1Layer.setLatLng(d.p1);
    if(typeof d.p1_spread_m==='number'&&d.p1_spread_m>0){
      const tip=`P1 ±${d.p1_spread_m.toFixed(2)} m (${d.p1_tier||'?'})`;
      if(!draftP1CircleLayer){
        draftP1CircleLayer=L.circle(d.p1,{radius:d.p1_spread_m,color:tierColor(d.p1_tier),fillOpacity:0.08,weight:1,dashArray:'3,3'}).bindTooltip(tip).addTo(map);
      }else{
        draftP1CircleLayer.setLatLng(d.p1);
        draftP1CircleLayer.setRadius(d.p1_spread_m);
        draftP1CircleLayer.setStyle({color:tierColor(d.p1_tier)});
        draftP1CircleLayer.setTooltipContent(tip);
      }
    }else if(draftP1CircleLayer){map.removeLayer(draftP1CircleLayer);draftP1CircleLayer=null;}
  }else{
    if(draftP1Layer){map.removeLayer(draftP1Layer);draftP1Layer=null;}
    if(draftP1CircleLayer){map.removeLayer(draftP1CircleLayer);draftP1CircleLayer=null;}
  }

  if(hasP2){
    if(!draftP2Layer)draftP2Layer=L.circleMarker(d.p2,{radius:6,color:'#fa0',fillColor:'#fa0',fillOpacity:1}).bindTooltip('P2 draft',{permanent:false}).addTo(map);
    else draftP2Layer.setLatLng(d.p2);
    if(typeof d.p2_spread_m==='number'&&d.p2_spread_m>0){
      const tip=`P2 ±${d.p2_spread_m.toFixed(2)} m (${d.p2_tier||'?'})`;
      if(!draftP2CircleLayer){
        draftP2CircleLayer=L.circle(d.p2,{radius:d.p2_spread_m,color:tierColor(d.p2_tier),fillOpacity:0.08,weight:1,dashArray:'3,3'}).bindTooltip(tip).addTo(map);
      }else{
        draftP2CircleLayer.setLatLng(d.p2);
        draftP2CircleLayer.setRadius(d.p2_spread_m);
        draftP2CircleLayer.setStyle({color:tierColor(d.p2_tier)});
        draftP2CircleLayer.setTooltipContent(tip);
      }
    }else if(draftP2CircleLayer){map.removeLayer(draftP2CircleLayer);draftP2CircleLayer=null;}
  }else{
    if(draftP2Layer){map.removeLayer(draftP2Layer);draftP2Layer=null;}
    if(draftP2CircleLayer){map.removeLayer(draftP2CircleLayer);draftP2CircleLayer=null;}
  }

  if(hasP1&&hasP2){
    if(!draftLineLayer)draftLineLayer=L.polyline([d.p1,d.p2],{color:'#fa0',weight:4,opacity:0.9,dashArray:'4,4'}).addTo(map);
    else draftLineLayer.setLatLngs([d.p1,d.p2]);
  }else if(draftLineLayer){map.removeLayer(draftLineLayer);draftLineLayer=null;}
}

// Inline feedback for a failed draft / track-save command, preferring
// the in-panel #draft-validate-hint over a modal alert.  Modal alerts
// interrupt the walk-across flow; an inline red line is less jarring
// and stays visible while the operator fixes the issue.  Falls back
// to alert() when the hint element is absent or not currently shown.
function showDraftCommandError(text){
  const hint=document.getElementById('draft-validate-hint');
  if(hint){
    hint.className='err';
    hint.textContent=text;
    return;
  }
  alert(text);
}

async function sendCommand(cmd){
  try{
    const r=await fetch('/command',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({cmd:cmd}),
    });
    if(!r.ok){
      const body=await r.json().catch(()=>({}));
      showDraftCommandError('command failed: '+(body.error||r.status));
    }
  }catch(e){showDraftCommandError('command error: '+e);}
}

document.getElementById('btn-new').addEventListener('click',()=>{
  const name=(document.getElementById('draft-name').value||'').trim();
  if(!name){alert('Enter a track name first');return;}
  sendCommand('track draft '+name);
});
document.getElementById('btn-mark-p1').addEventListener('click',()=>sendCommand('mark p1'));
document.getElementById('btn-mark-p2').addEventListener('click',()=>sendCommand('mark p2'));
document.getElementById('btn-save').addEventListener('click',()=>{
  if(!confirm('Save this draft as a new active track?'))return;
  sendCommand('track save');
});
document.getElementById('btn-cancel').addEventListener('click',()=>sendCommand('track cancel'));

function setRecMsg(text, cls){
  var m=document.getElementById('rec-msg');
  if(!m)return;
  m.className = cls || '';
  m.textContent = text || '';
}

document.getElementById('btn-rec-start').addEventListener('click',async ()=>{
  setRecMsg('Starting...', '');
  try{
    const r = await fetch('/command', {
      method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({cmd:'recording start'}),
    });
    const body=await r.json().catch(()=>({}));
    if(!r.ok){
      setRecMsg('start failed: '+(body.error||r.status),'err');
    } else {
      setRecMsg(body.message||'start confirmed by firmware','ok');
    }
  } catch(e){setRecMsg('start failed: '+e, 'err');}
});

document.getElementById('btn-rec-stop').addEventListener('click',async ()=>{
  if(!confirm('Save recording and stop session?'))return;
  setRecMsg('Stopping...', '');
  try{
    const r = await fetch('/command', {
      method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({cmd:'recording stop'}),
    });
    const body=await r.json().catch(()=>({}));
    if(!r.ok){
      setRecMsg('stop failed: '+(body.error||r.status),'err');
    } else {
      setRecMsg(body.message||'stop confirmed by firmware','ok');
    }
  } catch(e){setRecMsg('stop failed: '+e, 'err');}
});

// --- Track picker -----------------------------------------------
// `change` fires when the operator picks a different entry from the
// dropdown.  Sends `track select <id>` and surfaces the ACK /
// error inline.  We don't force re-fetch of catalog here — the
// firmware's `[track] selected:` line will update active_track_id
// via the normal parse path.
function setTrackPickerStatus(text, cls){
  var el = document.getElementById('track-picker-status');
  if(!el) return;
  el.className = cls || '';
  el.textContent = text || '';
}
document.getElementById('track-select').addEventListener('change', async (ev) => {
  const id = ev.target.value;
  if(!id) return;
  setTrackPickerStatus('Selecting ' + id + '...', '');
  try {
    const r = await fetch('/command', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({cmd: 'track select ' + id}),
    });
    const body = await r.json().catch(() => ({}));
    if(!r.ok){
      setTrackPickerStatus('select failed: ' + (body.error || r.status), 'err');
    } else {
      setTrackPickerStatus(body.message || ('selected ' + id), 'ok');
    }
  } catch(e) { setTrackPickerStatus('select failed: ' + e, 'err'); }
});
document.getElementById('btn-track-autodetect').addEventListener('click', async () => {
  setTrackPickerStatus('Auto-detecting from GPS fix...', '');
  try {
    const r = await fetch('/command', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({cmd: 'track autodetect'}),
    });
    const body = await r.json().catch(() => ({}));
    if(!r.ok){
      setTrackPickerStatus('autodetect failed: ' + (body.error || r.status), 'err');
    } else {
      setTrackPickerStatus(body.message || 'autodetect confirmed', 'ok');
    }
  } catch(e) { setTrackPickerStatus('autodetect failed: ' + e, 'err'); }
});

poll();
</script></body></html>
"""


_ALLOWED_COMMANDS = {
    "track cancel",
    "track save",
    "track status",
    "track autodetect",
    "tracks list",
    "mark p1",
    "mark p2",
    "recording start",
    "recording stop",
    "gps stream off",
}


def _is_allowed_command(cmd: str) -> bool:
    # Whitelist the exact command tokens the UI can emit.  Draft-start
    # is allowed with any body matching `track draft <name>` where the
    # firmware's own is_track_name_valid() will do the final check;
    # we just keep raw shell-meta out of the forwarded bytes.
    cmd = cmd.strip()
    if cmd in _ALLOWED_COMMANDS:
        return True
    if cmd.startswith("track draft "):
        body = cmd[len("track draft "):]
        if not body:
            return False
        for ch in body:
            if ord(ch) < 0x20 or ord(ch) > 0x7E:
                return False
            if ch in '"\\/:*?<>|':
                return False
        return True
    if cmd.startswith("gps stream "):
        body = cmd[len("gps stream "):].strip()
        if body == "off" or body == "0":
            return True
        if not body.isdigit():
            return False
        rate = int(body)
        return 1 <= rate <= 25
    if cmd.startswith("track select "):
        # Mirror firmware's is_track_id_valid: track_<1-3 ASCII digits>.
        # Python str.isdigit() accepts non-ASCII digits (e.g. U+FF11
        # FULLWIDTH ONE, ०-९ Devanagari).  The firmware uses C
        # isdigit() on unsigned char, which only matches '0'-'9', so
        # using Python isdigit() here would forward inputs the
        # firmware later rejects — confusing the ACK wait.  Use an
        # explicit ASCII-digit check instead (codex review 2026-04-22
        # Low).
        body = cmd[len("track select "):].strip()
        if not body.startswith("track_"):
            return False
        digits = body[len("track_"):]
        if not (1 <= len(digits) <= 3):
            return False
        for ch in digits:
            if ch < "0" or ch > "9":
                return False
        return True
    return False


class Handler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path == "/":
            body = HTML.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/state":
            with state_lock:
                snapshot = dict(state)
            # Merge the serial-link health snapshot so the browser can
            # render a disconnect banner without needing a second poll.
            # Kept under a nested "serial" key to avoid colliding with
            # any top-level state field the firmware parser owns.
            snapshot["serial"] = _read_serial_health()
            body = json.dumps(snapshot).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self) -> None:
        if self.path != "/command":
            self.send_response(404)
            self.end_headers()
            return
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length > 0 else b""
        try:
            payload = json.loads(raw.decode("utf-8") or "{}")
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'{"error":"invalid json"}')
            return
        cmd = (payload.get("cmd") or "").strip()
        if not _is_allowed_command(cmd):
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'{"error":"command not allowed"}')
            return
        # Advisory fast-path: if the reader already knows the port is
        # gone, skip the write attempt and return a direct 503.  The
        # authoritative check lives inside _serial_write_command's
        # lock — this one is just a nicer error for the common case.
        if _ser_ref[0] is None:
            self.send_response(503)
            self.end_headers()
            self.wfile.write(
                b'{"error":"USB disconnected - plug the board back in"}')
            return
        with state_lock:
            start_seq = state.get("event_seq", 0)
        try:
            _serial_write_command(cmd)
        except RuntimeError as exc:
            # Port was alive at the advisory check but dropped before
            # the write lock — surface as 503 with the operator-facing
            # message, not as a generic 500.
            if "serial not open" in str(exc):
                self.send_response(503)
                self.end_headers()
                self.wfile.write(
                    b'{"error":"USB disconnected - plug the board back in"}')
                return
            self.send_response(500)
            self.end_headers()
            self.wfile.write(f'{{"error":"{exc}"}}'.encode())
            return
        except Exception as exc:
            self.send_response(500)
            self.end_headers()
            self.wfile.write(f'{{"error":"{exc}"}}'.encode())
            return
        ack = _wait_for_command_ack(cmd, start_seq, _command_timeout_s(cmd))
        if ack is None:
            self.send_response(504)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(
                b'{"error":"firmware did not acknowledge the command in time"}'
            )
            return
        ok, message = ack
        self.send_response(200 if ok else 409)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        if ok:
            self.wfile.write(
                json.dumps({"ok": True, "message": message}).encode("utf-8")
            )
        else:
            self.wfile.write(
                json.dumps({"ok": False, "error": message}).encode("utf-8")
            )

    def log_message(self, *args, **kwargs) -> None:
        return


def main() -> None:
    threading.Thread(target=serial_reader, daemon=True).start()
    threading.Thread(target=track_query_bootstrap, daemon=True).start()
    print(f"[live_map] serial: {PORT} @ {BAUD}")
    print(f"[live_map] open http://{LISTEN[0]}:{LISTEN[1]}")
    try:
        ThreadingHTTPServer(LISTEN, Handler).serve_forever()
    except KeyboardInterrupt:
        print("\n[live_map] bye")
    finally:
        try:
            _serial_write_command("gps stream off")
        except Exception:
            pass


if __name__ == "__main__":
    main()
