import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createLabServer,websocketFrame } from '../server.mjs';

test('HTTP routes serve only the lab and expose correct permission policy',async t=>{
  const server=createLabServer();server.listen(0,'127.0.0.1');await once(server,'listening');t.after(()=>new Promise(resolve=>server.close(resolve)));
  const origin=`http://127.0.0.1:${server.address().port}`;
  const page=await fetch(origin);assert.equal(page.status,200);assert.match(page.headers.get('permissions-policy'),/camera=\(self\)/);assert.match(await page.text(),/Browser capability lab/);
  for(const route of ['/server.mjs','/../server.mjs','/%2e%2e%2fserver.mjs','/output/wt-key.pem'])assert.equal((await fetch(origin+route)).status,404);
  assert.equal((await fetch(origin+'/',{method:'POST'})).status,405);
  const config=await(await fetch(origin+'/api/config')).json();assert.equal(typeof config.webtransport.available,'boolean');
});

test('WebSocket echo roundtrip preserves exact payload and closes cleanly',async t=>{
  const server=createLabServer();server.listen(0,'127.0.0.1');await once(server,'listening');t.after(()=>new Promise(resolve=>server.close(resolve)));
  const socket=new WebSocket(`ws://127.0.0.1:${server.address().port}/echo`);await once(socket,'open');
  const received=once(socket,'message');socket.send('lume-known-payload');const [event]=await received;assert.equal(event.data,'lume-known-payload');socket.close();await once(socket,'close');
});

test('WebSocket server encoder handles extended payload lengths',()=>{
  const frame=websocketFrame('x'.repeat(300));assert.equal(frame[0],129);assert.equal(frame[1],126);assert.equal(frame.readUInt16BE(2),300);assert.equal(frame.length,304);
});
