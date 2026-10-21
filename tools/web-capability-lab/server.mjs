import http from 'node:http';
import https from 'node:https';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT=path.dirname(fileURLToPath(import.meta.url));
const TYPES={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.js':'text/javascript; charset=utf-8','.json':'application/json; charset=utf-8','.wasm':'application/wasm'};
const ALLOWED_FILES=new Set(['index.html','style.css','app.mjs','core.mjs','iframe-probe.html','iframe-probe.mjs','frame.css']);

export function websocketFrame(payload,opcode=1){
  const bytes=Buffer.isBuffer(payload)?payload:Buffer.from(payload);
  const header=bytes.length<126?Buffer.from([0x80|opcode,bytes.length]):Buffer.from([0x80|opcode,126,bytes.length>>8,bytes.length&255]);
  return Buffer.concat([header,bytes]);
}

function attachWebSocket(socket,head){
  let pending=Buffer.from(head);let closed=false;
  const end=(code=1000)=>{if(closed)return;closed=true;const b=Buffer.alloc(2);b.writeUInt16BE(code);socket.end(websocketFrame(b,8));};
  function consume(){
    while(!closed&&pending.length>=2){
      const first=pending[0],second=pending[1],opcode=first&15;
      if(!(first&128)||(first&112)||!(second&128)){end(1002);return;}
      let length=second&127,offset=2;
      if(length===126){if(pending.length<4)return;length=pending.readUInt16BE(2);offset=4;}
      if(length===127||length>65535){end(1009);return;}
      if(opcode>=8&&length>125){end(1002);return;}
      if(pending.length<offset+4+length)return;
      const mask=pending.subarray(offset,offset+4);offset+=4;const data=Buffer.alloc(length);
      for(let i=0;i<length;i++)data[i]=pending[offset+i]^mask[i%4];
      pending=pending.subarray(offset+length);
      if(opcode===8){end();return;}
      if(opcode===9)socket.write(websocketFrame(data,10));
      else if(opcode===1||opcode===2)socket.write(websocketFrame(data,opcode));
      else if(opcode!==10){end(1002);return;}
    }
  }
  socket.on('data',chunk=>{if(pending.length+chunk.length>131072){end(1009);return;}pending=Buffer.concat([pending,chunk]);consume();});
  socket.on('error',()=>{closed=true;socket.destroy();});socket.setTimeout(30000,()=>end());consume();
}

export function createLabServer({root=ROOT,tls=null,certificateFile=path.join(ROOT,'output','wt-certificate.json')}={}){
  const handler=(request,response)=>{
    const headers={'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer','Permissions-Policy':'camera=(self), microphone=(self), geolocation=(self)','Content-Security-Policy':"default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; connect-src 'self' ws://localhost:* ws://127.0.0.1:* wss://localhost:* wss://127.0.0.1:* https://localhost:4433 https://127.0.0.1:4433; img-src 'self' blob:; media-src 'self' blob:; frame-src 'self'; object-src 'none'; frame-ancestors 'self'"};
    const send=(status,body,type='text/plain; charset=utf-8')=>{response.writeHead(status,{...headers,'Content-Type':type});response.end(body);};
    if(request.method!=='GET'&&request.method!=='HEAD'){send(405,'Method not allowed');return;}
    let pathname;try{pathname=decodeURIComponent(new URL(request.url,'http://localhost').pathname);}catch{send(400,'Malformed URL');return;}
    if(pathname==='/health'){send(200,JSON.stringify({ok:true,service:'lume-capability-lab'}),'application/json');return;}
    if(pathname==='/api/config'){
      let wt={available:false};try{const saved=JSON.parse(fs.readFileSync(certificateFile,'utf8'));if(Array.isArray(saved.certificateHash)&&saved.certificateHash.length===32&&saved.expiresAt>Date.now())wt={available:true,url:'https://127.0.0.1:4433/echo',certificateHash:saved.certificateHash,serverTransport:'HTTP/3 only',expiresAt:saved.expiresAt};}catch{}
      send(200,JSON.stringify({webtransport:wt}),'application/json');return;
    }
    const file=pathname==='/'?'index.html':pathname.slice(1);
    if(!ALLOWED_FILES.has(file)){send(404,'Not found');return;}
    fs.readFile(path.join(root,file),(error,body)=>{if(error){send(404,'Not found');return;}response.writeHead(200,{...headers,'Content-Type':TYPES[path.extname(file)]||'application/octet-stream'});response.end(request.method==='HEAD'?undefined:body);});
  };
  const server=tls?https.createServer(tls,handler):http.createServer(handler);
  server.on('upgrade',(request,socket,head)=>{
    const origin=request.headers.origin;const ownOrigin=`${tls?'https':'http'}://${request.headers.host}`;const key=request.headers['sec-websocket-key'];
    if(request.url!=='/echo'||(origin&&origin!==ownOrigin)||request.headers['sec-websocket-version']!=='13'||!key||Buffer.from(key,'base64').length!==16){socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');return;}
    const accept=crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);attachWebSocket(socket,head);
  });
  return server;
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const port=Number(process.env.LUME_LAB_PORT||8765);const host=process.env.LUME_LAB_HOST||'127.0.0.1';
  const tls=process.env.LUME_LAB_TLS_CERT&&process.env.LUME_LAB_TLS_KEY?{cert:fs.readFileSync(process.env.LUME_LAB_TLS_CERT),key:fs.readFileSync(process.env.LUME_LAB_TLS_KEY)}:null;
  const server=createLabServer({tls});server.listen(port,host,()=>process.stdout.write(`Lume capability lab: ${tls?'https':'http'}://${host}:${port}\n`));
  for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>server.close(()=>process.exit(0)));
}
