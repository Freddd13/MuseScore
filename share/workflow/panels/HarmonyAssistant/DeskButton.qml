import QtQuick 2.9
import QtQuick.Controls 2.3 as Controls

Controls.Button {
    id:control
    property string iconName:""
    readonly property color glyphColor:control.iconName==="record" ? "#c54c50" : control.checked ? theme.accentText : theme.text
    UiTheme {id:theme}
    implicitHeight:flat ? 26 : 28
    implicitWidth:Math.max(flat ? 34 : 56,contentItem.implicitWidth+18)
    padding:5
    font.family:theme.fontFamily
    font.pixelSize:11
    opacity:enabled ? 1 : 0.45
    palette.buttonText:control.checked ? theme.accentText : theme.text
    icon.width:16
    icon.height:16
    icon.color:glyphColor
    icon.source:control.iconName ? "image://kumo-tools/"+control.iconName+"/"+String(glyphColor).replace("#","") : ""
    background:Rectangle {
        radius:2
        color:control.checked ? theme.accent : control.down || control.hovered ? theme.hover : control.flat ? "transparent" : theme.section
        border.width:control.flat && !control.hovered && !control.activeFocus ? 0 : 1
        border.color:control.activeFocus ? theme.accent : theme.line
    }
}
