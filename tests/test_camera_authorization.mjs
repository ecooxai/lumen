import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';

// Real getUserMedia code with an explicitly controlled native-camera bridge.
// This verifies asynchronous state/lifetime semantics, not physical capture.
const source=readFileSync(process.argv[2]||'src/js/prelude/08-window.js','utf8');
const start=source.indexOf('/* getUserMedia (real camera/mic)');
const end=source.indexOf('    class BlobEvent',start);
assert(start>=0 && end>start,'capture block must exist');
const code=source.slice(start,end)+'\n})();';
function fixture(){
 const state={now:0,status:0,opened:0,closed:0,tracks:0,microphones:0,allowMic:true,allowCam:true};
 const timers=new Map();let next=1;
 class Track{constructor(token,kind,label){this._={};this.kind=kind;this.label=label;this.readyState='live';state.tracks++;}stop(){if(this._.cam){state.closed++;this._.cam=0;}if(this._.mic){state.microphones--;this._.mic=0;}this.readyState='ended';}}
 class Stream{constructor(tracks){this.tracks=tracks;}getTracks(){return this.tracks;}getVideoTracks(){return this.tracks.filter(t=>t.kind==='video');}}
 const env={EventTarget,Event,DOMException,Promise,Date:{now:()=>state.now},MediaStreamTrack:Track,MediaStream:Stream,RTC_INTERNAL:Symbol(),localWindowId:1,
  location:{origin:'https://camera.example'},navigator:{mediaDevices:{},permissions:{}},
  setTimeout:(fn,delay=0)=>{const id=next++;timers.set(id,{fn,due:state.now+delay});return id;},clearTimeout:id=>timers.delete(id),
  N:{capState:()=>1,capAllowed:()=>true,capAsk:kind=>kind==='microphone'?state.allowMic:state.allowCam,capCameras:()=>['test camera'],capMics:()=>['test microphone'],
    capCamOpen:()=>{state.opened++;return 1;},capCamStatus:()=>state.status,capCamClose:()=>state.closed++,
    capMicOpen:()=>{state.microphones++;return 1;},capMicClose:()=>state.microphones--}
 };
 runInNewContext(code,env,{filename:'capture-prelude.js'});
 async function advance(ms=0){
  const end=state.now+ms;let ticks=0;
  while(true){const next=[...timers].filter(([,t])=>t.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;
   if(++ticks>1000)throw new Error('unbounded timer loop');state.now=next[1].due;timers.delete(next[0]);next[1].fn();await Promise.resolve();await Promise.resolve();
  }
  state.now=end;await Promise.resolve();await Promise.resolve();
 }
 const request=constraints=>{const r={settled:false,stream:null,error:null};env.navigator.mediaDevices.getUserMedia(constraints).then(s=>{r.settled=true;r.stream=s;},e=>{r.settled=true;r.error=e;});return r;};
 return {state,timers,advance,request};
}
let count=0,failures=0;
async function check(label,fn){count++;try{await fn();console.log('PASS',label);}catch(error){failures++;console.error('FAIL',label,error.message);}}
await check('pending OS authorization does not create or resolve a live track after two seconds',async()=>{
 const f=fixture(),r=f.request({video:true});await f.advance(3000);assert.equal(r.settled,false);assert.equal(f.state.tracks,0);assert.equal(f.state.opened,1);assert.equal(f.timers.size,1);
 f.state.status=-1;await f.advance(100);assert.equal(r.error.name,'NotAllowedError');assert.equal(f.state.closed,1);
});
await check('time spent awaiting OS permission does not exhaust frame startup budget',async()=>{
 const f=fixture(),r=f.request({video:true});await f.advance(15000);assert.equal(r.settled,false);
 f.state.status=1;await f.advance(9000);assert.equal(r.settled,false);f.state.status=2;await f.advance(100);
 assert.equal(r.error,null);assert.equal(r.stream.getVideoTracks().length,1);r.stream.getTracks()[0].stop();assert.equal(f.timers.size,0);
});
await check('authorization alone waits for the first captured frame',async()=>{
 const f=fixture(),r=f.request({video:true});await f.advance();f.state.status=1;await f.advance(500);assert.equal(r.settled,false);assert.equal(f.state.tracks,0);
 f.state.status=2;await f.advance(100);assert.equal(r.error,null);assert.equal(r.stream.getVideoTracks().length,1);assert.equal(f.timers.size,0);r.stream.getTracks()[0].stop();assert.equal(f.state.closed,1);
});
await check('delayed permission denial rejects and releases native camera without track',async()=>{
 const f=fixture(),r=f.request({video:true});await f.advance(5000);f.state.status=-1;await f.advance(100);
 assert.equal(r.error.name,'NotAllowedError');assert.equal(f.state.tracks,0);assert.equal(f.state.closed,1);assert.equal(f.timers.size,0);
});
await check('authorized camera without frames has bounded startup and releases device',async()=>{
 const f=fixture();f.state.status=1;const r=f.request({video:true});await f.advance(11000);
 assert.equal(r.error.name,'NotReadableError');assert.equal(f.state.tracks,0);assert.equal(f.state.closed,1);assert.equal(f.timers.size,0);
});
await check('removed camera rejects instead of leaving a perpetual pending capture',async()=>{
 const f=fixture(),r=f.request({video:true});await f.advance(1000);f.state.status=-2;await f.advance(100);
 assert.equal(r.error.name,'NotReadableError');assert.equal(f.state.closed,1);assert.equal(f.timers.size,0);
});
await check('microphone failure after camera startup releases the ready video track',async()=>{
 const f=fixture();f.state.status=2;f.state.allowMic=false;const r=f.request({video:true,audio:true});await f.advance();
 assert.equal(r.error.name,'NotAllowedError');assert.equal(f.state.closed,1);assert.equal(f.state.microphones,0);assert.equal(f.timers.size,0);
});
console.log(`RESULTS camera-authorization ${failures?'FAIL':'PASS'} (${count-failures}/${count}; controlled native bridge, physical camera untested)`);
process.exitCode=failures?1:0;
