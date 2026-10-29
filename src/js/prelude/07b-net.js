// Fetch "append a request Origin header": CORS requests and non-GET/HEAD requests carry the document origin.
function withOrigin(flat, url, method, mode) {
    const kept = [];
    for (let i = 0; i + 1 < flat.length; i += 2) { const n = String(flat[i]).toLowerCase(); if (!XHR_FORBIDDEN.has(n) && !/^(proxy-|sec-)/.test(n)) kept.push(flat[i], flat[i + 1]); }
    flat = kept;
    try {
        const o = location.origin, safe = /^(GET|HEAD)$/i.test(method);
        if ((new URL(url).origin === o || mode === 'no-cors') && safe) return flat;
        for (let i = 0; i < flat.length; i += 2) if (/^origin$/i.test(flat[i])) return flat;
        flat.push('Origin', o);
    } catch (e) {}
    return flat;
}

// CORS check: a cross-origin response is readable only if Access-Control-Allow-Origin admits this origin.
function corsOk(reqUrl, finalUrl, hdrs, cred) {
    const o = location.origin;
    let cross;
    try {
        const r = new URL(reqUrl), f = new URL(finalUrl || reqUrl);
        if (/^(data|blob|about):$/.test(f.protocol)) return true;
        if (location.protocol === 'lumen:' && r.protocol === 'file:' && f.protocol === 'file:') return true;   /* the Downloads viewer's files */
        if (location.protocol === 'file:' && (r.protocol === 'file:' || f.protocol === 'file:')) {   /* a file page reads files in its own folder or below */
            const dir = location.href.replace(/[?#].*$/, '').replace(/[^/]*$/, '');
            return [r, f].every(u => u.protocol === 'file:' && u.href.startsWith(dir) && !/(^|\/)(\.|%2e){1,2}(\/|$)/i.test(u.pathname));
        }
        cross = r.origin !== o || f.origin !== o;
    } catch (e) { return true; }
    if (!cross) return true;
    const get = (n) => { for (let i = 0; i + 1 < hdrs.length; i += 2) if (String(hdrs[i]).toLowerCase() === n) return String(hdrs[i + 1]).trim(); return null; };
    const a = get('access-control-allow-origin');
    if (a === null) return false;
    if (a === '*') return !cred;
    return a === o && (!cred || get('access-control-allow-credentials') === 'true');
}

// CORS preflight: cross-origin requests with a non-safelisted method or headers first send OPTIONS.
const hget = (hdrs, n) => { for (let i = 0; i + 1 < hdrs.length; i += 2) if (String(hdrs[i]).toLowerCase() === n) return String(hdrs[i + 1]).trim(); return null; };
const CORS_UNSAFE_BYTE = /[\x00-\x08\x0A-\x1F"():<>?@\[\\\]{}\x7F]/;
function corsUnsafeHeaders(flat) {
    const out = new SSet();
    for (let i = 0; i + 1 < flat.length; i += 2) {
        const n = String(flat[i]).toLowerCase(), v = String(flat[i + 1]);
        if (n === 'origin') continue;
        let safe = false;
        if (v.length <= 128) {
            if (n === 'accept') safe = !CORS_UNSAFE_BYTE.test(v);
            else if (n === 'accept-language' || n === 'content-language') safe = /^[0-9A-Za-z *,\-.;=]*$/.test(v);
            else if (n === 'content-type') safe = !CORS_UNSAFE_BYTE.test(v) && /^(application\/x-www-form-urlencoded|multipart\/form-data|text\/plain)$/i.test(v.split(';')[0].trim());
            else if (n === 'range') safe = /^bytes=\d+-\d*$/.test(v);
        }
        if (!safe) out.add(n);
    }
    return [...out].sort();
}
function needsPreflight(method, url, flat) {
    try { if (new URL(url).origin === location.origin) return null; } catch (e) { return null; }
    const hs = corsUnsafeHeaders(flat);
    return /^(GET|HEAD|POST)$/.test(method) && !hs.length ? null : hs;
}
const preflightHdrs = (method, hs) => ['Origin', location.origin, 'Access-Control-Request-Method', method].concat(hs.length ? ['Access-Control-Request-Headers', hs.join(',')] : []);
function preflightOk(url, method, hs, status, finalUrl, hdrs, cred) {
    if (status < 200 || status > 299 || !corsOk(url, finalUrl, hdrs, cred)) return false;
    const list = (n) => (hget(hdrs, n) || '').split(',').map(x => x.trim()).filter(Boolean);
    const ms = list('access-control-allow-methods'), hl = list('access-control-allow-headers').map(x => x.toLowerCase());
    if (!/^(GET|HEAD|POST)$/.test(method) && !ms.includes(method) && (cred || !ms.includes('*'))) return false;
    return hs.every(h => hl.includes(h) || (!cred && h !== 'authorization' && hl.includes('*')));
}
function corsSendSync(cred, method, url, flat, body) {
    const hs = needsPreflight(method, url, flat);
    if (hs) { const p = N.fetchSync('OPTIONS', url, preflightHdrs(method, hs), null); if (p[5] || !preflightOk(url, method, hs, p[0], p[2], p[3] || [], cred)) return [0, '', url, [], null, true]; }
    return N.fetchSync(method, url, flat, body);
}

/* the shared Fetch module's hooks for a document */
const FETCH_BASE = () => document.baseURI;
const FETCH_RPOLICY = () => { const h = document.head, m = h ? [...h.children].filter(e => e.localName === 'meta' && /^referrer$/i.test(e.name) && parseReferrerPolicy(e.content)) : []; return m.length ? m[m.length - 1].content : N.csp(document)[2]; };
const FETCH_BLOB = b => new Uint8Array(b._buf);
const FETCH_BLOBURL = u => objectURLs.get(u) || null;
const FETCH_BRAND = r => { if (N.wasmResponseBrand) N.wasmResponseBrand(r); };
const FETCH_WASM = N.wasmResponse ? f => N.wasmResponse(f) : null;
const FORM_ENTRIES = (form, submitter) => {
    if (!form || !form.elements) throw new TypeError("Failed to construct 'FormData': parameter 1 is not of type 'HTMLFormElement'.");
    if (submitter !== null && submitter !== undefined) {
        const t = submitter.type, sub = N.isNode(submitter) && ((submitter.localName === 'button' && t === 'submit') || (submitter.localName === 'input' && (t === 'submit' || t === 'image')));
        if (!sub) throw new TypeError("Failed to construct 'FormData': The specified element is not a submit button.");
        if (submitter.form !== form) throw new DOMException("Failed to construct 'FormData': The specified element is not owned by this form element.", 'NotFoundError');
    }
    if (formBuilding.has(form)) throw new DOMException("Failed to construct 'FormData': The form is already constructing its entry list.", 'InvalidStateError');
    const out = [];
    for (const e of form.elements) {
        if (!e.name || e.disabled || e.closest('datalist')) continue;
        const t = e.type, ln = e.localName;
        if ((t === 'checkbox' || t === 'radio') && !e.checked) continue;
        if (/^(button|reset|image)$/.test(t) || (t === 'submit' && e !== submitter) || ln === 'object' || ln === 'output' || ln === 'fieldset') continue;
        if (ln === 'select') { for (const o of e.options) if (o.selected && !o.disabled) out.push([e.name, o.value]); continue; }
        if (ln === 'input' && t === 'file') {
            const fs = e.files;
            if (fs && fs.length) for (const f of fs) out.push([e.name, f]);
            else out.push([e.name, new File([], '', { type: 'application/octet-stream' })]);
            continue;
        }
        out.push([e.name, e.value]);
        const dn = ln === 'textarea' || (ln === 'input' && /^(text|search)$/.test(t)) ? e.getAttribute('dirname') : null;
        if (dn) out.push([dn, 'ltr']);
    }
    return out;
};
/* the formdata event a form fires once its entry list is built (FormData(form) and submission) */
const formBuilding = new WeakSet();
class FormDataEvent extends Event {
    constructor(type, init) {
        if (arguments.length < 2) throw new TypeError(`Failed to construct 'FormDataEvent': 2 arguments required, but only ${arguments.length} present.`);
        if (!init || !(init.formData instanceof FormData)) throw new TypeError("Failed to construct 'FormDataEvent': Failed to read the 'formData' property from 'FormDataEventInit': Required member is undefined.");
        super(type, init); def(this, '_fd', init.formData);
    }
    get formData() { return this._fd; }
}
const FORM_FIRE = (form, fd) => {
    formBuilding.add(form);
    try { dispatch(form, new FormDataEvent('formdata', { bubbles: true, formData: fd }), true); } finally { formBuilding.delete(form); }
};
/* file: URLs: the Downloads viewer's files, or a file page's own folder and below */
function fileFetchOk(url) {
    if (location.protocol === 'lumen:') return true;
    if (location.protocol !== 'file:') return false;
    const dir = location.href.replace(/[?#].*$/, '').replace(/[^/]*$/, '');
    return url.startsWith(dir) && !/(^|\/)(\.|%2e){1,2}(\/|$)/i.test(new URL(url).pathname);
}
const FETCH_NET = (o, h) => {
    if (/^file:/i.test(o.url) && !fileFetchOk(o.url)) { setTimeout(() => h.done('blocked', 0, '', o.url, [], null)); return () => {}; }
    const flat = o.headers.concat([':redirect', 'manual', ':cookies', o.cookies ? 'include' : 'omit', ':cache', o.cache || 'default']);
    const id = N.fetch(o.method, o.url, flat, o.body, (status, statusText, url, hdrs, body, err) => h.done(err || null, status, statusText, url, hdrs, body),
        (status, statusText, url, hdrs) => h.head(status, statusText, url, hdrs), ab => h.chunk(ab));
    const cancel = () => N.abort(id);
    cancel.pause = on => N.fetchPause(id, on);
    return cancel;
};
/*@shared fetch.js*/
const CACHE_URL = input => { try { return new URL(reqState.has(input) ? reqState.get(input).url : String(input), document.baseURI).href; } catch (e) { return null; } };
const CACHE_RESP = responseFrom;
/*@shared cache.js*/
/* XMLHttpRequest's data: and blob: URLs: [bytes, type], null for a network error, undefined for other URLs */
function localBody(url) {
    if (url.startsWith('blob:')) { const b = objectURLs.get(url); return b ? [b._buf, b.type] : null; }
    if (!/^data:/i.test(url)) return undefined;
    const d = dataURL(url);
    return d ? [d.bytes.buffer, d.mime] : null;
}
// Blob URL response, honouring a single "Range: bytes=" request header (206 + Content-Range); null = network error.
function blobResponse(url, lb, range) {
    const buf = lb[0], n = buf.byteLength, h = url.startsWith('blob:') ? ['Content-Type', lb[1], 'Content-Length', String(n)] : ['Content-Type', lb[1]];
    if (range == null || !url.startsWith('blob:')) return [200, 'OK', h, buf];
    const m = /^bytes=(\d*)-(\d*)$/.exec(String(range).replace(/\s+/g, ''));
    if (!m || (m[1] === '' && m[2] === '')) return null;
    let a, b;
    if (m[1] === '') { a = Math.max(0, n - +m[2]); b = n - 1; }
    else { a = +m[1]; b = m[2] === '' ? n - 1 : Math.min(+m[2], n - 1); if (m[2] !== '' && +m[2] < a) return null; }
    if (a >= n || (m[1] === '' && +m[2] === 0)) return null;
    return [206, 'Partial Content', ['Content-Type', lb[1], 'Content-Length', String(b - a + 1), 'Content-Range', `bytes ${a}-${b}/${n}`], buf.slice(a, b + 1)];
}
/* the shared XMLHttpRequest's hooks for a document */
const XHR_WINDOW = true;
const XHR_HANDLERS = (proto, names) => installHandlers(proto, false, names);
const XHR_FIRE = (t, e) => dispatch(t, e);
const XHR_DECODE = (b, cs) => N.decodeBytes(b, cs);
const XHR_DOC = (x, text, mt) => {
    let d = null;
    if (mt === 'text/html' && x._rt === 'document' && x._async) d = N.parseDoc(text);
    else if (mt === '' || XML_TYPES.includes(mt) || mt.endsWith('+xml')) { d = parseXML(text, 'application/xml'); if (d.documentElement && d.documentElement.localName === 'parsererror') d = null; }
    if (d) def(d, '__url', x.responseURL);
    return d;
};
const XHR_DOC_BODY = (body, h) => {
    if (!(typeof Document === 'function' && body instanceof Document)) return undefined;
    const html = body.contentType === 'text/html';
    if (!h.has('content-type')) h.set('Content-Type', html ? 'text/html;charset=UTF-8' : 'application/xml;charset=UTF-8');
    return html ? (body.doctype ? `<!DOCTYPE ${body.doctype.name}>` : '') + (body.documentElement ? body.documentElement.outerHTML : '') : new XMLSerializer().serializeToString(body);
};
const XHR_SYNC = (method, url, flat, body, cred) => {
    const lb = localBody(url);
    if (lb !== undefined) { const br = lb && blobResponse(url, lb, hdrGet(flatPairs(flat), 'range')); return br ? [br[0], br[1], url, br[2], br[3], false] : [0, '', url, [], null, true]; }
    const r = corsSendSync(cred, method, url, withOrigin(flat, url, method, 'cors'), body);
    return r[5] || corsOk(url, r[2], r[3] || [], cred) ? r : [0, '', url, [], null, true];
};
/*@shared xhr.js*/
class CloseEvent extends Event {
    constructor(t, i = {}) { super(t, i); def(this, '_c', { wasClean: !!i.wasClean, code: i.code | 0, reason: i.reason === undefined ? '' : String(i.reason) }); }
    get wasClean() { return this._c.wasClean; } get code() { return this._c.code; } get reason() { return this._c.reason; }
}
class WebSocket extends EventTarget {
    constructor(url, protocols = undefined) {
        super();
        if (arguments.length < 1) throw new TypeError("Failed to construct 'WebSocket': 1 argument required, but only 0 present.");
        let s;
        try { s = new URL(String(url), location.href).href; } catch { throw new DOMException(`Failed to construct 'WebSocket': The URL '${url}' is invalid.`, 'SyntaxError'); }
        if (s.includes('#')) throw new DOMException(`Failed to construct 'WebSocket': The URL contains a fragment identifier ('${s.slice(s.indexOf('#'))}'). Fragment identifiers are not allowed in WebSocket URLs.`, 'SyntaxError');
        s = s.replace(/^http(s?):/, 'ws$1:');
        if (protocols !== null && typeof protocols === 'object' && typeof protocols[Symbol.iterator] !== 'function') protocols = protocols.protocols;
        if (!/^wss?:/.test(s)) throw new DOMException(`Failed to construct 'WebSocket': The URL's scheme must be either 'http', 'https', 'ws', or 'wss'. '${s.split(':')[0]}:' is not allowed.`, 'SyntaxError');
        const ps = protocols === undefined ? [] : typeof protocols === 'string' ? [protocols] : [...protocols].map(String);
        if (new SSet(ps.map(p => p.toLowerCase())).size !== ps.length || ps.some(p => !/^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/.test(p))) throw new DOMException("Failed to construct 'WebSocket': The subprotocol is invalid.", 'SyntaxError');
        def(this, '_s', { url: s, rs: 0, protocol: '', binaryType: 'blob', ps, buf: 0, dead: false });
        if (!cspConnect(s)) { def(this, '_id', 0); this._s.rs = 3; this._fail(); return; }
        def(this, '_id', N.wsOpen(s, ps.join(', '), location.origin, (type, data, code) => this._ev(type, data, code)));
    }
    get url() { return this._s.url; } get readyState() { return this._s.rs; } get protocol() { return this._s.protocol; }
    get extensions() { void this._s.url; return ''; } get bufferedAmount() { return this._s.buf; }
    get binaryType() { return this._s.binaryType; } set binaryType(v) { if (v === 'blob' || v === 'arraybuffer') this._s.binaryType = v; }
    _ev(type, data, code) {
        const st = this._s;
        if (st.dead) return;
        if (type === 0) {
            if (st.ps.length ? !st.ps.includes(data) : data) { st.rs = 2; N.wsClose(this._id, 1002, ''); this._fail(); return; }
            st.rs = 1; st.protocol = data; dispatch(this, new Event('open'));
        }
        else if (type === 1 || type === 2) { if (st.rs !== 1) return; dispatch(this, new MessageEvent('message', { data: type === 1 || st.binaryType === 'arraybuffer' ? data : new Blob([data]), origin: new URL(st.url).origin })); }
        else if (type === 3) dispatch(this, new Event('error'));
        else if (type === 4) { st.rs = 3; dispatch(this, new CloseEvent('close', { wasClean: code !== 1006, code, reason: data })); }
    }
    _fail() {
        const st = this._s; st.dead = true;
        setTimeout(() => { dispatch(this, new Event('error')); st.rs = 3; dispatch(this, new CloseEvent('close', { wasClean: false, code: 1006, reason: '' })); });
    }
    send(d) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'send' on 'WebSocket': 1 argument required, but only 0 present.");
        if (this._s.rs === 0) throw new DOMException("Failed to execute 'send' on 'WebSocket': Still in CONNECTING state.", 'InvalidStateError');
        const st = this._s, n = d instanceof Blob ? d.size : d instanceof ArrayBuffer || ArrayBuffer.isView(d) ? d.byteLength : new TextEncoder().encode(String(d)).length;
        st.buf += n;
        if (st.rs !== 1) return;
        setTimeout(() => { st.buf -= n; });
        if (d instanceof Blob) d.arrayBuffer().then(b => N.wsSend(this._id, b, true));
        else if (d instanceof ArrayBuffer || ArrayBuffer.isView(d)) N.wsSend(this._id, d, true);
        else N.wsSend(this._id, String(d), false);
    }
    close(code, reason) {
        if (code !== undefined && code !== 1000 && !(code >= 3000 && code <= 4999)) throw new DOMException(`Failed to execute 'close' on 'WebSocket': The close code must be either 1000, or between 3000 and 4999. ${code} is neither.`, 'InvalidAccessError');
        if (reason !== undefined && new TextEncoder().encode(String(reason)).length > 123) throw new DOMException("Failed to execute 'close' on 'WebSocket': The close reason must not be greater than 123 UTF-8 bytes.", 'SyntaxError');
        if (this._s.rs >= 2) return;
        if (this._s.rs === 0) { this._s.rs = 2; N.wsClose(this._id, 1000, ''); this._fail(); return; }
        this._s.rs = 2;
        N.wsClose(this._id, code === undefined ? (reason === undefined ? 0 : 1000) : code, reason === undefined ? '' : String(reason));
    }
}
installHandlers(WebSocket.prototype, false, ['open', 'message', 'error', 'close']);
for (const k of ['url', 'readyState', 'bufferedAmount', 'onopen', 'onerror', 'onclose', 'extensions', 'protocol', 'close', 'onmessage', 'binaryType', 'send']) {
    const P = WebSocket.prototype, d = Object.getOwnPropertyDescriptor(P, k);
    if (d.get && k.startsWith('on')) { const g = d.get; d.get = function () { if (!(this instanceof WebSocket) || !this._s) throw new TypeError('Illegal invocation'); return g.call(this); }; }
    d.enumerable = true; Object.defineProperty(P, k, d);
}
for (const k of ['wasClean', 'code', 'reason']) { const d = Object.getOwnPropertyDescriptor(CloseEvent.prototype, k); d.enumerable = true; Object.defineProperty(CloseEvent.prototype, k, d); }
for (const [k, v] of Object.entries({ CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 })) { Object.defineProperty(WebSocket, k, { value: v, enumerable: true }); Object.defineProperty(WebSocket.prototype, k, { value: v, enumerable: true }); }
const portList = t => (t && typeof t[Symbol.iterator] === 'function' ? [...t] : t && t.transfer ? [...t.transfer] : []).filter(p => p && typeof p.postMessage === 'function' && typeof p.start === 'function');
class MessagePort extends EventTarget { constructor() { super(); this.onmessage = null; def(this, '_other', null); } postMessage(d, t) { const ps = portList(t), r = this._wk || (this._other && this._other._wk); if (r) { N.workerPost(r[0], { __lumenPort: r[1], data: d, xfer: ps.map(p => { const k = ++portSeq; p._wk = [r[0], k]; if (p._other) remotePorts.set(k, p._other); return k; }) }); return; } const x = this._xt || (this._other && this._other._xt); if (x) { N.portTo(x[0], x[1], d, ps.map(p => exportPort(p, x[0]))); return; } const o = this._other; if (o) { const c = structuredClone(d); N.timer(() => o._deliver(c, ps), 0, false, true); } } _deliver(data, ports) { dispatch(this, new MessageEvent('message', { data, ports: ports || [] }), true); } start() {} close() { this._other = null; } }
// A port sent to another tab (another isolate) is bridged by a random key: the receiving tab gets a proxy port
// keyed by it, and each side's tabPorts maps the key to its local end, so messages on either end cross over.
const tabPorts = new SMap();
const tabKey = () => { const a = crypto.getRandomValues(new Uint32Array(2)); return a[0] * 2097152 + (a[1] & 2097151) || 1; };
function exportPort(p, dest) { const k = tabKey(); def(p, '_xt', [dest, k]); if (p._other) tabPorts.set(k, p._other); return k; }
function importPort(src, k) { const q = new MessagePort(); def(q, '_xt', [src, k]); tabPorts.set(k, q); return q; }
function portMessage(k, data, src, keys) { const p = tabPorts.get(k); if (p) p._deliver(data, keys.map(j => importPort(src, j))); }
class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } }
const BC_FIRE = (target, event) => dispatch(target, event, true);
const BC_LISTENERS = target => target.__ls;
/*@shared broadcast.js*/

const workers = new SMap(); let portSeq = 0; const remotePorts = new SMap();
class Worker extends EventTarget {
    constructor(url, opts) {
        super();
        if (arguments.length < 1) throw new TypeError("Failed to construct 'Worker': 1 argument required, but only 0 present.");
        const u = new URL(String(url), document.baseURI || N.url());
        let src = null, sourceType = '';
        if (u.protocol === 'blob:' || u.protocol === 'data:') { const lb = localBody(u.href); if (lb) { src = new TextDecoder().decode(lb[0]); sourceType = lb[1]; } }
        else if (u.origin !== location.origin) throw new DOMException(`Failed to construct 'Worker': Script at '${u.href}' cannot be accessed from origin '${location.origin}'.`, 'SecurityError');
        this.onmessage = null; this.onmessageerror = null; this.onerror = null;
        def(this, '_id', N.workerNew(u.href, src, N.userAgent(), N.platform(), opts && opts.name != null ? String(opts.name) : '', !!opts && opts.type === 'module', !!opts && opts[SHARED_WORKER] === true, false, sourceType));
        workers.set(this._id, this);
    }
    postMessage(m, t) { if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage' on 'Worker': 1 argument required, but only 0 present."); if (!this._id) return; const transfer = Array.isArray(t) ? t : t && t.transfer || []; for (const item of transfer) if (N.isWasmModule(item)) throw new DOMException('WebAssembly.Module is not transferable.', 'DataCloneError'); const ps = transfer.filter(p => p instanceof MessagePort); if (!ps.length) return N.workerPost(this._id, m); N.workerPost(this._id, { __lumenXfer: ps.map(p => { const k = ++portSeq; p._wk = [this._id, k]; if (p._other) remotePorts.set(k, p._other); return k; }), data: m }); }
    terminate() { if (this._id) { N.workerTerm(this._id); workers.delete(this._id); this._id = 0; } }
}
/* A SharedWorker: one worker per script URL and name in this document; each SharedWorker object connects to it
   through its own MessageChannel, which the worker's global receives as a connect event. */
const SHARED_WORKER = Symbol('shared'), sharedWorkers = new SMap();
class SharedWorker extends EventTarget {
    constructor(url, opts = undefined) {
        super();
        if (arguments.length < 1) throw new TypeError("Failed to construct 'SharedWorker': 1 argument required, but only 0 present.");
        const o = typeof opts === 'object' && opts !== null ? opts : { name: opts === undefined ? '' : String(opts) };
        const name = o.name == null ? '' : String(o.name);
        const href = new URL(String(url), document.baseURI || N.url()).href, key = href + '\n' + name;
        let entry = sharedWorkers.get(key);
        if (!entry || !entry.w._id) {
            const w = new Worker(href, { name, type: o.type, [SHARED_WORKER]: true });
            entry = { w, objs: new SSet() };
            w.onerror = e => { e.preventDefault(); for (const s of entry.objs) dispatch(s, new Event('error'), true); };
            sharedWorkers.set(key, entry);
        }
        entry.objs.add(this);
        this.onerror = null;
        const ch = new MessageChannel();
        def(this, '_port', ch.port1);
        entry.w.postMessage({ __lumenConnect: true }, [ch.port2]);
    }
    get port() { return this._port; }
}
function workerEvent(id, kind, data, message, filename, lineno, colno) {
    const w = workers.get(id); if (!w) return;
    if (kind === 0) {
        let ports = [];
        if (data && typeof data === 'object') {
            if (data.__lumenPort) { const p = remotePorts.get(data.__lumenPort); if (p) p._deliver(data.data, (data.xfer || []).map(k => { const q = new MessagePort(); q._wk = [id, k]; remotePorts.set(k, q); return q; })); return; }
            if (data.__lumenXfer) { ports = data.__lumenXfer.map(k => { const q = new MessagePort(); q._wk = [id, k]; remotePorts.set(k, q); return q; }); data = data.data; }
        }
        if (w._sw) return w._sw(0, data, ports);
        dispatch(w, new MessageEvent('message', { data, ports }), true);
    }
    else if (w._sw) w._sw(kind, null, [], message, filename, lineno);
    else if (kind === 1) {
        const e = new ErrorEvent('error', { message, filename, lineno, colno, cancelable: true });
        dispatch(w, e, true);
        if (!e.defaultPrevented) N.log(3, 'Uncaught (in worker) ' + message + ' at ' + filename + ':' + lineno);
    } else if (kind === 3) dispatch(w, new MessageEvent('messageerror', {}), true);
    else dispatch(w, new Event('error', { cancelable: true }), true);
}

/* Service workers: registrations per scope in this document. Each worker runs on its own thread as a Worker does;
   this side drives install and activate ({__lumenSW} messages, one lifecycle step per task) and relays messages
   between the worker's clients and navigator.serviceWorker. Fetches are not intercepted. An idle worker stops after
   30 s and starts again, without install or activate, when a message is sent to it. */
const SWK = Symbol('ServiceWorker'), swRegs = new SMap();
let swClientId = '', swCtrl = null, swReadyResolve = null, regSet, regUvc, swSetState, swRec;
const swClient = () => ({ id: swClientId || (swClientId = crypto.randomUUID()), url: location.href, type: 'window',
    frameType: G.parent !== G ? 'nested' : 'top-level', visibility: document.visibilityState, focused: document.hasFocus() });
const swReady = new Promise(r => { swReadyResolve = r; });
const swInScope = scope => { const u = new URL(location.href); u.hash = ''; return u.href.startsWith(scope); };
function swSpawn(rec, how) {
    rec.id = N.workerNew(rec.url, null, N.userAgent(), N.platform(), '', rec.type === 'module', false, true);
    if (!rec.id) return false;
    workers.set(rec.id, rec);
    N.workerPost(rec.id, { __lumenSW: how, scope: rec.reg.scope, updateViaCache: rec.reg.updateViaCache, client: swClient(), controlled: swCtrl === rec.sw, state: rec.sw.state });
    return true;
}
function swStop(rec) { clearTimeout(rec.idle); rec.idle = 0; if (rec.id) { N.workerTerm(rec.id); workers.delete(rec.id); rec.id = 0; } }
function swTouch(rec) { clearTimeout(rec.idle); rec.idle = rec.sw.state === 'activated' ? setTimeout(() => swStop(rec), 30000) : 0; }
function swSend(rec, m, ps = []) {
    if (!rec.id && !swSpawn(rec, 'wake')) return;
    swTouch(rec);
    if (!ps.length) return N.workerPost(rec.id, m);
    N.workerPost(rec.id, { __lumenXfer: ps.map(p => { const k = ++portSeq; p._wk = [rec.id, k]; if (p._other) remotePorts.set(k, p._other); return k; }), data: m });
}
function swRedundant(rec) {
    if (rec.sw.state === 'redundant') return;
    const r = rec.reg;
    for (const k of ['installing', 'waiting', 'active']) if (r[k] === rec.sw) regSet(r, k, null);
    swStop(rec);
    swSetState(rec.sw, 'redundant');
}
function swEvent(rec, kind, data, ports, message) {
    const step = f => { rec.chain = rec.chain.then(() => new Promise(res => setTimeout(() => { try { f(); } finally { res(); } }))); };
    swTouch(rec);
    if (kind === 1 || kind === 2) {   /* the script failed to load or threw while it ran */
        if (!rec.evaluated && rec.fail) { const f = rec.fail; rec.fail = null; swStop(rec); step(() => f(new TypeError(`Failed to register a ServiceWorker for scope ('${rec.reg.scope}') with script ('${rec.url}'): ${kind === 2 ? 'An unknown error occurred when fetching the script.' : 'ServiceWorker script evaluation failed'}`))); }
        else if (kind === 1) N.log(3, 'Uncaught (in service worker) ' + message);
        return;
    }
    if (kind === 3) { dispatch(swContainer, new MessageEvent('messageerror', { origin: location.origin, source: rec.sw }), true); return; }
    if (!data || typeof data !== 'object') return;
    const r = rec.reg;
    switch (data.__lumenSW) {
    case 'evaluated':
        rec.evaluated = true; rec.fail = null;
        step(() => { regSet(r, 'installing', rec.sw); swSetState(rec.sw, 'installing'); rec.ok(r); rec.ok = null; });
        step(() => dispatch(r, new Event('updatefound'), true));
        break;
    case 'installed':
        step(() => {
            if (rec.sw.state === 'redundant') return;
            if (!data.ok) { swRedundant(rec); if (!r.active && !r.waiting && swRegs.get(r.scope) === r) swRegs.delete(r.scope); return; }
            regSet(r, 'waiting', rec.sw); regSet(r, 'installing', null); swSetState(rec.sw, 'installed');
        });
        step(() => {   /* nothing waits on an older worker: activate at once */
            if (rec.sw.state !== 'installed') return;
            const old = r.active && swRec(r.active);
            if (old && old !== rec) { if (swCtrl === old.sw) { swCtrl = rec.sw; dispatch(swContainer, new Event('controllerchange'), true); } swRedundant(old); }
            regSet(r, 'active', rec.sw); regSet(r, 'waiting', null); swSetState(rec.sw, 'activating');
            swSend(rec, { __lumenSW: 'activate' });
        });
        break;
    case 'activated':
        step(() => {
            if (rec.sw.state === 'redundant') return;
            swSetState(rec.sw, 'activated'); swTouch(rec);
            if (swReadyResolve && swInScope(r.scope) && swRegs.get(r.scope) === r) { swReadyResolve(r); swReadyResolve = null; }
        });
        break;
    case 'claim':
        if (swInScope(r.scope) && swCtrl !== rec.sw) { swCtrl = rec.sw; dispatch(swContainer, new Event('controllerchange'), true); }
        break;
    case 'unregister': r.unregister(); break;
    case 'cmsg': dispatch(swContainer, new MessageEvent('message', { data: data.data, origin: location.origin, source: rec.sw, ports }), true); break;
    }
}
class ServiceWorker extends EventTarget {
    #url; #state = 'parsed'; #rec;
    constructor(key, url, rec) { if (key !== SWK) throw new TypeError('Illegal constructor'); super(); this.#url = url; this.#rec = rec; this.onstatechange = null; this.onerror = null; }
    get scriptURL() { return this.#url; }
    get state() { return this.#state; }
    postMessage(message, transfer = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage' on 'ServiceWorker': 1 argument required, but only 0 present.");
        if (this.#state === 'redundant') throw new DOMException("Failed to execute 'postMessage' on 'ServiceWorker': The ServiceWorker is in redundant state.", 'InvalidStateError');
        const t = Array.isArray(transfer) ? transfer : transfer && typeof transfer === 'object' && transfer.transfer || [];
        for (const item of t) if (N.isWasmModule(item)) throw new DOMException('WebAssembly.Module is not transferable.', 'DataCloneError');
        swSend(this.#rec, { __lumenSW: 'msg', data: message, client: swClient() }, t.filter(p => p instanceof MessagePort));
    }
    static {
        swSetState = (w, s) => { if (w.#state === s) return; w.#state = s; dispatch(w, new Event('statechange'), true); };
        swRec = w => w.#rec;
    }
}
class NavigationPreloadManager {
    #on = false; #hdr = 'true';
    constructor(key) { if (key !== SWK) throw new TypeError('Illegal constructor'); }
    enable() { this.#on = true; return Promise.resolve(); }
    disable() { this.#on = false; return Promise.resolve(); }
    setHeaderValue(v) { this.#hdr = String(v); return Promise.resolve(); }
    getState() { return Promise.resolve({ enabled: this.#on, headerValue: this.#hdr }); }
}
class ServiceWorkerRegistration extends EventTarget {
    #scope; #uvc; #slots = { installing: null, waiting: null, active: null }; #nav = new NavigationPreloadManager(SWK);
    constructor(key, scope, uvc) { if (key !== SWK) throw new TypeError('Illegal constructor'); super(); this.#scope = scope; this.#uvc = uvc; this.onupdatefound = null; }
    get installing() { return this.#slots.installing; }
    get waiting() { return this.#slots.waiting; }
    get active() { return this.#slots.active; }
    get scope() { return this.#scope; }
    get updateViaCache() { return this.#uvc; }
    get navigationPreload() { return this.#nav; }
    update() {
        if (!this.installing && !this.waiting && !this.active) return Promise.reject(new DOMException("Failed to update a ServiceWorker for scope ('" + this.#scope + "'): The registration has no newest worker.", 'InvalidStateError'));
        return Promise.resolve(this);
    }
    unregister() {
        if (swRegs.get(this.#scope) !== this) return Promise.resolve(false);
        swRegs.delete(this.#scope);
        for (const k of ['installing', 'waiting', 'active']) { const w = this.#slots[k]; if (w && w !== swCtrl) swRedundant(swRec(w)); }
        return Promise.resolve(true);
    }
    showNotification() { return Promise.reject(new TypeError("Failed to execute 'showNotification' on 'ServiceWorkerRegistration': No notification permission has been granted for this origin.")); }
    getNotifications() { return Promise.resolve([]); }
    static { regSet = (r, k, v) => { r.#slots[k] = v; }; regUvc = (r, v) => { r.#uvc = v; }; }
}
const swURL = (v, base, what) => {
    let u;
    try { u = new URL(String(v), base); } catch (e) { throw new TypeError(`Failed to register a ServiceWorker: The ${what} URL '${String(v)}' is invalid.`); }
    if (u.protocol !== 'http:' && u.protocol !== 'https:') throw new TypeError(`Failed to register a ServiceWorker: The URL protocol of the ${what} ('${u.href}') is not supported.`);
    if (/%2f|%5c/i.test(u.pathname)) throw new TypeError(`Failed to register a ServiceWorker: The provided ${what} ('${u.href}') includes a disallowed escape character.`);
    if (u.origin !== location.origin) throw new DOMException(`Failed to register a ServiceWorker: The origin of the provided ${what} ('${u.origin}') does not match the current origin ('${location.origin}').`, 'SecurityError');
    u.hash = '';
    return u;
};
class ServiceWorkerContainer extends EventTarget {
    constructor(key) { if (key !== SWK) throw new TypeError('Illegal constructor'); super(); this.oncontrollerchange = null; this.onmessage = null; this.onmessageerror = null; }
    get controller() { return swCtrl; }
    get ready() { return swReady; }
    register(scriptURL, options = undefined) {
        const argc = arguments.length;
        return new Promise((resolve, reject) => {
            if (argc < 1) throw new TypeError("Failed to execute 'register' on 'ServiceWorkerContainer': 1 argument required, but only 0 present.");
            if (options != null && typeof options !== 'object' && typeof options !== 'function') throw new TypeError("Failed to execute 'register' on 'ServiceWorkerContainer': The provided value is not of type 'RegistrationOptions'.");
            const o = options == null ? {} : options, base = document.baseURI || N.url();
            const scopeIn = o.scope, type = o.type === undefined ? 'classic' : String(o.type), uvc = o.updateViaCache === undefined ? 'imports' : String(o.updateViaCache);
            if (type !== 'classic' && type !== 'module') throw new TypeError(`Failed to execute 'register' on 'ServiceWorkerContainer': Failed to read the 'type' property from 'RegistrationOptions': The provided value '${type}' is not a valid enum value of type WorkerType.`);
            if (!['imports', 'all', 'none'].includes(uvc)) throw new TypeError(`Failed to execute 'register' on 'ServiceWorkerContainer': Failed to read the 'updateViaCache' property from 'RegistrationOptions': The provided value '${uvc}' is not a valid enum value of type ServiceWorkerUpdateViaCache.`);
            const u = swURL(scriptURL, base, 'script');
            const scope = scopeIn === undefined ? new URL('./', u) : swURL(scopeIn, base, 'scope');
            const maxScope = u.pathname.slice(0, u.pathname.lastIndexOf('/') + 1);
            if (!scope.pathname.startsWith(maxScope)) throw new DOMException(`Failed to register a ServiceWorker for scope ('${scope.href}') with script ('${u.href}'): The path of the provided scope ('${scope.pathname}') is not under the max scope allowed ('${maxScope}'). Adjust the scope, move the Service Worker script, or use the Service-Worker-Allowed HTTP header to allow the scope.`, 'SecurityError');
            let reg = swRegs.get(scope.href);
            if (reg) {
                const newest = reg.installing || reg.waiting || reg.active;
                if (newest && newest.scriptURL === u.href && swRec(newest).type === type && reg.updateViaCache === uvc) { resolve(reg); return; }
                regUvc(reg, uvc);
            } else swRegs.set(scope.href, reg = new ServiceWorkerRegistration(SWK, scope.href, uvc));
            const rec = { id: 0, url: u.href, type, reg, sw: null, evaluated: false, idle: 0, chain: Promise.resolve(), ok: resolve, fail: null };
            rec.sw = new ServiceWorker(SWK, u.href, rec);
            rec._sw = (...a) => swEvent(rec, ...a);
            rec.fail = e => { if (!reg.installing && !reg.waiting && !reg.active && swRegs.get(reg.scope) === reg) swRegs.delete(reg.scope); reject(e); };
            if (!swSpawn(rec, 'install')) { rec.fail(new TypeError('Failed to register a ServiceWorker: The worker could not be started.')); }
        });
    }
    getRegistration(clientURL = '') {
        return new Promise(resolve => {
            let u;
            try { u = new URL(String(clientURL), document.baseURI || N.url()); } catch (e) { throw new TypeError("Failed to execute 'getRegistration' on 'ServiceWorkerContainer': The documentURL provided is invalid."); }
            if (u.origin !== location.origin) throw new DOMException("Failed to execute 'getRegistration' on 'ServiceWorkerContainer': The origin of the provided documentURL ('" + u.origin + "') does not match the current origin ('" + location.origin + "').", 'SecurityError');
            u.hash = '';
            let best;
            for (const [s, r] of swRegs) if (u.href.startsWith(s) && (!best || s.length > best.scope.length)) best = r;
            resolve(best);
        });
    }
    getRegistrations() { return Promise.resolve([...swRegs.values()]); }
    startMessages() {}
}
const swContainer = new ServiceWorkerContainer(SWK);
