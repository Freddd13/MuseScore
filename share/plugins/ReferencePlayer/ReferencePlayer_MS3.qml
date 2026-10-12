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
 version:"1.0.0"
 requiresScore:false
 pluginType:"dock"
 dockArea:"bottom"
 implicitWidth:840;implicitHeight:410
 property int preferredRibbonHeight:410
 property var observer:null
 property var media:null
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
  applyLoop();
 }
 function saveProject(url) {
  if(!usable || !sourceIdentity){status="先打开原曲";return}
  try {
   Project.writeModule(observer,url,"reference",{file:Project.relative(url,media.path),identity:sourceIdentity,a:a,b:b,loop:looping,fade:fade,bookmarks:bookmarks,associations:associations,score:Project.scoreIdentity(curScore)},Project.scoreIdentity(curScore));
   workspace=String(url);persist();status="项目已保存";
  }catch(e){error(e)}
 }
 function loadProject(url) {
  try {var data=Project.read(observer,url).reference;if(!data)throw new Error("该项目尚无原曲参考");workspace=String(url);restored=data;media.open(Project.fileUrl(Project.resolve(url,data.file)));persist();}
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
  if(!usable){status=media?media.status:"需要支持原曲媒体扩展的 Kumo 版本";return}
  if(observer){observer.score=curScore;var c=observer.loadConfiguration("ReferencePlayer");if(c.speed)config=c;}
  media.speed=config.speed||1;media.volume=config.volume===undefined?.8:config.volume;pauseWhenHidden=!!config.pauseWhenHidden;
  if(config.workspace && config.score && curScore && config.score.name===curScore.scoreName && config.score.measures===curScore.nmeasures)loadProject(config.workspace);
 }
 onScoreStateChanged: {
  hasScore=!!curScore;
  if(observer)observer.score=curScore;
  if(state && !state.selectionChanged && state.startLayoutTick>=0 && associations.length) {
   associations=associations.map(function(v){var n=Project.copy(v);if(state.startLayoutTick<n.end && state.endLayoutTick>n.start)n.stale=true;return n});
   status="谱面已编辑 · 相关关联请核查";
  }
 }
 Component.onDestruction:{if(usable)media.pause();persist()}
 onVisibleChanged:{if(!visible&&usable&&pauseWhenHidden)media.pause()}
 Connections {target:media;ignoreUnknownSignals:true;onFileReady:root.updateFile(info);onWaveformChanged:wave.requestPaint();onWaveformReady:{root.peaksFrom=from;root.peaksUntil=until;root.viewportPeaks=peaks;wave.requestPaint()}}
 Rectangle {anchors.fill:parent;color:theme.background}
 ColumnLayout {
  anchors.fill:parent;anchors.margins:8;spacing:6
  RowLayout {
   Layout.fillWidth:true
   Label {text:"原曲参考";font.bold:true;color:theme.text}
   Label {Layout.fillWidth:true;text:root.usable?root.media.path.split(/[\\/]/).pop():"";elide:Text.ElideMiddle;color:theme.muted;visible:!root.narrow}
   Item {Layout.fillWidth:true;visible:root.narrow}
   Desk.DeskButton {text:"打开";enabled:root.usable;onClicked:audioDialog.open()}
   Desk.DeskButton {text:"项目";enabled:root.usable;onClicked:projectMenu.open()}
   Desk.DeskButton {text:typeof panelFloating==="boolean" && panelFloating?"停靠":"悬浮";onClicked:{if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}}
  }
  GridLayout {
   columns:root.compact?3:root.narrow?2:5;Layout.fillWidth:true;rowSpacing:4;columnSpacing:6
   Desk.DeskButton {text:root.usable && root.media.playing?"暂停":"播放原曲";enabled:root.usable && root.media.duration>0;onClicked:root.media.playing?root.media.pause():root.media.play()}
   Label {Layout.preferredWidth:88;text:root.usable?root.time(root.media.position):"0:00.000";color:theme.text;font.family:theme.fontFamily}
   Desk.DeskButton {text:"片段开始";visible:!root.compact;enabled:root.usable;onClicked:{root.a=root.media.position;root.applyLoop()}}
   Desk.DeskButton {text:"片段结束";visible:!root.compact;enabled:root.usable;onClicked:{root.b=root.media.position;root.applyLoop()}}
   Desk.DeskButton {text:"循环";visible:!root.compact;checkable:true;checked:root.looping;onClicked:{root.looping=checked;root.applyLoop()}}
   Label {text:"速度";color:theme.muted;visible:!root.narrow && !root.compact}
   Desk.DeskSpinBox {from:50;to:150;value:root.usable?Math.round(root.media.speed*100):100;enabled:root.usable;Layout.preferredWidth:88;onValueModified:{root.media.speed=value/100;root.persist()}}
   Slider {visible:!root.compact;from:0;to:1;value:root.usable?root.media.volume:.8;enabled:root.usable;Layout.preferredWidth:root.narrow?110:85;onMoved:{root.media.volume=value;root.persist()}}
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
   Rectangle {width:1;height:parent.height;color:theme.accent;x:root.usable?(root.media.position-root.viewStart)/(root.viewEnd-root.viewStart||1)*parent.width:0;visible:x>=0 && x<=parent.width}
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
   delegate:ItemDelegate {width:ListView.view.width;height:44;highlighted:index===root.selectedAssociation;text:modelData.name+" · "+root.time(modelData.from)+"–"+root.time(modelData.until)+(modelData.stale?" · 需检查":"");onClicked:root.selectAssociation(index);onDoubleClicked:root.playAssociation()}
   Label {visible:root.associations.length===0;anchors.centerIn:parent;text:"选乐谱范围和音频片段，再点“关联选区”。";color:theme.muted}
  }
  RowLayout {visible:!root.compact;Layout.fillWidth:true;Desk.DeskButton {objectName:"referencePlayAssociation";text:"播放选中片段";enabled:root.selectedAssociation>=0;onClicked:root.playAssociation()}Desk.DeskButton {text:"编辑关联";enabled:root.selectedAssociation>=0;onClicked:associationDialog.open()}Item{Layout.fillWidth:true}}
  Label {visible:!root.compact;Layout.fillWidth:true;Layout.preferredHeight:28;wrapMode:Text.Wrap;maximumLineCount:2;elide:Text.ElideRight;text:root.status+(root.usable?" · "+Messages.mediaState(root.media.status):"");color:theme.muted;font.pixelSize:11}
 }
 Menu {id:projectMenu;
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
   Label {text:"反复出现次数（仅用于定位说明）"}
   Desk.DeskSpinBox {from:1;to:99;value:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?1:(root.associations[root.selectedAssociation].occurrence||1);onValueModified:{var list=Project.copy(root.associations);list[root.selectedAssociation].occurrence=value;root.associations=list}}
   ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:root.selectedAssociation<0||root.selectedAssociation>=root.associations.length?[]:root.associations[root.selectedAssociation].anchors
    delegate:ItemDelegate {width:parent.width;text:modelData.tick+" → "+root.time(modelData.ms)+"  ×";onClicked:{var list=Project.copy(root.associations);list[root.selectedAssociation].anchors.splice(index,1);root.associations=list}}
   }
   Desk.DeskButton {text:"移除此关联";onClicked:{var list=Project.copy(root.associations);list.splice(root.selectedAssociation,1);root.associations=list;root.selectedAssociation=list.length?0:-1;associationDialog.close()}}
  }
 }
 Files.FileDialog {id:audioDialog;title:"打开原曲";nameFilters:["音频 (*.wav *.mp3 *.flac)"];onAccepted:root.media.open(String(fileUrl));onRejected:root.relocating=false}
 Files.FileDialog {id:loadDialog;title:"打开工作区";nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.loadProject(String(fileUrl))}
 Files.FileDialog {id:saveDialog;title:"保存工作区";selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.saveProject(String(fileUrl))}
}
