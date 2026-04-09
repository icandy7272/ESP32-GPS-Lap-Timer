// ============================================================
// GET / — HTML dashboard
// Embedded dashboard UI (HTML/CSS/JS) + root handler.
// ============================================================

#include "wifi_internal.h"

#include <Arduino.h>
#include <WebServer.h>

static String build_dashboard_html();
static String build_head_section();
static String build_style_section();
static String build_body_section();
static String build_script_section();

void handle_root() {
    if (is_throttled()) {
        server.send(503, "text/plain", "Recording in progress, try later");
        return;
    }
    String html = build_dashboard_html();
    server.send(200, "text/html", html);
}

// ============================================================
// HTML Dashboard builder (split into sections for readability)
// ============================================================

static String build_dashboard_html() {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html lang=\"en\">";
    html += build_head_section();
    html += build_body_section();
    html += build_script_section();
    html += "</html>";
    return html;
}

static String build_head_section() {
    return "<head>"
           "<meta charset=\"UTF-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>GPS Lap Timer</title>"
           + build_style_section() +
           "</head>";
}

static String build_style_section() {
    return "<style>"
           "*{box-sizing:border-box;margin:0;padding:0}"
           "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;"
             "padding:16px;max-width:600px;margin:auto}"
           "h1{text-align:center;color:#0ff;margin-bottom:16px;font-size:1.4em}"
           ".card{background:#16213e;border-radius:8px;padding:12px;margin-bottom:12px}"
           ".card h2{font-size:1em;color:#0af;margin-bottom:8px}"
           ".row{display:flex;justify-content:space-between;padding:4px 0}"
           ".label{color:#888}.val{font-weight:bold}"
           ".ok{color:#0f0}.warn{color:#f80}.err{color:#f44}"
           "a{color:#0af;text-decoration:none}"
           "a:hover{text-decoration:underline}"
           "ul{list-style:none;padding:0}"
           "li{padding:4px 0;border-bottom:1px solid #223}"
           "input,select{background:#0d1b2a;color:#eee;border:1px solid #334;"
             "border-radius:4px;padding:6px 8px;width:100%;margin:4px 0}"
           "button{background:#0af;color:#000;border:none;border-radius:4px;"
             "padding:8px 16px;cursor:pointer;font-weight:bold;margin-top:8px}"
           "button:hover{background:#08d}"
           "#msg{text-align:center;color:#0f0;padding:8px;display:none}"
           "</style>";
}

static String build_body_section() {
    return "<body>"
           "<h1>GPS Lap Timer</h1>"

           // Status card
           "<div class=\"card\" id=\"status-card\">"
           "<h2>Status</h2>"
           "<div class=\"row\"><span class=\"label\">GPS Fix</span>"
             "<span class=\"val\" id=\"gps-fix\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Satellites</span>"
             "<span class=\"val\" id=\"sats\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Recording</span>"
             "<span class=\"val\" id=\"rec\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Current Lap</span>"
             "<span class=\"val\" id=\"lap\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Best Lap</span>"
             "<span class=\"val\" id=\"best\">--</span></div>"
           "<div class=\"row\"><span class=\"label\">Track</span>"
             "<span class=\"val\" id=\"track\">--</span></div>"
           "<button id=\"rec-btn\" onclick=\"toggleRecording()\" "
             "style=\"width:100%;padding:12px;font-size:1.1em;margin-top:8px\">"
             "Loading...</button>"
           "</div>"

           // Sessions card
           "<div class=\"card\">"
           "<h2>Sessions</h2>"
           "<ul id=\"sessions\"><li>Loading...</li></ul>"
           "</div>"

           // Tracks card
           "<div class=\"card\">"
           "<h2>Tracks</h2>"
           "<ul id=\"tracks\"><li>Loading...</li></ul>"
           "<h2 style=\"margin-top:12px\">Add Track</h2>"
           "<input id=\"tname\" placeholder=\"Track name\">"
           "<p style=\"color:#888;font-size:12px;margin:4px 0\">Start/Finish Line</p>"
           "<input id=\"tlat1\" placeholder=\"SF lat1\" type=\"number\" step=\"any\">"
           "<input id=\"tlon1\" placeholder=\"SF lon1\" type=\"number\" step=\"any\">"
           "<input id=\"tlat2\" placeholder=\"SF lat2\" type=\"number\" step=\"any\">"
           "<input id=\"tlon2\" placeholder=\"SF lon2\" type=\"number\" step=\"any\">"
           "<input id=\"theading\" placeholder=\"SF heading (deg)\" type=\"number\" step=\"any\">"
           "<div id=\"sectors-box\">"
           "<p style=\"color:#888;font-size:12px;margin:8px 0 4px\">Sector Splits (optional, up to 3)</p>"
           "<div id=\"sector-list\"></div>"
           "<button type=\"button\" onclick=\"addSectorRow()\" "
             "style=\"background:#334;font-size:0.8em;margin-top:4px\">"
             "+ Add Sector Split</button>"
           "</div>"
           "<button onclick=\"addTrack()\">Create Track</button>"
           "</div>"

           // Settings card
           "<div class=\"card\">"
           "<h2>Settings</h2>"
           "<div class=\"row\"><span class=\"label\">SSID</span>"
             "<input id=\"s-ssid\"></div>"
           "<div class=\"row\"><span class=\"label\">Password</span>"
             "<input id=\"s-pass\" type=\"password\"></div>"
           "<div class=\"row\"><span class=\"label\">Brightness</span>"
             "<input id=\"s-bright\" type=\"number\" min=\"0\" max=\"255\"></div>"
           "<button onclick=\"saveSettings()\">Save Settings</button>"
           "<div id=\"msg\">Saved!</div>"
           "<p style=\"color:#aaa;font-size:12px;margin-top:8px\">"
             "WiFi\xe5\x90\x8d\xe7\xa7\xb0\xe5\x92\x8c\xe5\xaf\x86\xe7\xa0\x81"
             "\xe4\xbf\xae\xe6\x94\xb9\xe5\x90\x8e\xe9\x9c\x80\xe9\x87\x8d\xe5\x90\xaf"
             "\xe7\x94\x9f\xe6\x95\x88\xe3\x80\x82"
             "\xe4\xba\xae\xe5\xba\xa6\xe7\xab\x8b\xe5\x8d\xb3\xe7\x94\x9f\xe6\x95\x88\xe3\x80\x82"
           "</p>"
           "</div>"

           "</body>";
}

