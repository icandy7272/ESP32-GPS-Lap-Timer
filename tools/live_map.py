#!/usr/bin/env python3
"""Live GPS + detection-line visualisation over USB serial.

Reads the firmware's [gps] 1 Hz diagnostic line for current position,
the [track] boot-log lines for the active P1/P2 segment, and lap /
session events; serves a self-contained canvas-based live map at
http://127.0.0.1:8080.

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
}

# Shared reference to the open serial port so the bootstrap thread
# below can write query commands.  Set inside serial_reader once the
# port is open.
_ser_ref: list = [None]


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
        events.append({"t": time.time(), "text": text})
        if len(events) > EVENT_MAX:
            del events[: len(events) - EVENT_MAX]


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
_TRACK_FILE_RE = re.compile(r"^(track_\d+\.json)\s*$")
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

    if line.startswith("[gps]"):
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

    if ("[lap]" in line) or ("[session]" in line) or ("[xing]" in line) \
            or ("[xing-draft]" in line):
        _locked_append_event(line)

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


def serial_reader() -> None:
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except Exception as exc:
        print(f"[live_map] cannot open {PORT}: {exc}")
        sys.exit(2)
    _ser_ref[0] = ser

    buf = b""
    while True:
        try:
            data = ser.read(1024)
        except Exception as exc:
            print(f"[live_map] serial read error: {exc}")
            time.sleep(1)
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

    def send(cmd: str) -> None:
        try:
            ser.write((cmd + "\r\n").encode("utf-8"))
            ser.flush()
        except Exception as exc:
            print(f"[live_map] serial write failed: {exc}")

    send("ls tracks")
    _track_discovery["requested_ls"] = True

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

    send(f"cat tracks/{track_name}")
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
 #info{position:absolute;top:8px;left:8px;z-index:1000;padding:8px 12px;background:rgba(0,0,0,.78);border:1px solid #444;min-width:240px;border-radius:6px}
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
 #right-col{position:absolute;top:8px;right:8px;z-index:1000;width:280px;display:flex;flex-direction:column;gap:8px;max-height:calc(100vh - 240px);overflow-y:auto}
 #cands{padding:8px 10px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px;max-height:260px;overflow-y:auto;font-size:11px}
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
 #events .lap{color:#5f5}
 #events .xing{color:#fc5}
 #events .session{color:#ff5}
 .metric{color:#aaa}
 .bold{color:#fff;font-weight:bold}
 .leaflet-container{background:#0a0a0a}
</style></head><body>
<div id="map"></div>
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
  <div id="draft-status">idle — press Start Draft</div>
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
  setTimeout(poll,300);
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

function update(state){
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

  // draft
  renderDraft(state.draft||{});

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

function renderDraft(d){
  const active=!!d.active;
  const hasP1=!!d.p1, hasP2=!!d.p2;
  document.getElementById('btn-mark-p1').disabled=!active;
  document.getElementById('btn-mark-p2').disabled=!active;
  document.getElementById('btn-save').disabled=!(active&&hasP1&&hasP2);
  document.getElementById('btn-cancel').disabled=!active;
  document.getElementById('btn-new').disabled=active;
  document.getElementById('draft-name').disabled=active;
  document.getElementById('p1-ind').className=hasP1?'on':'';
  document.getElementById('p2-ind').className=hasP2?'on':'';
  document.getElementById('draft-status').textContent=d.status||(active?'…':'idle — press Start Draft');

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

async function sendCommand(cmd){
  try{
    const r=await fetch('/command',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({cmd:cmd}),
    });
    if(!r.ok){
      const body=await r.json().catch(()=>({}));
      alert('command failed: '+(body.error||r.status));
    }
  }catch(e){alert('command error: '+e);}
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
    if(!r.ok){
      const body=await r.json().catch(()=>({}));
      setRecMsg('start failed: '+(body.error||r.status),'err');
    } else {
      setRecMsg('start command sent — waiting for firmware ACK','ok');
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
    if(!r.ok){
      const body=await r.json().catch(()=>({}));
      setRecMsg('stop failed: '+(body.error||r.status),'err');
    } else {
      setRecMsg('stop command sent — firmware finalizes .vbo','ok');
    }
  } catch(e){setRecMsg('stop failed: '+e, 'err');}
});

poll();
</script></body></html>
"""


_ALLOWED_COMMANDS = {
    "track cancel",
    "track save",
    "track status",
    "mark p1",
    "mark p2",
    "recording start",
    "recording stop",
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
                body = json.dumps(state).encode()
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
        ser = _ser_ref[0]
        if ser is None:
            self.send_response(503)
            self.end_headers()
            self.wfile.write(b'{"error":"serial not open"}')
            return
        try:
            ser.write((cmd + "\r\n").encode("utf-8"))
            ser.flush()
        except Exception as exc:
            self.send_response(500)
            self.end_headers()
            self.wfile.write(f'{{"error":"{exc}"}}'.encode())
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"ok":true}')

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


if __name__ == "__main__":
    main()
