.pragma library
.import "Harmony.js" as Harmony

// Numeric, half-open harmonic regions. No native object is retained here.
function clone(value) { return JSON.parse(JSON.stringify(value)); }
function atTick(regions, tick, part) {
    var lo=0,hi=regions.length;
    while(lo<hi){var mid=(lo+hi)>>1;if(regions[mid].start<=tick)lo=mid+1;else hi=mid;}
    for(var i=lo-1;i>=0;--i) {
        if(regions[i].part===part && tick<regions[i].end)return regions[i];
        if(regions[i].part===part)break;
    }
    return null;
}
function bass(notes, tick) {
    var sounding=notes.filter(function(n){return n.end===undefined || n.end>tick;});
    // Expired pedal residue cannot outrank an actually held bass.
    var pool=sounding.length?sounding:notes;
    return pool.length?pool.reduce(function(a,b){return a.pitch<=b.pitch?a:b;}):null;
}
function describe(result, notes, tick, tonic, minor, degreeMode, preferFlats) {
    var out=clone(result), b=bass(notes,tick), flats=typeof preferFlats==="boolean"?preferFlats:[1,3,5,8,10].indexOf(tonic)>=0;
    out.bass=b?Harmony.mod12(b.pitch):-1;out.bassTpc=b?b.tpc:-1;
    out.chord="";out.degree="";out.tonic=tonic;out.minor=minor;out.inversion="未指定和弦";
    if(out.root>=0) {
        var rootNote=null;
        for(var i=0;i<notes.length;++i)if(Harmony.mod12(notes[i].pitch)===out.root){rootNote=notes[i];break;}
        out.chord=(out.rootSpelling|| (rootNote?Harmony.spelledName(rootNote.tpc,rootNote.pitch,flats):Harmony.pcName(out.root,flats)))+Harmony.defs[out.definition].suffix;
        if(b && out.bass!==out.root)out.chord+="/"+Harmony.spelledName(b.tpc,b.pitch,flats);
        out.degree=Harmony.roman(tonic,minor,out.root,out.definition,degreeMode,out.bass);
        out.inversion=b?Harmony.inversion(Harmony.labelFor(out.bass,out.root,out.definition)):"低音未确定";
    }
    out.chromatic=Harmony.isChromatic(tonic,minor,out.root,out.definition);
    return out;
}
function signature(r) {return [r.root,r.definition,r.bass,r.tonic,r.minor,r.chord,r.degree].join(":");}
function partsFor(frame, fallback) {return frame.parts && frame.parts.length?frame.parts:[{startTrack:fallback,endTrack:1024}];}
function tonicAt(frame, config) {
    if(config.manualKey)return Harmony.rootPcs[config.keyTonic];
    var major=[11,6,1,8,3,10,5,0,7,2,9,4,11,6,1];
    return Harmony.mod12(major[(frame.keySignature||0)+7]+(config.keyMode===1?9:0));
}
function pedalAt(frame, part) {
    var windows=frame.pedalWindows||[];
    for(var i=0;i<windows.length;++i)if(windows[i].firstTrack===part && windows[i].start<=frame.tick && frame.tick<windows[i].end)return windows[i];
    return null;
}
function createBuilder(frames, config, fallbackPart) {
    return {frames:frames,config:clone(config),fallbackPart:fallbackPart,byPart:{},keys:[],
        evidence:{},pedalResults:{},ornaments:{},regions:[],phase:0,frameIndex:0,partIndex:0,rowIndex:0,partPass:"score",traceIndex:-1,traceChoice:0,previous:null,current:null};
}
function ornament(state,row,key,note) {
    var onset=note.attackTick===undefined?note.tick:note.attackTick;
    if(note.end===undefined || note.end-onset>480)return false;
    var id=key+":"+note.track+":"+onset+":"+note.pitch;
    if(state.ornaments[id]!==undefined)return state.ornaments[id];
    var rows=state.byPart[key],lo=0,hi=rows.length;
    while(lo<hi){var mid=(lo+hi)>>1;if(rows[mid].start<=onset)lo=mid+1;else hi=mid;}
    var origin=Math.max(0,lo-1);
    function adjacent(direction) {
        for(var i=origin+direction,count=0;i>=0 && i<rows.length && count<4;i+=direction,++count) {
            if(Math.abs(rows[i].start-onset)>960)break;
            var attacks=rows[i].notes.filter(function(n){return n.track===note.track && (n.attackTick===undefined?n.tick:n.attackTick)===rows[i].start;});
            if(attacks.length)return attacks.length===1?attacks[0]:null;
        }
        return null;
    }
    var before=adjacent(-1),after=adjacent(1),yes=false;
    if(before && after) {
        var into=note.pitch-before.pitch,out=after.pitch-note.pitch;
        yes=into!==0 && out!==0 && Math.abs(into)<=2 && Math.abs(out)<=2 && (into*out>0 || before.pitch===after.pitch);
    }
    state.ornaments[id]=yes;return yes;
}
function ingest(state, frame, end) {
    var parts=partsFor(frame,state.fallbackPart);
    if(end<=frame.tick)return;
    for(var p=0;p<parts.length;++p) {
        var part=parts[p].startTrack,notes=frame.notes.filter(function(n){return n.track>=part && n.track<parts[p].endTrack;});
        var rows=state.byPart[part]||(state.byPart[part]=[]),window=state.config.pedal?pedalAt(frame,part):null;
        rows.push({frame:frame,notes:notes,start:frame.tick,end:end,pedal:window});
        if(window && state.config.pedalMode===0) {
            var key=part+":"+window.start+":"+window.end;
            var evidence=state.evidence[key]||(state.evidence[key]={weights:{},all:[],seen:{},representative:[]});
            var width=Math.max(1,Math.min(end,window.end)-Math.max(frame.tick,window.start));
            for(var n=0;n<notes.length;++n) {
                var note=notes[n],pc=Harmony.mod12(note.pitch),held=note.end===undefined||note.end>frame.tick;
                evidence.weights[pc]=(evidence.weights[pc]||0)+width*(held?1:.18);
                var noteKey=note.track+":"+(note.attackTick===undefined?note.tick:note.attackTick)+":"+note.pitch;
                if(!evidence.seen[noteKey]){evidence.seen[noteKey]=true;evidence.all.push(note);}
            }
            if(!evidence.representative.length && notes.length>=3)evidence.representative=notes;
        }
    }
}
function evaluate(state, row, key) {
    var config=state.config,tonic=tonicAt(row.frame,config),window=row.pedal,result;
    var keyName=Harmony.rootNames[config.keyTonic]||Harmony.pcName(tonic,false),flats=config.manualKey?
        keyName.indexOf("♭")>=0 || keyName.indexOf("♯")<0 && (config.keyMode===1?[0,2,5,7].indexOf(tonic)>=0:tonic===5):(row.frame.keySignature||0)<0;
    var pedalKey=window?key+":"+window.start+":"+window.end:"";
    if(config.auto===false)result={root:Harmony.rootPcs[config.root],rootSpelling:Harmony.rootNames[config.root],definition:config.quality,kind:"手动指定"};
    else if(window && config.pedalMode===0) {
        if(!state.pedalResults[pedalKey]) {
            var evidence=state.evidence[pedalKey];
            result=Harmony.detectWeighted(evidence.all,evidence.weights,null,tonic,config.keyMode===1,row.start);
            state.pedalResults[pedalKey]=describe(result,evidence.representative.length?evidence.representative:evidence.all,row.start,tonic,config.keyMode===1,config.degreeMode,flats);
            state.pedalResults[pedalKey].kind="踏板区间 · "+(result.kind||"综合识别");
        }
        result=state.pedalResults[pedalKey];
    } else {
        var weights={},heldNotes=row.notes.filter(function(n){return n.end===undefined||n.end>row.start;});
        var attacks=heldNotes.filter(function(n){return (n.attackTick===undefined?n.tick:n.attackTick)===row.start;});
        var freshBass=attacks.length?bass(attacks,row.start):null;
        var heldBass=bass(heldNotes,row.start);
        // Upper extensions alone must not demote a held accompaniment and invent a new root.
        var bassAttack=freshBass && (!heldBass || freshBass.pitch<=heldBass.pitch+12);
        var strongAttack=bassAttack && (attacks.length>=3 || heldNotes.some(function(n){return n.pitch>freshBass.pitch+12;}));
        for(var n=0;n<row.notes.length;++n) {
            var note=row.notes[n],pc=Harmony.mod12(note.pitch),active=note.end===undefined||note.end>row.start;
            var oldAttack=(note.attackTick===undefined?note.tick:note.attackTick)<row.start;
            var weight=active?(strongAttack && attacks.length>=3 && oldAttack?.3:1):(strongAttack?.12:.3);
            // A stepwise passing/neighbor tone does not automatically rename the accompaniment.
            if(active && note.pitch>(heldBass?heldBass.pitch:note.pitch)+12 && ornament(state,row,key,note))weight*=.18;
            weights[pc]=Math.max(weights[pc]||0,weight);
        }
        row.strongAttack=!!strongAttack;row.attackCount=attacks.length;
        result=Harmony.detectWeighted(row.notes,weights,null,tonic,config.keyMode===1,row.start,{bass:strongAttack?freshBass:null,collect:true});
        if(strongAttack && freshBass)result.harmonicBass=Harmony.mod12(freshBass.pitch);
    }
    if(result.chord===undefined)result=describe(result,row.notes,row.start,tonic,config.keyMode===1,config.degreeMode,flats);
    if(!row.notes.length && config.auto!==false && !row.frame.imported){row.pathOptions=[null];state.previous=null;return;}
    if(row.frame.imported) {
        result=describe(row.frame,row.notes,row.start,tonic,config.keyMode===1,config.degreeMode,flats);
        if(row.frame.chord)result.chord=row.frame.chord;
        if(row.frame.degree)result.degree=row.frame.degree;
    }
    if(result.root<0 && !(row.frame.imported && row.frame.chord)){row.pathOptions=[null];return;}
    var choices=result.candidates||[];delete result.candidates;row.pathOptions=[result];
    if(!row.frame.imported && config.auto!==false && !(window && config.pedalMode===0))for(var c=0;c<choices.length;++c) {
        var candidate=choices[c];if(candidate.root===result.root && candidate.definition===result.definition)continue;
        if(result.harmonicBass!==undefined)candidate.harmonicBass=result.harmonicBass;
        candidate.kind="结合前后和弦";
        row.pathOptions.push(describe(candidate,row.notes,row.start,tonic,config.keyMode===1,config.degreeMode,flats));
    }
    state.previous=result;
}
function transition(previous,current,row) {
    if(!previous || !current || previous.root<0 || current.root<0 || !Harmony.defs[previous.definition] || !Harmony.defs[current.definition] || row.frame.imported)return 0;
    if(previous.root===current.root && previous.definition===current.definition)return 0;
    var cost=row.strongAttack?.2:row.attackCount?.4:.65;
    if(previous.root===current.root)cost*=.55;
    var definition=Harmony.defs[previous.definition];
    if(Harmony.mod12(previous.root-current.root)===7 && definition.intervals.indexOf(4)>=0 && definition.intervals.indexOf(10)>=0)cost-=.06;
    return cost;
}
function scorePath(row,previous) {
    var choices=row.pathOptions||[null],weight=Math.max(.25,Math.min(2,(row.end-row.start)/480));
    row.pathCosts=[];row.pathBack=[];
    var minimum=Infinity;
    for(var i=0;i<choices.length;++i)minimum=Math.min(minimum,choices[i]&&typeof choices[i].rawCost==="number"?choices[i].rawCost:0);
    for(i=0;i<choices.length;++i) {
        var local=((choices[i]&&typeof choices[i].rawCost==="number"?choices[i].rawCost:0)-minimum)*weight,best=Infinity,back=0;
        if(!previous)best=0;
        else for(var j=0;j<previous.pathCosts.length;++j) {
            var cost=previous.pathCosts[j]+transition(previous.pathOptions[j],choices[i],row);
            if(cost<best-1e-9){best=cost;back=j;}
        }
        row.pathCosts.push(best+local);row.pathBack.push(back);
    }
}
function emitRegion(state,row,key) {
    var result=row.chosen;if(!result){state.current=null;return;}
    var current=state.current,window=row.pedal,same=current && current.end===row.start && signature(current)===signature(result);
    if(window && state.config.pedalMode===0 && current && current.pedalStart===window.start)same=true;
    if(same)current.end=row.end;
    else {
        current=clone(result);current.start=row.start;current.end=row.end;current.part=Number(key);
        current.id=key+":"+row.start;current.source=row.frame.imported?"imported":"auto";
        current.notes=clone(row.notes);current.bar=row.frame.bar;current.beat=row.frame.beat;
        current.pedalStart=window?window.start:-1;state.regions.push(current);state.current=current;
    }
}
function step(state, limit) {
    for(var count=0;count<limit;++count) {
        if(state.phase===0) {
            if(state.frameIndex<state.frames.length) {
                var i=state.frameIndex++,frame=state.frames[i];
                ingest(state,frame,i+1<state.frames.length?state.frames[i+1].tick:(frame.scoreEnd||frame.tick+480));
                continue;
            }
            for(var key in state.byPart)state.keys.push(key);
            state.phase=1;
        }
        if(state.partIndex>=state.keys.length) {
            state.regions.sort(function(a,b){return a.start-b.start||a.part-b.part;});state.phase=2;return true;
        }
        key=state.keys[state.partIndex];var rows=state.byPart[key];
        if(state.partPass==="score") {
            if(state.rowIndex<rows.length) {
                var index=state.rowIndex++;evaluate(state,rows[index],key);scorePath(rows[index],index?rows[index-1]:null);
            } else {
                state.partPass="trace";state.traceIndex=rows.length-1;
                var costs=rows[state.traceIndex].pathCosts,best=0;
                for(var c=1;c<costs.length;++c)if(costs[c]<costs[best]-1e-9)best=c;
                state.traceChoice=best;
            }
        } else if(state.partPass==="trace") {
            if(state.traceIndex>=0) {
                var row=rows[state.traceIndex--];row.chosen=row.pathOptions[state.traceChoice];
                if(row.chosen && row.pathOptions.length>1) {
                    row.chosen.alternatives=row.pathOptions.filter(function(option){return option && (option.root!==row.chosen.root || option.definition!==row.chosen.definition);}).map(function(option){return {root:option.root,definition:option.definition,cost:option.rawCost};});
                    row.chosen.kind="结合前后和弦 · 有其他解释";
                }
                state.traceChoice=row.pathBack[state.traceChoice];
            } else {state.partPass="emit";state.rowIndex=0;state.current=null;}
        } else if(state.rowIndex<rows.length)emitRegion(state,rows[state.rowIndex++],key);
        else {state.partIndex++;state.rowIndex=0;state.partPass="score";state.previous=null;state.current=null;}
    }
    return state.phase===2;
}
function build(frames, config, fallbackPart, overrides) {
    var state=createBuilder(frames,config,fallbackPart);
    while(!step(state,128)){}
    return overlay(state.regions,overrides||[]);
}
function overlay(regions, overrides) {
    if(!overrides.length)return regions.slice();
    var out=clone(regions);
    for(var i=0;i<overrides.length;++i) {
        var edit=overrides[i],next=[];
        for(var j=0;j<out.length;++j) {
            var region=out[j];
            if(region.part!==edit.part || region.end<=edit.start || region.start>=edit.end){next.push(region);continue;}
            if(region.start<edit.start){var left=clone(region);left.end=edit.start;next.push(left);}
            if(region.end>edit.end){var right=clone(region);right.start=edit.end;right.id=right.part+":"+right.start;next.push(right);}
        }
        if(!edit.suppressed){var manual=clone(edit);manual.id=manual.part+":"+manual.start;manual.source=edit.source==="imported"?"imported":"manual";next.push(manual);}
        out=next;
    }
    out.sort(function(a,b){return a.start-b.start||a.part-b.part;});return out;
}
function validEdits(edits) {
    if(!Array.isArray(edits)||edits.length>10000)throw Error("人工区间数量无效");
    return edits.map(function(edit){
        if(!edit || !NumberIsInteger(edit.start,0,2147483646) || !NumberIsInteger(edit.end,edit.start+1,2147483647) ||
           !NumberIsInteger(edit.part,0,1023) || !NumberIsInteger(edit.root,-1,11) || !NumberIsInteger(edit.definition,-1,28) ||
           ((edit.root<0)!==(edit.definition<0)))throw Error("人工区间无效");
        var out={start:edit.start,end:edit.end,part:edit.part,root:edit.root,definition:edit.definition,
            suppressed:edit.suppressed===true,bass:NumberIsInteger(edit.bass,-1,11)?edit.bass:-1,
            tonic:NumberIsInteger(edit.tonic,0,11)?edit.tonic:0,minor:edit.minor===true};
        out.chord=typeof edit.chord==="string"?edit.chord.slice(0,64):"";
        out.degree=typeof edit.degree==="string"?edit.degree.slice(0,24):"";
        out.chromatic=Harmony.isChromatic(out.tonic,out.minor,out.root,out.definition);
        if(edit.source==="imported")out.source="imported";
        if(edit.notes!==undefined && (!Array.isArray(edit.notes)||edit.notes.length>512))throw Error("人工区间音符无效");
        out.notes=(edit.notes||[]).map(function(n){
            if(!n || !NumberIsInteger(n.tick,0,2147483647) || !NumberIsInteger(n.track,0,1023) || !NumberIsInteger(n.index,0,127) ||
               !NumberIsInteger(n.pitch,0,127) || !NumberIsInteger(n.tpc,-1,33))throw Error("人工区间音符定位无效");
            var note={tick:n.tick,track:n.track,index:n.index,pitch:n.pitch,tpc:n.tpc,writtenPitch:n.writtenPitch,writtenTpc:n.writtenTpc};
            if(NumberIsInteger(n.end,n.tick,2147483647))note.end=n.end;
            if(NumberIsInteger(n.attackTick,0,n.tick))note.attackTick=n.attackTick;
            return note;
        });
        return out;
    });
}
function restore(edits, start, end, part) {
    var result=[];
    for(var i=0;i<edits.length;++i) {
        var edit=edits[i];
        if(edit.part!==part || edit.end<=start || edit.start>=end){result.push(clone(edit));continue;}
        if(edit.start<start){var left=clone(edit);left.end=start;result.push(left);}
        if(edit.end>end){var right=clone(edit);right.start=end;result.push(right);}
    }
    return result;
}
function NumberIsInteger(value,min,max) {return typeof value==="number" && isFinite(value) && Math.floor(value)===value && value>=min && value<=max;}
function workerBundle(sources) {
    function module(name,source) {
        var clean=source.replace(/^\.(pragma|import).*$/gm,""),names=[],match,pattern=/^(?:function|var)\s+([A-Za-z_$][\w$]*)/gm;
        while((match=pattern.exec(clean)))if(names.indexOf(match[1])<0)names.push(match[1]);
        return "var "+name+"=(function(){\n"+clean+"\nreturn {"+names.map(function(n){return n+":"+n}).join(",")+"};})();\n";
    }
    return "(function(){\n"+module("Harmony",sources.harmony)+module("Timeline",sources.timeline)+"return {regions:Timeline.build(input.frames,input.config,input.part,[])};})()";
}

// Move a boundary together with the neighboring harmony, keeping a continuous timeline.
function resizeNeighbours(regions,region,start,end) {
    var edits=[],previous=null,next=null;
    regions.forEach(function(r){if(r.part!==region.part)return;if(r.end===region.start)previous=r;if(r.start===region.end)next=r;});
    if(start!==region.start){
        if(previous && start>previous.start){var left=clone(previous);left.end=start;left.source="manual";edits.push(left);}
        else if(start>region.start){var gap=clone(region);gap.end=start;gap.suppressed=true;edits.push(gap);}
    }
    if(end!==region.end){
        if(next && end<next.end){var right=clone(next);right.start=end;right.source="manual";edits.push(right);}
        else if(end<region.end){var tail=clone(region);tail.start=end;tail.suppressed=true;edits.push(tail);}
    }
    return edits;
}
