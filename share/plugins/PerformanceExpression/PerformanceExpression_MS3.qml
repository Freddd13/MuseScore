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
 description:"演奏表达：把 MIDI 演奏的力度和速度用于选中的乐谱。"
 version:"1.3.0";requiresScore:true;pluginType:"dock";dockArea:"right"
 implicitWidth:440;implicitHeight:720
 property var observer:null
 property var editorContext:null
 property var audioProject:null
 property var captureSession:null
 property var captureState:captureSession?captureSession.state:({recording:false,stopping:false,saving:false,seconds:0,count:0,pitch:-1,velocity:0,device:""})
 property bool readForCapture:false
 property bool recordingImport:false
 property int captureEndMode:0
 property int captureDuration:30
 property bool captureMonitor:true
 function recordingPitch(p){return ["C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"][p%12]+(Math.floor(p/12)-1)}
 function saveCaptureOptions(){if(observer)observer.saveConfiguration("expression-recording",{endMode:captureEndMode,duration:captureDuration,monitor:captureMonitor})}
 property bool auditionAudio:false
 function publishComparison(){
  if(!observer||typeof observer.setEditorComparison!=="function"||stale)return;
  observer.setEditorComparison({revision:revision,result:result,candidate:candidate||{},selectedScoreId:selected>=0&&selected<result.matches.length?result.matches[selected].scoreId:"",scope:scope});
 }
 function editorRequest(action,scoreId,midiId){
  if(stale||busy)return;var index=result.matches.findIndex(function(m){return m.scoreId===scoreId});if(index>=0)selected=index;
  if(action==="select"){publishComparison();return;}
  if(action==="confirm"){confirm();return;}
  if(action==="anchor"){anchor();return;}
  if(action==="connect"){
   if(!scoreId||!midiId){status="先在编辑器点一个谱面音和一个演奏音，再连接。";return;}
   var list=Project.copy(links).filter(function(v){return v.scoreId!==scoreId&&v.midiId!==midiId});list.push({scoreId:scoreId,midiId:midiId});links=list;dirtyProject=true;pendingMatch=scoreId;pendingFeedback="已连接选定的两个音；乐谱没有改变。";runAlignment();return;
  }
  if(action==="unlink"){
   if(index<0)return;var m=result.matches[index],list=Project.copy(links).filter(function(v){return v.scoreId!==scoreId});list.push({scoreId:scoreId,midiId:m.midiId,disabled:true});links=list;dirtyProject=true;pendingFeedback="已解除这对音的对应。";runAlignment();
  }
 }
 onResultChanged:{publishComparison();candidateTimer.restart()}
 onCandidateChanged:publishComparison()
 onSelectedChanged:publishComparison()
 onStatusChanged:{if(editorContext)editorContext.feedback=status}
 Connections {target:root.editorContext;ignoreUnknownSignals:true;onRequested:root.editorRequest(action,scoreId,midiId)}
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
 property string previewWorkspace:""
 property string candidateConfig:""
 property var pendingAudition:null
 property int auditionWaits:0
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
 property int passageChoice:0
 property int occurrence:1
 property int selectedTrack:0
 property real fromSeconds:0
 property real untilSeconds:999999
 property bool whole:false
 property bool advancedSettings:false
 property var config:({strength:50,velocityStrength:50,rubatoStrength:50,velocity:true,rubato:false,keepDuration:true,accompaniment:false,deviceCenter:64,deviceGain:1,keepManual:false,unlocked:[]})
 property bool compact:height<170 || (typeof panelPlacement==="string"&&panelPlacement==="top")
 Desk.UiTheme {id:theme}
 function fail(e){status=Messages.readable(e)}
 function cancel(){var wasBusy=busy||captureState.recording||captureState.stopping||captureState.saving;readForCapture=false;if(captureSession)captureSession.cancel();pendingAudition=null;auditionRetry.stop();paging.stop();if(observer){observer.cancelReadOnlyJob();if(typeof observer.cancelPerformanceMidi==="function")observer.cancelPerformanceMidi()}busy=false;if(wasBusy)status="已取消，可以继续导入或录制。"}
 function runAlignment(){
  if(!midi||!notes.length){status="需要谱面起音与 MIDI";return}
  if(stale || observer.analysisRevision()!==revision){status="谱面已变化，请重新读取";return}
  candidate=null;busy=true;status="正在寻找并对应演奏…";
  token=observer.startReadOnlyJob(workerSource+"\nalign(input)",{notes:notes,performance:midi.notes,repeats:info.repeats,occurrence:occurrence,cacheKey:revision+":"+midi.sha256+":"+mode+":"+role+":"+selectedTrack+":"+fromSeconds+":"+untilSeconds+":"+occurrence,previousKey:result.cacheKey,segments:result.segments,from:fromSeconds,until:untilSeconds,role:role,track:selectedTrack,mode:mode===1?"tap":"score",passageChoice:passageChoice,anchors:manualAnchors,links:links});
 }
 function analyze(){if(!midi){status="请先导入 MIDI 或录制一段演奏";return}readScore(false)}
 function startRecording(){if(previewing&&!returnOriginal())return;readScore(true)}
 function beginRecording(){
  readForCapture=false;busy=false;
  if(!captureSession){status="更新软件后可以直接录制 MIDI；现在也可以导入 MIDI 文件。";return}
  var r=captureSession.start({endMode:["manual","duration","passage"][captureEndMode],duration:captureDuration,monitor:captureMonitor,notes:notes,start:scope.start,firstTrack:scope.firstTrack,tap:mode===1,role:role,track:selectedTrack});
  if(r.error){fail(r.error);return}busy=true;saveCaptureOptions();status="录制中，弹完后点击停止录制。";
 }
 function readScore(forCapture){
  if(observer.playing){status="请停播后分析";return}
  cancel();readForCapture=forCapture;observer.score=curScore;info=observer.workflowInfo();
  if(info.error){fail(info.error);return}
  scope=observer.analysisScope(whole);if(selectedTrack<scope.firstTrack||selectedTrack>=scope.endTrack)selectedTrack=scope.firstTrack;if(!scope.end){status="请选择目标谱段";return}
  notes=[];collecting=[];revision=observer.analysisRevision();nextTick=scope.start;stale=false;busy=true;status="正在读取所选谱段…";paging.start();
 }
 function slice(){
  if(!visible||observer.playing||observer.analysisRevision()!==revision){cancel();stale=true;status="任务暂停／谱面变化";return}
  var page=observer.performanceRange(scope.start,scope.end,scope.firstTrack,scope.endTrack,nextTick,batchSize,revision);
  if(page.error){cancel();fail(page.error);return}
  worstBatch=Math.max(worstBatch,page.milliseconds||0);if(page.milliseconds>2)batchSize=Math.max(1,Math.floor(batchSize/2));else if(page.milliseconds<.8)batchSize=Math.min(32,batchSize+1);
  Array.prototype.push.apply(collecting,page.notes||[]);nextTick=page.nextTick;
  if(page.done){notes=collecting;collecting=[];paging.stop();if(readForCapture)beginRecording();else runAlignment()}
 }
 function build(){
  if(!result.matches.length||stale){status="先导入演奏并对应谱段";return}
  candidateConfig=JSON.stringify(config);
  token=observer.startReadOnlyJob(workerSource+"\ncandidates(input)",{result:result,config:config,notes:notes,revision:revision,scope:scope});
  busy=true;status="正在计算新力度与速度…";
 }
 function editConfig(name,value){candidateTimer.restart();var c=Project.copy(config);c[name]=value;config=c;candidate=null;dirtyProject=true}
 function confirm(){
  if(selected<0||selected>=result.matches.length)return;
  var m=result.matches[selected];var l=Project.copy(links).filter(function(v){return v.scoreId!==m.scoreId&&v.midiId!==m.midiId});l.push({scoreId:m.scoreId,midiId:m.midiId});links=l;dirtyProject=true;pendingMatch=m.scoreId;pendingFeedback="已确认这对音，力度预览已更新。";runAlignment();
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
   if(pendingFeedback==="试听新效果"||pendingFeedback==="试听原版"){var use=pendingFeedback==="试听新效果";pendingFeedback="";audition(use);}
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
 function returnOriginal(){if(previewing&&audioProject&&audioProject.playing&&audioProject.joint)observer.stopReferencePlayback();var r=observer.returnWorkflowSource();if(r.error){fail(r.error);return false}previewing=false;status="已返回原谱，可以使用力度或继续比较。";return true}
 function stopAudition(){pendingAudition=null;auditionRetry.stop();if(previewing&&audioProject&&audioProject.playing&&audioProject.joint)observer.stopReferencePlayback();if(typeof observer.stopWorkflowPlayback!=="function"){status="请用软件上方的播放按钮停止。";return}var r=observer.stopWorkflowPlayback();if(r.error)fail(r.error);else status="试听已停止；可继续比较，或返回原谱。";}
 function applyPart(part){
  pendingAudition=null;auditionRetry.stop();
  if(!candidate||stale)return;
  if(previewing&&!returnOriginal())return;
  var values=Project.copy(candidate);if(part==="velocity")values.tempos=[];else values.velocity=[];
  var applied=observer.applyExpression(values);
  if(applied.error){fail(applied.error);return;}
  candidate=null;stale=true;status="已使用"+(part==="velocity"?"演奏力度":"演奏速度")+"，更新 "+applied.changed+" 项。Ctrl+Z 撤销。";
 }
 function apply(){
  if(!candidate||stale)return;
  if(typeof observer.applyExpression!=="function"){status="当前本体仅支持只读对应";return}
  var r=observer.applyExpression(candidate);if(r.error)fail(r.error);else {candidate=null;stale=true;status="已写入 "+r.changed+" 项 · 一次撤销；重新分析读取新值"}
 }
 function audition(useCandidate){pendingAudition=useCandidate;auditionWaits=0;advanceAudition()}
 function advanceAudition(){
  if(pendingAudition===null)return;
  if(previewing&&audioProject&&audioProject.playing&&audioProject.joint)observer.stopReferencePlayback();
  var stopped=observer.stopWorkflowPlayback();
  if(stopped.error){
   if(++auditionWaits<60){status="正在准备试听…";auditionRetry.restart();return;}
   pendingAudition=null;fail(stopped.error);return;
  }
  if(previewing&&!returnOriginal()){pendingAudition=null;return;}
  var useCandidate=pendingAudition;pendingAudition=null;beginAudition(useCandidate);
 }
 function beginAudition(useCandidate){
  if(!workspace && !previewWorkspace && typeof observer.temporaryWorkflowProject==="function")previewWorkspace=observer.temporaryWorkflowProject();
  var materials=workspace||previewWorkspace;
  if(!materials){pendingFeedback=useCandidate?"试听新效果":"试听原版";saveDialog.open();status="选择试听材料的保存位置。";return}
  if(typeof observer.previewExpression!=="function"){status="副本试听接口尚不可用";return}
  previewing=true;
  var range=Project.copy(scope);
  if(auditionAudio&&audioProject&&audioProject.available){var settings=audioProject.playbackSettings,track=audioProject.tracks[settings.selectedTrack||0],leader=track&&track.associations[settings.selectedAssociation||0];if(!leader){previewing=false;status="请先在原曲参考中选择一条关联。";return;}range.reference={tracks:audioProject.tracks,leader:leader,mode:settings.mode||0,speed:settings.speed||1,volume:settings.volume===undefined?.5:settings.volume,fade:settings.fade!==false,occurrence:occurrence};}
  var r=observer.previewExpression(materials,useCandidate?candidate:({}),range);if(r.error){previewing=false;fail(r.error)}else status=r.warning?Messages.readable(r.warning):"正在试听，点击“返回原谱”继续编辑。";
 }
 onRun:{
  if(typeof newWorkflowContext==="function")editorContext=newWorkflowContext();
  if(typeof newReferenceProject==="function")audioProject=newReferenceProject();
  if(typeof newScoreObserver!=="function"){status="需要支持 P1 的 Kumo 版本";return}
  observer=newScoreObserver();observer.score=curScore;
  if(typeof newMidiCaptureSession==="function")captureSession=newMidiCaptureSession();
  var recording=observer.loadConfiguration("expression-recording");if(recording){captureEndMode=Math.max(0,Math.min(2,recording.endMode||0));captureDuration=Math.max(1,Math.min(3600,recording.duration||30));captureMonitor=recording.monitor!==false;}
  workerSource=observer.readTextFile(String(Qt.resolvedUrl("Alignment.js")));
  if(typeof observer.performanceRange!=="function")status="需要支持 P1 演奏快照的版本";
 }
 onScoreStateChanged:{
  if(previewing)return;
  if(observer&&observer.score!==curScore)observer.score=curScore;
  if(notes.length&&observer&&observer.analysisRevision()!==revision){stale=true;candidate=null;cancel();status="乐谱已修改，点击“重新对应”更新效果"}
 }
 onVisibleChanged:if(!visible){cancel();if(observer&&typeof observer.cancelPerformanceMidi==="function")observer.cancelPerformanceMidi()}
 Component.onDestruction:{if(previewing&&observer)observer.returnWorkflowSource();cancel()}
 Connections {
  target:root.captureSession;ignoreUnknownSignals:true
  onFailed:{root.busy=false;root.fail(message)}
  onFinished:{
   root.recordingImport=true;root.loadedSourceHash="";root.manualAnchors=[];root.links=[];root.passageChoice=0;root.fromSeconds=0;root.untilSeconds=999999;root.busy=true;root.dirtyProject=true;
   root.midiToken=root.observer.readPerformanceMidi(path);root.status=reason+"，正在对应选段…";
  }
 }
 Timer {id:auditionRetry;interval:50;onTriggered:root.advanceAudition()}
 Timer {id:candidateTimer;interval:120;onTriggered:if(root.result.matches.length&&!root.busy&&!root.stale&&!root.previewing)root.build()}
 Timer {id:paging;interval:1;repeat:true;onTriggered:root.slice()}
 Connections {
  target:observer;ignoreUnknownSignals:true
  onPerformanceMidiReady:{
   if(token!==root.midiToken)return;
   if(result.error){root.busy=false;root.fail(result.error);return}
   if(root.loadedSourceHash&&root.loadedSourceHash!==result.sha256){root.busy=false;root.fail("项目原始 MIDI 已变化，请重新导入并核查人工对应");root.midi=null;return}
   root.busy=false;root.loadedSourceHash="";root.midi=result;root.status="已导入 "+result.notes.length+" 个音，正在寻找所选谱段";if(root.recordingImport){root.recordingImport=false;if(root.revision===root.observer.analysisRevision())root.runAlignment();else root.analyze()}else root.analyze();
  }
  onReadOnlyJobFinished:{
   if(token!==root.token)return;
   root.busy=false;if(result.error){root.fail(result.error);return}
   if(root.observer.analysisRevision()!==root.revision){root.stale=true;root.status="乐谱刚刚改变，正在更新对应";return}
   if(result.matches){root.result=result;root.selected=result.matches.length?0:-1;if(root.pendingMatch){for(var i=0;i<result.matches.length;++i)if(result.matches[i].scoreId===root.pendingMatch)root.selected=i;}root.pendingMatch="";root.status=(root.pendingFeedback?root.pendingFeedback+" ":"")+"已匹配 "+result.matches.length+" · 待确认 "+result.pending+" · 漏弹 "+result.missing.length+" · 多弹 "+result.extra.length;root.pendingFeedback=""}
   else{if(root.candidateConfig!==JSON.stringify(root.config)){root.build();return;}root.candidate=result;root.status="准备使用：力度 "+result.velocity.length+" 个音 · 速度 "+result.tempos.length+" 处 · 保留 "+result.kept.length+" 项"}
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
   Desk.DeskButton {text:"导入 MIDI";enabled:!root.busy&&!root.captureState.recording&&!root.captureState.stopping&&!root.captureState.saving;onClicked:midiDialog.open()}
   Desk.DeskButton {text:"项目";onClicked:projectMenu.open()}
   Desk.DeskButton {text:"悬浮";onClicked:{if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}}
  }
  RowLayout {
   Layout.fillWidth:true
   Desk.DeskButton {objectName:"expressionRecord";text:root.captureState.recording?"停止录制":"录制";enabled:root.captureSession!==null&&!root.captureState.stopping&&!root.captureState.saving&&(!root.busy||root.captureState.recording);onClicked:{if(root.captureState.recording)root.captureSession.stop();else root.startRecording()}}
   Desk.DeskComboBox {objectName:"expressionRecordEnd";Layout.fillWidth:true;model:["手动停止","到时停止","弹完选段停止"];currentIndex:root.captureEndMode;enabled:!root.captureState.recording&&!root.captureState.stopping;onActivated:{root.captureEndMode=currentIndex;root.saveCaptureOptions()}}
   Desk.DeskSpinBox {visible:root.captureEndMode===1;from:1;to:3600;value:root.captureDuration;onValueModified:{root.captureDuration=value;root.saveCaptureOptions()}}
   Label {visible:root.captureEndMode===1;text:"秒";color:theme.muted}
  }
  Label {visible:!!(root.captureState.recording||root.captureState.stopping||root.captureState.saving);Layout.fillWidth:true;Layout.preferredHeight:24;elide:Text.ElideRight;color:theme.accent;text:root.captureState.stopping?"正在收完最后的音…":root.captureState.saving?"正在保存录制…":(root.captureState.seconds||0).toFixed(1)+" 秒 · "+(root.captureState.count||0)+" 个音 · "+(root.captureState.pitch>=0?root.recordingPitch(root.captureState.pitch)+" · 力度 "+root.captureState.velocity:"等待键盘输入")}
  Label {Layout.fillWidth:true;Layout.preferredHeight:28;elide:Text.ElideRight;ToolTip.visible:statusHover.containsMouse;ToolTip.text:root.status;color:root.stale?theme.accent:theme.muted;text:root.status;MouseArea {id:statusHover;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.NoButton}}
  GridLayout {
   visible:root.advancedSettings&&!root.compact;columns:2;Layout.fillWidth:true
   CheckBox {text:"录制时听到键盘声音";checked:root.captureMonitor;onToggled:{root.captureMonitor=checked;root.saveCaptureOptions()}}
   Label {Layout.fillWidth:true;elide:Text.ElideRight;text:root.captureState.device||"使用软件当前 MIDI 输入";color:theme.muted}
   Desk.DeskComboBox {Layout.fillWidth:true;model:["按谱演奏（允许错漏）","任意音高敲击音序"];currentIndex:root.mode;onActivated:{root.mode=currentIndex;root.candidate=null}}
   Desk.DeskComboBox {Layout.fillWidth:true;model:["所选声部组","最高音旋律","指定谱表和声部"];currentIndex:root.role;onActivated:{root.role=currentIndex;root.candidate=null}}
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
   Desk.DeskButton {text:"重新对应";enabled:root.midi&&!root.busy&&!root.captureState.recording&&!root.captureState.stopping&&!root.captureState.saving;onClicked:root.analyze()}
   Desk.DeskButton {visible:root.advancedSettings;text:"重算对应";enabled:root.notes.length&&!root.busy&&!root.stale;onClicked:root.runAlignment()}
   Desk.DeskButton {text:root.advancedSettings?"收起设置":"更多设置";onClicked:root.advancedSettings=!root.advancedSettings}
   Desk.DeskButton {text:"编辑器对比";enabled:root.result.matches.length>0;onClicked:{if(typeof root.observer.showEditorComparison==="function")root.observer.showEditorComparison()}}
   Desk.DeskButton {text:"取消";enabled:root.busy;onClicked:root.cancel()}
  }
  Desk.DeskComboBox {objectName:"expressionPassageChoice";Layout.fillWidth:true;visible:!!(root.result.passage&&root.result.passage.candidates&&root.result.passage.candidates.length>1);model:root.result.passage&&root.result.passage.candidates?root.result.passage.candidates.map(function(v,i){return "演奏位置 "+(i+1)+" · "+v.from.toFixed(2)+"–"+v.until.toFixed(2)+" 秒"}):[];currentIndex:root.passageChoice;onActivated:{root.passageChoice=currentIndex;root.runAlignment()}}
  Canvas {
   id:timeline;visible:root.advancedSettings&&!root.compact;Layout.fillWidth:true;Layout.preferredHeight:100
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
   id:matches;objectName:"expressionMatches";visible:!root.compact;clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:100;model:root.result.matches;currentIndex:root.selected
   ScrollBar.vertical:ScrollBar {}
   delegate:ItemDelegate {
    width:parent.width;height:34;highlighted:index===root.selected
    text:"第 "+modelData.original.measure+" 小节 · "+(["C","C♯","D","E♭","E","F","F♯","G","A♭","A","B♭","B"][modelData.pitch%12]+(Math.floor(modelData.pitch/12)-1))+" → "+modelData.seconds.toFixed(3)+"s  "+"力度 "+modelData.velocity+(modelData.confidence<.8?" · 检查对应":"")
    onClicked:{root.selected=index;if(root.observer&&!root.observer.playing&&!root.previewing)root.locate()}
   }
  }
  RowLayout {
   visible:root.advancedSettings&&!root.compact;Layout.fillWidth:true
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
   Label {text:root.config.rubato?"力度 / 速度强度 %":"力度强度 %";color:theme.muted}
   RowLayout {Desk.DeskSpinBox {from:0;to:100;value:root.config.velocityStrength===undefined?root.config.strength:root.config.velocityStrength;onValueModified:root.editConfig("velocityStrength",value)}
   Desk.DeskSpinBox {visible:root.config.rubato;from:0;to:100;value:root.config.rubatoStrength===undefined?root.config.strength:root.config.rubatoStrength;onValueModified:root.editConfig("rubatoStrength",value)}}
   CheckBox {visible:root.config.rubato;text:"保持这段时长和两端速度";checked:root.config.keepDuration;onToggled:root.editConfig("keepDuration",checked)}
   CheckBox {visible:root.advancedSettings;text:"带动伴奏的力度走势";checked:root.config.accompaniment;onToggled:root.editConfig("accompaniment",checked)}
  }
  GridLayout {
   columns:root.width<600?2:4;visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {objectName:"expressionReturn";text:"返回原谱";visible:root.previewing;onClicked:{root.pendingAudition=null;auditionRetry.stop();root.returnOriginal()}}
   Desk.DeskButton {visible:root.advancedSettings;text:"更新效果";enabled:!root.busy&&!root.stale;onClicked:root.build()}
   Desk.DeskButton {objectName:"expressionOriginalAudition";text:"试听原来";enabled:!root.busy;onClicked:root.audition(false)}
   Desk.DeskButton {objectName:"expressionCandidateAudition";text:"试听新力度";enabled:root.candidate!==null&&!root.busy;onClicked:root.audition(true)}
   CheckBox {visible:root.advancedSettings || root.auditionAudio;text:"试听时播放参考音频";enabled:root.audioProject&&root.audioProject.available;checked:root.auditionAudio;onClicked:root.auditionAudio=checked}
   Desk.DeskButton {objectName:"expressionStop";text:"停止试听";visible:root.previewing;onClicked:root.stopAudition()}
   Desk.DeskButton {objectName:"expressionApplyVelocity";text:"使用力度";enabled:root.candidate!==null&&!root.busy&&!root.stale;onClicked:root.applyPart("velocity")}
   Desk.DeskButton {objectName:"expressionApplyTempo";text:"使用速度";visible:root.config.rubato;enabled:root.candidate!==null&&!root.busy&&!root.stale;onClicked:root.applyPart("tempo")}
   CheckBox {objectName:"expressionKeepManual";text:"保留手动调整";checked:root.config.keepManual===true;onClicked:root.editConfig("keepManual",checked)}
   Label {text:root.candidate?"将更新 "+root.candidate.velocity.length+" 个音，速度 "+root.candidate.tempos.length+" 处":"";color:theme.muted;Layout.fillWidth:true}
   Desk.DeskButton {text:"查看变化";enabled:root.candidate!==null;onClicked:changes.open()}
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
   Label {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"这里显示每个音将采用的力度。速度变化作用于这个位置的全部声部。"}
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
 Files.FileDialog {id:midiDialog;title:"导入演奏 MIDI（不打开新谱）";nameFilters:["MIDI (*.mid *.midi)"];onAccepted:{root.loadedSourceHash="";root.passageChoice=0;root.manualAnchors=[];root.links=[];root.candidate=null;root.midi=null;root.busy=true;root.status="正在读取演奏MIDI…";root.midiToken=root.observer.readPerformanceMidi(String(fileUrl))}}
 Files.FileDialog {id:loadDialog;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.load(String(fileUrl))}
 Files.FileDialog {id:saveDialog;selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.save(String(fileUrl))}
}
