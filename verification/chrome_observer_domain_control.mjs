// Neutral data-only CDP regression. No account/profile/page content is accessed.
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
const out=path.resolve('out/verification');fs.mkdirSync(out,{recursive:true});
const profile=fs.mkdtempSync(path.join(out,'chrome-observer-domain-control-'));
const proc=spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',[
  '--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync',
  '--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore'});
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));let ws;
try {
  let port;for(let i=0;i<100;i++){try{port=Number(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0]);if(port)break;}catch{}await sleep(50);}
  if(!port)throw new Error('No owned test port');
  const pages=await(await fetch('http://127.0.0.1:'+port+'/json/list')).json();const target=pages.find(p=>p.type==='page'&&p.url==='about:blank');
  if(!target)throw new Error('No owned empty target');
  ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  let seq=0;const pending=new Map();ws.addEventListener('message',event=>{const m=JSON.parse(event.data),p=pending.get(m.id);if(p){pending.delete(m.id);clearTimeout(p.timer);m.error?p.reject(new Error('CDP rejected')):p.resolve(m.result);}});
  function call(method,params={}){return new Promise((resolve,reject)=>{const id=++seq,timer=setTimeout(()=>{pending.delete(id);reject(new Error('CDP timeout'));},5000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));});}
  const version=await call('Browser.getVersion');
  await call('Page.addScriptToEvaluateOnNewDocument',{source:fs.readFileSync('verification/auth-status-probe.js','utf8')});
  await call('Page.navigate',{url:'data:text/html,<html><body>Neutral observer control A</body></html>'});await sleep(300);
  const before=await call('Runtime.evaluate',{expression:'({observerPresent:!!window.LumePrivateStatus,readyState:document.readyState})',returnByValue:true});
  await call('Page.enable');
  await call('Page.navigate',{url:'data:text/html,<html><body>Neutral observer control B</body></html>'});await sleep(300);
  const after=await call('Runtime.evaluate',{expression:'({observerPresent:!!window.LumePrivateStatus,readyState:document.readyState})',returnByValue:true});
  const result={scope:'Fresh data-only document; no real sites, cookies, account actions or security overrides',browser:version.product,actualBrowserPID:proc.pid,withoutPageEnable:before.result?.value,withPageEnable:after.result?.value,redGreenReproduced:before.result?.value?.observerPresent===false&&after.result?.value?.observerPresent===true};
  fs.writeFileSync(path.join(out,'chrome-observer-domain-control.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
  await call('Browser.close');
  if(!result.redGreenReproduced)process.exitCode=1;
} catch {console.error('Neutral observer control failed; no raw error retained');process.exitCode=1;}
finally {ws?.close();if(proc.exitCode===null)proc.kill();}
