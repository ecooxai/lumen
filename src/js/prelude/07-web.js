const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) { s = String(s); let r = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); if (a > 255 || b > 255 || c > 255) throw new DOMException('The string to be encoded contains characters outside of the Latin1 range.', 'InvalidCharacterError'); const n = (a << 16) | ((b || 0) << 8) | (c || 0); r += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < s.length ? B64[n >> 6 & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '='); } return r; }
function atob(s) { s = String(s).replace(/[\t\n\f\r ]/g, ''); if (s.length % 4 === 0) s = s.replace(/==?$/, ''); if (s.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(s)) throw new DOMException('The string to be decoded is not correctly encoded.', 'InvalidCharacterError'); let r = '', bits = 0, n = 0; for (const ch of s) { n = (n << 6) | B64.indexOf(ch); bits += 6; if (bits >= 8) { bits -= 8; r += String.fromCharCode((n >> bits) & 255); } } return r; }
const toBytes = (p) => typeof p === 'string' ? N.encode(p) : p instanceof ArrayBuffer ? p : ArrayBuffer.isView(p) ? p.buffer.slice(p.byteOffset, p.byteOffset + p.byteLength) : p instanceof Blob ? p._buf : N.encode(String(p));
const blobPartString = Function.prototype.call.bind(String.prototype.toWellFormed);
class Blob {
    constructor(parts = [], o = {}) {
        if (typeof parts === 'string') throw new TypeError('Blob parts must be a sequence');
        const bufs = [], text = [];
        const flush = () => { if (text.length) { bufs.push(N.encode(text.join(''))); text.length = 0; } };
        // Batch adjacent text to avoid an ArrayBuffer/native call per character.
        // USVString conversion is per part: separate lone surrogates must not pair.
        for (const p of parts) {
            if (typeof p === 'string') text.push(blobPartString(p));
            else { flush(); bufs.push(toBytes(p)); }
        }
        flush();
        const len = bufs.reduce((a, b) => a + b.byteLength, 0), u = new Uint8Array(len);
        let off = 0;
        for (const b of bufs) { u.set(new Uint8Array(b), off); off += b.byteLength; }
        def(this, '_buf', u.buffer); this.type = String(o.type || '').toLowerCase();
    }
    get size() { return this._buf.byteLength; }
    slice(a = 0, b = this.size, t = '') { return new Blob([this._buf.slice(a, b)], { type: t }); }
    arrayBuffer() { return Promise.resolve(this._buf.slice(0)); } text() { return Promise.resolve(N.decode(this._buf)); } bytes() { return Promise.resolve(new Uint8Array(this._buf.slice(0))); }
    stream() { const b = this._buf; return new ReadableStream({ start(c) { c.enqueue(new Uint8Array(b)); c.close(); } }); }
}
class File extends Blob { constructor(p, name, o = {}) { super(p, o); this.name = String(name); this.lastModified = o.lastModified || Date.now(); } }
class FileList {
    constructor(files = []) { files.forEach((f, i) => { this[i] = f; }); def(this, 'length', files.length); }
    item(i) { return this[i] ?? null; }
    *[Symbol.iterator]() { for (let i = 0; i < this.length; i++) yield this[i]; }
}
class DataTransferItem {
    constructor(kind, type, v) { this.kind = kind; this.type = type; def(this, '_v', v); }
    getAsFile() { return this.kind === 'file' ? this._v : null; }
    getAsString(cb) { if (this.kind === 'string' && typeof cb === 'function') setTimeout(() => cb(this._v), 0); }
    webkitGetAsEntry() { return null; }
}
class DataTransferItemList extends Array {
    static get [Symbol.species]() { return Array; }
    add(d, type) { const it = d instanceof File ? new DataTransferItem('file', d.type, d) : new DataTransferItem('string', String(type).toLowerCase(), String(d)); this.push(it); return it; }
    remove(i) { this.splice(i, 1); } clear() { this.length = 0; }
}
class DataTransfer {
    constructor() { def(this, '_items', new DataTransferItemList()); this.dropEffect = 'none'; this.effectAllowed = 'all'; }
    get items() { return this._items; }
    get files() { return new FileList(this._items.filter(i => i.kind === 'file').map(i => i._v)); }
    get types() { const t = this._items.filter(i => i.kind === 'string').map(i => i.type); if (this._items.some(i => i.kind === 'file')) t.push('Files'); return t; }
    getData(t) { t = String(t).toLowerCase(); if (t === 'text') t = 'text/plain'; const i = this._items.find(x => x.kind === 'string' && x.type === t); return i ? i._v : ''; }
    setData(t, v) { t = String(t).toLowerCase(); if (t === 'text') t = 'text/plain'; this.clearData(t); this._items.add(String(v), t); }
    clearData(t) { for (let i = this._items.length - 1; i >= 0; i--) { const x = this._items[i]; if (x.kind === 'string' && (t == null || x.type === String(t).toLowerCase())) this._items.splice(i, 1); } }
    setDragImage() {}
}
class FileReader extends EventTarget {
    constructor() { super(); this.result = null; this.readyState = 0; this.error = null; }
    _read(b, f) { this.readyState = 1; setTimeout(() => { this.result = f(b); this.readyState = 2; for (const t of ['load', 'loadend']) { const ev = new ProgressEvent(t); if (this['on' + t]) this['on' + t](ev); dispatch(this, ev); } }); }
    readAsText(b) { this._read(b, x => N.decode(x._buf)); } readAsArrayBuffer(b) { this._read(b, x => x._buf.slice(0)); }
    readAsDataURL(b) { this._read(b, x => 'data:' + (x.type || 'application/octet-stream') + ';base64,' + btoa(((u, s) => { for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768)); return s; })(new Uint8Array(x._buf), ''))); }
    abort() {}
}
/*@shared streams.js*/
const ENCN = N;
/*@shared encoding.js*/
const ZN = N;
/*@shared compression.js*/
const objectURLs = new Map(); let objectURLSeq = 0;
const URLN = N;
/*@shared url.js*/
/*@shared urlpattern.js*/
const URL_KEYS = ['href', 'protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin'];
Object.defineProperties(URL, {
    createObjectURL: { value: function createObjectURL(b) { const u = 'blob:' + location.origin + '/' + (++objectURLSeq).toString(16).padStart(8, '0') + '-lumen'; objectURLs.set(u, b); return u; }, writable: true, enumerable: true, configurable: true },
    revokeObjectURL: { value: function revokeObjectURL(u) { objectURLs.delete(String(u)); }, writable: true, enumerable: true, configurable: true },
});
