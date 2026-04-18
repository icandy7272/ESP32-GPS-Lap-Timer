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
}


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


def parse_line(line):
    m = _P1_RE.search(line)
    if m:
        _locked_set_line("p1", float(m.group(1)), float(m.group(2)))
    m = _P2_RE.search(line)
    if m:
        _locked_set_line("p2", float(m.group(1)), float(m.group(2)))
    m = _HEAD_RE.search(line)
    if m:
        _locked_set_line("heading", float(m.group(1)))

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


def serial_reader():
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


HTML = r"""<!doctype html>
<html><head>
<meta charset="utf-8"/>
<title>KartGPS Live Map</title>
<style>
 html,body{margin:0;height:100%;background:#0a0a0a;color:#ddd;font:13px/1.4 ui-monospace,monospace}
 #map{display:block;width:100vw;height:100vh}
 #info{position:absolute;top:8px;left:8px;padding:8px 12px;background:rgba(0,0,0,.75);border:1px solid #333;min-width:220px}
 #events{position:absolute;bottom:8px;left:8px;right:8px;max-height:160px;overflow-y:auto;padding:6px 10px;background:rgba(0,0,0,.75);border:1px solid #333;font-size:11px}
 #events .lap{color:#5f5}
 #events .xing{color:#fc5}
 #events .session{color:#ff5}
 .metric{color:#aaa}
 .bold{color:#fff;font-weight:bold}
</style></head><body>
<canvas id="map"></canvas>
<div id="info"></div>
<div id="events"></div>
<script>
const canvas=document.getElementById('map');
const ctx=canvas.getContext('2d');
const info=document.getElementById('info');
const events=document.getElementById('events');

function resize(){canvas.width=innerWidth;canvas.height=innerHeight}
addEventListener('resize',resize);resize();

const M_LAT=111000;
const m_lon=(lat)=>111000*Math.cos(lat*Math.PI/180);
const END_TOL_M=2.0;

let state={};

async function poll(){
  try{
    const r=await fetch('/state');
    state=await r.json();
    draw();
  }catch(e){/* ignore */}
  setTimeout(poll,300);
}

function proj(lat,lon,c,s){
  const dx=(lon-c[1])*m_lon(c[0]);
  const dy=(lat-c[0])*M_LAT;
  return [canvas.width/2+dx*s,canvas.height/2-dy*s];
}

function draw(){
  ctx.fillStyle='#0a0a0a';
  ctx.fillRect(0,0,canvas.width,canvas.height);

  const line=state.line||{};
  const cur=state.current;
  if(!cur && !(line.p1&&line.p2)){
    info.innerHTML='<span class="metric">Waiting for serial data…<br>Need either a [track] boot line or a [gps] fix.</span>';
    return;
  }

  let cLat,cLon;
  if(line.p1&&line.p2){cLat=(line.p1[0]+line.p2[0])/2;cLon=(line.p1[1]+line.p2[1])/2}
  else{cLat=cur[0];cLon=cur[1]}
  const center=[cLat,cLon];

  // auto scale
  let pts=[];
  if(line.p1)pts.push(line.p1);
  if(line.p2)pts.push(line.p2);
  if(cur)pts.push(cur);
  const trail=state.trail||[];
  for(const p of trail.slice(-80))pts.push(p);
  let max_m=8;
  for(const p of pts){
    const dx=(p[1]-cLon)*m_lon(cLat);
    const dy=(p[0]-cLat)*M_LAT;
    max_m=Math.max(max_m,Math.abs(dx),Math.abs(dy));
  }
  const margin=1.35;
  const s=Math.min(canvas.width,canvas.height)/(2*max_m*margin);

  // grid every 5 m
  const gm=5, gp=gm*s;
  ctx.strokeStyle='#1a1a1a';ctx.lineWidth=1;
  for(let x=canvas.width/2%gp;x<canvas.width;x+=gp){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,canvas.height);ctx.stroke();}
  for(let y=canvas.height/2%gp;y<canvas.height;y+=gp){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(canvas.width,y);ctx.stroke();}

  // line + extension
  if(line.p1&&line.p2){
    const dLat=line.p2[0]-line.p1[0], dLon=line.p2[1]-line.p1[1];
    const dm_lat=dLat*M_LAT, dm_lon=dLon*m_lon(cLat);
    const linelen=Math.hypot(dm_lat,dm_lon);
    const frac=Math.min(END_TOL_M/linelen,1.0);
    const e1=[line.p1[0]-dLat*frac,line.p1[1]-dLon*frac];
    const e2=[line.p2[0]+dLat*frac,line.p2[1]+dLon*frac];
    const sE1=proj(e1[0],e1[1],center,s);
    const sE2=proj(e2[0],e2[1],center,s);
    ctx.strokeStyle='#ff0';ctx.setLineDash([8,6]);ctx.lineWidth=2;
    ctx.beginPath();ctx.moveTo(...sE1);ctx.lineTo(...sE2);ctx.stroke();
    ctx.setLineDash([]);

    const sP1=proj(line.p1[0],line.p1[1],center,s);
    const sP2=proj(line.p2[0],line.p2[1],center,s);
    ctx.strokeStyle='#f44';ctx.lineWidth=5;
    ctx.beginPath();ctx.moveTo(...sP1);ctx.lineTo(...sP2);ctx.stroke();
    ctx.fillStyle='#f44';
    [sP1,sP2].forEach(pt=>{ctx.beginPath();ctx.arc(pt[0],pt[1],5,0,Math.PI*2);ctx.fill();});
    ctx.fillStyle='#fff';ctx.font='12px ui-monospace';
    ctx.fillText('P1',sP1[0]+7,sP1[1]-7);
    ctx.fillText('P2',sP2[0]+7,sP2[1]-7);
    ctx.fillText(linelen.toFixed(2)+' m',(sP1[0]+sP2[0])/2+8,(sP1[1]+sP2[1])/2+14);

    // valid heading
    if(typeof line.heading==='number'){
      const midLat=(line.p1[0]+line.p2[0])/2, midLon=(line.p1[1]+line.p2[1])/2;
      const sMid=proj(midLat,midLon,center,s);
      const rad=line.heading*Math.PI/180;
      const arr_m=Math.max(3,linelen*0.4);
      const ax=sMid[0]+Math.sin(rad)*arr_m*s;
      const ay=sMid[1]-Math.cos(rad)*arr_m*s;
      ctx.strokeStyle='#5f5';ctx.lineWidth=2;
      ctx.beginPath();ctx.moveTo(...sMid);ctx.lineTo(ax,ay);ctx.stroke();
      // arrowhead
      const back=(x,y,dx,dy)=>{ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x-dx,y-dy);ctx.stroke();};
      const bx=-Math.sin(rad-0.4)*6, by=Math.cos(rad-0.4)*6;
      const cx=-Math.sin(rad+0.4)*6, cy=Math.cos(rad+0.4)*6;
      back(ax,ay,bx,by);back(ax,ay,cx,cy);
    }
  }

  // trail
  if(trail.length>1){
    ctx.strokeStyle='#59f';ctx.lineWidth=1.5;
    ctx.beginPath();
    for(let i=0;i<trail.length;i++){
      const pt=proj(trail[i][0],trail[i][1],center,s);
      if(i===0)ctx.moveTo(...pt);else ctx.lineTo(...pt);
    }
    ctx.stroke();
  }

  // current pos
  if(cur){
    const pt=proj(cur[0],cur[1],center,s);
    ctx.fillStyle='#5f5';ctx.beginPath();ctx.arc(pt[0],pt[1],9,0,Math.PI*2);ctx.fill();
    ctx.strokeStyle='#fff';ctx.lineWidth=2;ctx.stroke();

    // distance to line midpoint
    if(line.p1&&line.p2){
      const midLat=(line.p1[0]+line.p2[0])/2, midLon=(line.p1[1]+line.p2[1])/2;
      const dx=(cur[1]-midLon)*m_lon(midLat);
      const dy=(cur[0]-midLat)*M_LAT;
      const d=Math.hypot(dx,dy);
      ctx.fillStyle='#fff';ctx.font='11px ui-monospace';
      ctx.fillText('d='+d.toFixed(1)+'m',pt[0]+12,pt[1]+4);
    }
  }

  // info panel
  const lat=cur?cur[0].toFixed(7):'—';
  const lon=cur?cur[1].toFixed(7):'—';
  const fix=state.fix_3d?'<span class="bold" style="color:#5f5">3D✓</span>':'<span style="color:#f55">no</span>';
  const tierNames=['Poor','Fair','Good','Excellent'];
  const tname=tierNames[state.quality_tier]||'?';
  const hdop=(state.hdop===-1||state.hdop==null)?'—':state.hdop.toFixed(1);
  info.innerHTML=`
    <span class="bold">lat</span> ${lat}<br>
    <span class="bold">lon</span> ${lon}<br>
    <span class="metric">sats</span> ${state.sats||0} · ${fix}<br>
    <span class="metric">quality</span> ${state.quality_score||0} <span class="metric">(${tname})</span><br>
    <span class="metric">hdop</span> ${hdop}<br>
    <span class="metric">trail</span> ${trail.length} pts<br>
    <span class="metric">line</span> ${line.p1&&line.p2?'loaded':'—'}<br>
    <span class="metric">scale</span> ~${(50/s).toFixed(1)} m / 50 px
  `;

  // events
  const now=Date.now()/1000;
  const evs=(state.events||[]).slice(-20).reverse();
  events.innerHTML=evs.map(e=>{
    let cls='';
    if(e.text.includes('[lap]'))cls='lap';
    else if(e.text.includes('[xing]'))cls='xing';
    else if(e.text.includes('[session]'))cls='session';
    const ago=Math.round(now-e.t);
    return `<div><span style="color:#666">[${ago}s]</span> <span class="${cls}">${e.text.replace(/</g,'&lt;')}</span></div>`;
  }).join('');
}

poll();
</script></body></html>
"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
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

    def log_message(self, *args, **kwargs):
        return


def main():
    threading.Thread(target=serial_reader, daemon=True).start()
    print(f"[live_map] serial: {PORT} @ {BAUD}")
    print(f"[live_map] open http://{LISTEN[0]}:{LISTEN[1]}")
    try:
        ThreadingHTTPServer(LISTEN, Handler).serve_forever()
    except KeyboardInterrupt:
        print("\n[live_map] bye")


if __name__ == "__main__":
    main()
