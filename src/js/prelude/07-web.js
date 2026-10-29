const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) { s = String(s); let r = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); if (a > 255 || b > 255 || c > 255) throw new DOMException('The string to be encoded contains characters outside of the Latin1 range.', 'InvalidCharacterError'); const n = (a << 16) | ((b || 0) << 8) | (c || 0); r += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < s.length ? B64[n >> 6 & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '='); } return r; }
function atob(s) { s = String(s).replace(/[\t\n\f\r ]/g, ''); if (s.length % 4 === 0) s = s.replace(/==?$/, ''); if (s.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(s)) throw new DOMException('The string to be decoded is not correctly encoded.', 'InvalidCharacterError'); let r = '', bits = 0, n = 0; for (const ch of s) { n = (n << 6) | B64.indexOf(ch); bits += 6; if (bits >= 8) { bits -= 8; r += String.fromCharCode((n >> bits) & 255); } } return r; }
const toBytes = (p) => typeof p === 'string' ? N.encode(p) : p instanceof ArrayBuffer ? p : ArrayBuffer.isView(p) ? p.buffer.slice(p.byteOffset, p.byteOffset + p.byteLength) : p instanceof Blob ? p._buf : N.encode(String(p));
const FILE_ENCODE = s => N.encode(s), FILEN = N;
/*@shared file.js*/
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
/*@shared streams.js*/
const ENCN = N;
/*@shared encoding.js*/
const ZN = N;
/*@shared compression.js*/
const objectURLs = new SMap();
/* a blob: URL's path ends with a random UUID (version 4) */
const blobUUID = () => { const h = [...Array(32)].map(() => (Math.random() * 16 | 0).toString(16)); h[12] = '4'; h[16] = '89ab'[Math.random() * 4 | 0]; const s = h.join(''); return `${s.slice(0, 8)}-${s.slice(8, 12)}-${s.slice(12, 16)}-${s.slice(16, 20)}-${s.slice(20)}`; };
const URLN = N;
/*@shared url.js*/
/*@shared urlpattern.js*/
const URL_KEYS = ['href', 'protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin'];
Object.defineProperties(URL, {
    createObjectURL: { value: function createObjectURL(b) { const u = 'blob:' + location.origin + '/' + blobUUID(); objectURLs.set(u, b); return u; }, writable: true, enumerable: true, configurable: true },
    revokeObjectURL: { value: function revokeObjectURL(u) { objectURLs.delete(String(u)); }, writable: true, enumerable: true, configurable: true },
});
