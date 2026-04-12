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
           ".gps-bar.warn{border-left-color:#fa0}"
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
var _trackSubmitPending=false;
var _gpsStabilitySamples=[];
var GPS_STABILITY_MIN_SATS=4;
var GPS_STABILITY_SAMPLE_COUNT=3;
var GPS_STABILITY_MAX_SPREAD_M=2.5;
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
  return !!_trackDraft.gps.fix;
}

function hasIncompleteSectors(){
  return _trackDraft.sectors.some(function(sector){
    return !sectorIsComplete(sector);
  });
}

function isCreateReady(){
  var nameField=$('track-name');
  return !!(nameField&&nameField.value.trim()&&
            _trackDraft.startFinish.p1&&
            _trackDraft.startFinish.p2&&
            typeof _trackDraft.startFinish.heading==='number'&&
            !hasIncompleteSectors());
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

function distanceMeters(p1,p2){
  var radiusM=6371000;
  var lat1=p1.lat*Math.PI/180;
  var lat2=p2.lat*Math.PI/180;
  var dLat=(p2.lat-p1.lat)*Math.PI/180;
  var dLon=(p2.lon-p1.lon)*Math.PI/180;
  var a=Math.sin(dLat/2)*Math.sin(dLat/2)
    +Math.cos(lat1)*Math.cos(lat2)*Math.sin(dLon/2)*Math.sin(dLon/2);
  var c=2*Math.atan2(Math.sqrt(a),Math.sqrt(1-a));
  return radiusM*c;
}

function resetGpsStabilitySamples(){
  _gpsStabilitySamples=[];
}

function pushGpsStabilitySample(point){
  _gpsStabilitySamples.push({lat:point.lat,lon:point.lon});
  if(_gpsStabilitySamples.length>GPS_STABILITY_SAMPLE_COUNT){
    _gpsStabilitySamples.shift();
  }
}

function maxGpsSampleSpreadMeters(){
  var maxSpread=0;
  for(var i=0;i<_gpsStabilitySamples.length;i++){
    for(var j=i+1;j<_gpsStabilitySamples.length;j++){
      maxSpread=Math.max(maxSpread,distanceMeters(_gpsStabilitySamples[i],_gpsStabilitySamples[j]));
    }
  }
  return maxSpread;
}

function updateGpsStabilitySamples(){
  if(!_trackDraft.gps.fix||_trackDraft.gps.satellites<GPS_STABILITY_MIN_SATS){
    resetGpsStabilitySamples();
    return;
  }
  pushGpsStabilitySample(currentGpsPoint());
}

function getGpsStatusInfo(){
  if(!_trackDraft.gps.fix){
    return {state:'no_fix',ready:false,label:'Waiting for fix',barClass:'err',textClass:'err'};
  }
  if(_trackDraft.gps.satellites<GPS_STABILITY_MIN_SATS){
    return {state:'low_sats',ready:false,label:'Waiting for better GPS',barClass:'err',textClass:'err'};
  }
  if(_gpsStabilitySamples.length<GPS_STABILITY_SAMPLE_COUNT){
    return {state:'stabilizing',ready:false,label:'Hold still... stabilizing',barClass:'warn',textClass:'warn'};
  }
  if(maxGpsSampleSpreadMeters()>GPS_STABILITY_MAX_SPREAD_M){
    return {state:'stabilizing',ready:false,label:'Hold still... stabilizing',barClass:'warn',textClass:'warn'};
  }
  return {state:'stable',ready:true,label:'Stable - ready to mark',barClass:'ok',textClass:'ok'};
}

function canMarkWithCurrentGps(){
  return getGpsStatusInfo().ready;
}

function gpsMarkingBlockMessage(){
  var gpsStatus=getGpsStatusInfo();
  if(gpsStatus.state==='no_fix'){
    return 'Wait for a valid GPS fix before marking points.';
  }
  if(gpsStatus.state==='low_sats'){
    return 'Wait for at least '+GPS_STABILITY_MIN_SATS+' satellites before marking points.';
  }
  return 'Hold still until GPS stabilizes before marking points.';
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
  _trackSubmitPending=false;
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

function applyStatusData(d){
  var gpsFix=$('gps-fix');
  var sats=$('sats');
  var rec=$('rec');
  var lap=$('lap');
  var best=$('best');
  var track=$('track');
  var lat=Number(d.lat);
  var lon=Number(d.lon);

  if(gpsFix){
    gpsFix.textContent=d.gps_fix?'Yes':'No';
    gpsFix.className='val '+(d.gps_fix?'ok':'err');
  }
  if(sats){sats.textContent=d.satellites||0;}
  if(rec){
    rec.textContent=d.recording?'REC':'Idle';
    rec.className='val '+(d.recording?'warn':'ok');
  }
  if(lap){lap.textContent=d.current_lap||0;}
  if(best){best.textContent=d.best_lap_ms>0?(d.best_lap_ms/1000).toFixed(3)+'s':'--';}
  if(track){track.textContent=d.track||'None';}

  _trackDraft.gps.fix=!!d.gps_fix;
  _trackDraft.gps.satellites=d.satellites||0;
  _trackDraft.gps.lat=isNaN(lat)?0:lat;
  _trackDraft.gps.lon=isNaN(lon)?0:lon;
  updateGpsStabilitySamples();
  _isRec=!!d.recording;
  updateRecBtn();
}

function fetchStatusSnapshot(){
  return fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    applyStatusData(d);
    return d;
  });
}

