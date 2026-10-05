import QtQuick 2.9
import QtQuick.Controls 2.2

// Reserve rows while content changes; full text remains available on hover.
UiLabel {
    id:label
    property int reservedLines: 1
    width:parent.width
    height:Math.ceil(metrics.height)*reservedLines
    clip:true
    wrapMode:reservedLines>1 ? Text.Wrap : Text.NoWrap
    maximumLineCount:reservedLines
    elide:Text.ElideRight
    FontMetrics {id:metrics; font:label.font}
    ToolTip.visible:area.containsMouse && label.text.length>0
    ToolTip.text:label.text
    ToolTip.delay:500
    MouseArea {id:area; anchors.fill:parent; hoverEnabled:true; acceptedButtons:Qt.NoButton}
}
