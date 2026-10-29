import http from 'node:http';
import crypto from 'node:crypto';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createMessageStore,createSampler,readJson} from './model.mjs';
import {encodeFrame,decodeFrames} from '../apple-web-lab-gpt6as/server.mjs';

const HERE=path.dirname(fileURLToPath(import.meta.url));
const STATIC={'/':'index.html','/app.js':'app.js','/style.css':'style.css','/config.json':'config.json'};
const MIME={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json; charset=utf-8','.gz':'application/gzip'};
const EMPTY_STATUS={taskId:'10290244',phase:'Awaiting a verified status receipt',iteration:0,fullOfficialWptPassPercent:null,wpt:null,apple:{sameIpa:'unverified'}};

function equalSecret(actual,expected) {
  if (typeof actual!=='string'||typeof expected!=='string'||!expected) return false;
  const a=crypto.createHash('sha256').update(actual).digest();
  const b=crypto.createHash('sha256').update(expected).digest();
  return crypto.timingSafeEqual(a,b);
}

export function createProgressServer(options={}) {
  const taskId=String(options.taskId||'10290244');
  if(!/^[A-Za-z0-9_-]{1,64}$/.test(taskId))throw Error('Invalid task identifier');
  const messageSessionId=String(options.messageSessionId||taskId);
  if(!/^[A-Za-z0-9_-]{1,64}$/.test(messageSessionId))throw Error('Invalid message session identifier');
  const emptyStatus={...EMPTY_STATUS,taskId};
  const stateDir=options.stateDir||path.resolve(HERE,`../../output/session-${taskId}/progress-state`);
  const publicDir=options.publicDir||path.join(HERE,'public');
  const store=createMessageStore(stateDir);
  const sample=createSampler();
  const allowedOrigins=new Set(options.allowedOrigins||[]);
  const peers=new Set();
  const rate=new Map();
  let metrics=null,sampling=false,timer;
  const originAllowed=req=>{
    try {const origin=new URL(req.headers.origin);return ['http:','https:'].includes(origin.protocol)&&(origin.host===req.headers.host||allowedOrigins.has(origin.origin));}
    catch {return false;}
  };
  const headers=req=>{
    const h={'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer','X-Frame-Options':'DENY','Content-Security-Policy':"default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self' https: wss:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'"};
    if (originAllowed(req)) {h['Access-Control-Allow-Origin']=req.headers.origin;h.Vary='Origin';}
    return h;
  };
  const json=(req,res,code,value)=>{res.writeHead(code,{...headers(req),'Content-Type':'application/json; charset=utf-8'});res.end(JSON.stringify(value));};
  const snapshot=async()=>{
    const status=await readJson(path.join(stateDir,'status.json'),emptyStatus);
    const scorecard=options.scorecard?await readJson(options.scorecard,null):null;
    return {schema:1,serverTime:new Date().toISOString(),refreshSeconds:2,metrics,
      status:status?.taskId===taskId?status:{...emptyStatus,phase:'Status identity mismatch; awaiting a matching receipt'},
      scorecard:scorecard?.task_id===taskId?scorecard:null};
  };
  const refresh=async()=>{
    if (sampling) return;
    sampling=true;
    try {
      metrics=await sample();
      if (peers.size) {
        const frame=encodeFrame(1,Buffer.from(JSON.stringify({type:'snapshot',value:await snapshot()})));
        for (const peer of peers) {
          if (peer.destroyed||peer.writableLength>131072) {peer.destroy();continue;}
          peer.write(frame);
        }
      }
    } finally {sampling=false;}
  };
  const server=http.createServer(async(req,res)=>{
    let url;
    try {url=new URL(req.url,'http://localhost');if(decodeURIComponent(url.pathname).includes('..')||decodeURIComponent(url.pathname).includes('\\')||decodeURIComponent(url.pathname).includes('\0'))return json(req,res,403,{error:'Forbidden'});}
    catch {return json(req,res,400,{error:'Bad URL'});}
    if (req.method==='OPTIONS') {
      if (!originAllowed(req)) return json(req,res,403,{error:'Origin rejected'});
      res.writeHead(204,{...headers(req),'Access-Control-Allow-Methods':'GET, POST','Access-Control-Allow-Headers':'Content-Type, Authorization','Access-Control-Max-Age':'600'});return res.end();
    }
    if(req.method==='GET'&&url.pathname==='/api/health')return json(req,res,200,{ok:true,taskId,live:'/live',messages:'A private access link is required. Messages are data, never commands.'});
    if(req.method==='GET'&&url.pathname==='/api/progress')return json(req,res,200,await snapshot());
    if(req.method==='GET'&&url.pathname==='/config.json')return json(req,res,200,{liveBase:'',taskId,...(messageSessionId===taskId?{}:{messageSessionId})});
    if(req.method==='GET'&&/^\/api\/message\/[a-f0-9-]{36}$/.test(url.pathname)) {
      const value=await store.read(url.pathname.split('/').at(-1),url.searchParams.get('key'));
      return json(req,res,value?200:404,value||{error:'Not found'});
    }
    if(req.method==='POST'&&url.pathname==='/api/message') {
      if (!originAllowed(req))return json(req,res,403,{error:'Origin rejected'});
      if(!options.messageToken||!equalSecret(req.headers.authorization,'Bearer '+options.messageToken))return json(req,res,403,{error:'Open the private access link to send a message'});
      if((req.headers['content-type']||'').split(';')[0].trim()!=='application/json')return json(req,res,415,{error:'JSON required'});
      const key=req.socket.remoteAddress||'unknown',now=Date.now();
      const recent=(rate.get(key)||[]).filter(t=>now-t<60000);
      if(recent.length>=10)return json(req,res,429,{error:'Please wait before sending another message'});
      recent.push(now);rate.set(key,recent);
      if(rate.size>1024)for(const [address,times] of rate)if(!times.some(t=>now-t<60000))rate.delete(address);
      let bytes=0;const chunks=[];
      try {
        for await(const chunk of req) {
          bytes+=chunk.length;
          if(bytes>8192){json(req,res,413,{error:'Message too large'});return;}
          chunks.push(chunk);
        }
        return json(req,res,201,await store.submit(JSON.parse(Buffer.concat(chunks).toString('utf8')).text));
      } catch(error) {return json(req,res,400,{error:error.message.startsWith('Message')?error.message:'Invalid message'});}
    }
    if(req.method!=='GET'&&req.method!=='HEAD')return json(req,res,405,{error:'Method not allowed'});
    const selected=STATIC[url.pathname];
    const filename=selected?path.join(publicDir,selected):url.pathname==='/src.tar.gz'?options.sourceArchive:null;
    if(!filename)return json(req,res,404,{error:'Not found'});
    try {
      const stat=await fsp.stat(filename);
      if(!stat.isFile())throw Error('Not a file');
      res.writeHead(200,{...headers(req),'Content-Type':MIME[path.extname(filename)]||'application/octet-stream','Content-Length':stat.size});
      if(req.method==='HEAD')return res.end();
      const stream=fs.createReadStream(filename);stream.on('error',()=>res.destroy());stream.pipe(res);
    } catch {return json(req,res,404,{error:'Not found'});}
  });
  server.on('upgrade',(req,socket,head)=>{
    const key=req.headers['sec-websocket-key'];
    if(req.url!=='/live'||!originAllowed(req)||req.headers['sec-websocket-version']!=='13'||typeof key!=='string'||Buffer.from(key,'base64').length!==16){socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');return;}
    const accept=crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    peers.add(socket);let buffer=Buffer.alloc(0),messageWindow=Date.now(),messageCount=0;
    socket.on('close',()=>peers.delete(socket));socket.on('error',()=>socket.destroy());
    socket.setTimeout(30000,()=>socket.destroy());
    const receive=data=>{
      try {
        buffer=Buffer.concat([buffer,data]);
        if(buffer.length>65550)throw Error('Too much input');
        const parsed=decodeFrames(buffer);buffer=parsed.remaining;
        for(const frame of parsed.frames){
          if(Date.now()-messageWindow>1000){messageWindow=Date.now();messageCount=0;}
          if(++messageCount>10)throw Error('Message rate exceeded');
          if(frame.opcode===8){socket.end(encodeFrame(8,frame.payload));return;}
          if(frame.opcode===9){socket.write(encodeFrame(10,frame.payload));continue;}
          if(frame.opcode===10)continue;
          if(frame.opcode!==1||!frame.final||frame.payload.length>2048)throw Error('Unsupported message');
          const value=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(frame.payload));
          if(value.type==='ping'&&typeof value.nonce==='string'&&value.nonce.length<=80&&Number.isFinite(value.clientTime)){
            socket.write(encodeFrame(1,Buffer.from(JSON.stringify({type:'pong',nonce:value.nonce,clientTime:value.clientTime,serverTime:Date.now()}))));
          }
        }
      } catch {socket.end(encodeFrame(8,Buffer.from([3,234])));}
    };
    socket.on('data',receive);if(head.length)receive(head);
    snapshot().then(value=>{if(!socket.destroyed)socket.write(encodeFrame(1,Buffer.from(JSON.stringify({type:'snapshot',value}))));});
  });
  server.on('listening',()=>{refresh().catch(()=>{});timer=setInterval(()=>refresh().catch(()=>{}),options.sampleInterval||2000);timer.unref();});
  server.on('close',()=>{clearInterval(timer);for(const peer of peers)peer.destroy();});
  server.closeProgress=()=>{for(const peer of peers)peer.destroy();server.closeAllConnections();server.close();};
  return server;
}

if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
  const taskId=process.env.LUME_TASK_ID||'10290244';
  const stateDir=path.resolve(process.env.LUME_PROGRESS_STATE||path.join(HERE,`../../output/session-${taskId}/progress-state`));
  await fsp.mkdir(stateDir,{recursive:true});
  const keyFile=path.join(stateDir,'message-access-key');
  let messageToken;
  try{messageToken=(await fsp.readFile(keyFile,'utf8')).trim();}
  catch{messageToken=crypto.randomBytes(24).toString('hex');await fsp.writeFile(keyFile,messageToken+'\n',{mode:0o600,flag:'wx'});}
  const server=createProgressServer({taskId,messageSessionId:process.env.LUME_MESSAGE_SESSION_ID||taskId,stateDir,messageToken,scorecard:process.env.LUME_SCORECARD?path.resolve(process.env.LUME_SCORECARD):path.resolve(HERE,`../../agent/scorecard-${taskId}.json`),sourceArchive:process.env.LUME_SOURCE_ARCHIVE?path.resolve(process.env.LUME_SOURCE_ARCHIVE):undefined,allowedOrigins:(process.env.LUME_ALLOWED_ORIGINS||'').split(',').filter(Boolean)});
  const port=Number(process.env.PORT||18784);
  server.listen(port,'127.0.0.1',()=>process.stdout.write(`Lume progress listening on http://127.0.0.1:${port}; private message key stored outside published files.\n`));
  process.on('SIGTERM',()=>server.closeProgress());process.on('SIGINT',()=>server.closeProgress());
}
