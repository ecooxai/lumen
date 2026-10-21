import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';

const source=readFileSync(process.argv[2]||'src/js/prelude/07w-audio.js','utf8');
const timers=new Map();let nextTimer=1,opens=0,now=0;
class Stream { constructor(tracks=[]){this.tracks=tracks;} getAudioTracks(){return this.tracks.filter(t=>t.kind==='audio');} }
const env={EventTarget,Event,DOMException,Float32Array,Uint8Array,Math,Set,Map,console,MediaStream:Stream,
 performance:{now:()=>now},queueMicrotask:fn=>fn(),setTimeout:fn=>{const id=nextTimer++;timers.set(id,fn);return id;},clearTimeout:id=>timers.delete(id),
 N:{waOpen:()=>{opens++;return 1;},waQueued:()=>0,waPush:()=>{},waPause:()=>{},waClose:()=>{}}};
runInNewContext(source,env,{filename:'07w-audio.js'});
let checks=0;function check(label,fn){fn();checks++;console.log('PASS',label);}
function track(samples,{rate=48000,channels=1,id='a'}={}){let read=false;return{kind:'audio',id,enabled:true,muted:false,readyState:'live',getSettings:()=>({sampleRate:rate,channelCount:channels}),_readPCM:()=>{if(read)return new Float32Array();read=true;return samples;}};}
check('real MediaStream source constructor is exposed',()=>assert.equal(typeof env.MediaStreamAudioSourceNode,'function'));
check('empty and invalid streams fail explicitly',()=>{const c=new env.AudioContext();assert.throws(()=>c.createMediaStreamSource({}),{name:'TypeError'});assert.throws(()=>c.createMediaStreamSource(new Stream()),{name:'InvalidStateError'});c.close();});
check('mono captured PCM reaches an analyser without speaker feedback',()=>{
 const c=new env.AudioContext(),t=track(new Float32Array(256).fill(0.25)),s=c.createMediaStreamSource(new Stream([t])),a=c.createAnalyser();a.fftSize=128;s.connect(a);c._quantum();
 const data=new Float32Array(128);a.getFloatTimeDomainData(data);assert(data.every(x=>x===0.25));assert.equal(opens,0);c.close();
});
check('stereo PCM preserves channel separation',()=>{
 const values=new Float32Array(256);for(let i=0;i<128;i++){values[i*2]=0.2;values[i*2+1]=-0.3;}
 const c=new env.AudioContext(),s=c.createMediaStreamSource(new Stream([track(values,{channels:2})]));s.connect(c.destination);const result=c._quantum();
 assert(result[0].every(x=>Math.abs(x-0.2)<1e-6));assert(result[1].every(x=>Math.abs(x+0.3)<1e-6));c.close();
});
check('capture sample rate is converted into context sample rate',()=>{
 const c=new env.AudioContext(),samples=Float32Array.from({length:65},(_,i)=>i/100),s=c.createMediaStreamSource(new Stream([track(samples,{rate:24000})]));s.connect(c.destination);const result=c._quantum();
 assert(Math.abs(result[0][1]-0.005)<1e-6);assert(Math.abs(result[0][126]-0.63)<1e-6);c.close();
});
check('disabled capture produces silence and ended capture stops graph activity',()=>{
 const c=new env.AudioContext(),t=track(new Float32Array(512).fill(0.75)),s=c.createMediaStreamSource(new Stream([t]));s.connect(c.destination);t.enabled=false;
 const result=c._quantum();assert(result[0].every(x=>x===0));t.readyState='ended';c._quantum();assert.equal(c._active.size,0);c.close();
});
check('capture backlog is bounded instead of growing with wall time',()=>{
 const c=new env.AudioContext(),s=c.createMediaStreamSource(new Stream([track(new Float32Array(96000).fill(0.4))]));s.connect(c.destination);const result=c._quantum();
 assert(s._buffer.length<=12000);assert(result[0].every(x=>Math.abs(x-0.4)<1e-6));c.close();
});
check('first track is selected deterministically and closing graph does not stop source',()=>{
 const c=new env.AudioContext(),z=track(new Float32Array(256).fill(0.8),{id:'z'}),a=track(new Float32Array(256).fill(0.1),{id:'a'}),s=new env.MediaStreamAudioSourceNode(c,{mediaStream:new Stream([z,a])});s.connect(c.destination);
 assert(c._quantum()[0].every(x=>Math.abs(x-0.1)<1e-6));c.close();assert.equal(a.readyState,'live');assert.equal(c._active.size,0);
});
console.log(`RESULTS audio-capture-graph PASS (${checks}/${checks} checks; deterministic PCM fixture, not physical microphone evidence)`);
