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
           ".mark-btn.cancel-btn{background:#45556f;color:#eef4ff}"
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
           ".current-track-panel{background:#0f1f32;border:1px solid #2a3c52;"
             "border-radius:8px;padding:10px;margin:10px 0}"
           ".current-track-head{align-items:center}"
           ".current-track-name{font-size:1.05em;font-weight:bold;color:#e8f3ff;"
             "margin-top:4px}"
           ".track-chip{display:inline-flex;align-items:center;justify-content:center;"
             "border:1px solid #355175;border-radius:999px;padding:2px 8px;font-size:11px;"
             "font-weight:bold;background:#17304d;color:#bcdfff}"
           ".track-chip.current{background:#12391f;border-color:#2faa5a;color:#7ff5a1}"
           ".track-row-name{display:flex;align-items:center;gap:8px;flex-wrap:wrap}"
           ".track-actions{display:flex;align-items:center;gap:4px}"
           ".track-meta{margin-top:6px}"
           ".recording-reason{min-height:1.4em}"
           ".nearby-track-list{display:grid;gap:8px}"
           ".nearby-track-row{display:flex;justify-content:space-between;align-items:center;"
             "gap:8px;border:1px solid #2a3c52;border-radius:8px;padding:8px;"
             "background:#12263d}"
           ".nearby-track-info{display:grid;gap:2px}"
           ".nearby-track-name{font-weight:bold;color:#e8f3ff}"
           ".nearby-track-distance{color:#9eb3ca;font-size:12px}"
           ".creation-steps{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));"
             "gap:8px;margin:8px 0 12px}"
           ".creation-step{border:1px solid #2a3c52;border-radius:999px;padding:8px 10px;"
             "text-align:center;font-size:12px;color:#9eb3ca;background:#10233a}"
           ".creation-step.done{border-color:#2faa5a;color:#7ff5a1;background:#12391f}"
           ".creation-step.active{border-color:#60a5fa;color:#dbeafe;background:#17304d}"
           ".creation-stage{margin-bottom:12px}"
           ".review-panel svg{width:100%;height:auto;display:block}"
           ".review-metrics{display:grid;gap:6px;margin-top:10px}"
           ".review-badge{display:inline-flex;align-items:center;justify-content:center;"
             "width:max-content;max-width:100%;padding:4px 10px;border-radius:999px;"
             "font-size:12px;font-weight:bold;border:1px solid #355175}"
           ".review-badge.high{background:#12391f;border-color:#2faa5a;color:#7ff5a1}"
           ".review-badge.medium{background:#3a2d12;border-color:#f59e0b;color:#fde68a}"
           ".review-badge.low{background:#3b1720;border-color:#ef4444;color:#fecaca}"
           ".review-copy{margin-top:8px}"
           ".sessions-list{display:grid;gap:8px}"
           ".session-card{border:1px solid #2a3c52;border-radius:8px;padding:10px;"
             "background:#0f1f32;display:grid;gap:6px}"
           ".session-meta{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));"
             "gap:8px;font-size:12px;color:#9eb3ca}"
           ".session-meta strong{display:block;color:#dbeafe;font-size:11px;margin-bottom:2px}"
           ".session-actions{display:flex;justify-content:flex-end}"
           ".advanced-toggle{width:100%;text-align:left;background:#0f1f32;color:#dbeafe;"
             "border:1px solid #2a3c52;padding:10px 12px;margin-top:0}"
           ".advanced-panel{margin-top:10px}"
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
           "<div id=\"rec-cta-reason\" class=\"helper-text recording-reason\"></div>"
           "</div>"

           // Sessions card
           "<div class=\"card\">"
           "<h2>Sessions</h2>"
           "<div id=\"sessions\" class=\"sessions-list\"><div class=\"helper-text\">Loading...</div></div>"
           "</div>"

           // Tracks card
           "<div class=\"card\">"
           "<h2>Tracks</h2>"
           "<div class=\"current-track-panel\">"
           "<div class=\"row current-track-head\"><span class=\"label\">Current Track</span>"
             "<span id=\"current-track-lock\" class=\"track-chip\" style=\"display:none\"></span></div>"
           "<div id=\"current-track-name\" class=\"current-track-name\">Loading...</div>"
           "<div id=\"current-track-source\" class=\"helper-text track-meta\">--</div>"
           "<div id=\"current-track-distance\" class=\"helper-text track-meta\"></div>"
           "</div>"
           "<div class=\"current-track-panel\">"
           "<div id=\"nearby-tracks\" class=\"nearby-track-list\">"
             "<div class=\"helper-text\">Nearby tracks unavailable.</div>"
           "</div>"
           "</div>"
           "<ul id=\"tracks\"><li>Loading...</li></ul>"
           "<h2 style=\"margin-top:12px\">Track Creation</h2>"
           "<div id=\"creation-steps\" class=\"creation-steps\">"
           "<div id=\"creation-step-name\" class=\"creation-step\">1. Name</div>"
           "<div id=\"creation-step-start-finish\" class=\"creation-step\">2. Start/Finish</div>"
           "<div id=\"creation-step-review\" class=\"creation-step\">3. Review</div>"
           "</div>"
           "<div id=\"creation-stage-name\" class=\"creation-stage\">"
           "<p class=\"helper-text\">Name</p>"
           "<input id=\"track-name\" placeholder=\"Track name\">"
           "</div>"
           "<div id=\"creation-stage-start-finish\" class=\"creation-stage\">"
           "<div id=\"creation-guidance\" class=\"helper-text\"></div>"
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
           "<button type=\"button\" id=\"sample-cancel-btn\" class=\"mark-btn cancel-btn\" style=\"display:none\">"
             "Cancel Sample</button>"
           "</div>"
           "<div id=\"creation-stage-review\" class=\"creation-stage\">"
           "<div id=\"geometry-review\" class=\"current-track-panel review-panel\"></div>"
           "<div id=\"review-copy\" class=\"helper-text review-copy\"></div>"
           "<button type=\"button\" id=\"repeatability-btn\" class=\"mark-btn\" style=\"display:none\">"
             "Repeatability Check</button>"
           "<div id=\"repeatability-summary\" class=\"helper-text\"></div>"
           "<button type=\"button\" id=\"create-track-btn\" class=\"primary-btn\" disabled>"
             "Create Track</button>"
           "<div id=\"track-create-msg\" class=\"helper-text\"></div>"
           "</div>"
           "</div>"

           // Settings card
           "<div class=\"card\">"
           "<button type=\"button\" id=\"advanced-toggle\" class=\"advanced-toggle\">Advanced</button>"
           "<div id=\"advanced-settings-panel\" class=\"advanced-panel\">"
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
var POINT_SAMPLE_WINDOW_MS=2400;
var POINT_SAMPLE_INTERVAL_MS=400;
var POINT_SAMPLE_MIN_VALID_COUNT=3;
var POINT_SAMPLE_HIGH_SPREAD_M=1.2;
var POINT_SAMPLE_MEDIUM_SPREAD_M=2.5;
var SHORT_LINE_MIN_LENGTH_M=5;
var SHORT_LINE_SPREAD_FACTOR=4;
var REPEATABILITY_CHECK_ENABLED=false;
var _pointSampling=null;
var _trackDraft={
  gps:{fix:false,satellites:0,lat:0,lon:0},
  startFinish:{p1:null,p2:null,heading:null,flipped:false},
  sectors:[],
  sectorsExpanded:false,
  repeatability:makeRepeatabilityState(false)
};
var _statusSnapshot={
  track:'',
  track_id:'',
  track_source:'',
  track_locked_manual:false,
  recording_cta_state:'blocked_no_track',
  recording_cta_reason:'Select a track before recording.',
  current_track_distance_m:-1,
  nearby_tracks:[]
};
var _trackList=[];
var _advancedSettingsOpen=false;
var _statusRefreshTimer=0;
var _statusRefreshInFlight=false;
var _gpsLastUpdateMs=-1;
var _statusRequestSeq=0;

