// SPDX-License-Identifier: GPL-2.0-or-later
// Values only. Recorded pitches identify taps; score pitches remain the material.
function clone(v) {return JSON.parse(JSON.stringify(v));}
function raster(tick, quantum) {return Math.round(tick/quantum)*quantum;}
function quarters(ticks) {
    function gcd(a,b){while(b){var t=a%b;a=b;b=t;}return Math.abs(a)||1;}
    var d=gcd(ticks,480);return {numerator:ticks/d,denominator:480/d};
}
function sources(data,track) {
    var notes=(data.notes||[]).filter(function(n){return n.track===track&&!n.contextOnly&&!n.tieBack&&!n.grace;});
    return Alignment.groups(notes,true,true).map(function(g,index){
        return {index:index,time:g.time,notes:g.notes,pitches:g.pitches};
    });
}
function scoreLength(group) {
    var durations=group.notes.map(function(n){
        return n.logicalEnd>n.tick?n.logicalEnd-n.tick:n.duration||n.end-n.tick||480;
    });
    return Math.max(1,Math.min.apply(null,durations));
}
function originalLength(group, quantum) {
    return Math.max(quantum,scoreLength(group));
}
function tapGroups(performance) {
    var notes=performance.slice().sort(function(a,b){return a.seconds-b.seconds||a.pitch-b.pitch;}), out=[];
    notes.forEach(function(n){var last=out[out.length-1];
        if(!last || n.seconds-last.time>.035 || n.seconds-last.time>.004 && last.pitches.indexOf(n.pitch)>=0){last={time:n.seconds,id:n.id,notes:[],pitches:[]};out.push(last);}
        last.notes.push(n);if(last.pitches.indexOf(n.pitch)<0)last.pitches.push(n.pitch);
    });return out;
}
function initialRows(data,performance,settings) {
    var seq=sources(data,settings.track), taps=tapGroups(performance), quantum=settings.quantum||120;
    if(!seq.length)return {rows:[],error:"所选声部还没有音高，请先输入音高，再录入节奏。"};
    if(!taps.length)return {rows:seq.map(function(g,i){return {tapId:"score-"+i,sourceIndex:i,start:g.time-data.scope.start,duration:scoreLength(g),velocity:g.notes[0].velocity||64,fromScore:true,estimated:false};})};
    var pairs=[];
    if(seq.length===taps.length)for(var i=0;i<seq.length;++i)pairs.push({s:i,p:i});
    else if(taps.length){try{pairs=Alignment.path(seq,taps,true);}catch(error){for(var i=0;i<Math.min(seq.length,taps.length);++i)pairs.push({s:i,p:i});}}
    var assigned={};pairs.forEach(function(p){assigned[p.p]=p.s;});
    var rows=taps.map(function(t,index){
        var start=Alignment.median(t.notes.map(function(n){return n.relativeTick;}));
        var end=Alignment.median(t.notes.map(function(n){return n.relativeOffTick;}));
        return {tapId:t.id,sourceIndex:assigned[index]===undefined?-1:assigned[index],start:raster(start,quantum),
            duration:settings.useReleases?Math.max(quantum,raster(end-start,quantum)):0,
            velocity:Math.max(1,Math.min(127,Math.round(Alignment.median(t.notes.map(function(n){return n.velocity;}))))),
            recordedSeconds:t.time,estimated:false};
    });
    return {rows:rows};
}
function fillMissing(rows,seq,scope,quantum) {
    var occupied={},manual={};rows.forEach(function(r){if(r.sourceIndex>=0)occupied[r.sourceIndex]=r;if(r.estimatedFor!==undefined && r.sourceIndex!==r.estimatedFor)manual[r.estimatedFor]=true;});
    for(var i=0;i<seq.length;++i)if(!occupied[i] && !manual[i]) {
        var before=null,after=null;
        for(var a=i-1;a>=0;--a)if(occupied[a]){before=occupied[a];break;}
        for(a=i+1;a<seq.length;++a)if(occupied[a]){after=occupied[a];break;}
        var at=seq[i].time-scope.start;
        if(before&&after)at=before.start+(after.start-before.start)*(i-before.sourceIndex)/(after.sourceIndex-before.sourceIndex);
        else if(before)at=before.start+(seq[i].time-seq[before.sourceIndex].time);
        else if(after)at=after.start-(seq[after.sourceIndex].time-seq[i].time);
        var value={tapId:"estimated-"+i,estimatedFor:i,sourceIndex:i,start:Math.max(0,raster(at,quantum)),duration:0,
            velocity:seq[i].notes[0].velocity||64,estimated:true};
        rows.push(value);occupied[i]=value;
    }
    return rows;
}
function voiceEvents(notes,seq,scope,bounds,errors) {
    var intervals=[],begins={},ends={},cuts={};
    bounds.forEach(function(t){cuts[t]=true;});
    notes.forEach(function(row){
        var group=seq[row.sourceIndex],original=row.fromScore&&!row.durationEdited&&row.duration===scoreLength(group);
        row.noteDurations={};
        group.notes.forEach(function(note){
            var length=original?(note.logicalEnd>note.tick?note.logicalEnd-note.tick:note.duration||note.end-note.tick):row.effectiveDuration;
            length=Math.max(1,Math.min(length,scope.end-scope.start-row.start));row.noteDurations[note.id]=length;
            var item={start:row.start,end:row.start+length,sourceIndex:row.sourceIndex,pitch:note.writtenPitch===undefined?note.pitch:note.writtenPitch,
                tpc1:note.tpc1,tpc2:note.tpc2,velocity:row.velocity,sourceId:note.id,
                writeVelocity:row.velocityEdited||(!row.fromScore&&row.estimatedFor===undefined)};
            var index=intervals.length;intervals.push(item);cuts[item.start]=cuts[item.end]=true;
            (begins[item.start]||(begins[item.start]=[])).push(index);(ends[item.end]||(ends[item.end]=[])).push(index);
        });
    });
    var times=Object.keys(cuts).map(Number).sort(function(a,b){return a-b;}),active={},events=[];
    for(var i=0;i+1<times.length;++i){
        var at=times[i],until=times[i+1];(ends[at]||[]).forEach(function(n){delete active[n];});
        (begins[at]||[]).forEach(function(n){active[n]=true;});
        var current=Object.keys(active).map(function(n){return intervals[+n];}).sort(function(a,b){return a.pitch-b.pitch;});
        var pitches=[];
        for(var n=0;n<current.length;++n){
            var item=current[n];
            if(n&&current[n-1].pitch===item.pitch){errors.push("第"+(current[n-1].sourceIndex+1)+"音和第"+(item.sourceIndex+1)+"音的同音时长重叠，请缩短前音或调整起音。");return [];}
            var pitch={pitch:item.pitch,tpc1:item.tpc1,tpc2:item.tpc2,sourceId:item.sourceId,tieNext:item.end>until};
            if(item.writeVelocity)pitch.velocity=item.velocity;pitches.push(pitch);
        }
        events.push({quarterTime:quarters(scope.start+at),quarterDuration:quarters(until-at),pitches:pitches,
            tieNext:pitches.length>0&&pitches.every(function(p){return p.tieNext;})});
    }
    return events;
}
function build(input) {
    var data=input.data,settings=input.settings||{},scope=clone(data.scope),quantum=settings.quantum||120;
    var seq=sources(data,settings.track), initial=input.rows?{rows:clone(input.rows)}:initialRows(data,input.performance||[],settings);
    if(initial.error)return {error:initial.error,rows:[]};
    if(!seq.length)return {error:"先选中一条有音高的声部。",rows:[]};
    var rows=initial.rows,total=scope.end-scope.start,errors=[],warnings=[];
    if(settings.keepAll!==false)rows=fillMissing(rows,seq,scope,quantum);
    rows.sort(function(a,b){return a.start-b.start||a.sourceIndex-b.sourceIndex;});
    var used={},notes=[],previous=-1,estimated=0;
    rows.forEach(function(row) {
        if(!row.fromScore)row.start=raster(row.start,quantum);
        row.velocity=Math.round(Math.max(1,Math.min(127,row.velocity||64)));
        row.duration=row.duration>0?(row.fromScore?row.duration:Math.max(quantum,raster(row.duration,quantum))):0;
        if(row.sourceIndex<0){row.status=row.estimatedFor===undefined?"多敲，可指定音序":"第"+(row.estimatedFor+1)+"音等待对应";return;}
        if(row.sourceIndex>=seq.length){errors.push("音序超出范围，请填写1至"+seq.length+"。 ");return;}
        if(used[row.sourceIndex])errors.push("第"+(row.sourceIndex+1)+"音对应了两次，请把多敲的一行设为0。");
        used[row.sourceIndex]=true;
        if(row.sourceIndex<previous)errors.push("音序先后发生变化，请拖动起音，或修改音序编号。");previous=row.sourceIndex;
        if(row.start<0||row.start>=total)errors.push("第"+(row.sourceIndex+1)+"音超出选段，请调整拍位或扩大选段。");
        row.status=row.estimated?"漏敲，按前后估计":row.manual?"手动调整":row.fromScore?"谱中音序":"已对应";estimated+=row.estimated?1:0;notes.push(row);
    });
    if(settings.keepAll!==false)for(var missing=0;missing<seq.length;++missing)if(!used[missing])errors.push("第"+(missing+1)+"音等待对应，请给它指定一次敲击或恢复估计行的音序。");
    for(var i=0;i<notes.length;++i) {
        var row=notes[i],next=i+1<notes.length?notes[i+1].start:total;
        if(next<=row.start){errors.push("两个音的起音重合，请拖开或选择更细的细分。");continue;}
        var length=row.duration|| (i+1<notes.length?next-row.start:originalLength(seq[row.sourceIndex],quantum));
        row.effectiveDuration=Math.min(length,total-row.start);
    }
    if(estimated)warnings.push(estimated+"组漏敲音已按前后位置估计，可在草稿中拖动。 ");
    var tuplets=[],bounds=[0,total],meters=data.meters||[];
    meters.forEach(function(m){if(m.tick>scope.start&&m.tick<scope.end)bounds.push(m.tick-scope.start);if(m.end>scope.start&&m.end<scope.end)bounds.push(m.end-scope.start);});
    notes.forEach(function(row){
        if(!row.fromScore)return;
        seq[row.sourceIndex].notes.forEach(function(note){
            var t=note.tuplet;if(!t)return;
            var start=t.start-scope.start,end=start+t.duration,ratio=String(t.ratio).split('/');
            if(start<0||end>total){errors.push("选段包含一部分连音组，请把这一组一起选中。");return;}
            if(ratio.length!==2||!(+ratio[0]>0)||!(+ratio[1]>0))return;
            if(!tuplets.some(function(existing){return existing.start===start&&existing.end===end;}))
                tuplets.push({start:start,end:end,actual:+ratio[0],normal:+ratio[1]});
        });
    });
    if(quantum===160) {
        notes.forEach(function(row){[row.start,row.start+row.effectiveDuration].forEach(function(at){
            if(at<=0||at>=total)return;
            var barStart=0,barEnd=total;
            meters.forEach(function(m){if(m.tick<=scope.start+at&&scope.start+at<m.end){barStart=m.tick-scope.start;barEnd=m.end-scope.start;}});
            var start=barStart+Math.floor((at-barStart)/480)*480,end=Math.min(start+480,barEnd,total);
            if(at===start||at===end)return;
            if(!tuplets.some(function(t){return t.start<=at&&at<=t.end;}))tuplets.push({start:start,end:end,actual:3,normal:2});
        });});
    }
    tuplets.forEach(function(t){bounds.push(t.start,t.end);});
    bounds.sort(function(a,b){return a-b;});bounds=bounds.filter(function(t,i){return !i||t!==bounds[i-1];});
    var events=errors.length?[]:voiceEvents(notes,seq,scope,bounds,errors);
    scope.firstTrack=settings.track;scope.endTrack=settings.track+1;
    return {rows:rows,errors:errors,warnings:warnings,sourceGroups:seq,estimated:estimated,
        scope:scope,voices:[{track:settings.track,events:events,tuplets:tuplets.map(function(t){return {quarterTime:quarters(scope.start+t.start),quarterDuration:quarters(t.end-t.start),actual:t.actual,normal:t.normal};})}],
        summary:"按给定的"+seq.length+"组音高写入节奏与演奏力度。"+(estimated?estimated+"组起音采用前后估计，草稿中可继续调整。":"")};
}
