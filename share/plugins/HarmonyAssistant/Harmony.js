.pragma library

// ES5 only: also runs in the Qt 5.9 QML engine used by MuseScore 3.
var colors = {"1":"#002d9c", "3":"#6929c4", "5":"#005d5d", "7":"#9f1853",
              "9":"#8a3800", "11":"#198038", "13":"#8e6a00", "外":"#697077"};
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
function bitCount(bits) {var count=0;while(bits){bits&=bits-1;++count;}return count;}
var templates=[];
for(var tr=0;tr<12;++tr) for(var td=0;td<defs.length;++td) {
    var mask=0, fifth=0;
    for(var ti=0;ti<defs[td].intervals.length;++ti) {
        var bit=1<<mod12(tr+defs[td].intervals[ti]);mask|=bit;
        if(defs[td].labels[ti]==="5")fifth=bit;
    }
    templates.push({root:tr,definition:td,mask:mask,fifth:fifth});
}
function detect(pitches) {
    var mask=0,bass=128;
    for(var i=0;i<pitches.length;++i){mask|=1<<mod12(pitches[i]);bass=Math.min(bass,pitches[i]);}
    var count=bitCount(mask);bass=mod12(bass);
    if(count<2)return {root:-1,definition:-1,kind:count?"单音":"空拍",alternatives:[]};
    var candidates=[];
    for(var t=0;t<templates.length;++t) {
        var template=templates[t],missing=template.mask&~mask,extra=mask&~template.mask;
        if(count===2 && !(defs[template.definition].suffix==="5" && !missing && !extra))continue;
        if(count>2 && bitCount(mask&template.mask)<3)continue;
        var cost=bitCount(missing)*5+bitCount(extra)*4.5;
        if(missing & template.fifth)cost-=3.7;
        if(missing & (1<<template.root))cost-=1.5;
        if(template.root===bass)cost-=0.3;
        candidates.push({root:template.root,definition:template.definition,cost:cost,missingMask:missing,extraMask:extra});
    }
    candidates.sort(function(a,b){return a.cost-b.cost || a.definition-b.definition || a.root-b.root;});
    if(!candidates.length || candidates[0].cost>5.5)
        return {root:-1,definition:-1,kind:count===2?"双音 · 请指定和弦":"未确定和弦",alternatives:[]};
    function detail(candidate) {
        var definition=defs[candidate.definition];candidate.missing=[];candidate.extra=[];
        for(var i=0;i<definition.intervals.length;++i)if(candidate.missingMask & (1<<mod12(candidate.root+definition.intervals[i])))candidate.missing.push(definition.labels[i]);
        for(i=0;i<12;++i)if(candidate.extraMask & (1<<i))candidate.extra.push(i);
        return candidate;
    }
    var best=detail(candidates[0]),alternatives=[];
    for(var c=1;c<candidates.length && alternatives.length<2;++c)
        if(candidates[c].cost-best.cost<0.8)alternatives.push(detail(candidates[c]));
    best.kind=best.missing.length || best.extra.length?"候选 · 不完整/含外音":alternatives.length?"匹配 · 存在歧义":"完整匹配";
    best.alternatives=alternatives;
    return best;
}
function detectWeighted(notes, weights, previous, tonic, minorKey, tick) {
    var maximum=0,mask=0,bass=null,held=notes.filter(function(n){return n.end===undefined || n.end>tick;});
    var pool=held.length?held:notes;
    for(var p=0;p<pool.length;++p)if(!bass || pool[p].pitch<bass.pitch)bass=pool[p];
    for(var pc in weights)maximum=Math.max(maximum,weights[pc]);
    if(!maximum)return {root:-1,definition:-1,kind:"空拍",alternatives:[]};
    var normalized=[];
    for(var i=0;i<12;++i){normalized[i]=(weights[i]||0)/maximum;if(normalized[i]>=.15)mask|=1<<i;}
    if(bitCount(mask)<3)return detect(notes.map(function(n){return n.pitch;}));
    var best=null,runner=null,scale=minorKey?[0,2,3,5,7,8,10]:[0,2,4,5,7,9,11];
    for(var t=0;t<templates.length;++t) {
        var candidate=templates[t],definition=defs[candidate.definition],hits=bitCount(mask&candidate.mask);
        if(hits<3)continue;
        var cost=.23*Math.max(0,definition.intervals.length-3);
        for(i=0;i<12;++i) {
            if(candidate.mask&(1<<i)) {
                var missing=1-normalized[i];
                cost+=missing*((candidate.fifth&(1<<i))?.45:i===candidate.root?1.8:3.2);
            } else cost+=normalized[i]*3.0;
        }
        if(bass) {
            var bassPc=mod12(bass.pitch);
            if(candidate.root===bassPc)cost-=.6;
            else if(candidate.mask&(1<<bassPc))cost-=.22;
        }
        if(previous && previous.root===candidate.root)cost-=previous.definition===candidate.definition?.65:.3;
        if(scale.indexOf(mod12(candidate.root-tonic))<0)cost+=.12;
        var result={root:candidate.root,definition:candidate.definition,cost:cost};
        if(!best || cost<best.cost){runner=best;best=result;}else if(!runner||cost<runner.cost)runner=result;
    }
    if(!best || best.cost>6)return {root:-1,definition:-1,kind:"未确定和弦",alternatives:[]};
    best.alternatives=runner && runner.cost-best.cost<.65?[runner]:[];
    best.kind=best.alternatives.length?"上下文匹配 · 存在歧义":"上下文匹配";
    return best;
}
function roman(tonic, minorKey, chordRoot, definition) {
    if(chordRoot<0 || definition<0) return "—";
    // Conventional Roman numerals reference the parallel major: A minor uses ♭III, ♭VI, ♭VII.
    var map=["I","♭II","II","♭III","III","IV","♯IV","V","♭VI","VI","♭VII","VII"];
    var d=defs[definition], base=map[mod12(chordRoot-tonic)];
    return (d.minor?base.toLowerCase():base)+d.roman;
}
function isChromatic(tonic, minorKey, chordRoot, definition) {
    if(chordRoot<0 || definition<0 || !defs[definition])return false;
    var scale=minorKey?[0,2,3,5,7,8,10]:[0,2,4,5,7,9,11], mask=0;
    for(var i=0;i<scale.length;++i)mask|=1<<mod12(tonic+scale[i]);
    // Minor's dominant and leading-tone chords may use the harmonic-minor leading tone.
    var relative=mod12(chordRoot-tonic);
    if(minorKey && (relative===7 || relative===11))mask|=1<<mod12(tonic+11);
    var intervals=defs[definition].intervals;
    for(i=0;i<intervals.length;++i)if(!(mask & (1<<mod12(chordRoot+intervals[i]))))return true;
    return false;
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
