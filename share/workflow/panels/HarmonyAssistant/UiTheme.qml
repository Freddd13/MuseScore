import QtQuick 2.9

QtObject {
    property SystemPalette palette: SystemPalette {colorGroup:SystemPalette.Active}
    readonly property bool dark: palette.window.r+palette.window.g+palette.window.b<1.5
    readonly property string fontFamily: Qt.platform.os==="windows" ? "Microsoft YaHei UI" : Qt.application.font.family
    readonly property color background: palette.window
    readonly property color text: palette.windowText
    readonly property color muted: mix(background,text,0.62)
    readonly property color section: dark ? "#34383d" : "#e7e9eb"
    readonly property color field: dark ? "#25292e" : "#fafafa"
    readonly property color line: dark ? "#555a60" : "#bfc3c7"
    readonly property color subtleLine: dark ? "#44494e" : "#d1d4d7"
    readonly property color hover: dark ? "#454b52" : "#dce0e3"
    readonly property color accent: palette.highlight
    readonly property color accentText: palette.highlightedText
    readonly property color selected: mix(field,accent,0.18)
    function mix(a,b,amount) {return Qt.rgba(a.r+(b.r-a.r)*amount,a.g+(b.g-a.g)*amount,a.b+(b.b-a.b)*amount,1)}
}
