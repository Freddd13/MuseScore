// SPDX-License-Identifier: GPL-2.0-or-later
// Pure value algorithms. No score, GUI, file, synth or model dependency.
function median(values) {
    var a=values.slice().sort(function(x,y){return x-y});
    return a.length ? a[Math.floor(a.length/2)] : 0;
}
function clone(value) {return JSON.parse(JSON.stringify(value))}
function groups(notes,score,tap,window) {
    var a=notes.filter(function(n){return !score || !n.tieBack}).slice();
    a.sort(function(x,y){return (score?((x.alignmentTick===undefined?x.tick:x.alignmentTick)-(y.alignmentTick===undefined?y.tick:y.alignmentTick)):x.seconds-y.seconds)||x.pitch-y.pitch});
    var out=[],seen={};
    for(var i=0;i<a.length;++i){
        var n=a[i],id=String(score?n.logicalId||n.id:n.id);
        if(score&&seen[id])continue;seen[id]=true;
        var time=score?(n.alignmentTick===undefined?n.tick:n.alignmentTick):n.seconds,last=out[out.length-1];
        // Rolling is only a coarse aid; preserve every original event below.
        var gap=window?window.gap:.035,span=window?window.span:.035;
        var retrigger=last&&!score&&window&&time-last.lastTime>.008&&last.notes.filter(function(v){return v.pitch===n.pitch}).length>=(window.repeats[n.pitch]||1);
        if(!last || (score?time!==last.time:time-last.lastTime>gap||time-last.time>span||retrigger)){
            last={time:time,notes:[],pitches:[],id:id};out.push(last);
        }
        last.notes.push(n);if(last.pitches.indexOf(n.pitch)<0)last.pitches.push(n.pitch);
        last.lastTime=time;
    }
    return out;
}
// Estimate the small within-chord gaps separately from the gaps between
// attacks. Every note remains an individual event for the fine match below.
function performanceWindow(scoreGroups,notes,tap) {
    var window={gap:.035,span:.035,repeats:{},adaptive:false},sizes=[];
    scoreGroups.forEach(function(g){sizes.push(g.pitches.length);var counts={};g.notes.forEach(function(n){counts[n.pitch]=(counts[n.pitch]||0)+1;window.repeats[n.pitch]=Math.max(window.repeats[n.pitch]||1,counts[n.pitch]);});});
    if(tap||median(sizes)<2)return window;
    var times=notes.map(function(n){return n.seconds}).sort(function(a,b){return a-b}),gaps=[];
    for(var i=1;i<times.length;++i)if(times[i]-times[i-1]>.001)gaps.push(times[i]-times[i-1]);
    gaps.sort(function(a,b){return a-b});if(gaps.length<6)return window;
    var low=gaps[Math.floor(gaps.length*.2)],high=gaps[Math.floor(gaps.length*.85)];
    for(var pass=0;pass<6;++pass){
        var boundary=Math.sqrt(low*high),short=[],long=[];
        gaps.forEach(function(gap){(gap<=boundary?short:long).push(gap)});
        if(short.length<3||long.length<3)return window;
        low=median(short);high=median(long);
    }
    if(high/low<2.5||low>.085)return window;
    // When short gaps are the minority they may join a rolled chord's tail
    // to the next bass attack. Keep the events separate for the score-aware
    // merge instead of deciding those groups from timing alone.
    if(short.length<gaps.length*.5){window.gap=.001;window.span=.001;return window;}
    window.gap=Math.max(.012,Math.min(.085,Math.sqrt(low*high)));
    window.span=Math.max(.035,Math.min(.18,high*.85));window.adaptive=true;
    return window;
}
function pitchCost(a,b) {
    var hits=0;
    for(var i=0;i<a.pitches.length;++i)if(b.pitches.indexOf(a.pitches[i])>=0)++hits;
    return .75*(1-hits/Math.max(1,a.pitches.length))+.25*(1-hits/Math.max(1,b.pitches.length));
}
function typicalIOI(a) {
    var intervals=[];for(var i=1;i<a.length;++i)if(a[i].time>a[i-1].time)intervals.push(a[i].time-a[i-1].time);
    return median(intervals)||1;
}
function onsetCost(s,p,i,j,tap,si,pi) {
    var a=s[i],b=p[j],pitch=tap?0:pitchCost(a,b);
    var time=0;
    if(i>0&&j>0){
        var x=(a.time-s[i-1].time)/si,y=(b.time-p[j-1].time)/pi;
        time=Math.min(1.5,Math.abs(Math.log(Math.max(.03,x)/Math.max(.03,y))))*.35;
    }
    var phase=Math.abs((a.time-s[0].time)/si-(b.time-p[0].time)/pi);
    // Rubato can move a long performance far from one global tempo. Pitch-mode
    // alignment therefore follows local intervals; tap mode still needs its
    // sequence phase when no pitch evidence is available.
    return pitch*1.8+time+(tap?Math.min(1.2,phase*.25):0);
}
function path(s,p,tap) {
    if(!s.length||!p.length)return [];
    var n=s.length,m=p.length,band=Math.min(Math.max(48,Math.abs(n-m)+24,Math.ceil(Math.max(n,m)*.08)),384);
    if(Math.abs(n-m)>band)throw new Error("演奏与目标段落的起音数量相差较多。请选择演奏时间片段，或在对应区固定一对音。");
    var width=2*band+1,trace=new Uint8Array((n+1)*width),forward=new Float64Array((n+1)*width),previous=new Float64Array(m+1),current=new Float64Array(m+1),si=typicalIOI(s),pi=typicalIOI(p),gap=.95;
    forward.fill(Infinity);
    for(var j=0;j<=Math.min(m,band);++j)forward[j+band]=j*gap;
    for(var j=0;j<=m;++j)previous[j]=j*gap;
    for(var i=1;i<=n;++i){
        for(j=0;j<=m;++j)current[j]=Infinity;
        current[0]=i*gap;
        if(i<=band)forward[i*width-i+band]=current[0];
        var lo=Math.max(1,i-band),hi=Math.min(m,i+band);
        for(j=lo;j<=hi;++j){
            var diagonal=previous[j-1]+onsetCost(s,p,i-1,j-1,tap,si,pi);
            var deletion=previous[j]+gap,insertion=current[j-1]+gap,kind=1,value=diagonal;
            if(deletion<value){value=deletion;kind=2}if(insertion<value){value=insertion;kind=3}
            current[j]=value;trace[i*width+j-i+band]=kind;forward[i*width+j-i+band]=value;
        }
        var tmp=previous;previous=current;current=tmp;
    }
    if(!isFinite(previous[m]))throw new Error("这段暂时没有找到连续对应。请在对应区固定一对音后重新对应。");
    var pairs=[],i=n,j=m;
    while(i>0&&j>0){
        var kind=trace[i*width+j-i+band];
        if(kind===1){pairs.push({s:i-1,p:j-1});--i;--j}
        else if(kind===2)--i;else if(kind===3)--j;else break;
    }
    pairs.reverse();
    // Compare other complete paths, including skipped notes, at each matched
    // attack. Repeated figures with missing attacks can have several equally
    // plausible positions even when their pitches agree.
    var optimum=previous[m],matchedRows={};
    pairs.forEach(function(pair){matchedRows[pair.s+1]=pair});
    var below=new Float64Array(m+2),back=new Float64Array(m+2);
    below.fill(Infinity);
    for(i=n;i>=0;--i){
        back.fill(Infinity);
        var lo=Math.max(0,i-band),hi=Math.min(m,i+band),pair=matchedRows[i];
        for(j=hi;j>=lo;--j){
            var remaining=i===n&&j===m?0:Math.min(
                i<n?below[j]+gap:Infinity,
                j<m?back[j+1]+gap:Infinity,
                i<n&&j<m?below[j+1]+onsetCost(s,p,i,j,tap,si,pi):Infinity);
            back[j]=remaining;
            if(pair&&j>0&&j!==pair.p+1){
                var total=forward[(i-1)*width+(j-1)-(i-1)+band]+
                    onsetCost(s,p,i-1,j-1,tap,si,pi)+remaining;
                if(total<=optimum+.08)pair.ambiguous=true;
            }
        }
        var swap=below;below=back;back=swap;
    }
    return pairs;
}
// Locate a score passage anywhere in a longer performance. Prefix/suffix are
// free; insertions and missing notes inside the passage still have a cost.
// Consecutive performed attacks can be one written, rolled chord.
function rolledOnsets(performed) {
    var out=[[],[],[],[]];
    for(var size=2;size<=3;++size)for(var last=size-1;last<performed.length;++last){
        var first=last-size+1;
        if(performed[last].lastTime-performed[first].time>.3)continue;
        var pitches=[];
        for(var k=first;k<=last;++k)performed[k].pitches.forEach(function(pitch){if(pitches.indexOf(pitch)<0)pitches.push(pitch)});
        if(pitches.length<=performed[last].pitches.length)continue;
        out[size][last]={first:first,pitches:pitches,time:performed[first].time};
    }
    return out;
}
function rolledCost(score,roll,size,tap){
    if(tap||!roll||score.pitches.length<2)return Infinity;
    // The previous raw event may still belong to the preceding roll. Its
    // distance is not the interval between the two written attacks. The
    // following fine pass evaluates intervals after these groups are merged.
    return pitchCost(score,roll)*1.8+.12*(size-1);
}
function mergeOnsets(performed,pairs){
    var spans={};(pairs||[]).forEach(function(pair){if(pair.pEnd>pair.p)spans[pair.p]=pair.pEnd});
    var out=[];
    for(var first=0;first<performed.length;++first){
        var last=spans[first];if(last===undefined){out.push(performed[first]);continue;}
        var group={time:performed[first].time,lastTime:performed[last].lastTime,id:performed[first].id,notes:[],pitches:[]};
        for(var k=first;k<=last;++k){Array.prototype.push.apply(group.notes,performed[k].notes);performed[k].pitches.forEach(function(pitch){if(group.pitches.indexOf(pitch)<0)group.pitches.push(pitch)})}
        out.push(group);first=last;
    }
    return out;
}

