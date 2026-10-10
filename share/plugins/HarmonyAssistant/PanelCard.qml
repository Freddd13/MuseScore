import QtQuick 2.9

Rectangle {
    default property alias content: body.data
    property alias body: body
    property real minimumBodyHeight: 0
    UiTheme {id:theme}
    color: theme.field
    radius: 2
    border.color: theme.subtleLine
    implicitHeight: Math.max(minimumBodyHeight, body.implicitHeight) + 20
    Column {
        id: body
        x: 10
        y: 10
        width: parent.width - 20
        spacing: 6
    }
}
