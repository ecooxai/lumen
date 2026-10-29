const headerOps = createHeadersInterface({
    getState: object => N.domStateGet(object, 6),
    setState: (object, state) => N.domStateSet(object, 6, state),
});
const { Headers } = headerOps;

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
function corsSend(credentials, onId, method, url, flat, body, cb, st) {
    const hs = needsPreflight(method, url, flat);
    const send = () => onId(N.fetch(method, url, flat, body, cb, st && st.head, st && st.chunk, credentials));
    if (!hs) return send();
    onId(N.fetch('OPTIONS', url, preflightHdrs(method, hs), null, (status, statusText, u, hdrs, b, err) => {
        if (err || !preflightOk(url, method, hs, status, u, hdrs || [], credentials === 'include')) return cb(0, '', url, [], null, true);
        send();
    }, undefined, undefined, 'omit'));
}
function corsSendSync(credentials, method, url, flat, body) {
    const hs = needsPreflight(method, url, flat);
    if (hs) { const p = N.fetchSync('OPTIONS', url, preflightHdrs(method, hs), null, 'omit'); if (p[5] || !preflightOk(url, method, hs, p[0], p[2], p[3] || [], credentials === 'include')) return [0, '', url, [], null, true]; }
    return N.fetchSync(method, url, flat, body, credentials);
}

