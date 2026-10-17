.pragma library
.import "Rules.js" as Rules
.import "../HarmonyAssistant/Harmony.js" as Harmony

// Value-only, bounded searches. Keyboard geometry and the vertical/horizontal
// split follow the chord-state formulation described in 1904.10237, section 5.4.
// These editable costs are personal presets, not learned model probabilities.
function copy(v) { return JSON.parse(JSON.stringify(v)); }
function pc(p) { return (p % 12 + 12) % 12; }
function keyX(p) {
    return Math.floor(p / 12) * 7 + [0,0.5,1,1.5,2,3,3.5,4,4.5,5,5.5,6][pc(p)];
}
function finger(n) {
    var found=0;
    (n.fingerings||[]).forEach(function(v) {
        var text=String(v).trim(), f=Number(text);
        if(text.length===1 && f>=1 && f<=5)found=found && found!==f ? -1 : f;
    });
    return found;
}
function noteChange(n, values) {
    var out={ref:copy(n.ref)};
    for(var k in values)out[k]=values[k];
    return out;
}
function weights(settings) {
    var w={stretch:1,move:1,blackThumb:0.6,crossing:1.2,repeat:0.35};
    for(var k in w)if(settings && typeof settings[k]==="number")w[k]=Math.max(0,Math.min(10,settings[k]));
    return w;
}
function combinations(keys, side, previous, lock) {
    var result=[], chosen=[];
    function next(i,last) {
        if(i===keys.length){result.push(chosen.slice());return;}
        var k=keys[i], fixed=0;
        for(var j=0;j<k.notes.length;++j) {
            var n=k.notes[j], f=lock?finger(n):0;
            if(f<0){return;}
            if(n.attackTick<k.tick && previous[n.logicalId])f=previous[n.logicalId];
            if(f && fixed && f!==fixed)return;
            if(f)fixed=f;
        }
        for(var f=1;f<=5;++f) {
            if(fixed && f!==fixed || i && (side==="right"?f<=last:f>=last))continue;
            chosen.push(f);next(i+1,f);chosen.pop();
        }
    }
    if(keys.length<=5)next(0,side==="right"?0:6);
    return result;
}
function shapeCost(keys, fingers, w) {
    var c=0;
    for(var i=0;i<keys.length;++i) {
        if(!Rules.white(keys[i].pitch) && (fingers[i]===1 || fingers[i]===5))c+=w.blackThumb;
        for(var j=0;j<i;++j) {
            var distance=Math.abs(keyX(keys[i].pitch)-keyX(keys[j].pitch));
            var gap=Math.abs(fingers[i]-fingers[j]);
            c+=w.stretch*Math.pow(Math.max(0,distance-gap*1.6),2);
            if(gap===1 && distance>2.5)c+=w.stretch*(distance-2.5)*2;
        }
    }
    return c;
}
function movement(old, keys, fs, side, seconds, w) {
    if(!old.keys.length)return 0;
    var c=0, sign=side==="right"?1:-1, dt=Math.max(0.06,seconds-old.seconds);
    for(var i=0;i<keys.length;++i)for(var j=0;j<old.keys.length;++j) {
        var dp=(keyX(keys[i].pitch)-keyX(old.keys[j].pitch))*sign, df=fs[i]-old.fs[j];
        c+=w.move*Math.max(0,Math.abs(dp-df*1.4)-1.5)/(dt+0.15)/Math.max(1,keys.length*old.keys.length);
        if(dp*df<0 && Math.abs(dp)<8)c+=w.crossing*(fs[i]===1||old.fs[j]===1?0.4:2);
        if(keys[i].pitch===old.keys[j].pitch && fs[i]!==old.fs[j])c+=w.repeat;
    }
    return c;
}
function handFrames(state, config) {
    var out={}, released={}, active=[], assignments={}, reports=[], native=typeof PianoHands!=="undefined";
    for(var i=0;i<state.frames.length;++i) {
        var frame=state.frames[i], tick=frame.tick;
        active=active.filter(function(n){return n.logicalEnd>tick && !released[n.logicalId];});
        (state.byTick[tick]||[]).forEach(function(n){if(!n.grace && !released[n.logicalId])active.push(n);});
        var parts={};
        active.forEach(function(n) {
            var hand=Rules.handFor(state,n,tick), v=copy(n);
            v.analysisHand=assignments[n.logicalId]||hand.hand;
            v.handKnown=hand.source.indexOf("自动推断")<0 || !!assignments[n.logicalId];
            (parts[n.partId===undefined?0:n.partId]||(parts[n.partId===undefined?0:n.partId]=[])).push(v);
        });
        for(var part in parts) {
            var notes=parts[part], seconds=notes.length?Math.max.apply(null,notes.filter(function(n){return n.attackTick===tick;}).map(function(n){return n.onSeconds;})):0;
            if(!isFinite(seconds))seconds=tick/960;
            var lookahead=[];
            for(var f=i+1;f<state.frames.length && lookahead.length<2;++f) {
                var future=state.frames[f];
                if(!(state.byTick[future.tick]||[]).some(function(n){return n.partId===Number(part);}))continue;
                var futureNotes=future.notes.filter(function(n){return n.partId===Number(part) && n.logicalEnd>future.tick;}).map(function(n){var v=copy(n),h=Rules.handFor(state,n,future.tick);v.analysisHand=h.hand;v.handKnown=h.source.indexOf("自动推断")<0;return v;});
                var futureSeconds=futureNotes.filter(function(n){return n.attackTick===future.tick;}).map(function(n){return n.onSeconds;});
                lookahead.push({tick:future.tick,seconds:futureSeconds.length?Math.max.apply(null,futureSeconds):future.tick/960,notes:futureNotes});
            }
            var report=native?PianoHands.analyse(notes,state.data.pedals||[],{part:Number(part),tick:tick,seconds:seconds,left:config.left,right:config.right,minimumHoldMs:config.minimumHoldMs,lookahead:lookahead}):null;
            var sideNotes={left:[],right:[]};
            if(report) {
                if(report.uncertain || report.limited)continue;
                for(var h=0;h<report.sides.length;++h) {
                    var side=report.sides[h];
                    (side.released||[]).forEach(function(index){released[notes[index].logicalId]=true;});
                    (side.notes||[]).forEach(function(index){
                        var n=notes[index];assignments[n.logicalId]=side.hand;
                        if(!released[n.logicalId])sideNotes[side.hand].push(n);
                    });
                }
                reports.push({tick:tick,report:report});
            } else notes.forEach(function(n){if(sideNotes[n.analysisHand])sideNotes[n.analysisHand].push(n);});
            for(var side in sideNotes) {
                var ns=sideNotes[side];
                if(!ns.some(function(n){return n.attackTick===tick;}))continue;
                var byPitch={};ns.forEach(function(n){(byPitch[n.pitch]||(byPitch[n.pitch]=[])).push(n);});
                var keys=Object.keys(byPitch).map(Number).sort(function(a,b){return a-b;}).map(function(p){return {pitch:p,tick:tick,notes:byPitch[p]};});
                (out[part+":"+side]||(out[part+":"+side]=[])).push({tick:tick,seconds:seconds,side:side,keys:keys});
            }
        }
    }
    return {frames:out,reports:reports,assignments:assignments};
}
function fingering(state, config, settings) {
    var hands=handFrames(state,config), routes=[], messages=[], w=weights(settings.weights);
    for(var key in hands.frames) {
        var frames=hands.frames[key], beam=[{cost:0,history:[],assigned:{},keys:[],fs:[],seconds:0}], skipped=0;
        for(var i=0;i<frames.length;++i) {
            var frame=frames[i], keys=frame.keys, limits=config[frame.side], maximum=keys.length && Rules.white(keys[0].pitch)&&Rules.white(keys[keys.length-1].pitch)?limits.white:limits.maximum;
            if(keys.length>5 || keys.length && keys[keys.length-1].pitch-keys[0].pitch>maximum){++skipped;continue;}
            var next=[];
            for(var b=0;b<beam.length;++b) {
                var previous=beam[b], possible=combinations(keys,frame.side,previous.assigned,settings.lockFingers!==false);
                for(var k=0;k<possible.length;++k) {
                    var fs=possible[k], assigned={}, history=previous.history.slice();
                    keys.forEach(function(g,index){g.notes.forEach(function(n){
                        assigned[n.logicalId]=fs[index];
                        if(n.attackTick===frame.tick && !n.contextOnly && !n.tieBack && !(settings.lockFingers!==false && (n.fingerings||[]).length))history.push({note:n,finger:fs[index]});
                    });});
                    next.push({cost:previous.cost+shapeCost(keys,fs,w)+movement(previous,keys,fs,frame.side,frame.seconds,w),history:history,assigned:assigned,keys:keys,fs:fs,seconds:frame.seconds});
                }
            }
            next.sort(function(a,b){return a.cost-b.cost;});
            var seen={};next=next.filter(function(r){var id=Object.keys(r.assigned).sort().map(function(n){return n+":"+r.assigned[n];}).join();if(seen[id])return false;seen[id]=true;return true;});
            if(next.length)beam=next.slice(0,32);else ++skipped;
        }
        if(skipped)messages.push((frames.length?frames[0].side==="left"?"左手":"右手":"")+skipped+"组音需要先确认分手或保持指法");
        routes.push(beam.slice(0,3));
    }
    var options=[], signatures={};
    for(var r=0;r<3;++r) {
        var changes=[];
        routes.forEach(function(route,index){var item=route[index===0?r:0]||route[0];if(!item)return;item.history.forEach(function(v){changes.push(noteChange(v.note,{finger:v.finger}));});});
        var signature=changes.map(function(c){return c.ref.id+":"+c.finger;}).join();
        if(!changes.length || signatures[signature])continue;signatures[signature]=true;
        options.push({title:"指法路线"+String.fromCharCode(65+r),summary:"为"+changes.length+"个音安排前后衔接的指法；已有指法和分手参与计算。"+(messages.length?messages.join("；")+"。":""),changes:changes});
    }
    return {options:options,messages:messages};
}
function canChange(n, settings) {
    return !n.contextOnly && !n.grace && !n.tieBack && !n.tieForward && (settings.preserveTracks||[0]).indexOf(n.track)<0;
}
function trial(state, changes, config) {
    var data=copy(state.data), byId={};changes.forEach(function(c){byId[c.ref.id]=c;});
    data.notes.forEach(function(n){var c=byId[n.id];if(c){n.pitch=c.pitch;n.writtenPitch=c.pitch;n.tpc=n.tpc1=c.tpc1;n.tpc2=c.tpc2;}});
    return Rules.run(data,config,[]);
}
function problemWeight(state) {
    var total=0;
    state.issues.forEach(function(i){if(i.type==="playability")total+=/^(span|keys|hand)-/.test(i.code)?20:/comfort/.test(i.code)?2:1;});
    return total;
}
function voicing(state,config,settings) {
    var options=[], tested=0, baseline=problemWeight(state);
    var candidates=state.data.notes.filter(function(n){return canChange(n,settings);});
    for(var i=0;i<candidates.length && tested<128;++i)for(var d=-12;d<=12;d+=24) {
        var n=candidates[i], pitch=n.pitch+d;if(pitch<21 || pitch>108 || state.data.notes.some(function(t){return t.tick===n.tick && t.track===n.track && t.pitch===pitch;}))continue;
        var changes=[noteChange(n,{pitch:pitch,tpc1:n.tpc1,tpc2:n.tpc2})], proposed=trial(state,changes,config);++tested;
        var benefit=baseline-problemWeight(proposed);
        if(benefit<=0)continue;
        options.push({title:"调整音区 · "+Rules.pitchLabel(n),summary:"第"+n.measure+"小节的"+Rules.pitchLabel(n)+(d<0?"降低":"升高")+"一个八度，减轻同时按键或前后移动的负担；保留旋律与和弦构成音。",changes:changes,benefit:benefit});
    }
    options.sort(function(a,b){return b.benefit-a.benefit;});
    return {options:options.slice(0,3),messages:options.length?[]:["这段暂未找到通过八度调整就能改善的写法。可检查分手或选择有困难的短段。"]};
}
function handoff(state,config,settings) {
    var plans=handFrames(state,config), grouped={}, options=[];
    state.data.notes.forEach(function(n){
        if(n.contextOnly || n.tieBack || n.grace)return;
        var given=Rules.handFor(state,n,n.attackTick), side=plans.assignments[n.logicalId];
        if(!side || side===given.hand || given.source.indexOf("自动推断")<0)return;
        var key=n.tick+":"+n.track+":"+side;(grouped[key]||(grouped[key]=[])).push(n);
    });
    for(var key in grouped) {
        var notes=grouped[key], side=plans.assignments[notes[0].logicalId];
        var changes=notes.map(function(n){return noteChange(n,{hand:side,handEnd:false});});
        options.push({title:"第"+notes[0].measure+"小节借用"+(side==="left"?"左手":"右手"),summary:"将"+notes.map(Rules.pitchLabel).join("、")+"交给"+(side==="left"?"左手":"右手")+"，配合另一只手的保持音和下一组音。采用后在这些音旁写入分手记号。",changes:changes});
        if(options.length>=3)break;
    }
    return {options:options,messages:options.length?[]:["当前分手下暂未找到更合适的换手位置；已有分手记号继续保持。"]};
}
function counterpoint(state, config, settings) {
    var targets=state.issues.filter(function(i){return i.type==="counterpoint";}), options=[], tested=0, original=targets.length;
    if(!targets.length)return {options:[],messages:["当前风格与声部设置下，没有需要调整的对位位置。可在编配检查中指定独立声部。"]};
    var targetTicks={};targets.forEach(function(i){targetTicks[i.tick]=true;});
    var ns=state.data.notes;
    for(var i=0;i<ns.length && tested<96;++i) {
        var n=ns[i];if(!targetTicks[n.tick] || !canChange(n,settings))continue;
        var chord=Rules.chordAt(state,n.tick), allowed=[];
        if(chord && chord.root>=0 && Harmony.defs[chord.definition])allowed=Harmony.defs[chord.definition].intervals.map(function(p){return pc(chord.root+p);});
        var sounding=ns.filter(function(a){return a.partId===n.partId && a.tick===n.tick && a.logicalEnd>n.tick;});
        for(var j=0;j<sounding.length;++j) {
            var source=sounding[j];if(allowed.length && allowed.indexOf(pc(source.pitch))<0)continue;
            var pitch=n.pitch+((source.pitch-n.pitch+1206)%12-6);
            if(pitch===n.pitch || Math.abs(pitch-n.pitch)>4)continue;
            var changes=[noteChange(n,{pitch:pitch,tpc1:source.tpc1,tpc2:source.tpc2})];
            var proposed=trial(state,changes,config);++tested;
            var count=proposed.issues.filter(function(v){return v.type==="counterpoint";}).length;
            var harmony=proposed.issues.filter(function(v){return v.type==="harmony";}).length-state.issues.filter(function(v){return v.type==="harmony";}).length;
            if(count>=original || harmony>0 || problemWeight(proposed)>problemWeight(state))continue;
            options.push({title:"第"+n.measure+"小节调整内声",summary:"将"+Rules.pitchLabel(n)+"改成同一和弦中的"+Harmony.spelledName(source.tpc1,pitch,false)+"，使前后声部进行更顺；保留指定旋律。请连同前后音试听。",changes:changes,benefit:original-count});
        }
    }
    options.sort(function(a,b){return b.benefit-a.benefit;});
    var seen={};options=options.filter(function(v){var s=JSON.stringify(v.changes);if(seen[s])return false;seen[s]=true;return true;});
    return {options:options.slice(0,3),messages:options.length?[]:["找到对位疑点，当前和弦音与旋律保留条件下，尚未找到更合适的局部改音。可先在编配检查查看前后音。"]};
}
function generate(input) {
    var config=Rules.defaults(), saved=input.config||{}, settings=input.settings||{};
    for(var k in saved)config[k]=saved[k];
    var state=Rules.run(input.data,config,[]), result;
    if(input.kind==="voicing")result=voicing(state,config,settings);
    else if(input.kind==="counterpoint")result=counterpoint(state,config,settings);
    else if(input.kind==="handoff")result=handoff(state,config,settings);
    else result=fingering(state,config,settings);
    result.options.forEach(function(v){v.scope=copy(input.data.scope);});
    result.snapshotId=input.data.snapshotId;return result;
}
