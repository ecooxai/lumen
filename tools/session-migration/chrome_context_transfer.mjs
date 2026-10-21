import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawn, execFileSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';

const directory=path.dirname(fileURLToPath(import.meta.url));
const sleep=milliseconds=>new Promise(resolve=>setTimeout(resolve,milliseconds));
class TransferError extends Error {constructor(code){super(code);this.code=code;}}

export function toCookieParam(cookie){
  const param={name:cookie.name,value:cookie.value,path:cookie.path,secure:cookie.secure,httpOnly:cookie.httpOnly,sourceScheme:cookie.sourceScheme,sourcePort:cookie.sourcePort};
  if(cookie.domain.startsWith('.'))param.domain=cookie.domain;
  // CDP makes an HTTPS URL imply Secure, even when secure:false is explicit.
  // Keep the actual Secure attribute in the URL; preserve the setting-origin
  // scheme independently in sourceScheme. This helper is used by fixtures only.
  else param.url=`${cookie.secure?'https':'http'}://${cookie.domain}/`;
  if(!cookie.session)param.expires=cookie.expires;
  if(cookie.sameSite!==undefined)param.sameSite=cookie.sameSite;
  return param;
}

export function sameCookies(left,right){
  if(left.length!==right.length)return false;
  const key=cookie=>JSON.stringify([cookie.domain,cookie.path,cookie.name]);
  const expected=new Map(left.map(cookie=>[key(cookie),cookie]));
  if(expected.size!==left.length||new Set(right.map(key)).size!==right.length)return false;
  return right.every(actual=>{
    const wanted=expected.get(key(actual));if(!wanted)return false;
    for(const field of ['value','secure','httpOnly','session','sourceScheme','sourcePort'])if(wanted[field]!==actual[field])return false;
    if((wanted.sameSite??null)!==(actual.sameSite??null))return false;
    return wanted.session||Math.abs(wanted.expires-actual.expires)<0.001;
  });
}

function cdp(ws){
  let sequence=0;const pending=new Map();
  ws.addEventListener('message',event=>{
    let message;try{message=JSON.parse(event.data);}catch{return;}
    const item=pending.get(message.id);if(!item)return;pending.delete(message.id);clearTimeout(item.timer);
    message.error?item.reject(new TransferError('chrome-protocol-rejected')):item.resolve(message.result);
  });
  ws.addEventListener('close',()=>{for(const item of pending.values()){clearTimeout(item.timer);item.reject(new TransferError('chrome-protocol-closed'));}pending.clear();});
  return (method,params={})=>new Promise((resolve,reject)=>{
    const id=++sequence;const timer=setTimeout(()=>{pending.delete(id);reject(new TransferError('chrome-protocol-timeout'));},12000);
    pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));
  });
}

export function assertOwnedProfileAvailable(profile){
  // Only a directory created by this helper may be launched or cleared.
  let marker;try{marker=JSON.parse(fs.readFileSync(path.join(profile,'LumePrivateProfile.json'),'utf8'));}catch{throw new TransferError('private-profile-marker-missing');}
  if(marker.kind!=='scoped-cookie-comparison'&&marker.kind!=='synthetic-cookie-fixture')throw new TransferError('invalid-private-profile-marker');
  for(const name of ['SingletonLock','SingletonSocket']){
    try{fs.lstatSync(path.join(profile,name));}catch(error){if(error.code==='ENOENT')continue;throw new TransferError('private-profile-lock-inspection-failed');}
    throw new TransferError('private-profile-already-in-use-or-stale-lock');
  }
}

