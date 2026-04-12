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
           "button:disabled{background:#334;color:#889;cursor:not-allowed}"
           "button:disabled:hover{background:#334}"
           ".gps-bar{background:#0f1f32;border:1px solid #2a3c52;border-left:4px solid #f44;"
             "border-radius:8px;padding:10px;margin:8px 0}"
           ".gps-bar.ok{border-left-color:#0f0}"
           ".gps-bar.err{border-left-color:#f44}"
           ".mark-row{display:flex;flex-direction:column;gap:6px;margin:8px 0}"
           ".mark-btn{width:100%;min-height:44px;padding:10px 12px;font-size:0.95em;"
             "margin-top:0}"
           ".mark-btn.marked{background:#12391f;color:#7ff5a1;border:1px solid #2faa5a}"
           ".coord{font-family:monospace;font-size:12px;color:#8ec5ff;"
             "word-break:break-word;padding:2px 0}"
           ".heading-bar{display:flex;align-items:center;justify-content:space-between;"
             "gap:8px;background:#0f1f32;border:1px solid #2a3c52;border-radius:8px;"
             "padding:8px;margin:8px 0}"
           ".heading-value{font-weight:bold;color:#d9ebff}"
           ".inline-btn{width:auto;min-height:0;padding:6px 10px;margin-top:0}"
           ".sector-card{border:1px solid #2a3c52;border-radius:8px;padding:8px;"
             "margin:8px 0;background:#0f1f32}"
           ".sector-head{display:flex;justify-content:space-between;align-items:center;"
             "gap:8px}"
           ".collapse-toggle{width:100%;text-align:left;background:#1b2f4c;color:#bcdfff;"
             "border:1px solid #355175;padding:10px 12px;margin-top:8px}"
           ".helper-text{color:#9eb3ca;font-size:12px;line-height:1.4;margin:4px 0}"
           ".primary-btn:disabled{background:#334;color:#889;cursor:not-allowed}"
           ".track-msg-ok{color:#7ff5a1}"
           ".track-msg-err{color:#ff8f8f}"
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
           "<button type=\"button\" id=\"sf-p2-btn\" class=\"mark-btn\" disabled>Mark P2</button>"
           "<div id=\"sf-p2-coord\" class=\"coord\">Not set</div>"
           "</div>"
           "<div id=\"sf-heading\" class=\"heading-bar\">"
           "<span id=\"sf-heading-text\" class=\"heading-value\">Heading pending</span>"
           "<button type=\"button\" id=\"sf-flip\" class=\"mark-btn inline-btn\" disabled>Flip</button>"
           "</div>"
           "</div>"
           "<button type=\"button\" id=\"sector-toggle\" class=\"collapse-toggle\">"
             "Sector Splits (optional)</button>"
           "<div id=\"sector-list\"></div>"
           "<button type=\"button\" id=\"add-sector-btn\" class=\"mark-btn\">+ Add Sector</button>"
           "<button type=\"button\" id=\"create-track-btn\" class=\"primary-btn\" disabled>"
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
    return R"JS(<script>
function $(id){return document.getElementById(id)}

var _isRec=false;
var _nextSectorId=1;
var _trackDraft={
  gps:{fix:false,satellites:0,lat:0,lon:0},
  startFinish:{p1:null,p2:null,heading:null,flipped:false},
  sectors:[],
  sectorsExpanded:false
};

function formatCoord(point){
  if(!point){return 'Not set';}
  return point.lat.toFixed(7)+', '+point.lon.toFixed(7);
}

function normalizeHeading(deg){
  var value=deg%360;
  if(value<0){value+=360;}
  return value;
}

function bearingDeg(p1,p2){
  var lat1=p1.lat*Math.PI/180;
  var lat2=p2.lat*Math.PI/180;
  var dLon=(p2.lon-p1.lon)*Math.PI/180;
  var y=Math.sin(dLon)*Math.cos(lat2);
  var x=Math.cos(lat1)*Math.sin(lat2)-Math.sin(lat1)*Math.cos(lat2)*Math.cos(dLon);
  return normalizeHeading(Math.atan2(y,x)*180/Math.PI);
}

function crossingHeadingDeg(p1,p2,flipped){
  var heading=normalizeHeading(bearingDeg(p1,p2)+90);
  if(flipped){heading=normalizeHeading(heading+180);}
  return heading;
}

