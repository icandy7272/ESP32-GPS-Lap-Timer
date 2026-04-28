#include "web_ui_internal.h"

const char* build_web_ui_script_track_creation_fragment() {
    return R"JS(
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

function setupCoachContent(){
  var sf=_trackDraft.startFinish;
  var validation=_trackDraft.validation||makeValidationState();
  if(!isNameReady()){
    return {
      title:'Step 1 - Name',
      body:'Enter a track name so this phone setup flow can save the draft correctly.'
    };
  }
  if(isPointSamplingActive()){
    return {
      title:'Hold Still',
      body:samplingProgressMessage()
    };
  }
  if(!sf.p1){
    if(canMarkWithCurrentGps()){
      return {
        title:'Step 2 - Mark P1',
        body:'Mark P1 at one end of the start/finish line.'
      };
    }
    return {
      title:'Step 2 - Wait for GPS',
      body:gpsMarkingBlockMessage()
    };
  }
  if(!sf.p2){
    if(canMarkWithCurrentGps()){
      return {
        title:'Step 2 - Mark P2',
        body:'Walk to the other end of the line, then mark P2.'
      };
    }
    return {
      title:'Step 2 - Move to P2',
      body:'Walk to the other end of the line. '+gpsMarkingBlockMessage()
    };
  }

  var lineInfo=lineConfidenceInfo(sf);
  if(!lineInfo.ready){
    return {
      title:'Step 3 - Recheck Line',
      body:lineInfo.label+'. Re-mark with a longer line or steadier GPS.'
    };
  }
  if(hasIncompleteSectors()){
    return {
      title:'Step 3 - Complete Sectors',
      body:'Complete or delete every sector split before saving.'
    };
  }
  if(_trackDraft.repeatability.active){
    return {
      title:'Step 3 - Finish Repeatability',
      body:'Finish the repeatability check before saving.'
    };
  }
  if(isCreateReady()){
    return {
      title:'Ready to Save',
      body:'Ready to save. Tap Create Track when the name and direction still look right.'
    };
  }
  if(validation.installing){
    return {
      title:'Step 3 - Starting Validation',
      body:'Installing the draft line. Keep the phone page open.'
    };
  }
  if(validation.active){
    var need=Math.max(0,MIN_ACCEPTED_CROSSINGS-(validation.accepted||0));
    return {
      title:'Step 3 - Validate Direction',
      body:'Walk across in the shown direction until PASS reaches '
        +MIN_ACCEPTED_CROSSINGS+'. '+need+' more needed.'
    };
  }
  return {
    title:'Step 3 - Validate Direction',
    body:'Review the direction arrow, then validate by walking across the line.'
  };
}

function renderSetupCoach(){
  var panel=$('setup-coach');
  if(!panel){return;}
  var coach=setupCoachContent();
  panel.innerHTML='<div class="setup-coach-title">'+escapeHtml(coach.title)+'</div>'+
    '<div class="setup-coach-body">'+escapeHtml(coach.body)+'</div>';
}

// MUST stay in sync with setupMapProjection in tools/ui_preview/web_console.js.
function setupMapProjection(points,width,height,padding){
  if(!points.length){return null;}
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
  var minSpanM=4;
  if(bounds.maxX-bounds.minX<minSpanM){
    var cx=(bounds.minX+bounds.maxX)/2;
    bounds.minX=cx-minSpanM/2;
    bounds.maxX=cx+minSpanM/2;
  }
  if(bounds.maxY-bounds.minY<minSpanM){
    var cy=(bounds.minY+bounds.maxY)/2;
    bounds.minY=cy-minSpanM/2;
    bounds.maxY=cy+minSpanM/2;
  }
  var innerW=width-padding*2;
  var innerH=height-padding*2;
  var spanX=bounds.maxX-bounds.minX;
  var spanY=bounds.maxY-bounds.minY;
  var scale=Math.min(innerW/spanX,innerH/spanY);
  if(!isFinite(scale)||scale<=0){scale=1;}
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

// MUST stay in sync with projectSetupMapPoint in tools/ui_preview/web_console.js.
function projectSetupMapPoint(point,projection){
  var local=projectPointMeters(point,projection.origin);
  return {
    x: projection.ox+(local.x-projection.bounds.minX)*projection.scale,
    y: projection.height-projection.oy-(local.y-projection.bounds.minY)*projection.scale
  };
}

// MUST stay in sync with setupMapUncertaintyMarkup in tools/ui_preview/web_console.js.
function setupMapUncertaintySvg(point,projection,color){
  var p=projectSetupMapPoint(point,projection);
  var radius=Math.min(24,Math.max(5,pointSpreadMeters(point)*projection.scale));
  return '<circle cx="'+p.x.toFixed(1)+'" cy="'+p.y.toFixed(1)+'" r="'+radius.toFixed(1)+
    '" fill="'+color+'" fill-opacity="0.14" stroke="'+color+'" stroke-opacity="0.35"></circle>';
}

// MUST stay in sync with setupMapMarkerMarkup in tools/ui_preview/web_console.js.
function setupMapMarkerSvg(point,projection,label,color,attrs){
  var p=projectSetupMapPoint(point,projection);
  return '<circle '+(attrs||'')+' cx="'+p.x.toFixed(1)+'" cy="'+p.y.toFixed(1)+
    '" r="5" fill="'+color+'"></circle>'+
    '<text x="'+p.x.toFixed(1)+'" y="'+(p.y-9).toFixed(1)+
    '" fill="#dbeafe" font-size="10" text-anchor="middle">'+label+'</text>';
}

// MUST stay in sync with renderSetupLocalMap in tools/ui_preview/web_console.js.
function renderSetupLocalMap(){
  var panel=$('setup-local-map');
  if(!panel){return;}
  if(!isNameReady()){
    panel.innerHTML='<div class="helper-text">Enter a track name to unlock the local setup map.</div>';
    return;
  }

  var sf=_trackDraft.startFinish;
  var current=hasValidFix()?currentGpsPoint():null;
  var points=[];
  if(current){points.push(current);}
  if(sf.p1){points.push(sf.p1);}
  if(sf.p2){points.push(sf.p2);}
  if(!points.length){
    panel.innerHTML='<div class="helper-text">Waiting for GPS or marked points.</div>';
    return;
  }

  var width=260;
  var height=170;
  var padding=18;
  var projection=setupMapProjection(points,width,height,padding);
  var svg='<svg viewBox="0 0 '+width+' '+height+'" aria-label="Local setup map">';
  svg+='<defs><marker id="setup-arrowhead" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#fbbf24"></path></marker></defs>';
  svg+='<rect x="0" y="0" width="'+width+'" height="'+height+'" rx="8" fill="#0b1827" stroke="#2a3c52"></rect>';
  svg+='<line x1="'+padding+'" y1="'+(height-padding)+'" x2="'+(width-padding)+
    '" y2="'+(height-padding)+'" stroke="#1f3550" stroke-width="1"></line>';
  svg+='<line x1="'+padding+'" y1="'+padding+'" x2="'+padding+
    '" y2="'+(height-padding)+'" stroke="#1f3550" stroke-width="1"></line>';

  if(sf.p1){svg+=setupMapUncertaintySvg(sf.p1,projection,'#38bdf8');}
  if(sf.p2){svg+=setupMapUncertaintySvg(sf.p2,projection,'#a78bfa');}
  if(sf.p1&&sf.p2){
    var p1=projectSetupMapPoint(sf.p1,projection);
    var p2=projectSetupMapPoint(sf.p2,projection);
    svg+='<line data-setup-line="start-finish" x1="'+p1.x.toFixed(1)+
      '" y1="'+p1.y.toFixed(1)+'" x2="'+p2.x.toFixed(1)+'" y2="'+p2.y.toFixed(1)+
      '" stroke="#38bdf8" stroke-width="4" stroke-linecap="round"></line>';
    if(typeof sf.heading==='number'){
      var midX=(p1.x+p2.x)/2;
      var midY=(p1.y+p2.y)/2;
      var rad=normalizeHeading(sf.heading)*Math.PI/180;
      var arrowLen=32;
      var dx=Math.sin(rad)*arrowLen/2;
      var dy=-Math.cos(rad)*arrowLen/2;
      svg+='<line data-setup-direction x1="'+(midX-dx).toFixed(1)+
        '" y1="'+(midY-dy).toFixed(1)+'" x2="'+(midX+dx).toFixed(1)+
        '" y2="'+(midY+dy).toFixed(1)+'" stroke="#fbbf24" stroke-width="3" stroke-linecap="round" marker-end="url(#setup-arrowhead)"></line>';
    }
  }
  if(sf.p1){svg+=setupMapMarkerSvg(sf.p1,projection,'P1','#38bdf8','');}
  if(sf.p2){svg+=setupMapMarkerSvg(sf.p2,projection,'P2','#a78bfa','');}
  if(current){
    svg+=setupMapMarkerSvg(current,projection,'GPS','#fbbf24','data-setup-current');
  }
  svg+='</svg>';

  var summary='<div class="setup-map-summary">';
  if(sf.p1&&sf.p2){
    summary+='<span class="setup-map-pill">Line '+reviewLineLengthMeters(sf).toFixed(1)+' m</span>';
    if(typeof sf.heading==='number'){
      summary+='<span class="setup-map-pill">Direction '+headingArrow(sf.heading)+' '+
        Math.round(sf.heading)+' deg '+compassLabel(sf.heading)+'</span>';
    }
  }else if(sf.p1){
    summary+='<span class="setup-map-pill">P1 marked</span>';
  }else{
    summary+='<span class="setup-map-pill">GPS preview</span>';
  }
  if(current){
    summary+='<span class="setup-map-pill">GPS '+formatCoord(current)+'</span>';
  }
  summary+='</div>';
  panel.innerHTML=svg+summary;
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
  renderSetupCoach();
  renderSetupLocalMap();
  renderStartFinishSection();
  renderSectorRows();
  renderGeometryReview();
  renderRepeatabilitySection();
  renderValidationSection();
  renderCreateButton();
  scheduleStatusRefresh();
}

// --- Phase B: draft validation section --------------------------
//
// Shown after Mark P2 completes and before Save.  Installs the draft
// line on the firmware, polls /api/tracks/draft_validation, and renders
// live PASS/REJECT counts and the last few candidates so the operator
// can walk across the line and confirm it triggers.

function renderValidationSection(){
  var section=$('validate-section');
  var btn=$('validate-btn');
  var summary=$('validate-summary');
  var events=$('validate-events');
  var flipBtn=$('validate-flip-btn');
  var cancelBtn=$('validate-cancel-btn');
  if(!section){return;}

  var v=_trackDraft.validation||makeValidationState();
  var ready=isReviewReady();
  section.style.display=ready?'block':'none';
  if(!ready){return;}

  if(btn){
    if(v.active){
      btn.textContent=v.accepted>=MIN_ACCEPTED_CROSSINGS
        ? 'Validating... ('+v.accepted+'/'+MIN_ACCEPTED_CROSSINGS+' ✓)'
        : 'Validating... walk across the line';
      btn.disabled=true;
    }else if(v.installing){
      btn.textContent='Starting...';
      btn.disabled=true;
    }else if(v.accepted>=MIN_ACCEPTED_CROSSINGS){
      btn.textContent='Re-validate';
      btn.disabled=false;
    }else{
      btn.textContent='Validate By Walking Across';
      btn.disabled=false;
    }
  }
  if(flipBtn){flipBtn.style.display=v.active?'inline-block':'none';}
  if(cancelBtn){cancelBtn.style.display=v.active?'inline-block':'none';}

  if(summary){
    if(v.lastError){
      summary.innerHTML='<span class="track-msg-err">'+v.lastError+'</span>';
    }else if(v.active){
      var need=Math.max(0,MIN_ACCEPTED_CROSSINGS-v.accepted);
      var needTxt=need>0
        ? (need+' more accepted crossing'+(need===1?'':'s')+' needed')
        : (_trackDraft.repeatability.active
            ? 'finish the repeatability check before saving'
            : (isCreateReady()?'ready to Save':'complete the remaining setup blockers before saving'));
      summary.innerHTML='<div class="validate-summary-box">'+
        '<span class="validate-summary-chip validate-summary-pass">PASS '+v.accepted+'/'+MIN_ACCEPTED_CROSSINGS+'</span>'+
        '<span class="validate-summary-chip validate-summary-reject">REJECT '+v.rejected+'</span>'+
        '</div>'+
        '<div>Walk across the line - '+needTxt+'.</div>';
    }else if(isCreateReady()){
      summary.innerHTML='<span class="track-msg-ok">Validated: PASS '+
        v.accepted+'/'+MIN_ACCEPTED_CROSSINGS+', REJECT '+v.rejected+
        '. Ready to Save.</span>';
    }else if((v.accepted||0)>=MIN_ACCEPTED_CROSSINGS&&_trackDraft.repeatability.active){
      summary.textContent='Validation passed. Finish the repeatability check before saving.';
    }else if((v.accepted||0)>=MIN_ACCEPTED_CROSSINGS&&hasIncompleteSectors()){
      summary.textContent='Validation passed. Complete or delete every sector split before saving.';
    }else if((v.accepted||0)>=MIN_ACCEPTED_CROSSINGS){
      summary.textContent='Validation passed. Complete the remaining setup blockers before saving.';
    }else{
      summary.textContent='Optional but recommended: walk across the line a few times and confirm it fires.';
    }
  }

  if(events){
    var rows=(v.events||[]).slice(0,VALIDATION_EVENTS_SHOWN).map(function(e){
      var cls=e.accepted?'pass':'reject';
      var verdict=e.accepted?'PASS':'REJECT';
      var uStr=(typeof e.u==='number')?e.u.toFixed(2):'?';
      var over=(typeof e.overshoot_m==='number')?e.overshoot_m.toFixed(2):'0';
      var hd=(typeof e.hdiff_deg==='number')
        ? (e.hdiff_deg>=0?'+':'')+e.hdiff_deg.toFixed(1)
        : '?';
      return '<div class="vrow '+cls+'">'+verdict+' '+
             '<span class="vreason">u='+uStr+' over='+over+'m hd='+hd+'° '+
             (e.reason||'')+'</span></div>';
    });
    events.innerHTML=rows.join('');
  }
}

// Build the body the firmware expects on POST /api/tracks/draft_validation.
// Uses the current draft line heading (so Flip Direction re-POSTs with
// heading ± 180°).
function validationPayload(){
  var sf=_trackDraft.startFinish;
  if(!sf.p1||!sf.p2||typeof sf.heading!=='number'){return null;}
  return {
    sf_lat1:sf.p1.lat, sf_lon1:sf.p1.lon,
    sf_lat2:sf.p2.lat, sf_lon2:sf.p2.lon,
    sf_heading:sf.heading
  };
}

// All validation state mutations re-resolve _trackDraft.validation by
// current identity and check it hasn't been swapped under them (via
// resetTrackDraft / resetValidationState).  Codex P2 from 2026-04-19
// follow-up: closing over the old `v` object let a late POST /
// pollValidation callback install a timer on a detached state that
// nothing could later clear, leaving the firmware line orphaned.
function startValidation(){
  var v0=_trackDraft.validation;
  if(!v0){v0=_trackDraft.validation=makeValidationState();}
  if(v0.installing||v0.active){return;}
  var payload=validationPayload();
  if(!payload){
    setTrackMsg('Mark both Start/Finish points first.','err');
    return;
  }
  v0.installing=true;
  v0.lastError=null;
  renderTrackDraft();
  fetch('/api/tracks/draft_validation',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(payload)
  }).then(function(response){
    return response.json().then(function(b){return {ok:response.ok,body:b};});
  }).then(function(result){
    // Re-resolve the CURRENT validation state.  If re-mark / reset
    // swapped it while POST was in flight, don't touch the new one.
    var vNow=_trackDraft.validation;
    if(vNow!==v0){
      // Old state was discarded.  DO NOT send DELETE here: by the
      // time we see vNow!==v0 the user may have already installed a
      // NEWER session (re-marked or re-validated) whose session_id
      // is live on the firmware.  An unscoped DELETE would kill it.
      // Codex P1 from 2026-04-19 round-3.
      //
      // The stale firmware line is self-superseded on the next POST
      // (which bumps session_id and installs the new line), and
      // initTrackCreationUi() cleans up any orphan on the next page
      // load — so it will not accumulate beyond one stale session.
      return;
    }
    vNow.installing=false;
    if(!result.ok){
      vNow.lastError='Failed to start validation: '+
        (result.body&&result.body.error||'unknown');
      renderTrackDraft();
      return;
    }
    vNow.active=true;
    vNow.accepted=0;
    vNow.rejected=0;
    vNow.events=[];
    // Session id tags every subsequent poll response; the firmware
    // bumps it on any set/clear so mismatched data is dropped.
    vNow.sessionId=(result.body&&result.body.session_id)|0;
    if(vNow.pollTimer){clearInterval(vNow.pollTimer);}
    vNow.pollTimer=setInterval(pollValidation,VALIDATION_POLL_MS);
    renderTrackDraft();
  }).catch(function(err){
    var vNow=_trackDraft.validation;
    if(vNow!==v0){return;}
    vNow.installing=false;
    vNow.lastError='Failed to start validation: '+err;
    renderTrackDraft();
  });
}

function pollValidation(){
  var v0=_trackDraft.validation;
  if(!v0||!v0.active){return;}
  fetch('/api/tracks/draft_validation').then(function(r){
    return r.json();
  }).then(function(body){
    // Reject stale / mismatched sessions.  Compare against the
    // CURRENT sessionId at receive time — NOT a value captured at
    // poll-start — because Flip Direction / re-mark can mutate it
    // synchronously while this fetch is in flight.  Codex P1 from
    // 2026-04-19 round-3 review: the old `var expectedId=v0.sessionId`
    // at top made the check a no-op for the exact case it was meant
    // to close.
    var vNow=_trackDraft.validation;
    if(!vNow||vNow!==v0) return;
    if(!vNow.active) return;
    // Session mismatch = firmware replaced our session with a different
    // one (another tab / serial takeover / flip that hasn't returned
    // yet).  Previously we just returned, which left the UI showing
    // stale accepted counts and an enabled Save button against state
    // the server no longer has.  Now we tear down locally so the
    // operator sees "session lost — re-validate" state.  Codex P2
    // from 2026-04-20 round-4.
    if(typeof body.session_id==='number' && body.session_id!==vNow.sessionId){
      if(vNow.sessionId !== 0){
        // Only clear when we had a real session id.  A sessionId of 0
        // means Flip Direction is in-flight and the new POST response
        // has not landed yet — NOT a takeover.  Just drop this poll.
        stopValidationLocal(vNow);
        vNow.lastError = 'Validation session lost (takeover or clear). Re-validate.';
        renderTrackDraft();
      }
      return;
    }
    // Defend against mixed-snapshot regressions on ancient firmware:
    // if firmware says !active, treat as drift and stop locally.
    if(body.active===false){
      stopValidationLocal(vNow);
      return;
    }
    vNow.accepted=body.accepted|0;
    vNow.rejected=body.rejected|0;
    vNow.events=(body.events||[]);
    renderTrackDraft();
  }).catch(function(){/* transient network — keep trying */});
}

// Strip polling + active flag + counters from a validation state
// without hitting the network.  Used when the firmware reports the
// session is gone (after a concurrent clear) so the UI doesn't loop.
function stopValidationLocal(v){
  if(!v) return;
  if(v.pollTimer){clearInterval(v.pollTimer); v.pollTimer=null;}
  v.active=false;
  v.accepted=0;
  v.rejected=0;
  v.events=[];
  v.sessionId=0;
  renderTrackDraft();
}

function stopValidation(){
  var v=_trackDraft.validation;
  if(!v){return;}
  // ZERO the counters so a subsequent re-mark / navigate-back cannot
  // leave isCreateReady() satisfied against a line that is no longer
  // the current draft.  Codex P1 from 2026-04-19 follow-up review.
  stopValidationLocal(v);
  // Fire and forget — firmware also clears on the next setter.
  fetch('/api/tracks/draft_validation',{method:'DELETE'}).catch(function(){});
}

function flipValidationDirection(){
  var sf=_trackDraft.startFinish;
  if(!sf.p1||!sf.p2||typeof sf.heading!=='number'){return;}
  // Toggle the flipped flag the existing flipStartFinishHeading()
  // uses so both the Repeatability and Validation paths see the
  // same direction.
  sf.flipped=!sf.flipped;
  updateStartFinishHeading();
  var v0=_trackDraft.validation;
  if(!v0||!v0.active){
    renderTrackDraft();
    return;
  }
  // Zero local counters immediately so isCreateReady() cannot satisfy
  // Save based on pre-flip PASS counts while we wait for the new
  // session id.  Save stays disabled until fresh accepted crossings
  // land against the flipped line.  Codex P1 from 2026-04-19.
  v0.accepted=0;
  v0.rejected=0;
  v0.events=[];
  v0.sessionId=0;  // old session invalidated; drop any in-flight poll
  var payload=validationPayload();
  if(!payload){renderTrackDraft();return;}
  fetch('/api/tracks/draft_validation',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(payload)
  }).then(function(r){return r.json().then(function(b){return {ok:r.ok,body:b};});})
    .then(function(result){
      var vNow=_trackDraft.validation;
      if(!vNow||vNow!==v0) return;
      if(!result.ok) return;
      vNow.sessionId=(result.body&&result.body.session_id)|0;
    })
    .catch(function(){});
  renderTrackDraft();
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
    // Re-marking a point invalidates any prior validation walk — the
    // firmware's draft line was against the OLD P1/P2.  Clear client
    // counters; backend will be re-installed when the operator hits
    // Validate again.
    if(!_trackDraft.repeatability.active){
      if(_trackDraft.validation&&_trackDraft.validation.active){
        stopValidation();
      }else{
        resetValidationState();
      }
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
  // Flipping direction invalidates any prior validation run.  Zero
  // counters + sessionId immediately so no in-flight poll response
  // or stale closure can re-satisfy the Save gate on the flipped
  // line (codex P1).  If a validation was active, re-install with
  // the new heading and cache the new session id on POST return.
  var v0=_trackDraft.validation;
  if(v0&&v0.active){
    v0.accepted=0;
    v0.rejected=0;
    v0.events=[];
    v0.sessionId=0;
    var payload=validationPayload();
    if(payload){
      fetch('/api/tracks/draft_validation',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(payload)
      }).then(function(r){return r.json().then(function(b){return {ok:r.ok,body:b};});})
        .then(function(result){
          var vNow=_trackDraft.validation;
          if(!vNow||vNow!==v0) return;
          if(!result.ok) return;
          vNow.sessionId=(result.body&&result.body.session_id)|0;
        })
        .catch(function(){});
    }
  }else{
    resetValidationState();
  }
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
  // DO NOT clear draft_validation before POST — the firmware gate at
  // /api/tracks needs the installed line + accepted counts to approve
  // the save.  Clearing here makes every save 409 (codex P1 from
  // 2026-04-19 round-3 review).  Cleanup happens in the success path
  // below; on failure we keep the validation state so the user can
  // fix the issue and retry without re-walking.
  // Stop local polling only so the UI doesn't flicker while the
  // server processes the request — but leave the firmware line
  // installed.
  if(_trackDraft.validation&&_trackDraft.validation.pollTimer){
    clearInterval(_trackDraft.validation.pollTimer);
    _trackDraft.validation.pollTimer=null;
  }
  // Snapshot the IDs of pre-existing tracks with this same name so
  // the ghost-success readback can distinguish "my POST landed" from
  // "a same-name track was already there" — even under concurrent
  // mutation of the list.  Codex P2 from 2026-04-20 round-6 review
  // caught a count-based proof flaw: if another client DELETEs a
  // same-name track between our snapshot and readback, the count
  // delta looks like our POST failed even though it succeeded.
  // Using id-set DIFF is stable across concurrent adds AND deletes:
  // we look for id in the readback that has our name AND was NOT in
  // the pre-POST snapshot, regardless of what else happened.
  //
  // Firmware allows duplicate names (track_store doesn't enforce
  // uniqueness), so the id-set is authoritative identity.
  var preExistingSameNameIds = {};
  if (typeof _trackList !== 'undefined' && _trackList) {
    _trackList.forEach(function(t){
      if (t && t.name === name && t.id) {
        preExistingSameNameIds[String(t.id)] = true;
      }
    });
  }
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
    // Server responded — safe to clear the pending flag here.  The
    // subsequent branches only mutate state (or restart polling) and
    // do NOT depend on the flag for their own guard.
    _trackSubmitPending=false;
    if(result.ok&&result.body.ok){
      // Save succeeded — NOW it is safe to clear the firmware draft
      // line so stale [xing-draft] events stop firing against what is
      // about to become the real active track.
      fetch('/api/tracks/draft_validation',{method:'DELETE'}).catch(function(){});
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
      // Save failed.  Keep the validation state installed on the
      // firmware so the user can fix whatever rejected the save
      // (e.g. name collision) and retry without re-walking.  Resume
      // polling so counts / events stay fresh in the UI.
      if(_trackDraft.validation&&_trackDraft.validation.active
         && !_trackDraft.validation.pollTimer){
        _trackDraft.validation.pollTimer=setInterval(pollValidation,VALIDATION_POLL_MS);
      }
      renderTrackDraft();
      setTrackMsg((result.body&&result.body.error)?result.body.error:'Failed to create track.','err');
    }
  }).catch(function(){
    // "Ghost success" recovery — the POST response may have been
    // dropped after the ESP32 actually committed the track.  Readback
    // via GET /api/tracks and diff against the pre-POST id snapshot.
    //
    // CRITICAL: do NOT clear _trackSubmitPending here — the readback
    // can take 1-2 s during which refreshStatus() keeps re-rendering
    // the draft and would re-enable the Save button, letting the
    // user double-submit.  The flag is cleared inside each terminal
    // branch of the readback chain (ghost success / failed / ambiguous
    // / readback-failed).  Codex P2 (#2) from 2026-04-20 round-6.
    var attemptedName = name;
    // Capture validation identity + session id so the async callback
    // can (a) tell whether the user is still on this same draft, and
    // (b) send a SCOPED DELETE that won't kill a newer session the
    // user may have started.  Codex P2 (#2) from 2026-04-20 round-5.
    var v0 = _trackDraft.validation;
    var v0SessionId = v0 ? (v0.sessionId || 0) : 0;
    fetch('/api/tracks').then(function(r){return r.json();}).then(function(body){
      // Clearing happens INSIDE each terminal branch below.  See
      // comment at the top of the catch().
      var tracks = (body && body.tracks) || [];
      // The new track is any track that has our attempted name AND
      // an id that was NOT in the pre-POST snapshot.  Robust to both
      // concurrent adds AND concurrent deletes of same-name tracks
      // because we compare against a fixed id set, not a count.
      // Codex P2 (#1) from 2026-04-20 round-6.
      var newSameNameTracks = tracks.filter(function(t){
        if (!t || t.name !== attemptedName || !t.id) return false;
        return !preExistingSameNameIds[String(t.id)];
      });
      var stillOnSameDraft = (v0 !== null && v0 === _trackDraft.validation);

      if (newSameNameTracks.length === 1) {
        // Ghost success — exactly one new same-name track appeared
        // since the pre-POST snapshot.  That id is the track we just
        // created, regardless of whether other tracks were added or
        // removed concurrently.
        var newMatch = newSameNameTracks[0];

        // Clean up the firmware draft line we installed for this
        // POST.  Prefer a scoped DELETE so a concurrent fresh session
        // (started by the user after the network error) is not
        // clobbered.
        var deleteUrl = '/api/tracks/draft_validation';
        if (v0SessionId > 0) {
          deleteUrl += '?session=' + encodeURIComponent(v0SessionId);
        }
        fetch(deleteUrl,{method:'DELETE'}).catch(function(){});

        _trackSubmitPending=false;
        if (stillOnSameDraft) {
          if (nameField) { nameField.value = ''; }
          resetTrackDraft();
          renderTrackDraft();
          loadTracks();
          if (newMatch.id) {
            selectTrack(newMatch.id, 'newly_created').then(function(selected){
              if (selected) {
                setTrackMsg('Track created (response was dropped, verified via readback).','ok');
              } else {
                setTrackMsg('Track created but auto-select failed; pick it manually.','err');
              }
            });
          } else {
            setTrackMsg('Track created (response was dropped, verified via readback).','ok');
          }
        } else {
          loadTracks();
          setTrackMsg('Earlier save "' + attemptedName +
                      '" actually landed (network response was dropped).'
                      + ' Check the Tracks list.','ok');
        }
      } else if (newSameNameTracks.length === 0) {
        // No new same-name id appeared → POST never committed.
        // Genuine network failure, retry is safe.
        _trackSubmitPending=false;
        if (stillOnSameDraft && v0.active && !v0.pollTimer) {
          v0.pollTimer = setInterval(pollValidation, VALIDATION_POLL_MS);
        }
        renderTrackDraft();
        if (stillOnSameDraft) {
          setTrackMsg('Failed to create track — network error.','err');
        }
      } else {
        // Ambiguous: 2+ new same-name tracks since snapshot.  Either
        // our save + a concurrent client's save both landed, or the
        // snapshot was empty and something else raced.  Refuse to
        // auto-select; let the operator eyeball the list.
        _trackSubmitPending=false;
        loadTracks();
        if (stillOnSameDraft) {
          if (v0.active && !v0.pollTimer) {
            v0.pollTimer = setInterval(pollValidation, VALIDATION_POLL_MS);
          }
          renderTrackDraft();
          setTrackMsg('Network error. Concurrent changes detected — check the Tracks list before retrying.','err');
        } else {
          setTrackMsg('Network error during an earlier save; concurrent changes detected. Check the Tracks list.','err');
        }
      }
    }).catch(function(){
      // Readback itself failed — can't tell which case we're in.
      _trackSubmitPending=false;
      var stillOnSameDraft = (v0 !== null && v0 === _trackDraft.validation);
      if (stillOnSameDraft && v0.active && !v0.pollTimer) {
        v0.pollTimer = setInterval(pollValidation, VALIDATION_POLL_MS);
      }
      renderTrackDraft();
      if (stillOnSameDraft) {
        setTrackMsg('Failed to create track — network error. Check the track list before retrying.','err');
      }
    });
  });
}

