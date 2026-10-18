import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import "../HarmonyAssistant" as Desk

ColumnLayout {
    id: bar
    property var workflow
    spacing: 3
    Desk.UiTheme { id: theme }
    RowLayout {
        Layout.fillWidth: true
        Label { text: "目标谱段"; color: theme.muted; font: Qt.application.font }
        Label {
            Layout.fillWidth: true
            text: bar.workflow.targetDescription
            elide: Text.ElideRight
            color: theme.text
            font: Qt.application.font
            ToolTip.visible: targetHover.containsMouse
            ToolTip.text: text
            MouseArea { id: targetHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
        Desk.DeskButton { objectName: "expressionTargetSelection"; text: "当前选区"; enabled: !bar.workflow.busy; onClicked: bar.workflow.chooseTarget(false) }
        Desk.DeskButton { objectName: "expressionTargetWhole"; text: "全谱"; enabled: !bar.workflow.busy; onClicked: bar.workflow.chooseTarget(true) }
    }
    RowLayout {
        Layout.fillWidth: true
        CheckBox {
            id: input
            objectName: "expressionMidiInput"
            text: "MIDI 输入"
            checked: !!bar.workflow.midiInputInfo.enabled
            enabled: !!bar.workflow.captureSession && typeof bar.workflow.captureSession.setInputEnabled === "function"
            font: Qt.application.font
            contentItem: Label { text: input.text; font: input.font; color: theme.text; verticalAlignment: Text.AlignVCenter; leftPadding: input.indicator.width + input.spacing }
            onClicked: bar.workflow.captureSession.setInputEnabled(checked)
        }
        Label {
            Layout.fillWidth: true
            text: bar.workflow.midiInputInfo.ready ? (bar.workflow.midiInputInfo.device || "当前键盘已连接") : "在偏好设置的输入／输出中选择键盘"
            elide: Text.ElideRight
            color: theme.muted
            font: Qt.application.font
        }
    }
    Label {
        Layout.fillWidth: true
        visible: !!bar.workflow.captureState.recording
        text: bar.workflow.captureState.waitingForFirstNote ? "录制已开始，等待第一个音…" :
              "已录 " + bar.workflow.captureState.count + " 个音 · " + (bar.workflow.captureState.seconds || 0).toFixed(1) + " 秒" +
              (bar.workflow.captureState.position ? " · " + (bar.workflow.captureState.positionUncertain ? "可能在" : "正在对应") + bar.workflow.captureState.position : "")
        color: theme.accent
        font: Qt.application.font
        elide: Text.ElideRight
    }
}
