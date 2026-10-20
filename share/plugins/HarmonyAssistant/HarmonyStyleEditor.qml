import QtQuick 2.9
import QtQuick.Layouts 1.3
import "Preferences.js" as Preferences

Column {
    id: editor
    objectName: prefix+"StyleEditor"
    UiTheme { id: theme }
    property var observer: null
    property var configuration: Preferences.defaults()
    property string prefix: "detail"
    property string chord: "C/E"
    property string degree: "I⁶"
    signal modified(var configuration)
    signal chooseColor(string key, string color)
    spacing: 8
    function option(suffix, value) {
        var next=JSON.parse(JSON.stringify(configuration))
        next[prefix+suffix]=value
        modified(Preferences.clean(next))
    }
    Item {
        width: parent.width
        height: Math.max(48, preview.implicitHeight+8)
        clip: true
        HarmonyLabel {
            id: preview
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            width: Math.min(parent.width, implicitWidth); height: implicitHeight
            observer: editor.observer
            chord: configuration[editor.prefix+"Content"]===1?"":editor.chord
            degree: configuration[editor.prefix+"Content"]===0?"":editor.degree
            pixels: configuration[editor.prefix+"Pixels"]
            degreeRatio: configuration[editor.prefix+"DegreeRatio"]/100
            gap: configuration[editor.prefix+"Gap"]; order: configuration[editor.prefix+"Order"]
            chordFont: ["","Edwin","Arial"][configuration[editor.prefix+"Font"]]
            degreeFont: configuration[editor.prefix+"DegreeFont"]
            color: configuration[editor.prefix+"Color"] || theme.text
        }
    }
    RowLayout {
        width: parent.width
        UiLabel { text: "显示内容"; color: theme.muted; Layout.preferredWidth: 94 }
        DeskComboBox {
            objectName: editor.prefix+"StyleContent"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            model: ["和弦与级数","仅和弦","仅级数"]
            currentIndex: [2,0,1].indexOf(configuration[editor.prefix+"Content"])
            onActivated: editor.option("Content", [2,0,1][currentIndex])
        }
    }
    RowLayout {
        width: parent.width
        UiLabel { text: "排列"; color: theme.muted; Layout.preferredWidth: 94 }
        DeskComboBox {
            objectName: editor.prefix+"StyleOrder"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            model: ["和弦在左","级数在左","和弦在上","级数在上"]
            currentIndex: configuration[editor.prefix+"Order"]
            enabled: configuration[editor.prefix+"Content"]===2
            onActivated: editor.option("Order", currentIndex)
        }
    }
    Repeater {
        model: [{key:"Pixels",title:"字号",min:12,max:editor.prefix==="detail"?40:32},
            {key:"DegreeRatio",title:"级数大小（%）",min:60,max:140},
            {key:"Gap",title:"两者间距",min:0,max:editor.prefix==="detail"?24:20}]
        RowLayout {
            width: parent.width
            UiLabel { text: modelData.title; color: theme.muted; Layout.fillWidth: true }
            DeskSpinBox {
                objectName: editor.prefix+"Style"+modelData.key
                from: modelData.min; to: modelData.max; editable: true
                value: configuration[editor.prefix+modelData.key]
                onValueModified: editor.option(modelData.key, value)
            }
        }
    }
    RowLayout {
        width: parent.width
        UiLabel { text: "和弦字体"; color: theme.muted; Layout.preferredWidth: 94 }
        DeskComboBox {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            model: ["跟随谱面和弦字体","Edwin","Arial"]
            currentIndex: configuration[editor.prefix+"Font"]
            onActivated: editor.option("Font", currentIndex)
        }
    }
    RowLayout {
        width: parent.width
        UiLabel { text: "级数字体"; color: theme.muted; Layout.preferredWidth: 94 }
        DeskTextField {
            Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 64
            text: configuration[editor.prefix+"DegreeFont"]
            onEditingFinished: if(text.trim().length)editor.option("DegreeFont", text.trim())
        }
    }
    ColorOption {
        width: parent.width; caption: "文字颜色"
        value: configuration[editor.prefix+"Color"] || String(theme.text)
        onEdited: editor.option("Color", color)
        onChooseRequested: editor.chooseColor(editor.prefix+"Color", value)
    }
    Flow {
        width: parent.width; spacing: 6
        DeskButton { text: "跟随主题颜色"; onClicked: editor.option("Color", "") }
        DeskButton {
            text: "恢复此处默认外观"
            onClicked: {
                var next=JSON.parse(JSON.stringify(configuration)), defaults=Preferences.defaults()
                var fields=["Pixels","DegreeRatio","Gap","Order","Content","Font","DegreeFont","Color"]
                fields.forEach(function(key){next[editor.prefix+key]=defaults[editor.prefix+key]})
                editor.modified(Preferences.clean(next))
            }
        }
    }
}
