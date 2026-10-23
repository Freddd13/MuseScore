import QtQuick 2.9
import QtQuick.Controls 2.2
import "../HarmonyAssistant" as Desk

Item {
    id: control
    property var workflow
    property string selectorName: "expressionPassageChoice"
    property var passage: workflow.result.passage
    property var choices: passage && passage.candidates ? passage.candidates : []
    implicitWidth: 360
    implicitHeight: 28
    visible: choices.length > 1
    Desk.UiTheme { id: theme }
    Label {
        id: caption
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: "演奏片段"
        color: theme.muted
        font: Qt.application.font
    }
    Desk.DeskComboBox {
        objectName: control.selectorName
        anchors.left: caption.right
        anchors.leftMargin: 6
        anchors.right: parent.right
        height: 28
        model: control.choices.map(function(v, i) { return (i + 1) + " · " + v.from.toFixed(2) + "–" + v.until.toFixed(2) + " 秒" })
        currentIndex: control.passage && control.passage.ambiguous && !control.passage.chosen ? -1 : control.passage && control.passage.chosen ? control.passage.chosenIndex : 0
        displayText: currentIndex < 0 ? "选择本次演奏的时间位置" : currentText
        enabled: !workflow.busy && !workflow.stale && !workflow.captureState.recording
        onActivated: workflow.choosePassage(currentIndex)
    }
}