function compassLabel(deg){
  var labels=['N','NE','E','SE','S','SW','W','NW'];
  return labels[Math.floor((normalizeHeading(deg)+22.5)/45)%8];
}

function headingArrow(deg){
  var arrows=['↑','↗','→','↘','↓','↙','←','↖'];
  return arrows[Math.floor((normalizeHeading(deg)+22.5)/45)%8];
}

function hasValidFix(){
  return _trackDraft.gps.fix&&(_trackDraft.gps.lat!==0||_trackDraft.gps.lon!==0);
}

function isCreateReady(){
  var nameField=$('track-name');
  return !!(nameField&&nameField.value.trim()&&
            _trackDraft.startFinish.p1&&
            _trackDraft.startFinish.p2&&
            typeof _trackDraft.startFinish.heading==='number');
}

function setTrackMsg(text,kind){
  var msg=$('track-create-msg');
  if(!msg){return;}
  msg.className='helper-text';
  if(kind==='ok'){msg.className+=' track-msg-ok';}
  if(kind==='err'){msg.className+=' track-msg-err';}
  msg.textContent=text||'';
}

function currentGpsPoint(){
  return {lat:_trackDraft.gps.lat,lon:_trackDraft.gps.lon};
}

function resetTrackDraft(){
  var gps=_trackDraft.gps;
  _trackDraft={
    gps:{fix:gps.fix,satellites:gps.satellites,lat:gps.lat,lon:gps.lon},
    startFinish:{p1:null,p2:null,heading:null,flipped:false},
    sectors:[],
    sectorsExpanded:false
  };
  _nextSectorId=1;
}

function updateStartFinishHeading(){
  var sf=_trackDraft.startFinish;
  if(sf.p1&&sf.p2){
    sf.heading=crossingHeadingDeg(sf.p1,sf.p2,sf.flipped);
  }else{
    sf.heading=null;
  }
}

function findSector(id){
  for(var i=0;i<_trackDraft.sectors.length;i++){
    if(_trackDraft.sectors[i].id===id){return _trackDraft.sectors[i];}
  }
  return null;
}

function updateSectorHeading(sector){
  if(sector.p1&&sector.p2){
    sector.heading=crossingHeadingDeg(sector.p1,sector.p2,sector.flipped);
  }else{
    sector.heading=null;
  }
}

function sectorIsComplete(sector){
  return !!(sector&&sector.p1&&sector.p2&&typeof sector.heading==='number');
}

function renderGpsBar(){
  var bar=$('gps-bar');
  if(!bar){return;}
  var fixOk=hasValidFix();
  bar.className='gps-bar '+(fixOk?'ok':'err');
  bar.innerHTML=
    '<div class="row"><span class="label">GPS</span><span class="val '+(fixOk?'ok':'err')+'">'+(fixOk?'Ready to mark':'Waiting for fix')+'</span></div>'+
    '<div class="row"><span class="label">Satellites</span><span class="val">'+_trackDraft.gps.satellites+'</span></div>'+
    '<div class="helper-text">Current position: '+(fixOk?formatCoord(currentGpsPoint()):'--')+'</div>';
}

function renderStartFinishSection(){
  var p1Btn=$('sf-p1-btn');
  var p2Btn=$('sf-p2-btn');
  var p1Coord=$('sf-p1-coord');
  var p2Coord=$('sf-p2-coord');
  var headingBar=$('sf-heading');
  var headingText=$('sf-heading-text');
  var flipBtn=$('sf-flip');
  var p1Marked=!!_trackDraft.startFinish.p1;
  var p2Marked=!!_trackDraft.startFinish.p2;
  var canMark=hasValidFix();
  var showHeading=p1Marked&&p2Marked&&typeof _trackDraft.startFinish.heading==='number';

  if(p1Btn){
    p1Btn.textContent=p1Marked?'✓ P1':'Mark P1';
    p1Btn.className='mark-btn'+(p1Marked?' marked':'');
    p1Btn.disabled=!canMark;
  }
  if(p2Btn){
    p2Btn.textContent=p2Marked?'✓ P2':'Mark P2';
    p2Btn.className='mark-btn'+(p2Marked?' marked':'');
    p2Btn.disabled=!canMark||!p1Marked;
  }
  if(p1Coord){p1Coord.textContent=formatCoord(_trackDraft.startFinish.p1);}
  if(p2Coord){p2Coord.textContent=formatCoord(_trackDraft.startFinish.p2);}
  if(headingBar){headingBar.style.display=showHeading?'flex':'none';}
  if(headingText&&showHeading){
    headingText.textContent=headingArrow(_trackDraft.startFinish.heading)+' '
      +Math.round(_trackDraft.startFinish.heading)+'° '
      +compassLabel(_trackDraft.startFinish.heading);
  }
  if(flipBtn){flipBtn.disabled=!showHeading;}
}

