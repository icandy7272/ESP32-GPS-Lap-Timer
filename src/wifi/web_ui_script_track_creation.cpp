#include "web_ui_internal.h"

const char* build_web_ui_script_track_creation_fragment() {
    return R"JS(
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


initTrackCreationUi();
renderAdvancedSettings();
refreshStatus();
loadSessions();
loadTracks();
loadSettings();
if($('advanced-toggle')){$('advanced-toggle').onclick=toggleAdvancedSettings;}
scheduleStatusRefresh();
)JS";
}
