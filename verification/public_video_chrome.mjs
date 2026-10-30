import fs from 'node:fs';
import path from 'node:path';
const [profile] = process.argv.slice(2);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let ws, closeBrowser;
try {
  let port;
  for (let i=0;i<100;i++) {
    try { port=Number(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0]); if(port)break; } catch {}
    await sleep(100);
  }
  if(!port)throw new Error('No owned Chrome');
  const pages=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target=pages.find(page=>page.type==='page'&&page.url==='about:blank');
  if(!target)throw new Error('No owned empty target');
  ws=new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  let seq=0;const pending=new Map();
  ws.addEventListener('message',event=>{const message=JSON.parse(event.data);const item=pending.get(message.id);if(item){pending.delete(message.id);clearTimeout(item.timer);message.error?item.reject(new Error('CDP failed')):item.resolve(message.result);}});
  function call(method,params={}) {return new Promise((resolve,reject)=>{const id=++seq;const timer=setTimeout(()=>{pending.delete(id);reject(new Error('CDP timeout'));},10000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));});}
  closeBrowser=()=>call('Browser.close');
  await call('Page.enable');
  const browserWindow=await call('Browser.getWindowForTarget',{targetId:target.id});
  for(let i=0;i<4;i++) {
    const response=await call('Runtime.evaluate',{expression:'({width:innerWidth,height:innerHeight})',returnByValue:true});const d=response.result?.value;
    if(!d)throw new Error('No dimensions');if(d.width===1024&&d.height===724)break;
    const b=(await call('Browser.getWindowBounds',{windowId:browserWindow.windowId})).bounds;
    await call('Browser.setWindowBounds',{windowId:browserWindow.windowId,bounds:{windowState:'normal',width:b.width+1024-d.width,height:b.height+724-d.height}});await sleep(200);
  }
  await call('Page.bringToFront');
  await call('Page.addScriptToEvaluateOnNewDocument',{source:fs.readFileSync('verification/public-video-probe.js','utf8')});
  await call('Page.navigate',{url:'https://www.youtube.com/watch?v=YE7VzlLtp-4'});
  const start=Date.now();
  for(const delay of [6000,16000,31000,46000]) {
    await sleep(Math.max(0,start+delay-Date.now()));
    const r=await call('Runtime.evaluate',{expression:'window.LumePublicVideo?.snapshot()',returnByValue:true});
    if(!r.exceptionDetails&&r.result?.value)console.log('LUME_PUBLIC_VIDEO '+JSON.stringify(r.result.value));
  }
} catch {process.exitCode=1;console.error('Public video observation unavailable; no raw error retained');}
finally {try {await closeBrowser?.();}catch{}ws?.close();}
