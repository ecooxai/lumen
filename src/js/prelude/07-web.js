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
const streamCtl = (s) => ({
    enqueue(x) { if (s._done || s._err) throw new TypeError('Cannot enqueue a chunk into a closed stream'); s._q.push(x); s._wake(); },
    close() { s._done = true; s._wake(); }, error(e) { if (!s._done) { s._err = e === undefined ? new TypeError('errored') : e; s._wake(); } },
    get desiredSize() { return s._err ? null : s._done ? 0 : s._hwm - s._q.length; },
});
class ReadableStreamDefaultReader {
    constructor(s) { if (s._reader) throw new TypeError('ReadableStream is locked'); def(this, '_s', s); s._reader = this; let r, j; def(this, '_closed', new Promise((a, b) => { r = a; j = b; })); this._closed.catch(() => {}); def(this, '_cr', [r, j]); s._settle(); }
    get closed() { return this._closed; }
    read() {
        const s = this._s; if (!s || s._reader !== this) return Promise.reject(new TypeError('Reader released'));
        return new Promise((res, rej) => { s._waits.push([res, rej]); s._flush(); });
    }
    releaseLock() { const s = this._s; if (s && s._reader === this) { s._reader = null; for (const [, j] of s._waits.splice(0)) j(new TypeError('Reader released')); } }
    cancel(r) { return this._s ? this._s._cancel(r) : Promise.resolve(); }
}
class ReadableStream {
    constructor(src = {}, strategy = {}) {
        src = src || {};
        def(this, '_q', []); def(this, '_done', false); def(this, '_err', undefined); def(this, '_waits', []); def(this, '_reader', null); def(this, '_pulling', false); def(this, '_started', false);
        def(this, '_hwm', strategy && strategy.highWaterMark != null ? strategy.highWaterMark : 1); def(this, '_src', src);
        const c = streamCtl(this); def(this, '_c', c);
        let r; try { r = src.start ? src.start.call(src, c) : undefined; } catch (e) { this._err = e; }
        Promise.resolve(r).then(() => { this._started = true; this._flush(); }, e => c.error(e));
    }
    _wake() { this._flush(); }
    _settle() { const rd = this._reader; if (!rd) return; if (this._err !== undefined && !this._q.length) rd._cr[1](this._err); else if (this._done && !this._q.length) rd._cr[0](undefined); }
    _flush() {
        while (this._waits.length && this._q.length) this._waits.shift()[0]({ value: this._q.shift(), done: false });
        if (this._waits.length && this._err !== undefined) for (const [, j] of this._waits.splice(0)) j(this._err);
        else if (this._waits.length && this._done) for (const [r] of this._waits.splice(0)) r({ value: undefined, done: true });
        this._settle();
        if (this._started && !this._pulling && !this._done && this._err === undefined && this._src.pull && (this._waits.length || this._q.length < this._hwm)) {
            this._pulling = true;
            let p; try { p = this._src.pull.call(this._src, this._c); } catch (e) { p = Promise.reject(e); }
            Promise.resolve(p).then(() => { this._pulling = false; if (this._waits.length) this._flush(); }, e => { this._pulling = false; this._c.error(e); });
        }
    }
    _cancel(r) { if (this._done && !this._q.length) return Promise.resolve(); this._q.length = 0; this._done = true; this._flush(); try { return Promise.resolve(this._src.cancel ? this._src.cancel.call(this._src, r) : undefined).then(() => {}); } catch (e) { return Promise.reject(e); } }
    get locked() { return !!this._reader; }
    getReader(o) { if (o && o.mode === 'byob') throw new TypeError('BYOB readers are not supported'); return new ReadableStreamDefaultReader(this); }
    cancel(r) { if (this._reader) return Promise.reject(new TypeError('ReadableStream is locked')); return this._cancel(r); }
    pipeTo(dest, o = {}) {
        if (this.locked) return Promise.reject(new TypeError('ReadableStream is locked'));
        if (!dest || typeof dest.getWriter !== 'function') return Promise.reject(new TypeError('pipeTo destination must be a WritableStream'));
        const rd = this.getReader(), w = dest.getWriter(), sig = o.signal;
        return new Promise((res, rej) => {
            let stop = false;
            const fin = (err, isErr) => { if (stop) return; stop = true; rd.releaseLock(); w.releaseLock(); isErr ? rej(err) : res(); };
            if (sig) { const onAbort = () => { const e = sig.reason !== undefined ? sig.reason : new DOMException('The operation was aborted.', 'AbortError'); if (!o.preventAbort) w.abort(e).catch(() => {}); if (!o.preventCancel) this._cancel(e); fin(e, true); }; if (sig.aborted) return onAbort(); sig.addEventListener('abort', onAbort, { once: true }); }
            const step = () => {
                if (stop) return;
                rd.read().then(({ value, done }) => {
                    if (stop) return;
                    if (done) { if (o.preventClose) fin(); else w.close().then(() => fin(), e => fin(e, true)); return; }
                    w.write(value).then(step, e => { if (!o.preventCancel) this._cancel(e); fin(e, true); });
                }, e => { if (!o.preventAbort) w.abort(e).catch(() => {}); fin(e, true); });
            };
            step();
        });
    }
    pipeThrough(t, o) { if (!t || !t.writable || !t.readable) throw new TypeError('pipeThrough requires {writable, readable}'); this.pipeTo(t.writable, o).catch(() => {}); return t.readable; }
    tee() {
        const rd = this.getReader(); let c1, c2, n = 2; const cancel = () => { if (--n === 0) rd.cancel(); };
        const pull = () => rd.read().then(({ value, done }) => { if (done) { try { c1.close(); } catch (e) {} try { c2.close(); } catch (e) {} } else { try { c1.enqueue(value); } catch (e) {} try { c2.enqueue(value); } catch (e) {} } }, e => { c1.error(e); c2.error(e); });
        return [new ReadableStream({ start(c) { c1 = c; }, pull, cancel }), new ReadableStream({ start(c) { c2 = c; }, pull, cancel })];
    }
    values(o) { const r = this.getReader(), pc = o && o.preventCancel; return { next() { return r.read().then(x => { if (x.done) r.releaseLock(); return x; }); }, return(v) { if (!pc) r.cancel(v); r.releaseLock(); return Promise.resolve({ value: v, done: true }); }, [Symbol.asyncIterator]() { return this; } }; }
    [Symbol.asyncIterator](o) { return this.values(o); }
    static from(it) {
        if (it instanceof ReadableStream) return it;
        const iter = it[Symbol.asyncIterator] ? it[Symbol.asyncIterator]() : it[Symbol.iterator]();
        return new ReadableStream({ pull(c) { return Promise.resolve(iter.next()).then(({ value, done }) => done ? c.close() : Promise.resolve(value).then(v => c.enqueue(v))); }, cancel(r) { if (iter.return) return iter.return(r); } }, { highWaterMark: 0 });
    }
}
function writableDeferred(handled = false) {
    const d = { state: 'pending' };
    d.promise = new Promise((resolve, reject) => {
        d.resolve = value => { if (d.state === 'pending') { d.state = 'fulfilled'; resolve(value); } };
        d.reject = reason => { if (d.state === 'pending') { d.state = 'rejected'; reject(reason); } };
    });
    if (handled) d.promise.catch(() => {});
    return d;
}
function writableRejectSlot(writer, key, reason) {
    if (writer[key].state !== 'pending') writer[key] = writableDeferred(true);
    writer[key].reject(reason);
}
class WritableStreamDefaultWriter {
    constructor(s) {
        if (!(s instanceof WritableStream)) throw new TypeError('Expected a WritableStream');
        if (s._writer) throw new TypeError('WritableStream is locked');
        def(this, '_s', s); def(this, '_ready', writableDeferred(true)); def(this, '_closed', writableDeferred(true));
        s._writer = this;
        if (s._state === 'writable') { if (s._closing || !s._backpressure) this._ready.resolve(); }
        else if (s._state === 'closed') { this._ready.resolve(); this._closed.resolve(); }
        else { this._ready.reject(s._err); if (s._state === 'errored') this._closed.reject(s._err); }
    }
    get closed() { return this._closed.promise; }
    get ready() { return this._ready.promise; }
    get desiredSize() {
        const s = this._s;
        if (!s) throw new TypeError('WritableStream writer lock was released');
        if (s._state === 'erroring' || s._state === 'errored') return null;
        return s._state === 'closed' ? 0 : s._hwm - s._queueSize;
    }
    write(chunk) { return this._s ? this._s._write(chunk, this) : Promise.reject(new TypeError('WritableStream writer lock was released')); }
    close() { return this._s ? this._s._close() : Promise.reject(new TypeError('WritableStream writer lock was released')); }
    abort(reason) { return this._s ? this._s._abort(reason) : Promise.reject(new TypeError('WritableStream writer lock was released')); }
    releaseLock() {
        if (!this._s) return;
        const error = new TypeError('WritableStream writer lock was released');
        writableRejectSlot(this, '_ready', error); writableRejectSlot(this, '_closed', error);
        this._s._writer = null; this._s = null;
    }
}
class WritableStream {
    constructor(sink = {}, strategy = {}) {
        sink = sink == null ? {} : sink; strategy = strategy == null ? {} : strategy;
        if (!['object', 'function'].includes(typeof sink)) throw new TypeError('Underlying sink must be an object');
        if (sink.type !== undefined) throw new RangeError('Writable stream types are not supported');
        const algorithms = {};
        for (const name of ['start', 'write', 'close', 'abort']) {
            const method = sink[name];
            if (method !== undefined && typeof method !== 'function') throw new TypeError(`Underlying sink ${name} must be callable`);
            algorithms[name] = method;
        }
        const size = strategy.size;
        if (size !== undefined && typeof size !== 'function') throw new TypeError('Queue strategy size must be callable');
        const highWaterMark = strategy.highWaterMark === undefined ? 1 : Number(strategy.highWaterMark);
        if (Number.isNaN(highWaterMark) || highWaterMark < 0) throw new RangeError('Invalid high water mark');
        def(this, '_sink', sink); def(this, '_algorithms', algorithms); def(this, '_size', size);
        def(this, '_writer', null); def(this, '_state', 'writable'); def(this, '_err', undefined);
        def(this, '_queue', []); def(this, '_queueSize', 0); def(this, '_hwm', highWaterMark);
        def(this, '_backpressure', highWaterMark <= 0); def(this, '_started', false);
        def(this, '_inFlight', null); def(this, '_closing', false); def(this, '_closeRequest', null); def(this, '_abortRequest', null);
        const ac = new AbortController(); def(this, '_ac', ac);
        const controller = { error: reason => this._controllerError(reason), signal: ac.signal }; def(this, '_c', controller);
        // A synchronous start exception belongs to the constructor, while a
        // rejected start promise errors the constructed stream asynchronously.
        const started = algorithms.start ? algorithms.start.call(sink, controller) : undefined;
        Promise.resolve(started).then(() => { this._started = true; this._advance(); }, error => { this._started = true; this._dealRejection(error); });
    }
    get locked() { return !!this._writer; }
    getWriter() { return new WritableStreamDefaultWriter(this); }
    close() { return this._writer ? Promise.reject(new TypeError('WritableStream is locked')) : this._close(); }
    abort(reason) { return this._writer ? Promise.reject(new TypeError('WritableStream is locked')) : this._abort(reason); }
    _updateBackpressure() {
        if (this._state !== 'writable' || this._closing) return;
        const pressure = this._hwm - this._queueSize <= 0;
        if (pressure !== this._backpressure && this._writer) {
            if (pressure) this._writer._ready = writableDeferred(true);
            else this._writer._ready.resolve();
        }
        this._backpressure = pressure;
    }
    _write(chunk, writer) {
        let size = 1;
        try { if (this._size) size = this._size.call(undefined, chunk); }
        catch (error) { this._controllerError(error); }
        // The strategy callback may release the writer or change stream state.
        if (writer._s !== this) return Promise.reject(new TypeError('WritableStream writer lock was released'));
        if (this._state === 'errored') return Promise.reject(this._err);
        if (this._closing || this._state === 'closed') return Promise.reject(new TypeError('Cannot write to a closing or closed stream'));
        if (this._state === 'erroring') return Promise.reject(this._err);
        const request = writableDeferred();
        try {
            size = Number(size);
            if (!Number.isFinite(size) || size < 0) throw new RangeError('Invalid queue chunk size');
        } catch (error) { this._controllerError(error); request.reject(error); return request.promise; }
        this._queue.push({ kind: 'write', chunk, size, request }); this._queueSize += size;
        this._updateBackpressure(); this._advance();
        return request.promise;
    }
    _close() {
        if (this._closing || this._state === 'closed' || this._state === 'errored') return Promise.reject(new TypeError('Cannot close this stream'));
        const request = writableDeferred(); this._closeRequest = request; this._closing = true;
        if (this._writer && this._backpressure && this._state === 'writable') this._writer._ready.resolve();
        this._queue.push({ kind: 'close', request }); this._advance();
        return request.promise;
    }
    _abort(reason) {
        if (this._state === 'closed' || this._state === 'errored') return Promise.resolve();
        this._ac.abort(reason);
        if (this._state === 'closed' || this._state === 'errored') return Promise.resolve();
        if (this._abortRequest) return this._abortRequest.request.promise;
        const request = writableDeferred(); const alreadyErroring = this._state === 'erroring';
        this._abortRequest = { request, reason, alreadyErroring };
        if (!alreadyErroring) this._startErroring(reason);
        else this._advance();
        return request.promise;
    }
    _clearAlgorithms() { this._algorithms = {}; this._size = undefined; this._sink = null; }
    _controllerError(reason) {
        if (this._state !== 'writable') return;
        this._clearAlgorithms(); this._startErroring(reason);
    }
    _startErroring(reason) {
        if (this._state !== 'writable') return;
        this._state = 'erroring'; this._err = reason;
        if (this._writer) writableRejectSlot(this._writer, '_ready', reason);
        if (this._started && !this._inFlight) this._finishErroring();
    }
    _dealRejection(reason) {
        if (this._state === 'writable') this._startErroring(reason);
        else if (this._state === 'erroring') this._finishErroring();
    }
    _rejectCloseAndClosed() {
        if (this._closeRequest) { this._closeRequest.reject(this._err); this._closeRequest = null; }
        if (this._writer) this._writer._closed.reject(this._err);
    }
    _finishErroring() {
        if (this._state !== 'erroring' || this._inFlight || !this._started) return;
        this._state = 'errored';
        for (const item of this._queue) if (item.kind === 'write') item.request.reject(this._err);
        this._queue.length = 0; this._queueSize = 0;
        const abort = this._abortRequest; this._abortRequest = null;
        if (!abort || abort.alreadyErroring) {
            if (abort) abort.request.reject(this._err);
            this._clearAlgorithms(); this._rejectCloseAndClosed(); return;
        }
        let result;
        try { result = this._algorithms.abort ? this._algorithms.abort.call(this._sink, abort.reason) : undefined; }
        catch (error) { result = Promise.reject(error); }
        this._clearAlgorithms();
        Promise.resolve(result).then(() => { abort.request.resolve(); this._rejectCloseAndClosed(); }, error => { abort.request.reject(error); this._rejectCloseAndClosed(); });
    }
    _advance() {
        if (!this._started || this._inFlight) return;
        if (this._state === 'erroring') { this._finishErroring(); return; }
        if (this._state !== 'writable' || !this._queue.length) return;
        const item = this._queue[0]; this._inFlight = item;
        if (item.kind === 'close') { this._queue.shift(); this._closeRequest = null; }
        let result;
        try { result = this._algorithms[item.kind] ? this._algorithms[item.kind].call(this._sink, ...(item.kind === 'write' ? [item.chunk, this._c] : [])) : undefined; }
        catch (error) { result = Promise.reject(error); }
        if (item.kind === 'close') this._clearAlgorithms();
        Promise.resolve(result).then(() => {
            item.request.resolve(); this._inFlight = null;
            if (item.kind === 'close') {
                this._state = 'closed'; this._err = undefined;
                if (this._abortRequest) { this._abortRequest.request.resolve(); this._abortRequest = null; }
                if (this._writer) this._writer._closed.resolve();
                return;
            }
            this._queue.shift(); this._queueSize = Math.max(0, this._queueSize - item.size);
            this._updateBackpressure(); this._advance();
        }, error => {
            item.request.reject(error); this._inFlight = null;
            if (item.kind === 'close' && this._abortRequest) { this._abortRequest.request.reject(error); this._abortRequest = null; }
            if (this._state === 'writable') this._clearAlgorithms();
            this._dealRejection(error);
        });
    }
}
class TransformStream {
    constructor(t = {}, ws = {}, rs = {}) {
        t = t || {};
        let rc; const readable = new ReadableStream({ start(c) { rc = c; }, cancel(r) { if (t.cancel) return t.cancel.call(t, r); } }, rs);
        const tc = { enqueue: (x) => rc.enqueue(x), error: (e) => { rc.error(e); writable._c.error(e); }, terminate: () => rc.close(), get desiredSize() { return rc.desiredSize; } };
        const writable = new WritableStream({
            start() { return t.start ? t.start.call(t, tc) : undefined; },
            write(x) { return t.transform ? t.transform.call(t, x, tc) : tc.enqueue(x); },
            close() { return Promise.resolve(t.flush ? t.flush.call(t, tc) : undefined).then(() => { try { rc.close(); } catch (e) {} }); },
            abort(r) { rc.error(r); },
        }, ws);
        def(this, '_r', readable); def(this, '_w', writable);
    }
    get readable() { return this._r; } get writable() { return this._w; }
}
class ByteLengthQueuingStrategy { constructor(o) { this.highWaterMark = o.highWaterMark; } size(c) { return c.byteLength; } }
class CountQueuingStrategy { constructor(o) { this.highWaterMark = o.highWaterMark; } size() { return 1; } }
class TextEncoderStream extends TransformStream { constructor() { const e = new TextEncoder(); super({ transform(c, k) { k.enqueue(e.encode(c)); } }); } get encoding() { return 'utf-8'; } }
class TextDecoderStream extends TransformStream { constructor(l = 'utf-8') { const d = new TextDecoder(l); super({ transform(c, k) { const s = d.decode(c); if (s) k.enqueue(s); } }); def(this, '_enc', d.encoding); } get encoding() { return this._enc; } }
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
