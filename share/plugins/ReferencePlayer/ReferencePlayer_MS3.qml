import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3 as Files
import MuseScore 3.0
import "../HarmonyAssistant" as Desk
import "../WorkflowCommon/Workspace.js" as Project
import "../WorkflowCommon/ActionMessages.js" as Messages

MuseScore {
    property string preferredPanelGroup: "bottomTools"
 id:root
 menuPath:"Plugins.Reference Player"
 description:"原曲参考：本地播放、波形循环、保调慢放及手动谱段关联。"
 version:"1.2.0"
 requiresScore:false
 pluginType:"dock"
 dockArea:"bottom"
 implicitWidth:840;implicitHeight:410
 property int preferredRibbonHeight:410
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
  var list=Project.copy(tracks),t=list[selectedTrack];t.path=media.path;t.identity=sourceIdentity;t.a=a;t.b=b;t.loop=looping;t.fade=fade;t.bookmarks=Project.copy(bookmarks);t.associations=Project.copy(associations);t.volume=media.volume;t.speed=media.speed;tracks=list;
  if(audioProject&&audioProject.available&&!audioProject.playing){if(observer&&typeof observer.setReferenceTracks==="function")observer.setReferenceTracks(tracks);else audioProject.setTracks(tracks);audioProject.selectedTrack=selectedTrack;audioProject.playbackSettings={mode:playbackMode,selectedTrack:selectedTrack,selectedAssociation:selectedAssociation,speed:media.speed,volume:jointVolume,fade:fade};}
 }
 function selectTrack(index){
  if(audioProject&&audioProject.playing&&observer)observer.stopReferencePlayback();
  if(index<0||index>=tracks.length)return;storeTrack();if(usable)media.pause();switchingTrack=true;selectedTrack=index;media=sessions[index];var t=tracks[index];
  sourceIdentity=t.identity||null;a=t.a||0;b=t.b||media.duration;looping=!!t.loop;fade=t.fade!==false;bookmarks=t.bookmarks||[];associations=t.associations||[];selectedAssociation=associations.length?0:-1;viewStart=0;viewEnd=media.duration;
  if(media.duration>0&&t.identity){var fresh=media.identity();if(fresh.size!==t.identity.size||fresh.modified!==t.identity.modified)associations=associations.map(function(v){var n=Project.copy(v);n.stale=true;return n});}
  viewportPeaks=[];media.volume=t.volume===undefined?.8:t.volume;media.speed=t.speed||1;switchingTrack=false;applyLoop();wave.requestPaint();status="已选音轨："+t.name;
 }
 function trackOption(name,value){if(selectedTrack<0)return;storeTrack();var list=Project.copy(tracks);list[selectedTrack][name]=value;tracks=list;if(audioProject&&!audioProject.playing)audioProject.setTracks(tracks);}
 function watchTrackFile(session,id) {
  session.fileReady.connect(function(info){
   var list=Project.copy(root.tracks),index=list.findIndex(function(t){return t.id===id});
   if(index<0)return;
   var track=list[index],previous=track.identity;
   track.identity=info;track.path=session.path;
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
  var retired=sessions.slice();
  if(audioProject&&audioProject.playing)observer.stopReferencePlayback();
  var list=Project.copy(data.tracks),objects=[];for(var i=0;i<list.length;++i){list[i].path=Project.resolve(url,list[i].path);var session=root.newReferenceMedia();objects.push(session);if(!list[i].id)list[i].id="restored-"+i;watchTrackFile(session,list[i].id);session.open(Project.fileUrl(list[i].path));}
  tracks=list;sessions=objects;selectedTrack=-1;selectTrack(Math.max(0,Math.min(list.length-1,data.selectedTrack||0)));restored=list[selectedTrack];workspace=String(url);playbackMode=data.playbackMode||0;status="正在恢复音轨与关联。";
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
  if(selectedAssociation<0){status="先选择一条谱段关联。";return;}
  storeTrack();for(var i=0;i<sessions.length;++i)sessions[i].pause();
  for(var ti=0;ti<tracks.length;++ti){if(tracks[ti].participate===false||tracks[ti].mute)continue;if(!sessions[ti]||sessions[ti].duration<=0){status="音轨尚未读完或文件不能播放："+tracks[ti].name;return;}var identity=sessions[ti].identity();if(tracks[ti].identity&&(identity.size!==tracks[ti].identity.size||identity.modified!==tracks[ti].identity.modified)){status="音频已改变，请选择该音轨，重新核对关联："+tracks[ti].name;return;}}
  var item=associations[selectedAssociation];observer.score=curScore;
  var request={revision:observer.analysisRevision(),start:item.start,end:item.end,occurrence:item.occurrence||1,mode:playbackMode,leader:item,speed:media.speed,tracks:tracks,volume:jointVolume,fade:fade};
  var plan=observer.referencePlan(request);if(plan.error){error(plan.error);return;}
  lastJointPlan=plan;lastSessionMode="joint";var r=observer.startReferencePlayback(plan);if(r.error)error(r.error);else status=playbackMode===0?"准备联合播放：开头对齐，此后各自保持速度。":"正在准备联合播放；临时速度不会保存到乐谱。";
 }
 function stopJoint(){restartLoop=false;loopRestart.stop();if(observer)observer.stopReferencePlayback();status="乐谱和参加播放的音轨已停止。";}
 Timer{id:loopRestart;interval:100;onTriggered:root.startJoint()}
 Connections{target:root.audioProject;ignoreUnknownSignals:true;onIndependentRequested:root.lastSessionMode="single";onJointRequested:{if(root.audioProject.playing)root.stopJoint();else root.startJoint();}onFinished:{if(wasJoint&&root.looping&&root.lastJointPlan){root.restartLoop=true;loopRestart.start();}}}
 onAssociationsChanged:storeTrack()
 onBookmarksChanged:storeTrack()
 onSelectedAssociationChanged:storeTrack()
 onPlaybackModeChanged:storeTrack()
 onJointVolumeChanged:storeTrack()
 property var owner:null
 property bool hasScore:false
 property string workspace:""
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
 property bool compact:(typeof panelPlacement==="string" && panelPlacement==="top") || height<170
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
   bookmarks=data.bookmarks||[];associations=data.associations||[];selectedAssociation=associations.length?0:-1;
   if(data.identity && (data.identity.size!==info.size || data.identity.modified!==info.modified))associations=associations.map(function(v){v=Project.copy(v);v.stale=true;return v});
   if(data.score && curScore && (data.score.name!==curScore.scoreName || data.score.measures!==curScore.nmeasures))associations=associations.map(function(v){v=Project.copy(v);v.stale=true;return v});
  }else status="已载入原曲";
  applyLoop();storeTrack();
 }
 function saveProject(url) {
  if(!usable || !sourceIdentity){status="先打开原曲";return}
  try {
   storeTrack();var savedTracks=Project.copy(tracks);savedTracks.forEach(function(t){t.path=Project.relative(url,t.path)});
   Project.writeModule(observer,url,"reference",{format:2,tracks:savedTracks,selectedTrack:selectedTrack,playbackMode:playbackMode,file:Project.relative(url,media.path),identity:sourceIdentity,a:a,b:b,loop:looping,fade:fade,bookmarks:bookmarks,associations:associations,score:Project.scoreIdentity(curScore)},Project.scoreIdentity(curScore));
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
  associations=list;selectedAssociation=list.length-1;status="已建立手动关联 · 可设置内部锚点";
 }
 function addAnchor() {
  var index=selectedAssociation;
  if(index<0||!observer||!usable)return;
  var list=Project.copy(associations),item=list[index],tick=observer.tick,ms=media.position;
  var anchors=(item.anchors||[]).filter(function(v){return v.tick!==tick});anchors.push({tick:tick,ms:ms});
  if(!Project.validateAnchors(anchors,item.start,item.end,item.from,item.until)){status="锚点须位于片段内部，且谱位和原曲时间顺序一致";return}
  item.anchors=anchors.sort(function(x,y){return x.tick-y.tick});associations=list;status="内部锚点已添加";
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
  hasScore=!!curScore;
  if(typeof root.newScoreObserver==="function")observer=root.newScoreObserver();
  if(typeof root.newReferenceMedia==="function")media=root.newReferenceMedia();
  if(typeof root.newReferenceProject==="function")audioProject=root.newReferenceProject();
  if(audioProject&&audioProject.available&&audioProject.tracks.length){var existing=Project.copy(audioProject.tracks);var objects=[];existing.forEach(function(t){var m=root.newReferenceMedia();objects.push(m);m.open(Project.fileUrl(t.path));});tracks=existing;sessions=objects;selectedTrack=-1;selectTrack(0);restored=existing[0];}
  if(!usable){status=media?media.status:"需要支持原曲媒体扩展的 Kumo 版本";return}
  if(observer){observer.score=curScore;var c=observer.loadConfiguration("ReferencePlayer");if(c.speed)config=c;}
  media.speed=config.speed||1;media.volume=config.volume===undefined?.8:config.volume;pauseWhenHidden=!!config.pauseWhenHidden;
  if(config.workspace && config.score && curScore && config.score.name===curScore.scoreName && config.score.measures===curScore.nmeasures)loadProject(config.workspace);
 }
 onScoreStateChanged: {
  if(audioProject&&audioProject.available&&audioProject.expressionPreview)return;
  hasScore=!!curScore;
  if(observer)observer.score=curScore;
  if(state && !state.selectionChanged && state.startLayoutTick>=0 && associations.length) {
   associations=associations.map(function(v){var n=Project.copy(v);if(state.startLayoutTick<n.end && state.endLayoutTick>n.start)n.stale=true;return n});
   status="谱面已编辑 · 相关关联请核查";
  }
 }
 Component.onDestruction:{storeTrack();if(audioProject&&audioProject.playing&&observer)observer.stopReferencePlayback();for(var i=0;i<sessions.length;++i)sessions[i].pause();if(usable)media.pause();persist()}
 onVisibleChanged:{if(!visible&&usable&&pauseWhenHidden)media.pause()}
 Connections {target:media;ignoreUnknownSignals:true;onFileReady:root.updateFile(info);onWaveformChanged:wave.requestPaint();onWaveformReady:{root.peaksFrom=from;root.peaksUntil=until;root.viewportPeaks=peaks;wave.requestPaint()}}
 Rectangle {anchors.fill:parent;color:theme.background}
 DropArea {anchors.fill:parent;onDropped:{
  if(!drop.urls.length)return;
  var added=0;for(var i=0;i<drop.urls.length;++i){var url=String(drop.urls[i]);if(/\.(wav|mp3|flac)$/i.test(url)){root.addingAudio=true;root.openAudio(url);added++;}}
  if(added){drop.acceptProposedAction();root.status="已添加 "+added+" 条音轨";}
 }}
 property string panelSpaceHandler:"toggleReferencePlayback"
 function toggleReferencePlayback() {
  if(!usable || !media.path){status="先打开一段音频，再按空格播放";return;}
  if(audioProject && audioProject.playing)stopJoint();
  else if(media.playing)media.pause();
  else if(lastSessionMode==="joint")startJoint();
  else if(lastSessionMode==="audio")playAudioTracks();
  else {lastSessionMode="single";media.play();}
 }

 ColumnLayout {
  anchors.fill:parent;anchors.margins:8;spacing:6
  RowLayout {
   Layout.fillWidth:true
   Label {text:"原曲参考";font.bold:true;color:theme.text}
   Label {Layout.fillWidth:true;text:root.compact?(root.audioProject&&root.audioProject.playing?root.audioProject.status:root.status):(root.usable?root.media.path.split(/[\\/]/).pop():"");elide:Text.ElideMiddle;color:theme.muted;visible:!root.narrow||root.compact}
   Item {Layout.fillWidth:true;visible:root.narrow}
   Desk.DeskButton {text:"打开";enabled:root.usable;onClicked:{root.addingAudio=false;audioDialog.open()}}
   Desk.DeskButton {text:"添加音轨";visible:!root.compact;enabled:root.usable;onClicked:{root.addingAudio=true;audioDialog.open()}}
   Desk.DeskButton {text:"项目";enabled:root.usable;onClicked:projectMenu.open()}
   Desk.DeskButton {text:typeof panelFloating==="boolean" && panelFloating?"停靠":"悬浮";onClicked:{if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}}
  }
  RowLayout {
   visible:!root.compact;Layout.fillWidth:true
   Desk.DeskComboBox {objectName:"referenceTracks";Layout.fillWidth:true;model:root.tracks.map(function(t){return t.name});currentIndex:root.selectedTrack;onActivated:root.selectTrack(currentIndex)}
   CheckBox {text:"参与";enabled:!root.audioProject||!root.audioProject.playing;checked:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length&&root.tracks[root.selectedTrack].participate!==false;onClicked:root.trackOption("participate",checked)}
   CheckBox {text:"静音";enabled:!root.audioProject||!root.audioProject.playing;checked:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length&&!!root.tracks[root.selectedTrack].mute;onClicked:root.trackOption("mute",checked)}
   CheckBox {text:"独听";enabled:!root.audioProject||!root.audioProject.playing;checked:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length&&!!root.tracks[root.selectedTrack].solo;onClicked:root.trackOption("solo",checked)}
  }
  RowLayout {
   Layout.fillWidth:true
   Desk.DeskComboBox {objectName:"referencePlaybackMode";enabled:!root.audioProject||!root.audioProject.playing;Layout.fillWidth:true;model:["开头对齐，各自播放","音频跟随乐谱","乐谱跟随选中音频"];currentIndex:root.playbackMode;onActivated:root.playbackMode=currentIndex}
   Desk.DeskButton {objectName:"referenceJointPlay";text:root.audioProject&&root.audioProject.playing?(root.audioProject.joint?"停止联动":"停止音轨"):"联合播放";enabled:root.audioProject&&root.audioProject.available;onClicked:{if(root.audioProject.playing)root.stopJoint();else root.startJoint()}}
   Label {text:"总音量";color:theme.muted;visible:!root.narrow&&!root.compact}
   Slider {from:0;to:1;value:root.jointVolume;enabled:!root.audioProject||!root.audioProject.playing;visible:!root.narrow&&!root.compact;Layout.preferredWidth:70;onMoved:root.jointVolume=value}
  }
  GridLayout {
   columns:root.compact?3:root.narrow?2:5;Layout.fillWidth:true;rowSpacing:4;columnSpacing:6
   Desk.DeskButton {text:root.usable && root.media.playing?"暂停":"播放原曲";enabled:root.usable && root.media.duration>0;onClicked:root.media.playing?root.media.pause():root.media.play()}
   Label {Layout.preferredWidth:88;text:root.usable?root.time(root.shownPosition):"0:00.000";color:theme.text;font.family:theme.fontFamily}
   Desk.DeskButton {text:"片段开始";visible:!root.compact;enabled:root.usable;onClicked:{root.a=root.media.position;root.applyLoop()}}
   Desk.DeskButton {text:"片段结束";visible:!root.compact;enabled:root.usable;onClicked:{root.b=root.media.position;root.applyLoop()}}
   Desk.DeskButton {text:"循环";visible:!root.compact;enabled:!root.audioProject||!root.audioProject.playing||root.audioProject.joint;checkable:true;checked:root.looping;onClicked:{root.looping=checked;root.applyLoop()}}
   Label {text:"速度";color:theme.muted;visible:!root.narrow && !root.compact}
   Desk.DeskSpinBox {from:50;to:150;value:root.usable?Math.round(root.media.speed*100):100;enabled:root.usable&&(!root.audioProject||!root.audioProject.playing);Layout.preferredWidth:88;onValueModified:{root.media.speed=value/100;root.storeTrack();root.persist()}}
   Slider {visible:!root.compact;from:0;to:1;value:root.usable?root.media.volume:.8;enabled:root.usable&&(!root.audioProject||!root.audioProject.playing);Layout.preferredWidth:root.narrow?110:85;onMoved:{root.media.volume=value;root.storeTrack();root.persist()}}
   Desk.DeskButton {text:"关联选区";visible:!root.compact;enabled:root.usable&&root.hasScore;onClicked:root.associate()}
  }
  Item {
   id:waveHost;clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:root.compact?0:64;visible:!root.compact
   Rectangle {anchors.fill:parent;color:theme.field;border.color:theme.line}
   Rectangle {x:(root.a-root.viewStart)/(root.viewEnd-root.viewStart||1)*parent.width;width:(root.b-root.a)/(root.viewEnd-root.viewStart||1)*parent.width;height:parent.height;color:theme.selected;visible:root.b>root.a;clip:true}
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
   MouseArea {
    anchors.fill:parent;acceptedButtons:Qt.LeftButton|Qt.RightButton
    property real downX:0;property int handle:0
    function ms(x){return Math.max(0,Math.min(root.media.duration,root.viewStart+(root.viewEnd-root.viewStart)*x/width))}
    onPressed:{if(!root.usable)return;downX=mouse.x;var ax=(root.a-root.viewStart)/(root.viewEnd-root.viewStart||1)*width,bx=(root.b-root.viewStart)/(root.viewEnd-root.viewStart||1)*width;handle=Math.abs(mouse.x-ax)<7?1:Math.abs(mouse.x-bx)<7?2:0;}
    onPositionChanged:{if(!pressed||!root.usable)return;if(handle===1)root.a=Math.min(root.b,ms(mouse.x));else if(handle===2)root.b=Math.max(root.a,ms(mouse.x));}
    onReleased:{if(!root.usable)return;if(handle)root.applyLoop();else if(Math.abs(mouse.x-downX)>7){root.a=ms(Math.min(mouse.x,downX));root.b=ms(Math.max(mouse.x,downX));root.applyLoop();}else root.media.seek(ms(mouse.x));}
    onWheel:{root.zoom(wheel.angleDelta.y>0?.8:1.25,wheel.x/width);wheel.accepted=true}
   }
  }
  RowLayout {
   visible:!root.compact;Layout.fillWidth:true
   Desk.DeskTextField {Layout.preferredWidth:root.narrow?76:100;text:Math.round(root.a);placeholderText:"开始（毫秒）";onEditingFinished:{var v=Number(text);if(isFinite(v)){root.a=Math.max(0,Math.min(root.b,v));root.applyLoop()}}}
   Desk.DeskTextField {Layout.preferredWidth:root.narrow?76:100;text:Math.round(root.b);placeholderText:"结束（毫秒）";onEditingFinished:{var v=Number(text);if(isFinite(v)&&root.usable){root.b=Math.max(root.a,Math.min(root.media.duration,v));root.applyLoop()}}}
   Desk.DeskButton {text:"全曲";enabled:root.usable;onClicked:{root.viewStart=0;root.viewEnd=root.media.duration;wave.requestPaint()}}
   Desk.DeskButton {text:"书签";enabled:root.usable;onClicked:{var list=Project.copy(root.bookmarks);list.push({ms:root.media.position,name:root.time(root.media.position)});root.bookmarks=list;}}
   Desk.DeskButton {text:"淡入";checkable:true;checked:root.fade;onClicked:{root.fade=checked;root.applyLoop()}}
   Item {Layout.fillWidth:true}
  }
  RowLayout {
   Layout.fillWidth:true;visible:!root.compact
   Desk.DeskComboBox {Layout.fillWidth:true;model:root.associations.map(function(v){return v.name+(v.stale?" · 待核查":"")});currentIndex:root.selectedAssociation;onActivated:root.selectAssociation(currentIndex)}
   Desk.DeskButton {objectName:"referenceLocate";text:"跳到乐谱";enabled:root.selectedAssociation>=0;onClicked:root.locate()}
   Desk.DeskButton {text:"谱位→原曲";enabled:root.selectedAssociation>=0;visible:!root.narrow;onClicked:root.seekScore()}
   Desk.DeskButton {text:"锚点";enabled:root.selectedAssociation>=0;onClicked:root.addAnchor()}
  }
  ListView {objectName:"referenceAssociations";visible:!root.compact;Layout.fillWidth:true;Layout.preferredHeight:root.associations.length?90:32;clip:true;model:root.associations;currentIndex:root.selectedAssociation;ScrollBar.vertical:ScrollBar{}
   delegate:ItemDelegate {width:ListView.view.width;height:44;highlighted:index===root.selectedAssociation;text:modelData.name+" · "+root.time(modelData.from)+"–"+root.time(modelData.until)+" · 第"+(modelData.occurrence||1)+"遍"+(modelData.active===false?" · 不参与":"")+(modelData.stale?" · 需检查":"");onClicked:root.selectAssociation(index);onDoubleClicked:root.playAssociation()}
   Label {visible:root.associations.length===0;anchors.centerIn:parent;text:"选乐谱范围和音频片段，再点“关联选区”。";color:theme.muted}
  }
  RowLayout {visible:!root.compact;Layout.fillWidth:true;Desk.DeskButton {objectName:"referencePlayAssociation";text:"播放选中片段";enabled:root.selectedAssociation>=0;onClicked:root.playAssociation()}Desk.DeskButton {text:"编辑关联";enabled:root.selectedAssociation>=0;onClicked:associationDialog.open()}Item{Layout.fillWidth:true}}
  Label {visible:!root.compact;Layout.fillWidth:true;Layout.preferredHeight:28;wrapMode:Text.Wrap;maximumLineCount:2;elide:Text.ElideRight;text:(root.audioProject&&root.audioProject.playing?root.audioProject.status:root.status)+(root.usable?" · "+Messages.mediaState(root.media.status):"");color:theme.muted;font.pixelSize:11}
 }
 Dialog {id:trackNameDialog;title:"音轨名称";modal:true;standardButtons:Dialog.Ok|Dialog.Cancel;onAccepted:root.trackOption("name",trackName.text);Desk.DeskTextField {id:trackName;width:240;text:root.selectedTrack>=0&&root.selectedTrack<root.tracks.length?root.tracks[root.selectedTrack].name:""}}
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
 Dialog {
  id:associationDialog;title:"关联与内部锚点";modal:true;width:340;height:380;standardButtons:Dialog.Close
  ColumnLayout {anchors.fill:parent
   Desk.DeskTextField {Layout.fillWidth:true;placeholderText:"关联名称";text:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?"":root.associations[root.selectedAssociation].name;onEditingFinished:{if(root.selectedAssociation<0)return;var list=Project.copy(root.associations);list[root.selectedAssociation].name=text;root.associations=list}}
   CheckBox {text:"这条关联参加联合播放";checked:root.selectedAssociation>=0&&root.selectedAssociation<root.associations.length&&root.associations[root.selectedAssociation].active!==false;onClicked:{if(root.selectedAssociation<0||root.selectedAssociation>=root.associations.length)return;var list=Project.copy(root.associations);list[root.selectedAssociation].active=checked;root.associations=list}}
   Label {text:"播放第几遍（反复后的出现次数）"}
   Desk.DeskSpinBox {from:1;to:99;value:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?1:(root.associations[root.selectedAssociation].occurrence||1);onValueModified:{var list=Project.copy(root.associations);list[root.selectedAssociation].occurrence=value;root.associations=list}}
   ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?[]:root.associations[root.selectedAssociation].anchors
    delegate:ItemDelegate {width:parent.width;text:modelData.tick+" → "+root.time(modelData.ms)+"  ×";onClicked:{var list=Project.copy(root.associations);list[root.selectedAssociation].anchors.splice(index,1);root.associations=list}}
   }
   Desk.DeskButton {text:"移除此关联";onClicked:{var list=Project.copy(root.associations);list.splice(root.selectedAssociation,1);root.associations=list;root.selectedAssociation=list.length?0:-1;associationDialog.close()}}
  }
 }
 Files.FileDialog {id:audioDialog;title:"打开原曲";nameFilters:["音频 (*.wav *.mp3 *.flac)"];onAccepted:root.openAudio(String(fileUrl));onRejected:root.relocating=false}
 Files.FileDialog {id:loadDialog;title:"打开工作区";nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.loadProject(String(fileUrl))}
 Files.FileDialog {id:saveDialog;title:"保存工作区";selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.saveProject(String(fileUrl))}
}
