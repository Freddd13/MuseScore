.pragma library

function groups(issues) {
    var result=[], byTick={};
    issues.forEach(function(issue,index) {
        var key=String(issue.tick), group=byTick[key];
        if(!group) {group={tick:issue.tick,indexes:[],note:null};byTick[key]=group;result.push(group);}
        group.indexes.push(index);
        (issue.notes||[]).forEach(function(note) {
            if(!group.note || Math.abs(note.tick-issue.tick)<Math.abs(group.note.tick-issue.tick))group.note=note;
        });
    });
    return result.sort(function(a,b){return a.tick-b.tick;});
}
