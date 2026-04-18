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


_P1_RE = re.compile(r"p1\s*=\s*\(([0-9.\-]+),\s*([0-9.\-]+)\)")
_P2_RE = re.compile(r"p2\s*=\s*\(([0-9.\-]+),\s*([0-9.\-]+)\)")
_HEAD_RE = re.compile(r"valid_heading\s*=\s*([0-9.\-]+)\s*deg")
_LATLON_RE = re.compile(r"lat=([0-9.\-]+)\s+lon=([0-9.\-]+)")
_SATS_RE = re.compile(r"sats=(\d+)(?:-(\d+))?")
_FIX3D_RE = re.compile(r"fix_3d=(\d)")
_Q_RE = re.compile(r"q=(\d+)\s+tier=(\d+)")
_HDOP_RE = re.compile(r"hdop=([0-9.\-]+)")
_TRACK_FILE_RE = re.compile(r"^(track_\d+\.json)\s*$")
_SERIAL_HEADER_RE = re.compile(r"^\[serial\] --- tracks/([^ ]+) ---")
_DRAFT_STARTED_RE = re.compile(r"\[draft\] started: (\S+)")
_DRAFT_P1_RE = re.compile(
    r"\[draft\] p1\s*=\s*\(([0-9.\-]+),\s*([0-9.\-]+)\)"
)
_DRAFT_P2_RE = re.compile(
    r"\[draft\] p2\s*=\s*\(([0-9.\-]+),\s*([0-9.\-]+)\)(?:\s+heading=([0-9.\-]+))?"
)
_DRAFT_SAVED_RE = re.compile(
    r"\[draft\] saved: (\S+) \(([^)]+)\) length=([0-9.\-]+)m heading=([0-9.\-]+)"
)


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
        with state_lock:
            state["draft"]["p1"] = [float(m.group(1)), float(m.group(2))]
            state["draft"]["status"] = "P1 marked. Walk to P2."
        return
    m = _DRAFT_P2_RE.search(line)
    if m:
        with state_lock:
            state["draft"]["p2"] = [float(m.group(1)), float(m.group(2))]
            if m.group(3):
                state["draft"]["heading"] = float(m.group(3))
            state["draft"]["status"] = "P2 marked. Review and Save."
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

    if ("[lap]" in line) or ("[session]" in line) or ("[xing]" in line):
        _locked_append_event(line)

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
            if line:
                parse_line(line)


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
 #events{position:absolute;bottom:8px;left:8px;right:8px;max-height:150px;overflow-y:auto;padding:6px 10px;background:rgba(0,0,0,.78);border:1px solid #444;font-size:11px;z-index:1000;border-radius:6px}
 #draft{position:absolute;top:8px;right:8px;z-index:1000;padding:10px 12px;background:rgba(0,0,0,.82);border:1px solid #555;border-radius:6px;min-width:260px}
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

  // draft markers + line (orange, distinct from saved red)
  if(hasP1){
    if(!draftP1Layer)draftP1Layer=L.circleMarker(d.p1,{radius:6,color:'#fa0',fillColor:'#fa0',fillOpacity:1}).bindTooltip('P1 draft',{permanent:false}).addTo(map);
    else draftP1Layer.setLatLng(d.p1);
  }else if(draftP1Layer){map.removeLayer(draftP1Layer);draftP1Layer=null;}

  if(hasP2){
    if(!draftP2Layer)draftP2Layer=L.circleMarker(d.p2,{radius:6,color:'#fa0',fillColor:'#fa0',fillOpacity:1}).bindTooltip('P2 draft',{permanent:false}).addTo(map);
    else draftP2Layer.setLatLng(d.p2);
  }else if(draftP2Layer){map.removeLayer(draftP2Layer);draftP2Layer=null;}

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

poll();
</script></body></html>
"""


_ALLOWED_COMMANDS = {
    "track cancel",
    "track save",
    "track status",
    "mark p1",
    "mark p2",
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
