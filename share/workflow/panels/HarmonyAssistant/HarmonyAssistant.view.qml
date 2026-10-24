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
    version: "1.11.0"
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
    property bool chromaticChord: Harmony.isChromatic(tonicForRegion(configuration.detailMode===1?summaryRegion:null),minorForRegion(configuration.detailMode===1?summaryRegion:null),chordRoot,chordDefinition)
    property color chordInk:configuration.chromaticAccent && chromaticChord ? configuration.chromaticColor : configuration.detailColor || ink
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
    property var correctionParts: []
    function readCorrectionParts() {
        var parts=[]
        if(curScore)for(var i=0;i<curScore.parts.length;++i)if(curScore.parts[i].startTrack>=firstTrack&&curScore.parts[i].startTrack<endTrack)parts.push({startTrack:curScore.parts[i].startTrack,endTrack:curScore.parts[i].endTrack})
        return parts
    }
    property var refreshedOverrides:Timeline.refreshNotes(manualOverrides,analysisRecords,correctionParts)
    property var editUndo: []
    property var editRedo: []
    property string loadedFingerprint: ""
    property string loadedStructure: ""
    property bool manualNeedsReview: false
    property int annotationTick: -1
    property string annotationSelectionKey: ""
    property int liveRoot: -1
    property int liveDefinition: -1
    property string liveRootSpelling: ""
    property var displayedLiveHarmony:null
    property var lastLiveHarmony:null
    property string instantChordText:displayedLiveHarmony?displayedLiveHarmony.chord:"—"
    property string instantDegreeText:displayedLiveHarmony?displayedLiveHarmony.degree:"—"
    property int focusedPart: 0
    property var focusedRegions: {
        var part=focusedPart
        return analysisRegions.filter(function(region){return region.part===part})
    }
    property var ownedRegion: Timeline.atTick(focusedRegions,currentTick,focusedPart)
    property var suppressedDisplayRanges:Timeline.suppressionIndex(manualOverrides)
    property var displayRegion:Timeline.displayAtTick(focusedRegions,currentTick,focusedPart,configuration.holdLastHarmony,suppressedDisplayRanges)
    property var summaryRegion:configuration.summaryMode===1&&displayRegion&&(!analysisDirty||displayRegion.source==="manual")?displayRegion:null
    property string summaryChordText: !summaryRegion ? instantChordText : summaryRegion.chord
    property string summaryDegreeText: !summaryRegion ? instantDegreeText : degreeForRegion(summaryRegion)
    property int summaryRoot: !summaryRegion ? displayedLiveHarmony?displayedLiveHarmony.root:-1 : summaryRegion.root
    property int summaryDefinition: !summaryRegion ? displayedLiveHarmony?displayedLiveHarmony.definition:-1 : summaryRegion.definition
    property color summaryInk: configuration.chromaticAccent && Harmony.isChromatic(tonicForRegion(summaryRegion),minorForRegion(summaryRegion),summaryRoot,summaryDefinition) ? configuration.chromaticColor : configuration.summaryColor || ink
    property var summaryToneRows:makeToneRows(summaryRoot,summaryDefinition,resultNotes(summaryRegion))
    property int analysisEnd: analysisRecords.length ? (analysisRecords[analysisRecords.length-1].scoreEnd || analysisRecords[analysisRecords.length-1].tick+480) : 1920
    property var pendingRecords: []
    property var importedRecords: []
    property var importedRegions: []
    readonly property bool usingImportedAnalysis:importedRecords.length>0 || importedRegions.some(function(region){return root.regionInScope(region)}) || manualOverrides.some(function(region){return region.source==="imported"&&root.regionInScope(region)})
    property string analysisFingerprint: ""
    property int analysisNextTick: 0
    property bool analysisDirty: true
    property string pendingFileAction: ""
    property string pendingExport: ""
    property int desiredSummaryHeight:Math.max(32,Math.ceil(configuration.summaryPixels*Preferences.labelHeightFactor(configuration.summaryContent,configuration.summaryOrder,configuration.summaryDegreeRatio)*1.3+(configuration.summaryContent===2&&configuration.summaryOrder>=2?configuration.summaryGap:0)+6))
    property int preferredRibbonHeight:desiredSummaryHeight
    property int detailLabelHeight:Math.max(44,Math.ceil(configuration.detailPixels*Preferences.labelHeightFactor(configuration.detailContent,configuration.detailOrder,configuration.detailDegreeRatio)*1.6+(configuration.detailContent===2&&configuration.detailOrder>=2?configuration.detailGap:0)+8))
    property bool compactPanelTitle:true
    property int lastSummaryHeight:32
    onDesiredSummaryHeightChanged:resizeSummary(false)
    property bool independentPanelSurfaces:true
    property string summaryPanelTitle:"和弦摘要"
    property bool ribbonContentHeight: true
    property string detailPanelTitle:"和弦助手 · 详细面板"
    property color detailPanelBackground:theme.background
    property bool started: false
    property var observer: null
    property bool surfaceActive: (visible || dualSummaryActive || dualDetailActive || (showChords.checked && !!ownerScore)) && (!observer || observer.surfaceVisible)
    property string analysisStatus: !ownerScore ? "打开乐谱后开始识别" : analysisTimer.running ? "正在读取 · "+pendingRecords.length+" 个位置" : regionJobToken || regionBuilder ? "正在分析前后和弦" : analysisDirty ? observer && observer.playing ? "正在识别当前位置 · 停播后整理全段" : "正在准备和弦分析" : "已更新 · "+analysisRegions.length+" 处和弦"+(manualNeedsReview?" · 人工范围待核对":"")
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
    property string fileNotice: ""
    property string fileNoticeKind: "analysis"
    property string placementNotice: ""
    property color ink: theme.text
    property color muted: theme.muted
    property var qualityNames: Harmony.defs.map(function(d) { return d.name })

    property bool nativeCorrectionWrite:false
    property string nativeCorrectionsJSON:""
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
        saveCorrections(true);applyScorePreview();syncScoreRangeEditor()
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
    function regionInScope(region){return region.part>=firstTrack&&region.part<endTrack}
    function rebuildRegions() {
        var edits=manualNeedsReview?[]:refreshedOverrides
        analysisRegions=Timeline.overlay(Timeline.overlay(automaticRegions.filter(root.regionInScope),importedRegions.filter(root.regionInScope)),edits.filter(root.regionInScope)).map(root.displayInterpretation)
        if(!analysisDirty && observer && typeof observer.publishHarmonyAnalysis==="function")
            observer.publishHarmonyAnalysis({snapshotId:observer.analysisContentRevision(),regions:analysisRegions,source:"HarmonyAssistant",schema:1})
    }
    function correctionStructure() {
        if(!observer || typeof observer.referenceStructureKey!=="function")return ""
        var parts=[]
        if(curScore)for(var i=0;i<curScore.parts.length;++i)parts.push([curScore.parts[i].startTrack,curScore.parts[i].endTrack])
        return observer.referenceStructureKey()+":"+JSON.stringify(parts)
    }
    function loadCorrections() {
        if(!advancedNative || (loadedFingerprint===analysisFingerprint&&!manualNeedsReview))return
        var structure=correctionStructure()
        if((manualOverrides.length || manualPositions.length) && loadedFingerprint.length) {
            if(loadedStructure.length&&loadedStructure===structure) {
                loadedFingerprint=analysisFingerprint
                if(manualNeedsReview)noticeText="人工和弦范围已恢复。"
                manualNeedsReview=false
            } else {
                manualNeedsReview=true;noticeText="小节或谱表结构已改变，请核对人工和弦的起止位置。"
            }
            return
        }
        loadedFingerprint=analysisFingerprint;loadedStructure=structure
        if(observer&&typeof observer.workflowData==="function"){
            var embedded=observer.workflowData("harmony");nativeCorrectionsJSON=JSON.stringify(embedded)
            if(embedded.schema===2){
                manualOverrides=Timeline.validEdits(embedded.overrides||[]);manualPositions=AnnotationEdits.positions(embedded.positions||[])
                if(embedded.structure&&structure&&embedded.structure!==structure){loadedStructure=embedded.structure;manualNeedsReview=true;noticeText="小节或谱表结构已改变，请核对人工和弦的起止位置。"}
                return
            }
        }
        var saved=observer.loadConfiguration("HarmonyAssistant-regions-"+analysisFingerprint)
        if(saved.schema===1 && saved.fingerprint===analysisFingerprint && !saved.inScore) {
            try{manualOverrides=Timeline.validEdits(saved.overrides);manualPositions=AnnotationEdits.positions(saved.positions)}catch(error){noticeText="人工范围未读取："+error}
        }
    }
    function syncNativeCorrections(){
        if(nativeCorrectionWrite||!observer||typeof observer.workflowData!=="function")return
        var saved=observer.workflowData("harmony"),json=JSON.stringify(saved)
        if(!nativeCorrectionsJSON.length||json===nativeCorrectionsJSON)return
        nativeCorrectionsJSON=json;fileNotice="";manualOverrides=Timeline.validEdits(saved.overrides||[]);manualPositions=AnnotationEdits.positions(saved.positions||[])
        loadedStructure=saved.structure||correctionStructure();manualNeedsReview=loadedStructure!==correctionStructure()
        loadedFingerprint=analysisFingerprint
        rebuildRegions();applyScorePreview();syncScoreRangeEditor()
    }
    function writeCorrections(data) {
        var result
        nativeCorrectionWrite=true
        try {result=observer.setWorkflowData("harmony",data)}
        finally {nativeCorrectionWrite=false}
        if(result.error){noticeText=result.error;return false}
        nativeCorrectionsJSON=JSON.stringify(observer.workflowData("harmony"))
        return true
    }
    function saveCorrections(inScore) {
        if(!advancedNative || !loadedFingerprint.length || manualNeedsReview)return false
        if(inScore&&typeof observer.setWorkflowData==="function"){
            var data={schema:2,fingerprint:loadedFingerprint,structure:loadedStructure,overrides:manualOverrides,positions:manualPositions}
            if(!writeCorrections(data))return false
        }
        var embedded=nativeCorrectionsJSON.length && JSON.parse(nativeCorrectionsJSON).schema===2
        if(!observer.saveConfiguration("HarmonyAssistant-regions-"+loadedFingerprint,{schema:1,fingerprint:loadedFingerprint,overrides:manualOverrides,positions:manualPositions,inScore:embedded}))noticeText="人工范围保存失败。"
        return true
    }
    function reanalyseImported() {
        var next=manualOverrides.filter(function(region){return region.source!=="imported"||!root.regionInScope(region)})
        fileNoticeKind="analysis"
        if(next.length===manualOverrides.length&&!importedRecords.length&&!importedRegions.length){optionsChanged();fileNotice="已开始重新检测。";return}
        var data={schema:2,fingerprint:analysisFingerprint,structure:correctionStructure(),overrides:next,positions:manualPositions}
        if(!writeCorrections(data)){fileNotice=noticeText;noticeText="";return}
        manualOverrides=next;importedRecords=[];importedRegions=[];loadedFingerprint=data.fingerprint;loadedStructure=data.structure
        saveCorrections();rebuildRegions();optionsChanged();fileNotice="已改用自动检测，手动修改继续保留。"
    }
    function regionsFromImportedFrames(frames,context,configuration) {
        var matching=analysisFingerprint===context.fingerprint
        var rows=frames.filter(function(frame){return frame.tick<context.scoreEnd}).map(function(frame){
            var out=Timeline.clone(frame),current=matching?Analysis.atTick(analysisRecords,frame.tick):null
            if(!current || current.tick!==frame.tick)current=observer.contextSnapshot(frame.tick,firstTrack,endTrack,false,0,false)
            out.parts=current.parts;out.keySignature=current.keySignature;out.scoreEnd=context.scoreEnd;out.imported=true
            var named=/^[A-G][♯♭#b]*/.exec(out.chord||"");if(named)out.rootSpelling=named[0]
            return out
        })
        return Timeline.build(rows,Analysis.interpretationConfig(configuration,collectPreferences()),firstTrack,[])
    }
    function editRange(command,start,end,rootIndex,qualityIndex,bassIndex) {
        if(analysisDirty){noticeText="正在更新和声区间，请稍候。";return}
        if(command==="select"){openAnnotation(start,focusedPart);return}
        if(command==="position" && ownedRegion){moveAnnotation(ownedRegion.start,ownedRegion.part,0,0);return}
        if(command==="review"){loadedFingerprint=analysisFingerprint;loadedStructure=correctionStructure();manualNeedsReview=false;rebuildRegions();saveCorrections(true);applyScorePreview();return}
        var next=Timeline.clone(manualOverrides),region=ownedRegion
        if((command==="undo"||command==="redo")&&typeof observer.workflowUndo==="function"){observer.workflowUndo(command==="redo");syncNativeCorrections();return}
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
                        var info=Timeline.describe({root:Harmony.rootPcs[rootIndex],rootSpelling:Harmony.rootNames[rootIndex],definition:qualityIndex},edit.notes,start,Harmony.rootPcs[keyTonic.currentIndex],keyMode.currentIndex===1,configuration.degreeMode,prefersFlats(),keySpelling())
                        if(bassIndex>0) {
                            info.bass=Harmony.rootPcs[bassIndex-1]
                            info.bassName=Harmony.rootNames[bassIndex-1];delete info.bassTpc
                            info.chord=info.chord.split("/")[0]+(info.bass!==info.root?"/"+info.bassName:"")
                            info.degree=Harmony.roman(Harmony.rootPcs[keyTonic.currentIndex],keyMode.currentIndex===1,info.root,info.definition,configuration.degreeMode,info.bass,info.bassName,info.rootSpelling,keySpelling())
                            info.inversion=Harmony.inversion(Harmony.labelFor(info.bass,info.root,info.definition))
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
        manualOverrides=Timeline.validEdits(next);loadedFingerprint=analysisFingerprint;loadedStructure=correctionStructure()
        rebuildRegions();saveCorrections(true);analyze();applyScorePreview()
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
        scoreRangeEditing=true
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
        return frameSnapshot(frame,false)
    }
    function frameSnapshot(frame,deferDetection) {
        var notes=frame.analysisNotes || frame.notes || []
        var pitches=notes.map(function(n){return n.pitch})
        var imported=Analysis.atTick(importedRecords,frame.tick)
        var result=!notes.length || deferDetection || imported ? {root:-1,definition:-1} : autoChord.checked ? Harmony.detect(pitches,notes) :
            {root:Harmony.rootPcs[rootCombo.currentIndex],definition:qualityCombo.currentIndex,rootSpelling:Harmony.rootNames[rootCombo.currentIndex]}
        if(imported)result={root:imported.root,definition:imported.definition,rootSpelling:imported.rootSpelling||""}
        var tonic=Harmony.rootPcs[keyTonic.currentIndex]
        if(!manualKey) {
            var major=[11,6,1,8,3,10,5,0,7,2,9,4,11,6,1]
            tonic=major[(frame.keySignature || 0)+7]
            if(keyMode.currentIndex===1)tonic=Harmony.mod12(tonic+9)
        }
        var described=Timeline.describe(result,notes,frame.tick,tonic,keyMode.currentIndex===1,configuration.degreeMode,prefersFlats(),keySpelling(frame))
        if(imported){if(typeof imported.chord==="string")described.chord=imported.chord;if(typeof imported.degree==="string")described.degree=imported.degree}
        var output={tick:frame.tick,bar:frame.bar,beat:frame.beat,root:result.root,definition:result.definition,
            chord:described.chord,degree:described.degree,rootSpelling:described.rootSpelling,tonicSpelling:described.tonicSpelling,
            notes:notes,parts:frame.parts,pedalWindows:frame.pedalWindows,scoreEnd:frame.scoreEnd,keySignature:frame.keySignature,imported:!!imported,chromatic:described.chromatic}
        if(deferDetection && !imported)output.deferredDescription=true
        return output
    }
    function applyFrameDescriptions(descriptions) {
        for(var i=0;i<descriptions.length && i<analysisRecords.length;++i) {
            var described=descriptions[i],frame=analysisRecords[i]
            if(!described || described.tick!==frame.tick)continue
            for(var key in described)if(key!=="tick")frame[key]=described[key]
            delete frame.deferredDescription
        }
    }
    function buildAnalysisChunk() {
        if(!curScore || !surfaceActive || !needsAnalysis())return
        if(observer.playing && analysisRecords.length){cancelRegionJob();analysisTimer.stop();analysisDirty=true;return}
        if(regionBuilder){buildRegionChunk();return}
        var batch=observer.analysisFrames(analysisNextTick,observer.playing?8:32,firstTrack,endTrack,pedalContext.checked,windowTicks())
        analysisFingerprint=batch.fingerprint || ""
        var frames=batch.frames || []
        var defer=regionWorkerSource.length && typeof observer.startReadOnlyJob==="function" && autoChord.checked && !importedRecords.length
        for(var i=0;i<frames.length;++i)pendingRecords.push(frameSnapshot(frames[i],defer))
        analysisNextTick=batch.nextTick
        if(analysisNextTick>=0) {analysisTimer.start(); return}
        analysisRecords=pendingRecords;pendingRecords=[];correctionParts=readCorrectionParts();loadCorrections()
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
        if(showFunctions.checked || scoreColoring.checked && allColor.checked)for(var i=0;i<analysisRecords.length;++i) {
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
        fileNoticeKind=pendingFileAction.indexOf("config")>=0?"config":"analysis"
        try {
            var action=pendingFileAction, content=""
            if(action.indexOf("import")===0) {
                content=observer.readTextFile(path)
                if(!content.length)throw Error("文件为空、超过 16 MiB 或无法读取")
                var parsed=JSON.parse(content)
                if(action==="import-config") {applyPreferences(Preferences.clean(parsed));savePreferences();fileNotice="配置已导入并保存。"}
                else {
                    var imported=Analysis.validate(parsed)
                    // Validate every section before changing memory or persisted corrections.
                    var overrides=Timeline.validEdits(imported.overrides||[]),regions=Timeline.validEdits(imported.regions||[]),positions=AnnotationEdits.positions(imported.positions)
                    setScope()
                    var context=observer.contextSnapshot(0,firstTrack,endTrack,pedalContext.checked,windowTicks(),false),fingerprint=context.fingerprint
                    if(imported.fingerprint!==fingerprint)throw Error("请切回导出时的乐谱与识别范围，再导入这个文件。")
                    imported.frames.forEach(function(frame){
                        if(frame.tick>context.scoreEnd || frame.tick===context.scoreEnd&&(frame.root>=0||frame.notes.length))throw Error("分析位置超出乐谱范围")
                        frame.notes.forEach(function(note){if(note.track<firstTrack||note.track>=endTrack||note.tick>=context.scoreEnd)throw Error("分析音符超出当前乐器范围")})
                    })
                    if(!regions.length&&imported.frames.length)regions=regionsFromImportedFrames(imported.frames,context,imported.configuration)
                    overrides.concat(regions).forEach(function(r){if((context.scoreEnd && r.end>context.scoreEnd) || r.part<firstTrack || r.part>=endTrack || partForTrack(r.part)!==r.part)throw Error("分析区间超出乐谱 / 乐器范围")})
                    positions.forEach(function(position){if(position.tick>=context.scoreEnd||position.part<firstTrack||position.part>=endTrack||partForTrack(position.part)!==position.part)throw Error("记号位置超出当前乐器范围")})
                    var next=Timeline.importEdits(regions,overrides),structure=correctionStructure()
                    var saved=observer.workflowData("harmony")
                    var previousEdits=saved.schema===2?Timeline.validEdits(saved.overrides||[]):manualOverrides
                    var previousPositions=saved.schema===2?AnnotationEdits.positions(saved.positions||[]):manualPositions
                    var retainedEdits=previousEdits.filter(function(region){return !root.regionInScope(region)})
                    var retainedPositions=previousPositions.filter(function(position){return !root.regionInScope(position)})
                    if((retainedEdits.length||retainedPositions.length)&&saved.structure&&saved.structure!==structure)throw Error("其他乐器的人工范围需要复核。请先在「修改和弦与范围」完成复核，再导入。")
                    var allEdits=retainedEdits.concat(next),allPositions=retainedPositions.concat(positions)
                    if(!writeCorrections({schema:2,fingerprint:fingerprint,structure:structure,overrides:allEdits,positions:allPositions})){var reason=noticeText;noticeText="";throw Error(reason)}
                    manualOverrides=allEdits;manualPositions=allPositions;loadedFingerprint=fingerprint;loadedStructure=structure;manualNeedsReview=false;editUndo=[];editRedo=[]
                    importedRecords=[];importedRegions=[];saveCorrections();rebuildRegions();optionsChanged()
                    var visibleCount=next.filter(function(region){return !region.suppressed}).length,hiddenCount=next.length-visibleCount
                    noticeText="";fileNotice="已导入 "+visibleCount+" 段和弦解释"+(hiddenCount?"、"+hiddenCount+" 处隐藏":"")+"。保存乐谱后保留。"
                }
            } else {
                content=action==="export-config" ? JSON.stringify(collectPreferences(),null,2) :
                    action==="csv" ? Analysis.csvRegions(analysisRegions) :
                    JSON.stringify(Analysis.document(analysisRecords,analysisFingerprint,collectPreferences(),analysisRegions,manualOverrides.filter(root.regionInScope).map(root.displayInterpretation),manualPositions.filter(root.regionInScope)),null,2)
                var suffix=action==="csv"?".csv":".json"
                if(path.toLowerCase().slice(-suffix.length)!==suffix)path+=suffix
                if(!observer.writeTextFile(path,content))throw Error("无法写入文件")
                fileNotice="已导出："+decodeURIComponent(path)
            }
        } catch(error) {fileNotice="操作未完成："+String(error)}
    }

    function same(a, b) { return a && b && a.is(b) }
    function ownsCurrentScore() {
        var observed=observer?observer.score:null
        if(!curScore)return !ownerScore && (!observer || !observed)
        return same(curScore,ownerScore) && (!observer || same(curScore,observed))
    }
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
    function keySpelling(frame) {
        return Timeline.tonicNameAt(frame||nativeSnapshot,{manualKey:manualKey,keyTonic:keyTonic.currentIndex,keyMode:keyMode.currentIndex})
    }
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
        if (liveRoot===chordRoot && liveRootSpelling.length)return liveRootSpelling
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
        if (ownsCurrentScore()) return
        saveCorrections()
        undoPause = false
        restoreColors(true)
        buildTimer.stop()
        buildSegment = null
        ownerScore = curScore
        fileNotice="";fileNoticeKind="analysis"
        lastLiveHarmony=null;displayedLiveHarmony=null
        if (observer) observer.score = curScore
        colorLedger = []
        activeColorKeys = ({})
        undoPause = false
        timeline = []
        cancelRegionJob();analysisTimer.stop()
        manualOverrides=[];manualPositions=[];scoreRangeEditing=false;editUndo=[];editRedo=[];analysisRegions=[];automaticRegions=[];regionBuilder=null;loadedFingerprint="";loadedStructure="";nativeCorrectionsJSON="";manualNeedsReview=false;annotationTick=-1
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
        displayedLiveHarmony=null;lastLiveHarmony=null;liveRoot=-1;liveDefinition=-1;liveRootSpelling=""
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
            saveCorrections();manualOverrides=[];manualPositions=[];scoreRangeEditing=false;editUndo=[];editRedo=[];loadedFingerprint="";loadedStructure="";nativeCorrectionsJSON="";manualNeedsReview=false
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
        if (!ownsCurrentScore() || !isOpen(ownerScore)) {
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
            var readFirst=firstTrack,readEnd=endTrack
            if(scope.currentIndex===1)for(var partIndex=0;partIndex<curScore.parts.length;++partIndex) {
                var part=curScore.parts[partIndex]
                if(part.startTrack===focusedPart){readFirst=part.startTrack;readEnd=part.endTrack;break}
            }
            nativeSnapshot = advancedNative ? observer.contextSnapshot(tick, readFirst, readEnd,
                pedalContext.checked, windowTicks(), lastPlaying && followPlayback.checked) :
                observer.snapshot(tick, readFirst, readEnd, lastPlaying && followPlayback.checked)
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
        var detectedSpelling=""
        if (!contextNotes.length && !currentNotes.length) {
            chordRoot = -1
            chordDefinition = -1
            matchText = currentTick < 0 ? "请选择音符、和弦或休止符" : "空拍 · 没有持续音"
        } else if (autoChord.checked) {
            var contextPitches = contextNotes.map(function(n){return n.pitch})
            var result = Harmony.detect(contextPitches.length ? contextPitches : currentPitches,contextPitches.length ? contextNotes : currentNotes)
            var imported = Analysis.atTick(importedRecords, currentTick)
            if (imported) result = {root:imported.root,definition:imported.definition,rootSpelling:imported.rootSpelling||"",kind:"已导入的分析",alternatives:[]}
            else if (contextPitches.length > currentPitches.length) result.kind += " · 踏板 / 琶音上下文"
            detectedSpelling=result.rootSpelling||""
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
                    var instant=Harmony.detect(currentPitches,currentNotes)
                    if(instant.root>=0 && (instant.root!==chordRoot || instant.definition!==chordDefinition))
                        alternativesText += (alternativesText.length?" · ":"")+"即时音："+nameOf(instant.root)+Harmony.defs[instant.definition].suffix
                }
            }
        } else {
            chordRoot = Harmony.rootPcs[rootCombo.currentIndex]
            chordDefinition = qualityCombo.currentIndex
            matchText = "手动指定"
        }
        liveRoot=chordRoot;liveDefinition=chordDefinition;liveRootSpelling=detectedSpelling
        updateTexts()
        updatePianoRange()
        repaintNotes()
        scheduleAnalysis()
    }
    function tonicForRegion(region){return manualKey||!region||region.tonic===undefined?Harmony.rootPcs[keyTonic.currentIndex]:region.tonic}
    function minorForRegion(region){return manualKey||!region||region.minor===undefined?keyMode.currentIndex===1:region.minor}
    function tonicNameForRegion(region){return manualKey||!region||region.tonic===undefined?keySpelling():region.tonicSpelling||Harmony.pcName(region.tonic,prefersFlats())}
    function displayInterpretation(region){
        var shown={};for(var field in region)shown[field]=region[field]
        shown.tonic=tonicForRegion(region);shown.minor=minorForRegion(region);shown.tonicSpelling=tonicNameForRegion(region)
        shown.degree=degreeForRegion(shown);shown.chromatic=Harmony.isChromatic(shown.tonic,shown.minor,shown.root,shown.definition)
        return shown
    }
    function degreeForRegion(region) {
        if(!region)return "—"
        var tonic=tonicForRegion(region),minor=minorForRegion(region)
        var bassName=region.bassName||Harmony.spelledName(region.bassTpc,region.bass,prefersFlats())
        var named=/^[A-G][♯♭#b]*/.exec(region.chord||"")
        return Harmony.roman(tonic,minor,region.root,region.definition,configuration.degreeMode,region.bass,bassName,
            region.rootSpelling||(named?named[0]:""),tonicNameForRegion(region))
    }
    function resultNotes(region) {
        return region && currentTick<region.end ? region.notes : contextNotes.length?contextNotes:currentNotes
    }
    function updateTexts() {
        chordRoot=liveRoot;chordDefinition=liveDefinition
        var liveNotes=resultNotes(null),tonic=Harmony.rootPcs[keyTonic.currentIndex],minor=keyMode.currentIndex===1
        var live=Timeline.describe({root:liveRoot,definition:liveDefinition,rootSpelling:liveRoot>=0?rootName():""},liveNotes,currentTick,tonic,minor,configuration.degreeMode,prefersFlats(),keySpelling())
        if(lastLiveHarmony && (lastLiveHarmony.part!==focusedPart || currentTick<lastLiveHarmony.tick))lastLiveHarmony=null
        var retained=false,canHold=configuration.holdLastHarmony&&!Timeline.isSuppressed(suppressedDisplayRanges,currentTick,focusedPart)&&!(ownedRegion&&ownedRegion.root<0&&ownedRegion.source!=="auto")
        if(live.root>=0) {
            lastLiveHarmony=Timeline.clone(live);lastLiveHarmony.tick=currentTick;lastLiveHarmony.part=focusedPart
        } else if(canHold && lastLiveHarmony) {
            live=Timeline.clone(lastLiveHarmony);live.degree=degreeForRegion(live);retained=true
        }
        live.chord=live.chord||(currentPitches.length===1?noteName(currentNotes[0]):"—")
        live.degree=live.degree||"—";displayedLiveHarmony=live
        chordRoot=live.root;chordDefinition=live.definition;chordText=live.chord;degreeText=live.degree
        var voicing = []
        for (var i = 0; i < currentNotes.length; ++i) voicing.push(noteName(currentNotes[i]))
        voicingText = voicing.length ? voicing.join(" · ") : "—"
        inversionText = "—"
        if (chordRoot >= 0) {
            var bass=Timeline.bass(liveNotes,currentTick)
            inversionText=live.inversion+(live.bass>=0?" · 低音 "+(retained?live.bassName:bass?noteName(bass):live.bassName):"")
        }
        if(retained)matchText="保留上一和弦"
        if(configuration.detailMode===1 && summaryRegion) {
            chordRoot=summaryRegion.root;chordDefinition=summaryRegion.definition
            chordText=summaryRegion.chord;degreeText=degreeForRegion(summaryRegion)
            var sourceLabel=summaryRegion.source==="manual"?"人工修正":summaryRegion.source==="imported"?"导入分析":summaryRegion.kind||"已识别"
            if(summaryRegion.source==="imported"){
                sourceLabel+=" · "+tonicNameForRegion(summaryRegion)+(minorForRegion(summaryRegion)?"小调":"大调")
            }
            matchText=(currentTick>=summaryRegion.end?"保留上一和弦 · ":"所属和弦 · ")+sourceLabel
            inversionText=(summaryRegion.inversion||Harmony.inversion(Harmony.labelFor(summaryRegion.bass,chordRoot,chordDefinition)))+(summaryRegion.bass>=0?" · 低音 "+Harmony.pcName(summaryRegion.bass,prefersFlats()):"")
            var candidates=summaryRegion.alternatives||[]
            alternativesText=candidates.length?"也可能是 "+candidates.map(function(a){return nameOf(a.root)+Harmony.defs[a.definition].suffix}).join(" / "):""
            if(summaryRegion.harmonicBass!==undefined && summaryRegion.harmonicBass!==summaryRegion.bass)alternativesText+=(alternativesText.length?" · ":"")+"新弹音的最低音 "+Harmony.pcName(summaryRegion.harmonicBass,prefersFlats())+"；原有低音仍在持续"
        }
        var roleNotes=resultNotes(configuration.detailMode===1?summaryRegion:null)
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
        if (!ownsCurrentScore()) {changeScore();requestRefresh(true); return}
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
        if (!ownsCurrentScore()) changeScore()
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
            try {regionWorkerSource=Timeline.workerBundle({harmony:observer.readTextFile(String(Qt.resolvedUrl("Harmony.js"))),timeline:observer.readTextFile(String(Qt.resolvedUrl("Timeline.js"))),packed:true})}
            catch(error){noticeText="后台和弦计算未启用："+error;regionWorkerSource=""}
        }
        syncDetailPanel()
        if(observer&&curScore){setScope();displayTick(Math.max(0,observer.tick),true)}
        requestRefresh(true)
    }
    onScoreStateChanged: {
        if (!started || internalChange || nativeCorrectionWrite) return
        syncNativeCorrections()
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
        title:"和弦助手 · 详细面板"; flags:Qt.Tool
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
        id:summarySettings;objectName:"harmonySummarySettings";title:"和弦摘要外观与调性";width:380;height:570;minimumWidth:320;flags:Qt.Tool;color:theme.background
        property bool placed:false
        ColorDialog {id:summaryColorPicker;title:"选择摘要文字颜色";showAlphaChannel:false;onAccepted:root.changeSummary("summaryColor",String(color))}
        ScrollView {anchors.fill:parent;anchors.margins:12;clip:true
            Column {width:summarySettings.width-36;spacing:10
                RowLayout {width:parent.width
                    UiLabel {text:"主音与调式";color:ink}
                    DeskComboBox {Layout.fillWidth:true;model:Harmony.rootNames;currentIndex:keyTonic.currentIndex;onActivated:{keyTonic.currentIndex=currentIndex;manualKey=true;root.updateTexts();root.optionsChanged()}}
                    DeskComboBox {model:["大调","小调"];currentIndex:keyMode.currentIndex;onActivated:{keyMode.currentIndex=currentIndex;root.updateTexts();root.optionsChanged()}}
                }
                HarmonyStyleEditor {width:parent.width;prefix:"summary";configuration:root.configuration;observer:root.observer;chord:root.summaryChordText;degree:root.summaryDegreeText
                    onModified:{root.configuration=configuration;configurationTimer.restart();root.resizeSummary(false)}
                    onChooseColor:{summaryColorPicker.color=color;summaryColorPicker.open()}
                }
                DeskButton {iconName:"close";text:"关闭";onClicked:summarySettings.hide()}
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
            var completed=result.payload?JSON.parse(result.payload):result
            root.applyFrameDescriptions(completed.descriptions||[])
            root.finishRegions(completed.regions||[])
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
            objectName:"harmonyDetailScroll"
            function scrollRange(){contentY=Math.max(0,rangeCard.y-6)}
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: panel.height + 16
            boundsBehavior: Flickable.StopAtBounds
            visible:true
            ScrollBar.vertical: ScrollBar { id:detailScroll;policy:flick.contentHeight>flick.height?ScrollBar.AlwaysOn:ScrollBar.AsNeeded }
            Flow {
                id: panel
                objectName:"harmonyDetailContent"
                x: 8
                y: 8
                width: Math.max(160, flick.width - 16 - (detailScroll.visible?detailScroll.width:0))
                spacing: 6
                Column {
                    width: parent.width
                    spacing: 3
                    RowLayout {
                        width: parent.width
                        UiLabel {text:"和弦助手"; Layout.fillWidth:true; color:ink; font.pixelSize:14; font.bold:true}
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
                            objectName:"harmonySettingsButton";font.family: theme.fontFamily;text:"设置"; checked:settingsExpanded; checkable:true; font.pixelSize:11; onClicked:{settingsExpanded=checked; configurationTimer.restart()}}
                    }
                    UiLabel {width:parent.width; text:Harmony.rootNames[keyTonic.currentIndex]+(keyMode.currentIndex===0?" 大调":" 小调")+" · "+scopeText; color:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                }
                PanelCard {
                    id:summaryCard; objectName:"harmonySummary"
                    width: root.expanded ? (panel.width-10)/2 : panel.width
                    fixedBodyHeight:root.detailLabelHeight+154+(settingsExpanded?36:0)
                    RowLayout {width:parent.width;height:28
                        UiLabel {Layout.fillWidth:true;text:positionText;color:muted;font.pixelSize:11;elide:Text.ElideRight}
                        DeskComboBox {objectName:"harmonyResultMode";Layout.preferredWidth:112;model:["实时结果","所属和弦"];currentIndex:configuration.resultMode;onActivated:{var c=Timeline.clone(configuration);c.resultMode=currentIndex;root.configuration=Preferences.clean(c);root.updateTexts();root.optionsChanged(false)}}
                    }
                    Item {width:parent.width;height:root.detailLabelHeight;clip:true
                        HarmonyLabel {objectName:"harmonyDetailLabel";anchors.left:parent.left;anchors.verticalCenter:parent.verticalCenter;width:Math.min(parent.width,implicitWidth);height:Math.min(parent.height,implicitHeight);observer:root.observer;chord:configuration.detailContent===1?"":root.chordText;degree:configuration.detailContent===0?"":root.degreeText;color:root.chordInk;pixels:configuration.detailPixels;degreeRatio:configuration.detailDegreeRatio/100;gap:configuration.detailGap;order:configuration.detailOrder;chordFont:["","Edwin","Arial"][configuration.detailFont];degreeFont:configuration.detailDegreeFont;revision:root.analysisFingerprint}
                    }
                    StableLabel {reservedLines:2; text:matchText+(configuration.chromaticAccent && chromaticChord?" · 含调外音":""); color:muted; font.pixelSize:12}
                    StableLabel {reservedLines:2; text:alternativesText; color:muted; font.pixelSize:11}
                    Rectangle {width:parent.width; height:1; color:theme.subtleLine}
                    StableLabel {text:inversionText; color:muted; font.pixelSize:11}
                    StableLabel {visible:settingsExpanded; reservedLines:2; text:voicingText; color:ink; font.pixelSize:12}
                }
                PanelCard {
                    id:rangeCard;objectName:"harmonyRangeCard";heading:root.manualNeedsReview?"修改和弦与范围 · 需核对":"修改和弦与范围";collapsed:configuration.collapsedSections.indexOf("range")>=0;onToggleRequested:root.toggleSection("range")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:advancedNative
                    RangeEditor {width:parent.width;observer:root.observer;handlesVisible:root.scoreRangeEditing;onEditingChanged:root.scoreRangeEditing=enabled;region:root.ownedRegion;regions:root.analysisRegions.filter(function(n){return n.part===root.focusedPart})
                        tick:Math.max(0,root.currentTick);scoreEnd:root.analysisEnd;canUndo:root.editUndo.length>0;canRedo:root.editRedo.length>0
                        roots:Harmony.rootNames;rootPcs:Harmony.rootPcs;qualities:root.qualityNames;onAction:root.editRange(command,start,end,rootIndex,qualityIndex,bassIndex)}
                    DeskButton {text:"复核完成：应用原人工范围";visible:root.manualNeedsReview;onClicked:root.editRange("review",0,0,0,0)}
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
                    heading:"实际音高";collapsed:configuration.collapsedSections.indexOf("pitches")>=0;onToggleRequested:root.toggleSection("pitches")
                    objectName:"harmonyKeyboard"
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:keyboardExpanded
                    RowLayout {
                        width:parent.width
                        UiLabel {text:"键盘范围"; Layout.fillWidth:true; color:ink; font.bold:true; font.pixelSize:13}
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
                    DeskSwitch {objectName:"harmonyHoldLast";text:"空拍／未确定时保留上一和弦";font.pixelSize:11;checked:configuration.holdLastHarmony;onClicked:{root.changeSummary("holdLastHarmony",checked);root.analyze()}}
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
                        text:observer?"播放时显示当前位置的和弦，原有选音继续可见。":playbackAvailable?"面板跟随实际播放位置更新。":"当前主程序按选区更新显示。"
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
                        text:observer?"临时功能色和音旁文字用于屏幕分析。打印、PDF采用原谱颜色。":"谱面配色记录在乐谱里。点「恢复原色」还原，面板配色可继续使用。"
                    }
                    Flow {
                        width:parent.width;spacing:6
                        DeskButton {iconName:"reset";text:"恢复原色";onClicked:stopColoring()}
                        DeskButton {iconName:"refresh";text:"刷新";onClicked:requestRefresh(true)}
                    }
                    StableLabel {reservedLines:2; text:placementNotice || noticeText; color:muted; font.pixelSize:10}
                }
                PanelCard {
                    heading:"外观、功能色与文字";collapsed:configuration.collapsedSections.indexOf("appearance")>=0;onToggleRequested:root.toggleSection("appearance")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:settingsExpanded
                    ConfigurationEditor {width:parent.width;configuration:root.configuration;observer:root.observer;chord:root.chordText;degree:root.degreeText;onModified:{root.configuration=configuration;rebuildRegions();analyze();optionsChanged(false)}onLayoutModified:{root.configuration=configuration;configurationTimer.restart()}}
                    Flow {
                        width:parent.width;spacing:6
                        DeskButton {iconName:"open";text:"导入配置";enabled:advancedNative;onClicked:chooseFile("import-config")}
                        DeskButton {iconName:"save";text:"导出配置";enabled:advancedNative;onClicked:chooseFile("export-config")}
                    }
                    UiLabel {width:parent.width; text:"设置自动保存，下次启动自动读取。"; color:muted; font.pixelSize:10; wrapMode:Text.Wrap}
                    StableLabel {reservedLines:2;text:root.fileNoticeKind==="config"?root.fileNotice:"";visible:root.fileNoticeKind==="config"&&root.fileNotice.length>0;color:muted;font.pixelSize:10}
                }

                PanelCard {
                    objectName:"harmonyExchangeCard"
                    heading:"分析文件";collapsed:configuration.collapsedSections.indexOf("exchange")>=0;onToggleRequested:root.toggleSection("exchange")
                    width:root.expanded ? (panel.width-10)/2 : panel.width
                    visible:settingsExpanded

                    Flow {
                        width:parent.width; spacing:6
                        DeskButton {iconName:"save";text:"导出 JSON"; enabled:advancedNative&&!!root.ownerScore; onClicked:prepareExport("json")}
                        DeskButton {text:"导出 CSV"; enabled:advancedNative&&!!root.ownerScore; onClicked:prepareExport("csv")}
                        DeskButton {text:"导入 JSON"; enabled:advancedNative&&!!root.ownerScore; onClicked:chooseFile("import-analysis")}
                        DeskButton {objectName:"harmonyReanalyseImported";text:"重新检测"; enabled:advancedNative&&!!root.ownerScore; onClicked:root.reanalyseImported();ToolTip.visible:hovered;ToolTip.text:"移除导入的解释，按当前音符重新分析；手动修改继续保留。"}
                    }
                    UiLabel {width:parent.width; text:root.analysisStatus+(root.usingImportedAnalysis?" · 含导入解释":""); color:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                    StableLabel {reservedLines:2;text:root.fileNoticeKind==="analysis"?root.fileNotice:"";color:muted;font.pixelSize:10}
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
                    HarmonyLabel {id:ribbonChord;objectName:"harmonyRibbonChord";observer:root.observer;chord:configuration.summaryContent===1?"":root.summaryChordText;degree:configuration.summaryContent===0?"":root.summaryDegreeText;color:root.summaryInk;pixels:configuration.summaryPixels;degreeRatio:configuration.summaryDegreeRatio/100;gap:configuration.summaryGap;order:configuration.summaryOrder;degreeFont:configuration.summaryDegreeFont;chordFont:["","Edwin","Arial"][configuration.summaryFont];revision:root.analysisFingerprint;Layout.preferredWidth:implicitWidth;Layout.preferredHeight:implicitHeight;Layout.maximumHeight:ribbonGroup.height;Layout.maximumWidth:Math.max(50,ribbonGroup.width-90);Layout.alignment:Qt.AlignVCenter}
                    UiLabel {id:ribbonStatus;text:(root.analysisStatus.indexOf("已更新")===0?"已更新":root.analysisStatus)+" · "+positionText.replace(/^当前位置[：:]\s*/,"")+" · "+(!root.summaryRegion?"实时":"所属");color:muted;font.pixelSize:11;visible:summarySurface.width>660;elide:Text.ElideRight;Layout.preferredWidth:Math.min(230,implicitWidth);Layout.fillWidth:true;Layout.alignment:Qt.AlignVCenter}
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
