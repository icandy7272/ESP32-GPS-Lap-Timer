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
           ".gps-bar{background:#0f1f32;border:1px solid #2a3c52;border-left:4px solid #f44;"
             "border-radius:8px;padding:10px;margin:8px 0}"
           ".gps-bar.ok{border-left-color:#0f0}"
           ".gps-bar.err{border-left-color:#f44}"
           ".mark-row{display:flex;flex-direction:column;gap:6px;margin:8px 0}"
           ".mark-btn{width:100%;min-height:44px;padding:10px 12px;font-size:0.95em;"
             "margin-top:0}"
           ".mark-btn.marked{background:#12391f;color:#7ff5a1;border:1px solid #2faa5a}"
           ".mark-btn.disabled{background:#334;color:#889;cursor:not-allowed}"
           ".coord{font-family:monospace;font-size:12px;color:#8ec5ff;"
             "word-break:break-word;padding:2px 0}"
           ".heading-bar{display:flex;align-items:center;justify-content:space-between;"
             "gap:8px;background:#0f1f32;border:1px solid #2a3c52;border-radius:8px;"
             "padding:8px;margin:8px 0}"
           ".sector-card{border:1px solid #2a3c52;border-radius:8px;padding:8px;"
             "margin:8px 0;background:#0f1f32}"
           ".collapse-toggle{width:100%;text-align:left;background:#1b2f4c;color:#bcdfff;"
             "border:1px solid #355175;padding:10px 12px;margin-top:8px}"
           ".helper-text{color:#9eb3ca;font-size:12px;line-height:1.4;margin:4px 0}"
           ".primary-btn.disabled{background:#334;color:#889;cursor:not-allowed}"
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
           "<h2 style=\"margin-top:12px\">Track Creation</h2>"
           "<input id=\"track-name\" placeholder=\"Track name\">"
           "<div id=\"gps-bar\" class=\"gps-bar err\">"
           "<div class=\"helper-text\">GPS status unavailable</div>"
           "</div>"
           "<div id=\"sf-section\">"
           "<p class=\"helper-text\">Start/Finish</p>"
           "<div class=\"mark-row\">"
           "<button type=\"button\" id=\"sf-p1-btn\" class=\"mark-btn\">Mark P1</button>"
           "<div id=\"sf-p1-coord\" class=\"coord\">Not set</div>"
           "</div>"
           "<div class=\"mark-row\">"
           "<button type=\"button\" id=\"sf-p2-btn\" class=\"mark-btn disabled\">Mark P2</button>"
           "<div id=\"sf-p2-coord\" class=\"coord\">Not set</div>"
           "</div>"
           "<div id=\"sf-heading\" class=\"heading-bar\">"
           "<span>Heading pending</span>"
           "<button type=\"button\" id=\"sf-flip\" class=\"mark-btn\" "
             "style=\"width:auto;min-height:0;padding:6px 10px\">Flip</button>"
           "</div>"
           "</div>"
           "<button type=\"button\" id=\"sector-toggle\" class=\"collapse-toggle\">"
             "Sector Splits (optional)</button>"
           "<div id=\"sector-list\"></div>"
           "<button type=\"button\" id=\"add-sector-btn\" class=\"mark-btn\">+ Add Sector</button>"
           "<button type=\"button\" id=\"create-track-btn\" class=\"primary-btn disabled\">"
             "Create Track</button>"
           "<div id=\"track-create-msg\" class=\"helper-text\"></div>"
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
             "var d=document.createElement('div');"
             "d.className='sector-card';"
             "d.innerHTML='<div class=\"helper-text\">Sector '+_sectorCount+' placeholder</div>';"
             "$('sector-list').appendChild(d)}"

           // Add track
           "function addTrack(){"
             "var nameField=$('track-name');"
             "var msg=$('track-create-msg');"
             "if(!msg)return;"
             "if(!nameField||!nameField.value.trim()){"
               "msg.textContent='Please enter a track name.';"
               "return}"
             "msg.textContent='Track creation scaffold ready. Mark-flow logic comes next.'}"

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