function captureCurrentGpsPoint(){
  return fetchStatusSnapshot().then(function(){
    if(!hasValidFix()){
      throw {code:'no_fix'};
    }
    if(!canMarkWithCurrentGps()){
      throw {code:getGpsStatusInfo().state};
    }
    return currentGpsPoint();
  });
}

function storeLinePoint(line,which,point,label){
  var other=which==='p1'?line.p2:line.p1;
  var minSpanM=2;
  if(other&&distanceMeters(point,other)<minSpanM){
    setTrackMsg(label+' points must be at least '+minSpanM+' m apart.','err');
    return false;
  }
  line[which]=point;
  return true;
}

function renderGpsBar(){
  var bar=$('gps-bar');
  var gpsStatus=getGpsStatusInfo();
  if(!bar){return;}
  var fixOk=hasValidFix();
  bar.className='gps-bar '+gpsStatus.barClass;
  bar.innerHTML=
    '<div class="row"><span class="label">GPS</span><span class="val '+gpsStatus.textClass+'">'+gpsStatus.label+'</span></div>'+
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
  var canMark=canMarkWithCurrentGps();
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
    var canMark=canMarkWithCurrentGps();
    var canMarkP2=canMark&&p1Marked;
    var headingHtml='<div class="helper-text">Mark P1 then P2 to calculate heading.</div>';
    if(sectorIsComplete(sector)){
      headingHtml='<div class="heading-bar"><span class="heading-value">'
        +headingArrow(sector.heading)+' '+Math.round(sector.heading)+'° '+compassLabel(sector.heading)
        +'</span><button type="button" class="mark-btn inline-btn" data-sector-action="flip" data-sector-id="'+sector.id+'">Flip</button></div>';
    }
    html+='<div class="sector-card">'
      +'<div class="sector-head"><strong>Sector '+(i+1)+'</strong>'
      +'<button type="button" class="mark-btn inline-btn" data-sector-action="delete" data-sector-id="'+sector.id+'">Delete</button></div>'
      +'<div class="mark-row">'
      +'<button type="button" class="mark-btn'+(p1Marked?' marked':'')+'" data-sector-action="mark-p1" data-sector-id="'+sector.id+'"'+(canMark?'':' disabled')+'>'+(p1Marked?'✓ P1':'Mark P1')+'</button>'
      +'<div class="coord">'+formatCoord(sector.p1)+'</div></div>'
      +'<div class="mark-row">'
      +'<button type="button" class="mark-btn'+(p2Marked?' marked':'')+'" data-sector-action="mark-p2" data-sector-id="'+sector.id+'"'+(canMarkP2?'':' disabled')+'>'+(p2Marked?'✓ P2':'Mark P2')+'</button>'
      +'<div class="coord">'+formatCoord(sector.p2)+'</div></div>'
      +headingHtml
      +'</div>';
  }
  list.innerHTML=html;
}

function renderCreateButton(){
  var btn=$('create-track-btn');
  if(!btn){return;}
  btn.textContent=_trackSubmitPending?'Creating Track...':'Create Track';
  btn.disabled=_trackSubmitPending||!isCreateReady();
}

function renderTrackDraft(){
  renderGpsBar();
  renderStartFinishSection();
  renderSectorRows();
  renderCreateButton();
}

function markStartFinishPoint(which){
  if(which==='p2'&&!_trackDraft.startFinish.p1){
    setTrackMsg('Mark P1 first.','err');
    renderTrackDraft();
    return;
  }
  setTrackMsg('Capturing current GPS sample...','');
  captureCurrentGpsPoint().then(function(point){
    if(!storeLinePoint(_trackDraft.startFinish,which,point,'Start/Finish')){
      renderTrackDraft();
      return;
    }
    updateStartFinishHeading();
    setTrackMsg('', '');
    renderTrackDraft();
  }).catch(function(err){
    if(err&&err.code==='no_fix'){
      setTrackMsg('Wait for a valid GPS fix before marking points.','err');
    }else if(err&&(err.code==='low_sats'||err.code==='stabilizing')){
      setTrackMsg(gpsMarkingBlockMessage(),'err');
    }else{
      setTrackMsg('Failed to capture current GPS sample.','err');
    }
    renderTrackDraft();
  });
}

function flipStartFinishHeading(){
  if(!_trackDraft.startFinish.p1||!_trackDraft.startFinish.p2){return;}
  _trackDraft.startFinish.flipped=!_trackDraft.startFinish.flipped;
  updateStartFinishHeading();
  renderTrackDraft();
}

