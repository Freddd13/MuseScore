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
 version:"1.2.0";requiresScore:true;pluginType:"dock";dockArea:"right";implicitWidth:400;implicitHeight:560
 property var observer:null
 property string workspace:""
 property var groups:[]
 property int groupIndex:-1
 property int selected:0
 property var currentInfo:({})
 property string status:"新建组后，在A里修改；切换时会自动保存。"
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
 property var currentOwner:findOwner(currentInfo.file,groups,workspace)
 property var active:currentOwner&&currentOwner.groupIndex===groupIndex?currentOwner.entry:null
 property var removedState:null
 Desk.UiTheme {id:theme}
 function fail(e){status=Messages.readable(e);busy=false}
 function local(path){return Project.path(path).replace(/\\/g,"/").toLowerCase()}
 function resolveEntry(e){return Project.fileUrl(Project.resolve(workspace,e.path))}
 function findOwner(file,items,project){
  if(!file)return null;
  var at=local(file);
  for(var g=0;g<items.length;++g)for(var i=0;i<items[g].entries.length;++i){
   var e=items[g].entries[i],paths=[Project.resolve(project,e.path)].concat(e.aliases||[]);
   for(var j=0;j<paths.length;++j)if(local(paths[j])===at)return {groupIndex:g,entryIndex:i,entry:e};
  }
  return null;
 }
 function refresh(){
  if(!observer)return;
  observer.score=curScore;var info=typeof observer.workflowStatus==="function"?observer.workflowStatus():observer.workflowInfo();
  currentInfo=info;if(info.error)status=Messages.readable(info.error);
 }
 function rangeText(scope){return scope?"第"+scope.firstMeasure+(scope.lastMeasure===scope.firstMeasure?"":"–"+scope.lastMeasure)+"小节":""}
 function fileName(kind){return Project.directory(workspace)+kind+"-"+Date.now()+"-"+Math.floor(Math.random()*1000000)+".mscz"}
 function setGroup(g){var a=Project.copy(groups);a[groupIndex]=g;groups=a;dirty=true}
 function saveManifest(){try{Project.writeModule(observer,workspace,"variants",{groups:groups,groupIndex:currentOwner?currentOwner.groupIndex:groupIndex,selected:currentOwner?currentOwner.entryIndex:selected},currentInfo);dirty=false;status="工作区已保存。";return true}catch(e){fail(e);return false}}
 function newGroup(){
  if(!workspace){pendingGroup=true;saveDialog.open();return}
  refresh();if(currentInfo.error){fail(currentInfo.error);return}if(observer.playing){fail("先停止播放，再新建写法。");return}
  if(currentOwner&&!currentOwner.entry.baseline&&!saveActive())return;
  var scope=observer.analysisScope(false);if(!scope.end){fail("先在谱面选一段，再新建组。");return}
  var base=fileName("baseline"),a=fileName("variant"),rev=observer.analysisRevision();
  var first=observer.copyWorkflowScore(Project.fileUrl(base),rev,false);if(first.error){fail(first.error);return}
  var second=observer.copyWorkflowScore(Project.fileUrl(a),rev,false);if(second.error){fail(second.error);return}
  var all=Project.copy(groups);all.push({id:"group-"+Date.now()+"-"+Math.floor(Math.random()*1000000),name:rangeText(scope),scope:scope,occurrence:1,source:currentInfo.file,entries:[Rules.newEntry("原始版本",Project.relative(workspace,base),true,first.sha256,""),Rules.newEntry("A · 试写",Project.relative(workspace,a),false,second.sha256,"")],baseline:null});groups=all;groupIndex=all.length-1;selected=1;
  readFingerprint("baseline");status="已建立A，正在打开。";
 }
 function readFingerprint(action){
  if(busy)return;if(observer.playing){fail("停止播放后可以检查差异。");return}
  busy=true;queryAction=action;queryRevision=observer.analysisRevision();fingerprint={measures:[],staves:0};nextMeasure=0;paging.start();
 }
 function page(){
  if(!visible||observer.analysisRevision()!==queryRevision){paging.stop();fail("谱面已变化，检查已取消");return}
  var p=observer.workflowFingerprint(nextMeasure,1,queryRevision);if(p.error){paging.stop();fail(p.error);return}
  var f=fingerprint;Array.prototype.push.apply(f.measures,p.measures);f.staves=p.staves;if(nextMeasure===0)f.context=p.context;fingerprint=f;nextMeasure=p.next;
  if(p.done){paging.stop();busy=false;var g=Project.copy(group);
   if(queryAction==="baseline"){g.baseline=Project.copy(fingerprint);setGroup(g);saveManifest();switchTo(1,false)}
   else if(active){var c=Rules.compare(g.baseline,fingerprint,g.scope);for(var i=0;i<g.entries.length;++i)if(g.entries[i].id===active.id){c.revision=queryRevision;g.entries[i].comparison=c;}setGroup(g);status=c.same?"范围外相同 · 段内 "+c.inside+" 小节改变":c.reasons.join("；")}
  }
 }
 function switchTo(index,audition){
  if(!group||index<0||index>=group.entries.length)return;
  refresh();
  if(currentOwner&&!currentOwner.entry.baseline&&!saveActive())return;
  selected=index;switching=true;var e=group.entries[index],r=observer.openWorkflowScore(resolveEntry(e),e.baseline,e.baseline?e.hash:"");switching=false;
  if(r.error){fail(r.error);refresh();return}selected=index;refresh();
  if(!e.savedRevision&&!currentInfo.dirty){var g=Project.copy(group);g.entries[index].savedRevision=observer.analysisRevision();setGroup(g);}
  if(!saveManifest())return;
  var range=Project.copy(group.scope);range.occurrence=group.occurrence||1;
  r=observer.navigateWorkflow(range,audition);if(r.error)fail(r.error);else status=audition?("试听第"+range.firstMeasure+"–"+range.lastMeasure+"小节 · 第"+range.occurrence+"遍"):"正在编辑："+e.name;
  if(!audition)readFingerprint("compare");
 }
 function duplicate(){
  refresh();if(!workspace){pendingCopy=true;saveDialog.open();return}
  if(!group||!active){fail("先打开一份写法，再复制");return}
  if(currentOwner&&!currentOwner.entry.baseline&&!saveActive())return;
  var path=fileName("variant"),r=observer.copyWorkflowScore(Project.fileUrl(path),observer.analysisRevision(),false);if(r.error){fail(r.error);return}
  var g=Project.copy(group);var letter=g.entries.length,name=letter<27?String.fromCharCode(64+letter)+" · 试写":"写法 "+letter;while(g.entries.some(function(e){return e.name===name})){++letter;name=letter<27?String.fromCharCode(64+letter)+" · 试写":"写法 "+letter}g.entries.push(Rules.newEntry(name,Project.relative(workspace,path),false,r.sha256,""));setGroup(g);saveManifest();switchTo(g.entries.length-1,false);
 }
 function saveActive(){
  refresh();if(!currentOwner||currentOwner.entry.baseline)return saveManifest();
  var ownerIndex=currentOwner.groupIndex,id=currentOwner.entry.id,r=null;
  if(typeof observer.saveWorkflowScore==="function"){
   r=observer.saveWorkflowScore(observer.analysisRevision());if(r.error){fail(r.error);return false}
  }
  if(!r||r.needsCopy){
   var path=fileName("saved-variant");r=observer.copyWorkflowScore(Project.fileUrl(path),observer.analysisRevision(),false);
   if(r.error){fail(r.error);return false}
  }
  var g=Project.copy(groups[ownerIndex]);
  for(var i=0;i<g.entries.length;++i)if(g.entries[i].id===id){
   var e=g.entries[i];e.aliases=(e.aliases||[]).concat([currentInfo.file]);
   e.aliases=e.aliases.filter(function(path,at,all){return all.indexOf(path)===at});
   e.path=Project.relative(workspace,r.path);e.hash=r.sha256;e.savedRevision=observer.analysisRevision();
  }
  var all=Project.copy(groups);all[ownerIndex]=g;groups=all;dirty=true;return saveManifest();
 }
 function removeWriting(){
  if(!group||busy)return;refresh();if(currentOwner&&!currentOwner.entry.baseline&&!saveActive())return;
  var wasCurrent=currentOwner&&currentOwner.groupIndex===groupIndex&&currentOwner.entryIndex===selected;
  var g=Project.copy(group);if(!g.id)g.id="group-"+g.entries[0].id;
  removedState={kind:"writing",groupId:g.id,groupIndex:groupIndex,entryIndex:selected,entry:Project.copy(g.entries[selected]),group:Project.copy(g)};
  g.entries.splice(selected,1);
  if(!g.entries.length){var a=Project.copy(groups);a.splice(groupIndex,1);groups=a;groupIndex=currentOwner?currentOwner.groupIndex:Math.min(groupIndex,a.length-1);selected=currentOwner?currentOwner.entryIndex:0}
  else {setGroup(g);selected=currentOwner&&currentOwner.groupIndex===groupIndex?currentOwner.entryIndex:Math.min(selected,g.entries.length-1)}
  refresh();saveManifest();if(wasCurrent&&group)switchTo(selected,false);
  status="已移除所选写法；点恢复删除可放回。";
 }
 function removeGroup(){
  if(!group||busy)return;refresh();if(currentOwner&&!currentOwner.entry.baseline&&!saveActive())return;
  var wasCurrent=currentOwner&&currentOwner.groupIndex===groupIndex;
  removedState={kind:"group",groupIndex:groupIndex,selected:selected,group:Project.copy(group)};
  var a=Project.copy(groups);a.splice(groupIndex,1);groups=a;
  groupIndex=currentOwner?currentOwner.groupIndex:Math.min(groupIndex,a.length-1);selected=currentOwner?currentOwner.entryIndex:0;
  refresh();saveManifest();if(wasCurrent&&group)switchTo(selected,false);
  status="已移除这组；谱文件保留在项目文件夹。";
 }
 function restoreRemoval(){
  if(!removedState||busy)return;refresh();if(currentOwner&&!currentOwner.entry.baseline&&!saveActive())return;
  var record=removedState,a=Project.copy(groups),at=-1;
  if(record.kind==="writing")for(var i=0;i<a.length;++i)if(a[i].id===record.groupId){at=i;break}
  if(at>=0){a[at].entries.splice(Math.min(record.entryIndex,a[at].entries.length),0,Project.copy(record.entry));selected=Math.min(record.entryIndex,a[at].entries.length-1)}
  else {at=Math.min(record.groupIndex,a.length);a.splice(at,0,Project.copy(record.group));selected=record.kind==="writing"?record.entryIndex:record.selected}
  groups=a;groupIndex=at;removedState=null;refresh();saveManifest();switchTo(selected,false);
  status="已恢复删除的项目。";
 }
 function saveAt(url){
  var next=String(url);
  if(workspace&&workspace!==next){
   refresh();if(!saveActive())return;
   var previous=workspace,a=Project.copy(groups);
   a.forEach(function(g){g.entries.forEach(function(e){e.path=Project.relative(next,Project.resolve(previous,e.path))})});
   groups=a;
  }
  workspace=next;
  if(pendingGroup){pendingGroup=false;newGroup()}
  else if(pendingCopy){pendingCopy=false;duplicate()}
  else saveActive();
 }
 function load(url){
  paging.stop();busy=false;refresh();if(currentOwner&&!currentOwner.entry.baseline&&!saveActive())return;
  try{
   var p=Project.read(observer,url).variants;
   if(!p||!Array.isArray(p.groups)||p.groups.some(function(g){return !g||!g.scope||!Array.isArray(g.entries)||!g.entries.length}))
    throw new Error("请选择完整谱试写保存的JSON工作区。");
   workspace=String(url);groups=p.groups;removedState=null;
   groupIndex=groups.length?Math.max(0,Math.min(p.groupIndex||0,groups.length-1)):-1;selected=p.selected||0;
   groups.forEach(function(g){if(!g.id)g.id="group-"+g.entries[0].id;g.entries.forEach(function(e){e.aliases=[];e.savedRevision=""})});
   if(groups.length)switchTo(selected,false);else status="工作区已打开，选择谱段后新建组。";
  }catch(e){fail(e)}
 }
 function saved(e){return currentOwner&&currentOwner.entry.id===e.id?(observer&&observer.analysisRevision()===e.savedRevision):true}
 function comparisonText(e){
  if(!e.comparison||(active&&active.id===e.id&&e.comparison.revision!==currentInfo.revision))return "点检查差异可更新";
  return e.comparison.same?"范围外相同":e.comparison.reasons.join("；");
 }
 onRun:{if(typeof newScoreObserver!=="function"){fail("请用 Kumo 版 MuseScore 打开此工具。");return}observer=newScoreObserver();observer.score=curScore;refresh()}
 Connections {target:root.observer;ignoreUnknownSignals:true;function onActiveScoreChanged(){if(!root.switching)root.refresh()}}
 onScoreStateChanged:if(!switching&&visible){refresh();if(busy&&observer.analysisRevision()!==queryRevision){paging.stop();busy=false;status="乐谱已修改；点检查差异可更新列表。"}}
 onVisibleChanged:{if(!visible){paging.stop();busy=false}else if(observer)refresh()}
 Timer {id:paging;interval:1;repeat:true;onTriggered:root.page()}
 Rectangle {anchors.fill:parent;color:theme.background}
 ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:6
  RowLayout {Layout.fillWidth:true
   Label {Layout.fillWidth:true;elide:Text.ElideRight;text:root.currentOwner?root.currentOwner.entry.name+" · 完整谱试写":root.currentInfo.name?root.currentInfo.name+" · 完整谱试写":"完整谱试写";font.bold:true;color:theme.text}
   Desk.DeskButton {text:"项目";onClicked:projectMenu.open()}
   Desk.DeskButton {text:"悬浮";onClicked:if(typeof root.setPanelFloating==="function")root.setPanelFloating(!root.panelFloating)}
  }
  Label {Layout.fillWidth:true;Layout.preferredHeight:root.compact?24:40;maximumLineCount:root.compact?1:2;wrapMode:Text.Wrap;elide:Text.ElideRight;text:(root.group?root.rangeText(root.group.scope)+" · ":"")+root.status;color:theme.muted}
  ScrollView {id:legacyBody;objectName:"legacyVariantBody";ScrollBar.vertical.active:true;ScrollBar.vertical.objectName:"legacyVariantScrollBar";ScrollBar.horizontal.policy:ScrollBar.AlwaysOff;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:48;contentWidth:availableWidth;clip:true
   ColumnLayout {objectName:"legacyVariantContent";width:Math.max(0,legacyBody.availableWidth-(legacyBody.contentHeight>legacyBody.availableHeight?legacyBody.ScrollBar.vertical.width+4:0));spacing:6
  Flow {visible:!root.compact;Layout.fillWidth:true;spacing:6
   Desk.DeskButton {text:"新建组";enabled:!root.busy;onClicked:root.newGroup()}
   Desk.DeskButton {text:"复制新写法";enabled:root.active&&!root.busy;onClicked:root.duplicate()}
   Desk.DeskButton {text:"保存工作区";enabled:!root.busy;onClicked:root.workspace?root.saveActive():saveDialog.open()}
  }
  Desk.DeskComboBox {visible:!root.compact&&root.groups.length>0;Layout.fillWidth:true;enabled:!root.busy;model:root.groups.map(function(g){return g.name});currentIndex:root.groupIndex;onActivated:{var next=currentIndex;root.refresh();if(root.currentOwner&&!root.currentOwner.entry.baseline&&!root.saveActive())return;root.groupIndex=next;root.selected=0;root.refresh();root.switchTo(0,false)}}
  RowLayout {visible:!root.compact;Layout.fillWidth:true
   Label {text:root.group?root.rangeText(root.group.scope):"选择谱段后新建组";color:theme.muted}Item {Layout.fillWidth:true}
   Label {visible:!!root.group;text:"第几遍";color:theme.muted}Desk.DeskSpinBox {visible:!!root.group;enabled:root.group&&!root.busy;from:1;to:99;value:root.group?root.group.occurrence:1;onValueModified:{var g=Project.copy(root.group);g.occurrence=value;root.setGroup(g)}}
  }
  Desk.DeskComboBox {visible:root.compact&&!!root.group;Layout.fillWidth:true;enabled:root.group&&!root.busy;model:root.group?root.group.entries.map(function(e){return e.name}):[];currentIndex:root.selected;onActivated:root.switchTo(currentIndex,false)}
  ListView {objectName:"legacyVariantList";visible:!root.compact;enabled:!root.busy;Layout.fillWidth:true;Layout.preferredHeight:Math.max(100,Math.min(340,root.height-410));Layout.minimumHeight:100;clip:true;model:root.group?root.group.entries:[];currentIndex:root.selected;ScrollBar.vertical:ScrollBar {}
   delegate:ItemDelegate {width:ListView.view.width;height:55;highlighted:index===root.selected
    contentItem:ColumnLayout {spacing:2
     Label {Layout.fillWidth:true;elide:Text.ElideRight;text:modelData.name+" · "+(root.active&&root.active.id===modelData.id?(modelData.baseline?"正在查看":"正在编辑"):modelData.baseline?"保留初始内容":root.saved(modelData)?"已保存":"修改待保存");font.bold:index===root.selected;color:theme.text}
     Label {Layout.fillWidth:true;elide:Text.ElideRight;text:root.comparisonText(modelData);color:theme.muted;font.pixelSize:12}
    }
    onClicked:root.switchTo(index,false)
   }
  }
  Flow {Layout.fillWidth:true;spacing:6
   Desk.DeskButton {text:"切换并定位";enabled:root.group&&!root.busy;onClicked:root.switchTo(root.selected,false)}
   Desk.DeskButton {text:"从同处试听";enabled:root.group&&!root.busy;onClicked:root.switchTo(root.selected,true)}
  }
  Flow {visible:!root.compact;Layout.fillWidth:true;spacing:6
   Desk.DeskButton {text:"检查差异";enabled:root.active&&!root.busy;onClicked:root.readFingerprint("compare")}
   Desk.DeskButton {text:"重命名";enabled:root.group&&!root.busy;onClicked:{nameEdit.text=root.group.entries[root.selected].name;rename.open()}}
   Desk.DeskButton {objectName:"legacyDeleteWriting";text:"删除写法";enabled:root.group&&!root.busy;onClicked:root.removeWriting()}
  }
  Flow {visible:!root.compact;Layout.fillWidth:true;spacing:6
   Desk.DeskButton {objectName:"legacyRestoreWriting";text:"恢复删除";enabled:root.removedState&&!root.busy;onClicked:root.restoreRemoval()}
   Desk.DeskButton {text:"另存为成果谱";enabled:root.active&&!root.active.baseline;onClicked:resultDialog.open()}
  }
  Label {objectName:"legacyVariantHelp";visible:!root.compact;Layout.fillWidth:true;wrapMode:Text.Wrap;color:theme.muted;text:"点一份写法直接切换。切换前自动保存当前修改；JSON工作区与谱文件放在一起。"}
   }
  }
 }
 Menu {id:projectMenu
  MenuItem {text:"打开工作区";onTriggered:loadDialog.open()}
  MenuItem {text:"选择工作区保存位置";onTriggered:saveDialog.open()}
  MenuItem {text:"移除这组";enabled:root.group&&!root.busy;onTriggered:root.removeGroup()}
  MenuSeparator {}
  MenuItem {text:"打开内置分段方案";onTriggered:root.cmd("regional-variants")}
 }
 Dialog {id:rename;title:"写法名称";width:320;height:170;modal:true;standardButtons:Dialog.Ok|Dialog.Cancel;Desk.DeskTextField {id:nameEdit;anchors.fill:parent}onAccepted:{var g=Project.copy(root.group);g.entries[root.selected].name=nameEdit.text;root.setGroup(g);root.saveManifest()}}
 Files.FileDialog {id:saveDialog;selectExisting:false;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.saveAt(String(fileUrl));onRejected:{root.pendingGroup=false;root.pendingCopy=false}}
 Files.FileDialog {id:loadDialog;nameFilters:["Kumo 工作区 (*.json)"];onAccepted:root.load(String(fileUrl))}
 Files.FileDialog {id:resultDialog;selectExisting:false;nameFilters:["MuseScore (*.mscz)"];onAccepted:{var r=root.observer.copyWorkflowScore(String(fileUrl),root.observer.analysisRevision(),true);root.status=r.error||"成果谱已保存。"}}
}
