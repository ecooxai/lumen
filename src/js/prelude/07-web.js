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
            if (sig) { const onAbort = () => { const e = sig.reason !== undefined ? sig.reason : new DOMException('The operation was aborted.', 'AbortError'); if (!o.preventAbort) dest.abort(e).catch(() => {}); if (!o.preventCancel) this._cancel(e); fin(e, true); }; if (sig.aborted) return onAbort(); sig.addEventListener('abort', onAbort, { once: true }); }
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
class WritableStreamDefaultWriter {
    constructor(s) { if (s._writer) throw new TypeError('WritableStream is locked'); def(this, '_s', s); s._writer = this; }
    get closed() { return this._s._closedP; } get ready() { return Promise.resolve(); } get desiredSize() { const s = this._s; return s._err !== undefined ? null : s._closing ? 0 : Math.max(0, s._hwm - s._pending); }
    write(x) { return this._s._write(x); } close() { return this._s._close(); } abort(r) { return this._s.abort(r); }
    releaseLock() { if (this._s && this._s._writer === this) this._s._writer = null; }
}
class WritableStream {
    constructor(sink = {}, strategy = {}) {
        sink = sink || {};
        def(this, '_sink', sink); def(this, '_writer', null); def(this, '_err', undefined); def(this, '_closing', false); def(this, '_pending', 0); def(this, '_hwm', strategy && strategy.highWaterMark != null ? strategy.highWaterMark : 1);
        const ac = new AbortController();
        const c = { error: (e) => { if (this._err === undefined) this._err = e; }, signal: ac.signal }; def(this, '_c', c); def(this, '_ac', ac);
        let r, j; def(this, '_closedP', new Promise((a, b) => { r = a; j = b; })); this._closedP.catch(() => {}); def(this, '_cr', [r, j]);
        let st; try { st = sink.start ? sink.start.call(sink, c) : undefined; } catch (e) { st = Promise.reject(e); }
        def(this, '_chain', Promise.resolve(st).catch(e => { this._err = e; this._cr[1](e); }));
    }
    get locked() { return !!this._writer; }
    getWriter() { return new WritableStreamDefaultWriter(this); }
    _write(x) {
        if (this._closing) return Promise.reject(new TypeError('Cannot write to a closing stream'));
        this._pending++;
        const p = this._chain.then(() => { if (this._err !== undefined) throw this._err; return this._sink.write ? this._sink.write.call(this._sink, x, this._c) : undefined; }).finally(() => this._pending--);
        this._chain = p.catch(e => { if (this._err === undefined) this._err = e; this._cr[1](e); });
        return p.then(() => {});
    }
    _close() {
        if (this._closing) return Promise.reject(new TypeError('Stream is already closing'));
        this._closing = true;
        const p = this._chain.then(() => { if (this._err !== undefined) throw this._err; return this._sink.close ? this._sink.close.call(this._sink) : undefined; });
        this._chain = p.then(() => this._cr[0](), e => this._cr[1](e));
        return p.then(() => {});
    }
    close() { if (this._writer) return Promise.reject(new TypeError('WritableStream is locked')); return this._close(); }
    abort(r) { if (this._err === undefined) this._err = r; this._ac.abort(r); this._cr[1](r); return Promise.resolve(this._sink.abort ? this._sink.abort.call(this._sink, r) : undefined).then(() => {}); }
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
