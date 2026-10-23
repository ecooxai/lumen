/* Fetch (fetch.spec.whatwg.org): Headers, Request, Response, FormData, the body mixin and fetch(), shared by the page
   and worker preludes. Redirects, CORS (with preflights), referrers, data: and blob: URLs and response filtering run
   here. The including prelude defines the realm and transport hooks:
     FETCH_BASE()          the base URL that request URLs are parsed against
     FETCH_BLOB(blob)      a Blob's bytes, as a Uint8Array
     FETCH_BLOBURL(url)    the Blob behind one of this realm's blob: URLs, or null
     FETCH_NET(o, h)       one request that never follows redirects: o = { method, url, headers: [name, value, ...],
                           body: Uint8Array or null, cookies }. The host calls h.head(status, statusText, url, headers)
                           when it streams the body, then h.chunk(ArrayBuffer) for each piece, and always
                           h.done(error, status, statusText, url, headers, ArrayBuffer) last (its body unused after a
                           head). Returns a function that cancels the request; its pause(on) stops and resumes
                           reading a streamed body (flow control).
     FETCH_BRAND(response) marks a Response for WebAssembly's streaming compile
     FORM_ENTRIES(form, submitter)  the entry list [[name, value], ...] of new FormData(form, submitter) */
const FETCH_TOKEN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
const FETCH_WS_TRIM = /^[\t\n\r ]+|[\t\n\r ]+$/g;
const ferr = (where, msg) => new TypeError(`Failed to ${where}: ${msg}`);
const fnet = () => new TypeError('Failed to fetch');
const fargs = (n, have, where) => { if (have < n) throw ferr(where, `${n} argument${n > 1 ? 's' : ''} required, but only ${have} present.`); };
const fbyteString = (v, where) => { const s = String(v); if (/[^\x00-\xff]/.test(s)) throw ferr(where, 'String contains non ISO-8859-1 code point.'); return s; };
const fenc = s => new TextEncoder().encode(s);
const fdec = b => new TextDecoder().decode(b);
const fisObj = v => v !== null && (typeof v === 'object' || typeof v === 'function');
const flatin1 = b => { let s = ''; for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode.apply(null, b.subarray(i, i + 8192)); return s; };
const fbytesOf = s => { const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i) & 255; return u; };
const fconcat = parts => { let n = 0; for (const p of parts) n += p.byteLength; const out = new Uint8Array(n); let o = 0; for (const p of parts) { out.set(p, o); o += p.byteLength; } return out; };
const fushort = v => { const x = Number(v); if (!Number.isFinite(x)) return 0; return ((Math.trunc(x) % 65536) + 65536) % 65536; };

