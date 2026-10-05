import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import MuseScore 3.0
import "Harmony.js" as Harmony

MuseScore {
    id: root
    menuPath: "Plugins.Harmony Assistant"
    description: "钢琴和声助手：持续音识别、级数、音程功能与可选谱面配色。"
    version: "1.0.0"
    requiresScore: true
    pluginType: "dock"
    dockArea: "right"
    implicitWidth: 360
    implicitHeight: 740

    property bool started: false
    property var observer: null
    property bool surfaceActive: visible && (!observer || observer.surfaceVisible)
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
    property string positionText: "等待选区"
    property string noticeText: ""
    property color ink: "#253342"
    property color muted: "#6B7785"
    property var qualityNames: Harmony.defs.map(function(d) { return d.name })

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
        for (var i = 0; i < currentNotes.length; ++i)
            if (Harmony.mod12(currentNotes[i].pitch) === chordRoot)
                return Harmony.spelledName(currentNotes[i].tpc, currentNotes[i].pitch, prefersFlats())
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
        cacheDirty = true
        manualKey = false
        lastPlaying = false
        lastPlayTick = -1
        playbackAvailable = !!observer || (!!curScore && typeof curScore.harmonyPlaybackTick === "number" &&
                typeof curScore.harmonyPlaybackActive === "boolean")
        clearDisplay(curScore ? "请选择音符、和弦或休止符" : "没有打开乐谱")
    }
    function clearDisplay(message) {
        currentTick = -1
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
        positionText = "等待选区"
        matchText = message
    }
    function requestRefresh(dirty) {
        if (!started || internalChange) return
        if (dirty) {
            cacheDirty = true
            buildTimer.stop()
            buildSegment = null
        }
        if (surfaceActive) refreshTimer.restart()
    }
    function refreshSelection() {
        changeScore()
        if (!curScore || !surfaceActive) return
        if (lastPlaying && followPlayback.checked) return
        var location = selectedLocation()
        if (!location) {
            restoreColors()
            clearDisplay("请选择音符、和弦或休止符")
            return
        }
        selectionTrack = location.track
        var oldFirst = firstTrack, oldEnd = endTrack
        setScope()
        if (oldFirst !== firstTrack || oldEnd !== endTrack) cacheDirty = true
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
            nativeSnapshot = observer.snapshot(tick, firstTrack, endTrack, lastPlaying && followPlayback.checked)
            notes = nativeSnapshot.notes || []
        } else {
            for (var t = 0; t < timeline.length; ++t) {
                var event = Harmony.atTick(timeline[t], tick)
                if (event) for (var n = 0; n < event.notes.length; ++n) notes.push(event.notes[n])
            }
        }
        notes.sort(function(a,b) {return a.pitch - b.pitch || a.track - b.track})
        for (var i = 0; i < notes.length; ++i) pitches.push(notes[i].pitch)
        var changed = JSON.stringify(notes) !== JSON.stringify(currentNotes)
        currentTick = tick
        positionText = lastPlaying && followPlayback.checked ? "正在播放 · 包含持续音" : "当前选区 · 包含持续音"
        if (observer && nativeSnapshot.bar !== undefined)
            positionText = (lastPlaying && followPlayback.checked ? "播放" : "选区") +
                    " · 第 " + nativeSnapshot.bar + " 小节 · 第 " + nativeSnapshot.beat + " 拍"
        var previousKey = keyTonic.currentIndex
        initializeKey(tick)
        if (!changed && !force) {
            if (previousKey !== keyTonic.currentIndex) updateTexts()
            return
        }
        currentNotes = notes
        currentPitches = pitches
        analyze()
    }
    function analyze() {
        alternativesText = ""
        if (!currentNotes.length) {
            chordRoot = -1
            chordDefinition = -1
            matchText = currentTick < 0 ? "请选择音符、和弦或休止符" : "空拍 · 没有持续音"
        } else if (autoChord.checked) {
            var result = Harmony.detect(currentPitches)
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
            }
        } else {
            chordRoot = Harmony.rootPcs[rootCombo.currentIndex]
            chordDefinition = qualityCombo.currentIndex
            matchText = "手动指定"
        }
        updateTexts()
        updatePianoRange()
        repaintNotes()
    }
    function updateTexts() {
        chordText = chordRoot < 0 ? (currentPitches.length === 1 ? noteName(currentNotes[0]) : "—") :
                rootName() + Harmony.defs[chordDefinition].suffix
        degreeText = Harmony.roman(Harmony.rootPcs[keyTonic.currentIndex], keyMode.currentIndex === 1,
                                   chordRoot, chordDefinition)
        var voicing = []
        for (var i = 0; i < currentNotes.length; ++i) voicing.push(noteName(currentNotes[i]))
        voicingText = voicing.length ? voicing.join(" · ") : "—"
        inversionText = "—"
        if (currentNotes.length && chordRoot >= 0) {
            var bass = currentNotes[0], label = Harmony.labelFor(bass.pitch, chordRoot, chordDefinition)
            inversionText = Harmony.inversion(label) + " · 低音 " + noteName(bass)
            if (Harmony.mod12(bass.pitch) !== chordRoot) chordText += "/" + Harmony.spelledName(bass.tpc, bass.pitch, prefersFlats())
        }
        var degrees = ["1","3","5","7","9","11","13"], rows = []
        for (var d = 0; d < degrees.length; ++d) {
            var degree = degrees[d], labels = [], played = [], expected = false
            if (chordDefinition >= 0) {
                var definition = Harmony.defs[chordDefinition]
                for (var k = 0; k < definition.labels.length; ++k)
                    if (Harmony.role(definition.labels[k]) === degree) {
                        expected = true
                        labels.push(definition.labels[k])
                    }
            }
            for (i = 0; i < currentNotes.length; ++i) {
                var functionLabel = Harmony.labelFor(currentNotes[i].pitch, chordRoot, chordDefinition)
                if (Harmony.role(functionLabel) === degree) played.push(noteName(currentNotes[i]))
            }
            rows.push({degree:degree, label:labels.length ? labels.join(" / ") : degree,
                       notes:played.length ? played.join(" · ") : expected ? "缺音" : "—",
                       present:played.length > 0, expected:expected, color:Harmony.colors[degree]})
        }
        toneRows = rows
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
                wanted[locatorKey(currentNotes[i])] = {location:currentNotes[i], color:Harmony.colorFor(currentNotes[i].pitch, chordRoot, chordDefinition)}
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
            var colors = []
            if (scoreColoring.checked && chordRoot >= 0)
                for (var i = 0; i < currentNotes.length; ++i) {
                    var descriptor = {}
                    for (var field in currentNotes[i]) descriptor[field] = currentNotes[i][field]
                    descriptor.color = Harmony.colorFor(descriptor.pitch, chordRoot, chordDefinition)
                    colors.push(descriptor)
                }
            observer.setNotePreviewColors(colors)
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
    }
    function updatePianoRange() {
        if (!currentPitches.length) {pianoStart = 48; pianoOctaves = 3; return}
        var low = currentPitches[0], high = currentPitches[currentPitches.length - 1]
        pianoStart = Math.floor(low / 12) * 12
        pianoOctaves = Math.max(3, Math.ceil((high - pianoStart + 1) / 12))
    }
    function midiActive(midi) {return currentPitches.indexOf(midi) >= 0}
    function pianoColor(midi, black) {
        return midiActive(midi) ? Harmony.colorFor(midi, chordRoot, chordDefinition) : black ? "#28333D" : "#FFFFFF"
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
        }
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
        if (observer) observer.enabled = visible
        if (!visible) {buildTimer.stop(); restoreColors()}
        else requestRefresh(true)
    }
    onSurfaceActiveChanged: {
        if (!started) return
        if (!surfaceActive) {refreshTimer.stop(); playbackTimer.stop(); buildTimer.stop()}
        else {followNativePosition(); requestRefresh(false)}
    }
    Component.onDestruction: {if (started) {undoPause = false; restoreColors(true)}}
    Timer {id:refreshTimer; interval:55; onTriggered:refreshSelection()}
    Timer {id:buildTimer; interval:10; repeat:true; onTriggered:buildChunk()}
    Timer {id:playbackTimer; interval:16; onTriggered:followNativePosition()}
    Timer {interval:!observer && playbackAvailable ? 80 : 400; running:started && surfaceActive; repeat:true; onTriggered:pollHost()}
    Connections {
        target:observer
        ignoreUnknownSignals:true
        onPositionChanged:{if(!playbackTimer.running)playbackTimer.start()}
        onScoreChanged:requestRefresh(true)
    }

    Rectangle {
        anchors.fill: parent
        color: "#F4F6F8"
        Flickable {
            id: flick
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: panel.height + 24
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            Column {
                id: panel
                x: 12
                y: 12
                width: Math.max(180, flick.width - 24)
                spacing: 10
                Column {
                    width: parent.width
                    spacing: 3
                    RowLayout {
                        width: parent.width
                        UiLabel {text:"和声助手"; Layout.fillWidth:true; color:ink; font.pixelSize:19; font.bold:true}
                        ToolButton {
                            font.family: "Microsoft YaHei UI";text:"键盘"; checked:keyboardExpanded; checkable:true; font.pixelSize:11; onClicked:keyboardExpanded=checked}
                        ToolButton {
                            font.family: "Microsoft YaHei UI";text:"设置"; checked:settingsExpanded; checkable:true; font.pixelSize:11; onClicked:settingsExpanded=checked}
                    }
                    UiLabel {width:parent.width; text:Harmony.rootNames[keyTonic.currentIndex]+(keyMode.currentIndex===0?" 大调":" 小调")+" · "+scopeText; color:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                }
                PanelCard {
                    width: parent.width
                    UiLabel {text:positionText; color:muted; font.pixelSize:11}
                    UiLabel {width:parent.width; text:chordText; color:ink; font.pixelSize:30; font.bold:true; wrapMode:Text.WrapAnywhere}
                    RowLayout {
                        width: parent.width
                        UiLabel {text:"级数"; color:muted; font.pixelSize:12}
                        UiLabel {Layout.fillWidth:true; text:degreeText; color:ink; font.pixelSize:21; font.bold:true; wrapMode:Text.WrapAnywhere}
                    }
                    UiLabel {width:parent.width; text:matchText; color:muted; font.pixelSize:12; wrapMode:Text.Wrap}
                    UiLabel {width:parent.width; visible:text.length>0; text:alternativesText; color:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                    Rectangle {width:parent.width; height:1; color:"#EBEEF0"}
                    UiLabel {width:parent.width; text:inversionText; color:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                    UiLabel {width:parent.width; visible:settingsExpanded; text:voicingText; color:ink; font.pixelSize:12; wrapMode:Text.Wrap}
                }
                PanelCard {
                    width: parent.width
                    UiLabel {text:"音程功能"; color:ink; font.bold:true; font.pixelSize:13}
                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: toneRows
                            delegate: Rectangle {
                                width: Math.max(64, (parent.width - (parent.width >= 290 ? 18 : 12)) / (parent.width >= 290 ? 4 : 3))
                                height: toneBody.implicitHeight + 12
                                radius: 6
                                color: modelData.present ? "#F2F5F7" : "#FAFBFC"
                                border.color: modelData.present ? modelData.color : "#E6E9ED"
                                Column {
                                    id:toneBody
                                    x:6; y:6; width:parent.width-12; spacing:4
                                    UiLabel {width:parent.width; text:modelData.label; color:modelData.color; font.pixelSize:14; font.bold:true; wrapMode:Text.Wrap}
                                    UiLabel {width:parent.width; text:modelData.notes; color:modelData.present?ink:muted; font.pixelSize:11; wrapMode:Text.Wrap}
                                }
                            }
                        }
                    }
                    UiLabel {
                        width:parent.width; color:muted; font.pixelSize:10; wrapMode:Text.Wrap
                        text:"有色框：实际发声 · 缺音：尚未发声 · —：未使用\n挂音显示 2 / 4，六和弦显示 6。"
                    }
                    Flow {
                        width:parent.width; spacing:5
                        Repeater {
                            model:currentNotes
                            delegate:Rectangle {
                                property string functionLabel:Harmony.labelFor(modelData.pitch,chordRoot,chordDefinition)
                                width:noteChip.implicitWidth+16; height:26; radius:4
                                color:Harmony.colorFor(modelData.pitch,chordRoot,chordDefinition)
                                UiLabel {id:noteChip; anchors.centerIn:parent; text:noteName(modelData)+" · "+functionLabel; color:"white"; font.pixelSize:11}
                            }
                        }
                    }
                }
                PanelCard {
                    width:parent.width
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
                    width:parent.width
                    visible:settingsExpanded
                    UiLabel {text:"识别与调性"; color:ink; font.bold:true; font.pixelSize:13}
                    RowLayout {
                        width:parent.width
                        UiLabel {text:"调性"; color:muted; font.pixelSize:12}
                        ComboBox {
                            font.family: "Microsoft YaHei UI";
                            id:keyTonic; Layout.fillWidth:true; Layout.minimumWidth:55; implicitHeight:32
                            model:Harmony.rootNames; font.pixelSize:12
                            onActivated:{manualKey=true; updateTexts()}
                        }
                        ComboBox {
                            font.family: "Microsoft YaHei UI";
                            id:keyMode; Layout.preferredWidth:80; implicitHeight:32
                            model:["大调","小调"]; font.pixelSize:12
                            onActivated:{if(!manualKey && currentTick>=0) initializeKey(currentTick); updateTexts()}
                        }
                    }
                    UiLabel {width:parent.width; text:manualKey?"调性由你指定":"主音取自当前位置调号；大 / 小调请自行确认"; color:muted; font.pixelSize:10; wrapMode:Text.Wrap}
                    RowLayout {
                        width:parent.width
                        Switch {
                            font.family: "Microsoft YaHei UI";id:autoChord; text:"自动识别"; checked:true; font.pixelSize:12; onToggled:analyze()}
                        Item {Layout.fillWidth:true}
                        Button {
                            font.family: "Microsoft YaHei UI";text:"读调号"; implicitHeight:32; font.pixelSize:11; onClicked:{manualKey=false; if(currentTick>=0)initializeKey(currentTick); updateTexts()}}
                    }
                    ComboBox {
                            font.family: "Microsoft YaHei UI";
                        id:scope; width:parent.width; implicitHeight:32; font.pixelSize:12
                        model:["当前乐器（钢琴双谱表）","全谱"]
                        onActivated:requestRefresh(true)
                    }
                    RowLayout {
                        width:parent.width
                        ComboBox {
                            font.family: "Microsoft YaHei UI";id:rootCombo; Layout.preferredWidth:76; implicitHeight:32; model:Harmony.rootNames; font.pixelSize:12; onActivated:{autoChord.checked=false; analyze()}}
                        ComboBox {
                            font.family: "Microsoft YaHei UI";id:qualityCombo; Layout.fillWidth:true; Layout.minimumWidth:80; implicitHeight:32; model:qualityNames; font.pixelSize:12; onActivated:{autoChord.checked=false; analyze()}}
                    }
                }
                PanelCard {
                    width:parent.width
                    Switch {
                            font.family: "Microsoft YaHei UI";
                        id:followPlayback; text:"跟随播放"; checked:true; enabled:playbackAvailable; font.pixelSize:12
                        visible:settingsExpanded && playbackAvailable
                        onToggled:{lastPlayTick=-1; requestRefresh(false)}
                    }
                    UiLabel {
                        width:parent.width; font.pixelSize:10; color:muted; wrapMode:Text.Wrap
                        visible:settingsExpanded || !playbackAvailable
                        text:observer?"跟随音序器发声音；选中状态与播放标记保持可见。":playbackAvailable?"读取真实播放位置；播放中仅更新面板。":"当前主程序没有提供播放位置接口，暂时跟随选区。"
                    }
                    Switch {
                            font.family: "Microsoft YaHei UI";
                        id:scoreColoring; text:"谱面临时配色"; checked:false; font.pixelSize:12
                        onToggled:{if(checked)undoPause=false; if(!internalChange)repaintNotes()}
                    }
                    UiLabel {
                        width:parent.width; font.pixelSize:10; color:muted; wrapMode:Text.Wrap
                        visible:scoreColoring.checked || settingsExpanded
                        text:observer?"仅屏幕预览，不改音符原色、撤销记录或保存 / 导出内容。":"开启会修改谱面颜色并产生撤销记录。保存 / 导出前请恢复原色；面板配色始终可用。"
                    }
                    RowLayout {
                        width:parent.width
                        Button {
                            font.family: "Microsoft YaHei UI";text:"恢复原色"; Layout.fillWidth:true; implicitHeight:32; font.pixelSize:12; onClicked:stopColoring()}
                        Button {
                            font.family: "Microsoft YaHei UI";text:"刷新"; Layout.fillWidth:true; implicitHeight:32; font.pixelSize:12; onClicked:requestRefresh(true)}
                    }
                    UiLabel {width:parent.width; visible:noticeText.length>0; text:noticeText; color:muted; font.pixelSize:10; wrapMode:Text.Wrap}
                }
            }
        }
    }
}
