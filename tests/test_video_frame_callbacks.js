const assert=require('node:assert/strict'), fs=require('node:fs'), vm=require('node:vm');
const {test}=require('node:test');
const source=fs.readFileSync('src/js/prelude/05-ce.js','utf8');
function harness() {
 const frames=new Map(), errors=[];let pump=null, proto;
 const ctx={H:{HTMLMediaElement:{}}, reflectUrl(){},reflectInt(){},
  methods(p,m){Object.defineProperties(p,Object.getOwnPropertyDescriptors(m));},
  mk(name,parent,tags,fn){proto={};fn(proto);},report(e){errors.push(e)},
  N:{videoFramePump(fn){pump=fn},videoFrameInfo(el,last){if(!el.video)throw TypeError('Not a video');const f=frames.get(el);return f&&f[0]!==last?f:null},videoQuality(){return [9,2]},now(){return 123}}
 };
 vm.runInNewContext(source.slice(source.indexOf('// A pending callback'),source.indexOf("mk('HTMLAudioElement'")),ctx);
 return {el:()=>Object.assign(Object.create(proto),{video:true}),frame(el,id){frames.set(el,[id,id/30,100,640,360,id])}, tick(){if(pump)pump(105)},armed:()=>!!pump,errors};
}
test('callbacks wait for real frames, supply metadata, and fire once per frame',()=>{
 const h=harness(),v=h.el();let n=0;
 v.requestVideoFrameCallback((now,m)=>{n++;assert.equal(now,105);assert.equal(m.mediaTime,1/30);assert.equal(m.width,640);assert.equal(m.height,360);assert.equal(m.presentedFrames,1);assert.equal(m.presentationTime,100);assert.equal(m.expectedDisplayTime,105)});
 h.tick();assert.equal(n,0);h.frame(v,1);h.tick();assert.equal(n,1);assert.equal(h.armed(),false);
 v.requestVideoFrameCallback(()=>n++);h.tick();assert.equal(n,1);h.frame(v,2);h.tick();assert.equal(n,2);assert.equal(h.errors.length,0);
});
test('cancel works before dispatch and from a callback in the same frame',()=>{
 const h=harness(),v=h.el();let n=0;
 const before=v.requestVideoFrameCallback(()=>n++);v.cancelVideoFrameCallback(before);assert.equal(h.armed(),false);
 let later;v.requestVideoFrameCallback(()=>{n++;v.cancelVideoFrameCallback(later)});later=v.requestVideoFrameCallback(()=>n++);
 h.frame(v,1);h.tick();assert.equal(n,1);assert.equal(h.armed(),false);
});
test('reentrant callbacks wait for the next frame; exceptions do not drop others',()=>{
 const h=harness(),v=h.el();let n=0;
 v.requestVideoFrameCallback(()=>{n++;v.requestVideoFrameCallback(()=>n++);throw Error('expected')});
 v.requestVideoFrameCallback(()=>n++);h.frame(v,1);h.tick();assert.equal(n,2);assert.equal(h.errors.length,1);
 h.tick();assert.equal(n,2);h.frame(v,2);h.tick();assert.equal(n,3);assert.equal(h.armed(),false);
});
test('cancellation is per element and playback quality is a readonly snapshot',()=>{
 const h=harness(),a=h.el(),b=h.el();let n=0;
 const id=a.requestVideoFrameCallback(()=>n++);b.cancelVideoFrameCallback(id);h.frame(a,1);h.tick();assert.equal(n,1);
 assert.throws(()=>a.requestVideoFrameCallback(null),/Callback/);
 assert.throws(()=>a.requestVideoFrameCallback.call({},()=>{}),/Not a video/);
 const q=a.getVideoPlaybackQuality();assert.equal(q.totalVideoFrames,9);assert.equal(q.droppedVideoFrames,2);assert.equal(q.creationTime,123);
 assert.equal(Reflect.set(q,'totalVideoFrames',0),false);assert.notEqual(q,a.getVideoPlaybackQuality());
});

test('registration after a presentation gap waits for a new frame',()=>{
 const h=harness(),v=h.el();let n=0;
 h.frame(v,1);v.requestVideoFrameCallback(()=>n++);h.tick();assert.equal(n,0);
 h.frame(v,2);h.tick();assert.equal(n,1);
 h.frame(v,3);v.requestVideoFrameCallback(()=>n++);h.tick();assert.equal(n,1);
 h.frame(v,4);h.tick();assert.equal(n,2);
});