/* ---- MIME types (mimesniff § 4) and header value lists ---- */
function parseMime(input) {
    input = input.replace(FETCH_WS_TRIM, '');
    const slash = input.indexOf('/');
    if (slash < 0) return null;
    const type = input.slice(0, slash);
    if (!FETCH_TOKEN.test(type)) return null;
    let p = slash + 1, semi = input.indexOf(';', p);
    if (semi < 0) semi = input.length;
    const sub = input.slice(p, semi).replace(/[\t\n\r ]+$/, '');
    if (!FETCH_TOKEN.test(sub)) return null;
    const m = { type: type.toLowerCase(), subtype: sub.toLowerCase(), params: new Map() };
    p = semi;
    while (p < input.length) {
        p++;
        while (p < input.length && /[\t\n\r ]/.test(input[p])) p++;
        let name = '';
        while (p < input.length && input[p] !== ';' && input[p] !== '=') name += input[p++];
        name = name.toLowerCase();
        if (p < input.length) { if (input[p] === ';') continue; p++; }
        if (p >= input.length) break;
        let value = '';
        if (input[p] === '"') {
            p++;
            for (;;) {
                while (p < input.length && input[p] !== '"' && input[p] !== '\\') value += input[p++];
                if (p >= input.length) break;
                if (input[p++] === '\\') { if (p >= input.length) { value += '\\'; break; } value += input[p++]; }
                else break;
            }
            while (p < input.length && input[p] !== ';') p++;
        } else {
            while (p < input.length && input[p] !== ';') value += input[p++];
            value = value.replace(/[\t\n\r ]+$/, '');
            if (!value) continue;
        }
        if (name && FETCH_TOKEN.test(name) && /^[\t\x20-\x7e\x80-\xff]*$/.test(value) && !m.params.has(name)) m.params.set(name, value);
    }
    return m;
}
const mimeEssence = m => m.type + '/' + m.subtype;
function serializeMime(m) {
    let s = mimeEssence(m);
    for (const [k, v] of m.params) s += ';' + k + '=' + (v !== '' && FETCH_TOKEN.test(v) ? v : '"' + v.replace(/(["\\])/g, '\\$1') + '"');
    return s;
}
/* "get, decode, and split": a combined header value split at commas outside quoted strings */
function splitValues(v) {
    const out = []; let p = 0, cur = '';
    for (;;) {
        while (p < v.length && v[p] !== '"' && v[p] !== ',') cur += v[p++];
        if (p < v.length && v[p] === '"') {
            const start = p++;
            for (;;) {
                while (p < v.length && v[p] !== '"' && v[p] !== '\\') p++;
                if (p >= v.length) break;
                if (v[p++] === '\\') { if (p >= v.length) break; p++; } else break;
            }
            cur += v.slice(start, p);
            if (p < v.length) continue;
        }
        out.push(cur.replace(/^[\t ]+|[\t ]+$/g, '')); cur = '';
        if (p >= v.length) return out;
        p++;
    }
}
const hdrGet = (list, name) => { const l = name.toLowerCase(), vs = []; for (const [n, v] of list) if (n.toLowerCase() === l) vs.push(v); return vs.length ? vs.join(', ') : null; };
const hdrHas = (list, name) => { const l = name.toLowerCase(); return list.some(([n]) => n.toLowerCase() === l); };
/* "extract a MIME type" from a header list */
function extractMime(list) {
    const v = hdrGet(list, 'content-type');
    if (v === null) return null;
    let charset = null, essence = null, mime = null;
    for (const s of splitValues(v)) {
        const t = parseMime(s);
        if (!t || mimeEssence(t) === '*/*') continue;
        mime = t;
        if (mimeEssence(mime) !== essence) { charset = mime.params.has('charset') ? mime.params.get('charset') : null; essence = mimeEssence(mime); }
        else if (!mime.params.has('charset') && charset !== null) mime.params.set('charset', charset);
    }
    return mime;
}

/* ---- header guards (Fetch § 2.2.2) ---- */
const FORBIDDEN_REQ = new Set(['accept-charset', 'accept-encoding', 'access-control-request-headers', 'access-control-request-method', 'connection', 'content-length', 'cookie', 'cookie2', 'date', 'dnt', 'expect', 'host', 'keep-alive', 'origin', 'referer', 'set-cookie', 'te', 'trailer', 'transfer-encoding', 'upgrade', 'via']);
function forbiddenRequestHeader(name, value) {
    const l = name.toLowerCase();
    if (FORBIDDEN_REQ.has(l) || l.startsWith('proxy-') || l.startsWith('sec-')) return true;
    if (l === 'x-http-method' || l === 'x-http-method-override' || l === 'x-method-override') return splitValues(value).some(m => /^(connect|trace|track)$/i.test(m));
    return false;
}
const forbiddenResponseName = n => /^set-cookie2?$/i.test(n);
const CORS_UNSAFE_BYTE_RE = /[\x00-\x08\x0a-\x1f"():<>?@\[\\\]{}\x7f]/;
function corsSafelisted(name, value) {
    if (value.length > 128) return false;
    switch (name.toLowerCase()) {
    case 'accept': return !CORS_UNSAFE_BYTE_RE.test(value);
    case 'accept-language': case 'content-language': return /^[0-9A-Za-z *,\-.;=]*$/.test(value);
    case 'content-type': {
        if (CORS_UNSAFE_BYTE_RE.test(value)) return false;
        const m = parseMime(value);
        return !!m && ['application/x-www-form-urlencoded', 'multipart/form-data', 'text/plain'].includes(mimeEssence(m));
    }
    case 'range': { const r = /^bytes=(\d+)-(\d*)$/i.exec(value); return !!r && (r[2] === '' || +r[1] <= +r[2]); }
    }
    return false;
}
const NO_CORS_NAMES = new Set(['accept', 'accept-language', 'content-language', 'content-type']);
const noCorsSafelisted = (name, value) => NO_CORS_NAMES.has(name.toLowerCase()) && corsSafelisted(name, value);
/* "CORS-unsafe request-header names": lowercase, sorted, unique */
function corsUnsafeNames(list) {
    const unsafe = new Set(), safe = new Set(); let size = 0;
    for (const [n, v] of list) {
        const l = n.toLowerCase();
        if (corsSafelisted(n, v)) { safe.add(l); size += v.length; } else unsafe.add(l);
    }
    if (size > 1024) for (const l of safe) unsafe.add(l);
    return [...unsafe].sort();
}

/* ---- Headers ---- */
const hdrState = new WeakMap();
const hdrOf = h => { const s = hdrState.get(h); if (!s) throw new TypeError('Illegal invocation'); return s; };
const hdrNew = (list, guard) => { const h = new Headers(), s = hdrState.get(h); s.list = list; s.guard = guard; return h; };
function hdrCheck(s, name, value, where) {
    if (!FETCH_TOKEN.test(name)) throw ferr(where, `Invalid name`);
    if (value !== null && /[\0\r\n]/.test(value)) throw ferr(where, `Invalid value`);
    if (s.guard === 'immutable') throw ferr(where, 'Headers are immutable');
}
const hdrDropPrivileged = s => { if (s.guard === 'request-no-cors') s.list = s.list.filter(([n]) => n.toLowerCase() !== 'range'); };
function hdrAppend(s, name, value, where) {
    value = value.replace(FETCH_WS_TRIM, '');
    hdrCheck(s, name, value, where);
    if (s.guard === 'request' && forbiddenRequestHeader(name, value)) return;
    if (s.guard === 'request-no-cors') { const t = hdrGet(s.list, name); if (!noCorsSafelisted(name, t === null ? value : t + ', ' + value)) return; }
    if (s.guard === 'response' && forbiddenResponseName(name)) return;
    s.list.push([name, value]); s.ver++;
    hdrDropPrivileged(s);
}
/* "sort and combine", cached per change */
function hdrSorted(s) {
    if (s.sortedVer === s.ver && s.sortedList === s.list) return s.sorted;
    const names = [...new Set(s.list.map(([n]) => n.toLowerCase()))].sort(), out = [];
    for (const n of names) {
        if (n === 'set-cookie') { for (const [k, v] of s.list) if (k.toLowerCase() === n) out.push([n, v]); }
        else out.push([n, hdrGet(s.list, n)]);
    }
    s.sorted = out; s.sortedVer = s.ver; s.sortedList = s.list;
    return out;
}
/* HeadersInit: a Headers object's list, else (record<ByteString, ByteString> or sequence<sequence<ByteString>>) */
function headersInit(init, where) {
    if (hdrState.has(init)) return hdrState.get(init).list.map(e => e.slice());
    if (!fisObj(init)) throw ferr(where, "The provided value is not of type '(record<ByteString, ByteString> or sequence<sequence<ByteString>>)'.");
    const out = [], it = init[Symbol.iterator];
    if (it !== undefined && it !== null) {
        if (typeof it !== 'function') throw ferr(where, 'The object must have a callable @@iterator property.');
        for (const pair of { [Symbol.iterator]: () => it.call(init) }) {
            if (!fisObj(pair) || typeof pair[Symbol.iterator] !== 'function') throw ferr(where, 'The provided value cannot be converted to a sequence.');
            const a = Array.from(pair, x => fbyteString(x, where));
            if (a.length !== 2) throw ferr(where, 'Invalid value');
            out.push(a);
        }
        return out;
    }
    for (const k of Reflect.ownKeys(init)) {
        const d = Reflect.getOwnPropertyDescriptor(init, k);
        if (!d || !d.enumerable) continue;
        if (typeof k === 'symbol') throw ferr(where, 'Cannot convert a Symbol value to a string');
        const name = fbyteString(k, where);
        out.push([name, fbyteString(init[k], where)]);
    }
    return out;
}
const hdrIters = new WeakMap();
const HeadersIteratorProto = Object.create(Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())), {
    next: { value: function next() {
        const it = hdrIters.get(this);
        if (!it) throw new TypeError('Illegal invocation');
        const pairs = hdrSorted(it.s);
        if (it.i >= pairs.length) return { value: undefined, done: true };
        const [k, v] = pairs[it.i++];
        return { value: it.kind === 'keys' ? k : it.kind === 'values' ? v : [k, v], done: false };
    }, writable: true, enumerable: true, configurable: true },
    [Symbol.toStringTag]: { value: 'Headers Iterator', configurable: true },
});
const hdrIter = (h, kind) => { const it = Object.create(HeadersIteratorProto); hdrIters.set(it, { s: hdrOf(h), kind, i: 0 }); return it; };
class Headers {
    constructor(init = undefined) {
        const s = { list: [], guard: 'none', ver: 0, sorted: null, sortedVer: -1, sortedList: null };
        hdrState.set(this, s);
        if (init !== undefined) for (const [n, v] of headersInit(init, "construct 'Headers'")) hdrAppend(s, n, v, "construct 'Headers'");
    }
    append(name, value) {
        const W = "execute 'append' on 'Headers'", s = hdrOf(this);
        fargs(2, arguments.length, W);
        hdrAppend(s, fbyteString(name, W), fbyteString(value, W), W);
    }
    delete(name) {
        const W = "execute 'delete' on 'Headers'", s = hdrOf(this);
        fargs(1, arguments.length, W);
        name = fbyteString(name, W);
        hdrCheck(s, name, null, W);
        const l = name.toLowerCase();
        if (s.guard === 'request' && forbiddenRequestHeader(name, '')) return;
        if (s.guard === 'request-no-cors' && !NO_CORS_NAMES.has(l) && l !== 'range') return;
        if (s.guard === 'response' && forbiddenResponseName(name)) return;
        if (!hdrHas(s.list, name)) return;
        s.list = s.list.filter(([n]) => n.toLowerCase() !== l); s.ver++;
        hdrDropPrivileged(s);
    }
    get(name) {
        const W = "execute 'get' on 'Headers'", s = hdrOf(this);
        fargs(1, arguments.length, W);
        name = fbyteString(name, W);
        if (!FETCH_TOKEN.test(name)) throw ferr(W, 'Invalid name');
        return hdrGet(s.list, name);
    }
    getSetCookie() { return hdrOf(this).list.filter(([n]) => n.toLowerCase() === 'set-cookie').map(e => e[1]); }
    has(name) {
        const W = "execute 'has' on 'Headers'", s = hdrOf(this);
        fargs(1, arguments.length, W);
        name = fbyteString(name, W);
        if (!FETCH_TOKEN.test(name)) throw ferr(W, 'Invalid name');
        return hdrHas(s.list, name);
    }
    set(name, value) {
        const W = "execute 'set' on 'Headers'", s = hdrOf(this);
        fargs(2, arguments.length, W);
        name = fbyteString(name, W); value = fbyteString(value, W).replace(FETCH_WS_TRIM, '');
        hdrCheck(s, name, value, W);
        if (s.guard === 'request' && forbiddenRequestHeader(name, value)) return;
        if (s.guard === 'request-no-cors' && !noCorsSafelisted(name, value)) return;
        if (s.guard === 'response' && forbiddenResponseName(name)) return;
        const l = name.toLowerCase(), i = s.list.findIndex(([n]) => n.toLowerCase() === l);
        if (i < 0) s.list.push([name, value]);
        else s.list = s.list.filter((e, j) => j <= i || e[0].toLowerCase() !== l).map((e, j) => j === i ? [e[0], value] : e);
        s.ver++;
        hdrDropPrivileged(s);
    }
    forEach(callback, thisArg = undefined) {
        const s = hdrOf(this);
        if (typeof callback !== 'function') throw ferr("execute 'forEach' on 'Headers'", 'The callback provided as parameter 1 is not a function.');
        for (let i = 0, pairs = hdrSorted(s); i < pairs.length; i++, pairs = hdrSorted(s)) callback.call(thisArg, pairs[i][1], pairs[i][0], this);
    }
    entries() { return hdrIter(this, 'entries'); }
    keys() { return hdrIter(this, 'keys'); }
    values() { return hdrIter(this, 'values'); }
}
Object.defineProperty(Headers.prototype, Symbol.iterator, { value: Headers.prototype.entries, writable: true, configurable: true });
Object.defineProperty(Headers.prototype, Symbol.toStringTag, { value: 'Headers', configurable: true });
const headersFlat = h => hdrOf(h).list.flat();

/* ---- FormData ---- */
const fdState = new WeakMap();
const fdOf = f => { const s = fdState.get(f); if (!s) throw new TypeError('Illegal invocation'); return s; };
function fdEntry(name, value, filename, n, where) {
    name = String(name).toWellFormed();
    if (value instanceof Blob) {
        if (filename !== undefined) value = new File([value], String(filename).toWellFormed(), { type: value.type, lastModified: value instanceof File ? value.lastModified : Date.now() });
        else if (!(value instanceof File)) value = new File([value], 'blob', { type: value.type });
    } else if (n > 2) throw ferr(where, "parameter 2 is not of type 'Blob'.");
    else value = String(value).toWellFormed();
    return [name, value];
}
const fdIters = new WeakMap();
const FormDataIteratorProto = Object.create(Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())), {
    next: { value: function next() {
        const it = fdIters.get(this);
        if (!it) throw new TypeError('Illegal invocation');
        if (it.i >= it.s.list.length) return { value: undefined, done: true };
        const [k, v] = it.s.list[it.i++];
        return { value: it.kind === 'keys' ? k : it.kind === 'values' ? v : [k, v], done: false };
    }, writable: true, enumerable: true, configurable: true },
    [Symbol.toStringTag]: { value: 'FormData Iterator', configurable: true },
});
const fdIter = (f, kind) => { const it = Object.create(FormDataIteratorProto); fdIters.set(it, { s: fdOf(f), kind, i: 0 }); return it; };
class FormData {
    constructor(form = undefined, submitter = null) {
        const s = { list: [] };
        if (form !== undefined) s.list = FORM_ENTRIES(form, submitter).map(([k, v]) => fdEntry(k, v, undefined, 2, "construct 'FormData'"));
        fdState.set(this, s);
        if (form !== undefined) FORM_FIRE(form, this);
    }
    append(name, value, filename = undefined) {
        const W = "execute 'append' on 'FormData'", s = fdOf(this);
        fargs(2, arguments.length, W);
        s.list.push(fdEntry(name, value, filename, arguments.length, W));
    }
    delete(name) { const s = fdOf(this); fargs(1, arguments.length, "execute 'delete' on 'FormData'"); name = String(name).toWellFormed(); s.list = s.list.filter(e => e[0] !== name); }
    get(name) { const s = fdOf(this); fargs(1, arguments.length, "execute 'get' on 'FormData'"); name = String(name).toWellFormed(); const e = s.list.find(x => x[0] === name); return e ? e[1] : null; }
    getAll(name) { const s = fdOf(this); fargs(1, arguments.length, "execute 'getAll' on 'FormData'"); name = String(name).toWellFormed(); return s.list.filter(x => x[0] === name).map(x => x[1]); }
    has(name) { const s = fdOf(this); fargs(1, arguments.length, "execute 'has' on 'FormData'"); name = String(name).toWellFormed(); return s.list.some(x => x[0] === name); }
    set(name, value, filename = undefined) {
        const W = "execute 'set' on 'FormData'", s = fdOf(this);
        fargs(2, arguments.length, W);
        const e = fdEntry(name, value, filename, arguments.length, W), i = s.list.findIndex(x => x[0] === e[0]);
        if (i < 0) s.list.push(e);
        else { s.list[i] = e; s.list = s.list.filter((x, j) => j <= i || x[0] !== e[0]); }
    }
    forEach(callback, thisArg = undefined) {
        const s = fdOf(this);
        if (typeof callback !== 'function') throw ferr("execute 'forEach' on 'FormData'", 'The callback provided as parameter 1 is not a function.');
        for (let i = 0; i < s.list.length; i++) callback.call(thisArg, s.list[i][1], s.list[i][0], this);
    }
    entries() { return fdIter(this, 'entries'); }
    keys() { return fdIter(this, 'keys'); }
    values() { return fdIter(this, 'values'); }
}
Object.defineProperty(FormData.prototype, Symbol.iterator, { value: FormData.prototype.entries, writable: true, configurable: true });
Object.defineProperty(FormData.prototype, Symbol.toStringTag, { value: 'FormData', configurable: true });
/* multipart/form-data as browsers send it: names escaped, text newlines as CRLF, files with filename and type */
function multipartEncode(fd) {
    const bd = '----LumenFormBoundary' + Math.random().toString(36).slice(2), esc = x => x.replace(/\n/g, '%0A').replace(/\r/g, '%0D').replace(/"/g, '%22');
    const crlf = x => x.replace(/\r\n|\r|\n/g, '\r\n'), parts = [];
    for (const [k, v] of fdOf(fd).list) {
        const name = esc(crlf(k));
        if (v instanceof Blob) {
            parts.push(fenc(`--${bd}\r\nContent-Disposition: form-data; name="${name}"; filename="${esc(v.name)}"\r\nContent-Type: ${v.type || 'application/octet-stream'}\r\n\r\n`), FETCH_BLOB(v), fenc('\r\n'));
        } else parts.push(fenc(`--${bd}\r\nContent-Disposition: form-data; name="${name}"\r\n\r\n${crlf(v)}\r\n`));
    }
    parts.push(fenc(`--${bd}--\r\n`));
    return [fconcat(parts), bd];
}
/* Body.formData() of a multipart/form-data body; null when it does not parse */
function multipartDecode(bytes, boundary) {
    const s = flatin1(bytes), delim = '--' + boundary, fd = new FormData(), list = fdOf(fd).list;
    const unesc = x => fdec(fbytesOf(x)).replace(/%0A/gi, '\n').replace(/%0D/gi, '\r').replace(/%22/g, '"');
    if (!s.startsWith(delim)) return null;
    let p = delim.length;
    for (;;) {
        if (s.startsWith('--', p)) return fd;
        if (!s.startsWith('\r\n', p)) return null;
        p += 2;
        let name = null, filename = null, ctype = null;
        for (;;) {
            const e = s.indexOf('\r\n', p);
            if (e < 0) return null;
            const line = s.slice(p, e); p = e + 2;
            if (line === '') break;
            const c = line.indexOf(':');
            if (c <= 0) return null;
            const hn = line.slice(0, c).trim().toLowerCase(), hv = line.slice(c + 1).replace(/^[\t ]+|[\t ]+$/g, '');
            if (hn === 'content-disposition') {
                if (!/^form-data\s*(;|$)/i.test(hv)) return null;
                const nm = /;\s*name="([^"]*)"/i.exec(hv), fn = /;\s*filename="([^"]*)"/i.exec(hv);
                if (!nm) return null;
                name = unesc(nm[1]); filename = fn ? unesc(fn[1]) : null;
            } else if (hn === 'content-type') ctype = hv;
        }
        if (name === null) return null;
        const end = s.indexOf('\r\n' + delim, p);
        if (end < 0) return null;
        const body = bytes.slice(p, end);
        list.push([name, filename !== null ? new File([body], filename, { type: ctype || 'text/plain' }) : fdec(body)]);
        p = end + 2 + delim.length;
    }
}

/* ---- bodies: { stream, bytes, taken, ctl, err } (bytes null for a stream body) ---- */
function bodyStreamOf(b) {
    if (!b.stream) {
        const bytes = b.bytes, err = b.err;
        b.stream = new ReadableStream({ __proto__: null, type: 'bytes', start(c) { b.ctl = c; if (err !== undefined) { c.error(err); return; } if (bytes.byteLength) c.enqueue(bytes.slice()); c.close(); } });
    }
    return b.stream;
}
const bodyUsedOf = b => b !== null && (b.taken || (!!b.stream && streamDisturbed(b.stream)));
const bodyUnusable = b => b !== null && (bodyUsedOf(b) || (!!b.stream && b.stream.locked));
/* errors a body once its fetch is aborted, unless it was already read */
function bodyAbort(b, reason) {
    if (!b) return;
    if (!b.stream) { b.err = reason; return; }
    if (b.ctl) try { b.ctl.error(reason); } catch (e) {}
}
function bodyClone(b) {
    if (b.stream) { const [x, y] = b.stream.tee(); b.stream = x; b.ctl = null; return { stream: y, bytes: b.bytes, taken: false }; }
    return { stream: null, bytes: b.bytes, taken: false, err: b.err };
}
/* new Request(request): the body moves to the new request and the old one becomes used */
function bodyTake(b) {
    const s = bodyStreamOf(b); b.taken = true;
    if (b.bytes !== null) { s.getReader().read().catch(() => {}); return { stream: null, bytes: b.bytes, taken: false }; }
    const ts = new TransformStream(); s.pipeTo(ts.writable, { __proto__: null }).catch(() => {});
    return { stream: ts.readable, bytes: null, taken: false };
}
async function bodyReadAll(stream) {
    const reader = stream.getReader(), parts = [];
    for (;;) {
        const { value, done } = await reader.read();
        if (done) return fconcat(parts);
        if (!(value instanceof Uint8Array)) throw new TypeError('The body stream must contain Uint8Array chunks.');
        parts.push(value.slice());
    }
}
/* "extract a body": { body, type } */
function extractBody(o, keepalive, where) {
    let bytes = null, stream = null, type = null;
    if (typeof o === 'string') { bytes = fenc(o); type = 'text/plain;charset=UTF-8'; }
    else if (o instanceof Blob) { bytes = FETCH_BLOB(o).slice(); if (o.type) type = o.type; }
    else if (o instanceof ArrayBuffer || (typeof SharedArrayBuffer === 'function' && o instanceof SharedArrayBuffer)) bytes = new Uint8Array(o.slice(0));
    else if (ArrayBuffer.isView(o)) bytes = new Uint8Array(o.buffer.slice(o.byteOffset, o.byteOffset + o.byteLength));
    else if (o instanceof URLSearchParams) { bytes = fenc(o.toString()); type = 'application/x-www-form-urlencoded;charset=UTF-8'; }
    else if (o instanceof FormData) { const [b, bd] = multipartEncode(o); bytes = b; type = 'multipart/form-data; boundary=' + bd; }
    else if (o instanceof ReadableStream) {
        if (keepalive) throw ferr(where, 'keepalive cannot be set when the body is a ReadableStream.');
        if (streamDisturbed(o) || o.locked) throw ferr(where, 'The ReadableStream is disturbed or locked.');
        stream = o;
    } else { bytes = fenc(String(o)); type = 'text/plain;charset=UTF-8'; }
    return { body: { stream, bytes, taken: false }, type };
}
const reqState = new WeakMap(), respState = new WeakMap();
const bodyOwner = o => reqState.get(o) || respState.get(o);
function bodyConsume(o, kind) {
    const st = bodyOwner(o);
    if (!st) return Promise.reject(new TypeError('Illegal invocation'));
    const iface = reqState.has(o) ? 'Request' : 'Response', b = st.body;
    if (bodyUnusable(b)) return Promise.reject(ferr(`execute '${kind}' on '${iface}'`, 'body stream already read'));
    const list = hdrOf(st.headers).list;
    return (b === null ? Promise.resolve(new Uint8Array(0)) : bodyReadAll(bodyStreamOf(b))).then(bytes => {
        switch (kind) {
        case 'arrayBuffer': return bytes.buffer;
        case 'bytes': return bytes;
        case 'text': return fdec(bytes);
        case 'json': return JSON.parse(fdec(bytes));
        case 'blob': { const m = extractMime(list); return new Blob([bytes], { type: m ? serializeMime(m) : '' }); }
        case 'formData': {
            const m = extractMime(list), e = m && mimeEssence(m);
            if (e === 'multipart/form-data') {
                const fd = m.params.has('boundary') ? multipartDecode(bytes, m.params.get('boundary')) : null;
                if (!fd) throw ferr(`execute 'formData' on '${iface}'`, 'Invalid form data');
                return fd;
            }
            if (e === 'application/x-www-form-urlencoded') { const fd = new FormData(); for (const [k, v] of new URLSearchParams(fdec(bytes))) fdOf(fd).list.push([k, v]); return fd; }
            throw ferr(`execute 'formData' on '${iface}'`, 'Invalid MIME type');
        }
        }
    });
}
const BODY_MIXIN = {
    get body() { const st = bodyOwner(this); if (!st) throw new TypeError('Illegal invocation'); return st.body === null ? null : bodyStreamOf(st.body); },
    get bodyUsed() { const st = bodyOwner(this); if (!st) throw new TypeError('Illegal invocation'); return bodyUsedOf(st.body); },
    arrayBuffer() { return bodyConsume(this, 'arrayBuffer'); },
    blob() { return bodyConsume(this, 'blob'); },
    bytes() { return bodyConsume(this, 'bytes'); },
    formData() { return bodyConsume(this, 'formData'); },
    json() { return bodyConsume(this, 'json'); },
    text() { return bodyConsume(this, 'text'); },
    textStream() {
        const st = bodyOwner(this);
        if (!st) throw new TypeError('Illegal invocation');
        if (bodyUnusable(st.body)) throw ferr(`execute 'textStream' on '${reqState.has(this) ? 'Request' : 'Response'}'`, 'body stream already read');
        return (st.body === null ? new ReadableStream({ __proto__: null, start(c) { c.close(); } }) : bodyStreamOf(st.body)).pipeThrough(new TextDecoderStream());
    },
};

/* ---- Request ---- */
const reqOf = r => { const s = reqState.get(r); if (!s) throw new TypeError('Illegal invocation'); return s; };
const REQ_ENUMS = {
    mode: ['navigate', 'same-origin', 'no-cors', 'cors'], credentials: ['omit', 'same-origin', 'include'],
    cache: ['default', 'no-store', 'reload', 'no-cache', 'force-cache', 'only-if-cached'], redirect: ['follow', 'error', 'manual'],
    referrerPolicy: ['', 'no-referrer', 'no-referrer-when-downgrade', 'same-origin', 'origin', 'strict-origin', 'origin-when-cross-origin', 'strict-origin-when-cross-origin', 'unsafe-url'],
    priority: ['high', 'low', 'auto'], duplex: ['half'],
};
const REQ_INIT_KEYS = ['body', 'cache', 'credentials', 'duplex', 'headers', 'integrity', 'keepalive', 'method', 'mode', 'priority', 'redirect', 'referrer', 'referrerPolicy', 'signal', 'window'];
function readRequestInit(init, where) {
    const o = {};
    if (init === undefined || init === null) return o;
    if (!fisObj(init)) throw ferr(where, "The provided value is not of type 'RequestInit'.");
    for (const k of REQ_INIT_KEYS) {
        let v = init[k];
        if (v === undefined) continue;
        if (REQ_ENUMS[k]) { v = String(v); if (!REQ_ENUMS[k].includes(v)) throw ferr(where, `Failed to read the '${k}' property from 'RequestInit': The provided value '${v}' is not a valid enum value.`); }
        else if (k === 'keepalive') v = !!v;
        else if (k === 'method') v = fbyteString(v, where);
        else if (k === 'integrity' || k === 'referrer') v = String(v);
        else if (k === 'signal') { if (v !== null && !(v instanceof AbortSignal)) throw ferr(where, "Failed to read the 'signal' property from 'RequestInit': Failed to convert value to 'AbortSignal'."); }
        else if (k === 'headers') v = headersInit(v, where);
        o[k] = v;
    }
    return o;
}
const followSignal = s => s ? AbortSignal.any([s]) : new AbortController().signal;
class Request {
    constructor(input, init = undefined) {
        const W = "construct 'Request'";
        fargs(1, arguments.length, W);
        const src = reqState.get(input) || null;
        if (!src) input = String(input).toWellFormed();
        const o = readRequestInit(init, W), base = FETCH_BASE();
        let r, fallbackMode = null, signal = null;
        if (!src) {
            let u; try { u = new URL(input, base); } catch (e) { throw ferr(W, `Failed to parse URL from ${input}`); }
            if (u.username || u.password) throw ferr(W, `Request cannot be constructed from a URL that includes credentials: ${input}`);
            r = { method: 'GET', url: u.href, headers: [], mode: 'no-cors', credentials: 'same-origin', cache: 'default', redirect: 'follow', referrer: 'client', referrerPolicy: '', integrity: '', keepalive: false, priority: 'auto', reloadNavigation: false, historyNavigation: false, useCorsPreflight: false };
            fallbackMode = 'cors';
        } else {
            r = { ...src, headers: hdrOf(src.headers).list.map(e => e.slice()) };
            signal = src.signal;
        }
        if ('window' in o && o.window !== null) throw ferr(W, "'window' must be null.");
        if (Object.keys(o).length) {
            if (r.mode === 'navigate') r.mode = 'same-origin';
            r.reloadNavigation = r.historyNavigation = false;
            r.referrer = 'client'; r.referrerPolicy = '';
        }
        if ('referrer' in o) {
            if (o.referrer === '') r.referrer = 'no-referrer';
            else {
                let p; try { p = new URL(o.referrer, base); } catch (e) { throw ferr(W, `Referrer '${o.referrer}' is not a valid URL.`); }
                r.referrer = p.href === 'about:client' || p.origin !== location.origin ? 'client' : p.href;
            }
        }
        if ('referrerPolicy' in o) r.referrerPolicy = o.referrerPolicy;
        const mode = 'mode' in o ? o.mode : fallbackMode;
        if (mode === 'navigate') throw ferr(W, "Cannot construct a Request with a RequestInit whose mode member is set as 'navigate'.");
        if (mode !== null) r.mode = mode;
        for (const k of ['credentials', 'cache', 'redirect', 'integrity', 'keepalive', 'priority']) if (k in o) r[k] = o[k];
        if (r.cache === 'only-if-cached' && r.mode !== 'same-origin') throw ferr(W, "'only-if-cached' can be set only with 'same-origin' mode");
        if ('method' in o) {
            let m = o.method;
            if (!FETCH_TOKEN.test(m)) throw ferr(W, `'${m}' is not a valid HTTP method.`);
            if (/^(connect|trace|track)$/i.test(m)) throw ferr(W, `'${m}' HTTP method is unsupported.`);
            if (/^(delete|get|head|options|post|put)$/i.test(m)) m = m.toUpperCase();
            r.method = m;
        }
        if ('signal' in o) signal = o.signal;
        const h = hdrNew(r.headers, 'request'), hs = hdrOf(h);
        if (r.mode === 'no-cors') {
            if (!/^(GET|HEAD|POST)$/.test(r.method)) throw ferr(W, `'${r.method}' is unsupported in no-cors mode.`);
            hs.guard = 'request-no-cors';
        }
        if (Object.keys(o).length) {
            const list = 'headers' in o ? o.headers : hs.list.map(e => e.slice());
            hs.list = []; hs.ver++;
            for (const [n, v] of list) hdrAppend(hs, n, v, W);
        }
        const inputBody = src ? src.body : null;
        if ((('body' in o && o.body !== null) || inputBody !== null) && (r.method === 'GET' || r.method === 'HEAD')) throw ferr(W, 'Request with GET/HEAD method cannot have body.');
        let initBody = null;
        if ('body' in o && o.body !== null) {
            const x = extractBody(o.body, r.keepalive, W);
            initBody = x.body;
            if (x.type !== null && !hdrHas(hs.list, 'content-type')) { hs.list.push(['Content-Type', x.type]); hs.ver++; }
        }
        const body = initBody || inputBody;
        if (body !== null && body.bytes === null) {
            if (initBody !== null && !('duplex' in o)) throw ferr(W, 'The `duplex` member must be specified for a request with a streaming body.');
            if (r.mode !== 'same-origin' && r.mode !== 'cors') throw ferr(W, 'If request is made from ReadableStream, mode should be "same-origin" or "cors".');
            r.useCorsPreflight = true;
        }
        let finalBody = body;
        if (initBody === null && inputBody !== null) {
            if (bodyUnusable(inputBody)) throw ferr(W, 'Cannot construct a Request with a Request object that has already been used.');
            finalBody = bodyTake(inputBody);
        }
        reqState.set(this, { ...r, headers: h, signal: followSignal(signal), body: finalBody });
    }
    get method() { return reqOf(this).method; }
    get url() { return reqOf(this).url; }
    get headers() { return reqOf(this).headers; }
    get destination() { reqOf(this); return ''; }
    get referrer() { const r = reqOf(this).referrer; return r === 'no-referrer' ? '' : r === 'client' ? 'about:client' : r; }
    get referrerPolicy() { return reqOf(this).referrerPolicy; }
    get mode() { return reqOf(this).mode; }
    get credentials() { return reqOf(this).credentials; }
    get cache() { return reqOf(this).cache; }
    get redirect() { return reqOf(this).redirect; }
    get integrity() { return reqOf(this).integrity; }
    get keepalive() { return reqOf(this).keepalive; }
    get isReloadNavigation() { return reqOf(this).reloadNavigation; }
    get isHistoryNavigation() { return reqOf(this).historyNavigation; }
    get signal() { return reqOf(this).signal; }
    get duplex() { reqOf(this); return 'half'; }
    clone() {
        const s = reqOf(this);
        if (bodyUnusable(s.body)) throw ferr("execute 'clone' on 'Request'", 'Request body is already used');
        const c = Object.create(Request.prototype), hs = hdrOf(s.headers);
        reqState.set(c, { ...s, headers: hdrNew(hs.list.map(e => e.slice()), hs.guard), signal: followSignal(s.signal), body: s.body && bodyClone(s.body) });
        return c;
    }
}
Object.defineProperties(Request.prototype, Object.getOwnPropertyDescriptors(BODY_MIXIN));
Object.defineProperty(Request.prototype, Symbol.toStringTag, { value: 'Request', configurable: true });

/* ---- Response ---- */
const respOf = r => { const s = respState.get(r); if (!s) throw new TypeError('Illegal invocation'); return s; };
const NULL_BODY_STATUS = new Set([101, 103, 204, 205, 304]), REDIRECT_STATUS = new Set([301, 302, 303, 307, 308]);
function readResponseInit(init, where) {
    const o = { headers: null, status: 200, statusText: '' };
    if (init === undefined || init === null) return o;
    if (!fisObj(init)) throw ferr(where, "The provided value is not of type 'ResponseInit'.");
    if (init.headers !== undefined) o.headers = headersInit(init.headers, where);
    if (init.status !== undefined) o.status = fushort(init.status);
    if (init.statusText !== undefined) o.statusText = fbyteString(init.statusText, where);
    return o;
}
function respMake(st) { const r = Object.create(Response.prototype); respState.set(r, st); FETCH_BRAND(r); return r; }
/* "initialize a response" */
function respInit(st, i, x, where) {
    if (i.status < 200 || i.status > 599) throw new RangeError(`Failed to ${where}: The status provided (${i.status}) is outside the range [200, 599].`);
    if (!/^[\t\x20-\x7e\x80-\xff]*$/.test(i.statusText)) throw ferr(where, 'Invalid statusText');
    st.status = i.status; st.statusText = i.statusText;
    const hs = hdrOf(st.headers);
    if (i.headers) for (const [n, v] of i.headers) hdrAppend(hs, n, v, where);
    if (x) {
        if (NULL_BODY_STATUS.has(st.status)) throw ferr(where, 'Response with null body status cannot have body');
        st.body = x.body;
        if (x.type !== null && !hdrHas(hs.list, 'content-type')) { hs.list.push(['Content-Type', x.type]); hs.ver++; }
    }
}
const respBlank = () => ({ type: 'default', status: 200, statusText: '', headers: hdrNew([], 'response'), urlList: [], body: null });
class Response {
    constructor(body = null, init = undefined) {
        const W = "construct 'Response'";
        const x = body !== null ? extractBody(body, false, W) : null, st = respBlank();
        respInit(st, readResponseInit(init, W), x, W);
        respState.set(this, st);
        FETCH_BRAND(this);
    }
    static error() { return respMake({ type: 'error', status: 0, statusText: '', headers: hdrNew([], 'immutable'), urlList: [], body: null }); }
    static redirect(url, status = 302) {
        const W = "execute 'redirect' on 'Response'";
        fargs(1, arguments.length, W);
        url = String(url).toWellFormed();
        let u; try { u = new URL(url, FETCH_BASE()); } catch (e) { throw ferr(W, `Failed to parse URL from ${url}`); }
        status = fushort(status);
        if (!REDIRECT_STATUS.has(status)) throw new RangeError(`Failed to ${W}: Invalid status code`);
        return respMake({ type: 'default', status, statusText: '', headers: hdrNew([['Location', u.href]], 'immutable'), urlList: [], body: null });
    }
    static json(data, init = undefined) {
        const W = "execute 'json' on 'Response'";
        fargs(1, arguments.length, W);
        const s = JSON.stringify(data);
        if (s === undefined) throw ferr(W, 'The data is not JSON serializable');
        const i = readResponseInit(init, W), st = respBlank();
        respInit(st, i, { body: { stream: null, bytes: fenc(s), taken: false }, type: 'application/json' }, W);
        return respMake(st);
    }
    get type() { return respOf(this).type; }
    get url() { const l = respOf(this).urlList; return l.length ? l[l.length - 1].replace(/#.*$/s, '') : ''; }
    get redirected() { return respOf(this).urlList.length > 1; }
    get status() { return respOf(this).status; }
    get ok() { const s = respOf(this).status; return s >= 200 && s <= 299; }
    get statusText() { return respOf(this).statusText; }
    get headers() { return respOf(this).headers; }
    clone() {
        const s = respOf(this);
        if (bodyUnusable(s.body)) throw ferr("execute 'clone' on 'Response'", 'Response body is already used');
        const hs = hdrOf(s.headers);
        return respMake({ ...s, headers: hdrNew(hs.list.map(e => e.slice()), hs.guard), urlList: s.urlList.slice(), body: s.body && bodyClone(s.body) });
    }
}
Object.defineProperties(Response.prototype, Object.getOwnPropertyDescriptors(BODY_MIXIN));
Object.defineProperty(Response.prototype, Symbol.toStringTag, { value: 'Response', configurable: true });
if (FETCH_WASM) FETCH_WASM(response => {
    const st = respOf(response);
    return { mime: hdrGet(hdrOf(st.headers).list, 'content-type') || '', status: st.status, type: st.type, url: response.url,
        take: () => { if (bodyUnusable(st.body)) throw new TypeError('Body has already been consumed or locked.'); return st.body ? bodyStreamOf(st.body).getReader() : null; } };
});
/* a Response for stored or synthesized state: { status, statusText, headers: [[name, value]], bytes, url, type, redirected } */
const responseFrom = e => respMake({ type: e.type || 'default', status: e.status, statusText: e.statusText || '', headers: hdrNew(e.headers.map(x => x.slice()), 'immutable'),
    urlList: e.url ? (e.redirected ? [e.url, e.url] : [e.url]) : [], body: e.bytes ? { stream: null, bytes: e.bytes.slice(), taken: false } : null });

/* ---- fetch() ---- */
/* data: URL processor (Fetch § 4.2) */
function dataURL(href) {
    const input = href.replace(/#.*$/s, '').slice(5), comma = input.indexOf(',');
    if (comma < 0) return null;
    let mime = input.slice(0, comma).replace(/^[\t\n\f\r ]+|[\t\n\f\r ]+$/g, ''), raw = '';
    const enc = input.slice(comma + 1);
    for (let i = 0; i < enc.length; i++) {
        if (enc[i] === '%' && /^[0-9A-Fa-f]{2}$/.test(enc.slice(i + 1, i + 3))) { raw += String.fromCharCode(parseInt(enc.slice(i + 1, i + 3), 16)); i += 2; }
        else raw += enc[i];
    }
    let bytes;
    if (/;[\x20]*base64$/i.test(mime)) {
        try { raw = atob(raw); } catch (e) { return null; }
        mime = mime.replace(/;[\x20]*base64$/i, '');
    }
    bytes = fbytesOf(raw);
    if (mime.startsWith(';')) mime = 'text/plain' + mime;
    const m = parseMime(mime) || parseMime('text/plain;charset=US-ASCII');
    return { mime: serializeMime(m), bytes };
}
/* a single "Range: bytes=" value, whitespace allowed: [start, end] with either null, or null */
function parseRange(v) {
    const m = /^bytes[\t ]*=[\t ]*(\d*)[\t ]*-[\t ]*(\d*)[\t ]*$/i.exec(v);
    if (!m || (m[1] === '' && m[2] === '')) return null;
    const s = m[1] === '' ? null : +m[1], e = m[2] === '' ? null : +m[2];
    return s !== null && e !== null && s > e ? null : [s, e];
}
const fetchTrustworthy = u => /^(https|wss|file):$/.test(u.protocol) || /^(localhost|127(\.\d+){3}|\[::1\])$|\.localhost$/.test(u.hostname);
const fetchSite = h => /^[\d.]+$|^\[/.test(h) ? h : h.split('.').slice(-2).join('.');
const fetchSameSite = (a, b) => a.protocol === b.protocol && fetchSite(a.hostname) === fetchSite(b.hostname);
const REFERRER_POLICIES = REQ_ENUMS.referrerPolicy.slice(1);
const parseReferrerPolicy = v => { let p = ''; if (v !== null) for (const t of v.split(',')) { const x = t.trim().toLowerCase(); if (REFERRER_POLICIES.includes(x)) p = x; } return p; };
/* "determine request's referrer" (Referrer Policy § 8.3): the Referer value, or null for none */
function fetchReferrer(R, url) {
    let ref = R.referrer;
    if (ref === 'no-referrer') return null;
    if (ref === 'client') ref = location.href;
    let u; try { u = new URL(ref); } catch (e) { return null; }
    if (/^(about|blob|data):$/.test(u.protocol)) return null;
    u.username = ''; u.password = ''; u.hash = '';
    let full = u.href.replace(/#$/, '');
    const origin = u.origin + '/', same = u.origin === url.origin, down = fetchTrustworthy(u) && !fetchTrustworthy(url);
    if (full.length > 4096) full = origin;
    switch (R.referrerPolicy || 'strict-origin-when-cross-origin') {
    case 'no-referrer': return null;
    case 'origin': return origin;
    case 'unsafe-url': return full;
    case 'strict-origin': return down ? null : origin;
    case 'same-origin': return same ? full : null;
    case 'origin-when-cross-origin': return same ? full : origin;
    case 'no-referrer-when-downgrade': return down ? null : full;
    default: return same ? full : down ? null : origin;
    }
}
const fetchOrigin = R => R.tainted ? 'null' : R.origin;
function corsCheck(R, list) {
    const o = hdrGet(list, 'access-control-allow-origin');
    if (o === null) return false;
    if (R.credentials !== 'include' && o === '*') return true;
    if (o !== fetchOrigin(R)) return false;
    return R.credentials !== 'include' || hdrGet(list, 'access-control-allow-credentials') === 'true';
}
/* the headers of one network request: the request's, then those the fetch adds */
function fetchHeaders(R, url, preflight) {
    const h = preflight ? [['Accept', '*/*'], ...R.headers] : R.headers.map(e => e.slice()), has = n => hdrHas(h, n);
    if (!preflight) {
        let cache = R.cache;
        if (cache === 'default' && ['if-modified-since', 'if-none-match', 'if-unmodified-since', 'if-match', 'if-range'].some(has)) cache = 'no-store';
        if (cache === 'no-cache' && !has('cache-control')) h.push(['Cache-Control', 'max-age=0']);
        if (cache === 'no-store' || cache === 'reload') { if (!has('pragma')) h.push(['Pragma', 'no-cache']); if (!has('cache-control')) h.push(['Cache-Control', 'no-cache']); }
        if (!has('accept')) h.push(['Accept', '*/*']);
    }
    h.push(['Referer', fetchReferrer(R, url) || '']);
    let o = fetchOrigin(R);
    if (R.tainting === 'cors' || preflight) h.push(['Origin', o]);
    else if (R.method !== 'GET' && R.method !== 'HEAD') {
        if (R.mode !== 'cors') {
            switch (R.referrerPolicy || 'strict-origin-when-cross-origin') {
            case 'no-referrer': o = 'null'; break;
            case 'no-referrer-when-downgrade': case 'strict-origin': case 'strict-origin-when-cross-origin': if (/^https:/.test(R.origin) && url.protocol !== 'https:') o = 'null'; break;
            case 'same-origin': if (url.origin !== R.origin) o = 'null'; break;
            }
        }
        h.push(['Origin', o]);
    }
    if (fetchTrustworthy(url)) {   /* Fetch Metadata */
        let site = 'same-origin';
        const origin = new URL(R.origin === 'null' ? 'about:blank' : R.origin);
        for (const x of R.urlList) {
            const u = new URL(x);
            if (u.origin === R.origin) continue;
            site = 'same-site';
            if (!fetchSameSite(u, origin)) { site = 'cross-site'; break; }
        }
        h.push(['Sec-Fetch-Dest', 'empty'], ['Sec-Fetch-Mode', preflight ? 'cors' : R.mode], ['Sec-Fetch-Site', site]);
    }
    return h;
}
const flatPairs = a => { const l = []; for (let i = 0; i + 1 < a.length; i += 2) l.push([String(a[i]), String(a[i + 1])]); return l; };
/* one request on the network, without redirects: resolves with { status, statusText, headers, body, cancel } */
function fetchNetwork(R, url, ctl, preflight) {
    const headers = fetchHeaders(R, url, preflight);
    const cookies = !preflight && (R.credentials === 'include' || (R.credentials === 'same-origin' && R.tainting === 'basic'));
    return new Promise((resolve, reject) => {
        let streamed = false, sctl = null, cancel = null, paused = false;
        const finish = () => { if (ctl.cancel === stop) ctl.cancel = null; };
        const stop = () => { if (cancel) cancel(); cancel = null; if (sctl) { try { sctl.error(new TypeError('network error')); } catch (e) {} sctl = null; } finish(); };
        ctl.cancel = stop;
        cancel = FETCH_NET({ method: preflight ? 'OPTIONS' : R.method, url: url.href.replace(/#.*$/s, ''), headers: headers.flat(), body: preflight ? null : R.body, cookies }, {
            head(status, statusText, u, hdrs) {
                if (streamed) return;
                streamed = true;
                if ((hdrs || []).some((x, i) => i & 1 && String(x).includes('\0'))) { stop(); reject(fnet()); return; }
                const body = { stream: null, bytes: null, taken: false };
                /* flow control: the network pauses while more than 16 MB wait unread, and resumes below 1 MB */
                body.stream = new ReadableStream({ __proto__: null, type: 'bytes', start(c) { sctl = c; body.ctl = c; }, pull() { if (paused && cancel) { paused = false; cancel.pause(false); } },
                    cancel() { if (cancel) cancel(); cancel = null; sctl = null; finish(); } }, { __proto__: null, highWaterMark: 1 << 20 });
                resolve({ status, statusText, headers: flatPairs(hdrs || []), body, cancel: stop });
            },
            chunk(ab) {
                if (!sctl) return;
                try { if (ab.byteLength) sctl.enqueue(new Uint8Array(ab)); } catch (e) {}
                if (!paused && cancel && cancel.pause && sctl.desiredSize < -(16 << 20)) { paused = true; cancel.pause(true); }
            },
            done(err, status, statusText, u, hdrs, ab) {
                cancel = null; finish();
                if (streamed) { if (sctl) { try { if (err) sctl.error(new TypeError('network error')); else sctl.close(); } catch (e) {} sctl = null; } return; }
                if (err) { reject(fnet()); return; }
                if ((hdrs || []).some((x, i) => i & 1 && String(x).includes('\0'))) { reject(fnet()); return; }
                resolve({ status, statusText, headers: flatPairs(hdrs || []), body: { stream: null, bytes: new Uint8Array(ab || new ArrayBuffer(0)), taken: false }, cancel: () => {} });
            },
        });
    });
}
const preflightCache = new Map();
async function corsPreflight(R, url, ctl) {
    const names = corsUnsafeNames(R.headers), inc = R.credentials === 'include', key = R.origin + ' ' + url.href + ' ' + inc;
    const safeMethod = /^(GET|HEAD|POST)$/.test(R.method), e = preflightCache.get(key);
    if (e && e.expires > Date.now() && (safeMethod || e.methods.has(R.method) || (!inc && e.methods.has('*'))) &&
        names.every(n => e.headers.has(n) || (!inc && n !== 'authorization' && e.headers.has('*')))) return;
    const pr = { ...R, headers: [['Access-Control-Request-Method', R.method]].concat(names.length ? [['Access-Control-Request-Headers', names.join(',')]] : []) };
    const res = await fetchNetwork(pr, url, ctl, true);
    if (res.body.stream) res.cancel();
    if (!corsCheck(R, res.headers) || res.status < 200 || res.status > 299) throw fnet();
    const list = (n, re) => { const v = hdrGet(res.headers, n); if (v === null) return []; const a = v.split(',').map(x => x.trim()).filter(Boolean); return a.every(x => x === '*' || re.test(x)) ? a : null; };
    let methods = list('access-control-allow-methods', FETCH_TOKEN);
    const allowed = list('access-control-allow-headers', FETCH_TOKEN);
    if (methods === null || allowed === null) throw fnet();
    if (!methods.length && R.useCorsPreflight) methods = [R.method];
    if (!safeMethod && !methods.includes(R.method) && (inc || !methods.includes('*'))) throw fnet();
    const hl = allowed.map(x => x.toLowerCase());
    if (R.headers.some(([n]) => n.toLowerCase() === 'authorization') && !hl.includes('authorization')) throw fnet();
    for (const n of names) if (!hl.includes(n) && (inc || !hl.includes('*'))) throw fnet();
    let age = parseInt(hdrGet(res.headers, 'access-control-max-age') ?? '5', 10);
    if (!(age >= 0)) age = 5;
    if (age > 0) preflightCache.set(key, { expires: Date.now() + Math.min(age, 7200) * 1000, methods: new Set(methods), headers: new Set(hl) });
}
const REQUEST_BODY_HEADERS = new Set(['content-encoding', 'content-language', 'content-location', 'content-type']);
async function fetchHttp(R, url, ctl, preflight) {
    if (preflight) await corsPreflight(R, url, ctl);
    if (ctl.aborted) throw fnet();
    const res = await fetchNetwork(R, url, ctl, false);
    if (R.tainting === 'cors' && !corsCheck(R, res.headers)) { res.cancel(); throw fnet(); }
    if (!REDIRECT_STATUS.has(res.status) || url.protocol === 'file:') return res;
    if (R.redirect === 'error') { res.cancel(); throw fnet(); }
    if (R.redirect === 'manual') { res.cancel(); return { ...res, opaqueRedirect: true, body: null }; }
    const loc = hdrGet(res.headers, 'location');
    if (loc === null) return res;
    res.cancel();
    /* HTTP-redirect fetch */
    let u; try { u = new URL(loc, url.href); } catch (e) { throw fnet(); }
    if (!u.href.includes('#') && url.href.includes('#')) u = new URL(u.href + url.href.slice(url.href.indexOf('#')));
    if (!/^https?:$/.test(u.protocol)) throw fnet();
    if (R.redirectCount === 20) throw fnet();
    R.redirectCount++;
    if (R.mode === 'cors' && (u.username || u.password) && R.origin !== u.origin) throw fnet();
    if (R.tainting === 'cors' && (u.username || u.password)) throw fnet();
    if (res.status !== 303 && R.body !== null && R.streamBody) throw fnet();
    if (((res.status === 301 || res.status === 302) && R.method === 'POST') || (res.status === 303 && R.method !== 'GET' && R.method !== 'HEAD')) {
        R.method = 'GET'; R.body = null;
        R.headers = R.headers.filter(([n]) => !REQUEST_BODY_HEADERS.has(n.toLowerCase()));
    }
    if (url.origin !== u.origin) R.headers = R.headers.filter(([n]) => n.toLowerCase() !== 'authorization');
    if (url.origin !== u.origin && R.origin !== url.origin) R.tainted = true;
    R.url = u.href; R.urlList.push(u.href);
    const rp = parseReferrerPolicy(hdrGet(res.headers, 'referrer-policy'));
    if (rp) R.referrerPolicy = rp;
    return fetchMain(R, ctl);
}
function fetchLocal(R, url) {
    const ok = (status, statusText, headers, bytes) => ({ status, statusText, headers, body: { stream: null, bytes, taken: false }, cancel: () => {} });
    if (url.protocol === 'data:') { const d = dataURL(url.href); if (!d) throw fnet(); return ok(200, 'OK', [['Content-Type', d.mime]], d.bytes); }
    if (url.protocol !== 'blob:' || R.method !== 'GET') throw fnet();
    const blob = FETCH_BLOBURL(url.href.replace(/#.*$/s, ''));
    if (!blob) throw fnet();
    const bytes = FETCH_BLOB(blob), n = bytes.byteLength, range = hdrGet(R.headers, 'range');
    if (range === null) return ok(200, 'OK', [['Content-Length', String(n)], ['Content-Type', blob.type]], bytes.slice());
    const r = parseRange(range);
    if (!r) throw fnet();
    let [a, b] = r;
    if (a === null) { a = Math.max(0, n - b); b = n - 1; }
    else { if (a >= n) throw fnet(); if (b === null || b >= n) b = n - 1; }
    return ok(206, 'Partial Content', [['Content-Length', String(b - a + 1)], ['Content-Type', blob.type], ['Content-Range', `bytes ${a}-${b}/${n}`]], bytes.slice(a, b + 1));
}
/* Fetch's bad ports: never fetched over HTTP(S) */
const BAD_PORTS = new Set([0, 1, 7, 9, 11, 13, 15, 17, 19, 20, 21, 22, 23, 25, 37, 42, 43, 53, 69, 77, 79, 87, 95, 101, 102, 103, 104, 109, 110, 111, 113, 115, 117, 119, 123, 135, 137, 139, 143, 161, 179, 389, 427, 465, 512, 513, 514, 515, 526, 530, 531, 532, 540, 548, 554, 556, 563, 587, 601, 636, 989, 990, 993, 995, 1719, 1720, 1723, 2049, 3659, 4045, 4190, 5060, 5061, 6000, 6566, 6665, 6666, 6667, 6668, 6669, 6679, 6697, 10080]);
/* main fetch: picks the response tainting, then a scheme or HTTP fetch */
async function fetchMain(R, ctl) {
    if (ctl.aborted) throw fnet();
    const url = new URL(R.url);
    if (/^https?:$/.test(url.protocol) && url.port !== '' && BAD_PORTS.has(+url.port)) throw fnet();
    const local = url.protocol === 'data:' || url.protocol === 'blob:' || url.protocol === 'about:';
    if ((url.origin === R.origin && R.tainting === 'basic') || url.protocol === 'data:') {
        R.tainting = 'basic';
        return local ? fetchLocal(R, url) : fetchHttp(R, url, ctl, false);
    }
    if (R.mode === 'same-origin') throw fnet();
    if (R.mode === 'no-cors') {
        if (R.redirect !== 'follow') throw fnet();
        R.tainting = 'opaque';
        return local ? fetchLocal(R, url) : fetchHttp(R, url, ctl, false);
    }
    if (!/^https?:$/.test(url.protocol)) throw fnet();
    R.tainting = 'cors';
    const preflight = R.useCorsPreflight || !/^(GET|HEAD|POST)$/.test(R.method) || corsUnsafeNames(R.headers).length > 0;
    return fetchHttp(R, url, ctl, preflight);
}
const SAFE_RESPONSE_NAMES = new Set(['cache-control', 'content-language', 'content-length', 'content-type', 'expires', 'last-modified', 'pragma']);
/* the filtered response fetch() resolves with */
function fetchResponse(R, res) {
    let type = R.tainting, headers = res.headers, body = res.body, status = res.status, statusText = res.statusText, urlList = R.urlList.slice();
    if (res.opaqueRedirect) { type = 'opaqueredirect'; status = 0; statusText = ''; headers = []; body = null; }
    else if (type === 'basic') headers = headers.filter(([n]) => !forbiddenResponseName(n));
    else if (type === 'cors') {
        const ex = new Set((hdrGet(headers, 'access-control-expose-headers') || '').split(',').map(x => x.trim().toLowerCase()).filter(Boolean));
        const all = ex.has('*') && R.credentials !== 'include';
        headers = headers.filter(([n]) => { const l = n.toLowerCase(); return !forbiddenResponseName(l) && (SAFE_RESPONSE_NAMES.has(l) || ex.has(l) || all); });
    } else { if (body && body.stream) res.cancel(); status = 0; statusText = ''; headers = []; body = null; urlList = []; }
    if (body && (R.method === 'HEAD' || NULL_BODY_STATUS.has(status))) { if (body.stream) res.cancel(); body = null; }
    return respMake({ type, status, statusText, headers: hdrNew(headers, 'immutable'), urlList, body });
}
/* Subresource Integrity: the body matches one of the strongest algorithm's digests (or no metadata is usable) */
function integrityOk(metadata, bytes) {
    const ALGS = { sha256: 'SHA-256', sha384: 'SHA-384', sha512: 'SHA-512' }, RANK = ['sha256', 'sha384', 'sha512'];
    const items = [];
    for (const tok of metadata.split(/[\t\n\f\r ]+/)) {
        const m = /^(sha256|sha384|sha512)-([A-Za-z0-9+/_-]+={0,2})(\?.*)?$/.exec(tok);
        if (m) items.push([m[1], m[2]]);
    }
    if (!items.length) return true;
    const best = items.reduce((b, [a]) => Math.max(b, RANK.indexOf(a)), 0), alg = RANK[best];
    const norm = x => x.replace(/-/g, '+').replace(/_/g, '/').replace(/=+$/, '');
    const got = norm(btoa(flatin1(new Uint8Array(CN.cDigest(ALGS[alg], bytes.slice().buffer)))));
    return items.some(([a, d]) => a === alg && norm(d) === got);
}
function fetch(input, init = undefined) {
    if (arguments.length < 1) return Promise.reject(ferr("execute 'fetch'", '1 argument required, but only 0 present.'));
    let req;
    try { req = new Request(input, init); } catch (e) { return Promise.reject(e); }
    const rs = reqState.get(req), signal = rs.signal;
    if (signal.aborted) { if (rs.body && rs.body.stream) rs.body.stream.cancel(signal.reason).catch(() => {}); return Promise.reject(signal.reason); }
    return new Promise((resolve, reject) => {
        const ctl = { aborted: false, cancel: null, response: null };
        signal.addEventListener('abort', () => {
            ctl.aborted = true;
            if (ctl.cancel) ctl.cancel();
            if (rs.body && rs.body.stream && !rs.body.stream.locked) rs.body.stream.cancel(signal.reason).catch(() => {});
            reject(signal.reason);
            if (ctl.response) bodyAbort(respState.get(ctl.response).body, signal.reason);
        }, { once: true });
        const R = { method: rs.method, url: rs.url, urlList: [rs.url], headers: hdrOf(rs.headers).list.map(e => e.slice()), body: null, streamBody: false,
            mode: rs.mode, credentials: rs.credentials, cache: rs.cache, redirect: rs.redirect, referrer: rs.referrer, referrerPolicy: rs.referrerPolicy,
            origin: location.origin, tainted: false, tainting: 'basic', redirectCount: 0, useCorsPreflight: rs.useCorsPreflight };
        (async () => {
            if (rs.body) {
                if (rs.body.bytes !== null) R.body = rs.body.bytes;
                else { R.streamBody = true; R.body = await bodyReadAll(bodyStreamOf(rs.body)); }
            }
            if (rs.keepalive && R.body && R.body.byteLength > 65536) throw fnet();
            const res = await fetchMain(R, ctl);
            if (rs.integrity) {
                if (R.tainting === 'opaque') throw fnet();
                const bytes = !res.body ? new Uint8Array(0) : res.body.bytes !== null ? res.body.bytes : await bodyReadAll(res.body.stream);
                if (!integrityOk(rs.integrity, bytes)) throw fnet();
                if (res.body) res.body = { stream: null, bytes, taken: false };
            }
            return fetchResponse(R, res);
        })().then(r => { if (ctl.aborted) { bodyAbort(respState.get(r).body, signal.reason); return; } ctl.response = r; resolve(r); }, e => { if (!ctl.aborted) reject(e instanceof TypeError ? e : fnet()); });
    });
}
