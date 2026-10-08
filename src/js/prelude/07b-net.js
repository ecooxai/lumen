class Headers {
    constructor(init) { def(this, '_m', new Map()); if (init instanceof Headers) for (const [k, v] of init) this.append(k, v); else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v); else if (init && typeof init === 'object') for (const k of Object.keys(init)) this.append(k, init[k]); }
    append(k, v) { k = String(k).toLowerCase(); v = String(v).trim(); const o = this._m.get(k); this._m.set(k, o == null ? v : k === 'set-cookie' ? o + '\n' + v : o + ', ' + v); }
    set(k, v) { this._m.set(String(k).toLowerCase(), String(v).trim()); } get(k) { return this._m.get(String(k).toLowerCase()) ?? null; }
    has(k) { return this._m.has(String(k).toLowerCase()); } delete(k) { this._m.delete(String(k).toLowerCase()); }
    getSetCookie() { const v = this._m.get('set-cookie'); return v ? v.split('\n') : []; }
    forEach(fn, self) { for (const [k, v] of this) fn.call(self, v, k, this); }
    *entries() { yield* [...this._m].sort((a, b) => a[0] < b[0] ? -1 : 1); } keys() { return [...this._m.keys()].sort()[Symbol.iterator](); } values() { return [...this.entries()].map(e => e[1])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
    _flat() { const a = []; for (const [k, v] of this._m) a.push(k, v); return a; }
}
const fromFlat = (a) => { const h = new Headers(); for (let i = 0; i + 1 < a.length; i += 2) h.append(a[i], a[i + 1]); return h; };
function bodyInit(b, h) {
    if (b == null) return null;
    if (typeof b === 'string') { if (h && !h.has('content-type')) h.set('content-type', 'text/plain;charset=UTF-8'); return N.encode(b); }
    if (b instanceof URLSearchParams) { if (h && !h.has('content-type')) h.set('content-type', 'application/x-www-form-urlencoded;charset=UTF-8'); return N.encode(b.toString()); }
    if (b instanceof FormData) { const bd = '----LumenFormBoundary' + Math.random().toString(36).slice(2); if (h) h.set('content-type', 'multipart/form-data; boundary=' + bd); let s = ''; for (const [k, v] of b) s += `--${bd}\r\nContent-Disposition: form-data; name="${k}"\r\n\r\n${typeof v === 'string' ? v : ''}\r\n`; return N.encode(s + `--${bd}--\r\n`); }
    if (b instanceof Blob) { if (h && b.type && !h.has('content-type')) h.set('content-type', b.type); return b._buf; }
    return toBytes(b);
}
class Body {
    _take() { if (this.bodyUsed) return Promise.reject(new TypeError('Body has already been consumed.')); def(this, '_used', true); return Promise.resolve(this._b || new ArrayBuffer(0)); }
    get bodyUsed() { return !!this._used; }
    get body() { if (!this._b) return null; const b = this._b; return new ReadableStream({ start(c) { c.enqueue(new Uint8Array(b)); c.close(); } }); }
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
function fetch(input, init) {
    let req; try { req = new Request(input, init); } catch (e) { return Promise.reject(e); }
    return new Promise((resolve, reject) => {
        if (req.signal.aborted) return reject(req.signal.reason);
        const lb = localBody(req.url);
        if (lb !== undefined) { if (!lb) return reject(new TypeError('Failed to fetch')); const r = new Response(lb[0], { headers: { 'content-type': lb[1] } }); r.url = req.url; return resolve(r); }
        const id = N.fetch(req.method, req.url, req.headers._flat(), req._b, (status, statusText, url, hdrs, body, err) => {
            if (err) return reject(new TypeError('Failed to fetch'));
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
class XMLHttpRequest extends XMLHttpRequestEventTarget {
    constructor() { super(); Object.assign(this, { readyState: 0, status: 0, statusText: '', responseURL: '', responseType: '', timeout: 0, withCredentials: false, onreadystatechange: null }); def(this, '_rh', new Headers()); def(this, '_hdrs', []); def(this, '_body', null); this.upload = new XMLHttpRequestEventTarget(); }
    open(m, u, async = true) { this._m = String(m).toUpperCase(); this._u = new URL(String(u), document.baseURI).href; this._async = async !== false; this._rh = new Headers(); this._state(1); }
    setRequestHeader(k, v) { if (this.readyState !== 1) throw new DOMException("The object's state must be OPENED.", 'InvalidStateError'); this._rh.append(k, v); }
    getResponseHeader(k) { if (this.readyState < 2) return null; const h = fromFlat(this._hdrs); return h.get(k); }
    getAllResponseHeaders() { if (this.readyState < 2) return ''; let s = ''; for (let i = 0; i + 1 < this._hdrs.length; i += 2) s += this._hdrs[i].toLowerCase() + ': ' + this._hdrs[i + 1] + '\r\n'; return s; }
    overrideMimeType() {}
    _state(s) { this.readyState = s; const ev = new Event('readystatechange'); if (typeof this.onreadystatechange === 'function') { try { this.onreadystatechange(ev); } catch (e) { report(e); } } if (this.__ls) dispatch(this, ev); }
    _fire(t, tgt = this) { const n = this._body ? this._body.byteLength : 0; dispatch(tgt, new ProgressEvent(t, { lengthComputable: true, loaded: n, total: n })); }
    _done(status, statusText, url, hdrs, body, err) {
        if (this._aborted) return;
        if (this._timer) clearTimeout(this._timer);
        if (err) { this.status = 0; this._state(4); this._fire('error'); this._fire('loadend'); return; }
        this.status = status; this.statusText = statusText; this.responseURL = url; this._hdrs = hdrs; this._body = body;
        this._state(2); this._state(3); this._fire('progress'); this._state(4); this._fire('load'); this._fire('loadend');
    }
    send(body) {
        if (this.readyState !== 1) throw new DOMException("The object's state must be OPENED.", 'InvalidStateError');
        const b = this._m === 'GET' || this._m === 'HEAD' ? null : bodyInit(body, this._rh);
        const lb = localBody(this._u);
        if (lb !== undefined) { const f = () => this._done(lb ? 200 : 0, lb ? 'OK' : '', this._u, lb ? ['content-type', lb[1]] : [], lb ? lb[0] : null, !lb); if (this._async) setTimeout(f); else f(); return; }
        if (!this._async) { const r = N.fetchSync(this._m, this._u, this._rh._flat(), b); this._done(...r); return; }
        this._fire('loadstart');
        this._id = N.fetch(this._m, this._u, this._rh._flat(), b, (...a) => this._done(...a));
        if (this.timeout > 0) this._timer = setTimeout(() => { N.abort(this._id); this._aborted = true; this._state(4); this._fire('timeout'); this._fire('loadend'); }, this.timeout);
    }
    abort() { if (this._id) N.abort(this._id); this._aborted = true; if (this.readyState > 1 && this.readyState < 4) { this._state(4); this._fire('abort'); this._fire('loadend'); } this.readyState = 0; }
    get responseText() { if (this.responseType && this.responseType !== 'text') throw new DOMException("The value is only accessible if the object's 'responseType' is '' or 'text'.", 'InvalidStateError'); return this._body ? N.decode(this._body) : ''; }
    get response() {
        if (this.readyState !== 4 || !this._body) return this.responseType === '' || this.responseType === 'text' ? (this._body ? N.decode(this._body) : '') : null;
        switch (this.responseType) {
        case 'arraybuffer': return this._body; case 'blob': return new Blob([this._body], { type: this.getResponseHeader('content-type') || '' });
        case 'json': try { return JSON.parse(N.decode(this._body)); } catch (e) { return null; }
        case 'document': return null; default: return N.decode(this._body);
        }
    }
    get responseXML() { return null; }
}
Object.assign(XMLHttpRequest, { UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 });
class WebSocket extends EventTarget {
    constructor(url) { super(); this.url = String(url); this.readyState = 3; this.protocol = ''; this.extensions = ''; this.bufferedAmount = 0; this.binaryType = 'blob'; setTimeout(() => { for (const t of ['error', 'close']) { const ev = t === 'close' ? Object.assign(new Event('close'), { code: 1006, reason: '', wasClean: false }) : new Event('error'); if (this['on' + t]) this['on' + t](ev); dispatch(this, ev); } }); }
    send() { throw new DOMException('WebSocket is not supported yet', 'InvalidStateError'); } close() {}
}
Object.assign(WebSocket, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
class MessagePort extends EventTarget { constructor() { super(); this.onmessage = null; def(this, '_other', null); } postMessage(d) { const o = this._other; if (o) setTimeout(() => { const ev = new MessageEvent('message', { data: structuredClone(d) }); if (o.onmessage) o.onmessage(ev); dispatch(o, ev); }); } start() {} close() { this._other = null; } }
class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } }
class BroadcastChannel extends EventTarget { constructor(n) { super(); this.name = String(n); } postMessage() {} close() {} }
