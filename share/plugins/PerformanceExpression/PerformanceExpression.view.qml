import "ExpressionSettings.js" as ExpressionSettings
import QtQuick 2.9
import QtQuick.Window 2.2
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3 as Files
import MuseScore 3.0
import "../HarmonyAssistant" as Desk
import "../WorkflowCommon/Workspace.js" as Project
import "../WorkflowCommon/ActionMessages.js" as Messages
import "Alignment.js" as Rules
import "ExpressionProgress.js" as Progress
MuseScore {
    property string preferredPanelGroup: "rightTools"
 id:root
 property bool withinEditor:typeof workflowEmbedded!=="undefined" && workflowEmbedded
 property bool embeddedWorkflow:(typeof workflowNativeTools!=="undefined"&&workflowNativeTools)||withinEditor
 property bool nativeUiNames:true
 property var embeddedActiveDialog:null
 Window {
  id:embeddedDialogWindow;title:"演奏表达";visible:false;flags:Qt.Dialog
  color:theme.background
  onClosing:{if(root.embeddedActiveDialog)root.embeddedActiveDialog.close()}
 }
 Connections {target:root.embeddedActiveDialog;ignoreUnknownSignals:true;onClosed:embeddedDialogWindow.hide()}
 function openToolDialog(dialog){
  if(!embeddedWorkflow){dialog.open();return;}
  embeddedActiveDialog=dialog;dialog.parent=embeddedDialogWindow.contentItem;
  var w=Math.max(380,dialog.width||dialog.implicitWidth||430),h=Math.max(120,dialog.height||dialog.implicitHeight||300);
  embeddedDialogWindow.width=w+16;embeddedDialogWindow.height=Math.min(h+16,760);dialog.x=8;dialog.y=8;
  if(h>744)dialog.height=744;
  embeddedDialogWindow.show();dialog.open();embeddedDialogWindow.raise();embeddedDialogWindow.requestActivate();
 }

 menuPath:"Plugins.Performance Expression"
 description:"演奏表达：把 MIDI 演奏的力度和速度用于选中的乐谱。"
 version:"1.5.1";requiresScore:true;pluginType:"dock";dockArea:"right"
 implicitWidth:embeddedWorkflow?700:440;implicitHeight:embeddedWorkflow?180:720
 property var observer:null
 property var editorContext:null
 property var liveSelection:({scope:{},notes:[]})
 property var scopedCandidate:candidate?Progress.portion(candidate,applicationScope,liveSelection):({velocity:[],tempos:[]})
 onApplicationScopeChanged:publishComparison()
 onLiveSelectionChanged:publishComparison()
 property string editorOwner:"expression-"+Date.now()+"-"+Math.random()
 property var audioProject:null
 property var captureSession:null
 property var captureState:captureSession?captureSession.state:({recording:false,stopping:false,saving:false,seconds:0,count:0,pitch:-1,velocity:0,device:""})
 property bool readForCapture:false
 property bool recordingImport:false
 property bool alignmentOnShow:false
 property int captureEndMode:0
 property int captureDuration:30
 property bool captureMonitor:true
 property var midiInputInfo:({})
 onCaptureStateChanged:if(captureState.input)midiInputInfo=captureState.input
 property var retainedScope:null
 property var scopeOwner:null
 property var displayedScope:retainedScope||liveSelection.scope||scope
 property string targetDescription:displayedScope&&displayedScope.firstMeasure ? "第 "+displayedScope.firstMeasure+"–"+displayedScope.lastMeasure+" 小节"+(displayedScope.partial&&displayedScope.firstBeat!==undefined?" · 第 "+Number(displayedScope.firstBeat.toFixed(3))+" 拍起，至第 "+displayedScope.endMeasure+" 小节"+(displayedScope.endAtBar?"末尾":"第 "+Number(displayedScope.endBeat.toFixed(3))+" 拍前"):"")+" · "+(displayedScope.part||"乐谱")+" · 谱表 "+(Math.floor(displayedScope.firstTrack/4)+1)+"–"+Math.ceil(displayedScope.endTrack/4) : "选中要演奏的一段，再点“当前选区”"
 function sameScore(a,b){return a&&b&&(a===b||a.is(b))}
 function prepareEditing(){pendingAudition=null;auditionRetry.stop();return !previewing||returnOriginal()}
 function readTarget(useWhole){
  var selection=observer.expressionSelection(),range=Project.copy(useWhole?observer.analysisScope(true):selection.scope)
  if(!useWhole&&!range.exact&&(selection.rangeSelected||(selection.notes||[]).length>1)&&typeof observer.arrangementState==="function"){
   var state=observer.arrangementState({}),exact=state.scope
   if(exact&&exact.end>exact.start){range.partial=exact.start!==range.start||exact.end!==range.end;["start","end","firstTrack","endTrack","firstMeasure","lastMeasure","noncontiguous"].forEach(function(key){range[key]=exact[key]})}
  }
  return range
 }
 function chooseTarget(useWhole){
  if(!observer||!curScore){status="打开乐谱后，选择要演奏的段落。";return}
  if(!prepareEditing())return
  whole=useWhole;observer.score=curScore;retainedScope=readTarget(useWhole);scopeOwner=curScore;scope=retainedScope
  candidate=null;result=({matches:[],missing:[],extra:[],pending:0});notes=[];selected=-1
  status="目标已设为："+targetDescription
  if(midi)readScore(false)
 }
 function recordingPitch(p){return ["C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"][p%12]+(Math.floor(p/12)-1)}
 function saveCaptureOptions(){if(observer)observer.saveConfiguration("expression-recording",{endMode:captureEndMode,duration:captureDuration,monitor:captureMonitor})}
 property bool auditionAudio:false
 property bool auditionLoop:false
 property string publishedComparisonKey:""
 function publishComparison(){
  if(!observer||typeof observer.setEditorComparison!=="function"||stale)return;
  var state={owner:editorOwner,revision:revision,selectedMidiId:selectedMidiId,previewCanUndo:previewUndo.length>0,previewCanRedo:previewRedo.length>0,busy:busy||(candidate!==null&&previewUpdating),applyCounts:{velocity:scopedCandidate.velocity.length,tempos:scopedCandidate.tempos.length},selectedScoreId:selected>=0&&selected<result.matches.length?result.matches[selected].scoreId:""};
  var key=revision+":"+previewVersion+":"+resultVersion;
  if(publishedComparisonKey===key&&typeof observer.updateEditorComparisonState==="function"){
   var updated=observer.updateEditorComparisonState(state);if(updated.ok)return;
  }
  state.previewVersion=previewVersion;state.resultVersion=resultVersion;state.result=displayed.result;state.candidate=candidate||{};state.scope=scope;
  var sent=observer.setEditorComparison(state);if(sent.ok)publishedComparisonKey=key;
 }
 function editorRequest(action,scoreId,midiId){
  if(!editorContext || editorContext.expression.owner!==editorOwner)return;
  if(stale||busy||(candidate&&previewUpdating)){status=(busy||previewUpdating)?"正在对应演奏，请等结果出现后继续。":"乐谱已改变，请重新对应这一段。";return;}
  if(action==="applyVelocity" || action==="applyTempo"){if(!candidate){status="先完成演奏对应，再使用预览里的力度或速度。";return;}applyPart(action==="applyVelocity"?"velocity":"tempo");return;}
  if(action==="auditionOriginal" || action==="auditionCandidate"){audition(action==="auditionCandidate");return;}
  if(action==="undoPreview"||action==="redoPreview"){
   var backward=action==="undoPreview",history=(backward?previewUndo:previewRedo).slice();
   if(!history.length)return;
   var current={config:Project.copy(config)},previous=history.pop();
   if(backward){previewUndo=history;previewRedo=previewRedo.concat([current]);}else{previewRedo=history;previewUndo=previewUndo.concat([current]);}
   candidateConfig="";config=previous.config;candidateTimer.restart();dirtyProject=true;status=backward?"已撤销上一次预览调整。":"已重做预览调整。";return;
  }
  if(action==="velocityTargets"||action==="tempoTargets"){
   var values;try{values=JSON.parse(midiId)}catch(e){status="请重新拖动预览中的音。";return;}
   pushPreviewUndo();
   var next=Project.copy(config),key=action==="velocityTargets"?"previewVelocity":"previewTempo",updates=next[key]||{};
   values.forEach(function(v){updates[action==="velocityTargets"?v.id:v.tick]=action==="velocityTargets"?v.target:v.bpm;});next[key]=updates;config=next;candidateConfig=JSON.stringify(config);candidate=Progress.targets(candidate,config);dirtyProject=true;status="已调整拟采用效果。试听后点击使用力度或使用速度。";return;
  }
  var index=result.matches.findIndex(function(m){return m.scoreId===scoreId});if(index>=0)selected=index;
  if(action==="select"){selectedMidiId=midiId;publishComparison();return;}
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
 onCandidateChanged:{previewVersion++;publishComparison();if(candidate&&previewing&&!legacyAudition&&playbackState.playing&&playbackState.mode==="candidate"&&!applying&&!refreshingApplication){pendingAudition=true;auditionRetry.restart()}}
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
 property int previewVersion:0
 property int resultVersion:0
 property string selectedMidiId:""
 property var previewUndo:[]
 property var previewRedo:[]
 onPreviewUndoChanged:publishComparison()
 onPreviewRedoChanged:publishComparison()
 property var appliedProgress:({velocity:{},tempos:{}})
 property bool showApplied:false
 property var displayed:Progress.display(result,appliedProgress,showApplied)
 onDisplayedChanged:{resultVersion++;publishComparison()}
 property int applicationScope:1
 property bool applying:false
 property bool refreshingApplication:false
 property string applicationMessage:""
 property int savedVelocityCount:0
 property var info:({})
 property string revision:""
 property string workspace:""
 property string previewWorkspace:""
 property string candidateConfig:""
 property bool previewUpdating:candidateConfig!==JSON.stringify(config)
 onPreviewUpdatingChanged:publishComparison()
 onConfigChanged:publishComparison()
 onBusyChanged:publishComparison()
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
 property bool legacyAudition:false
 property var playbackState:editorContext&&editorContext.audition?editorContext.audition:({})
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
 property var config:ExpressionSettings.defaults()
 property bool compact:height<170 || (typeof panelPlacement==="string"&&panelPlacement==="top")
 Desk.UiTheme {id:theme}
 function openMenu(menu,control){if(root.embeddedWorkflow){root.openToolDialog(menu);return;}var where=control.mapToItem(root,0,control.height);menu.x=Math.max(0,Math.min(where.x,root.width-menu.implicitWidth));menu.y=Math.max(0,Math.min(where.y,root.height-menu.implicitHeight));menu.open()}
 function fail(e){status=Messages.readable(e)}
 function freshVelocitySettings(){var next=Project.copy(config);next.velocityStrength=100;next.velocityWriteMode="fixed";config=next}
 function velocityWriteText(item){var mode=item.writeMode||"preserve";return mode==="fixed"||mode==="preserve"&&item.oldType===1?"固定力度 "+item.target:"跟随力度记号 "+Math.round((item.target-item.oldBase)*100/item.oldBase)+"%"}
 function cancel(){var wasBusy=busy||captureState.recording||captureState.stopping||captureState.saving;readForCapture=false;refreshingApplication=false;if(captureSession)captureSession.cancel();pendingAudition=null;auditionRetry.stop();paging.stop();if(observer){observer.cancelReadOnlyJob();if(typeof observer.cancelPerformanceMidi==="function")observer.cancelPerformanceMidi()}busy=false;if(wasBusy)status="已取消，可以继续导入或录制。"}
 function runAlignment(){
  if(!midi||!notes.length){status="需要谱面起音与 MIDI";return}
  if(stale || observer.analysisRevision()!==revision){status="谱面已变化，请重新读取";return}
  candidate=null;busy=true;status="正在寻找并对应演奏…";
  token=observer.startReadOnlyJob(workerSource+"\nalign(input)",{notes:notes,performance:midi.notes,repeats:info.repeats,occurrence:occurrence,cacheKey:revision+":"+scope.start+":"+scope.end+":"+scope.firstTrack+":"+scope.endTrack+":"+midi.sha256+":"+mode+":"+role+":"+selectedTrack+":"+fromSeconds+":"+untilSeconds+":"+occurrence,previousKey:result.cacheKey,segments:result.segments,from:fromSeconds,until:untilSeconds,role:role,track:selectedTrack,mode:mode===1?"tap":"score",passageChoice:passageChoice,anchors:manualAnchors,links:links});
 }
 function importMidi(path){
  if(!path)return;
  if(!prepareEditing())return;
  freshVelocitySettings();appliedProgress=({velocity:{},tempos:{}});loadedSourceHash="";passageChoice=0;manualAnchors=[];links=[];candidate=null;midi=null;busy=true;status="正在读取演奏 MIDI…";midiToken=observer.readPerformanceMidi(String(path));
 }
 function chooseMidi(){
  if(root.embeddedWorkflow&&typeof root.choosePerformanceMidi==="function")root.importMidi(root.choosePerformanceMidi());
  else midiDialog.open();
 }
 function analyze(){if(!prepareEditing())return;if(!midi){status="请先导入 MIDI 或录制一段演奏";return}readScore(false)}
 function startRecording(){if(!prepareEditing())return;readScore(true)}
 function beginRecording(){
  readForCapture=false;busy=false;
  if(!notes.length){status="这段没有起音，请选择有音符的谱段后录制。";return}
  if(!captureSession){status="更新软件后可以直接录制 MIDI；现在也可以导入 MIDI 文件。";return}
  var r=captureSession.start({endMode:["manual","duration","passage"][captureEndMode],duration:captureDuration,monitor:captureMonitor,notes:notes,start:scope.start,firstTrack:scope.firstTrack,tap:mode===1,role:role,track:selectedTrack});
  if(r.error){fail(r.error);return}busy=true;saveCaptureOptions();status="录制中，弹完后点击停止录制。";
 }
 function readScore(forCapture){
  if(observer.playing){status="请停播后分析";return}
  cancel();readForCapture=forCapture;observer.score=curScore;info=observer.workflowInfo();
  if(info.error){fail(info.error);return}
  scope=retainedScope&&sameScore(scopeOwner,curScore)?Project.copy(retainedScope):readTarget(whole);retainedScope=Project.copy(scope);scopeOwner=curScore;if(selectedTrack<scope.firstTrack||selectedTrack>=scope.endTrack)selectedTrack=scope.firstTrack;if(!scope.end){status="请选择目标谱段";return}
  notes=[];collecting=[];revision=observer.analysisRevision();nextTick=scope.start;stale=false;busy=true;status="正在读取所选谱段…";paging.start();
 }
 function refreshHistory(){if(observer&&typeof observer.expressionVelocityHistory==="function")savedVelocityCount=observer.expressionVelocityHistory({}).count||0}
 function refreshLiveSelection(){if(!observer||typeof observer.expressionSelection!=="function")return;var selection=observer.expressionSelection();if(!selection.rangeSelected)selection.scope=retainedScope||scope;liveSelection=selection}
 onScopeChanged:refreshLiveSelection()
 function currentApplication(){refreshLiveSelection();return Progress.portion(candidate,applicationScope,liveSelection)}
 function readApplied(message){
  previewUndo=[];previewRedo=[];applicationMessage=message;refreshingApplication=true;revision=observer.analysisRevision();collecting=[];nextTick=scope.start;busy=true;stale=false;candidate=null;paging.start();
 }
 function restoreVelocity(replace){
  if(!prepareEditing())return;
  if(observer.playing){status="停止播放后可以恢复或保存力度。";return;}
  var selection=observer.expressionSelection();var range=applicationScope===2?({}):Project.copy(applicationScope===1&&!selection.rangeSelected?(retainedScope||scope):selection.scope);
  range.selectedOnly=applicationScope===0;range.revision=observer.analysisRevision();
  applying=true;var r=replace?observer.saveCurrentExpressionVelocity(range):observer.restoreExpressionVelocity(range);applying=false;
  if(r.error){fail(r.error);return;}
  refreshHistory();
  var text=replace?"已把当前力度保存为比较起点":"已恢复应用前的力度";
  if(notes.length)readApplied(text+" · "+r.changed+" 个音。Ctrl+Z 撤销。");else status=text+" · "+r.changed+" 个音。";
 }
 function slice(){
  if(!visible||observer.playing||observer.analysisRevision()!==revision){cancel();stale=true;status="任务暂停／谱面变化";return}
  var page=observer.performanceRange(scope.start,scope.end,scope.firstTrack,scope.endTrack,nextTick,batchSize,revision);
  if(page.error){cancel();fail(page.error);return}
  worstBatch=Math.max(worstBatch,page.milliseconds||0);if(page.milliseconds>2)batchSize=Math.max(1,Math.floor(batchSize/2));else if(page.milliseconds<.8)batchSize=Math.min(32,batchSize+1);
  Array.prototype.push.apply(collecting,page.notes||[]);nextTick=page.nextTick;
  if(page.done){notes=collecting;collecting=[];paging.stop();appliedProgress=Progress.reconcile(appliedProgress,notes);
   if(refreshingApplication){result=Progress.refreshed(result,notes);refreshingApplication=false;busy=false;status=applicationMessage;refreshHistory();candidateTimer.stop();build();}
   else if(readForCapture)beginRecording();else runAlignment();
  }
 }
 function build(){
  if(!result.matches.length||stale){status="先导入演奏并对应谱段";return}
  candidateConfig=JSON.stringify(config);
  token=observer.startReadOnlyJob(workerSource+"\ncandidates(input)",{result:result,config:config,notes:notes,revision:revision,scope:scope});
  busy=true;status="正在计算新力度与速度…";
 }
 function pushPreviewUndo(){if(candidate){previewUndo=previewUndo.concat([{config:Project.copy(config)}]).slice(-40);previewRedo=[];}}
 function editConfig(name,value){
  if(!prepareEditing())return;pushPreviewUndo();candidateTimer.restart();var c=Project.copy(config);c[name]=value;config=c;dirtyProject=true;status="正在更新拟采用的力度与速度…";publishComparison()}
 function confirm(){
  if(!prepareEditing())return;
  if(selected<0||selected>=result.matches.length)return;
  var m=result.matches[selected];var l=Project.copy(links).filter(function(v){return v.scoreId!==m.scoreId&&v.midiId!==m.midiId});l.push({scoreId:m.scoreId,midiId:m.midiId});links=l;dirtyProject=true;pendingMatch=m.scoreId;pendingFeedback="已确认这对音，力度预览已更新。";runAlignment();
 }
 function anchor(){
  if(!prepareEditing())return;
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
   Project.writeModule(observer,url,"expression",{source:Project.relative(url,raw),sha256:midi.sha256,notes:notes,scope:scope,score:info,anchors:manualAnchors,links:links,applied:appliedProgress,mode:mode,role:role,track:selectedTrack,occurrence:occurrence,config:config,from:fromSeconds,until:untilSeconds},info);
   workspace=String(url);dirtyProject=false;status="项目与原始 MIDI 已保存";
   if(pendingFeedback==="试听新效果"||pendingFeedback==="试听原版"){var use=pendingFeedback==="试听新效果";pendingFeedback="";audition(use);}
  }catch(e){fail(e)}
 }
 function load(url){
  if(!prepareEditing())return;
  try{
   var data=Project.read(observer,url).expression;if(!data)throw new Error("无表达项目");
   workspace=String(url);retainedScope=Project.copy(data.scope);scopeOwner=curScore;appliedProgress=data.applied||({velocity:{},tempos:{}});loadedSourceHash=data.sha256||"";scope=data.scope;notes=data.notes||[];manualAnchors=data.anchors||[];links=data.links||[];mode=data.mode||0;role=data.role||0;selectedTrack=data.track||0;occurrence=data.occurrence||1;config=ExpressionSettings.restored(data.config);fromSeconds=data.from||0;untilSeconds=data.until||999999;
   info=data.score||{};revision=observer.analysisRevision();stale=true;
   midiToken=observer.readPerformanceMidi(Project.fileUrl(Project.resolve(url,data.source)));status="已恢复人工对应；重新读取谱面以验证候选";
  }catch(e){fail(e)}
 }
 function returnOriginal(){if(!legacyAudition&&typeof observer.finishExpressionAudition==="function"){observer.finishExpressionAudition();previewing=false;status="已结束比较，可以继续编辑乐谱。";return true;}if(previewing&&audioProject&&audioProject.playing&&audioProject.joint)observer.stopReferencePlayback();var r=observer.returnWorkflowSource();if(r.error){fail(r.error);return false}previewing=false;status="已返回原谱，可以使用力度或继续比较。";return true}
 function stopAudition(){pendingAudition=null;auditionRetry.stop();if(previewing&&audioProject&&audioProject.playing&&audioProject.joint)observer.stopReferencePlayback();if(typeof observer.stopWorkflowPlayback!=="function"){status="请用软件上方的播放按钮停止。";return}var r=observer.stopWorkflowPlayback();if(r.error)fail(r.error);else status="试听已停止；可继续比较，或返回原谱。";}
 function applyPart(part){
  if(!candidate||stale||!prepareEditing())return;
  var values=currentApplication();if(part==="velocity")values.tempos=[];else values.velocity=[];
  if(!values.velocity.length&&!values.tempos.length){status=applicationScope===0?"选中要使用的音后再点使用。":"这一范围的结果已经使用，可以继续处理其它音。";return;}
  applying=true;var response=observer.applyExpression(values);applying=false;
  if(response.error){fail(response.error);return;}
  var completed=Project.copy(values);completed.velocity=response.appliedVelocity||values.velocity;appliedProgress=Progress.record(appliedProgress,completed);
  readApplied("已使用"+(part==="velocity"?"演奏力度":"演奏速度")+"，更新 "+response.changed+" 项。Ctrl+Z 撤销。");
 }
 function apply(){
  if(!candidate||stale||!prepareEditing())return;
  var values=currentApplication();applying=true;var response=observer.applyExpression(values);applying=false;
  if(response.error){fail(response.error);return;}values.velocity=response.appliedVelocity||values.velocity;appliedProgress=Progress.record(appliedProgress,values);readApplied("已使用力度和速度，共 "+response.changed+" 项。Ctrl+Z 撤销。");
 }
 function audition(useCandidate){pendingAudition=useCandidate;auditionWaits=0;advanceAudition()}
 function advanceAudition(){
  if(pendingAudition===null)return;
  if(typeof observer.auditionExpression==="function"){
   if(legacyAudition&&previewing&&!returnOriginal()){pendingAudition=null;return;}
   var chosen=pendingAudition;pendingAudition=null;legacyAudition=false;beginAudition(chosen);return;
  }
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
  var range=Project.copy(scope);range.loop=auditionLoop;range.occurrence=Math.max(1,occurrence);
  if(auditionAudio&&audioProject&&audioProject.available){var settings=audioProject.playbackSettings,track=audioProject.tracks[settings.selectedTrack||0],leader=track&&track.associations[settings.selectedAssociation||0];if(!leader){previewing=false;status="请先在原曲参考中选择一条关联。";return;}range.reference={tracks:audioProject.tracks,leader:leader,mode:settings.mode||0,speed:settings.speed||1,volume:settings.volume===undefined?.5:settings.volume,fade:settings.fade!==false,occurrence:occurrence};}
  if(typeof observer.auditionExpression==="function"){
   var prepared=candidate||{revision:revision,scope:scope,velocity:[],tempos:[]};
   var playing=observer.auditionExpression(materials,prepared,range,useCandidate);
   if(playing.error){previewing=false;fail(playing.error);}else status=playing.preparing?"正在准备参考音频与乐谱…":"正在播放："+(useCandidate?"拟采用效果":"谱中效果")+"。切换效果会接着当前拍位播放。";
   return;
  }
  legacyAudition=true;
  var r=observer.previewExpression(materials,useCandidate?candidate:({}),range);if(r.error){previewing=false;fail(r.error)}else status=r.warning?Messages.readable(r.warning):"正在试听，点击“返回原谱”继续编辑。";
 }
 onRun:{
  if(typeof newWorkflowContext==="function")editorContext=newWorkflowContext();
  if(typeof newReferenceProject==="function")audioProject=newReferenceProject();
  if(typeof newScoreObserver!=="function"){status="需要支持 P1 的 Kumo 版本";return}
  observer=newScoreObserver();observer.score=curScore;refreshHistory();refreshLiveSelection();
  if(typeof newMidiCaptureSession==="function")captureSession=newMidiCaptureSession();
  var recording=observer.loadConfiguration("expression-recording");if(recording){captureEndMode=Math.max(0,Math.min(2,recording.endMode||0));captureDuration=Math.max(1,Math.min(3600,recording.duration||30));captureMonitor=recording.monitor!==false;}
  workerSource=observer.readTextFile(String(Qt.resolvedUrl("Alignment.js")));
  if(typeof observer.performanceRange!=="function")status="需要支持 P1 演奏快照的版本";
 }
 function updateScoreContext(){
  if((previewing&&legacyAudition)||applying||refreshingApplication)return;
  if(scopeOwner&&!sameScore(scopeOwner,curScore)){cancel();retainedScope=null;scopeOwner=null;scope=({});notes=[];result=({matches:[],missing:[],extra:[],pending:0});candidate=null;stale=false;status="已切换乐谱，请选择目标谱段。";}
  if(observer&&!sameScore(observer.score,curScore))observer.score=curScore;
  refreshLiveSelection();
  if(notes.length&&observer&&observer.analysisRevision()!==revision){stale=true;candidate=null;cancel();status="乐谱已修改，点击“重新对应”更新效果"}

 }
 onScoreStateChanged:root.updateScoreContext()
 onVisibleChanged:{if(!visible){candidateTimer.stop();if(!captureState.recording&&!captureState.stopping&&!captureState.saving&&!recordingImport){cancel();if(observer&&typeof observer.cancelPerformanceMidi==="function")observer.cancelPerformanceMidi()}}else if(alignmentOnShow){alignmentOnShow=false;analyze()}else if(candidate&&previewUpdating)candidateTimer.restart()}
 Connections {target:observer;ignoreUnknownSignals:true;onWorkflowPlaybackError:root.fail(message);onActiveScoreChanged:root.updateScoreContext()}
 Component.onDestruction:{if(previewing&&observer)returnOriginal();cancel()}
 Connections {
  target:root.captureSession;ignoreUnknownSignals:true
  onFailed:{root.busy=false;root.fail(message)}
  onFinished:{
   root.freshVelocitySettings();root.appliedProgress=({velocity:{},tempos:{}});root.recordingImport=true;root.loadedSourceHash="";root.manualAnchors=[];root.links=[];root.passageChoice=0;root.fromSeconds=0;root.untilSeconds=999999;root.busy=true;root.dirtyProject=true;
   root.midiToken=root.observer.readPerformanceMidi(path);root.status=reason+"，正在对应选段…";
  }
 }
 Timer {id:auditionRetry;interval:50;onTriggered:root.advanceAudition()}
 Timer {interval:1000;repeat:true;running:root.visible&&root.captureSession!==null&&!root.captureState.recording;onTriggered:if(typeof root.captureSession.inputState==="function")root.midiInputInfo=root.captureSession.inputState()}
 Timer {id:candidateTimer;interval:120;onTriggered:if(root.result.matches.length&&!root.busy&&!root.stale&&(!root.previewing||!root.legacyAudition))root.build()}
 Timer {id:paging;interval:1;repeat:true;onTriggered:root.slice()}
 Connections {
  target:observer;ignoreUnknownSignals:true
  onPerformanceMidiReady:{
   if(token!==root.midiToken)return;
   if(result.error){root.busy=false;root.fail(result.error);return}
   if(root.loadedSourceHash&&root.loadedSourceHash!==result.sha256){root.busy=false;root.fail("项目原始 MIDI 已变化，请重新导入并核查人工对应");root.midi=null;return}
   root.busy=false;root.loadedSourceHash="";root.midi=result;root.status="已导入 "+result.notes.length+" 个音，正在寻找所选谱段";if(root.recordingImport){root.recordingImport=false;if(!root.visible){root.alignmentOnShow=true;root.status="录制已保存，打开面板后显示对应结果。";}else if(root.revision===root.observer.analysisRevision())root.runAlignment();else root.analyze()}else root.analyze();
  }
  onReadOnlyJobFinished:{
   if(token!==root.token)return;
   root.busy=false;if(result.error){root.fail(result.error);return}
   if(root.observer.analysisRevision()!==root.revision){root.stale=true;root.status="乐谱刚刚改变，正在更新对应";return}
   if(result.matches){root.appliedProgress=Progress.changedMatches(root.appliedProgress,root.result,result);root.result=result;root.selected=result.matches.length?0:-1;if(root.pendingMatch){for(var i=0;i<result.matches.length;++i)if(result.matches[i].scoreId===root.pendingMatch)root.selected=i;}root.pendingMatch="";root.status=(root.pendingFeedback?root.pendingFeedback+" ":"")+"已匹配 "+result.matches.length+" · 待确认 "+result.pending+" · 漏弹 "+result.missing.length+" · 多弹 "+result.extra.length;root.pendingFeedback=""}
   else{if(root.candidateConfig!==JSON.stringify(root.config)){root.build();return;}root.candidate=Progress.pending(Progress.targets(result,root.config),root.appliedProgress);root.status=(root.applicationMessage?root.applicationMessage+" ":"")+"还有 "+root.candidate.velocity.length+" 个音的力度、"+root.candidate.tempos.length+" 处速度可用";root.applicationMessage=""}
  }
 }
 Rectangle {anchors.fill:parent;color:theme.background}
 // Compact editor page: source, result and adoption share one visible surface.
 ScrollView {
  visible:root.embeddedWorkflow;anchors.fill:parent;clip:true
  ColumnLayout {
   width:Math.max(260,root.width-16);spacing:5
   Loader {Layout.fillWidth:true;active:root.embeddedWorkflow;sourceComponent:Component {ExpressionScopeBar {workflow:root}}}
   GridLayout {
    columns:root.width>1000?6:root.width>650?4:2;Layout.fillWidth:true;columnSpacing:6;rowSpacing:5
    Desk.DeskButton {objectName:"embeddedExpressionImport";text:"导入 MIDI";Layout.fillWidth:true;enabled:!root.busy&&!root.captureState.recording;onClicked:root.chooseMidi()}
    Desk.DeskButton {objectName:root.embeddedWorkflow?"expressionRecord":"embedded_expressionRecord";text:root.captureState.recording?"停止录制":"录制";Layout.fillWidth:true;enabled:root.captureSession!==null&&!root.captureState.stopping&&!root.captureState.saving&&(!root.busy||root.captureState.recording);onClicked:{if(root.captureState.recording)root.captureSession.stop();else root.startRecording()}}
    Desk.DeskButton {text:"重新对应";Layout.fillWidth:true;enabled:!!root.midi&&!root.busy&&!root.captureState.recording;onClicked:root.analyze()}
    Desk.DeskComboBox {objectName:root.embeddedWorkflow?"expressionApplicationScope":"embedded_expressionApplicationScope";Layout.fillWidth:true;model:["选中音","当前选段","全部已对应范围"];currentIndex:root.applicationScope;onActivated:root.applicationScope=currentIndex}
    Desk.DeskComboBox {objectName:root.embeddedWorkflow?"expressionVelocityWriteMode":"embedded_expressionVelocityWriteMode";Layout.fillWidth:true;model:["逐音固定力度","继续跟随力度记号","保留原保存方式"];currentIndex:root.config.velocityWriteMode==="fixed"?0:root.config.velocityWriteMode==="follow"?1:2;onActivated:root.editConfig("velocityWriteMode",["fixed","follow","preserve"][currentIndex])}
    Desk.DeskButton {objectName:"embeddedExpressionSettings";text:"设置与详情";Layout.fillWidth:true;onClicked:root.openToolDialog(expressionSettings)}
    Desk.DeskButton {objectName:root.embeddedWorkflow?"expressionOriginalAudition":"embedded_expressionOriginalAudition";text:"谱中效果";Layout.fillWidth:true;enabled:root.notes.length>0&&!root.busy&&!root.stale;onClicked:root.audition(false)}
    Desk.DeskButton {objectName:root.embeddedWorkflow?"expressionCandidateAudition":"embedded_expressionCandidateAudition";text:"拟采用效果";Layout.fillWidth:true;enabled:root.candidate!==null&&!root.busy&&!root.stale&&!root.previewUpdating;onClicked:root.audition(true)}
    Desk.DeskButton {objectName:root.embeddedWorkflow?"expressionApplyVelocity":"embedded_expressionApplyVelocity";text:"使用力度 · "+root.scopedCandidate.velocity.length+"音";Layout.fillWidth:true;enabled:root.candidate!==null&&root.scopedCandidate.velocity.length>0&&!root.busy&&!root.stale&&!root.previewUpdating;onClicked:root.applyPart("velocity")}
    Desk.DeskButton {objectName:root.embeddedWorkflow?"expressionApplyTempo":"embedded_expressionApplyTempo";text:"使用速度 · "+root.scopedCandidate.tempos.length+"处";Layout.fillWidth:true;enabled:root.candidate!==null&&root.scopedCandidate.tempos.length>0&&!root.busy&&!root.stale&&!root.previewUpdating;onClicked:root.applyPart("tempo")}
    Desk.DeskButton {objectName:root.embeddedWorkflow?"expressionStop":"embedded_expressionStop";text:"停止试听";Layout.fillWidth:true;enabled:root.previewing;onClicked:root.stopAudition()}
    Desk.DeskButton {id:embeddedOperations;objectName:root.embeddedWorkflow?"expressionApplicationMenuButton":"embedded_expressionApplicationMenuButton";text:"更多操作";Layout.fillWidth:true;onClicked:root.openMenu(applicationMenu,embeddedOperations)}
   }
   GridLayout {
    columns:root.width>700?8:3;Layout.fillWidth:true
    Label {text:"采用演奏力度";color:theme.muted}
    Desk.DeskSpinBox {objectName:"embeddedExpressionStrength";from:0;to:100;value:root.config.velocityStrength===undefined?root.config.strength:root.config.velocityStrength;onValueModified:root.editConfig("velocityStrength",value)}
    Label {text:"%";color:theme.muted}
    Desk.DeskSwitch {text:"使用速度";checked:root.config.rubato;onClicked:root.editConfig("rubato",checked)}
    Desk.DeskSwitch {objectName:root.embeddedWorkflow?"expressionKeepManual":"embedded_expressionKeepManual";text:"保留手动调整";checked:root.config.keepManual===true;onClicked:root.editConfig("keepManual",checked)}
    Desk.DeskSwitch {text:"循环试听";checked:root.auditionLoop;onClicked:{root.auditionLoop=checked;if(root.observer&&typeof root.observer.setExpressionAuditionLoop==="function")root.observer.setExpressionAuditionLoop(checked)}}
    Desk.DeskButton {text:"漏弹 "+root.result.missing.length;enabled:root.result.missing.length>0;onClicked:{root.issueKind="missing";root.openToolDialog(issues)}}
    Desk.DeskButton {text:"多弹 "+root.result.extra.length;enabled:root.result.extra.length>0;onClicked:{root.issueKind="extra";root.openToolDialog(issues)}}
    Desk.DeskButton {text:"需核对 "+root.result.pending;enabled:root.result.pending>0;onClicked:{root.issueKind="pending";root.openToolDialog(issues)}}
    Item {Layout.fillWidth:true}
   }
   RowLayout {Layout.fillWidth:true
    Label {text:"录制结束";color:theme.muted}
    Desk.DeskComboBox {Layout.fillWidth:true;model:["手动停止","到时停止","弹完选段停止"];currentIndex:root.captureEndMode;enabled:!root.captureState.recording;onActivated:{root.captureEndMode=currentIndex;root.saveCaptureOptions()}}
    Desk.DeskSpinBox {visible:root.captureEndMode===1;from:1;to:3600;value:root.captureDuration;onValueModified:{root.captureDuration=value;root.saveCaptureOptions()}}
    Label {visible:root.captureEndMode===1;text:"秒";color:theme.muted}
   }
   Label {Layout.fillWidth:true;wrapMode:Text.Wrap;color:theme.muted;text:root.status}
   Label {Layout.fillWidth:true;color:theme.accent;visible:root.previewing;text:(root.playbackState.playing?"正在播放：":"当前试听：")+(root.playbackState.mode==="candidate"?"拟采用效果":"谱中效果")}
  }
 }

 ScrollView {
  id:fullPanel;visible:!root.embeddedWorkflow;anchors.fill:parent;anchors.margins:8;clip:true
  ColumnLayout {
   width:Math.max(260,fullPanel.availableWidth);height:Math.max(implicitHeight,fullPanel.availableHeight);spacing:6
  RowLayout {
   Layout.fillWidth:true
   Label {text:"演奏表达";font.bold:true;color:theme.text}
   Item {Layout.fillWidth:true}
   Desk.DeskButton {text:"停止";visible:root.compact&&root.previewing;onClicked:root.stopAudition()}
   Desk.DeskButton {text:"返回";visible:root.compact&&root.previewing;onClicked:root.returnOriginal()}
   Desk.DeskButton {text:"导入 MIDI";enabled:!root.busy&&!root.captureState.recording&&!root.captureState.stopping&&!root.captureState.saving;onClicked:root.chooseMidi()}
   Desk.DeskButton {id:projectButton;text:"项目";onClicked:root.openMenu(projectMenu,projectButton)}
   Desk.DeskButton {text:"悬浮";onClicked:{if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}}
  }
  Loader {Layout.fillWidth:true;active:!root.embeddedWorkflow;sourceComponent:Component {ExpressionScopeBar {workflow:root}}}
  RowLayout {
   Layout.fillWidth:true
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionRecord":"expressionRecord";text:root.captureState.recording?"停止录制":"录制";enabled:root.captureSession!==null&&!root.captureState.stopping&&!root.captureState.saving&&(!root.busy||root.captureState.recording);onClicked:{if(root.captureState.recording)root.captureSession.stop();else root.startRecording()}}
   Desk.DeskComboBox {objectName:root.embeddedWorkflow?"legacy_expressionRecordEnd":"expressionRecordEnd";Layout.fillWidth:true;model:["手动停止","到时停止","弹完选段停止"];currentIndex:root.captureEndMode;enabled:!root.captureState.recording&&!root.captureState.stopping;onActivated:{root.captureEndMode=currentIndex;root.saveCaptureOptions()}}
   Desk.DeskSpinBox {visible:root.captureEndMode===1;from:1;to:3600;value:root.captureDuration;onValueModified:{root.captureDuration=value;root.saveCaptureOptions()}}
   Label {visible:root.captureEndMode===1;text:"秒";color:theme.muted}
  }
  Label {visible:!!(root.captureState.recording||root.captureState.stopping||root.captureState.saving);Layout.fillWidth:true;Layout.preferredHeight:24;elide:Text.ElideRight;color:theme.accent;text:root.captureState.stopping?"正在收完最后的音…":root.captureState.saving?"正在保存录制…":(root.captureState.seconds||0).toFixed(1)+" 秒 · "+(root.captureState.count||0)+" 个音 · "+(root.captureState.pitch>=0?root.recordingPitch(root.captureState.pitch)+" · 力度 "+root.captureState.velocity:"等待键盘输入")}
  Label {Layout.fillWidth:true;Layout.preferredHeight:28;elide:Text.ElideRight;ToolTip.visible:statusHover.containsMouse;ToolTip.text:root.status;color:root.stale?theme.accent:theme.muted;text:root.status;MouseArea {id:statusHover;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.NoButton}}
  RowLayout {
   Layout.fillWidth:true
   Desk.DeskButton {text:"重新对应";enabled:root.midi&&!root.busy&&!root.captureState.recording&&!root.captureState.stopping&&!root.captureState.saving;onClicked:root.analyze()}
   Desk.DeskButton {visible:root.advancedSettings;text:"重算对应";enabled:root.notes.length&&!root.busy&&!root.stale;onClicked:root.runAlignment()}
   Desk.DeskButton {text:"设置";onClicked:{root.advancedSettings=true;root.openToolDialog(expressionSettings)}}
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionOpenEditor":"expressionOpenEditor";text:"编辑器对比";enabled:root.result.matches.length>0;onClicked:{root.publishComparison();if(typeof root.observer.showEditorComparison==="function"){var opened=root.observer.showEditorComparison();if(opened.error)root.fail(opened.error);else root.status="力度区已显示前后对比。实心条可拖动，使用力度后写入乐谱。"}}}
   Desk.DeskButton {text:"取消";enabled:root.busy;onClicked:root.cancel()}
  }
  RowLayout {visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {id:matchedButton;text:"已匹配 "+root.result.matches.length;enabled:root.result.matches.length>0;onClicked:matches.positionViewAtIndex(Math.max(0,matches.currentIndex),ListView.Center)}
   Desk.DeskButton {text:"漏弹 "+root.result.missing.length;enabled:root.result.missing.length>0;onClicked:{root.issueKind="missing";root.openToolDialog(issues)}}
   Desk.DeskButton {text:"多弹 "+root.result.extra.length;enabled:root.result.extra.length>0;onClicked:{root.issueKind="extra";root.openToolDialog(issues)}}
   Desk.DeskButton {text:"需核对 "+root.result.pending;enabled:root.result.pending>0;onClicked:{root.issueKind="pending";root.openToolDialog(issues)}}
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
   id:matches;objectName:"expressionMatches";visible:!root.compact;clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:72;model:root.displayed.rows;currentIndex:root.displayed.indexes[root.selected]===undefined?-1:root.displayed.indexes[root.selected]
   Label {anchors.centerIn:parent;width:Math.max(100,parent.width-24);horizontalAlignment:Text.AlignHCenter;wrapMode:Text.Wrap;color:theme.muted;visible:root.result.matches.length>0&&!root.displayed.rows.length;text:"这段的演奏力度已使用。\n更多 → 显示已使用的对应，可查看刚才的结果。\n更多 → 恢复应用前力度，可按音、选段或全曲恢复。"}
   ScrollBar.vertical:ScrollBar {}
   delegate:ItemDelegate {
    property var match:modelData.match
    width:parent.width;height:34;highlighted:modelData.index===root.selected
    text:"第 "+match.original.measure+" 小节 · "+(["C","C♯","D","E♭","E","F","F♯","G","A♭","A","B♭","B"][match.pitch%12]+(Math.floor(match.pitch/12)-1))+" → "+match.seconds.toFixed(3)+"s  "+"力度 "+match.velocity+(modelData.applied?" · 已使用":"")+(match.confidence<.8?" · 检查对应":"")
    onClicked:{root.selected=modelData.index;if(root.observer&&!root.observer.playing&&!root.previewing)root.locate()}
   }
  }
  RowLayout {
   visible:root.advancedSettings&&!root.compact;Layout.fillWidth:true
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionLocate":"expressionLocate";text:"定位对应音";enabled:root.selected>=0&&!root.busy;onClicked:root.locate()}
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionConfirm":"expressionConfirm";text:"确认对应";enabled:root.selected>=0&&!root.stale&&!root.busy;onClicked:root.confirm()}
   Desk.DeskButton {text:"固定这一处对应";enabled:root.selected>=0&&!root.stale;onClicked:root.anchor()}
   Desk.DeskButton {text:"校正…";enabled:root.notes.length>0;onClicked:root.openToolDialog(correction)}
  }
  Rectangle {visible:!root.compact;Layout.fillWidth:true;height:1;color:theme.line}
  GridLayout {
   visible:!root.compact;columns:2;Layout.fillWidth:true
   CheckBox {text:"力度";checked:root.config.velocity;onToggled:root.editConfig("velocity",checked)}
   CheckBox {text:"Rubato（全声部速度）";checked:root.config.rubato;onToggled:root.editConfig("rubato",checked)}
   Label {text:root.config.rubato?"采用演奏效果的比例（力度／速度）":"采用演奏力度的比例";color:theme.muted}
   RowLayout {Desk.DeskSpinBox {from:0;to:100;value:root.config.velocityStrength===undefined?root.config.strength:root.config.velocityStrength;onValueModified:root.editConfig("velocityStrength",value)}
   Desk.DeskSpinBox {visible:root.config.rubato;from:0;to:100;value:root.config.rubatoStrength===undefined?root.config.strength:root.config.rubatoStrength;onValueModified:root.editConfig("rubatoStrength",value)}}
   CheckBox {visible:root.config.rubato;text:"保持这段时长和两端速度";checked:root.config.keepDuration;onToggled:root.editConfig("keepDuration",checked)}
   CheckBox {visible:root.advancedSettings;text:"带动伴奏的力度走势";checked:root.config.accompaniment;onToggled:root.editConfig("accompaniment",checked)}
  }
  RowLayout {visible:!root.compact;Layout.fillWidth:true
   Label {text:"使用范围";color:theme.text}
   Desk.DeskComboBox {objectName:root.embeddedWorkflow?"legacy_expressionApplicationScope":"expressionApplicationScope";Layout.fillWidth:true;model:["选中音","当前选段","全部已对应范围"];currentIndex:root.applicationScope;onActivated:root.applicationScope=currentIndex}
  }
  RowLayout {Layout.fillWidth:true
   Label {text:"保存方式";color:theme.muted}
   Desk.DeskComboBox {objectName:root.embeddedWorkflow?"legacy_expressionVelocityWriteMode":"expressionVelocityWriteMode";Layout.fillWidth:true;model:["逐音固定力度","继续跟随力度记号","保留各音原保存方式"];currentIndex:root.config.velocityWriteMode==="fixed"?0:root.config.velocityWriteMode==="follow"?1:2;onActivated:root.editConfig("velocityWriteMode",["fixed","follow","preserve"][currentIndex]);ToolTip.visible:hovered;ToolTip.text:currentIndex===0?"这些音直接采用演奏力度，谱上的力度记号继续显示。":currentIndex===1?"按各音的力度记号换算增减百分比；以后调整记号，这些音一起变化。":"各音继续使用原来的相对或固定方式。"}
  }
  Label {visible:!root.compact&&root.previewing;Layout.fillWidth:true;elide:Text.ElideRight;color:theme.accent;text:(root.playbackState.preparing?"正在准备：":root.playbackState.playing?"正在播放：":"已暂停：")+(root.playbackState.mode==="candidate"?"拟采用效果":"谱中效果")}
  RowLayout {visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionOriginalAudition":"expressionOriginalAudition";Layout.fillWidth:true;text:"谱中效果";enabled:root.notes.length>0&&!root.busy&&!root.stale;onClicked:root.audition(false)}
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionCandidateAudition":"expressionCandidateAudition";Layout.fillWidth:true;text:"拟采用效果";enabled:root.candidate!==null&&!root.busy&&!root.stale&&!root.previewUpdating;onClicked:root.audition(true)}
   Desk.DeskButton {text:"循环";checkable:true;checked:root.auditionLoop;onClicked:{root.auditionLoop=checked;if(root.observer&&typeof root.observer.setExpressionAuditionLoop==="function")root.observer.setExpressionAuditionLoop(checked)}}
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionStop":"expressionStop";text:"停止";visible:root.previewing;onClicked:root.stopAudition()}
  }
  RowLayout {visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionApplyVelocity":"expressionApplyVelocity";Layout.fillWidth:true;text:"使用力度 · "+root.scopedCandidate.velocity.length+"音";enabled:root.candidate!==null&&root.scopedCandidate.velocity.length>0&&!root.busy&&!root.stale&&!root.previewUpdating;onClicked:root.applyPart("velocity")}
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionApplyTempo":"expressionApplyTempo";Layout.fillWidth:true;text:"使用速度 · "+root.scopedCandidate.tempos.length+"处";visible:root.config.rubato;enabled:root.candidate!==null&&root.scopedCandidate.tempos.length>0&&!root.busy&&!root.stale&&!root.previewUpdating;onClicked:root.applyPart("tempo")}
   Desk.DeskButton {id:applicationMenuButton;objectName:root.embeddedWorkflow?"legacy_expressionApplicationMenuButton":"expressionApplicationMenuButton";text:"更多";onClicked:root.openMenu(applicationMenu,applicationMenuButton)}
  }
  CheckBox {visible:!root.compact;objectName:root.embeddedWorkflow?"legacy_expressionKeepManual":"expressionKeepManual";text:"保留手动调整";checked:root.config.keepManual===true;onClicked:root.editConfig("keepManual",checked)}
  Label {visible:!root.compact;Layout.fillWidth:true;elide:Text.ElideRight;text:root.scope.firstMeasure?"对应谱段：第 "+root.scope.firstMeasure+"–"+root.scope.lastMeasure+" 小节 · "+root.scope.part:"选中谱段后导入 MIDI，或直接录制。";color:theme.muted}
  RowLayout {visible:!root.compact&&root.previewing;Layout.fillWidth:true
   Desk.DeskButton {objectName:root.embeddedWorkflow?"legacy_expressionReturn":"expressionReturn";text:"结束比较";onClicked:{root.pendingAudition=null;auditionRetry.stop();root.returnOriginal()}}
   Item {Layout.fillWidth:true}
  }

 }
 }
 Dialog {id:expressionSettings;title:"录制与对应设置";modal:false;width:Math.max(300,Math.min(420,root.width));height:440;standardButtons:Dialog.Close
  ScrollView {anchors.fill:parent;clip:true
  GridLayout {
   columns:2;width:parent.width
   Desk.DeskSwitch {text:"录制时听到键盘声音";checked:root.captureMonitor;onToggled:{root.captureMonitor=checked;root.saveCaptureOptions()}}
   Label {Layout.fillWidth:true;elide:Text.ElideRight;text:root.captureState.device||"使用软件当前 MIDI 输入";color:theme.muted}
   Desk.DeskComboBox {Layout.fillWidth:true;model:["按谱演奏（允许错漏）","任意音高敲击音序"];currentIndex:root.mode;onActivated:{root.mode=currentIndex;root.candidate=null}}
   Desk.DeskComboBox {Layout.fillWidth:true;model:["所选声部组","最高音旋律","指定谱表和声部"];currentIndex:root.role;onActivated:{root.role=currentIndex;root.candidate=null}}
   Desk.DeskSpinBox {visible:root.role===2;from:root.scope.firstTrack||0;to:Math.max(from,(root.scope.endTrack||1)-1);value:root.selectedTrack;onValueModified:root.selectedTrack=value}
   Label {text:"采用演奏速度的比例";color:theme.muted}
   Desk.DeskSpinBox {from:0;to:100;value:root.config.rubatoStrength;onValueModified:root.editConfig("rubatoStrength",value)}
   Desk.DeskSwitch {Layout.columnSpan:2;Layout.fillWidth:true;text:"保持这段时长和两端速度";checked:root.config.keepDuration;onClicked:root.editConfig("keepDuration",checked)}
   Desk.DeskSwitch {Layout.columnSpan:2;Layout.fillWidth:true;text:"带动伴奏的力度走势";checked:root.config.accompaniment;onClicked:root.editConfig("accompaniment",checked)}
   Label {text:"反复第几次（0=全部）";color:theme.muted}
   Desk.DeskSpinBox {from:0;to:99;value:root.occurrence;onValueModified:root.occurrence=value}
   Desk.DeskSwitch {text:"全谱";checked:root.whole;enabled:!root.busy;onClicked:root.chooseTarget(checked)}
   RowLayout {Layout.fillWidth:true
   Label {text:"演奏起秒";color:theme.muted}
   Desk.DeskTextField {Layout.fillWidth:true;placeholderText:"0";text:root.fromSeconds;onEditingFinished:root.fromSeconds=Math.max(0,Number(text)||0)}
   }
   RowLayout {Layout.fillWidth:true
   Label {text:"演奏止秒";color:theme.muted}
   Desk.DeskTextField {Layout.fillWidth:true;placeholderText:"至文件末尾";text:root.untilSeconds>=999999?"":root.untilSeconds;onEditingFinished:root.untilSeconds=Math.max(root.fromSeconds,Number(text)||999999)}
   }
  }
  }
 }
 property string issueKind:"missing"
 Menu {id:issueMenu
  MenuItem {text:"重新比较已使用的结果";onTriggered:{root.appliedProgress=({velocity:{},tempos:{}});root.build()}}
  MenuItem {text:"读取并对应全曲";onTriggered:root.chooseTarget(true)}
 }
 Dialog {id:issues;title:root.issueKind==="missing"?"漏弹的音":root.issueKind==="extra"?"多弹的音":"需要核对的对应";modal:false;width:Math.min(400,root.width);height:300;standardButtons:Dialog.Close
  ListView {anchors.fill:parent;clip:true;model:root.issueKind==="pending"?root.result.matches.filter(function(m){return !m.confirmed&&m.confidence<.8}):root.result[root.issueKind];ScrollBar.vertical:ScrollBar{}
   delegate:ItemDelegate {width:parent.width;text:root.issueKind==="extra"?(modelData.seconds.toFixed(3)+"秒 · "+root.recordingPitch(modelData.pitch)):"第 "+(modelData.original||modelData).measure+" 小节 · "+root.recordingPitch(modelData.pitch);onClicked:{
    if(root.issueKind==="extra"){
     root.selectedMidiId=modelData.id;root.publishComparison();root.observer.showEditorCorrespondence();
     var tick=Progress.tickAt(root.displayed.result.timing,modelData.seconds),closest=null;
     if(tick!==null)root.notes.forEach(function(n){if(!closest||Math.abs(n.tick-tick)<Math.abs(closest.tick-tick))closest=n;});
     if(closest)root.observer.navigateWorkflow({start:closest.tick,firstTrack:closest.track,index:closest.index,writtenPitch:closest.writtenPitch,writtenTpc:closest.writtenTpc},false);
     root.status="已在演奏对应区标出 "+modelData.seconds.toFixed(3)+" 秒的多弹音。点一个谱面音，再点这个演奏音，可以重新连接。";
    }
    else {root.observer.showEditorCorrespondence();var n=modelData.original||modelData;root.observer.navigateWorkflow({start:n.tick,firstTrack:n.track,index:n.index,writtenPitch:n.writtenPitch,writtenTpc:n.writtenTpc},false);var i=root.result.matches.findIndex(function(m){return m.scoreId===modelData.scoreId});if(i>=0)root.selected=i;root.status="已定位第 "+n.measure+" 小节；可在编辑器调整对应。";}
   }}
  }
 }
 function showMoreActions(){root.openMenu(applicationMenu,embeddedOperations)}
 Menu {id:applicationMenu
  MenuItem {objectName:"expressionLocate";visible:root.embeddedWorkflow;text:"定位选中对应音";enabled:root.selected>=0&&!root.busy;onTriggered:root.locate()}
  MenuItem {objectName:"expressionConfirm";visible:root.embeddedWorkflow;text:"确认选中对应";enabled:root.selected>=0&&!root.stale&&!root.busy;onTriggered:root.confirm()}
  MenuItem {objectName:"expressionReturn";visible:root.embeddedWorkflow;text:"结束比较";enabled:root.previewing;onTriggered:root.returnOriginal()}
  MenuItem {visible:root.embeddedWorkflow;text:"打开表达项目";onTriggered:loadDialog.open()}
  MenuItem {visible:root.embeddedWorkflow;text:"保存表达项目";onTriggered:{if(root.workspace)root.save(root.workspace);else saveDialog.open()}}
  MenuItem {visible:root.embeddedWorkflow;text:"另存表达项目";onTriggered:saveDialog.open()}
  MenuItem {visible:root.embeddedWorkflow;text:"设备校准";onTriggered:root.openToolDialog(calibration)}
  MenuItem {text:"显示已使用的对应";checkable:true;checked:root.showApplied;onTriggered:root.showApplied=checked}
  MenuItem {objectName:"expressionOpenEditor";text:"在编辑器调整对应";enabled:root.result.matches.length>0;onTriggered:{root.publishComparison();root.observer.showEditorCorrespondence()}}
  MenuItem {text:"读取并对应全曲";enabled:!!root.midi&&!root.busy;onTriggered:root.chooseTarget(true)}
  MenuItem {text:"查看每个音的变化";enabled:root.candidate!==null;onTriggered:root.openToolDialog(changes)}
  MenuItem {objectName:"expressionRestoreVelocity";text:"恢复应用前力度";enabled:root.savedVelocityCount>0&&!root.busy;onTriggered:root.restoreVelocity(false)}
  MenuItem {text:"以当前力度为比较起点";enabled:!root.busy;onTriggered:root.restoreVelocity(true)}
  MenuItem {text:"重新比较已使用的结果";enabled:!root.busy&&!root.stale;onTriggered:{root.appliedProgress=({velocity:{},tempos:{}});root.build()}}
  MenuItem {text:"试听时播放参考音频";checkable:true;checked:root.auditionAudio;enabled:root.audioProject&&root.audioProject.available;onTriggered:root.auditionAudio=checked}
 }
 Menu {id:projectMenu
  MenuItem {text:"关闭旧试听页";enabled:!root.busy&&!root.previewing&&root.observer&&!root.observer.playing;onTriggered:{var r=root.observer.closeExpressionPreviewTabs();root.status=r.error?Messages.readable(r.error):"已关闭 "+r.closed+" 个旧试听页"+(r.kept?"，另有 "+r.kept+" 页保存着修改。":"。")}}
  MenuItem {text:"打开项目";onTriggered:loadDialog.open()}
  MenuItem {text:"保存项目";onTriggered:{if(root.workspace)root.save(root.workspace);else saveDialog.open()}}
  MenuItem {text:"另存项目";onTriggered:saveDialog.open()}
  MenuItem {text:"设备校准";onTriggered:root.openToolDialog(calibration)}
 }
 Dialog {
  id:correction;title:"调整演奏对应";modal:true;width:380;height:320;standardButtons:Dialog.Close
  ColumnLayout {anchors.fill:parent
   Label {text:"选择谱音与演奏事件，可重新连接或标记漏／多弹"}
   Desk.DeskComboBox {id:scoreChoice;Layout.fillWidth:true;model:root.result.matches.map(function(m){return m.original}).concat(root.result.missing).map(function(n){return n.id+" · "+n.pitch});}
   Desk.DeskComboBox {id:midiChoice;Layout.fillWidth:true;model:root.midi?root.midi.notes.map(function(n){return n.id+" · "+n.seconds.toFixed(3)+"s · "+n.pitch}):[]}
   RowLayout {
    Desk.DeskButton {text:"重新连接";onClicked:{var ns=root.result.matches.map(function(m){return m.original}).concat(root.result.missing),ms=root.midi.notes;if(scoreChoice.currentIndex<0||midiChoice.currentIndex<0)return;var l=root.links.filter(function(v){return v.scoreId!==ns[scoreChoice.currentIndex].id&&v.midiId!==ms[midiChoice.currentIndex].id});l.push({scoreId:ns[scoreChoice.currentIndex].id,midiId:ms[midiChoice.currentIndex].id});root.links=l;root.runAlignment()}}
    Desk.DeskButton {text:"取消对应";onClicked:{if(root.selected<0)return;var m=root.result.matches[root.selected],l=Project.copy(root.links);l.push({scoreId:m.scoreId,midiId:m.midiId,disabled:true});root.links=l;root.runAlignment()}}
   }
   Desk.DeskButton {text:"清除固定的对应位置";onClicked:{root.manualAnchors=[];root.runAlignment()}}
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
    delegate:ItemDelegate {width:parent.width;text:modelData.reason||((modelData.source==="inferred"?"推算 · ":"实测 · ")+(modelData.id||("tick "+modelData.tick))+" · 原 "+(modelData.oldBpm||modelData.originalVelocity||modelData.oldBase)+" → "+(modelData.target||modelData.bpm)+ (modelData.target?"（写回 "+root.velocityWriteText(modelData)+"）":""));onClicked:{if(!modelData.id)return;var c=Project.copy(root.config);if(c.unlocked.indexOf(modelData.id)<0)c.unlocked.push(modelData.id);root.config=c;root.candidate=null;changes.close();root.build()}}
   }
   Desk.DeskButton {text:"应用到当前原谱（一次撤销）";enabled:root.candidate!==null&&!root.stale;onClicked:{root.apply();changes.close()}}
  }
 }
 Files.FileDialog {id:midiDialog;objectName:"expressionMidiFileDialog";title:"导入演奏 MIDI（不打开新谱）";nameFilters:["MIDI (*.mid *.midi)"];onAccepted:root.importMidi(String(fileUrl))}
 Files.FileDialog {id:loadDialog;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.load(String(fileUrl))}
 Files.FileDialog {id:saveDialog;selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.save(String(fileUrl))}
}
