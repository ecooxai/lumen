const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const source=fs.readFileSync(path.join(__dirname,'public-video-gesture-probe.js'),'utf8');
let checks=0;
function check(name,fn){fn();checks++;console.log('PASS '+name);}
function fixture(){
  let now=0,tick,quality={totalVideoFrames:0,droppedVideoFrames:0},forbidden=0,covered=false,ad=false;
  const mediaListeners=new Map(),documentListeners=new Map(),windowListeners=new Map();
  function listener(map,name,fn){if(!map.has(name))map.set(name,new Set());map.get(name).add(fn);}
  function emit(map,name,event){for(const fn of [...(map.get(name)||[])])fn({type:name,...event});}
  const video={paused:true,muted:false,readyState:4,videoWidth:854,videoHeight:480,currentTime:0,currentSrc:'blob:fixture-only',
    getVideoPlaybackQuality(){return {...quality};},play(){forbidden++;throw new Error('play() forbidden');},
    pause(){this.paused=true;emit(mediaListeners,'pause',{isTrusted:true});},
    addEventListener(n,f){listener(mediaListeners,n,f);},removeEventListener(n,f){mediaListeners.get(n)?.delete(f);}};
  const player={classList:{contains(n){return n==='ad-showing'&&ad;}},querySelector(){return button;},parentElement:null};
  const button={tagName:'BUTTON',disabled:false,parentElement:player,getAttribute(){return 'Play';},
    getBoundingClientRect(){return {left:100,top:100,width:80,height:80};},contains(n){return n===this;}};
  const document={hidden:false,querySelector(n){return n==='video'?video:null;},getElementById(){return player;},
    elementFromPoint(){return covered?{}:button;},
    addEventListener(n,f){listener(documentListeners,n,f);},removeEventListener(n,f){documentListeners.get(n)?.delete(f);}};
  const context={document,location:{hostname:'www.youtube.com',pathname:'/watch',search:'?v=YE7VzlLtp-4'},URLSearchParams,
    Date:{now(){return now++;}},Number,Object,String,JSON,innerWidth:1024,innerHeight:724,devicePixelRatio:1,
    setInterval(fn){tick=fn;return 1;},clearInterval(){tick=null;},
    getComputedStyle(){return {display:'block',visibility:'visible',opacity:'1'};},
    addEventListener(n,f){listener(windowListeners,n,f);},removeEventListener(n,f){windowListeners.get(n)?.delete(f);}};
  context.window=context;context.top=context;vm.runInNewContext(source,context);
  return {api:context.LumePublicVideoGesture,video,button,document,mediaListeners,
    covered(value){covered=value;},ad(value){ad=value;},forbidden(){return forbidden;},
    click(trusted=true){emit(documentListeners,'click',{target:button,isTrusted:trusted});},
    playing(trusted=true){video.paused=false;emit(mediaListeners,'playing',{isTrusted:trusted});},
    advance(seconds,total,dropped=0){now+=seconds*1000;video.currentTime+=seconds;quality={totalVideoFrames:total,droppedVideoFrames:dropped};tick?.();},
    event(name){emit(mediaListeners,name,{isTrusted:true});}};
}
check('observer never calls play() and enforces/readbacks muted',()=>{const f=fixture();for(let i=0;i<3;i++)f.api.snapshot();assert.equal(f.video.muted,true);assert.equal(f.forbidden(),0);assert.equal(f.api.snapshot().programmaticPlayCalled,false);});
check('covered and ad controls are refused before arming',()=>{const f=fixture();f.covered(true);assert.equal(f.api.arm().eligible,false);f.covered(false);f.ad(true);assert.equal(f.api.target().eligible,false);assert.equal(f.api.snapshot().clickArmed,false);});
check('one trusted control click with actual progress reaches unchanged thresholds',()=>{const f=fixture();assert.equal(f.api.arm().eligible,true);assert.equal(f.api.arm().eligible,false);f.click();f.playing();for(let i=1;i<=3;i++)f.advance(1,i*10);const r=f.api.stop();assert.equal(r.state,'video-frame-progress-observed');assert.equal(r.resourcesReleased,true);assert.equal(r.currentTimeDeltaSeconds,3);assert.equal(r.nonDroppedFrameCountDelta,30);assert.equal(r.trustedControlClickCount,1);assert.equal(r.eventTimeline.find(e=>e.type==='control-click').elapsedMs,r.trustedClickElapsedMs);assert.equal(f.forbidden(),0);});
check('synthetic click cannot establish trusted playback',()=>{const f=fixture();f.api.arm();f.click(false);f.playing();for(let i=1;i<=4;i++)f.advance(1,i*15);const r=f.api.stop();assert.equal(r.trustedControlClickCount,0);assert.equal(r.untrustedControlClickCount,1);assert.equal(r.state,'playback-not-verified');});
check('untrusted playing events do not satisfy the observed event gate',()=>{const f=fixture();f.api.arm();f.click();f.playing(false);for(let i=1;i<=4;i++)f.advance(1,i*15);assert.equal(f.api.stop().playingAfterTrustedClick,false);});
check('real time progress cannot substitute for zero frame counters',()=>{const f=fixture();f.api.arm();f.click();f.playing();for(let i=1;i<=4;i++)f.advance(1,0);assert.equal(f.api.stop().state,'playback-not-verified');});
check('pre-cleanup pause is retained separately and event history is bounded',()=>{const f=fixture();f.api.arm();f.click();f.playing();f.video.paused=true;f.event('pause');for(let i=0;i<40;i++)f.event('waiting');const r=f.api.stop();assert.equal(r.eventTimeline.length,24);assert.ok(r.eventsOmitted>0);assert.ok(r.eventTimeline.some(e=>e.type==='pause'&&!e.cleanup));assert.equal(f.mediaListeners.get('pause').size,0);assert.ok(JSON.stringify(r).length<8192);});
(async()=>{
  const {dispatchSinglePlayClick}=await import(pathToFileURL(path.join(__dirname,'public_video_gesture_chrome.mjs')).href);
  const target={eligible:true,control:'large-play',paused:true,muted:true,readyState:4,x:140,y:140};
  let calls=[];const proof=await dispatchSinglePlayClick(async(m,p)=>{calls.push([m,p]);return {};},target);
  check('CDP sends exactly one measured left press/release pair',()=>{assert.equal(calls.length,2);assert.deepEqual(calls.map(x=>x[1].type),['mousePressed','mouseReleased']);assert.ok(calls.every(x=>x[0]==='Input.dispatchMouseEvent'&&x[1].x===140&&x[1].y===140&&x[1].clickCount===1));assert.equal(proof.pressAcknowledged,true);assert.equal(proof.releaseAcknowledged,true);assert.equal(proof.physicalInputVerified,false);});
  calls=[];const failed=await dispatchSinglePlayClick(async(m,p)=>{calls.push(p.type);if(p.type==='mousePressed')throw new Error('fixture failure');return {};},target);
  check('interrupted press gets one release but never a retried press or PASS',()=>{assert.deepEqual(calls,['mousePressed','mouseReleased']);assert.equal(failed.pressAcknowledged,false);assert.equal(failed.releaseAcknowledged,true);});
  calls=[];await assert.rejects(dispatchSinglePlayClick(async()=>{calls.push(1);},{...target,control:'account-login'}));
  check('non-player control cannot dispatch input',()=>assert.equal(calls.length,0));
  console.log(`RESULTS public-video-gesture ${checks}/${checks} (0 failures)`);
})().catch(()=>{console.error('FAIL trusted-input fixtures');process.exitCode=1;});
