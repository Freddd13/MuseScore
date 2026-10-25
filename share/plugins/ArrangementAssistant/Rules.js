.pragma library
.import "../HarmonyAssistant/Harmony.js" as Harmony
.import "../HarmonyAssistant/Timeline.js" as Timeline
.import "Playback.js" as Playback

// Piano register references transcribed from https://nicechord.com/post/low-interval-limit/
// Keys are actual semitone distance, including compound intervals; values are MIDI low notes.
// Enharmonic intervals with identical sounding pitches use the same acoustic reference.
var lowIntervalLimits={"1":52,"2":51,"3":48,"4":46,"5":46,"6":46,"7":34,"8":43,"9":41,"10":41,"11":41,"13":40,"14":39,"15":36,"16":34};
var lowIntervalNames={"1":"小二度","2":"大二度","3":"小三度","4":"大三度","5":"纯四度","6":"三全音","7":"纯五度","8":"小六度","9":"大六度","10":"小七度","11":"大七度","13":"小九度","14":"大九度","15":"小十度","16":"大十度"};
function pitchLabel(n){return Harmony.spelledName(n.tpc,n.pitch,false)+(Math.floor(n.pitch/12)-1);}
function timingClock(data) {
    var points={};
    function add(tick,seconds,bpm) {
        if(typeof seconds!=="number" || !isFinite(seconds))return;
        var row={tick:tick,seconds:seconds};if(bpm>0)row.bpm=bpm;points[tick]=row;
    }
    var tempos=data.tempos||[],scope=data.scope||{},complete=tempos.some(function(t){return t.tick<=scope.start&&t.bpm>0;})&&tempos.some(function(t){return t.tick>=scope.end&&t.bpm>0;});
    if(!complete)(data.notes||[]).forEach(function(n){if(!n.grace){add(n.attackTick===undefined?n.tick:n.attackTick,n.onSeconds);add(n.logicalEnd===undefined?n.end:n.logicalEnd,n.endSeconds);}});
    tempos.forEach(function(t){add(t.tick,t.seconds,t.bpm);});
    return Object.keys(points).map(function(t){return points[t];}).sort(function(a,b){return a.tick-b.tick;});
}
function timeAt(clock,tick) {
    if(!clock.length)return null;
    var index=precedingTick(clock,tick,true),point=clock[Math.max(0,index)];
    if(point.tick===tick)return point.seconds;
    if(point.bpm>0)return point.seconds+(tick-point.tick)/(8*point.bpm);
    var other=index+1<clock.length?clock[index+1]:clock[Math.max(0,index-1)];
    return other.tick!==point.tick?point.seconds+(tick-point.tick)*(other.seconds-point.seconds)/(other.tick-point.tick):null;
}
function lowVoicing(s,frame){
    var held=frame.notes.filter(function(n){return !n.grace&&n.play!==false&&n.piano!==false&&(n.soundingEnd===undefined?n.logicalEnd:n.soundingEnd)>frame.tick;}),keys=[],byKey={},parts={};
    held.forEach(function(n){
        var part=n.partId===undefined?s.data.scope.firstTrack:n.partId,key=part+":"+n.pitch,group=byKey[key];
        if(!group){group={part:part,pitch:n.pitch,notes:[],latest:n.attackTick,begin:n.attackTick,end:0};byKey[key]=group;keys.push(group);}
        group.notes.push(n);group.latest=Math.max(group.latest,n.attackTick);group.begin=Math.min(group.begin,n.attackTick);group.end=Math.max(group.end,n.soundingEnd===undefined?n.logicalEnd:n.soundingEnd);
    });
    var limits=s.config.lowIntervalLimits||lowIntervalLimits;
    for(var i=0;i<keys.length;++i)for(var j=i+1;j<keys.length;++j){
        var a=keys[i],b=keys[j];if(a.part!==b.part)continue;
        var distance=Math.abs(a.pitch-b.pitch),limit=limits[String(distance)];
        if(limit===undefined||Math.min(a.pitch,b.pitch)>=limit)continue;
        var pair=a.pitch<b.pitch?[a,b]:[b,a];
        var from=Math.max(s.data.scope.start,pair[0].begin,pair[1].begin),until=Math.min(s.data.scope.end,pair[0].end,pair[1].end);
        if(until<=from)continue;
        var beginSeconds=timeAt(s.clock,from),endSeconds=timeAt(s.clock,until),seconds=beginSeconds===null||endSeconds===null?null:Math.max(0,endSeconds-beginSeconds);
        var residual=pair.some(function(group){return group.end>Math.max.apply(null,group.notes.map(function(n){return n.logicalEnd;}));}),arpeggio=pair[0].latest!==pair[1].latest;
        var velocities=pair.map(function(group){var values=group.notes.filter(function(n){return n.attackTick===group.latest&&typeof n.velocity==="number";}).map(function(n){return n.velocity;});return values.length?Math.max.apply(null,values):null;});
        var description=pitchLabel(pair[0].notes[0])+"–"+pitchLabel(pair[1].notes[0])+"（"+lowIntervalNames[String(distance)]+"），低端音低于参考 "+Harmony.sharpNames[limit%12]+(Math.floor(limit/12)-1)+"。";
        description+=(residual?"含踏板保持。":arpeggio?"先后起音。":"同时起音。")+"共同持续 "+(seconds===null?Number(((until-from)/480).toFixed(2))+" 四分拍":Math.round(seconds*1000)+" ms");
        if(velocities[0]!==null&&velocities[1]!==null)description+="，起音力度 "+velocities[0]+" / "+velocities[1];
        var result=parts[a.part]||(parts[a.part]={notes:[],details:[]});
        result.details.push({start:from,end:until,seconds:seconds,velocities:velocities,source:residual?"pedal":arpeggio?"arpeggio":"together",pitches:[pair[0].pitch,pair[1].pitch],description:description+"。"});
        pair.forEach(function(group){group.notes.forEach(function(n){if(result.notes.indexOf(n)<0)result.notes.push(n);});});
    }
    for(var part in parts) {
        var found=parts[part];found.details.sort(function(a,b){return (b.seconds||0)-(a.seconds||0) || Math.min.apply(null,b.velocities)-Math.min.apply(null,a.velocities);});
        var reported=issue(s,"harmony","low-cluster",frame.tick,found.notes,"低音排列可能较浑浊",found.details.map(function(detail){return detail.description;}).join("\n")+"\n持续时间按谱上时值与踏板计算。可试听这组音的清晰程度，再考虑拉开排列。参考表可在设置中调整。","提示");
        if(reported){reported.lowDetails=found.details;reported.lowKey=part+":"+found.details.map(function(detail){return detail.pitches.join("-");}).sort().join(",");}
    }
}
function playedLowVoicing(s,frame) {
    var groups={},keys=[],parts={},range=frame.range;
    frame.windows.forEach(function(window){
        if(window.incomplete||window.end<=frame.utick||window.notes.every(function(note){return note.grace;}))return;
        var key=window.part+":"+window.pitch,group=groups[key];
        if(!group){group={part:window.part,pitch:window.pitch,notes:[],start:window.start,end:window.end,startSeconds:window.startSeconds,endSeconds:window.endSeconds,latest:window.start,velocity:window.velocity,pedal:false};groups[key]=group;keys.push(group);}
        group.start=Math.min(group.start,window.start);group.end=Math.max(group.end,window.end);
        group.startSeconds=Math.min(group.startSeconds,window.startSeconds);group.endSeconds=Math.max(group.endSeconds,window.endSeconds);
        if(window.start>=group.latest){group.latest=window.start;group.velocity=window.velocity;}
        group.pedal=group.pedal||window.end>window.keyEnd;
        window.notes.forEach(function(note){if(!group.notes.some(function(n){return n.id===note.id;}))group.notes.push(note);});
    });
    var limits=s.config.lowIntervalLimits||lowIntervalLimits;
    for(var i=0;i<keys.length;++i)for(var j=i+1;j<keys.length;++j){
        var a=keys[i],b=keys[j];if(a.part!==b.part)continue;
        var distance=Math.abs(a.pitch-b.pitch),limit=limits[String(distance)];
        if(limit===undefined||Math.min(a.pitch,b.pitch)>=limit)continue;
        var pair=a.pitch<b.pitch?[a,b]:[b,a],from=Math.max(range.start,a.start,b.start),until=Math.min(range.end,a.end,b.end);
        var first=Math.max(range.startSeconds,a.startSeconds,b.startSeconds),last=Math.min(range.endSeconds,a.endSeconds,b.endSeconds);
        if(until<=from||last<=first)continue;
        var seconds=last-first,residual=a.pedal||b.pedal,arpeggio=a.latest!==b.latest;
        var description=pitchLabel(pair[0].notes[0])+"–"+pitchLabel(pair[1].notes[0])+"（"+lowIntervalNames[String(distance)]+"），低端音低于参考 "+Harmony.sharpNames[limit%12]+(Math.floor(limit/12)-1)+"。";
        description+=(residual?"含踏板保持。":arpeggio?"先后起音。":"同时起音。")+"实际共同持续 "+Math.round(seconds*1000)+" ms，起音力度 "+pair[0].velocity+" / "+pair[1].velocity;
        var found=parts[a.part]||(parts[a.part]={notes:[],details:[]});
        found.details.push({start:from-range.offset,end:until-range.offset,playbackStart:from,playbackEnd:until,seconds:seconds,
            occurrence:range.occurrence||1,velocities:[pair[0].velocity,pair[1].velocity],source:residual?"pedal":arpeggio?"arpeggio":"together",pitches:[pair[0].pitch,pair[1].pitch],description:description+"。"});
        pair.forEach(function(group){group.notes.forEach(function(note){if(!found.notes.some(function(n){return n.id===note.id;}))found.notes.push(note);});});
    }
    for(var part in parts){
        var result=parts[part];result.details.sort(function(a,b){return b.seconds-a.seconds||Math.min.apply(null,b.velocities)-Math.min.apply(null,a.velocities);});
        var instance=result.details.map(function(detail){return detail.pitches.join("-")+"@"+detail.playbackStart+"-"+detail.playbackEnd;}).sort().join(",");
        var report=issue(s,"harmony","low-cluster",frame.tick,result.notes,"低音排列可能较浑浊",result.details.map(function(detail){return detail.description;}).join("\n")+"\n持续时间来自实际起音、松键与踏板事件。可试听清晰程度，再考虑拉开排列。","提示","play:"+instance);
        if(report){report.lowDetails=result.details;report.lowKey=part+":"+result.details.map(function(detail){return detail.pitches.join("-");}).sort().join(",");report.occurrence=range.occurrence||1;report.utick=frame.utick;}
    }
}
function onsetSpans(s,frame){
    var groups={};frame.notes.forEach(function(n){if(n.attackTick!==frame.tick||n.grace||n.piano===false)return;var h=handFor(s,n,frame.tick);if(h.hand!=="left"&&h.hand!=="right")return;var k=(n.partId===undefined?0:n.partId)+":"+h.hand;(groups[k]||(groups[k]={hand:h.hand,notes:[],certain:true})).notes.push(n);groups[k].certain=groups[k].certain&&h.source.indexOf("自动推断")<0;});
    for(var key in groups){var g=groups[key],ns=g.notes,low=Math.min.apply(null,ns.map(function(n){return n.pitch;})),high=Math.max.apply(null,ns.map(function(n){return n.pitch;})),param=s.config[g.hand],maximum=white(low)&&white(high)?param.white:param.maximum;
        if(high-low>maximum)issue(s,"playability","span-"+g.hand,frame.tick,ns,(g.hand==="left"?"左手":"右手")+"这组新音跨度超过个人上限",pitchLabel(ns.filter(function(n){return n.pitch===low;})[0])+" 到 "+pitchLabel(ns.filter(function(n){return n.pitch===high;})[0])+"，跨度 "+(high-low)+" 半音，上限 "+maximum+"。"+(ns.some(function(n){return n.rolled;})?"已标记滚奏，可按展开顺序检查分手。":"这些音同时起音，可考虑调整分手或排列。"),g.certain&&!ns.some(function(n){return n.rolled;})?"需核查":"注意");
    }
}