function toggleSectorSection(){
  _trackDraft.sectorsExpanded=!_trackDraft.sectorsExpanded;
  renderTrackDraft();
}

function addSectorRow(){
  if(_trackDraft.sectors.length>=3){
    setTrackMsg('You can add up to 3 sector splits.','err');
    renderTrackDraft();
    return;
  }
  _trackDraft.sectorsExpanded=true;
  _trackDraft.sectors.push({id:_nextSectorId++,p1:null,p2:null,heading:null,flipped:false});
  setTrackMsg('', '');
  renderTrackDraft();
}

function deleteSectorRow(id){
  _trackDraft.sectors=_trackDraft.sectors.filter(function(sector){
    return sector.id!==id;
  });
  if(!_trackDraft.sectors.length){_trackDraft.sectorsExpanded=false;}
  renderTrackDraft();
}

function markSectorPoint(id,which){
  var sector=findSector(id);
  if(!sector){return;}
  if(which==='p2'&&!sector.p1){
    setTrackMsg('Mark P1 first for this sector.','err');
    renderTrackDraft();
    return;
  }
  setTrackMsg('Capturing current GPS sample...','');
  captureCurrentGpsPoint().then(function(point){
    if(!storeLinePoint(sector,which,point,'Sector')){
      renderTrackDraft();
      return;
    }
    updateSectorHeading(sector);
    setTrackMsg('', '');
    renderTrackDraft();
  }).catch(function(err){
    if(err&&err.code==='no_fix'){
      setTrackMsg('Wait for a valid GPS fix before marking points.','err');
    }else if(err&&(err.code==='low_sats'||err.code==='stabilizing')){
      setTrackMsg(gpsMarkingBlockMessage(),'err');
    }else{
      setTrackMsg('Failed to capture current GPS sample.','err');
    }
    renderTrackDraft();
  });
}

function flipSectorHeading(id){
  var sector=findSector(id);
  if(!sector||!sector.p1||!sector.p2){return;}
  sector.flipped=!sector.flipped;
  updateSectorHeading(sector);
  renderTrackDraft();
}

function addTrack(){
  var nameField=$('track-name');
  var name=nameField?nameField.value.trim():'';
  var payload;

  if(_trackSubmitPending){
    return;
  }
  if(!name){
    setTrackMsg('Please enter a track name.','err');
    renderTrackDraft();
    return;
  }
  if(!_trackDraft.startFinish.p1||
     !_trackDraft.startFinish.p2||
     typeof _trackDraft.startFinish.heading!=='number'){
    setTrackMsg('Mark both Start/Finish points before creating the track.','err');
    renderTrackDraft();
    return;
  }
  if(hasIncompleteSectors()){
    setTrackMsg('Complete or delete every sector split before creating the track.','err');
    renderTrackDraft();
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

  _trackSubmitPending=true;
  renderTrackDraft();
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
    _trackSubmitPending=false;
    if(result.ok&&result.body.ok){
      if(nameField){nameField.value='';}
      resetTrackDraft();
      renderTrackDraft();
      loadTracks();
      setTrackMsg('Track created successfully.','ok');
    }else{
      renderTrackDraft();
      setTrackMsg((result.body&&result.body.error)?result.body.error:'Failed to create track.','err');
    }
  }).catch(function(){
    _trackSubmitPending=false;
    renderTrackDraft();
    setTrackMsg('Failed to create track.','err');
  });
}

function initTrackCreationUi(){
  if($('sf-p1-btn')){$('sf-p1-btn').onclick=function(){markStartFinishPoint('p1');};}
  if($('sf-p2-btn')){$('sf-p2-btn').onclick=function(){markStartFinishPoint('p2');};}
  if($('sf-flip')){$('sf-flip').onclick=flipStartFinishHeading;}
  if($('sector-toggle')){$('sector-toggle').onclick=toggleSectorSection;}
  if($('add-sector-btn')){$('add-sector-btn').onclick=addSectorRow;}
  if($('sector-list')){
    $('sector-list').onclick=function(evt){
      var target=evt&&evt.target;
      var action=target&&target.getAttribute?target.getAttribute('data-sector-action'):'';
      var id=+(target&&target.getAttribute?target.getAttribute('data-sector-id'):0);
      if(!action||!id){return;}
      if(action==='delete'){deleteSectorRow(id);return;}
      if(action==='flip'){flipSectorHeading(id);return;}
      if(action==='mark-p1'){markSectorPoint(id,'p1');return;}
      if(action==='mark-p2'){markSectorPoint(id,'p2');}
    };
  }
  if($('create-track-btn')){$('create-track-btn').onclick=addTrack;}
  if($('track-name')){
    $('track-name').addEventListener('input',function(){
      renderTrackDraft();
      if(this.value.trim()){setTrackMsg('', '');}
    });
  }
  renderTrackDraft();
}

function refreshStatus(){
  fetchStatusSnapshot().then(function(){
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
