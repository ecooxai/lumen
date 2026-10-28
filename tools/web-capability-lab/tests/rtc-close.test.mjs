import test from 'node:test';
import assert from 'node:assert/strict';
import {ResourceScope,within,observeDataChannelClose,finishDataChannelClose} from '../core.mjs';

class Channel extends EventTarget {
  readyState='open';closeCalls=0;
  close(){if(this.readyState==='closing'||this.readyState==='closed')return;this.closeCalls++;this.readyState='closing';}
  finish(){this.readyState='closed';this.dispatchEvent(new Event('close'));}
}
function pair(){
  const channel=new Channel(),scope=new ResourceScope(),order=[];
  scope.add(()=>{order.push('peer-disposed');if(channel.readyState!=='closed')order.push('transport-aborted-before-stream-close');});
  scope.add(()=>channel.close());
  const completion=observeDataChannelClose(channel,scope);
  channel.addEventListener('error',()=>completion.recordError());
  return {channel,scope,order,completion};
}

test('old synchronous channel/peer cleanup aborts a stream still closing',()=>{
  const p=pair();p.scope.dispose();
  assert.equal(p.channel.closeCalls,1);
  assert.deepEqual(p.order,['peer-disposed','transport-aborted-before-stream-close']);
  assert.equal(p.completion.snapshot().gracefulChannelCloseObserved,false);
});

test('actual delayed close event precedes peer disposal without a fixed sleep',async()=>{
  const p=pair();let resolved=false;
  const work=p.completion.finish().then(value=>{resolved=true;return value;});
  await Promise.resolve();
  assert.equal(p.channel.readyState,'closing');assert.equal(resolved,false);assert.deepEqual(p.order,[]);
  p.channel.finish();
  assert.deepEqual(await work,{gracefulChannelCloseObserved:true,dataChannelReadyState:'closed',dataChannelErrorCount:0});
  p.scope.dispose();assert.deepEqual(p.order,['peer-disposed']);
});

test('remote close while HTTP peer acknowledgement is pending is recorded, not assumed',async()=>{
  const p=pair();let acknowledge;const peerAck=new Promise(resolve=>{acknowledge=resolve;});let peerAcknowledged=false;
  const work=(async()=>{await peerAck;peerAcknowledged=true;return p.completion.finish();})();
  p.channel.finish();await Promise.resolve();
  assert.equal(peerAcknowledged,false);assert.deepEqual(p.order,[]);
  acknowledge();assert.equal((await work).gracefulChannelCloseObserved,true);assert.equal(peerAcknowledged,true);
  assert.equal(p.channel.closeCalls,0);p.scope.dispose();assert.deepEqual(p.order,['peer-disposed']);
});

test('transport error remains fatal before echo and after peer acknowledgement',async()=>{
  for(const startedClosing of [false,true]){
    const p=pair();const work=startedClosing?p.completion.finish():null;
    p.channel.dispatchEvent(new Event('error'));p.channel.finish();
    await assert.rejects(work||p.completion.finish(),/transport error/);
    assert.equal(p.completion.snapshot().dataChannelErrorCount,1);
    assert.equal(p.completion.snapshot().gracefulChannelCloseObserved,false);p.scope.dispose();
  }
});

test('closed-state polling without an actual close event cannot pass',async()=>{
  const p=pair();p.channel.readyState='closed';
  await assert.rejects(p.completion.finish(),/lacks an observed/);p.scope.dispose();
});

test('a channel that never closes remains bounded and fails with cleanup',async()=>{
  const p=pair();await assert.rejects(within(p.completion.finish(),15,p.scope));
  assert.equal(p.scope.closed,true);assert.equal(p.completion.snapshot().gracefulChannelCloseObserved,false);
  assert.equal(p.order[0],'peer-disposed');
});

test('a close event with non-closed state cannot pass',async()=>{
  const p=pair();const work=p.completion.finish();p.channel.dispatchEvent(new Event('close'));
  await assert.rejects(work,/did not have closed state/);p.scope.dispose();
});


function deferred(){let resolve;const promise=new Promise(done=>{resolve=done;});return {promise,resolve};}

test('peer connection is retained until the remote actual-close marker arrives',async()=>{
  const p=pair(),sent=deferred(),remote=deferred();let sentMarker=false;
  const work=finishDataChannelClose(p.completion,p.scope,async()=>{sentMarker=true;sent.resolve();},()=>remote.promise,3);
  await Promise.resolve();assert.equal(sentMarker,false);assert.deepEqual(p.order,[]);
  p.channel.finish();await sent.promise;
  assert.equal(p.completion.snapshot().gracefulChannelCloseObserved,true);assert.deepEqual(p.order,[]);
  remote.resolve(5);const evidence=await work;
  assert.equal(evidence.peerChannelCloseAcknowledged,true);assert.equal(evidence.peerChannelCloseSignalSequence,5);
  p.scope.dispose();assert.deepEqual(p.order,['peer-disposed']);
});

test('stale, reversed or untyped remote close acknowledgement cannot pass',async()=>{
  for(const sequence of [10,9,false,'12']){
    const p=pair();const work=finishDataChannelClose(p.completion,p.scope,async()=>{},async()=>sequence,10);
    p.channel.finish();await assert.rejects(work,/stale or out of order/);p.scope.dispose();
  }
});

test('transport error after local close remains fatal during the remote barrier',async()=>{
  const p=pair(),sent=deferred(),remote=deferred();
  const work=finishDataChannelClose(p.completion,p.scope,async()=>{sent.resolve();},()=>remote.promise,3);
  p.channel.finish();await sent.promise;p.channel.dispatchEvent(new Event('error'));remote.resolve(5);
  await assert.rejects(work,/Transport error/);assert.equal(p.completion.snapshot().dataChannelErrorCount,1);p.scope.dispose();
});

test('missing remote-close marker times out and a late response cannot turn cancellation into pass',async()=>{
  const p=pair(),sent=deferred(),remote=deferred();
  const work=finishDataChannelClose(p.completion,p.scope,async()=>{sent.resolve();},()=>remote.promise,3);
  p.channel.finish();await sent.promise;
  await assert.rejects(within(work,15,p.scope));assert.equal(p.scope.closed,true);
  remote.resolve(5);await assert.rejects(work,/cancelled/);assert.deepEqual(p.order,['peer-disposed']);
});