async function startOwnedChrome(profile,executable){
  assertOwnedProfileAvailable(profile);
  fs.rmSync(path.join(profile,'DevToolsActivePort'),{force:true});
  const child=spawn(executable,[`--user-data-dir=${profile}`,'--headless=new','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0','--restore-last-session','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','about:blank'],{stdio:'ignore'});
  let exited=false,launchError=false;child.once('exit',()=>{exited=true;});child.once('error',()=>{launchError=true;exited=true;});
  let ws,call;
  async function close(){
    if(ws?.readyState===WebSocket.OPEN){try{await call('Browser.close');}catch{}}
    ws?.close();
    for(let i=0;i<40&&!exited;i++)await sleep(100);
    if(!exited){child.kill('SIGTERM');for(let i=0;i<20&&!exited;i++)await sleep(100);}
    if(!exited){child.kill('SIGKILL');for(let i=0;i<20&&!exited;i++)await sleep(100);}
    if(!exited)throw new TransferError('owned-chrome-did-not-exit');
  }
  try{
    let port;
    for(let i=0;i<120;i++){
      if(launchError||exited)throw new TransferError('owned-chrome-start-failed');
      try{port=Number(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0]);if(port>0&&port<65536)break;}catch{}
      await sleep(100);
    }
    if(!port)throw new TransferError('owned-chrome-start-timeout');
    const version=await(await fetch(`http://127.0.0.1:${port}/json/version`,{signal:AbortSignal.timeout(4000)})).json();
    const endpoint=new URL(version.webSocketDebuggerUrl);
    if(endpoint.hostname!=='127.0.0.1'&&endpoint.hostname!=='localhost')throw new TransferError('non-loopback-debugging-endpoint');
    ws=new WebSocket(endpoint);await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new TransferError('chrome-websocket-timeout')),4000);ws.addEventListener('open',()=>{clearTimeout(timer);resolve();},{once:true});ws.addEventListener('error',()=>{clearTimeout(timer);reject(new TransferError('chrome-websocket-error'));},{once:true});});
    call=cdp(ws);return {call,close};
  }catch(error){await close();throw error;}
}

function python(mode,cookies){
  const executable=process.env.LUME_BRIDGE_PYTHON||path.resolve(directory,'../web-capability-lab/.venv/bin/python');
  try{
    const output=execFileSync(executable,[path.join(directory,'browser_bridge.py'),mode],{input:cookies===undefined?'':JSON.stringify({cookies}),encoding:'utf8',stdio:['pipe','pipe','ignore'],maxBuffer:4*1024*1024,timeout:10000});
    return JSON.parse(output);
  }catch(error){
    let code='private-profile-helper-failed';try{const result=JSON.parse(String(error.stdout));if(/^[a-z0-9-]{1,80}$/.test(result.reason))code=result.reason;}catch{}
    throw new TransferError(code);
  }
}

