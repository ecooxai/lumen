const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const {test}=require('node:test');
function harness(){
 const source=fs.readFileSync('src/js/prelude/08-window.js','utf8');
 let pump=null,reads=0,requests=0;const errors=[];
 class Rect{constructor(x=0,y=0,width=0,height=0){Object.assign(this,{x,y,width,height,left:x,top:y,right:x+width,bottom:y+height})}}
 const ctx={N:{intersectionPump(fn){pump=fn;if(fn)requests++},viewport:()=>[400,600],now:()=>10},DOMRect:Rect,report:e=>errors.push(e),setTimeout(){throw Error('observer must not poll')},setInterval(){throw Error('observer must not poll')}};
 vm.runInNewContext(source.slice(0,source.indexOf('class PerformanceObserver'))+'\nthis.IO=IntersectionObserver',ctx);
 return {IO:ctx.IO,el:()=>({isConnected:true,y:10,getBoundingClientRect(){reads++;return new Rect(10,this.y,20,20)}}),tick(){if(pump)pump()},armed:()=>!!pump,reads:()=>reads,requests:()=>requests,errors};
}
test('observe requests asynchronous native delivery with no repeating timer',()=>{
 const h=harness(),el=h.el();let calls=0;const io=new h.IO((entries,self)=>{calls++;assert.equal(self,io);assert.equal(entries[0].isIntersecting,true)});
 io.observe(el);assert.equal(calls,0);assert.equal(h.reads(),0);assert.equal(h.requests(),1);io.observe(el);assert.equal(h.requests(),1);
 h.tick();assert.equal(calls,1);assert.equal(h.reads(),1);assert.equal(h.requests(),1);io.disconnect();assert.equal(h.armed(),false);
});
test('geometry changes deliver visibility transitions and detachment',()=>{
 const h=harness(),el=h.el(),seen=[];const io=new h.IO(es=>seen.push(es[0].isIntersecting));io.observe(el);h.tick();
 el.y=100;h.tick();assert.deepEqual(seen,[true]);el.y=800;h.tick();assert.deepEqual(seen,[true,false]);
 el.y=10;h.tick();el.isConnected=false;h.tick();assert.deepEqual(seen,[true,false,true,false]);
 io.unobserve(el);assert.equal(h.armed(),false);el.isConnected=true;io.observe(el);h.tick();assert.deepEqual(seen,[true,false,true,false,true]);
});
test('unobserve last target releases observer immediately',()=>{
 const h=harness(),a=h.el(),b=h.el(),first=new h.IO(()=>{}),second=new h.IO(()=>{});
 first.observe(a);second.observe(b);first.unobserve(a);assert.equal(h.armed(),true);second.unobserve(b);assert.equal(h.armed(),false);assert.equal(h.reads(),0);
});
test('callbacks can cancel another observer and register a future observation',()=>{
 const h=harness(),a=h.el(),b=h.el();let first=0,second=0,third=0;
 const later=new h.IO(()=>third++),cancelled=new h.IO(()=>second++),io=new h.IO(()=>{first++;cancelled.disconnect();later.observe(b)});
 io.observe(a);cancelled.observe(a);h.tick();assert.equal(first,1);assert.equal(second,0);assert.equal(third,0);h.tick();assert.equal(third,1);
 io.disconnect();later.disconnect();assert.equal(h.armed(),false);
});
test('exceptions leave later observers and future deliveries active',()=>{
 const h=harness(),el=h.el();let calls=0;const bad=new h.IO(()=>{throw Error('expected')}),good=new h.IO(()=>calls++);
 bad.observe(el);good.observe(el);h.tick();assert.equal(calls,1);assert.equal(h.errors.length,1);el.y=800;h.tick();assert.equal(calls,2);assert.equal(h.errors.length,2);
 bad.disconnect();good.disconnect();assert.equal(h.armed(),false);
});
