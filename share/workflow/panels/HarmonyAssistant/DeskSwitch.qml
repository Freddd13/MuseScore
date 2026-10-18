import QtQuick 2.9
import QtQuick.Controls 2.2 as Controls

Controls.Switch {
    id:control
    UiTheme {id:theme}
    implicitHeight:28
    spacing:8;padding:2;leftPadding:2;rightPadding:2
    font.family:theme.fontFamily;font.pixelSize:12
    opacity:enabled ? 1 : 0.45
    indicator:Rectangle {
        x:control.leftPadding;y:(control.height-height)/2;width:15;height:15;radius:2
        color:control.checked ? theme.accent : theme.field
        border.color:control.activeFocus ? theme.accent : theme.line
        Text {anchors.centerIn:parent;text:control.checked ? "✓" : "";color:theme.accentText;font.pixelSize:12}
    }
    contentItem:Text {
        text:control.text;font:control.font;color:theme.text
        leftPadding:control.indicator.width+control.spacing
        verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight
    }
}
