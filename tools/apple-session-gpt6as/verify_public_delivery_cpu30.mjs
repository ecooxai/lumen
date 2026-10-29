import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {performance} from 'node:perf_hooks';

const taskId='20261029-cpu30', root=process.cwd();
assert.notEqual(process.env.NODE_TLS_REJECT_UNAUTHORIZED,'0','TLS verification must remain enabled');
const iteration=process.env.LUME_DELIVERY_ITERATION||'1';
assert.match(iteration,/^[1-9][0-9]{0,2}$/);
const out=path.join(root,`out/lume-${taskId}/public-delivery-${iteration}`);
await fs.mkdir(out,{recursive:false,mode:0o700});
const state=path.join(root,`output/session-${taskId}/progress-state`);
const read=async p=>JSON.parse(await fs.readFile(p,'utf8'));
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const manifest=await read(path.join(root,`output/session-${taskId}/public/manifest.json`));
const publication=await read(path.join(root,`output/session-${taskId}/surge-publication.json`));
const server=await read(path.join(state,'server-receipt.json'));
assert.equal(publication.deployed,true);
assert.equal(publication.sourceCommit,manifest.sourceCommit);
assert.equal(publication.archive.sha256,manifest.sha256);
assert.equal(publication.archive.bytes,manifest.bytes);
const origins=[{label:'live',url:server.publicURL},{label:'surge',url:publication.publicURL}];
assert.deepEqual(origins.map(r=>r.url),['https://dd1cacbcfa84c17d.q4d3dgcpeeoq.opentunnel.xyz','https://lume-102900614-gpt6-astra-pro.surge.sh']);
const start=performance.now();
const result={schema:1,taskId,startedUTC:new Date().toISOString(),passed:false,scope:'Full public source bytes and real-browser desktop/mobile progress-page smoke. Native Lume performance is measured separately.',sourceCommit:manifest.sourceCommit,expectedArchive:{bytes:manifest.bytes,sha256:manifest.sha256},origins:[],ui:[],privateKeysOmitted:true};
const save=async()=>{await fs.writeFile(path.join(out,'receipt.tmp'),JSON.stringify(result,null,2)+'\n');await fs.rename(path.join(out,'receipt.tmp'),path.join(out,'receipt.json'));};
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function get(origin,endpoint,maxBytes=1000000){
  const url=new URL(endpoint,origin);url.searchParams.set('deliveryCheck',crypto.randomUUID());
  const response=await fetch(url,{cache:'no-store',redirect:'error',signal:AbortSignal.timeout(45000)});
  assert.equal(response.status,200,`${origin}${endpoint}: HTTP ${response.status}`);
  const hash=crypto.createHash('sha256');let bytes=0;const chunks=[];
  for await(const chunk of response.body){bytes+=chunk.length;assert.ok(bytes<=maxBytes,'Response exceeds the bounded byte limit');hash.update(chunk);if(endpoint!=='/src.tar.gz')chunks.push(chunk);}
  return {bytes,sha256:hash.digest('hex'),data:chunks.length?Buffer.concat(chunks):null,tlsCertificateValidation:'Node default certificate verification; no override'};
}
try{
  for(const origin of origins){
    const began=performance.now(), row={...origin,verified:false};result.origins.push(row);
    const archive=await get(origin.url,'/src.tar.gz',manifest.bytes);
    assert.equal(archive.bytes,manifest.bytes);assert.equal(archive.sha256,manifest.sha256);
    row.archive={bytes:archive.bytes,sha256:archive.sha256,fullByteHashVerified:true};
    if(origin.label==='surge'){
      const source=await get(origin.url,'/source-manifest.json');
      const sourceJson=JSON.parse(source.data.toString());
      assert.equal(sourceJson.sourceCommit,manifest.sourceCommit);assert.equal(sourceJson.sha256,manifest.sha256);
      row.sourceManifest={sha256:source.sha256,sourceCommit:sourceJson.sourceCommit};
    }else{
      const publicStatus=JSON.parse((await get(origin.url,'/api/progress')).data.toString()).status;
      assert.equal(publicStatus.sourceCommit,manifest.sourceCommit);assert.equal(publicStatus.sourceArchive.sha256,manifest.sha256);
      row.sourceManifest={source:'/api/progress status.sourceArchive',sourceCommit:publicStatus.sourceCommit,archiveSHA256:publicStatus.sourceArchive.sha256};
    }
    const config=JSON.parse((await get(origin.url,'/config.json')).data.toString());
    assert.equal(config.taskId,taskId);assert.equal(config.messageSessionId,'102900614');
    if(origin.label==='surge')assert.equal(config.liveBase,origins[0].url);
    row.config={taskId:config.taskId,messageSessionId:config.messageSessionId,liveBase:config.liveBase};
    row.viewHashes={};
    for(const file of ['app.js','index.html','style.css']){
      const observed=await get(origin.url,file==='index.html'?'/':'/'+file);
      assert.equal(observed.sha256,publication.publishedFileHashes[file],`${file} public bytes differ`);
      row.viewHashes[file]=observed.sha256;
    }
    row.verified=true;row.durationSeconds=Number(((performance.now()-began)/1000).toFixed(3));await save();
  }
  const health=JSON.parse((await get(origins[0].url,'/api/health')).data.toString());
  assert.equal(health.ok,true);assert.equal(health.taskId,taskId);
  const progress=JSON.parse((await get(origins[0].url,'/api/progress')).data.toString());
  assert.equal(progress.status.taskId,taskId);assert.equal(progress.status.sourceCommit,manifest.sourceCommit);
  assert.equal(progress.status.sourceArchive.sha256,manifest.sha256);
  result.currentAcceptedArtifact={sha256:progress.status.currentWpt?.binarySha256,sourceCommit:progress.status.currentWpt?.sourceCommit,recorded:progress.status.currentWpt?.recorded,fullyPassing:progress.status.currentWpt?.fullyPassing};
  result.publicSourceBytesVerified=true;
  await save();
  assert.equal(typeof WebSocket,'function','This installed Node runtime needs its built-in WebSocket API for the isolated browser smoke');
  const chrome=path.join(root,'output/session-102900614/chrome-official-reference-v2/Google Chrome.app/Contents/MacOS/Google Chrome');
  const chromeSHA='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4';
  assert.equal(digest(await fs.readFile(chrome)),chromeSHA);
  const profile=path.join(out,'owned-browser-profile');await fs.mkdir(profile,{mode:0o700});
  const log=await fs.open(path.join(out,'browser.log'),'wx',0o600);
  const browser=spawn(chrome,['--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:['ignore',log.fd,log.fd]});
  let ws,closed=false,id=0;const pending=new Map();
  const call=(method,params={},sessionId)=>new Promise((resolve,reject)=>{
    const identifier=++id;const timer=setTimeout(()=>{pending.delete(identifier);reject(Error('CDP timeout: '+method));},15000);
    pending.set(identifier,{resolve,reject,timer});ws.send(JSON.stringify({id:identifier,method,params,...(sessionId?{sessionId}:{})}));
  });
  try{
    let port;
    for(let attempt=0;attempt<100;attempt++){
      try{const [first]=String(await fs.readFile(path.join(profile,'DevToolsActivePort'),'utf8')).trim().split('\n');port=Number(first);if(port)break;}catch{}
      if(browser.exitCode!==null)throw Error('Owned Chrome exited before CDP readiness');await sleep(100);
    }
    assert.ok(port,'Owned Chrome CDP port unavailable');
    const descriptor=await fetch('http://127.0.0.1:'+port+'/json/version',{signal:AbortSignal.timeout(5000)}).then(r=>r.json());
    ws=new WebSocket(descriptor.webSocketDebuggerUrl);
    await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
    ws.addEventListener('message',event=>{const value=JSON.parse(event.data);if(!value.id)return;const item=pending.get(value.id);if(!item)return;pending.delete(value.id);clearTimeout(item.timer);value.error?item.reject(Error(value.error.message)):item.resolve(value.result);});
    result.browser={binarySHA256:chromeSHA,version:descriptor.Browser,normalSandbox:true,profileScope:'Fresh owned test profile; no user profile or session used'};
    const access=(await fs.readFile(path.join(state,'message-access-key'),'utf8')).trim();
    assert.ok(access.length>=24);
    for(const origin of origins){
      const context=await call('Target.createBrowserContext');
      const target=await call('Target.createTarget',{url:'about:blank',browserContextId:context.browserContextId});
      const {sessionId}=await call('Target.attachToTarget',{targetId:target.targetId,flatten:true});
      const evaluate=async expression=>{const answer=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true},sessionId);if(answer.exceptionDetails)throw Error('Page evaluation failed');return answer.result.value;};
      await call('Page.enable',{},sessionId);await call('Runtime.enable',{},sessionId);
      for(const viewport of [{name:'desktop',width:1440,height:1000,mobile:false},{name:'mobile',width:390,height:844,mobile:true}]){
        const row={origin:origin.label,viewport,passed:false};result.ui.push(row);
        await call('Emulation.setDeviceMetricsOverride',{width:viewport.width,height:viewport.height,deviceScaleFactor:1,mobile:viewport.mobile},sessionId);
        await call('Page.navigate',{url:origin.url+'/?deliverySmoke='+crypto.randomUUID()},sessionId);
        let observed;
        for(let attempt=0;attempt<60;attempt++){
          observed=await evaluate(`(()=>{const text=id=>document.getElementById(id)?.textContent||'';return {title:document.title,connection:text('connection'),count:text('wpt-count'),rate:text('wpt-rate'),cpu:text('cpu'),ram:text('ram'),latency:text('latency'),disks:document.querySelectorAll('#disks tr').length,width:innerWidth,scrollWidth:document.documentElement.scrollWidth,score:text('score'),artifact:text('artifact'),privateAccessEnabled:document.getElementById('send')?.disabled===false}})()`);
          if(observed.title.includes(taskId)&&observed.connection.startsWith('Live')&&observed.latency.endsWith(' ms')&&observed.cpu.endsWith('%')&&observed.disks>0)break;
          await sleep(200);
        }
        assert.ok(observed.title.includes(taskId));assert.ok(observed.connection.startsWith('Live'));assert.ok(observed.latency.endsWith(' ms'));assert.ok(observed.cpu.endsWith('%'));assert.ok(observed.ram.endsWith(' GB'));assert.ok(observed.disks>0);
        assert.ok(observed.count.includes(String(progress.status.currentWpt.recorded)));assert.ok(observed.artifact.includes(progress.status.currentWpt.binarySha256.slice(0,20)));
        assert.ok(observed.scrollWidth<=observed.width+1,'Page has horizontal viewport overflow');
        const card=await evaluate(`(()=>{const rate=document.querySelector('.hero>.rate-card'),summary=document.querySelector('.hero>div:not(.rate-card)');return {label:rate?.querySelector('.eyebrow')?.textContent,rateTop:rate?.getBoundingClientRect().top,summaryTop:summary?.getBoundingClientRect().top}})()`);
        assert.equal(card.label,'Selected WPT pass rate');
        if(viewport.mobile)assert.ok(card.rateTop<card.summaryTop,'Mobile WPT card must precede the long summary');
        row.wptCard=card;
        if(viewport.name==='desktop'){
          await evaluate('sessionStorage.setItem("lume102900614-access",'+JSON.stringify(access)+');true');
          await call('Page.reload',{ignoreCache:true},sessionId);
          let enabled=false;
          for(let attempt=0;attempt<40;attempt++){
            enabled=await evaluate('document.getElementById("send")?.disabled===false && location.hash==="" && sessionStorage.getItem("lume102900614-access")!==null');if(enabled)break;await sleep(100);
          }
          assert.equal(enabled,true,'Ordinary reload lost the explicit old message namespace');row.privateAccessSurvivedOrdinaryReload=true;
          await sleep(2200);
        }
        const actionLayout=await evaluate(`(()=>{const button=document.getElementById('send'),badge=document.getElementById('score');button.scrollIntoView({block:'end'});const a=button.getBoundingClientRect(),b=badge.getBoundingClientRect(),overlap=Math.max(0,Math.min(a.right,b.right)-Math.max(a.left,b.left))*Math.max(0,Math.min(a.bottom,b.bottom)-Math.max(a.top,b.top));const result={button:{left:a.left,right:a.right,top:a.top,bottom:a.bottom},badge:{left:b.left,right:b.right,top:b.top,bottom:b.bottom},intersectionArea:overlap};window.scrollTo(0,0);return result})()`);
        assert.equal(actionLayout.intersectionArea,0,'Fixed score badge overlaps the message action');row.messageActionLayout=actionLayout;
        await sleep(75);
        const screenshot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false},sessionId);
        const file=path.join(out,origin.label+'-'+viewport.name+'.png'), bytes=Buffer.from(screenshot.data,'base64');await fs.writeFile(file,bytes,{mode:0o600});
        row.observed=observed;row.screenshot={path:path.relative(root,file),bytes:bytes.length,sha256:digest(bytes),scope:'Internal visual QA only; excludes private keys and messages'};row.passed=true;await save();
      }
      await call('Target.disposeBrowserContext',{browserContextId:context.browserContextId});
    }
    await call('Browser.close');closed=true;
  }finally{
    if(ws)ws.close();for(const item of pending.values()){clearTimeout(item.timer);item.reject(Error('Browser smoke closing'));}pending.clear();
    if(!closed&&browser.exitCode===null)browser.kill('SIGTERM');
    for(let attempt=0;attempt<40&&browser.exitCode===null;attempt++)await sleep(100);
    if(browser.exitCode===null){browser.kill('SIGKILL');throw Error('Owned UI smoke browser did not exit cleanly');}
    result.browserCleanup={exitCode:browser.exitCode,signalCode:browser.signalCode,closedThroughCDP:closed};await log.close();
    // This exact fresh profile was created above and may contain the test access capability.
    await fs.rm(profile,{recursive:true,force:false});
  }
  assert.equal(result.ui.length,4);assert.ok(result.ui.every(row=>row.passed));
  result.passed=true;
}catch(error){result.error=error.name+': '+error.message;}
result.finishedUTC=new Date().toISOString();result.durationSeconds=Number(((performance.now()-start)/1000).toFixed(3));await save();
process.stdout.write(JSON.stringify(result,null,2)+'\n');process.exitCode=result.passed?0:1;
