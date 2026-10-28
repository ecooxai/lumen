const { TextEncoder, TextDecoder } = createTextCodecInterfaces({
    get: (kind, object) => N.domStateGet(object, kind),
    put: (kind, object, state) => N.domStateSet(object, kind, state),
}, { label: N.textEncoding, decode: N.textDecode });
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) { s = String(s); let r = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); if (a > 255 || b > 255 || c > 255) throw new DOMException('The string to be encoded contains characters outside of the Latin1 range.', 'InvalidCharacterError'); const n = (a << 16) | ((b || 0) << 8) | (c || 0); r += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < s.length ? B64[n >> 6 & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '='); } return r; }
function atob(s) { s = String(s).replace(/[\t\n\f\r ]/g, ''); if (s.length % 4 === 0) s = s.replace(/==?$/, ''); if (s.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(s)) throw new DOMException('The string to be decoded is not correctly encoded.', 'InvalidCharacterError'); let r = '', bits = 0, n = 0; for (const ch of s) { n = (n << 6) | B64.indexOf(ch); bits += 6; if (bits >= 8) { bits -= 8; r += String.fromCharCode((n >> bits) & 255); } } return r; }
const blobFileOps = createBlobFileInterfaces(nativeIDL, {
    blobState: N.blobState, encode: N.encode, decode: N.decode,
    stream: bytes => new ReadableStream({start(controller) { controller.enqueue(bytes); controller.close(); }}),
});
const {Blob, File} = blobFileOps;
const toBytes = (p) => typeof p === 'string' ? N.encode(p) : blobFileOps.isBlob(p) ? blobFileOps.copyBuffer(p) : p instanceof ArrayBuffer ? p : ArrayBuffer.isView(p) ? p.buffer.slice(p.byteOffset, p.byteOffset + p.byteLength) : N.encode(String(p));
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
    add(d, type) { const it = blobFileOps.isFile(d) ? new DataTransferItem('file', blobFileOps.type(d), d) : new DataTransferItem('string', String(type).toLowerCase(), String(d)); this.push(it); return it; }
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
const { FileReader } = createFileReaderInterfaces(nativeIDL, {
    schedule: callback => N.timer(callback, 0, false),
    cancel: N.clearTimer,
    blobState: N.blobState,
    package: N.fileReaderPackage,
    fire(reader, name, loaded, total, queued) {
        const event = new ProgressEvent(name, { lengthComputable: total > 0, loaded, total });
        dispatch(reader, event, true, queued ? N.fileReaderCheckpoint : undefined, true);
    },
}, false);
const { ReadableStream, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultWriter,
    TransformStream, ByteLengthQueuingStrategy, CountQueuingStrategy, TextEncoderStream, TextDecoderStream
} = createWebStreamInterfaces(nativeIDL, { TextEncoder, TextDecoder, AbortController, DOMException });
const objectURLs = new Map(); let objectURLSeq = 0;
if (typeof WebAssembly === 'object' && !WebAssembly.compileStreaming) {
    const wasmBody = async (src, fn) => {
        const r = await src;
        const err = m => new TypeError('WebAssembly.' + fn + '(): ' + m);
        if (!(r instanceof Response)) throw err('Argument 0 must be provided and must be a Response');
        if (/^https?:/.test(r.url || '') && !/^application\/wasm\s*(;|$)/i.test(r.headers.get('content-type') || ''))
            throw err("Incorrect response MIME type. Expected 'application/wasm'.");
        if (!r.ok) throw err('HTTP status code is not ok');
        if (r.bodyUsed) throw err('Response body has already been used');
        return r.arrayBuffer();
    };
    Object.defineProperties(WebAssembly, {
        compileStreaming: { value: async function compileStreaming(src) { return WebAssembly.compile(await wasmBody(src, 'compileStreaming')); }, writable: true, configurable: true },
        instantiateStreaming: { value: async function instantiateStreaming(src, imports) { return WebAssembly.instantiate(await wasmBody(src, 'instantiateStreaming'), imports); }, writable: true, configurable: true },
    });
}
const urlSearchParamsOps = createURLSearchParams(nativeIDL);
const { URLSearchParams } = urlSearchParamsOps;
const { URL, keys: URL_KEYS } = createURLInterface(N.urlParse, N.urlSet, urlSearchParamsOps, {
    create(b) {
        const isBlob = blobFileOps.isBlob(b);
        if (!isBlob && !(typeof MediaSource !== 'undefined' && b instanceof MediaSource)) throw new TypeError('Object URL requires a Blob or MediaSource');
        const u = 'blob:' + location.origin + '/' + (++objectURLSeq).toString(16).padStart(8, '0') + '-lumen';
        objectURLs.set(u, b);
        if (isBlob) {
            const type = blobFileOps.type(b);
            if (blobFileOps.size(b) < 8e6 && (!type || /javascript|ecmascript/i.test(type)))
                N.blobMod(u, N.decode(blobFileOps.copyBuffer(b)));
        }
        return u;
    },
    revoke(u) {
        const b = objectURLs.get(u);
        if (blobFileOps.isBlob(b)) {
            const path = blobFileOps.mediaFile(b);
            if (path) { N.blobFileFree(path); blobFileOps.mediaFile(b, null); }
        }
        if (objectURLs.delete(u)) N.blobMod(u);
    },
}, nativeIDL);
