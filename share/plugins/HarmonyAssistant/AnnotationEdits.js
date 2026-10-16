.pragma library
function positions(list) {
    if(list===undefined)return [];
    if(!Array.isArray(list)||list.length>10000)throw Error("记号位置数量过多");
    return list.map(function(p){if(!p||typeof p.tick!=="number"||p.tick<0||p.tick!==Math.floor(p.tick)||typeof p.part!=="number"||p.part<0||p.part>1023||p.part!==Math.floor(p.part)||typeof p.x!=="number"||typeof p.y!=="number"||!isFinite(p.x)||!isFinite(p.y)||Math.abs(p.x)>100||Math.abs(p.y)>100)throw Error("记号位置格式有误");return {tick:p.tick,part:p.part,x:p.x,y:p.y};});
}
function index(list){var out={};for(var i=0;i<list.length;++i)out[list[i].part+":"+list[i].tick]=list[i];return out;}
function moved(list,tick,part,x,y){var out=list.filter(function(p){return p.tick!==tick||p.part!==part;});if(x||y)out.push({tick:tick,part:part,x:x,y:y});return positions(out);}
