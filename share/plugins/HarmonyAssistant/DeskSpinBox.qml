import QtQuick 2.9
import QtQuick.Controls 2.2 as Controls

Controls.SpinBox {
    id:control
    UiTheme {id:theme}
    implicitHeight:28;implicitWidth:92
    leftPadding:6;rightPadding:23
    font.family:theme.fontFamily;font.pixelSize:11
    opacity:enabled ? 1 : 0.45
    contentItem:TextInput {
        text:control.textFromValue(control.value,control.locale)
        font:control.font;color:theme.text;selectionColor:theme.accent;selectedTextColor:theme.accentText
        horizontalAlignment:Text.AlignLeft;verticalAlignment:Text.AlignVCenter
        readOnly:!control.editable;validator:control.validator;inputMethodHints:Qt.ImhFormattedNumbersOnly
    }
    up.indicator:Rectangle {
        x:control.width-width;y:0;width:20;height:control.height/2
        color:control.up.pressed ? theme.selected : control.up.hovered ? theme.hover : theme.section
        Text {anchors.centerIn:parent;text:"+";color:theme.text;font.pixelSize:10}
    }
    down.indicator:Rectangle {
        x:control.width-width;y:control.height/2;width:20;height:control.height/2
        color:control.down.pressed ? theme.selected : control.down.hovered ? theme.hover : theme.section
        Text {anchors.centerIn:parent;text:"−";color:theme.text;font.pixelSize:10}
    }
    background:Rectangle {color:theme.field;radius:2;border.color:control.activeFocus ? theme.accent : theme.line}
}
