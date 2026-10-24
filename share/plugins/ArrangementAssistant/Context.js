.pragma library

function owner(parts,track,fallback) {
    for(var i=0;i<parts.length;++i) {
        var first=parts[i].firstTrack===undefined?parts[i].startTrack:parts[i].firstTrack;
        if(first<=track && track<parts[i].endTrack)return first;
    }
    return fallback;
}
function create(before,firstTrack,endTrack,parts,harmonies) {
    var state={before:before,next:before,wanted:{},found:{},done:before<=0};
    for(var i=0;i<parts.length;++i) {
        var first=parts[i].firstTrack===undefined?parts[i].startTrack:parts[i].firstTrack;
        if(first<endTrack && parts[i].endTrack>firstTrack)state.wanted[first]=true;
    }
    if(!Object.keys(state.wanted).length)state.wanted[firstTrack]=true;
    for(i=0;i<harmonies.length;++i)if(harmonies[i].tick===before)state.found[owner(parts,harmonies[i].track,firstTrack)]=harmonies[i];
    if(Object.keys(state.wanted).every(function(part){return !!state.found[part];}))state.done=true;
    return state;
}
function take(state,page) {
    if(page.error)return page.error;
    var rows=page.harmonies||[];
    for(var i=0;i<rows.length;++i) {
        var row=rows[i],part=row.part;
        if(!state.wanted[part] || row.tick>=state.before)continue;
        if(!state.found[part] || state.found[part].tick<=row.tick)state.found[part]=row;
    }
    state.done=!!page.done || Object.keys(state.wanted).every(function(part){return !!state.found[part];});
    if(!state.done && !(page.nextTick>=0 && page.nextTick<state.next))return "no-progress";
    state.next=page.nextTick;return "";
}
function previous(state) {
    return Object.keys(state.found).map(function(part){return state.found[part];}).filter(function(row){return row.tick<state.before;});
}
function mergeRows(oldRows,newRows,begin,end,intervals) {
    var rows=(oldRows||[]).filter(function(row){return intervals?(row.end<=begin || row.tick>=end):(row.tick<begin || row.tick>=end);}),keys={};
    rows.concat(newRows||[]).forEach(function(row){keys[[row.tick,row.track===undefined?row.measure:row.track,row.index,row.graceIndex,row.hand,row.marker,row.pitch,row.left].join(":")]=row;});
    return Object.keys(keys).map(function(key){return keys[key];}).sort(function(a,b){return a.tick-b.tick;});
}
function merge(cached,pending,begin,end) {
    var result={};for(var key in cached)result[key]=cached[key];
    result.notes=(cached.notes||[]).filter(function(note){return note.tick<begin || note.tick>=end;}).concat(pending.notes);
    result.pedals=(cached.pedals||[]).filter(function(pedal){return pedal.end<=begin || pedal.tick>=end;}).concat(pending.pedals);
    ["harmonies","hands","keys","tempos"].forEach(function(name){result[name]=mergeRows(cached[name],pending[name],begin,end,false);});
    ["rests","meters"].forEach(function(name){result[name]=mergeRows(cached[name],pending[name],begin,end,true);});
    result.parts=pending.parts;
    var regions=[];(cached.harmonyRegions||[]).forEach(function(region){
        if(region.end<=begin || region.start>=end){regions.push(region);return;}
        if(region.start<begin){var left={};for(var field in region)left[field]=region[field];left.end=begin;regions.push(left);}
        if(region.end>end){var right={};for(var field in region)right[field]=region[field];right.start=end;regions.push(right);}
    });
    result.harmonyRegions=regions.concat(pending.harmonyRegions||[]);return result;
}
