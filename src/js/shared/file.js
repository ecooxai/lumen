/* File API (w3c.github.io/FileAPI): Blob, File, FileReader and FileReaderSync, shared by the page and worker preludes.
   A Blob keeps its bytes in a non-enumerable _buf ArrayBuffer, which the preludes' own code reads directly. The
   including prelude defines FILE_ENCODE(string), the UTF-8 bytes of a string (an ArrayBuffer or a Uint8Array). */
const fileErr = (where, msg) => new TypeError(`Failed to ${where}: ${msg}`);
const fileType = t => /[^\x20-\x7e]/.test(t) ? '' : t.toLowerCase();
/* WebIDL [Clamp] long long */
const fileClamp = v => { let x = Number(v); if (Number.isNaN(x)) return 0; x = Math.min(Math.max(x, -(2 ** 63)), 2 ** 63); const f = Math.floor(x); return x - f !== 0.5 ? Math.round(x) : f % 2 === 0 ? f : f + 1; };
const fileBlobOf = b => { if (!(b instanceof Blob) || !Object.hasOwn(b, '_buf')) throw new TypeError('Illegal invocation'); return b; };
/* BlobPropertyBag / FilePropertyBag members, read in WebIDL (alphabetical) order */
function fileOptions(o, where, file) {
    const r = { endings: 'transparent', type: '', lastModified: undefined };
    if (o === undefined || o === null) return r;
    if (typeof o !== 'object' && typeof o !== 'function') throw fileErr(where, "The provided value is not of type 'BlobPropertyBag'.");
    const e = o.endings;
    if (e !== undefined) { r.endings = String(e); if (r.endings !== 'transparent' && r.endings !== 'native') throw fileErr(where, `The provided value '${r.endings}' is not a valid enum value of type EndingType.`); }
    if (file) { const lm = o.lastModified; if (lm !== undefined) { const x = Number(lm); r.lastModified = Number.isFinite(x) ? Math.trunc(x) : 0; } }
    const t = o.type;
    if (t !== undefined) r.type = fileType(String(t));
    return r;
}
/* the bytes of a sequence<BlobPart> */
function fileParts(parts, endings, where) {
    if (parts === undefined) return new ArrayBuffer(0);
    if (parts === null || (typeof parts !== 'object' && typeof parts !== 'function') || typeof parts[Symbol.iterator] !== 'function')
        throw fileErr(where, "The provided value cannot be converted to a sequence.");
    const bufs = [], text = [];
    const flush = () => { if (text.length) { bufs.push(new Uint8Array(FILE_ENCODE(text.join('')))); text.length = 0; } };
    for (const p of parts) {
        if (p instanceof ArrayBuffer) { flush(); bufs.push(new Uint8Array(p.slice(0))); }
        else if (ArrayBuffer.isView(p)) { flush(); bufs.push(new Uint8Array(p.buffer, p.byteOffset, p.byteLength).slice()); }
        else if (p instanceof Blob && Object.hasOwn(p, '_buf')) { flush(); bufs.push(new Uint8Array(p._buf)); }
        else { let s = String(p).toWellFormed(); if (endings === 'native') s = s.replace(/\r\n|\r/g, '\n'); text.push(s); }   /* one USVString per part */
    }
    flush();
    let n = 0;
    for (const b of bufs) n += b.byteLength;
    const u = new Uint8Array(n);
    let o = 0;
    for (const b of bufs) { u.set(b, o); o += b.byteLength; }
    return u.buffer;
}
/* Blob and File cross to workers and frames as host objects carrying their bytes, type (and name, date) */
const fileRec = b => { if (FILEN.cloneRec) FILEN.cloneRec(b, Object.hasOwn(b, '_name') ? [b._buf, b._type, b._name, b._lm] : [b._buf, b._type]); };
class Blob {
    constructor(blobParts = undefined, options = undefined) {
        const W = "construct 'Blob'", opt = fileOptions(options, W, false);
        def(this, '_buf', fileParts(blobParts, opt.endings, W));
        def(this, '_type', opt.type);
        fileRec(this);
    }
    get size() { return fileBlobOf(this)._buf.byteLength; }
    get type() { return fileBlobOf(this)._type; }
    slice(start = undefined, end = undefined, contentType = undefined) {
        const n = fileBlobOf(this)._buf.byteLength;
        const rel = (v, d) => { if (v === undefined) return d; const x = fileClamp(v); return x < 0 ? Math.max(n + x, 0) : Math.min(x, n); };
        const a = rel(start, 0), b = rel(end, n);
        const r = Object.create(Blob.prototype);
        def(r, '_buf', this._buf.slice(a, Math.max(a, b)));
        def(r, '_type', contentType === undefined ? '' : fileType(String(contentType)));
        fileRec(r);
        return r;
    }
    stream() {
        const b = fileBlobOf(this)._buf;
        return new ReadableStream({ __proto__: null, type: 'bytes', start(c) { if (b.byteLength) c.enqueue(new Uint8Array(b.slice(0))); c.close(); } });
    }
    text() { try { fileBlobOf(this); } catch (e) { return Promise.reject(e); } return Promise.resolve(new TextDecoder().decode(this._buf)); }
    arrayBuffer() { try { fileBlobOf(this); } catch (e) { return Promise.reject(e); } return Promise.resolve(this._buf.slice(0)); }
    bytes() { try { fileBlobOf(this); } catch (e) { return Promise.reject(e); } return Promise.resolve(new Uint8Array(this._buf.slice(0))); }
    textStream() { return this.stream().pipeThrough(new TextDecoderStream()); }
}
Object.defineProperty(Blob.prototype, Symbol.toStringTag, { value: 'Blob', configurable: true });
if (FILEN.cloneHost) FILEN.cloneHost(Blob.prototype, (o, r) => { def(o, '_buf', r[0]); def(o, '_type', r[1]); fileRec(o); }, 2);
class File extends Blob {
    constructor(fileBits, fileName, options = undefined) {
        const W = "construct 'File'";
        if (arguments.length < 2) throw fileErr(W, `2 arguments required, but only ${arguments.length} present.`);
        const name = String(fileName).toWellFormed(), opt = fileOptions(options, W, true);
        super(undefined);
        def(this, '_buf', fileParts(fileBits, opt.endings, W));
        def(this, '_type', opt.type);
        def(this, '_name', name);
        def(this, '_lm', opt.lastModified === undefined ? Date.now() : opt.lastModified);
        fileRec(this);
    }
    get name() { if (!Object.hasOwn(this, '_name')) throw new TypeError('Illegal invocation'); return this._name; }
    get lastModified() { if (!Object.hasOwn(this, '_lm')) throw new TypeError('Illegal invocation'); return this._lm; }
    get webkitRelativePath() { if (!Object.hasOwn(this, '_name')) throw new TypeError('Illegal invocation'); return ''; }
}
if (FILEN.cloneHost) FILEN.cloneHost(File.prototype, (o, r) => { def(o, '_buf', r[0]); def(o, '_type', r[1]); def(o, '_name', r[2]); def(o, '_lm', r[3]); fileRec(o); }, 3);
Object.defineProperty(File.prototype, Symbol.toStringTag, { value: 'File', configurable: true });
/* FileReader's package data step */
function fileRead(blob, kind, label) {
    const u = new Uint8Array(blob._buf);
    if (kind === 'ArrayBuffer') return blob._buf.slice(0);
    if (kind === 'BinaryString') { let s = ''; for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768)); return s; }
    if (kind === 'DataURL') { let s = ''; for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768)); return 'data:' + (blob.type || 'application/octet-stream') + ';base64,' + btoa(s); }
    /* Text: the given label, else the type's charset, else UTF-8; a byte order mark wins */
    let enc = 'utf-8';
    const tryLabel = l => { try { return new TextDecoder(l).encoding; } catch (e) { return null; } };
    const fromType = () => { const m = /;\s*charset\s*=\s*("?)([^";]+)\1/i.exec(blob.type); return m ? tryLabel(m[2].trim()) : null; };
    enc = (label !== undefined && tryLabel(String(label))) || fromType() || 'utf-8';
    if (u[0] === 0xef && u[1] === 0xbb && u[2] === 0xbf) enc = 'utf-8';
    else if (u[0] === 0xfe && u[1] === 0xff) enc = 'utf-16be';
    else if (u[0] === 0xff && u[1] === 0xfe) enc = 'utf-16le';
    return new TextDecoder(enc).decode(u);
}
const FR_EVENTS = ['loadstart', 'progress', 'load', 'abort', 'error', 'loadend'];
const frState = new WeakMap();
const frOf = r => { const s = frState.get(r); if (!s) throw new TypeError('Illegal invocation'); return s; };
class FileReader extends EventTarget {
    constructor() {
        super();
        const s = { state: 0, result: null, error: null, gen: 0, handlers: {} };
        frState.set(this, s);
    }
    get readyState() { return frOf(this).state; }
    get result() { return frOf(this).result; }
    get error() { return frOf(this).error; }
    #read(n, blob, kind, label, op) {
        const s = frOf(this), W = `execute '${op}' on 'FileReader'`;
        if (n < 1 || !(blob instanceof Blob)) throw fileErr(W, "parameter 1 is not of type 'Blob'.");
        if (s.state === 1) throw new DOMException(`Failed to ${W}: The object is already busy reading Blobs.`, 'InvalidStateError');
        s.state = 1; s.result = null; s.error = null;
        const gen = ++s.gen, total = blob.size;
        const fire = (t, n) => this.dispatchEvent(new ProgressEvent(t, { lengthComputable: true, loaded: n, total }));
        setTimeout(() => {
            if (s.gen !== gen) return;
            fire('loadstart', 0);
            setTimeout(() => {
                if (s.gen !== gen) return;
                if (total) fire('progress', total);
                if (s.gen !== gen) return;
                s.state = 2;
                try { s.result = fileRead(blob, kind, label); } catch (e) { s.error = new DOMException('The file could not be read.', 'NotReadableError'); fire('error', 0); if (s.state !== 1) fire('loadend', 0); return; }
                fire('load', total);
                if (s.state !== 1) fire('loadend', total);
            });
        });
    }
    readAsArrayBuffer(blob) { this.#read(arguments.length, blob, 'ArrayBuffer', undefined, 'readAsArrayBuffer'); }
    readAsBinaryString(blob) { this.#read(arguments.length, blob, 'BinaryString', undefined, 'readAsBinaryString'); }
    readAsText(blob, encoding = undefined) { this.#read(arguments.length, blob, 'Text', encoding, 'readAsText'); }
    readAsDataURL(blob) { this.#read(arguments.length, blob, 'DataURL', undefined, 'readAsDataURL'); }
    abort() {
        const s = frOf(this);
        if (s.state === 0 || s.state === 2) { s.result = null; return; }
        s.state = 2; s.result = null; s.gen++;
        this.dispatchEvent(new ProgressEvent('abort'));
        if (s.state !== 1) this.dispatchEvent(new ProgressEvent('loadend'));
    }
}
for (const t of FR_EVENTS) Object.defineProperty(FileReader.prototype, 'on' + t, {
    get() { return frOf(this).handlers[t] ?? null; },
    set(v) { frOf(this).handlers[t] = typeof v === 'function' || (v !== null && typeof v === 'object') ? v : null; },
    enumerable: true, configurable: true,
});
for (const [i, k] of ['EMPTY', 'LOADING', 'DONE'].entries()) { Object.defineProperty(FileReader, k, { value: i, enumerable: true }); Object.defineProperty(FileReader.prototype, k, { value: i, enumerable: true }); }
Object.defineProperty(FileReader.prototype, Symbol.toStringTag, { value: 'FileReader', configurable: true });
/* workers only: the same reads, synchronously */
class FileReaderSync {
    readAsArrayBuffer(blob) { return fileSyncRead(blob, 'ArrayBuffer', undefined, 'readAsArrayBuffer', arguments.length); }
    readAsBinaryString(blob) { return fileSyncRead(blob, 'BinaryString', undefined, 'readAsBinaryString', arguments.length); }
    readAsText(blob, encoding = undefined) { return fileSyncRead(blob, 'Text', encoding, 'readAsText', arguments.length); }
    readAsDataURL(blob) { return fileSyncRead(blob, 'DataURL', undefined, 'readAsDataURL', arguments.length); }
}
function fileSyncRead(blob, kind, label, op, n) {
    if (n < 1 || !(blob instanceof Blob)) throw fileErr(`execute '${op}' on 'FileReaderSync'`, "parameter 1 is not of type 'Blob'.");
    return fileRead(blob, kind, label);
}
Object.defineProperty(FileReaderSync.prototype, Symbol.toStringTag, { value: 'FileReaderSync', configurable: true });