function renderSectorRows(){
  var toggle=$('sector-toggle');
  var list=$('sector-list');
  var addBtn=$('add-sector-btn');
  if(!toggle||!list||!addBtn){return;}

  toggle.textContent=_trackDraft.sectorsExpanded?'Hide Sector Splits (optional)':'Show Sector Splits (optional)';
  list.style.display=_trackDraft.sectorsExpanded?'block':'none';
  addBtn.style.display=_trackDraft.sectorsExpanded?'block':'none';
  addBtn.disabled=_trackDraft.sectors.length>=3;

  if(!_trackDraft.sectorsExpanded){
    list.innerHTML='';
    return;
  }

  if(!_trackDraft.sectors.length){
    list.innerHTML='<div class="helper-text">No sectors yet. Add up to 3 optional split lines.</div>';
    return;
  }

  var html='';
  for(var i=0;i<_trackDraft.sectors.length;i++){
    var sector=_trackDraft.sectors[i];
    var p1Marked=!!sector.p1;
    var p2Marked=!!sector.p2;
    var canMarkP2=hasValidFix()&&p1Marked;
    var headingHtml='<div class="helper-text">Mark P1 then P2 to calculate heading.</div>';
    if(sectorIsComplete(sector)){
      headingHtml='<div class="heading-bar"><span class="heading-value">'
        +headingArrow(sector.heading)+' '+Math.round(sector.heading)+'° '+compassLabel(sector.heading)
        +'</span><button type="button" class="mark-btn inline-btn" onclick="flipSectorHeading('+sector.id+')">Flip</button></div>';
    }
    html+='<div class="sector-card">'
      +'<div class="sector-head"><strong>Sector '+(i+1)+'</strong>'
      +'<button type="button" class="mark-btn inline-btn" onclick="deleteSectorRow('+sector.id+')">Delete</button></div>'
      +'<div class="mark-row">'
      +'<button type="button" class="mark-btn'+(p1Marked?' marked':'')+'" onclick="markSectorPoint('+sector.id+',\'p1\')"'+(hasValidFix()?'':' disabled')+'>'+(p1Marked?'✓ P1':'Mark P1')+'</button>'
      +'<div class="coord">'+formatCoord(sector.p1)+'</div></div>'
      +'<div class="mark-row">'
      +'<button type="button" class="mark-btn'+(p2Marked?' marked':'')+'" onclick="markSectorPoint('+sector.id+',\'p2\')"'+(canMarkP2?'':' disabled')+'>'+(p2Marked?'✓ P2':'Mark P2')+'</button>'
      +'<div class="coord">'+formatCoord(sector.p2)+'</div></div>'
      +headingHtml
      +'</div>';
  }
  list.innerHTML=html;
}

function renderCreateButton(){
  var btn=$('create-track-btn');
  if(!btn){return;}
  btn.disabled=!isCreateReady();
}

function renderTrackDraft(){
  renderGpsBar();
  renderStartFinishSection();
  renderSectorRows();
  renderCreateButton();
}

function markStartFinishPoint(which){
  if(!hasValidFix()){
    setTrackMsg('Wait for a valid GPS fix before marking points.','err');
    renderTrackDraft();
    return;
  }
  if(which==='p2'&&!_trackDraft.startFinish.p1){
    setTrackMsg('Mark P1 first.','err');
    renderTrackDraft();
    return;
  }
  _trackDraft.startFinish[which]=currentGpsPoint();
  updateStartFinishHeading();
  setTrackMsg('', '');
  renderTrackDraft();
}

function flipStartFinishHeading(){
  if(!_trackDraft.startFinish.p1||!_trackDraft.startFinish.p2){return;}
  _trackDraft.startFinish.flipped=!_trackDraft.startFinish.flipped;
  updateStartFinishHeading();
  renderTrackDraft();
}

