import QtQuick 2.9

Rectangle {
    default property alias content: body.data
    property alias body: body
    color: "#FFFFFF"
    radius: 10
    border.color: "#E1E5E8"
    implicitHeight: body.implicitHeight + 24
    Column {
        id: body
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8
    }
}
