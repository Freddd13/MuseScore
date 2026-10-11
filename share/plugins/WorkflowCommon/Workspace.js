// SPDX-License-Identifier: GPL-2.0-or-later
function copy(value) {return JSON.parse(JSON.stringify(value))}
function path(url) {var s=String(url);if(s.indexOf("file:///")===0)s=decodeURIComponent(s.substr(/^file:\/\/\/[A-Za-z]:/.test(s)?8:7));return s}
function fileUrl(value) {var s=String(value).replace(/\\/g,"/");return s.indexOf("file:")===0?s:(s.charAt(0)==="/"? "file://"+s:"file:///"+s)}
function directory(url) {var p=path(url).replace(/\\/g,"/");return p.substring(0,p.lastIndexOf("/")+1)}
function resolve(workspace,value) {
    if(!value)return "";
    var p=String(value).replace(/\\/g,"/");
    return /^[A-Za-z]:\//.test(p)||p.charAt(0)==="/"?p:directory(workspace)+p;
}
function relative(workspace,value) {
    var p=path(value).replace(/\\/g,"/"),base=directory(workspace);
    return p.substr(0,base.length).toLowerCase()===base.toLowerCase()?p.substr(base.length):p;
}
function read(observer,url) {
    var value=JSON.parse(observer.readTextFile(url));
    if(value.schema!==1)throw new Error("不支持的工作区版本");
    return value;
}
function writeModule(observer,url,name,data,score) {
    var value={schema:1,score:score,reference:null,expression:null,variants:null};
    var existing=observer.readTextFile(url);
    if(existing.length){value=JSON.parse(existing);if(value.schema!==1)throw new Error("不支持的工作区版本");}
    value[name]=copy(data);value.updated=new Date().toISOString();
    if(!observer.writeTextFile(url,JSON.stringify(value,null,2)))throw new Error("无法保存工作区，旧文件保留");
    return value;
}
function scoreIdentity(score) {return score?{name:score.scoreName,measures:score.nmeasures,staves:score.nstaves}:null}
function validateAnchors(anchors,start,end,from,until) {
    var a=anchors.slice().sort(function(x,y){return x.tick-y.tick});
    var tick=start,second=from;
    for(var i=0;i<a.length;++i){if(a[i].tick<=tick||a[i].tick>=end||a[i].ms<=second||a[i].ms>=until)return false;tick=a[i].tick;second=a[i].ms;}
    return true;
}
function mapped(association,tick) {
    var points=[{tick:association.start,ms:association.from}].concat(association.anchors||[]).concat([{tick:association.end,ms:association.until}]);
    for(var i=1;i<points.length;++i)if(tick<=points[i].tick){var a=points[i-1],b=points[i];return a.ms+(b.ms-a.ms)*(tick-a.tick)/(b.tick-a.tick);}
    return association.until;
}
