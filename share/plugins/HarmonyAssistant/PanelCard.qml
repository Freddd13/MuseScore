import QtQuick 2.9

Rectangle {
    default property alias content: body.data
    property alias body: body
    property real minimumBodyHeight: 0
    property real fixedBodyHeight:0
    property string heading:""
    property bool collapsed:false
    signal toggleRequested()
    UiTheme {id:theme}
    color: theme.field
    radius: 2
    border.color: theme.subtleLine
    implicitHeight:(heading.length?30:0)+(collapsed?0:(fixedBodyHeight>0?fixedBodyHeight:Math.max(minimumBodyHeight, body.implicitHeight))+20)
    Item {width:parent.width;height:30;visible:heading.length>0
        UiLabel {anchors.left:parent.left;anchors.leftMargin:10;anchors.verticalCenter:parent.verticalCenter;text:(collapsed?"▸ ":"▾ ")+heading;color:theme.text;font.bold:true}
        MouseArea {anchors.fill:parent;onClicked:toggleRequested()}
    }
    Column {
        id: body
        x: 10
        y:(parent.heading.length?30:0)+10
        visible:!parent.collapsed
        width: parent.width - 20
        height:parent.fixedBodyHeight>0?parent.fixedBodyHeight:implicitHeight
        clip:parent.fixedBodyHeight>0
        spacing: 6
    }
}
