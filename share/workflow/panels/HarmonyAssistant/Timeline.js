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
function suppressionIndex(edits) {
    var grouped={};
    (edits||[]).forEach(function(edit){
        if(edit.suppressed)(grouped[edit.part]||(grouped[edit.part]=[])).push({start:edit.start,end:edit.end});
    });
    for(var part in grouped) {
        var rows=grouped[part].sort(function(a,b){return a.start-b.start;}),merged=[];
        rows.forEach(function(row){
            var last=merged.length?merged[merged.length-1]:null;
            if(last && row.start<=last.end)last.end=Math.max(last.end,row.end);else merged.push(row);
        });
        grouped[part]=merged;
    }
    return grouped;
}
function isSuppressed(index,tick,part) {
    var rows=index&&index[part]||[],lo=0,hi=rows.length;
    while(lo<hi){var mid=(lo+hi)>>1;if(rows[mid].start<=tick)lo=mid+1;else hi=mid;}
    return lo>0 && tick<rows[lo-1].end;
}
function displayAtTick(regions,tick,part,hold,blocked) {
    var current=atTick(regions,tick,part);
    if(current || !hold || isSuppressed(blocked,tick,part))return current;
    var lo=0,hi=regions.length;
    while(lo<hi){var mid=(lo+hi)>>1;if(regions[mid].start<=tick)lo=mid+1;else hi=mid;}
    for(var i=lo-1;i>=0;--i)if(regions[i].part===part)return regions[i];
    return null;
}
function bass(notes, tick) {
    var sounding=notes.filter(function(n){return n.end===undefined || n.end>tick;});
    // Expired pedal residue cannot outrank an actually held bass.
    var pool=sounding.length?sounding:notes;
    return pool.length?pool.reduce(function(a,b){return a.pitch<=b.pitch?a:b;}):null;
}
function describe(result, notes, tick, tonic, minor, degreeMode, preferFlats, tonicSpelling) {
    var out=clone(result), b=bass(notes,tick), flats=typeof preferFlats==="boolean"?preferFlats:[1,3,5,8,10].indexOf(tonic)>=0;
    out.bass=b?Harmony.mod12(b.pitch):-1;out.bassTpc=b?b.tpc:-1;
    out.bassName=b?Harmony.spelledName(b.tpc,b.pitch,flats):"";
    out.chord="";out.degree="";out.tonic=tonic;out.tonicSpelling=tonicSpelling||Harmony.pcName(tonic,flats);out.minor=minor;out.inversion="未指定和弦";
    if(out.root>=0) {
        var rootNote=null;
        for(var i=0;i<notes.length;++i)if(Harmony.mod12(notes[i].pitch)===out.root){rootNote=notes[i];break;}
        out.rootSpelling=out.rootSpelling|| (rootNote?Harmony.spelledName(rootNote.tpc,rootNote.pitch,flats):Harmony.pcName(out.root,flats));
        out.chord=out.rootSpelling+Harmony.defs[out.definition].suffix;
        if(b && out.bass!==out.root)out.chord+="/"+Harmony.spelledName(b.tpc,b.pitch,flats);
        out.degree=Harmony.roman(tonic,minor,out.root,out.definition,degreeMode,out.bass,out.bassName,out.rootSpelling,out.tonicSpelling);
        out.inversion=b?Harmony.inversion(Harmony.labelFor(out.bass,out.root,out.definition)):"低音未确定";
    }
    out.chromatic=Harmony.isChromatic(tonic,minor,out.root,out.definition);
    return out;
}
function signature(r) {return [r.root,r.definition,r.bass,r.tonic,r.minor,r.chord,r.degree].join(":");}
function partsFor(frame, fallback) {return frame.parts && frame.parts.length?frame.parts:[{startTrack:fallback,endTrack:1024}];}
function frameForPart(frame, part) {
    if(typeof part.keySignature!=="number" || part.keySignature===frame.keySignature)return frame;
    var local={};for(var key in frame)local[key]=frame[key];local.keySignature=part.keySignature;return local;
}
function tonicAt(frame, config) {
    if(config.manualKey)return Harmony.rootPcs[config.keyTonic];
    var major=[11,6,1,8,3,10,5,0,7,2,9,4,11,6,1];
    return Harmony.mod12(major[(frame.keySignature||0)+7]+(config.keyMode===1?9:0));
}
function tonicNameAt(frame, config) {
    if(config.manualKey)return Harmony.rootNames[config.keyTonic];
    var names=config.keyMode===1?["A♭","E♭","B♭","F","C","G","D","A","E","B","F♯","C♯","G♯","D♯","A♯"]:
        ["C♭","G♭","D♭","A♭","E♭","B♭","F","C","G","D","A","E","B","F♯","C♯"];
    return names[(frame.keySignature||0)+7] || Harmony.pcName(tonicAt(frame,config),false);
}
function pedalAt(frame, part) {
    var windows=frame.pedalWindows||[];
    for(var i=0;i<windows.length;++i)if(windows[i].firstTrack===part && windows[i].start<=frame.tick && frame.tick<windows[i].end)return windows[i];
    return null;
}
function createBuilder(frames, config, fallbackPart) {
    return {frames:frames,config:clone(config),fallbackPart:fallbackPart,byPart:{},keys:[],
        evidence:{},pedalResults:{},ornaments:{},contexts:{},segmentDetections:{},segmentDetectionCount:0,segmentProfiles:{},segmentProfileCount:0,regions:[],phase:0,frameIndex:0,partIndex:0,rowIndex:0,contextIndex:0,segmentJob:null,partPass:"segments",traceIndex:-1,traceChoice:0,previous:null,current:null};
}
var primaryLabels={"1":true,"2":true,"♭3":true,"3":true,"4":true,"♭5":true,"5":true,"♯5":true};
// Jointly compare complete arpeggio spans. One edge is evaluated per step so
// the existing GUI fallback can yield between candidates just like the worker.
function segmentJob(context) {
    context.events.sort(function(a,b){return a.attackTick-b.attackTick||a.track-b.track||a.pitch-b.pitch;});
    return {context:context,bounds:[0],states:[[null]],target:1,from:0,pending:{},phase:"prepare",prepareIndex:0,selected:[],applyIndex:0,rowIndex:0};
}
function prepareSegment(state,job,key) {
    var rows=job.context.rows,bounds=job.bounds,floor=job.context.floor,i=job.prepareIndex++;
    if(i<rows.length) {
        var attacks=rows[i].notes.filter(function(n){return (n.attackTick===undefined?n.tick:n.attackTick)===rows[i].start;});
        var low=attacks.length?bass(attacks,rows[i].start):null;
        rows[i].freshCount=Harmony.unique(attacks.map(function(n){return n.pitch;})).length;
        rows[i].lowEntry=!!low && low.pitch<=floor+12;
        var actual=bass(rows[i].notes.filter(function(n){return n.end===undefined||n.end>rows[i].start;}),rows[i].start);
        if(job.stableBass===undefined && actual)job.stableBass=Harmony.mod12(actual.pitch);
        rows[i].structuralBass=!!low && rows[i].lowEntry && actual && actual.pitch===low.pitch &&
            low.end-rows[i].start>=480 && Harmony.mod12(low.pitch)!==job.stableBass &&
            (rows[i].freshCount>=3 || !ornament(state,rows[i],key,low));
        if(rows[i].structuralBass)job.stableBass=Harmony.mod12(low.pitch);
        var previous=rows[i-1],held=previous?previous.notes.filter(function(n){return n.end===undefined||n.end>previous.start;}):[];
        var previousPcs=Harmony.unique(held.map(function(n){return n.pitch;}));
        var changedPitch=attacks.some(function(n){return previousPcs.indexOf(Harmony.mod12(n.pitch))<0;});
        if(rows[i].lowEntry && rows[i].freshCount>=3) {
            var onset=Harmony.detect(attacks.map(function(n){return n.pitch;}),attacks);
            if(onset.root>=0 && !onset.extra.length)rows[i].onsetRoots=[onset.root].concat((onset.alternatives||[]).map(function(c){return c.root;}));
        }
        if(i>0 && (rows[i].lowEntry || rows[i].freshCount>=3 || changedPitch))bounds.push(i);
    }
    if(job.prepareIndex>=rows.length){bounds.push(rows.length);job.phase="score";}
}
function segmentDurations(notes,start,end,weights) {
    var intervals=[],result={};for(var pc=0;pc<12;++pc)intervals.push([]);
    notes.forEach(function(note){
        var onset=note.attackTick===undefined?note.tick:note.attackTick;
        var left=Math.max(start,onset),right=Math.min(end,note.end===undefined?end:note.end);
        if(right>left)intervals[Harmony.mod12(note.pitch)].push({start:left,end:right});
    });
    for(pc=0;pc<12;++pc) {
        var rows=intervals[pc].sort(function(a,b){return a.start-b.start;}),length=0,left=-1,right=-1;
        rows.forEach(function(row){
            if(left<0){left=row.start;right=row.end;}
            else if(row.start<=right)right=Math.max(right,row.end);
            else {length+=right-left;left=row.start;right=row.end;}
        });
        if(left>=0)length+=right-left;
        result[pc]=(weights[pc]||0)*length/Math.max(1,end-start);
    }
    return result;
}
function rankSegment(state,notes,weights,durations,hint,frame,start) {
    var spellings={},weightKey=[],maximum=0;
    notes.forEach(function(note){spellings[Harmony.mod12(note.pitch)+":"+note.tpc]=true;});
    for(var i=0;i<12;++i){weightKey.push(weights[i]||0,durations[i]||0);maximum=Math.max(maximum,weights[i]||0);}
    var spellingKey=[];for(var name in spellings)spellingKey.push(name);spellingKey.sort();
    var tonic=tonicAt(frame,state.config),minor=state.config.keyMode===1;
    var detectionKey=tonic+":"+minor+":"+(hint?Harmony.mod12(hint.pitch):-1)+":"+weightKey.join(",")+":"+spellingKey.join(",");
    if(state.segmentDetections[detectionKey])return state.segmentDetections[detectionKey];
    var byId={};
    function profileResult(profile) {
        var normalized=[],maximum=0;for(var i=0;i<12;++i)maximum=Math.max(maximum,profile[i]||0);
        for(i=0;i<12;++i)normalized.push(maximum?(profile[i]||0)/maximum:0);
        var profileKey=tonic+":"+minor+":"+(hint?Harmony.mod12(hint.pitch):-1)+":"+normalized.join(",")+":"+spellingKey.join(",");
        if(state.segmentProfiles[profileKey])return state.segmentProfiles[profileKey];
        var detected=Harmony.detectWeighted(notes,profile,null,tonic,minor,start,{bass:hint,collect:true});
        if(state.segmentProfileCount>=4096){state.segmentProfiles={};state.segmentProfileCount=0;}
        state.segmentProfiles[profileKey]=detected;++state.segmentProfileCount;return detected;
    }
    function addProfile(profile) {
        var detected=profileResult(profile);
        if(detected.root>=0)byId[detected.root+":"+detected.definition]=detected;
        (detected.candidates||[]).forEach(function(option){byId[option.root+":"+option.definition]=option;});
    }
    addProfile(weights);
    var needsDuration=false;
    for(var id in byId) {
        var option=byId[id],definition=Harmony.defs[option.definition];
        for(i=0;i<definition.intervals.length;++i) {
            var pc=Harmony.mod12(option.root+definition.intervals[i]);
            if(!primaryLabels[definition.labels[i]] && (durations[pc]||0)<.5*(weights[pc]||0))needsDuration=true;
        }
    }
    if(needsDuration)addProfile(durations);
    var ranked=[],evidence=Harmony.spellingEvidence(notes,weights);
    for(var id in byId) {
        var option=clone(byId[id]),definition=Harmony.defs[option.definition],cost=.23*Math.max(0,definition.intervals.length-3),template={};
        for(i=0;i<definition.intervals.length;++i) {
            var pc=Harmony.mod12(option.root+definition.intervals[i]),label=definition.labels[i];template[pc]=true;
            // Primary chord tones need to occur in the span; colour tones and
            // non-chord notes also use their actual share of sounding time.
            var primary=primaryLabels[label];
            var amount=maximum?(primary?(weights[pc]||0):(durations[pc]||0))/maximum:0;
            cost+=(1-amount)*(definition.intervals[i]===7?.45:pc===option.root?1.8:3.2);
        }
        for(i=0;i<12;++i)if(!template[i])cost+=(durations[i]||0)/Math.max(.001,maximum)*3;
        if(hint)cost-=Harmony.mod12(hint.pitch)===option.root?.6:template[Harmony.mod12(hint.pitch)]?.22:0;
        var fit=Harmony.spellingFit(evidence,option.root,option.definition);cost+=fit.cost;
        option.rootSpelling=fit.name||option.rootSpelling;option.cost=cost;option.rawCost=cost;delete option.candidates;
        if(cost<=6)ranked.push(option);
    }
    ranked.sort(function(a,b){return a.rawCost-b.rawCost||a.definition-b.definition||a.root-b.root;});ranked=ranked.slice(0,4);
    if(!ranked.length)ranked=[{root:-1,definition:-1,rawCost:6,kind:"未确定和弦"}];
    ranked.forEach(function(option){option.alternatives=ranked.filter(function(other){return other!==option&&other.rawCost-option.rawCost<.8;}).map(function(other){return {root:other.root,definition:other.definition,cost:other.rawCost};});});
    if(state.segmentDetectionCount>=4096){state.segmentDetections={};state.segmentDetectionCount=0;}
    state.segmentDetections[detectionKey]=ranked;++state.segmentDetectionCount;return ranked;
}
function segmentEvidence(state,job,from,to,key) {
    var context=job.context,rows=context.rows,first=rows[job.bounds[from]],last=rows[job.bounds[to]-1];
    var start=first.start,end=last.end,notes=[],weights={},active=first.notes.filter(function(n){return (n.attackTick===undefined?n.tick:n.attackTick)<start && (n.end===undefined||n.end>start);});
    var events=context.events,lo=0,hi=events.length;
    while(lo<hi){var mid=(lo+hi)>>1;if(events[mid].attackTick<start)lo=mid+1;else hi=mid;}
    for(var i=lo;i<events.length && events[i].attackTick<end;++i)notes.push(events[i]);
    var fresh=Harmony.unique(notes.map(function(n){return n.pitch;})).length;
    function weigh(note,held) {
        var weight=1;
        if(held)weight*=first.lowEntry && fresh>=3?.15:1;
        if(ornament(state,first,key,note))weight*=.18;
        var pc=Harmony.mod12(note.pitch);weights[pc]=Math.max(weights[pc]||0,weight);
    }
    notes.forEach(function(note){weigh(note,false);});active.forEach(function(note){weigh(note,true);notes.push(note);});
    var harmonicBass=notes.length?notes.reduce(function(a,b){return a.pitch<=b.pitch?a:b;}):null;
    var freshBass=context.events.filter(function(n){return n.attackTick===start;});
    var hint=first.lowEntry && freshBass.length?bass(freshBass,start):harmonicBass;
    var options=rankSegment(state,notes,weights,segmentDurations(notes,start,end,weights),hint,first.frame,start);
    return options.map(function(option){
        var sounding=first.notes.filter(function(n){return (n.attackTick===undefined?n.tick:n.attackTick)<=start && (n.end===undefined||n.end>start);});
        var out=describe(option,sounding.length?sounding:notes,start,tonicAt(first.frame,state.config),state.config.keyMode===1,state.config.degreeMode,undefined,tonicNameAt(first.frame,state.config));
        delete out.candidates;out.kind="分解伴奏 · "+(option.alternatives.length?"有其他解释":"结合前后和弦");out.notes=notes;out.segmentStart=start;out.segmentEnd=end;
        if(first.lowEntry && hint && Harmony.unique(freshBass.map(function(n){return n.pitch;})).length>=3)out.harmonicBass=Harmony.mod12(hint.pitch);
        out.rawCost=typeof option.rawCost==="number"?option.rawCost:typeof option.cost==="number"?option.cost:6;
        return out;
    });
}
function stepSegment(state,job,key) {
    var rows=job.context.rows,bounds=job.bounds;
    if(job.phase==="prepare"){prepareSegment(state,job,key);return false;}
    if(job.phase==="score") {
        var from=job.from++,to=job.target,first=rows[bounds[from]],last=rows[bounds[to]-1];
        var bassChange=false;for(var b=from+1;b<to;++b)if(rows[bounds[b]].structuralBass)bassChange=true;
        if(!bassChange && (last.end-first.start<=3840 || from===to-1)) {
            var options=segmentEvidence(state,job,from,to,key),parents=job.states[from]||[];
            for(var c=0;c<options.length;++c)for(var p=0;p<parents.length;++p) {
                var option=options[c],parent=parents[p],previous=parent&&parent.result,penalty=0;
                if(from>0)penalty=previous && previous.root===option.root && previous.definition===option.definition?.35:
                    first.lowEntry?(first.freshCount>=3?.45:.65):1.4;
                var changes=0;
                for(var b=from+1;b<to;++b) {
                    var boundary=rows[bounds[b]];
                    if(boundary.onsetRoots && boundary.onsetRoots.indexOf(option.root)<0)changes+=.8*Math.max(.25,(rows[bounds[b+1]-1].end-boundary.start)/480);
                }
                var cost=(parent?parent.cost:0)+option.rawCost*Math.max(.25,(last.end-first.start)/480)+penalty+changes+.2;
                var id=option.root+":"+option.definition,best=job.pending[id];
                if(!best || cost<best.cost-1e-9)job.pending[id]={cost:cost,result:option,from:from,to:to,back:parent};
            }
        }
        if(job.from>=to) {
            var ranked=[];for(var id in job.pending)ranked.push(job.pending[id]);
            ranked.sort(function(a,b){return a.cost-b.cost;});job.states[to]=ranked.slice(0,4);job.pending={};
            if(++job.target>=bounds.length) {
                var chosen=job.states[to][0];while(chosen){job.selected.unshift(chosen);chosen=chosen.back;}
                var merged=[];
                job.selected.forEach(function(span){
                    var previous=merged.length?merged[merged.length-1]:null;
                    if(previous && previous.result.root===span.result.root && previous.result.definition===span.result.definition && rows[bounds[span.from]].freshCount<3 && !rows[bounds[span.from]].structuralBass) {
                        previous.to=span.to;previous.result.segmentEnd=span.result.segmentEnd;
                        var seen={};previous.result.notes.forEach(function(n){seen[n.track+":"+n.tick+":"+n.index+":"+n.pitch]=true;});
                        span.result.notes.forEach(function(n){var id=n.track+":"+n.tick+":"+n.index+":"+n.pitch;if(!seen[id]){previous.result.notes.push(n);seen[id]=true;}});
                    } else merged.push(span);
                });
                job.selected=merged;
                job.phase="apply";
            } else job.from=Math.max(0,job.target-8);
        }
        return false;
    }
    if(job.applyIndex>=job.selected.length)return true;
    var span=job.selected[job.applyIndex],index=Math.max(bounds[span.from],job.rowIndex);
    rows[index].segmentResult=span.result;job.rowIndex=index+1;
    if(job.rowIndex>=bounds[span.to])++job.applyIndex;
    return false;
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
        var localFrame=frameForPart(frame,parts[p]);
        var row={frame:localFrame,notes:notes,start:frame.tick,end:end,pedal:window};rows.push(row);
        if(window && state.config.pedalMode===1 && state.config.auto!==false && !frame.imported) {
            var contexts=state.contexts[part]||(state.contexts[part]=[]),context=contexts.length?contexts[contexts.length-1]:null;
            var identity=window.start+":"+window.end+":"+tonicNameAt(localFrame,state.config);
            if(!context || context.identity!==identity || context.end!==frame.tick) {
                context={identity:identity,start:frame.tick,end:end,rows:[],events:[],seen:{},floor:Infinity};contexts.push(context);
            }
            context.rows.push(row);context.end=end;
            for(var n=0;n<notes.length;++n) {
                var note=notes[n],onset=note.attackTick===undefined?note.tick:note.attackTick;
                if(onset<context.start)continue;
                var id=note.track+":"+onset+":"+note.pitch+":"+note.tpc;
                if(!context.seen[id]){context.seen[id]=true;var event=clone(note);event.attackTick=onset;context.events.push(event);context.floor=Math.min(context.floor,note.pitch);}
            }
        }
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
    if(row.segmentResult) {
        row.pathOptions=row.segmentResult.root>=0?[row.segmentResult]:[null];return;
    }
    var config=state.config,tonic=tonicAt(row.frame,config),window=row.pedal,result;
    var keyName=Harmony.rootNames[config.keyTonic]||Harmony.pcName(tonic,false),flats=config.manualKey?
        keyName.indexOf("♭")>=0 || keyName.indexOf("♯")<0 && (config.keyMode===1?[0,2,5,7].indexOf(tonic)>=0:tonic===5):(row.frame.keySignature||0)<0;
    var pedalKey=window?key+":"+window.start+":"+window.end:"";
    if(config.auto===false)result={root:Harmony.rootPcs[config.root],rootSpelling:Harmony.rootNames[config.root],definition:config.quality,kind:"手动指定"};
    else if(window && config.pedalMode===0) {
        var resultKey=pedalKey+":"+tonicNameAt(row.frame,config)+":"+config.keyMode;
        if(!state.pedalResults[resultKey]) {
            var evidence=state.evidence[pedalKey];
            result=Harmony.detectWeighted(evidence.all,evidence.weights,null,tonic,config.keyMode===1,row.start);
            state.pedalResults[resultKey]=describe(result,evidence.representative.length?evidence.representative:evidence.all,row.start,tonic,config.keyMode===1,config.degreeMode,flats,tonicNameAt(row.frame,config));
            state.pedalResults[resultKey].kind="踏板区间 · "+(result.kind||"综合识别");
        }
        result=state.pedalResults[resultKey];
    } else {
        var weights={},heldNotes=row.notes.filter(function(n){return n.end===undefined||n.end>row.start;});
        var attacks=heldNotes.filter(function(n){return (n.attackTick===undefined?n.tick:n.attackTick)===row.start;});
        var freshBass=attacks.length?bass(attacks,row.start):null;
        var heldBass=bass(heldNotes,row.start);
        // Upper extensions alone must not demote a held accompaniment and invent a new root.
        var bassAttack=freshBass && (!heldBass || freshBass.pitch<=heldBass.pitch+12);
        var freshCount=Harmony.unique(attacks.map(function(n){return n.pitch;})).length;
        var strongAttack=bassAttack && freshCount>=3;
        for(var n=0;n<row.notes.length;++n) {
            var note=row.notes[n],pc=Harmony.mod12(note.pitch),active=note.end===undefined||note.end>row.start;
            var oldAttack=(note.attackTick===undefined?note.tick:note.attackTick)<row.start;
            var weight=active?(strongAttack && oldAttack?.3:1):(strongAttack?.12:.3);
            // A stepwise passing/neighbor tone does not automatically rename the accompaniment.
            if(active && note.pitch>(heldBass?heldBass.pitch:note.pitch)+12 && ornament(state,row,key,note))weight*=.18;
            weights[pc]=Math.max(weights[pc]||0,weight);
        }
        row.strongAttack=!!strongAttack;row.attackCount=attacks.length;
        result=Harmony.detectWeighted(row.notes,weights,null,tonic,config.keyMode===1,row.start,{bass:strongAttack?freshBass:null,collect:true});
        if(strongAttack && freshBass)result.harmonicBass=Harmony.mod12(freshBass.pitch);
    }
    if(result.chord===undefined)result=describe(result,row.notes,row.start,tonic,config.keyMode===1,config.degreeMode,flats,tonicNameAt(row.frame,config));
    if(!row.notes.length && config.auto!==false && !row.frame.imported){row.pathOptions=[null];state.previous=null;return;}
    if(row.frame.imported) {
        result=describe(row.frame,row.notes,row.start,tonic,config.keyMode===1,config.degreeMode,flats,tonicNameAt(row.frame,config));
        if(row.frame.chord)result.chord=row.frame.chord;
        if(row.frame.degree)result.degree=row.frame.degree;
    }
    if(result.root<0 && !(row.frame.imported && row.frame.chord)){row.pathOptions=[null];return;}
    var choices=result.candidates||[];delete result.candidates;row.pathOptions=[result];
    if(!row.frame.imported && config.auto!==false && !(window && config.pedalMode===0))for(var c=0;c<choices.length;++c) {
        var candidate=choices[c];if(candidate.root===result.root && candidate.definition===result.definition)continue;
        if(result.harmonicBass!==undefined)candidate.harmonicBass=result.harmonicBass;
        candidate.kind="结合前后和弦";
        row.pathOptions.push(describe(candidate,row.notes,row.start,tonic,config.keyMode===1,config.degreeMode,flats,tonicNameAt(row.frame,config)));
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
    var current=state.current,window=row.pedal,source=row.frame.imported?"imported":"auto";
    var same=current && current.end===row.start && current.source===source && signature(current)===signature(result);
    if(window && state.config.pedalMode===0 && current && current.end===row.start && current.pedalStart===window.start &&
       current.source==="auto" && source==="auto" && current.root===result.root && current.definition===result.definition &&
       current.tonicSpelling===result.tonicSpelling && current.minor===result.minor)same=true;
    if(same)current.end=row.end;
    else {
        current=clone(result);current.start=row.start;current.end=row.end;current.part=Number(key);
        current.id=key+":"+row.start;current.source=source;
        current.notes=clone(result.notes||row.notes);current.bar=row.frame.bar;current.beat=row.frame.beat;
        current.pedalStart=window?window.start:-1;state.regions.push(current);state.current=current;
    }
}
function step(state, limit) {
    for(var count=0;count<limit;++count) {
        if(state.phase===0) {
            if(state.frameIndex<state.frames.length) {
                var i=state.frameIndex++,frame=state.frames[i];
                if(frame.deferredDescription)completeFrame(frame,state.config);
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
        if(state.partPass==="segments") {
            var contexts=state.contexts[key]||[];
            if(state.contextIndex<contexts.length) {
                if(!state.segmentJob)state.segmentJob=segmentJob(contexts[state.contextIndex]);
                if(stepSegment(state,state.segmentJob,key)){state.segmentJob=null;++state.contextIndex;}
            } else state.partPass="score";
        } else if(state.partPass==="score") {
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
        else {state.partIndex++;state.rowIndex=0;state.partPass="segments";state.contextIndex=0;state.segmentJob=null;state.previous=null;state.current=null;}
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
function refreshNotes(edits, frames, parts) {
    if(!edits.length)return [];
    var byPart={}, byId={};
    parts.forEach(function(part){byPart[part.startTrack]=[];});
    function partOf(track) {
        for(var p=0;p<parts.length;++p)if(track>=parts[p].startTrack&&track<parts[p].endTrack)return parts[p].startTrack;
        return -1;
    }
    frames.forEach(function(frame){(frame.notes||[]).forEach(function(note){
        var key=note.tick+":"+note.track+":"+note.index;
        if(byId[key])return;
        byId[key]=note;
        var part=partOf(note.track);
        (byPart[part]||(byPart[part]=[])).push(note);
    });});
    for(var part in byPart)byPart[part].sort(function(a,b){return a.tick-b.tick||a.track-b.track||a.index-b.index;});
    return edits.map(function(edit){
        var out=clone(edit), rows=byPart[edit.part];
        if(!rows||edit.suppressed)return out;
        var lo=0,hi=rows.length;
        while(lo<hi){var mid=(lo+hi)>>1;if(rows[mid].tick<edit.start)lo=mid+1;else hi=mid;}
        var notes=[],seen={};
        function add(note){var key=note.tick+":"+note.track+":"+note.index;if(!seen[key]&&notes.length<512){seen[key]=true;notes.push(clone(note));}}
        (edit.notes||[]).forEach(function(old){
            var note=byId[old.tick+":"+old.track+":"+old.index];
            if(note&&note.tick<edit.start&&note.end>edit.start)add(note);
        });
        for(var i=lo;i<rows.length&&rows[i].tick<edit.end&&notes.length<512;++i)add(rows[i]);
        // A named rest uses the nearest written note as its drawing anchor.
        if(!notes.length&&rows.length)add(rows[Math.min(lo,rows.length-1)]);
        out.notes=notes;
        return out;
    });
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
        if(NumberIsInteger(edit.bassTpc,-1,33))out.bassTpc=edit.bassTpc;
        if(typeof edit.bassName==="string")out.bassName=edit.bassName.slice(0,12);
        if(typeof edit.rootSpelling==="string")out.rootSpelling=edit.rootSpelling.slice(0,12);
        if(typeof edit.tonicSpelling==="string")out.tonicSpelling=edit.tonicSpelling.slice(0,12);
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
function frameDescription(frame) {
    return {tick:frame.tick,root:frame.root,definition:frame.definition,chord:frame.chord,degree:frame.degree,
        rootSpelling:frame.rootSpelling,tonicSpelling:frame.tonicSpelling,chromatic:frame.chromatic};
}
function completeFrame(frame,config) {
    var names=tonicNameAt(frame,config),tonic=tonicAt(frame,config),flats=config.manualKey?names.indexOf("♭")>=0 || names.indexOf("♯")<0 && (config.keyMode===1?[0,2,5,7].indexOf(tonic)>=0:tonic===5):(frame.keySignature||0)<0;
    var result=Harmony.detect(frame.notes.map(function(n){return n.pitch;}),frame.notes);
    var described=describe(result,frame.notes,frame.tick,tonic,config.keyMode===1,config.degreeMode,flats,names);
    for(var key in frameDescription(described))if(key!=="tick")frame[key]=described[key];
    delete frame.deferredDescription;
}
function workerBundle(sources) {
    function module(name,source) {
        var clean=source.replace(/^\.(pragma|import).*$/gm,""),names=[],match,pattern=/^(?:function|var)\s+([A-Za-z_$][\w$]*)/gm;
        while((match=pattern.exec(clean)))if(names.indexOf(match[1])<0)names.push(match[1]);
        return "var "+name+"=(function(){\n"+clean+"\nreturn {"+names.map(function(n){return n+":"+n}).join(",")+"};})();\n";
    }
    return "(function(){\n"+module("Harmony",sources.harmony)+module("Timeline",sources.timeline)+
        "var deferred=input.frames.map(function(f){return !!f.deferredDescription;});var regions=Timeline.build(input.frames,input.config,input.part,[]);"+
        "var output={regions:regions,descriptions:input.frames.map(function(f,i){return deferred[i]?Timeline.frameDescription(f):null;})};"+
        (sources.packed?"return {payload:JSON.stringify(output)};":"return output;")+"})()";
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
