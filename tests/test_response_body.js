const assert = require('node:assert/strict'), fs = require('node:fs'), vm = require('node:vm');
const {test} = require('node:test');
function harness() {
    const web = fs.readFileSync('src/js/prelude/07-web.js','utf8');
    const net = fs.readFileSync('src/js/prelude/07b-net.js','utf8');
    const context = {ArrayBuffer,Uint8Array,URL,URLSearchParams,Blob,AbortController,
        def:(o,k,v)=>Object.defineProperty(o,k,{value:v,writable:true,configurable:true}),
        toBytes:b=>b instanceof ArrayBuffer?b:b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength),
        N:{encode:s=>new TextEncoder().encode(s).buffer,decode:b=>new TextDecoder().decode(b)}};
    vm.runInNewContext(web.slice(web.indexOf('const streamCtl'),web.indexOf('class WritableStreamDefaultWriter')) +
        net.slice(0,net.indexOf('function localBody')) + '\nthis.API={Response,ReadableStream};',context);
    return context.API;
}
test('Response body identity, locking, disturbance and second consumption',async()=>{
    const {Response}=harness(),r=new Response('hello');
    assert.equal(r.body,r.body);const rd=r.body.getReader();assert.equal(r.bodyUsed,false);
    await assert.rejects(r.text(),/consumed or locked/);rd.releaseLock();
    assert.equal(await r.text(),'hello');assert.equal(r.bodyUsed,true);assert.equal(r.body.locked,true);
    await assert.rejects(r.text(),/consumed or locked/);assert.throws(()=>r.clone(),/consumed or locked/);
});
test('direct body reads and cancellation disturb the response',async()=>{
    const {Response}=harness(),r=new Response('data'),rd=r.body.getReader();
    await rd.read();rd.releaseLock();assert.equal(r.bodyUsed,true);await assert.rejects(r.text(),/consumed or locked/);
    const canceled=new Response('other');await canceled.body.cancel();assert.equal(canceled.bodyUsed,true);
});
test('null and empty bodies keep distinct semantics',async()=>{
    const {Response}=harness(),nil=new Response(null),empty=new Response('');
    assert.equal(nil.body,null);assert.equal(await nil.text(),'');assert.equal(nil.bodyUsed,false);
    assert.notEqual(empty.body,null);assert.equal(await empty.text(),'');assert.equal(empty.bodyUsed,true);
});
test('response clone tees the bytes and copies metadata',async()=>{
    const {Response}=harness(),r=new Response('cloned',{status:201,headers:{'Content-Type':'text/plain'}}),c=r.clone();
    assert.equal(c.status,201);assert.deepEqual(await Promise.all([r.text(),c.text()]),['cloned','cloned']);
});
test('public bodyUsed and status overrides do not replace internal state',async()=>{
    const {Response}=harness(),r=new Response('x',{status:201});
    Object.defineProperty(r,'bodyUsed',{get(){throw Error('must not call override')}});
    Object.defineProperty(r,'status',{get(){throw Error('must not call override')}});
    assert.equal(r.ok,true);assert.equal(await r.text(),'x');await assert.rejects(r.text(),/consumed or locked/);
});
