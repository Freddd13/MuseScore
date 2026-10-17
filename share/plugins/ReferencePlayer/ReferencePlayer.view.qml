import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3 as Files
import QtQuick.Window 2.2
import MuseScore 3.0
import "../HarmonyAssistant" as Desk
import "../WorkflowCommon/Workspace.js" as Project
import "../WorkflowCommon/ActionMessages.js" as Messages
import "../WorkflowCommon" as Common

MuseScore {
    property string preferredPanelGroup: "bottomTools"
 id:root
 menuPath:"Plugins.Reference Player"
 description:"原曲参考：本地播放、波形循环、保调慢放及手动谱段关联。"
 version:"1.3.0"
 requiresScore:false
 pluginType:"dock"
 dockArea:"bottom"
 implicitWidth:840;implicitHeight:410
 property int preferredRibbonHeight:compact?Math.ceil(16+header.implicitHeight+6+transport.implicitHeight):410
 onPreferredRibbonHeightChanged:if(compact&&typeof root.fitPanelHeight==="function")root.fitPanelHeight(preferredRibbonHeight,false,true)
 property bool independentPanelSurfaces:true
 property bool compactPanelTitle:true
 property bool ribbonContentHeight:true
 property bool documentReady:false
 property bool restoringDocument:false
 property bool writingDocument:false
 property string documentJSON:""
 property bool linkEditor:false
 property bool handTool:false
 property real lastLinkedTick:-1
 function scheduleDocument(){if(documentReady&&!restoringDocument&&!writingDocument&&owner&&observer)documentTimer.restart()}
 function saveScoreProject(){
  if(!documentReady||restoringDocument||writingDocument||!owner||!observer||typeof observer.setWorkflowData!=="function")return
  if(observer.playing)return
  if(!tracks.length&&!observer.workflowData("reference").schema)return
  var list=Project.copy(tracks);list.forEach(function(t){delete t.missing;var p=observer.referenceFileLocation(t.path);t.originalPath=p.path;t.relativePath=p.relative})
  var data={schema:2,structure:observer.referenceStructureKey(),tracks:list,selectedTrack:selectedTrack,playbackMode:playbackMode,volume:jointVolume,linked:linkEditor}
  writingDocument=true;var result=observer.setWorkflowData("reference",data);writingDocument=false
  if(result.error){status=result.error;return}documentJSON=JSON.stringify(observer.workflowData("reference"))
 }
 function restoreScoreProject(){
  documentTimer.stop()
  if(!observer||typeof observer.workflowData!=="function")return
  var data=observer.workflowData("reference");documentJSON=JSON.stringify(data);restoringDocument=true
  if(data.schema===2&&data.tracks&&data.tracks.length){
   var value=Project.copy(data);value.tracks.forEach(function(t){var p=observer.referenceFileLocation(t.originalPath||t.path,t.relativePath||"");t.path=p.path;t.missing=!p.exists;if(data.structure&&data.structure!==root.observer.referenceStructureKey())(t.associations||[]).forEach(function(a){a.stale=true})})
   restoreTracks(value,"");jointVolume=data.volume===undefined?.5:data.volume;linkEditor=!!data.linked
   status=value.tracks.some(function(t){return t.missing})?"音轨与关联已恢复；找不到的音频可点“重新定位”。":"正在读取这份乐谱的参考音轨。"
  }else{
   if(audioProject&&audioProject.playing)stopJoint();var retired=sessions.slice();if(media&&retired.indexOf(media)<0)retired.push(media);switchingTrack=true;tracks=[];sessions=[];selectedTrack=-1;associations=[];bookmarks=[];selectedAssociation=-1;sourceIdentity=null;a=0;b=0;viewStart=0;viewEnd=0;media=root.newReferenceMedia();switchingTrack=false
   if(audioProject&&audioProject.available)audioProject.setTracks([]);retired.forEach(function(m){root.releaseReferenceMedia(m)});status="打开原曲，为这份乐谱建立片段关联。"
  }
  restoringDocument=false
 }
 function linkFromScore(){
  if(!linkEditor||!observer||!usable||selectedAssociation<0||media.playing||(audioProject&&audioProject.playing))return
  var selected=observer.expressionSelection(),tick=selected.notes&&selected.notes.length?selected.notes[0].tick:observer.tick,item=associations[selectedAssociation]
  if(tick===lastLinkedTick||tick<item.start||tick>item.end)return;lastLinkedTick=tick;media.seek(Project.mapped(item,tick))
 }
 function linkToScore(ms){
  if(!linkEditor||!observer||selectedAssociation<0||observer.playing)return
  var item=associations[selectedAssociation],points=[{tick:item.start,ms:item.from}].concat(item.anchors||[]).concat([{tick:item.end,ms:item.until}]);if(ms<item.from||ms>item.until)return
  var i=1;while(i<points.length-1&&ms>points[i].ms)i++;var a=points[i-1],b=points[i],tick=Math.round(a.tick+(ms-a.ms)*(b.tick-a.tick)/(b.ms-a.ms));lastLinkedTick=tick;observer.navigateWorkflow({start:tick,end:tick+1,firstTrack:item.firstTrack,endTrack:item.endTrack},false)
 }
 Timer {id:documentTimer;interval:250;onTriggered:root.saveScoreProject()}
 onTracksChanged:scheduleDocument()
 onSelectedTrackChanged:scheduleDocument()
 onLinkEditorChanged:scheduleDocument()
 property var observer:null
 property var media:null
 property var audioProject:null
 property var tracks:[]
 property var sessions:[]
 property int selectedTrack:-1
 property bool addingAudio:false
 property bool switchingTrack:false
 property int playbackMode:0
 property real jointVolume:.5
 property var lastJointPlan:null
 property bool restartLoop:false
 property string lastSessionMode:""
 function playAudioTracks(){
  if(!audioProject||!audioProject.available)return;
  if(audioProject.playing){stopJoint();return;}storeTrack();for(var i=0;i<sessions.length;++i)sessions[i].pause();
  var solo=tracks.some(function(t){return !!t.solo}),clips=[];
  tracks.forEach(function(t){if(t.participate===false||t.mute||(solo&&!t.solo))return;var from=t.a||0,until=t.b||0,speed=t.speed||1;if(until>from)clips.push({path:t.path,identity:t.identity,volume:t.volume===undefined?.8:t.volume,pieces:[{from:from,until:until,at:0,duration:(until-from)/speed,fade:t.fade!==false}]})});
  lastSessionMode="audio";var r=audioProject.start({clips:clips,joint:false,volume:jointVolume,loop:looping});if(r.error)error(r.error);else status="只播放参加的音轨，从各自片段开头一起开始；短音轨先结束。";
 }
 property real shownPosition:audioProject&&audioProject.playing?(!audioProject.joint||playbackMode===0?a+audioProject.position*(media?media.speed:1):Project.mapped(associations[selectedAssociation]||{start:0,end:1,from:0,until:1},audioProject.scoreTick)):media?media.position:0
 function storeTrack(){
  if(switchingTrack||selectedTrack<0||selectedTrack>=tracks.length||!media||media.duration<=0)return;
  var list=Project.copy(tracks),t=list[selectedTrack];t.path=media.path;t.identity=sourceIdentity;t.a=a;t.b=b;t.loop=looping;t.fade=fade;t.bookmarks=Project.copy(bookmarks);t.associations=Project.copy(associations);t.selectedAssociation=selectedAssociation;t.volume=media.volume;t.speed=media.speed;tracks=list;
  if(audioProject&&audioProject.available&&!audioProject.playing){if(observer&&typeof observer.setReferenceTracks==="function")observer.setReferenceTracks(tracks);else audioProject.setTracks(tracks);audioProject.selectedTrack=selectedTrack;audioProject.playbackSettings={mode:playbackMode,selectedTrack:selectedTrack,selectedAssociation:selectedAssociation,speed:media.speed,volume:jointVolume,fade:fade};}
 }
 function selectTrack(index){
  associationUndo=[];sliceBefore=null;
  if(audioProject&&audioProject.playing&&observer)observer.stopReferencePlayback();
  if(index<0||index>=tracks.length)return;storeTrack();if(usable)media.pause();switchingTrack=true;selectedTrack=index;media=sessions[index];var t=tracks[index];
  sourceIdentity=t.identity||null;a=t.a||0;b=t.b||media.duration;looping=!!t.loop;fade=t.fade!==false;bookmarks=t.bookmarks||[];associations=t.associations||[];selectedAssociation=Math.max(-1,Math.min(associations.length-1,t.selectedAssociation===undefined?(associations.length?0:-1):t.selectedAssociation));if(selectedAssociation>=0){a=associations[selectedAssociation].from;b=associations[selectedAssociation].until;}viewStart=0;viewEnd=media.duration;
  if(media.duration>0&&t.identity){var fresh=media.identity();if(fresh.size!==t.identity.size||fresh.modified!==t.identity.modified)associations=associations.map(function(v){var n=Project.copy(v);n.stale=true;return n});}
  viewportPeaks=[];media.volume=t.volume===undefined?.8:t.volume;media.speed=t.speed||1;switchingTrack=false;applyLoop();wave.requestPaint();status="已选音轨："+t.name;
 }
 function trackOption(name,value){if(selectedTrack<0)return;storeTrack();var list=Project.copy(tracks);list[selectedTrack][name]=value;tracks=list;if(audioProject&&!audioProject.playing)audioProject.setTracks(tracks);}
 function watchTrackFile(session,id) {
  session.fileReady.connect(function(info){
   var list=Project.copy(root.tracks),index=list.findIndex(function(t){return t.id===id});
   if(index<0)return;
   var track=list[index],previous=track.identity;
   track.identity=info;track.path=session.path;track.missing=false;
   if(!(track.b>track.a))track.b=session.duration;
   if(previous && (previous.size!==info.size || previous.modified!==info.modified))track.associations=(track.associations||[]).map(function(v){v.stale=true;return v});
   root.tracks=list;
   if(root.audioProject && !root.audioProject.playing)root.audioProject.setTracks(list);
  });
 }
 function openAudio(url){
  if(audioProject&&audioProject.playing){status="请先停止联合播放。";return;}
  if(relocating){media.open(String(url));return;}
  storeTrack();if(usable)media.pause();
  if(addingAudio||selectedTrack<0){
   if(tracks.length>=16){status="最多添加 16 条音轨。";return;}
   var session=root.newReferenceMedia(),list=Project.copy(tracks),objects=sessions.slice();objects.push(session);list.push({id:"audio-"+Date.now()+"-"+list.length,name:Project.path(url).split(/[\\/]/).pop(),path:String(url),associations:[],mute:false,solo:false,participate:true,volume:.8,speed:1});sessions=objects;tracks=list;selectedTrack=list.length-1;media=session;watchTrackFile(session,list[selectedTrack].id);
  }
  addingAudio=false;restored=null;media.open(String(url));
 }
 function restoreTracks(data,url){
  var retired=sessions.slice();if(media&&retired.indexOf(media)<0)retired.push(media);
  if(audioProject&&audioProject.playing)observer.stopReferencePlayback();
  var list=Project.copy(data.tracks),objects=[];for(var i=0;i<list.length;++i){list[i].path=Project.resolve(url,list[i].path);var session=root.newReferenceMedia();objects.push(session);if(!list[i].id)list[i].id="restored-"+i;watchTrackFile(session,list[i].id);session.open(Project.fileUrl(list[i].path));}
  tracks=list;sessions=objects;selectedTrack=-1;selectTrack(Math.max(0,Math.min(list.length-1,data.selectedTrack||0)));restored=list[selectedTrack];if(!restoringDocument)workspace=String(url);playbackMode=data.playbackMode||0;status="正在恢复音轨与关联。";
  if(typeof root.releaseReferenceMedia==="function")retired.forEach(function(m){root.releaseReferenceMedia(m)});
 }
 function removeTrack(){
  if(selectedTrack<0||selectedTrack>=tracks.length)return;if(audioProject&&audioProject.playing)stopJoint();var index=selectedTrack,retired=sessions[index],list=Project.copy(tracks),objects=sessions.slice();list.splice(index,1);objects.splice(index,1);switchingTrack=true;tracks=list;sessions=objects;selectedTrack=-1;switchingTrack=false;
  if(list.length)selectTrack(Math.min(index,list.length-1));else{media=root.newReferenceMedia();sourceIdentity=null;associations=[];bookmarks=[];selectedAssociation=-1;a=0;b=0;viewStart=0;viewEnd=0;}
  if(typeof root.releaseReferenceMedia==="function")root.releaseReferenceMedia(retired);if(observer&&typeof observer.setReferenceTracks==="function")observer.setReferenceTracks(tracks);status="已移除音轨记录，原音频文件保留。";
 }
 function startJoint(){
  if(audioProject&&audioProject.expressionPreview){status="正在试听副本。请返回原谱后，再从这里开始新的联合播放。";return;}
  if(!audioProject||!audioProject.available||!observer||!curScore){status="需要支持多音频联动的软件版本。";return;}
  if(observer.playing){status="请先停止乐谱播放。";return;}
  if(selectedAssociation<0){status="在下方选择一条片段关联，再点播放；也可选谱段后点“关联当前选段”。";return;}
  storeTrack();for(var i=0;i<sessions.length;++i)sessions[i].pause();
  for(var ti=0;ti<tracks.length;++ti){if(tracks[ti].participate===false||tracks[ti].mute)continue;if(!sessions[ti]||sessions[ti].duration<=0){status="音轨尚未读完或文件不能播放："+tracks[ti].name;return;}var identity=sessions[ti].identity();if(tracks[ti].identity&&(identity.size!==tracks[ti].identity.size||identity.modified!==tracks[ti].identity.modified)){status="音频已改变，请选择该音轨，重新核对关联："+tracks[ti].name;return;}}
  var item=associations[selectedAssociation];observer.score=curScore;
  var request={revision:observer.analysisRevision(),start:item.start,end:item.end,occurrence:item.occurrence||1,mode:playbackMode,leader:item,speed:media.speed,tracks:tracks,volume:jointVolume,fade:fade};
  var plan=observer.referencePlan(request);if(plan.error){error(plan.error);return;}
  plan.loop=looping;lastJointPlan=plan;lastSessionMode="joint";var r=observer.startReferencePlayback(plan);if(r.error)error(r.error);else status=playbackMode===0?"准备联合播放：开头对齐，此后各自保持速度。":"正在准备联合播放；临时速度不会保存到乐谱。";
 }
 function stopJoint(){restartLoop=false;loopRestart.stop();if(observer)observer.stopReferencePlayback();status="乐谱和参加播放的音轨已停止。";}
 Timer{id:loopRestart;interval:100;onTriggered:root.startJoint()}
 Connections{target:root.audioProject;ignoreUnknownSignals:true;onIndependentRequested:root.lastSessionMode="single";onJointRequested:{if(root.audioProject.playing)root.stopJoint();else root.startJoint();}onFinished:{}}
 onAssociationsChanged:storeTrack()
 onBookmarksChanged:storeTrack()
 onSelectedAssociationChanged:storeTrack()
 onPlaybackModeChanged:{storeTrack();scheduleDocument()}
 onJointVolumeChanged:{storeTrack();scheduleDocument()}
 property var owner:null
 property bool hasScore:false
 property string workspace:""
 property string structureKey:""
 property string status:"打开本地原曲开始"
 property var bookmarks:[]
 property var associations:[]
 property int selectedAssociation:-1
 property real a:0
 property real b:0
 property bool looping:false
 property bool fade:true
 property real viewStart:0
 property real viewEnd:0
 property var config:({speed:1,volume:.8,workspace:"",alignment:0})
 property var restored:null
 property bool usable:media && media.available
 property bool compact:(!panelFloating&&typeof panelPlacement==="string" && panelPlacement==="top")
 property bool narrow:width<600
 property var sourceIdentity:null
 property var viewportPeaks:[]
 property real peaksFrom:0
 property real peaksUntil:0
 property bool relocating:false
 property bool pauseWhenHidden:false
 property bool playingAssociation:false
 Desk.UiTheme {id:theme}
 function time(ms) {var sec=Math.max(0,ms)/1000;return Math.floor(sec/60)+":"+("0"+Math.floor(sec%60)).slice(-2)+"."+("00"+Math.floor(ms%1000)).slice(-3)}
 function error(e) {status=Messages.readable(e)}
 function persist() {
  if(observer)observer.saveConfiguration("ReferencePlayer",{speed:usable?media.speed:config.speed,volume:usable?media.volume:config.volume,workspace:workspace,alignment:config.alignment,pauseWhenHidden:pauseWhenHidden,score:Project.scoreIdentity(curScore)})
 }
 function applyLoop(){if(usable)media.setLoop(a,b,looping,fade)}
 function updateFile(info) {
  if(relocating){relocating=false;restored={identity:sourceIdentity,a:a,b:b,loop:looping,fade:fade,bookmarks:bookmarks,associations:associations,score:Project.scoreIdentity(curScore)}}
  viewportPeaks=[];
  sourceIdentity=info;a=0;b=media.duration;viewStart=0;viewEnd=b;bookmarks=[];associations=[];selectedAssociation=-1;
  if(restored) {
   var data=restored;restored=null;
   if(data.identity && (data.identity.size!==info.size || data.identity.modified!==info.modified))status="音频内容已变化 · 关联待核查";
   else status="已恢复项目";
   a=Math.max(0,Math.min(data.a||0,b));b=Math.max(a,Math.min(data.b||b,b));looping=!!data.loop;fade=data.fade!==false;
   bookmarks=data.bookmarks||[];associations=data.associations||[];selectedAssociation=Math.max(-1,Math.min(associations.length-1,data.selectedAssociation===undefined?(associations.length?0:-1):data.selectedAssociation));if(selectedAssociation>=0){a=associations[selectedAssociation].from;b=associations[selectedAssociation].until;}
   if(data.identity && (data.identity.size!==info.size || data.identity.modified!==info.modified))associations=associations.map(function(v){v=Project.copy(v);v.stale=true;return v});
   if(data.score && curScore && (data.score.name!==curScore.scoreName || data.score.measures!==curScore.nmeasures))associations=associations.map(function(v){v=Project.copy(v);v.stale=true;return v});
  }else status="已载入原曲";
  applyLoop();storeTrack();
 }
 function saveProject(url) {
  if(!usable || !sourceIdentity){status="先打开原曲";return}
  try {
   storeTrack();var savedTracks=Project.copy(tracks);savedTracks.forEach(function(t){t.path=Project.relative(url,t.path)});
   Project.writeModule(observer,url,"reference",{format:2,tracks:savedTracks,selectedTrack:selectedTrack,playbackMode:playbackMode,file:Project.relative(url,media.path),identity:sourceIdentity,a:a,b:b,loop:looping,fade:fade,bookmarks:bookmarks,associations:associations,selectedAssociation:selectedAssociation,score:Project.scoreIdentity(curScore)},Project.scoreIdentity(curScore));
   workspace=String(url);persist();status="项目已保存";
  }catch(e){error(e)}
 }
 function loadProject(url) {
  try {var data=Project.read(observer,url).reference;if(!data)throw new Error("该项目尚无原曲参考");workspace=String(url);if(data.tracks&&data.tracks.length){restoreTracks(data,url);persist();return;}restored=data;media.open(Project.fileUrl(Project.resolve(url,data.file)));persist();}
  catch(e){error(e)}
 }
 function associate() {
  if(!observer || !curScore || b<=a){status="请选择谱段及原曲片段";return}
  observer.score=curScore;var scope=observer.analysisScope(false);
  if(!scope.end){status="当前没有可关联谱段";return}
  var list=Project.copy(associations);list.push({name:"第"+scope.firstMeasure+"–"+scope.lastMeasure+"小节",start:scope.start,end:scope.end,firstTrack:scope.firstTrack,endTrack:scope.endTrack,from:a,until:b,occurrence:1,anchors:[],stale:false});
  associations=list;selectedAssociation=list.length-1;status="已建立手动关联 · 可设置内部对齐点";
 }
 function addAnchor() {
  var index=selectedAssociation;
  if(index<0||!observer||!usable)return;
  var list=Project.copy(associations),item=list[index],location=typeof observer.expressionSelection==="function"?observer.expressionSelection():{},selectedNotes=location.notes||[],tick=selectedNotes.length?selectedNotes[0].tick:observer.tick,ms=media.position;
  var anchors=(item.anchors||[]).filter(function(v){return v.tick!==tick});anchors.push({tick:tick,ms:ms});
  if(!Project.validateAnchors(anchors,item.start,item.end,item.from,item.until)){status="将乐谱光标和音频播放头放在片段内，两边的对齐点须按时间先后排列";return}
  associationUndo=associationUndo.concat([{associations:Project.copy(associations),selected:selectedAssociation,a:a,b:b}]).slice(-30);item.anchors=anchors.sort(function(x,y){return x.tick-y.tick});associations=list;wave.requestPaint();status="内部对齐点已添加";
 }
 function editAnchor(index,ms,tick){
  if(selectedAssociation<0)return;var list=Project.copy(associations),item=list[selectedAssociation],points=Project.copy(item.anchors||[]);if(index<0||index>=points.length)return;
  if(ms===null)points.splice(index,1);else points[index]={ms:ms,tick:tick===undefined?points[index].tick:tick};
  if(!Project.validateAnchors(points,item.start,item.end,item.from,item.until)){status="这个对齐点应位于片段内，并保持两边的时间先后顺序。";return;}
  associationUndo=associationUndo.concat([{associations:Project.copy(associations),selected:selectedAssociation,a:a,b:b}]).slice(-30);item.anchors=points;associations=list;status="内部对齐点已更新。";wave.requestPaint();
 }
 function bindScore(){
  hasScore=!!curScore;if(!observer)return;
  var changed=!Project.sameScore(owner,curScore);
  if(changed)saveScoreProject();owner=curScore;observer.score=curScore;
  if(changed){structureKey=typeof observer.referenceStructureKey==="function"?observer.referenceStructureKey():"";lastLinkedTick=-1;restoreScoreProject();}
 }
 function updateAssociationScore(){
  if(selectedAssociation<0||!curScore||!observer)return;var scope=observer.analysisScope(false);if(scope.end<=scope.start)return;
  associationUndo=associationUndo.concat([{associations:Project.copy(associations),selected:selectedAssociation,a:a,b:b}]).slice(-30);var list=Project.copy(associations),item=list[selectedAssociation];
  if(item.start!==scope.start||item.end!==scope.end)item.anchors=[];
  item.start=scope.start;item.end=scope.end;item.firstTrack=scope.firstTrack;item.endTrack=scope.endTrack;item.name="第"+scope.firstMeasure+"–"+scope.lastMeasure+"小节";item.stale=false;associations=list;status="已改为当前选中的乐谱范围。";
 }
 function locate() {
  if(selectedAssociation<0||!curScore)return;
  if(observer.playing){status="请停播后定位谱面";return}
  var item=associations[selectedAssociation];if(item.stale){status="关联已过期，请核对并重新关联";return}
  if(typeof observer.navigateWorkflow==="function"){var located=observer.navigateWorkflow(item,false);if(located.error)error(located.error);else status="已跳到 "+item.name+"；乐谱与音频仍可分别播放。";return}
  var cursor=curScore.newCursor();cursor.track=item.firstTrack;cursor.rewindToTick(item.start);
  if(cursor.element)curScore.selection.select(cursor.element);
 }
 function seekScore() {
  if(selectedAssociation<0||!observer)return;
  var item=associations[selectedAssociation];if(item.stale){status="关联已过期，请核对";return}
  if(observer.tick<item.start||observer.tick>item.end){status="当前谱位不在关联范围";return}
  media.seek(Project.mapped(item,observer.tick));
 }
 function selectAssociation(index){
  if(index<0||index>=associations.length)return;selectedAssociation=index;playingAssociation=false;var item=associations[index];a=item.from;b=item.until;viewStart=Math.max(0,a-500);viewEnd=Math.min(media.duration,b+500);media.seek(a);applyLoop();wave.requestPaint();
  status=item.stale?"片段已显示；乐谱对应需要重新检查。":"已显示 "+item.name+" 的音频片段；可以播放片段或跳到乐谱。";
 }
 function playAssociation(){
  if(selectedAssociation<0)return;selectAssociation(selectedAssociation);media.seek(a);playingAssociation=true;media.play();status="正在播放对应音频片段。";
 }
 Timer {interval:40;repeat:true;running:root.playingAssociation&&root.usable;onTriggered:{if(!root.media.playing){root.playingAssociation=false}else if(!root.looping&&root.media.position>=root.b-2){root.media.pause();root.playingAssociation=false;root.status="片段播放结束。"}}}
 function zoom(factor,center) {
  if(!usable)return;var duration=media.duration,span=Math.max(250,Math.min(duration,(viewEnd-viewStart)*factor));
  var middle=viewStart+(viewEnd-viewStart)*center;viewStart=Math.max(0,Math.min(duration-span,middle-span*center));viewEnd=viewStart+span;wave.requestPaint();if(typeof media.requestWaveform==="function")media.requestWaveform(viewStart,viewEnd,Math.round(waveHost.width));
 }
 onRun: {
  hasScore=!!curScore;owner=curScore;
  if(typeof root.newScoreObserver==="function")observer=root.newScoreObserver();
  if(typeof root.newReferenceMedia==="function")media=root.newReferenceMedia();
  if(typeof root.newReferenceProject==="function")audioProject=root.newReferenceProject();
  if(!curScore&&audioProject&&audioProject.available&&audioProject.tracks.length){var existing=Project.copy(audioProject.tracks);var objects=[];existing.forEach(function(t){var m=root.newReferenceMedia();objects.push(m);m.open(Project.fileUrl(t.path));});tracks=existing;sessions=objects;selectedTrack=-1;selectTrack(0);restored=existing[0];}
  if(!usable){status=media?media.status:"需要支持原曲媒体扩展的 Kumo 版本";return}
  if(observer){observer.score=curScore;var c=observer.loadConfiguration("ReferencePlayer");if(c.speed)config=c;}
  media.speed=config.speed||1;media.volume=config.volume===undefined?.8:config.volume;pauseWhenHidden=!!config.pauseWhenHidden;
  documentReady=true;
  if(curScore){structureKey=typeof observer.referenceStructureKey==="function"?observer.referenceStructureKey():"";restoreScoreProject();}
  if(curScore&&!observer.workflowData("reference").schema&&config.workspace&&config.score&&config.score.name===curScore.scoreName&&config.score.measures===curScore.nmeasures)loadProject(config.workspace);
 }
 onScoreStateChanged: {
  if(writingDocument||restoringDocument)return;
  if(audioProject&&audioProject.available&&audioProject.expressionPreview)return;
  bindScore();
  var shape=observer&&typeof observer.referenceStructureKey==="function"?observer.referenceStructureKey():"";
  if(structureKey&&shape&&structureKey!==shape&&associations.length){
   associations=associations.map(function(v){var n=Project.copy(v);n.stale=true;return n});
   status="小节或拍号已改变，请在关联行调整对应范围。";
  }
  structureKey=shape;
  if(documentReady&&observer&&typeof observer.workflowData==="function"&&documentJSON!==JSON.stringify(observer.workflowData("reference")))restoreScoreProject();
  linkFromScore();
 }
 Component.onDestruction:{storeTrack();saveScoreProject();if(audioProject&&audioProject.playing&&observer)observer.stopReferencePlayback();for(var i=0;i<sessions.length;++i)sessions[i].pause();if(usable)media.pause();persist()}
 onVisibleChanged:{if(!visible){spaceDown=false;spacePanned=false;if(usable&&pauseWhenHidden)media.pause();storeTrack();saveScoreProject();persist()}}
 Connections {target:observer;ignoreUnknownSignals:true;onWorkflowPlaybackError:root.status=message;onActiveScoreChanged:root.bindScore()}
 Connections {target:media;ignoreUnknownSignals:true;onFileReady:root.updateFile(info);onWaveformChanged:wave.requestPaint();onWaveformReady:{root.peaksFrom=from;root.peaksUntil=until;root.viewportPeaks=peaks;wave.requestPaint()}}
 Rectangle {anchors.fill:parent;color:theme.background}
 DropArea {anchors.fill:parent;onDropped:{
  if(!drop.urls.length)return;
  var added=0;for(var i=0;i<drop.urls.length;++i){var url=String(drop.urls[i]);if(/\.(wav|mp3|flac)$/i.test(url)){root.addingAudio=true;root.openAudio(url);added++;}}
  if(added){drop.acceptProposedAction();root.status="已添加 "+added+" 条音轨";}
 }}
 property string panelSpaceHandler:"toggleReferencePlayback"
 property string panelSpacePressHandler:"beginReferenceSpace"
 property string panelSpaceReleaseHandler:"endReferenceSpace"
 property string panelSpaceCancelHandler:"cancelReferenceSpace"
 function cancelReferenceSpace(){spaceDown=false;spacePanned=false}
 property bool spaceDown:false
 property bool spacePanned:false
 property int transportTarget:0
 property var associationUndo:[]
 property var sliceBefore:null
 property bool changingSlice:false
 function beginReferenceSpace(){spaceDown=true;spacePanned=false}
 function endReferenceSpace(){var play=spaceDown&&!spacePanned;spaceDown=false;spacePanned=false;if(play)toggleReferencePlayback()}
 function toggleReferencePlayback() {
  if(transportTarget===1){
   if(observer&&observer.playing){observer.stopWorkflowPlayback();status="乐谱已停止。";return;}
   if(!observer||!curScore){status="打开乐谱后可以播放。";return;}
   observer.score=curScore;var range=selectedAssociation>=0?associations[selectedAssociation]:observer.analysisScope(false),r=observer.navigateWorkflow(range,true);if(r.error)error(r.error);else status="正在播放乐谱。";return;
  }
  if(!usable || !media.path){status="打开一段音频后即可播放。";return;}
  if(audioProject&&audioProject.playing){stopJoint();return;}
  if(transportTarget===2){startJoint();return;}
  if(media.playing){media.pause();status="原曲已暂停。";return;}
  if(tracks.length>1)playAudioTracks();else{lastSessionMode="single";media.play();status="正在播放原曲。";}
 }
 function readTime(text){var pieces=String(text).trim().split(":");if(pieces.length===2)return (Number(pieces[0])*60+Number(pieces[1]))*1000;return Number(text)*1000}
 function beginSlice(){if(!sliceBefore)sliceBefore={associations:Project.copy(associations),selected:selectedAssociation,a:a,b:b}}
 function finishSlice(){
  if(!sliceBefore)return;
  var before=sliceBefore;sliceBefore=null;
  if(selectedAssociation>=0 && selectedAssociation<associations.length && (a!==before.a||b!==before.b)){
   associationUndo=associationUndo.concat([before]).slice(-30);var list=Project.copy(associations),item=list[selectedAssociation];item.from=a;item.until=b;item.anchors=(item.anchors||[]).filter(function(point){return point.ms>a&&point.ms<b});associations=list;status="片段范围已更新；可点“撤销片段修改”恢复。";
  }
  applyLoop();storeTrack();wave.requestPaint();
 }
 function useAssociation(index,enabled){
  if(index<0||index>=associations.length)return;var list=Project.copy(associations),target=list[index];
  associationUndo=associationUndo.concat([{associations:Project.copy(associations),selected:selectedAssociation,a:a,b:b}]).slice(-30);
  list.forEach(function(item,i){if(i===index)item.active=enabled;else if(enabled&&(item.occurrence||1)===(target.occurrence||1)&&item.start<target.end&&item.end>target.start)item.active=false;});
  associations=list;status=enabled?"本次一起播放采用“"+target.name+"”。":"这条关联已退出本次一起播放。";
 }
 function deleteAssociation(index){
  if(index<0||index>=associations.length)return;associationUndo=associationUndo.concat([{associations:Project.copy(associations),selected:selectedAssociation,a:a,b:b}]).slice(-30);
  var list=Project.copy(associations);list.splice(index,1);associations=list;selectedAssociation=Math.min(index,list.length-1);if(selectedAssociation>=0)selectAssociation(selectedAssociation);storeTrack();status="关联已删除；可点“撤销片段修改”恢复。";
 }
 function undoAssociation(){if(!associationUndo.length)return;var previous=associationUndo[associationUndo.length-1];associationUndo=associationUndo.slice(0,-1);associations=previous.associations;selectedAssociation=previous.selected;a=previous.a;b=previous.b;applyLoop();storeTrack();status="已恢复上一步片段修改。";wave.requestPaint()}
 function refreshWaveform(){wave.requestPaint();if(usable&&typeof media.requestWaveform==="function")media.requestWaveform(viewStart,viewEnd,Math.round(waveHost.width))}
 function fitSlice(){if(!usable)return;var padding=Math.max(150,(b-a)*.08);viewStart=Math.max(0,a-padding);viewEnd=Math.min(media.duration,b+padding);viewportPeaks=[];wave.requestPaint();if(typeof media.requestWaveform==="function")media.requestWaveform(viewStart,viewEnd,Math.round(waveHost.width))}

 ColumnLayout {
  anchors.fill:parent;anchors.margins:8;spacing:6
  RowLayout {id:header;Layout.fillWidth:true
   Label {text:"原曲参考";font.bold:true;color:theme.text}
   Desk.DeskComboBox {objectName:"referenceTracks";Layout.fillWidth:true;Layout.minimumWidth:50;model:root.tracks.map(function(t){return t.name});currentIndex:root.selectedTrack;onActivated:root.selectTrack(currentIndex)}
   Desk.DeskButton {text:"打开";enabled:root.usable;onClicked:{root.addingAudio=false;audioDialog.open()}}
   Desk.DeskButton {text:"添加";visible:!root.compact;enabled:root.usable;onClicked:{root.addingAudio=true;audioDialog.open()}}
   Desk.DeskButton {text:"项目";visible:!root.compact;enabled:root.usable;onClicked:projectMenu.open()}
   Desk.DeskButton {text:"展开";visible:root.compact;onClicked:{root.setPanelFloating(true);root.resizePanel(840,650)}}
   Desk.DeskButton {text:"×";visible:root.compact;implicitWidth:28;onClicked:root.hidePanel()}
   Desk.DeskButton {text:typeof panelFloating==="boolean"&&panelFloating?"停靠":"悬浮";visible:!root.narrow;onClicked:{if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}}
  }
  Flow {id:transport;Layout.fillWidth:true;Layout.preferredHeight:implicitHeight;spacing:5
   Desk.DeskComboBox {objectName:"referenceTransportTarget";width:90;model:["原曲","乐谱","一起播放"];currentIndex:root.transportTarget;onActivated:root.transportTarget=currentIndex}
   Desk.DeskButton {objectName:"referenceJointPlay";width:82;text:root.audioProject&&root.audioProject.playing||root.transportTarget===1&&root.observer&&root.observer.playing?"停止":root.media&&root.media.playing?"暂停":"播放";enabled:root.usable;onClicked:root.toggleReferencePlayback()}
   Label {width:82;height:28;text:root.usable?root.time(root.shownPosition):"0:00.000";verticalAlignment:Text.AlignVCenter;color:theme.text}
   Common.DragNumber {hintHost:root;objectName:"referenceSpeed";width:100;value:root.usable?root.media.speed*100:100;minimum:50;maximum:150;sensitivity:.5;caption:"速度 ";suffix:"%";enabled:root.usable&&(!root.audioProject||!root.audioProject.playing);onEdited:root.media.speed=value/100;onEditingFinished:{root.storeTrack();root.persist()}}
   Common.DragNumber {hintHost:root;objectName:"referenceVolume";width:112;value:root.usable?root.media.volume*100:80;minimum:0;maximum:100;caption:"音量 ";suffix:"%";rangeIndicator:true;enabled:root.usable&&(!root.audioProject||!root.audioProject.playing);onEdited:root.media.volume=value/100;onEditingFinished:{root.storeTrack();root.persist()}}
   Desk.DeskButton {text:"循环";visible:!root.compact;checkable:true;checked:root.looping;onClicked:{root.looping=checked;if(root.audioProject&&typeof root.audioProject.repeatSelection==="boolean")root.audioProject.repeatSelection=checked;root.applyLoop()}}
  }
  Label {Layout.fillWidth:true;Layout.preferredHeight:28;visible:!root.compact;wrapMode:Text.Wrap;maximumLineCount:2;elide:Text.ElideRight;text:root.audioProject&&root.audioProject.playing?root.audioProject.status:root.status;color:theme.muted;font.pixelSize:11}
  ScrollView {Layout.fillWidth:true;Layout.fillHeight:true;visible:!root.compact;clip:true
   Column {width:root.width-32;spacing:6
    Flow {width:parent.width;spacing:6;visible:root.tracks.length>1||root.transportTarget===2
     CheckBox {text:"参加播放";checked:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length&&root.tracks[root.selectedTrack].participate!==false;enabled:!root.audioProject||!root.audioProject.playing;onClicked:root.trackOption("participate",checked)}
     CheckBox {text:"静音";checked:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length&&!!root.tracks[root.selectedTrack].mute;enabled:!root.audioProject||!root.audioProject.playing;onClicked:root.trackOption("mute",checked)}
     CheckBox {text:"独听";checked:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length&&!!root.tracks[root.selectedTrack].solo;enabled:!root.audioProject||!root.audioProject.playing;onClicked:root.trackOption("solo",checked)}
     Common.DragNumber {hintHost:root;objectName:"referenceMasterVolume";width:130;minimum:0;maximum:100;value:root.jointVolume*100;caption:"总音量 ";suffix:"%";rangeIndicator:true;onEdited:{root.jointVolume=value/100;if(root.audioProject&&typeof root.audioProject.volume==="number")root.audioProject.volume=value/100}onEditingFinished:root.storeTrack()}
    }
    RowLayout {width:parent.width;visible:root.transportTarget===2
     Desk.DeskComboBox {objectName:"referencePlaybackMode";enabled:!root.audioProject||!root.audioProject.playing;Layout.fillWidth:true;model:["开头对齐，各自播放","音频跟随乐谱","乐谱跟随选中音频"];currentIndex:root.playbackMode;onActivated:root.playbackMode=currentIndex}
     Label {text:root.tracks.filter(function(t){return t.participate!==false&&!t.mute}).length+" 条音轨";color:theme.muted}
    }
  Item {
   id:waveHost;objectName:"referenceWaveform";clip:true;width:parent.width;height:Math.max(90,Math.min(360,root.height-420));visible:!root.compact
   Rectangle {anchors.fill:parent;color:theme.field;border.color:theme.line}
   Rectangle {x:(root.a-root.viewStart)/(root.viewEnd-root.viewStart||1)*parent.width;width:(root.b-root.a)/(root.viewEnd-root.viewStart||1)*parent.width;height:parent.height;color:theme.selected;visible:root.b>root.a&&root.media&&root.media.duration>0;clip:true}
   Canvas {
    id:wave;anchors.fill:parent;clip:true
    onWidthChanged:requestPaint();onHeightChanged:requestPaint()
    onPaint:{
     var c=getContext("2d");c.clearRect(0,0,width,height);c.strokeStyle=theme.muted;c.lineWidth=1;c.beginPath();
     if(!root.usable || !root.media.waveform.length)return;
     var detail=root.viewportPeaks.length && root.peaksFrom===root.viewStart && root.peaksUntil===root.viewEnd;var data=detail?root.viewportPeaks:root.media.waveform,duration=detail?root.viewEnd-root.viewStart:root.media.duration;
     for(var x=0;x<width;++x){var index=Math.floor((detail?(root.viewEnd-root.viewStart)*x/width:root.viewStart+(root.viewEnd-root.viewStart)*x/width)/duration*data.length);var level=data[Math.max(0,Math.min(data.length-1,index))]||0;var h=Math.max(1,Math.min(1,level)*height*.46);c.moveTo(x,height/2-h);c.lineTo(x,height/2+h);}c.stroke();
    }
   }
   Rectangle {width:1;height:parent.height;color:theme.accent;x:root.usable?(root.shownPosition-root.viewStart)/(root.viewEnd-root.viewStart||1)*parent.width:0;visible:x>=0 && x<=parent.width}
   Rectangle {width:2;height:parent.height;color:theme.accent;x:(root.a-root.viewStart)/(root.viewEnd-root.viewStart||1)*parent.width}
   Rectangle {width:2;height:parent.height;color:theme.accent;x:(root.b-root.viewStart)/(root.viewEnd-root.viewStart||1)*parent.width}
   Repeater {model:root.selectedAssociation>=0&&root.selectedAssociation<root.associations.length?(root.associations[root.selectedAssociation].anchors||[]):[]
    Item {property int pointIndex:index;property real draftMs:modelData.ms;x:(draftMs-root.viewStart)/(root.viewEnd-root.viewStart||1)*waveHost.width-6;width:12;height:waveHost.height;z:2
     Rectangle {x:5;width:2;height:parent.height;color:theme.accent;opacity:.65}
     Label {x:5;y:3;text:"对齐 "+(index+1);color:theme.accent;font.pixelSize:10}
     MouseArea {anchors.fill:parent;preventStealing:true;cursorShape:Qt.SizeHorCursor;property real origin:0;property real value:0
      onPressed:{origin=mapToItem(waveHost,mouse.x,mouse.y).x;value=modelData.ms}
      onPositionChanged:if(pressed)parent.draftMs=value+(mapToItem(waveHost,mouse.x,mouse.y).x-origin)/waveHost.width*(root.viewEnd-root.viewStart)
      onReleased:root.editAnchor(parent.pointIndex,parent.draftMs)
     }
    }
   }
   MouseArea {
    id:waveMouse;objectName:"referenceWaveformMouse";anchors.fill:parent;preventStealing:true;acceptedButtons:Qt.LeftButton|Qt.MiddleButton;hoverEnabled:true
    property real downX:0;property real downMs:0;property real downView:0;property real downSpan:0;property int handle:0;property bool moved:false;property bool panning:false
    cursorShape:root.spaceDown||root.handTool?pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor:Qt.CrossCursor
    function ms(x){return Math.max(0,Math.min(root.media.duration,root.viewStart+(root.viewEnd-root.viewStart)*x/width))}
    onPressed:{if(!root.usable)return;root.forceActiveFocus();downX=mouse.x;downMs=ms(mouse.x);downView=root.viewStart;downSpan=root.viewEnd-root.viewStart;moved=false;panning=root.spaceDown||root.handTool||mouse.button===Qt.MiddleButton;root.beginSlice();var ax=(root.a-root.viewStart)/(downSpan||1)*width,bx=(root.b-root.viewStart)/(downSpan||1)*width;handle=Math.abs(mouse.x-ax)<8?1:Math.abs(mouse.x-bx)<8?2:0;}
    onPositionChanged:{if(!pressed||!root.usable)return;if(Math.abs(mouse.x-downX)>3)moved=true;
     if(panning){root.spacePanned=root.spacePanned||moved;var left=Math.max(0,Math.min(root.media.duration-downSpan,downView-(mouse.x-downX)/width*downSpan));root.viewStart=left;root.viewEnd=left+downSpan;wave.requestPaint();return;}
     if(handle===1)root.a=Math.min(root.b-1,ms(mouse.x));else if(handle===2)root.b=Math.max(root.a+1,ms(mouse.x));else if(moved){root.selectedAssociation=-1;root.a=Math.min(downMs,ms(mouse.x));root.b=Math.max(downMs,ms(mouse.x));}
    }
    onReleased:{if(!root.usable)return;if(panning){root.sliceBefore=null;root.refreshWaveform();return;}if(handle||moved)root.finishSlice();else{root.sliceBefore=null;root.media.seek(ms(mouse.x));root.linkToScore(ms(mouse.x));}}
    onCanceled:{root.sliceBefore=null;if(root.spaceDown)root.spacePanned=true}
    onWheel:{root.zoom(wheel.angleDelta.y>0?.8:1.25,wheel.x/width);wheel.accepted=true}
   }
   Label {anchors.centerIn:parent;visible:!root.usable||!root.media.path||root.media.duration<=0;text:root.selectedTrack>=0&&root.tracks[root.selectedTrack]&&root.tracks[root.selectedTrack].missing?"找不到音频：点“重新定位音频”选择文件":"将 WAV、MP3、FLAC 拖到这里";color:theme.muted}
  }

    ScrollBar {objectName:"referenceTimeScroll";width:parent.width;orientation:Qt.Horizontal;policy:ScrollBar.AlwaysOn;active:true
        size:root.usable&&root.media.duration>0?Math.min(1,(root.viewEnd-root.viewStart)/root.media.duration):1
        position:root.usable&&root.media.duration>0?root.viewStart/root.media.duration:0
        onPositionChanged:if(pressed&&root.usable){var span=root.viewEnd-root.viewStart;root.viewStart=Math.max(0,Math.min(root.media.duration-span,position*root.media.duration));root.viewEnd=root.viewStart+span;root.refreshWaveform()}
    }
    RowLayout {width:parent.width
        Desk.DeskButton {text:"选择";checkable:true;checked:!root.handTool;onClicked:root.handTool=false}
        Desk.DeskButton {text:"手形平移";checkable:true;checked:root.handTool;onClicked:root.handTool=true;ToolTip.visible:hovered;ToolTip.text:"拖动波形平移时间轴；鼠标中键也可以平移"}
        CheckBox {text:"联动编辑器定位";checked:root.linkEditor;onClicked:{root.linkEditor=checked;root.linkFromScore()}}
    }
    Flow {width:parent.width;spacing:5
     Common.DragNumber {hintHost:root;objectName:"referenceSliceStart";width:126;value:root.a;minimum:0;maximum:Math.max(0,root.b-1);sensitivity:10;caption:"开始 ";formatter:root.time;parser:root.readTime;onEditingStarted:root.beginSlice();onEdited:root.a=value;onEditingFinished:root.finishSlice()}
     Common.DragNumber {hintHost:root;objectName:"referenceSliceEnd";width:126;value:root.b;minimum:root.a+1;maximum:root.usable?root.media.duration:1;sensitivity:10;caption:"结束 ";formatter:root.time;parser:root.readTime;onEditingStarted:root.beginSlice();onEdited:root.b=value;onEditingFinished:root.finishSlice()}
     Desk.DeskButton {text:"片段视图";onClicked:root.fitSlice()}
     Desk.DeskButton {text:"全曲视图";enabled:root.usable;onClicked:{root.viewStart=0;root.viewEnd=root.media.duration;root.viewportPeaks=[];wave.requestPaint()}}
     Desk.DeskButton {text:"播放头设为开始";onClicked:{root.beginSlice();root.a=Math.min(root.b-1,root.shownPosition);root.finishSlice()}}
     Desk.DeskButton {text:"播放头设为结束";onClicked:{root.beginSlice();root.b=Math.max(root.a+1,root.shownPosition);root.finishSlice()}}
     Desk.DeskButton {text:"书签";enabled:root.usable;onClicked:{var list=Project.copy(root.bookmarks);list.push({ms:root.shownPosition,name:root.time(root.shownPosition)});root.bookmarks=list}}
     Desk.DeskButton {text:"边界淡入淡出";checkable:true;checked:root.fade;onClicked:{root.fade=checked;root.applyLoop()}}
    }
    Flow {width:parent.width;spacing:5
     Desk.DeskButton {text:"重新定位音频";visible:root.selectedTrack>=0&&!!root.tracks[root.selectedTrack]&&root.tracks[root.selectedTrack].missing===true;onClicked:{root.relocating=true;audioDialog.open()}}
     Desk.DeskButton {objectName:"referenceAssociate";text:"关联当前选段";enabled:root.usable&&root.hasScore&&root.b>root.a;onClicked:root.associate()}
     Desk.DeskButton {objectName:"referenceLocate";text:"跳到乐谱";enabled:root.selectedAssociation>=0;onClicked:root.locate()}
     Desk.DeskButton {text:"从当前谱位定位音频";enabled:root.selectedAssociation>=0;onClicked:root.seekScore()}
     Desk.DeskButton {objectName:"referenceAlignmentPoints";text:"内部对齐点";enabled:root.selectedAssociation>=0;onClicked:associationDialog.open()}
     Desk.DeskButton {text:"撤销片段修改";enabled:root.associationUndo.length>0;onClicked:root.undoAssociation()}
    }
    ListView {objectName:"referenceAssociations";width:parent.width;height:root.associations.length?Math.min(150,root.associations.length*(root.narrow?84:56)):30;clip:true;model:root.associations;currentIndex:root.selectedAssociation;ScrollBar.vertical:ScrollBar{}
     delegate:Rectangle {width:ListView.view.width;height:root.narrow?84:56;color:index===root.selectedAssociation?theme.selected:theme.field;border.color:theme.subtleLine
      MouseArea {anchors.fill:parent;onClicked:root.selectAssociation(index);onDoubleClicked:{root.selectAssociation(index);root.playAssociation()}}
      Column {x:6;y:5;width:parent.width-12;spacing:4
       Label {width:parent.width;text:(modelData.name||"已关联片段")+" · "+root.time(modelData.from)+"–"+root.time(modelData.until);elide:Text.ElideRight;color:theme.text}
       Row {spacing:4
        Label {height:26;width:root.narrow?90:160;verticalAlignment:Text.AlignVCenter;text:"第 "+(modelData.occurrence||1)+" 遍"+(modelData.stale?" · 待核对":"")+(modelData.active===false?" · 未参加":"");color:theme.muted;font.pixelSize:11}
        Desk.DeskButton {objectName:index===root.selectedAssociation?"referencePlayAssociation":"referencePlayAssociation"+index;text:"播放";implicitWidth:48;onClicked:{root.selectAssociation(index);root.playAssociation()}}
        Desk.DeskButton {text:"修改";implicitWidth:48;onClicked:{root.selectAssociation(index);associationDialog.open()}}
        Desk.DeskButton {text:"删除";implicitWidth:48;onClicked:root.deleteAssociation(index)}
        CheckBox {height:26;text:"本次";checked:modelData.active!==false;onClicked:root.useAssociation(index,checked)}
       }
      }
     }
     Label {visible:root.associations.length===0;anchors.centerIn:parent;text:"拖出音频片段，选中乐谱小节，再点“关联当前选段”。";color:theme.muted;font.pixelSize:11}
    }
   }
  }
 }
 Dialog {id:trackNameDialog;title:"音轨名称";modal:true;width:280;standardButtons:Dialog.Ok|Dialog.Cancel;onAccepted:root.trackOption("name",trackName.text);Desk.DeskTextField {id:trackName;width:240;text:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length?root.tracks[root.selectedTrack].name:""}}
 Menu {id:projectMenu;
 MenuItem {text:"当前音轨改名";enabled:root.selectedTrack>=0;onTriggered:trackNameDialog.open()}
 MenuItem {text:"移除此音轨，保留原文件";enabled:root.selectedTrack>=0;onTriggered:root.removeTrack()}
 MenuItem {text:root.audioProject&&root.audioProject.playing?"停止音轨播放":"只播放全部参与音轨";enabled:root.audioProject&&root.audioProject.available;onTriggered:root.playAudioTracks()}
 MenuItem {text:"隐藏面板时暂停原曲";checkable:true;checked:root.pauseWhenHidden;onTriggered:{root.pauseWhenHidden=checked;root.persist()}}
 MenuItem {text:"重新定位音频";onTriggered:{root.relocating=true;audioDialog.open()}}
 MenuItem {text:"确认当前关联";enabled:root.selectedAssociation>=0;onTriggered:{var list=Project.copy(root.associations);list[root.selectedAssociation].stale=false;root.associations=list}}
 MenuItem {text:"关联详情";enabled:root.selectedAssociation>=0;onTriggered:associationDialog.open()}MenuItem {text:"打开工作区";onTriggered:loadDialog.open()}
 MenuItem {text:"保存工作区";onTriggered:{if(root.workspace)root.saveProject(root.workspace);else saveDialog.open()}}
 MenuItem {text:"另存工作区";onTriggered:saveDialog.open()}
 MenuItem {text:"书签列表";onTriggered:bookmarkDialog.open()}}
 Dialog {
  id:bookmarkDialog;title:"原曲书签";modal:true;width:300;height:300;standardButtons:Dialog.Close
  ListView {anchors.fill:parent;clip:true;model:root.bookmarks;delegate:ItemDelegate {width:parent.width;text:modelData.name;onClicked:{root.media.seek(modelData.ms);bookmarkDialog.close()}}}
 }
 Window {
  id:associationDialog;objectName:"referenceAssociationDetails";title:"片段与乐谱对应";flags:Qt.Tool;width:390;height:520;minimumWidth:340;minimumHeight:460;color:theme.background
  function open(){var area=root.panelScreen(root);x=Math.max(area.x,Math.min(root.mapToGlobal(0,0).x,area.x+area.width-width));y=Math.max(area.y,Math.min(root.mapToGlobal(0,0).y+40,area.y+area.height-height));show();raise();requestActivate()}
  ColumnLayout {anchors.fill:parent;anchors.margins:12
   Desk.DeskTextField {Layout.fillWidth:true;placeholderText:"关联名称";text:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?"":root.associations[root.selectedAssociation].name;onEditingFinished:{if(root.selectedAssociation<0)return;var list=Project.copy(root.associations);list[root.selectedAssociation].name=text;root.associations=list}}
   CheckBox {text:"这条关联参加联合播放";checked:root.selectedAssociation>=0&&root.selectedAssociation<root.associations.length&&root.associations[root.selectedAssociation].active!==false;onClicked:root.useAssociation(root.selectedAssociation,checked)}
   Desk.DeskButton {text:"改为当前选中的乐谱范围";enabled:root.hasScore;onClicked:root.updateAssociationScore()}
   Desk.DeskButton {text:"范围已核对";visible:root.selectedAssociation>=0&&root.selectedAssociation<root.associations.length&&!!root.associations[root.selectedAssociation].stale;onClicked:{var list=Project.copy(root.associations);list[root.selectedAssociation].stale=false;root.associations=list;root.status="这条关联已可用于一起播放。"}}
   Label {text:"播放第几遍（反复后的出现次数）"}
   Desk.DeskSpinBox {from:1;to:99;value:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?1:(root.associations[root.selectedAssociation].occurrence||1);onValueModified:{var list=Project.copy(root.associations);list[root.selectedAssociation].occurrence=value;root.associations=list}}
   Label {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"内部对齐点：标出录音中的这一刻，对应谱子的这一拍。先点乐谱位置，再点波形位置，最后添加。中间位置也会按这些点对齐。";color:theme.muted;font.pixelSize:11}
   Desk.DeskButton {objectName:"referenceAddAlignmentPoint";text:"添加当前两个位置为对齐点";onClicked:root.addAnchor()}
   ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?[]:root.associations[root.selectedAssociation].anchors
    delegate:RowLayout {width:ListView.view.width;height:36;property int pointIndex:index
     Label {text:"对齐点 "+(index+1);color:theme.text}
     Common.DragNumber {hintHost:root;property real pendingMs:modelData.ms;width:126;value:pendingMs;onEdited:pendingMs=value;minimum:root.a+1;maximum:root.b-1;sensitivity:10;formatter:root.time;parser:root.readTime;onEditingFinished:root.editAnchor(pointIndex,pendingMs)}
     Desk.DeskButton {objectName:"referenceDeleteAlignmentPoint"+index;text:"删除";onClicked:root.editAnchor(pointIndex,null)}
    }
   }
   RowLayout {Desk.DeskButton {text:"移除此关联";onClicked:{root.deleteAssociation(root.selectedAssociation);associationDialog.close()}} Desk.DeskButton {text:"关闭";onClicked:associationDialog.close()}}
  }
 }
 Files.FileDialog {id:audioDialog;title:"打开原曲";nameFilters:["音频 (*.wav *.mp3 *.flac)"];onAccepted:root.openAudio(String(fileUrl));onRejected:root.relocating=false}
 Files.FileDialog {id:loadDialog;title:"打开工作区";nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.loadProject(String(fileUrl))}
 Files.FileDialog {id:saveDialog;title:"保存工作区";selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.saveProject(String(fileUrl))}
}
