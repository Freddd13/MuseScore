import QtQuick 2.9
import QtQuick.Controls 2.2 as Controls

Controls.ComboBox {
    id:control
    UiTheme {id:theme}
    implicitHeight:28;implicitWidth:120
    leftPadding:7;rightPadding:23
    font.family:theme.fontFamily;font.pixelSize:11
    opacity:enabled ? 1 : 0.45
    contentItem:Text {
        text:control.displayText;font:control.font;color:theme.text
        verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight
    }
    indicator:Text {x:control.width-width-8;y:(control.height-height)/2;text:"▾";color:theme.muted;font.pixelSize:12}
    background:Rectangle {color:theme.field;radius:2;border.color:control.activeFocus ? theme.accent : theme.line}
    delegate:Controls.ItemDelegate {
        width:control.width;height:28
        text:control.textRole ? modelData[control.textRole] : modelData
        font:control.font;highlighted:control.highlightedIndex===index
        contentItem:Text {text:parent.text;font:control.font;color:theme.text;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight}
        background:Rectangle {color:parent.highlighted ? theme.selected : theme.field}
    }
    popup:Controls.Popup {
        y:control.height-1;width:control.width;padding:1
        implicitHeight:Math.min(260,contentItem.implicitHeight+2)
        contentItem:ListView {
            clip:true;implicitHeight:contentHeight
            model:control.popup.visible ? control.delegateModel : null
            currentIndex:control.highlightedIndex
            Controls.ScrollIndicator.vertical:Controls.ScrollIndicator {}
        }
        background:Rectangle {color:theme.field;border.color:theme.line;radius:2}
    }
}
