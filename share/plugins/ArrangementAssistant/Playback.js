.pragma library

// MIDI note-off suppression and restrikes have already been resolved by the host.
// Keep the written note reference alongside each actual key/sustain interval.
function copy(value) {return JSON.parse(JSON.stringify(value));}
function create(data) {
    return {data:data,phase:0,index:0,byId:{},keys:{},pedals:{},released:{},windows:[],
        onsets:{},times:{},ticks:[],active:[],ranges:(data.ranges||[]).slice().sort(function(a,b){return a.start-b.start;}),rangeIndex:0,frame:null};
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
        if(s.index<notes.length){var note=notes[s.index++];s.byId[note.id]=note;if(note.logicalId&&!s.byId[note.logicalId])s.byId[note.logicalId]=note;}
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
        var range=rangeAt(s,tick);
        if(range){
            var written=tick-range.offset,attacks=s.onsets[tick].filter(function(window){return window.notes.some(function(note){return note.tick>=range.scoreStart&&note.tick<range.scoreEnd;});});
            if(attacks.length)written=attacks[attacks.length-1].notes[0].tick;
            s.frame={utick:tick,seconds:s.times[tick],tick:written,windows:s.active.slice(),range:range};
        }
    } else return true;
    return false;
}
function run(data) {var state=create(data),frames=[];while(!step(state))if(state.frame)frames.push(state.frame);return {windows:state.windows,frames:frames};}
