// SPDX-License-Identifier: GPL-2.0-or-later
// Value-only workspace comparisons. The original score is never an editing target.
function copy(v){return JSON.parse(JSON.stringify(v))}
function compare(base,current,scope){
 var changes=[],same=base.staves===current.staves&&base.measures.length===current.measures.length;
 if(base.context!==current.context){same=false;changes.push("全谱乐器／样式配置改变")}
 if(base.staves!==current.staves)changes.push("谱表数量改变");
 if(base.measures.length!==current.measures.length)changes.push("小节数量改变");
 var outside=0,inside=0;
 for(var i=0;i<Math.min(base.measures.length,current.measures.length);++i){
  var a=base.measures[i],b=current.measures[i];
  if(a.tick!==b.tick||a.end!==b.end||a.signature!==b.signature){same=false;changes.push("第"+(i+1)+"小节长度／拍号改变")}
  if(a.hash!==b.hash){if(a.end<=scope.start||a.tick>=scope.end)++outside;else ++inside;}
 }
 if(outside){same=false;changes.push("段外 "+outside+" 小节有改动")}
 return {same:same,outside:outside,inside:inside,reasons:changes};
}
function newEntry(label,path,baseline,hash,revision){return {id:String(Date.now())+"-"+Math.floor(Math.random()*1000000),name:label,path:path,baseline:!!baseline,hash:hash||"",savedRevision:revision||"",aliases:[],comparison:null}}
