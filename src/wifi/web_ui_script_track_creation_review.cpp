#include "web_ui_internal.h"

const char* build_web_ui_script_track_creation_review_fragment() {
    return R"JS(
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
)JS";
}
