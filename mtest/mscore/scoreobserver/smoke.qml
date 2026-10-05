import QtQuick 2.9
import MuseScore 3.0
import FileIO 3.0

MuseScore {
    menuPath: "Plugins.Harmony native smoke"
    version: "1.0.0"
    requiresScore: true
    FileIO {id: report}
    onRun: {
        var result = {failures:[], api:false}
        try {
            var component = Qt.createComponent("../../../share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml")
            if (component.status !== Component.Ready) throw new Error(component.errorString())
            var panel = component.createObject(root)
            if (!panel) throw new Error(component.errorString())
            var cursor = curScore.newCursor()
            cursor.track = 0
            cursor.rewindToTick(480)
            var selected = cursor.segment.elementAt(0).notes[0]
            curScore.selection.select(selected)
            panel.run()
            panel.refreshSelection()
            result.api = panel.nativeAvailable
            result.chord = panel.chordText
            result.notes = panel.currentNotes.length
            result.indexBuilds = panel.observer.indexBuildCount
            result.indexMilliseconds = panel.observer.indexBuildMilliseconds
            if (!result.api) result.failures.push("observer missing")
            if (result.chord !== "Cmaj13") result.failures.push("chord: " + result.chord)
            if (result.notes !== 6) result.failures.push("sustained note count")
            var before = String(selected.color)
            panel.repaintNotes()
            if (String(selected.color) !== before) result.failures.push("preview wrote color")
            var start = Date.now()
            for (var i = 0; i < 1000; ++i) panel.observer.snapshot(480 + i % 400, 0, 8, false)
            result.thousandSnapshotsMilliseconds = Date.now() - start
            if (panel.observer.indexBuildCount !== result.indexBuilds) result.failures.push("cache rebuilt during queries")
            panel.displayTick(960, true)
            if (panel.currentNotes.length) result.failures.push("rest did not clear")
            panel.destroy()
        } catch (error) { result.failures.push(String(error)) }
        report.source = Qt.resolvedUrl("native-smoke.json").toString().replace(/^file:\/\/\//, "")
        report.write(JSON.stringify(result, null, 2))
        console.log("HARMONY_NATIVE_SMOKE " + JSON.stringify(result))
    }
    id: root
}
