import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const sleep = ms => new Promise(resolve => setTimeout(resolve,ms));

// Exactly one press/release pair at the observed control center. This is CDP
// automation, not a physical human click and not Runtime.evaluate userGesture.
export async function dispatchSinglePlayClick(call,target) {
  if (!target || target.eligible !== true || !['large-play','transport-play'].includes(target.control) ||
      target.paused !== true || target.muted !== true || target.readyState < 2 ||
      !Number.isFinite(target.x) || !Number.isFinite(target.y) || target.x <= 0 || target.y <= 0 || target.x >= 1024 || target.y >= 724) {
    throw new Error('No eligible fixed play control');
  }
  const result={schema:1,method:'Input.dispatchMouseEvent',control:target.control,clicksRequested:1,
    pressRequested:false,releaseRequested:false,pressAcknowledged:false,releaseAcknowledged:false,
    programmaticPlayCalled:false,physicalInputVerified:false};
  const position={x:target.x,y:target.y,button:'left',clickCount:1,pointerType:'mouse'};
  try {
    result.pressRequested=true;
    await call('Input.dispatchMouseEvent',{...position,type:'mousePressed',buttons:1});
    result.pressAcknowledged=true;
  } catch {} finally {
    // Even an interrupted press gets its one release; there is no second press
    // and no second playback attempt. Missing acknowledgments remain failures.
    result.releaseRequested=true;
    try { await call('Input.dispatchMouseEvent',{...position,type:'mouseReleased',buttons:0}); result.releaseAcknowledged=true; } catch {}
  }
  return result;
}

// The navigation command acknowledges a frame/loader, not document readiness.
// Keep a bounded set of lifecycle receipts; never retain or emit URLs or titles.
export function createNavigationReadiness() {
  let selected=null; const receipts=[];
  function event(message) {
    if(message?.method!=='Page.lifecycleEvent')return;
    const p=message.params;
    if(!p || !['DOMContentLoaded','load'].includes(p.name) ||
       typeof p.frameId!=='string' || !p.frameId || p.frameId.length>128 ||
       typeof p.loaderId!=='string' || !p.loaderId || p.loaderId.length>128)return;
    if(selected && (p.frameId!==selected.frameId || p.loaderId!==selected.loaderId))return;
    if(receipts.length===48)receipts.shift();
    receipts.push({frameId:p.frameId,loaderId:p.loaderId,name:p.name});
  }
  function select(navigation) {
    if(selected || !navigation || navigation.errorText ||
       typeof navigation.frameId!=='string' || !navigation.frameId || navigation.frameId.length>128 ||
       typeof navigation.loaderId!=='string' || !navigation.loaderId || navigation.loaderId.length>128)throw new Error('No unique successful navigation');
    selected={frameId:navigation.frameId,loaderId:navigation.loaderId};
  }
  function seen(name) { return !!selected && receipts.some(p=>p.frameId===selected.frameId && p.loaderId===selected.loaderId && p.name===name); }
  return {event,select,isReady:()=>seen('DOMContentLoaded'),summary:()=>({
    navigationSelected:!!selected,navigationDOMContentLoadedObserved:seen('DOMContentLoaded'),
    navigationLoadObserved:seen('load'),navigationReceiptCount:receipts.filter(p=>selected && p.frameId===selected.frameId && p.loaderId===selected.loaderId).length})};
}

export async function waitForNavigationReadiness(gate,start,clock={now:Date.now,sleep}) {
  while(!gate.isReady() && clock.now()-start<20000)await clock.sleep(100);
  if(!gate.isReady())throw new Error('Navigation readiness deadline');
}

