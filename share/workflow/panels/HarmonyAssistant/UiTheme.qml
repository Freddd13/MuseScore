import QtQuick 2.9

QtObject {
    objectName: "workflowUiTheme"
    property SystemPalette palette: SystemPalette {colorGroup:SystemPalette.Active}
    readonly property var hostPalette: typeof workflowPanelPalette === "undefined" ? null : workflowPanelPalette
    readonly property bool dark: background.r+background.g+background.b<1.5
    readonly property string fontFamily: Qt.application.font.family
    readonly property color background: hostPalette && hostPalette.background ? hostPalette.background : palette.window
    readonly property color text: hostPalette && hostPalette.text ? hostPalette.text : palette.windowText
    readonly property color muted: mix(background,text,0.62)
    readonly property color section: hostPalette && hostPalette.button ? hostPalette.button : palette.button
    readonly property color field: hostPalette && hostPalette.field ? hostPalette.field : palette.base
    readonly property color line: hostPalette && hostPalette.line ? hostPalette.line : palette.mid
    readonly property color subtleLine: mix(background,text,0.22)
    readonly property color hover: mix(section,accent,0.12)
    readonly property color accent: hostPalette && hostPalette.accent ? hostPalette.accent : palette.highlight
    readonly property color accentText: palette.highlightedText
    readonly property color selected: mix(field,accent,0.18)
    function mix(a,b,amount) {return Qt.rgba(a.r+(b.r-a.r)*amount,a.g+(b.g-a.g)*amount,a.b+(b.b-a.b)*amount,1)}
}
