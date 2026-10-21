.pragma library

function anchor(issue) {
    var result=null;
    (issue.notes||[]).forEach(function(note){
        if(!result || Math.abs(note.tick-issue.tick)<Math.abs(result.tick-issue.tick))result=note;
    });
    return result;
}

function groups(issues) {
    var result=[], byTick={};
    issues.forEach(function(issue,index) {
        var key=String(issue.tick), group=byTick[key];
        if(!group) {group={tick:issue.tick,indexes:[],note:null};byTick[key]=group;result.push(group);}
        group.indexes.push(index);
        var note=anchor(issue);
        if(note && (!group.note || Math.abs(note.tick-issue.tick)<Math.abs(group.note.tick-issue.tick)))group.note=note;
    });
    return result.sort(function(a,b){return a.tick-b.tick;});
}

function filter(issues, mask, severity) {
    var bits={harmony:1,counterpoint:2,playability:4};
    return issues.filter(function(issue) {
        return !!(mask & bits[issue.type]) && (!severity || issue.severity === ["","需核查","注意","提示"][severity]);
    });
}
