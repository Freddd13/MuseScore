.pragma library

// Versioned, bounded plain data. Imported configuration is never executed.
var roles = ["1","3","5","7","9","11","13","外"];
var labels = ["1","♭3","3","♭5","5","♯5","♭♭7","♭7","7","♭9","9","♯9","11","♯11","♭13","13","2","4","6","外"];
var palettes = {
    "Carbon": ["#002d9c","#6929c4","#005d5d","#9f1853","#8a3800","#198038","#8e6a00","#697077"],
    "柔和": ["#3c5488","#8064a2","#397b83","#9e5670","#ad7441","#5b8063","#938448","#697077"],
    "单色": ["#34495e","#34495e","#34495e","#34495e","#34495e","#34495e","#34495e","#697077"]
};
function defaults() {
    var colors={}, names={};
    for(var i=0;i<roles.length;++i) colors[roles[i]]=palettes.Carbon[i];
    for(i=0;i<labels.length;++i) names[labels[i]]=labels[i];
    return {schema:1,colors:colors,labels:names,noteLabels:{},palette:"Carbon",auto:true,
        follow:true,coloring:true,allColor:false,chordLabels:false,noteFunctions:false,
        pedal:true,window:0,scope:0,keyMode:0,keyTonic:0,manualKey:false,
        root:0,quality:0,keyboard:false,settings:false,
        ribbonAlign:1,ribbonPosition:50,chordContent:2,chordOrder:0,chordFont:0,chordScale:100,
        chordColor:"#343a3f",highlightColor:"#0043ce",highlightBackground:"#d0e2ff",
        chromaticAccent:true,chromaticColor:"#a2191f"};
}
function clean(source) {
    if(!source || source.schema!==1) throw Error("配置版本应为 1");
    var out=defaults(), booleans=["auto","follow","coloring","allColor","chordLabels","noteFunctions","pedal","manualKey","keyboard","settings","chromaticAccent"];
    for(var i=0;i<booleans.length;++i) if(typeof source[booleans[i]]==="boolean") out[booleans[i]]=source[booleans[i]];
    var limits={window:4,scope:1,keyMode:1,keyTonic:16,root:16,quality:28,ribbonAlign:3,ribbonPosition:100,chordContent:2,chordOrder:3,chordFont:2};
    for(var key in limits) if(typeof source[key]==="number" && source[key]===Math.floor(source[key]) && source[key]>=0 && source[key]<=limits[key])out[key]=source[key];
    if(palettes[source.palette])out.palette=source.palette;
    if(typeof source.chordScale==="number" && Math.floor(source.chordScale)===source.chordScale && source.chordScale>=60 && source.chordScale<=200)out.chordScale=source.chordScale;
    var styleColors=["chordColor","highlightColor","highlightBackground","chromaticColor"];
    for(i=0;i<styleColors.length;++i){var k=styleColors[i];if(source[k]!==undefined){
        if(typeof source[k]!=="string" || !/^#[0-9a-f]{6}$/i.test(source[k]))throw Error("颜色需要 #RRGGBB："+k);
        out[k]=source[k];}}
    for(i=0;i<roles.length;++i) {
        var role=roles[i], color=source.colors && source.colors[role];
        if(color!==undefined && !/^#[0-9a-f]{6}$/i.test(color)) throw Error("颜色需要 #RRGGBB："+role);
        if(color)out.colors[role]=color;
    }
    for(i=0;i<labels.length;++i) {
        var name=source.labels && source.labels[labels[i]];
        if(name!==undefined) {
            if(typeof name!=="string" || name.length>24)throw Error("功能名最多 24 字："+labels[i]);
            out.labels[labels[i]]=name;
        }
    }
    for(i=0;i<12;++i) if(source.noteLabels && typeof source.noteLabels[i]==="string" && source.noteLabels[i].length<=24) out.noteLabels[i]=source.noteLabels[i];
    return out;
}
function preset(config, name) {
    var out=clean(config);
    if(!palettes[name])return out;
    out.palette=name;
    for(var i=0;i<roles.length;++i)out.colors[roles[i]]=palettes[name][i];
    return out;
}
