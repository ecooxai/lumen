import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import tls from 'node:tls';
import {performance} from 'node:perf_hooks';
import {pathToFileURL} from 'node:url';

const task=process.env.LUME_TASK_ID;
assert.match(task||'',/^[A-Za-z0-9_-]{1,64}$/);
const root=process.cwd(),state=path.join(root,'output','session-'+task,'progress-state');
const origin=process.env.LUME_PROGRESS_ORIGIN;
assert.ok(origin?.startsWith('https://'));
const {decodeFrames}=await import(pathToFileURL(path.join(root,'tools/apple-web-lab-gpt6as/server.mjs')));
const {createMessageStore}=await import(pathToFileURL(path.join(root,'tools/lume-progress-10290244/model.mjs')));
const result={taskId:task,startedUTC:new Date().toISOString(),publicURL:origin,passed:false};
let token,receiptKey;
async function request(endpoint,options={}){
  const response=await fetch(origin+endpoint,{...options,signal:AbortSignal.timeout(12000)});
  assert.ok(response.ok,'Expected a successful response from the task endpoint');
  return response.json();
}
async function websocketProbe(){
  const url=new URL(origin),key=crypto.randomBytes(16).toString('base64');
  const expected=crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  const socket=tls.connect({host:url.hostname,port:Number(url.port||443),servername:url.hostname});
  let timer;
  try{return await new Promise((resolve,reject)=>{
    let buffer=Buffer.alloc(0),upgraded=false,nonce,sent;
    const samples=[],roundTrips=[];
    const ping=()=>{
      nonce=crypto.randomUUID();sent=performance.now();
      const payload=Buffer.from(JSON.stringify({type:'ping',nonce,clientTime:sent}));
      const mask=crypto.randomBytes(4),data=Buffer.from(payload);
      for(let i=0;i<data.length;i++)data[i]^=mask[i%4];
      socket.write(Buffer.concat([Buffer.from([0x81,0x80|data.length]),mask,data]));
    };
    timer=setTimeout(()=>reject(Error('Live WebSocket did not deliver distinct samples and nonce echoes')),14000);
    socket.once('error',reject);
    socket.once('secureConnect',()=>socket.write(`GET /live HTTP/1.1\r\nHost: ${url.host}\r\nOrigin: ${origin}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${key}\r\n\r\n`));
    socket.on('data',chunk=>{
      try{
        buffer=Buffer.concat([buffer,chunk]);
        if(!upgraded){
          const end=buffer.indexOf('\r\n\r\n');if(end<0)return;
          const headers=buffer.subarray(0,end).toString();assert.match(headers,/^HTTP\/1\.1 101/);
          const accept=headers.split('\r\n').find(line=>/^sec-websocket-accept:/i.test(line))?.split(':').slice(1).join(':').trim();
          assert.equal(accept,expected);buffer=buffer.subarray(end+4);upgraded=true;ping();
        }
        const decoded=decodeFrames(buffer,{requireMask:false});buffer=decoded.remaining;
        for(const frame of decoded.frames){
          if(frame.opcode===8)throw Error('WebSocket closed before continuity verification');
          if(frame.opcode!==1)continue;
          const value=JSON.parse(frame.payload.toString());
          if(value.type==='pong'&&value.nonce===nonce){roundTrips.push(Number((performance.now()-sent).toFixed(3)));if(roundTrips.length<2)ping();}
          if(value.type==='snapshot'){
            assert.equal(value.value?.status?.taskId,task,'WebSocket task identity differs');
            const metrics=value.value.metrics;
            if(metrics?.sampledAt&&!samples.includes(metrics.sampledAt)){
              assert.ok(Number.isFinite(metrics.memory?.totalGB),'Missing measured host memory');
              assert.ok(Array.isArray(metrics.disks),'Missing measured volume inventory');
              samples.push(metrics.sampledAt);
            }
          }
          if(roundTrips.length>=2&&samples.length>=2){
            const delta=(Date.parse(samples.at(-1))-Date.parse(samples.at(-2)))/1000;
            assert.ok(delta>=1.5&&delta<=3,'Live refresh interval is outside the declared two-second cadence');
            resolve({tlsCertificateValidated:socket.authorized,handshakeAcceptVerified:true,taskIdentityVerified:true,
              sampleTimestamps:samples,observedSampleIntervalSeconds:delta,publicRoundTripMs:roundTrips,
              latencyScope:'Measured round trip from the Mac verifier through the public route; the browser UI independently measures its own client round trip.'});
          }
        }
      }catch(error){reject(error);}
    });
  });}finally{clearTimeout(timer);socket.destroy();}
}
try{
  const health=await request('/api/health');assert.equal(health.taskId,task);assert.equal(health.ok,true);
  const config=await request('/config.json');assert.equal(config.taskId,task);
  result.messageSessionId=config.messageSessionId||config.taskId;
  assert.equal(result.messageSessionId,process.env.LUME_MESSAGE_SESSION_ID||task,'Private client namespace changed during task handoff');
  const progress=await request('/api/progress');assert.equal(progress.status.taskId,task);assert.equal(progress.status.fullOfficialWptPassPercent,null);
  result.websocket=await websocketProbe();
  token=(await fs.readFile(path.join(state,'message-access-key'),'utf8')).trim();
  const clientPath=path.join(state,'continuity-verification-client.json');
  const text='Automated continuity verification for Lume task '+task+'. Confirm the current task can receive and answer its private progress messages.';
  let client;
  try{client=JSON.parse(await fs.readFile(clientPath,'utf8'));assert.equal(client.text,text);}
  catch(error){
    if(error.code!=='ENOENT')throw error;
    client={...await request('/api/message',{method:'POST',headers:{origin,'content-type':'application/json',authorization:'Bearer '+token},body:JSON.stringify({text})}),text};
    await fs.writeFile(clientPath,JSON.stringify(client)+'\n',{mode:0o600,flag:'wx'});
  }
  receiptKey=client.key;
  const store=createMessageStore(state);
  const pending=await store.pending();assert.ok(pending.some(row=>row.id===client.id)||client.replied,'Verification message not present in task inbox');
  const reply='Automated engineering verification received this message for task '+task+'. The private inbox and reply path are connected; the active engineering agent reviews user messages between checkpoints.';
  await store.reply(client.id,reply);
  const delivered=await request('/api/message/'+client.id+'?key='+encodeURIComponent(client.key));
  assert.equal(delivered.text,text);assert.equal(delivered.reply,reply);
  client.replied=true;await fs.writeFile(clientPath,JSON.stringify(client)+'\n',{mode:0o600});
  result.message={id:client.id,stored:true,replyObserved:true,privateKeysOmitted:true};
  assert.ok(!JSON.stringify(progress).includes(token),'A private capability appeared in public progress');
  result.passed=true;
}catch(error){
  let message=error.name+': '+error.message;
  if(token)message=message.replaceAll(token,'[private capability omitted]');
  if(receiptKey)message=message.replaceAll(receiptKey,'[private receipt key omitted]');
  result.error=message.slice(0,600);
}
result.finishedUTC=new Date().toISOString();
await fs.writeFile(path.join(state,'continuity-public-verification.json'),JSON.stringify(result,null,2)+'\n',{mode:0o600});
process.stdout.write(JSON.stringify(result,null,2)+'\n');
process.exitCode=result.passed?0:1;
