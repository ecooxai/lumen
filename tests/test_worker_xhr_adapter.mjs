#!/usr/bin/env node
// Host-adapter checks for the worker factory. This is NOT Lumen execution or WPT.
// The native transport and native decoder are replaced explicitly below.
// Header expectations follow https://fetch.spec.whatwg.org/#concept-filtered-response-cors
// and https://xhr.spec.whatwg.org/#the-getresponseheader()-method .
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
const option = name => { const i = args.indexOf(name); return i < 0 ? undefined : args[i + 1]; };
const sourcePath = path.resolve(option('--source') || path.join(path.dirname(fileURLToPath(import.meta.url)), '../src/js/worker_xhr.js'));
const source = fs.readFileSync(sourcePath, 'utf8');
const factory = new Function(source + '\nreturn createWorkerXMLHttpRequestInterfaces;')();
const origin = 'https://owner.example';
const other = 'https://other.example';
const encode = value => new TextEncoder().encode(value);
class HostProgressEvent extends Event {
  constructor(name, init = {}) { super(name, init); for (const key of ['loaded', 'total', 'lengthComputable']) Object.defineProperty(this, key, { value: init[key] || (key === 'lengthComputable' ? false : 0) }); }
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
function fixture(headers = [], config = {}) {
  const sent = [], aborted = [], pending = new Map();
  let sequence = 0;
  const response = (url, override = {}) => [override.status ?? config.status ?? 200, 'OK', override.url ?? config.finalURL ?? url,
    override.headers ?? headers, encode(override.text ?? config.text ?? 'body'), override.error ?? config.error ?? false];
  const W = {
    send(method, url, flat, body, credentials, callback, requestOrigin) {
      const id = ++sequence;
      const record = { id, method, url, headers: flat.slice(), body, credentials, requestOrigin, callback };
      sent.push(record); pending.set(id, record);
      if (!config.manual) queueMicrotask(() => { if (pending.has(id)) { pending.delete(id); callback(...response(url)); } });
      return id;
    },
    sendSync(method, url, flat, body, credentials, timeout, requestOrigin) {
      sent.push({ method, url, headers: flat.slice(), body, credentials, timeout, requestOrigin });
      return response(url);
    },
    abort(id) { aborted.push(id); pending.delete(id); },
    responseTextDecode(bytes) { return new TextDecoder().decode(bytes); },
    decode(bytes) { return new TextDecoder().decode(bytes); },
  };
  const interfaces = factory({ W, EventTarget, Event, ProgressEvent: HostProgressEvent, DOMException,
    Headers, URL, Blob, URLSearchParams, TextEncoder, location: { origin, href: origin + '/worker.js' },
    headerOps: { flat: headers => [...headers].flat() },
    bodyInit: (body, headers) => { if (typeof body === 'string') { if (!headers.has('content-type')) headers.set('content-type', 'text/plain;charset=UTF-8'); return encode(body); } return body; },
    blobFileOps: { isBlob: value => value instanceof Blob, copyBuffer: () => { throw new Error('Blob adapter intentionally unavailable'); }, type: value => value.type },
    objectURLs: new Map(), dispatch: (target, event) => target.dispatchEvent(event),
  });
  const make = (url = origin + '/resource', credentials = false, async = true) => {
    const xhr = new interfaces.XMLHttpRequest(); xhr.open('GET', url, async); xhr.withCredentials = credentials; return xhr;
  };
  return { ...interfaces, sent, aborted, make,
    complete(id, override = {}, evenIfAborted = false) { const record = evenIfAborted ? sent.find(r => r.id === id) : pending.get(id); assert.ok(record, 'known transport request'); pending.delete(id); record.callback(...response(record.url, override)); },
  };
}
async function load(xhr) {
  const done = new Promise((resolve, reject) => {
    xhr.addEventListener('load', () => resolve(xhr), { once: true });
    xhr.addEventListener('error', () => reject(new Error('unexpected XHR network error')), { once: true });
    xhr.addEventListener('timeout', () => reject(new Error('unexpected XHR timeout')), { once: true });
  });
  xhr.send(); return done;
}
const rows = [];
const started = new Date().toISOString(), startMono = performance.now();
async function check(name, fn) {
  const start = performance.now(); let timer;
  try { await Promise.race([Promise.resolve().then(fn), new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('host adapter check timeout')), 1500); })]); rows.push({ name, ok: true, durationSeconds: (performance.now() - start) / 1000 }); }
  catch (error) { rows.push({ name, ok: false, error: error.stack || String(error), durationSeconds: (performance.now() - start) / 1000 }); }
  finally { clearTimeout(timer); }
  console.log((rows.at(-1).ok ? 'PASS ' : 'FAIL ') + name + (rows.at(-1).ok ? '' : '\n' + rows.at(-1).error));
}
const standardHeaders = ['Content-Type', 'text/plain; charset=utf-8', 'Content-Length', '4', 'Set-Cookie', 'secret=one', 'Set-Cookie2', 'secret=two', 'X-Private', 'private', 'X-Visible', 'visible'];
const crossHeaders = [...standardHeaders, 'Access-Control-Allow-Origin', origin, 'Access-Control-Allow-Credentials', 'true'];
for (const mode of ['async', 'sync']) {
  await check(`${mode}: same-origin headers hide both cookie names`, async () => {
    const f = fixture(standardHeaders), x = f.make(undefined, false, mode === 'async');
    if (mode === 'async') await load(x); else x.send();
    assert.equal(x.getResponseHeader('SET-cookie'), null); assert.equal(x.getResponseHeader('Set-Cookie2'), null);
    assert.equal(x.getResponseHeader('X-Private'), 'private'); assert.ok(!/set-cookie/i.test(x.getAllResponseHeaders()));
  });
  await check(`${mode}: cross-origin defaults hide private headers but retain safelist`, async () => {
    const f = fixture(crossHeaders), x = f.make(other + '/resource', false, mode === 'async');
    if (mode === 'async') await load(x); else x.send();
    assert.equal(x.getResponseHeader('X-Private'), null); assert.equal(x.getResponseHeader('Content-Type'), 'text/plain; charset=utf-8');
    assert.ok(!/x-private|access-control/i.test(x.getAllResponseHeaders()));
  });
  await check(`${mode}: explicit exposure never overrides forbidden cookies`, async () => {
    const f = fixture([...crossHeaders, 'Access-Control-Expose-Headers', ' X-Visible, SET-cookie, Set-Cookie2 ']), x = f.make(other + '/expose', true, mode === 'async');
    if (mode === 'async') await load(x); else x.send();
    assert.equal(x.getResponseHeader('x-visible'), 'visible'); assert.equal(x.getResponseHeader('x-private'), null);
    assert.equal(x.getResponseHeader('set-cookie'), null); assert.equal(x.getResponseHeader('set-cookie2'), null);
  });
  await check(`${mode}: wildcard exposes private headers without credentials`, async () => {
    const f = fixture([...crossHeaders, 'Access-Control-Expose-Headers', '*']), x = f.make(other + '/wildcard', false, mode === 'async');
    if (mode === 'async') await load(x); else x.send();
    assert.equal(x.getResponseHeader('x-private'), 'private'); assert.equal(x.getResponseHeader('set-cookie'), null);
  });
  await check(`${mode}: credentialed wildcard does not expose private headers`, async () => {
    const f = fixture([...crossHeaders, 'Access-Control-Expose-Headers', '*']), x = f.make(other + '/wildcard', true, mode === 'async');
    if (mode === 'async') await load(x); else x.send();
    assert.equal(x.getResponseHeader('x-private'), null); assert.equal(x.getResponseHeader('x-visible'), null);
  });
}
await check('shared response filtering preserves duplicate values and excludes forbidden cookies', () => {
  const f = fixture();
  const result = f.transport.responseHeaders(other + '/x', other + '/x', [...crossHeaders, 'Access-Control-Expose-Headers', 'X-Visible', 'X-Visible', 'second'], 'same-origin');
  assert.deepEqual(result.filter((_, index) => index % 2 === 0).map(name => name.toLowerCase()), ['content-type', 'content-length', 'x-visible', 'x-visible']);
  assert.equal(new Headers(Array.from({ length: result.length / 2 }, (_, i) => result.slice(i * 2, i * 2 + 2))).get('x-visible'), 'visible, second');
});
await check('response redirected cross-origin retains CORS header filtering', async () => {
  const f = fixture(crossHeaders, { finalURL: other + '/final' }), x = f.make(); await load(x);
  assert.equal(x.getResponseHeader('x-private'), null); assert.equal(x.responseURL, other + '/final');
});
await check('CORS denial dispatches error and leaves response unreadable', async () => {
  const f = fixture(standardHeaders), x = f.make(other + '/denied');
  const events = []; for (const name of ['load', 'error', 'loadend']) x.addEventListener(name, () => events.push(name));
  x.send(); await delay(0); assert.deepEqual(events, ['error', 'loadend']); assert.equal(x.status, 0); assert.equal(x.responseText, ''); assert.equal(x.getAllResponseHeaders(), '');
});
await check('send returns before asynchronous completion and preserves state/event order', async () => {
  const f = fixture(standardHeaders), x = new f.XMLHttpRequest(), states = [], events = [];
  x.onreadystatechange = () => states.push(x.readyState);
  for (const name of ['loadstart', 'progress', 'load', 'loadend']) x.addEventListener(name, () => events.push(name));
  x.open('GET', '/ok'); const done = load(x); assert.equal(x.readyState, 1); await done;
  assert.deepEqual(states, [1, 2, 3, 4]); assert.deepEqual(events, ['loadstart', 'progress', 'load', 'loadend']); assert.equal(x.responseText, 'body');
});
await check('abort cancels native request and suppresses late completion', async () => {
  const f = fixture(standardHeaders, { manual: true }), x = f.make(), events = [];
  for (const name of ['abort', 'load', 'loadend']) x.addEventListener(name, () => events.push(name));
  x.send(); x.abort(); f.complete(1, {}, true); await delay(0);
  assert.deepEqual(f.aborted, [1]); assert.deepEqual(events, ['abort', 'loadend']); assert.equal(x.readyState, 0); assert.equal(x.status, 0);
});
await check('abort during loadstart prevents issuing a request', () => {
  const f = fixture(standardHeaders), x = f.make(); x.onloadstart = () => x.abort(); x.send(); assert.equal(f.sent.length, 0); assert.equal(x.readyState, 0);
});
await check('reentrant open during HEADERS_RECEIVED suppresses stale load events', async () => {
  const f = fixture(standardHeaders), x = f.make(), events = [];
  x.onreadystatechange = () => { if (x.readyState === 2) x.open('GET', '/replacement'); };
  x.onload = () => events.push('load'); x.send(); await delay(0);
  assert.equal(x.readyState, 1); assert.equal(x.status, 0); assert.deepEqual(events, []);
});
await check('timeout cancels native request and suppresses late success', async () => {
  const f = fixture(standardHeaders, { manual: true }), x = f.make(), events = [];
  x.timeout = 5; for (const name of ['timeout', 'load', 'loadend']) x.addEventListener(name, () => events.push(name));
  x.send(); await delay(20); f.complete(1, {}, true); assert.deepEqual(f.aborted, [1]); assert.deepEqual(events, ['timeout', 'loadend']); assert.equal(x.status, 0);
});
await check('reopening aborts old transport and only new response becomes visible', async () => {
  const f = fixture(standardHeaders, { manual: true }), x = f.make(), loads = [];
  x.onload = () => loads.push(x.responseText); x.send(); x.open('GET', '/second'); x.send(); f.complete(1, { text: 'stale' }, true); f.complete(2, { text: 'fresh' });
  assert.deepEqual(f.aborted, [1]); assert.deepEqual(loads, ['fresh']); assert.equal(x.responseText, 'fresh');
});
await check('GET request ignores body and strips author forbidden request headers', async () => {
  const f = fixture(standardHeaders), x = f.make(); x.setRequestHeader('Cookie', 'private'); x.setRequestHeader('Sec-Fetch-Site', 'forged'); x.setRequestHeader('X-Allowed', 'value'); x.send('ignored'); await delay(0);
  assert.equal(f.sent[0].body, null); const h = new Headers(Array.from({ length: f.sent[0].headers.length / 2 }, (_, i) => f.sent[0].headers.slice(i * 2, i * 2 + 2)));
  assert.equal(h.get('cookie'), null); assert.equal(h.get('sec-fetch-site'), null); assert.equal(h.get('x-allowed'), 'value');
});
await check('brand checks and send state failures are observable', () => {
  const f = fixture(); assert.throws(() => f.XMLHttpRequest.prototype.open.call({}, 'GET', '/'), TypeError);
  const x = new f.XMLHttpRequest(); assert.throws(() => x.send(), { name: 'InvalidStateError' }); assert.throws(() => x.open('TRACE', '/'), { name: 'SecurityError' });
  const y = fixture([], { manual: true }).make(); y.send(); assert.throws(() => y.send(), { name: 'InvalidStateError' }); y.abort();
});
await check('worker document responseType is ignored and responseXML is absent', () => {
  const f = fixture(), x = f.make(); x.responseType = 'document'; assert.equal(x.responseType, ''); assert.equal('responseXML' in x, false);
});
await check('JSON response parses the bytes through explicit decoder adapter', async () => {
  const f = fixture(['Content-Type', 'application/json'], { text: '{"ok":true}' }), x = f.make(); x.responseType = 'json'; await load(x); assert.deepEqual(x.response, { ok: true }); assert.equal(x.response, x.response); assert.throws(() => x.responseText, { name: 'InvalidStateError' });
});
await check('response URL removes fragments', async () => {
  const f = fixture(standardHeaders, { finalURL: origin + '/final#secret' }), x = f.make(); await load(x); assert.equal(x.responseURL, origin + '/final');
});
await check('XMLHttpRequestUpload cannot be directly constructed', () => { const f = fixture(); assert.throws(() => new f.XMLHttpRequestUpload(), TypeError); });
await check('XMLHttpRequestEventTarget cannot be directly constructed', () => { const f = fixture(); assert.throws(() => new f.XMLHttpRequestEventTarget(), TypeError); });

const receipt = { kind: 'host-adapter-semantic-checks', nativeLumenExecuted: false, officialWPT: false, nativeTransportExercised: false,
  decoder: 'Node TextDecoder adapter; native decoder untested', node: process.version, sourcePath,
  sourceSHA256: crypto.createHash('sha256').update(source).digest('hex'),
  fixtureSHA256: crypto.createHash('sha256').update(fs.readFileSync(fileURLToPath(import.meta.url))).digest('hex'),
  started, ended: new Date().toISOString(), durationSeconds: (performance.now() - startMono) / 1000,
  total: rows.length, passed: rows.filter(row => row.ok).length, failed: rows.filter(row => !row.ok).length,
  monitor: 'Subsecond host-adapter suite; no meaningful 1Hz app resource measurement', rows };
if (option('--output')) fs.writeFileSync(path.resolve(option('--output')), JSON.stringify(receipt, null, 2) + '\n');
console.log('HOST_ADAPTER_RESULTS ' + JSON.stringify({ total: receipt.total, passed: receipt.passed, failed: receipt.failed, durationSeconds: receipt.durationSeconds, sourceSHA256: receipt.sourceSHA256 }));
process.exitCode = receipt.failed ? 1 : 0;
