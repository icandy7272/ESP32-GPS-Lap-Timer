#include "web_ui_internal.h"

const char* build_web_ui_script_dashboard_fragment() {
    return R"JS(
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
)JS";
}