function toggleSectorSection(){
  _trackDraft.sectorsExpanded=!_trackDraft.sectorsExpanded;
  renderSectorRows();
}

function addSectorRow(){
  if(_trackDraft.sectors.length>=3){
    setTrackMsg('You can add up to 3 sector splits.','err');
    renderSectorRows();
    return;
  }
  _trackDraft.sectorsExpanded=true;
  _trackDraft.sectors.push({id:_nextSectorId++,p1:null,p2:null,heading:null,flipped:false});
  setTrackMsg('', '');
  renderSectorRows();
}

function deleteSectorRow(id){
  _trackDraft.sectors=_trackDraft.sectors.filter(function(sector){
    return sector.id!==id;
  });
  if(!_trackDraft.sectors.length){_trackDraft.sectorsExpanded=false;}
  renderSectorRows();
}

function markSectorPoint(id,which){
  var sector=findSector(id);
  if(!sector){return;}
  if(!hasValidFix()){
    setTrackMsg('Wait for a valid GPS fix before marking points.','err');
    renderSectorRows();
    return;
  }
  if(which==='p2'&&!sector.p1){
    setTrackMsg('Mark P1 first for this sector.','err');
    renderSectorRows();
    return;
  }
  sector[which]=currentGpsPoint();
  updateSectorHeading(sector);
  setTrackMsg('', '');
  renderSectorRows();
}

function flipSectorHeading(id){
  var sector=findSector(id);
  if(!sector||!sector.p1||!sector.p2){return;}
  sector.flipped=!sector.flipped;
  updateSectorHeading(sector);
  renderSectorRows();
}

function addTrack(){
  var nameField=$('track-name');
  var name=nameField?nameField.value.trim():'';
  var payload;

  if(!name){
    setTrackMsg('Please enter a track name.','err');
    renderCreateButton();
    return;
  }
  if(!isCreateReady()){
    setTrackMsg('Mark both Start/Finish points before creating the track.','err');
    renderCreateButton();
    return;
  }

  payload={
    name:name,
    sf_lat1:_trackDraft.startFinish.p1.lat,
    sf_lon1:_trackDraft.startFinish.p1.lon,
    sf_lat2:_trackDraft.startFinish.p2.lat,
    sf_lon2:_trackDraft.startFinish.p2.lon,
    sf_heading:_trackDraft.startFinish.heading,
    sectors:_trackDraft.sectors.filter(sectorIsComplete).map(function(sector){
      return {
        lat1:sector.p1.lat,
        lon1:sector.p1.lon,
        lat2:sector.p2.lat,
        lon2:sector.p2.lon,
        heading:sector.heading
      };
    })
  };

  setTrackMsg('Creating track...','');
  fetch('/api/tracks',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(payload)
  }).then(function(response){
    return response.json().then(function(body){
      return {ok:response.ok,body:body};
    },function(){
      return {ok:response.ok,body:{}};
    });
  }).then(function(result){
    if(result.ok&&result.body.ok){
      if(nameField){nameField.value='';}
      resetTrackDraft();
      renderTrackDraft();
      loadTracks();
      setTrackMsg('Track created successfully.','ok');
    }else{
      setTrackMsg((result.body&&result.body.error)?result.body.error:'Failed to create track.','err');
      renderCreateButton();
    }
  }).catch(function(){
    setTrackMsg('Failed to create track.','err');
    renderCreateButton();
  });
}

function initTrackCreationUi(){
  if($('sf-p1-btn')){$('sf-p1-btn').onclick=function(){markStartFinishPoint('p1');};}
  if($('sf-p2-btn')){$('sf-p2-btn').onclick=function(){markStartFinishPoint('p2');};}
  if($('sf-flip')){$('sf-flip').onclick=flipStartFinishHeading;}
  if($('sector-toggle')){$('sector-toggle').onclick=toggleSectorSection;}
  if($('add-sector-btn')){$('add-sector-btn').onclick=addSectorRow;}
  if($('create-track-btn')){$('create-track-btn').onclick=addTrack;}
  if($('track-name')){
    $('track-name').addEventListener('input',function(){
      renderCreateButton();
      if(this.value.trim()){setTrackMsg('', '');}
    });
  }
  renderTrackDraft();
}

