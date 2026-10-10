import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3
import QtQuick.Window 2.2
import MuseScore 3.0
import "../HarmonyAssistant" as Desk
import "../HarmonyAssistant/Analysis.js" as Analysis
import "../HarmonyAssistant/Harmony.js" as Harmony
import "Rules.js" as Rules

MuseScore {
    id:root
    menuPath:"Plugins.Arrangement Check"
    description:"编配检查：和声疑点、基础对位与上下文可弹性；只读分析。"
    version:"1.0.0"
    requiresScore:true
    pluginType:"dock"
    dockArea:"right"
    implicitWidth:370
    implicitHeight:720
    property int preferredRibbonHeight:76
    property string detailPanelTitle:"编配检查 · 详情"
    property color detailPanelBackground:theme.background
    property bool ribbon:(typeof panelPlacement==="string" && (panelPlacement==="top" || panelPlacement==="bottom")) || (width>500 && height<180)
    property var observer:null
    property var owner:null
    property var config:Rules.defaults()
    property var scope:({start:0,end:0,firstTrack:0,endTrack:8})
    property var snapshot:null
    property var task:null
    property string workerSource:""
    property int workerToken:0
    property var issues:[]
    property var roles:[]
    property var imported:[]
    property var counts:({harmony:0,counterpoint:0,playability:0})
    property var current:list.currentIndex>=0 && list.currentIndex<issues.length ? issues[list.currentIndex] : null
    property string status:"点击分析开始"
    property string revision:""
    property bool analyzed:false
    property bool requested:false
    property var cachedData:null
    property var pendingData:null
    property int dirtyStart:-1
    property int dirtyEnd:-1
    property bool localRefresh:false
    property bool stale:false
    property bool running:false
    property bool started:false
    property bool pendingLocate:false
    property bool whole:false
    property bool activeSurface:visible && (!observer || observer.surfaceVisible)
    property real worstBatch:0
    Desk.UiTheme {id:theme}
    function persist() {if(observer)observer.saveConfiguration("ArrangementAssistant",config)}
    function syncDetail() {
        if(typeof root.showDetailPanel==="function")root.showDetailPanel(ribbon && config.dualPanel)
    }
    function cancel() {work.stop();if(observer && typeof observer.cancelReadOnlyJob==="function")observer.cancelReadOnlyJob();workerToken=0;running=false;task=null;snapshot=null;pendingData=null}
    function markDirty(from,until) {
        if(!requested)return
        if(typeof from==="number" && from>=0 && typeof until==="number" && until>from) {dirtyStart=dirtyStart<0?from:Math.min(dirtyStart,from);dirtyEnd=Math.max(dirtyEnd,until)} else {dirtyStart=-1;dirtyEnd=-1}
        stale=true;status=observer && observer.playing ? "播放中 · 缓存已过期" : "待更新"
        cancel()
        if(config.autoRefresh && activeSurface && observer && !observer.playing)refresh.restart()
    }
    function start(retainScope) {
        if(!observer || typeof observer.analysisRange!=="function") {status="需要支持完整只读快照的 Kumo 版本";return}
        if(observer.playing) {status="播放中保留缓存，停播后可分析";return}
        cancel()
        if(!retainScope || scope.end<=scope.start)scope=observer.analysisScope(whole)
        if(!scope.end) {status="当前没有可分析的范围";return}
        if(!scope.piano) {status="请选择钢琴声部组再分析";return}
        revision=observer.analysisRevision()
        // One preceding measure provides voice-leading context. Long ties retain their logical onset.
        requested=true
        var target=Rules.clone(scope);localRefresh=!!(retainScope && cachedData && dirtyStart>=0 && dirtyEnd>dirtyStart)
        if(localRefresh) {
            var begin=dirtyStart,finish=dirtyEnd
            for(var p=0;p<cachedData.pedals.length;++p) {var window=cachedData.pedals[p];if(window.tick<finish && window.end>begin) {begin=Math.min(begin,window.tick);finish=Math.max(finish,window.end)}}
            var originalStart=begin,originalEnd=finish
            for(var n=0;n<cachedData.notes.length;++n) {var note=cachedData.notes[n];if(note.attackTick<originalEnd && note.logicalEnd>originalStart) {begin=Math.min(begin,note.attackTick);finish=Math.max(finish,note.logicalEnd)}}
            target.start=Math.max(scope.start,begin);target.end=Math.min(scope.end,finish)
            if(target.end<=target.start) {localRefresh=false;target=Rules.clone(scope)}
        }
        var context=Math.max(0,target.start-3840)
        snapshot={scope:target,notes:[],rests:[],meters:[],harmonies:[],hands:[],pedals:[],parts:[],tempos:[],contextStart:context,next:context}
        task=null;running=true;status="读取完整声部…";work.start()
    }
    function slice() {
        if(!activeSurface || !observer || observer.playing) {cancel();return}
        if(observer.analysisRevision()!==revision) {cancel();markDirty();return}
        var began=Date.now()
        if(!task) {
            var page=observer.analysisRange(snapshot.contextStart,snapshot.scope.end,snapshot.scope.firstTrack,snapshot.scope.endTrack,snapshot.next,16,revision)
            if(page.error) {cancel();status="读取暂停："+page.error;stale=true;return}
            var names=["notes","rests","meters","harmonies","hands","pedals","parts","tempos"]
            for(var i=0;i<names.length;++i)Array.prototype.push.apply(snapshot[names[i]],page[names[i]]||[])
            snapshot.next=page.nextTick
            if(page.done) {
                pendingData=snapshot
                status="检查和声、对位与手位…"
                if(typeof observer.startReadOnlyJob==="function" && workerSource.length) {
                    work.stop();workerToken=observer.startReadOnlyJob(workerSource,{data:snapshot,config:config,imported:imported});snapshot=null
                } else task=Rules.create(snapshot,config,imported)
            }
        } else if(Rules.step(task,2)) {
            finishResult({issues:task.issues,roles:task.roles})
        }
        worstBatch=Math.max(worstBatch,Date.now()-began)
    }
    function finishResult(result) {
            var output=result.issues
            if(localRefresh && pendingData) {
                var from=pendingData.scope.start,until=pendingData.scope.end
                output=issues.filter(function(i){return i.tick<from || i.tick>=until}).concat(output).sort(function(a,b){return a.tick-b.tick})
            }
            if(pendingData) {
                if(localRefresh && cachedData) {
                    var begin=pendingData.contextStart,end=pendingData.scope.end
                    cachedData.notes=cachedData.notes.filter(function(n){return n.tick<begin || n.tick>=end}).concat(pendingData.notes)
                    cachedData.pedals=cachedData.pedals.filter(function(p){return p.end<=begin || p.tick>=end}).concat(pendingData.pedals)
                } else cachedData=pendingData
            }
            dirtyStart=-1;dirtyEnd=-1;pendingData=null
            var totals={harmony:0,counterpoint:0,playability:0}
            for(var i=0;i<output.length;++i)totals[output[i].type]++
            issues=output;counts=totals;roles=result.roles;analyzed=true;stale=false;running=false;task=null;snapshot=null
            status="已更新"+(localRefresh?" · 局部刷新":"")+(imported.length?" · 使用导入解释，请核对来源":"")+(scope.noncontiguous?" · 非连续选择按起止范围检查":"")
            list.currentIndex=issues.length?0:-1;work.stop();workerToken=0
    }
    function locate() {
        if(!current || !curScore)return
        if(observer && observer.playing) {pendingLocate=true;status="定位将在停播后执行";return}
        pendingLocate=false
        var n=current.notes[0];if(!n)return
        var cursor=curScore.newCursor();cursor.track=n.track;cursor.rewindToTick(n.tick)
        if(cursor.element && cursor.element.notes && n.index<cursor.element.notes.length)curScore.selection.select(cursor.element.notes[n.index])
        if(typeof root.focusPanel==="function")root.focusPanel()
    }
    function ignore() {
        if(!current)return
        var c=Rules.clone(config);c.ignored.push(current.id);config=c;persist();start(true)
    }
    function roleChange(track,field,value) {
        var c=Rules.clone(config),role=c.roles[String(track)] || {}
        role[field]=value;c.roles[String(track)]=role;config=c;persist();markDirty()
    }
    function selectedHand(hand) {
        if(!curScore || !curScore.selection)return
        var selection=curScore.selection,c=Rules.clone(config),count=0
        if(selection.isRange && selection.startSegment) {
            c.handOverrides.push({start:selection.startSegment.tick,end:selection.endSegment?selection.endSegment.tick:scope.end,firstTrack:selection.startStaff*4,endTrack:(selection.endStaff+1)*4,hand:hand});count++
        } else for(var i=0;i<selection.elements.length;++i) {
            var e=selection.elements[i]
            if(typeof e.pitch!=="number" || !e.parent)continue
            c.handOverrides.push({start:e.parent.tick,end:e.parent.tick+1,firstTrack:e.track,endTrack:e.track+1,pitch:e.pitch,hand:hand});count++
        }
        if(count) {config=c;markDirty();status="已校正所选范围分手，停播后更新"} else status="请先在谱面选择音符或选区"
    }
    function newScore() {
        if(owner===curScore)return
        cancel();owner=curScore;if(observer)observer.score=curScore
        // Ignored locations and role/chord overrides belong to this score, not to every score.
        var c=Rules.clone(config);c.roles={};c.handOverrides=[];c.ignored=[];c.chords=[];config=c
        imported=[];issues=[];roles=[];counts={harmony:0,counterpoint:0,playability:0};analyzed=false;requested=false;cachedData=null;pendingData=null;dirtyStart=-1;dirtyEnd=-1;stale=false;revision="";status="点击分析开始"
    }
    onRun: {
        started=true
        if(typeof root.newScoreObserver==="function")observer=root.newScoreObserver()
        if(observer) {
            var saved=observer.loadConfiguration("ArrangementAssistant")
            if(saved.schema===1) {
                var c=Rules.defaults();for(var key in saved)if(key!=="roles" && key!=="handOverrides" && key!=="ignored" && key!=="chords")c[key]=saved[key];config=c
            }
        }
        if(observer && typeof observer.startReadOnlyJob==="function")workerSource=Rules.workerBundle({
            harmony:observer.readTextFile(String(Qt.resolvedUrl("../HarmonyAssistant/Harmony.js"))),
            timeline:observer.readTextFile(String(Qt.resolvedUrl("../HarmonyAssistant/Timeline.js"))),
            rules:observer.readTextFile(String(Qt.resolvedUrl("Rules.js")))})
        newScore();syncDetail()
    }
    onScoreStateChanged: {
        if(!started)return
        if(owner!==curScore)newScore()
        else if(observer && requested && observer.analysisRevision()!==revision)markDirty(state.startLayoutTick,state.endLayoutTick)
    }
    onActiveSurfaceChanged: {
        if(!started)return
        if(!activeSurface) {cancel();refresh.stop()}
        else if(requested && stale && config.autoRefresh && observer && !observer.playing)refresh.restart()
    }
    onRibbonChanged:syncDetail()
    Component.onDestruction:{cancel();persist()}
    Timer {id:refresh;interval:750;onTriggered:root.start(true)}
    Timer {id:work;interval:12;repeat:true;onTriggered:root.slice()}
    Connections {
        target:observer;ignoreUnknownSignals:true
        onPositionChanged:{if(observer.playing) {if(root.running) {root.cancel();root.stale=root.requested;root.status="播放中 · 显示缓存"}refresh.stop()}if(root.activeSurface && !observer.playing) {if(root.pendingLocate)root.locate();if(root.stale && root.config.autoRefresh && !refresh.running && !root.running)refresh.start()}}
        onScoreChanged:root.newScore()
        onReadOnlyJobFinished:{
            if(token!==root.workerToken)return
            if(!root.activeSurface || observer.playing || observer.analysisRevision()!==root.revision) {root.cancel();root.markDirty();return}
            if(result.error) {root.cancel();root.status="规则计算失败："+result.error;return}
            root.finishResult(result)
        }
    }
    Rectangle {anchors.fill:parent;color:theme.background}
    RowLayout {
        id:summary;anchors.top:parent.top;anchors.left:parent.left;anchors.right:parent.right;anchors.margins:8;height:root.ribbon?52:32
        Item {Layout.fillWidth:true;visible:root.ribbon && root.config.alignment>0}
        Label {text:"编配检查";font.bold:true;color:theme.text;visible:root.width>620 || !root.ribbon}
        Label {text:root.analyzed || root.running ? "第"+root.scope.firstMeasure+"–"+root.scope.lastMeasure+"小节" : "未分析";color:theme.text;Layout.maximumWidth:140;elide:Text.ElideRight;visible:root.ribbon}
        Label {text:"和声 "+root.counts.harmony+" · 对位 "+root.counts.counterpoint+" · 可弹性 "+root.counts.playability;color:theme.text;visible:root.ribbon}
        Label {text:root.stale?"待更新":root.running?"分析中":"";color:theme.muted;visible:root.ribbon && root.width>750}
        Item {Layout.fillWidth:true;visible:!root.ribbon || root.config.alignment<2}
        Desk.DeskButton {text:"分析";enabled:!root.running && (!root.observer || !root.observer.playing);onClicked:root.start(false)}
        Desk.DeskButton {text:root.ribbon?"详情":"设置";onClicked:{if(root.ribbon) {if(typeof root.showDetailPanel==="function")root.showDetailPanel(true);else floatingDetails.show()} else settings.open()}}
    }
    Item {
        id:details
        parent:root.ribbon ? (typeof root.detailPanelVisible==="boolean" && root.detailPanelVisible ? root.detailPanelHost : floatingDetails.contentItem) : root
        anchors.fill:parent;anchors.topMargin:root.ribbon?8:summary.height+16;anchors.margins:8
        visible:!root.ribbon || (typeof root.detailPanelVisible==="boolean" && root.detailPanelVisible) || floatingDetails.visible
        ColumnLayout {
            anchors.fill:parent;spacing:6
            RowLayout {
                Layout.fillWidth:true
                Desk.DeskComboBox {id:rangeBox;model:["选区／当前小节","当前钢琴全谱"];Layout.fillWidth:true;onActivated:root.whole=currentIndex===1}
                Desk.DeskButton {text:"悬浮";visible:typeof root.setPanelFloating==="function" && !root.ribbon;onClicked:root.setPanelFloating(!root.panelFloating)}
                Desk.DeskButton {text:"设置";onClicked:settings.open()}
            }
            Label {text:"和声 "+root.counts.harmony+" · 对位 "+root.counts.counterpoint+" · 可弹性 "+root.counts.playability;color:theme.text;Layout.fillWidth:true}
            Desk.DeskComboBox {id:styleBox;model:["调性钢琴／流行","严格对位","爵士"];currentIndex:root.config.style;Layout.fillWidth:true;onActivated:{var c=Rules.clone(root.config);c.style=currentIndex;root.config=c;root.persist();root.markDirty()}}
            Label {text:root.status;Layout.fillWidth:true;Layout.preferredHeight:28;Layout.minimumHeight:28;Layout.maximumHeight:28;maximumLineCount:2;elide:Text.ElideRight;wrapMode:Text.WordWrap;color:theme.muted;font.pixelSize:11}
            ListView {
                id:list;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:100;Layout.preferredHeight:270;clip:true;model:root.issues;spacing:1
                ScrollBar.vertical:ScrollBar {}
                delegate:Rectangle {
                    width:list.width;height:58;color:ListView.isCurrentItem?theme.selected:theme.field
                    Column {anchors.fill:parent;anchors.margins:7;spacing:3
                        Text {text:"第"+modelData.bar+"小节 · 第"+Number(modelData.beat).toFixed(2)+"拍 · "+modelData.severity;color:theme.muted;font.pixelSize:10}
                        Text {text:modelData.title;width:parent.width;elide:Text.ElideRight;color:theme.text;font.pixelSize:12}
                    }
                    MouseArea {anchors.fill:parent;onClicked:{list.currentIndex=index;if(!root.observer || !root.observer.playing)root.locate()}}
                }
            }
            Rectangle {Layout.fillWidth:true;height:1;color:theme.subtleLine}
            Label {text:"当前问题的依据";font.bold:true;color:theme.text}
            Flickable {
                Layout.fillWidth:true;Layout.minimumHeight:90;Layout.preferredHeight:175;Layout.maximumHeight:220;clip:true;contentHeight:explanation.height
                Text {id:explanation;width:parent.width;text:root.current?root.current.basis+"\n\n涉及音："+root.current.notes.map(function(n){return Harmony.spelledName(n.tpc,n.pitch,false)+" · 谱表"+(Math.floor(n.track/4)+1)+"声部"+(n.track%4+1)}).join("，"):"选择一条问题查看依据。未列出问题不代表完整证明可弹。";wrapMode:Text.WordWrap;color:theme.text;font.pixelSize:12}
                ScrollBar.vertical:ScrollBar {}
            }
            RowLayout {
                Desk.DeskButton {text:"定位";enabled:!!root.current;onClicked:root.locate()}
                Desk.DeskButton {text:"上一条";enabled:list.currentIndex>0;onClicked:{list.currentIndex--;root.locate()}}
                Desk.DeskButton {text:"下一条";enabled:list.currentIndex+1<root.issues.length;onClicked:{list.currentIndex++;root.locate()}}
                Desk.DeskButton {text:"忽略此处";enabled:!!root.current;onClicked:root.ignore()}
            }
            RowLayout {
                Desk.DeskButton {text:"声部／分手";onClicked:roleDialog.open()}
                Desk.DeskButton {text:"和弦解释";onClicked:chordDialog.open()}
                Desk.DeskButton {text:"导入和声";onClicked:importDialog.open()}
            }
        }
    }
    Window {id:floatingDetails;title:"编配检查 · 详情";width:370;height:700;minimumWidth:310;minimumHeight:450;flags:Qt.Tool;color:theme.background}
    Popup {
        id:settings;parent:details;modal:true;focus:true;closePolicy:Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x:Math.max(0,(root.width-width)/2);y:0;width:Math.min(350,details.width);height:Math.min(520,details.height)
        background:Rectangle {color:theme.background;border.color:theme.line}
        ScrollView {anchors.fill:parent;clip:true
            ColumnLayout {width:settings.availableWidth
                Label {text:"编配检查设置";font.bold:true;color:theme.text}
                Repeater {model:[{key:"harmony",text:"和声疑点"},{key:"counterpoint",text:"基础对位"},{key:"playability",text:"上下文可弹性"},{key:"autoRefresh",text:"停播后自动刷新"},{key:"dualPanel",text:"顶部摘要 + 右侧详情"}]
                    Desk.DeskSwitch {text:modelData.text;checked:root.config[modelData.key];onClicked:{var c=Rules.clone(root.config);c[modelData.key]=checked;root.config=c;root.persist();root.syncDetail();root.markDirty()}}
                }
                Desk.DeskComboBox {model:["摘要左对齐","摘要居中","摘要右对齐"];currentIndex:root.config.alignment;onActivated:{var c=Rules.clone(root.config);c.alignment=currentIndex;root.config=c;root.persist()}}
                Desk.DeskComboBox {model:["调性未确认"].concat(Harmony.rootNames);currentIndex:root.config.tonic<0?0:Harmony.rootPcs.indexOf(root.config.tonic)+1;onActivated:{var c=Rules.clone(root.config);c.tonic=currentIndex?Harmony.rootPcs[currentIndex-1]:-1;root.config=c;root.persist();root.markDirty()}}
                Desk.DeskSwitch {text:"已确认小调";enabled:root.config.tonic>=0;checked:root.config.minor;onClicked:{var c=Rules.clone(root.config);c.minor=checked;root.config=c;root.persist();root.markDirty()}}
                Label {text:"对位规则（预设可覆盖）";color:theme.text}
                Repeater {model:[{key:"parallel",text:"平行五八"},{key:"hidden",text:"外声部隐伏"},{key:"crossing",text:"声部交叉"},{key:"overlap",text:"声部重叠"},{key:"leaps",text:"大跳"},{key:"resolution",text:"已确认调性下的解决疑点"}]
                    Desk.DeskSwitch {text:modelData.text;checked:Rules.ruleEnabled(root.config,modelData.key);onClicked:{var c=Rules.clone(root.config);c.ruleOverrides[modelData.key]=checked;root.config=c;root.persist();root.markDirty()}}
                }
                Repeater {model:["left","right"]
                    ColumnLayout {
                        property string side:modelData
                        Label {text:side==="left"?"左手跨度（半音）":"右手跨度（半音）";color:theme.text}
                        Repeater {model:[{key:"comfort",text:"舒适"},{key:"maximum",text:"最大"},{key:"white",text:"两端白键最大"}]
                            RowLayout {property string parameter:modelData.key
                                Label {text:modelData.text;color:theme.text;Layout.fillWidth:true}
                                Desk.DeskSpinBox {from:7;to:24;value:root.config[side][parameter];onValueModified:{var c=Rules.clone(root.config);c[side][parameter]=value;root.config=c;root.persist();root.markDirty()}}
                            }
                        }
                    }
                }
                Desk.DeskButton {text:"关闭";onClicked:settings.close()}
            }
        }
    }
    Popup {
        id:roleDialog;parent:details;modal:true;focus:true;width:Math.min(350,details.width);height:Math.min(550,details.height);y:0
        background:Rectangle {color:theme.background;border.color:theme.line}
        ScrollView {anchors.fill:parent;clip:true
            ColumnLayout {width:roleDialog.availableWidth
                Label {text:"自动建议 + 快速校正";font.bold:true;color:theme.text}
                Label {text:roles.length?"按谱表／voice 设置；最高最低音线适用于和弦声部。":"先分析以取得声部列表。";wrapMode:Text.WordWrap;Layout.fillWidth:true;color:theme.muted}
                Repeater {model:root.roles
                    ColumnLayout {property int track:modelData.track;property var row:modelData
                        Label {text:"谱表"+(Math.floor(track/4)+1)+" · voice "+(track%4+1)+" · "+row.source+(row.uncertain?" · 待确认":"");color:theme.text}
                        Desk.DeskComboBox {model:["旋律","低音","内声部","不参与声部检查"];currentIndex:["melody","bass","inner","ignore"].indexOf(row.role);Layout.fillWidth:true;onActivated:root.roleChange(track,"role",["melody","bass","inner","ignore"][currentIndex])}
                        Desk.DeskComboBox {model:["自动分手／符号优先","左手","右手"];currentIndex:["auto","left","right"].indexOf(row.hand);Layout.fillWidth:true;onActivated:root.roleChange(track,"hand",["auto","left","right"][currentIndex])}
                        Desk.DeskComboBox {model:["单音线（复调待确认）","最高音线","最低音线"];currentIndex:["single","top","bottom"].indexOf(row.line);Layout.fillWidth:true;onActivated:root.roleChange(track,"line",["single","top","bottom"][currentIndex])}
                        Desk.DeskSwitch {text:"独立声部（加倍请关闭）";checked:row.independent;onClicked:root.roleChange(track,"independent",checked)}
                    }
                }
                RowLayout {Desk.DeskButton {text:"所选音／选区左手";onClicked:root.selectedHand("left")}Desk.DeskButton {text:"所选音／选区右手";onClicked:root.selectedHand("right")}}
                Desk.DeskButton {text:"清除范围分手";onClicked:{var c=Rules.clone(root.config);c.handOverrides=[];root.config=c;root.markDirty()}}
                Desk.DeskButton {text:"恢复自动建议";onClicked:{var c=Rules.clone(root.config);c.roles={};root.config=c;root.markDirty()}}
                Desk.DeskButton {text:"关闭";onClicked:roleDialog.close()}
            }
        }
    }
    Popup {
        id:chordDialog;parent:details;modal:true;focus:true;width:Math.min(350,details.width);height:300;y:0
        background:Rectangle {color:theme.background;border.color:theme.line}
        ColumnLayout {anchors.fill:parent
            Label {text:"局部和弦解释";font.bold:true;color:theme.text}
            Label {text:"起止 tick（每四分音符 480）";color:theme.muted}
            RowLayout {Desk.DeskSpinBox {id:chordStart;from:0;to:2147483647;value:root.current?root.current.tick:root.scope.start;editable:true}Desk.DeskSpinBox {id:chordEnd;from:0;to:2147483647;value:root.scope.end;editable:true}}
            Desk.DeskComboBox {id:chordRoot;model:Harmony.rootNames;Layout.fillWidth:true}
            Desk.DeskComboBox {id:chordQuality;model:Harmony.defs.map(function(d){return d.name});Layout.fillWidth:true}
            Desk.DeskButton {text:"应用本谱局部解释";enabled:chordEnd.value>chordStart.value;onClicked:{var c=Rules.clone(root.config);c.chords.push({start:chordStart.value,end:chordEnd.value,part:root.scope.firstTrack,root:Harmony.rootPcs[chordRoot.currentIndex],definition:chordQuality.currentIndex,source:"局部指定"});root.config=c;root.start(true);chordDialog.close()}}
            Desk.DeskButton {text:"清除本谱局部解释";onClicked:{var c=Rules.clone(root.config);c.chords=[];root.config=c;root.markDirty()}}
            Desk.DeskButton {text:"关闭";onClicked:chordDialog.close()}
        }
    }
    FileDialog {
        id:importDialog;title:"导入和声助手分析（当前谱）";nameFilters:["和声分析 (*.json)"];selectExisting:true
        onAccepted:{
            try {
                var document=JSON.parse(observer.readTextFile(String(fileUrl))),validated=Analysis.validate(document)
                if(!document.regions || !document.regions.length)throw new Error("需要包含范围的 schema 2 分析文件")
                root.imported=document.regions.map(function(c){var copy=Rules.clone(c);copy.source="导入解释（请核对谱与范围）";return copy})
                root.status="已导入；请核对谱版本与范围";root.start(false)
            } catch(error) {root.status="导入失败："+error}
        }
    }
}
