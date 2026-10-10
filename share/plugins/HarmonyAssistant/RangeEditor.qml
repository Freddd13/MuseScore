import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id:editor
    UiTheme {id:theme}
    objectName:"harmonyRangeEditor"
    property var region:null
    property var regions:[]
    property int tick:0
    property int scoreEnd:2147483647
    property bool canUndo:false
    property bool canRedo:false
    property var qualities:[]
    property var roots:[]
    property var rootPcs:[]
    signal action(string command,int start,int end,int rootIndex,int qualityIndex,int bassIndex)
    onRegionChanged:if(region){root.currentIndex=Math.max(0,editor.rootPcs.indexOf(region.root));quality.currentIndex=Math.max(0,region.definition)}
    spacing:8
    UiLabel {text:"和声范围编辑";font.bold:true;color:theme.text;font.pixelSize:13}
    UiLabel {width:parent.width;text:region ? region.chord+" · ticks "+region.start+"–"+region.end : "选择一个记号或谱面位置";wrapMode:Text.Wrap;color:theme.muted;font.pixelSize:11}
    DeskSwitch {id:editing;objectName:"harmonyRangeEditing";text:"显示可拖动的范围手柄"}
    Rectangle {
        id:strip;width:parent.width;height:70;visible:editing.checked;color:theme.section;radius:4
        property int leftTick:region?Math.max(0,region.start-480):Math.max(0,tick-480)
        property int rightTick:region?Math.min(scoreEnd,region.end+480):Math.min(scoreEnd,tick+1920)
        function xFor(value){return 12+(width-24)*(value-leftTick)/Math.max(1,rightTick-leftTick)}
        function tickFor(x){return Math.max(0,Math.min(scoreEnd,Math.round((leftTick+(x-12)/(width-24)*(rightTick-leftTick))/60)*60))}
        Repeater {
            model:editing.checked ? editor.regions.filter(function(n){return n.end>strip.leftTick && n.start<strip.rightTick}).slice(0,128) : []
            Rectangle {visible:modelData.end>strip.leftTick && modelData.start<strip.rightTick
                x:strip.xFor(Math.max(modelData.start,strip.leftTick));y:20;height:30
                width:Math.max(1,strip.xFor(Math.min(modelData.end,strip.rightTick))-x-2)
                color:editor.region && modelData.id===editor.region.id?theme.selected:theme.section
                UiLabel {anchors.centerIn:parent;width:parent.width-4;text:modelData.chord;elide:Text.ElideRight;horizontalAlignment:Text.AlignHCenter;color:theme.text;font.pixelSize:11}
                MouseArea {anchors.fill:parent;onClicked:editor.action("select",modelData.start,modelData.end,0,0,0)}
            }
        }
        Repeater {
            model:editor.region?2:0
            Rectangle {id:handle;width:12;height:48;y:10;radius:3;color:theme.accent
                objectName:index===0?"harmonyRangeStart":"harmonyRangeEnd"
                x:strip.xFor(index===0?editor.region.start:editor.region.end)-width/2
                MouseArea {anchors.fill:parent;drag.target:handle;drag.axis:Drag.XAxis;drag.minimumX:0;drag.maximumX:strip.width-handle.width
                    onReleased:{var value=strip.tickFor(handle.x+handle.width/2);editor.action("range",index===0?value:editor.region.start,index===0?editor.region.end:value,0,0,0)}}
            }
        }
    }
    RowLayout {width:parent.width
        UiLabel {text:"起点 tick";font.pixelSize:11;color:theme.muted}
        DeskSpinBox {id:start;Layout.fillWidth:true;from:0;to:Math.max(0,editor.scoreEnd-1);value:editor.region?editor.region.start:Math.max(0,editor.tick);stepSize:60;editable:true}
    }
    RowLayout {width:parent.width
        UiLabel {text:"终点 tick";font.pixelSize:11;color:theme.muted}
        DeskSpinBox {id:end;Layout.fillWidth:true;from:1;to:editor.scoreEnd;value:editor.region?editor.region.end:Math.min(editor.scoreEnd,Math.max(0,editor.tick)+480);stepSize:60;editable:true}
    }
    RowLayout {width:parent.width
        DeskComboBox {id:root;model:editor.roots;Layout.preferredWidth:70}
        DeskComboBox {id:quality;model:editor.qualities;Layout.fillWidth:true}
    }
    RowLayout {width:parent.width
        UiLabel {text:"低音";color:theme.muted;font.pixelSize:11}
        DeskComboBox {id:bass;model:["实际低音"].concat(editor.roots);Layout.fillWidth:true}
    }
    Flow {width:parent.width;spacing:5
        DeskButton {text:"应用范围";enabled:!!editor.region;onClicked:editor.action("range",start.value,end.value,root.currentIndex,quality.currentIndex,0)}
        DeskButton {text:"指定和弦";onClicked:editor.action("assign",start.value,end.value,root.currentIndex,quality.currentIndex,bass.currentIndex)}
        DeskButton {text:"在当前位置拆分";enabled:!!editor.region && editor.tick>editor.region.start && editor.tick<editor.region.end;onClicked:editor.action("split",editor.tick,end.value,0,0,0)}
        DeskButton {text:"合并前一段";enabled:!!editor.region;onClicked:editor.action("merge",start.value,end.value,0,0,0)}
        DeskButton {text:"排除此范围";onClicked:editor.action("suppress",start.value,end.value,0,0,0)}
        DeskButton {text:"恢复自动";onClicked:editor.action("restore",start.value,end.value,0,0,0)}
        DeskButton {text:"撤销修正";enabled:editor.canUndo;onClicked:editor.action("undo",0,0,0,0,0)}
        DeskButton {text:"重做修正";enabled:editor.canRedo;onClicked:editor.action("redo",0,0,0,0,0)}
    }
    UiLabel {width:parent.width;text:"手柄吸附至 1/8 拍；数值输入可精确到 tick。只改分析范围，原谱音符和原生撤销不变。删除识别边界请用合并。";wrapMode:Text.Wrap;color:theme.muted;font.pixelSize:10}
}
