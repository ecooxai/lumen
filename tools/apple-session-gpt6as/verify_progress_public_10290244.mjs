import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import tls from 'node:tls';
import {pathToFileURL} from 'node:url';
import {performance} from 'node:perf_hooks';

const root=process.cwd();
const taskId=process.env.LUME_TASK_ID||'10290244';
assert.match(taskId,/^[A-Za-z0-9_-]{1,64}$/);
const output=path.join(root,`output/session-${taskId}`);
const state=path.join(output,'progress-state');
const {decodeFrames}=await import(pathToFileURL(path.join(root,'tools/apple-web-lab-gpt6as/server.mjs')));
const read=async filename=>JSON.parse(await fs.readFile(filename,'utf8'));
const receipt=await read(path.join(state,'server-receipt.json'));
const origin=receipt.publicURL;
assert.ok(origin?.startsWith('https://'),'A verified public HTTPS origin is required');
const privateClient=path.join(state,'verification-client.json');

async function request(endpoint,options={}){
  const response=await fetch(origin+endpoint,{...options,signal:AbortSignal.timeout(15000)});
  const text=await response.text();
  assert.ok(response.ok,`HTTP ${response.status} for ${endpoint.split('?')[0]}`);
  return JSON.parse(text);
}

async function websocketProbe(){
  const url=new URL(origin),key=crypto.randomBytes(16).toString('base64');
  const expected=crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  const socket=tls.connect({host:url.hostname,port:Number(url.port||443),servername:url.hostname});
  let timeout;
  try{
    return await new Promise((resolve,reject)=>{
      let buffer=Buffer.alloc(0),upgraded=false;
      const samples=[],roundTrips=[];let nonce,sent;
      const ping=()=>{
        nonce=crypto.randomUUID();sent=performance.now();
        const payload=Buffer.from(JSON.stringify({type:'ping',nonce,clientTime:sent}));
        assert.ok(payload.length<126);
        const mask=crypto.randomBytes(4),data=Buffer.from(payload);
        for(let i=0;i<data.length;i++)data[i]^=mask[i%4];
        socket.write(Buffer.concat([Buffer.from([0x81,0x80|data.length]),mask,data]));
      };
      const finish=()=>{
        if(roundTrips.length>=2&&samples.length>=2){
          resolve({tlsCertificateValidated:socket.authorized,handshakeAcceptVerified:true,
            websocketRoundTripMs:roundTrips,sampleTimestamps:samples,
            observedSampleIntervalSeconds:(Date.parse(samples.at(-1))-Date.parse(samples.at(-2)))/1000});
        }
      };
      timeout=setTimeout(()=>reject(Error('Public WebSocket did not deliver two live samples and nonce echoes')),12000);
      socket.once('error',reject);
      socket.once('secureConnect',()=>{
        socket.write(`GET /live HTTP/1.1\r\nHost: ${url.host}\r\nOrigin: ${origin}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${key}\r\n\r\n`);
      });
      socket.on('data',chunk=>{
        try{
          buffer=Buffer.concat([buffer,chunk]);
          if(!upgraded){
            const end=buffer.indexOf('\r\n\r\n');if(end<0)return;
            const headers=buffer.subarray(0,end).toString();
            assert.match(headers,/^HTTP\/1\.1 101/);
            const accept=headers.split('\r\n').find(line=>/^sec-websocket-accept:/i.test(line))?.split(':').slice(1).join(':').trim();
            assert.equal(accept,expected);buffer=buffer.subarray(end+4);upgraded=true;ping();
          }
          const decoded=decodeFrames(buffer,{requireMask:false});buffer=decoded.remaining;
          for(const frame of decoded.frames){
            if(frame.opcode===8)throw Error('Live endpoint closed the WebSocket during verification');
            if(frame.opcode!==1)continue;
            const value=JSON.parse(frame.payload.toString());
            if(value.type==='pong'&&value.nonce===nonce){roundTrips.push(Number((performance.now()-sent).toFixed(3)));if(roundTrips.length<2)ping();}
            if(value.type==='snapshot'&&value.value?.metrics?.sampledAt){
              const stamp=value.value.metrics.sampledAt;
              if(!samples.includes(stamp))samples.push(stamp);
            }
            finish();
          }
        }catch(error){reject(error);}
      });
    });
  }finally{clearTimeout(timeout);socket.destroy();}
}