function locatePassage(s,p,tap) {
    var n=s.length,m=p.length;
    if(n*m>8000000 && n>32) {
        var prefix=locatePassage(s.slice(0,Math.min(32,n)),p,tap);
        var start=Math.max(0,prefix.start-3),end=Math.min(m,start+n*2+32);
        var window=p.slice(start,end);
        if(n*window.length>8000000)return {start:start,end:Math.min(m,start+Math.ceil((s[n-1].time-s[0].time)/typicalIOI(s))+1),alternatives:[],cost:prefix.cost};
        var local=locatePassage(s,window,tap);
        function shift(value){value.start+=start;value.end+=start;(value.pairs||[]).forEach(function(pair){pair.p+=start;if(pair.pEnd!==undefined)pair.pEnd+=start;})}shift(local);local.alternatives.forEach(shift);return local;
    }
    var trace=new Uint8Array((n+1)*(m+1)),previous=new Float64Array(m+1),current=new Float64Array(m+1);
    var si=typicalIOI(s),pi=typicalIOI(p),gap=.95,rolls=rolledOnsets(p);
    for(var i=1;i<=n;++i) {
        current[0]=i*gap;
        for(var j=1;j<=m;++j) {
            var time=0;
            if(i>1&&j>1)time=Math.min(1.5,Math.abs(Math.log(Math.max(.03,(s[i-1].time-s[i-2].time)/si)/Math.max(.03,(p[j-1].time-p[j-2].time)/pi))))*.35;
            var diagonal=previous[j-1]+(tap?0:pitchCost(s[i-1],p[j-1])*1.8)+time;
            var deletion=previous[j]+gap,insertion=current[j-1]+gap,kind=1,value=diagonal;
            if(deletion<value){value=deletion;kind=2}if(insertion<value){value=insertion;kind=3}
            for(var size=2;size<=3&&size<=j;++size){var rolled=previous[j-size]+rolledCost(s[i-1],rolls[size][j-1],size,tap);if(rolled<value){value=rolled;kind=size+2;}}current[j]=value;trace[i*(m+1)+j]=kind;
        }
        var swap=previous;previous=current;current=swap;
    }
    var endings=[];for(j=1;j<=m;++j)endings.push({end:j,cost:previous[j]});
    endings.sort(function(a,b){return a.cost-b.cost||a.end-b.end});
    function recover(end) {
        var row=n,col=end,pairs=[];
        while(row>0&&col>0){var kind=trace[row*(m+1)+col];if(kind===1){pairs.push({s:row-1,p:col-1});--row;--col}else if(kind===2)--row;else if(kind>=4){var size=kind-2;pairs.push({s:row-1,p:col-size,pEnd:col-1});--row;col-=size;}else --col;}
        pairs.reverse();return {start:pairs.length?pairs[0].p:col,end:end,pairs:pairs};
    }
    var best=recover(endings[0].end);best.cost=endings[0].cost/n;best.alternatives=[];
    for(i=1;i<endings.length&&best.alternatives.length<3;++i){var candidate=endings[i];
        if(candidate.cost>endings[0].cost+Math.max(.4,n*.08))break;
        var other=recover(candidate.end);
        if(Math.abs(other.start-best.start)<Math.max(2,n*.5)||best.alternatives.some(function(v){return Math.abs(v.start-other.start)<Math.max(2,n*.5)}))continue;
        other.cost=candidate.cost/n;best.alternatives.push(other);
    }
    return best;
}
function align(input) {
    var score=input.notes||[];
    if(input.repeats&&input.repeats.length){
        var expanded=[],counts={};
        input.repeats.forEach(function(segment){
            score.forEach(function(n){
                if(n.tick<segment.tick||n.tick>=segment.end)return;
                var occurrence=(counts[n.id]||0)+1;counts[n.id]=occurrence;
                if(input.occurrence>0&&occurrence!==input.occurrence)return;
                var v=clone(n);v.sourceId=n.id;v.id=n.id+"@"+occurrence;v.logicalId=(n.logicalId||n.id)+"@"+occurrence;v.occurrence=occurrence;v.alignmentTick=segment.utick+n.tick-segment.tick;expanded.push(v);
            });
        });score=expanded;
    }
    var contextGroups=groups(score,true,input.mode==="tap");
    if(input.role===1){var highest={};score.forEach(function(n){highest[n.tick]=Math.max(highest[n.tick]||0,n.pitch)});score=score.filter(function(n){return n.pitch===highest[n.tick]})}
    else if(input.role===2)score=score.filter(function(n){return n.track===input.track});
    var performance=(input.performance||[]).filter(function(n){return n.seconds>=(input.from||0)&&n.seconds<=(input.until===undefined?Infinity:input.until)}),tap=input.mode==="tap";
    var linkPerformance=performance;
    var window=performanceWindow(contextGroups,performance,tap);
    var s=groups(score,true,tap),p=groups(performance,false,tap,window),anchors=input.anchors||[],links=input.links||[];
    if(!s.length||!p.length)return {matches:[],missing:score,extra:performance,pending:0};
    var scoreIds={},midiIds={};s.forEach(function(g){g.notes.forEach(function(n){scoreIds[n.id]=true})});performance.forEach(function(n){midiIds[n.id]=true});
    anchors=anchors.filter(function(a){return scoreIds[a.scoreId]&&midiIds[a.midiId]});
    if(input.role>0&&!tap){
        var pitches={},manualMidi={};
        s.forEach(function(group){group.pitches.forEach(function(pitch){for(var delta=-2;delta<=2;++delta)pitches[pitch+delta]=true})});
        anchors.concat(links.filter(function(link){return !link.disabled&&scoreIds[link.scoreId]})).forEach(function(link){manualMidi[link.midiId]=true});
        p=p.filter(function(group){return group.notes.some(function(note){return pitches[note.pitch]||manualMidi[note.id]})});
        if(!p.length)return {matches:[],missing:score,extra:performance,pending:0};
    }

    if(anchors.length) {
        var positions=anchors.map(function(a){return {s:s.findIndex(function(g){return g.notes.some(function(n){return n.id===a.scoreId})}),p:p.findIndex(function(g){return g.notes.some(function(n){return n.id===a.midiId})})}}).sort(function(a,b){return a.s-b.s});
        for(var at=1;at<positions.length;++at)if(positions[at].s!==positions[at-1].s||positions[at].p!==positions[at-1].p)if(positions[at].s<=positions[at-1].s||positions[at].p<=positions[at-1].p)throw new Error("固定对应的先后顺序交叉了，请调整其中一对音。");
        var first=positions[0],last=positions[positions.length-1],si=typicalIOI(s),pi=typicalIOI(p)*Math.max(1,median(s.map(function(group){return group.pitches.length}))/Math.max(1,median(p.map(function(group){return group.pitches.length}))));
        var from=p[first.p].time-((s[first.s].time-s[0].time)/si*2+4)*pi;
        var until=p[last.p].lastTime+((s[s.length-1].time-s[last.s].time)/si*2+4)*pi;
        performance=performance.filter(function(n){return n.seconds>=from&&n.seconds<=until});p=groups(performance,false,tap,window);
        if(!tap&&p.length>s.length*1.2)p=mergeOnsets(p,locatePassage(s,p,false).pairs);
    }
    var passage=null;
    if(!anchors.length && p.length>s.length*1.2 && input.findPassage!==false) {
        passage=locatePassage(s,p,tap);
        var wholeGroups=p;
        passage.candidates=[{from:p[passage.start].time,until:p[Math.max(passage.start,passage.end-1)].lastTime+.001,cost:passage.cost}].concat(passage.alternatives.map(function(v){return {from:wholeGroups[v.start].time,until:wholeGroups[Math.max(v.start,v.end-1)].lastTime+.001,cost:v.cost};}));
        var choice=input.passageChoice||0,choiceValid=typeof input.passageChoice==="number"&&input.passageChoice>=0&&input.passageChoice<passage.candidates.length;
        if(typeof input.passageFrom==="number"&&input.passageFrom>=0){
            var distance=Infinity;
            passage.candidates.forEach(function(v,index){var delta=Math.abs(v.from-input.passageFrom);if(delta<distance){distance=delta;choice=index}});
            choiceValid=distance<=Math.max(.35,typicalIOI(p)*2);
        }
        var chosen=choice>0&&passage.alternatives[choice-1]?passage.alternatives[choice-1]:passage;
        var begin=p[chosen.start].time,end=p[Math.max(chosen.start,chosen.end-1)].lastTime+.001;
        performance=performance.filter(function(v){return v.seconds>=begin&&v.seconds<=end});
        p=mergeOnsets(wholeGroups,chosen.pairs).filter(function(group){return group.time>=begin&&group.time<end});
        passage.from=begin;passage.until=end;
        passage.alternatives=passage.candidates.slice(1);
        passage.ambiguous=passage.alternatives.some(function(v){return v.cost-passage.candidates[0].cost<=.03});
        passage.chosen=choiceValid;passage.chosenIndex=choiceValid?choice:-1;
    }
    var cuts=[{s:-1,p:-1}],fixed=[];
    anchors.forEach(function(a){
        var x=s.findIndex(function(g){return g.notes.some(function(n){return n.id===a.scoreId})});
        var y=p.findIndex(function(g){return g.notes.some(function(n){return n.id===a.midiId})});
        if(x<0||y<0)throw new Error("固定对应的音已变化，请重新选择这一对音。");
        fixed.push({s:x,p:y});
    });
    fixed.sort(function(a,b){return a.s-b.s});
    fixed.forEach(function(a){var last=cuts[cuts.length-1];if(a.s===last.s&&a.p===last.p)return;if(a.s<=last.s||a.p<=last.p)throw new Error("固定对应的先后顺序交叉了，请调整其中一对音。");cuts.push(a)});
    cuts.push({s:s.length,p:p.length});
    var cacheKey=(input.cacheKey||"")+":"+window.gap+":"+window.span+":"+p.map(function(group){return group.id}).join("|");
    var coarse=[],matches=[],usedScore={},usedMidi={},segments={},cached=cacheKey===input.previousKey?(input.segments||{}):{};
    for(var c=1;c<cuts.length;++c){
        var a=cuts[c-1],b=cuts[c],key=[a.s,a.p,b.s,b.p].join(":"),local=cached[key]||path(s.slice(a.s+1,b.s),p.slice(a.p+1,b.p),tap);segments[key]=local;
        local.forEach(function(v){coarse.push({s:v.s+a.s+1,p:v.p+a.p+1,ambiguous:v.ambiguous})});
        if(b.s<s.length)coarse.push({s:b.s,p:b.p,anchor:true});
    }
    var scale=typicalIOI(p)/typicalIOI(s),reserved={},coarseByScore={},uncertainNotes={};
    coarse.forEach(function(pair){
        coarseByScore[pair.s]=pair;
        p[pair.p].notes.forEach(function(n){
            if(s[pair.s].pitches.indexOf(n.pitch)>=0)reserved[n.id]=pair.s;
        });
    });
    // In a rapid two-note figure, one lost attack can leave several following
    // positions possible. Keep the affected figure together for correction.
    var run=[],runPitches=[];
    function finishRun(){
        if(run.length>=4){
            var affected=run.some(function(index){var pair=coarseByScore[index];return !pair||pair.ambiguous||pitchCost(s[index],p[pair.p])>.34});
            [run[0]-1,run[run.length-1]+1].forEach(function(index){
                var pair=coarseByScore[index];if(pair&&pitchCost(s[index],p[pair.p])>.34)affected=true;
            });
            if(affected)run.forEach(function(index){s[index].notes.forEach(function(n){uncertainNotes[n.id]=true})});
        }
        run=[];runPitches=[];
    }
    s.forEach(function(group,index){
        if(group.pitches.length!==1){finishRun();return;}
        var pitch=group.pitches[0],previous=run.length?s[run[run.length-1]]:null;
        if(previous&&(group.time-previous.time>240||(runPitches.length===2&&runPitches.indexOf(pitch)<0)))finishRun();
        run.push(index);if(runPitches.indexOf(pitch)<0)runPitches.push(pitch);
    });
    finishRun();
    coarse.forEach(function(pair,index){
        var sg=s[pair.s],pg=p[pair.p],cost=tap?0:pitchCost(sg,pg);
        var repeated=!!pair.ambiguous;
        var candidates=[];
        // Adjacent rolling events remain separate and can match this onset.
        for(var k=Math.max(0,pair.p-2);k<Math.min(p.length,pair.p+3);++k){
            if(Math.abs(p[k].time-pg.time)>.18)continue;
            p[k].notes.forEach(function(n){
                if(!usedMidi[n.id]&&(reserved[n.id]===undefined||reserved[n.id]===pair.s))candidates.push(n);
            });
        }
        sg.notes.slice().sort(function(x,y){return x.pitch-y.pitch}).forEach(function(n){
            var best=null,bestCost=Infinity;
            candidates.forEach(function(m){
                if(usedMidi[m.id])return;
                var distance=(tap?0:Math.abs(m.pitch-n.pitch)*.4)+Math.abs(m.seconds-pg.time);
                if(distance<bestCost){best=m;bestCost=distance}
            });
            if(!best||(!tap&&Math.abs(best.pitch-n.pitch)>2))return;
            var exact=tap||n.pitch===best.pitch,confirmed=!!pair.anchor&&exact;
            var confidence=confirmed?1:(!exact||cost>.34||repeated?.55:.88),uncertainty="";
            if(repeated)uncertainty="附近有几种相近的对应，请核对音名和演奏时间。";
            var nearbySame=[];
            for(var adjacent=Math.max(0,pair.p-3);adjacent<Math.min(p.length,pair.p+4);++adjacent){
                if(Math.abs(p[adjacent].time-pg.time)>.3)continue;
                p[adjacent].notes.forEach(function(m){
                    if(m.pitch===n.pitch&&!usedMidi[m.id]&&(reserved[m.id]===undefined||reserved[m.id]===pair.s))nearbySame.push(m);
                });
            }
            var sameScore=sg.notes.filter(function(v){return v.pitch===n.pitch}).length;
            if(!confirmed&&uncertainNotes[n.id]){confidence=Math.min(confidence,.55);uncertainty="这段快速反复音有漏弹或错位，请在对应区核对。";}
            if(!confirmed&&nearbySame.length>sameScore){confidence=Math.min(confidence,.55);uncertainty="附近弹了几次同音，请选择这个谱音对应的那一次。";}
            if(passage&&passage.ambiguous&&!passage.chosen&&!confirmed){confidence=Math.min(confidence,.55);uncertainty="演奏中有相似段落，请先选择演奏片段的时间。";}
            if(!confirmed&&(best.ambiguous||n.grace)){confidence=Math.min(confidence,.5);uncertainty="装饰音或重叠同音需要核对具体起音。";}
            var row={scoreId:n.id,midiId:best.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,performedPitch:best.pitch,seconds:best.seconds,velocity:best.velocity,confidence:confidence,confirmed:confirmed,status:exact?"匹配":"错音待确认",uncertainty:uncertainty,source:"measured",original:n};
            matches.push(row);usedScore[n.id]=true;usedMidi[best.id]=true;
        });
    });
    links.forEach(function(link){
        var n=score.find(function(v){return v.id===link.scoreId});
        if(!n)return;
        if(link.disabled){matches=matches.filter(function(v){return v.scoreId!==n.id});return;}
        var m=linkPerformance.find(function(v){return v.id===link.midiId});if(!m)return;
        matches=matches.filter(function(v){return v.scoreId!==n.id&&v.midiId!==m.id});
        matches.push({scoreId:n.id,midiId:m.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,performedPitch:m.pitch,seconds:m.seconds,velocity:m.velocity,confidence:1,confirmed:true,status:"人工对应",source:"measured",original:n});
    });
    usedScore={};usedMidi={};matches.forEach(function(v){usedScore[v.scoreId]=true;usedMidi[v.midiId]=true});
    var matchedById={},pitchNotes={};
    matches.forEach(function(m){matchedById[m.scoreId]=m});
    s.forEach(function(group,index){group.notes.forEach(function(n){
        var key=n.track+":"+n.pitch;if(!pitchNotes[key])pitchNotes[key]=[];
        pitchNotes[key].push({note:n,group:index});
    })});
    Object.keys(pitchNotes).forEach(function(key){
        var line=pitchNotes[key];
        line.forEach(function(value,index){
            if(usedScore[value.note.id])return;
            for(var step=-1;step<=1;step+=2){
                var neighbor=line[index+step];if(!neighbor)continue;
                var m=matchedById[neighbor.note.id];
                if(m&&!m.confirmed&&Math.abs(s[value.group].time-s[neighbor.group].time)<=480&&pitchCost(s[value.group],s[neighbor.group])===0){
                    m.confidence=Math.min(m.confidence,.55);m.status="同音待核对";m.uncertainty="相邻位置有未对应的同音，请核对这次起音属于哪个谱音。";
                }
            }
        });
    });
    if(anchors.length&&coarse.length) {
        var begin=p[coarse[0].p].time,end=p[coarse[coarse.length-1].p].lastTime+.001;
        performance=performance.filter(function(n){return n.seconds>=begin&&n.seconds<=end});
        passage={from:begin,until:end,candidates:[],alternatives:[],anchored:true};
    }
    matches.sort(function(a,b){return (a.original.alignmentTick||a.tick)-(b.original.alignmentTick||b.tick)||a.track-b.track||a.index-b.index});
    return {matches:matches,displayMatches:matches.slice().sort(function(a,b){return a.tick-b.tick||a.track-b.track||a.index-b.index}),missing:score.filter(function(n){return !n.tieBack&&!usedScore[n.id]}),extra:performance.filter(function(n){return !usedMidi[n.id]}),segments:segments,cacheKey:cacheKey,pending:matches.filter(function(v){return !v.confirmed&&v.confidence<.8}).length,scale:scale,passage:passage};
}
// Tempo transfer uses the complete written clock, including protected marks,
// rests and curve intervals. Only value data crosses the worker boundary.
function tempoTransfer(input,confirmed){
    var config=input.config||{},kept=[],byTick={};
    confirmed.filter(function(m){return !m.original.grace}).forEach(function(m){if(!byTick[m.tick])byTick[m.tick]=[];byTick[m.tick].push(m)});
    var anchors=Object.keys(byTick).map(Number).sort(function(a,b){return a-b}).map(function(tick){return {tick:tick,seconds:median(byTick[tick].map(function(m){return m.seconds})),note:byTick[tick][0].original}});
    if(anchors.length<4)return {tempos:[],kept:[{reason:"至少对应四处不同的起音后，可以使用演奏速度。"}],summary:null};
    for(var i=1;i<anchors.length;++i)if(anchors[i].seconds<=anchors[i-1].seconds)return {tempos:[],kept:[{tick:anchors[i].tick,reason:"这几处演奏时间前后交叉，调整对应后可以使用速度。"}],summary:null};
    var context=input.tempoContext||{},scope=input.scope||{},start=scope.start===undefined?(context.start===undefined?anchors[0].tick:context.start):scope.start;
    var end=scope.end===undefined?(context.end===undefined?anchors[anchors.length-1].tick:context.end):scope.end;
    if(end<=start)return {tempos:[],kept:[{reason:"选择要采用速度的完整谱段。"}],summary:null};
    var base={},initial=anchors[0].note.bpm||120;
    if(context.points&&context.points.length)context.points.forEach(function(p){base[p.tick]={tick:p.tick,bpm:p.bpm,pause:p.pause||0,fixed:p.fixed!==false,owned:!!p.owned}});
    else {var previousRate;anchors.forEach(function(p){if(p.note.bpm&&(previousRate===undefined||p.note.bpm!==previousRate||p.note.tempoProtected))base[p.tick]={tick:p.tick,bpm:p.note.bpm,pause:0,fixed:true};previousRate=p.note.bpm;});}
    var baseTicks=Object.keys(base).map(Number).sort(function(a,b){return a-b}),baseRates=[],baseTimes=[],pauseSums=[];
    var activeRate=initial,previousTick=baseTicks.length?baseTicks[0]:start,time=0,pauseSum=0;
    baseTicks.forEach(function(tick){var p=base[tick];time+=(tick-previousTick)/480*60/activeRate+(p.pause||0);pauseSum+=p.pause||0;if(p.fixed)activeRate=p.bpm;baseTimes.push(time);baseRates.push(activeRate);pauseSums.push(pauseSum);previousTick=tick;});
    function upper(tick){var lo=0,hi=baseTicks.length;while(lo<hi){var mid=(lo+hi)>>1;if(baseTicks[mid]<=tick)lo=mid+1;else hi=mid;}return lo;}
    function baseAt(tick){var index=upper(tick)-1;return index<0?initial:baseRates[index];}
    function baseTime(tick){var index=upper(tick)-1;return index<0?tick/480*60/initial:baseTimes[index]+(tick-baseTicks[index])/480*60/baseRates[index];}
    function pauses(from,until){var first=upper(from)-1,last=upper(until)-1;return (last<0?0:pauseSums[last])-(first<0?0:pauseSums[first]);}
    function duration(events,from,until){
        if(events===base)return baseTime(until)-baseTime(from);
        var ticks=Object.keys(events).map(Number).sort(function(a,b){return a-b}),rate=initial,at=from,seconds=0;
        for(var j=0;j<ticks.length;++j){var tick=ticks[j],event=events[tick];if(tick<=from){if(event.fixed!==false)rate=event.bpm;continue;}if(tick>until)break;seconds+=(tick-at)/480*60/rate+(event.pause||0);at=tick;if(event.fixed!==false)rate=event.bpm;}
        return seconds+(until-at)/480*60/rate;
    }
    var originalDuration=duration(base,start,end),strength=Math.max(0,Math.min(1,(config.rubatoStrength===undefined?(config.strength===undefined?50:config.strength):config.rubatoStrength)/100));
    var keep=config.keepDuration!==false,rates=[],gaps=[];
    for(i=1;i<anchors.length;++i){var a=anchors[i-1],b=anchors[i],elapsed=b.seconds-a.seconds-pauses(a.tick,b.tick);if(elapsed<=0)return {tempos:[],kept:[{tick:b.tick,reason:"谱上的停顿长于这两处演奏间隔，请检查对应的演奏位置。"}],summary:null};rates.push((b.tick-a.tick)/480*60/elapsed);gaps.push(b.tick-a.tick);}
    function interval(tick){var lo=0,hi=anchors.length-1;while(lo+1<hi){var mid=(lo+hi)>>1;if(anchors[mid].tick<=tick)lo=mid;else hi=mid;}return Math.min(lo,anchors.length-2);}
    function mapped(tick){var j=interval(tick),a=anchors[j],b=anchors[j+1],span=b.seconds-a.seconds-pauses(a.tick,b.tick);return a.seconds+(tick-a.tick)/(b.tick-a.tick)*span+pauses(a.tick,tick);}
    var measuredDuration=mapped(end)-mapped(start),normalization=keep&&originalDuration>0?measuredDuration/originalDuration:1;
    var locks={},cuts=[start,end],ranges=context.protectedRanges||[],protectedTicks=context.protectedTicks||[];
    if(!context.points)anchors.forEach(function(p){if(p.note.tempoProtected)protectedTicks.push(p.tick)});
    protectedTicks=protectedTicks.filter(function(v){var t=typeof v==="number"?v:v.tick;return t>=start&&t<end});
    ranges=ranges.filter(function(r){return r.start<end&&r.end>start});
    protectedTicks.forEach(function(value){var tick=typeof value==="number"?value:value.tick;locks[tick]=true;if(tick>start&&tick<end)cuts.push(tick);kept.push({tick:tick,reason:typeof value==="object"&&value.reason?value.reason:"沿用谱上已有的速度记号。"});});
    ranges.forEach(function(range){if(range.start>start&&range.start<end)cuts.push(range.start);if(range.end>start&&range.end<end)cuts.push(range.end);kept.push({tick:Math.max(start,range.start),reason:range.reason||"沿用谱上已有的速度变化。"});});
    Object.keys(input.appliedTempos||{}).forEach(function(key){var tick=Number(key),saved=input.appliedTempos[key];if(tick>=start&&tick<=end&&(saved===true||Math.abs(saved.bpm-baseAt(tick))<.01)){locks[tick]=true;if(tick>start&&tick<end)cuts.push(tick);}});
    function protectedAt(tick){return locks[tick]||ranges.some(function(r){return tick>=r.start&&tick<r.end});}
    var nodes={},typical=median(gaps)||480;
    function addNode(tick,locked){
        if(tick<start||tick>end)return;
        var j=interval(tick),rate=rates[j];
        if(gaps[j]<=typical*3)rate=median(rates.slice(Math.max(0,j-1),Math.min(rates.length,j+2)));
        var old=baseAt(tick);
        var ease=keep?Math.max(0,Math.min(1,(tick-start)/(typical*2),(end-tick)/(typical*2))):1;
        var fixed=!!locked||!!protectedAt(tick),manual=!fixed&&(config.previewTempo||{})[tick]!==undefined;
        var target=fixed?old:manual?Math.max(5,Math.min(999,Number(config.previewTempo[tick]))):Math.max(5,Math.min(999,old+(rate*normalization-old)*strength*ease));
        nodes[tick]={tick:tick,oldBpm:old,bpm:target,locked:fixed||manual};
    }
    anchors.forEach(function(p){addNode(p.tick,false)});
    baseTicks.forEach(function(tick){if(base[tick].owned)addNode(tick,false)});
    (context.restoreTicks||[]).forEach(function(value){addNode(typeof value==="number"?value:value.tick,true)});
    if(keep){var inside=Object.keys(nodes).map(Number).filter(function(t){return t<end}).sort(function(a,b){return a-b});if(inside.length){addNode(inside[0],true);addNode(inside[inside.length-1],true);}}
    if(context.endWritable&&end===context.end||(context.writableTicks||[]).indexOf(end)>=0)addNode(end,true);
    else if(!keep&&context.scoreEnd!==undefined&&end<context.scoreEnd){var lastInside=anchors.filter(function(p){return p.tick<end&&p.tick>=start});if(lastInside.length)addNode(lastInside[lastInside.length-1].tick,true);}
    var all={};baseTicks.forEach(function(tick){all[tick]=true});Object.keys(nodes).forEach(function(tick){all[tick]=true});cuts.forEach(function(tick){all[tick]=true});
    var timeline=Object.keys(all).map(Number).sort(function(a,b){return a-b}),pieces=[],rate=baseAt(start),owner=null;
    for(i=0;i<timeline.length-1;++i){
        var tick=timeline[i],next=timeline[i+1];if(base[tick]&&base[tick].fixed){rate=base[tick].bpm;owner=null;}
        if(nodes[tick]){rate=nodes[tick].bpm;owner=nodes[tick].locked?null:tick;}
        if(tick>=start&&next<=end)pieces.push({start:tick,end:next,weight:(next-tick)/480*60,rate:rate,owner:owner});
    }
    cuts=keep?cuts.filter(function(tick,index,list){return list.indexOf(tick)===index}).sort(function(a,b){return a-b}):[start,end];
    var pieceIndex=0;
    for(var part=1;part<cuts.length;++part){
        var from=cuts[part-1],until=cuts[part],fixed=pauses(from,until),weights={};
        while(pieceIndex<pieces.length&&pieces[pieceIndex].end<=from)++pieceIndex;
        for(var at=pieceIndex;at<pieces.length&&pieces[at].end<=until;++at){var piece=pieces[at];if(piece.owner===null)fixed+=piece.weight/piece.rate;else weights[piece.owner]=(weights[piece.owner]||0)+piece.weight;}
        var keys=Object.keys(weights),desired=keep?duration(base,from,until):originalDuration*(1-strength)+measuredDuration*strength;
        if(!keys.length){if(Math.abs(desired-fixed)>1e-6){Object.keys(nodes).forEach(function(key){if(Number(key)>=from&&Number(key)<until)nodes[key].bpm=nodes[key].oldBpm});kept.push({tick:from,reason:"这处预览速度会改变保留的句长；扩大选段或关闭保持时长后可以继续调整。"});}continue;}
        var remaining=desired-fixed;
        function flexible(factor){var total=0;keys.forEach(function(key){total+=weights[key]/Math.max(5,Math.min(999,nodes[key].bpm*factor))});return total;}
        if(remaining<flexible(1e9)-1e-7||remaining>flexible(1e-9)+1e-7){
            Object.keys(nodes).forEach(function(key){if(Number(key)>=from&&Number(key)<until)nodes[key].bpm=nodes[key].oldBpm});kept.push({tick:from,reason:"这段保留原有速度后，已没有足够的可调范围；请扩大选段或调整采用比例。"});continue;
        }
        var lo=1e-9,hi=1e9;for(var pass=0;pass<80;++pass){var factor=Math.sqrt(lo*hi);if(flexible(factor)>remaining)lo=factor;else hi=factor;}
        keys.forEach(function(key){nodes[key].bpm=Math.max(5,Math.min(999,nodes[key].bpm*Math.sqrt(lo*hi)))});
    }
    // Keep restoration points even when their value equals the old score:
    // a preceding new tempo may otherwise continue beyond this range.
    var written={},tempos=[],cursor=0,active=initial;Object.keys(base).forEach(function(key){written[key]=clone(base[key])});
    Object.keys(nodes).map(Number).sort(function(a,b){return a-b}).forEach(function(tick){
        var node=nodes[tick];while(cursor<baseTicks.length&&baseTicks[cursor]<=tick){var p=base[baseTicks[cursor++]];if(p.fixed!==false)active=p.bpm;}
        if(Math.abs(active-node.bpm)<.00001)return;
        tempos.push({tick:tick,oldBpm:node.oldBpm,bpm:node.bpm});written[tick]={tick:tick,bpm:node.bpm,pause:base[tick]?base[tick].pause:0,fixed:true};active=node.bpm;
    });
    return {tempos:tempos,nodes:Object.keys(nodes).map(Number),kept:kept,summary:{ranges:1,originalDuration:originalDuration,performedDuration:measuredDuration,proposedDuration:duration(written,start,end),keepDuration:keep,protectedCount:protectedTicks.length+ranges.length}};
}
function candidates(input) {
    var result=clone(input.result),config=input.config||{},strength=(config.strength===undefined?50:config.strength)/100,velocity=[],tempos=[],kept=[],confirmed=result.matches.filter(function(m){return m.confirmed||m.confidence>=.8}),unlock=config.unlocked||[];
    confirmed.forEach(function(m){
        var n=m.original,vs=config.velocityStrength===undefined?strength:config.velocityStrength/100;
        if(!config.velocity)return;
        if(n.protected&&config.keepManual!==false&&unlock.indexOf(n.id)<0){kept.push({id:n.id,reason:n.protection||"保留手动调整"});return}
        if(config.velocityWriteMode==="follow"&&n.velocityBase<=0){kept.push({id:n.id,reason:"当前位置的力度参考为 0；选择逐音固定力度即可采用演奏值。"});return;}
        var measured=Math.max(1,Math.min(127,Math.round((m.velocity-(config.deviceCenter||64))*(config.deviceGain||1)+64)));
        var target=Math.max(1,Math.min(127,Math.round(n.velocity*(1-vs)+measured*vs)));
        velocity.push({id:n.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,writtenPitch:n.writtenPitch,writtenTpc:n.writtenTpc,graceIndex:n.graceIndex,oldType:n.velocityType,oldRaw:n.velocityRaw,oldBase:n.velocityBase,target:target,originalVelocity:n.velocity,writeMode:config.velocityWriteMode||"preserve",sourceSeconds:m.seconds,scoreSeconds:n.scoreSeconds===undefined?n.onSeconds:n.scoreSeconds,source:"measured",unlock:config.keepManual===false||unlock.indexOf(n.id)>=0});
    });
    if(config.accompaniment&&velocity.length){
        (input.notes||[]).forEach(function(n){
            if(n.tieBack||velocity.some(function(v){return v.id===n.id})||n.protected)return;
            var nearest=null,distance=Infinity;
            confirmed.forEach(function(m){if(Math.abs(m.tick-n.tick)<distance){distance=Math.abs(m.tick-n.tick);nearest=m}});
            if(!nearest||distance>480)return;
            var vs=config.velocityStrength===undefined?strength:config.velocityStrength/100,calibrated=Math.max(1,Math.min(127,(nearest.velocity-(config.deviceCenter||64))*(config.deviceGain||1)+64)),ratio=calibrated/Math.max(1,nearest.original.velocity),target=Math.round(n.velocity*(1-vs+vs*ratio));
            velocity.push({id:n.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,writtenPitch:n.writtenPitch,writtenTpc:n.writtenTpc,graceIndex:n.graceIndex,oldType:n.velocityType,oldRaw:n.velocityRaw,oldBase:n.velocityBase,target:Math.max(1,Math.min(127,target)),writeMode:config.velocityWriteMode||"preserve",source:"inferred",unlock:config.keepManual===false||unlock.indexOf(n.id)>=0});
        });
    }
    var sourceCounts={};velocity.forEach(function(v){var key=[v.tick,v.track,v.index,v.graceIndex].join(":");sourceCounts[key]=(sourceCounts[key]||0)+1});
    velocity=velocity.filter(function(v){var key=[v.tick,v.track,v.index,v.graceIndex].join(":");if(sourceCounts[key]>1){kept.push({id:v.id,reason:"反复多次对应：请选择一次出现后写回"});return false}return true});
    var repeatedTicks={};confirmed.forEach(function(m){var key=[m.tick,m.track,m.index].join(":");repeatedTicks[key]=(repeatedTicks[key]||0)+1});
    var repeated=Object.keys(repeatedTicks).some(function(k){return repeatedTicks[k]>1});
    if(config.rubato&&repeated)kept.push({reason:"Rubato 请先选择一次反复出现"});
    var tempoSummary=null,tempoNodes=[];
    if(config.rubato&&!repeated){
        var ranges=input.tempoRanges===undefined?[input.scope||{}]:input.tempoRanges;
        ranges.forEach(function(range){var local={};Object.keys(input).forEach(function(k){local[k]=input[k]});local.scope=range;var transfer=tempoTransfer(local,confirmed);
            Array.prototype.push.apply(tempos,transfer.tempos);Array.prototype.push.apply(tempoNodes,transfer.nodes||[]);Array.prototype.push.apply(kept,transfer.kept);
            if(transfer.summary){if(!tempoSummary)tempoSummary={ranges:0,originalDuration:0,performedDuration:0,proposedDuration:0,keepDuration:transfer.summary.keepDuration,protectedCount:0};["ranges","originalDuration","performedDuration","proposedDuration","protectedCount"].forEach(function(k){tempoSummary[k]+=transfer.summary[k]});}
        });
    }
    velocity.forEach(function(v){if(v.graceIndex===undefined)delete v.graceIndex});
    var velocityById={};velocity.forEach(function(v){velocityById[v.id]=v});
    return {revision:input.revision,scope:input.scope,keepDuration:config.keepDuration!==false,velocity:velocity,velocityById:velocityById,tempos:tempos,tempoSummary:tempoSummary,tempoNodes:tempoNodes,tempoScoped:input.tempoRanges!==undefined,kept:kept,protect:true};
}
