/* XMLHttpRequest (xhr.spec.whatwg.org), shared by the page and worker preludes. Asynchronous requests run on the
   shared Fetch module's main fetch (redirects, CORS, referrers, data: and blob: URLs). The including prelude
   defines, after fetch.js:
     XHR_WINDOW                  true in a document (synchronous requests may not set a timeout or responseType)
     XHR_HANDLERS(proto, names)  installs the on<name> event handler attributes
     XHR_FIRE(target, event)     dispatches a trusted event
     XHR_DECODE(bytes, charset)  the response text
     XHR_DOC(xhr, text, mime)    responseXML / the 'document' response, or null
     XHR_DOC_BODY(body, headers) a Document body serialized (setting Content-Type), or undefined for other bodies
     XHR_SYNC(method, url, headers, body, credentials)  a synchronous request:
                                 [status, statusText, url, headers, ArrayBuffer body, error] */
const XHR_FORBIDDEN = new Set(['accept-charset', 'accept-encoding', 'access-control-request-headers', 'access-control-request-method', 'connection', 'content-length', 'cookie', 'cookie2', 'date', 'dnt', 'expect', 'host', 'keep-alive', 'origin', 'referer', 'set-cookie', 'te', 'trailer', 'transfer-encoding', 'upgrade', 'via']);
const xhrU32 = v => { v = Number(v); if (!Number.isFinite(v)) return 0; v = Math.trunc(v) % 4294967296; return v < 0 ? v + 4294967296 : v; };
let xhrMakeTarget = false;
class XMLHttpRequestEventTarget extends EventTarget { constructor() { if (!xhrMakeTarget) throw new TypeError('Illegal constructor'); super(); } }
XHR_HANDLERS(XMLHttpRequestEventTarget.prototype, ['abort', 'error', 'load', 'loadend', 'loadstart', 'progress', 'timeout']);
class XMLHttpRequestUpload extends XMLHttpRequestEventTarget {}
const xhrNewUpload = () => { xhrMakeTarget = true; try { return new XMLHttpRequestUpload(); } finally { xhrMakeTarget = false; } };
class XMLHttpRequest extends XMLHttpRequestEventTarget {
    constructor() {
        xhrMakeTarget = true;
        try { super(); } finally { xhrMakeTarget = false; }
        for (const [k, v] of Object.entries({ _rs: 0, _st: 0, _stt: '', _url: '', _rt: '', _to: 0, _wc: false, _gen: 0, _send: false, _async: true, _hdrs: [], _body: null, _resp: undefined, _mime: null, _upDone: true, _ctl: null, _timer: 0, _m: 'GET', _u: '' })) def(this, k, v);
        def(this, '_up', xhrNewUpload());
        def(this, '_rh', new Headers());
    }
    get readyState() { return this._rs; } get status() { return this._st; } get statusText() { return this._stt; }
    get responseURL() { return this._url.replace(/#.*$/, ''); }
    get upload() { return this._up; }
    get timeout() { return this._to; }
    set timeout(v) { if (XHR_WINDOW && this._rs === 1 && !this._async) throw new DOMException('Timeouts cannot be set for synchronous requests made from a document.', 'InvalidAccessError'); this._to = xhrU32(v); }
    get withCredentials() { return this._wc; }
    set withCredentials(v) { if (this._rs > 1 || this._send) throw new DOMException("The value may only be set if the object's state is UNSENT or OPENED.", 'InvalidStateError'); this._wc = !!v; }
    get responseType() { return this._rt; }
    set responseType(v) {
        v = String(v);
        if (!['', 'arraybuffer', 'blob', 'document', 'json', 'text'].includes(v) || (v === 'document' && !XHR_WINDOW)) return;
        if (this._rs >= 3) throw new DOMException("The response type cannot be set if the object's state is LOADING or DONE.", 'InvalidStateError');
        if (XHR_WINDOW && this._rs === 1 && !this._async) throw new DOMException('The response type cannot be changed for synchronous requests made from a document.', 'InvalidAccessError');
        this._rt = v;
    }
    _state(s) { this._rs = s; XHR_FIRE(this, new Event('readystatechange')); }
    _fire(t, tgt = this, n = 0, tot = 0, lc = false) { XHR_FIRE(tgt, new ProgressEvent(t, { lengthComputable: lc, loaded: n, total: tot })); }
    _terminate() {
        this._gen++;
        if (this._ctl) { this._ctl.aborted = true; if (this._ctl.cancel) this._ctl.cancel(); this._ctl = null; }
        if (this._timer) { clearTimeout(this._timer); this._timer = 0; }
    }
    _reset() { this._st = 0; this._stt = ''; this._url = ''; this._hdrs = []; this._body = null; this._resp = undefined; }
    open(method, url, async, user, pass) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'open' on 'XMLHttpRequest': 2 arguments required, but only ${arguments.length} present.`);
        method = String(method); url = String(url);
        if (arguments.length > 2) { async = !!async; if (user != null) user = String(user); if (pass != null) pass = String(pass); } else async = true;
        if (!FETCH_TOKEN.test(method)) throw new DOMException(`'${method}' is not a valid HTTP method.`, 'SyntaxError');
        const up = method.toUpperCase();
        if (up === 'CONNECT' || up === 'TRACE' || up === 'TRACK') throw new DOMException(`'${method}' HTTP method is unsupported.`, 'SecurityError');
        if (['DELETE', 'GET', 'HEAD', 'OPTIONS', 'POST', 'PUT'].includes(up)) method = up;
        let u; try { u = new URL(url, FETCH_BASE()); } catch (e) { throw new DOMException(`Invalid URL`, 'SyntaxError'); }
        if (user != null || pass != null) { if (u.host) { if (user != null) u.username = user; if (pass != null) u.password = pass; } }
        if (XHR_WINDOW && !async && (this._to || this._rt)) throw new DOMException('Synchronous requests from a document must not set a response type or timeout.', 'InvalidAccessError');
        this._terminate();
        this._m = method; this._u = u.href; this._async = async; this._send = false; this._upDone = true;
        this._rh = new Headers(); this._reset();
        if (this._rs !== 1) this._state(1);
    }
    setRequestHeader(k, v) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'setRequestHeader' on 'XMLHttpRequest': 2 arguments required, but only ${arguments.length} present.`);
        if (this._rs !== 1 || this._send) throw new DOMException("The object's state must be OPENED.", 'InvalidStateError');
        k = String(k); v = String(v).replace(/^[\t\n\r ]+|[\t\n\r ]+$/g, '');
        if (!FETCH_TOKEN.test(k)) throw new DOMException(`'${k}' is not a valid HTTP header field name.`, 'SyntaxError');
        if (/[\0\r\n]/.test(v) || /[^\x00-\xff]/.test(v)) throw new DOMException(`'${v}' is not a valid HTTP header field value.`, 'SyntaxError');
        if (forbiddenRequestHeader(k, v)) return;
        this._rh.append(k, v);
    }
    _hdrList() { const a = []; for (let i = 0; i + 1 < this._hdrs.length; i += 2) a.push([String(this._hdrs[i]).toLowerCase(), String(this._hdrs[i + 1])]); return a; }
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
        XHR_FIRE(this, new Event('readystatechange'));
        if (!this._async) return;
        if (!this._upDone) { this._upDone = true; this._fire(ev, this._up); this._fire('loadend', this._up); }
        this._fire(ev); this._fire('loadend');
    }
    _done(gen, status, statusText, url, hdrs, body, err) {
        if (gen !== this._gen) return;
        this._ctl = null; if (this._timer) { clearTimeout(this._timer); this._timer = 0; }
        if (err || (hdrs || []).some((x, i) => i & 1 && String(x).includes('\0'))) { this._errorSteps('error'); if (!this._async) throw new DOMException(`Failed to load '${this._u}'.`, 'NetworkError'); return; }
        if (!this._upDone) { this._upDone = true; if (this._async) { const n = this._reqLen; this._fire('progress', this._up, n, n, true); this._fire('load', this._up, n, n, true); this._fire('loadend', this._up, n, n, true); } }
        this._st = status; this._stt = statusText; this._url = url || this._u; this._hdrs = hdrs || []; this._body = body;
        const n = body ? body.byteLength : 0, len = +this.getResponseHeader('content-length'), lc = Number.isFinite(len) && len > 0, tot = lc ? len : 0;
        if (this._async) {
            this._state(2); if (gen !== this._gen) return;
            if (n) { this._state(3); if (gen !== this._gen) return; this._fire('progress', this, n, tot, lc); if (gen !== this._gen) return; }
        }
        this._send = false; this._state(4); if (gen !== this._gen) return;
        this._fire('load', this, n, tot, lc); this._fire('loadend', this, n, tot, lc);
    }
    /* an asynchronous request on the Fetch module's main fetch */
    _net(gen, body) {
        const R = { method: this._m, url: this._u, urlList: [this._u], headers: hdrOf(this._rh).list.map(e => e.slice()), body, streamBody: false,
            mode: 'cors', credentials: this._wc ? 'include' : 'same-origin', cache: 'default', redirect: 'follow', referrer: 'client', referrerPolicy: '',
            origin: location.origin, tainted: false, tainting: 'basic', redirectCount: 0, useCorsPreflight: !!(body && this._upListened) };
        const ctl = { aborted: false, cancel: null };
        this._ctl = ctl;
        (async () => {
            const st = respState.get(fetchResponse(R, await fetchMain(R, ctl)));
            const b = !st.body ? new Uint8Array(0) : st.body.bytes !== null ? st.body.bytes : await bodyReadAll(st.body.stream);
            return [st.status, st.statusText, st.urlList.length ? st.urlList[st.urlList.length - 1] : this._u, hdrOf(st.headers).list.flat(), b.slice().buffer, false];
        })().then(r => { if (!ctl.aborted) this._done(gen, ...r); }, () => { if (!ctl.aborted) this._done(gen, 0, '', this._u, [], null, true); });
    }
    send(body = null) {
        if (this._rs !== 1 || this._send) throw new DOMException("The object's state must be OPENED.", 'InvalidStateError');
        if (this._m !== 'GET' && this._m !== 'HEAD' && body != null) {
            const doc = XHR_DOC_BODY(body, this._rh);
            if (doc !== undefined) body = doc;
            const ct = this._rh.get('content-type');
            if ((doc !== undefined || typeof body === 'string' || body instanceof URLSearchParams) && ct != null)
                this._rh.set('content-type', ct.replace(/(;\s*charset\s*=\s*)("[^"]*"|[^;]*)/i, (m, p, v) => /^"?utf-8"?$/i.test(v.trim()) ? m : p + 'UTF-8'));
        }
        let b = null;
        if (this._m !== 'GET' && this._m !== 'HEAD' && body != null) {
            if (body instanceof ArrayBuffer || ArrayBuffer.isView(body) ? (typeof SharedArrayBuffer === 'function' && (body.buffer || body) instanceof SharedArrayBuffer) : false)
                throw new TypeError("Failed to execute 'send' on 'XMLHttpRequest': The provided ArrayBufferView value must not be shared.");
            const x = extractBody(body instanceof ReadableStream ? String(body) : body, false, "execute 'send' on 'XMLHttpRequest'");
            if (x.type !== null && !this._rh.has('content-type')) this._rh.set('Content-Type', x.type);
            b = x.body.bytes;
        }
        this._reqLen = b ? b.byteLength : 0;
        this._upDone = !b; this._send = true;
        const gen = this._gen;
        if (this._async) {
            this._fire('loadstart');
            if (!this._upDone) this._fire('loadstart', this._up, 0, this._reqLen, true);
            if (gen !== this._gen || this._rs !== 1 || !this._send) return;
            this._net(gen, b);
            if (this._to > 0) this._timer = setTimeout(() => { if (gen !== this._gen) return; this._terminate(); this._errorSteps('timeout'); }, this._to);
            return;
        }
        this._done(gen, ...XHR_SYNC(this._m, this._u, headersFlat(this._rh), b, this._wc));
    }
    abort() {
        this._terminate();
        if ((this._rs === 1 && this._send) || this._rs === 2 || this._rs === 3) this._errorSteps('abort');
        if (this._rs === 4) { this._rs = 0; this._reset(); }
    }
    /* XHR's "final MIME type": the override (application/octet-stream if it does not parse), else the response's, else text/xml */
    _finalMime() {
        if (this._mime !== null) { const m = parseMime(this._mime); return m ? serializeMime(m) : 'application/octet-stream'; }
        const m = extractMime(this._hdrList());
        return m ? serializeMime(m) : 'text/xml';
    }
    _mimeType() { return (this._mime || this.getResponseHeader('content-type') || '').split(';')[0].trim().toLowerCase(); }
    _charset() { const m = /;\s*charset\s*=\s*("?)([^";]*)\1/i.exec(this._mime || this.getResponseHeader('content-type') || ''); return m ? m[2].trim() : null; }
    _text() { return this._body ? XHR_DECODE(this._body, this._charset()) : ''; }
    get responseText() {
        if (this._rt && this._rt !== 'text') throw new DOMException("The value is only accessible if the object's 'responseType' is '' or 'text' (was '" + this._rt + "').", 'InvalidStateError');
        return this._rs < 3 ? '' : this._text();
    }
    _doc() {
        if (this._resp !== undefined) return this._resp;
        return this._resp = this._body ? XHR_DOC(this, this._text(), this._mimeType()) : null;
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
        const b = this._body || new ArrayBuffer(0);
        switch (this._rt) {
        case 'arraybuffer': return this._resp = b instanceof ArrayBuffer ? b : b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
        case 'blob': return this._resp = new Blob([b], { type: this._finalMime() });
        case 'json': try { return this._resp = JSON.parse(new TextDecoder().decode(b)); } catch (e) { return this._resp = null; }
        }
        return null;
    }
}
/* the upload object's listeners make a cross-origin request preflighted (registered before send()) */
{
    const add = EventTarget.prototype.addEventListener;
    Object.defineProperty(XMLHttpRequest.prototype, '_upListened', { get() { return !!this._up._listened; }, configurable: true });
    Object.defineProperty(XMLHttpRequestUpload.prototype, 'addEventListener', { value: function addEventListener(...a) { def(this, '_listened', true); return add.apply(this, a); }, writable: true, configurable: true });
}
XHR_HANDLERS(XMLHttpRequest.prototype, ['readystatechange']);
for (const [k, v] of Object.entries({ UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 })) { Object.defineProperty(XMLHttpRequest, k, { value: v, enumerable: true }); Object.defineProperty(XMLHttpRequest.prototype, k, { value: v, enumerable: true }); }
/* WebIDL brand checks: an accessor called on anything but a real XMLHttpRequest(EventTarget) throws */
const xhrBrand = (proto, ok) => {
    for (const k of Object.getOwnPropertyNames(proto)) {
        const d = Object.getOwnPropertyDescriptor(proto, k);
        if (!d.get) continue;
        const g = d.get, s = d.set;
        d.get = function () { if (!ok(this)) throw new TypeError('Illegal invocation'); return g.call(this); };
        Object.defineProperty(d.get, 'name', { value: 'get ' + k });
        if (s) { d.set = function (v) { if (!ok(this)) throw new TypeError('Illegal invocation'); return s.call(this, v); }; Object.defineProperty(d.set, 'name', { value: 'set ' + k }); }
        Object.defineProperty(proto, k, d);
    }
};
xhrBrand(XMLHttpRequestEventTarget.prototype, o => o instanceof XMLHttpRequestEventTarget && Object.getPrototypeOf(o) !== XMLHttpRequestEventTarget.prototype && o !== XMLHttpRequest.prototype && o !== XMLHttpRequestUpload.prototype);
xhrBrand(XMLHttpRequest.prototype, o => o !== null && typeof o === 'object' && Object.prototype.hasOwnProperty.call(o, '_rs'));
