import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import "Preferences.js" as Preferences

Column {
    id:editor
    UiTheme {id:theme}
    property var configuration:Preferences.defaults()
    signal modified(var configuration)
    signal chooseColor(string key,string color)
    spacing:8
    function option(key,value) {
        var next=JSON.parse(JSON.stringify(configuration))
        next[key]=value
        modified(Preferences.clean(next))
    }
    UiLabel {text:"显示位置与谱面记号"; font.bold:true; color:theme.text; font.pixelSize:13}
    RowLayout {width:parent.width
        UiLabel {text:"面板显示结果";color:theme.muted;font.pixelSize:11}
        DeskComboBox {model:["实时结果","所属和弦"];currentIndex:configuration.resultMode;Layout.fillWidth:true;onActivated:editor.option("resultMode",currentIndex)}
    }
    DeskSwitch {text:"同时显示顶部摘要与右侧详情"; checked:configuration.dualPanel; onToggled:if(checked!==configuration.dualPanel)editor.option("dualPanel",checked)}
    RowLayout {
        width:parent.width
        UiLabel {text:"顶栏位置"; color:theme.muted; font.pixelSize:11}
        DeskComboBox {model:["靠左","居中","靠右","自定位置"]; currentIndex:configuration.ribbonAlign; Layout.fillWidth:true; implicitHeight:28; onActivated:editor.option("ribbonAlign",currentIndex)}
    }
    RowLayout {
        width:parent.width; visible:configuration.ribbonAlign===3
        UiLabel {text:"横向位置 %"; Layout.fillWidth:true; color:theme.muted; font.pixelSize:11}
        DeskSpinBox {from:0; to:100; value:configuration.ribbonPosition; editable:true; implicitHeight:28; onValueModified:editor.option("ribbonPosition",value)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"记号内容"; color:theme.muted; font.pixelSize:11}
        DeskComboBox {model:["仅和弦","仅级数","和弦与级数"]; currentIndex:configuration.chordContent; Layout.fillWidth:true; implicitHeight:28; onActivated:editor.option("chordContent",currentIndex)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"组合排列"; color:theme.muted; font.pixelSize:11}
        DeskComboBox {model:["和弦 → 级数","级数 → 和弦","和弦在上","级数在上"]; currentIndex:configuration.chordOrder; enabled:configuration.chordContent===2; Layout.fillWidth:true; implicitHeight:28; onActivated:editor.option("chordOrder",currentIndex)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"记号字体"; color:theme.muted; font.pixelSize:11}
        DeskComboBox {model:["跟随谱面和弦字体","Edwin","Arial"]; currentIndex:configuration.chordFont; Layout.fillWidth:true; implicitHeight:28; onActivated:editor.option("chordFont",currentIndex)}
    }
    RowLayout {
        width:parent.width
        UiLabel {text:"记号大小 %"; Layout.fillWidth:true; color:theme.muted; font.pixelSize:11}
        DeskSpinBox {from:60; to:200; stepSize:10; value:configuration.chordScale; editable:true; implicitHeight:28; onValueModified:editor.option("chordScale",value)}
        DeskButton {text:"100%";implicitHeight:28;onClicked:editor.option("chordScale",100);ToolTip.visible:hovered;ToolTip.text:"使用乐谱和弦记号的原大小"}
    }
    Repeater {
        model:[{key:"chordColor",name:"记号颜色"},{key:"highlightColor",name:"高亮文字"},{key:"highlightBackground",name:"高亮背景"},{key:"chromaticColor",name:"离调强调"}]
        ColorOption {width:parent.width; caption:modelData.name; value:configuration[modelData.key]; onEdited:editor.option(modelData.key,color); onChooseRequested:editor.chooseColor(modelData.key,value)}
    }
    DeskSwitch {text:"优先保留原谱和弦 / 级数"; checked:configuration.respectExistingHarmony; onToggled:if(checked!==configuration.respectExistingHarmony)editor.option("respectExistingHarmony",checked)}
    DeskSwitch {text:"避让其它谱面内容"; objectName:"harmonyAvoidScoreContent"; checked:configuration.avoidScoreContent; onToggled:if(checked!==configuration.avoidScoreContent)editor.option("avoidScoreContent",checked)}
    DeskSwitch {text:"记号背景遮罩"; checked:configuration.chordMask; onToggled:if(checked!==configuration.chordMask)editor.option("chordMask",checked)}
    DeskSwitch {text:"小节开头重述所属和弦";checked:configuration.repeatBars;onToggled:if(checked!==configuration.repeatBars)editor.option("repeatBars",checked)}
    RowLayout {width:parent.width
        UiLabel {text:"级数字体";color:theme.muted;font.pixelSize:11}
        TextField {text:configuration.degreeFont;Layout.fillWidth:true;maximumLength:64;onEditingFinished:if(text.trim().length)editor.option("degreeFont",text.trim())}
    }
    UiLabel {width:parent.width; text:"记号对齐和弦变化的时间位置；默认在最上层显示；相邻记号重叠时上下排列。避让和遮罩分别开关，横向位置始终对应和弦变化。"; color:theme.muted; font.pixelSize:10; wrapMode:Text.Wrap}
    UiLabel {width:parent.width; text:"同位置已有原谱和弦时，分析留在面板，谱面只补充级数；已有罗马级数时只补充和弦。关闭后可同时显示原谱和分析结果。开启避让时也会保护正在编辑的文字。"; color:theme.muted; font.pixelSize:10; wrapMode:Text.Wrap}
    DeskSwitch {text:"强调含调外音的和弦"; checked:configuration.chromaticAccent; onToggled:if(checked!==configuration.chromaticAccent)editor.option("chromaticAccent",checked)}
    UiLabel {width:parent.width; text:"小调兼容属和弦及导音和弦；强调色表示模板含调外音，不代表唯一和声解释。不冲突的记号保持同一高度，过密时上下排列；双击可跳转面板。"; color:theme.muted; font.pixelSize:10; wrapMode:Text.Wrap}
}
