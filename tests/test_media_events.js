const assert = require('node:assert/strict'), fs = require('node:fs'), vm = require('node:vm');
const {test} = require('node:test');
const source = fs.readFileSync('src/js/prelude/05-ce.js', 'utf8');
function harness() {
 const states = new Map(); let id = 0, pump;
 const N = {
  mediaPump(fn) { pump = fn; }, mediaNew() { states.set(++id, [0,true,false,false,false,false,0,NaN,0,0,'']); return id; },
  mediaState(i) { return states.get(i); }, mediaFree(i) { states.delete(i); }, mediaVolume() {}, mediaOpen() {},
  mediaSeek(i,t) { states.get(i)[6]=t; }, mediaPlay(i) { states.get(i)[1]=false; }
 };
 const ctx = {N, Event, setTimeout(){}, def(o,k,v){o[k]=v}, videoFrameStates:new WeakMap(), objectURLs:new Map(),
  MediaSource:class {}, MediaError:class {constructor(code,message){this.code=code;this.message=message}}};
 vm.runInNewContext(source.slice(0,source.indexOf("mk('HTMLMediaElement'")) + '\nglobalThis.api={attach:mediaAttach,detach:mediaDetach};', ctx);
 return {new(){const el={events:[],dispatchEvent(e){this.events.push(e.type)}};ctx.api.attach(el,'/media');return el;},
  set(el, values){Object.assign(states.get(el.__mp), values)}, poll(){return pump?.()}, armed(){return !!pump},detach:ctx.api.detach};
}
test('paused media has no periodic work but retains native notifications',()=>{
 const h=harness(), el=h.new(); assert.equal(h.poll(),false); assert.equal(h.armed(),true);
 h.set(el,{0:4,7:10,8:640,9:360}); assert.equal(h.poll(),false);
 assert.deepEqual(el.events,['durationchange','loadedmetadata','resize','loadeddata','canplay','canplaythrough']);
 el.events=[];h.poll();assert.deepEqual(el.events,[]);
 h.detach(el);assert.equal(h.armed(),false);
});
test('playing media retains time updates, buffering and completion events',()=>{
 const h=harness(),el=h.new();el.__paused=false;h.set(el,{0:4,1:false,6:1,7:10});assert.equal(h.poll(),true);
 el.events=[];h.set(el,{4:true,6:1.1});h.poll();assert.deepEqual(el.events,['waiting']);
 el.events=[];h.set(el,{4:false,6:1.3});h.poll();assert.deepEqual(el.events,['playing','timeupdate']);
 el.events=[];h.set(el,{2:true,6:10});assert.equal(h.poll(),false);assert.equal(el.__paused,true);
 assert.deepEqual(el.events.slice(-3),['timeupdate','pause','ended']);
});
test('seek completion is delivered while paused, including when native seeking already cleared',()=>{
 const h=harness(),el=h.new();h.set(el,{0:4,7:10});h.poll();el.events=[];
 el.__st.seeking=true;h.set(el,{6:2});assert.equal(h.poll(),false);
 assert.deepEqual(el.events,['timeupdate','seeked']);
});
test('errors fire once and detaching one element retains the other subscription',()=>{
 const h=harness(),a=h.new(),b=h.new();h.poll();h.set(a,{5:true,10:'network error'});h.poll();h.poll();
 assert.deepEqual(a.events,['error']);assert.equal(a.__err.code,2);
 h.detach(a);assert.equal(h.armed(),true);h.set(b,{7:12});h.poll();assert.deepEqual(b.events,['durationchange']);
 h.detach(b);assert.equal(h.armed(),false);
});
