const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const source=fs.readFileSync(process.argv[2]||path.join(__dirname,'Harmony.js'),'utf8').replace(/^\.pragma library\s*/, '');
const h={}; vm.createContext(h); vm.runInContext(source,h);
for (const [pitches,name] of [[[60,64,67],'C'],[[64,67,72],'C'],[[60,63,67,70],'Cm7'],[[60,62,64,67,70],'C9'],[[60,62,64,65,67,70],'C11'],[[60,62,64,67,69,70],'C13'],[[60,64,66,67,71],'Cmaj7(♯11)'],[[60,61,64,67,70],'C7(♭9)'],[[60,63,64,67,70],'C7(♯9)'],[[60,63,66,70],'Cm7♭5']]) {
 let d=h.detect(pitches); let actual=d.root<0?'?':h.sharpNames[d.root]+h.defs[d.definition].suffix;
 assert.equal(actual,name, `${pitches}: ${actual} vs ${name}`);
}
assert.equal(h.detect([60]).root,-1); assert.equal(h.detect([60,64]).root,-1);
assert.equal(h.inversion('9'),'延伸音 / 外音低音');
assert.equal(h.role('♯11'),'11'); assert.equal(h.role('♭5'),'5');
assert.equal(h.spelledName(14,60,false),'C'); assert.equal(h.spelledName(13,65,false),'F');
assert.equal(h.spelledName(12,70,false),'B♭'); assert.equal(h.spelledName(20,66,false),'F♯');
assert.equal(h.roman(9,true,0,0),'♭III');
assert.equal(h.atTick([{tick:0,end:960},{tick:960,end:1440}],480).tick,0);
assert.equal(h.atTick([{tick:0,end:480}],480),null);
console.log('Harmony checks passed');
