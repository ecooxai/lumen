class Headers {
    constructor(init) { def(this, '_m', new Map()); def(this, '_n', new Map()); if (init instanceof Headers) for (const [k, v] of init) this.append(k, v); else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v); else if (init && typeof init === 'object') for (const k of Object.keys(init)) this.append(k, init[k]); }
    append(k, v) { k = String(k); const l = k.toLowerCase(); if (!this._n.has(l)) this._n.set(l, k); k = l; v = String(v).trim(); const o = this._m.get(k); this._m.set(k, o == null ? v : k === 'set-cookie' ? o + '\n' + v : o + ', ' + v); }
    set(k, v) { k = String(k); const l = k.toLowerCase(); if (!this._n.has(l)) this._n.set(l, k); this._m.set(l, String(v).trim()); } get(k) { return this._m.get(String(k).toLowerCase()) ?? null; }
    has(k) { return this._m.has(String(k).toLowerCase()); } delete(k) { k = String(k).toLowerCase(); this._m.delete(k); this._n.delete(k); }
    getSetCookie() { const v = this._m.get('set-cookie'); return v ? v.split('\n') : []; }
    forEach(fn, self) { for (const [k, v] of this) fn.call(self, v, k, this); }
    *entries() { yield* [...this._m].sort((a, b) => a[0] < b[0] ? -1 : 1); } keys() { return [...this._m.keys()].sort()[Symbol.iterator](); } values() { return [...this.entries()].map(e => e[1])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
    _flat() { const a = []; for (const [k, v] of this._m) a.push(this._n.get(k) || k, v); return a; }
}

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
    const out = new Set();
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
function corsSend(cred, onId, method, url, flat, body, cb) {
    const hs = needsPreflight(method, url, flat);
    if (!hs) return onId(N.fetch(method, url, flat, body, cb));
    onId(N.fetch('OPTIONS', url, preflightHdrs(method, hs), null, (status, st, u, hdrs, b, err) => {
        if (err || !preflightOk(url, method, hs, status, u, hdrs || [], cred)) return cb(0, '', url, [], null, true);
        onId(N.fetch(method, url, flat, body, cb));
    }));
}
function corsSendSync(cred, method, url, flat, body) {
    const hs = needsPreflight(method, url, flat);
    if (hs) { const p = N.fetchSync('OPTIONS', url, preflightHdrs(method, hs), null); if (p[5] || !preflightOk(url, method, hs, p[0], p[2], p[3] || [], cred)) return [0, '', url, [], null, true]; }
    return N.fetchSync(method, url, flat, body);
}

