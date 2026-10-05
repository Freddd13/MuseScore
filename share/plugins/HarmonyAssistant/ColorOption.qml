import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

RowLayout {
    id:row
    property string caption
    property string value
    signal edited(string color)
    signal chooseRequested()
    spacing:6
    Rectangle {
        width:24; height:24; radius:4; color:row.value; border.color:"#c6c6c6"
        MouseArea {anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:row.chooseRequested()}
    }
    UiLabel {text:row.caption; Layout.preferredWidth:58; color:"#343a3f"; font.pixelSize:11}
    TextField {text:row.value; Layout.fillWidth:true; Layout.minimumWidth:64; implicitHeight:32; font.pixelSize:11; maximumLength:7; onEditingFinished:row.edited(text)}
    Button {text:"选色"; implicitHeight:32; implicitWidth:48; font.pixelSize:11; onClicked:row.chooseRequested()}
}