function makeRepeatabilityState(flipped){
  return {
    active:false,
    status:'idle',
    candidate:{p1:null,p2:null,heading:null,flipped:!!flipped},
    result:null
  };
}

function resetRepeatabilityState(){
  _trackDraft.repeatability=makeRepeatabilityState(_trackDraft.startFinish.flipped);
}

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
  var lineInfo=lineConfidenceInfo(_trackDraft.startFinish);
  var nameField=$('track-name');
  return !!(nameField&&nameField.value.trim()&&
            _trackDraft.startFinish.p1&&
            _trackDraft.startFinish.p2&&
            typeof _trackDraft.startFinish.heading==='number'&&
            lineInfo.ready&&
            !_trackDraft.repeatability.active&&
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

function sampleTargetCount(){
  return Math.max(1,Math.floor(POINT_SAMPLE_WINDOW_MS/POINT_SAMPLE_INTERVAL_MS));
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

function sortNumericAsc(a,b){
  return a-b;
}

function medianValue(values){
  var sorted=values.slice().sort(sortNumericAsc);
  var mid=Math.floor(sorted.length/2);
  if(!sorted.length){return 0;}
  if(sorted.length%2){return sorted[mid];}
  return (sorted[mid-1]+sorted[mid])/2;
}

function localOriginPoint(points){
  var lat=0;
  var lon=0;
  points.forEach(function(point){
    lat+=point.lat;
    lon+=point.lon;
  });
  return {
    lat:lat/points.length,
    lon:lon/points.length
  };
}

function lonMetersPerDegree(lat){
  return 111320*Math.cos(lat*Math.PI/180);
}

function projectPointMeters(point,origin){
  var lonScale=lonMetersPerDegree(origin.lat);
  if(Math.abs(lonScale)<0.000001){lonScale=0.000001;}
  return {
    x:(point.lon-origin.lon)*lonScale,
    y:(point.lat-origin.lat)*110540
  };
}

function unprojectPointMeters(point,origin){
  var lonScale=lonMetersPerDegree(origin.lat);
  if(Math.abs(lonScale)<0.000001){lonScale=0.000001;}
  return {
    lat:origin.lat+point.y/110540,
    lon:origin.lon+point.x/lonScale
  };
}

function pointSampleCount(point){
  return typeof point.sampleCount==='number'?point.sampleCount:sampleTargetCount();
}

function pointSpreadMeters(point){
  return typeof point.spreadM==='number'?point.spreadM:0;
}

function pointConfidenceTier(point){
  var sampleCount=pointSampleCount(point);
  var spreadM=pointSpreadMeters(point);
  if(sampleCount>=5&&spreadM<=POINT_SAMPLE_HIGH_SPREAD_M){
    return 'high';
  }
  if(sampleCount>=POINT_SAMPLE_MIN_VALID_COUNT&&spreadM<=POINT_SAMPLE_MEDIUM_SPREAD_M){
    return 'medium';
  }
  return 'low';
}

function pointConfidenceActionLabel(point){
  var tier=typeof point==='string'?point:pointConfidenceTier(point);
  if(tier==='high'){return 'Good - ready to save';}
  if(tier==='medium'){return 'Acceptable - short lines may drift';}
  return 'Noisy - try again';
}

function lineConfidenceInfo(line){
  if(!line||!line.p1||!line.p2){
    return {
      ready:false,
      tier:'low',
      label:'Mark both points to review',
      lineLengthM:0,
      shortThresholdM:SHORT_LINE_MIN_LENGTH_M,
      maxSpreadM:0,
      isShort:false
    };
  }
  var lineLengthM=distanceMeters(line.p1,line.p2);
  var maxSpreadM=Math.max(pointSpreadMeters(line.p1),pointSpreadMeters(line.p2));
  var shortThresholdM=Math.max(SHORT_LINE_MIN_LENGTH_M,SHORT_LINE_SPREAD_FACTOR*maxSpreadM);
  var isShort=lineLengthM<shortThresholdM;
  var p1Tier=pointConfidenceTier(line.p1);
  var p2Tier=pointConfidenceTier(line.p2);
  var tierRank=Math.min(
    p1Tier==='high'?3:(p1Tier==='medium'?2:1),
    p2Tier==='high'?3:(p2Tier==='medium'?2:1)
  );
  if(isShort&&(p1Tier!=='high'||p2Tier!=='high')){
    if(REPEATABILITY_CHECK_ENABLED&&_trackDraft.repeatability.status==='passed'){
      return {
        ready:true,
        tier:'high',
        label:'Good - ready to save',
        lineLengthM:lineLengthM,
        shortThresholdM:shortThresholdM,
        maxSpreadM:maxSpreadM,
        isShort:true
      };
    }
    if(REPEATABILITY_CHECK_ENABLED&&_trackDraft.repeatability.status==='failed'){
      return {
        ready:false,
        tier:'low',
        label:'Repeatability mismatch - remeasure line',
        lineLengthM:lineLengthM,
        shortThresholdM:shortThresholdM,
        maxSpreadM:maxSpreadM,
        isShort:true
      };
    }
    return {
      ready:false,
      tier:'low',
      label:'Short line: higher confidence required',
      lineLengthM:lineLengthM,
      shortThresholdM:shortThresholdM,
      maxSpreadM:maxSpreadM,
      isShort:true
    };
  }
  if(tierRank>=3){
    return {
      ready:true,
      tier:'high',
      label:'Good - ready to save',
      lineLengthM:lineLengthM,
      shortThresholdM:shortThresholdM,
      maxSpreadM:maxSpreadM,
      isShort:isShort
    };
  }
  if(tierRank>=2){
    return {
      ready:true,
      tier:'medium',
      label:'Acceptable - short lines may drift',
      lineLengthM:lineLengthM,
      shortThresholdM:shortThresholdM,
      maxSpreadM:maxSpreadM,
      isShort:isShort
    };
  }
  return {
    ready:false,
    tier:'low',
    label:'Noisy - try again',
    lineLengthM:lineLengthM,
    shortThresholdM:shortThresholdM,
    maxSpreadM:maxSpreadM,
    isShort:isShort
  };
}

function repeatabilityThresholds(line){
  var lineLengthM=distanceMeters(line.p1,line.p2);
  if(lineLengthM<8){
    return {midpointM:1.5,headingDeg:12};
  }
  return {midpointM:3,headingDeg:20};
}

function normalizeAngleDelta(a,b){
  var delta=Math.abs(normalizeHeading(a)-normalizeHeading(b));
  return delta>180?360-delta:delta;
}

function lineMidpoint(line){
  return {
    lat:(line.p1.lat+line.p2.lat)/2,
    lon:(line.p1.lon+line.p2.lon)/2
  };
}

function compareRepeatabilityLines(baseLine,checkLine){
  var thresholds=repeatabilityThresholds(baseLine);
  var midpointDeltaM=distanceMeters(lineMidpoint(baseLine),lineMidpoint(checkLine));
  var headingDeltaDeg=normalizeAngleDelta(baseLine.heading,checkLine.heading);
  return {
    passed:midpointDeltaM<=thresholds.midpointM&&headingDeltaDeg<=thresholds.headingDeg,
    midpointDeltaM:midpointDeltaM,
    headingDeltaDeg:headingDeltaDeg,
    thresholds:thresholds
  };
}

function activeStartFinishLine(){
  if(_trackDraft.repeatability.active){
    return _trackDraft.repeatability.candidate;
  }
  return _trackDraft.startFinish;
}

function isPointSamplingActive(){
  return !!(_pointSampling&&_pointSampling.active);
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

function nowMs(){
  return Date.now?Date.now():0;
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
    return {state:'no_fix',ready:false,label:'Need GPS fix',barClass:'err',textClass:'err'};
  }
  if(_trackDraft.gps.satellites<GPS_STABILITY_MIN_SATS){
    return {state:'low_sats',ready:false,label:'Need better GPS',barClass:'err',textClass:'err'};
  }
  if(_gpsStabilitySamples.length<GPS_STABILITY_SAMPLE_COUNT){
    return {state:'stabilizing',ready:false,label:'Hold still... stabilizing',barClass:'warn',textClass:'warn'};
  }
  if(maxGpsSampleSpreadMeters()>GPS_STABILITY_MAX_SPREAD_M){
    return {state:'stabilizing',ready:false,label:'Hold still... stabilizing',barClass:'warn',textClass:'warn'};
  }
  return {state:'stable',ready:true,label:'Good - ready to mark',barClass:'ok',textClass:'ok'};
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

function isNameReady(){
  var nameField=$('track-name');
  return !!(nameField&&nameField.value.trim());
}

function gpsFreshnessLabel(){
  if(_gpsLastUpdateMs<0){
    return 'Stale - refresh before trusting';
  }
  var ageMs=nowMs()-_gpsLastUpdateMs;
  if(ageMs<1000){
    return 'Live';
  }
  if(ageMs<2000){
    return '1s ago';
  }
  if(ageMs<3000){
    return '2s ago';
  }
  return 'Stale - refresh before trusting';
}

function isTrackCreationPollingActive(){
  return isNameReady()
    || !!_trackDraft.startFinish.p1
    || !!_trackDraft.startFinish.p2
    || _trackDraft.sectors.length>0
    || _trackDraft.sectorsExpanded
    || _trackSubmitPending;
}

function getStatusRefreshIntervalMs(){
  if(isPointSamplingActive()){
    return POINT_SAMPLE_INTERVAL_MS;
  }
  return isTrackCreationPollingActive()?1000:2000;
}

function scheduleStatusRefresh(){
  var intervalMs=getStatusRefreshIntervalMs();
  if(_statusRefreshTimer){
    clearInterval(_statusRefreshTimer);
    _statusRefreshTimer=0;
  }
  _statusRefreshTimer=setInterval(function(){
    if(isPointSamplingActive()&&nowMs()-_pointSampling.startedAtMs>=POINT_SAMPLE_WINDOW_MS){
      return;
    }
    if(_statusRefreshInFlight){return;}
    refreshStatus();
  },intervalMs);
}

function isReviewReady(){
  return isNameReady()
    && _trackDraft.startFinish.p1
    && _trackDraft.startFinish.p2
    && typeof _trackDraft.startFinish.heading==='number'
    && !hasIncompleteSectors();
}

function currentCreationStage(){
  if(!isNameReady()){return 'name';}
  if(!isReviewReady()){return 'start_finish';}
  return 'review';
}

function resetTrackDraft(){
  var gps=_trackDraft.gps;
  _pointSampling=null;
  _trackDraft={
    gps:{fix:gps.fix,satellites:gps.satellites,lat:gps.lat,lon:gps.lon},
    startFinish:{p1:null,p2:null,heading:null,flipped:false},
    sectors:[],
    sectorsExpanded:false,
    repeatability:makeRepeatabilityState(false)
  };
  _nextSectorId=1;
  _trackSubmitPending=false;
}

function hasSelectedTrack(){
  return !!(_statusSnapshot.track&&_statusSnapshot.track!=='No Track');
}

function currentTrackName(){
  return hasSelectedTrack()?_statusSnapshot.track:'No Track selected';
}

function currentTrackSource(){
  if(_statusSnapshot.track_source){return _statusSnapshot.track_source;}
  return hasSelectedTrack()?'Auto-detected':'Waiting to select';
}

function formatDistanceMeters(distanceMeters){
  if(typeof distanceMeters!=='number'||distanceMeters<0){return '';}
  return Math.round(distanceMeters)+' m away';
}

function formatSessionBestLap(bestLapMs){
  var value=Number(bestLapMs);
  return value>0?(value/1000).toFixed(3)+'s':'--';
}

function escapeHtml(value){
  return String(value==null?'':value)
    .replace(/&/g,'&amp;')
    .replace(/</g,'&lt;')
    .replace(/>/g,'&gt;')
    .replace(/"/g,'&quot;')
    .replace(/'/g,'&#39;');
}

function normalizeSessionItem(session){
  if(typeof session==='string'){
    return {filename:session,date:'Unknown date',track:'Unknown track',best_lap_ms:-1};
  }
  session=session||{};
  return {
    filename:session.filename||'session.vbo',
    date:session.date||'Unknown date',
    track:session.track||'Unknown track',
    best_lap_ms:Number(session.best_lap_ms)
  };
}

function normalizeNearbyTracks(list){
  if(!Array.isArray(list)){return [];}
  return list.filter(function(track){
    return !!(track&&track.id&&track.name&&!isNaN(Number(track.distance_m)));
  }).map(function(track){
    return {
      id:track.id,
      name:track.name,
      distance_m:Number(track.distance_m)
    };
  }).sort(function(a,b){
    return a.distance_m-b.distance_m;
  });
}

function currentTrackMatches(track){
  if(!track){return false;}
  if(_statusSnapshot.track_id){
    return track.id===_statusSnapshot.track_id;
  }
  return !!_statusSnapshot.track&&track.name===_statusSnapshot.track;
}

function getRecordingCtaConfig(){
  var state=_statusSnapshot.recording_cta_state||(_isRec?'recording':(hasSelectedTrack()?'ready':'blocked_no_track'));
  if(_isRec||state==='recording'){
    return {label:'Stop Recording',disabled:false,background:'#f44',color:'#fff7ed'};
  }
  if(state==='ready'){
    return {label:'Ready to Record',disabled:false,background:'#0af',color:'#000'};
  }
  if(state==='blocked_no_track'){
    return {label:'Select Track to Record',disabled:true,background:'#334',color:'#889'};
  }
  return {label:'Start Recording',disabled:false,background:'#0af',color:'#000'};
}

function renderCurrentTrackSummary(){
  var name=$('current-track-name');
  var source=$('current-track-source');
  var distance=$('current-track-distance');
  var lock=$('current-track-lock');
  if(name){name.textContent=currentTrackName();}
  if(source){source.textContent=currentTrackSource();}
  if(distance){
    distance.textContent=hasSelectedTrack()
      ? formatDistanceMeters(_statusSnapshot.current_track_distance_m)
      : '';
  }
  if(lock){
    if(_statusSnapshot.track_locked_manual){
      lock.textContent=_statusSnapshot.track_source==='Newly created'?'New':'Manual';
      lock.style.display='inline-flex';
    }else{
      lock.textContent='';
      lock.style.display='none';
    }
  }
}

function renderNearbyTrackChooser(){
  var nearby=$('nearby-tracks');
  if(!nearby){return;}
  var html='<div class="row current-track-head"><span class="label">Nearby Tracks</span>';
  if(_statusSnapshot.track_locked_manual&&!_isRec&&_trackDraft.gps.fix){
    html+='<button type="button" class="mark-btn inline-btn" data-nearby-action="auto">Resume Auto</button>';
  }
  html+='</div>';

  if(!_statusSnapshot.nearby_tracks.length){
    html+='<div class="helper-text">No nearby alternatives right now.</div>';
    nearby.innerHTML=html;
    return;
  }

  for(var i=0;i<_statusSnapshot.nearby_tracks.length;i++){
    var candidate=_statusSnapshot.nearby_tracks[i];
    html+='<div class="nearby-track-row">'
      +'<div class="nearby-track-info"><span class="nearby-track-name">'+candidate.name+'</span>'
      +'<span class="nearby-track-distance">'+Math.round(candidate.distance_m)+' m away</span></div>'
      +'<button type="button" class="mark-btn inline-btn" data-nearby-action="select" data-track-id="'+candidate.id+'">Use This Track</button>'
      +'</div>';
  }
  nearby.innerHTML=html;
}

function renderTracksList(){
  var ul=$('tracks');
  if(!ul){return;}
  ul.innerHTML='';
  if(!_trackList.length){
    ul.innerHTML='<li>No tracks</li>';
    return;
  }
  _trackList.forEach(function(t){
    var li=document.createElement('li');
    li.style.display='flex';
    li.style.justifyContent='space-between';
    li.style.alignItems='center';
    li.style.gap='8px';
    var nameWrap=document.createElement('span');
    nameWrap.className='track-row-name';
    var nameText=document.createElement('span');
    nameText.textContent=t.name+' ('+t.id+')';
    nameWrap.appendChild(nameText);
    if(currentTrackMatches(t)){
      var chip=document.createElement('span');
      chip.className='track-chip current';
      chip.textContent='Current';
      nameWrap.appendChild(chip);
    }
    var bx=document.createElement('span');
    bx.className='track-actions';
    var sb=document.createElement('button');
    sb.textContent='\u9009\u4e3a\u5f53\u524d';
    sb.style.cssText='padding:4px 8px;margin:0 4px;font-size:0.8em';
    sb.onclick=function(){selectTrack(t.id,'manual');};
    var db=document.createElement('button');
    db.textContent='\u5220\u9664';
    db.style.cssText='padding:4px 8px;font-size:0.8em;background:#f44';
    db.onclick=function(){deleteTrack(t.id,t.name);};
    bx.appendChild(sb);
    bx.appendChild(db);
    li.appendChild(nameWrap);
    li.appendChild(bx);
    ul.appendChild(li);
  });
}

function updateLineHeading(line){
  if(line.p1&&line.p2){
    line.heading=crossingHeadingDeg(line.p1,line.p2,line.flipped);
  }else{
    line.heading=null;
  }
}

function updateStartFinishHeading(){
  updateLineHeading(_trackDraft.startFinish);
}

function setCreationStepState(id,label,state){
  var step=$(id);
  if(!step){return;}
  step.textContent=label;
  step.className='creation-step';
  if(state){step.className+=' '+state;}
}

function renderCreationStages(){
  var stage=currentCreationStage();
  var nameStage=$('creation-stage-name');
  var sfStage=$('creation-stage-start-finish');
  var reviewStage=$('creation-stage-review');
  var guidance=$('creation-guidance');
  setCreationStepState('creation-step-name','1. Name',stage==='name'?'active':'done');
  setCreationStepState('creation-step-start-finish','2. Start/Finish',
    stage==='start_finish'?'active':(stage==='review'?'done':''));
  setCreationStepState('creation-step-review','3. Review',stage==='review'?'active':'');
  if(nameStage){nameStage.style.display='block';}
  if(sfStage){sfStage.style.display=isNameReady()?'block':'none';}
  if(reviewStage){reviewStage.style.display=isReviewReady()?'block':'none';}
  if(guidance){
    guidance.textContent=isNameReady()
      ? 'Name the track, then mark start/finish only when GPS is stable.'
      : 'Start by entering a track name.';
  }
}

function reviewLineLengthMeters(line){
  if(!line||!line.p1||!line.p2){return 0;}
  return distanceMeters(line.p1,line.p2);
}

function reviewProjection(lines,width,height,padding){
  var points=[];
  lines.forEach(function(entry){
    points.push(entry.line.p1);
    points.push(entry.line.p2);
  });
  var origin=localOriginPoint(points);
  var bounds={
    minX:Infinity,
    maxX:-Infinity,
    minY:Infinity,
    maxY:-Infinity
  };
  points.forEach(function(point){
    var projected=projectPointMeters(point,origin);
    bounds.minX=Math.min(bounds.minX,projected.x);
    bounds.maxX=Math.max(bounds.maxX,projected.x);
    bounds.minY=Math.min(bounds.minY,projected.y);
    bounds.maxY=Math.max(bounds.maxY,projected.y);
  });
  if(bounds.minX===bounds.maxX){
    bounds.minX-=0.5;
    bounds.maxX+=0.5;
  }
  if(bounds.minY===bounds.maxY){
    bounds.minY-=0.5;
    bounds.maxY+=0.5;
  }
  var innerW=width-padding*2;
  var innerH=height-padding*2;
  var spanX=bounds.maxX-bounds.minX;
  var spanY=bounds.maxY-bounds.minY;
  var scale=Math.min(innerW/spanX,innerH/spanY);
  var usedW=spanX*scale;
  var usedH=spanY*scale;
  return {
    origin:origin,
    bounds:bounds,
    width:width,
    height:height,
    padding:padding,
    scale:scale,
    ox:padding+(innerW-usedW)/2,
    oy:padding+(innerH-usedH)/2
  };
}

function projectReviewPoint(point,projection){
  var local=projectPointMeters(point,projection.origin);
  return {
    x: projection.ox+(local.x-projection.bounds.minX)*projection.scale,
    y: projection.height-projection.oy-(local.y-projection.bounds.minY)*projection.scale
  };
}

function reviewScaleMeters(projection){
  var candidates=[1,2,5,10,20,50,100,200];
  var maxMeters=(projection.width-projection.padding*2)*0.32/projection.scale;
  var chosen=candidates[0];
  for(var i=0;i<candidates.length;i++){
    if(candidates[i]<=maxMeters){
      chosen=candidates[i];
    }
  }
  return chosen;
}

function reviewMetricHtml(label,value){
  return '<div class="helper-text"><strong>'+label+':</strong> '+value+'</div>';
}

function reviewConfidenceBadgeHtml(lineInfo){
  return '<div class="review-badge '+lineInfo.tier+'">'+lineInfo.label+'</div>';
}

function renderGeometryReview(){
  var panel=$('geometry-review');
  var copy=$('review-copy');
  if(!panel||!copy){return;}
  copy.textContent='A new track becomes current immediately after creation. Do not close or refresh this page during track creation.';
  if(!isReviewReady()){
    panel.innerHTML='<div class="helper-text">Complete the name and geometry to unlock review.</div>';
    return;
  }

  var lineInfo=lineConfidenceInfo(_trackDraft.startFinish);
  var lines=[{label:'Start/Finish',line:_trackDraft.startFinish,color:'#38bdf8'}];
  _trackDraft.sectors.filter(sectorIsComplete).forEach(function(sector,index){
    lines.push({label:'S'+(index+1),line:sector,color:'#f59e0b'});
  });

  var width=240;
  var height=160;
  var padding=18;
  var projection=reviewProjection(lines,width,height,padding);
  var scaleMeters=reviewScaleMeters(projection);
  var scalePixels=scaleMeters*projection.scale;
  var svg='<svg viewBox="0 0 '+width+' '+height+'" aria-label="Track geometry review">';
  svg+='<rect x="0" y="0" width="'+width+'" height="'+height+'" rx="14" fill="#0b1827" stroke="#2a3c52"></rect>';
  svg+='<g class="review-north">';
  svg+='<line x1="'+(width-24)+'" y1="42" x2="'+(width-24)+'" y2="20" stroke="#dbeafe" stroke-width="2.5" stroke-linecap="round"></line>';
  svg+='<polygon points="'+(width-24)+',14 '+(width-29)+',24 '+(width-19)+',24" fill="#dbeafe"></polygon>';
  svg+='<text x="'+(width-24)+'" y="56" fill="#dbeafe" font-size="10" text-anchor="middle">North</text>';
  svg+='</g>';
  lines.forEach(function(entry){
    var start=projectReviewPoint(entry.line.p1,projection);
    var end=projectReviewPoint(entry.line.p2,projection);
    var startRadius=Math.max(6,pointSpreadMeters(entry.line.p1)*projection.scale);
    var endRadius=Math.max(6,pointSpreadMeters(entry.line.p2)*projection.scale);
    svg+='<circle class="review-uncertainty" cx="'+start.x.toFixed(1)+'" cy="'+start.y.toFixed(1)+'" r="'+startRadius.toFixed(1)+'" fill="'+entry.color+'" fill-opacity="0.16" stroke="'+entry.color+'" stroke-opacity="0.35"></circle>';
    svg+='<circle class="review-uncertainty" cx="'+end.x.toFixed(1)+'" cy="'+end.y.toFixed(1)+'" r="'+endRadius.toFixed(1)+'" fill="'+entry.color+'" fill-opacity="0.16" stroke="'+entry.color+'" stroke-opacity="0.35"></circle>';
    svg+='<line data-review-label="'+entry.label+'" x1="'+start.x.toFixed(1)+'" y1="'+start.y.toFixed(1)+'" x2="'+end.x.toFixed(1)+'" y2="'+end.y.toFixed(1)+'" stroke="'+entry.color+'" stroke-width="4" stroke-linecap="round"></line>';
    svg+='<circle cx="'+start.x.toFixed(1)+'" cy="'+start.y.toFixed(1)+'" r="4" fill="'+entry.color+'"></circle>';
    svg+='<circle cx="'+end.x.toFixed(1)+'" cy="'+end.y.toFixed(1)+'" r="4" fill="'+entry.color+'"></circle>';
    svg+='<text x="'+((start.x+end.x)/2).toFixed(1)+'" y="'+((start.y+end.y)/2-8).toFixed(1)+'" fill="#dbeafe" font-size="11" text-anchor="middle">'+entry.label+'</text>';
  });
  svg+='<line class="review-scale-bar" x1="'+padding+'" y1="'+(height-padding)+'" x2="'+(padding+scalePixels).toFixed(1)+'" y2="'+(height-padding)+'" stroke="#dbeafe" stroke-width="3" stroke-linecap="round"></line>';
  svg+='<text x="'+(padding+scalePixels/2).toFixed(1)+'" y="'+(height-padding-8)+'" fill="#dbeafe" font-size="10" text-anchor="middle">Scale '+scaleMeters+' m</text>';
  svg+='</svg>';

  var summary='<div class="review-metrics">'
    +reviewConfidenceBadgeHtml(lineInfo)
    +reviewMetricHtml('Line length',lineInfo.lineLengthM.toFixed(1)+' m')
    +reviewMetricHtml('Crossing',headingArrow(_trackDraft.startFinish.heading)+' '+Math.round(_trackDraft.startFinish.heading)+'° '+compassLabel(_trackDraft.startFinish.heading))
    +reviewMetricHtml('Scale',scaleMeters+' m')
    +'</div>';
  var warning='';
  if(lineInfo.isShort&&!lineInfo.ready){
    warning='<div class="helper-text track-msg-err">'+lineInfo.label+'. Re-mark with a longer line or steadier GPS.</div>';
  }
  panel.innerHTML=svg+summary+warning;
}

function findSector(id){
  for(var i=0;i<_trackDraft.sectors.length;i++){
    if(_trackDraft.sectors[i].id===id){return _trackDraft.sectors[i];}
  }
  return null;
}

function updateSectorHeading(sector){
  updateLineHeading(sector);
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
  _gpsLastUpdateMs=nowMs();
  updateGpsStabilitySamples();
  captureSamplingStatus(d);
  _isRec=!!d.recording;
  _statusSnapshot.track=d.track||'';
  _statusSnapshot.track_id=d.track_id||'';
  _statusSnapshot.track_source=d.track_source||(d.track&&d.track!=='No Track'?'Auto-detected':'Waiting to select');
  _statusSnapshot.track_locked_manual=!!d.track_locked_manual;
  _statusSnapshot.recording_cta_state=d.recording_cta_state||(d.recording?'recording':(d.track&&d.track!=='No Track'?'ready':'blocked_no_track'));
  _statusSnapshot.recording_cta_reason=d.recording_cta_reason||(_statusSnapshot.recording_cta_state==='blocked_no_track'?'Select a track before recording.':'');
  _statusSnapshot.current_track_distance_m=Number(d.current_track_distance_m);
  if(isNaN(_statusSnapshot.current_track_distance_m)){
    _statusSnapshot.current_track_distance_m=-1;
  }
  _statusSnapshot.nearby_tracks=normalizeNearbyTracks(d.nearby_tracks);
  renderCurrentTrackSummary();
  renderNearbyTrackChooser();
  renderTracksList();
  updateRecBtn();
}

function fetchStatusSnapshot(){
  var requestId=++_statusRequestSeq;
  var requestedAtMs=nowMs();
  return fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    d.__requestId=requestId;
    d.__requestedAtMs=requestedAtMs;
    applyStatusData(d);
    return d;
  });
}

function pointCaptureSummary(point){
  var spreadText=point.spreadM.toFixed(1)+' m';
  return 'Sampled '+point.sampleCount+' fixes, spread '+spreadText+'. '+pointConfidenceActionLabel(point)+'.';
}

function sampledPointFromSession(session){
  var origin=localOriginPoint(session.samples);
  var projected=session.samples.map(function(sample){
    return projectPointMeters(sample,origin);
  });
  var aggregated=unprojectPointMeters({
    x:medianValue(projected.map(function(sample){return sample.x;})),
    y:medianValue(projected.map(function(sample){return sample.y;}))
  },origin);
  var point={
    lat:aggregated.lat,
    lon:aggregated.lon
  };
  var spreadM=0;
  session.samples.forEach(function(sample){
    spreadM=Math.max(spreadM,distanceMeters(point,sample));
  });
  point.sampleCount=session.samples.length;
  point.spreadM=spreadM;
  point.capturedAtMs=nowMs();
  point.captureAgeMs=Math.max(0,nowMs()-session.latestSampleAtMs);
  point.confidence=pointConfidenceTier(point);
  point.captureSummary=pointCaptureSummary(point);
  return point;
}

function clearPointSamplingSession(){
  var session=_pointSampling;
  if(!session){return null;}
  if(session.finalizeTimer){clearTimeout(session.finalizeTimer);}
  _pointSampling=null;
  scheduleStatusRefresh();
  return session;
}

function rejectPointSampling(err){
  var session=clearPointSamplingSession();
  if(!session){return;}
  session.reject(err);
}

function resolvePointSampling(point){
  var session=clearPointSamplingSession();
  if(!session){return;}
  session.resolve(point);
}

function samplingProgressMessage(){
  if(!isPointSamplingActive()){return '';}
  return 'Sampling point... '+_pointSampling.samples.length+'/'+_pointSampling.targetSamples+' fixes collected. Hold still.';
}

function updateSamplingProgressMessage(){
  if(!isPointSamplingActive()){return;}
  setTrackMsg(samplingProgressMessage(),'');
}

function captureCurrentGpsPoint(meta){
  return new Promise(function(resolve,reject){
    if(isPointSamplingActive()){
      reject({code:'busy'});
      return;
    }
    if(!hasValidFix()){
      reject({code:'no_fix'});
      return;
    }
    if(!canMarkWithCurrentGps()){
      reject({code:getGpsStatusInfo().state});
      return;
    }
    _pointSampling={
      active:true,
      startedAtMs:nowMs(),
      samples:[],
      latestSampleAtMs:nowMs(),
      targetSamples:sampleTargetCount(),
      minFreshRequestId:_statusRequestSeq+1,
      awaitingFreshRequest:_statusRefreshInFlight,
      meta:meta||{},
      resolve:resolve,
      reject:reject,
      finalizeTimer:setTimeout(function(){
        if(!isPointSamplingActive()){return;}
        if(_pointSampling.samples.length<POINT_SAMPLE_MIN_VALID_COUNT){
          rejectPointSampling({
            code:'too_few_samples',
            sampleCount:_pointSampling.samples.length
          });
          return;
        }
        resolvePointSampling(sampledPointFromSession(_pointSampling));
      },POINT_SAMPLE_WINDOW_MS)
    };
    updateSamplingProgressMessage();
    renderTrackDraft();
    if(!_statusRefreshInFlight){
      refreshStatus();
    }
  });
}

function cancelPointSampling(){
  if(!isPointSamplingActive()){return;}
  rejectPointSampling({code:'canceled'});
}

function captureSamplingStatus(status){
  if(!isPointSamplingActive()){return;}
  var requestId=Number(status&&status.__requestId);
  if(isNaN(requestId)||requestId<_pointSampling.minFreshRequestId){
    return;
  }
  if(!status||!status.gps_fix||(status.satellites||0)<GPS_STABILITY_MIN_SATS){
    rejectPointSampling({code:'gps_lost'});
    return;
  }
  var lat=Number(status.lat);
  var lon=Number(status.lon);
  if(isNaN(lat)||isNaN(lon)){
    rejectPointSampling({code:'gps_lost'});
    return;
  }
  if(_pointSampling.samples.length>=_pointSampling.targetSamples){
    return;
  }
  _pointSampling.samples.push({lat:lat,lon:lon});
  _pointSampling.latestSampleAtMs=nowMs();
  updateSamplingProgressMessage();
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
  var samplingHtml=isPointSamplingActive()
    ? '<div class="helper-text">'+samplingProgressMessage()+'</div>'
    : '';
  bar.className='gps-bar '+gpsStatus.barClass;
  bar.innerHTML=
    '<div class="row"><span class="label">GPS</span><span class="val '+gpsStatus.textClass+'">'+gpsStatus.label+'</span></div>'+
    '<div class="row"><span class="label">Satellites</span><span class="val">'+_trackDraft.gps.satellites+'</span></div>'+
    '<div class="helper-text">Current position: '+(fixOk?formatCoord(currentGpsPoint()):'--')+'</div>'+
    '<div class="helper-text">Freshness: '+gpsFreshnessLabel()+'</div>'+
    samplingHtml;
}

function renderStartFinishSection(){
  var p1Btn=$('sf-p1-btn');
  var p2Btn=$('sf-p2-btn');
  var p1Coord=$('sf-p1-coord');
  var p2Coord=$('sf-p2-coord');
  var headingBar=$('sf-heading');
  var headingText=$('sf-heading-text');
  var flipBtn=$('sf-flip');
  var cancelBtn=$('sample-cancel-btn');
  var targetLine=activeStartFinishLine();
  var p1Marked=!!targetLine.p1;
  var p2Marked=!!targetLine.p2;
  var canMark=canMarkWithCurrentGps();
  var showHeading=p1Marked&&p2Marked&&typeof targetLine.heading==='number'&&!_trackDraft.repeatability.active;
  var sampling=isPointSamplingActive();
  var samplingP1=sampling&&_pointSampling.meta.scope==='start_finish'&&_pointSampling.meta.which==='p1';
  var samplingP2=sampling&&_pointSampling.meta.scope==='start_finish'&&_pointSampling.meta.which==='p2';
  var labelPrefix=_trackDraft.repeatability.active?'Check ':'';

  if(p1Btn){
    p1Btn.textContent=samplingP1?'Sampling P1...':(p1Marked?'Re-mark P1':labelPrefix+'P1');
    p1Btn.className='mark-btn'+(p1Marked?' marked':'');
    p1Btn.disabled=sampling||!canMark;
  }
  if(p2Btn){
    p2Btn.textContent=samplingP2?'Sampling P2...':(p2Marked?'Re-mark P2':labelPrefix+'P2');
    p2Btn.className='mark-btn'+(p2Marked?' marked':'');
    p2Btn.disabled=sampling||!canMark||!p1Marked;
  }
  if(p1Coord){p1Coord.textContent=formatCoord(targetLine.p1);}
  if(p2Coord){p2Coord.textContent=formatCoord(targetLine.p2);}
  if(headingBar){headingBar.style.display=showHeading?'flex':'none';}
  if(headingText&&showHeading){
    headingText.textContent=headingArrow(targetLine.heading)+' '
      +Math.round(targetLine.heading)+'° '
      +compassLabel(targetLine.heading);
  }
  if(flipBtn){flipBtn.disabled=!showHeading;}
  if(cancelBtn){cancelBtn.style.display=sampling?'block':'none';}
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
    var sampling=isPointSamplingActive();
    var samplingP1=sampling&&_pointSampling.meta.scope==='sector'&&_pointSampling.meta.id===sector.id&&_pointSampling.meta.which==='p1';
    var samplingP2=sampling&&_pointSampling.meta.scope==='sector'&&_pointSampling.meta.id===sector.id&&_pointSampling.meta.which==='p2';
    var canMarkP2=canMark&&p1Marked&&!sampling;
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
      +'<button type="button" class="mark-btn'+(p1Marked?' marked':'')+'" data-sector-action="mark-p1" data-sector-id="'+sector.id+'"'+((canMark&&!sampling)?'':' disabled')+'>'+(samplingP1?'Sampling P1...':(p1Marked?'✓ P1':'Mark P1'))+'</button>'
      +'<div class="coord">'+formatCoord(sector.p1)+'</div></div>'
      +'<div class="mark-row">'
      +'<button type="button" class="mark-btn'+(p2Marked?' marked':'')+'" data-sector-action="mark-p2" data-sector-id="'+sector.id+'"'+(canMarkP2?'':' disabled')+'>'+(samplingP2?'Sampling P2...':(p2Marked?'✓ P2':'Mark P2'))+'</button>'
      +'<div class="coord">'+formatCoord(sector.p2)+'</div></div>'
      +headingHtml
      +'</div>';
  }
  list.innerHTML=html;
}

function renderRepeatabilitySection(){
  var btn=$('repeatability-btn');
  var summary=$('repeatability-summary');
  var lineInfo=lineConfidenceInfo(_trackDraft.startFinish);
  if(btn){
    btn.style.display=(REPEATABILITY_CHECK_ENABLED&&isReviewReady())?'block':'none';
    btn.textContent=_trackDraft.repeatability.active
      ? 'Repeatability Check In Progress'
      : (_trackDraft.repeatability.status==='passed'?'Repeatability Confirmed':'Repeatability Check');
    btn.disabled=!REPEATABILITY_CHECK_ENABLED||!isReviewReady()||_trackDraft.repeatability.active;
  }
  if(summary){
    if(!REPEATABILITY_CHECK_ENABLED){
      summary.textContent='';
    }else if(_trackDraft.repeatability.active){
      summary.textContent='Repeatability check active: re-mark P1 then P2 for the same line.';
    }else if(_trackDraft.repeatability.status==='passed'&&_trackDraft.repeatability.result){
      summary.textContent='Repeatability passed. Midpoint delta '
        +_trackDraft.repeatability.result.midpointDeltaM.toFixed(1)+' m, heading delta '
        +_trackDraft.repeatability.result.headingDeltaDeg.toFixed(1)+'°.';
    }else if(_trackDraft.repeatability.status==='failed'&&_trackDraft.repeatability.result){
      summary.textContent='Repeatability mismatch - remeasure line. Midpoint delta '
        +_trackDraft.repeatability.result.midpointDeltaM.toFixed(1)+' m, heading delta '
        +_trackDraft.repeatability.result.headingDeltaDeg.toFixed(1)+'°.';
    }else if(lineInfo.isShort&&!lineInfo.ready){
      summary.textContent='Short lines benefit from an optional repeatability check before saving.';
    }else{
      summary.textContent='Optional: confirm the line again if you want extra confidence.';
    }
  }
}

function renderCreateButton(){
  var btn=$('create-track-btn');
  if(!btn){return;}
  btn.textContent=_trackSubmitPending?'Creating Track...':'Create Track';
  btn.disabled=_trackSubmitPending||!isCreateReady();
}

function renderTrackDraft(){
  renderCreationStages();
  renderGpsBar();
  renderStartFinishSection();
  renderSectorRows();
  renderGeometryReview();
  renderRepeatabilitySection();
  renderCreateButton();
  scheduleStatusRefresh();
}

function renderAdvancedSettings(){
  var panel=$('advanced-settings-panel');
  var toggle=$('advanced-toggle');
  if(panel){
    panel.style.display=_advancedSettingsOpen?'block':'none';
  }
  if(toggle){
    toggle.textContent=_advancedSettingsOpen?'Hide Advanced':'Advanced';
  }
}

function markStartFinishPoint(which){
  var targetLine=activeStartFinishLine();
  var lineLabel=_trackDraft.repeatability.active?'Repeatability':'Start/Finish';
  if(isPointSamplingActive()){
    setTrackMsg('Finish the current sample or cancel it first.','err');
    renderTrackDraft();
    return;
  }
  if(which==='p2'&&!targetLine.p1){
    setTrackMsg('Mark P1 first.','err');
    renderTrackDraft();
    return;
  }
  setTrackMsg('Sampling point...','');
  captureCurrentGpsPoint({
    scope:'start_finish',
    which:which
  }).then(function(point){
    if(!storeLinePoint(targetLine,which,point,lineLabel)){
      renderTrackDraft();
      return;
    }
    updateLineHeading(targetLine);
    if(_trackDraft.repeatability.active&&targetLine.p1&&targetLine.p2&&typeof targetLine.heading==='number'){
      finalizeRepeatabilityCheck();
      return;
    }
    if(!_trackDraft.repeatability.active){
      resetRepeatabilityState();
    }
    setTrackMsg(point.captureSummary,point.confidence==='high'?'ok':(point.confidence==='low'?'err':''));
    renderTrackDraft();
  }).catch(function(err){
    if(err&&err.code==='no_fix'){
      setTrackMsg('Wait for a valid GPS fix before marking points.','err');
    }else if(err&&(err.code==='low_sats'||err.code==='stabilizing')){
      setTrackMsg(gpsMarkingBlockMessage(),'err');
    }else if(err&&err.code==='busy'){
      setTrackMsg('Finish the current sample or cancel it first.','err');
    }else if(err&&err.code==='gps_lost'){
      setTrackMsg('GPS dropped during sampling. Move to a clearer spot and retry.','err');
    }else if(err&&err.code==='too_few_samples'){
      setTrackMsg('Need at least 3 good fixes during sampling. Hold still and retry.','err');
    }else if(err&&err.code==='canceled'){
      setTrackMsg('Sampling canceled. No point stored.','');
    }else{
      setTrackMsg('Failed to sample the current GPS point.','err');
    }
    renderTrackDraft();
  });
}

function flipStartFinishHeading(){
  if(!_trackDraft.startFinish.p1||!_trackDraft.startFinish.p2){return;}
  _trackDraft.startFinish.flipped=!_trackDraft.startFinish.flipped;
  updateStartFinishHeading();
  resetRepeatabilityState();
  renderTrackDraft();
}

function toggleSectorSection(){
  _trackDraft.sectorsExpanded=!_trackDraft.sectorsExpanded;
  renderTrackDraft();
}

function startRepeatabilityCheck(){
  if(!REPEATABILITY_CHECK_ENABLED||!isReviewReady()){
    return;
  }
  _trackDraft.repeatability=makeRepeatabilityState(_trackDraft.startFinish.flipped);
  _trackDraft.repeatability.active=true;
  _trackDraft.repeatability.status='measuring';
  setTrackMsg('Repeatability check: re-mark P1 then P2 for the same line.','');
  renderTrackDraft();
}

function finalizeRepeatabilityCheck(){
  var result=compareRepeatabilityLines(_trackDraft.startFinish,_trackDraft.repeatability.candidate);
  _trackDraft.repeatability.active=false;
  _trackDraft.repeatability.status=result.passed?'passed':'failed';
  _trackDraft.repeatability.result=result;
  if(result.passed){
    setTrackMsg('Repeatability check passed. Midpoint delta '
      +result.midpointDeltaM.toFixed(1)+' m, heading delta '
      +result.headingDeltaDeg.toFixed(1)+'°.','ok');
  }else{
    setTrackMsg('Repeatability mismatch - remeasure line. Midpoint delta '
      +result.midpointDeltaM.toFixed(1)+' m, heading delta '
      +result.headingDeltaDeg.toFixed(1)+'°.','err');
  }
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
  if(isPointSamplingActive()){
    setTrackMsg('Finish the current sample or cancel it first.','err');
    renderTrackDraft();
    return;
  }
  if(which==='p2'&&!sector.p1){
    setTrackMsg('Mark P1 first for this sector.','err');
    renderTrackDraft();
    return;
  }
  setTrackMsg('Sampling point...','');
  captureCurrentGpsPoint({
    scope:'sector',
    id:id,
    which:which
  }).then(function(point){
    if(!storeLinePoint(sector,which,point,'Sector')){
      renderTrackDraft();
      return;
    }
    updateSectorHeading(sector);
    setTrackMsg(point.captureSummary,point.confidence==='high'?'ok':(point.confidence==='low'?'err':''));
    renderTrackDraft();
  }).catch(function(err){
    if(err&&err.code==='no_fix'){
      setTrackMsg('Wait for a valid GPS fix before marking points.','err');
    }else if(err&&(err.code==='low_sats'||err.code==='stabilizing')){
      setTrackMsg(gpsMarkingBlockMessage(),'err');
    }else if(err&&err.code==='busy'){
      setTrackMsg('Finish the current sample or cancel it first.','err');
    }else if(err&&err.code==='gps_lost'){
      setTrackMsg('GPS dropped during sampling. Move to a clearer spot and retry.','err');
    }else if(err&&err.code==='too_few_samples'){
      setTrackMsg('Need at least 3 good fixes during sampling. Hold still and retry.','err');
    }else if(err&&err.code==='canceled'){
      setTrackMsg('Sampling canceled. No point stored.','');
    }else{
      setTrackMsg('Failed to sample the current GPS point.','err');
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
      var createdId=result.body&&result.body.id;
      if(nameField){nameField.value='';}
      resetTrackDraft();
      renderTrackDraft();
      loadTracks();
      if(!createdId){
        setTrackMsg('Track created, but failed to select it automatically.','err');
        return;
      }
      selectTrack(createdId,'newly_created').then(function(selected){
        if(selected){
          setTrackMsg('Track created and selected.','ok');
        }else{
          setTrackMsg('Track created, but failed to select it automatically.','err');
        }
      });
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
  if($('sample-cancel-btn')){$('sample-cancel-btn').onclick=cancelPointSampling;}
  if($('repeatability-btn')){$('repeatability-btn').onclick=startRepeatabilityCheck;}
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
  if($('nearby-tracks')){
    $('nearby-tracks').onclick=function(evt){
      var target=evt&&evt.target;
      var action=target&&target.getAttribute?target.getAttribute('data-nearby-action'):'';
      var id=target&&target.getAttribute?target.getAttribute('data-track-id'):'';
      if(action==='select'&&id){
        selectTrack(id,'manual');
        return;
      }
      if(action==='auto'){
        selectTrack('', 'auto');
      }
    };
  }
  if($('track-name')){
    $('track-name').addEventListener('input',function(){
      renderTrackDraft();
      if(this.value.trim()){setTrackMsg('', '');}
    });
  }
  renderTrackDraft();
}

function refreshStatus(){
  if(_statusRefreshInFlight){return;}
  _statusRefreshInFlight=true;
  fetchStatusSnapshot().then(function(){
    renderTrackDraft();
  }).catch(function(){})
    .then(function(){
      _statusRefreshInFlight=false;
      if(isPointSamplingActive()&&_pointSampling.awaitingFreshRequest){
        _pointSampling.awaitingFreshRequest=false;
        refreshStatus();
      }
    });
}

function loadSessions(){
  fetch('/api/sessions').then(function(r){return r.json();}).then(function(d){
    var ul=$('sessions');
    ul.innerHTML='';
    if(!d.sessions||!d.sessions.length){
      ul.innerHTML='<div class="helper-text">No sessions</div>';
      return;
    }
    d.sessions.forEach(function(rawSession){
      var session=normalizeSessionItem(rawSession);
      var card=document.createElement('div');
      card.className='session-card';
      var safeFileName=escapeHtml(session.filename);
      card.innerHTML=
        '<div class="session-meta">'
        +'<div><strong>Date</strong>'+escapeHtml(session.date)+'</div>'
        +'<div><strong>Track</strong>'+escapeHtml(session.track)+'</div>'
        +'<div><strong>Best Lap</strong>'+escapeHtml(formatSessionBestLap(session.best_lap_ms))+'</div>'
        +'</div>'
        +'<div class="session-actions"><a href="/files/'+encodeURIComponent(session.filename)+'">Download '
        +safeFileName+'</a></div>';
      ul.appendChild(card);
    });
  }).catch(function(){});
}

function loadTracks(){
  fetch('/api/tracks').then(function(r){return r.json();}).then(function(d){
    _trackList=d.tracks||[];
    renderTracksList();
  }).catch(function(){
    _trackList=[];
    renderTracksList();
  });
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

function toggleAdvancedSettings(){
  _advancedSettingsOpen=!_advancedSettingsOpen;
  renderAdvancedSettings();
}

function selectTrack(id,source){
  var payload={};
  if(id){payload.id=id;}
  if(source){payload.source=source;}
  return fetch('/api/tracks/select',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(payload)
  }).then(function(r){
    return r.json().then(function(body){
      return {ok:r.ok,body:body};
    },function(){
      return {ok:r.ok,body:{}};
    });
  }).then(function(result){
    if(result.ok&&result.body.ok){
      refreshStatus();
      return true;
    }
    return false;
  }).catch(function(){return false;});
}

function deleteTrack(id,name){
  if(!confirm('\u786e\u8ba4\u5220\u9664\u8d5b\u9053: '+name+'?')){return;}
  fetch('/api/tracks/delete',{
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

function toggleRecording(){
  var config=getRecordingCtaConfig();
  if(config.disabled){return;}
  var act=_isRec?'stop':'start';
  fetch('/api/recording',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({action:act})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      _isRec=d.recording;
      _statusSnapshot.recording_cta_state=d.recording?'recording':_statusSnapshot.recording_cta_state;
      updateRecBtn();
      refreshStatus();
    }
  }).catch(function(){});
}

function updateRecBtn(){
  var b=$('rec-btn');
  var reason=$('rec-cta-reason');
  var config=getRecordingCtaConfig();
  if(!b){return;}
  b.textContent=config.label;
  b.disabled=!!config.disabled;
  b.style.background=config.background;
  b.style.color=config.color;
  if(reason){reason.textContent=_statusSnapshot.recording_cta_reason||'';}
}

initTrackCreationUi();
renderAdvancedSettings();
refreshStatus();
loadSessions();
loadTracks();
loadSettings();
if($('advanced-toggle')){$('advanced-toggle').onclick=toggleAdvancedSettings;}
scheduleStatusRefresh();
</script>)JS";
}
