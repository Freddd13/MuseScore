import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3 as Files
import MuseScore 3.0
import "../HarmonyAssistant" as Desk
import "../WorkflowCommon/Workspace.js" as Project
import "../WorkflowCommon/ActionMessages.js" as Messages
import "Variants.js" as Rules
MuseScore {
    property string preferredPanelGroup: "rightTools"
 id:root
 menuPath:"Plugins.Arrangement Variants";description:"试写方案：独立完整谱副本、原生标签页切换、范围比较和试听。"
 version:"1.1.0";requiresScore:true;pluginType:"dock";dockArea:"right";implicitWidth:400;implicitHeight:560
 property var observer:null
 property string workspace:""
 property var groups:[]
 property int groupIndex:-1
 property int selected:0
 property var currentInfo:({})
 property string status:"这里管理旧版完整副本。新编配可打开内置分段方案。"
 property bool busy:false
 property bool switching:false
 property bool pendingGroup:false
 property bool pendingCopy:false
 property bool dirty:false
 property bool compact:height<170||(typeof panelPlacement==="string"&&panelPlacement==="top")
 property var fingerprint:({measures:[],staves:0})
 property int nextMeasure:0
 property string queryRevision:""
 property string queryAction:""
 property var group:groupIndex>=0&&groupIndex<groups.length?groups[groupIndex]:null
 property var active:null
 Desk.UiTheme {id:theme}
 function fail(e){status=Messages.readable(e);busy=false}
 function local(path){return Project.path(path).replace(/\\/g,"/").toLowerCase()}
 function resolveEntry(e){return Project.fileUrl(Project.resolve(workspace,e.path))}
 function refresh(){
  if(!observer)return;
  observer.score=curScore;var info=typeof observer.workflowStatus==="function"?observer.workflowStatus():observer.workflowInfo();if(info.error){status=info.error;return}currentInfo=info;active=null;
  if(group)for(var i=0;i<group.entries.length;++i){var e=group.entries[i],paths=[Project.resolve(workspace,e.path)].concat(e.aliases||[]);for(var j=0;j<paths.length;++j)if(local(paths[j])===local(info.file))active=e;}
 }
 function fileName(kind){return Project.directory(workspace)+kind+"-"+Date.now()+"-"+Math.floor(Math.random()*1000000)+".mscz"}
 function setGroup(g){var a=Project.copy(groups);a[groupIndex]=g;groups=a;dirty=true}
 function saveManifest(){try{Project.writeModule(observer,workspace,"variants",{groups:groups,groupIndex:groupIndex,selected:selected},currentInfo);dirty=false;status="项目已保存；之前保存的方案文件仍保留。";return true}catch(e){fail(e);return false}}
 function newGroup(){
  if(!workspace){pendingGroup=true;saveDialog.open();return}
  refresh();if(currentInfo.error||observer.playing){fail("先停止播放／渲染");return}
  var scope=observer.analysisScope(false);if(!scope.end){fail("选择起止谱段");return}
  var base=fileName("baseline"),a=fileName("variant"),rev=observer.analysisRevision();
  var first=observer.copyWorkflowScore(Project.fileUrl(base),rev,false);if(first.error){fail(first.error);return}
  var second=observer.copyWorkflowScore(Project.fileUrl(a),rev,false);if(second.error){fail(second.error);return}
  var all=Project.copy(groups);all.push({name:"第"+scope.firstMeasure+"–"+scope.lastMeasure+"小节",scope:scope,occurrence:1,source:currentInfo.file,entries:[Rules.newEntry("原始版本",Project.relative(workspace,base),true,first.sha256,""),Rules.newEntry("A · 试写",Project.relative(workspace,a),false,second.sha256,"")],baseline:null});groups=all;groupIndex=all.length-1;selected=1;
  readFingerprint("baseline");status="已建立原始版本和可编辑方案 A。原谱没有改变。";
 }
 function readFingerprint(action){
  if(busy)return;if(observer.playing){fail("停播后检查比较条件");return}
  busy=true;queryAction=action;queryRevision=observer.analysisRevision();fingerprint={measures:[],staves:0};nextMeasure=0;paging.start();
 }
 function page(){
  if(!visible||observer.analysisRevision()!==queryRevision){paging.stop();fail("谱面已变化，检查已取消");return}
  var p=observer.workflowFingerprint(nextMeasure,1,queryRevision);if(p.error){paging.stop();fail(p.error);return}
  var f=fingerprint;Array.prototype.push.apply(f.measures,p.measures);f.staves=p.staves;if(nextMeasure===0)f.context=p.context;fingerprint=f;nextMeasure=p.next;
  if(p.done){paging.stop();busy=false;var g=Project.copy(group);
   if(queryAction==="baseline"){g.baseline=Project.copy(fingerprint);setGroup(g);saveManifest();switchTo(1,false)}
   else if(active){var c=Rules.compare(g.baseline,fingerprint,g.scope);for(var i=0;i<g.entries.length;++i)if(g.entries[i].id===active.id){c.revision=queryRevision;g.entries[i].comparison=c;}setGroup(g);status=c.same?"同条件比较 · 段内 "+c.inside+" 小节改变":c.reasons.join("；")}
  }
 }
 function switchTo(index,audition){
  if(!group||index<0||index>=group.entries.length)return;
  switching=true;var e=group.entries[index],r=observer.openWorkflowScore(resolveEntry(e),e.baseline,e.hash);switching=false;
  if(r.error){fail(r.error);refresh();return}selected=index;refresh();
  if(!e.savedRevision&&!currentInfo.dirty){var g=Project.copy(group);g.entries[index].savedRevision=observer.analysisRevision();setGroup(g);}
  var range=Project.copy(group.scope);range.occurrence=group.occurrence||1;
  r=observer.navigateWorkflow(range,audition);if(r.error)fail(r.error);else status=audition?("试听上下文 tick "+r.start+"–"+r.end+" · 反复第"+range.occurrence+"次"):"已切换独立标签页 · "+e.name;
  if(!audition)readFingerprint("compare");
 }
 function duplicate(){
  refresh();if(!workspace){pendingCopy=true;saveDialog.open();return}
  if(!group||!active){fail("先切到本组基准或方案");return}
  var path=fileName("variant"),r=observer.copyWorkflowScore(Project.fileUrl(path),observer.analysisRevision(),false);if(r.error){fail(r.error);return}
  var g=Project.copy(group);g.entries.push(Rules.newEntry("方案 "+g.entries.length,Project.relative(workspace,path),false,r.sha256,""));setGroup(g);saveManifest();switchTo(g.entries.length-1,false);
 }
 function saveActive(){
  refresh();if(!active||active.baseline)return saveManifest();
  var path=fileName("saved-variant"),r=observer.copyWorkflowScore(Project.fileUrl(path),observer.analysisRevision(),false);if(r.error){fail(r.error);return false}
  var g=Project.copy(group);for(var i=0;i<g.entries.length;++i)if(g.entries[i].id===active.id){var e=g.entries[i];e.aliases=(e.aliases||[]).concat([currentInfo.file]);e.path=Project.relative(workspace,path);e.hash=r.sha256;e.savedRevision=observer.analysisRevision();}
  setGroup(g);return saveManifest();
 }
 function load(url){paging.stop();busy=false;try{var p=Project.read(observer,url).variants;if(!p||!p.groups.length)throw new Error("没有方案工作区");workspace=String(url);groups=p.groups;groupIndex=Math.max(0,Math.min(p.groupIndex||0,groups.length-1));selected=p.selected||0;groups.forEach(function(g){g.entries.forEach(function(e){e.aliases=[];e.savedRevision=""})});switchTo(selected,false)}catch(e){fail(e)}}
 function saved(e){return active&&active.id===e.id?(observer&&observer.analysisRevision()===e.savedRevision):true}
 function comparisonText(e){
  if(!e.comparison||(active&&active.id===e.id&&e.comparison.revision!==currentInfo.revision))return "比较待检查";
  return e.comparison.same?"同条件比较":e.comparison.reasons.join("；");
 }
 onRun:{if(typeof newScoreObserver!=="function"){fail("需要 P1 方案宿主");return}observer=newScoreObserver();observer.score=curScore;refresh()}
 onScoreStateChanged:if(!switching&&visible){refresh();if(busy&&observer.analysisRevision()!==queryRevision){paging.stop();busy=false;status="谱面变化，比较待更新"}}
 onVisibleChanged:{if(!visible){paging.stop();busy=false}else if(observer)refresh()}
 Timer {id:paging;interval:1;repeat:true;onTriggered:root.page()}
 Rectangle {anchors.fill:parent;color:theme.background}
 ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:6
  RowLayout {Layout.fillWidth:true;Label {text:"试写方案";font.bold:true;color:theme.text}Item {Layout.fillWidth:true}
   Desk.DeskButton {text:"内置分段方案";onClicked:root.cmd("regional-variants")}
   Desk.DeskButton {text:"新建组";enabled:!root.busy;onClicked:root.newGroup()}
   Desk.DeskButton {text:"项目";onClicked:projectMenu.open()}
   Desk.DeskButton {text:"悬浮";onClicked:if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}
  }
  Label {Layout.fillWidth:true;Layout.preferredHeight:28;elide:Text.ElideRight;text:root.status;color:theme.muted}
  Desk.DeskComboBox {visible:!root.compact;Layout.fillWidth:true;enabled:!root.busy;model:root.groups.map(function(g){return g.name});currentIndex:root.groupIndex;onActivated:{root.groupIndex=currentIndex;root.refresh()}}
  RowLayout {visible:!root.compact;Label {text:root.group?"范围 tick "+root.group.scope.start+"–"+root.group.scope.end:"无分组";color:theme.muted}Item {Layout.fillWidth:true}
   Label {text:"反复次数";color:theme.muted}Desk.DeskSpinBox {from:1;to:99;value:root.group?root.group.occurrence:1;onValueModified:{var g=Project.copy(root.group);g.occurrence=value;root.setGroup(g)}}
  }
  ListView {visible:!root.compact;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:100;clip:true;model:root.group?root.group.entries:[];currentIndex:root.selected;ScrollBar.vertical:ScrollBar {}
   delegate:ItemDelegate {width:parent.width;height:55;highlighted:index===root.selected
    text:modelData.name+" · "+(modelData.baseline?"原始版本，不作修改":root.saved(modelData)?"已保存工作区":"未保存工作区")+"\n"+root.comparisonText(modelData)
    onClicked:root.selected=index;onDoubleClicked:root.switchTo(index,false)
   }
  }
  RowLayout {Layout.fillWidth:true
   Desk.DeskButton {text:"切换并定位";enabled:root.group&&!root.busy;onClicked:root.switchTo(root.selected,false)}
   Desk.DeskButton {text:"从同处试听";enabled:root.group&&!root.busy;onClicked:root.switchTo(root.selected,true)}
  }
  RowLayout {visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {text:"复制当前方案";enabled:!root.busy;onClicked:root.duplicate()}
   Desk.DeskButton {text:"检查差异";enabled:root.active&&!root.busy;onClicked:root.readFingerprint("compare")}
   Desk.DeskButton {text:"重命名";enabled:root.group&&!root.busy;onClicked:{nameEdit.text=root.group.entries[root.selected].name;rename.open()}}
  }
  RowLayout {visible:!root.compact;Layout.fillWidth:true
   Desk.DeskButton {text:"保存工作区";enabled:!root.busy;onClicked:root.workspace?root.saveActive():saveDialog.open()}
   Desk.DeskButton {text:"另存为成果谱";enabled:root.active&&!root.active.baseline;onClicked:resultDialog.open()}
  }
  Label {visible:!root.compact;Layout.fillWidth:true;wrapMode:Text.Wrap;color:theme.muted;text:"完整副本可自由编辑。保存当前方案使用新版本文件；其它已打开方案须逐个保存。成果保留整个方案谱。"}
 }
 Menu {id:projectMenu;MenuItem {text:"打开工作区";onTriggered:loadDialog.open()}MenuItem {text:"选择工作区保存位置";onTriggered:saveDialog.open()}}
 Dialog {id:rename;title:"方案名称";width:320;height:170;modal:true;standardButtons:Dialog.Ok|Dialog.Cancel;Desk.DeskTextField {id:nameEdit;anchors.fill:parent}onAccepted:{var g=Project.copy(root.group);g.entries[root.selected].name=nameEdit.text;root.setGroup(g);root.saveManifest()}}
 Files.FileDialog {id:saveDialog;selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:{root.workspace=String(fileUrl);if(root.pendingGroup){root.pendingGroup=false;root.newGroup()}else if(root.pendingCopy){root.pendingCopy=false;root.duplicate()}else root.saveActive()}}
 Files.FileDialog {id:loadDialog;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.load(String(fileUrl))}
 Files.FileDialog {id:resultDialog;selectExisting:false;nameFilters:["MuseScore (*.mscz)"];onAccepted:{var r=root.observer.copyWorkflowScore(String(fileUrl),root.observer.analysisRevision(),true);root.status=r.error||"成果谱已保存，原谱路径与状态不变"}}
}
