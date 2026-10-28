export const STATES = ['idle', 'running', 'pass', 'fail', 'blocked', 'unsupported', 'cancelled', 'configuration'];

export class ReportStore {
  constructor(ids) {
    this.results = Object.fromEntries(ids.map(id => [id, { id, state: 'idle', detail: 'Ready to test', evidence: {}, updated: null }]));
  }
  set(id, state, detail, evidence = {}) {
    if (!STATES.includes(state)) throw new Error(`Invalid result state: ${state}`);
    if (!this.results[id]) throw new Error(`Unknown capability: ${id}`);
    this.results[id] = { id, state, detail, evidence, updated: new Date().toISOString() };
    return this.results[id];
  }
  coverage() {
    const counts=this.summary();
    return { scoreOutOf100: counts.total?Math.round(100*counts.pass/counts.total):0, passed:counts.pass, total:counts.total, definition:'Passed probes divided by all requested probes; unavailable and untested probes remain in the denominator. This is not an overall browser-quality or HTML5Test score.' };
  }
  summary() {
    const counts = Object.fromEntries(STATES.map(s => [s, 0]));
    for (const result of Object.values(this.results)) counts[result.state]++;
    return { total: Object.keys(this.results).length, pass: counts.pass, fail: counts.fail,
      blocked: counts.blocked, unsupported: counts.unsupported, idle: counts.idle,
      running: counts.running, cancelled: counts.cancelled, configuration: counts.configuration };
  }
}

export class ResourceScope {
  constructor() { this.closed = false; this.cancelled = false; this.resources = new Set(); }
  get size() { return this.resources.size; }
  add(cleanup) {
    if (this.closed) { try { cleanup(); } catch {} return () => {}; }
    this.resources.add(cleanup);
    return () => this.resources.delete(cleanup);
  }
  dispose() {
    if (this.closed) return;
    this.closed = true;
    for (const cleanup of [...this.resources].reverse()) { try { cleanup(); } catch {} }
    this.resources.clear();
  }
  cancel(){ this.cancelled=true; this.dispose(); }
  timer(callback, delay) {
    if (this.closed) return;
    let remove;
    const timer = setTimeout(() => { remove(); if (!this.closed) callback(); }, delay);
    remove = this.add(() => clearTimeout(timer));
    return timer;
  }
  listen(target, event, listener, options) {
    target.addEventListener(event, listener, options);
    this.add(() => target.removeEventListener(event, listener, options));
  }
}

export async function within(promise, milliseconds, scope) {
  let timer, remove, timedOut=false;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => { timedOut=true; scope?.dispose(); reject(new Error(`Test timed out after ${milliseconds / 1000}s`)); }, milliseconds);
      remove=scope?.add(()=>{clearTimeout(timer);if(scope.cancelled&&!timedOut){const error=new Error('Test cancelled and resources released');error.name='AbortError';reject(error);}});
    })]);
  } finally { clearTimeout(timer); remove?.(); }
}

export function errorState(error) {
  return ['NotAllowedError', 'PermissionDeniedError', 'NotFoundError', 'NotReadableError', 'SecurityError'].includes(error?.name) ? 'blocked' : 'fail';
}

export function validateEcho(expected, actual) {
  if (expected !== actual) throw new Error('Echo payload mismatch');
  return true;
}

export function inspectRTCEvidence({stats, capabilities, connectionState, iceConnectionState}) {
  const measured = value => Number.isFinite(value) && value >= 0 ? value : null;
  const pair = stats.find(item => item.type === 'candidate-pair' && item.state === 'succeeded' && item.nominated) || stats.find(item => item.type === 'candidate-pair' && item.state === 'succeeded');
  const transport = stats.find(item => item.type === 'transport' && item.dtlsState === 'connected');
  const channel = stats.find(item => item.type === 'data-channel' && item.state === 'open');
  const transportBytesSent = measured(transport?.bytesSent) ?? measured(pair?.bytesSent);
  const transportBytesReceived = measured(transport?.bytesReceived) ?? measured(pair?.bytesReceived);
  const dataChannelBytesSent = measured(channel?.bytesSent);
  const dataChannelBytesReceived = measured(channel?.bytesReceived);
  const nativeBackend = capabilities?.webrtcBackend ?? null;
  const declaredNative = capabilities?.webrtcNetworkTransport === true && nativeBackend === 'libdatachannel-0.24.6';
  const denied = capabilities?.webrtcNetworkTransport === false;
  const local = stats.find(item => item.id === pair?.localCandidateId && item.type === 'local-candidate');
  const remote = stats.find(item => item.id === pair?.remoteCandidateId && item.type === 'remote-candidate');
  const selectedPairObserved = !!(local && remote && ['udp','tcp'].includes(local.protocol) && ['udp','tcp'].includes(remote.protocol) && local.port > 0 && remote.port > 0);
  const networkStatsObserved = !denied && !!(pair?.nominated && transport && transportBytesSent > 0 && transportBytesReceived > 0);
  const nativeChannelStatsObserved = !denied && declaredNative && connectionState === 'connected' && ['connected','completed'].includes(iceConnectionState) && selectedPairObserved && dataChannelBytesSent > 0 && dataChannelBytesReceived > 0;
  return {verified: networkStatsObserved || nativeChannelStatsObserved, networkStatsObserved, nativeChannelStatsObserved,
    evidenceBasis: networkStatsObserved ? 'ICE/DTLS transport counters' : nativeChannelStatsObserved ? 'native selected ICE pair and measured data-channel payload counters' : null,
    nativeBackend, selectedPairObserved, transportBytesSent, transportBytesReceived, dataChannelBytesSent, dataChannelBytesReceived,
    dtlsState: transport?.dtlsState ?? null, candidatePairState: pair?.state ?? null,
    mediaTransportSupported: capabilities?.webrtcMediaTransport ?? null};
}


