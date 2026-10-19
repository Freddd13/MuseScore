// Value-only rows shared by the compact result list and its checks.
function pitchName(pitch, tpc) {
    if (typeof pitch !== "number" || !isFinite(pitch)) return "音高待核对";
    if (typeof tpc === "number" && tpc >= 0 && tpc <= 33) {
        var degree = tpc % 7, accidental = Math.floor((tpc - 13) / 7);
        return ["C", "G", "D", "A", "E", "B", "F"][degree] +
            (accidental < 0 ? accidental === -2 ? "♭♭" : "♭" : accidental === 2 ? "♯♯" : accidental === 1 ? "♯" : "") +
            (Math.floor((pitch - [0, 7, 2, 9, 4, 11, 5][degree] - accidental) / 12) - 1);
    }
    return ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"][((pitch % 12) + 12) % 12] + (Math.floor(pitch / 12) - 1);
}
function position(note) {
    var text = note.measure ? "第 " + note.measure + " 小节" : "谱面位置";
    if (typeof note.beat === "number") text += " · 第 " + Number(note.beat.toFixed(3)) + " 拍";
    return text;
}
function noteKey(note) {
    return [note.tick, note.track, note.graceIndex === undefined || note.graceIndex === null ? "" : note.graceIndex, note.index].join(":");
}
function keptReasons(candidate) {
    var reasons = {};
    ((candidate || {}).kept || []).forEach(function(item) { reasons[item.id] = item.reason; });
    return reasons;
}
function build(result, progress, candidate, filter) {
    result = result || {};
    var done = (progress || {}).velocity || {}, values = (candidate || {}).velocityById || {}, kept = keptReasons(candidate);
    if (filter === 2 || filter === 3) {
        return (result[filter === 2 ? "missing" : "extra"] || []).map(function(note) {
            return {kind:filter === 2 ? "missing" : "extra", note:note, index:-1,
                pitch:pitchName(note.writtenPitch === undefined ? note.pitch : note.writtenPitch, note.writtenTpc), place:filter === 2 ? position(note) : Number(note.seconds || 0).toFixed(3) + " 秒",
                detail:filter === 2 ? "谱上有这个音，演奏中尚未找到对应" : "演奏中有这个音，可在对应区选择它所指的谱音"};
        });
    }
    var rows = [];
    (result.matches || []).forEach(function(match, index) {
        var note = match.original || {}, applied = !!done[noteKey(note)], pending = !match.confirmed && match.confidence < .8;
        if (filter === 4 ? !applied : applied || filter === 1 && !pending) return;
        var proposed = values[note.id], old = note.velocity;
        rows.push({kind:"match", note:note, match:match, index:index, applied:applied, pending:pending,
            pitch:pitchName(note.writtenPitch === undefined ? match.pitch : note.writtenPitch, note.writtenTpc), place:position(note),
            original:typeof old === "number" ? old : null,
            proposed:proposed ? proposed.target : null,
            detail:applied ? "已使用演奏力度" : pending ? "请核对这两个音的对应" : kept[note.id] || "演奏 " + Number(match.seconds || 0).toFixed(3) + " 秒 · 力度 " + match.velocity});
    });
    return rows;
}
