.pragma library

// MIDI note-off suppression and restrikes have already been resolved by the host.
// Keep the written note reference alongside each actual key/sustain interval.
function copy(value) {return JSON.parse(JSON.stringify(value));}
function create(data,handIndex,handOverrides) {
    var boundaries={};handIndex=handIndex||{};
    for(var track in handIndex) {
        var index=handIndex[track];Object.keys(index.singles).forEach(function(tick){boundaries[track+":"+tick]=true;});
        index.left.concat(index.right).forEach(function(range){boundaries[track+":"+range.start.tick]=true;boundaries[track+":"+range.end.tick]=true;});
    }
    return {data:data,handIndex:handIndex,handOverrides:handOverrides||[],handBoundaries:boundaries,phase:0,index:0,byId:{},chains:{},notationCache:{},chordSizes:{},keys:{},pedals:{},released:{},windows:[],pedalWindows:[],activePedals:{},
        onsets:{},times:{},ticks:[],active:[],activeKeys:[],ranges:(data.ranges||[]).slice().sort(function(a,b){return a.start-b.start;}),rangeIndex:0,frame:null};
}
function closeSound(window,row) {
    window.end=row.utick;window.endSeconds=row.seconds;
}
function closeKey(s,key,row) {
    var window=s.keys[key];if(!window)return;
    window.keyEnd=row.utick;window.keyEndSeconds=row.seconds;delete s.keys[key];
    if(s.pedals[window.channelKey]) (s.released[window.channelKey]||(s.released[window.channelKey]=[])).push(window);
    else closeSound(window,row);
}
function source(s,row) {
    var value=copy(s.byId[row.id]||s.byId[row.logicalId]||row);
    ["tick","track","index","graceIndex","grace","pitch","writtenPitch","eventIndex"].forEach(function(key){if(row[key]!==undefined)value[key]=row[key];});
    if(row.tpc===undefined)delete value.tpc;else value.tpc=row.tpc;
    value.id=row.id;value.velocity=row.velocity;value.occurrence=row.occurrence||1;
    return value;
}
function event(s,row) {
    var channel=row.partId+":"+row.channel,key=channel+":"+row.pitch;
    if(row.kind==="pedal") {
        var down=row.value>=64;
        var current=s.activePedals[channel];
        if(down && !current) {
            current={tick:row.utick,seconds:row.seconds,channelKey:channel,supported:row.value===127};
            s.activePedals[channel]=current;s.pedalWindows.push(current);
        }
        if(current && row.value!==0 && row.value!==127)current.supported=false;
        if(!down && current){current.end=row.utick;current.endSeconds=row.seconds;delete s.activePedals[channel];}
        if(!down)(s.released[channel]||[]).forEach(function(window){closeSound(window,row);});
        if(!down)s.released[channel]=[];
        s.pedals[channel]=down;return;
    }
    if(row.muted)return;
    if(row.kind==="off") {if(!row.discard)closeKey(s,key,row);return;}
    if(row.kind!=="on")return;
    var previous=s.keys[key],note=source(s,row);
    if(previous && previous.start===row.utick) {
        previous.notes.push(note);previous.velocity=row.velocity;return;
    }
    // Seq sends a release before a restrike; the new strike replaces the old voice.
    if(previous){previous.keyEnd=row.utick;previous.keyEndSeconds=row.seconds;closeSound(previous,row);delete s.keys[key];}
    (s.released[channel]||[]).forEach(function(window){if(window.pitch===row.pitch&&window.end===undefined)closeSound(window,row);});
    s.released[channel]=(s.released[channel]||[]).filter(function(window){return window.end===undefined;});
    var window={key:key,channelKey:channel,part:row.partId,pitch:row.pitch,start:row.utick,startSeconds:row.seconds,
        velocity:row.velocity,notes:[note],occurrence:row.occurrence||1,offset:row.offset||0};
    s.windows.push(window);s.keys[key]=window;
}
function rangeAt(s,tick) {
    while(s.rangeIndex<s.ranges.length && s.ranges[s.rangeIndex].end<=tick)++s.rangeIndex;
    var range=s.ranges[s.rangeIndex];return range && range.start<=tick && tick<range.end?range:null;
}
function step(s) {
    s.frame=null;
    if(s.phase===0) {
        var notes=s.data.notes||[];
        if(s.index<notes.length){var note=notes[s.index++];s.byId[note.id]=note;if(note.logicalId&&!s.byId[note.logicalId])s.byId[note.logicalId]=note;
            if(!note.grace) {
                var chain=s.chains[note.logicalId||note.id]||(s.chains[note.logicalId||note.id]=[]),at=chain.length;
                while(at && chain[at-1].tick>note.tick)--at;chain.splice(at,0,note);
                var chord=note.tick+":"+note.track;s.chordSizes[chord]=(s.chordSizes[chord]||0)+1;
                var correction=s.handOverrides.some(function(range){return range.start<=note.tick && note.tick<range.end && range.firstTrack<=note.track && note.track<range.endTrack;});
                var changed=note.fingerings&&note.fingerings.length || s.handBoundaries[note.track+":"+note.tick] || correction || note.attackTrack!==undefined && note.track!==note.attackTrack;
                if(note.tieBack && changed && typeof note.positionSeconds==="number")s.ranges.forEach(function(range){
                    if(note.tick<range.scoreStart || note.tick>=range.scoreEnd || typeof range.scoreStartSeconds!=="number")return;
                    var tick=note.tick+range.offset;if(!s.onsets[tick])s.onsets[tick]=[];
                    s.times[tick]=range.startSeconds+note.positionSeconds-range.scoreStartSeconds;
                });
            }
        }
        else{s.phase=1;s.index=0;}
    } else if(s.phase===1) {
        if(s.index<s.data.events.length)event(s,s.data.events[s.index++]);
        else{s.phase=2;s.index=0;}
    } else if(s.phase===2) {
        if(s.index<s.windows.length){var window=s.windows[s.index++];
            // Complete exports pair their notes. Leave unmatched material out of acoustic advice.
            if(window.end===undefined && window.keyEnd!==undefined && s.data.endTick>=window.keyEnd)closeSound(window,{utick:s.data.endTick,seconds:s.data.endSeconds});
            if(window.end===undefined)window.incomplete=true;
            if(!window.incomplete && window.end>window.start){(s.onsets[window.start]||(s.onsets[window.start]=[])).push(window);s.times[window.start]=window.startSeconds;}
        } else {
            s.ranges.forEach(function(range){if(!s.onsets[range.start])s.onsets[range.start]=[];s.times[range.start]=range.startSeconds;});
            s.ticks=Object.keys(s.onsets).map(Number).sort(function(a,b){return a-b;});s.phase=3;s.index=0;
        }
    } else if(s.phase===3) {
        if(s.index>=s.ticks.length){s.phase=4;return true;}
        var tick=s.ticks[s.index++];s.active=s.active.filter(function(window){return window.end>tick;}).concat(s.onsets[tick]);
        s.activeKeys=s.activeKeys.filter(function(window){return window.keyEnd>tick;}).concat(s.onsets[tick]);
        var range=rangeAt(s,tick);
        if(range){
            var written=tick-range.offset,attacks=s.onsets[tick].filter(function(window){return window.notes.some(function(note){return note.tick>=range.scoreStart&&note.tick<range.scoreEnd;});});
            if(attacks.length)written=attacks[attacks.length-1].notes[0].tick;
            s.frame={utick:tick,seconds:s.times[tick],tick:written,windows:s.active.slice(),keys:s.activeKeys.slice(),range:range};
        }
    } else return true;
    return false;
}
function run(data) {var state=create(data),frames=[];while(!step(state))if(state.frame)frames.push(state.frame);return {windows:state.windows,frames:frames};}

