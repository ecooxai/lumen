import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import net from 'node:net';
import crypto from 'node:crypto';
import {once} from 'node:events';
import {test} from 'node:test';
import {parseDisks, publicDisks, createMessageStore, cpuPercent} from '../model.mjs';
import {createProgressServer} from '../server.mjs';
import {decodeFrames} from '../../apple-web-lab-gpt6as/server.mjs';

test('disk measurements retain every mount and never sum shared APFS volumes', () => {
  const disks = parseDisks('Filesystem 1024-blocks Used Available Capacity Mounted on\n/dev/disk3s1 1000000 200000 800000 20% /\n/dev/disk3s5 1000000 400000 600000 40% /System/Volumes/Data\n/dev/disk4s1 500000 100000 400000 20% /Volumes/Test Disk\n');
  assert.equal(disks.length, 3);
  assert.equal(disks[0].totalGB, 1.024);
  assert.equal(disks[2].mount, '/Volumes/Test Disk');
  assert.equal(disks[2].availableGB, 0.4096);
  const visible=publicDisks([...disks,{filesystem:'//user:secret@private-host',mount:'/Users/private-name/private-data',totalGB:12,usedGB:8,availableGB:4,capacityPercent:67}]);
  assert.equal(visible.length,4);
  assert.equal(visible[0].mount,'/');
  assert.equal(visible[2].mount,'Private volume 1');
  assert.equal(visible[3].mount,'Private volume 2');
  assert.equal(visible[2].availableGB,disks[2].availableGB);
  assert.equal(visible[3].totalGB,12);
  assert.equal(JSON.stringify(visible).includes('secret'),false);
  assert.equal(JSON.stringify(visible).includes('private-name'),false);
  assert.equal(JSON.stringify(visible).includes('Test Disk'),false);
});

test('CPU is an interval measurement with an unavailable first sample', () => {
  assert.equal(cpuPercent(null, {idle:5,total:10}), null);
  assert.equal(cpuPercent({idle:5,total:10}, {idle:12,total:20}), 30);
  assert.equal(cpuPercent({idle:5,total:10}, {idle:5,total:10}), null);
});

test('message replies require the private receipt key and never execute message text', async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'lume-msg-'));
  try {
    const store = createMessageStore(dir);
    const receipt = await store.submit('$(touch SHOULD_NOT_EXIST) <script>alert(1)</script>');
    assert.equal((await store.pending()).length, 1);
    assert.equal(await store.read(receipt.id, 'wrong'), null);
    await store.reply(receipt.id, 'Received by agent.');
    const reply = await store.read(receipt.id, receipt.key);
    assert.equal(reply.reply, 'Received by agent.');
    assert.equal(reply.keyHash, undefined);
    assert.equal(reply.text, '$(touch SHOULD_NOT_EXIST) <script>alert(1)</script>');
    assert.equal((await store.pending()).length, 0);
    await assert.rejects(store.submit(' '), /between 1 and 2000/);
    await assert.rejects(store.submit('x'.repeat(2001)), /between 1 and 2000/);
    assert.equal(await store.read('../../secret', receipt.key), null);
  } finally {await fs.rm(dir, {recursive:true,force:true});}
});

test('public routes are allowlisted; inbox and filesystem secrets cannot be read', async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'lume-progress-'));
  const state = path.join(dir, 'state');
  await fs.mkdir(state);
  await fs.writeFile(path.join(dir, 'private-key.pem'), 'SECRET');
  const server = createProgressServer({stateDir:state, publicDir:path.resolve(import.meta.dirname,'../public'), sampleInterval:10000});
  server.listen(0,'127.0.0.1'); await once(server,'listening');
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    const health = await fetch(base+'/api/health');
    assert.equal(health.status, 200);
    const progress = await fetch(base+'/api/progress');
    assert.equal(progress.status, 200);
    assert.equal((await progress.json()).status.fullOfficialWptPassPercent, null);
    for (const p of ['/api/messages','/private-key.pem','/.git/config','/%2e%2e/private-key.pem','/%2e%2e%2fprivate-key.pem']) {
      const result = await fetch(base+p);
      assert.ok(result.status===403 || result.status===404);
      assert.ok(!(await result.text()).includes('SECRET'));
    }
    assert.equal((await fetch(base+'/src.tar.gz')).status,404);
    const missingKey=await fetch(base+'/api/message',{method:'POST',headers:{origin:base,authorization:'Bearer undefined','content-type':'application/json'},body:JSON.stringify({text:'must not be accepted'})});
    assert.equal(missingKey.status,403);
  } finally {server.closeAllConnections(); await new Promise(resolve=>server.close(resolve));await fs.rm(dir,{recursive:true,force:true});}
});

