export const STATES = ['untested', 'running', 'pass', 'fail', 'blocked', 'unsupported', 'stopped'];

// Native console bridges may cap a single line near 2 KiB. Preserve the full
// report in bounded records instead of silently losing results after context.
export function diagnosticConsoleLines(report) {
  const text = JSON.stringify(report), size = 256;
  if (text.length > 128 * 1024) return ['LUME_LAB_REPORT_ERROR '+JSON.stringify({error:'Report exceeds 128 KiB diagnostic limit'})];
  const id = String(report.exportedAt || Date.now());
  const total = Math.ceil(text.length / size);
  const lines = (report.results || []).map(result => 'LUME_LAB_RESULT '+JSON.stringify({id:result.id,state:result.state,evidence:String(result.evidence).slice(0,256),durationMs:result.durationMs}));
  for(let index=0;index<total;index++)lines.push('LUME_LAB_REPORT_CHUNK '+JSON.stringify({version:1,id,index,total,data:text.slice(index*size,(index+1)*size)}));
  return lines;
}

export class LabModel {
  constructor(ids) {
    this.startedAt = new Date().toISOString();
    this.results = new Map(ids.map(id => [id, { id, state: 'untested', evidence: 'Not run', durationMs: null, updatedAt: null }]));
    this.listeners = new Set();
    this.context = {};
    this.details = {};
  }
  subscribe(listener) { this.listeners.add(listener); return () => this.listeners.delete(listener); }
  recordDetail(key, detail) {this.details[key] = {...detail, updatedAt:new Date().toISOString()};}
  record(id, state, evidence, durationMs = null) {
    if (!STATES.includes(state) || !this.results.has(id)) throw Error('Invalid result');
    this.results.set(id, {id, state, evidence: String(evidence), durationMs, updatedAt: new Date().toISOString()});
    for (const listener of this.listeners) listener(this.report());
  }
  report() {
    const results = [...this.results.values()].map(r => ({...r}));
    const summary = Object.fromEntries(STATES.map(state => [state, results.filter(r => r.state === state).length]));
    summary.total = results.length;
    return {schemaVersion: 1, startedAt: this.startedAt, exportedAt: new Date().toISOString(), context: {...this.context}, details:{...this.details}, summary, results};
  }
}

export class CleanupBag {
  constructor() { this.items = new Set(); this.closed = false; }
  get size() { return this.items.size; }
  add(dispose) {
    if (this.closed) { Promise.resolve().then(dispose).catch(() => {}); return () => {}; }
    this.items.add(dispose); return () => this.items.delete(dispose);
  }
  async clear() {
    this.closed = true; const items = [...this.items]; this.items.clear();
    await Promise.allSettled(items.reverse().map(dispose => Promise.resolve().then(dispose)));
  }
}

export function validateAuthMessage(event, pending, now = Date.now()) {
  const d = event.data;
  return Boolean(pending && event.origin === pending.origin && event.source === pending.popup &&
    d && d.type === 'lume-mock-oauth' && d.state === pending.state && d.provider === pending.provider &&
    ['approved', 'cancelled'].includes(d.status) && now <= pending.expires);
}

export function validateFrameMessage(event, expected) {
  return Boolean(expected && event.source === expected.source && event.origin === expected.origin && event.data &&
    event.data.type === 'lume-frame' && event.data.nonce === expected.nonce);
}

export function validPosition(coords) {
  return Boolean(coords && Number.isFinite(coords.latitude) && Math.abs(coords.latitude)<=90 && Number.isFinite(coords.longitude) && Math.abs(coords.longitude)<=180 && Number.isFinite(coords.accuracy) && coords.accuracy>=0);
}

export function validVideo(video, track) {
  return Boolean(video && Number.isFinite(video.videoWidth) && video.videoWidth>0 && Number.isFinite(video.videoHeight) && video.videoHeight>0 && track?.readyState==='live');
}

