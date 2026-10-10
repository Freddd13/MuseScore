import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

ColumnLayout {
    id:panel
    property var regions:[]
    property var unplaced:({})
    property int tick:-1
    signal activated(int tick,int part)
    UiTheme {id:theme}
    spacing:6
    UiLabel {Layout.fillWidth:true;text:"识别点 · 当前乐器";font.bold:true;font.pixelSize:13}
    UiLabel {Layout.fillWidth:true;text:"查看完整结果；点击定位并高亮。";color:theme.muted;font.pixelSize:11}
    ListView {
        id:list;objectName:"harmonyMarkerList"
        Layout.fillWidth:true;Layout.fillHeight:true;clip:true
        model:panel.regions
        ScrollBar.vertical:ScrollBar {}
        delegate:Rectangle {
            width:list.width;height:58
            property bool current:modelData.start<=panel.tick && panel.tick<modelData.end
            property bool blocked:!!panel.unplaced[modelData.part+":"+modelData.start]
            color:current ? theme.selected : theme.field
            Rectangle {width:parent.width;height:1;anchors.bottom:parent.bottom;color:theme.subtleLine}
            Rectangle {width:2;height:parent.height;color:current ? theme.accent : "transparent"}
            Column {
                x:8;y:7;width:parent.width-16;spacing:4
                RowLayout {
                    width:parent.width
                    UiLabel {Layout.fillWidth:true;text:modelData.chord+"  "+modelData.degree;font.pixelSize:15;font.bold:true;elide:Text.ElideRight}
                    UiLabel {text:blocked ? "待排" : modelData.source==="manual" ? "修正" : "";color:blocked ? "#b56b24" : theme.muted;font.pixelSize:10}
                }
                UiLabel {text:(modelData.bar ? "第 "+modelData.bar+" 小节 · " : "")+modelData.start+"–"+modelData.end+" tick";color:theme.muted;font.pixelSize:10}
            }
            MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:panel.activated(modelData.start,modelData.part)}
        }
        UiLabel {anchors.centerIn:parent;visible:!list.count;text:"暂无识别点";color:theme.muted}
    }
    UiLabel {Layout.fillWidth:true;text:"待排表示谱面仍无安全空间，结果不会丢失。持续的同一和弦只在起点标记；范围可在设置中拆分。";wrapMode:Text.Wrap;color:theme.muted;font.pixelSize:10}
}
