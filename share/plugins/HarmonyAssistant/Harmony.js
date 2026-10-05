.pragma library

// ES5 only: also runs in the Qt 5.9 QML engine used by MuseScore 3.
var colors = {"1":"#B75555", "3":"#9D711E", "5":"#377CAB", "7":"#805EB1",
              "9":"#278879", "11":"#AE673F", "13":"#A85185", "外":"#737E8A"};
var rootNames = ["C","C♯","D♭","D","D♯","E♭","E","F","F♯","G♭","G","G♯","A♭","A","A♯","B♭","B"];
var rootPcs = [0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11];
var sharpNames = ["C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"];
var flatNames = ["C","D♭","D","E♭","E","F","G♭","G","A♭","A","B♭","B"];
function def(name, suffix, intervals, labels, minor, roman) {
    return {name:name, suffix:suffix, intervals:intervals, labels:labels, minor:!!minor, roman:roman || ""};
}
var defs = [
    def("大三和弦","",[0,4,7],["1","3","5"]),
    def("小三和弦","m",[0,3,7],["1","♭3","5"],true),
    def("属七","7",[0,4,7,10],["1","3","5","♭7"],false,"7"),
    def("大七","maj7",[0,4,7,11],["1","3","5","7"],false,"maj7"),
    def("小七","m7",[0,3,7,10],["1","♭3","5","♭7"],true,"7"),
    def("减三","dim",[0,3,6],["1","♭3","♭5"],true,"°"),
    def("减七","dim7",[0,3,6,9],["1","♭3","♭5","♭♭7"],true,"°7"),
    def("半减七","m7♭5",[0,3,6,10],["1","♭3","♭5","♭7"],true,"ø7"),
    def("增三","+",[0,4,8],["1","3","♯5"],false,"+"),
    def("挂二","sus2",[0,2,7],["1","2","5"],false,"sus2"),
    def("挂四","sus4",[0,5,7],["1","4","5"],false,"sus4"),
    def("六和弦","6",[0,4,7,9],["1","3","5","6"],false,"6"),
    def("小六","m6",[0,3,7,9],["1","♭3","5","6"],true,"6"),
    def("加九","add9",[0,2,4,7],["1","9","3","5"],false,"add9"),
    def("小加九","m(add9)",[0,2,3,7],["1","9","♭3","5"],true,"add9"),
    def("属九","9",[0,2,4,7,10],["1","9","3","5","♭7"],false,"9"),
    def("大九","maj9",[0,2,4,7,11],["1","9","3","5","7"],false,"maj9"),
    def("小九","m9",[0,2,3,7,10],["1","9","♭3","5","♭7"],true,"9"),
    def("五度","5",[0,7],["1","5"],false,"5"),
    def("属十一","11",[0,2,4,5,7,10],["1","9","3","11","5","♭7"],false,"11"),
    def("小十一","m11",[0,2,3,5,7,10],["1","9","♭3","11","5","♭7"],true,"11"),
    def("大七升十一","maj7(♯11)",[0,4,6,7,11],["1","3","♯11","5","7"],false,"maj7(♯11)"),
    def("属十三","13",[0,2,4,7,9,10],["1","9","3","5","13","♭7"],false,"13"),
    def("大十三","maj13",[0,2,4,7,9,11],["1","9","3","5","13","7"],false,"maj13"),
    def("小十三","m13",[0,2,3,5,7,9,10],["1","9","♭3","11","5","13","♭7"],true,"13"),
    def("属七降九","7(♭9)",[0,1,4,7,10],["1","♭9","3","5","♭7"],false,"7(♭9)"),
    def("属七升九","7(♯9)",[0,3,4,7,10],["1","♯9","3","5","♭7"],false,"7(♯9)"),
    def("属七挂四","7sus4",[0,5,7,10],["1","4","5","♭7"],false,"7sus4"),
    def("小大七","m(maj7)",[0,3,7,11],["1","♭3","5","7"],true,"maj7")
];
function mod12(n) { return (n % 12 + 12) % 12; }
function unique(pitches) {
    var result=[];
    for(var i=0;i<pitches.length;++i) if(result.indexOf(mod12(pitches[i]))<0) result.push(mod12(pitches[i]));
    return result.sort(function(a,b){return a-b;});
}
function pcName(pc, flats) {return (flats?flatNames:sharpNames)[mod12(pc)];}
function spelledName(tpc, pitch, flats) {
    if(typeof tpc !== "number") return pcName(pitch,flats);
    var letter=["F","C","G","D","A","E","B"][(tpc+1+70)%7];
    var accidental=Math.floor((tpc+1)/7)-2;
    return letter+(accidental<0?new Array(-accidental+1).join("♭"):new Array(accidental+1).join("♯"));
}
function role(label) {
    var degree=String(label).replace(/[♭♯]/g,"");
    // Suspensions and sixth chords keep their musical labels; use the related extension palette.
    return degree==="2"?"9":degree==="4"?"11":degree==="6"?"13":degree;
}
function labelFor(pc, tonic, definition) {
    if(tonic<0 || definition<0) return "外";
    var d=defs[definition], i=d.intervals.indexOf(mod12(pc-tonic));
    return i<0?"外":d.labels[i];
}
function colorFor(pc, tonic, definition) {return colors[role(labelFor(pc,tonic,definition))] || colors["外"];}
function detect(pitches) {
    var pcs=unique(pitches), bass=pitches.length?mod12(Math.min.apply(Math,pitches)):-1;
    if(pcs.length<2) return {root:-1, definition:-1, kind:pcs.length?"单音":"空拍", alternatives:[]};
    var candidates=[];
    for(var r=0;r<12;++r) for(var j=0;j<defs.length;++j) {
        var d=defs[j], missing=[], extra=[], hits=0, cost=0;
        for(var i=0;i<d.intervals.length;++i) {
            if(pcs.indexOf(mod12(r+d.intervals[i]))>=0) ++hits;
            else {
                missing.push(d.labels[i]);
                cost+=d.labels[i]==="5"?1.3:d.labels[i]==="1"?3.5:5;
            }
        }
        for(i=0;i<pcs.length;++i) if(d.intervals.indexOf(mod12(pcs[i]-r))<0) extra.push(pcs[i]);
        cost+=extra.length*4.5;
        if(r===bass) cost-=0.3;
        // Weak dyads must not be promoted to seventh or extended chords.
        if(pcs.length===2 && !(d.suffix==="5" && !missing.length && !extra.length)) continue;
        if(hits<3 && pcs.length>2) continue;
        candidates.push({root:r,definition:j,cost:cost,missing:missing,extra:extra});
    }
    candidates.sort(function(a,b){return a.cost-b.cost || a.definition-b.definition || a.root-b.root;});
    if(!candidates.length || candidates[0].cost>5.5)
        return {root:-1,definition:-1,kind:pcs.length===2?"双音 · 请指定和弦":"未确定和弦",alternatives:[]};
    var best=candidates[0], alternatives=[];
    for(var c=1;c<candidates.length && alternatives.length<2;++c)
        if(candidates[c].cost-best.cost<0.8) alternatives.push(candidates[c]);
    best.kind=best.missing.length || best.extra.length?"候选 · 不完整/含外音":alternatives.length?"匹配 · 存在歧义":"完整匹配";
    best.alternatives=alternatives;
    return best;
}
function roman(tonic, minorKey, chordRoot, definition) {
    if(chordRoot<0 || definition<0) return "—";
    // Conventional Roman numerals reference the parallel major: A minor uses ♭III, ♭VI, ♭VII.
    var map=["I","♭II","II","♭III","III","IV","♯IV","V","♭VI","VI","♭VII","VII"];
    var d=defs[definition], base=map[mod12(chordRoot-tonic)];
    return (d.minor?base.toLowerCase():base)+d.roman;
}
function inversion(label) {
    return {"1":"原位","3":"第一转位","♭3":"第一转位","5":"第二转位","♭5":"第二转位","♯5":"第二转位",
            "7":"第三转位","♭7":"第三转位","♭♭7":"第三转位"}[label] || "延伸音 / 外音低音";
}
function atTick(events,tick) {
    var lo=0,hi=events.length-1,found=-1;
    while(lo<=hi) {var mid=(lo+hi)>>1; if(events[mid].tick<=tick){found=mid;lo=mid+1;}else hi=mid-1;}
    return found>=0 && tick<events[found].end?events[found]:null;
}