export async function fixture(executable){
  const profile=fs.mkdtempSync(path.join(os.tmpdir(),'lume-cookie-fixture-'));fs.chmodSync(profile,0o700);fs.mkdirSync(path.join(profile,'Default'),{mode:0o700});
  fs.writeFileSync(path.join(profile,'Default/Preferences'),JSON.stringify({session:{restore_on_startup:1}}),{mode:0o600});
  fs.writeFileSync(path.join(profile,'LumePrivateProfile.json'),JSON.stringify({kind:'synthetic-cookie-fixture'}),{mode:0o600});
  let browser;
  try{
    browser=await startOwnedChrome(profile,executable);
    const expiry=Date.now()/1000+3600;
    await browser.call('Storage.setCookies',{cookies:[
      {name:'fixture_session',value:'synthetic-only',url:'https://fixture.invalid/',path:'/',secure:true,httpOnly:true,sameSite:'Strict',sourceScheme:'Secure',sourcePort:443},
      {name:'fixture_persistent',value:'synthetic-only',domain:'.fixture.invalid',path:'/scope',secure:true,httpOnly:false,sameSite:'None',expires:expiry,sourceScheme:'Secure',sourcePort:443},
      // CDP's explicit sourceScheme records an HTTPS setting origin without
      // changing this cookie's intentionally absent Secure attribute.
      {name:'fixture_https_nonsecure',value:'synthetic-only',url:'http://fixture.invalid/',path:'/source',secure:false,httpOnly:true,sameSite:'Lax',expires:expiry,sourceScheme:'Secure',sourcePort:443}
    ]});
    const expected=(await browser.call('Storage.getCookies')).cookies;
    if(expected.length!==3||expected.filter(c=>c.session).length!==1||!expected.some(c=>c.domain==='fixture.invalid')||!expected.some(c=>c.domain==='.fixture.invalid')||!expected.some(c=>c.name==='fixture_https_nonsecure'&&c.domain==='fixture.invalid'&&c.sourceScheme==='Secure'&&c.secure===false&&c.httpOnly===true))throw new TransferError('synthetic-cookie-setup-failed');
    await browser.call('Storage.clearCookies');await browser.call('Storage.setCookies',{cookies:expected.map(toCookieParam)});
    if(!sameCookies(expected,(await browser.call('Storage.getCookies')).cookies))throw new TransferError('synthetic-cookie-roundtrip-failed');
    await browser.close();browser=null;
    browser=await startOwnedChrome(profile,executable);
    if(!sameCookies(expected,(await browser.call('Storage.getCookies')).cookies))throw new TransferError('synthetic-session-restore-failed');
    return {passed:true,fixture:'synthetic cookies only; no user profile or Keychain CLI accessed',sessionCookiesPreserved:1,persistentCookiesPreserved:2,hostOnlyAndDomainScopePreserved:true,httpsOriginNonSecureCookiePreserved:true,cleanReopenVerified:true};
  }finally{await browser?.close();fs.rmSync(profile,{recursive:true,force:true});}
}

async function transfer(executable){
  if(process.platform!=='darwin')throw new TransferError('same-mac-required');
  const copied=python('clone');let browser;
  try{
    browser=await startOwnedChrome(copied.chromeProfile,executable);
    const available=(await browser.call('Storage.getCookies')).cookies;
    const validation=python('validate',available);
    const expected=validation.acceptedIndices.map(index=>available[index]);
    if(available.length!==expected.length||expected.length!==copied.copiedRows||expected.filter(c=>c.session).length!==copied.sessionCookies)throw new TransferError('scoped-cookies-did-not-load-completely');
    // The scoped SQLite clone already contains exactly the accepted set.
    // Do not clear and reinsert real cookies: CDP may normalize creation/source
    // metadata or expiry. Keep the original rows and verify a normal reopen.
    if(!sameCookies(expected,(await browser.call('Storage.getCookies')).cookies))throw new TransferError('selected-cookie-read-mismatch');
    await browser.close();browser=null;
    browser=await startOwnedChrome(copied.chromeProfile,executable);
    if(!sameCookies(expected,(await browser.call('Storage.getCookies')).cookies))throw new TransferError('selected-session-restore-mismatch');
    await browser.close();browser=null;
    const persisted=python('write',expected);
    return {status:'profiles-prepared',chromeProfile:copied.chromeProfile,lumeProfile:persisted.lumeProfile,matchedCookies:expected.length,sessionCookies:copied.sessionCookies,persistentCookies:copied.persistentCookies,cleanReopenVerified:true,originalCookieRowsPreserved:true,realCookieMutationAPIsUsed:false,authenticationVerified:false,keychainAccess:'installed Chrome normal app access; OS policy unchanged',cookieValuesInReport:false,sourceProfileModified:false};
  }finally{await browser?.close();}
}

async function main(){
  process.umask(0o077);
  const executable=process.env.LUME_CHROME_EXECUTABLE||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
  if(process.argv[2]==='--fixture')console.log(JSON.stringify(await fixture(executable)));
  else if(process.argv[2]==='--apply')console.log(JSON.stringify(await transfer(executable)));
  else throw new TransferError('explicit-fixture-or-apply-required');
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
  main().catch(error=>{console.log(JSON.stringify({status:'pending-or-unsupported',reason:error instanceof TransferError?error.code:'unexpected-browser-bridge-error',authenticationVerified:false,cookieValuesInReport:false}));process.exitCode=2;});
}