function initTrackCreationUi(){
  if($('sf-p1-btn')){$('sf-p1-btn').onclick=function(){markStartFinishPoint('p1');};}
  if($('sf-p2-btn')){$('sf-p2-btn').onclick=function(){markStartFinishPoint('p2');};}
  if($('sample-cancel-btn')){$('sample-cancel-btn').onclick=cancelPointSampling;}
  if($('repeatability-btn')){$('repeatability-btn').onclick=startRepeatabilityCheck;}
  if($('validate-btn')){$('validate-btn').onclick=startValidation;}
  if($('validate-cancel-btn')){$('validate-cancel-btn').onclick=stopValidation;}
  if($('validate-flip-btn')){$('validate-flip-btn').onclick=flipValidationDirection;}
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
  // Reconcile any orphaned firmware-side draft_validation line from
  // a previous WEB page load (closed tab / reload while validation
  // was active).  Only DELETE if owner === 'web' AND scope the
  // DELETE to the session_id we just saw — firmware rejects the
  // clear if a newer session has taken over between GET and DELETE,
  // so we can't accidentally kill another tab's fresh session.
  // Codex P2 from 2026-04-20 round-4 review (race on unscoped
  // init-time DELETE).
  fetch('/api/tracks/draft_validation').then(function(r){return r.json();})
    .then(function(body){
      if(body && body.active && body.owner === 'web'
         && typeof body.session_id === 'number' && body.session_id > 0){
        fetch('/api/tracks/draft_validation?session='
              + encodeURIComponent(body.session_id),
              {method:'DELETE'}).catch(function(){});
      }
    })
    .catch(function(){/* no firmware / boot race — ignore */});
}


initTrackCreationUi();
if($('tab-status')){$('tab-status').onclick=function(){setActiveTab('status');};}
if($('tab-sessions')){$('tab-sessions').onclick=function(){setActiveTab('sessions');};}
if($('tab-tracks')){$('tab-tracks').onclick=function(){setActiveTab('tracks');};}
renderPrimaryTabs();
renderAdvancedSettings();
refreshStatus();
loadSessions();
loadTracks();
loadSettings();
if($('advanced-toggle')){$('advanced-toggle').onclick=toggleAdvancedSettings;}
scheduleStatusRefresh();
)JS";
}
