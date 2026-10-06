.pragma library

// Exchange data has numeric anchors, no native wrappers or serialized score mutations.
function document(frames, fingerprint, config, regions, overrides) {
    return {format:"HarmonyAssistant.analysis",schema:2,ticksPerQuarter:480,
        fingerprint:fingerprint,configuration:config,frames:frames,regions:regions||[],overrides:overrides||[]};
}
function validate(source) {
    if(!source || source.format!=="HarmonyAssistant.analysis" || (source.schema!==1 && source.schema!==2) || source.ticksPerQuarter!==480)
        throw Error("不支持的分析格式 / 时间单位");
    if(typeof source.fingerprint!=="string" || !/^[0-9a-f]{64}$/.test(source.fingerprint))throw Error("缺少乐谱指纹");
    if(!Array.isArray(source.frames) || source.frames.length>100000)throw Error("分析帧数量无效");
    var last=-1, frames=[];
    function integer(value,min,max){return typeof value==="number" && Math.floor(value)===value && value>=min && value<=max;}
    for(var i=0;i<source.frames.length;++i) {
        var f=source.frames[i];
        if(!f || !integer(f.tick,0,2147483647) || f.tick<=last || !integer(f.root,-1,11) || !integer(f.definition,-1,28) ||
            ((f.root<0)!==(f.definition<0)) || !Array.isArray(f.notes) || f.notes.length>512)throw Error("分析帧无效："+i);
        var notes=[];
        for(var n=0;n<f.notes.length;++n) {
            var d=f.notes[n];
            if(!d || !integer(d.tick,0,f.tick) || !integer(d.track,0,1023) || !integer(d.index,0,127) ||
                !integer(d.pitch,0,127) || !integer(d.tpc,-1,33)) throw Error("音符定位无效："+i);
            var note={tick:d.tick,track:d.track,index:d.index,pitch:d.pitch,tpc:d.tpc};
            if(integer(d.writtenPitch,0,127))note.writtenPitch=d.writtenPitch;
            if(integer(d.writtenTpc,-1,33))note.writtenTpc=d.writtenTpc;
            if(integer(d.end,d.tick+1,2147483647))note.end=d.end;
            if(integer(d.attackTick,0,d.tick))note.attackTick=d.attackTick;
            notes.push(note);
        }
        frames.push({tick:f.tick,bar:integer(f.bar,1,1000000)?f.bar:1,beat:integer(f.beat,1,128)?f.beat:1,
            root:f.root,definition:f.definition,chord:typeof f.chord==="string"?f.chord.slice(0,64):"",
            degree:typeof f.degree==="string"?f.degree.slice(0,24):"",notes:notes});
        last=f.tick;
    }
    return {frames:frames,fingerprint:source.fingerprint,regions:source.schema===2?source.regions||[]:[],overrides:source.schema===2?source.overrides||[]:[]};
}
function atTick(frames, tick) {
    var lo=0,hi=frames.length;
    while(lo<hi){var mid=(lo+hi)>>1;if(frames[mid].tick<=tick)lo=mid+1;else hi=mid;}
    return lo ? frames[lo-1] : null;
}
function csv(frames) {
    function quote(value){return '"'+String(value).replace(/"/g,'""')+'"';}
    var rows=["tick,bar,beat,chord,degree,pitches"];
    for(var i=0;i<frames.length;++i){var f=frames[i];rows.push([f.tick,f.bar,f.beat,quote(f.chord),quote(f.degree),quote(f.notes.map(function(n){return n.pitch;}).join(" "))].join(","));}
    return "\uFEFF"+rows.join("\r\n")+"\r\n";
}
function csvRegions(regions) {
    function quote(value){return '"'+String(value).replace(/"/g,'""')+'"';}
    var rows=["startTick,endTick,part,chord,degree,root,bass,source"];
    for(var i=0;i<regions.length;++i){var r=regions[i];rows.push([r.start,r.end,r.part,quote(r.chord),quote(r.degree),r.root,r.bass,quote(r.source)].join(","));}
    return "\uFEFF"+rows.join("\r\n")+"\r\n";
}
