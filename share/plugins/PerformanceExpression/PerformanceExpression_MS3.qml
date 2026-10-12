import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3 as Files
import MuseScore 3.0
import "../HarmonyAssistant" as Desk
import "../WorkflowCommon/Workspace.js" as Project
import "../WorkflowCommon/ActionMessages.js" as Messages
import "Alignment.js" as Rules
MuseScore {
    property string preferredPanelGroup: "rightTools"
 id:root
 menuPath:"Plugins.Performance Expression"
 description:"演奏表达：错漏音容错对应、力度与 rubato 的保护候选。"
 version:"1.0.0";requiresScore:true;pluginType:"dock";dockArea:"right"
 implicitWidth:440;implicitHeight:720
 property var observer:null
 property string workerSource:""
 property var midi:null
 property var scope:({})
 property var notes:[]
 property var collecting:[]
 property var manualAnchors:[]
 property var links:[]
 property var result:({matches:[],missing:[],extra:[],pending:0})
 property var candidate:null
 property var info:({})
 property string revision:""
 property string workspace:""
 property string status:"选择谱段并导入演奏 MIDI"
 property int token:0
 property int midiToken:0
 property int nextTick:0
 property int batchSize:12
 property real worstBatch:0
 property int selected:-1
 property bool busy:false
 property bool stale:false
 property bool previewing:false
 property bool dirtyProject:false
 property string pendingMatch:""
 property string pendingFeedback:""
 property string loadedSourceHash:""
 property int mode:0
 property int role:0
 property int occurrence:1
 property int selectedTrack:0
 property real fromSeconds:0
 property real untilSeconds:999999
 property bool whole:false
 property var config:({strength:50,velocityStrength:50,rubatoStrength:50,velocity:true,rubato:false,keepDuration:true,accompaniment:false,deviceCenter:64,deviceGain:1,unlocked:[]})
 property bool compact:height<170 || (typeof panelPlacement==="string"&&panelPlacement==="top")
 Desk.UiTheme {id:theme}
 function fail(e){status=Messages.readable(e)}
 function cancel(){paging.stop();if(observer){observer.cancelReadOnlyJob();if(typeof observer.cancelPerformanceMidi==="function")observer.cancelPerformanceMidi()}busy=false}
 function runAlignment(){
  if(!midi||!notes.length){status="需要谱面起音与 MIDI";return}
  if(stale || observer.analysisRevision()!==revision){status="谱面已变化，请重新读取";return}
  candidate=null;busy=true;status="后台容错对齐…";
  token=observer.startReadOnlyJob(workerSource+"\nalign(input)",{notes:notes,performance:midi.notes,repeats:info.repeats,occurrence:occurrence,cacheKey:revision+":"+midi.sha256+":"+mode+":"+role+":"+selectedTrack+":"+fromSeconds+":"+untilSeconds+":"+occurrence,previousKey:result.cacheKey,segments:result.segments,from:fromSeconds,until:untilSeconds,role:role,track:selectedTrack,mode:mode===1?"tap":"score",anchors:manualAnchors,links:links});
 }
 function analyze(){
  if(!midi){status="请先导入 MIDI";return}
  if(observer.playing){status="请停播后分析";return}
  cancel();observer.score=curScore;info=observer.workflowInfo();
  if(info.error){fail(info.error);return}
  scope=observer.analysisScope(whole);if(!scope.end){status="请选择目标谱段";return}
  notes=[];collecting=[];revision=observer.analysisRevision();nextTick=scope.start;stale=false;busy=true;status="分批读取完整声部…";paging.start();
 }
 function slice(){
  if(!visible||observer.playing||observer.analysisRevision()!==revision){cancel();stale=true;status="任务暂停／谱面变化";return}
  var page=observer.performanceRange(scope.start,scope.end,scope.firstTrack,scope.endTrack,nextTick,batchSize,revision);
  if(page.error){cancel();fail(page.error);return}
  worstBatch=Math.max(worstBatch,page.milliseconds||0);if(page.milliseconds>2)batchSize=Math.max(1,Math.floor(batchSize/2));else if(page.milliseconds<.8)batchSize=Math.min(32,batchSize+1);
  Array.prototype.push.apply(collecting,page.notes||[]);nextTick=page.nextTick;
  if(page.done){notes=collecting;collecting=[];paging.stop();runAlignment()}
 }
 function build(){
  if(!result.matches.length||stale){status="先完成可靠对应";return}
  token=observer.startReadOnlyJob(workerSource+"\ncandidates(input)",{result:result,config:config,notes:notes,revision:revision,scope:scope});
  busy=true;status="生成保护候选…";
 }
 function editConfig(name,value){var c=Project.copy(config);c[name]=value;config=c;candidate=null;dirtyProject=true}
 function confirm(){
  if(selected<0||selected>=result.matches.length)return;
  var m=result.matches[selected];var l=Project.copy(links).filter(function(v){return v.scoreId!==m.scoreId&&v.midiId!==m.midiId});l.push({scoreId:m.scoreId,midiId:m.midiId});links=l;dirtyProject=true;pendingMatch=m.scoreId;pendingFeedback="已确认这对音；乐谱没有改变。";runAlignment();
 }
 function anchor(){
  if(selected<0||selected>=result.matches.length)return;
  var m=result.matches[selected];var a=manualAnchors.filter(function(v){return v.scoreId!==m.scoreId});a.push({scoreId:m.scoreId,midiId:m.midiId});manualAnchors=a;dirtyProject=true;pendingMatch=m.scoreId;pendingFeedback="已固定这处对应，正在更新旁边的对应。";runAlignment();
 }
 function locate(){
  if(selected<0||selected>=result.matches.length)return;
  if(stale){status="乐谱已改变，请重新读取后定位。";return}
  if(previewing&&!returnOriginal())return;
  var m=result.matches[selected],target=Project.copy(m.original);target.start=m.tick;target.firstTrack=m.track;
  var r=observer.navigateWorkflow(target,false);if(r.error)fail(r.error);else status="已选中第 "+m.original.measure+" 小节的对应音；演奏位置 "+m.seconds.toFixed(3)+" 秒。";
 }
 function save(url){
  try{
   if(!midi)throw new Error("先导入演奏");
   var raw=Project.directory(url)+"performance-"+midi.sha256.substr(0,16)+".mid";
   if(typeof observer.savePerformanceSource==="function"){
    var saved=observer.savePerformanceSource(midi.path,Project.fileUrl(raw),midi.sha256);if(saved.error)throw new Error(saved.error);
   }else throw new Error("本体不支持原始 MIDI 保存");
   Project.writeModule(observer,url,"expression",{source:Project.relative(url,raw),sha256:midi.sha256,notes:notes,scope:scope,score:info,anchors:manualAnchors,links:links,mode:mode,role:role,track:selectedTrack,occurrence:occurrence,config:config,from:fromSeconds,until:untilSeconds},info);
   workspace=String(url);dirtyProject=false;status="项目与原始 MIDI 已保存";
   if(pendingFeedback==="试听拟采用结果"||pendingFeedback==="试听原版"){var use=pendingFeedback==="试听拟采用结果";pendingFeedback="";audition(use);}
  }catch(e){fail(e)}
 }
 function load(url){
  try{
   var data=Project.read(observer,url).expression;if(!data)throw new Error("无表达项目");
   workspace=String(url);loadedSourceHash=data.sha256||"";scope=data.scope;notes=data.notes||[];manualAnchors=data.anchors||[];links=data.links||[];mode=data.mode||0;role=data.role||0;selectedTrack=data.track||0;occurrence=data.occurrence||1;config=data.config||config;fromSeconds=data.from||0;untilSeconds=data.until||999999;
   info=data.score||{};revision=observer.analysisRevision();stale=true;
   midiToken=observer.readPerformanceMidi(Project.fileUrl(Project.resolve(url,data.source)));status="已恢复人工对应；重新读取谱面以验证候选";
  }catch(e){fail(e)}
 }
 function returnOriginal(){var r=observer.returnWorkflowSource();if(r.error){fail(r.error);return false}previewing=false;status="已返回原谱；拟采用结果还未写入。";return true}
 function stopAudition(){if(typeof observer.stopWorkflowPlayback!=="function"){status="请用软件上方的播放按钮停止。";return}var r=observer.stopWorkflowPlayback();if(r.error)fail(r.error);else status="试听已停止；可继续比较，或返回原谱。";}
 function apply(){
  if(!candidate||stale)return;
  if(typeof observer.applyExpression!=="function"){status="当前本体仅支持只读对应";return}
  var r=observer.applyExpression(candidate);if(r.error)fail(r.error);else {candidate=null;stale=true;status="已写入 "+r.changed+" 项 · 一次撤销；重新分析读取新值"}
 }
 function audition(useCandidate){
  if(!workspace){pendingFeedback=useCandidate?"试听拟采用结果":"试听原版";saveDialog.open();status="先选择项目保存位置，试听用的乐谱也放在那里。";return}
  if(typeof observer.previewExpression!=="function"){status="副本试听接口尚不可用";return}
  previewing=true;
  var r=observer.previewExpression(workspace,useCandidate?candidate:({}),scope);if(r.error){previewing=false;fail(r.error)}else status=r.warning?Messages.readable(r.warning):"正在试听。原谱没有改变；停止后可返回原谱。";
 }
 onRun:{
  if(typeof newScoreObserver!=="function"){status="需要支持 P1 的 Kumo 版本";return}
  observer=newScoreObserver();observer.score=curScore;
  workerSource=observer.readTextFile(String(Qt.resolvedUrl("Alignment.js")));
  if(typeof observer.performanceRange!=="function")status="需要支持 P1 演奏快照的版本";
 }
 onScoreStateChanged:{
  if(previewing)return;
  if(observer&&observer.score!==curScore)observer.score=curScore;
  if(notes.length&&observer&&observer.analysisRevision()!==revision){stale=true;candidate=null;cancel();status="谱面／会话变化 · 对应缓存待核查"}
 }
 onVisibleChanged:if(!visible){cancel();if(observer&&typeof observer.cancelPerformanceMidi==="function")observer.cancelPerformanceMidi()}
 Component.onDestruction:{if(previewing&&observer)observer.returnWorkflowSource();cancel()}
 Timer {id:paging;interval:1;repeat:true;onTriggered:root.slice()}
 Connections {
  target:observer;ignoreUnknownSignals:true
  onPerformanceMidiReady:{
   if(token!==root.midiToken)return;
   if(result.error){root.fail(result.error);return}
   if(root.loadedSourceHash&&root.loadedSourceHash!==result.sha256){root.fail("项目原始 MIDI 已变化，请重新导入并核查人工对应");root.midi=null;return}
   root.loadedSourceHash="";root.midi=result;root.status="MIDI 已载入 · "+result.notes.length+" 起音；选择范围并自动对齐";
  }
  onReadOnlyJobFinished:{
   if(token!==root.token)return;
   root.busy=false;if(result.error){root.fail(result.error);return}
   if(root.observer.analysisRevision()!==root.revision){root.stale=true;root.status="过期结果已拒绝";return}
   if(result.matches){root.result=result;root.selected=result.matches.length?0:-1;if(root.pendingMatch){for(var i=0;i<result.matches.length;++i)if(result.matches[i].scoreId===root.pendingMatch)root.selected=i;}root.pendingMatch="";root.status=(root.pendingFeedback?root.pendingFeedback+" ":"")+"已匹配 "+result.matches.length+" · 待确认 "+result.pending+" · 漏弹 "+result.missing.length+" · 多弹 "+result.extra.length;root.pendingFeedback=""}
   else{root.candidate=result;root.status="候选：力度 "+result.velocity.length+" · 速度 "+result.tempos.length+" · 保留 "+result.kept.length}
  }
 }
 Rectangle {anchors.fill:parent;color:theme.background}
 ColumnLayout {
  anchors.fill:parent;anchors.margins:8;spacing:6
  RowLayout {
   Layout.fillWidth:true
   Label {text:"演奏表达";font.bold:true;color:theme.text}
   Item {Layout.fillWidth:true}
   Desk.DeskButton {text:"停止";visible:root.compact&&root.previewing;onClicked:root.stopAudition()}
   Desk.DeskButton {text:"返回";visible:root.compact&&root.previewing;onClicked:root.returnOriginal()}
   Desk.DeskButton {text:"导入 MIDI";enabled:!root.busy;onClicked:midiDialog.open()}
   Desk.DeskButton {text:"项目";onClicked:projectMenu.open()}
   Desk.DeskButton {text:"悬浮";onClicked:{if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}}
  }
  Label {Layout.fillWidth:true;Layout.preferredHeight:28;elide:Text.ElideRight;color:root.stale?theme.accent:theme.muted;text:root.status}
  GridLayout {
   visible:!root.compact;columns:2;Layout.fillWidth:true
   Desk.DeskComboBox {Layout.fillWidth:true;model:["按谱演奏（允许错漏）","任意音高敲击音序"];currentIndex:root.mode;onActivated:{root.mode=currentIndex;root.candidate=null}}
   Desk.DeskComboBox {Layout.fillWidth:true;model:["所选声部组","最高音旋律","指定谱表 voice"];currentIndex:root.role;onActivated:{root.role=currentIndex;root.candidate=null}}
   Desk.DeskSpinBox {visible:root.role===2;from:root.scope.firstTrack||0;to:Math.max(from,(root.scope.endTrack||1)-1);value:root.selectedTrack;onValueModified:root.selectedTrack=value}
   Label {text:"反复第几次（0=全部）";color:theme.muted}
   Desk.DeskSpinBox {from:0;to:99;value:root.occurrence;onValueModified:root.occurrence=value}
   CheckBox {text:"全谱";checked:root.whole;onToggled:root.whole=checked}
   RowLayout {Layout.fillWidth:true
   Label {text:"演奏起秒";color:theme.muted}
   Desk.DeskTextField {Layout.fillWidth:true;placeholderText:"0";text:root.fromSeconds;onEditingFinished:root.fromSeconds=Math.max(0,Number(text)||0)}
   }
   RowLayout {Layout.fillWidth:true
   Label {text:"演奏止秒";color:theme.muted}
   Desk.DeskTextField {Layout.fillWidth:true;placeholderText:"至文件末尾";text:root.untilSeconds>=999999?"":root.untilSeconds;onEditingFinished:root.untilSeconds=Math.max(root.fromSeconds,Number(text)||999999)}
   }
  }
  RowLayout {
   Layout.fillWidth:true
   Desk.DeskButton {text:"自动对齐";enabled:root.midi&&!root.busy;onClicked:root.analyze()}
   Desk.DeskButton {text:"重算对应";enabled:root.notes.length&&!root.busy&&!root.stale;onClicked:root.runAlignment()}
   Desk.DeskButton {text:"取消";enabled:root.busy;onClicked:root.cancel()}
  }
  Canvas {
   id:timeline;visible:!root.compact;Layout.fillWidth:true;Layout.preferredHeight:100
   onWidthChanged:requestPaint()
   Connections {target:root;onResultChanged:timeline.requestPaint();onSelectedChanged:timeline.requestPaint()}
   onPaint:{
    var c=getContext("2d");c.clearRect(0,0,width,height);c.fillStyle=theme.field;c.fillRect(0,0,width,height);
    var m=root.result.matches;if(!m.length)return;var a=Infinity,b=-Infinity,from=Infinity,until=-Infinity;
    for(var i=0;i<m.length;++i){a=Math.min(a,m[i].tick);b=Math.max(b,m[i].tick);from=Math.min(from,m[i].seconds);until=Math.max(until,m[i].seconds);}
    var span=Math.max(1,b-a),seconds=Math.max(.1,until-from);c.fillStyle=theme.text;c.fillText("乐谱",8,14);c.fillText("演奏 "+from.toFixed(2)+"–"+until.toFixed(2)+" 秒",8,98);
    var step=Math.max(1,Math.ceil(m.length/200));
    function draw(i,selected){var v=m[i],x=16+(width-32)*(v.tick-a)/span,y=16+(width-32)*(v.seconds-from)/seconds;c.strokeStyle=selected?theme.accent:theme.line;c.lineWidth=selected?2:1;c.beginPath();c.moveTo(x,24);c.lineTo(y,76);c.stroke();c.fillStyle=selected?theme.accent:theme.muted;c.beginPath();c.arc(x,24,selected?4:2,0,Math.PI*2);c.arc(y,76,selected?4:2,0,Math.PI*2);c.fill();}
    for(i=0;i<m.length;i+=step)if(i!==root.selected)draw(i,false);if(root.selected>=0&&root.selected<m.length)draw(root.selected,true);
   }
  }
  ListView {
   id:matches;visible:!root.compact;clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:100;model:root.result.matches;currentIndex:root.selected
   ScrollBar.vertical:ScrollBar {}
   delegate:ItemDelegate {
    width:parent.width;height:34;highlighted:index===root.selected
    text:"第 "+modelData.original.measure+" 小节 · "+(["C","C♯","D","E♭","E","F","F♯","G","A♭","A","B♭","B"][modelData.pitch%12]+(Math.floor(modelData.pitch/12)-1))+" → "+modelData.seconds.toFixed(3)+"s  "+(modelData.confirmed?"已确认":modelData.confidence<.8?"待确认":modelData.status)
    onClicked:root.selected=index
   }
  }
  RowLayout {
   visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {objectName:"expressionLocate";text:"定位对应音";enabled:root.selected>=0&&!root.busy;onClicked:root.locate()}
   Desk.DeskButton {objectName:"expressionConfirm";text:"确认对应";enabled:root.selected>=0&&!root.stale&&!root.busy;onClicked:root.confirm()}
   Desk.DeskButton {text:"设锚点";enabled:root.selected>=0&&!root.stale;onClicked:root.anchor()}
   Desk.DeskButton {text:"校正…";enabled:root.notes.length>0;onClicked:correction.open()}
  }
  Rectangle {visible:!root.compact;Layout.fillWidth:true;height:1;color:theme.line}
  GridLayout {
   visible:!root.compact;columns:2;Layout.fillWidth:true
   CheckBox {text:"力度";checked:root.config.velocity;onToggled:root.editConfig("velocity",checked)}
   CheckBox {text:"Rubato（全声部速度）";checked:root.config.rubato;onToggled:root.editConfig("rubato",checked)}
   Label {text:"力度 / Rubato 强度 %";color:theme.muted}
   RowLayout {Desk.DeskSpinBox {from:0;to:100;value:root.config.velocityStrength===undefined?root.config.strength:root.config.velocityStrength;onValueModified:root.editConfig("velocityStrength",value)}
   Desk.DeskSpinBox {from:0;to:100;value:root.config.rubatoStrength===undefined?root.config.strength:root.config.rubatoStrength;onValueModified:root.editConfig("rubatoStrength",value)}}
   CheckBox {text:"保留段长和边界";checked:root.config.keepDuration;onToggled:root.editConfig("keepDuration",checked)}
   CheckBox {text:"推算伴奏趋势";checked:root.config.accompaniment;onToggled:root.editConfig("accompaniment",checked)}
  }
  GridLayout {
   columns:root.width<600?2:4;visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {objectName:"expressionReturn";text:"返回原谱";visible:root.previewing;onClicked:root.returnOriginal()}
   Desk.DeskButton {text:"生成拟采用结果";enabled:!root.busy&&!root.stale;onClicked:root.build()}
   Desk.DeskButton {objectName:"expressionOriginalAudition";text:"试听原版";enabled:!root.busy;onClicked:root.audition(false)}
   Desk.DeskButton {text:"试听拟采用结果";enabled:root.candidate!==null&&!root.busy;onClicked:root.audition(true)}
   Desk.DeskButton {objectName:"expressionStop";text:"停止试听";visible:root.previewing;onClicked:root.stopAudition()}
   Desk.DeskButton {text:"差异／应用";enabled:root.candidate!==null;onClicked:changes.open()}
  }
 }
 Menu {id:projectMenu
  MenuItem {text:"打开项目";onTriggered:loadDialog.open()}
  MenuItem {text:"保存项目";onTriggered:{if(root.workspace)root.save(root.workspace);else saveDialog.open()}}
  MenuItem {text:"另存项目";onTriggered:saveDialog.open()}
  MenuItem {text:"设备校准";onTriggered:calibration.open()}
 }
 Dialog {
  id:correction;title:"人工对应与锚点";modal:true;width:380;height:320;standardButtons:Dialog.Close
  ColumnLayout {anchors.fill:parent
   Label {text:"选择谱音与演奏事件，可重新连接或标记漏／多弹"}
   Desk.DeskComboBox {id:scoreChoice;Layout.fillWidth:true;model:root.result.matches.map(function(m){return m.original}).concat(root.result.missing).map(function(n){return n.id+" · "+n.pitch});}
   Desk.DeskComboBox {id:midiChoice;Layout.fillWidth:true;model:root.midi?root.midi.notes.map(function(n){return n.id+" · "+n.seconds.toFixed(3)+"s · "+n.pitch}):[]}
   RowLayout {
    Desk.DeskButton {text:"重新连接";onClicked:{var ns=root.result.matches.map(function(m){return m.original}).concat(root.result.missing),ms=root.midi.notes;if(scoreChoice.currentIndex<0||midiChoice.currentIndex<0)return;var l=root.links.filter(function(v){return v.scoreId!==ns[scoreChoice.currentIndex].id&&v.midiId!==ms[midiChoice.currentIndex].id});l.push({scoreId:ns[scoreChoice.currentIndex].id,midiId:ms[midiChoice.currentIndex].id});root.links=l;root.runAlignment()}}
    Desk.DeskButton {text:"取消对应";onClicked:{if(root.selected<0)return;var m=root.result.matches[root.selected],l=Project.copy(root.links);l.push({scoreId:m.scoreId,midiId:m.midiId,disabled:true});root.links=l;root.runAlignment()}}
   }
   Desk.DeskButton {text:"移除所有人工锚点";onClicked:{root.manualAnchors=[];root.runAlignment()}}
   Label {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"漏弹 "+root.result.missing.map(function(n){return n.id}).join(", ")+"\n多弹 "+root.result.extra.map(function(n){return n.id}).join(", ");elide:Text.ElideRight;maximumLineCount:5}
  }
 }
 Dialog {
  id:calibration;title:"设备响应校准";modal:true;width:320;height:230;standardButtons:Dialog.Close
  ColumnLayout {anchors.fill:parent
   Label {text:"中心力度（映射到64）"}
 Desk.DeskSpinBox {from:1;to:127;value:root.config.deviceCenter;onValueModified:root.editConfig("deviceCenter",value)}
   Label {text:"响应斜率 %"}
 Desk.DeskSpinBox {from:25;to:200;value:Math.round(root.config.deviceGain*100);onValueModified:root.editConfig("deviceGain",value/100)}
  }
 }
 Dialog {
  id:changes;title:"写回差异与保护";modal:true;width:480;height:520;standardButtons:Dialog.Close
  ColumnLayout {anchors.fill:parent
   Label {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"速度影响所有声部。未确认对应不写入；已有手工值默认保护。试听请先保存项目。"}
   Desk.DeskComboBox {id:curveKind;model:["力度曲线（原值 / 候选）","速度曲线（原值 / 候选）"];onActivated:expressionCurve.requestPaint()}
   Canvas {id:expressionCurve;Layout.fillWidth:true;Layout.preferredHeight:120
    onWidthChanged:requestPaint()
    Connections {target:root;onCandidateChanged:expressionCurve.requestPaint()}
    onPaint:{
     var c=getContext("2d");c.clearRect(0,0,width,height);c.fillStyle=theme.field;c.fillRect(0,0,width,height);
     if(!root.candidate)return;
     var values=curveKind.currentIndex===0?root.candidate.velocity:root.candidate.tempos;if(!values.length)return;
     values=values.slice().sort(function(a,b){return a.tick-b.tick});
     var start=root.scope.start,end=root.scope.end,max=127;
     if(curveKind.currentIndex===1)max=Math.max.apply(Math,values.map(function(v){return Math.max(v.oldBpm,v.bpm)}))*1.1;
     for(var k=0;k<2;++k){c.strokeStyle=k?theme.accent:theme.muted;c.lineWidth=k?2:1;c.beginPath();
      for(var i=0;i<values.length;++i){var v=values[i],value=curveKind.currentIndex===0?(k?v.target:(v.oldType===1?v.oldRaw:Math.round(v.oldBase*(1+v.oldRaw/100)))):(k?v.bpm:v.oldBpm),x=8+(width-16)*(v.tick-start)/Math.max(1,end-start),y=height-18-(height-28)*value/max;if(i)c.lineTo(x,y);else c.moveTo(x,y)}c.stroke();
     }
     c.fillStyle=theme.muted;c.fillText("原值",8,12);c.fillStyle=theme.accent;c.fillText("候选",55,12);
    }
   }
   ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:root.candidate?root.candidate.velocity.concat(root.candidate.tempos).concat(root.candidate.kept):[]
    delegate:ItemDelegate {width:parent.width;text:modelData.reason||((modelData.source==="inferred"?"推算 · ":"实测 · ")+(modelData.id||("tick "+modelData.tick))+" · 原 "+(modelData.oldBpm||modelData.oldBase)+" → "+(modelData.target||modelData.bpm)+ (modelData.target?"（写回 "+(modelData.oldType===1?modelData.target:Math.round((modelData.target-modelData.oldBase)*100/modelData.oldBase))+"）":""));onClicked:{if(!modelData.id)return;var c=Project.copy(root.config);if(c.unlocked.indexOf(modelData.id)<0)c.unlocked.push(modelData.id);root.config=c;root.candidate=null;changes.close();root.build()}}
   }
   Desk.DeskButton {text:"应用到当前原谱（一次撤销）";enabled:root.candidate!==null&&!root.stale;onClicked:{root.apply();changes.close()}}
  }
 }
 Files.FileDialog {id:midiDialog;title:"导入演奏 MIDI（不打开新谱）";nameFilters:["MIDI (*.mid *.midi)"];onAccepted:{root.manualAnchors=[];root.links=[];root.candidate=null;root.midiToken=root.observer.readPerformanceMidi(String(fileUrl))}}
 Files.FileDialog {id:loadDialog;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.load(String(fileUrl))}
 Files.FileDialog {id:saveDialog;selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.save(String(fileUrl))}
}
