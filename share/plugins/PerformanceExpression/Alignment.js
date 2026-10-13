// SPDX-License-Identifier: GPL-2.0-or-later
// Pure value algorithms. No score, GUI, file, synth or model dependency.
function median(values) {
    var a=values.slice().sort(function(x,y){return x-y});
    return a.length ? a[Math.floor(a.length/2)] : 0;
}
function clone(value) {return JSON.parse(JSON.stringify(value))}
function groups(notes,score,tap) {
    var a=notes.filter(function(n){return !score || !n.tieBack}).slice();
    a.sort(function(x,y){return (score?((x.alignmentTick===undefined?x.tick:x.alignmentTick)-(y.alignmentTick===undefined?y.tick:y.alignmentTick)):x.seconds-y.seconds)||x.pitch-y.pitch});
    var out=[],seen={};
    for(var i=0;i<a.length;++i){
        var n=a[i],id=String(score?n.logicalId||n.id:n.id);
        if(score&&seen[id])continue;seen[id]=true;
        var time=score?(n.alignmentTick===undefined?n.tick:n.alignmentTick):n.seconds,last=out[out.length-1];
        // Rolling is only a coarse aid; preserve every original event below.
        if(!last || (score?time!==last.time:time-last.time>.035)){
            last={time:time,notes:[],pitches:[],id:id};out.push(last);
        }
        last.notes.push(n);if(last.pitches.indexOf(n.pitch)<0)last.pitches.push(n.pitch);
    }
    return out;
}
function pitchCost(a,b) {
    var hits=0;
    for(var i=0;i<a.pitches.length;++i)if(b.pitches.indexOf(a.pitches[i])>=0)++hits;
    return 1-hits/Math.max(a.pitches.length,b.pitches.length);
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
    return pitch*1.8+time+Math.min(1.2,phase*.25);
}
function path(s,p,tap) {
    var n=s.length,m=p.length,band=Math.min(Math.max(48,Math.abs(n-m)+24,Math.ceil(Math.max(n,m)*.08)),384);
    if(Math.abs(n-m)>band)throw new Error("长度差过大；请选取对应演奏片段或设锚点");
    var width=2*band+1,trace=new Uint8Array((n+1)*width),previous=new Float64Array(m+1),current=new Float64Array(m+1),si=typicalIOI(s),pi=typicalIOI(p),gap=.95;
    for(var j=0;j<=m;++j)previous[j]=j*gap;
    for(var i=1;i<=n;++i){
        for(j=0;j<=m;++j)current[j]=Infinity;
        current[0]=i*gap;
        var lo=Math.max(1,i-band),hi=Math.min(m,i+band);
        for(j=lo;j<=hi;++j){
            var diagonal=previous[j-1]+onsetCost(s,p,i-1,j-1,tap,si,pi);
            var deletion=previous[j]+gap,insertion=current[j-1]+gap,kind=1,value=diagonal;
            if(deletion<value){value=deletion;kind=2}if(insertion<value){value=insertion;kind=3}
            current[j]=value;trace[i*width+j-i+band]=kind;
        }
        var tmp=previous;previous=current;current=tmp;
    }
    if(!isFinite(previous[m]))throw new Error("匹配超出窗口；请设锚点分段");
    var pairs=[],i=n,j=m;
    while(i>0&&j>0){
        var kind=trace[i*width+j-i+band];
        if(kind===1){pairs.push({s:i-1,p:j-1});--i;--j}
        else if(kind===2)--i;else if(kind===3)--j;else break;
    }
    return pairs.reverse();
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
    if(input.role===1){var highest={};score.forEach(function(n){highest[n.tick]=Math.max(highest[n.tick]||0,n.pitch)});score=score.filter(function(n){return n.pitch===highest[n.tick]})}
    else if(input.role===2)score=score.filter(function(n){return n.track===input.track});
    var performance=(input.performance||[]).filter(function(n){return n.seconds>=(input.from||0)&&n.seconds<=(input.until===undefined?Infinity:input.until)}),tap=input.mode==="tap";
    var s=groups(score,true,tap),p=groups(performance,false,tap),anchors=input.anchors||[],links=input.links||[];
    if(!s.length||!p.length)return {matches:[],missing:score,extra:performance,pending:0};
    var cuts=[{s:-1,p:-1}],fixed=[];
    anchors.forEach(function(a){
        var x=s.findIndex(function(g){return g.notes.some(function(n){return n.id===a.scoreId})});
        var y=p.findIndex(function(g){return g.notes.some(function(n){return n.id===a.midiId})});
        if(x<0||y<0)throw new Error("锚点来源已变化");
        fixed.push({s:x,p:y});
    });
    fixed.sort(function(a,b){return a.s-b.s});
    fixed.forEach(function(a){var last=cuts[cuts.length-1];if(a.s<=last.s||a.p<=last.p)throw new Error("锚点必须单调，不能交叉");cuts.push(a)});
    cuts.push({s:s.length,p:p.length});
    var coarse=[],matches=[],usedScore={},usedMidi={},segments={},cached=input.cacheKey===input.previousKey?(input.segments||{}):{};
    for(var c=1;c<cuts.length;++c){
        var a=cuts[c-1],b=cuts[c],key=[a.s,a.p,b.s,b.p].join(":"),local=cached[key]||path(s.slice(a.s+1,b.s),p.slice(a.p+1,b.p),tap);segments[key]=local;
        local.forEach(function(v){coarse.push({s:v.s+a.s+1,p:v.p+a.p+1})});
        if(b.s<s.length)coarse.push({s:b.s,p:b.p,anchor:true});
    }
    var scale=typicalIOI(p)/typicalIOI(s);
    coarse.forEach(function(pair,index){
        var sg=s[pair.s],pg=p[pair.p],cost=tap?0:pitchCost(sg,pg);
        var repeated=tap;
        if(index>0&&index+1<coarse.length){
            var left=s[coarse[index-1].s],right=s[coarse[index+1].s];
            repeated=tap?Math.abs((sg.time-left.time)-(right.time-sg.time))<.001:
                pitchCost(left,sg)===0&&pitchCost(right,sg)===0;
        }
        var candidates=[];
        // Adjacent rolling events remain separate and can match this onset.
        for(var k=Math.max(0,pair.p-2);k<Math.min(p.length,pair.p+3);++k){
            if(Math.abs(p[k].time-pg.time)>.18)continue;
            p[k].notes.forEach(function(n){if(!usedMidi[n.id])candidates.push(n)});
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
            var confidence=confirmed?1:(!exact||cost>.34||repeated?.55:.88);
            if(best.ambiguous||n.grace)confidence=Math.min(confidence,.5);
            var row={scoreId:n.id,midiId:best.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,seconds:best.seconds,velocity:best.velocity,confidence:confidence,confirmed:confirmed,status:exact?"匹配":"错音待确认",source:"measured",original:n};
            matches.push(row);usedScore[n.id]=true;usedMidi[best.id]=true;
        });
    });
    links.forEach(function(link){
        var n=score.find(function(v){return v.id===link.scoreId}),m=performance.find(function(v){return v.id===link.midiId});
        if(!n||!m)return;
        matches=matches.filter(function(v){return v.scoreId!==n.id&&v.midiId!==m.id});
        if(!link.disabled)matches.push({scoreId:n.id,midiId:m.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,seconds:m.seconds,velocity:m.velocity,confidence:1,confirmed:true,status:"人工对应",source:"measured",original:n});
    });
    usedScore={};usedMidi={};matches.forEach(function(v){usedScore[v.scoreId]=true;usedMidi[v.midiId]=true});
    matches.sort(function(a,b){return (a.original.alignmentTick||a.tick)-(b.original.alignmentTick||b.tick)||a.track-b.track||a.index-b.index});
    return {matches:matches,displayMatches:matches.slice().sort(function(a,b){return a.tick-b.tick||a.track-b.track||a.index-b.index}),missing:score.filter(function(n){return !n.tieBack&&!usedScore[n.id]}),extra:performance.filter(function(n){return !usedMidi[n.id]}),segments:segments,cacheKey:input.cacheKey||"",pending:matches.filter(function(v){return !v.confirmed&&v.confidence<.8}).length,scale:scale};
}
function candidates(input) {
    var result=clone(input.result),config=input.config||{},strength=(config.strength===undefined?50:config.strength)/100,velocity=[],tempos=[],kept=[],confirmed=result.matches.filter(function(m){return m.confirmed||m.confidence>=.8}),unlock=config.unlocked||[];
    confirmed.forEach(function(m){
        var n=m.original,vs=config.velocityStrength===undefined?strength:config.velocityStrength/100;
        if(!config.velocity)return;
        if(n.protected&&unlock.indexOf(n.id)<0){kept.push({id:n.id,reason:n.protection||"已有手工演奏值"});return}
        var measured=Math.max(1,Math.min(127,Math.round((m.velocity-(config.deviceCenter||64))*(config.deviceGain||1)+64)));
        var target=Math.max(1,Math.min(127,Math.round(n.velocity*(1-vs)+measured*vs)));
        velocity.push({id:n.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,writtenPitch:n.writtenPitch,writtenTpc:n.writtenTpc,graceIndex:n.graceIndex,oldType:n.velocityType,oldRaw:n.velocityRaw,oldBase:n.velocityBase,target:target,source:"measured",unlock:unlock.indexOf(n.id)>=0});
    });
    if(config.accompaniment&&velocity.length){
        (input.notes||[]).forEach(function(n){
            if(n.tieBack||velocity.some(function(v){return v.id===n.id})||n.protected)return;
            var nearest=null,distance=Infinity;
            confirmed.forEach(function(m){if(Math.abs(m.tick-n.tick)<distance){distance=Math.abs(m.tick-n.tick);nearest=m}});
            if(!nearest||distance>480)return;
            var vs=config.velocityStrength===undefined?strength:config.velocityStrength/100,calibrated=Math.max(1,Math.min(127,(nearest.velocity-(config.deviceCenter||64))*(config.deviceGain||1)+64)),ratio=calibrated/Math.max(1,nearest.original.velocity),target=Math.round(n.velocity*(1-vs+vs*ratio));
            velocity.push({id:n.id,tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,writtenPitch:n.writtenPitch,writtenTpc:n.writtenTpc,graceIndex:n.graceIndex,oldType:n.velocityType,oldRaw:n.velocityRaw,oldBase:n.velocityBase,target:Math.max(1,Math.min(127,target)),source:"inferred"});
        });
    }
    var sourceCounts={};velocity.forEach(function(v){var key=[v.tick,v.track,v.index,v.graceIndex].join(":");sourceCounts[key]=(sourceCounts[key]||0)+1});
    velocity=velocity.filter(function(v){var key=[v.tick,v.track,v.index,v.graceIndex].join(":");if(sourceCounts[key]>1){kept.push({id:v.id,reason:"反复多次对应：请选择一次出现后写回"});return false}return true});
    var repeatedTicks={};confirmed.forEach(function(m){var key=[m.tick,m.track,m.index].join(":");repeatedTicks[key]=(repeatedTicks[key]||0)+1});
    var repeated=Object.keys(repeatedTicks).some(function(k){return repeatedTicks[k]>1});
    if(config.rubato&&repeated)kept.push({reason:"Rubato 请先选择一次反复出现"});
    if(config.rubato&&!repeated){
        var rs=config.rubatoStrength===undefined?strength:config.rubatoStrength/100;
        var byTick={};confirmed.filter(function(m){return !m.original.grace}).forEach(function(m){if(!byTick[m.tick])byTick[m.tick]=[];byTick[m.tick].push(m)});
        var ticks=Object.keys(byTick).map(Number).sort(function(a,b){return a-b}),points=ticks.map(function(t){return {tick:t,seconds:median(byTick[t].map(function(m){return m.seconds})),original:byTick[t][0].original.scoreSeconds===undefined?byTick[t][0].original.onSeconds:byTick[t][0].original.scoreSeconds}});
        if(points.length<4)kept.push({reason:"Rubato 至少需要四个可靠的不同起音"});
        else {
            var span=points[points.length-1].seconds-points[0].seconds,originalSpan=points[points.length-1].original-points[0].original;
            if(span<=0)throw new Error("演奏时间映射必须严格单调");
            var factor=config.keepDuration!==false?originalSpan/span:1,previous=points[0].seconds;
            for(var i=1;i<points.length;++i){
                if(points[i].seconds<=previous)throw new Error("可靠对应交叉；请校正锚点");previous=points[i].seconds;
            }
            // Piecewise beat velocities, median-smoothed; boundary points retain score tempo.
            var rates=[];
            for(i=1;i<points.length;++i)rates.push((points[i].tick-points[i-1].tick)/480*60/((points[i].seconds-points[i-1].seconds)*factor));
            for(i=0;i<points.length-1;++i){
                var point=points[i],n=byTick[point.tick][0].original,window=rates.slice(Math.max(0,i-1),Math.min(rates.length,i+2)),rate=median(window);
                var ease=Math.min(1,i/2,(points.length-2-i)/2),target=n.bpm+(rate-n.bpm)*rs*Math.max(0,ease);
                if(n.tempoProtected){kept.push({tick:point.tick,reason:"已有速度记号／曲线保护"});continue}
                if(target<5||target>999){kept.push({tick:point.tick,reason:"速度超出可写范围"});continue}
                tempos.push({tick:point.tick,oldBpm:n.bpm,bpm:target});
            }
            var last=points[points.length-1],ln=byTick[last.tick][0].original;
            if(!ln.tempoProtected)tempos.push({tick:last.tick,oldBpm:ln.bpm,bpm:ln.bpm});
            // Normalize the *written* step-tempo approximation, not just input anchors.
            if(config.keepDuration!==false&&tempos.length>2){
                var duration=0;for(i=1;i<tempos.length;++i)duration+=(tempos[i].tick-tempos[i-1].tick)/480*60/tempos[i-1].bpm;
                var desired=points[points.length-1].original-points[0].original;
                var fixed=0,flex=0;for(i=1;i<tempos.length;++i){var dt=(tempos[i].tick-tempos[i-1].tick)/480*60/tempos[i-1].bpm;if(i===1)fixed+=dt;else flex+=dt}
                if(desired>fixed&&flex>0){var correction=flex/(desired-fixed);for(i=1;i<tempos.length-1;++i)tempos[i].bpm*=correction;}
            }
        }
    }
    velocity.forEach(function(v){if(v.graceIndex===undefined)delete v.graceIndex});
    var velocityById={};velocity.forEach(function(v){velocityById[v.id]=v});
    return {revision:input.revision,scope:input.scope,keepDuration:config.keepDuration!==false,velocity:velocity,velocityById:velocityById,tempos:tempos,kept:kept,protect:true};
}
