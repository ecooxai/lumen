// Private status only. Do not enable Runtime console events or save page content.
import fs from 'node:fs';
import path from 'node:path';
const [profile,site,presentation='headless']=process.argv.slice(2);
const urls={google:'https://www.google.com/',chatgpt:'https://chatgpt.com/',youtube:'https://www.youtube.com/',x:'https://x.com/'};
if(!urls[site])throw new Error('Unknown authorized site');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
let ws,closeBrowser,stage='connect';
function diagnostic(value){console.log('LUME_AUTH_PROBE '+JSON.stringify({schema:1,...value}));}
try{
  let port;
  for(let i=0;i<100;i++){try{port=Number(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0]);if(port)break;}catch{}await sleep(100);}
  if(!port)throw new Error('Owned private Chrome unavailable');
  const pages=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target=pages.find(page=>page.type==='page'&&page.url==='about:blank')||pages.find(page=>page.type==='page');if(!target)throw new Error('No owned page');
  for(const page of pages){if(page.type==='page'&&page.id!==target.id)await fetch(`http://127.0.0.1:${port}/json/close/${page.id}`);}
  ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  let sequence=0;const pending=new Map();
  ws.addEventListener('message',event=>{const message=JSON.parse(event.data);const item=pending.get(message.id);if(item){pending.delete(message.id);clearTimeout(item.timer);message.error?item.reject(new Error('CDP command failed')):item.resolve(message.result);}});
  function call(method,params={}){return new Promise((resolve,reject)=>{const id=++sequence;const timer=setTimeout(()=>{pending.delete(id);reject(new Error('CDP command timed out'));},10000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));});}
  closeBrowser=()=>call('Browser.close');
  await call('Page.enable');stage='page-enabled';diagnostic({stage,pageEnabled:true});
  if(presentation==='headful'){
    const browserWindow=await call('Browser.getWindowForTarget',{targetId:target.id});
    for(let attempt=0;attempt<4;attempt++){
      const reply=await call('Runtime.evaluate',{expression:'({width:innerWidth,height:innerHeight})',returnByValue:true});
      const dimensions=reply.result?.value;if(!dimensions)throw new Error('Owned window dimensions unavailable');
      if(dimensions.width===1024&&dimensions.height===724)break;
      const actual=await call('Browser.getWindowBounds',{windowId:browserWindow.windowId});
      await call('Browser.setWindowBounds',{windowId:browserWindow.windowId,bounds:{windowState:'normal',width:actual.bounds.width+1024-dimensions.width,height:actual.bounds.height+724-dimensions.height}});
      await sleep(200);
    }
    await call('Page.bringToFront');
  }else await call('Emulation.setDeviceMetricsOverride',{width:1024,height:724,deviceScaleFactor:1,mobile:false});
  await call('Page.addScriptToEvaluateOnNewDocument',{source:fs.readFileSync('verification/auth-status-probe.js','utf8')});stage='observer-registered';
  await call('Page.navigate',{url:urls[site]});stage='navigated';
  const start=Date.now();
  for(const delay of [5000,15000,30000,45000]){
    await sleep(Math.max(0,start+delay-Date.now()));
    const result=await call('Runtime.evaluate',{expression:'window.LumePrivateStatus?.snapshot()',returnByValue:true});
    stage='sampled';diagnostic({stage,sampleDelayMs:delay,observerPresent:!!result.result?.value,evaluationException:!!result.exceptionDetails});
    if(!result.exceptionDetails&&result.result?.value)console.log('LUME_AUTH_STATUS '+JSON.stringify(result.result.value));
  }
}catch{process.exitCode=1;diagnostic({stage,helperFailed:true});}
finally{try{await closeBrowser?.();}catch{}ws?.close();}