function refreshStatus(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    $('gps-fix').textContent=d.gps_fix?'Yes':'No';
    $('gps-fix').className='val '+(d.gps_fix?'ok':'err');
    $('sats').textContent=d.satellites;
    $('rec').textContent=d.recording?'REC':'Idle';
    $('rec').className='val '+(d.recording?'warn':'ok');
    $('lap').textContent=d.current_lap;
    $('best').textContent=d.best_lap_ms>0?(d.best_lap_ms/1000).toFixed(3)+'s':'--';
    $('track').textContent=d.track||'None';
    _trackDraft.gps.fix=!!d.gps_fix;
    _trackDraft.gps.satellites=d.satellites||0;
    _trackDraft.gps.lat=d.lat||0;
    _trackDraft.gps.lon=d.lon||0;
    _isRec=d.recording;
    updateRecBtn();
    renderTrackDraft();
  }).catch(function(){});
}

function loadSessions(){
  fetch('/api/sessions').then(function(r){return r.json();}).then(function(d){
    var ul=$('sessions');
    ul.innerHTML='';
    if(!d.sessions||!d.sessions.length){
      ul.innerHTML='<li>No sessions</li>';
      return;
    }
    d.sessions.forEach(function(s){
      var li=document.createElement('li');
      li.innerHTML='<a href="/files/'+s+'">'+s+'</a>';
      ul.appendChild(li);
    });
  }).catch(function(){});
}

function loadTracks(){
  fetch('/api/tracks').then(function(r){return r.json();}).then(function(d){
    var ul=$('tracks');
    ul.innerHTML='';
    if(!d.tracks||!d.tracks.length){
      ul.innerHTML='<li>No tracks</li>';
      return;
    }
    d.tracks.forEach(function(t){
      var li=document.createElement('li');
      li.style.display='flex';
      li.style.justifyContent='space-between';
      li.style.alignItems='center';
      var sp=document.createElement('span');
      sp.textContent=t.name+' ('+t.id+')';
      var bx=document.createElement('span');
      var sb=document.createElement('button');
      sb.textContent='\u9009\u4e3a\u5f53\u524d';
      sb.style.cssText='padding:4px 8px;margin:0 4px;font-size:0.8em';
      sb.onclick=function(){selectTrack(t.id);};
      var db=document.createElement('button');
      db.textContent='\u5220\u9664';
      db.style.cssText='padding:4px 8px;font-size:0.8em;background:#f44';
      db.onclick=function(){deleteTrack(t.id,t.name);};
      bx.appendChild(sb);
      bx.appendChild(db);
      li.appendChild(sp);
      li.appendChild(bx);
      ul.appendChild(li);
    });
  }).catch(function(){});
}

function loadSettings(){
  fetch('/api/settings').then(function(r){return r.json();}).then(function(d){
    $('s-ssid').value=d.wifi_ssid;
    $('s-pass').value=d.wifi_pass;
    $('s-bright').value=d.brightness;
  }).catch(function(){});
}

function saveSettings(){
  var b={wifi_ssid:$('s-ssid').value,wifi_pass:$('s-pass').value,brightness:+$('s-bright').value};
  fetch('/api/settings',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(b)
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      var m=$('msg');
      m.style.display='block';
      setTimeout(function(){m.style.display='none';},2000);
    }
  }).catch(function(){});
}

function selectTrack(id){
  fetch('/api/tracks/select',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({id:id})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      refreshStatus();
      loadTracks();
    }
  }).catch(function(){});
}

function deleteTrack(id,name){
  if(!confirm('\u786e\u8ba4\u5220\u9664\u8d5b\u9053: '+name+'?')){return;}
  fetch('/api/tracks/delete',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({id:id})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){loadTracks();}
  }).catch(function(){});
}

function toggleRecording(){
  var act=_isRec?'stop':'start';
  fetch('/api/recording',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({action:act})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      _isRec=d.recording;
      updateRecBtn();
    }
  }).catch(function(){});
}

function updateRecBtn(){
  var b=$('rec-btn');
  if(_isRec){
    b.textContent='\u505c\u6b62\u5f55\u5236';
    b.style.background='#f44';
  }else{
    b.textContent='\u5f00\u59cb\u5f55\u5236';
    b.style.background='#0af';
  }
}

initTrackCreationUi();
refreshStatus();
loadSessions();
loadTracks();
loadSettings();
setInterval(refreshStatus,2000);
</script>)JS";
}
