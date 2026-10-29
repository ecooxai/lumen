const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const {test}=require('node:test');
function harness(){
 const source=fs.readFileSync('src/js/prelude/08-window.js','utf8');
 let pump=null,reads=0;const errors=[];
 const ctx={N:{resizePump(fn){pump=fn},rect(el){reads++;return [0,0,el.w,el.h]}},DOMRect:class{constructor(x,y,width,height){Object.assign(this,{x,y,width,height})}},report:e=>errors.push(e),setTimeout(){throw Error('observer must not poll')},setInterval(){throw Error('observer must not poll')}};
 vm.runInNewContext(source.slice(0,source.indexOf('class PerformanceObserver'))+'\nthis.RO=ResizeObserver',ctx);
 return {RO:ctx.RO,el:()=>({w:100,h:40}),tick(){if(pump)pump()},armed:()=>!!pump,reads:()=>reads,errors};
}
test('initial observation is asynchronous and idle has no polling',()=>{
 const h=harness(),el=h.el();let calls=0;const ro=new h.RO((entries,self)=>{calls++;assert.equal(self,ro);assert.equal(entries[0].contentRect.width,100)});
 ro.observe(el);assert.equal(calls,0);assert.equal(h.reads(),0);h.tick();assert.equal(calls,1);assert.equal(h.reads(),1);
 ro.observe(el);assert.equal(h.reads(),1);ro.disconnect();assert.equal(h.armed(),false);
});
test('completed layouts report changed sizes, not unchanged geometry',()=>{
 const h=harness(),a=h.el(),b=h.el();const batches=[];const ro=new h.RO(e=>batches.push(e));ro.observe(a);ro.observe(b);h.tick();assert.equal(batches[0].length,2);
 h.tick();assert.equal(batches.length,1);a.w=140;h.tick();assert.equal(batches.length,2);assert.equal(batches[1].length,1);assert.equal(batches[1][0].target,a);
 ro.unobserve(a);a.w=190;h.tick();assert.equal(batches.length,2);ro.unobserve(b);assert.equal(h.armed(),false);
});
test('callbacks can cancel later observers and register another delivery',()=>{
 const h=harness(),a=h.el(),b=h.el();let first=0,second=0,third=0;
 const r3=new h.RO(()=>third++),r2=new h.RO(()=>second++),r1=new h.RO(()=>{first++;r2.disconnect();r3.observe(b)});
 r1.observe(a);r2.observe(a);h.tick();assert.equal(first,1);assert.equal(second,0);assert.equal(third,0);h.tick();assert.equal(third,1);
 r1.disconnect();r3.disconnect();assert.equal(h.armed(),false);
});
test('callback exceptions do not lose other observers or future size changes',()=>{
 const h=harness(),a=h.el();let calls=0;const bad=new h.RO(()=>{throw Error('expected')}),good=new h.RO(()=>calls++);
 bad.observe(a);good.observe(a);h.tick();assert.equal(calls,1);assert.equal(h.errors.length,1);a.h++;h.tick();assert.equal(calls,2);assert.equal(h.errors.length,2);
 bad.disconnect();good.disconnect();good.observe(a);h.tick();assert.equal(calls,3);
});
