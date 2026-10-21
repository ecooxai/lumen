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
