.pragma library
.import "../HarmonyAssistant/Harmony.js" as Harmony
.import "../HarmonyAssistant/Timeline.js" as Timeline

// Pure ES5 rules. Native pointers, score edits, playback and UI are deliberately absent.
function defaults() {
    return {schema:1,style:0,autoRefresh:true,dualPanel:true,alignment:1,
        harmony:true,counterpoint:true,playability:true,
        left:{comfort:14,maximum:15,white:16},right:{comfort:14,maximum:15,white:16},
        roles:{},ruleOverrides:{},handOverrides:[],ignored:[],chords:[],tonic:-1,minor:false,minimumHoldMs:120};
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
        frames:[],active:[],pedalNotes:[],roles:[],issues:[],previousHands:{},previousLines:{},pairState:{},regions:[],seen:{},logicalSeen:{},trackTicks:{},trackIndices:{},trackParts:{},handIndex:indexHands(data.hands||[])};
}
function indexHands(marks) {
    var tracks={},out={};
    for(var i=0;i<marks.length;++i)(tracks[marks[i].track]||(tracks[marks[i].track]=[])).push(marks[i]);
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
function issue(s,type,code,tick,notes,title,basis,severity) {
    if(tick<s.data.scope.start || tick>=s.data.scope.end)return;
    var ids=notes.map(function(n){return n.id || n.track+":"+n.tick+":"+n.index;}).sort();
    var continuous=type==="harmony" || type==="playability" && /^(span|keys|comfort|hand)-/.test(code);
    var anchor=continuous?notes.map(function(n){return n.track+":"+n.attackTick+":"+n.pitch;}).sort().join(","):tick;
    var id=type+":"+code+":"+anchor+":"+ids.join(",");
    if(s.seen[id] || s.config.ignored.indexOf(id)>=0)return;
    var meter=null;for(var m=0;m<(s.data.meters||[]).length;++m)if(s.data.meters[m].tick<=tick && tick<s.data.meters[m].end) {meter=s.data.meters[m];break;}
    s.seen[id]=true;s.issues.push({id:id,type:type,code:code,tick:tick,notes:notes,title:title,basis:basis,
        severity:severity||"提示",bar:meter?meter.measure:notes.length?notes[0].measure:0,beat:meter?1+(tick-meter.tick)/(480*4/meter.denominator):notes.length?notes[0].beat:1});
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
function roleFor(s,track) {
    var user=s.config.roles[String(track)],groups=s.byTrack[track],poly=false;
    for(var key in groups)if(groups[key].length>1){poly=true;break;}
    var bottom=Math.floor(track/4)>Math.floor((s.trackParts[track]===undefined?s.data.scope.firstTrack:s.trackParts[track])/4);
    var automatic={track:Number(track),role:bottom?"bass":"melody",hand:bottom?"left":"right",
        line:poly?(bottom?"bottom":"top"):"single",source:"自动推断",independent:!poly,uncertain:poly};
    if(user) {
        for(var field in user)automatic[field]=user[field];
        var confirmed=user.role!==undefined || user.line!==undefined || typeof user.independent==="boolean";
        automatic.source=confirmed?"用户指定":"自动推断（用户分手）";if(confirmed)automatic.uncertain=false;
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
function chordAt(s,tick) {
    var lists=[s.config.chords,s.imported];
    for(var l=0;l<lists.length;++l)for(var i=lists[l].length-1;i>=0;--i) {
        var c=lists[l][i];if(c.start<=tick && tick<c.end && (c.part===undefined || c.part===s.data.scope.firstTrack))return c;
    }
    var harmonies=s.data.harmonies||[],native=null;
    for(i=0;i<harmonies.length;++i)if(harmonies[i].tick<=tick && (!native || native.tick<=harmonies[i].tick))native=harmonies[i];
    if(native && native.rootTpc>=0) {
        var suffix=native.name.replace(/^[A-Ga-g](?:[#♯b♭]*)/,"").split("/")[0].replace(/^M/,"maj");
        for(i=0;i<Harmony.defs.length;++i)if(Harmony.defs[i].suffix===suffix)return {root:Harmony.mod12((native.rootTpc-14)*7),definition:i,source:"谱内和弦",alternatives:[]};
        return null; // Unparsed spelling is not silently replaced with a major triad.
    }
    return Timeline.atTick(s.regions,tick,s.data.scope.firstTrack);
}
function neighbours(s,n) {
    var groups=s.byTrack[n.track],ticks=s.trackTicks[n.track]||[],index=(s.trackIndices[n.track]||{})[n.attackTick],role=null;
    for(var i=0;i<s.roles.length;++i)if(s.roles[i].track===n.track)role=s.roles[i];
    var line=role?role.line:"single";
    return {previous:index>0?choose(groups[ticks[index-1]],line):null,next:index>=0 && index+1<ticks.length?choose(groups[ticks[index+1]],line):null};
}
function explained(s,n,tick,chord) {
    var pair=neighbours(s,n),a=pair.previous,b=pair.next;
    if(!b || b.attackTick-tick>480*2)return "";
    var into=a?n.pitch-a.pitch:0,out=b.pitch-n.pitch;
    if(a && Math.abs(into)<=2 && Math.abs(out)<=2 && into*out>0)return "经过音";
    if(a && a.pitch===b.pitch && Math.abs(out)<=2)return "邻音";
    if(n.attackTick<tick && Math.abs(out)<=2 && Harmony.defs[chord.definition].intervals.indexOf(Harmony.mod12(b.pitch-chord.root))>=0)return "挂留／延留的级进解决";
    return "";
}
function harmonic(s,frame) {
    if(!s.config.harmony)return;
    var tick=frame.tick,held=frame.notes.filter(function(n){return (n.soundingEnd===undefined?n.logicalEnd:n.soundingEnd)>tick;}),chord=chordAt(s,tick);
    // Bass-zone adjacent pitches are a texture suspicion, not an assertion of harmonic error.
    for(var i=0;i<held.length;++i)for(var j=i+1;j<held.length;++j) {
        var a=held[i],b=held[j],distance=Math.abs(a.pitch-b.pitch);
        if(Math.max(a.pitch,b.pitch)<60 && distance>0 && distance<=2 && Math.min(a.soundingEnd===undefined?a.logicalEnd:a.soundingEnd,b.soundingEnd===undefined?b.logicalEnd:b.soundingEnd)-tick>=240)
            issue(s,"harmony","low-cluster",tick,[a,b],"低音区持续密集冲突","间隔 "+distance+" 半音；至少持续半拍。可考虑拉开低音排列，或确认这是刻意的簇音。",s.config.style===2?"提示":"注意");
    }
    if(!chord || chord.root<0 || !Harmony.defs[chord.definition] || (chord.alternatives && chord.alternatives.length))return;
    var defined=chord.source && chord.source!=="auto",intervals=Harmony.defs[chord.definition].intervals;
    for(i=0;i<held.length;++i) {
        var n=held[i],pc=Harmony.mod12(n.pitch-chord.root);if(intervals.indexOf(pc)>=0)continue;
        // Extensions are not errors; even in a strict preset they need contextual review.
        if([2,5,9].indexOf(pc)>=0)continue;
        var explanation=explained(s,n,tick,chord);if(explanation)continue;
        if((n.soundingEnd===undefined?n.logicalEnd:n.soundingEnd)-tick<480 || n.grace)continue;
        var clash=false;for(j=0;j<intervals.length;++j)if([1,11].indexOf(Harmony.mod12(pc-intervals[j]))>=0)clash=true;
        if(clash && defined)issue(s,"harmony","chord-color",tick,[n],"持续音可能改变指定和弦色彩","依据："+(chord.source||"局部指定")+"；音 "+Harmony.spelledName(n.tpc,n.pitch,false)+" 与和弦音形成半音关系，未找到近期级进解决。请确认是否为预期变化音。","需核查");
    }
}
function ruleEnabled(config,name) {
    if(config.ruleOverrides && typeof config.ruleOverrides[name]==="boolean")return config.ruleOverrides[name];
    return !(config.style===2 && ["parallel","hidden","resolution"].indexOf(name)>=0);
}
function counterpoint(s,frame) {
    if(!s.config.counterpoint)return;
    var lines=[],tick=frame.tick;
    for(var i=0;i<s.roles.length;++i) {
        var role=s.roles[i];if(role.role==="ignore")continue;
        var pool=frame.notes.filter(function(n){return n.track===role.track && n.logicalEnd>tick;});
        var note=choose(pool,role.line);if(note)lines.push({note:note,role:role});
    }
    var present={};for(i=0;i<lines.length;++i)present[lines[i].note.track]=true;
    for(var old in s.pairState)if(!present[s.pairState[old].a.track] || !present[s.pairState[old].b.track])delete s.pairState[old];
    for(old in s.previousLines)if(!present[old])delete s.previousLines[old];
    for(i=0;i<lines.length;++i)for(var j=i+1;j<lines.length;++j) {
        var a=lines[i],b=lines[j],key=a.role.track+":"+b.role.track,prev=s.pairState[key];
        s.pairState[key]={a:a.note,b:b.note,tick:tick};
        if(!a.role.independent || !b.role.independent || a.role.uncertain || b.role.uncertain || !prev)continue;
        if(a.note.pitch===b.note.pitch && a.note.attackTick===b.note.attackTick)continue;
        var da=a.note.pitch-prev.a.pitch,db=b.note.pitch-prev.b.pitch;
        if(a.note.attackTick===prev.a.attackTick && b.note.attackTick===prev.b.attackTick)continue;
        var before=perfect(prev.a,prev.b),after=perfect(a.note,b.note);
        var evidence="前一组："+Harmony.spelledName(prev.a.tpc,prev.a.pitch,false)+(Math.floor(prev.a.pitch/12)-1)+" / "+Harmony.spelledName(prev.b.tpc,prev.b.pitch,false)+(Math.floor(prev.b.pitch/12)-1)+"；当前："+Harmony.spelledName(a.note.tpc,a.note.pitch,false)+(Math.floor(a.note.pitch/12)-1)+" / "+Harmony.spelledName(b.note.tpc,b.note.pitch,false)+(Math.floor(b.note.pitch/12)-1)+"。";
        if(ruleEnabled(s.config,"parallel") && da*db>0 && before && before===after)issue(s,"counterpoint","parallel-"+after,tick,[a.note,b.note,prev.a,prev.b],"独立声部平行"+(after===5?"五度":"八度"),evidence+"前后均为拼写明确的纯音程；两个声部同向移动。"+(a.role.source==="自动推断"?"声部来自自动推断，可在角色设置中改为加倍。":""),s.config.style===1?"需核查":"注意");
        if(ruleEnabled(s.config,"crossing") && (prev.a.pitch-prev.b.pitch)*(a.note.pitch-b.note.pitch)<0)issue(s,"counterpoint","crossing",tick,[a.note,b.note,prev.a,prev.b],"声部发生交叉",evidence+"前后上下位置互换；交叉可能是预期效果。","注意");
        else if(ruleEnabled(s.config,"overlap") && (a.note.pitch<prev.b.pitch && prev.a.pitch>prev.b.pitch || b.note.pitch>prev.a.pitch && prev.b.pitch<prev.a.pitch))
            issue(s,"counterpoint","overlap",tick,[a.note,b.note,prev.a,prev.b],"相邻位置出现声部重叠",evidence+"一个声部进入另一声部先前占用的音区。","提示");
        var outer=(a.role.role==="melody" && b.role.role==="bass") || (b.role.role==="melody" && a.role.role==="bass");
        var upper=a.note.pitch>b.note.pitch?da:db;
        if(ruleEnabled(s.config,"hidden") && outer && da*db>0 && after && before!==after && Math.abs(upper)>2)issue(s,"counterpoint","hidden",tick,[a.note,b.note,prev.a,prev.b],"外声部同向进入纯音程",evidence+"上声部跳进，同向进入纯"+(after===5?"五":"八")+"度。流行／爵士中通常只作风格提示。",s.config.style===1?"注意":"提示");
    }
    if(!lines.length) {s.pairState={};s.previousLines={};}
    for(i=0;i<lines.length;++i) {
        var line=lines[i],previous=s.previousLines[line.role.track];
        if(ruleEnabled(s.config,"leaps") && previous && previous.attackTick!==line.note.attackTick && Math.abs(previous.pitch-line.note.pitch)>12)
            issue(s,"counterpoint","leap",tick,[previous,line.note],"声部出现大跳","跳跃 "+Math.abs(previous.pitch-line.note.pitch)+" 半音；可核对后续方向及旋律意图。","提示");
        if(previous && previous.attackTick!==line.note.attackTick && s.config.tonic>=0 && ruleEnabled(s.config,"resolution") && Harmony.mod12(previous.pitch-s.config.tonic)===11 && line.note.pitch-previous.pitch!==1)
            issue(s,"counterpoint","resolution",tick,[line.note],"已确认调性下的导音去向", "用户已确认主音；导音未上行半音至主音。可能是有意离调或下行线条，请核对。",s.config.style===1?"注意":"提示");
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
    var report=PianoHands.analyse(notes,s.data.pedals||[],{tick:tick,seconds:seconds,left:s.config.left,right:s.config.right,minimumHoldMs:s.config.minimumHoldMs===undefined?120:s.config.minimumHoldMs,part:Number(part)});
    if(report.limited || report.uncertain) {
        issue(s,"playability","hand-uncertain",tick,notes,"这处按键关系需要确认",report.limited?"同时延续的按键太多，本次不自动安排松手。可先确认分手和踏板范围。":"同一个按键有相互冲突的分手或指法说明，请先确认。","提示");continue;
    }
    for(var h=0;h<report.sides.length;++h) {
        var side=report.sides[h],label=side.hand==="left"?"左手":"右手",before=side.before,after=side.after;
        if(!before.count)continue;
        var involved=side.notes.map(function(index){return notes[index];}),released=side.released.map(function(index){return notes[index];});
        var basis="按谱面一直保持时："+before.count+"个不同按键，跨度"+(before.high-before.low)+"半音。";
        basis+="检查的最短按键参考为"+(s.config.minimumHoldMs===undefined?120:s.config.minimumHoldMs)+" ms；这是可调设置，不是演奏保证。";
        basis+=report.inferred?"其中部分分手来自自动建议，可校正。":"分手由用户或符号指定。";
        if(released.length) {
            basis+="同一踏板内可提前松开 "+released.map(function(n){return Harmony.spelledName(n.tpc,n.pitch,false)+(Math.floor(n.pitch/12)-1);}).join("、")+"；之后需保持"+after.count+"键，跨度"+(after.high-after.low)+"半音。";
            issue(s,"playability","release-"+side.hand,tick,involved,label+"需要提前松开旧音",basis,"注意");
        }
        if(!after.fits) {
            basis+="松开允许释放的旧音后仍超过"+after.maximum+"半音或5个按键；新起音自身的负担不能靠裁剪旧音解决。";
            if(after.fingerConflicts)basis+="现有指法要求同一个手指同时保持不同按键，请确认指法或松手时机。";
            if(side.rolled)basis+="已标记琶音，需要另外核查展开顺序。";
            issue(s,"playability",(after.count>5?"keys-":"span-")+side.hand,tick,involved,label+"仍有按键或跨度困难",basis,report.inferred || side.rolled?"注意":"需核查");
        } else if(after.high-after.low>s.config[side.hand].comfort) {
            issue(s,"playability","comfort-"+side.hand,tick,involved,label+"超出舒适跨度",basis+"提前松手后的跨度仍超过个人舒适值；两端白键才采用白键十度上限。","提示");
        }
        if(after.crowdedBlackKeys && after.high-after.low>=12)issue(s,"playability","shape-"+side.hand,tick,involved,label+"的黑白键排列需要核对",basis+"跨度较大，同时有相邻黑白键。两端白键满足上限，也不代表内部手型一定舒适；可检查指法、换手或排列。","提示");
        if(side.attacks.length && side.travel>0) {
            var movement="最低必要手位移动"+side.travel+"半音；起音之间有"+Math.round(side.available*1000)+" ms。";
            if(side.travel>=7 && side.available<.3)issue(s,"playability","transition-"+side.hand,tick,involved,label+"转场时间较短",movement+"即使提前松手也仍需快速移动；可考虑换手或改变排列。","注意");
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
                var transition="最低必要手位移动 "+distance+" 半音；起音间隔 "+Math.round(available*1000)+" ms；严格释放后的空隙 "+Math.round(strict*1000)+" ms；当前保持 "+held.length+" 音。";
                if((distance>=7 && available<.3) || (distance>0 && held.length>0 && span>maximum))
                    issue(s,"playability","transition-"+side,tick,notes,label+"转场需要提前释放或换手",transition+(underPedal?"踏板保持可减少手指保持限制。":"无踏板时提前释放可能改变连贯听感。")+" 可考虑滚奏／换手；自动分手可校正。","注意");
                else if(distance>=12)issue(s,"playability","travel-"+side,tick,notes,label+"大幅手位移动",transition+"请结合实际速度与连续负荷核查。","提示");
                if(distance>=5 && available<.5 && previous.busy)issue(s,"playability","load-"+side,tick,notes,label+"连续转场负荷",transition+"连续两次移动时间较短，可考虑重新分手。","注意");
                s.previousHands[side]={low:low,high:high,seconds:seconds,end:Math.max.apply(null,notes.map(function(n){return n.endSeconds;})),busy:distance>=5 && available<.5};
            } else s.previousHands[side]={low:low,high:high,seconds:seconds,end:Math.max.apply(null,notes.map(function(n){return n.endSeconds;})),busy:false};
        }
    }
}
function step(s,budget) {
    var began=Date.now(),limit=budget||2,operations=0;
    do {
        if(s.phase===0) {
            if(s.index<s.data.notes.length) {
                var n=clone(s.data.notes[s.index++]);
                var tick=n.attackTick===undefined?n.tick:n.attackTick;
                var logical=n.logicalId || n.track+":"+tick+":"+n.pitch+":"+(n.attackIndex===undefined?n.index:n.attackIndex);
                if(s.logicalSeen[logical])continue;s.logicalSeen[logical]=true;
                n.id=n.id||logical;n.logicalId=logical;s.trackParts[n.track]=n.partId===undefined?s.data.scope.firstTrack:n.partId;
                n.attackTick=tick;n.logicalEnd=n.logicalEnd===undefined?n.end:n.logicalEnd;
                var group=s.byTick[tick]||(s.byTick[tick]=[]);group.push(n);
                if(n.logicalEnd<s.data.scope.end && !s.byTick[n.logicalEnd])s.byTick[n.logicalEnd]=[];
                var track=s.byTrack[n.track]||(s.byTrack[n.track]={});(track[tick]||(track[tick]=[])).push(n);
            } else {
                var boundaries=(s.data.harmonies||[]).map(function(c){return c.tick;}).concat((s.config.chords||[]).map(function(c){return c.start;}),(s.imported||[]).map(function(c){return c.start;}),(s.data.meters||[]).map(function(m){return m.tick;}));
                (s.data.pedals||[]).forEach(function(p){boundaries.push(p.tick,p.end);});
                for(var boundary=0;boundary<boundaries.length;++boundary)if(boundaries[boundary]>=0 && boundaries[boundary]<s.data.scope.end && !s.byTick[boundaries[boundary]])s.byTick[boundaries[boundary]]=[];
                s.ticks=Object.keys(s.byTick).map(Number).sort(function(a,b){return a-b;});
                for(var key in s.byTrack) {
                    s.roles.push(roleFor(s,key));var ordered=Object.keys(s.byTrack[key]).map(Number).sort(function(a,b){return a-b;});s.trackTicks[key]=ordered;s.trackIndices[key]={};
                    for(var oi=0;oi<ordered.length;++oi)s.trackIndices[key][ordered[oi]]=oi;
                }
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
                    parts:[{startTrack:s.data.scope.firstTrack,endTrack:s.data.scope.endTrack}],bar:attacks.length?attacks[0].measure:1,beat:attacks.length?attacks[0].beat:1,
                    pedalWindows:(s.data.pedals||[]).map(function(p){return {start:p.tick,end:p.end,firstTrack:p.firstTrack,endTrack:p.endTrack};})};
                s.frames.push(nativeFrame);
            } else {
                s.builder=Timeline.createBuilder(s.frames,{auto:true,pedal:true,pedalMode:0,manualKey:s.config.tonic>=0,keyTonic:Math.max(0,Harmony.rootPcs.indexOf(s.config.tonic)),keyMode:s.config.minor?1:0},s.data.scope.firstTrack);
                s.phase=2;
            }
        } else if(s.phase===2) {
            if(Timeline.step(s.builder,1)) {s.regions=s.builder.regions;s.phase=3;s.index=0;}
        } else if(s.phase===3) {
            if(s.index<s.frames.length) {var frame=s.frames[s.index++];harmonic(s,frame);counterpoint(s,frame);playability(s,frame);}
            else {s.issues.sort(function(a,b){return a.tick-b.tick || a.type.localeCompare(b.type);});s.phase=4;return true;}
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
    return "(function(){\n"+module("Harmony",sources.harmony)+module("Timeline",sources.timeline)+module("Rules",sources.rules)+"var result=Rules.run(input.data,input.config,input.imported);return {issues:result.issues,roles:result.roles};})()";
}
