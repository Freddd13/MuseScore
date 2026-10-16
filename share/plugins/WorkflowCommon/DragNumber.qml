import QtQuick 2.9
import QtQuick.Controls 2.2
import "../HarmonyAssistant" as Desk

FocusScope {
    id:control
    property real value:0
    property real minimum:0
    property real maximum:100
    property real step:1
    property real sensitivity:1
    property string caption:""
    property string suffix:""
    property bool rangeIndicator:false
    property var formatter:function(v){return String(Math.round(v))}
    property var parser:function(t){return Number(t)}
    property bool editing:false
    property var hintHost:null
    signal edited(real value)
    signal editingStarted()
    signal editingFinished()
    implicitWidth:96
    implicitHeight:rangeIndicator?36:28
    Desk.UiTheme {id:theme}
    function setValue(v){if(isFinite(v))edited(Math.max(minimum,Math.min(maximum,Math.round(v/step)*step)))}
    function beginEdit(){field.text=formatter(value);editing=true;field.forceActiveFocus();field.selectAll()}
    function commit(){if(!editing)return;var next=parser(field.text);editing=false;setValue(next);editingFinished();control.forceActiveFocus()}
    Rectangle {anchors.fill:parent;color:theme.field;border.color:control.activeFocus?theme.accent:theme.line;radius:2}
    Text {anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;height:control.rangeIndicator?25:parent.height;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;text:control.caption+control.formatter(control.value)+control.suffix;color:theme.text;font.family:theme.fontFamily;font.pixelSize:11;visible:!control.editing;elide:Text.ElideRight}
    Rectangle {visible:control.rangeIndicator;x:18;y:parent.height-7;width:parent.width-40;height:2;color:theme.line
        Rectangle {height:2;width:parent.width*Math.max(0,Math.min(1,(control.value-control.minimum)/(control.maximum-control.minimum||1)));color:theme.accent}
    }
    Text {visible:control.rangeIndicator;x:4;y:parent.height-13;text:control.minimum;font.pixelSize:8;color:theme.muted}
    Text {visible:control.rangeIndicator;anchors.right:parent.right;anchors.rightMargin:4;y:parent.height-13;text:control.maximum;font.pixelSize:8;color:theme.muted}
    TextField {id:field;visible:control.editing;anchors.fill:parent;selectByMouse:true;horizontalAlignment:Text.AlignHCenter;font.family:theme.fontFamily;font.pixelSize:11;onAccepted:control.commit();onEditingFinished:control.commit();Keys.onEscapePressed:{control.editing=false;control.editingFinished();control.forceActiveFocus()}}
    MouseArea {
        id:mouse;anchors.fill:parent;preventStealing:true;enabled:!control.editing;hoverEnabled:true;cursorShape:Qt.SizeHorCursor
        property real downX:0
        property real downValue:0
        property bool moved:false
        onPressed:{hintTimer.stop();if(control.hintHost)control.hintHost.showPanelHint("",control);control.editingStarted();control.forceActiveFocus();downX=mouse.x;downValue=control.value;moved=false}
        onPositionChanged:if(pressed && (moved||Math.abs(mouse.x-downX)>3)){moved=true;control.setValue(downValue+(mouse.x-downX)*control.sensitivity*(mouse.modifiers&Qt.ShiftModifier?.1:1))}
        onReleased:{if(!moved)control.beginEdit();else control.editingFinished()}
        onEntered:hintTimer.restart()
        onExited:{hintTimer.stop();if(control.hintHost)control.hintHost.showPanelHint("",control)}
        Timer {id:hintTimer;interval:700;onTriggered:if(control.hintHost)control.hintHost.showPanelHint("左右拖动调整，按住 Shift 可微调；单击可输入数值。",control)}
        ToolTip.visible:!control.hintHost&&containsMouse&&!pressed;ToolTip.delay:700;ToolTip.text:"左右拖动调整，按住 Shift 可微调；单击可输入数值。"
    }
    Keys.onUpPressed:setValue(value+step)
    Keys.onDownPressed:setValue(value-step)
    Keys.onReturnPressed:beginEdit()
}
