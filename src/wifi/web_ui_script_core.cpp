#include "web_ui_internal.h"

const char* build_web_ui_script_core_fragment() {
    return R"JS(
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
)JS";
}
