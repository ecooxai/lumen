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
