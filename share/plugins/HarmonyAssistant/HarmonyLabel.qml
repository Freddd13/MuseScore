import QtQuick 2.9

Item {
    id:label
    property var observer:null
    property string chord:""
    property string degree:""
    property color color:"#343a3f"
    property real pixels:20
    property real degreeRatio:1
    property real gap:6
    property int order:0
    property string chordFont:""
    property string degreeFont:"Edwin"
    property string revision:""
    property var rendered:{
        var stamp=revision
        return observer && typeof observer.harmonyLabel==="function" ? observer.harmonyLabel(chord,degree,{pixels:pixels,degreeRatio:degreeRatio,gap:gap,order:order,font:chordFont,degreeFont:degreeFont,color:String(color)}) : null
    }
    implicitWidth:rendered&&rendered.width?rendered.width:fallback.implicitWidth
    implicitHeight:rendered&&rendered.height?rendered.height:fallback.implicitHeight
    Image {anchors.fill:parent;source:label.rendered&&label.rendered.source?label.rendered.source:"";fillMode:Image.PreserveAspectFit;horizontalAlignment:Image.AlignLeft;smooth:true;cache:false}
    Text {id:fallback;visible:!label.rendered||!label.rendered.source;text:label.chord+(label.chord&&label.degree?"  ":"")+label.degree;font.family:"Edwin";font.pixelSize:label.pixels;color:label.color;anchors.verticalCenter:parent.verticalCenter}
}
