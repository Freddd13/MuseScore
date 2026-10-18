// Application progress belongs to physical score notes, including repeat occurrences.
function copy(value) { return JSON.parse(JSON.stringify(value)); }
function noteKey(note) { return [note.tick, note.track, note.graceIndex === undefined || note.graceIndex === null ? "" : note.graceIndex, note.index].join(":"); }
function pending(candidate, progress) {
    var value = copy(candidate), done = progress || {};
    value.velocity = (value.velocity || []).filter(function(n) { return !(done.velocity || {})[noteKey(n)]; });
    value.tempos = (value.tempos || []).filter(function(n) { return !(done.tempos || {})[n.tick]; });
    value.velocityById = {};
    value.velocity.forEach(function(n) { value.velocityById[n.id] = n; });
    return value;
}
function record(progress, applied) {
    var done = copy(progress || {velocity:{}, tempos:{}});
    (applied.velocity || []).forEach(function(n) { done.velocity[noteKey(n)] = n.oldType===undefined||n.oldRaw===undefined?true:{type:n.oldType,raw:n.oldRaw}; });
    (applied.tempos || []).forEach(function(n) { done.tempos[n.tick] = n.bpm===undefined?true:{bpm:n.bpm}; });
    return done;
}
function reconcile(progress, notes) {
    var done=copy(progress||{velocity:{},tempos:{}});
    notes.forEach(function(n){var key=noteKey(n),saved=done.velocity[key];if(saved&&saved!==true&&(saved.type!==n.velocityType||saved.raw!==n.velocityRaw))delete done.velocity[key];var tempo=done.tempos[n.tick];if(tempo&&tempo!==true&&n.bpm!==undefined&&Math.abs(tempo.bpm-n.bpm)>.01)delete done.tempos[n.tick];});
    return done;
}
function inRange(note, range) {
    return note.tick >= range.start && note.tick < range.end && note.track >= range.firstTrack && note.track < range.endTrack;
}
function portion(candidate, kind, selection) {
    var value = {}; Object.keys(candidate).forEach(function(key){value[key]=candidate[key];});
    if (kind === 2) return value;
    var range = selection.scope, keys = {}, ticks = {};
    (selection.notes || []).forEach(function(n) { keys[noteKey(n)] = true; ticks[n.tick] = true; });
    value.velocity = value.velocity.filter(function(n) { return kind === 0 ? !!keys[noteKey(n)] : inRange(n, range); });
    value.tempos = value.tempos.filter(function(n) { return kind === 0 ? !!ticks[n.tick] : n.tick >= range.start && n.tick < range.end; });
    return value;
}
function refreshed(result, notes) {
    var value = copy(result), byKey = {};
    notes.forEach(function(n) { byKey[noteKey(n)] = n; });
    value.matches.forEach(function(m) { var n=byKey[noteKey(m.original)]; if(n) m.original=copy(n); });
    value.displayMatches = value.matches.slice().sort(function(a,b) { return a.tick-b.tick || a.track-b.track || a.index-b.index; });
    value.missing = (value.missing || []).map(function(n) { return byKey[noteKey(n)] || n; });
    return value;
}
function targets(candidate, config) {
    var value=copy(candidate), velocities=config.previewVelocity||{}, tempos=config.previewTempo||{};
    value.velocityById={};
    (value.velocity||[]).forEach(function(n){if(velocities[n.id]!==undefined)n.target=Math.max(1,Math.min(127,Math.round(velocities[n.id])));value.velocityById[n.id]=n;});
    (value.tempos||[]).forEach(function(n){if(tempos[n.tick]!==undefined)n.bpm=Math.max(5,Math.min(999,tempos[n.tick]));});
    return value;
}
function display(result, progress, showApplied) {
    var done=(progress||{}).velocity||{}, rows=[], indexes={}, published=[];
    (result.matches||[]).forEach(function(match,index){
        var applied=!!done[noteKey(match.original)];if(applied&&!showApplied)return;
        indexes[index]=rows.length;rows.push({index:index,match:match,applied:applied});
        var value={};Object.keys(match).forEach(function(key){value[key]=match[key];});value.applied=applied;published.push(value);
    });
    var filtered={};Object.keys(result).forEach(function(key){filtered[key]=result[key];});filtered.matches=published;
    filtered.displayMatches=published.slice().sort(function(a,b){return a.tick-b.tick||a.track-b.track||a.index-b.index;});
    filtered.timing=timing(result);
    return {rows:rows,indexes:indexes,result:filtered};
}
// A monotone local clock, calculated when the matching changes, including used notes.
function timing(result) {
    var groups={}, all=result.matches||[], occurrence=0;
    all.forEach(function(m){var n=m.original||{};if(n.occurrence>0&&(occurrence===0||n.occurrence<occurrence))occurrence=n.occurrence;});
    all.forEach(function(m){
        if(occurrence&&(m.original||{}).occurrence!==occurrence)return;
        if(!m.confirmed&&m.confidence<.8)return;
        if(!groups[m.tick])groups[m.tick]=[];groups[m.tick].push(m.seconds);
    });
    var points=[];Object.keys(groups).sort(function(a,b){return a-b;}).forEach(function(tick){
        var values=groups[tick].sort(function(a,b){return a-b;}),seconds=values[Math.floor(values.length/2)];
        if(!points.length||seconds>points[points.length-1].seconds)points.push({seconds:seconds,tick:Number(tick)});
    });
    if(points.length<2&&all.length>1){
        var sorted=all.slice().sort(function(a,b){return a.seconds-b.seconds;}),first=sorted[0],last=sorted[sorted.length-1];
        if(last.seconds>first.seconds&&last.tick>first.tick)points=[{seconds:first.seconds,tick:first.tick},{seconds:last.seconds,tick:last.tick}];
    }
    return points;
}
function tickAt(points, seconds) {
    if(!points.length)return null;if(points.length===1)return points[0].tick;
    var low=1,high=points.length-1;
    while(low<high){var mid=Math.floor((low+high)/2);if(points[mid].seconds<seconds)low=mid+1;else high=mid;}
    var p=points[low-1],q=points[low];return p.tick+(seconds-p.seconds)*(q.tick-p.tick)/(q.seconds-p.seconds);
}
function changedMatches(progress, before, after) {
    var done=copy(progress||{velocity:{},tempos:{}}), previous={}, changed=false;
    (before.matches||[]).forEach(function(m){previous[m.scoreId]=m.midiId;});
    (after.matches||[]).forEach(function(m){if(previous[m.scoreId]&&previous[m.scoreId]!==m.midiId){delete done.velocity[noteKey(m.original)];changed=true;}});
    if(changed)done.tempos={};return done;
}
