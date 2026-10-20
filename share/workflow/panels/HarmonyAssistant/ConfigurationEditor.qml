import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3
import "Preferences.js" as Preferences

Column {
    id:editor
    UiTheme {id:theme}
    objectName:"harmonyConfiguration"
    property var configuration: Preferences.defaults()
    property var observer: null
    property string chord: "C/E"
    property string degree: "I⁶"
    signal modified(var configuration)
    signal layoutModified(var configuration)
    function toggleDetails(key) {
        var next=JSON.parse(JSON.stringify(configuration)),index=next.expandedSubsections.indexOf(key)
        if(index<0)next.expandedSubsections.push(key);else next.expandedSubsections.splice(index,1)
        layoutModified(Preferences.clean(next))
    }
    spacing: 8
    function change(group,key,value) {
        var next=JSON.parse(JSON.stringify(configuration))
        if(group)next[group][key]=value
        else next[key]=value
        try { modified(Preferences.clean(next)) } catch(error) { errorText.text=String(error) }
    }
    function openColor(group,key,color) {picker.group=group; picker.key=key; picker.color=color; picker.open()}
    ColorDialog {
        id:picker; objectName:"harmonyColorPicker"; title:"选择颜色"; showAlphaChannel:false
        property string group
        property string key
        onAccepted:editor.change(group,key,String(color))
    }
    PanelCard {
        objectName:"harmonySummaryStyleCard";width:parent.width;heading:"顶部摘要外观"
        collapsed:editor.configuration.expandedSubsections.indexOf("summaryStyle")<0;onToggleRequested:editor.toggleDetails("summaryStyle")
        DeskSwitch {text:"同时显示顶部摘要与右侧详情";checked:configuration.dualPanel;onToggled:if(checked!==configuration.dualPanel)editor.change("","dualPanel",checked)}
        RowLayout {width:parent.width
            UiLabel {text:"摘要位置";color:theme.muted;Layout.preferredWidth:94}
            DeskComboBox {Layout.fillWidth:true;Layout.minimumWidth:0;model:["靠左","居中","靠右","自定位置"];currentIndex:configuration.ribbonAlign;onActivated:editor.change("","ribbonAlign",currentIndex)}
        }
        RowLayout {width:parent.width;visible:configuration.ribbonAlign===3
            UiLabel {text:"横向位置（%）";color:theme.muted;Layout.fillWidth:true}
            DeskSpinBox {from:0;to:100;value:configuration.ribbonPosition;editable:true;onValueModified:editor.change("","ribbonPosition",value)}
        }
        HarmonyStyleEditor {width:parent.width;prefix:"summary";configuration:editor.configuration;observer:editor.observer;chord:editor.chord;degree:editor.degree;onModified:editor.modified(configuration);onChooseColor:editor.openColor("",key,color)}
    }
    PanelCard {
        objectName:"harmonyDetailStyleCard";width:parent.width;heading:"右侧详情外观"
        collapsed:editor.configuration.expandedSubsections.indexOf("detailStyle")<0;onToggleRequested:editor.toggleDetails("detailStyle")
        HarmonyStyleEditor {width:parent.width;prefix:"detail";configuration:editor.configuration;observer:editor.observer;chord:editor.chord;degree:editor.degree;onModified:editor.modified(configuration);onChooseColor:editor.openColor("",key,color)}
    }
    PanelCard {objectName:"harmonyScoreStyleCard";width:parent.width;heading:"谱面和弦记号";collapsed:editor.configuration.expandedSubsections.indexOf("appearance")<0;onToggleRequested:editor.toggleDetails("appearance")
    AppearanceEditor {
        width:parent.width; configuration:editor.configuration
        onModified:editor.modified(configuration)
        onChooseColor:editor.openColor("",key,color)
    }
    }
    PanelCard {width:parent.width;heading:"离调和弦强调（所有位置）";collapsed:editor.configuration.expandedSubsections.indexOf("accent")<0;onToggleRequested:editor.toggleDetails("accent")
        DeskSwitch {text:"强调含调外音的和弦";checked:configuration.chromaticAccent;onToggled:if(checked!==configuration.chromaticAccent)editor.change("","chromaticAccent",checked)}
        ColorOption {width:parent.width;caption:"强调颜色";value:configuration.chromaticColor;onEdited:editor.change("","chromaticColor",color);onChooseRequested:editor.openColor("","chromaticColor",value)}
        UiLabel {width:parent.width;text:"摘要、详情与谱面共用此强调色。小调的属和弦与导音和弦按和声小调处理。";wrapMode:Text.Wrap;color:theme.muted;font.pixelSize:11}
    }
    PanelCard {width:parent.width;heading:"功能色";collapsed:editor.configuration.expandedSubsections.indexOf("colors")<0;onToggleRequested:editor.toggleDetails("colors")
    Rectangle {width:parent.width; height:1; color:theme.subtleLine}
    RowLayout {
        width:parent.width
        UiLabel { text:"功能色方案"; Layout.fillWidth:true; font.bold:true; color:theme.text }
        DeskComboBox {
            model:["Carbon","柔和","单色"]; implicitHeight:28; Layout.preferredWidth:104
            currentIndex:model.indexOf(configuration.palette)
            onActivated:modified(Preferences.preset(configuration,model[currentIndex]))
        }
    }
    UiLabel { width:parent.width; text:"点击色块或「选色」打开选色器，也可填写 #RRGGBB。"; color:theme.muted; font.pixelSize:11; wrapMode:Text.Wrap }
    Repeater {
        model:Preferences.roles
        ColorOption {width:parent.width; caption:modelData; value:configuration.colors[modelData]; onEdited:editor.change("colors",modelData,color); onChooseRequested:editor.openColor("colors",modelData,value)}
    }
    }
    PanelCard {width:parent.width;heading:"音旁文字";collapsed:editor.configuration.expandedSubsections.indexOf("labels")<0;onToggleRequested:editor.toggleDetails("labels")
    UiLabel { text:"音旁文字（留空可隐藏该功能）"; font.bold:true; color:theme.text; font.pixelSize:12 }
    Repeater {
        model:Preferences.labels
        RowLayout {
            width:parent.width
            UiLabel { text:modelData; Layout.preferredWidth:36; color:theme.muted }
            DeskTextField {
                Layout.fillWidth:true; maximumLength:24
                text:configuration.labels[modelData]
                onEditingFinished:change("labels",modelData,text)
            }
        }
    }
    DeskButton { id:fixedPitch; text:checked?"收起固定音名覆盖":"固定音名覆盖（可选）"; checkable:true; implicitHeight:28 }
    Column {
        width:parent.width; visible:fixedPitch.checked; spacing:6
        UiLabel {width:parent.width; text:"按十二音级指定文字；留空沿用和弦功能名。"; color:theme.muted; font.pixelSize:11; wrapMode:Text.Wrap}
        Repeater {
            model:["C","C♯ / D♭","D","D♯ / E♭","E","F","F♯ / G♭","G","G♯ / A♭","A","A♯ / B♭","B"]
            RowLayout {
                width:parent.width
                UiLabel {text:modelData; Layout.preferredWidth:66; color:theme.muted; font.pixelSize:11}
                DeskTextField {
                    Layout.fillWidth:true; maximumLength:24
                    text:configuration.noteLabels[index] || ""; placeholderText:"沿用功能名"
                    onEditingFinished:change("noteLabels",index,text)
                }
            }
        }
    }
    }
    UiLabel { id:errorText; width:parent.width; visible:text.length>0; color:"#b42626"; font.pixelSize:11; wrapMode:Text.Wrap }
}