static String build_script_section() {
    return "<script>"
           "function $(id){return document.getElementById(id)}"

           // Refresh status every 2s
           "function refreshStatus(){"
             "fetch('/api/status').then(r=>r.json()).then(d=>{"
               "$('gps-fix').textContent=d.gps_fix?'Yes':'No';"
               "$('gps-fix').className='val '+(d.gps_fix?'ok':'err');"
               "$('sats').textContent=d.satellites;"
               "$('rec').textContent=d.recording?'REC':'Idle';"
               "$('rec').className='val '+(d.recording?'warn':'ok');"
               "$('lap').textContent=d.current_lap;"
               "$('best').textContent=d.best_lap_ms>0?"
                 "(d.best_lap_ms/1000).toFixed(3)+'s':'--';"
               "$('track').textContent=d.track||'None';"
               "_isRec=d.recording;updateRecBtn();"
             "}).catch(()=>{})}"

           // Load sessions
           "function loadSessions(){"
             "fetch('/api/sessions').then(r=>r.json()).then(d=>{"
               "let ul=$('sessions');ul.innerHTML='';"
               "if(!d.sessions||!d.sessions.length){"
                 "ul.innerHTML='<li>No sessions</li>';return}"
               "d.sessions.forEach(s=>{"
                 "let li=document.createElement('li');"
                 "li.innerHTML='<a href=\"/files/'+s+'\">'+s+'</a>';"
                 "ul.appendChild(li)})"
             "}).catch(()=>{})}"

           // Load tracks
           "function loadTracks(){"
             "fetch('/api/tracks').then(r=>r.json()).then(d=>{"
               "let ul=$('tracks');ul.innerHTML='';"
               "if(!d.tracks||!d.tracks.length){"
                 "ul.innerHTML='<li>No tracks</li>';return}"
               "d.tracks.forEach(t=>{"
                 "let li=document.createElement('li');"
                 "li.style.display='flex';li.style.justifyContent='space-between';"
                 "li.style.alignItems='center';"
                 "let sp=document.createElement('span');"
                 "sp.textContent=t.name+' ('+t.id+')';"
                 "let bx=document.createElement('span');"
                 "let sb=document.createElement('button');"
                 "sb.textContent='\\u9009\\u4e3a\\u5f53\\u524d';"
                 "sb.style.cssText='padding:4px 8px;margin:0 4px;font-size:0.8em';"
                 "sb.onclick=function(){selectTrack(t.id)};"
                 "let db=document.createElement('button');"
                 "db.textContent='\\u5220\\u9664';"
                 "db.style.cssText='padding:4px 8px;font-size:0.8em;background:#f44';"
                 "db.onclick=function(){deleteTrack(t.id,t.name)};"
                 "bx.appendChild(sb);bx.appendChild(db);"
                 "li.appendChild(sp);li.appendChild(bx);"
                 "ul.appendChild(li)})"
             "}).catch(()=>{})}"

           // Sector row management
           "var _sectorCount=0;"
           "function addSectorRow(){"
             "if(_sectorCount>=3)return;"
             "_sectorCount++;"
             "var n=_sectorCount;"
             "var d=document.createElement('div');"
             "d.id='sec'+n;"
             "d.innerHTML='<p style=\"color:#aaa;font-size:11px\">Split '+n+'</p>'"
               "+'<input id=\"slat1_'+n+'\" placeholder=\"Sector '+n+' lat1\" type=\"number\" step=\"any\">'"
               "+'<input id=\"slon1_'+n+'\" placeholder=\"Sector '+n+' lon1\" type=\"number\" step=\"any\">'"
               "+'<input id=\"slat2_'+n+'\" placeholder=\"Sector '+n+' lat2\" type=\"number\" step=\"any\">'"
               "+'<input id=\"slon2_'+n+'\" placeholder=\"Sector '+n+' lon2\" type=\"number\" step=\"any\">'"
               "+'<input id=\"shd_'+n+'\" placeholder=\"Sector '+n+' heading\" type=\"number\" step=\"any\">';"
             "$('sector-list').appendChild(d)}"

           // Add track
           "function addTrack(){"
             "var secs=[];"
             "for(var i=1;i<=_sectorCount;i++){"
               "var la1=$('slat1_'+i),lo1=$('slon1_'+i);"
               "var la2=$('slat2_'+i),lo2=$('slon2_'+i),hd=$('shd_'+i);"
               "if(la1&&la1.value)secs.push({lat1:+la1.value,lon1:+lo1.value,"
                 "lat2:+la2.value,lon2:+lo2.value,heading:+hd.value})}"
             "let b={name:$('tname').value,"
               "sf_lat1:+$('tlat1').value,sf_lon1:+$('tlon1').value,"
               "sf_lat2:+$('tlat2').value,sf_lon2:+$('tlon2').value,"
               "sf_heading:+$('theading').value,sectors:secs};"
             "fetch('/api/tracks',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify(b)})"
             ".then(r=>r.json()).then(d=>{if(d.ok)loadTracks()})"
             ".catch(()=>{})}"

           // Load settings
           "function loadSettings(){"
             "fetch('/api/settings').then(r=>r.json()).then(d=>{"
               "$('s-ssid').value=d.wifi_ssid;"
               "$('s-pass').value=d.wifi_pass;"
               "$('s-bright').value=d.brightness"
             "}).catch(()=>{})}"

           // Save settings
           "function saveSettings(){"
             "let b={wifi_ssid:$('s-ssid').value,"
               "wifi_pass:$('s-pass').value,"
               "brightness:+$('s-bright').value};"
             "fetch('/api/settings',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify(b)})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok){let m=$('msg');m.style.display='block';"
                 "setTimeout(()=>m.style.display='none',2000)}"
             "}).catch(()=>{})}"

           // Select track
           "function selectTrack(id){"
             "fetch('/api/tracks/select',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify({id:id})})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok){refreshStatus();loadTracks()}"
             "}).catch(()=>{})}"

           // Delete track
           "function deleteTrack(id,name){"
             "if(!confirm('\\u786e\\u8ba4\\u5220\\u9664\\u8d5b\\u9053: '+name+'?'))return;"
             "fetch('/api/tracks/delete',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify({id:id})})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok)loadTracks()"
             "}).catch(()=>{})}"

           // Toggle recording
           "var _isRec=false;"
           "function toggleRecording(){"
             "let act=_isRec?'stop':'start';"
             "fetch('/api/recording',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify({action:act})})"
             ".then(r=>r.json()).then(d=>{"
               "if(d.ok){_isRec=d.recording;updateRecBtn()}"
             "}).catch(()=>{})}"
           "function updateRecBtn(){"
             "let b=$('rec-btn');"
             "if(_isRec){b.textContent='\\u505c\\u6b62\\u5f55\\u5236';"
               "b.style.background='#f44'}"
             "else{b.textContent='\\u5f00\\u59cb\\u5f55\\u5236';"
               "b.style.background='#0af'}}"

           // Init
           "refreshStatus();loadSessions();loadTracks();loadSettings();"
           "setInterval(refreshStatus,2000);"

           "</script>";
}
