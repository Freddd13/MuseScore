import QtQuick 2.9
import QtQuick.Controls 2.2 as Controls

Controls.Button {
    id:control
    UiTheme {id:theme}
    implicitHeight:flat ? 26 : 28
    implicitWidth:Math.max(flat ? 34 : 56,contentItem.implicitWidth+18)
    padding:5
    font.family:theme.fontFamily
    font.pixelSize:11
    opacity:enabled ? 1 : 0.45
    contentItem:Text {
        text:control.text;font:control.font;elide:Text.ElideRight
        horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter
        color:control.checked ? theme.accentText : theme.text
    }
    background:Rectangle {
        radius:2
        color:control.checked ? theme.accent : control.down || control.hovered ? theme.hover : control.flat ? "transparent" : theme.section
        border.width:control.flat && !control.hovered && !control.activeFocus ? 0 : 1
        border.color:control.activeFocus ? theme.accent : theme.line
    }
}