test('live WebSocket returns a real nonce echo for client measured round-trip time', async () => {
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'lume-ws-'));
  const server=createProgressServer({stateDir:dir,publicDir:path.resolve(import.meta.dirname,'../public'),sampleInterval:10000});
  server.listen(0,'127.0.0.1');await once(server,'listening');
  const port=server.address().port,origin=`http://127.0.0.1:${port}`;
  const client=net.connect(port,'127.0.0.1');await once(client,'connect');
  let timer;
  try{
    const pong=await new Promise((resolve,reject)=>{
      let buffer=Buffer.alloc(0),handshake=false;
      timer=setTimeout(()=>reject(Error('WebSocket echo deadline exceeded')),3000);
      client.on('error',reject);
      client.on('data',chunk=>{
        try{
          buffer=Buffer.concat([buffer,chunk]);
          if(!handshake){
            const end=buffer.indexOf('\r\n\r\n');if(end<0)return;
            assert.match(buffer.subarray(0,end).toString(),/^HTTP\/1\.1 101/);
            buffer=buffer.subarray(end+4);handshake=true;
            const payload=Buffer.from(JSON.stringify({type:'ping',nonce:'fresh-test-nonce',clientTime:42})),mask=crypto.randomBytes(4),encoded=Buffer.from(payload);
            for(let i=0;i<encoded.length;i++)encoded[i]^=mask[i%4];
            client.write(Buffer.concat([Buffer.from([0x81,0x80|payload.length]),mask,encoded]));
          }
          const decoded=decodeFrames(buffer,{requireMask:false});buffer=decoded.remaining;
          for(const frame of decoded.frames)if(frame.opcode===1){const value=JSON.parse(frame.payload.toString());if(value.type==='pong')resolve(value);}
        }catch(error){reject(error);}
      });
      client.write(`GET /live HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nOrigin: ${origin}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${crypto.randomBytes(16).toString('base64')}\r\n\r\n`);
    });
    assert.equal(pong.nonce,'fresh-test-nonce');assert.equal(pong.clientTime,42);assert.ok(Number.isFinite(pong.serverTime));
  }finally{clearTimeout(timer);client.destroy();server.closeProgress();await fs.rm(dir,{recursive:true,force:true});}
});

test('message HTTP submission enforces same origin, content type and receipt access', async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'lume-http-msg-'));
  const server = createProgressServer({stateDir:dir,publicDir:path.resolve(import.meta.dirname,'../public'),sampleInterval:10000,messageToken:'test-access-key'});
  server.listen(0,'127.0.0.1'); await once(server,'listening');
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    const send = (origin, body, contentType='application/json') => fetch(base+'/api/message',{method:'POST',headers:{origin,'content-type':contentType,authorization:'Bearer test-access-key'},body});
    assert.equal((await send('https://unrelated.example',JSON.stringify({text:'hello'}))).status,403);
    assert.equal((await send(base,'{"text":"hello"}','text/plain')).status,415);
    assert.equal((await send(base,'{"text":')).status,400);
    const response = await send(base,JSON.stringify({text:'Please check current WPT rate'}));
    assert.equal(response.status,201);
    const receipt = await response.json();
    assert.equal((await fetch(base+`/api/message/${receipt.id}?key=wrong`)).status,404);
    const ownMessage = await fetch(base+`/api/message/${receipt.id}?key=${receipt.key}`);
    assert.equal(ownMessage.status,200);
    assert.equal((await ownMessage.json()).text,'Please check current WPT rate');
  } finally {server.closeAllConnections();await new Promise(resolve=>server.close(resolve));await fs.rm(dir,{recursive:true,force:true});}
});
