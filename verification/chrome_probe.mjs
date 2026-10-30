import fs from 'node:fs';
import path from 'node:path';
const [profile, prefix, url, kind='html5', presentation='headless'] = process.argv.slice(2);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let port;
for(let attempt=0; attempt<100; attempt++) {
  try { port=Number(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0]); if(port)break; }catch{}
  await sleep(100);
}
if(!port)throw new Error('Owned Chrome debugging port did not become ready');
const tabs=await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const target=tabs.find(tab=>tab.type==='page');
if(!target)throw new Error('Owned Chrome has no page target');
const ws=new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
const pending=new Map();let sequence=0;
const detectorRequests=new Set(), detectorNetwork=[];
ws.addEventListener('message',event=>{const msg=JSON.parse(event.data);
if(msg.method==='Network.requestWillBeSent'){try{const u=new URL(msg.params.request.url);if(u.hostname==='api.whichbrowser.net'&&u.pathname==='/rel/detect.js'){detectorRequests.add(msg.params.requestId);detectorNetwork.push({event:'request',type:msg.params.type});}}catch{}}
if(msg.method==='Network.responseReceived'&&detectorRequests.has(msg.params.requestId)){const r=msg.params.response;detectorNetwork.push({event:'response',status:r.status,protocol:r.protocol,remoteIPAddress:r.remoteIPAddress,fromDiskCache:!!r.fromDiskCache,fromServiceWorker:!!r.fromServiceWorker,mimeType:r.mimeType});}
if(msg.method==='Network.loadingFailed'&&detectorRequests.has(msg.params.requestId))detectorNetwork.push({event:'failed',error:msg.params.errorText,blockedReason:msg.params.blockedReason||null});
if(msg.method==='Network.loadingFinished'&&detectorRequests.has(msg.params.requestId))detectorNetwork.push({event:'finished',bytes:msg.params.encodedDataLength});
if(msg.id&&pending.has(msg.id)){const item=pending.get(msg.id);pending.delete(msg.id);clearTimeout(item.timer);msg.error?item.reject(new Error(JSON.stringify(msg.error))):item.resolve(msg.result);}});
function call(method,params={}) {return new Promise((resolve,reject)=>{const id=++sequence;const timer=setTimeout(()=>{pending.delete(id);reject(new Error(`CDP timeout ${method}`));},35000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression,awaitPromise=false){const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise});if(result.exceptionDetails)throw new Error(result.exceptionDetails.text+': '+(result.exceptionDetails.exception?.description||''));return result.result?.value;}
try {
  await call('Page.enable');await call('Runtime.enable');await call('Network.enable');
  if(presentation==='headful'){
    const browserWindow=await call('Browser.getWindowForTarget',{targetId:target.id});
    for(let attempt=0;attempt<4;attempt++){
      const dimensions=await evaluate('({width:innerWidth,height:innerHeight,outerWidth,outerHeight,devicePixelRatio})');
      if(dimensions.width===1024&&dimensions.height===724)break;
      const actual=await call('Browser.getWindowBounds',{windowId:browserWindow.windowId});
      await call('Browser.setWindowBounds',{windowId:browserWindow.windowId,bounds:{windowState:'normal',width:actual.bounds.width+1024-dimensions.width,height:actual.bounds.height+724-dimensions.height}});
      await sleep(200);
    }
    await call('Page.bringToFront');
  }else await call('Emulation.setDeviceMetricsOverride',{width:1024,height:724,deviceScaleFactor:1,mobile:false});
  console.log('LUME_WINDOW_EVIDENCE '+JSON.stringify({phase:'before-navigation',presentation,...await evaluate('({viewport:{width:innerWidth,height:innerHeight},devicePixelRatio,visibility:document.visibilityState})')}));
  if(['gpu-challenge','identity-simulation'].includes(kind))await call('Page.addScriptToEvaluateOnNewDocument',{source:fs.readFileSync('verification/'+(kind==='gpu-challenge'?'gpu-challenge-probe.js':'identity-simulation-probe.js'),'utf8')});
  await call('Page.navigate',{url});
  if(kind==='identity-simulation'){
    let result;
    for(let attempt=0;attempt<80;attempt++){
      result=await evaluate('window.LumeIdentityEvidence?.snapshot()');
      if(result&&['pass','fail','startup-timeout'].includes(result.status))break;
      await sleep(250);
    }
    fs.writeFileSync(prefix+'.probe.json',JSON.stringify(result||{status:'startup-timeout'},null,2));
    console.log('LUME_IDENTITY_EVIDENCE '+JSON.stringify(result||{status:'startup-timeout'}));
  }else if(kind==='capture-control'){
    await sleep(1200);
    const result=await evaluate('({readyState:document.readyState,visible:!document.hidden,background:getComputedStyle(document.body).backgroundColor,viewport:{width:innerWidth,height:innerHeight}})');
    const screenshot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(prefix+'.cdp.png',Buffer.from(screenshot.data,'base64'));
    fs.writeFileSync(prefix+'.probe.json',JSON.stringify(result,null,2));
    console.log('LUME_CAPTURE_REQUEST overview');console.log('LUME_CHROME_EVIDENCE '+JSON.stringify({kind,...result}));
    await sleep(10000);
  } else if(kind==='gpu-challenge'){
    let result;const captured=new Set();
    for(let attempt=0;attempt<90;attempt++){
      result=await evaluate('window.LumeGPUChallenge?.snapshot()');
      if(result?.phase&&!captured.has(result.phase)){
        await sleep(800);
        const capture=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
        fs.writeFileSync(prefix+'.'+(result.phase==='A'?'overview':'gpu')+'.png',Buffer.from(capture.data,'base64'));captured.add(result.phase);
      }
      if(result&&['pass','fail','unsupported','startup-timeout'].includes(result.status)&&result.resourcesReleased)break;
      await sleep(250);
    }
    fs.writeFileSync(prefix+'.probe.json',JSON.stringify(result||{status:'startup-timeout'},null,2));
    console.log('LUME_GPU_EVIDENCE '+JSON.stringify(result||{status:'startup-timeout'}));
  } else if(kind==='rtc'){
    let result;
    for(let attempt=0;attempt<115;attempt++){
      result=await evaluate('window.LumeRTCPeer?.snapshot()');
      if(result&&['pass','fail','unsupported','cancelled'].includes(result.state))break;
      await sleep(250);
    }
    fs.writeFileSync(prefix+'.probe.json',JSON.stringify(result||{state:'startup-timeout'},null,2));
    const screenshot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(prefix+'.png',Buffer.from(screenshot.data,'base64'));
    console.log('LUME_RTC_INTEROP '+JSON.stringify(result||{state:'startup-timeout'}));
  } else if(kind==='lab'||kind==='common'||kind==='showcase'){
    for(let attempt=0;attempt<60;attempt++){if(await evaluate('!!window.LumeLab'))break;await sleep(250);}
    const result=await evaluate(kind==='common'?"(async()=>{for(const id of ['wasm','webgl','webgpu','iframe','websocket'])await LumeLab.run(id);return LumeLab.snapshot()})()":'window.LumeLab.runAutomatic()',true);
    result.detectorNetwork=detectorNetwork;
    fs.writeFileSync(prefix+'.probe.json',JSON.stringify(result,null,2));
    const screenshot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(prefix+'.png',Buffer.from(screenshot.data,'base64'));
    if(kind==='common')console.log('LUME_CAPTURE_REQUEST overview');
    if(kind==='showcase'){
      fs.copyFileSync(prefix+'.png',prefix+'.overview.png');
      console.log('LUME_CAPTURE_REQUEST overview');await sleep(2000);
      for(const [stage,id] of [['gpu','webgl'],['network','websocket']]){
        await evaluate(`document.querySelector('[data-capability="${id}"]').scrollIntoView({block:'start',behavior:'instant'})`);
        await sleep(600);
        const captured=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
        fs.writeFileSync(prefix+'.'+stage+'.png',Buffer.from(captured.data,'base64'));
        console.log('LUME_CAPTURE_REQUEST '+stage);await sleep(2000);
      }
    }
    await sleep(3500);
    await evaluate("LumeLab.stopAll('independent verifier teardown')");
    fs.writeFileSync(prefix+'.post-stop.json',JSON.stringify(await evaluate('LumeLab.snapshot()'),null,2));
    console.log('LUME_CHROME_EVIDENCE '+JSON.stringify({kind,summary:result.summary,resourcesAfterStop:await evaluate('LumeLab.snapshot().resources'),presentation:await evaluate('({viewport:{width:innerWidth,height:innerHeight},devicePixelRatio,visibility:document.visibilityState})')}));
    await sleep(15000);
  } else {
    await sleep(29000);
    const result=await evaluate(`(()=>{const text=document.body?.innerText||'';const score=/scores\\s+(\\d+)\\s+out of\\s+(\\d+)/i.exec(text);return {case:'html5test-co',url:location.href,title:document.title,readyState:document.readyState,score:score?Number(score[1]):null,maximum:score?Number(score[2]):null,userAgent:navigator.userAgent,viewport:{width:innerWidth,height:innerHeight},globals:{WhichBrowser:typeof WhichBrowser,Browsers:typeof Browsers,Test:typeof Test},pageText:text.slice(0,18000)}})()`);
    result.detectorNetwork=detectorNetwork;
    fs.writeFileSync(prefix+'.probe.json',JSON.stringify(result,null,2));
    const screenshot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(prefix+'.png',Buffer.from(screenshot.data,'base64'));
    console.log('LUME_CHROME_EVIDENCE '+JSON.stringify({kind,score:result.score,maximum:result.maximum,title:result.title,viewport:result.viewport,globals:result.globals,detectorNetwork}));
  }
} finally { ws.close(); }
