import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import "Preferences.js" as Preferences

Column {
    id:editor
    property var configuration:Preferences.defaults()
    signal modified(var configuration)
    signal chooseColor(string key,string color)
    spacing:8
    function option(key,value) {
        var next=JSON.parse(JSON.stringify(configuration))
        next[key]=value
        modified(Preferences.clean(next))
    }
    UiLabel {text:"显示位置与谱面记号"; font.bold:true; color:"#343a3f"; font.pixelSize:13}
    Switch {text:"顶部摘要同时显示右侧详情"; checked:configuration.dualPanel; onToggled:if(checked!==configuration.dualPanel)editor.option("dualPanel",checked)}
    RowLayout {
        width:parent.width
        UiLabel {text:"顶栏位置"; color:"#697077"; font.pixelSize:11}
        ComboBox {model:["靠左","居中","靠右","自定位置"]; currentIndex:configuration.ribbonAlign; Layout.fillWidth:true; implicitHeight:32; onActivated:editor.option("ribbonAlign",currentIndex)}
    }
    RowLayout {
        width:parent.width; visible:configuration.ribbonAlign===3
        UiLabel {text:"横向位置 %"; Layout.fillWidth:true; color:"#697077"; font.pixelSize:11}
        SpinBox {from:0; to:100; value:configuration.ribbonPosition; editable:true; implicitHeight:32; onValueModified:editor.option("ribbonPosition",value)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"记号内容"; color:"#697077"; font.pixelSize:11}
        ComboBox {model:["仅和弦","仅级数","和弦与级数"]; currentIndex:configuration.chordContent; Layout.fillWidth:true; implicitHeight:32; onActivated:editor.option("chordContent",currentIndex)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"组合排列"; color:"#697077"; font.pixelSize:11}
        ComboBox {model:["和弦 → 级数","级数 → 和弦","和弦在上","级数在上"]; currentIndex:configuration.chordOrder; enabled:configuration.chordContent===2; Layout.fillWidth:true; implicitHeight:32; onActivated:editor.option("chordOrder",currentIndex)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"记号字体"; color:"#697077"; font.pixelSize:11}
        ComboBox {model:["跟随谱面和弦字体","Edwin","Arial"]; currentIndex:configuration.chordFont; Layout.fillWidth:true; implicitHeight:32; onActivated:editor.option("chordFont",currentIndex)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"记号大小 %"; Layout.fillWidth:true; color:"#697077"; font.pixelSize:11}
        SpinBox {from:60; to:200; stepSize:10; value:configuration.chordScale; editable:true; implicitHeight:32; onValueModified:editor.option("chordScale",value)}
    }
    Repeater {
        model:[{key:"chordColor",name:"记号颜色"},{key:"highlightColor",name:"高亮文字"},{key:"highlightBackground",name:"高亮背景"},{key:"chromaticColor",name:"离调强调"}]
        ColorOption {width:parent.width; caption:modelData.name; value:configuration[modelData.key]; onEdited:editor.option(modelData.key,color); onChooseRequested:editor.chooseColor(modelData.key,value)}
    }
    Switch {text:"优先保留原谱和弦 / 级数"; checked:configuration.respectExistingHarmony; onToggled:if(checked!==configuration.respectExistingHarmony)editor.option("respectExistingHarmony",checked)}
    Switch {text:"记号背景遮罩"; checked:configuration.chordMask; onToggled:if(checked!==configuration.chordMask)editor.option("chordMask",checked)}
    UiLabel {width:parent.width; text:"记号对齐和弦变化的时间位置；关闭遮罩后只显示文字，仍避让原谱，当前记号以文字色高亮。"; color:"#697077"; font.pixelSize:10; wrapMode:Text.Wrap}
    UiLabel {width:parent.width; text:"同位置已有原谱和弦时，分析留在面板，谱面只补充级数；已有罗马级数时只补充和弦。关闭可同时显示，仍会避让。编辑框始终优先。"; color:"#697077"; font.pixelSize:10; wrapMode:Text.Wrap}
    Switch {text:"强调含调外音的和弦"; checked:configuration.chromaticAccent; onToggled:if(checked!==configuration.chromaticAccent)editor.option("chromaticAccent",checked)}
    UiLabel {width:parent.width; text:"小调兼容属和弦及导音和弦；强调色表示模板含调外音，不代表唯一和声解释。记号在同一系统统一高度，过密时省略；双击可跳转面板。"; color:"#697077"; font.pixelSize:10; wrapMode:Text.Wrap}
}