const fromFlat = (a) => { const h = new Headers(); for (let i = 0; i + 1 < a.length; i += 2) h.append(a[i], a[i + 1]); return h; };
function bodyInit(b, h) {
    if (b == null) return null;
    if (typeof b === 'string') { if (h && !h.has('content-type')) h.set('Content-Type', 'text/plain;charset=UTF-8'); return N.encode(b); }
    if (b instanceof URLSearchParams) { if (h && !h.has('content-type')) h.set('Content-Type', 'application/x-www-form-urlencoded;charset=UTF-8'); return N.encode(b.toString()); }
    if (b instanceof FormData) { const bd = '----LumenFormBoundary' + Math.random().toString(36).slice(2); if (h && !h.has('content-type')) h.set('Content-Type', 'multipart/form-data; boundary=' + bd); return multipartBody([...b], bd).buffer; }
    if (blobFileOps.isBlob(b)) { const type = blobFileOps.type(b); if (h && type && !h.has('content-type')) h.set('Content-Type', type); return blobFileOps.copyBuffer(b); }
    if (b instanceof ReadableStream) return b;
    return toBytes(b);
}
// Web IDL converts URL and the BodyInit union before running sendBeacon's
// algorithm. Extracting a stream is later and fails with keepalive enabled.
function beaconBodyValue(value) {
    if (value == null) return null;
    if (blobFileOps.isBlob(value) || value instanceof FormData || value instanceof URLSearchParams ||
        value instanceof ReadableStream || value instanceof ArrayBuffer || ArrayBuffer.isView(value)) return value;
    if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol value to a string');
    return String(value);
}
function queueBeacon(url, data) {
    const parsed = N.urlParse(url, document.baseURI);
    if (!parsed || (parsed[1] !== 'http:' && parsed[1] !== 'https:'))
        throw new TypeError('sendBeacon requires a valid HTTP or HTTPS URL');
    if (data instanceof ReadableStream) throw new TypeError('A keepalive request cannot have a stream body');
    const headers = new Headers();
    const body = bodyInit(data, headers);
    return N.sendBeacon(parsed[0], body, headers.get('content-type') || '');
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
    formData() {
        return this._take().then(body => {
            const essence = (this.headers.get('content-type') || '').split(';', 1)[0].trim().toLowerCase();
            if (essence !== 'application/x-www-form-urlencoded') throw new TypeError('Unsupported form-data media type');
            const bytes = body instanceof ArrayBuffer ? new Uint8Array(body) :
                new Uint8Array(body.buffer, body.byteOffset, body.byteLength);
            const form = new FormData();
            for (const [name, value] of urlSearchParamsOps.parseBytes(bytes)) form.append(name, value);
            return form;
        });
    }
}
class Request extends Body {
    constructor(input, init = {}) {
        super(); const src = input instanceof Request ? input : null;
        if (init == null) init = {};
        if (typeof init !== 'object' && typeof init !== 'function') throw new TypeError('Expected RequestInit dictionary');
        this.url = new URL(src ? src.url : String(input), document.baseURI).href;
        this.method = String(init.method || (src && src.method) || 'GET').toUpperCase();
        const suppliedHeaders = init.headers;
        this.signal = init.signal || (src && src.signal) || new AbortController().signal;
        for (const [key, fallback] of Object.entries({ credentials: 'same-origin', mode: 'cors', cache: 'default', redirect: 'follow', referrer: 'about:client', integrity: '', keepalive: false, priority: 'auto' })) {
            const value = init[key] !== undefined ? init[key] : src ? src[key] : fallback;
            this[key] = key === 'keepalive' ? !!value : String(value);
        }
        if (!['omit', 'same-origin', 'include'].includes(this.credentials)) throw new TypeError('Invalid Request credentials mode');
        if (!['same-origin', 'no-cors', 'cors'].includes(this.mode)) throw new TypeError('Invalid Request mode');
        if (this.mode === 'no-cors' && !['GET', 'HEAD', 'POST'].includes(this.method)) throw new TypeError('Invalid no-cors method');
        this.headers = headerOps.create(suppliedHeaders === undefined ? src ? src.headers : undefined : suppliedHeaders,
                                        this.mode === 'no-cors' ? 'request-no-cors' : 'request');
        def(this, '_b', init.body !== undefined ? bodyInit(init.body, this.headers) : src ? src._b : null);
    }
    clone() {
        if (this.bodyUsed || (this._b instanceof ReadableStream && this._b.locked)) throw new TypeError('Request body has already been consumed or is locked.');
        const r = new Request(this);
        if (this._b instanceof ReadableStream) { const [a, b] = this._b.tee(); this._b = a; r._b = b; }
        return r;
    }
}
class Response extends Body {
    constructor(body = null, init = {}) { super(); this.headers = headerOps.create(init.headers, 'response'); def(this, '_b', bodyInit(body, this.headers)); this.status = init.status ?? 200; this.statusText = init.statusText || ''; this.type = 'default'; this.url = ''; this.redirected = false; }
    get ok() { return this.status >= 200 && this.status < 300; }
    clone() { const r = new Response(null, this); if (this._b instanceof ReadableStream) { const [a, b] = this._b.tee(); this._b = a; r._b = b; } else r._b = this._b; r.url = this.url; r.type = this.type; headerOps.setGuard(r.headers, headerOps.guard(this.headers)); return r; }
    static json(d, i) { const r = new Response(JSON.stringify(d), i); r.headers.set('content-type', 'application/json'); return r; }
    static error() { const r = new Response(null, { status: 0 }); r.type = 'error'; return immutableResponse(r); }
    static redirect(u, s = 302) { return immutableResponse(new Response(null, { status: s, headers: { location: String(u) } })); }
}
function immutableResponse(response) { headerOps.setGuard(response.headers, 'immutable'); return response; }
// Data URLs carry bytes. Percent decoding must not apply UTF-8 validation;
// the consumer (XHR, Response.text, ArrayBuffer, etc.) chooses decoding later.
function dataURLMimeType(value) {
    const token = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
    const quoted = /^[\t\x20-\x7E\x80-\xFF]*$/;
    value = value.replace(/^[\t\n\r ]+|[\t\n\r ]+$/g, '');
    const slash = value.indexOf('/');
    if (slash <= 0 || !token.test(value.slice(0, slash))) return null;
    let end = value.indexOf(';', slash + 1); if (end < 0) end = value.length;
    const subtype = value.slice(slash + 1, end).replace(/[\t\n\r ]+$/g, '');
    if (!token.test(subtype)) return null;
    let result = value.slice(0, slash).toLowerCase() + '/' + subtype.toLowerCase();
    const seen = new Set();
    for (let position = end; position < value.length;) {
        position++;
        while (position < value.length && /[\t\n\r ]/.test(value[position])) position++;
        const start = position;
        while (position < value.length && value[position] !== '=' && value[position] !== ';') position++;
        const name = value.slice(start, position).toLowerCase();
        if (position === value.length) break;
        if (value[position] === ';') continue;
        position++;
        let parameter = '';
        if (value[position] === '"') {
            position++;
            while (position < value.length) {
                let ch = value[position++];
                if (ch === '"') break;
                if (ch === '\\' && position < value.length) ch = value[position++];
                parameter += ch;
            }
            while (position < value.length && value[position] !== ';') position++;
        } else {
            const start = position;
            while (position < value.length && value[position] !== ';') position++;
            parameter = value.slice(start, position).replace(/[\t\n\r ]+$/g, '');
            if (!parameter) continue;
        }
        if (!token.test(name) || !quoted.test(parameter) || seen.has(name)) continue;
        seen.add(name);
        result += ';' + name + '=' + (token.test(parameter) ? parameter : '"' + parameter.replace(/["\\]/g, '\\$&') + '"');
    }
    return result;
}
function localBody(url) {
    if (url.startsWith('blob:')) { const b = objectURLs.get(url); return blobFileOps.isBlob(b) ? [blobFileOps.copyBuffer(b), blobFileOps.type(b)] : null; }
    if (!url.startsWith('data:')) return undefined;
    const fragment = url.indexOf('#'); if (fragment >= 0) url = url.slice(0, fragment);
    const comma = url.indexOf(','); if (comma < 0) return null;
    let mime = url.slice(5, comma).replace(/^[\t\n\f\r ]+|[\t\n\f\r ]+$/g, '');
    const encoded = new Uint8Array(N.encode(url.slice(comma + 1)));
    const decoded = new Uint8Array(encoded.length);
    const hex = code => code >= 48 && code <= 57 ? code - 48 : code >= 65 && code <= 70 ? code - 55 : code >= 97 && code <= 102 ? code - 87 : -1;
    let length = 0;
    for (let index = 0; index < encoded.length; index++) {
        if (encoded[index] === 37 && index + 2 < encoded.length) {
            const high = hex(encoded[index + 1]), low = hex(encoded[index + 2]);
            if (high >= 0 && low >= 0) { decoded[length++] = high * 16 + low; index += 2; continue; }
        }
        decoded[length++] = encoded[index];
    }
    const marker = /; *base64$/i.exec(mime);
    let body;
    if (marker) {
        mime = mime.slice(0, marker.index);
        const chunks = [];
        for (let index = 0; index < length; index += 4096)
            chunks.push(String.fromCharCode(...decoded.subarray(index, Math.min(index + 4096, length))));
        let binary; try { binary = atob(chunks.join('')); } catch (error) { return null; }
        const bytes = new Uint8Array(binary.length);
        for (let index = 0; index < bytes.length; index++) bytes[index] = binary.charCodeAt(index);
        body = bytes.buffer;
    } else body = decoded.buffer.slice(0, length);
    if (mime.startsWith(';')) mime = 'text/plain' + mime;
    return [body, dataURLMimeType(mime) || 'text/plain;charset=US-ASCII'];
}
// Blob URL response, honouring a single "Range: bytes=" request header (206 + Content-Range); null = network error.
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
function fetch(input, init) {
    let req; try { req = new Request(input, init); } catch (e) { return Promise.reject(e); }
    return new Promise((resolve, reject) => {
        if (req.signal.aborted) return reject(req.signal.reason);
        const lb = localBody(req.url);
        if (lb !== undefined) { const br = lb && blobResponse(req.url, lb, req.headers.get('range')); if (!br) return reject(new TypeError('Failed to fetch')); const r = new Response(null, { status: br[0], statusText: br[1], headers: fromFlat(br[2]) }); r._b = br[3]; r.url = req.url; r.type = 'basic'; return resolve(immutableResponse(r)); }
        const build = (status, statusText, url, hdrs) => {
            if (!corsOk(req.url, url, hdrs || [], req.credentials === 'include')) {
                if (req.mode !== 'no-cors') return null;
                const r = new Response(null); r.status = 0; r.url = ''; r.type = 'opaque';
                return immutableResponse(r);
            }
            if (req.mode === 'same-origin') { try { if (new URL(url || req.url).origin !== location.origin) return null; } catch (e) {} }
            const r = new Response(null, { status, statusText, headers: fromFlat(hdrs) }); r.url = url; r.redirected = url !== req.url; r.type = 'basic';
            return immutableResponse(r);
        };
        let id = 0, ctl = null, settled = false;
        const head = (status, statusText, url, hdrs) => {
            if (settled) return;
            const r = build(status, statusText, url, hdrs); if (!r || r.type !== 'basic') return;
            r._b = new ReadableStream({ start(c) { ctl = c; } }); settled = true; resolve(r);
        };
        const chunk = (ab) => { if (ctl) try { ctl.enqueue(new Uint8Array(ab)); } catch (e) {} };
        corsSend(req.credentials, (i) => id = i, req.method, req.url, withOrigin(headerOps.flat(req.headers), req.url, req.method, req.mode), req._b, (status, statusText, url, hdrs, body, err) => {
            if (settled) { if (ctl) { try { if (err) ctl.error(new TypeError('network error')); else ctl.close(); } catch (e) {} ctl = null; } return; }
            if (err) return reject(new TypeError('Failed to fetch'));
            const r = build(status, statusText, url, hdrs); if (!r) return reject(new TypeError('Failed to fetch'));
            if (r.type === 'basic') r._b = body;
            resolve(r);
        }, { head, chunk });
        req.signal.addEventListener('abort', () => { N.abort(id); if (ctl) { try { ctl.error(req.signal.reason); } catch (e) {} ctl = null; } reject(req.signal.reason); });
    });
}
// Newlines in field names and string values are normalized during serialization,
// leaving FormData entries and file byte sequences unchanged.
function formLineEndings(value) {
    return String(value).replace(/\r\n?|\n/g, '\r\n');
}
function* formEntryPairs(entries) {
    for (const [name, value] of entries) yield [formLineEndings(name),
        formLineEndings(blobFileOps.isBlob(value) ? (blobFileOps.isFile(value) ? blobFileOps.name(value) : 'blob') : value)];
}
function formAddBytes(total, amount) {
    // Match windows.cc's existing 64 MiB navigation request bound before the
    // aggregate is allocated. Fetch/XHR multipart does not use this bound.
    if (!Number.isSafeInteger(amount) || amount < 0 || amount > 64 * 1024 * 1024 - total)
        throw new RangeError('Encoded form exceeds the request byte limit');
    return total + amount;
}
function formURLEncoded(entries, encoding) {
    const parts = [];
    let length = 0;
    for (const [name, value] of formEntryPairs(entries)) {
        const key = N.formEncode(name, encoding, 2), body = N.formEncode(value, encoding, 2);
        length = formAddBytes(length, key.length);
        length = formAddBytes(length, body.length);
        length = formAddBytes(length, parts.length ? 2 : 1); // '=' and any '&'.
        parts.push(key + '=' + body);
    }
    return parts.join('&');
}
function formPlainText(entries, encoding) {
    const parts = [];
    let length = 0;
    for (const [name, value] of formEntryPairs(entries)) {
        length = formAddBytes(length, N.formUtf8Length(name));
        length = formAddBytes(length, N.formUtf8Length(value));
        length = formAddBytes(length, 3); // '=' and CRLF.
        parts.push(name + '=' + value + '\r\n');
    }
    return N.formEncode(parts.join(''), encoding, 0);
}
function multipartPart(value, encoding, parameter) {
    const text = String(value);
    if (encoding !== undefined) return new Uint8Array(N.formEncode(text, encoding, parameter ? 1 : 0));
    // Fetch/XHR FormData keeps its UTF-8 path without the navigation byte limit.
    const bytes = new Uint8Array(N.encode(text));
    if (!parameter) return bytes;
    let length = bytes.byteLength;
    for (const byte of bytes) if (byte === 10 || byte === 13 || byte === 34) length += 2;
    if (length === bytes.byteLength) return bytes;
    const escaped = new Uint8Array(length), digits = '0123456789ABCDEF';
    let at = 0;
    for (const byte of bytes) {
        if (byte === 10 || byte === 13 || byte === 34) {
            escaped[at++] = 37;
            escaped[at++] = digits.charCodeAt(byte >> 4);
            escaped[at++] = digits.charCodeAt(byte & 15);
        } else escaped[at++] = byte;
    }
    return escaped;
}
function multipartBody(entries, bd, encoding = undefined) {
    const ascii = text => new Uint8Array(N.encode(text)), parts = [];
    let length = 0;
    const append = (...items) => {
        for (const part of items) {
            length = encoding === undefined ? length + part.byteLength : formAddBytes(length, part.byteLength);
            parts.push(part);
        }
    };
    for (const [name, value] of entries) {
        append(ascii(`--${bd}\r\nContent-Disposition: form-data; name="`),
            multipartPart(formLineEndings(name), encoding, true), ascii('"'));
        if (blobFileOps.isBlob(value)) {
            // Filenames are escaped after encoding, without newline normalization.
            append(ascii('; filename="'), multipartPart(blobFileOps.isFile(value) ? blobFileOps.name(value) : 'blob', encoding, true),
                ascii(`"\r\nContent-Type: ${blobFileOps.type(value) || 'application/octet-stream'}\r\n\r\n`));
            // Reject an oversized navigation payload before allocating its private copy.
            if (encoding !== undefined) formAddBytes(formAddBytes(length, blobFileOps.size(value)), 2);
            append(new Uint8Array(blobFileOps.copyBuffer(value)), ascii('\r\n'));
        } else append(ascii('\r\n\r\n'), multipartPart(formLineEndings(value), encoding, false), ascii('\r\n'));
    }
    append(ascii(`--${bd}--\r\n`));
    const out = new Uint8Array(length);
    let offset = 0;
    for (const part of parts) { out.set(part, offset); offset += part.byteLength; }
    return out;
}
function formEntries(form, encoding) {
    const entries = [];
    for (const element of form.elements) {
        if (!element.name || element.disabled) continue;
        const type = element.type;
        if ((type === 'checkbox' || type === 'radio') && !element.checked) continue;
        if (type === 'file') {
            const files = [...element.files];
            if (!files.length) entries.push([element.name, new File([], '', {type: 'application/octet-stream'})]);
            for (const file of files) entries.push([element.name, file]);
            continue;
        }
        if (/^(submit|button|reset|image)$/.test(type)) continue;
        const charset = type === 'hidden' && N.name(element) === 'input' &&
            String(element.name).toLowerCase() === '_charset_';
        entries.push([element.name, charset ? encoding : element.value]);
    }
    return entries;
}
const formDataBrands = privateDOMState(11);
class FormData {
    constructor(form = undefined) {
        if (form !== undefined) formReflectionBrand(form, 'form');
        def(this, '_l', form ? formEntries(form, 'UTF-8') : []); formDataBrands.set(this, {});
    }
    append(k, v, fn = undefined) { this._l.push([String(k), blobFileOps.isBlob(v) ? (fn ? new File([v], fn, {type: blobFileOps.type(v), lastModified: blobFileOps.isFile(v) ? blobFileOps.modified(v) : undefined}) : v) : String(v)]); }
    set(k, v, fn = undefined) { this.delete(k); this.append(k, v, fn); } delete(k) { this._l = this._l.filter(e => e[0] !== String(k)); }
    get(k) { const e = this._l.find(x => x[0] === String(k)); return e ? e[1] : null; } getAll(k) { return this._l.filter(x => x[0] === String(k)).map(x => x[1]); }
    has(k) { return this._l.some(x => x[0] === String(k)); } forEach(fn, s = undefined) { for (const [k, v] of this._l) fn.call(s, v, k, this); }
    entries() { return this._l.map(x => x.slice())[Symbol.iterator](); } keys() { return this._l.map(x => x[0])[Symbol.iterator](); } values() { return this._l.map(x => x[1])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
}
// Brand checks apply to every public operation and event-handler attribute.
function checkIDLPrototype(prototype, brand, required = {}) {
    for (const [name, descriptor] of Object.entries(Object.getOwnPropertyDescriptors(prototype))) {
        if (name === 'constructor' || name.startsWith('_')) continue;
        for (const key of ['value', 'get', 'set']) if (typeof descriptor[key] === 'function') {
            const original = descriptor[key];
            const checked = function(...args) {
                if (!brand.has(this)) throw new TypeError('Illegal invocation');
                if (args.length < (required[name] || 0)) throw new TypeError(name + ' requires more arguments');
                return Reflect.apply(original, this, args);
            };
            Object.defineProperties(checked, {
                name: {value: key === 'value' ? name : key + ' ' + name},
                length: {value: original.length},
            });
            descriptor[key] = checked;
        }
        descriptor.enumerable = true;
        Object.defineProperty(prototype, name, descriptor);
    }
}
checkIDLPrototype(FormData.prototype, formDataBrands, {append: 2, set: 2, delete: 1, get: 1, getAll: 1, has: 1, forEach: 1});
Object.defineProperty(FormData.prototype, Symbol.iterator, {
    value: FormData.prototype.entries, writable: true, configurable: true,
});
const xhrTargetToken = {}, xhrTargetBrands = privateDOMState(12), xhrBrands = privateDOMState(13);
class XMLHttpRequestEventTarget extends EventTarget {
    constructor(...args) {
        if (args[0] !== xhrTargetToken) illegal();
        super(); xhrTargetBrands.set(this, {});
    }
}
installHandlers(XMLHttpRequestEventTarget.prototype, false, ['abort', 'error', 'load', 'loadend', 'loadstart', 'progress', 'timeout']);
checkIDLPrototype(XMLHttpRequestEventTarget.prototype, xhrTargetBrands);
const XHR_FORBIDDEN = new Set(['accept-charset', 'accept-encoding', 'access-control-request-headers', 'access-control-request-method', 'connection', 'content-length', 'cookie', 'cookie2', 'date', 'dnt', 'expect', 'host', 'keep-alive', 'origin', 'referer', 'set-cookie', 'te', 'trailer', 'transfer-encoding', 'upgrade', 'via']);
const HTTP_TOKEN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
let xhrMakeUpload = false;
class XMLHttpRequestUpload extends XMLHttpRequestEventTarget { constructor() { if (!xhrMakeUpload) illegal(); super(xhrTargetToken); } }
class XMLHttpRequest extends XMLHttpRequestEventTarget {
    constructor() {
        super(xhrTargetToken); xhrBrands.set(this, {});
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
    open(method, url, ...optional) {
        let [async, user, pass] = optional;
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
    send(body = null) {
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
        const credentials = this._wc ? 'include' : 'same-origin';
        if (!this._async) { const r = corsSendSync(credentials, this._m, this._u, withOrigin(headerOps.flat(this._rh), this._u, this._m, 'cors'), b); this._done(gen, ...r); return; }
        corsSend(credentials, (i) => this._id = i, this._m, this._u, withOrigin(headerOps.flat(this._rh), this._u, this._m, 'cors'), b, (...a) => this._done(gen, ...a));
        if (this._to > 0) this._timer = setTimeout(() => { if (gen !== this._gen) return; this._terminate(); this._errorSteps('timeout'); }, this._to);
    }
    abort() {
        this._terminate();
        if ((this._rs === 1 && this._send) || this._rs === 2 || this._rs === 3) this._errorSteps('abort');
        if (this._rs === 4) { this._rs = 0; this._reset(); }
    }
    _mimeType() { return (this._mime || this.getResponseHeader('content-type') || '').split(';')[0].trim().toLowerCase(); }
    _text() { return this._body ? N.responseTextDecode(this._body, this.getResponseHeader('content-type') || '', this._mime || '') : ''; }
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
        if (d) N.setDocumentURL(d, this.responseURL);
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
        case 'json': try { return this._resp = JSON.parse(this._body ? N.decode(this._body) : ''); } catch (e) { return this._resp = null; }
        }
        return null;
    }
}
installHandlers(XMLHttpRequest.prototype, false, ['readystatechange']);
checkIDLPrototype(XMLHttpRequest.prototype, xhrBrands, {open: 2, setRequestHeader: 2, getResponseHeader: 1, overrideMimeType: 1});
for (const [k, v] of Object.entries({ UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 })) { Object.defineProperty(XMLHttpRequest, k, { value: v, enumerable: true }); Object.defineProperty(XMLHttpRequest.prototype, k, { value: v, enumerable: true }); }
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
        if (new Set(ps.map(p => p.toLowerCase())).size !== ps.length || ps.some(p => !/^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/.test(p))) throw new DOMException("Failed to construct 'WebSocket': The subprotocol is invalid.", 'SyntaxError');
        def(this, '_s', { url: s, rs: 0, protocol: '', binaryType: 'blob', ps, buf: 0, dead: false });
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
// Convert the Web IDL overload before serialization. In particular, do not
// silently discard non-port transferables or evaluate a transfer getter twice.
function cloneTransferList(options, legacy = false) {
    if (options === undefined || options === null) return [];
    if (typeof options !== 'object' && typeof options !== 'function') throw new TypeError('Transfer options must be an object.');
    let value, method;
    if (legacy) {
        method = options[Symbol.iterator];
        value = method === undefined || method === null ? options.transfer : options;
    } else value = options.transfer;
    if (value === undefined) return [];
    if (value === null || (typeof value !== 'object' && typeof value !== 'function')) throw new TypeError('Transfer must be a sequence of objects.');
    if (value !== options || method === undefined || method === null) method = value[Symbol.iterator];
    if (typeof method !== 'function') throw new TypeError('Transfer must be iterable.');
    const iterator = method.call(value);
    const list = [];
    for (const item of { [Symbol.iterator]() { return iterator; } }) {
        if (item === null || (typeof item !== 'object' && typeof item !== 'function')) throw new TypeError('A transferable must be an object.');
        list.push(item);
    }
    return list;
}
const portBufferLength = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'byteLength').get;
function portList(options) {
    const list = cloneTransferList(options, true), ports = [], buffers = [], seen = new Set();
    for (const item of list) {
        if (seen.has(item)) throw new DOMException('The transfer list contains a duplicate object.', 'DataCloneError');
        seen.add(item);
        if (item instanceof MessagePort) {
            if (portState(item).detached) throw new DOMException('The MessagePort has already been transferred.', 'DataCloneError');
            ports.push(item);
        }
        else {
            // The intrinsic checks the ArrayBuffer brand, including detached
            // buffers; public properties and SharedArrayBuffer cannot spoof it.
            try { portBufferLength.call(item); }
            catch (_) { throw new DOMException('This object is not transferable.', 'DataCloneError'); }
            buffers.push(item);
        }
    }
    return { ports, buffers };
}
const messagePortState = new WeakMap();
const remotePortKey = (worker, port) => worker + ':' + port;
function portState(port) {
    const state = messagePortState.get(port);
    if (!state) throw new TypeError('Illegal MessagePort receiver');
    return state;
}
class MessagePort extends EventTarget {
    constructor() {
        super();
        messagePortState.set(this, { started: false, closed: false, detached: false, scheduled: false, queue: [], handler: null, sharedConnection: 0 });
        this.onmessageerror = null; def(this, '_other', null);
    }
    get onmessage() { return portState(this).handler; }
    set onmessage(value) {
        const state = portState(this);
        state.handler = typeof value === 'function' ? value : null;
        this.start();
    }
    postMessage(data, options) {
        const state = portState(this);
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage': 1 argument required.");
        const { ports, buffers } = portList(options), remote = !state.closed && (this._wk || (this._other && this._other._wk));
        if (ports.includes(this)) throw new DOMException('A port cannot transfer itself.', 'DataCloneError');
        if (remote) {
            const ids = ports.map(() => ++portSeq);
            N.workerPost(remote[0], { __lumenPort: remote[1], data: portSub(data, ports), xfer: ids }, buffers);
            adoptWorkerPorts(remote[0], ports, ids);
            return;
        }
        const other = !state.closed && this._other;
        const cloned = structuredClone(portSub(data, ports), { transfer: buffers });
        if (other) setTimeout(() => other._deliver(portRes(cloned, ports), ports));
    }
    _deliver(data, ports) {
        const state = portState(this);
        if (state.closed) return;
        state.queue.push({ data, ports: ports || [] });
        this._flush();
    }
    _flush() {
        const state = portState(this);
        if (state.closed || !state.started || state.scheduled || !state.queue.length) return;
        state.scheduled = true;
        setTimeout(() => {
            state.scheduled = false;
            if (state.closed || !state.started || !state.queue.length) return;
            const message = state.queue.shift();
            dispatch(this, new MessageEvent('message', message), true);
            this._flush();
        });
    }
    start() { const state = portState(this); state.started = true; this._flush(); }
    close() {
        const state = portState(this);
        if (state.closed) return;
        state.closed = true; state.queue.length = 0;
        if (this._other && this._other._other === this) this._other._other = null;
        this._other = null;
        if (this._wk) {
            remotePorts.delete(remotePortKey(this._wk[0], this._wk[1]));
            N.workerPortClose(this._wk[0], this._wk[1]);
        }
        this._wk = null;
        if (state.sharedConnection) { releaseSharedConnection(state.sharedConnection); state.sharedConnection = 0; }
    }
}
class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } }
const bcAll = new Set();
let bcIds = 0;
class BroadcastChannel extends EventTarget {
    constructor(n) {
        super();
        if (!arguments.length) throw new TypeError("Failed to construct 'BroadcastChannel': 1 argument required");
        Object.defineProperty(this, '_id', { value: ++bcIds }); Object.defineProperty(this, '_closed', { value: false, writable: true });
        this.name = String(n); this.onmessage = null; this.onmessageerror = null; bcAll.add(this);
    }
    postMessage(m) {
        if (this._closed) throw new DOMException("Failed to execute 'postMessage' on 'BroadcastChannel': Channel is closed", 'InvalidStateError');
        try { N.busPost(ssOrigin, 'bc', this.name, m, this._id); } catch (e) { throw new DOMException(String(e && e.message || e), 'DataCloneError'); }
    }
    close() { this._closed = true; bcAll.delete(this); }
}

const portSub = (v, ps, seen = new Set()) => { if (!ps.length || !v || typeof v !== 'object' || seen.has(v)) return v; const i = ps.indexOf(v); if (i >= 0) return { __lumenPortRef: i }; seen.add(v); if (Array.isArray(v)) return v.map(x => portSub(x, ps, seen)); const pr = Object.getPrototypeOf(v); if (pr !== Object.prototype && pr !== null) return v; const o = {}; for (const k of Object.keys(v)) o[k] = portSub(v[k], ps, seen); return o; };
const portRes = (v, ps, seen = new Set()) => { if (!ps.length || !v || typeof v !== 'object' || seen.has(v)) return v; if (typeof v.__lumenPortRef === 'number' && Object.keys(v).length === 1) return ps[v.__lumenPortRef]; seen.add(v); for (const k of Object.keys(v)) v[k] = portRes(v[k], ps, seen); return v; };
const workers = new Map(); let portSeq = 0; const remotePorts = new Map();
// An endpoint can move between real worker isolates. Keep its entanglement in
// the private broker, not in the detached JavaScript wrapper. The shared native
// connection remains alive while any owned port or broker route still uses it.
const sharedConnections = new Map(), endpointBridges = new Map();
function retainSharedConnection(id) {
    const connection = sharedConnections.get(id);
    if (connection) { ++connection.references; return id; }
    return 0;
}
function releaseSharedConnection(id) {
    const connection = sharedConnections.get(id);
    if (!connection || --connection.references > 0) return;
    sharedConnections.delete(id); N.sharedWorkerClose(id); workers.delete(id);
}
function remotePort(worker, key) {
    const port = new MessagePort(); port._wk = [worker, key];
    portState(port).sharedConnection = retainSharedConnection(worker);
    remotePorts.set(remotePortKey(worker, key), port); return port;
}
function sendPortMessage(route, data, ports) {
    const ids = ports.map(() => ++portSeq);
    N.workerPost(route[0], {__lumenPort: route[1], data: portSub(data, ports), xfer: ids}, []);
    adoptWorkerPorts(route[0], ports, ids);
}
function watchBridge(worker, bridge) {
    let bridges = endpointBridges.get(worker);
    if (!bridges) endpointBridges.set(worker, bridges = new Set());
    bridges.add(bridge); N.workerWatchLifecycle(worker);
}
function unlinkBridge(bridge, close = false) {
    if (!bridge.active) return 0;
    bridge.active = false;
    for (const route of [bridge.left, bridge.right]) {
        const key = remotePortKey(route[0], route[1]);
        if (remotePorts.get(key)?._bridge === bridge) remotePorts.delete(key);
        const bridges = endpointBridges.get(route[0]);
        if (bridges) { bridges.delete(bridge); if (!bridges.size) endpointBridges.delete(route[0]); }
        if (close) N.workerPortClose(route[0], route[1]);
    }
    const connection = bridge.sharedConnection; bridge.sharedConnection = 0;
    if (close && connection) releaseSharedConnection(connection);
    return close ? 0 : connection;
}
function closeWorkerBridges(worker) {
    for (const bridge of [...(endpointBridges.get(worker) || [])]) unlinkBridge(bridge, true);
}
function disconnectWorkerPeer(port) {
    // A peer's close only disentangles the endpoints. Messages already queued
    // on this receiving port still belong to it and must retain FIFO delivery.
    if (port._wk) remotePorts.delete(remotePortKey(port._wk[0], port._wk[1]));
    port._wk = null;
    const state = portState(port);
    if (state.sharedConnection) { releaseSharedConnection(state.sharedConnection); state.sharedConnection = 0; }
}
function movedWorkerPort(worker, source) {
    const key = remotePortKey(worker, source), old = remotePorts.get(key), port = new MessagePort();
    if (old?._bridge) {
        const bridge = old._bridge;
        const route = bridge.left[0] === worker && bridge.left[1] === source ? bridge.right : bridge.left;
        portState(port).sharedConnection = unlinkBridge(bridge);
        port._wk = route.slice(); remotePorts.set(remotePortKey(route[0], route[1]), port);
    } else if (old instanceof MessagePort) {
        // The other endpoint has returned to this document. Both wrappers now
        // live here; neither endpoint depends on the departed worker anymore.
        remotePorts.delete(key); old._wk = null;
        old._other = port; port._other = old;
        const state = portState(old);
        if (state.sharedConnection) { releaseSharedConnection(state.sharedConnection); state.sharedConnection = 0; }
    }
    return port;
}
function adoptWorkerPorts(worker, ports, ids) {
    for (let index = 0; index < ports.length; ++index) {
        const port = ports[index], state = portState(port), destination = [worker, ids[index]];
        const pending = state.queue.splice(0);
        const source = port._wk, other = port._other;
        if (source && !state.closed) {
            const bridge = {left: source.slice(), right: destination, active: true, sharedConnection: state.sharedConnection};
            // Transfer the existing native-connection reference into the
            // broker. Closing the detached sender must not close its receiver.
            state.sharedConnection = 0;
            remotePorts.set(remotePortKey(source[0], source[1]), {
                _bridge: bridge,
                _deliver(data, incoming) { if (bridge.active) sendPortMessage(destination, data, incoming); },
            });
            remotePorts.set(remotePortKey(worker, ids[index]), {
                _bridge: bridge,
                _deliver(data, incoming) { if (bridge.active) sendPortMessage(source, data, incoming); },
            });
            watchBridge(source[0], bridge); watchBridge(worker, bridge);
        } else if (other && !state.closed) {
            other._other = null; other._wk = destination;
            portState(other).sharedConnection = retainSharedConnection(worker);
            remotePorts.set(remotePortKey(worker, ids[index]), other);
        }
        port._other = null; port._wk = null;
        state.detached = state.closed = true;
        for (const message of pending) sendPortMessage(destination, message.data, message.ports);
    }
}
class Worker extends EventTarget {
    constructor(url, opts) {
        super();
        if (arguments.length < 1) throw new TypeError("Failed to construct 'Worker': 1 argument required, but only 0 present.");
        const u = new URL(String(url), document.baseURI || N.url());
        let src = null;
        if (u.protocol === 'blob:') { const b = objectURLs.get(u.href); if (blobFileOps.isBlob(b)) src = N.decode(blobFileOps.copyBuffer(b)); }
        else if (u.protocol === 'data:') { const lb = localBody(u.href); if (lb) src = new TextDecoder().decode(lb[0]); }
        else if (u.origin !== location.origin) throw new DOMException(`Failed to construct 'Worker': Script at '${u.href}' cannot be accessed from origin '${location.origin}'.`, 'SecurityError');
        this.onmessage = null; this.onmessageerror = null; this.onerror = null;
        def(this, '_id', N.workerNew(u.href, src, N.userAgent(), N.platform(), opts && opts.name != null ? String(opts.name) : '', !!opts && opts.type === 'module'));
        workers.set(this._id, this);
    }
    postMessage(data, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage' on 'Worker': 1 argument required.");
        const { ports, buffers } = portList(options);
        if (!ports.length) return N.workerPost(this._id, data, buffers);
        const ids = ports.map(() => ++portSeq);
        N.workerPost(this._id, { __lumenXfer: ids, data: portSub(data, ports) }, buffers);
        adoptWorkerPorts(this._id, ports, ids);
    }
    terminate() { if (this._id) { closeWorkerBridges(this._id); N.workerTerm(this._id); workers.delete(this._id); this._id = 0; } }
}
class SharedWorker extends EventTarget {
    constructor(url, options) {
        super();
        if (arguments.length < 1) throw new TypeError("Failed to construct 'SharedWorker': 1 argument required.");
        // Web IDL union: primitive second arguments name the worker; objects
        // are WorkerOptions dictionaries. Enum failures are synchronous.
        const opts = options !== null && (typeof options === 'object' || typeof options === 'function')
            ? options : options === undefined || options === null ? {} : { name: '' + options };
        const rawCredentials = opts.credentials, rawName = opts.name, rawType = opts.type;
        const credentials = rawCredentials === undefined ? 'same-origin' : '' + rawCredentials;
        const name = rawName === undefined ? '' : '' + rawName;
        const type = rawType === undefined ? 'classic' : '' + rawType;
        if (type !== 'classic' && type !== 'module') throw new TypeError('Invalid SharedWorker type');
        if (!['omit', 'same-origin', 'include'].includes(credentials)) throw new TypeError('Invalid SharedWorker credentials');
        const input = '' + url;
        let u;
        try { u = new URL(input, document.baseURI || N.url()); }
        catch (_) { throw new DOMException('Invalid SharedWorker script URL.', 'SyntaxError'); }
        let src = null;
        if (u.protocol === 'blob:') { const b = objectURLs.get(u.href); if (blobFileOps.isBlob(b)) src = N.decode(blobFileOps.copyBuffer(b)); }
        else if (u.protocol === 'data:') { const lb = localBody(u.href); if (lb) src = new TextDecoder().decode(lb[0]); }
        this.onerror = null;
        const port = new MessagePort();
        Object.defineProperty(this, 'port', { value: port, enumerable: true });
        const id = N.sharedWorkerNew(u.href, src, N.userAgent(), N.platform(), name, type === 'module', credentials);
        if (!id) { setTimeout(() => dispatch(this, new Event('error', { cancelable: true }), true)); return; }
        def(this, '_id', id);
        sharedConnections.set(id, {references: 1});
        port._wk = [id, 0]; portState(port).sharedConnection = id;
        remotePorts.set(remotePortKey(id, 0), port);
        workers.set(id, this);
    }
}
function workerEvent(id, kind, data, message, filename, lineno, colno, moves, queues) {
    const w = workers.get(id); if (!w) return;
    if (kind === 4) { closeWorkerBridges(id); return; }
    if (kind === 5) {
        const port = remotePorts.get(remotePortKey(id, data));
        if (port?._bridge) unlinkBridge(port._bridge, true);
        else if (port instanceof MessagePort) disconnectWorkerPeer(port);
        return;
    }
    if (kind === 0) {
        const moved = new Map(moves || []), incoming = new Map();
        const receive = key => {
            if (!incoming.has(key)) incoming.set(key, moved.has(key) ? movedWorkerPort(id, moved.get(key)) : remotePort(id, key));
            return incoming.get(key);
        };
        for (const [key, messages] of queues || []) {
            const port = receive(key);
            for (const message of messages) {
                const ports = message.xfer.map(receive);
                port._deliver(portRes(message.data, ports), ports);
            }
        }
        let ports = [];
        if (data && typeof data === 'object') {
            if (Object.prototype.hasOwnProperty.call(data, '__lumenPort')) {
                const p = remotePorts.get(remotePortKey(id, data.__lumenPort));
                if (p) {
                    const qs = (data.xfer || []).map(receive);
                    p._deliver(portRes(data.data, qs), qs);
                }
                return;
            }
            if (data.__lumenXfer) { ports = data.__lumenXfer.map(receive); data = portRes(data.data, ports); }
        }
        dispatch(w, new MessageEvent('message', { data, ports }), true);
    }
    else if (kind === 1) {
        const e = new ErrorEvent('error', { message, filename, lineno, colno, cancelable: true });
        dispatch(w, e, true);
        if (!e.defaultPrevented) N.log(3, 'Uncaught (in worker) ' + message + ' at ' + filename + ':' + lineno);
    } else if (kind === 3) dispatch(w, new MessageEvent('messageerror', {}), true);
    else dispatch(w, new Event('error', { cancelable: true }), true);
}
