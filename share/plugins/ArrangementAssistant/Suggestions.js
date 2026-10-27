.pragma library
.import "Rules.js" as Rules
.import "Playback.js" as Playback
.import "../HarmonyAssistant/Harmony.js" as Harmony

// Value-only, bounded searches. Keyboard geometry and the vertical/horizontal
// split follow the chord-state formulation described in 1904.10237, section 5.4.
// These editable costs are personal presets, not learned model probabilities.
function copy(v) { return JSON.parse(JSON.stringify(v)); }
function pc(p) { return (p % 12 + 12) % 12; }
function keyX(p) {
    return Math.floor(p / 12) * 7 + [0,0.5,1,1.5,2,3,3.5,4,4.5,5,5.5,6][pc(p)];
}
function fingerChoices(n) {
    if(n.analysisFinger)return 1<<(n.analysisFinger-1);
    var mask=31;
    (n.fingerings||[]).forEach(function(v) {
        var text=String(v).replace(/\s/g,"").replace(/[\u2010\u2011\u2013\u2014\u2212]/g,"-");
        if(/^[1-5](?:-[1-5]){1,4}$/.test(text)){mask&=1<<(Number(text[0])-1);return;}
        if(/^(?:\[[1-5]\]|\([1-5]\))$/.test(text))text=text[1];
        if(!/^[1-5](?:\([1-5]\))*$/.test(text))return;
        var digits=text.match(/[1-5]/g),choice=0;
        digits.forEach(function(f){choice|=1<<(Number(f)-1);});mask&=choice;
    });
    return mask;
}
function keyId(n) {return n.analysisId||n.logicalId||n.id;}
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
function combinations(keys, side, previous, lock, written) {
    written=written||{};
    var result=[], chosen=[];
    function next(i) {
        if(i===keys.length){result.push(chosen.slice());return;}
        var k=keys[i], mask=31;
        for(var j=0;j<k.notes.length;++j) {
            var n=k.notes[j], held=Rules.keyStart(n)<k.tick;
            if(lock)mask&=fingerChoices(n);
            if(held && previous[keyId(n)] && !(lock && n.analysisFinger))mask&=1<<(previous[keyId(n)]-1);
            if(n.ref && written[n.ref.id] && (!lock || !(n.fingerings||[]).length))mask&=1<<(written[n.ref.id]-1);
        }
        for(var f=1;f<=5;++f) {
            if(!(mask&(1<<(f-1))) || chosen.indexOf(f)>=0)continue;
            var crossing=false;
            for(var j=0;j<chosen.length;++j)if((side==="right"?f<chosen[j]:f>chosen[j]) && f!==1 && chosen[j]!==1)crossing=true;
            if(crossing)continue;
            chosen.push(f);next(i+1);chosen.pop();
        }
    }
    if(keys.length<=5)next(0);
    return result;
}
function shapeCost(keys, fingers, w, side) {
    var c=0;
    for(var i=0;i<keys.length;++i) {
        if(!Rules.white(keys[i].pitch) && (fingers[i]===1 || fingers[i]===5))c+=w.blackThumb;
        for(var j=0;j<i;++j) {
            var distance=Math.abs(keyX(keys[i].pitch)-keyX(keys[j].pitch));
            var gap=Math.abs(fingers[i]-fingers[j]);
            if(side==="left"?fingers[i]>fingers[j]:fingers[i]<fingers[j])c+=w.crossing*8;
            c+=w.stretch*Math.pow(Math.max(0,distance-gap*1.6),2);
            if(gap===1 && distance>2.5)c+=w.stretch*(distance-2.5)*2;
        }
    }
    return c;
}
function movement(old, keys, fs, side, seconds, w) {
    if(!old.keys.length)return 0;
    var c=0, sign=side==="right"?1:-1;
    for(var i=0;i<keys.length;++i)for(var j=0;j<old.keys.length;++j) {
        var release=old.seconds;
        old.keys[j].notes.forEach(function(n){var end=n.analysisReleaseSeconds===undefined?n.physicalEndSeconds:n.analysisReleaseSeconds;
            if(typeof end==="number")release=Math.max(release,Math.min(seconds,end));});
        var dt=Math.max(0.06,seconds-release);
        var dp=(keyX(keys[i].pitch)-keyX(old.keys[j].pitch))*sign, df=fs[i]-old.fs[j];
        c+=w.move*Math.max(0,Math.abs(dp-df*1.4)-1.5)/(dt+0.15)/Math.max(1,keys.length*old.keys.length);
        if(dp*df<0 && Math.abs(dp)<8)c+=w.crossing*(fs[i]===1||old.fs[j]===1?0.4:2);
        if(keys[i].pitch===old.keys[j].pitch && fs[i]!==old.fs[j])c+=w.repeat;
    }
    return c;
}
function handFrames(state, config, settings) {
    var out={}, assignments={}, writtenAssignments={}, reports=[], messages=[], native=typeof PianoHands!=="undefined",frames=state.frames;
    if(state.data.playback) {
        var reader=Playback.create(state.data.playback,state.handIndex,config.handOverrides);frames=[];
        while(!Playback.step(reader))if(reader.frame)frames.push(Playback.physicalFrame(reader,reader.frame));
    }
    function assigned(n,frame) {
        var hand=Rules.handFor(state,n,frame.playbackTick===undefined?frame.tick:n.assignmentTick===undefined?n.attackTick:n.assignmentTick),v=copy(n);
        if(settings && settings.lockFingers===false)v.fingerings=[];
        v.analysisHand=hand.hand;v.handKnown=hand.source.indexOf("自动推断")<0;
        return v;
    }
    for(var i=0;i<frames.length;++i) {
        var frame=frames[i], tick=frame.tick,at=frame.playbackTick===undefined?tick:frame.playbackTick;
        var parts={};
        frame.notes.forEach(function(n) {
            if(Rules.keyStart(n)>at || Rules.keyEnd(n)<=at || n.grace || n.play===false || n.piano===false)return;
            var v=assigned(n,frame);
            (parts[n.partId===undefined?0:n.partId]||(parts[n.partId===undefined?0:n.partId]=[])).push(v);
        });
        for(var part in parts) {
            var notes=parts[part], seconds=frame.seconds===undefined?Math.max.apply(null,notes.filter(function(n){return Rules.keyStart(n)===at;}).map(function(n){return n.onSeconds;})):frame.seconds;
            if(!isFinite(seconds))seconds=tick/960;
            var lookahead=[];
            var nextFrames=frame.lookahead||frames.slice(i+1,i+33);
            for(var f=0;f<nextFrames.length && lookahead.length<2;++f) {
                var future=nextFrames[f],futureAt=future.playbackTick===undefined?future.tick:future.playbackTick;
                var futureNotes=future.notes.filter(function(n){return Number(n.partId===undefined?0:n.partId)===Number(part) && Rules.keyStart(n)<=futureAt && Rules.keyEnd(n)>futureAt && !n.grace && n.play!==false && n.piano!==false;}).map(function(n){return assigned(n,future);});
                var attacks=futureNotes.filter(function(n){return Rules.keyStart(n)===futureAt;});if(!attacks.length)continue;
                var futureSeconds=future.seconds===undefined?Math.max.apply(null,attacks.map(function(n){return n.onSeconds;})):future.seconds;
                lookahead.push({tick:futureAt,seconds:isFinite(futureSeconds)?futureSeconds:futureAt/960,notes:futureNotes});
            }
            var options={part:Number(part),tick:at,seconds:seconds,left:config.left,right:config.right,minimumHoldMs:config.minimumHoldMs,lookahead:lookahead};
            if(typeof frame.nextChangeSeconds==="number")options.nextChangeSeconds=frame.nextChangeSeconds;
            if(typeof frame.endSeconds==="number")options.endSeconds=frame.endSeconds;
            var report=native?PianoHands.analyse(notes,frame.playbackPedals||state.data.pedals||[],options):null;
            var sideNotes={left:[],right:[]};
            if(report && (report.uncertain || report.limited))messages.push("第"+(notes[0].measure||"当前")+"小节有分手或指法需要核对，继续安排其余明确的音");
            if(report) {
                var effective=report.effectiveFingers||{};for(var index in effective)if(notes[index])notes[index].analysisFinger=effective[index];
                for(var h=0;h<report.sides.length;++h) {
                    var side=report.sides[h];
                    (side.notes||[]).forEach(function(index){
                        var n=notes[index];if(!n)return;assignments[keyId(n)]=side.hand;
                        var written=n.id;writtenAssignments[written]=writtenAssignments[written]&&writtenAssignments[written]!==side.hand?"mixed":side.hand;
                        if((side.released||[]).indexOf(index)<0)sideNotes[side.hand].push(n);
                    });
                }
                reports.push({tick:tick,utick:at,report:report});
            } else notes.forEach(function(n){if(sideNotes[n.analysisHand])sideNotes[n.analysisHand].push(n);});
            for(var side in sideNotes) {
                var ns=sideNotes[side];
                if(!ns.some(function(n){return Rules.keyStart(n)===at;}))continue;
                var byPitch={};ns.forEach(function(n){(byPitch[n.pitch]||(byPitch[n.pitch]=[])).push(n);});
                var keys=Object.keys(byPitch).map(Number).sort(function(a,b){return a-b;}).map(function(p){return {pitch:p,tick:at,notes:byPitch[p]};});
                (out[part+":"+side]||(out[part+":"+side]=[])).push({tick:at,scoreTick:tick,seconds:seconds,side:side,keys:keys});
            }
        }
    }
    return {frames:out,reports:reports,assignments:assignments,writtenAssignments:writtenAssignments,messages:messages.filter(function(v,i,a){return a.indexOf(v)===i;})};
}
function fingering(state, config, settings) {
    var hands=handFrames(state,config,settings), routes=[], messages=hands.messages.slice(), w=weights(settings.weights);
    for(var key in hands.frames) {
        var frames=hands.frames[key], beam=[{cost:0,history:[],assigned:{},written:{},keys:[],fs:[],seconds:0}], skipped=0;
        for(var i=0;i<frames.length;++i) {
            var frame=frames[i], keys=frame.keys, limits=config[frame.side], maximum=keys.length && Rules.white(keys[0].pitch)&&Rules.white(keys[keys.length-1].pitch)?limits.white:limits.maximum;
            if(keys.length>5 || keys.length && keys[keys.length-1].pitch-keys[0].pitch>maximum){++skipped;continue;}
            var next=[];
            for(var b=0;b<beam.length;++b) {
                var previous=beam[b], possible=combinations(keys,frame.side,previous.assigned,settings.lockFingers!==false,previous.written);
                for(var k=0;k<possible.length;++k) {
                    var fs=possible[k], assigned={}, written=copy(previous.written),history=previous.history.slice();
                    keys.forEach(function(g,index){g.notes.forEach(function(n){
                        assigned[keyId(n)]=fs[index];
                        if(Rules.keyStart(n)===frame.tick && !n.contextOnly && !n.tieBack && n.ref && !(settings.lockFingers!==false && (n.fingerings||[]).length)) {
                            if(!written[n.ref.id])history.push({note:n,finger:fs[index]});written[n.ref.id]=fs[index];
                        }
                    });});
                    next.push({cost:previous.cost+shapeCost(keys,fs,w,frame.side)+movement(previous,keys,fs,frame.side,frame.seconds,w),history:history,assigned:assigned,written:written,keys:keys,fs:fs,seconds:frame.seconds});
                }
            }
            next.sort(function(a,b){return a.cost-b.cost;});
            var seen={};next=next.filter(function(r){var id=JSON.stringify(r.written)+"/"+Object.keys(r.assigned).sort().map(function(n){return n+":"+r.assigned[n];}).join();if(seen[id])return false;seen[id]=true;return true;});
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
        var positions={};state.data.notes.forEach(function(n){positions[n.id]=n;});
        var description=changes.slice(0,6).map(function(c){var n=positions[c.ref.id];return n?"第"+n.measure+"小节第"+n.beat+"拍 "+Rules.pitchLabel(n)+"用"+c.finger+"指":"";}).filter(function(v){return v.length;}).join("；");
        if(changes.length>6)description+="；另有"+(changes.length-6)+"个音，可点下方列表查看";
        options.push({title:"指法路线"+String.fromCharCode(65+r),summary:description+"。按"+(state.data.playback?"实际起音和松键":"谱面时值")+"比较前后衔接，保留已有指法和分手。"+(messages.length?messages.join("；")+"。":""),changes:changes});
    }
    return {options:options,messages:messages};
}
function canChange(n, settings) {
    return !n.contextOnly && !n.grace && !n.tieBack && !n.tieForward && (settings.preserveTracks||[0]).indexOf(n.track)<0;
}
function trial(state, changes, config) {
    var data=copy(state.data), byId={},original={};changes.forEach(function(c){byId[c.ref.id]=c;});
    state.data.notes.forEach(function(n){original[n.id]=n;});
    function update(n){var c=byId[n.id];if(c){n.pitch=c.pitch;n.writtenPitch=c.pitch;n.tpc=n.tpc1=c.tpc1;n.tpc2=c.tpc2;}}
    data.notes.forEach(update);
    if(data.playback) {
        (data.playback.notes||[]).forEach(update);
        var playing={};
        (data.playback.events||[]).forEach(function(event){
            var change=byId[event.id],source=original[event.id];
            if(change && source) {
                event.pitch+=change.pitch-source.pitch;event.writtenPitch=change.pitch;
                if(pc(event.pitch)===pc(change.pitch))event.tpc=change.tpc1;else delete event.tpc;
            }
            // Re-evaluate overlapping same-pitch voices after a pitch change,
            // following MidiEventFixup. The live score/events remain untouched.
            if(event.muted || event.kind!=="on"&&event.kind!=="off")return;
            var key=event.partId+":"+event.channel+":"+event.pitch,count=playing[key]||0;
            if(event.kind==="on"){++count;event.discard=count>1?1:0;}
            else {event.discard=count===0||count>1?1:0;count=Math.max(0,count-1);}
            playing[key]=count;
        });
    }
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
        var given=Rules.handFor(state,n,n.attackTick), side=plans.writtenAssignments[n.id]||plans.assignments[n.logicalId];
        if(!side || side==="mixed" || side===given.hand || given.source.indexOf("自动推断")<0)return;
        var key=n.tick+":"+n.track+":"+side;(grouped[key]||(grouped[key]=[])).push(n);
    });
    for(var key in grouped) {
        var notes=grouped[key], side=plans.writtenAssignments[notes[0].id]||plans.assignments[notes[0].logicalId];
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
        var chord=Rules.chordAt(state,n.tick,Rules.partFor(state,n.track)), allowed=[];
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
