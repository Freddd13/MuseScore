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
    signal modified(var configuration)
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
    AppearanceEditor {
        width:parent.width; configuration:editor.configuration
        onModified:editor.modified(configuration)
        onChooseColor:editor.openColor("",key,color)
    }
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
    UiLabel { id:errorText; width:parent.width; visible:text.length>0; color:"#b42626"; font.pixelSize:11; wrapMode:Text.Wrap }
}
