import {LabModel,CleanupBag,classifyFailure,diagnosticConsoleLines} from './model.js';
import {LabView} from './view.js';
import * as demos from './demos.js';

const handlers={camera:demos.camera,microphone:demos.microphone,location:demos.location,frame:demos.frame,wasm:demos.wasm,webrtc:demos.webrtc,websocket:demos.websocket,webtransport:demos.webtransport,graphics:demos.graphics,storage:demos.storage,worker:demos.worker,google:ctx=>demos.auth('google',ctx),apple:ctx=>demos.auth('apple',ctx),fetch:demos.fetchDemo};
const model=new LabModel(Object.keys(handlers)),view=new LabView(),sessions=new Map();
let batchGeneration=0;
model.context={userAgent:navigator.userAgent,platform:navigator.platform,engineCapabilities:navigator.lumenCapabilities??null,secureContext:window.isSecureContext,url:window.location.origin+window.location.pathname,viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio},note:'Functional exercise results; OAuth is a labeled local simulation. Process CPU/RAM requires external sampling. Location coordinates are omitted.'};
const resourceCount=()=>[...sessions.values()].reduce((n,s)=>n+s.bag.size,0);
const render=()=>{view.render(model.report(),resourceCount());view.runtime();};
model.subscribe(render);render();
document.getElementById('ws-endpoint').value=`${window.location.protocol==='https:'?'wss':'ws'}://${window.location.host}/echo`;

async function stop(id, announce=true) {
  const session=sessions.get(id);if(!session)return;
  sessions.delete(id);session.abort.abort();await session.bag.clear();
  if(!sessions.has(id)&&model.results.get(id).state==='running')model.record(id,'stopped','Stopped by the user or page lifecycle.');
  if(announce)view.log(`${id}: resources released`);render();
}
async function stopAll() {batchGeneration++;await Promise.all([...sessions.keys()].map(id=>stop(id)));demos.clearRecording();render();}

function run(id) {
  if(!handlers[id])return Promise.reject(Error(`Unknown exercise: ${id}`));
  if(sessions.has(id))return stop(id,false).then(()=>run(id));
  const bag=new CleanupBag(),abort=new AbortController(),session={bag,abort};sessions.set(id,session);
  const start=performance.now();model.record(id,'running','Exercise is running. Respond to any device permission prompt.');
  const ctx={bag,signal:abort.signal,stop,log:message=>view.log(message),detail:(key,detail)=>model.recordDetail(key,detail)};
  // Execute synchronously up to the first await so popup creation retains the user's activation.
  let pending;
  try{pending=handlers[id](ctx);}catch(error){pending=Promise.reject(error);}
  return Promise.resolve(pending).then(async result=>{
    if(sessions.get(id)!==session)return;
    if(!result.keep){await bag.clear();if(sessions.get(id)!==session)return;sessions.delete(id);}
    model.record(id,result.state,result.evidence,Math.round((performance.now()-start)*10)/10);view.log(`${id}: ${result.state} — ${result.evidence}`);
    return model.results.get(id);
  }).catch(async error=>{
    if(sessions.get(id)!==session)return;
    const cancelled=abort.signal.aborted;
    abort.abort();await bag.clear();if(sessions.get(id)!==session)return;sessions.delete(id);
    const {state,message}=classifyFailure(error,{id,cancelled});
    model.record(id,state,message,Math.round((performance.now()-start)*10)/10);view.log(`${id}: ${state} — ${message}`);return model.results.get(id);
  }).finally(render);
}

for(const id of Object.keys(handlers))document.querySelector(`[data-testid="run-${id}"]`)?.addEventListener('click',()=>run(id));
for(const button of document.querySelectorAll('[data-stop]'))button.addEventListener('click',()=>stop(button.dataset.stop));
document.querySelector('[data-testid="stop-all"]').addEventListener('click',()=>stopAll());
let safeRunning=false;
document.querySelector('[data-testid="run-safe"]').addEventListener('click',async event=>{
  if(safeRunning)return;safeRunning=true;const button=event.currentTarget;button.disabled=true;
  const generation=++batchGeneration;
  try{for(const id of ['wasm','worker','storage','fetch','webrtc','websocket','frame']){if(generation!==batchGeneration)break;await run(id);}}finally{button.disabled=false;safeRunning=false;}
});
document.querySelector('[data-testid="export-report"]').addEventListener('click',()=>{
  const report=model.report();report.activeResourceCount=resourceCount();
  const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));const link=document.createElement('a');link.href=url;link.download=`gpt6-astra-pro-devinmac3d-web-lume-evidence-${Date.now()}.json`;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
});
document.addEventListener('visibilitychange',()=>{if(document.hidden){for(const id of ['camera','microphone','webrtc','graphics'])stop(id);}});
window.addEventListener('pagehide',()=>{stopAll();});
window.addEventListener('resize',()=>view.runtime());
fetch('./iteration.json',{cache:'no-store'}).then(r=>r.ok?r.json():null).then(data=>{if(data){model.context.iteration=data;view.iteration(data);}}).catch(()=>view.iteration({scope:'manifest unavailable'}));
fetch('./api/webtransport-config',{cache:'no-store'}).then(r=>r.ok?r.json():null).then(data=>{
  if(data?.configured&&!document.getElementById('wt-endpoint').value){document.getElementById('wt-endpoint').value=data.url;document.getElementById('wt-cert').value=data.certificateHash;model.context.webtransportConfiguration=data;view.log('Existing HTTP/3 endpoint configured from its public certificate fingerprint. Connection and protocol evidence are not yet verified.');}
}).catch(()=>{});
window.__lumeLab={report:()=>({...model.report(),activeResourceCount:resourceCount()}),run,stop,stopAll,get resourceCount(){return resourceCount();}};
view.log('Ready. No captures start until you choose them. Repeatable checks use real output assertions.');
const automation=new URLSearchParams(window.location.search);
if(automation.get('autorun')==='1'){
  const backend=automation.get('backend');
  if(['webgpu','webgpu-offscreen','webgl'].includes(backend))document.getElementById('graphics-backend').value=backend;
  const endpoint=automation.get('webtransport');if(endpoint)document.getElementById('wt-endpoint').value=endpoint;
  const hash=automation.get('certificate');if(hash)document.getElementById('wt-cert').value=hash;
  setTimeout(async()=>{
    const generation=++batchGeneration;
    for(const id of ['wasm','worker','storage','fetch','webrtc','websocket','frame']){if(generation!==batchGeneration)break;await run(id);}
    if(generation===batchGeneration&&['webgpu','webgpu-offscreen','webgl'].includes(backend))await run('graphics');
    if(generation===batchGeneration&&endpoint)await run('webtransport');
    const report=window.__lumeLab.report();
    for(const line of diagnosticConsoleLines(report))console.log(line);
    window.__lumeLab.automationComplete=true;
    if(automation.get('cleanup')!=='0')await stopAll();
    console.log('LUME_LAB_CLEANUP '+JSON.stringify({activeResourceCount:resourceCount()}));
  },150);
}
