import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import "Preferences.js" as Preferences

Column {
    property var configuration: Preferences.defaults()
    signal modified(var configuration)
    spacing: 8
    function change(group,key,value) {
        var next=JSON.parse(JSON.stringify(configuration))
        next[group][key]=value
        try { modified(Preferences.clean(next)) } catch(error) { errorText.text=String(error) }
    }
    RowLayout {
        width:parent.width
        UiLabel { text:"功能色方案"; Layout.fillWidth:true; font.bold:true; color:"#343a3f" }
        ComboBox {
            model:["Carbon","柔和","单色"]; implicitHeight:32; Layout.preferredWidth:104
            currentIndex:model.indexOf(configuration.palette)
            onActivated:modified(Preferences.preset(configuration,model[currentIndex]))
        }
    }
    UiLabel { width:parent.width; text:"色块用于区分功能；界面保持中性。颜色填写 #RRGGBB。"; color:"#697077"; font.pixelSize:11; wrapMode:Text.Wrap }
    Repeater {
        model:Preferences.roles
        RowLayout {
            width:parent.width; spacing:8
            Rectangle { width:14; height:14; radius:3; color:configuration.colors[modelData] }
            UiLabel { text:modelData; Layout.preferredWidth:28; color:"#343a3f" }
            TextField {
                Layout.fillWidth:true; implicitHeight:32; font.pixelSize:12
                text:configuration.colors[modelData]
                onEditingFinished:change("colors",modelData,text)
            }
        }
    }
    UiLabel { text:"音旁文字（留空可隐藏该功能）"; font.bold:true; color:"#343a3f"; font.pixelSize:12 }
    Repeater {
        model:Preferences.labels
        RowLayout {
            width:parent.width
            UiLabel { text:modelData; Layout.preferredWidth:36; color:"#697077" }
            TextField {
                Layout.fillWidth:true; implicitHeight:30; font.pixelSize:12; maximumLength:24
                text:configuration.labels[modelData]
                onEditingFinished:change("labels",modelData,text)
            }
        }
    }
    Button { id:fixedPitch; text:checked?"收起固定音名覆盖":"固定音名覆盖（可选）"; checkable:true; implicitHeight:32 }
    Column {
        width:parent.width; visible:fixedPitch.checked; spacing:6
        UiLabel {width:parent.width; text:"按十二音级指定文字；留空沿用和弦功能名。"; color:"#697077"; font.pixelSize:11; wrapMode:Text.Wrap}
        Repeater {
            model:["C","C♯ / D♭","D","D♯ / E♭","E","F","F♯ / G♭","G","G♯ / A♭","A","A♯ / B♭","B"]
            RowLayout {
                width:parent.width
                UiLabel {text:modelData; Layout.preferredWidth:66; color:"#697077"; font.pixelSize:11}
                TextField {
                    Layout.fillWidth:true; implicitHeight:30; maximumLength:24; font.pixelSize:12
                    text:configuration.noteLabels[index] || ""; placeholderText:"沿用功能名"
                    onEditingFinished:change("noteLabels",index,text)
                }
            }
        }
    }
    UiLabel { id:errorText; width:parent.width; visible:text.length>0; color:"#b42626"; font.pixelSize:11; wrapMode:Text.Wrap }
}
