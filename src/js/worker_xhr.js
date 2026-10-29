// Dedicated-worker XHR uses native async jobs and the same byte decoder as
// window XHR. This factory contains no document or main-isolate dependencies.
function createWorkerXMLHttpRequestInterfaces(ops) {
const { W, EventTarget, Event, ProgressEvent, DOMException, Headers, URL, Blob,
    URLSearchParams, headerOps, bodyInit, blobFileOps, objectURLs, location,
    dispatch, TextEncoder } = ops;
const def = (object, name, value) => Object.defineProperty(object, name, { value, writable: true, configurable: true });
const illegal = () => { throw new TypeError('Illegal constructor'); };
const toU32 = value => Number(value) >>> 0;
const xhrBrands = new WeakSet(), targetBrands = new WeakSet(), handlers = new WeakMap();
function installHandlers(prototype, unused, names) {
    for (const name of names) { Object.defineProperty(prototype, 'on' + name, {
        enumerable: true, configurable: true,
        get() { if (!targetBrands.has(this)) throw new TypeError('Illegal invocation'); return handlers.get(this)?.get(name)?.value || null; },
        set(value) {
            if (!targetBrands.has(this)) throw new TypeError('Illegal invocation');
            let map = handlers.get(this); if (!map) { map = new Map(); handlers.set(this, map); }
            let state = map.get(name);
            value = typeof value === 'function' ? value : null;
            if (state) {
                state.value = value;
                if (!value) { this.removeEventListener(name, state.listener); map.delete(name); }
            } else if (value) {
                state = { value, listener: null };
                state.listener = function(event) { if (state.value) return state.value.call(this, event); };
                map.set(name, state); this.addEventListener(name, state.listener);
            }
        },
    });
    const descriptor = Object.getOwnPropertyDescriptor(prototype, 'on' + name);
    Object.defineProperty(descriptor.get, 'name', { value: 'get on' + name });
    Object.defineProperty(descriptor.set, 'name', { value: 'set on' + name });
    }
}
const N = {
    fetch(method, url, headers, body, callback, unusedHead, unusedChunk, credentials) {
        return W.send(method, url, headers, body, credentials, callback, location.origin);
    },
    fetchSync(method, url, headers, body, credentials, timeout) {
        return W.sendSync(method, url, headers, body, credentials, timeout || 0, location.origin);
    },
    abort: W.abort, responseTextDecode: W.responseTextDecode, decode: W.decode,
    encode: value => new TextEncoder().encode(value),
};
const localBody = url => {
    if (!url.startsWith('blob:')) return undefined;
    const blob = objectURLs.get(url.replace(/#.*$/, ''));
    return blobFileOps.isBlob(blob) ? [blobFileOps.copyBuffer(blob), blobFileOps.type(blob)] : null;
};
function blobResponse(url, lb, range) {
    const buf = lb[0], n = buf.byteLength, h = ['Content-Type', lb[1], 'Content-Length', String(n)];
    if (range == null || !url.startsWith('blob:')) return [200, 'OK', h, buf];
    const m = /^[\t ]*bytes[\t ]*=[\t ]*(\d*)[\t ]*-[\t ]*(\d*)[\t ]*$/.exec(String(range));
    if (!m || (m[1] === '' && m[2] === '')) return null;
    let a, b;
    if (m[1] === '') { a = Math.max(0, n - +m[2]); b = n - 1; }
    else { a = +m[1]; b = m[2] === '' ? n - 1 : Math.min(+m[2], n - 1); if (m[2] !== '' && +m[2] < a) return null; }
    if (a >= n || (m[1] === '' && +m[2] === 0)) return null;
    return [206, 'Partial Content', ['Content-Type', lb[1], 'Content-Length', String(b - a + 1), 'Content-Range', `bytes ${a}-${b}/${n}`], buf.slice(a, b + 1)];
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

// Response headers have a separate visibility policy from request headers.
// Cookies are never exposed to script; cross-origin callers see only the CORS
// safelist and explicitly exposed names (credentialed '*' is a literal name).
function responseHeaders(requestURL, responseURL, headers, credentials) {
    let cross = false;
    try {
        const request = new URL(requestURL), response = new URL(responseURL || requestURL);
        cross = !/^(data|blob|about):$/.test(response.protocol) &&
            (request.origin !== location.origin || response.origin !== location.origin);
    } catch (_) { cross = true; }
    const visible = new Set(['cache-control', 'content-language', 'content-length', 'content-type', 'expires', 'last-modified', 'pragma']);
    let wildcard = false;
    if (cross) {
        for (let index = 0; index + 1 < headers.length; index += 2)
            if (String(headers[index]).toLowerCase() === 'access-control-expose-headers')
                for (const name of String(headers[index + 1]).split(',')) {
                    const lower = name.trim().toLowerCase();
                    if (lower === '*' && credentials !== 'include') wildcard = true;
                    else if (lower) visible.add(lower);
                }
    }
    const filtered = [];
    for (let index = 0; index + 1 < headers.length; index += 2) {
        const lower = String(headers[index]).toLowerCase();
        if (lower === 'set-cookie' || lower === 'set-cookie2') continue;
        if (!cross || wildcard || visible.has(lower)) filtered.push(headers[index], headers[index + 1]);
    }
    return filtered;
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
function corsSend(credentials, onId, method, url, flat, body, cb, st) {
    const hs = needsPreflight(method, url, flat);
    const send = () => onId(N.fetch(method, url, flat, body, cb, st && st.head, st && st.chunk, credentials));
    if (!hs) return send();
    onId(N.fetch('OPTIONS', url, preflightHdrs(method, hs), null, (status, statusText, u, hdrs, b, err) => {
        if (err || !preflightOk(url, method, hs, status, u, hdrs || [], credentials === 'include')) return cb(0, '', url, [], null, true);
        send();
    }, undefined, undefined, 'omit'));
}
function corsSendSync(credentials, method, url, flat, body, timeout) {
    const hs = needsPreflight(method, url, flat);
    if (hs) { const p = N.fetchSync('OPTIONS', url, preflightHdrs(method, hs), null, 'omit', timeout); if (p[5] || !preflightOk(url, method, hs, p[0], p[2], p[3] || [], credentials === 'include')) return [0, '', url, [], null, true]; }
    return N.fetchSync(method, url, flat, body, credentials, timeout);
}

const xhrTargetToken = {};
class XMLHttpRequestEventTarget extends EventTarget {
    constructor(...args) {
        if (args[0] !== xhrTargetToken) illegal();
        super(); targetBrands.add(this);
    }
}
installHandlers(XMLHttpRequestEventTarget.prototype, false, ['abort', 'error', 'load', 'loadend', 'loadstart', 'progress', 'timeout']);
const XHR_FORBIDDEN = new Set(['accept-charset', 'accept-encoding', 'access-control-request-headers', 'access-control-request-method', 'connection', 'content-length', 'cookie', 'cookie2', 'date', 'dnt', 'expect', 'host', 'keep-alive', 'origin', 'referer', 'set-cookie', 'te', 'trailer', 'transfer-encoding', 'upgrade', 'via']);
const HTTP_TOKEN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
let xhrMakeUpload = false;
class XMLHttpRequestUpload extends XMLHttpRequestEventTarget { constructor() { if (!xhrMakeUpload) illegal(); super(xhrTargetToken); } }
class XMLHttpRequest extends XMLHttpRequestEventTarget {
    constructor() {
        super(xhrTargetToken); xhrBrands.add(this);
        for (const [k, v] of Object.entries({ _rs: 0, _st: 0, _stt: '', _url: '', _rt: '', _to: 0, _wc: false, _gen: 0, _send: false, _async: true, _hdrs: [], _body: null, _resp: undefined, _mime: null, _up: null, _upDone: true, _id: 0, _timer: 0, _m: 'GET', _u: '' })) def(this, k, v);
        def(this, '_rh', new Headers());
    }
    get readyState() { return this._rs; } get status() { return this._st; } get statusText() { return this._stt; }
    get responseURL() { return this._url.replace(/#.*$/, ''); }
    get upload() { if (!this._up) { xhrMakeUpload = true; try { this._up = new XMLHttpRequestUpload(); } finally { xhrMakeUpload = false; } } return this._up; }
    get timeout() { return this._to; }
    set timeout(v) { this._to = toU32(v); }
    get withCredentials() { return this._wc; }
    set withCredentials(v) { if (this._rs > 1 || this._send) throw new DOMException("The value may only be set if the object's state is UNSENT or OPENED.", 'InvalidStateError'); this._wc = !!v; }
    get responseType() { return this._rt; }
    set responseType(v) {
        v = String(v); if (v === 'document' || !['', 'arraybuffer', 'blob', 'json', 'text'].includes(v)) return;
        if (this._rs >= 3) throw new DOMException("The response type cannot be set if the object's state is LOADING or DONE.", 'InvalidStateError');
        this._rt = v;
    }
    _state(s) { this._rs = s; dispatch(this, new Event('readystatechange')); }
    _fire(t, tgt = this, n = 0, tot = 0, lc = false) { dispatch(tgt, new ProgressEvent(t, { lengthComputable: lc, loaded: n, total: tot })); }
    _terminate() { this._gen++; if (this._id) { N.abort(this._id); this._id = 0; } if (this._timer) { clearTimeout(this._timer); this._timer = 0; } }
    _reset() { this._st = 0; this._stt = ''; this._url = ''; this._hdrs = []; this._body = null; this._resp = undefined; }
    open(method, url, ...optional) {
        let [async, user, pass] = optional;
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'open' on 'XMLHttpRequest': 2 arguments required, but only ${arguments.length} present.`);
        method = String(method); url = String(url);
        if (arguments.length > 2) { async = !!async; if (user != null) user = String(user); if (pass != null) pass = String(pass); } else async = true;
        if (!HTTP_TOKEN.test(method)) throw new DOMException(`'${method}' is not a valid HTTP method.`, 'SyntaxError');
        const up = method.toUpperCase();
        if (up === 'CONNECT' || up === 'TRACE' || up === 'TRACK') throw new DOMException(`'${method}' HTTP method is unsupported.`, 'SecurityError');
        if (['DELETE', 'GET', 'HEAD', 'OPTIONS', 'POST', 'PUT'].includes(up)) method = up;
        let u; try { u = new URL(url, location.href); } catch (e) { throw new DOMException(`Invalid URL`, 'SyntaxError'); }
        if (user != null || pass != null) { if (u.host) { if (user != null) u.username = user; if (pass != null) u.password = pass; } }
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
        if (!arguments.length) throw new TypeError('getResponseHeader requires a header name');
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
    overrideMimeType(m) { if (!arguments.length) throw new TypeError('overrideMimeType requires a MIME type'); if (this._rs >= 3) throw new DOMException("MimeType cannot be overridden when the state is LOADING or DONE.", 'InvalidStateError'); this._mime = String(m); }
    _errorSteps(ev) {
        const gen = this._gen;
        this._rs = 4; this._send = false; this._reset();
        dispatch(this, new Event('readystatechange'));
        if (!this._async || gen !== this._gen) return;
        if (!this._upDone) {
            this._upDone = true;
            if (this._up) {
                this._fire(ev, this._up); if (gen !== this._gen) return;
                this._fire('loadend', this._up); if (gen !== this._gen) return;
            }
        }
        this._fire(ev); if (gen !== this._gen) return; this._fire('loadend');
    }
    _done(gen, status, statusText, url, hdrs, body, err, timedOut = false) {
        if (gen !== this._gen) return;
        this._id = 0; if (this._timer) { clearTimeout(this._timer); this._timer = 0; }
        if (err || !corsOk(this._u, url, hdrs || [], this._wc)) { this._errorSteps(timedOut ? 'timeout' : 'error'); if (!this._async) throw new DOMException(`Failed to load '${this._u}'.`, timedOut ? 'TimeoutError' : 'NetworkError'); return; }
        if (!this._upDone) { this._upDone = true; if (this._up && this._async) { const n = this._reqLen; this._fire('progress', this._up, n, n, true); if (gen !== this._gen) return; this._fire('load', this._up, n, n, true); if (gen !== this._gen) return; this._fire('loadend', this._up, n, n, true); if (gen !== this._gen) return; } }
        this._st = status; this._stt = statusText; this._url = url || this._u; this._hdrs = responseHeaders(this._u, url, hdrs || [], this._wc ? 'include' : 'same-origin'); this._body = body;
        const n = body ? body.byteLength : 0, len = +hget(this._hdrs, 'content-length'), lc = Number.isFinite(len) && len > 0, tot = lc ? len : 0;
        if (this._async) {
            this._state(2); if (gen !== this._gen) return;
            if (n) { this._state(3); if (gen !== this._gen) return; this._fire('progress', this, n, tot, lc); if (gen !== this._gen) return; }
        }
        this._send = false; this._state(4); if (gen !== this._gen) return;
        this._fire('load', this, n, tot, lc); if (gen !== this._gen) return; this._fire('loadend', this, n, tot, lc);
    }
    send(body = null) {
        if (this._rs !== 1 || this._send) throw new DOMException("The object's state must be OPENED.", 'InvalidStateError');
        if (this._m !== 'GET' && this._m !== 'HEAD' && body != null) {
            const isDoc = false; // Document is not exposed in a worker.
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
        const credentials = this._wc ? 'include' : 'same-origin';
        if (!this._async) { const r = corsSendSync(credentials, this._m, this._u, withOrigin(headerOps.flat(this._rh), this._u, this._m, 'cors'), b, this._to); this._done(gen, ...r); return; }
        corsSend(credentials, (i) => this._id = i, this._m, this._u, withOrigin(headerOps.flat(this._rh), this._u, this._m, 'cors'), b, (...a) => this._done(gen, ...a));
        if (this._to > 0) this._timer = setTimeout(() => { if (gen !== this._gen) return; this._terminate(); this._errorSteps('timeout'); }, this._to);
    }
    abort() {
        this._terminate(); const gen = this._gen;
        if ((this._rs === 1 && this._send) || this._rs === 2 || this._rs === 3) this._errorSteps('abort');
        if (gen === this._gen && this._rs === 4) { this._rs = 0; this._reset(); }
    }
    _mimeType() { return (this._mime || this.getResponseHeader('content-type') || '').split(';')[0].trim().toLowerCase(); }
    _text() { return this._body ? N.responseTextDecode(this._body, this.getResponseHeader('content-type') || '', this._mime || '') : ''; }
    get responseText() {
        if (this._rt && this._rt !== 'text') throw new DOMException("The value is only accessible if the object's 'responseType' is '' or 'text' (was '" + this._rt + "').", 'InvalidStateError');
        return this._rs < 3 ? '' : this._text();
    }
    get response() {
        if (this._rt === '' || this._rt === 'text') return this.responseText;
        if (this._rs !== 4) return null;
        if (this._resp !== undefined) return this._resp;
        const b = this._body || new Uint8Array(0);
        switch (this._rt) {
        case 'arraybuffer': return this._resp = b instanceof ArrayBuffer ? b : b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
        case 'blob': return this._resp = new Blob([b], { type: this._mime || this.getResponseHeader('content-type') || '' });
        case 'json': try { return this._resp = JSON.parse(this._body ? N.decode(this._body) : ''); } catch (e) { return this._resp = null; }
        }
        return null;
    }
}
installHandlers(XMLHttpRequest.prototype, false, ['readystatechange']);
for (const [k, v] of Object.entries({ UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 })) { Object.defineProperty(XMLHttpRequest, k, { value: v, enumerable: true }); Object.defineProperty(XMLHttpRequest.prototype, k, { value: v, enumerable: true }); }

// Web IDL receiver checks also apply to getters and methods on worker XHR.
for (const [name, descriptor] of Object.entries(Object.getOwnPropertyDescriptors(XMLHttpRequest.prototype))) {
    if (name === 'constructor' || name.startsWith('_')) continue;
    for (const key of ['value', 'get', 'set']) if (typeof descriptor[key] === 'function') {
        const original = descriptor[key];
        const checked = function(...args) {
            if (!xhrBrands.has(this)) throw new TypeError('Illegal invocation');
            return Reflect.apply(original, this, args);
        };
        Object.defineProperties(checked, { name: { value: original.name }, length: { value: original.length } });
        descriptor[key] = checked;
    }
    descriptor.enumerable = true; Object.defineProperty(XMLHttpRequest.prototype, name, descriptor);
}
for (const Constructor of [XMLHttpRequest, XMLHttpRequestEventTarget, XMLHttpRequestUpload])
    Object.defineProperty(Constructor.prototype, Symbol.toStringTag, { value: Constructor.name, configurable: true });
return { XMLHttpRequest, XMLHttpRequestEventTarget, XMLHttpRequestUpload,
    transport: { send: corsSend, sendSync: corsSendSync, cancel: N.abort, allowed: corsOk, withOrigin, responseHeaders } };
}
