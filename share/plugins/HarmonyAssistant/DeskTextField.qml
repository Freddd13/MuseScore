import QtQuick 2.9
import QtQuick.Controls 2.2 as Controls

Controls.TextField {
    id:control
    UiTheme {id:theme}
    implicitHeight:28
    font.family:theme.fontFamily;font.pixelSize:11
    color:theme.text;selectionColor:theme.accent;selectedTextColor:theme.accentText
    placeholderTextColor:theme.muted
    leftPadding:6;rightPadding:6
    opacity:enabled ? 1 : 0.45
    background:Rectangle {color:theme.field;radius:2;border.color:control.activeFocus ? theme.accent : theme.line}
}