export async function validateByteStreamEcho(reader, expected) {
  let offset=0;
  for(let chunks=0;chunks<1024;chunks++) {
    const {value,done}=await reader.read();
    if(done) {
      if(offset!==expected.byteLength)throw new Error('Stream echo ended before the expected payload');
      return offset;
    }
    if(!(value instanceof Uint8Array))throw new Error('Stream echo requires byte chunks');
    if(offset+value.byteLength>expected.byteLength)throw new Error('Stream echo contained trailing payload');
    for(let index=0;index<value.byteLength;index++)if(value[index]!==expected[offset+index])throw new Error('Stream echo payload mismatch');
    offset+=value.byteLength;
  }
  throw new Error('Stream echo exceeded the bounded chunk limit');
}

// Keep the peer connection alive until the channel's actual close event.
// Calling channel.close() and peer.close() in the same cleanup pass can abort
// an in-flight SCTP stream reset. Errors remain fatal, including after echoes.
export function observeDataChannelClose(channel, scope) {
  let closeObserved=false, errors=0, resolveClosed, rejectClosed;
  const closed=new Promise((resolve,reject)=>{resolveClosed=resolve;rejectClosed=reject;});
  closed.catch(()=>{});
  const snapshot=()=>({gracefulChannelCloseObserved:closeObserved&&errors===0,
    dataChannelReadyState:channel.readyState,dataChannelErrorCount:errors});
  scope.listen(channel,'close',()=>{
    if(channel.readyState!=='closed'){rejectClosed(new Error('Channel close event did not have closed state'));return;}
    closeObserved=true;resolveClosed();
  });
  scope.add(()=>{if(!closeObserved)rejectClosed(new Error('Scope disposed before the channel close event'));});
  return {
    snapshot,
    recordError(){errors++;rejectClosed(new Error('Data channel reported a transport error'));return snapshot();},
    async finish(){
      if(errors)throw new Error('Data channel reported a transport error');
      if(channel.readyState==='closed'&&!closeObserved)throw new Error('Closed state lacks an observed channel close event');
      if(!['open','closing','closed'].includes(channel.readyState))throw new Error('Data channel is not ready for graceful close');
      if(channel.readyState==='open')channel.close();
      await closed;
      if(errors||!closeObserved||channel.readyState!=='closed')throw new Error('Graceful channel close was not verified');
      return snapshot();
    }
  };
}

// A local close callback need not mean the remote endpoint has processed its
// stream reset yet. Retain the peer until BOTH endpoints attest an actual
// channel-close event. The existing ordered broker carries the two successive
// completion markers; increasing sequences distinguish echo and close phases.
export async function finishDataChannelClose(completion, scope, sendClosed, waitForPeerClosed, echoSequence) {
  const active=()=>{if(scope.closed)throw new Error('Close barrier cancelled');};
  active();
  if(!Number.isSafeInteger(echoSequence)||echoSequence<=0)throw new Error('Missing peer echo-completion sequence');
  await completion.finish();active();
  await sendClosed();active();
  const closedSequence=await waitForPeerClosed();active();
  if(!Number.isSafeInteger(closedSequence)||closedSequence<=echoSequence)throw new Error('Peer close acknowledgement is stale or out of order');
  const evidence=completion.snapshot();
  if(!evidence.gracefulChannelCloseObserved||evidence.dataChannelReadyState!=='closed'||evidence.dataChannelErrorCount!==0)throw new Error('Transport error while waiting for peer close acknowledgement');
  return {...evidence,peerChannelCloseAcknowledged:true,peerChannelCloseSignalSequence:closedSequence};
}
