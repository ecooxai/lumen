import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {WebSocketServer} from 'ws';
const base=path.dirname(fileURLToPath(import.meta.url)), runtime=path.join(base,'runtime'), site=path.join(base,'site');
fs.mkdirSync(runtime,{recursive:true,mode:0o700}); fs.chmodSync(runtime,0o700);
const secretPath=path.join(runtime,'viewer-token');
if(!fs.existsSync(secretPath)) fs.writeFileSync(secretPath,crypto.randomBytes(32).toString('hex'),{mode:0o600});
const secret=fs.readFileSync(secretPath,'utf8').trim();
const equal=s=>{if(typeof s!=='string')return false;const candidate=Buffer.from(s),expected=Buffer.from(secret);return candidate.length===expected.length&&crypto.timingSafeEqual(candidate,expected)};
const authorized=req=>equal((req.headers.authorization||'').replace(/^Bearer /,''));
function endpoints(){try{return JSON.parse(fs.readFileSync(path.join(runtime,'endpoints.json'),'utf8'))}catch{return {}}}
function originAllowed(origin){if(!origin)return true;try{const u=new URL(origin);return ['http://127.0.0.1:4187','http://localhost:4187',endpoints().tunnel,endpoints().surge].includes(u.origin)}catch{return false}}
function data(file,fallback){try{return JSON.parse(fs.readFileSync(path.join(base,file),'utf8'))}catch{return fallback}}
function messages(){try{return fs.readFileSync(path.join(runtime,'messages.jsonl'),'utf8').trim().split('\n').filter(Boolean).slice(-100).map(x=>JSON.parse(x))}catch{return []}}
function visibleMessages(isPrivate){return messages().filter(m=>isPrivate||m.public===true)}
function currentStatus(){
 const status=data('status.json',{}),run=data('../validation/linux-wpt-20261028/full-paired-checkpoint/state.json',null);
 if(run){
  const completed=Array.isArray(run.shards)?run.shards.length:0,active=run.current?.shard;
  status.full_baseline={state:run.status||'unknown',completed_shards:completed,total_shards:20,selected_urls:39394,current_shard:active||null,started_utc:run.started_utc||null};
  status.current_evidence=[...(status.current_evidence||[]),['Historical broad-run state',`${completed}/20 shards completed; ${run.status||'unknown'}. Historical8,000 and partial004 remain separate from guarded measurements.`]];
 }
 const guarded=data('../validation/linux-wpt-20261028/guarded-original-first2000/state.json',null);
 if(guarded){status.guarded_baseline={state:guarded.status,observed_urls:guarded.observed_urls,selected_urls:guarded.selected_urls,verified_spawns:guarded.verified_native_spawns,startup_failures:guarded.guard_startup_failures,finished_utc:guarded.finished_utc||null};status.current_evidence=[...(status.current_evidence||[]),['Guarded original baseline state',`${guarded.observed_urls||0}/${guarded.selected_urls||2000} URLs; ${guarded.status||'unknown'}; ${guarded.verified_native_spawns||0} verified launches, ${guarded.guard_startup_failures||0} startup failures. Separate measurement condition.`]];}
 return status;
}
function snapshot(isPrivate){return {type:'snapshot',status:currentStatus(),telemetry,messages:visibleMessages(isPrivate)}}
function append(message){const f=path.join(runtime,'messages.jsonl');if(fs.existsSync(f)&&fs.statSync(f).size>1048576)fs.writeFileSync(f,messages().slice(-50).map(x=>JSON.stringify(x)).join('\n')+'\n',{mode:0o600});fs.appendFileSync(f,JSON.stringify(message)+'\n',{mode:0o600})}
let previousCpu=null, telemetry={utc:new Date().toISOString(),cpu_percent:null,ram:{},disks:[]};
function sample(){
 try{
  const values=fs.readFileSync('/proc/stat','utf8').split('\n')[0].trim().split(/\s+/).slice(1,9).map(Number),total=values.reduce((a,b)=>a+b,0),idle=values[3]+values[4];
  let cpu=null;if(previousCpu&&total>previousCpu.total)cpu=100*(1-(idle-previousCpu.idle)/(total-previousCpu.total));previousCpu={total,idle};
  const mem=Object.fromEntries(fs.readFileSync('/proc/meminfo','utf8').trim().split('\n').map(l=>{let [k,v]=l.split(':');return [k,Number(v.trim().split(/\s+/)[0])*1024]}));
  const disks=[], seen=new Set();
  for(const line of fs.readFileSync('/proc/mounts','utf8').trim().split('\n')){
   const [device,mount,type]=line.split(' ');if(!device.startsWith('/dev/')||seen.has(device))continue;
   const m=mount.replace(/\\040/g,' ');try{const s=fs.statfsSync(m);seen.add(device);disks.push({device,mount:m,total_gb:s.blocks*s.bsize/1e9,used_gb:(s.blocks-s.bfree)*s.bsize/1e9,available_gb:s.bavail*s.bsize/1e9,type})}catch{}
  }
  telemetry={utc:new Date().toISOString(),cpu_percent:cpu,logical_cpus:os.cpus().length,ram:{total_gb:mem.MemTotal/1e9,used_gb:(mem.MemTotal-mem.MemAvailable)/1e9,available_gb:mem.MemAvailable/1e9,swap_total_gb:mem.SwapTotal/1e9,swap_used_gb:(mem.SwapTotal-mem.SwapFree)/1e9},disks};
 }catch(e){telemetry={...telemetry,error:'System telemetry unavailable',utc:new Date().toISOString()}}
}
function json(res,code,obj){res.writeHead(code,{'Content-Type':'application/json; charset=utf-8','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});res.end(JSON.stringify(obj))}
const server=http.createServer(async(req,res)=>{
 const origin=req.headers.origin;if(origin&&!originAllowed(origin))return json(res,403,{error:'Origin not allowed'});
 if(origin){res.setHeader('Access-Control-Allow-Origin',origin);res.setHeader('Vary','Origin');res.setHeader('Access-Control-Allow-Headers','Authorization, Content-Type');res.setHeader('Access-Control-Allow-Methods','GET, OPTIONS')}
 if(req.method==='OPTIONS'){res.writeHead(204);return res.end()}
 const url=new URL(req.url,'http://localhost');if(req.method!=='GET')return json(res,405,{error:'Method not allowed'});
 if(url.pathname==='/healthz')return json(res,200,{ok:true,service:'lumen-wpt-progress'});
 if(url.pathname==='/showcase.mp4'){const meta=data('runtime/showcase-manifest.json',null),f=path.join(runtime,'lumen-linux-gpt-6-astra-builtin-agents-rubywebterm-showcase.mp4');if(!meta?.reviewed_publication||!fs.existsSync(f)||fs.statSync(f).size!==meta.bytes)return json(res,404,{error:'Reviewed showcase not prepared'});res.writeHead(200,{'Content-Type':'video/mp4','Content-Length':meta.bytes,'Content-Disposition':'attachment; filename="lumen-linux-gpt-6-astra-builtin-agents-rubywebterm-showcase.mp4"','Cache-Control':'no-store'});return fs.createReadStream(f).pipe(res)}
 if(url.pathname.startsWith('/api/')||url.pathname==='/src.tar.gz'){
  if(url.pathname==='/api/status')return json(res,200,snapshot(authorized(req)));
  if(url.pathname==='/src.tar.gz'){const f=path.join(runtime,'src.tar.gz');if(!fs.existsSync(f))return json(res,404,{error:'Source archive not prepared'});res.writeHead(200,{'Content-Type':'application/gzip','Content-Disposition':'attachment; filename="lumen-linux-gpt-6-astra-builtin-agents-rubywebterm-src.tar.gz"','Cache-Control':'no-store'});return fs.createReadStream(f).pipe(res)}
  return json(res,404,{error:'Unknown endpoint'});
 }
 if(url.pathname==='/'||url.pathname==='/index.html'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-cache','X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer','Content-Security-Policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self' https: wss:; img-src 'self' data:; base-uri 'none'; frame-ancestors 'none'"});return fs.createReadStream(path.join(site,'index.html')).pipe(res)}
 if(url.pathname==='/config.js'){res.writeHead(200,{'Content-Type':'application/javascript','Cache-Control':'no-store'});return res.end('window.LUMEN_API="";')}
 return json(res,404,{error:'Not found'});
});
const wss=new WebSocketServer({noServer:true,maxPayload:4096,handleProtocols:p=>p.has('lumen-status')?'lumen-status':false});
server.on('upgrade',(req,socket,head)=>{
 const protocols=(req.headers['sec-websocket-protocol']||'').split(',').map(x=>x.trim());const key=protocols.find(x=>x.startsWith('auth.'))?.slice(5);
 if(req.url!=='/ws'||!originAllowed(req.headers.origin)||(key&&!equal(key))||wss.clients.size>=16){socket.write('HTTP/1.1 403 Forbidden\r\n\r\n');return socket.destroy()}
 wss.handleUpgrade(req,socket,head,ws=>{ws.isPrivate=equal(key);wss.emit('connection',ws,req)});
});
wss.on('connection',ws=>{
 let sent=[];ws.send(JSON.stringify(snapshot(ws.isPrivate)));
 ws.on('error',()=>{});ws.on('message',buf=>{
  let msg;try{msg=JSON.parse(buf.toString())}catch{return}
  if(msg.type==='ping'&&Number.isFinite(msg.client_ms))return ws.send(JSON.stringify({type:'pong',client_ms:msg.client_ms,server_utc:new Date().toISOString()}));
  if(msg.type==='message'&&typeof msg.text==='string'){
   sent=sent.filter(t=>Date.now()-t<10000);if(sent.length>=4)return ws.send(JSON.stringify({type:'error',text:'Please wait before sending more messages.'}));
   const body=msg.text.trim().slice(0,2000);if(!body)return;sent.push(Date.now());
   append({id:crypto.randomUUID(),utc:new Date().toISOString(),role:'user',public:!ws.isPrivate,text:body});broadcast();
  }
 });
});
function broadcast(){for(const ws of wss.clients)if(ws.readyState===1&&ws.bufferedAmount<65536)ws.send(JSON.stringify(snapshot(ws.isPrivate)))}
sample();setInterval(()=>{sample();broadcast()},2000);
const port=Number(process.env.LUMEN_PROGRESS_PORT||4187);server.listen(port,'127.0.0.1',()=>console.log(`Lumen progress listening at 127.0.0.1:${port}`));
for(const sig of ['SIGTERM','SIGINT'])process.on(sig,()=>{wss.close();server.close();setTimeout(()=>process.exit(0),250).unref()});