const fromFlat = (a) => { const h = new Headers(); for (let i = 0; i + 1 < a.length; i += 2) h.append(a[i], a[i + 1]); return h; };
function bodyInit(b, h) {
    if (b == null) return null;
    if (typeof b === 'string') { if (h && !h.has('content-type')) h.set('Content-Type', 'text/plain;charset=UTF-8'); return N.encode(b); }
    if (b instanceof URLSearchParams) { if (h && !h.has('content-type')) h.set('Content-Type', 'application/x-www-form-urlencoded;charset=UTF-8'); return N.encode(b.toString()); }
    if (b instanceof FormData) { const bd = '----LumenFormBoundary' + Math.random().toString(36).slice(2); if (h && !h.has('content-type')) h.set('Content-Type', 'multipart/form-data; boundary=' + bd); let s = ''; for (const [k, v] of b) s += `--${bd}\r\nContent-Disposition: form-data; name="${k}"\r\n\r\n${typeof v === 'string' ? v : ''}\r\n`; return N.encode(s + `--${bd}--\r\n`); }
    if (b instanceof Blob) { if (h && b.type && !h.has('content-type')) h.set('Content-Type', b.type); return b._buf; }
    if (b instanceof ReadableStream) return b;
    return toBytes(b);
}
class Body {
    _take() {
        if (this.bodyUsed) return Promise.reject(new TypeError('Body has already been consumed.'));
        def(this, '_used', true);
        const b = this._b;
        if (!(b instanceof ReadableStream)) return Promise.resolve(b || new ArrayBuffer(0));
        return (async () => { const parts = []; let n = 0; for await (const c of b) { const u = typeof c === 'string' ? new Uint8Array(N.encode(c)) : c instanceof ArrayBuffer ? new Uint8Array(c) : new Uint8Array(c.buffer, c.byteOffset, c.byteLength); parts.push(u); n += u.length; } const out = new Uint8Array(n); let o = 0; for (const u of parts) { out.set(u, o); o += u.length; } return out.buffer; })();
    }
    get bodyUsed() { return !!this._used; }
    get body() { if (!this._b) return null; const b = this._b; if (b instanceof ReadableStream) return b; return new ReadableStream({ start(c) { c.enqueue(new Uint8Array(b)); c.close(); } }); }
    arrayBuffer() { return this._take(); } text() { return this._take().then(N.decode); } json() { return this.text().then(JSON.parse); }
    blob() { return this._take().then(b => new Blob([b], { type: (this.headers.get('content-type') || '') })); } bytes() { return this._take().then(b => new Uint8Array(b)); }
    formData() { return this.text().then(t => { const f = new FormData(); for (const [k, v] of new URLSearchParams(t)) f.append(k, v); return f; }); }
}
class Request extends Body {
    constructor(input, init = {}) {
        super(); const src = input instanceof Request ? input : null;
        this.url = new URL(src ? src.url : String(input), document.baseURI).href;
        this.method = String(init.method || (src && src.method) || 'GET').toUpperCase();
        this.headers = new Headers(init.headers || (src && src.headers));
        def(this, '_b', init.body !== undefined ? bodyInit(init.body, this.headers) : src ? src._b : null);
        this.signal = init.signal || (src && src.signal) || new AbortController().signal;
        Object.assign(this, { credentials: init.credentials || 'same-origin', mode: init.mode || 'cors', cache: init.cache || 'default', redirect: init.redirect || 'follow', referrer: 'about:client', integrity: '', keepalive: !!init.keepalive, priority: init.priority || 'auto' });
    }
    clone() { return new Request(this); }
}
class Response extends Body {
    constructor(body = null, init = {}) { super(); this.headers = new Headers(init.headers); def(this, '_b', bodyInit(body, this.headers)); this.status = init.status ?? 200; this.statusText = init.statusText || ''; this.type = 'default'; this.url = ''; this.redirected = false; }
    get ok() { return this.status >= 200 && this.status < 300; }
    clone() { const r = new Response(null, this); r._b = this._b; r.url = this.url; r.type = this.type; return r; }
    static json(d, i) { const r = new Response(JSON.stringify(d), i); r.headers.set('content-type', 'application/json'); return r; }
    static error() { const r = new Response(null, { status: 0 }); r.type = 'error'; return r; }
    static redirect(u, s = 302) { return new Response(null, { status: s, headers: { location: String(u) } }); }
}
function localBody(url) {
    if (url.startsWith('blob:')) { const b = objectURLs.get(url); return b ? [b._buf, b.type] : null; }
    const m = /^data:([^,]*?)(;base64)?,(.*)$/s.exec(url); if (!m) return undefined;
    const raw = m[2] ? atob(decodeURIComponent(m[3])) : decodeURIComponent(m[3]);
    const u = new Uint8Array(raw.length); for (let i = 0; i < raw.length; i++) u[i] = raw.charCodeAt(i) & 255;
    return [m[2] ? u.buffer : N.encode(decodeURIComponent(m[3])), m[1] || 'text/plain;charset=US-ASCII'];
}
// Blob URL response, honouring a single "Range: bytes=" request header (206 + Content-Range); null = network error.
function blobResponse(url, lb, range) {
    const buf = lb[0], n = buf.byteLength, h = ['Content-Type', lb[1], 'Content-Length', String(n)];
    if (range == null || !url.startsWith('blob:')) return [200, 'OK', h, buf];
    const m = /^bytes=(\d*)-(\d*)$/.exec(String(range).replace(/\s+/g, ''));
    if (!m || (m[1] === '' && m[2] === '')) return null;
    let a, b;
    if (m[1] === '') { a = Math.max(0, n - +m[2]); b = n - 1; }
    else { a = +m[1]; b = m[2] === '' ? n - 1 : Math.min(+m[2], n - 1); if (m[2] !== '' && +m[2] < a) return null; }
    if (a >= n || (m[1] === '' && +m[2] === 0)) return null;
    return [206, 'Partial Content', ['Content-Type', lb[1], 'Content-Length', String(b - a + 1), 'Content-Range', `bytes ${a}-${b}/${n}`], buf.slice(a, b + 1)];
}
function fetch(input, init) {
    let req; try { req = new Request(input, init); } catch (e) { return Promise.reject(e); }
    return new Promise((resolve, reject) => {
        if (req.signal.aborted) return reject(req.signal.reason);
        const lb = localBody(req.url);
        if (lb !== undefined) { const br = lb && blobResponse(req.url, lb, req.headers.get('range')); if (!br) return reject(new TypeError('Failed to fetch')); const r = new Response(null, { status: br[0], statusText: br[1], headers: fromFlat(br[2]) }); r._b = br[3]; r.url = req.url; r.type = 'basic'; return resolve(r); }
        let id = 0; corsSend(req.credentials === 'include', (i) => id = i, req.method, req.url, withOrigin(req.headers._flat(), req.url, req.method, req.mode), req._b, (status, statusText, url, hdrs, body, err) => {
            if (err) return reject(new TypeError('Failed to fetch'));
            if (!corsOk(req.url, url, hdrs || [], req.credentials === 'include')) {
                if (req.mode !== 'no-cors') return reject(new TypeError('Failed to fetch'));
                const r = new Response(null); r.status = 0; r.url = ''; r.type = 'opaque';
                return resolve(r);
            }
            if (req.mode === 'same-origin') { try { if (new URL(url || req.url).origin !== location.origin) return reject(new TypeError('Failed to fetch')); } catch (e) {} }
            const r = new Response(null, { status, statusText, headers: fromFlat(hdrs) }); r._b = body; r.url = url; r.redirected = url !== req.url; r.type = 'basic';
            resolve(r);
        });
        req.signal.addEventListener('abort', () => { N.abort(id); reject(req.signal.reason); });
    });
}
class FormData {
    constructor(form) { def(this, '_l', []); if (form) for (const e of form.elements) { if (!e.name || e.disabled) continue; const t = e.type; if ((t === 'checkbox' || t === 'radio') && !e.checked) continue; if (/^(submit|button|reset|image|file)$/.test(t)) continue; this._l.push([e.name, e.value]); } }
    append(k, v, fn) { this._l.push([String(k), v instanceof Blob ? (fn ? new File([v], fn, v) : v) : String(v)]); }
    set(k, v, fn) { this.delete(k); this.append(k, v, fn); } delete(k) { this._l = this._l.filter(e => e[0] !== String(k)); }
    get(k) { const e = this._l.find(x => x[0] === String(k)); return e ? e[1] : null; } getAll(k) { return this._l.filter(x => x[0] === String(k)).map(x => x[1]); }
    has(k) { return this._l.some(x => x[0] === String(k)); } forEach(fn, s) { for (const [k, v] of this._l) fn.call(s, v, k, this); }
    entries() { return this._l.map(x => x.slice())[Symbol.iterator](); } keys() { return this._l.map(x => x[0])[Symbol.iterator](); } values() { return this._l.map(x => x[1])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
}
class XMLHttpRequestEventTarget extends EventTarget {}
installHandlers(XMLHttpRequestEventTarget.prototype, false, ['abort', 'error', 'load', 'loadend', 'loadstart', 'progress', 'timeout']);
const XHR_FORBIDDEN = new Set(['accept-charset', 'accept-encoding', 'access-control-request-headers', 'access-control-request-method', 'connection', 'content-length', 'cookie', 'cookie2', 'date', 'dnt', 'expect', 'host', 'keep-alive', 'origin', 'referer', 'set-cookie', 'te', 'trailer', 'transfer-encoding', 'upgrade', 'via']);
const HTTP_TOKEN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
let xhrMakeUpload = false;
class XMLHttpRequestUpload extends XMLHttpRequestEventTarget { constructor() { if (!xhrMakeUpload) illegal(); super(); } }
class XMLHttpRequest extends XMLHttpRequestEventTarget {
    constructor() {
        super();
        for (const [k, v] of Object.entries({ _rs: 0, _st: 0, _stt: '', _url: '', _rt: '', _to: 0, _wc: false, _gen: 0, _send: false, _async: true, _hdrs: [], _body: null, _resp: undefined, _mime: null, _up: null, _upDone: true, _id: 0, _timer: 0, _m: 'GET', _u: '' })) def(this, k, v);
        def(this, '_rh', new Headers());
    }
    get readyState() { return this._rs; } get status() { return this._st; } get statusText() { return this._stt; }
    get responseURL() { return this._url.replace(/#.*$/, ''); }
    get upload() { if (!this._up) { xhrMakeUpload = true; try { this._up = new XMLHttpRequestUpload(); } finally { xhrMakeUpload = false; } } return this._up; }
    get timeout() { return this._to; }
    set timeout(v) { if (this._rs === 1 && !this._async) throw new DOMException('Timeouts cannot be set for synchronous requests made from a document.', 'InvalidAccessError'); this._to = toU32(v); }
    get withCredentials() { return this._wc; }
    set withCredentials(v) { if (this._rs > 1 || this._send) throw new DOMException("The value may only be set if the object's state is UNSENT or OPENED.", 'InvalidStateError'); this._wc = !!v; }
    get responseType() { return this._rt; }
    set responseType(v) {
        v = String(v); if (!['', 'arraybuffer', 'blob', 'document', 'json', 'text'].includes(v)) return;
        if (this._rs >= 3) throw new DOMException("The response type cannot be set if the object's state is LOADING or DONE.", 'InvalidStateError');
        if (this._rs === 1 && !this._async) throw new DOMException('The response type cannot be changed for synchronous requests made from a document.', 'InvalidAccessError');
        this._rt = v;
    }
    _state(s) { this._rs = s; dispatch(this, new Event('readystatechange')); }
    _fire(t, tgt = this, n = 0, tot = 0, lc = false) { dispatch(tgt, new ProgressEvent(t, { lengthComputable: lc, loaded: n, total: tot })); }
    _terminate() { this._gen++; if (this._id) { N.abort(this._id); this._id = 0; } if (this._timer) { clearTimeout(this._timer); this._timer = 0; } }
    _reset() { this._st = 0; this._stt = ''; this._url = ''; this._hdrs = []; this._body = null; this._resp = undefined; }
    open(method, url, async, user, pass) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'open' on 'XMLHttpRequest': 2 arguments required, but only ${arguments.length} present.`);
        method = String(method); url = String(url);
        if (arguments.length > 2) { async = !!async; if (user != null) user = String(user); if (pass != null) pass = String(pass); } else async = true;
        if (!HTTP_TOKEN.test(method)) throw new DOMException(`'${method}' is not a valid HTTP method.`, 'SyntaxError');
        const up = method.toUpperCase();
        if (up === 'CONNECT' || up === 'TRACE' || up === 'TRACK') throw new DOMException(`'${method}' HTTP method is unsupported.`, 'SecurityError');
        if (['DELETE', 'GET', 'HEAD', 'OPTIONS', 'POST', 'PUT'].includes(up)) method = up;
        let u; try { u = new URL(url, document.baseURI); } catch (e) { throw new DOMException(`Invalid URL`, 'SyntaxError'); }
        if (user != null || pass != null) { if (u.host) { if (user != null) u.username = user; if (pass != null) u.password = pass; } }
        if (!async && (this._to || this._rt)) throw new DOMException('Synchronous requests from a document must not set a response type or timeout.', 'InvalidAccessError');
        this._terminate();
        this._m = method; this._u = u.href; this._async = async; this._send = false; this._upDone = true;
        this._rh = new Headers(); this._reset();
        if (this._rs !== 1) this._state(1);
    }
    setRequestHeader(k, v) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'setRequestHeader' on 'XMLHttpRequest': 2 arguments required, but only ${arguments.length} present.`);
        if (this._rs !== 1 || this._send) throw new DOMException("The object's state must be OPENED.", 'InvalidStateError');
        k = String(k); v = String(v).replace(/^[\t\n\r ]+|[\t\n\r ]+$/g, '');
        if (!HTTP_TOKEN.test(k)) throw new DOMException(`'${k}' is not a valid HTTP header field name.`, 'SyntaxError');
        if (/[\0\r\n]/.test(v) || /[^\x00-\xff]/.test(v)) throw new DOMException(`'${v}' is not a valid HTTP header field value.`, 'SyntaxError');
        const l = k.toLowerCase();
        if (XHR_FORBIDDEN.has(l) || l.startsWith('proxy-') || l.startsWith('sec-')) return;
        this._rh.append(k, v);
    }
    _hdrList() { const a = []; for (let i = 0; i + 1 < this._hdrs.length; i += 2) a.push([this._hdrs[i].toLowerCase(), this._hdrs[i + 1]]); return a; }
    getResponseHeader(k) {
        if (this._rs < 2 || !this._hdrs.length) return null;
        k = String(k).toLowerCase(); if (k === 'set-cookie' || k === 'set-cookie2') return null;
        const v = this._hdrList().filter(h => h[0] === k).map(h => h[1]);
        return v.length ? v.join(', ') : null;
    }
    getAllResponseHeaders() {
        if (this._rs < 2) return '';
        const m = new Map();
        for (const [k, v] of this._hdrList()) { if (k === 'set-cookie' || k === 'set-cookie2') continue; m.set(k, m.has(k) ? m.get(k) + ', ' + v : v); }
        return [...m.keys()].sort((a, b) => a < b ? -1 : a > b ? 1 : 0).map(k => k + ': ' + m.get(k) + '\r\n').join('');
    }
    overrideMimeType(m) { if (this._rs >= 3) throw new DOMException("MimeType cannot be overridden when the state is LOADING or DONE.", 'InvalidStateError'); this._mime = String(m); }
    _errorSteps(ev) {
        this._rs = 4; this._send = false; this._reset();
        dispatch(this, new Event('readystatechange'));
        if (!this._async) return;
        if (!this._upDone) { this._upDone = true; if (this._up) { this._fire(ev, this._up); this._fire('loadend', this._up); } }
        this._fire(ev); this._fire('loadend');
    }
    _done(gen, status, statusText, url, hdrs, body, err) {
        if (gen !== this._gen) return;
        this._id = 0; if (this._timer) { clearTimeout(this._timer); this._timer = 0; }
        if (err || !corsOk(this._u, url, hdrs || [], this._wc)) { this._errorSteps('error'); if (!this._async) throw new DOMException(`Failed to load '${this._u}'.`, 'NetworkError'); return; }
        if (!this._upDone) { this._upDone = true; if (this._up && this._async) { const n = this._reqLen; this._fire('progress', this._up, n, n, true); this._fire('load', this._up, n, n, true); this._fire('loadend', this._up, n, n, true); } }
        this._st = status; this._stt = statusText; this._url = url || this._u; this._hdrs = hdrs || []; this._body = body;
        const n = body ? body.byteLength : 0, len = +this.getResponseHeader('content-length'), lc = Number.isFinite(len) && len > 0, tot = lc ? len : 0;
        if (this._async) {
            this._state(2); if (gen !== this._gen) return;
            if (n) { this._state(3); if (gen !== this._gen) return; this._fire('progress', this, n, tot, lc); if (gen !== this._gen) return; }
        }
        this._send = false; this._state(4); if (gen !== this._gen) return;
        this._fire('load', this, n, tot, lc); this._fire('loadend', this, n, tot, lc);
    }
    send(body) {
        if (this._rs !== 1 || this._send) throw new DOMException("The object's state must be OPENED.", 'InvalidStateError');
        if (this._m !== 'GET' && this._m !== 'HEAD' && body != null) {
            const isDoc = typeof Document === 'function' && body instanceof Document;
            if (isDoc) {
                const html = body.contentType === 'text/html';
                if (!this._rh.has('content-type')) this._rh.set('Content-Type', html ? 'text/html;charset=UTF-8' : 'application/xml;charset=UTF-8');
                body = html ? (body.doctype ? `<!DOCTYPE ${body.doctype.name}>` : '') + (body.documentElement ? body.documentElement.outerHTML : '')
                            : new XMLSerializer().serializeToString(body);
            }
            const ct = this._rh.get('content-type');
            if ((isDoc || typeof body === 'string' || body instanceof URLSearchParams) && ct != null)
                this._rh.set('content-type', ct.replace(/(;\s*charset\s*=\s*)("[^"]*"|[^;]*)/i, (m, p, v) => /^"?utf-8"?$/i.test(v.trim()) ? m : p + 'UTF-8'));
        }
        const b = this._m === 'GET' || this._m === 'HEAD' || body == null ? null : bodyInit(body, this._rh);
        this._reqLen = b ? (b.byteLength ?? b.length ?? 0) : 0;
        this._upDone = !b; this._send = true;
        const gen = this._gen;
        if (this._async) {
            this._fire('loadstart');
            if (!this._upDone && this._up) this._fire('loadstart', this._up, 0, this._reqLen, true);
            if (gen !== this._gen || this._rs !== 1 || !this._send) return;
        }
        const lb = localBody(this._u);
        if (lb !== undefined) { const br = lb && blobResponse(this._u, lb, this._rh.get('range')); const f = () => br ? this._done(gen, br[0], br[1], this._u, br[2], br[3], false) : this._done(gen, 0, '', this._u, [], null, true); if (this._async) setTimeout(f); else f(); return; }
        if (!this._async) { const r = corsSendSync(this._wc, this._m, this._u, withOrigin(this._rh._flat(), this._u, this._m, 'cors'), b); this._done(gen, ...r); return; }
        corsSend(this._wc, (i) => this._id = i, this._m, this._u, withOrigin(this._rh._flat(), this._u, this._m, 'cors'), b, (...a) => this._done(gen, ...a));
        if (this._to > 0) this._timer = setTimeout(() => { if (gen !== this._gen) return; this._terminate(); this._errorSteps('timeout'); }, this._to);
    }
    abort() {
        this._terminate();
        if ((this._rs === 1 && this._send) || this._rs === 2 || this._rs === 3) this._errorSteps('abort');
        if (this._rs === 4) { this._rs = 0; this._reset(); }
    }
    _mimeType() { return (this._mime || this.getResponseHeader('content-type') || '').split(';')[0].trim().toLowerCase(); }
    _text() { if (!this._body) return ''; let s = N.decode(this._body); if (s.charCodeAt(0) === 0xFEFF) s = s.slice(1); return s; }
    get responseText() {
        if (this._rt && this._rt !== 'text') throw new DOMException("The value is only accessible if the object's 'responseType' is '' or 'text' (was '" + this._rt + "').", 'InvalidStateError');
        return this._rs < 3 ? '' : this._text();
    }
    _doc() {
        if (this._resp !== undefined) return this._resp;
        const mt = this._mimeType(); let d = null;
        if (this._body) {
            if (mt === 'text/html' && this._rt === 'document' && this._async) d = N.parseDoc(this._text());
            else if (mt === '' || XML_TYPES.includes(mt) || mt.endsWith('+xml')) { d = parseXML(this._text(), 'application/xml'); if (d.documentElement && d.documentElement.localName === 'parsererror') d = null; }
        }
        if (d) def(d, '__url', this.responseURL);
        return this._resp = d;
    }
    get responseXML() {
        if (this._rt && this._rt !== 'document') throw new DOMException("The value is only accessible if the object's 'responseType' is '' or 'document' (was '" + this._rt + "').", 'InvalidStateError');
        return this._rs === 4 ? this._doc() : null;
    }
    get response() {
        if (this._rt === '' || this._rt === 'text') return this.responseText;
        if (this._rs !== 4) return null;
        if (this._rt === 'document') return this._doc();
        if (this._resp !== undefined) return this._resp;
        const b = this._body || new Uint8Array(0);
        switch (this._rt) {
        case 'arraybuffer': return this._resp = b instanceof ArrayBuffer ? b : b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
        case 'blob': return this._resp = new Blob([b], { type: this._mime || this.getResponseHeader('content-type') || '' });
        case 'json': try { return this._resp = JSON.parse(this._text()); } catch (e) { return this._resp = null; }
        }
        return null;
    }
}
installHandlers(XMLHttpRequest.prototype, false, ['readystatechange']);
for (const [k, v] of Object.entries({ UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 })) { Object.defineProperty(XMLHttpRequest, k, { value: v, enumerable: true }); Object.defineProperty(XMLHttpRequest.prototype, k, { value: v, enumerable: true }); }
class WebSocket extends EventTarget {
    constructor(url) { super(); this.url = String(url); this.readyState = 3; this.protocol = ''; this.extensions = ''; this.bufferedAmount = 0; this.binaryType = 'blob'; setTimeout(() => { for (const t of ['error', 'close']) { const ev = t === 'close' ? Object.assign(new Event('close'), { code: 1006, reason: '', wasClean: false }) : new Event('error'); if (this['on' + t]) this['on' + t](ev); dispatch(this, ev); } }); }
    send() { throw new DOMException('WebSocket is not supported yet', 'InvalidStateError'); } close() {}
}
Object.assign(WebSocket, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
class MessagePort extends EventTarget { constructor() { super(); this.onmessage = null; def(this, '_other', null); } postMessage(d) { const o = this._other; if (o) setTimeout(() => { const ev = new MessageEvent('message', { data: structuredClone(d) }); if (o.onmessage) o.onmessage(ev); dispatch(o, ev, true); }); } start() {} close() { this._other = null; } }
class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } }
class BroadcastChannel extends EventTarget { constructor(n) { super(); this.name = String(n); } postMessage() {} close() {} }

const workers = new Map();
class Worker extends EventTarget {
    constructor(url, opts) {
        super();
        if (arguments.length < 1) throw new TypeError("Failed to construct 'Worker': 1 argument required, but only 0 present.");
        const u = new URL(String(url), document.baseURI || N.url());
        if (opts && opts.type === 'module') throw new DOMException("Failed to construct 'Worker': Module scripts are not supported in workers yet.", 'NotSupportedError');
        let src = null;
        if (u.protocol === 'blob:') { const b = objectURLs.get(u.href); if (b) src = new TextDecoder().decode(b._buf); }
        else if (u.protocol === 'data:') { const lb = localBody(u.href); if (lb) src = new TextDecoder().decode(lb[0]); }
        else if (u.origin !== location.origin) throw new DOMException(`Failed to construct 'Worker': Script at '${u.href}' cannot be accessed from origin '${location.origin}'.`, 'SecurityError');
        this.onmessage = null; this.onmessageerror = null; this.onerror = null;
        def(this, '_id', N.workerNew(u.href, src, N.userAgent(), N.platform(), opts && opts.name != null ? String(opts.name) : ''));
        workers.set(this._id, this);
    }
    postMessage(m, t) { if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage' on 'Worker': 1 argument required, but only 0 present."); if (this._id) N.workerPost(this._id, m); }
    terminate() { if (this._id) { N.workerTerm(this._id); workers.delete(this._id); this._id = 0; } }
}
function workerEvent(id, kind, data, message, filename, lineno, colno) {
    const w = workers.get(id); if (!w) return;
    if (kind === 0) dispatch(w, new MessageEvent('message', { data }), true);
    else if (kind === 1) {
        const e = new ErrorEvent('error', { message, filename, lineno, colno, cancelable: true });
        dispatch(w, e, true);
        if (!e.defaultPrevented) N.log(3, 'Uncaught (in worker) ' + message + ' at ' + filename + ':' + lineno);
    } else if (kind === 3) dispatch(w, new MessageEvent('messageerror', {}), true);
    else dispatch(w, new Event('error', { cancelable: true }), true);
}
