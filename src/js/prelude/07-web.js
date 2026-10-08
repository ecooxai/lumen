class TextEncoder { get encoding() { return 'utf-8'; } encode(s = '') { return new Uint8Array(N.encode(String(s))); } encodeInto(s, u8) { const b = this.encode(s); const n = Math.min(b.length, u8.length); u8.set(b.subarray(0, n)); return { read: s.length, written: n }; } }
class TextDecoder { constructor(l = 'utf-8') { this.encoding = String(l).toLowerCase(); } decode(b) { if (b == null) return ''; const ab = b instanceof ArrayBuffer ? b : b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength); return N.decode(ab); } }
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) { s = String(s); let r = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); if (a > 255 || b > 255 || c > 255) throw new DOMException('The string to be encoded contains characters outside of the Latin1 range.', 'InvalidCharacterError'); const n = (a << 16) | ((b || 0) << 8) | (c || 0); r += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < s.length ? B64[n >> 6 & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '='); } return r; }
function atob(s) { s = String(s).replace(/[\t\n\f\r ]/g, ''); if (s.length % 4 === 0) s = s.replace(/==?$/, ''); if (s.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(s)) throw new DOMException('The string to be decoded is not correctly encoded.', 'InvalidCharacterError'); let r = '', bits = 0, n = 0; for (const ch of s) { n = (n << 6) | B64.indexOf(ch); bits += 6; if (bits >= 8) { bits -= 8; r += String.fromCharCode((n >> bits) & 255); } } return r; }
const toBytes = (p) => typeof p === 'string' ? N.encode(p) : p instanceof ArrayBuffer ? p : ArrayBuffer.isView(p) ? p.buffer.slice(p.byteOffset, p.byteOffset + p.byteLength) : p instanceof Blob ? p._buf : N.encode(String(p));
class Blob {
    constructor(parts = [], o = {}) { const bufs = parts.map(toBytes); const len = bufs.reduce((a, b) => a + b.byteLength, 0); const u = new Uint8Array(len); let off = 0; for (const b of bufs) { u.set(new Uint8Array(b), off); off += b.byteLength; } def(this, '_buf', u.buffer); this.type = String(o.type || '').toLowerCase(); }
    get size() { return this._buf.byteLength; }
    slice(a = 0, b = this.size, t = '') { return new Blob([this._buf.slice(a, b)], { type: t }); }
    arrayBuffer() { return Promise.resolve(this._buf.slice(0)); } text() { return Promise.resolve(N.decode(this._buf)); } bytes() { return Promise.resolve(new Uint8Array(this._buf.slice(0))); }
    stream() { const b = this._buf; return new ReadableStream({ start(c) { c.enqueue(new Uint8Array(b)); c.close(); } }); }
}
class File extends Blob { constructor(p, name, o = {}) { super(p, o); this.name = String(name); this.lastModified = o.lastModified || Date.now(); } }
class FileReader extends EventTarget {
    constructor() { super(); this.result = null; this.readyState = 0; this.error = null; }
    _read(b, f) { this.readyState = 1; setTimeout(() => { this.result = f(b); this.readyState = 2; for (const t of ['load', 'loadend']) { const ev = new ProgressEvent(t); if (this['on' + t]) this['on' + t](ev); dispatch(this, ev); } }); }
    readAsText(b) { this._read(b, x => N.decode(x._buf)); } readAsArrayBuffer(b) { this._read(b, x => x._buf.slice(0)); }
    readAsDataURL(b) { this._read(b, x => 'data:' + (x.type || 'application/octet-stream') + ';base64,' + btoa(String.fromCharCode(...new Uint8Array(x._buf)))); }
    abort() {}
}
class ReadableStream {
    constructor(src = {}) { def(this, '_q', []); def(this, '_done', false); def(this, '_wait', null); const self = this; const c = { enqueue(x) { self._q.push(x); self._wake(); }, close() { self._done = true; self._wake(); }, error(e) { self._err = e; self._wake(); }, desiredSize: 1 }; if (src.start) Promise.resolve().then(() => src.start(c)); def(this, '_c', c); def(this, '_pull', src.pull); }
    _wake() { const w = this._wait; if (w) { this._wait = null; w(); } }
    getReader() { const s = this; return { read() { return new Promise((res, rej) => { const go = () => { if (s._err) rej(s._err); else if (s._q.length) res({ value: s._q.shift(), done: false }); else if (s._done) res({ value: undefined, done: true }); else { s._wait = go; if (s._pull) s._pull(s._c); } }; go(); }); }, releaseLock() {}, cancel() { s._done = true; return Promise.resolve(); }, closed: new Promise(() => {}) }; }
    get locked() { return false; } cancel() { this._done = true; return Promise.resolve(); }
    async *[Symbol.asyncIterator]() { const r = this.getReader(); for (;;) { const { value, done } = await r.read(); if (done) return; yield value; } }
}
const objectURLs = new Map(); let objectURLSeq = 0;
class URLSearchParams {
    constructor(init = '') {
        def(this, '_l', []); def(this, '_u', null);
        if (init instanceof URLSearchParams) this._l = init._l.map(x => x.slice());
        else if (typeof init === 'object' && init) { if (Symbol.iterator in init) for (const [k, v] of init) this._l.push([String(k), String(v)]); else for (const k of Object.keys(init)) this._l.push([k, String(init[k])]); }
        else { init = String(init); if (init[0] === '?') init = init.slice(1); for (const p of init.split('&')) { if (!p) continue; const i = p.indexOf('='); const dec = (s) => { try { return decodeURIComponent(s.replace(/\+/g, ' ')); } catch (e) { return s; } }; this._l.push(i < 0 ? [dec(p), ''] : [dec(p.slice(0, i)), dec(p.slice(i + 1))]); } }
    }
    _upd() { if (this._u) { const s = this.toString(); this._u._set('search', s ? '?' + s : ''); } }
    get size() { return this._l.length; }
    append(k, v) { this._l.push([String(k), String(v)]); this._upd(); }
    delete(k, v) { k = String(k); this._l = this._l.filter(([a, b]) => a !== k || (v !== undefined && b !== String(v))); this._upd(); }
    get(k) { const e = this._l.find(([a]) => a === String(k)); return e ? e[1] : null; }
    getAll(k) { return this._l.filter(([a]) => a === String(k)).map(e => e[1]); }
    has(k, v) { return this._l.some(([a, b]) => a === String(k) && (v === undefined || b === String(v))); }
    set(k, v) { k = String(k); const i = this._l.findIndex(([a]) => a === k); if (i < 0) this._l.push([k, String(v)]); else { this._l[i][1] = String(v); this._l = this._l.filter(([a], j) => a !== k || j === i); } this._upd(); }
    sort() { this._l.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); this._upd(); }
    forEach(fn, self) { for (const [k, v] of this._l) fn.call(self, v, k, this); }
    keys() { return this._l.map(e => e[0])[Symbol.iterator](); } values() { return this._l.map(e => e[1])[Symbol.iterator](); }
    entries() { return this._l.map(e => e.slice())[Symbol.iterator](); } [Symbol.iterator]() { return this.entries(); }
    toString() { const enc = (s) => encodeURIComponent(s).replace(/%20/g, '+').replace(/[!'()~]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase()); return this._l.map(([k, v]) => enc(k) + '=' + enc(v)).join('&'); }
}
const URL_KEYS = ['href', 'protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin'];
class URL {
    constructor(u, base) { const r = N.urlParse(String(u), base === undefined ? null : String(base)); if (!r) throw new TypeError(`Failed to construct 'URL': Invalid URL`); def(this, '_p', r); def(this, '_sp', null); }
    _set(k, v) {
        const p = this._p, i = URL_KEYS.indexOf(k);
        if (k === 'href') { const r = N.urlParse(String(v), null); if (!r) throw new TypeError('Invalid URL'); this._p = r; }
        else {
            v = String(v);
            if (k === 'search' && v && v[0] !== '?') v = '?' + v; if (k === 'hash' && v && v[0] !== '#') v = '#' + v; if (k === 'protocol' && !v.endsWith(':')) v += ':';
            if (k === 'pathname' && v[0] !== '/') v = '/' + v;
            p[i] = v; if (k === 'hostname' || k === 'port') p[4] = p[5] + (p[6] ? ':' + p[6] : '');
            const auth = p[2] ? p[2] + (p[3] ? ':' + p[3] : '') + '@' : '';
            const r = N.urlParse(p[1] + '//' + auth + p[4] + p[7] + p[8] + p[9], null); if (r) this._p = r;
        }
        if (this._sp && k !== 'search') { this._sp._l = new URLSearchParams(this._p[8])._l; }
    }
    get searchParams() { if (!this._sp) { const sp = new URLSearchParams(this._p[8]); sp._u = this; def(this, '_sp', sp); } return this._sp; }
    toString() { return this._p[0]; } toJSON() { return this._p[0]; }
    static canParse(u, b) { return !!N.urlParse(String(u), b === undefined ? null : String(b)); }
    static parse(u, b) { try { return new URL(u, b); } catch (e) { return null; } }
    static createObjectURL(b) { const u = 'blob:' + location.origin + '/' + (++objectURLSeq).toString(16).padStart(8, '0') + '-lumen'; objectURLs.set(u, b); return u; }
    static revokeObjectURL(u) { objectURLs.delete(u); }
}
URL_KEYS.forEach((k, i) => acc(URL.prototype, k, function () { return this._p[i]; }, k === 'origin' ? undefined : function (v) { this._set(k, v); }));