// Same proof contract as the repository's existing web-capability-lab/core.mjs.
export function inspectRTCProof({stats,capabilities,connectionState,iceConnectionState}) {
  const measured=value=>Number.isFinite(value)&&value>=0?value:null;
  const pair=stats.find(s=>s.type==='candidate-pair'&&s.state==='succeeded'&&s.nominated)||stats.find(s=>s.type==='candidate-pair'&&s.state==='succeeded');
  const transport=stats.find(s=>s.type==='transport'&&s.dtlsState==='connected');
  const channel=stats.find(s=>s.type==='data-channel'&&s.state==='open');
  const transportBytesSent=measured(transport?.bytesSent)??measured(pair?.bytesSent),transportBytesReceived=measured(transport?.bytesReceived)??measured(pair?.bytesReceived);
  const dataChannelBytesSent=measured(channel?.bytesSent),dataChannelBytesReceived=measured(channel?.bytesReceived);
  const nativeBackend=capabilities?.webrtcBackend??null;
  const native=capabilities?.webrtcNetworkTransport===true&&nativeBackend==='libdatachannel-0.24.6';
  const denied=capabilities?.webrtcNetworkTransport===false;
  const local=stats.find(s=>s.type==='local-candidate'&&s.id===pair?.localCandidateId),remote=stats.find(s=>s.type==='remote-candidate'&&s.id===pair?.remoteCandidateId);
  const selectedPairObserved=Boolean(local&&remote&&['udp','tcp'].includes(local.protocol)&&['udp','tcp'].includes(remote.protocol)&&local.port>0&&remote.port>0);
  const networkStatsObserved=!denied&&Boolean(pair?.nominated&&transport&&transportBytesSent>0&&transportBytesReceived>0);
  const nativeChannelStatsObserved=!denied&&native&&connectionState==='connected'&&['connected','completed'].includes(iceConnectionState)&&selectedPairObserved&&dataChannelBytesSent>0&&dataChannelBytesReceived>0;
  return {verified:networkStatsObserved||nativeChannelStatsObserved,networkStatsObserved,nativeChannelStatsObserved,nativeBackend,selectedPairObserved,transportBytesSent,transportBytesReceived,dataChannelBytesSent,dataChannelBytesReceived,dtlsState:transport?.dtlsState??null,candidatePairState:pair?.state??null,crossBrowserInteroperability:false,mediaTransportSupported:capabilities?.webrtcMediaTransport??null};
}

export function formatError(error) {
  const name = error?.name || 'Error';
  const hints = {
    NotAllowedError: 'Permission was denied, dismissed, or blocked by a device/site policy. Check app and site permissions, then retry.',
    NotFoundError: 'No matching device is available. A simulator may not expose physical cameras or microphones.',
    NotReadableError: 'The device is busy or unavailable. Close other capture sessions and retry.',
    SecurityError: 'A secure context or permissions policy is required.',
    AbortError: 'The operation was stopped.',
    TimeoutError: 'The operation timed out. Check device prompts and endpoint reachability.'
  };
  return `${name}: ${hints[name] || error?.message || String(error)}`;
}

export function classifyFailure(error,{id,cancelled=false}={}) {
  const geo=id==='location'&&[1,2,3].includes(error?.code);
  const state=error?.name==='AbortError'&&cancelled?'stopped':['NotAllowedError','NotFoundError','NotReadableError','SecurityError'].includes(error?.name)||geo?'blocked':'fail';
  let message;
  if(error?.name==='AbortError'&&!cancelled)message=`AbortError: ${error.message||'The API aborted internally; no user Stop was requested.'}`;
  else if(geo)message={1:'Location permission denied. Check app and site permissions.',2:'Location unavailable. Check simulator location settings or device location services.',3:'Location timed out. Check simulator location settings or device location services.'}[error.code];
  else message=formatError(error);
  return {state,message};
}

export function safeTransportURL(value) {
  const url = new URL(value);
  if (url.protocol !== 'https:' || url.username || url.password || url.hash) throw Error('Use an HTTPS WebTransport endpoint without credentials or a fragment.');
  return url.href;
}

export function nonce() {
  if (globalThis.crypto?.randomUUID) return crypto.randomUUID();
  if (!globalThis.crypto?.getRandomValues) throw Error('Secure random generation unavailable. Use HTTPS or localhost.');
  return [...crypto.getRandomValues(new Uint8Array(16))].map(x => x.toString(16).padStart(2, '0')).join('');
}

export function timeout(promise, ms, signal) {
  return new Promise((resolve, reject) => {
    let timer;
    const abort = () => finish(reject, new DOMException('Stopped', 'AbortError'));
    const finish = (fn, value) => { clearTimeout(timer); signal?.removeEventListener('abort', abort); fn(value); };
    timer = setTimeout(() => finish(reject, new DOMException('Timed out', 'TimeoutError')), ms);
    Promise.resolve(promise).then(v => finish(resolve, v), e => finish(reject, e));
    if (signal?.aborted) abort();
    else signal?.addEventListener('abort', abort, {once: true});
  });
}
