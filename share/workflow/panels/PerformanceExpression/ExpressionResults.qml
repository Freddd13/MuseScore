import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import "../HarmonyAssistant" as Desk
import "ExpressionRows.js" as Rows

ColumnLayout {
    id: panel
    property var workflow
    // A preview drag updates visible values without rebuilding the list or
    // resetting the user's scroll position.
    property var rows: Rows.build(workflow.result, workflow.appliedProgress, null, filter.currentIndex)
    property var previewValues: workflow.candidate ? workflow.candidate.velocityById || ({}) : ({})
    property var kept: Rows.keptReasons(workflow.candidate)
    spacing: 5
    Desk.UiTheme { id: theme }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Label { text: "对应结果"; height: 28; verticalAlignment: Text.AlignVCenter; color: theme.text; font: Qt.application.font }
        Desk.DeskComboBox {
            id: filter
            objectName: "expressionResultFilter"
            width: 148
            model: ["待使用", "需核对", "漏弹", workflow.role > 0 ? "其它演奏音" : "多弹", "已使用"]
            onActivated: workflow.showApplied = currentIndex === 4
        }
        Label { text: panel.rows.length + " 项"; height: 28; verticalAlignment: Text.AlignVCenter; color: theme.muted; font: Qt.application.font }
        Desk.DeskButton {
            objectName: "expressionResultEditor"
            text: "查看对应"
            enabled: workflow.result.matches.length > 0 || workflow.result.missing.length > 0 || workflow.result.extra.length > 0
            onClicked: { workflow.publishComparison(); workflow.observer.showEditorCorrespondence() }
        }
        Desk.DeskButton { text: "确认这对音"; visible: filter.currentIndex === 1; enabled: workflow.selected >= 0 && !workflow.busy; onClicked: workflow.confirm() }
    }
    Connections {
        target: workflow
        function onShowAppliedChanged() {
            if (workflow.showApplied) filter.currentIndex = 4
            else if (filter.currentIndex === 4) filter.currentIndex = 0
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitHeight: 180
        color: theme.field
        border.color: theme.line
        ListView {
            id: list
            objectName: "embeddedExpressionResults"
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: panel.rows
            ScrollBar.vertical: ScrollBar { id: resultScroll; policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            delegate: ItemDelegate {
                id: resultRow
                property var proposed: panel.previewValues[modelData.note.id]
                property var newVelocity: proposed ? proposed.target : null
                objectName: "expressionResultRow"
                width: Math.max(0, list.width - (resultScroll.visible ? resultScroll.width : 0))
                height: 50
                padding: 6
                highlighted: modelData.kind === "match" && modelData.index === workflow.selected
                background: Rectangle { color: parent.highlighted ? theme.selected : parent.hovered ? theme.hover : "transparent" }
                contentItem: ColumnLayout {
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: modelData.pitch; Layout.preferredWidth: 48; color: theme.text; font.family: Qt.application.font.family; font.pointSize: Qt.application.font.pointSize; font.bold: true }
                        Label { text: modelData.place; Layout.fillWidth: true; elide: Text.ElideRight; color: theme.text; font: Qt.application.font }
                        Label {
                            visible: modelData.kind === "match"
                            text: modelData.kind !== "match" || modelData.original === null ? "" : resultRow.newVelocity === null ? "力度 " + modelData.original : modelData.original + " → " + resultRow.newVelocity
                            color: resultRow.newVelocity === null ? theme.muted : theme.text
                            font.family: Qt.application.font.family
                            font.pointSize: Qt.application.font.pointSize
                            font.bold: true
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: (modelData.applied || modelData.pending ? modelData.detail : panel.kept[modelData.note.id] || modelData.detail) + (modelData.note.track === undefined ? "" : " · 谱表 " + (Math.floor(modelData.note.track / 4) + 1) + "／声部 " + (modelData.note.track % 4 + 1))
                        elide: Text.ElideRight
                        color: theme.muted
                        font: Qt.application.font
                    }
                }
                onClicked: workflow.selectResultRow(modelData)
            }
        }
        Label {
            anchors.centerIn: parent
            width: Math.max(80, parent.width - 24)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: theme.muted
            font: Qt.application.font
            visible: panel.rows.length === 0
            text: workflow.busy ? workflow.status : !workflow.midi ? "导入 MIDI 或录制一遍后，这里显示对应的音和力度变化。" : filter.currentIndex === 0 && !workflow.result.matches.length ? "当前还没有逐音对应。可从“漏弹／多弹”查看位置，或点“重新对应”继续调整。" : filter.currentIndex === 0 ? "这部分结果已处理。可切到“已使用”查看，或继续处理其它位置。" : "这一类目前有 0 项。"
        }
    }
}