// Keep the score reference intact. The planner reads these separate physical
// fields so edited note lengths, repeats and delayed attacks use actual MIDI.
function notationAt(s,source,tick) {
    var key=source.logicalId||source.id,chain=s?s.chains[key]:null;
    if(!chain || chain.length<2 || chain[0].pitch!==source.pitch)return copy(source);
    var cached=s.notationCache[key];
    if(!cached) {
        cached=[];var fingers=[],anchor={tick:chain[0].tick,track:chain[0].track,index:chain[0].index};
        for(var i=0;i<chain.length;++i) {
            var n=chain[i],index=s.handIndex[n.track],singles=index?index.singles[n.tick]||[]:[];
            if(singles.some(function(mark){return mark.index===undefined||mark.index<0||mark.index===n.index;}))anchor={tick:n.tick,track:n.track,index:n.index};
            if(n.fingerings && n.fingerings.length)fingers=n.fingerings;
            cached.push({note:n,fingers:fingers,anchor:anchor});
        }
        s.notationCache[key]=cached;
    }
    var lo=0,hi=cached.length;while(lo<hi){var mid=(lo+hi)>>1;if(cached[mid].note.tick<=tick)lo=mid+1;else hi=mid;}
    var current=cached[Math.max(0,lo-1)],note=copy(current.note);
    note.fingerings=copy(current.fingers);note.handAnchors=[current.anchor];
    note.physicalSourceId=source.id;note.physicalSourceTrack=source.track;
    note.assignmentTick=note.tick;note.assignmentIndex=note.index;note.assignmentChordSize=s.chordSizes[note.tick+":"+note.track]||1;
    ["pitch","velocity","occurrence","eventIndex","play","piano"].forEach(function(key){if(source[key]!==undefined)note[key]=source[key];});
    return note;
}
function physicalNotes(windows,tick,s,scoreTick) {
    var result=[];
    windows.forEach(function(window){
        if(window.incomplete || window.start>tick || !(window.keyEnd>tick))return;
        window.notes.forEach(function(source){
            if(source.grace || source.play===false || source.piano===false)return;
            var note=notationAt(s,source,scoreTick);
            note.analysisId=source.id+"@"+window.channelKey+":"+window.start;
            note.physicalStart=window.start;note.physicalEnd=window.keyEnd;
            note.physicalOnSeconds=window.startSeconds;note.physicalEndSeconds=window.keyEndSeconds;
            note.playbackChannel=window.channelKey;note.partId=window.part;
            result.push(note);
        });
    });
    return result;
}
function physicalFrame(s,frame) {
    var keys=frame.keys||frame.windows,result={tick:frame.tick,playbackTick:frame.utick,seconds:frame.seconds,notes:physicalNotes(keys,frame.utick,s,frame.utick-frame.range.offset),lookahead:[]};
    var pool=keys.slice(),rangeIndex=s.rangeIndex,last=frame.utick,counts={},pending=0;
    result.notes.forEach(function(note){if(counts[note.partId]===undefined){counts[note.partId]=0;++pending;}});
    for(var i=s.index;i<s.ticks.length && i<s.index+32 && pending>0;++i){
        var at=s.ticks[i],onsets=s.onsets[at]||[];
        pool=pool.filter(function(window){return window.keyEnd>at;}).concat(onsets);
        while(rangeIndex<s.ranges.length && s.ranges[rangeIndex].end<=at)++rangeIndex;
        var range=s.ranges[rangeIndex];if(!range || at<range.start || at>=range.end)continue;
        var notes=physicalNotes(pool,at,s,at-range.offset);
        var needed={};notes.forEach(function(note){if(note.physicalStart===at && counts[note.partId]!==undefined && counts[note.partId]<2)needed[note.partId]=true;});
        if(!Object.keys(needed).length)continue;
        var written=at-range.offset;
        for(var j=onsets.length-1;j>=0;--j)if(onsets[j].notes.length){written=onsets[j].notes[0].tick;break;}
        result.lookahead.push({tick:written,playbackTick:at,seconds:s.times[at],notes:notes});last=at;
        for(var part in needed)if(++counts[part]===2)--pending;
    }
    result.playbackPedals=s.pedalWindows.filter(function(pedal){return pedal.tick<=last && pedal.end>frame.utick;});
    return result;
}