// Pure ES5 rules. Native pointers, score edits, playback and UI are deliberately absent.
function defaults() {
    return {schema:1,style:0,autoRefresh:true,dualPanel:true,alignment:1,
        harmony:true,counterpoint:true,playability:true,lowIntervalLimits:clone(lowIntervalLimits),
        scoreHints:false,left:{comfort:14,maximum:15,white:16},right:{comfort:14,maximum:15,white:16},
        roles:{},ruleOverrides:{},handOverrides:[],ignored:[],chords:[],tonic:-1,tonicSpelling:"",minor:false,minimumHoldMs:120};
}
function clone(v) {return JSON.parse(JSON.stringify(v));}
function white(p) {return [0,2,4,5,7,9,11].indexOf(Harmony.mod12(p))>=0;}
function stepOf(n) {
    if(typeof n.tpc!=="number")return null;
    var degree=((n.tpc-14)*4%7+7)%7,natural=[0,2,4,5,7,9,11][degree],accidental=Math.floor((n.tpc+1)/7)-2;
    return Math.round((n.pitch-natural-accidental)/12)*7+degree;
}
function perfect(a,b) {
    var x=stepOf(a),y=stepOf(b);if(x===null || y===null)return 0;
    var chromatic=Harmony.mod12(Math.abs(b.pitch-a.pitch)),diatonic=Math.abs(y-x)%7;
    return chromatic===7 && diatonic===4 ? 5 : chromatic===0 && diatonic===0 && a.pitch!==b.pitch ? 8 : 0;
}
function create(data,config,imported) {
    return {data:data,config:clone(config),imported:imported||[],phase:0,index:0,byTick:{},ticks:[],byTrack:{},
        frames:[],active:[],pedalNotes:[],roles:[],issues:[],previousHands:{},previousLines:{},pairState:{},regions:[],seen:{},logicalSeen:{},trackTicks:{},trackIndices:{},trackParts:{},trackTextures:{},trackProfiles:{},trackKeys:null,roleIndex:0,textureJob:null,clock:timingClock(data),keyIndex:indexKeys(data.keys||[]),handIndex:indexHands(data.hands||[])};
}
function indexKeys(keys) {
    var result={};keys.forEach(function(event){(result[event.track]||(result[event.track]=[])).push(event);});
    for(var track in result)result[track].sort(function(a,b){return a.tick-b.tick;});
    return result;
}
function precedingTick(list,tick,events) {
    var lo=0,hi=list.length;
    while(lo<hi){var mid=(lo+hi)>>1;if((events?list[mid].tick:list[mid])<=tick)lo=mid+1;else hi=mid;}
    return lo-1;
}
function indexHands(marks) {
    var tracks={},out={};
    for(var i=0;i<marks.length;++i)if(!marks[i].grace)(tracks[marks[i].track]||(tracks[marks[i].track]=[])).push(marks[i]);
    for(var track in tracks) {
        var list=tracks[track].slice().sort(function(a,b){return a.tick-b.tick || (a.end===true||a.marker==="end"?1:0)-(b.end===true||b.marker==="end"?1:0);});
        var index={left:[],right:[],singles:{}},next={left:null,right:null},matched={};
        function single(m){(index.singles[m.tick]||(index.singles[m.tick]=[])).push(m);}
        for(i=list.length-1;i>=0;--i) {
            var m=list[i],side=m.hand==="left" || m.left===true?"left":"right";
            if(m.end===true || m.marker==="end"){next[side]={mark:m,index:i};continue;}
            if(next[side]){index[side].push({start:m,end:next[side].mark});matched[next[side].index]=true;}
            else single(m);
            next[side]=null;
        }
        for(i=0;i<list.length;++i)if((list[i].end===true || list[i].marker==="end") && !matched[i])single(list[i]);
        index.left.reverse();index.right.reverse();out[track]=index;
    }
    return out;
}
function issue(s,type,code,tick,notes,title,basis,severity,instance) {
    if(tick<s.data.scope.start || tick>=s.data.scope.end)return;
    var ids=notes.map(function(n){return n.id || n.track+":"+n.tick+":"+n.index;}).sort();
    var continuous=type==="harmony" || type==="playability" && /^(span|keys|comfort|hand|fingering)-/.test(code);
    var anchor=continuous?notes.map(function(n){return n.track+":"+n.attackTick+":"+n.pitch;}).sort().join(","):tick;
    var id=type+":"+code+":"+anchor+":"+ids.join(",");
    var previousId=id;if(instance)id+="@"+instance;
    if(s.seen[id] || s.config.ignored.indexOf(id)>=0 || instance && s.config.ignored.indexOf(previousId)>=0)return;
    var meter=null;for(var m=0;m<(s.data.meters||[]).length;++m)if(s.data.meters[m].tick<=tick && tick<s.data.meters[m].end) {meter=s.data.meters[m];break;}
    var result={id:id,type:type,code:code,tick:tick,notes:notes,title:title,basis:basis,
        severity:severity||"提示",bar:meter?meter.measure:notes.length?notes[0].measure:0,beat:meter?1+(tick-meter.tick)/(480*4/meter.denominator):notes.length?notes[0].beat:1};
    s.seen[id]=true;s.issues.push(result);return result;
}
function pedal(s,tick,track) {
    var windows=s.data.pedals||[];
    for(var i=0;i<windows.length;++i)if(windows[i].tick<=tick && tick<windows[i].end && track>=windows[i].firstTrack && track<windows[i].endTrack)return windows[i];
    return null;
}
function choose(group,line) {
    if(!group.length)return null;
    if(line==="single" && group.length!==1)return null;
    return group.reduce(function(a,b){return line==="bottom"?(a.pitch<b.pitch?a:b):(a.pitch>b.pitch?a:b);});
}
function createTexture(groups) {
    var ticks=Object.keys(groups).map(Number).sort(function(a,b){return a-b;}),pitches=[],invalid=[0],leaps=[0];
    for(var i=0;i<ticks.length;++i) {
        var group=groups[ticks[i]],pitch=group.length?group[0].pitch:0;pitches.push(pitch);invalid.push(invalid[i]+(group.length===1?0:1));
        if(i){var distance=Math.abs(pitch-pitches[i-1]);leaps.push(leaps[i-1]+(distance>=3 && distance<=12?1:0));}
    }
    return {ticks:ticks,pitches:pitches,invalid:invalid,leaps:leaps,index:0,profile:{checked:0,broken:0,onsets:{}}};
}
function stepTexture(job,count) {
    var ticks=job.ticks,profile=job.profile,limit=count||32;
    for(var checked=0;job.index+5<ticks.length && checked<limit;++job.index,++checked) {
        var i=job.index;
        if(job.invalid[i+6]!==job.invalid[i])continue;++profile.checked;
        if(job.leaps[i+5]-job.leaps[i]<4)continue;
        var pcs={},low=128,high=-1,minGap=Infinity,maxGap=0;
        for(var j=0;j<6;++j) {
            var pitch=job.pitches[i+j];pcs[Harmony.mod12(pitch)]=true;low=Math.min(low,pitch);high=Math.max(high,pitch);
            if(j) {var gap=ticks[i+j]-ticks[i+j-1];minGap=Math.min(minGap,gap);maxGap=Math.max(maxGap,gap);}
        }
        if(Object.keys(pcs).length<=4 && high-low>=7 && maxGap<=minGap*2) {
            ++profile.broken;
            var start=Math.abs(job.pitches[i]-job.pitches[i+1])<=2?1:0;
            var end=Math.abs(job.pitches[i+4]-job.pitches[i+5])<=2?5:6;
            for(j=start;j<end;++j)profile.onsets[ticks[i+j]]=true;
        }
    }
    return job.index+5>=ticks.length;
}
function textureProfile(groups) {
    var job=createTexture(groups);while(!stepTexture(job,128)){}return job.profile;
}
function brokenChordTexture(groups) {
    var profile=textureProfile(groups);return profile.checked>0 && profile.broken/profile.checked>=.5;
}
function roleFor(s,track) {
    var user=s.config.roles[String(track)],groups=s.byTrack[track],poly=false;
    for(var key in groups)if(groups[key].length>1){poly=true;break;}
    var bottom=Math.floor(track/4)>Math.floor((s.trackParts[track]===undefined?s.data.scope.firstTrack:s.trackParts[track])/4);
    var profile=(s.trackProfiles||{})[track]||textureProfile(groups);s.trackTextures[track]=profile.onsets;
    var broken=!poly && profile.checked>0 && profile.broken/profile.checked>=.5;
    var automatic={track:Number(track),role:bottom?"bass":"melody",hand:bottom?"left":"right",
        line:poly?(bottom?"bottom":"top"):"single",source:broken?"自动建议（分解和弦）":"自动推断",independent:!poly&&!broken,uncertain:poly||broken};
    if(user) {
        for(var field in user)automatic[field]=user[field];
        var confirmed=user.role!==undefined || user.line!==undefined || typeof user.independent==="boolean";
        automatic.source=confirmed?"用户指定":"自动推断（用户分手）";
        if(confirmed) {
            automatic.uncertain=false;
            if(user.independent===undefined)automatic.independent=automatic.role!=="ignore";
        }
    }
    return automatic;
}
function handFor(s,n,tick) {
    var corrected=s.config.handOverrides||[];
    for(var k=corrected.length-1;k>=0;--k) {
        var override=corrected[k];
        if(override.start<=tick && tick<override.end && n.track>=override.firstTrack && n.track<override.endTrack && (override.pitch===undefined || override.pitch===n.writtenPitch || override.pitch===n.pitch))return {hand:override.hand,source:"用户指定（所选范围）"};
    }
    var role=null;for(var i=0;i<s.roles.length;++i)if(s.roles[i].track===n.track)role=s.roles[i];
    var assigned=s.config.roles[String(n.track)];
    if(assigned && (assigned.hand==="left" || assigned.hand==="right"))return {hand:assigned.hand,source:"用户指定"};
    var candidates=[],uncertain=false,index=s.handIndex[n.track];
    function side(m) {return m.hand==="left" || m.left===true ? "left":"right";}
    function attached(m,n) {return m.index<0 || m.index===undefined || m.index===(n.attackIndex===undefined?n.index:n.attackIndex);}
    var singles=index?index.singles[n.attackTick]||[]:[];
    for(var i=0;i<singles.length;++i)if(attached(singles[i],n))candidates.push(side(singles[i]));
    for(var h=0;index && h<2;++h) {
        var ranges=index[h===0?"left":"right"],lo=0,hi=ranges.length;
        while(lo<hi){var mid=(lo+hi)>>1;if(ranges[mid].start.tick<=tick)lo=mid+1;else hi=mid;}
        if(!lo)continue;var range=ranges[lo-1],m=range.start,endpoint=range.end;if(tick>endpoint.tick)continue;
        if(n.attackTick===m.tick && !attached(m,n) || tick===endpoint.tick && !attached(endpoint,n))continue;
        var group=(s.byTrack[n.track]||{})[n.attackTick]||[];
        if(m.index>=0 && endpoint.index>=0 && group.length>1 && n.attackTick!==m.tick && tick<endpoint.tick)uncertain=true;
        else candidates.push(side(m));
    }
    var left=candidates.indexOf("left")>=0,right=candidates.indexOf("right")>=0;
    if(left && right)return {hand:"auto",source:"分手符号冲突，待确认"};
    if(left || right)return {hand:left?"left":"right",source:"分手符号"};
    if(uncertain)return {hand:"auto",source:"分手标记之间有多个同时音，请确认所指声部"};
    return {hand:role && role.hand!=="auto"?role.hand:Math.floor(n.track/4)>Math.floor(s.data.scope.firstTrack/4)?"left":"right",source:"自动推断"};
}
function partFor(s,track) {
    if(s.trackParts[track]!==undefined)return s.trackParts[track];
    var parts=s.data.parts||[];
    for(var i=0;i<parts.length;++i) {
        var start=parts[i].firstTrack===undefined?parts[i].startTrack:parts[i].firstTrack;
        if(track>=start && track<parts[i].endTrack)return start;
    }
    return s.data.scope.firstTrack;
}
function partsAt(s,tick) {
    var input=s.data.parts||[],parts=[];
    for(var i=0;i<input.length;++i) {
        var start=input[i].firstTrack===undefined?input[i].startTrack:input[i].firstTrack;
        if(start<s.data.scope.endTrack && input[i].endTrack>s.data.scope.firstTrack && !parts.some(function(p){return p.startTrack===start;}))
            parts.push({startTrack:start,endTrack:input[i].endTrack});
    }
    if(!parts.length)parts=[{startTrack:s.data.scope.firstTrack,endTrack:s.data.scope.endTrack}];
    for(i=0;i<parts.length;++i) {
        var part=parts[i],staff=Math.floor(Math.max(part.startTrack,s.data.scope.firstTrack)/4)*4,latest=-Infinity,key=0;
        var events=s.keyIndex[staff]||[],at=precedingTick(events,tick,true);
        if(at>=0){latest=events[at].tick;key=events[at].key;}
        if(latest===-Infinity)for(var track in s.byTrack)if(Math.floor(Number(track)/4)===Math.floor(staff/4)) {
            var ticks=s.trackTicks[track]||[],index=precedingTick(ticks,tick,false),group=index<0?[]:s.byTrack[track][ticks[index]];
            if(group.length && typeof group[0].key==="number" && ticks[index]>=latest){latest=ticks[index];key=group[0].key;}
        }
        part.keySignature=key;
    }
    return parts;
}
function chordAt(s,tick,part) {
    if(part===undefined)part=s.data.scope.firstTrack;
    var lists=[s.config.chords,s.imported];
    for(var l=0;l<lists.length;++l)for(var i=lists[l].length-1;i>=0;--i) {
        var c=lists[l][i];if(c.start<=tick && tick<c.end && (c.part===undefined?part===s.data.scope.firstTrack:c.part===part))return c;
    }
    var harmonies=s.data.harmonies||[],native=null;
    for(i=0;i<harmonies.length;++i)if((harmonies[i].track===undefined?part===s.data.scope.firstTrack:partFor(s,harmonies[i].track)===part) && harmonies[i].tick<=tick && (!native || native.tick<=harmonies[i].tick))native=harmonies[i];
    if(native && native.rootTpc>=0) {
        var suffix=native.name.replace(/^[A-Ga-g](?:[#♯b♭]*)/,"").split("/")[0].replace(/^M/,"maj");
        for(i=0;i<Harmony.defs.length;++i)if(Harmony.defs[i].suffix===suffix)return {root:Harmony.mod12((native.rootTpc-14)*7),definition:i,start:native.tick,source:"谱内和弦",alternatives:[]};
        return null; // Unparsed spelling is not silently replaced with a major triad.
    }
    return Timeline.atTick(s.regions,tick,part);
}
function neighbours(s,n) {
    var groups=s.byTrack[n.track],ticks=s.trackTicks[n.track]||[],index=(s.trackIndices[n.track]||{})[n.attackTick],role=null;
    for(var i=0;i<s.roles.length;++i)if(s.roles[i].track===n.track)role=s.roles[i];
    var line=role?role.line:"single";
    var selected=choose((groups||{})[n.attackTick]||[],line);
    if(!selected || selected.id!==n.id)return {previous:null,next:null};
    return {previous:index>0?choose(groups[ticks[index-1]],line):null,next:index>=0 && index+1<ticks.length?choose(groups[ticks[index+1]],line):null};
}
function chordTone(note,chord) {
    return !!(note && chord && chord.root>=0 && Harmony.defs[chord.definition] && Harmony.defs[chord.definition].intervals.indexOf(Harmony.mod12(note.pitch-chord.root))>=0);
}
function smallStep(a,b) {
    var distance=Math.abs(b.pitch-a.pitch),from=stepOf(a),to=stepOf(b);
    return distance>0 && distance<=2 && (from===null || to===null || Math.abs(to-from)<=1);
}
function explained(s,n,tick,chord) {
    var pair=neighbours(s,n),a=pair.previous,b=pair.next;
    if(!b || b.attackTick-tick>480*2 || n.logicalEnd<b.attackTick || !smallStep(n,b))return "";
    var into=a?n.pitch-a.pitch:0,out=b.pitch-n.pitch;
    if(a && a.logicalEnd>=n.attackTick && smallStep(a,n) && chordTone(a,chord) && chordTone(b,chord)) {
        if(into*out>0)return "经过音";
        if(a.pitch===b.pitch)return "邻音";
    }
    if(n.attackTick<tick && chord.start>n.attackTick && chordTone(b,chord)) {
        var preparation=chordAt(s,n.attackTick,partFor(s,n.track));
        if(!preparation || preparation.root<0 || chordTone(n,preparation))return "保持音的级进解决";
    }
    return "";
}
function harmonyEnd(s,tick,chord,part) {
    if(part===undefined)part=s.data.scope.firstTrack;
    var until=Math.min(s.data.scope.end,chord.end===undefined?s.data.scope.end:chord.end);
    var lists=[s.config.chords,s.imported,s.data.harmonies||[],s.data.harmonyRegions||[]];
    for(var list=0;list<lists.length;++list)for(var i=0;i<lists[list].length;++i) {
        var candidate=lists[list][i],at=candidate.start===undefined?candidate.tick:candidate.start;
        var owner=candidate.part===undefined?(candidate.track===undefined?s.data.scope.firstTrack:partFor(s,candidate.track)):candidate.part;
        if(at<=tick || at>=until || owner!==part)continue;
        var following=chordAt(s,at,part);
        if(!following || following.root!==chord.root || following.definition!==chord.definition)until=at;
    }
    return until;
}
function harmonic(s,frame) {
    if(!s.config.harmony)return;
    if(!s.data.playback)lowVoicing(s,frame);
    var parts=frame.parts||partsAt(s,frame.tick);
    for(var p=0;p<parts.length;++p)harmonicPart(s,frame,parts[p].startTrack);
}
function harmonicPart(s,frame,part) {
    var tick=frame.tick,held=frame.notes.filter(function(n){return partFor(s,n.track)===part && (n.soundingEnd===undefined?n.logicalEnd:n.soundingEnd)>tick;}),chord=chordAt(s,tick,part);
    var i;
    if(!chord || chord.root<0 || !Harmony.defs[chord.definition])return;
    var candidates=[chord].concat(chord.alternatives||[]);
    // Check a common finding across every complete interpretation.
    for(i=0;i<candidates.length;++i)if(candidates[i].root<0 || !Harmony.defs[candidates[i].definition])return;
    var defined=chord.source && chord.source!=="auto",until=harmonyEnd(s,tick,chord,part);
    var names=candidates.map(function(c){return (c.rootSpelling||Harmony.sharpNames[Harmony.mod12(c.root)])+Harmony.defs[c.definition].suffix;});
    for(i=0;i<held.length;++i) {
        var n=held[i];if(Math.min(until,n.soundingEnd===undefined?n.logicalEnd:n.soundingEnd)-tick<480 || n.grace)continue;
        var common=true,partners=[];
        for(var c=0;c<candidates.length && common;++c) {
            var candidate=candidates[c],intervals=Harmony.defs[candidate.definition].intervals,pc=Harmony.mod12(n.pitch-candidate.root);
            if(intervals.indexOf(pc)>=0 || [2,5,9].indexOf(pc)>=0 || explained(s,n,tick,candidate)) {common=false;break;}
            var adjacent=held.filter(function(other){return other!==n && !other.grace && (other.soundingEnd===undefined?other.logicalEnd:other.soundingEnd)-tick>=480 && intervals.indexOf(Harmony.mod12(other.pitch-candidate.root))>=0 && [1,11].indexOf(Harmony.mod12(n.pitch-other.pitch))>=0;});
            if(!adjacent.length && !(defined && intervals.some(function(interval){return [1,11].indexOf(Harmony.mod12(pc-interval))>=0;}))){common=false;break;}
            adjacent.forEach(function(other){if(partners.indexOf(other)<0)partners.push(other);});
        }
        if(!common)continue;
        var tone=pitchLabel(n),relationships=partners.map(function(other){
            var distance=Math.abs(n.pitch-other.pitch),interval=lowIntervalNames[String(distance)];
            return tone+" 和 "+pitchLabel(other)+" 相隔"+(interval?interval+"（"+distance+"半音）":distance+"半音");
        }).filter(function(text,index,list){return list.indexOf(text)===index;});
        var title=defined?"持续音可能改变指定和弦色彩":"持续音与和弦音形成半音关系";
        var basis="按 "+names.join(" / ")+" 理解，"+(relationships.length?relationships.join("；")+"，同时发声至少一拍。":tone+" 与所指定和弦的构成音有半音关系，并持续至少一拍。");
        var sourceName=chord.source==="manual"?"手动指定":chord.source==="imported"?"导入解释":chord.source;
        basis+=defined?"和弦来自"+sourceName+"。":"这是根据当前音符识别的和弦。";
        basis+="可试听这两个音的摩擦感；需要更柔和时，可试着错开起音、缩短持续音，或改变排列。若这是需要的色彩，可忽略此处。";
        issue(s,"harmony",defined?"chord-color":"auto-chord-color",tick,[n].concat(partners),title,basis,defined?"需核查":"提示");
    }
}
function ruleEnabled(config,name) {
    if(config.ruleOverrides && typeof config.ruleOverrides[name]==="boolean")return config.ruleOverrides[name];
    if(name==="oppositePerfect")return config.style===1;
    return !(config.style===2 && ["parallel","hidden","resolution"].indexOf(name)>=0);
}
function lineName(role) {
    return "谱表"+(Math.floor(role.track/4)+1)+" · 声部"+(role.track%4+1)+"（"+({melody:"旋律",bass:"低音",inner:"内声部"}[role.role]||"音线")+"）";
}
function lineEvidence(a,b,previous) {
    return lineName(a.role)+"："+(previous?pitchLabel(previous.a)+" → ":"")+pitchLabel(a.note)+"；"+
        lineName(b.role)+"："+(previous?pitchLabel(previous.b)+" → ":"")+pitchLabel(b.note)+"。";
}
function structuralLine(s,role,pool) {
    var note=choose(pool,role.line);if(!note)return null;
    var explicit=s.config.roles[String(role.track)]||{};
    var confirmed=explicit.role!==undefined || explicit.line!==undefined || typeof explicit.independent==="boolean";
    if(confirmed)return role.independent && !role.uncertain?{note:note,role:role}:null;
    if(role.role==="ignore" || role.source.indexOf("加倍")>=0 || (s.trackTextures[role.track]||{})[note.attackTick])return null;
    if(role.independent && !role.uncertain)return {note:note,role:role};
    var group=s.byTrack[role.track][note.attackTick]||[];
    if(pool.length!==1 || group.length!==1)return null;
    var local=clone(role);local.independent=true;local.uncertain=false;local.source="自动推断（当前单音线）";
    return {note:note,role:local};
}
function tonicChoice(config) {
    if(config.tonic<0)return -1;
    var choice=Harmony.rootNames.indexOf(config.tonicSpelling||"");
    if(choice<0 || Harmony.rootPcs[choice]!==config.tonic)choice=Harmony.rootPcs.indexOf(config.tonic);
    return choice;
}
function confirmedTonicTpc(config) {
    var choice=tonicChoice(config);return choice<0?null:[14,21,9,16,23,11,18,13,20,8,15,22,10,17,24,12,19][choice];
}
function counterpoint(s,frame) {
    if(!s.config.counterpoint)return;
    var lines=[],tick=frame.tick;
    for(var i=0;i<s.roles.length;++i) {
        var role=s.roles[i];if(role.role==="ignore")continue;
        var pool=frame.notes.filter(function(n){return !n.grace && n.track===role.track && n.logicalEnd>tick;});
        var selected=structuralLine(s,role,pool);if(selected)lines.push(selected);
    }
    var present={};for(i=0;i<lines.length;++i)present[lines[i].note.track]=true;
    for(var old in s.pairState)if(!present[s.pairState[old].a.track] || !present[s.pairState[old].b.track])delete s.pairState[old];
    for(old in s.previousLines)if(!present[old])delete s.previousLines[old];
    for(i=0;i<lines.length;++i)for(var j=i+1;j<lines.length;++j) {
        var a=lines[i],b=lines[j],key=a.role.track+":"+b.role.track,prev=s.pairState[key];
        s.pairState[key]={a:a.note,b:b.note,tick:tick};
        if(!a.role.independent || !b.role.independent || a.role.uncertain || b.role.uncertain)continue;
        var rank={bass:0,inner:1,melody:2},order=rank[a.role.role]-rank[b.role.role];
        var crossed=order?(a.note.pitch-b.note.pitch)*order<0:prev&&(prev.a.pitch-prev.b.pitch)*(a.note.pitch-b.note.pitch)<0;
        var alreadyCrossed=order&&prev&&(prev.a.pitch-prev.b.pitch)*order<0;
        if(ruleEnabled(s.config,"crossing") && crossed && (!alreadyCrossed || tick===s.data.scope.start))
            issue(s,"counterpoint","crossing",tick,prev?[a.note,b.note,prev.a,prev.b]:[a.note,b.note],"声部位置交叉",lineEvidence(a,b,prev)+"请核对这两条音线的上下位置和声部归属。","注意");
        if(!prev)continue;
        if(a.note.pitch===b.note.pitch && a.note.attackTick===b.note.attackTick)continue;
        var da=a.note.pitch-prev.a.pitch,db=b.note.pitch-prev.b.pitch;
        if(a.note.attackTick===prev.a.attackTick && b.note.attackTick===prev.b.attackTick)continue;
        var evidence=lineEvidence(a,b,prev);
        var before=perfect(prev.a,prev.b),after=perfect(a.note,b.note);
        if(ruleEnabled(s.config,"parallel") && da*db>0 && before && before===after)issue(s,"counterpoint","parallel-"+after,tick,[a.note,b.note,prev.a,prev.b],"独立声部平行"+(after===5?"五度":"八度"),evidence+"前后均为拼写明确的纯音程；两个声部同向移动。"+(a.role.source==="自动推断"?"声部来自自动推断，可在角色设置中改为加倍。":""),s.config.style===1?"需核查":"注意");
        if(ruleEnabled(s.config,"oppositePerfect") && da*db<0 && before && before===after)issue(s,"counterpoint","opposite-perfect-"+after,tick,[a.note,b.note,prev.a,prev.b],"独立声部反向进入纯"+(after===5?"五度":"八度"),evidence+"严格对位可进一步检查这两条线的独立性。", "提示");
        var exchanged=(prev.a.pitch-prev.b.pitch)*(a.note.pitch-b.note.pitch)<0;
        if(!exchanged && ruleEnabled(s.config,"overlap") && (prev.a.pitch>prev.b.pitch ? a.note.pitch<prev.b.pitch || b.note.pitch>prev.a.pitch : b.note.pitch<prev.a.pitch || a.note.pitch>prev.b.pitch))
            issue(s,"counterpoint","overlap",tick,[a.note,b.note,prev.a,prev.b],"相邻位置出现声部重叠",evidence+"一个声部进入另一声部先前占用的音区。","提示");
        var outer=(a.role.role==="melody" && b.role.role==="bass") || (b.role.role==="melody" && a.role.role==="bass");
        var upper=a.role.role==="melody"?da:db;
        if(ruleEnabled(s.config,"hidden") && outer && da*db>0 && after && before!==after && Math.abs(upper)>2)issue(s,"counterpoint","hidden",tick,[a.note,b.note,prev.a,prev.b],"外声部同向进入纯音程",evidence+"上声部跳进，同向进入纯"+(after===5?"五":"八")+"度。流行／爵士中通常只作风格提示。",s.config.style===1?"注意":"提示");
    }
    if(!lines.length) {s.pairState={};s.previousLines={};}
    for(i=0;i<lines.length;++i) {
        var line=lines[i],previous=s.previousLines[line.role.track];
        if(!line.role.independent || line.role.uncertain)continue;
        if(ruleEnabled(s.config,"leaps") && previous && previous.attackTick!==line.note.attackTick && Math.abs(previous.pitch-line.note.pitch)>12)
            issue(s,"counterpoint","leap",tick,[previous,line.note],"声部出现大跳","跳跃 "+Math.abs(previous.pitch-line.note.pitch)+" 半音；可核对后续方向及旋律意图。","提示");
        var tonicTpc=confirmedTonicTpc(s.config);
        if(previous && previous.attackTick!==line.note.attackTick && previous.pitch!==line.note.pitch && previous.logicalEnd>=line.note.attackTick && partFor(s,line.role.track)===s.data.scope.firstTrack && tonicTpc!==null && previous.tpc===tonicTpc+5 && ruleEnabled(s.config,"resolution") && Harmony.mod12(previous.pitch-s.config.tonic)===11 && line.note.pitch-previous.pitch!==1)
            issue(s,"counterpoint","resolution",tick,[previous,line.note],"已确认调性下的导音去向",lineName(line.role)+"："+pitchLabel(previous)+" → "+pitchLabel(line.note)+"。当前主音为"+Harmony.rootNames[tonicChoice(s.config)]+"，可核对导音去向与这条旋律的意图。",s.config.style===1?"注意":"提示");
        s.previousLines[line.role.track]=line.note;
    }
}
function physicalPlayability(s,frame) {
    if(typeof PianoHands==="undefined" || typeof PianoHands.analyse!=="function")return false;
    var tick=frame.tick,notes=[],seconds=tick/960,parts={};
    for(var i=0;i<frame.notes.length;++i) {
        var original=frame.notes[i];if(original.logicalEnd<=tick || original.grace || original.piano===false)continue;
        var assignment=handFor(s,original,tick);
        if(assignment.hand!=="left" && assignment.hand!=="right") {
            issue(s,"playability","hand-uncertain",tick,[original],"分手关系待确认",assignment.source+"；确认后再检查这几个音的按键占用。","提示");continue;
        }
        var note=clone(original);note.analysisHand=assignment.hand;note.handKnown=assignment.source.indexOf("自动推断")<0;
        note.id=note.id||note.track+":"+note.attackTick+":"+note.index;
        if(note.attackTick===tick)seconds=note.onSeconds;
        var part=note.partId===undefined?s.data.scope.firstTrack:note.partId;(parts[part]||(parts[part]=[])).push(note);
    }
    for(var part in parts) {
    notes=parts[part];
    var lookahead=[];
    for(var f=s.index;f<s.frames.length && f<s.index+32 && lookahead.length<2;++f) {
        var upcoming=s.frames[f],futureNotes=[],futureSeconds=upcoming.tick/960,attacked=false;
        for(var j=0;j<upcoming.notes.length;++j) {
            var source=upcoming.notes[j],sourcePart=source.partId===undefined?s.data.scope.firstTrack:source.partId;
            if(Number(sourcePart)!==Number(part) || source.logicalEnd<=upcoming.tick || source.grace || source.piano===false)continue;
            var hand=handFor(s,source,upcoming.tick);if(hand.hand!=="left" && hand.hand!=="right")continue;
            var next=clone(source);next.analysisHand=hand.hand;next.handKnown=hand.source.indexOf("自动推断")<0;next.id=next.id||next.track+":"+next.attackTick+":"+next.index;
            futureNotes.push(next);if(next.attackTick===upcoming.tick){attacked=true;futureSeconds=next.onSeconds;}
        }
        if(attacked)lookahead.push({tick:upcoming.tick,seconds:futureSeconds,notes:futureNotes});
    }
    var report=PianoHands.analyse(notes,s.data.pedals||[],{tick:tick,seconds:seconds,left:s.config.left,right:s.config.right,minimumHoldMs:s.config.minimumHoldMs===undefined?120:s.config.minimumHoldMs,part:Number(part),lookahead:lookahead});
    if(report.limited || report.uncertain && !Array.isArray(report.uncertainHands)) {
        onsetSpans(s,frame);
        issue(s,"playability","hand-uncertain",tick,notes,"这处按键关系需要确认",report.limited?"同时延续的按键太多，本次不自动安排松手。可先确认分手和踏板范围。":"同一个按键有相互冲突的分手或指法说明，请先确认。","提示");continue;
    }
    if(report.uncertainHands && report.uncertainHands.length)
        issue(s,"playability","hand-uncertain",tick,report.uncertainHands.map(function(index){return notes[index];}),"同一按键的分手需要确认","这几个同音音符分配给了两只手。可在“声部与左右手”中指定由哪只手弹；其余已明确分手的音继续检查。","提示");
    if(report.uncertainFingerings && report.uncertainFingerings.length)
        issue(s,"playability","fingering-uncertain",tick,report.uncertainFingerings.map(function(index){return notes[index];}),"同一按键记录了不同指法","这些音上记录了不同的手指编号。可核对或修改指法；按键数量、跨度和踏板下的松手仍按实际音高检查。","提示");
    for(var h=0;h<report.sides.length;++h) {
        var side=report.sides[h],label=side.hand==="left"?"左手":"右手",before=side.before,after=side.after;
        if(!before.count)continue;
        var involved=side.notes.map(function(index){return notes[index];}),released=side.released.map(function(index){return notes[index];});
        var basis=(side.partial?"按已明确分手的音检查，":"按谱面一直保持时：")+before.count+"个不同按键，跨度"+(before.high-before.low)+"半音。";
        basis+="检查的最短按键参考为"+(s.config.minimumHoldMs===undefined?120:s.config.minimumHoldMs)+" ms，可在设置中调整。";
        basis+=report.inferred?"其中部分分手来自自动建议，可校正。":"分手由用户或符号指定。";
        if(released.length) {
            basis+="同一踏板内可提前松开 "+released.map(function(n){return Harmony.spelledName(n.tpc,n.pitch,false)+(Math.floor(n.pitch/12)-1);}).join("、")+"；之后需保持"+after.count+"键，跨度"+(after.high-after.low)+"半音。";
            var names=released.map(function(n){return Harmony.spelledName(n.tpc,n.pitch,false)+(Math.floor(n.pitch/12)-1);});
            issue(s,"playability","release-"+side.hand,tick,involved,label+"先松开"+names.slice(0,3).join("、")+(names.length>3?"等音":"")+"，再弹下一组音",basis,"注意");
        }
        if(!after.fits) {
            var remaining=involved.filter(function(n){return released.indexOf(n)<0;}),distance=after.high-after.low;
            function keyName(pitch){var found=remaining.filter(function(n){return n.pitch===pitch;})[0];return found?pitchLabel(found):Harmony.sharpNames[Harmony.mod12(pitch)]+(Math.floor(pitch/12)-1);}
            var title;
            if(after.count>5){title=label+"需要同时保持"+after.count+"个按键";basis+="这一时刻需保持"+after.count+"个不同按键。";}
            else if(distance>after.maximum)title=label+"需同时保持 "+keyName(after.low)+"–"+keyName(after.high)+"（"+distance+"半音）";
            else title=label+"的同一手指需要同时按不同音";
            if(distance>after.maximum)basis+=keyName(after.low)+"–"+keyName(after.high)+" 相距"+distance+"半音，个人上限为"+after.maximum+"半音。可调整分手、排列或旧音的松手时机。";
            if(after.fingerConflicts)basis+="现有指法要求同一个手指同时保持不同按键，可核对指法或松手时机。";
            if(side.rolled)basis+="已标记琶音，需要另外核查展开顺序。";
            issue(s,"playability",(after.count>5?"keys-":"span-")+side.hand,tick,involved,title,basis,report.inferred || side.rolled?"注意":"需核查");
        } else if(after.high-after.low>s.config[side.hand].comfort) {
            issue(s,"playability","comfort-"+side.hand,tick,involved,label+"超出舒适跨度",basis+"提前松手后的跨度仍超过个人舒适值；两端白键才采用白键十度上限。","提示");
        }
        if(after.crowdedBlackKeys && after.high-after.low>=12)issue(s,"playability","shape-"+side.hand,tick,involved,label+"的黑白键排列需要核对",basis+"跨度较大，同时有相邻黑白键。可检查内部黑白键的指法，或尝试换手、调整排列。","提示");
        if(side.attacks.length && side.travel>0) {
            var movement="手位至少需要移动"+side.travel+"半音；两组音之间有"+Math.round(side.available*1000)+" ms。";
            if(side.travel>=7 && side.available<.3)issue(s,"playability","transition-"+side.hand,tick,involved,label+"弹下一组音的时间较短",movement+"即使提前松手也仍需快速移动；可考虑换手或改变排列。","注意");
            else if(side.travel>=12)issue(s,"playability","travel-"+side.hand,tick,involved,label+"需要较大手位移动",movement+"请结合你的实际速度检查。","提示");
        }
    }
    }
    return true;
}
function playability(s,frame) {
    if(!s.config.playability)return;
    if(physicalPlayability(s,frame))return;
    var groups={left:[],right:[]},sources={left:[],right:[]},tick=frame.tick;
    for(var i=0;i<frame.notes.length;++i) {
        var n=frame.notes[i];if(n.logicalEnd<=tick || n.grace)continue;
        var hand=handFor(s,n,tick);if(!groups[hand.hand]) {issue(s,"playability","hand-uncertain",tick,[n],"分手关系待确认",hand.source+"；可按所选音或 voice 校正，暂不据此判定跨度错误。","提示");continue;}groups[hand.hand].push(n);
        if(sources[hand.hand].indexOf(hand.source)<0)sources[hand.hand].push(hand.source);
    }
    for(var side in groups) {
        var notes=groups[side].sort(function(a,b){return a.pitch-b.pitch;});if(!notes.length)continue;
        var unique=[];for(i=0;i<notes.length;++i)if(unique.indexOf(notes[i].pitch)<0)unique.push(notes[i].pitch);
        var low=notes[0].pitch,high=notes[notes.length-1].pitch,span=high-low,parameter=s.config[side],maximum=white(low)&&white(high)?parameter.white:parameter.maximum;
        var rolled=notes.some(function(n){return n.rolled;}),held=notes.filter(function(n){return n.attackTick<tick;}),underPedal=pedal(s,tick,notes[0].track),label=side==="left"?"左手":"右手";
        var basis="跨度 "+span+" 半音；舒适 "+parameter.comfort+"，最大 "+maximum+"。分手来自："+sources[side].join("、")+"。";
        if(unique.length>5)issue(s,"playability","keys-"+side,tick,notes,label+"同时按键负担",basis+"需要 "+unique.length+" 个不同按键；可考虑换手或滚奏。",underPedal || rolled?"注意":"需核查");
        if(span>maximum)issue(s,"playability","span-"+side,tick,notes,label+"跨度超过个人上限",basis+(rolled?"已记滚奏，需要核查展开时序。":underPedal?"踏板允许提前释放旧音，可考虑换手或滚奏。":"严格保持下可考虑换手或滚奏。"),underPedal || rolled?"注意":"需核查");
        else if(span>parameter.comfort)issue(s,"playability","comfort-"+side,tick,notes,label+"超出舒适跨度",basis+"两端白键才使用白键十度上限；内侧黑键仍需核对手型。","提示");
        var attacks=notes.filter(function(n){return n.attackTick===tick;}),previous=s.previousHands[side];
        if(attacks.length) {
            var seconds=attacks[0].onSeconds;
            if(previous) {
                var distance=Math.max(0,high-maximum-previous.low,previous.high-maximum-low),available=Math.max(0,seconds-previous.seconds),strict=Math.max(0,seconds-previous.end);
                var transition="手位至少需要移动 "+distance+" 半音；起音间隔 "+Math.round(available*1000)+" ms；严格释放后的空隙 "+Math.round(strict*1000)+" ms；当前保持 "+held.length+" 音。";
                if((distance>=7 && available<.3) || (distance>0 && held.length>0 && span>maximum))
                    issue(s,"playability","transition-"+side,tick,notes,label+"弹下一组音需要提前松手或换手",transition+(underPedal?"踏板保持可减少手指保持限制。":"无踏板时提前释放可能改变连贯听感。")+" 可考虑滚奏／换手；自动分手可校正。","注意");
                else if(distance>=12)issue(s,"playability","travel-"+side,tick,notes,label+"大幅手位移动",transition+"请结合实际速度与连续负荷核查。","提示");
                if(distance>=5 && available<.5 && previous.busy)issue(s,"playability","load-"+side,tick,notes,label+"连续移动手位比较紧张",transition+"连续两次移动时间较短，可考虑重新分手。","注意");
                s.previousHands[side]={low:low,high:high,seconds:seconds,end:Math.max.apply(null,notes.map(function(n){return n.endSeconds;})),busy:distance>=5 && available<.5};
            } else s.previousHands[side]={low:low,high:high,seconds:seconds,end:Math.max.apply(null,notes.map(function(n){return n.endSeconds;})),busy:false};
        }
    }
}
function sharedHarmony(automatic,shared) {
    if(!shared || !shared.length)return automatic.slice();
    var tagged=(shared||[]).map(function(region){var edit=clone(region);edit._analysisSource=region.source||"auto";return edit;});
    return Timeline.overlay(automatic,tagged).map(function(region){
        if(region._analysisSource!==undefined){region.source=region._analysisSource;delete region._analysisSource;}
        return region;
    });
}
function step(s,budget) {
    var began=Date.now(),limit=budget||2,operations=0;
    do {
        if(s.phase===0) {
            if(s.index<s.data.notes.length) {
                var n=clone(s.data.notes[s.index++]);if(n.grace)continue;
                var tick=n.attackTick===undefined?n.tick:n.attackTick;
                var logical=n.logicalId || n.track+":"+tick+":"+n.pitch+":"+(n.attackIndex===undefined?n.index:n.attackIndex);
                if(s.logicalSeen[logical])continue;s.logicalSeen[logical]=true;
                n.id=n.id||logical;n.logicalId=logical;s.trackParts[n.track]=n.partId===undefined?s.data.scope.firstTrack:n.partId;
                n.attackTick=tick;n.logicalEnd=n.logicalEnd===undefined?n.end:n.logicalEnd;
                var group=s.byTick[tick]||(s.byTick[tick]=[]);group.push(n);
                if(n.logicalEnd<s.data.scope.end && !s.byTick[n.logicalEnd])s.byTick[n.logicalEnd]=[];
                var track=s.byTrack[n.track]||(s.byTrack[n.track]={});(track[tick]||(track[tick]=[])).push(n);
            } else if(s.trackKeys===null) {
                var boundaries=[s.data.scope.start].concat((s.data.harmonies||[]).map(function(c){return c.tick;}),(s.data.meters||[]).map(function(m){return m.tick;}),(s.data.keys||[]).map(function(k){return k.tick;}));
                [s.config.chords||[],s.imported||[],s.data.harmonyRegions||[]].forEach(function(regions){regions.forEach(function(region){boundaries.push(region.start,region.end);});});
                (s.data.pedals||[]).forEach(function(p){boundaries.push(p.tick,p.end);});
                for(var boundary=0;boundary<boundaries.length;++boundary)if(boundaries[boundary]>=0 && boundaries[boundary]<s.data.scope.end && !s.byTick[boundaries[boundary]])s.byTick[boundaries[boundary]]=[];
                s.ticks=Object.keys(s.byTick).map(Number).sort(function(a,b){return a-b;});
                s.trackKeys=Object.keys(s.byTrack);
            } else if(s.roleIndex<s.trackKeys.length) {
                var key=s.trackKeys[s.roleIndex];
                if(!s.textureJob) {
                    s.textureJob=createTexture(s.byTrack[key]);s.trackTicks[key]=s.textureJob.ticks;s.trackIndices[key]={};
                    for(var oi=0;oi<s.textureJob.ticks.length;++oi)s.trackIndices[key][s.textureJob.ticks[oi]]=oi;
                }
                if(stepTexture(s.textureJob,32)) {s.trackProfiles[key]=s.textureJob.profile;s.roles.push(roleFor(s,key));s.textureJob=null;++s.roleIndex;}
            } else {
                for(var a=0;a<s.roles.length;++a)for(var b=a+1;b<s.roles.length;++b) {
                    var first=s.roles[a],second=s.roles[b],left=s.byTrack[first.track],right=s.byTrack[second.track],ta=Object.keys(left).sort(),tb=Object.keys(right).sort();
                    function confirmed(role) {return role && (role.role!==undefined || role.line!==undefined || typeof role.independent==="boolean");}
                    var specified=confirmed(s.config.roles[String(first.track)]) || confirmed(s.config.roles[String(second.track)]);
                    if(specified || ta.length<2 || ta.join()!==tb.join())continue;
                    var difference=null,doubled=true;
                    for(var k=0;k<ta.length;++k) {
                        var na=choose(left[ta[k]],first.line),nb=choose(right[ta[k]],second.line);
                        if(!na || !nb) {doubled=false;break;}
                        var delta=na.pitch-nb.pitch;if(delta%12 || difference!==null && delta!==difference || na.logicalEnd!==nb.logicalEnd) {doubled=false;break;}difference=delta;
                    }
                    if(doubled) {second.independent=false;second.source="自动推断（加倍候选）";}
                }
                s.phase=1;s.index=0;
            }
        } else if(s.phase===1) {
            if(s.index<s.ticks.length) {
                tick=s.ticks[s.index++];var attacks=s.byTick[tick];
                s.active=s.active.filter(function(n){return n.logicalEnd>tick;}).concat(attacks);
                s.pedalNotes=s.pedalNotes.filter(function(n){var p=pedal(s,tick,n.track);return p && n.logicalEnd>p.tick && n.attackTick<p.end;});
                var pedalIds={};for(var pn=0;pn<s.pedalNotes.length;++pn)pedalIds[s.pedalNotes[pn].id || s.pedalNotes[pn].logicalId]=true;
                for(var an=0;an<s.active.length;++an) {
                    var candidate=s.active[an],p=pedal(s,tick,candidate.track),id=candidate.id||candidate.logicalId;
                    if(p && candidate.logicalEnd>p.tick && !pedalIds[id]){s.pedalNotes.push(candidate);pedalIds[id]=true;}
                }
                var pool=s.active.slice();for(var i=0;i<s.pedalNotes.length;++i)if(pool.indexOf(s.pedalNotes[i])<0)pool.push(s.pedalNotes[i]);
                var nativeFrame={tick:tick,notes:pool.map(function(n){var v=clone(n),p=pedal(s,tick,n.track);v.end=v.logicalEnd;v.soundingEnd=p && n.logicalEnd>p.tick?Math.max(n.logicalEnd,p.end):n.logicalEnd;return v;}),scoreEnd:s.data.scope.end,
                    parts:partsAt(s,tick),bar:attacks.length?attacks[0].measure:1,beat:attacks.length?attacks[0].beat:1,
                    pedalWindows:(s.data.pedals||[]).map(function(p){return {start:p.tick,end:p.end,firstTrack:p.firstTrack,endTrack:p.endTrack};})};
                s.frames.push(nativeFrame);
            } else {
                s.builder=Timeline.createBuilder(s.frames,{auto:true,pedal:true,pedalMode:0,manualKey:s.config.tonic>=0,keyTonic:Math.max(0,tonicChoice(s.config)),keyMode:s.config.minor?1:0},s.data.scope.firstTrack);
                s.phase=2;
            }
        } else if(s.phase===2) {
            if(Timeline.step(s.builder,1)) {s.regions=sharedHarmony(s.builder.regions,s.data.harmonyRegions);s.phase=3;s.index=0;}
        } else if(s.phase===3) {
            if(s.index<s.frames.length) {var frame=s.frames[s.index++];harmonic(s,frame);counterpoint(s,frame);playability(s,frame);}
            else if(s.data.playback) {
                s.playback=Playback.create(s.data.playback);
                s.playbackState={data:{scope:s.data.playback.scope,meters:s.data.playback.meters},config:s.config,seen:s.seen,issues:s.issues};
                s.phase=4;
            } else {s.issues.sort(function(a,b){return a.tick-b.tick || a.type.localeCompare(b.type);});s.phase=5;return true;}
        } else if(s.phase===4) {
            if(Playback.step(s.playback)) {s.issues.sort(function(a,b){return a.tick-b.tick || (a.utick||0)-(b.utick||0) || a.type.localeCompare(b.type);});s.phase=5;return true;}
            if(s.playback.frame && s.config.harmony)playedLowVoicing(s.playbackState,s.playback.frame);
        } else return true;
        ++operations;
    } while(operations<128 && Date.now()-began<limit);
    return false;
}
function run(data,config,imported) {var state=create(data,config,imported);while(!step(state,20)){}return state;}

// Resolve the existing modules into an isolated worker closure; no second harmony implementation.
function workerBundle(sources) {
    function module(name,source) {
        var clean=source.replace(/^\.(pragma|import).*$/gm,""),names=[],match,pattern=/^(?:function|var)\s+([A-Za-z_$][\w$]*)/gm;
        while((match=pattern.exec(clean)))if(names.indexOf(match[1])<0)names.push(match[1]);
        return "var "+name+"=(function(){\n"+clean+"\nreturn {"+names.map(function(n){return n+":"+n}).join(",")+"};})();\n";
    }
    return "(function(){\n"+module("Harmony",sources.harmony)+module("Timeline",sources.timeline)+(sources.playback?module("Playback",sources.playback):"")+module("Rules",sources.rules)+"var result=Rules.run(input.data,input.config,input.imported);return {issues:result.issues,roles:result.roles};})()";
}