async function main() {
  const [profile] = process.argv.slice(2); let ws, call, input=null, stopped=false;
  const readiness=createNavigationReadiness(); let navigationReadyWaitMs=null;
  let phase='profile-preflight', lastMethod=null, lastCDPError=null, exceptionClass=null, failureDiagnostic=null, cleanupFailed=false;
  function diagnostic(failed) { return {schema:1,phase:lastCDPError?.phase||phase,failed,cdpMethod:lastCDPError?.method||lastMethod,cdpErrorCode:lastCDPError?.code??null,
    errorKind:lastCDPError?.kind||(exceptionClass?'script-exception':failed?'local-guard':'none'),
    contextTransition:lastCDPError?.contextTransition===true,exceptionClass,observerCompleted:stopped,cleanupFailed,...readiness.summary(),navigationReadyWaitMs}; }
  try {
    if (!profile || !path.resolve(profile).startsWith(path.resolve('out/verification')+path.sep+'public-video-gesture-empty-')) throw new Error('Unexpected owned profile');
    phase='debug-port'; let port;
    for (let i=0;i<100;i++) {
      try { port=Number(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0]); if(Number.isInteger(port)&&port>0&&port<65536)break; } catch {}
      await sleep(100);
    }
    if(!port)throw new Error('No owned Chrome');
    phase='owned-target'; const pages=await(await fetch(`http://127.0.0.1:${port}/json/list`,{signal:AbortSignal.timeout(5000)})).json();
    const empty=pages.filter(page=>page.type==='page'&&page.url==='about:blank');
    if(empty.length!==1)throw new Error('No unique owned empty target');
    const target=empty[0], endpoint=new URL(target.webSocketDebuggerUrl);
    if(endpoint.protocol!=='ws:'||endpoint.hostname!=='127.0.0.1'||Number(endpoint.port)!==port)throw new Error('Unexpected debugger endpoint');
    phase='debug-connect'; ws=new WebSocket(endpoint.href);
    await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Debugger connect timeout')),5000);ws.addEventListener('open',()=>{clearTimeout(timer);resolve();},{once:true});ws.addEventListener('error',()=>{clearTimeout(timer);reject(new Error('Debugger unavailable'));},{once:true});});
    let seq=0;const pending=new Map();
    ws.addEventListener('message',event=>{const message=JSON.parse(event.data);readiness.event(message);const item=pending.get(message.id);if(item){pending.delete(message.id);clearTimeout(item.timer);if(message.error){const code=message.error.code;lastCDPError={kind:'cdp-error',method:item.method,phase:item.phase,code:Number.isInteger(code)&&Math.abs(code)<1e9?code:null,contextTransition:/Execution context was destroyed|Cannot find context with specified id|Cannot find execution context|Inspected target navigated/i.test(String(message.error.message||''))};item.reject(new Error('CDP failed'));}else item.resolve(message.result);}});
    call=(method,params={})=>new Promise((resolve,reject)=>{lastMethod=method;const callPhase=phase,id=++seq;const timer=setTimeout(()=>{pending.delete(id);lastCDPError={kind:'cdp-timeout',method,phase:callPhase,code:null,contextTransition:false};reject(new Error('CDP timeout'));},5000);pending.set(id,{resolve,reject,timer,method,phase:callPhase});ws.send(JSON.stringify({id,method,params}));});
    async function evaluate(expression) { const r=await call('Runtime.evaluate',{expression,returnByValue:true}); if(r.exceptionDetails){const cls=r.exceptionDetails.exception?.className;exceptionClass=['Error','TypeError','ReferenceError','SyntaxError','RangeError'].includes(cls)?cls:'OtherError';throw new Error('Typed observation unavailable');} return r.result?.value; }
    phase='page-enable'; await call('Page.enable');
    phase='lifecycle-enable'; await call('Page.setLifecycleEventsEnabled',{enabled:true});
    phase='viewport'; const browserWindow=await call('Browser.getWindowForTarget',{targetId:target.id});
    for(let i=0;i<4;i++) {
      const d=await evaluate('({width:innerWidth,height:innerHeight})');
      if(!d)throw new Error('No dimensions');if(d.width===1024&&d.height===724)break;
      const b=(await call('Browser.getWindowBounds',{windowId:browserWindow.windowId})).bounds;
      await call('Browser.setWindowBounds',{windowId:browserWindow.windowId,bounds:{windowState:'normal',width:b.width+1024-d.width,height:b.height+724-d.height}});await sleep(200);
    }
    const dimensions=await evaluate('({width:innerWidth,height:innerHeight})');
    if(dimensions.width!==1024||dimensions.height!==724)throw new Error('Viewport unavailable');
    phase='foreground'; await call('Page.bringToFront');
    phase='observer-install'; await call('Page.addScriptToEvaluateOnNewDocument',{source:fs.readFileSync('verification/public-video-gesture-probe.js','utf8')});
    phase='navigate'; const navigation=await call('Page.navigate',{url:'https://www.youtube.com/watch?v=YE7VzlLtp-4'});
    const start=Date.now(),reported=new Set();let final=null,lastTarget=null;
    readiness.select(navigation);
    phase='wait-navigation-ready';
    try{await waitForNavigationReadiness(readiness,start);}finally{navigationReadyWaitMs=Date.now()-start;}
    while(Date.now()-start<55000) {
      phase='observe'; const snapshot=await evaluate('window.LumePublicVideoGesture?.snapshot()');
      if(snapshot?.resourcesReleased===true){final=snapshot;stopped=true;break;}
      if(snapshot && !input && Date.now()-start<30000) {
        phase='measure-control'; lastTarget=await evaluate('window.LumePublicVideoGesture.target()');
        if(lastTarget?.eligible===true) {
          phase='arm-control'; const armed=await evaluate('window.LumePublicVideoGesture.arm()');
          phase='dispatch-control'; input=await dispatchSinglePlayClick(call,armed);input.elapsedMs=Date.now()-start;
          console.log('LUME_PUBLIC_VIDEO_INPUT '+JSON.stringify(input));
        }
      }
      for(const delay of [5000,15000,30000,44000])if(snapshot&&snapshot.elapsedMs>=delay&&!reported.has(delay)){
        reported.add(delay);console.log('LUME_PUBLIC_VIDEO_GESTURE '+JSON.stringify(snapshot));
      }
      await sleep(1000);
    }
    if(!final) {phase='finish-observation';final=await evaluate('window.LumePublicVideoGesture?.stop()');stopped=!!final?.resourcesReleased;}
    if(final)console.log('LUME_PUBLIC_VIDEO_GESTURE '+JSON.stringify(final));
    if(!input)console.log('LUME_PUBLIC_VIDEO_INPUT '+JSON.stringify({schema:1,method:'none',control:null,clicksRequested:0,pressRequested:false,releaseRequested:false,pressAcknowledged:false,releaseAcknowledged:false,programmaticPlayCalled:false,physicalInputVerified:false,unavailableReason:lastTarget?.reason||'no-probe'}));
    if(!stopped||!input?.pressAcknowledged||!input?.releaseAcknowledged)process.exitCode=1;
  } catch {
    failureDiagnostic=diagnostic(true);process.exitCode=1;console.error('Public trusted-input observation unavailable; no raw error retained');
  } finally {
    if(call&&!stopped)try{const r=await call('Runtime.evaluate',{expression:'window.LumePublicVideoGesture?.stop()',returnByValue:true});if(r.result?.value)console.log('LUME_PUBLIC_VIDEO_GESTURE '+JSON.stringify(r.result.value));}catch{}
    try{await call?.('Browser.close');}catch{cleanupFailed=true;process.exitCode=1;}
    ws?.close();
    const finalDiagnostic=failureDiagnostic||diagnostic(process.exitCode===1);finalDiagnostic.cleanupFailed=cleanupFailed;finalDiagnostic.observerCompleted=stopped;
    console.log('LUME_PUBLIC_VIDEO_DIAGNOSTIC '+JSON.stringify(finalDiagnostic));
  }
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href)await main();