const mode=process.argv[2]||'probe';
const result={schema:1,mode,startedUTC:new Date().toISOString(),publicURL:origin,passed:false};
try{
  if(mode==='reply-check'){
    const client=await read(privateClient);
    const value=await request(`/api/message/${client.id}?key=${encodeURIComponent(client.key)}`);
    assert.equal(value.text,client.text);
    assert.ok(typeof value.reply==='string'&&value.reply.trim().length>0,'Agent has not replied to the verification message');
    result.messageId=value.id;result.agentReplyObserved=true;result.reply=value.reply;
  }else if(mode==='probe'){
    const health=await request('/api/health');assert.equal(health.taskId,taskId);assert.equal(health.ok,true);
    const progress=await request('/api/progress');assert.equal(progress.status.taskId,taskId);
    assert.equal(progress.status.fullOfficialWptPassPercent,null);
    const localStatus=await read(path.join(state,'status.json'));
    assert.deepEqual(progress.status.wpt,localStatus.wpt);
    assert.deepEqual(progress.status.prioritySummary,localStatus.prioritySummary);
    result.wpt={passed:progress.status.wpt.passed,total:progress.status.wpt.total,scope:progress.status.wpt.scope,binarySha256:progress.status.wpt.binarySha256};
    result.prioritySummary=progress.status.prioritySummary;
    result.websocket=await websocketProbe();
    const manifest=await read(path.join(output,'public/manifest.json'));
    const download=await fetch(origin+'/src.tar.gz',{signal:AbortSignal.timeout(60000)});
    assert.equal(download.status,200);
    const hash=crypto.createHash('sha256');let bytes=0;
    for await(const chunk of download.body){bytes+=chunk.length;assert.ok(bytes<=manifest.bytes);hash.update(chunk);}
    assert.equal(bytes,manifest.bytes);assert.equal(hash.digest('hex'),manifest.sha256);
    result.publicSourceArchive={bytes,sha256:manifest.sha256,sourceCommit:manifest.sourceCommit,fullByteHashVerified:true};
    const text=`Automated delivery check for task ${taskId}: please confirm you received this progress-page message and are continuing WPT and Apple performance work.`;
    let message;
    try{
      message=await read(privateClient);
      assert.equal(message.text,text);
      const stored=await request(`/api/message/${message.id}?key=${encodeURIComponent(message.key)}`);
      assert.equal(stored.text,text);
      result.existingVerificationMessageReused=true;
    }catch(error){
      if(error.code!=='ENOENT')throw error;
      const token=(await fs.readFile(path.join(state,'message-access-key'),'utf8')).trim();
      message=await request('/api/message',{method:'POST',headers:{origin,'content-type':'application/json',authorization:'Bearer '+token},body:JSON.stringify({text})});
      await fs.writeFile(privateClient,JSON.stringify({...message,text})+'\n',{mode:0o600,flag:'wx'});
    }
    result.messageId=message.id;result.messageStoredForAgentReview=true;result.privateKeysOmitted=true;
  }else{throw Error('Expected probe or reply-check');}
  result.passed=true;
}catch(error){result.error=error.name+': '+error.message;}
result.finishedUTC=new Date().toISOString();
await fs.writeFile(path.join(output,'progress-public-'+mode+'.json'),JSON.stringify(result,null,2)+'\n');
process.stdout.write(JSON.stringify(result,null,2)+'\n');
process.exitCode=result.passed?0:1;
