import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import MuseScore 3.0
import "Harmony.js" as Harmony
import "Preferences.js" as Preferences
import "AnnotationEdits.js" as AnnotationEdits
import "Analysis.js" as Analysis
import "Timeline.js" as Timeline
import QtQuick.Dialogs 1.3
import QtQuick.Window 2.2

MuseScore {
    property string preferredPanelGroup: "rightTools"
    id: root
    UiTheme {id:theme}
    menuPath: "Plugins.Harmony Assistant"
    description: "钢琴和声助手：持续音识别、级数、音程功能与可选谱面配色。"
    version: "1.10.1"
    requiresScore: true
    pluginType: "dock"
    dockArea: "right"
    implicitWidth: 360
    implicitHeight: 740

    property var configuration: Preferences.defaults()
    property bool loadingConfiguration: false
    property bool ribbon: (width > 460 && height < 300) ||
        (typeof panelPlacement === "string" && (panelPlacement === "top" || panelPlacement === "bottom") && height < 400)

    // Settings outlive the widgets in each dock surface.
    property QtObject keyTonic: QtObject {property int currentIndex:0}
    property QtObject keyMode: QtObject {property int currentIndex:0}
    property QtObject autoChord: QtObject {property bool checked:true}
    property QtObject pedalContext: QtObject {property bool checked:true}
    property QtObject arpeggioWindow: QtObject {property int currentIndex:0}
    property QtObject scope: QtObject {property int currentIndex:0}
    property QtObject rootCombo: QtObject {property int currentIndex:0}
    property QtObject qualityCombo: QtObject {property int currentIndex:0}
    property QtObject followPlayback: QtObject {property bool checked:true}
    property QtObject scoreColoring: QtObject {property bool checked:false}
    property QtObject allColor: QtObject {property bool checked:false}
    property QtObject showChords: QtObject {property bool checked:false}
    property QtObject showFunctions: QtObject {property bool checked:false}
    property var detailFlick:root.dualDetailActive?auxHarmonyDetails.item:root.ribbon?popupHarmonyDetails.item:inlineHarmonyDetails.item
    property bool expanded:detailFlick?detailFlick.width>=680:false
    property bool advancedNative: observer && typeof observer.contextSnapshot === "function"
    property bool fixedChordNative: observer && typeof observer.setActiveScorePreview === "function"
    property bool dualDetailActive:ribbon && configuration.dualPanel && typeof root.detailPanelVisible==="boolean" && root.detailPanelVisible
    property bool dualSummaryActive:!ribbon && configuration.dualPanel && typeof root.summaryPanelVisible==="boolean" && root.summaryPanelVisible
    property bool chromaticChord: Harmony.isChromatic(Harmony.rootPcs[keyTonic.currentIndex],keyMode.currentIndex===1,chordRoot,chordDefinition)
    property color chordInk:configuration.chromaticAccent && chromaticChord ? configuration.chromaticColor : ink
    property var contextNotes: []
    property var analysisRecords: []
    property var analysisRegions: []
    property var automaticRegions: []
    property var regionBuilder: null
    property string regionWorkerSource: ""
    property int regionJobToken: 0
    property string regionRevision: ""
    property int regionFirstTrack: 0
    property int regionEndTrack: 0
    property var unplacedMarkers: []
    property var unplacedIndex: ({})
    property var manualOverrides: []
    property var editUndo: []
    property var editRedo: []
    property string loadedFingerprint: ""
    property bool manualNeedsReview: false
    property int annotationTick: -1
    property string annotationSelectionKey: ""
    property int liveRoot: -1
    property int liveDefinition: -1
    property string instantChordText: "—"
    property string instantDegreeText: "—"
    property int focusedPart: 0
    property var ownedRegion: Timeline.atTick(analysisRegions,currentTick,focusedPart)
    property string summaryChordText: configuration.summaryMode===0 ? instantChordText : ownedRegion ? ownedRegion.chord : analysisDirty ? "分析中…" : "—"
    property string summaryDegreeText: configuration.summaryMode===0 ? instantDegreeText : ownedRegion ? ownedRegion.degree : "—"
    property int summaryRoot: configuration.summaryMode===0 ? liveRoot : ownedRegion ? ownedRegion.root : -1
    property int summaryDefinition: configuration.summaryMode===0 ? liveDefinition : ownedRegion ? ownedRegion.definition : -1
    property color summaryInk: configuration.chromaticAccent && Harmony.isChromatic(Harmony.rootPcs[keyTonic.currentIndex],keyMode.currentIndex===1,summaryRoot,summaryDefinition) ? configuration.chromaticColor : ink
    property var summaryToneRows: makeToneRows(summaryRoot,summaryDefinition,configuration.summaryMode===1 && ownedRegion ? ownedRegion.notes : contextNotes.length ? contextNotes : currentNotes)
    property int analysisEnd: analysisRecords.length ? (analysisRecords[analysisRecords.length-1].scoreEnd || analysisRecords[analysisRecords.length-1].tick+480) : 1920
    property var pendingRecords: []
    property var importedRecords: []
    property var importedRegions: []
    property string analysisFingerprint: ""
    property int analysisNextTick: 0
    property bool analysisDirty: true
    property string pendingFileAction: ""
    property string pendingExport: ""
    property int desiredSummaryHeight:Math.max(32,Math.ceil(configuration.summaryPixels*(configuration.summaryOrder>=2?1+configuration.summaryDegreeRatio/100:Math.max(1,configuration.summaryDegreeRatio/100))*1.3+(configuration.summaryOrder>=2?configuration.summaryGap:0)+6))
    property int preferredRibbonHeight:desiredSummaryHeight
    property bool compactPanelTitle:true
    property int lastSummaryHeight:32
    onDesiredSummaryHeightChanged:resizeSummary(false)
    property bool independentPanelSurfaces:true
    property string summaryPanelTitle:"和弦摘要"
    property bool ribbonContentHeight: true
    property string detailPanelTitle:"和声助手 · 详细面板"
    property color detailPanelBackground:theme.background
    property bool started: false
    property var observer: null
    property bool surfaceActive: (visible || dualSummaryActive || dualDetailActive || (showChords.checked && !!ownerScore)) && (!observer || observer.surfaceVisible)
    property string analysisStatus: !ownerScore ? "打开乐谱后开始识别" : analysisTimer.running ? "正在读取 · "+pendingRecords.length+" 个位置" : regionJobToken || regionBuilder ? "正在分析前后和弦" : analysisDirty ? observer && observer.playing ? "播放中 · 当前和弦" : "等待更新" : "已更新 · "+analysisRegions.length+" 处和弦"
    property bool nativeAvailable: observer !== null
    property var nativeSnapshot: ({})
    property bool internalChange: false
    property var ownerScore: null
    property bool cacheDirty: true
    property var timeline: []
    property var buildTimeline: []
    property var buildSegment: null
    property int firstTrack: 0
    property int endTrack: 0
    property int selectionTrack: 0
    property int currentTick: -1
    property var currentNotes: [] // Numeric descriptors only. Never cache native note wrappers.
    property var currentPitches: []
    property var colorLedger: []
    property var activeColorKeys: ({})
    property bool undoPause: false
    property bool settingsExpanded: false
    property bool keyboardExpanded: false
    property var toneRows: []
    property int chordRoot: -1
    property int chordDefinition: -1
    property int pianoStart: 48
    property int pianoOctaves: 3
    property int keyAccidentals: 0
    property bool manualKey: false
    property bool playbackAvailable: false
    property bool lastPlaying: false
    property int lastPlayTick: -1
    property string chordText: "—"
    property string degreeText: "—"
    property string matchText: "请选择音符、和弦或休止符"
    property string alternativesText: ""
    property string voicingText: "—"
    property string inversionText: "—"
    property string scopeText: "当前乐器 · 包含全部声部"
    property string positionText: "—"
    property string noticeText: ""
    property string placementNotice: ""
    property color ink: theme.text
    property color muted: theme.muted
    property var qualityNames: Harmony.defs.map(function(d) { return d.name })

    property var manualPositions:[]
    property var positionIndex:AnnotationEdits.index(manualPositions)
    property bool scoreRangeEditing:false
    onScoreRangeEditingChanged:syncScoreRangeEditor()
    onOwnedRegionChanged:syncScoreRangeEditor()
    onAnalysisDirtyChanged:syncScoreRangeEditor()
    function editSnapshot(){return {overrides:Timeline.clone(manualOverrides),positions:Timeline.clone(manualPositions)}}
    function syncScoreRangeEditor(){
        if(!observer || typeof observer.setChordEditRegion!=="function")return
        var region=ownedRegion
        if(!region || !scoreRangeEditing || analysisDirty || lastPlaying || !showChords.checked){observer.setChordEditRegion({});return}
        var position=positionIndex[region.part+":"+region.start]||{x:0,y:0}
        observer.setChordEditRegion({start:region.start,end:region.end,track:region.part,offsetX:position.x,offsetY:position.y})
    }
    function moveAnnotation(tick,part,x,y){
        editUndo=editUndo.concat([editSnapshot()]).slice(-100);editRedo=[]
        manualPositions=AnnotationEdits.moved(manualPositions,tick,part,x,y)
        saveCorrections();applyScorePreview();syncScoreRangeEditor()
    }
    function resizeSummary(force){
        if(!started || !(ribbon||dualSummaryActive) || typeof root.fitPanelHeight!=="function")return
        var current=dualSummaryActive&&root.summaryPanelHost?root.summaryPanelHost.height:height
        root.fitPanelHeight(desiredSummaryHeight,dualSummaryActive,force||current<=lastSummaryHeight+4);lastSummaryHeight=desiredSummaryHeight
    }
    function placeWindow(window,anchor){
        if(typeof root.panelScreen!=="function")return
        var area=root.panelScreen(window.placed?window.contentItem:anchor||root);if(!area.width)return
        window.width=Math.min(window.width,area.width-16);window.height=Math.min(window.height,area.height-16)
        if(!window.placed){window.x=area.x+(area.width-window.width)/2;window.y=area.y+(area.height-window.height)/2;window.placed=true}
        window.x=Math.max(area.x,Math.min(window.x,area.x+area.width-window.width));window.y=Math.max(area.y,Math.min(window.y,area.y+area.height-window.height))
    }
    function openSummarySettings(anchor){placeWindow(summarySettings,anchor);summarySettings.show();summarySettings.raise();summarySettings.requestActivate()}
    function toggleSection(key){var c=Timeline.clone(configuration),index=c.collapsedSections.indexOf(key);if(index<0)c.collapsedSections.push(key);else c.collapsedSections.splice(index,1);configuration=Preferences.clean(c);configurationTimer.restart()}
    function changeSummary(key,value){var c=Timeline.clone(configuration);c[key]=value;configuration=Preferences.clean(c);configurationTimer.restart()}
    function windowTicks() {return [0,240,480,960,1920][arpeggioWindow.currentIndex] || 0}
    function windowHeight() {return 650}
    function setRibbonPosition(index) {
        var c=JSON.parse(JSON.stringify(configuration));c.ribbonAlign=index
        configuration=Preferences.clean(c);configurationTimer.restart()
    }
    function partForTrack(track) {
        if(curScore)for(var i=0;i<curScore.parts.length;++i)if(track>=curScore.parts[i].startTrack && track<curScore.parts[i].endTrack)return curScore.parts[i].startTrack
        return firstTrack
    }
    function highlightedTick() {return lastPlaying && followPlayback.checked ? currentTick : annotationTick>=0 ? annotationTick : currentTick}
    function rebuildRegions() {
        analysisRegions=Timeline.overlay(Timeline.overlay(automaticRegions,importedRegions),manualNeedsReview?[]:manualOverrides)
    }
    function loadCorrections() {
        if(!advancedNative || loadedFingerprint===analysisFingerprint)return
        if((manualOverrides.length || manualPositions.length) && loadedFingerprint.length) {
            manualNeedsReview=true;noticeText="谱面已变更；人工范围暂未应用，请复核后确认。";return
        }
        loadedFingerprint=analysisFingerprint
        var saved=observer.loadConfiguration("HarmonyAssistant-regions-"+analysisFingerprint)
        if(saved.schema===1 && saved.fingerprint===analysisFingerprint) {
            try{manualOverrides=Timeline.validEdits(saved.overrides);manualPositions=AnnotationEdits.positions(saved.positions)}catch(error){noticeText="人工范围未读取："+error}
        }
    }
    function saveCorrections() {
        if(!advancedNative || !loadedFingerprint.length || manualNeedsReview)return
        if(!observer.saveConfiguration("HarmonyAssistant-regions-"+loadedFingerprint,{schema:1,fingerprint:loadedFingerprint,overrides:manualOverrides,positions:manualPositions}))noticeText="人工范围保存失败。"
    }
    function editRange(command,start,end,rootIndex,qualityIndex,bassIndex) {
        if(analysisDirty){noticeText="正在更新和声区间，请稍候。";return}
        if(command==="select"){openAnnotation(start,focusedPart);return}
        if(command==="position" && ownedRegion){moveAnnotation(ownedRegion.start,ownedRegion.part,0,0);return}
        if(command==="review"){loadedFingerprint=analysisFingerprint;manualNeedsReview=false;rebuildRegions();saveCorrections();applyScorePreview();return}
        var next=Timeline.clone(manualOverrides),region=ownedRegion
        if(command==="undo" || command==="redo") {
            var source=command==="undo"?editUndo:editRedo,target=command==="undo"?editRedo:editUndo
            if(!source.length)return
            target=target.concat([editSnapshot()]);var saved=source[source.length-1];next=Array.isArray(saved)?saved:saved.overrides;manualPositions=Array.isArray(saved)?manualPositions:AnnotationEdits.positions(saved.positions);source=source.slice(0,-1)
            if(command==="undo"){editUndo=source;editRedo=target}else{editRedo=source;editUndo=target}
        } else {
            if(manualNeedsReview){noticeText="请先复核已有人工范围。";return}
            if(command==="split" && region){next.push(Timeline.clone(region));next[next.length-1].start=start}
            else if(command==="merge" && region) {
                var previous=null
                for(var i=0;i<analysisRegions.length;++i)if(analysisRegions[i].part===region.part && analysisRegions[i].end===region.start)previous=analysisRegions[i]
                if(!previous){noticeText="没有相邻的前一和弦。";return}
                var merged=Timeline.clone(previous);merged.end=region.end;next.push(merged);annotationTick=merged.start
            } else {
                if(start<0 || end<=start || end>analysisEnd){noticeText="范围必须在谱内，且终点晚于起点。";return}
                if(command==="restore")next=Timeline.restore(next,start,end,partForTrack(selectionTrack))
                else {
                    if(region && region.source==="manual")next=next.filter(function(n){return !(n.part===region.part && n.start===region.start && n.end===region.end)})
                    if(command==="range" && region)next=next.concat(Timeline.resizeNeighbours(analysisRegions,region,start,end))
                    var edit=region?Timeline.clone(region):{part:partForTrack(selectionTrack),notes:Timeline.clone(currentNotes)}
                    if(!edit.notes.length) {
                        // A rest still has a time position. Use a nearby numeric anchor in this part.
                        var nearest=null,distance=Infinity
                        for(var a=0;a<analysisRecords.length;++a)for(var b=0;b<analysisRecords[a].notes.length;++b) {
                            var candidate=analysisRecords[a].notes[b],delta=Math.abs(candidate.tick-start)
                            if(partForTrack(candidate.track)===edit.part && delta<distance){nearest=candidate;distance=delta}
                        }
                        if(nearest)edit.notes=[Timeline.clone(nearest)]
                    }
                    if(command==="assign") {
                        var info=Timeline.describe({root:Harmony.rootPcs[rootIndex],rootSpelling:Harmony.rootNames[rootIndex],definition:qualityIndex},edit.notes,start,Harmony.rootPcs[keyTonic.currentIndex],keyMode.currentIndex===1,configuration.degreeMode)
                        if(bassIndex>0) {
                            info.bass=Harmony.rootPcs[bassIndex-1]
                            info.chord=info.chord.split("/")[0]+(info.bass!==info.root?"/"+Harmony.rootNames[bassIndex-1]:"")
                        }
                        for(var field in info)edit[field]=info[field]
                    }
                    if(edit.root===undefined){edit.root=-1;edit.definition=-1}
                    edit.start=start;edit.end=end;edit.source="manual";edit.suppressed=command==="suppress";next.push(edit);annotationTick=start
                }
            }
            editUndo=editUndo.concat([editSnapshot()]).slice(-100);editRedo=[]
            if(command==="range" && region && start!==region.start){var prior=positionIndex[region.part+":"+region.start];if(prior){manualPositions=AnnotationEdits.moved(manualPositions,region.start,region.part,0,0);manualPositions=AnnotationEdits.moved(manualPositions,start,region.part,prior.x,prior.y)}}
        }
        manualOverrides=Timeline.validEdits(next);loadedFingerprint=analysisFingerprint
        rebuildRegions();saveCorrections();analyze();applyScorePreview()
    }
    function openAnnotation(tick,track) {
        if(!curScore)return
        annotationTick=tick
        if(!lastPlaying) {
            var partEnd=curScore.nstaves*4
            for(var p=0;p<curScore.parts.length;++p)if(curScore.parts[p].startTrack===track){partEnd=curScore.parts[p].endTrack;break}
            var frame=observer.snapshot(tick,track,partEnd,false)
            if(frame.notes.length) {var note=resolveNote(curScore,frame.notes[0]);if(note)curScore.selection.select(note)}
            selectionTrack=track;setScope();displayTick(tick,true)
            var selected=selectedLocation();annotationSelectionKey=selected?selected.tick+":"+selected.track:""
        }
        if(typeof root.focusPanel==="function")root.focusPanel()
        if(root.ribbon)openDetails()
        settingsExpanded=true;scoreRangeEditing=true
        if(configuration.collapsedSections.indexOf("range")>=0)toggleSection("range")
        syncScoreRangeEditor();Qt.callLater(function(){if(root.detailFlick)root.detailFlick.scrollRange()})
    }
    function syncDetailPanel() {
        if(!started || loadingConfiguration)return
        if(typeof root.showDetailPanel==="function")root.showDetailPanel(ribbon && configuration.dualPanel)
        if(typeof root.showSummaryPanel==="function")root.showSummaryPanel(!ribbon && configuration.dualPanel)
        syncAnalysisSurface()
        if(!ribbon)detailPopup.visible=false
    }
    function syncAnalysisSurface() {
        if(observer && typeof observer.auxiliarySurfaceVisible==="boolean") {
            observer.auxiliarySurfaceVisible=dualDetailActive || dualSummaryActive || (showChords.checked && !!curScore)
            observer.enabled=visible || observer.auxiliarySurfaceVisible
        }
    }
    function toggleDetailPanel() {
        var c=JSON.parse(JSON.stringify(configuration));c.dualPanel=!(dualDetailActive || dualSummaryActive)
        configuration=Preferences.clean(c);configurationTimer.restart();syncDetailPanel()
    }
    function openDetails() {
        if(dualDetailActive) {
            if(typeof root.focusDetailPanel==="function")root.focusDetailPanel()
            else root.showDetailPanel(true)
        }
        else detailPopup.open()
    }
    function openDetailWindow() {
        if(!ribbon&&typeof root.setPanelFloating==="function"){root.setPanelFloating(true);root.focusPanel();return}
        var c=JSON.parse(JSON.stringify(configuration));c.dualPanel=false
        configuration=Preferences.clean(c);syncDetailPanel();configurationTimer.restart();detailPopup.open()
    }
    function detailPanelClosed() {
        if(!started)return
        var c=JSON.parse(JSON.stringify(configuration));c.dualPanel=false
        configuration=Preferences.clean(c);configurationTimer.restart()
    }
    function chordDescriptor(note,frame) {
        var d=JSON.parse(JSON.stringify(note))
        if(fixedChordNative) {
            d.chord=configuration.chordContent===1 ? "" : frame.chord || (frame.notes.length?"?":"")
            d.degree=configuration.chordContent===0 ? "" : frame.degree || (frame.root<0 && frame.notes.length && configuration.chordContent===1?"?":"")
            d.chordTick=frame.start===undefined?frame.tick:frame.start;d.chordUntil=frame.end===undefined?frame.tick:frame.end
            d.chordOrder=configuration.chordOrder;d.chordScale=configuration.chordScale/100
            d.chordFont=["","Edwin","Arial"][configuration.chordFont]
            d.degreeFont=configuration.degreeFont
            var position=positionIndex[frame.part+":"+d.chordTick];if(position){d.offsetX=position.x;d.offsetY=position.y}
            d.chordColor=configuration.chromaticAccent && frame.chromatic ? configuration.chromaticColor : configuration.chordColor
            d.highlightColor=configuration.highlightColor;d.highlightBackground=configuration.highlightBackground
            d.preferExistingHarmony=configuration.respectExistingHarmony
            d.chordMask=configuration.chordMask;d.onTop=true;d.avoidScoreContent=configuration.avoidScoreContent
        } else d.chord=configuration.chordContent===0 ? frame.chord : configuration.chordContent===1 ? frame.degree : frame.chord+" · "+frame.degree
        return d
    }
    function functionText(pitch, tonic, definition) {
        var fixed=configuration.noteLabels[Harmony.mod12(pitch)]
        if(typeof fixed==="string" && fixed.length)return fixed
        var label=Harmony.labelFor(pitch,tonic,definition)
        return configuration.labels[label] === undefined ? label : configuration.labels[label]
    }
    function colorFor(pitch, tonic, definition) {
        return configuration.colors[Harmony.role(Harmony.labelFor(pitch,tonic,definition))] || configuration.colors["外"]
    }
    function collectPreferences() {
        var c=JSON.parse(JSON.stringify(configuration))
        c.auto=autoChord.checked; c.follow=followPlayback.checked; c.coloring=scoreColoring.checked
        c.allColor=allColor.checked; c.chordLabels=showChords.checked; c.noteFunctions=showFunctions.checked
        c.pedal=pedalContext.checked; c.window=arpeggioWindow.currentIndex; c.scope=scope.currentIndex
        c.keyMode=keyMode.currentIndex; c.keyTonic=keyTonic.currentIndex; c.manualKey=manualKey
        c.root=rootCombo.currentIndex; c.quality=qualityCombo.currentIndex
        c.keyboard=keyboardExpanded; c.settings=settingsExpanded
        return Preferences.clean(c)
    }
    function applyPreferences(value) {
        loadingConfiguration=true
        try {
            var c=Preferences.clean(value)
            configuration=c
            autoChord.checked=c.auto; followPlayback.checked=c.follow; scoreColoring.checked=c.coloring
            allColor.checked=c.allColor; showChords.checked=c.chordLabels; showFunctions.checked=c.noteFunctions
            pedalContext.checked=c.pedal; arpeggioWindow.currentIndex=c.window; scope.currentIndex=c.scope
            keyMode.currentIndex=c.keyMode; keyTonic.currentIndex=c.keyTonic; manualKey=c.manualKey
            rootCombo.currentIndex=c.root; qualityCombo.currentIndex=c.quality
            keyboardExpanded=c.keyboard; settingsExpanded=c.settings
        } catch(error) {noticeText="配置未应用："+String(error)}
        finally {loadingConfiguration=false}
        optionsChanged()
    }
    function savePreferences() {
        if(!started || loadingConfiguration)return
        var c=collectPreferences()
        if(advancedNative) {
            if(!observer.saveConfiguration("HarmonyAssistant",c))noticeText="配置保存失败，请检查设置目录权限。"
        } else if(settingsStore.item) settingsStore.item.payload=JSON.stringify(c)
    }
    function optionsChanged(rebuild) {
        if(!started || loadingConfiguration)return
        syncDetailPanel()
        configurationTimer.restart()
        if(rebuild!==false) {cancelRegionJob();analysisDirty=true; regionBuilder=null;analysisTimer.stop()}
        requestRefresh(false)
        if(advancedNative) {
            if(rebuild===false && !analysisDirty)applyScorePreview()
            else {if(!showChords.checked && !showFunctions.checked)observer.setScorePreview([]); scheduleAnalysis()}
        }
    }
    function cancelRegionJob() {
        if(regionJobToken && observer && typeof observer.cancelReadOnlyJob==="function")observer.cancelReadOnlyJob()
        regionJobToken=0
    }
    function finishRegions(regions) {
        automaticRegions=regions;regionBuilder=null;analysisDirty=false
        rebuildRegions();analyze();applyScorePreview()
        if(pendingExport.length){var action=pendingExport;pendingExport="";chooseFile(action)}
    }
    function needsAnalysis() {return advancedNative && ((scoreColoring.checked && allColor.checked) || showChords.checked || showFunctions.checked || configuration.summaryMode===1 || configuration.detailMode===1 || pendingExport.length>0)}
    function scheduleAnalysis() {
        if(!started || !surfaceActive || !curScore || !needsAnalysis() || analysisTimer.running || regionBuilder || regionJobToken || (observer.playing && analysisRecords.length) || !analysisDirty)return
        setScope()
        regionRevision=typeof observer.analysisRevision==="function"?observer.analysisRevision():""
        pendingRecords=[]
        analysisNextTick=0
        analysisTimer.start()
    }
    function frameResult(frame) {
        var notes=frame.analysisNotes || frame.notes || []
        var pitches=notes.map(function(n){return n.pitch})
        var result=!notes.length ? {root:-1,definition:-1} : autoChord.checked ? Harmony.detect(pitches) :
            {root:Harmony.rootPcs[rootCombo.currentIndex],definition:qualityCombo.currentIndex}
        var imported=Analysis.atTick(importedRecords,frame.tick)
        if(imported){result.root=imported.root;result.definition=imported.definition}
        var chord="", degree="", tonic=Harmony.rootPcs[keyTonic.currentIndex]
        if(!manualKey) {
            var major=[11,6,1,8,3,10,5,0,7,2,9,4,11,6,1]
            tonic=major[(frame.keySignature || 0)+7]
            if(keyMode.currentIndex===1)tonic=Harmony.mod12(tonic+9)
        }
        if(result.root>=0) {
            var name=nameOf(result.root)
            for(var i=0;i<notes.length;++i)if(Harmony.mod12(notes[i].pitch)===result.root){name=Harmony.spelledName(notes[i].tpc,notes[i].pitch,prefersFlats());break}
            chord=name+Harmony.defs[result.definition].suffix
            degree=Harmony.roman(tonic,keyMode.currentIndex===1,result.root,result.definition,configuration.degreeMode,notes.length?Math.min.apply(null,pitches):undefined)
            if(notes.length && Harmony.mod12(Math.min.apply(null,pitches))!==result.root)chord+="/"+nameOf(Math.min.apply(null,pitches))
        }
        if(imported){if(typeof imported.chord==="string")chord=imported.chord;if(typeof imported.degree==="string")degree=imported.degree}
        return {tick:frame.tick,bar:frame.bar,beat:frame.beat,root:result.root,definition:result.definition,
            chord:chord,degree:degree,notes:notes,parts:frame.parts,pedalWindows:frame.pedalWindows,scoreEnd:frame.scoreEnd,keySignature:frame.keySignature,imported:!!imported,chromatic:Harmony.isChromatic(tonic,keyMode.currentIndex===1,result.root,result.definition,configuration.degreeMode)}
    }
    function buildAnalysisChunk() {
        if(!curScore || !surfaceActive || !needsAnalysis())return
        if(observer.playing && analysisRecords.length){cancelRegionJob();analysisTimer.stop();analysisDirty=true;return}
        if(regionBuilder){buildRegionChunk();return}
        var batch=observer.analysisFrames(analysisNextTick,observer.playing?8:32,firstTrack,endTrack,pedalContext.checked,windowTicks())
        analysisFingerprint=batch.fingerprint || ""
        var frames=batch.frames || []
        for(var i=0;i<frames.length;++i)pendingRecords.push(frameResult(frames[i]))
        analysisNextTick=batch.nextTick
        if(analysisNextTick>=0) {analysisTimer.start(); return}
        analysisRecords=pendingRecords;pendingRecords=[];loadCorrections()
        if(regionWorkerSource.length && typeof observer.startReadOnlyJob==="function") {
            regionFirstTrack=firstTrack;regionEndTrack=endTrack
            regionJobToken=observer.startReadOnlyJob(regionWorkerSource,{frames:analysisRecords,config:collectPreferences(),part:firstTrack})
            if(regionJobToken){matchText="正在判断前后和弦…";return}
        }
        regionBuilder=Timeline.createBuilder(analysisRecords,collectPreferences(),firstTrack)
        buildRegionChunk()
    }
    function buildRegionChunk() {
        var startedAt=Date.now(),complete=false
        for(var count=0;count<32 && Date.now()-startedAt<2;++count)if(Timeline.step(regionBuilder,1)){complete=true;break}
        if(!complete){analysisTimer.start();return}
        finishRegions(regionBuilder.regions)
    }
    function applyScorePreview() {
        if(!advancedNative)return
        var descriptors=[],written={},markers={},regions=analysisRegions
        for(var r=0;r<regions.length;++r) {
            var region=regions[r]
            if(showChords.checked && region.notes.length) {
                var anchor=region.notes.filter(function(n){return n.tick===region.start})[0]||region.notes[0]
                var marker=chordDescriptor(anchor,region);markers[region.part+":"+region.start]=marker
            }
        }
        for(var i=0;i<analysisRecords.length;++i) {
            var frame=analysisRecords[i]
            for(var n=0;n<frame.notes.length;++n) {
                var note=frame.notes[n]
                if(note.tick!==frame.tick)continue
                var key=note.tick+":"+note.track+":"+note.index
                if(written[key])continue
                written[key]=true
                var part=partForTrack(note.track),owned=Timeline.atTick(regions,frame.tick,part)
                if(!owned)continue
                var d=Timeline.clone(note)
                if(scoreColoring.checked && allColor.checked)d.color=colorFor(d.pitch,owned.root,owned.definition)
                if(showFunctions.checked)d.label=functionText(d.pitch,owned.root,owned.definition)
                var markerKey=part+":"+frame.tick,m=markers[markerKey]
                if(m && m.track===note.track && m.index===note.index) {
                    for(var field in m)d[field]=m[field]
                    delete markers[markerKey]
                }
                if(d.color || d.label || d.chord || d.degree)descriptors.push(d)
            }
        }
        for(var markerKey in markers)descriptors.push(markers[markerKey])
        if(configuration.repeatBars && showChords.checked) {
            var lastBar=-1
            for(i=0;i<analysisRecords.length;++i) {
                frame=analysisRecords[i]
                if(frame.bar===lastBar)continue
                lastBar=frame.bar
                for(r=0;r<regions.length;++r)if(regions[r].start<frame.tick && frame.tick<regions[r].end && regions[r].notes.length) {
                    var repeated=Timeline.clone(regions[r]);repeated.start=frame.tick
                    descriptors.push(chordDescriptor(repeated.notes[0],repeated))
                }
            }
        }
        observer.setScorePreview(descriptors);syncScoreRangeEditor()
        observer.clearNotePreviewColors();repaintNotes()
        if(typeof observer.previewStatus==="function") {
            var status=observer.previewStatus()
            unplacedMarkers=status.unplaced || []
            var unavailable={}
            for(var u=0;u<unplacedMarkers.length;++u)unavailable[unplacedMarkers[u].track+":"+unplacedMarkers[u].tick]=true
            unplacedIndex=unavailable
            placementNotice=status.hidden>0 ? status.hidden+" 个记号待排：点击面板「记号」查看并定位。" : status.fontFallback ? "记号字体缺失，已回退到 "+status.fontFallback : ""
        }
    }
    function prepareExport(action) {
        pendingExport=action
        if(analysisDirty || !analysisRecords.length) {analysisDirty=true; scheduleAnalysis()}
        else {pendingExport="";chooseFile(action)}
    }
    function chooseFile(action) {pendingFileAction=action; exchangeDialog.open()}
    function exchangeFile(path) {
        try {
            var action=pendingFileAction, content=""
            if(action.indexOf("import")===0) {
                content=observer.readTextFile(path)
                if(!content.length)throw Error("文件为空、超过 16 MiB 或无法读取")
                var parsed=JSON.parse(content)
                if(action==="import-config") {applyPreferences(Preferences.clean(parsed));savePreferences();noticeText="配置已导入并保存。"}
                else {
                    var imported=Analysis.validate(parsed)
                    // Validate every section before changing memory or persisted corrections.
                    var overrides=Timeline.validEdits(imported.overrides||[]),regions=Timeline.validEdits(imported.regions||[])
                    setScope()
                    var context=observer.contextSnapshot(0,firstTrack,endTrack,pedalContext.checked,windowTicks(),false),fingerprint=context.fingerprint
                    if(imported.fingerprint!==fingerprint)throw Error("分析与当前乐谱 / 乐器范围不一致，未应用")
                    overrides.concat(regions).forEach(function(r){if((context.scoreEnd && r.end>context.scoreEnd) || r.part<firstTrack || r.part>=endTrack || partForTrack(r.part)!==r.part)throw Error("分析区间超出乐谱 / 乐器范围")})
                    manualOverrides=overrides;manualPositions=AnnotationEdits.positions(imported.positions);loadedFingerprint=fingerprint;manualNeedsReview=false;editUndo=[];editRedo=[];saveCorrections()
                    importedRegions=regions;importedRegions.forEach(function(r){r.source="imported"})
                    importedRecords=importedRegions.length?[]:imported.frames; analysisDirty=true; scheduleAnalysis(); requestRefresh(false)
                    noticeText="已导入 "+imported.frames.length+" 个分析切片。"
                }
            } else {
                content=action==="export-config" ? JSON.stringify(collectPreferences(),null,2) :
                    action==="csv" ? Analysis.csvRegions(analysisRegions) :
                    JSON.stringify(Analysis.document(analysisRecords,analysisFingerprint,collectPreferences(),analysisRegions,manualOverrides,manualPositions),null,2)
                var suffix=action==="csv"?".csv":".json"
                if(path.toLowerCase().slice(-suffix.length)!==suffix)path+=suffix
                if(!observer.writeTextFile(path,content))throw Error("无法写入文件")
                noticeText="已导出："+decodeURIComponent(path)
            }
        } catch(error) {noticeText="操作未完成："+String(error)}
    }

    function same(a, b) { return a && b && a.is(b) }
    function isOpen(score) {
        if (!score) return false
        var opened = scores
        for (var i = 0; i < opened.length; ++i) if (opened[i].is(score)) return true
        return false
    }
    function prefersFlats() {
        var name = Harmony.rootNames[keyTonic.currentIndex]
        return name.indexOf("♭") >= 0 || name === "F" ||
                (keyMode.currentIndex === 1 && ["D","G","C","F"].indexOf(name) >= 0)
    }
    function nameOf(pc) { return Harmony.pcName(pc, prefersFlats()) }
    function noteName(note) {
        var spelling = Harmony.spelledName(note.tpc, note.pitch, prefersFlats())
        // B♯3 and C♭4 have an octave different from their MIDI enharmonic spelling.
        var naturals = {C:0,D:2,E:4,F:5,G:7,A:9,B:11}
        var accidentals = (spelling.match(/♯/g) || []).length - (spelling.match(/♭/g) || []).length
        return spelling + (Math.floor((note.pitch - naturals[spelling.charAt(0)] - accidentals) / 12) - 1)
    }
    function rootName() {
        if (chordRoot < 0) return "—"
        if (!autoChord.checked) return Harmony.rootNames[rootCombo.currentIndex]
        var namingNotes = contextNotes.length ? contextNotes : currentNotes
        for (var i = 0; i < namingNotes.length; ++i)
            if (Harmony.mod12(namingNotes[i].pitch) === chordRoot)
                return Harmony.spelledName(namingNotes[i].tpc, namingNotes[i].pitch, prefersFlats())
        return nameOf(chordRoot)
    }
    function rootIndex(pc) {
        var name = nameOf(pc), index = Harmony.rootNames.indexOf(name)
        return index < 0 ? 0 : index
    }
    function findSegment(element) {
        var parent = element
        while (parent && parent.type !== Element.SEGMENT) parent = parent.parent
        return parent
    }
    function selectedLocation() {
        if (!curScore || !curScore.selection) return null
        var selection = curScore.selection, elements = selection.elements
        for (var i = 0; i < elements.length; ++i) {
            var el = elements[i]
            if (el.type === Element.NOTE || el.type === Element.CHORD || el.type === Element.REST) {
                var segment = findSegment(el)
                if (segment) return {tick:segment.tick, track:el.track}
            }
        }
        if (selection.isRange && selection.startSegment)
            return {tick:selection.startSegment.tick, track:selection.startStaff * 4}
        return null
    }
    function setScope() {
        focusedPart=partForTrack(selectionTrack)
        firstTrack = 0
        endTrack = curScore.nstaves * 4
        scopeText = "全谱 · 全部声部"
        if (scope.currentIndex === 0) {
            var parts = curScore.parts
            for (var i = 0; i < parts.length; ++i) {
                var part = parts[i]
                if (selectionTrack >= part.startTrack && selectionTrack < part.endTrack) {
                    firstTrack = part.startTrack
                    endTrack = part.endTrack
                    scopeText = part.partName.replace(/<[^>]*>/g, "") + " · 全部声部"
                    break
                }
            }
        }
    }
    function changeScore() {
        if (same(curScore, ownerScore)) return
        saveCorrections()
        undoPause = false
        restoreColors(true)
        buildTimer.stop()
        buildSegment = null
        ownerScore = curScore
        if (observer) observer.score = curScore
        colorLedger = []
        activeColorKeys = ({})
        undoPause = false
        timeline = []
        cancelRegionJob();analysisTimer.stop()
        manualOverrides=[];manualPositions=[];scoreRangeEditing=false;editUndo=[];editRedo=[];analysisRegions=[];automaticRegions=[];regionBuilder=null;loadedFingerprint="";manualNeedsReview=false;annotationTick=-1
        unplacedMarkers=[];unplacedIndex=({});placementNotice=""
        importedRecords = []
        importedRegions = []
        analysisRecords = []
        analysisDirty = true
        cacheDirty = true
        manualKey = false
        lastPlaying = false
        lastPlayTick = -1
        playbackAvailable = !!observer || (!!curScore && typeof curScore.harmonyPlaybackTick === "number" &&
                typeof curScore.harmonyPlaybackActive === "boolean")
        clearDisplay(curScore ? "正在读取当前小节" : "没有打开乐谱")
        syncAnalysisSurface()
    }
    function clearDisplay(message) {
        currentTick = -1
        if(fixedChordNative)observer.setActiveScorePreview(-1)
        currentNotes = []
        currentPitches = []
        chordRoot = -1
        chordDefinition = -1
        chordText = "—"
        degreeText = "—"
        voicingText = "—"
        inversionText = "—"
        alternativesText = ""
        toneRows = []
        positionText = "—"
        matchText = message
    }
    function requestRefresh(dirty) {
        if (!started || internalChange) return
        if (dirty) {
            cancelRegionJob();analysisTimer.stop()
            cacheDirty = true
            buildTimer.stop()
            buildSegment = null
            analysisDirty = true
            regionBuilder = null
            unplacedMarkers=[];unplacedIndex=({});placementNotice=""
            if (importedRecords.length || importedRegions.length) {importedRecords=[];importedRegions=[]; noticeText="乐谱已变更，已退出导入结果；请重新分析。"}
        }
        if (surfaceActive) refreshTimer.restart()
    }
    function refreshSelection() {
        changeScore()
        if (!curScore || !surfaceActive) return
        if (lastPlaying && followPlayback.checked) return
        var location = selectedLocation()
        if (!location) location={tick:observer && observer.playing ? Math.max(0,observer.tick) : Math.max(0,currentTick),track:selectionTrack>=0?selectionTrack:0}
        selectionTrack = location.track
        var oldFirst = firstTrack, oldEnd = endTrack
        setScope()
        if (oldFirst !== firstTrack || oldEnd !== endTrack) {
            cancelRegionJob();analysisTimer.stop()
            saveCorrections();manualOverrides=[];manualPositions=[];scoreRangeEditing=false;editUndo=[];editRedo=[];loadedFingerprint="";manualNeedsReview=false
            analysisRegions=[];automaticRegions=[];regionBuilder=null;cacheDirty=true;analysisDirty=true;importedRecords=[];importedRegions=[];annotationTick=-1
            unplacedMarkers=[];unplacedIndex=({});placementNotice=""
            if(advancedNative)observer.setScorePreview([])
        }
        if(annotationSelectionKey.length && annotationSelectionKey===location.tick+":"+location.track && annotationTick>=0)location.tick=annotationTick
        else {annotationTick=-1;annotationSelectionKey=""}
        currentTick = location.tick
        if (observer) {cacheDirty = false; displayTick(location.tick, true)}
        else if (cacheDirty) beginBuild()
        else displayTick(location.tick)
    }
    function beginBuild() {
        restoreColors()
        buildTimer.stop()
        var tracks = []
        for (var t = firstTrack; t < endTrack; ++t) tracks.push([])
        buildTimeline = tracks
        buildSegment = curScore.firstSegment()
        matchText = "正在读取和声…"
        buildTimer.start()
    }
    function buildChunk() {
        if (!same(curScore, ownerScore) || !isOpen(ownerScore)) {
            buildTimer.stop()
            requestRefresh(true)
            return
        }
        // Yield to notation/playback/UI every 64 segments. Store numbers, not QObject wrappers.
        var segment = buildSegment, count = 0
        while (segment && count++ < 64) {
            if (segment.segmentType === Segment.ChordRest) {
                for (var t = firstTrack; t < endTrack; ++t) {
                    var el = segment.elementAt(t)
                    if (!el || (el.type !== Element.CHORD && el.type !== Element.REST)) continue
                    var duration = el.actualDuration.ticks, notes = []
                    if (el.type === Element.CHORD) {
                        var nativeNotes = el.notes
                        for (var n = 0; n < nativeNotes.length; ++n) {
                            var note = nativeNotes[n]
                            notes.push({tick:segment.tick, track:t, index:n, pitch:note.pitch, tpc:note.tpc})
                        }
                    }
                    buildTimeline[t - firstTrack].push({tick:segment.tick, end:segment.tick + duration, notes:notes})
                }
            }
            segment = segment.next
        }
        buildSegment = segment
        if (!segment) {
            buildTimer.stop()
            timeline = buildTimeline
            buildTimeline = []
            cacheDirty = false
            displayTick(currentTick, true)
        }
    }
    function initializeKey(tick) {
        if (manualKey || !curScore) return
        var ks
        if (observer) ks = nativeSnapshot.keySignature
        else {
            var cursor = curScore.newCursor()
            cursor.track = selectionTrack
            cursor.rewindToTick(tick)
            ks = cursor.keySignature
        }
        if (typeof ks !== "number") return
        keyAccidentals = ks
        var major = ["C♭","G♭","D♭","A♭","E♭","B♭","F","C","G","D","A","E","B","F♯","C♯"]
        var minor = ["A♭","E♭","B♭","F","C","G","D","A","E","B","F♯","C♯","G♯","D♯","A♯"]
        var name = (keyMode.currentIndex === 0 ? major : minor)[ks + 7]
        // The root menu represents C♭ enharmonically as B; the signature is still read correctly.
        if (name === "C♭") name = "B"
        var index = Harmony.rootNames.indexOf(name)
        if (index >= 0) keyTonic.currentIndex = index
    }
    function displayTick(tick, force) {
        if ((!observer && cacheDirty) || tick < 0) return
        var notes = [], pitches = []
        if (observer) {
            nativeSnapshot = advancedNative ? observer.contextSnapshot(tick, firstTrack, endTrack,
                pedalContext.checked, windowTicks(), lastPlaying && followPlayback.checked) :
                observer.snapshot(tick, firstTrack, endTrack, lastPlaying && followPlayback.checked)
            notes = nativeSnapshot.notes || []
        } else {
            for (var t = 0; t < timeline.length; ++t) {
                var event = Harmony.atTick(timeline[t], tick)
                if (event) for (var n = 0; n < event.notes.length; ++n) notes.push(event.notes[n])
            }
        }
        var detectedNotes = observer && nativeSnapshot.analysisNotes ? nativeSnapshot.analysisNotes : notes
        var contextChanged = JSON.stringify(detectedNotes) !== JSON.stringify(contextNotes)
        contextNotes = detectedNotes
        notes.sort(function(a,b) {return a.pitch - b.pitch || a.track - b.track})
        for (var i = 0; i < notes.length; ++i) pitches.push(notes[i].pitch)
        var changed = JSON.stringify(notes) !== JSON.stringify(currentNotes)
        currentTick = tick
        if(fixedChordNative)observer.setActiveScorePreview(highlightedTick())
        positionText = lastPlaying && followPlayback.checked ? "正在播放 · 包含持续音" : "当前位置 · 包含持续音"
        if (observer && nativeSnapshot.bar !== undefined)
            positionText = (lastPlaying && followPlayback.checked ? "播放" : "当前位置") +
                    " · 第 " + nativeSnapshot.bar + " 小节 · 第 " + nativeSnapshot.beat + " 拍"
        var previousKey = keyTonic.currentIndex
        initializeKey(tick)
        if (!changed && !contextChanged && !force) {
            if (previousKey !== keyTonic.currentIndex) updateTexts()
            return
        }
        currentNotes = notes
        currentPitches = pitches
        analyze()
    }
    function analyze() {
        alternativesText = ""
        if (!contextNotes.length && !currentNotes.length) {
            chordRoot = -1
            chordDefinition = -1
            matchText = currentTick < 0 ? "请选择音符、和弦或休止符" : "空拍 · 没有持续音"
        } else if (autoChord.checked) {
            var contextPitches = contextNotes.map(function(n){return n.pitch})
            var result = Harmony.detect(contextPitches.length ? contextPitches : currentPitches)
            var imported = Analysis.atTick(importedRecords, currentTick)
            if (imported) result = {root:imported.root,definition:imported.definition,kind:"已导入的分析",alternatives:[]}
            else if (contextPitches.length > currentPitches.length) result.kind += " · 踏板 / 琶音上下文"
            chordRoot = result.root
            chordDefinition = result.definition
            matchText = result.kind
            if (chordRoot >= 0) {
                rootCombo.currentIndex = rootIndex(chordRoot)
                qualityCombo.currentIndex = chordDefinition
                var names = []
                for (var a = 0; a < result.alternatives.length; ++a) {
                    var alt = result.alternatives[a]
                    names.push(nameOf(alt.root) + Harmony.defs[alt.definition].suffix)
                }
                if (names.length) alternativesText = "也可能是 " + names.join(" / ")
                if(contextPitches.length>currentPitches.length) {
                    var instant=Harmony.detect(currentPitches)
                    if(instant.root>=0 && (instant.root!==chordRoot || instant.definition!==chordDefinition))
                        alternativesText += (alternativesText.length?" · ":"")+"即时音："+nameOf(instant.root)+Harmony.defs[instant.definition].suffix
                }
            }
        } else {
            chordRoot = Harmony.rootPcs[rootCombo.currentIndex]
            chordDefinition = qualityCombo.currentIndex
            matchText = "手动指定"
        }
        liveRoot=chordRoot;liveDefinition=chordDefinition
        updateTexts()
        updatePianoRange()
        repaintNotes()
        scheduleAnalysis()
    }
    function updateTexts() {
        chordRoot=liveRoot;chordDefinition=liveDefinition
        chordText = chordRoot < 0 ? (currentPitches.length === 1 ? noteName(currentNotes[0]) : "—") :
                rootName() + Harmony.defs[chordDefinition].suffix
        degreeText = Harmony.roman(Harmony.rootPcs[keyTonic.currentIndex], keyMode.currentIndex === 1,
                                   chordRoot, chordDefinition,configuration.degreeMode,currentNotes.length?currentNotes[0].pitch:-1)
        var voicing = []
        for (var i = 0; i < currentNotes.length; ++i) voicing.push(noteName(currentNotes[i]))
        voicingText = voicing.length ? voicing.join(" · ") : "—"
        inversionText = "—"
        if (currentNotes.length && chordRoot >= 0) {
            var bass = currentNotes[0], label = Harmony.labelFor(bass.pitch, chordRoot, chordDefinition)
            inversionText = Harmony.inversion(label) + " · 低音 " + noteName(bass)
            if (Harmony.mod12(bass.pitch) !== chordRoot) chordText += "/" + Harmony.spelledName(bass.tpc, bass.pitch, prefersFlats())
        }
        instantChordText=chordText;instantDegreeText=degreeText
        if(configuration.detailMode===1 && ownedRegion) {
            chordRoot=ownedRegion.root;chordDefinition=ownedRegion.definition
            chordText=ownedRegion.chord;degreeText=ownedRegion.degree
            matchText="所属和弦 · "+(ownedRegion.source==="manual"?"人工修正":ownedRegion.kind||"已识别")
            inversionText=(ownedRegion.inversion||Harmony.inversion(Harmony.labelFor(ownedRegion.bass,chordRoot,chordDefinition)))+(ownedRegion.bass>=0?" · 低音 "+Harmony.pcName(ownedRegion.bass,prefersFlats()):"")
            var candidates=ownedRegion.alternatives||[]
            alternativesText=candidates.length?"也可能是 "+candidates.map(function(a){return nameOf(a.root)+Harmony.defs[a.definition].suffix}).join(" / "):""
            if(ownedRegion.harmonicBass!==undefined && ownedRegion.harmonicBass!==ownedRegion.bass)alternativesText+=(alternativesText.length?" · ":"")+"新伴奏低音 "+Harmony.pcName(ownedRegion.harmonicBass,prefersFlats())+"；较早低音仍在延续"
        }
        var roleNotes = configuration.detailMode===1 && ownedRegion ? ownedRegion.notes : contextNotes.length ? contextNotes : currentNotes
        toneRows = makeToneRows(chordRoot,chordDefinition,roleNotes)
    }
    function makeToneRows(toneRoot,toneDefinition,roleNotes) {
        var degrees = ["1","3","5","7","9","11","13"], rows = []
        for (var d = 0; d < degrees.length; ++d) {
            var degree = degrees[d], labels = [], played = [], expected = false
            if (toneDefinition >= 0) {
                var definition = Harmony.defs[toneDefinition]
                for (var k = 0; k < definition.labels.length; ++k)
                    if (Harmony.role(definition.labels[k]) === degree) {
                        expected = true
                        labels.push(definition.labels[k])
                    }
            }
            for (var i = 0; i < roleNotes.length; ++i) {
                var functionLabel = Harmony.labelFor(roleNotes[i].pitch, toneRoot, toneDefinition)
                if (Harmony.role(functionLabel) === degree) played.push(noteName(roleNotes[i]))
            }
            rows.push({degree:degree, label:labels.length ? labels.join(" / ") : degree,
                       notes:played.length ? played.join(" · ") : expected ? "缺音" : "—",
                       present:played.length > 0, expected:expected, color:configuration.colors[degree]})
        }
        return rows
    }
    function locatorKey(note) {return note.tick + ":" + note.track + ":" + note.index + ":" + note.pitch + ":" + note.tpc}
    function resolveNote(score, location) {
        // A fresh wrapper ensures deletion, undo and score switches cannot dereference a cached note.
        var cursor = score.newCursor()
        cursor.track = location.track
        cursor.rewindToTick(location.tick)
        if (!cursor.segment || cursor.segment.tick !== location.tick) return null
        var chord = cursor.segment.elementAt(location.track)
        if (!chord || chord.type !== Element.CHORD) return null
        var notes = chord.notes
        if (location.index >= notes.length) return null
        var note = notes[location.index]
        return note.pitch === location.pitch && note.tpc === location.tpc ? note : null
    }
    function colorEqual(a,b) {return String(a).toLowerCase() === String(b).toLowerCase()}
    function syncColors(enabled, all) {
        var score = ownerScore
        if (!isOpen(score) || internalChange || undoPause) return
        var wanted = {}, writes = [], ledger = colorLedger.slice(0), nextActive = {}
        if (enabled && chordRoot >= 0)
            for (var i = 0; i < currentNotes.length; ++i)
                wanted[locatorKey(currentNotes[i])] = {location:currentNotes[i], color:colorFor(currentNotes[i].pitch, chordRoot, chordDefinition)}
        for (i = 0; i < ledger.length; ++i) {
            var record = ledger[i]
            if (!all && !activeColorKeys[record.key] && !wanted[record.key]) continue
            var note = resolveNote(score, record.location), desired = wanted[record.key]
            delete wanted[record.key]
            if (!note) continue
            var actual = String(note.color)
            // Respect subsequent user or other-plugin color edits.
            if (!colorEqual(actual, record.applied) && !colorEqual(actual, record.original)) {
                record.original = actual
                record.applied = actual
                continue
            }
            var target = desired ? desired.color : record.original
            if (!colorEqual(actual, target)) writes.push({note:note,color:target})
            if (desired) {record.applied = desired.color; nextActive[record.key] = true}
            // Keep past entries while the dock is alive: undo can resurrect an earlier preview color.
        }
        for (var key in wanted) {
            var value = wanted[key], fresh = resolveNote(score, value.location)
            if (!fresh) continue
            ledger.push({key:key,location:value.location,original:String(fresh.color),applied:value.color})
            nextActive[key] = true
            if (!colorEqual(fresh.color,value.color)) writes.push({note:fresh,color:value.color})
        }
        colorLedger = ledger
        activeColorKeys = nextActive
        if (!writes.length) return
        internalChange = true
        var commandStarted = false
        try {
            score.startCmd()
            commandStarted = true
            for (i = 0; i < writes.length; ++i) writes[i].note.color = writes[i].color
        } finally {
            try {if (commandStarted) score.endCmd()} finally {internalChange = false}
        }
    }
    function repaintNotes() {
        if (observer) {
            var descriptors = []
            if (chordRoot >= 0) {
                var notes = currentNotes.length ? currentNotes : contextNotes
                var chordAnchor=0
                for(var a=1;a<notes.length;++a)if(notes[a].tick>notes[chordAnchor].tick)chordAnchor=a
                for (var i=0;i<notes.length;++i) {
                    var d=JSON.parse(JSON.stringify(notes[i]))
                    if(scoreColoring.checked)d.color=colorFor(d.pitch,chordRoot,chordDefinition)
                    if(advancedNative && showFunctions.checked)d.label=functionText(d.pitch,chordRoot,chordDefinition)
                    if(advancedNative && !fixedChordNative && showChords.checked && i===chordAnchor)d.chord=chordText+" · "+degreeText
                    if(advancedNative)d.active=lastPlaying && followPlayback.checked
                    if(d.color || d.label || d.chord)descriptors.push(d)
                }
            }
            observer.setNotePreviewColors(descriptors)
            if(fixedChordNative)observer.setActiveScorePreview(highlightedTick())
        } else syncColors(scoreColoring.checked && !lastPlaying)
    }
    function restoreColors(all) {
        if (observer) observer.clearNotePreviewColors()
        else syncColors(false, all)
    }
    function stopColoring() {
        undoPause = false
        scoreColoring.checked = false
        restoreColors(true)
        if(advancedNative)observer.clearAllPreviews()
        applyScorePreview()
        savePreferences()
    }
    function updatePianoRange() {
        if (!currentPitches.length) {pianoStart = 48; pianoOctaves = 3; return}
        var low = currentPitches[0], high = currentPitches[currentPitches.length - 1]
        pianoStart = Math.floor(low / 12) * 12
        pianoOctaves = Math.max(3, Math.ceil((high - pianoStart + 1) / 12))
    }
    function midiActive(midi) {return currentPitches.indexOf(midi) >= 0}
    function pianoColor(midi, black) {
        return midiActive(midi) ? colorFor(midi, chordRoot, chordDefinition) : black ? "#28333D" : "#FFFFFF"
    }
    function whiteMidi(index) {return pianoStart + Math.floor(index / 7) * 12 + [0,2,4,5,7,9,11][index % 7]}
    function blackMidi(index) {return pianoStart + Math.floor(index / 5) * 12 + [1,3,6,8,10][index % 5]}
    function blackX(index, w) {return (Math.floor(index / 5) * 7 + [0,1,3,4,5][index % 5] + 1) * w - w * 0.31}
    function pollHost() {
        if (!same(curScore,ownerScore)) {requestRefresh(true); return}
        if (observer) return // Native position events replace playback polling.
        if (!playbackAvailable || !curScore) return
        var playing = curScore.harmonyPlaybackActive
        if (playing !== lastPlaying) {
            lastPlaying = playing
            if (playing) restoreColors()
            else {lastPlayTick = -1; requestRefresh(false)}
        }
        if (playing && followPlayback.checked) {
            var tick = curScore.harmonyPlaybackTick
            if (tick !== lastPlayTick) {
                lastPlayTick = tick
                if (cacheDirty && !buildTimer.running) {setScope(); currentTick = tick; beginBuild()}
                else if (!cacheDirty) displayTick(tick)
                else currentTick = tick
            }
        }
    }
    function followNativePosition() {
        if (!observer || !started || !surfaceActive) return
        if (!same(curScore,ownerScore)) changeScore()
        var playing = observer.playing
        if (playing !== lastPlaying) {
            lastPlaying = playing
            if(playing && analysisRecords.length){cancelRegionJob();analysisTimer.stop();regionBuilder=null}
            if (!playing) {lastPlayTick = -1; requestRefresh(false); return}
        }
        if (playing && followPlayback.checked) {
            var tick = observer.tick
            setScope()
            cacheDirty = false
            lastPlayTick = tick
            displayTick(tick)
        }
    }
    onRun: {
        started = true
        if (typeof root.newScoreObserver === "function") {
            observer = root.newScoreObserver()
            if (observer) scoreColoring.checked = true
            changeScore()
            if (advancedNative) {
                var saved=observer.loadConfiguration("HarmonyAssistant")
                if(saved.schema)applyPreferences(saved)
            } else settingsStore.active=true
        }
        if(observer && typeof observer.startReadOnlyJob==="function" && typeof observer.readTextFile==="function") {
            try {regionWorkerSource=Timeline.workerBundle({harmony:observer.readTextFile(String(Qt.resolvedUrl("Harmony.js"))),timeline:observer.readTextFile(String(Qt.resolvedUrl("Timeline.js")))})}
            catch(error){noticeText="后台和弦计算未启用："+error;regionWorkerSource=""}
        }
        syncDetailPanel()
        requestRefresh(true)
    }
    onScoreStateChanged: {
        if (!started || internalChange) return
        if (state.undoRedo && !observer) {
            // Never change the score from an undo/redo callback. Keep the inspector read-only.
            undoPause = true
            scoreColoring.checked = false
            noticeText = "撤销 / 重做后已暂停谱面配色；需要时可点恢复原色。"
        }
        var dirty = state.undoRedo || state.excerptsChanged || state.instrumentsChanged ||
                (typeof state.startLayoutTick === "number" && state.startLayoutTick >= 0)
        if (dirty && observer) observer.clearNotePreviewColors()
        requestRefresh(dirty)
    }
    onVisibleChanged: {
        if (!started) return
        syncAnalysisSurface()
        if (!surfaceActive) {buildTimer.stop(); restoreColors();markerWindow.hide()}
        else requestRefresh(false)
    }
    onSurfaceActiveChanged: {
        if (!started) return
        if (!surfaceActive) {cancelRegionJob();refreshTimer.stop(); playbackTimer.stop(); buildTimer.stop(); analysisTimer.stop();markerWindow.hide()}
        else if(advancedNative) {analysisDirty=true; scheduleAnalysis()}
        if (surfaceActive) {followNativePosition(); requestRefresh(false)}
    }
    onRibbonChanged:{syncDetailPanel();resizeSummary(false)}
    onDualSummaryActiveChanged:{syncAnalysisSurface();resizeSummary(false)}
    onDualDetailActiveChanged:syncAnalysisSurface()
    Component.onDestruction: {cancelRegionJob();if (started) {savePreferences();saveCorrections(); undoPause = false; restoreColors(true); if(advancedNative)observer.clearAllPreviews()}}
    Timer {id:configurationTimer; interval:350; onTriggered:savePreferences()}
    Timer {id:analysisTimer; interval:12; onTriggered:buildAnalysisChunk()}
    Loader {id:settingsStore; active:false; source:"SettingsStore.qml"; onLoaded:{if(item.payload.length){try{applyPreferences(JSON.parse(item.payload))}catch(error){noticeText="配置未读取："+error}}}}
    Window {
        id:markerWindow;objectName:"harmonyMarkerWindow"
        width:360;height:430;minimumWidth:260;minimumHeight:220
        visible:false;flags:Qt.Tool;title:"和声助手 · 识别点";color:theme.background
        MarkerList {
            anchors.fill:parent;anchors.margins:10
            regions:markerWindow.visible ? root.analysisRegions.filter(function(r){return r.part===root.focusedPart}) : []
            unplaced:root.unplacedIndex;tick:root.currentTick
            onActivated:root.openAnnotation(tick,part)
        }
    }
    FileDialog {
        id:exchangeDialog
        title:pendingFileAction.indexOf("import")===0 ? "导入 JSON" : "导出和声信息"
        selectExisting:pendingFileAction.indexOf("import")===0
        nameFilters:pendingFileAction==="csv" ? ["CSV (*.csv)"] : ["JSON (*.json)"]
        onAccepted:exchangeFile(String(fileUrl))
    }
    Window {
        id:detailPopup; width:760; height:650; visible:false
        title:"和声助手 · 详细面板"; flags:Qt.Tool
        property bool opened:visible
        property bool placed:false
        function open(){root.placeWindow(detailPopup,ribbonMenuWindow.anchorItem);visible=true;requestActivate()}
    }
    Window {
        id:ribbonMenuWindow;objectName:"harmonyRibbonMenuWindow";width:230;height:menuColumn.implicitHeight+16
        visible:false;flags:Qt.Popup|Qt.FramelessWindowHint;color:theme.background
        property bool positionOnly:true
        property var anchorItem:null
        property var area:({})
        function clamp(){if(!visible||!area.width)return;x=Math.max(area.x,Math.min(x,area.x+area.width-width));y=Math.max(area.y,Math.min(y,area.y+area.height-height))}
        onHeightChanged:clamp()
        function openAt(item,position){anchorItem=item;positionOnly=position;area=typeof root.panelScreen==="function"?root.panelScreen(item):{};var point=item.mapToGlobal(0,item.height+3);x=point.x;y=point.y;show();clamp();raise();requestActivate()}

        Column {
            id:menuColumn;x:8;y:8;width:parent.width-16;spacing:4
            Column {
                width:parent.width;visible:ribbonMenuWindow.positionOnly;spacing:4
                Repeater {model:["靠左","居中","靠右","自定位置"];delegate:DeskButton {
                    objectName:"harmonyRibbonAlign"+index;width:parent.width;text:modelData;checked:configuration.ribbonAlign===index
                    onClicked:{root.setRibbonPosition(index);ribbonMenuWindow.hide()}
                }}
                Slider {width:parent.width;visible:configuration.ribbonAlign===3;from:0;to:100;value:configuration.ribbonPosition
                    onMoved:{var c=JSON.parse(JSON.stringify(configuration));c.ribbonPosition=Math.round(value);root.configuration=Preferences.clean(c);configurationTimer.restart()}}
            }
            Column {
                width:parent.width;visible:!ribbonMenuWindow.positionOnly;spacing:4
                DeskButton {objectName:"harmonyRibbonMarkerList";width:parent.width;text:root.unplacedMarkers.length?"记号列表（有待检查项）":"记号列表";enabled:root.advancedNative;onClicked:{ribbonMenuWindow.hide();markerWindow.show();markerWindow.raise();markerWindow.requestActivate()}}
                DeskButton {width:parent.width;text:"隐藏顶部摘要";onClicked:{ribbonMenuWindow.hide();if(root.dualSummaryActive)root.toggleDetailPanel();else if(typeof root.hidePanel==="function")root.hidePanel()}}
                DeskButton {objectName:"harmonyRibbonAppearance";width:parent.width;text:"摘要外观与调性";onClicked:{ribbonMenuWindow.hide();root.openSummarySettings(ribbonMenuWindow.anchorItem)}}
                DeskButton {width:parent.width;text:"详细窗口";onClicked:{ribbonMenuWindow.hide();root.openDetailWindow()}}
                DeskButton {width:parent.width;text:(root.dualSummaryActive && typeof root.summaryPanelFloating==="boolean" ? root.summaryPanelFloating : root.panelFloating)?"停靠这个面板":"悬浮这个面板";enabled:typeof root.setPanelFloating==="function";onClicked:{ribbonMenuWindow.hide();(root.dualSummaryActive && typeof root.setSummaryPanelFloating==="function")?root.setSummaryPanelFloating(!root.summaryPanelFloating):root.setPanelFloating(!root.panelFloating)}}
            }
        }
    }
    Window {
        id:summarySettings;objectName:"harmonySummarySettings";title:"和弦摘要外观与调性";width:360;height:400;minimumWidth:320;flags:Qt.Tool;color:theme.background
        property bool placed:false
        ScrollView {anchors.fill:parent;anchors.margins:12;clip:true
            Column {width:summarySettings.width-32;spacing:10
                HarmonyLabel {observer:root.observer;chord:root.summaryChordText;degree:root.summaryDegreeText;color:root.summaryInk;pixels:configuration.summaryPixels;degreeRatio:configuration.summaryDegreeRatio/100;gap:configuration.summaryGap;order:configuration.summaryOrder;degreeFont:configuration.degreeFont;chordFont:["","Edwin","Arial"][configuration.chordFont];revision:root.analysisFingerprint}
                RowLayout {width:parent.width
                    UiLabel {text:"主音与调式";color:ink}
                    DeskComboBox {Layout.fillWidth:true;model:Harmony.rootNames;currentIndex:keyTonic.currentIndex;onActivated:{keyTonic.currentIndex=currentIndex;manualKey=true;root.updateTexts();root.optionsChanged()}}
                    DeskComboBox {model:["大调","小调"];currentIndex:keyMode.currentIndex;onActivated:{keyMode.currentIndex=currentIndex;root.updateTexts();root.optionsChanged()}}
                }
                Repeater {model:[{key:"summaryPixels",title:"字号",min:12,max:32},{key:"summaryDegreeRatio",title:"级数相对大小（%）",min:60,max:140},{key:"summaryGap",title:"和弦与级数间距",min:0,max:20}]
                    RowLayout {width:parent.width
                        UiLabel {text:modelData.title;color:ink;Layout.fillWidth:true}
                        DeskSpinBox {from:modelData.min;to:modelData.max;value:configuration[modelData.key];onValueModified:root.changeSummary(modelData.key,value)}
                    }
                }
                DeskComboBox {width:parent.width;model:["和弦在左，级数在右","级数在左，和弦在右","和弦在上，级数在下","级数在上，和弦在下"];currentIndex:configuration.summaryOrder;onActivated:root.changeSummary("summaryOrder",currentIndex)}
                UiLabel {width:parent.width;wrapMode:Text.Wrap;text:"摘要使用乐谱中的和弦字体和专用音乐符号。上下排列时，可拖动面板下边缘增加高度。";color:muted;font.pixelSize:11}
                DeskButton {text:"恢复摘要默认外观";onClicked:{root.changeSummary("summaryPixels",20);root.changeSummary("summaryDegreeRatio",100);root.changeSummary("summaryGap",6);root.changeSummary("summaryOrder",0);root.resizeSummary(true)}}
                DeskButton {text:"关闭";onClicked:summarySettings.hide()}
            }
        }
    }
    Timer {id:refreshTimer; interval:55; onTriggered:refreshSelection()}
    Timer {id:buildTimer; interval:10; repeat:true; onTriggered:buildChunk()}
    Timer {id:playbackTimer; interval:16; onTriggered:followNativePosition()}
    Timer {interval:!observer && playbackAvailable ? 80 : 400; running:started && surfaceActive; repeat:true; onTriggered:pollHost()}
    Connections {
        target:observer
        ignoreUnknownSignals:true
        onPositionChanged:{if(!playbackTimer.running)playbackTimer.start()}
        onScoreChanged:requestRefresh(true)
        onActiveScoreChanged:requestRefresh(true)
        onPreviewActivated:root.openAnnotation(tick,track)
        onPreviewRangeEdited:root.editRange("range",start,end,0,0,0)
        onPreviewPositionEdited:root.moveAnnotation(tick,track,x,y)
        onReadOnlyJobFinished: {
            if(token!==root.regionJobToken)return
            root.regionJobToken=0
            if(!root.surfaceActive || !curScore || firstTrack!==root.regionFirstTrack || endTrack!==root.regionEndTrack || observer.analysisRevision()!==root.regionRevision) {root.analysisDirty=true;root.scheduleAnalysis();return}
            if(result.error){root.analysisDirty=true;root.matchText="分析未完成";root.noticeText="和弦计算未完成："+result.error;return}
            root.finishRegions(result.regions||[])
        }
    }
    Connections {target:root; ignoreUnknownSignals:true; onPanelDetailClosed:root.detailPanelClosed();onPanelSummaryClosed:root.detailPanelClosed()}

    Rectangle {
        id:backdrop
        anchors.fill: parent
        color: theme.background
        Component {id:harmonyDetails
Flickable {
            id: flick
            function scrollRange(){contentY=Math.max(0,rangeCard.y-6)}
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: panel.height + 16
            boundsBehavior: Flickable.StopAtBounds
            visible:true
            ScrollBar.vertical: ScrollBar { }
            Flow {
                id: panel
                x: 8
                y: 8
                width: Math.max(160, flick.width - 16)
                spacing: 6
                Column {
                    width: parent.width
                    spacing: 3
                    RowLayout {
                        width: parent.width
                        UiLabel {text:"和声助手"; Layout.fillWidth:true; color:ink; font.pixelSize:14; font.bold:true}
                        DeskButton {
                            objectName:"harmonyPointButton";flat:true;text:root.unplacedMarkers.length ? "记号 !" : "记号"
                            implicitWidth:48;enabled:root.advancedNative
                            onClicked:{markerWindow.show();markerWindow.raise();markerWindow.requestActivate()}
                            ToolTip.visible:hovered;ToolTip.text:root.unplacedMarkers.length ? root.unplacedMarkers.length+" 个待排记号；查看全部识别点" : "查看全部识别点与所属区间"
                        }
                        DeskButton {flat:true;
                            text:typeof root.panelFloating === "boolean" && root.panelFloating ? "停靠" : "悬浮"
                            visible:typeof root.setPanelFloating === "function"
                            font.pixelSize:11
                            onClicked:root.setPanelFloating(!root.panelFloating)
                        }
                        DeskButton {flat:true;
                            font.family: theme.fontFamily;text:"键盘"; checked:keyboardExpanded; checkable:true; font.pixelSize:11; onClicked:{keyboardExpanded=checked; configurationTimer.restart()}}
                        DeskButton {flat:true;
                            font.family: theme.fontFamily;text:"设置"; checked:settingsExpanded; checkable:true; font.pixelSize:11; onClicked:{settingsExpanded=checked; configurationTimer.restart()}}
                    }
                    UiLabel {width:parent.width; text:Harmony.rootNames[keyTonic.currentIndex]+(keyMode.currentIndex===0?" 大调":" 小调")+" · "+scopeText; color:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                }
                PanelCard {
                    id:summaryCard; objectName:"harmonySummary"
                    width: root.expanded ? (panel.width-10)/2 : panel.width
                    minimumBodyHeight:settingsExpanded ? 190 : 156
                    StableLabel {text:positionText; color:muted; font.pixelSize:11}
                    HarmonyLabel {width:Math.min(parent.width,implicitWidth);height:implicitHeight;observer:root.observer;chord:root.chordText;color:root.chordInk;pixels:24;chordFont:["","Edwin","Arial"][configuration.chordFont];revision:root.analysisFingerprint}
                    RowLayout {
                        width: parent.width
                        UiLabel {text:"级数"; color:muted; font.pixelSize:12}
                        HarmonyLabel {Layout.preferredWidth:implicitWidth;Layout.preferredHeight:implicitHeight;observer:root.observer;degree:root.degreeText;color:root.chordInk;pixels:24;degreeFont:configuration.degreeFont;revision:root.analysisFingerprint}
                    }
                    StableLabel {reservedLines:2; text:matchText+(configuration.chromaticAccent && chromaticChord?" · 含调外音":""); color:muted; font.pixelSize:12}
                    StableLabel {reservedLines:2; text:alternativesText; color:muted; font.pixelSize:11}
                    Rectangle {width:parent.width; height:1; color:theme.subtleLine}
                    StableLabel {text:inversionText; color:muted; font.pixelSize:11}
                    StableLabel {visible:settingsExpanded; reservedLines:2; text:voicingText; color:ink; font.pixelSize:12}
                }
                PanelCard {
                    heading:"音程功能";collapsed:configuration.collapsedSections.indexOf("tones")>=0;onToggleRequested:root.toggleSection("tones")
                    id:tonesCard; objectName:"harmonyFunctions"
                    width: root.expanded ? (panel.width-10)/2 : panel.width
                    minimumBodyHeight:body.width>=290 ? 224 : 292

                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: toneRows
                            delegate: Rectangle {
                                width: Math.max(64, (parent.width - (parent.width >= 290 ? 18 : 12)) / (parent.width >= 290 ? 4 : 3))
                                height:64
                                radius: 1
                                color: modelData.present ? theme.section : theme.field
                                border.color:theme.subtleLine
                                Rectangle {width:parent.width-12; height:2; x:6; y:parent.height-3; color:modelData.present?modelData.color:theme.subtleLine; radius:1}
                                Column {
                                    id:toneBody
                                    x:6; y:6; width:parent.width-12; spacing:4
                                    StableLabel {text:modelData.label; color:ink; font.pixelSize:14; font.bold:true}
                                    StableLabel {reservedLines:2; text:modelData.notes; color:modelData.present?ink:muted; font.pixelSize:11}
                                }
                            }
                        }
                    }
                    UiLabel {
                        width:parent.width; color:muted; font.pixelSize:10; wrapMode:Text.Wrap
                        text:"色条：识别音（含开启的上下文） · 缺音：未奏出 · —：未使用\n挂音显示 2 / 4，六和弦显示 6。"
                    }
                    Flow {
                        width:parent.width; height:57; clip:true; spacing:5
                        Repeater {
                            model:currentNotes
                            delegate:Rectangle {
                                property string functionLabel:Harmony.labelFor(modelData.pitch,chordRoot,chordDefinition)
                                width:noteChip.implicitWidth+16; height:26; radius:1
                                color:theme.background; border.color:theme.subtleLine
                                Rectangle {x:0;y:6;width:3;height:14;color:root.colorFor(modelData.pitch,chordRoot,chordDefinition)}
                                UiLabel {id:noteChip; anchors.centerIn:parent; text:noteName(modelData)+" · "+root.functionText(modelData.pitch,chordRoot,chordDefinition); color:ink; font.pixelSize:11}
                            }
                        }
                    }
                }
                PanelCard {
                    objectName:"harmonyKeyboard"
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:keyboardExpanded
                    RowLayout {
                        width:parent.width
                        UiLabel {text:"实际音高"; Layout.fillWidth:true; color:ink; font.bold:true; font.pixelSize:13}
                        UiLabel {text:pianoOctaves+" 个八度"; color:muted; font.pixelSize:11}
                    }
                    Flickable {
                        id:pianoScroll
                        width:parent.width; height:112; clip:true
                        contentWidth:Math.max(width,pianoOctaves*7*15); contentHeight:94
                        boundsBehavior:Flickable.StopAtBounds
                        ScrollBar.horizontal:ScrollBar { }
                        Item {
                            id:piano
                            width:pianoScroll.contentWidth; height:94
                            property real whiteW:width/(pianoOctaves*7)
                            Repeater {
                                model:pianoOctaves*7
                                delegate:Rectangle {
                                    property int midi:whiteMidi(index)
                                    x:index*piano.whiteW; width:piano.whiteW; height:94
                                    color:pianoColor(midi,false); border.color:"#BBC3CC"; border.width:0.7
                                    UiLabel {
                                        anchors.horizontalCenter:parent.horizontalCenter
                                        anchors.bottom:parent.bottom; anchors.bottomMargin:5
                                        text:Harmony.mod12(parent.midi)===0 ? "C"+(Math.floor(parent.midi/12)-1) : ""
                                        font.pixelSize:9; color:midiActive(parent.midi)?"white":muted
                                    }
                                }
                            }
                            Repeater {
                                model:pianoOctaves*5
                                delegate:Rectangle {
                                    property int midi:blackMidi(index)
                                    x:blackX(index,piano.whiteW); width:piano.whiteW*0.62; height:58
                                    z:2; color:pianoColor(midi,true); border.color:"#28333D"; border.width:0.7
                                }
                            }
                        }
                    }
                }
                PanelCard {
                    heading:"识别与调性";collapsed:configuration.collapsedSections.indexOf("key")>=0;onToggleRequested:root.toggleSection("key")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:settingsExpanded

                    RowLayout {
                        width:parent.width
                        UiLabel {text:"调性"; color:muted; font.pixelSize:12}
                        DeskComboBox {
                            font.family: theme.fontFamily;
                            id:keyTonicControl;currentIndex:root.keyTonic.currentIndex;onCurrentIndexChanged:if(currentIndex>=0)root.keyTonic.currentIndex=currentIndex; Layout.fillWidth:true; Layout.minimumWidth:55; implicitHeight:28
                            model:Harmony.rootNames; font.pixelSize:12
                            onActivated:{manualKey=true; updateTexts(); optionsChanged()}
                        }
                        DeskComboBox {
                            font.family: theme.fontFamily;
                            id:keyModeControl;currentIndex:root.keyMode.currentIndex;onCurrentIndexChanged:if(currentIndex>=0)root.keyMode.currentIndex=currentIndex; Layout.preferredWidth:80; implicitHeight:28
                            model:["大调","小调"]; font.pixelSize:12
                            onActivated:{if(!manualKey && currentTick>=0) initializeKey(currentTick); updateTexts(); optionsChanged()}
                        }
                    }
                    UiLabel {width:parent.width; text:manualKey?"调性由你指定":"主音取自当前位置调号；大 / 小调请自行确认"; color:muted; font.pixelSize:10; wrapMode:Text.Wrap}
                    RowLayout {
                        width:parent.width
                        DeskSwitch {
                            font.family: theme.fontFamily;id:autoChordControl; text:"自动识别"; checked:root.autoChord.checked; font.pixelSize:12; onToggled:{root.autoChord.checked=checked;analyze(); optionsChanged()}}
                        Item {Layout.fillWidth:true}
                        DeskButton {
                            font.family: theme.fontFamily;text:"读调号"; implicitHeight:28; font.pixelSize:11; onClicked:{manualKey=false; if(currentTick>=0)initializeKey(currentTick); updateTexts(); optionsChanged()}}
                    }
                    RowLayout {width:parent.width
                        UiLabel {text:"踏板内识别";color:muted;font.pixelSize:11}
                        DeskComboBox {model:["单个和弦","多个和弦"];currentIndex:configuration.pedalMode;Layout.fillWidth:true;onActivated:{var c=Timeline.clone(configuration);c.pedalMode=currentIndex;configuration=Preferences.clean(c);optionsChanged()}}
                    }
                    RowLayout {width:parent.width
                        UiLabel {text:"级数参照";color:muted;font.pixelSize:11}
                        DeskComboBox {model:["当前调式","同名大调"];currentIndex:configuration.degreeMode;Layout.fillWidth:true;onActivated:{var c=Timeline.clone(configuration);c.degreeMode=currentIndex;configuration=Preferences.clean(c);rebuildRegions();updateTexts();optionsChanged()}}
                    }
                    UiLabel {width:parent.width;text:keyMode.currentIndex===1 ? (configuration.degreeMode===0?"小调第三、六、七级记为 III / VI / VII；升高导音记为 ♯VII。":"小调第三、六、七级相对同名大调记为 ♭III / ♭VI / ♭VII。") : "大调两种参照相同。";color:muted;font.pixelSize:10;wrapMode:Text.Wrap}
                    DeskSwitch {id:pedalContextControl; text:"踏板保持"; checked:root.pedalContext.checked; enabled:advancedNative; onToggled:{root.pedalContext.checked=checked;optionsChanged()}}
                    RowLayout {
                        width:parent.width
                        UiLabel {text:"无踏板聚合"; color:muted; font.pixelSize:11}
                        DeskComboBox {id:arpeggioWindowControl;currentIndex:root.arpeggioWindow.currentIndex;onCurrentIndexChanged:if(currentIndex>=0)root.arpeggioWindow.currentIndex=currentIndex; model:["即时","半拍","1 拍","2 拍","4 拍"]; Layout.fillWidth:true; implicitHeight:28; onActivated:optionsChanged()}
                    }
                    UiLabel {width:parent.width; text:"聚合限当前小节，休止截断。快速转和弦时建议即时或半拍；踏板保持不等同于声学残响。"; color:muted; font.pixelSize:10; wrapMode:Text.Wrap; visible:settingsExpanded}
                    DeskComboBox {
                            font.family: theme.fontFamily;
                        id:scopeControl;currentIndex:root.scope.currentIndex;onCurrentIndexChanged:if(currentIndex>=0)root.scope.currentIndex=currentIndex; width:parent.width; implicitHeight:28; font.pixelSize:12
                        model:["当前乐器（钢琴双谱表）","全谱"]
                        onActivated:{requestRefresh(true); optionsChanged()}
                    }
                    RowLayout {
                        width:parent.width
                        DeskComboBox {
                            font.family: theme.fontFamily;id:rootComboControl;currentIndex:root.rootCombo.currentIndex;onCurrentIndexChanged:if(currentIndex>=0)root.rootCombo.currentIndex=currentIndex; Layout.preferredWidth:76; implicitHeight:28; model:Harmony.rootNames; font.pixelSize:12; onActivated:{autoChord.checked=false; analyze(); optionsChanged()}}
                        DeskComboBox {
                            font.family: theme.fontFamily;id:qualityComboControl;currentIndex:root.qualityCombo.currentIndex;onCurrentIndexChanged:if(currentIndex>=0)root.qualityCombo.currentIndex=currentIndex; Layout.fillWidth:true; Layout.minimumWidth:80; implicitHeight:28; model:qualityNames; font.pixelSize:12; onActivated:{autoChord.checked=false; analyze(); optionsChanged()}}
                    }
                }
                PanelCard {
                    heading:"谱面显示与着色";collapsed:configuration.collapsedSections.indexOf("display")>=0;onToggleRequested:root.toggleSection("display")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    DeskSwitch {
                            font.family: theme.fontFamily;
                        id:followPlaybackControl; text:"跟随播放"; checked:root.followPlayback.checked; enabled:playbackAvailable; font.pixelSize:12
                        visible:settingsExpanded && playbackAvailable
                        onToggled:{root.followPlayback.checked=checked;lastPlayTick=-1; requestRefresh(false); configurationTimer.restart()}
                    }
                    UiLabel {
                        width:parent.width; font.pixelSize:10; color:muted; wrapMode:Text.Wrap
                        visible:settingsExpanded || !playbackAvailable
                        text:observer?"跟随音序器发声音；选中状态与播放标记保持可见。":playbackAvailable?"读取真实播放位置；播放中仅更新面板。":"当前主程序没有提供播放位置接口，暂时跟随选区。"
                    }
                    DeskSwitch {
                            font.family: theme.fontFamily;
                        id:scoreColoringControl; text:"谱面临时配色"; checked:root.scoreColoring.checked; font.pixelSize:12
                        onToggled:{root.scoreColoring.checked=checked;if(checked)undoPause=false; if(!internalChange){repaintNotes(); optionsChanged()}}
                    }
                    DeskSwitch {id:allColorControl; text:"配色覆盖全部小节"; checked:root.allColor.checked; enabled:advancedNative && scoreColoring.checked; onToggled:{root.allColor.checked=checked;optionsChanged()}}
                    DeskSwitch {id:showChordsControl; text:"谱面上方显示和弦 / 级数"; checked:root.showChords.checked; enabled:advancedNative; onToggled:{root.showChords.checked=checked;optionsChanged()}}
                    DeskSwitch {id:showFunctionsControl; text:"音旁显示功能名"; checked:root.showFunctions.checked; enabled:advancedNative; onToggled:{root.showFunctions.checked=checked;optionsChanged()}}
                    UiLabel {
                        width:parent.width; font.pixelSize:10; color:muted; wrapMode:Text.Wrap
                        visible:scoreColoring.checked || settingsExpanded
                        text:observer?"仅屏幕预览，不改音符原色、撤销记录或保存 / 导出内容。":"开启会修改谱面颜色并产生撤销记录。保存 / 导出前请恢复原色；面板配色始终可用。"
                    }
                    RowLayout {
                        width:parent.width
                        DeskButton {
                            font.family: theme.fontFamily;text:"恢复原色"; Layout.fillWidth:true; implicitHeight:28; font.pixelSize:12; onClicked:stopColoring()}
                        DeskButton {
                            font.family: theme.fontFamily;text:"刷新"; Layout.fillWidth:true; implicitHeight:28; font.pixelSize:12; onClicked:requestRefresh(true)}
                    }
                    StableLabel {reservedLines:2; text:placementNotice || noticeText; color:muted; font.pixelSize:10}
                }
                PanelCard {
                    heading:"外观与功能色";collapsed:configuration.collapsedSections.indexOf("appearance")>=0;onToggleRequested:root.toggleSection("appearance")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:settingsExpanded
                    ConfigurationEditor {width:parent.width; configuration:root.configuration; onModified:{root.configuration=configuration; rebuildRegions();analyze();optionsChanged(false)}}
                    RowLayout {
                        width:parent.width
                        DeskButton {text:"导入配置"; Layout.fillWidth:true; enabled:advancedNative; onClicked:chooseFile("import-config")}
                        DeskButton {text:"导出配置"; Layout.fillWidth:true; enabled:advancedNative; onClicked:chooseFile("export-config")}
                    }
                    UiLabel {width:parent.width; text:"设置自动保存，下次启动自动读取。"; color:muted; font.pixelSize:10; wrapMode:Text.Wrap}
                }
                PanelCard {
                    id:rangeCard;heading:"修改和弦与范围";collapsed:configuration.collapsedSections.indexOf("range")>=0;onToggleRequested:root.toggleSection("range")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:settingsExpanded && advancedNative
                    RangeEditor {width:parent.width;observer:root.observer;handlesVisible:root.scoreRangeEditing;onEditingChanged:root.scoreRangeEditing=enabled;region:root.ownedRegion;regions:root.analysisRegions.filter(function(n){return n.part===root.focusedPart})
                        tick:Math.max(0,root.currentTick);scoreEnd:root.analysisEnd;canUndo:root.editUndo.length>0;canRedo:root.editRedo.length>0
                        roots:Harmony.rootNames;rootPcs:Harmony.rootPcs;qualities:root.qualityNames;onAction:root.editRange(command,start,end,rootIndex,qualityIndex,bassIndex)}
                    DeskButton {text:"复核完成：应用原人工范围";visible:root.manualNeedsReview;onClicked:root.editRange("review",0,0,0,0)}
                }
                PanelCard {
                    heading:"全谱和弦分析";collapsed:configuration.collapsedSections.indexOf("exchange")>=0;onToggleRequested:root.toggleSection("exchange")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:settingsExpanded

                    Flow {
                        width:parent.width; spacing:6
                        DeskButton {text:"导出 JSON"; enabled:advancedNative; onClicked:prepareExport("json")}
                        DeskButton {text:"导出 CSV"; enabled:advancedNative; onClicked:prepareExport("csv")}
                        DeskButton {text:"导入 JSON"; enabled:advancedNative; onClicked:chooseFile("import-analysis")}
                        DeskButton {text:"重新检测"; enabled:advancedNative; onClicked:{importedRecords=[];importedRegions=[]; optionsChanged()}}
                    }
                    UiLabel {width:parent.width; text:root.analysisStatus+(importedRecords.length?" · 使用导入分析":""); color:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                }
            }
        }
}
        Loader {id:inlineHarmonyDetails;anchors.fill:parent;active:!root.ribbon;sourceComponent:harmonyDetails}
        Loader {id:popupHarmonyDetails;parent:detailPopup.contentItem;anchors.fill:parent;active:root.ribbon&&!root.dualDetailActive;sourceComponent:harmonyDetails}
        Loader {id:auxHarmonyDetails;parent:root.detailPanelHost;anchors.fill:parent;active:root.dualDetailActive&&!!root.detailPanelHost;sourceComponent:harmonyDetails}

        Component {id:harmonySummary
Item {
            id:summarySurface
            visible:true; anchors.fill:parent; clip:true
            Rectangle {anchors.fill:parent;color:theme.background}
            Item {
                id:ribbonGroup; objectName:"harmonyRibbonGroup"
                width:Math.min(ribbonChord.implicitWidth+8+(ribbonStatus.visible?Math.min(230,ribbonStatus.implicitWidth)+8:0)+(ribbonTones.visible?ribbonTones.implicitWidth+8:0),Math.max(60,parent.width-ribbonControls.width-24))
                height:parent.height-4;anchors.verticalCenter:parent.verticalCenter
                x:{var maximum=Math.max(12,parent.width-ribbonControls.width-width-36);
                    return configuration.ribbonAlign===0 ? 12 : configuration.ribbonAlign===2 ? maximum :
                        configuration.ribbonAlign===3 ? 12+(maximum-12)*configuration.ribbonPosition/100 : Math.max(12,Math.min(maximum,(parent.width-ribbonControls.width-width)/2))}
                RowLayout {
                    id:ribbonRow;anchors.fill:parent;spacing:8
                    HarmonyLabel {id:ribbonChord;objectName:"harmonyRibbonChord";observer:root.observer;chord:root.summaryChordText;degree:root.summaryDegreeText;color:root.summaryInk;pixels:configuration.summaryPixels;degreeRatio:configuration.summaryDegreeRatio/100;gap:configuration.summaryGap;order:configuration.summaryOrder;degreeFont:configuration.degreeFont;chordFont:["","Edwin","Arial"][configuration.chordFont];revision:root.analysisFingerprint;Layout.preferredWidth:implicitWidth;Layout.preferredHeight:implicitHeight;Layout.maximumHeight:ribbonGroup.height;Layout.maximumWidth:Math.max(50,ribbonGroup.width-90);Layout.alignment:Qt.AlignVCenter}
                    UiLabel {id:ribbonStatus;text:(root.analysisDirty?"分析中":root.analysisStatus.indexOf("已更新")===0?"已更新":root.analysisStatus)+" · "+positionText.replace(/^当前位置[：:]\s*/,"")+" · "+(configuration.summaryMode===0?"实时":"所属");color:muted;font.pixelSize:11;visible:summarySurface.width>660;elide:Text.ElideRight;Layout.preferredWidth:Math.min(230,implicitWidth);Layout.fillWidth:true;Layout.alignment:Qt.AlignVCenter}
                    Row {
                        id:ribbonTones;visible:summarySurface.width>=1200;spacing:3;Layout.alignment:Qt.AlignVCenter
                        Repeater {model:root.summaryToneRows;delegate:Rectangle {
                            width:26;height:24;color:theme.field;radius:1
                            Rectangle{x:0;y:5;width:2;height:14;color:modelData.present?modelData.color:"#d0d0d0"}
                            UiLabel{anchors.centerIn:parent;text:modelData.label;color:modelData.present?ink:muted;font.pixelSize:11}
                        }}
                    }
                }
            }
            Row {
                id:ribbonControls;spacing:4;width:230;anchors.right:parent.right;anchors.rightMargin:12;anchors.verticalCenter:parent.verticalCenter
                DeskButton {id:ribbonAlignmentButton;objectName:"harmonyRibbonAlignment";width:98;text:["靠左","居中","靠右","自定位置"][configuration.ribbonAlign]+" ▾";onClicked:ribbonMenuWindow.openAt(ribbonAlignmentButton,true)}
                DeskButton {width:58;text:"详情";checked:root.dualDetailActive;visible:typeof root.showDetailPanel==="function";onClicked:{if(root.dualSummaryActive)root.focusPanel();else root.toggleDetailPanel()}ToolTip.visible:hovered;ToolTip.text:root.dualSummaryActive?"打开右侧详情":"显示／隐藏右侧详情"}
                DeskButton {id:ribbonMoreButton;objectName:"harmonyRibbonMore";width:58;text:"更多";onClicked:ribbonMenuWindow.openAt(ribbonMoreButton,false)}

            }
        }
}
        Loader {anchors.fill:parent;active:root.ribbon&&!root.dualSummaryActive;sourceComponent:harmonySummary}
        Loader {parent:root.summaryPanelHost;anchors.fill:parent;active:root.dualSummaryActive&&!!root.summaryPanelHost;sourceComponent:harmonySummary}

    }
}
