import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

RowLayout {
    id: root
    property string caption: ""
    property string suffix: "%"
    property real value: 0
    property real minimum: 0
    property real maximum: 100
    property real step: 1
    property int decimals: 0
    signal edited(real newValue)
    signal editingFinished()
    spacing: 5
    implicitWidth: 222
    implicitHeight: Math.max(28, number.implicitHeight)
    Label { text: root.caption; Layout.preferredWidth: 40; font: Qt.application.font }
    Slider {
        id: slider
        objectName: root.objectName + "Slider"
        Layout.fillWidth: true
        Layout.minimumWidth: 48
        from: root.minimum; to: root.maximum; value: root.value; stepSize: root.step
        Accessible.name: root.caption
        onMoved: root.edited(value)
        onPressedChanged: if (!pressed) root.editingFinished()
        ToolTip.visible: hovered || pressed
        ToolTip.text: value.toFixed(root.decimals) + root.suffix
    }
    TextField {
        id: number
        objectName: root.objectName + "Value"
        Layout.preferredWidth: 76
        horizontalAlignment: Text.AlignRight
        rightPadding: root.suffix.length ? 22 : 8
        text: root.value.toLocaleString(Qt.locale(), 'f', root.decimals)
        font: Qt.application.font
        validator: DoubleValidator { bottom: root.minimum; top: root.maximum; decimals: root.decimals; locale: Qt.locale().name }
        onEditingFinished: {
            var parsed = Number.fromLocaleString(Qt.locale(), text)
            if (isFinite(parsed)) root.edited(Math.max(root.minimum, Math.min(root.maximum, parsed)))
            text = root.value.toLocaleString(Qt.locale(), 'f', root.decimals)
            root.editingFinished()
        }
        Label { anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; text: root.suffix; font: number.font }
        Accessible.name: root.caption + "数值"
    }
}
