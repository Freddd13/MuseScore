import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

RowLayout {
    id:row
    UiTheme {id:theme}
    property string caption
    property string value
    signal edited(string color)
    signal chooseRequested()
    spacing:6
    Rectangle {
        width:24; height:24; radius:4; color:row.value; border.color:theme.line
        MouseArea {anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:row.chooseRequested()}
    }
    UiLabel {text:row.caption; Layout.preferredWidth:58; color:theme.text; font.pixelSize:11}
    DeskTextField {text:row.value; Layout.fillWidth:true; Layout.minimumWidth:64; implicitHeight:28; font.pixelSize:11; maximumLength:7; onEditingFinished:row.edited(text)}
    DeskButton {text:"选色"; implicitHeight:28; implicitWidth:48; font.pixelSize:11; onClicked:row.chooseRequested()}
}
