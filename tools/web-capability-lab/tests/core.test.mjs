import test from 'node:test';
import assert from 'node:assert/strict';
import { ReportStore, ResourceScope, errorState, within, validateEcho } from '../core.mjs';

test('reports retain unsupported and blocked as distinct from failures', () => {
  const report = new ReportStore(['camera', 'gpu', 'wasm']);
  report.set('camera', 'blocked', 'permission denied');
  report.set('gpu', 'unsupported', 'API missing');
  report.set('wasm', 'pass', '42', { actual: 42 });
  assert.deepEqual(report.summary(), { total: 3, pass: 1, fail: 0, blocked: 1, unsupported: 1, idle: 0, running: 0, cancelled: 0, configuration: 0 });
  assert.throws(() => report.set('camera', 'pass-ish', 'invalid'), /state/);
});

test('scope cleanup is idempotent, reverse ordered, and late resources close immediately', () => {
  const scope = new ResourceScope();
  const calls = [];
  scope.add(() => calls.push('first'));
  scope.add(() => calls.push('second'));
  scope.dispose(); scope.dispose();
  scope.add(() => calls.push('late'));
  assert.deepEqual(calls, ['second', 'first', 'late']);
  assert.equal(scope.size, 0);
});

test('one throwing cleanup does not prevent other cleanup', () => {
  const scope = new ResourceScope(); let closed = false;
  scope.add(() => { closed = true; });
  scope.add(() => { throw Error('device already lost'); });
  scope.dispose(); assert.equal(closed, true);
});

test('timeout closes scope and does not leave a live timer', async () => {
  const scope = new ResourceScope(); let stopped = false;
  scope.add(() => { stopped = true; });
  await assert.rejects(within(new Promise(() => {}), 5, scope), /timed out/);
  assert.equal(stopped, true);
  assert.equal(scope.size, 0);
});

test('echo verification requires exact received payload', () => {
  assert.equal(validateEcho('lume-test', 'lume-test'), true);
  assert.throws(() => validateEcho('lume-test', 'unexpected'), /mismatch/);
});

test('manual stop immediately cancels pending operations and their timeout',async()=>{
  const scope=new ResourceScope();const pending=within(new Promise(()=>{}),10000,scope);
  scope.cancel();await assert.rejects(pending,{name:'AbortError'});assert.equal(scope.size,0);
});

test('permission denial is blocked, unavailable hardware is blocked, actual errors fail', () => {
  assert.equal(errorState({ name: 'NotAllowedError' }), 'blocked');
  assert.equal(errorState({ name: 'NotFoundError' }), 'blocked');
  assert.equal(errorState({ name: 'Error' }), 'fail');
});


test('coverage uses all requested probes, retaining blocked, unsupported and untested in its denominator',()=>{
  const report=new ReportStore(['real','blocked','unsupported','not-run','mock']);
  report.set('real','pass','actual result');report.set('blocked','blocked','permission denied');report.set('unsupported','unsupported','API missing');report.set('mock','configuration','simulation is not authentication',{simulation:true,authenticated:false});
  assert.equal(report.coverage().scoreOutOf100,20);assert.equal(report.coverage().passed,1);assert.equal(report.coverage().total,5);
  assert.equal(report.results['not-run'].state,'idle');assert.equal(report.results.mock.state,'configuration');
  assert.equal(new ReportStore([]).coverage().scoreOutOf100,0);
});

test('native channel evidence requires a selected native ICE pair, state and measured bidirectional bytes',async()=>{
  const {inspectRTCEvidence}=await import('../core.mjs');
  const stats=[{id:'local',type:'local-candidate',protocol:'udp',port:1234},{id:'remote',type:'remote-candidate',protocol:'udp',port:5678},{type:'candidate-pair',state:'succeeded',localCandidateId:'local',remoteCandidateId:'remote'},{type:'data-channel',state:'open',bytesSent:49,bytesReceived:49}];
  const input={stats,capabilities:{engine:'Lumen',webrtcNetworkTransport:true,webrtcBackend:'libdatachannel-0.24.6',webrtcMediaTransport:false},connectionState:'connected',iceConnectionState:'completed'};
  const result=inspectRTCEvidence(input);assert.equal(result.verified,true);assert.equal(result.nativeChannelStatsObserved,true);assert.equal(result.networkStatsObserved,false);assert.equal(result.transportBytesSent,null);assert.equal(result.dtlsState,null);assert.equal(result.mediaTransportSupported,false);
  for(const change of [{connectionState:'connecting'},{iceConnectionState:'checking'},{capabilities:{webrtcNetworkTransport:false}},{capabilities:{}},{stats:stats.filter(item=>item.type!=='candidate-pair')},{stats:stats.map(item=>item.type==='data-channel'?{...item,bytesReceived:0}:item)}])assert.equal(inspectRTCEvidence({...input,...change}).verified,false);
});

test('ordinary transport evidence retains the nonzero transport guard and never substitutes payload bytes',async()=>{
  const {inspectRTCEvidence}=await import('../core.mjs');
  const stats=[{type:'candidate-pair',state:'succeeded',nominated:true,bytesSent:0,bytesReceived:0},{type:'transport',dtlsState:'connected',bytesSent:0,bytesReceived:0},{type:'data-channel',state:'open',bytesSent:49,bytesReceived:49}];
  assert.equal(inspectRTCEvidence({stats}).verified,false);
  stats[1]={...stats[1],bytesSent:100,bytesReceived:100};assert.equal(inspectRTCEvidence({stats}).networkStatsObserved,true);
  assert.equal(inspectRTCEvidence({stats,capabilities:{webrtcNetworkTransport:false}}).verified,false);
});


test('reliable stream echo accepts fragmentation only through an exact final EOF', async()=>{
  const {validateByteStreamEcho}=await import('../core.mjs');
  const expected=new Uint8Array([11,22,33,44]);
  const readable=new ReadableStream({start(controller){controller.enqueue(expected.slice(0,1));controller.enqueue(expected.slice(1));controller.close();}});
  const reader=readable.getReader();
  assert.equal(await validateByteStreamEcho(reader,expected),4);reader.releaseLock();
});

test('reliable stream echo rejects missing, changed and trailing bytes', async()=>{
  const {validateByteStreamEcho}=await import('../core.mjs');
  const expected=new Uint8Array([11,22,33]);
  for(const values of [[11,22],[11,90,33],[11,22,33,44]]){
    const reader=new ReadableStream({start(controller){controller.enqueue(new Uint8Array(values));controller.close();}}).getReader();
    await assert.rejects(validateByteStreamEcho(reader,expected),/echo/);await reader.cancel();reader.releaseLock();
  }
});

test('reliable stream echo requires bytes and bounds endless empty chunks', async()=>{
  const {validateByteStreamEcho}=await import('../core.mjs');
  const expected=new Uint8Array([11]);
  const reader=new ReadableStream({start(controller){controller.enqueue('11');controller.close();}}).getReader();
  await assert.rejects(validateByteStreamEcho(reader,expected),/byte/);await reader.cancel();reader.releaseLock();
  let reads=0;const endless={read:async()=>{reads++;return{done:false,value:new Uint8Array(0)};}};
  await assert.rejects(validateByteStreamEcho(endless,expected),/chunk limit/);assert.equal(reads,1024);
});
