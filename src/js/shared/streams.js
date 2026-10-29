/* Streams per the WHATWG Streams standard, shared by the page and worker preludes: ReadableStream with its default
   and byte controllers and readers, WritableStream, TransformStream and the queuing strategies. The functions follow
   the standard's abstract operations; each object keeps its internal slots in a record behind a private #r field, and
   the records point back to their objects through .pub. Promises use the intrinsics captured here, so a page that
   patches Promise.prototype.then or Object.prototype does not see the streams' internal steps. */
const { ReadableStream, ReadableStreamDefaultReader, ReadableStreamBYOBReader, ReadableStreamDefaultController,
        ReadableByteStreamController, ReadableStreamBYOBRequest, WritableStream, WritableStreamDefaultWriter,
        WritableStreamDefaultController, TransformStream, TransformStreamDefaultController, ByteLengthQueuingStrategy,
        CountQueuingStrategy, streamDisturbed, streamTee } = (() => {
const $Promise = Promise, thenP = Promise.prototype.then, PResolve = Promise.resolve.bind(Promise), PReject = Promise.reject.bind(Promise);
const noop = () => {}, one = () => 1;
const INTERNAL = Symbol('streams internal'), END = Symbol('end of iteration');
const isObj = v => v !== null && (typeof v === 'object' || typeof v === 'function');
const illegal = () => { throw new TypeError('Illegal constructor'); };

/* promises */
const resolved = v => v !== null && (typeof v === 'object' || typeof v === 'function') ? new $Promise(r => r(v)) : PResolve(v);   /* WebIDL: always a new promise */
const rejected = e => PReject(e);
const handled = p => { thenP.call(p, undefined, noop); return p; };
const upon = (p, f, r) => { thenP.call(p, f, r); };
const uponF = (p, f) => { thenP.call(p, f, noop); };
const uponR = (p, r) => { thenP.call(p, undefined, r); };
const react = (p, f, r) => thenP.call(p, f, r);
const micro = f => { thenP.call(PResolve(undefined), f); };
const cap = () => { const c = { st: 0, p: null, res: null, rej: null }; c.p = new $Promise((a, b) => { c.res = a; c.rej = b; }); return c; };
const capResolved = v => ({ st: 1, p: resolved(v), res: noop, rej: noop });
const capRejected = e => ({ st: 2, p: handled(rejected(e)), res: noop, rej: noop });
const capResolve = (c, v) => { if (c.st) return; c.st = 1; c.res(v); };
const capReject = (c, e) => { if (c.st) return; c.st = 2; c.rej(e); };
const promiseCall = (f, self, args) => { try { return resolved(Reflect.apply(f, self, args)); } catch (e) { return rejected(e); } };
const waitAll = ps => {   /* "waiting for all", without Promise.all's observable lookups */
    const c = cap(); let n = ps.length;
    if (!n) { capResolve(c); return c.p; }
    for (const p of ps) upon(p, () => { if (--n === 0) capResolve(c); }, e => capReject(c, e));
    return c.p;
};

/* a queue that shifts in O(1) */
class Q {
    constructor() { this.a = []; this.h = 0; }
    get length() { return this.a.length - this.h; }
    push(x) { this.a.push(x); }
    peek() { return this.a[this.h]; }
    shift() {
        const x = this.a[this.h]; this.a[this.h++] = undefined;
        if (this.h === this.a.length) { this.a = []; this.h = 0; }
        else if (this.h > 1024 && this.h * 2 > this.a.length) { this.a = this.a.slice(this.h); this.h = 0; }
        return x;
    }
    take() { const r = this.a.slice(this.h); this.a = []; this.h = 0; return r; }
}
const resetQueue = c => { c.q = new Q(); c.qSize = 0; };
const dequeueValue = c => { const p = c.q.shift(); c.qSize -= p.size; if (c.qSize < 0) c.qSize = 0; return p.value; };
const enqueueValueWithSize = (c, value, size) => {
    if (typeof size !== 'number' || size !== size || size < 0 || size === Infinity)
        throw new RangeError('The return value of a queuing strategy\'s size function must be a finite, non-NaN, non-negative number.');
    c.q.push({ value, size }); c.qSize += size;
};

/* buffers, through the intrinsic accessors */
const AB = ArrayBuffer, abProto = AB.prototype, gd = (o, k) => Object.getOwnPropertyDescriptor(o, k);
const abLen = gd(abProto, 'byteLength').get, abTransferF = abProto.transfer;
const abDetached = gd(abProto, 'detached') ? gd(abProto, 'detached').get : null;
const abResizable = gd(abProto, 'resizable') ? gd(abProto, 'resizable').get : null;
const taProto = Object.getPrototypeOf(Uint8Array.prototype);
const taBuf = gd(taProto, 'buffer').get, taOff = gd(taProto, 'byteOffset').get, taLen = gd(taProto, 'byteLength').get, taLength = gd(taProto, 'length').get;
const taTag = gd(taProto, Symbol.toStringTag).get;
const dvBuf = gd(DataView.prototype, 'buffer').get, dvOff = gd(DataView.prototype, 'byteOffset').get, dvLen = gd(DataView.prototype, 'byteLength').get;
const isViewF = AB.isView, $Uint8Array = Uint8Array, $DataView = DataView;
const TA = { Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array, BigInt64Array, BigUint64Array };
if (typeof Float16Array === 'function') TA.Float16Array = Float16Array;
const isAB = b => { try { abLen.call(b); return true; } catch (e) { return false; } };
const isDetached = b => abDetached ? abDetached.call(b) : (() => { try { new $Uint8Array(b); return false; } catch (e) { return true; } })();
const bufLen = b => abLen.call(b);
const transferAB = b => {
    if (abTransferF) return abTransferF.call(b);
    const n = abLen.call(b), out = new AB(n); new $Uint8Array(out).set(new $Uint8Array(b)); structuredClone(b, { transfer: [b] }); return out;
};
const cloneAB = (b, off, n) => { const src = new $Uint8Array(b, off, n), out = new AB(n); new $Uint8Array(out).set(src); return out; };
const copyBytes = (dst, dstOff, src, srcOff, n) => { new $Uint8Array(dst, dstOff, n).set(new $Uint8Array(src, srcOff, n)); };
/* an ArrayBufferView's slots: { buffer, byteOffset, byteLength, length, ctor, es } */
const viewParts = v => {
    const tag = taTag.call(v);
    if (tag !== undefined) { const ctor = TA[tag]; return { buffer: taBuf.call(v), byteOffset: taOff.call(v), byteLength: taLen.call(v), length: taLength.call(v), ctor, es: ctor.BYTES_PER_ELEMENT, ta: true }; }
    const buffer = dvBuf.call(v); let byteOffset = 0, byteLength = 0;
    try { byteOffset = dvOff.call(v); byteLength = dvLen.call(v); } catch (e) {}
    return { buffer, byteOffset, byteLength, length: byteLength, ctor: $DataView, es: 1, ta: false };
};
/* WebIDL ArrayBufferView: not shared, not resizable */
const toView = (v, what) => {
    if (!isObj(v) || !isViewF(v)) throw new TypeError(`${what}: The provided value is not of type 'ArrayBufferView'.`);
    const p = viewParts(v);
    if (!isAB(p.buffer)) throw new TypeError(`${what}: The provided ArrayBufferView value must not be shared.`);
    if (abResizable && abResizable.call(p.buffer)) throw new TypeError(`${what}: The provided ArrayBufferView value must not be resizable.`);
    return p;
};

/* WebIDL conversions */
const enforceULL = (v, what) => {
    const x = +v;
    if (!Number.isFinite(x)) throw new TypeError(`${what}: Value is not of type 'unsigned long long'.`);
    const t = Math.trunc(x);
    if (t < 0 || t > Number.MAX_SAFE_INTEGER) throw new TypeError(`${what}: Value is outside the 'unsigned long long' value range.`);
    return t === 0 ? 0 : t;
};
const dictOf = (v, what, type) => {
    if (v === undefined || v === null) return null;
    if (!isObj(v)) throw new TypeError(`${what}: The provided value is not of type '${type}'.`);
    return v;
};
const fnMember = (d, k, what, type) => {
    const v = d[k];
    if (v === undefined) return undefined;
    if (typeof v !== 'function') throw new TypeError(`${what}: Failed to read the '${k}' property from '${type}': The provided value is not a function.`);
    return v;
};
const toStrategy = (s, what) => {
    const d = dictOf(s, what, 'QueuingStrategy');
    if (!d) return { hwm: undefined, size: undefined };
    const h = d.highWaterMark, hwm = h === undefined ? undefined : +h;
    return { hwm, size: fnMember(d, 'size', what, 'QueuingStrategy') };
};
const extractHWM = (st, def) => {
    if (st.hwm === undefined) return def;
    if (st.hwm !== st.hwm || st.hwm < 0) throw new RangeError('The highWaterMark of a queuing strategy must be a non-negative, non-NaN number.');
    return st.hwm;
};
const extractSize = st => { const f = st.size; return f === undefined ? one : chunk => +f(chunk); };
const toPipeOptions = (o, what) => {
    const d = dictOf(o, what, 'StreamPipeOptions');
    if (!d) return { preventAbort: false, preventCancel: false, preventClose: false, signal: undefined };
    const preventAbort = !!d.preventAbort, preventCancel = !!d.preventCancel, preventClose = !!d.preventClose, signal = d.signal;
    if (signal !== undefined && !isSignal(signal)) throw new TypeError(`${what}: Failed to read the 'signal' property from 'StreamPipeOptions': Failed to convert value to 'AbortSignal'.`);
    return { preventAbort, preventCancel, preventClose, signal };
};
/* a real AbortSignal of this realm, or one from another realm (an iframe's), checked with that realm's own getter */
const isSignal = s => {
    if (!isObj(s)) return false;
    if (s instanceof AbortSignal) return abortSignalIs(s);
    try { const C = s.constructor, g = C && C.name === 'AbortSignal' && Object.getOwnPropertyDescriptor(C.prototype, 'aborted'); if (g && g.get) { g.get.call(s); return true; } } catch (e) {}
    return false;
};

/* ===== ReadableStream ===== */
let rsOf, rsIs, readerOf, readerIs, byobReaderIs, wsOf, wsIs;
const newRS = () => ({ state: 'readable', reader: undefined, err: undefined, disturbed: false, ctl: undefined, pub: null });
const pubRS = r => { new ReadableStream(INTERNAL, r); return r; };
const isLocked = s => s.reader !== undefined;
const createRS = (start, pull, cancel, hwm = 1, size = one) => {
    const s = pubRS(newRS()), c = newRSDC();
    setUpRSDC(s, c, start, pull, cancel, hwm, size);
    return s;
};
const createByteRS = (start, pull, cancel) => {
    const s = pubRS(newRS()), c = newRBSC();
    setUpRBSC(s, c, start, pull, cancel, 0, undefined);
    return s;
};
const rsCancel = (s, reason) => {
    s.disturbed = true;
    if (s.state === 'closed') return resolved(undefined);
    if (s.state === 'errored') return rejected(s.err);
    rsClose(s);
    const reader = s.reader;
    if (reader !== undefined && reader.byob) { const reqs = reader.reqs.take(); reader.reqs = new Q(); for (const r of reqs) r.close(undefined); }
    const p = s.ctl.cancelSteps(reason);
    return react(p, noop);
};
const rsClose = s => {
    s.state = 'closed';
    const reader = s.reader;
    if (reader === undefined) return;
    capResolve(reader.closed, undefined);
    if (!reader.byob) { const reqs = reader.reqs.take(); reader.reqs = new Q(); for (const r of reqs) r.close(); }
};
const rsError = (s, e) => {
    s.state = 'errored'; s.err = e;
    const reader = s.reader;
    if (reader === undefined) return;
    capReject(reader.closed, e); handled(reader.closed.p);
    if (!reader.byob) defaultReaderErrorReadRequests(reader, e);
    else byobReaderErrorReadIntoRequests(reader, e);
};
const rsAddReadRequest = (s, rr) => { s.reader.reqs.push(rr); };
const rsFulfillReadRequest = (s, chunk, done) => { const rr = s.reader.reqs.shift(); if (done) rr.close(); else rr.chunk(chunk); };
const rsFulfillReadIntoRequest = (s, chunk, done) => { const rr = s.reader.reqs.shift(); if (done) rr.close(chunk); else rr.chunk(chunk); };
const rsNumReadRequests = s => s.reader.reqs.length;
const rsHasDefaultReader = s => s.reader !== undefined && !s.reader.byob;
const rsHasBYOBReader = s => s.reader !== undefined && s.reader.byob;

/* readers */
const readerGenericInit = (r, s) => {
    r.s = s; s.reader = r;
    if (s.state === 'readable') r.closed = cap();
    else if (s.state === 'closed') r.closed = capResolved(undefined);
    else r.closed = capRejected(s.err);
};
const readerGenericCancel = (r, reason) => rsCancel(r.s, reason);
const readerGenericRelease = r => {
    const s = r.s;
    const e = new TypeError('Releasing reader');
    if (s.state === 'readable') capReject(r.closed, e);
    else r.closed = capRejected(e);
    handled(r.closed.p);
    s.ctl.releaseSteps();
    s.reader = undefined; r.s = undefined;
};
const defaultReaderErrorReadRequests = (r, e) => { const reqs = r.reqs.take(); r.reqs = new Q(); for (const q of reqs) q.error(e); };
const byobReaderErrorReadIntoRequests = (r, e) => { const reqs = r.reqs.take(); r.reqs = new Q(); for (const q of reqs) q.error(e); };
const defaultReaderRelease = r => { readerGenericRelease(r); defaultReaderErrorReadRequests(r, new TypeError('Releasing reader')); };
const byobReaderRelease = r => { readerGenericRelease(r); byobReaderErrorReadIntoRequests(r, new TypeError('Releasing reader')); };
const defaultReaderRead = (r, rr) => {
    const s = r.s;
    s.disturbed = true;
    if (s.state === 'closed') rr.close();
    else if (s.state === 'errored') rr.error(s.err);
    else s.ctl.pullSteps(rr);
};
const byobReaderRead = (r, view, min, rr) => {
    const s = r.s;
    s.disturbed = true;
    if (s.state === 'errored') rr.error(s.err);
    else rbscPullInto(s.ctl, view, min, rr);
};
const newReader = byob => ({ byob, s: undefined, closed: null, reqs: new Q(), pub: null });
const setUpDefaultReader = (r, s) => {
    if (isLocked(s)) throw new TypeError("Failed to construct 'ReadableStreamDefaultReader': ReadableStream is locked");
    readerGenericInit(r, s);
    r.reqs = new Q();
};
const setUpBYOBReader = (r, s) => {
    if (isLocked(s)) throw new TypeError("Failed to construct 'ReadableStreamBYOBReader': ReadableStream is locked");
    if (!s.ctl.byte) throw new TypeError("Failed to construct 'ReadableStreamBYOBReader': Cannot use a BYOB reader with a non-byte stream");
    readerGenericInit(r, s);
    r.reqs = new Q();
};
const acquireDefaultReader = s => { const r = newReader(false); new ReadableStreamDefaultReader(INTERNAL, r); setUpDefaultReader(r, s); return r; };
const acquireBYOBReader = s => { const r = newReader(true); new ReadableStreamBYOBReader(INTERNAL, r); setUpBYOBReader(r, s); return r; };

/* default controller */
const newRSDC = () => {
    const c = { byte: false, s: undefined, q: null, qSize: 0, started: false, closeReq: false, pullAgain: false, pulling: false,
                size: undefined, hwm: 0, pull: undefined, cancel: undefined, pub: null };
    c.cancelSteps = reason => { resetQueue(c); const r = c.cancel(reason); rsdcClearAlgorithms(c); return r; };
    c.pullSteps = rr => {
        const s = c.s;
        if (c.q.length > 0) {
            const chunk = dequeueValue(c);
            if (c.closeReq && c.q.length === 0) { rsdcClearAlgorithms(c); rsClose(s); }
            else rsdcCallPullIfNeeded(c);
            rr.chunk(chunk);
        } else {
            rsAddReadRequest(s, rr);
            rsdcCallPullIfNeeded(c);
        }
    };
    c.releaseSteps = noop;
    new ReadableStreamDefaultController(INTERNAL, c);
    return c;
};
const rsdcCallPullIfNeeded = c => {
    if (!rsdcShouldCallPull(c)) return;
    if (c.pulling) { c.pullAgain = true; return; }
    c.pulling = true;
    upon(c.pull(), () => { c.pulling = false; if (c.pullAgain) { c.pullAgain = false; rsdcCallPullIfNeeded(c); } }, e => { rsdcError(c, e); });
};
const rsdcShouldCallPull = c => {
    const s = c.s;
    if (!rsdcCanCloseOrEnqueue(c) || !c.started) return false;
    if (isLocked(s) && rsNumReadRequests(s) > 0) return true;
    return rsdcDesiredSize(c) > 0;
};
const rsdcClearAlgorithms = c => { c.pull = undefined; c.cancel = undefined; c.size = undefined; };
const rsdcClose = c => {
    if (!rsdcCanCloseOrEnqueue(c)) return;
    c.closeReq = true;
    if (c.q.length === 0) { rsdcClearAlgorithms(c); rsClose(c.s); }
};
const rsdcEnqueue = (c, chunk) => {
    if (!rsdcCanCloseOrEnqueue(c)) return;
    const s = c.s;
    if (isLocked(s) && rsNumReadRequests(s) > 0) rsFulfillReadRequest(s, chunk, false);
    else {
        let size;
        try { size = c.size(chunk); } catch (e) { rsdcError(c, e); throw e; }
        try { enqueueValueWithSize(c, chunk, size); } catch (e) { rsdcError(c, e); throw e; }
    }
    rsdcCallPullIfNeeded(c);
};
const rsdcError = (c, e) => {
    const s = c.s;
    if (s.state !== 'readable') return;
    resetQueue(c); rsdcClearAlgorithms(c); rsError(s, e);
};
const rsdcDesiredSize = c => { const st = c.s.state; return st === 'errored' ? null : st === 'closed' ? 0 : c.hwm - c.qSize; };
const rsdcHasBackpressure = c => !rsdcShouldCallPull(c);
const rsdcCanCloseOrEnqueue = c => !c.closeReq && c.s.state === 'readable';
const setUpRSDC = (s, c, start, pull, cancel, hwm, size) => {
    c.s = s; resetQueue(c);
    c.started = c.closeReq = c.pullAgain = c.pulling = false;
    c.size = size; c.hwm = hwm; c.pull = pull; c.cancel = cancel;
    s.ctl = c;
    const startResult = start();
    upon(resolved(startResult), () => { c.started = true; rsdcCallPullIfNeeded(c); }, r => { rsdcError(c, r); });
};
const setUpRSDCFromSource = (s, src, d, hwm, size) => {
    const c = newRSDC();
    let start = noop, pull = () => resolved(undefined), cancel = () => resolved(undefined);
    if (d.start) start = () => Reflect.apply(d.start, src, [c.pub]);
    if (d.pull) pull = () => promiseCall(d.pull, src, [c.pub]);
    if (d.cancel) cancel = reason => promiseCall(d.cancel, src, [reason]);
    setUpRSDC(s, c, start, pull, cancel, hwm, size);
};

/* byte controller */
const newRBSC = () => {
    const c = { byte: true, s: undefined, q: null, qSize: 0, started: false, closeReq: false, pullAgain: false, pulling: false,
                hwm: 0, pull: undefined, cancel: undefined, autoAlloc: undefined, pending: [], byobReq: null, pub: null };
    c.cancelSteps = reason => { rbscClearPendingPullIntos(c); resetQueue(c); const r = c.cancel(reason); rbscClearAlgorithms(c); return r; };
    c.pullSteps = rr => {
        const s = c.s;
        if (c.qSize > 0) { rbscFillReadRequestFromQueue(c, rr); return; }
        const n = c.autoAlloc;
        if (n !== undefined) {
            let buffer;
            try { buffer = new AB(n); } catch (e) { rr.error(e); return; }
            c.pending.push({ buffer, bufferByteLength: n, byteOffset: 0, byteLength: n, bytesFilled: 0, minimumFill: 1, es: 1, ctor: $Uint8Array, readerType: 'default' });
        }
        rsAddReadRequest(s, rr);
        rbscCallPullIfNeeded(c);
    };
    c.releaseSteps = () => { if (c.pending.length) { const first = c.pending[0]; first.readerType = 'none'; c.pending = [first]; } };
    new ReadableByteStreamController(INTERNAL, c);
    return c;
};
const rbscCallPullIfNeeded = c => {
    if (!rbscShouldCallPull(c)) return;
    if (c.pulling) { c.pullAgain = true; return; }
    c.pulling = true;
    upon(c.pull(), () => { c.pulling = false; if (c.pullAgain) { c.pullAgain = false; rbscCallPullIfNeeded(c); } }, e => { rbscError(c, e); });
};
const rbscShouldCallPull = c => {
    const s = c.s;
    if (s.state !== 'readable' || c.closeReq || !c.started) return false;
    if (rsHasDefaultReader(s) && rsNumReadRequests(s) > 0) return true;
    if (rsHasBYOBReader(s) && rsNumReadRequests(s) > 0) return true;
    return rbscDesiredSize(c) > 0;
};
const rbscClearAlgorithms = c => { c.pull = undefined; c.cancel = undefined; };
const rbscClearPendingPullIntos = c => { rbscInvalidateBYOBRequest(c); c.pending = []; };
const rbscClose = c => {
    const s = c.s;
    if (c.closeReq || s.state !== 'readable') return;
    if (c.qSize > 0) { c.closeReq = true; return; }
    if (c.pending.length) {
        const first = c.pending[0];
        if (first.bytesFilled % first.es !== 0) { const e = new TypeError('Insufficient bytes to fill elements in the given buffer'); rbscError(c, e); throw e; }
    }
    rbscClearAlgorithms(c); rsClose(s);
};
const rbscCommitPullIntoDescriptor = (s, pid) => {
    let done = false;
    if (s.state === 'closed') done = true;
    const filledView = rbscConvertPullIntoDescriptor(pid);
    if (pid.readerType === 'default') rsFulfillReadRequest(s, filledView, done);
    else rsFulfillReadIntoRequest(s, filledView, done);
};
const rbscConvertPullIntoDescriptor = pid => {
    const buffer = transferAB(pid.buffer);
    return new pid.ctor(buffer, pid.byteOffset, pid.bytesFilled / pid.es);
};
const rbscEnqueue = (c, chunk, p) => {
    const s = c.s;
    if (c.closeReq || s.state !== 'readable') return;
    const { buffer, byteOffset, byteLength } = p;
    if (isDetached(buffer)) throw new TypeError("Failed to execute 'enqueue' on 'ReadableByteStreamController': The chunk's buffer is detached.");
    const transferred = transferAB(buffer);
    if (c.pending.length) {
        const first = c.pending[0];
        if (isDetached(first.buffer)) throw new TypeError("Failed to execute 'enqueue' on 'ReadableByteStreamController': The BYOB request's buffer has been detached and so cannot be filled with an enqueued chunk");
        rbscInvalidateBYOBRequest(c);
        first.buffer = transferAB(first.buffer);
        if (first.readerType === 'none') rbscEnqueueDetachedPullIntoToQueue(c, first);
    }
    if (rsHasDefaultReader(s)) {
        rbscProcessReadRequestsUsingQueue(c);
        if (rsNumReadRequests(s) === 0) rbscEnqueueChunkToQueue(c, transferred, byteOffset, byteLength);
        else {
            if (c.pending.length) rbscShiftPendingPullInto(c);
            rsFulfillReadRequest(s, new $Uint8Array(transferred, byteOffset, byteLength), false);
        }
    } else if (rsHasBYOBReader(s)) {
        rbscEnqueueChunkToQueue(c, transferred, byteOffset, byteLength);
        const filled = rbscProcessPullIntoDescriptorsUsingQueue(c);
        for (const pid of filled) rbscCommitPullIntoDescriptor(s, pid);
    } else rbscEnqueueChunkToQueue(c, transferred, byteOffset, byteLength);
    rbscCallPullIfNeeded(c);
};
const rbscEnqueueChunkToQueue = (c, buffer, byteOffset, byteLength) => { c.q.push({ buffer, byteOffset, byteLength }); c.qSize += byteLength; };
const rbscEnqueueClonedChunkToQueue = (c, buffer, byteOffset, byteLength) => {
    let clone;
    try { clone = cloneAB(buffer, byteOffset, byteLength); } catch (e) { rbscError(c, e); throw e; }
    rbscEnqueueChunkToQueue(c, clone, 0, byteLength);
};
const rbscEnqueueDetachedPullIntoToQueue = (c, pid) => {
    if (pid.bytesFilled > 0) rbscEnqueueClonedChunkToQueue(c, pid.buffer, pid.byteOffset, pid.bytesFilled);
    rbscShiftPendingPullInto(c);
};
const rbscError = (c, e) => {
    const s = c.s;
    if (s.state !== 'readable') return;
    rbscClearPendingPullIntos(c); resetQueue(c); rbscClearAlgorithms(c); rsError(s, e);
};
const rbscFillHeadPullIntoDescriptor = (c, size, pid) => { pid.bytesFilled += size; };
const rbscFillPullIntoDescriptorFromQueue = (c, pid) => {
    const maxBytesToCopy = Math.min(c.qSize, pid.byteLength - pid.bytesFilled);
    const maxBytesFilled = pid.bytesFilled + maxBytesToCopy;
    let remaining = maxBytesToCopy, ready = false;
    const maxAlignedBytes = maxBytesFilled - maxBytesFilled % pid.es;
    if (maxAlignedBytes >= pid.minimumFill) { remaining = maxAlignedBytes - pid.bytesFilled; ready = true; }
    const q = c.q;
    while (remaining > 0) {
        const head = q.peek();
        const n = Math.min(remaining, head.byteLength);
        copyBytes(pid.buffer, pid.byteOffset + pid.bytesFilled, head.buffer, head.byteOffset, n);
        if (head.byteLength === n) q.shift();
        else { head.byteOffset += n; head.byteLength -= n; }
        c.qSize -= n;
        rbscFillHeadPullIntoDescriptor(c, n, pid);
        remaining -= n;
    }
    return ready;
};
const rbscFillReadRequestFromQueue = (c, rr) => {
    const entry = c.q.shift();
    c.qSize -= entry.byteLength;
    rbscHandleQueueDrain(c);
    rr.chunk(new $Uint8Array(entry.buffer, entry.byteOffset, entry.byteLength));
};
const rbscGetBYOBRequest = c => {
    if (c.byobReq === null && c.pending.length) {
        const first = c.pending[0];
        const view = new $Uint8Array(first.buffer, first.byteOffset + first.bytesFilled, first.byteLength - first.bytesFilled);
        const req = { ctl: c, view, pub: null };
        new ReadableStreamBYOBRequest(INTERNAL, req);
        c.byobReq = req;
    }
    return c.byobReq;
};
const rbscDesiredSize = c => { const st = c.s.state; return st === 'errored' ? null : st === 'closed' ? 0 : c.hwm - c.qSize; };
const rbscHandleQueueDrain = c => {
    if (c.qSize === 0 && c.closeReq) { rbscClearAlgorithms(c); rsClose(c.s); }
    else rbscCallPullIfNeeded(c);
};
const rbscInvalidateBYOBRequest = c => {
    if (c.byobReq === null) return;
    c.byobReq.ctl = undefined; c.byobReq.view = null; c.byobReq = null;
};
const rbscProcessPullIntoDescriptorsUsingQueue = c => {
    const filled = [];
    while (c.pending.length) {
        if (c.qSize === 0) break;
        const pid = c.pending[0];
        if (rbscFillPullIntoDescriptorFromQueue(c, pid)) { rbscShiftPendingPullInto(c); filled.push(pid); }
    }
    return filled;
};
const rbscProcessReadRequestsUsingQueue = c => {
    const reader = c.s.reader;
    while (reader.reqs.length > 0) {
        if (c.qSize === 0) return;
        rbscFillReadRequestFromQueue(c, reader.reqs.shift());
    }
};
const rbscPullInto = (c, view, min, rir) => {
    const s = c.s;
    const p = viewParts(view);
    const minimumFill = min * p.es;
    let buffer;
    try { buffer = transferAB(p.buffer); } catch (e) { rir.error(e); return; }
    const pid = { buffer, bufferByteLength: bufLen(buffer), byteOffset: p.byteOffset, byteLength: p.byteLength, bytesFilled: 0, minimumFill, es: p.es, ctor: p.ctor, readerType: 'byob' };
    if (c.pending.length) { c.pending.push(pid); rsAddReadRequest(s, rir); return; }
    if (s.state === 'closed') { rir.close(new pid.ctor(pid.buffer, pid.byteOffset, 0)); return; }
    if (c.qSize > 0) {
        if (rbscFillPullIntoDescriptorFromQueue(c, pid)) {
            const filledView = rbscConvertPullIntoDescriptor(pid);
            rbscHandleQueueDrain(c);
            rir.chunk(filledView);
            return;
        }
        if (c.closeReq) { const e = new TypeError('Insufficient bytes to fill elements in the given buffer'); rbscError(c, e); rir.error(e); return; }
    }
    c.pending.push(pid);
    rsAddReadRequest(s, rir);
    rbscCallPullIfNeeded(c);
};
const rbscRespond = (c, bytesWritten) => {
    const first = c.pending[0], st = c.s.state;
    if (st === 'closed') { if (bytesWritten !== 0) throw new TypeError("Failed to execute 'respond' on 'ReadableStreamBYOBRequest': bytesWritten must be 0 when calling respond() on a closed stream"); }
    else {
        if (bytesWritten === 0) throw new TypeError("Failed to execute 'respond' on 'ReadableStreamBYOBRequest': bytesWritten must be greater than 0 when calling respond() on a readable stream");
        if (first.bytesFilled + bytesWritten > first.byteLength) throw new RangeError("Failed to execute 'respond' on 'ReadableStreamBYOBRequest': bytesWritten out of range");
    }
    first.buffer = transferAB(first.buffer);
    rbscRespondInternal(c, bytesWritten);
};
const rbscRespondInClosedState = (c, first) => {
    if (first.readerType === 'none') rbscShiftPendingPullInto(c);
    const s = c.s;
    if (rsHasBYOBReader(s)) {
        const filled = [];
        while (filled.length < rsNumReadRequests(s)) filled.push(rbscShiftPendingPullInto(c));
        for (const pid of filled) rbscCommitPullIntoDescriptor(s, pid);
    }
};
const rbscRespondInReadableState = (c, bytesWritten, pid) => {
    rbscFillHeadPullIntoDescriptor(c, bytesWritten, pid);
    if (pid.readerType === 'none') {
        rbscEnqueueDetachedPullIntoToQueue(c, pid);
        const filled = rbscProcessPullIntoDescriptorsUsingQueue(c);
        for (const f of filled) rbscCommitPullIntoDescriptor(c.s, f);
        return;
    }
    if (pid.bytesFilled < pid.minimumFill) return;
    rbscShiftPendingPullInto(c);
    const rem = pid.bytesFilled % pid.es;
    if (rem > 0) {
        const end = pid.byteOffset + pid.bytesFilled;
        rbscEnqueueClonedChunkToQueue(c, pid.buffer, end - rem, rem);
    }
    pid.bytesFilled -= rem;
    const filled = rbscProcessPullIntoDescriptorsUsingQueue(c);
    rbscCommitPullIntoDescriptor(c.s, pid);
    for (const f of filled) rbscCommitPullIntoDescriptor(c.s, f);
};
const rbscRespondInternal = (c, bytesWritten) => {
    const first = c.pending[0];
    rbscInvalidateBYOBRequest(c);
    if (c.s.state === 'closed') rbscRespondInClosedState(c, first);
    else rbscRespondInReadableState(c, bytesWritten, first);
    rbscCallPullIfNeeded(c);
};
const rbscRespondWithNewView = (c, view, p) => {
    const first = c.pending[0], st = c.s.state;
    if (st === 'closed') { if (p.byteLength !== 0) throw new TypeError("Failed to execute 'respondWithNewView' on 'ReadableStreamBYOBRequest': The view's length must be 0 when calling respondWithNewView() on a closed stream"); }
    else if (p.byteLength === 0) throw new TypeError("Failed to execute 'respondWithNewView' on 'ReadableStreamBYOBRequest': The view's length must be greater than 0 when calling respondWithNewView() on a readable stream");
    if (first.byteOffset + first.bytesFilled !== p.byteOffset) throw new RangeError("Failed to execute 'respondWithNewView' on 'ReadableStreamBYOBRequest': The region specified by view does not match byobRequest");
    if (first.bufferByteLength !== bufLen(p.buffer)) throw new RangeError("Failed to execute 'respondWithNewView' on 'ReadableStreamBYOBRequest': The buffer of view has different capacity than byobRequest");
    if (first.bytesFilled + p.byteLength > first.byteLength) throw new RangeError("Failed to execute 'respondWithNewView' on 'ReadableStreamBYOBRequest': The region specified by view is larger than byobRequest");
    const n = p.byteLength;
    first.buffer = transferAB(p.buffer);
    rbscRespondInternal(c, n);
};
const rbscShiftPendingPullInto = c => c.pending.shift();
const setUpRBSC = (s, c, start, pull, cancel, hwm, autoAlloc) => {
    c.s = s; c.pullAgain = c.pulling = false; c.byobReq = null; resetQueue(c);
    c.closeReq = c.started = false; c.hwm = hwm; c.pull = pull; c.cancel = cancel; c.autoAlloc = autoAlloc; c.pending = [];
    s.ctl = c;
    const startResult = start();
    upon(resolved(startResult), () => { c.started = true; rbscCallPullIfNeeded(c); }, r => { rbscError(c, r); });
};
const setUpRBSCFromSource = (s, src, d, hwm) => {
    const c = newRBSC();
    let start = noop, pull = () => resolved(undefined), cancel = () => resolved(undefined);
    if (d.start) start = () => Reflect.apply(d.start, src, [c.pub]);
    if (d.pull) pull = () => promiseCall(d.pull, src, [c.pub]);
    if (d.cancel) cancel = reason => promiseCall(d.cancel, src, [reason]);
    const autoAlloc = d.autoAllocateChunkSize;
    if (autoAlloc === 0) throw new TypeError("Failed to construct 'ReadableStream': autoAllocateChunkSize must be greater than 0");
    setUpRBSC(s, c, start, pull, cancel, hwm, autoAlloc);
};

/* tee */
const rsTee = (s, cloneForBranch2 = false) => s.ctl.byte ? rsByteTee(s) : rsDefaultTee(s, cloneForBranch2);
const rsDefaultTee = (s, cloneForBranch2) => {
    const reader = acquireDefaultReader(s);
    let reading = false, readAgain = false, canceled1 = false, canceled2 = false, reason1, reason2, branch1, branch2;
    const cancelP = cap();
    const pull = () => {
        if (reading) { readAgain = true; return resolved(undefined); }
        reading = true;
        defaultReaderRead(reader, {
            chunk: chunk => {
                micro(() => {
                    readAgain = false;
                    let chunk2 = chunk;
                    // Fetch's body clone uses the specification tee wrapper;
                    // public default-stream tee deliberately shares chunks.
                    if (!canceled2 && cloneForBranch2) {
                        try { chunk2 = structuredClone(chunk); }
                        catch (e) {
                            rsdcError(branch1.ctl, e); rsdcError(branch2.ctl, e);
                            capResolve(cancelP, rsCancel(s, e)); return;
                        }
                    }
                    if (!canceled1) rsdcEnqueue(branch1.ctl, chunk);
                    if (!canceled2) rsdcEnqueue(branch2.ctl, chunk2);
                    reading = false;
                    if (readAgain) pull();
                });
            },
            close: () => {
                reading = false;
                if (!canceled1) rsdcClose(branch1.ctl);
                if (!canceled2) rsdcClose(branch2.ctl);
                if (!canceled1 || !canceled2) capResolve(cancelP, undefined);
            },
            error: () => { reading = false; },
        });
        return resolved(undefined);
    };
    const cancel1 = reason => {
        canceled1 = true; reason1 = reason;
        if (canceled2) capResolve(cancelP, rsCancel(s, [reason1, reason2]));
        return cancelP.p;
    };
    const cancel2 = reason => {
        canceled2 = true; reason2 = reason;
        if (canceled1) capResolve(cancelP, rsCancel(s, [reason1, reason2]));
        return cancelP.p;
    };
    branch1 = createRS(noop, pull, cancel1);
    branch2 = createRS(noop, pull, cancel2);
    uponR(reader.closed.p, r => {
        rsdcError(branch1.ctl, r); rsdcError(branch2.ctl, r);
        if (!canceled1 || !canceled2) capResolve(cancelP, undefined);
    });
    return [branch1, branch2];
};
const cloneAsUint8Array = v => { const p = viewParts(v); return new $Uint8Array(cloneAB(p.buffer, p.byteOffset, p.byteLength)); };
const rsByteTee = s => {
    let reader = acquireDefaultReader(s);
    let reading = false, readAgain1 = false, readAgain2 = false, canceled1 = false, canceled2 = false, reason1, reason2, branch1, branch2;
    const cancelP = cap();
    const forwardReaderError = r => {
        uponR(r.closed.p, e => {
            if (r !== reader) return;
            rbscError(branch1.ctl, e); rbscError(branch2.ctl, e);
            if (!canceled1 || !canceled2) capResolve(cancelP, undefined);
        });
    };
    const pullWithDefaultReader = () => {
        if (reader.byob) { byobReaderRelease(reader); reader = acquireDefaultReader(s); forwardReaderError(reader); }
        defaultReaderRead(reader, {
            chunk: chunk => {
                micro(() => {
                    readAgain1 = readAgain2 = false;
                    const chunk1 = chunk; let chunk2 = chunk;
                    if (!canceled1 && !canceled2) {
                        try { chunk2 = cloneAsUint8Array(chunk); }
                        catch (e) { rbscError(branch1.ctl, e); rbscError(branch2.ctl, e); capResolve(cancelP, rsCancel(s, e)); return; }
                    }
                    if (!canceled1) rbscEnqueue(branch1.ctl, chunk1, viewParts(chunk1));
                    if (!canceled2) rbscEnqueue(branch2.ctl, chunk2, viewParts(chunk2));
                    reading = false;
                    if (readAgain1) pull1(); else if (readAgain2) pull2();
                });
            },
            close: () => {
                reading = false;
                if (!canceled1) rbscClose(branch1.ctl);
                if (!canceled2) rbscClose(branch2.ctl);
                if (branch1.ctl.pending.length) rbscRespond(branch1.ctl, 0);
                if (branch2.ctl.pending.length) rbscRespond(branch2.ctl, 0);
                if (!canceled1 || !canceled2) capResolve(cancelP, undefined);
            },
            error: () => { reading = false; },
        });
    };
    const pullWithBYOBReader = (view, forBranch2) => {
        if (!reader.byob) { defaultReaderRelease(reader); reader = acquireBYOBReader(s); forwardReaderError(reader); }
        const byobBranch = forBranch2 ? branch2 : branch1, otherBranch = forBranch2 ? branch1 : branch2;
        byobReaderRead(reader, view, 1, {
            chunk: chunk => {
                micro(() => {
                    readAgain1 = readAgain2 = false;
                    const byobCanceled = forBranch2 ? canceled2 : canceled1, otherCanceled = forBranch2 ? canceled1 : canceled2;
                    if (!otherCanceled) {
                        let cloned;
                        try { cloned = cloneAsUint8Array(chunk); }
                        catch (e) { rbscError(byobBranch.ctl, e); rbscError(otherBranch.ctl, e); capResolve(cancelP, rsCancel(s, e)); return; }
                        if (!byobCanceled) rbscRespondWithNewView(byobBranch.ctl, chunk, viewParts(chunk));
                        rbscEnqueue(otherBranch.ctl, cloned, viewParts(cloned));
                    } else if (!byobCanceled) rbscRespondWithNewView(byobBranch.ctl, chunk, viewParts(chunk));
                    reading = false;
                    if (readAgain1) pull1(); else if (readAgain2) pull2();
                });
            },
            close: chunk => {
                reading = false;
                const byobCanceled = forBranch2 ? canceled2 : canceled1, otherCanceled = forBranch2 ? canceled1 : canceled2;
                if (!byobCanceled) rbscClose(byobBranch.ctl);
                if (!otherCanceled) rbscClose(otherBranch.ctl);
                if (chunk !== undefined) {
                    if (!byobCanceled) rbscRespondWithNewView(byobBranch.ctl, chunk, viewParts(chunk));
                    if (!otherCanceled && otherBranch.ctl.pending.length) rbscRespond(otherBranch.ctl, 0);
                }
                if (!byobCanceled || !otherCanceled) capResolve(cancelP, undefined);
            },
            error: () => { reading = false; },
        });
    };
    const pull1 = () => {
        if (reading) { readAgain1 = true; return resolved(undefined); }
        reading = true;
        const req = rbscGetBYOBRequest(branch1.ctl);
        if (req === null) pullWithDefaultReader(); else pullWithBYOBReader(req.view, false);
        return resolved(undefined);
    };
    const pull2 = () => {
        if (reading) { readAgain2 = true; return resolved(undefined); }
        reading = true;
        const req = rbscGetBYOBRequest(branch2.ctl);
        if (req === null) pullWithDefaultReader(); else pullWithBYOBReader(req.view, true);
        return resolved(undefined);
    };
    const cancel1 = reason => {
        canceled1 = true; reason1 = reason;
        if (canceled2) capResolve(cancelP, rsCancel(s, [reason1, reason2]));
        return cancelP.p;
    };
    const cancel2 = reason => {
        canceled2 = true; reason2 = reason;
        if (canceled1) capResolve(cancelP, rsCancel(s, [reason1, reason2]));
        return cancelP.p;
    };
    branch1 = createByteRS(noop, pull1, cancel1);
    branch2 = createByteRS(noop, pull2, cancel2);
    forwardReaderError(reader);
    return [branch1, branch2];
};

/* pipeTo */
const rsPipeTo = (source, dest, preventClose, preventAbort, preventCancel, signal) => {
    const reader = acquireDefaultReader(source), writer = acquireWriter(dest);
    source.disturbed = true;
    let shuttingDown = false, currentWrite = resolved(undefined), abortAlgorithm;
    const done = cap();
    const finalize = (isError, error) => {
        writerRelease(writer);
        defaultReaderRelease(reader);
        if (signal !== undefined) abortSignalRemove(signal, abortAlgorithm);
        if (isError) capReject(done, error); else capResolve(done, undefined);
    };
    const waitForWritesToFinish = () => {
        const old = currentWrite;
        return react(currentWrite, () => old !== currentWrite ? waitForWritesToFinish() : undefined);
    };
    const shutdownWithAction = (action, originalIsError, originalError) => {
        if (shuttingDown) return;
        shuttingDown = true;
        const doTheRest = () => { upon(action(), () => finalize(originalIsError, originalError), e => finalize(true, e)); };
        if (dest.state === 'writable' && !wsCloseQueuedOrInFlight(dest)) uponF(waitForWritesToFinish(), doTheRest);
        else doTheRest();
    };
    const shutdown = (isError, error) => {
        if (shuttingDown) return;
        shuttingDown = true;
        if (dest.state === 'writable' && !wsCloseQueuedOrInFlight(dest)) uponF(waitForWritesToFinish(), () => finalize(isError, error));
        else finalize(isError, error);
    };
    if (signal !== undefined) {
        abortAlgorithm = () => {
            const error = signal.reason;
            const actions = [];
            if (!preventAbort) actions.push(() => dest.state === 'writable' ? wsAbort(dest, error) : resolved(undefined));
            if (!preventCancel) actions.push(() => source.state === 'readable' ? rsCancel(source, error) : resolved(undefined));
            shutdownWithAction(() => waitAll(actions.map(a => a())), true, error);
        };
        if (signal.aborted) { abortAlgorithm(); return done.p; }
        abortSignalAdd(signal, abortAlgorithm);
    }
    const pipeStep = () => {
        if (shuttingDown) return resolved(true);
        return react(writer.ready.p, () => {
            const c = cap();
            defaultReaderRead(reader, {
                /* the write waits a microtask: an enqueue() must not run the sink's write synchronously */
                chunk: chunk => micro(() => { currentWrite = react(writerWrite(writer, chunk), undefined, noop); capResolve(c, false); }),
                close: () => capResolve(c, true),
                error: e => capReject(c, e),
            });
            return c.p;
        });
    };
    const pipeLoop = () => {
        const c = cap();
        const next = isDone => { if (isDone) capResolve(c, undefined); else upon(pipeStep(), next, e => capReject(c, e)); };
        next(false);
        return c.p;
    };
    /* errors must be propagated forward */
    if (source.state === 'errored') { const e = source.err; if (!preventAbort) shutdownWithAction(() => wsAbort(dest, e), true, e); else shutdown(true, e); }
    else uponR(reader.closed.p, e => { if (!preventAbort) shutdownWithAction(() => wsAbort(dest, e), true, e); else shutdown(true, e); });
    /* errors must be propagated backward */
    if (dest.state === 'errored') { const e = dest.err; if (!preventCancel) shutdownWithAction(() => rsCancel(source, e), true, e); else shutdown(true, e); }
    else uponR(writer.closed.p, e => { if (!preventCancel) shutdownWithAction(() => rsCancel(source, e), true, e); else shutdown(true, e); });
    /* closing must be propagated forward */
    if (source.state === 'closed') { if (!preventClose) shutdownWithAction(() => writerCloseWithErrorPropagation(writer)); else shutdown(); }
    else uponF(reader.closed.p, () => { if (!preventClose) shutdownWithAction(() => writerCloseWithErrorPropagation(writer)); else shutdown(); });
    /* closing must be propagated backward */
    if (wsCloseQueuedOrInFlight(dest) || dest.state === 'closed') {
        const e = new TypeError('the destination writable stream closed before all data could be piped to it');
        if (!preventCancel) shutdownWithAction(() => rsCancel(source, e), true, e); else shutdown(true, e);
    }
    handled(pipeLoop());
    return done.p;
};

/* from(asyncIterable) */
const getAsyncIterator = obj => {
    if (!isObj(obj)) throw new TypeError("Failed to execute 'from' on 'ReadableStream': The provided value is not of type 'object'.");
    let method = obj == null ? undefined : obj[Symbol.asyncIterator];
    if (method === undefined || method === null) {
        const syncMethod = obj == null ? undefined : obj[Symbol.iterator];
        if (syncMethod === undefined || syncMethod === null) throw new TypeError("Failed to execute 'from' on 'ReadableStream': The provided value is not iterable.");
        if (typeof syncMethod !== 'function') throw new TypeError("Failed to execute 'from' on 'ReadableStream': @@iterator is not a function.");
        const it = Reflect.apply(syncMethod, obj, []);
        if (!isObj(it)) throw new TypeError("Failed to execute 'from' on 'ReadableStream': The iterator is not an object.");
        const next = it.next;
        /* CreateAsyncFromSyncIterator */
        const unwrap = (r) => {
            if (!isObj(r)) throw new TypeError('The iterator result is not an object.');
            const done = !!r.done, value = r.value;
            return react(resolved(value), v => ({ value: v, done }));
        };
        return {
            iterator: {
                return(v) {
                    let m;
                    try { m = it.return; } catch (e) { return rejected(e); }
                    if (m === undefined || m === null) return resolved({ value: v, done: true });
                    let r;
                    try { r = Reflect.apply(m, it, [v]); } catch (e) { return rejected(e); }
                    if (!isObj(r)) return rejected(new TypeError('The iterator result is not an object.'));
                    try { return unwrap(r); } catch (e) { return rejected(e); }
                },
            },
            next: () => { let r; try { r = Reflect.apply(next, it, []); return unwrap(r); } catch (e) { return rejected(e); } },
        };
    }
    if (typeof method !== 'function') throw new TypeError("Failed to execute 'from' on 'ReadableStream': @@asyncIterator is not a function.");
    const it = Reflect.apply(method, obj, []);
    if (!isObj(it)) throw new TypeError("Failed to execute 'from' on 'ReadableStream': The iterator is not an object.");
    const next = it.next;
    return { iterator: it, next: () => Reflect.apply(next, it, []) };
};
const rsFromIterable = obj => {
    const rec = getAsyncIterator(obj);
    let s;
    const pull = () => {
        let nextResult;
        try { nextResult = rec.next(); } catch (e) { return rejected(e); }
        return react(resolved(nextResult), r => {
            if (!isObj(r)) throw new TypeError('The promise returned by the iterator.next() method must fulfill with an object');
            if (r.done) rsdcClose(s.ctl);
            else rsdcEnqueue(s.ctl, r.value);
        });
    };
    const cancel = reason => {
        const it = rec.iterator;
        let m;
        try { m = it.return; } catch (e) { return rejected(e); }
        if (m === undefined || m === null) return resolved(undefined);
        let r;
        try { r = Reflect.apply(m, it, [reason]); } catch (e) { return rejected(e); }
        return react(resolved(r), x => { if (!isObj(x)) throw new TypeError('The promise returned by the iterator.return() method must fulfill with an object'); });
    };
    s = createRS(noop, pull, cancel, 0);
    return s;
};

/* async iteration */
const AsyncIteratorPrototype = Object.getPrototypeOf(Object.getPrototypeOf(async function* () {}).prototype);
const iterResult = (value, done) => ({ value, done });
let rsaiIs, rsaiState;
class RSAsyncIterator {
    #st;
    constructor(k, st) { if (k !== INTERNAL) illegal(); this.#st = st; }
    static { rsaiIs = o => isObj(o) && #st in o; rsaiState = o => o.#st; }
    next() {
        if (!rsaiIs(this)) return rejected(new TypeError("Failed to execute 'next' on 'ReadableStream AsyncIterator': Illegal invocation"));
        const st = rsaiState(this);
        const nextSteps = () => {
            if (st.finished) return resolved(iterResult(undefined, true));
            const c = cap(), reader = st.reader;
            defaultReaderRead(reader, {
                chunk: v => capResolve(c, v),
                close: () => { defaultReaderRelease(reader); capResolve(c, END); },
                error: e => { defaultReaderRelease(reader); capReject(c, e); },
            });
            return react(c.p, v => { st.ongoing = null; if (v === END) { st.finished = true; return iterResult(undefined, true); } return iterResult(v, false); },
                         e => { st.ongoing = null; st.finished = true; throw e; });
        };
        st.ongoing = st.ongoing ? react(st.ongoing, nextSteps, nextSteps) : nextSteps();
        return st.ongoing;
    }
    return(value) {
        if (!rsaiIs(this)) return rejected(new TypeError("Failed to execute 'return' on 'ReadableStream AsyncIterator': Illegal invocation"));
        const st = rsaiState(this);
        const returnSteps = () => {
            if (st.finished) return resolved(iterResult(value, true));
            st.finished = true;
            const reader = st.reader;
            if (!st.preventCancel) { const r = readerGenericCancel(reader, value); defaultReaderRelease(reader); return r; }
            defaultReaderRelease(reader);
            return resolved(undefined);
        };
        st.ongoing = st.ongoing ? react(st.ongoing, returnSteps, returnSteps) : returnSteps();
        return react(st.ongoing, () => iterResult(value, true));
    }
}
delete RSAsyncIterator.prototype.constructor;
Object.setPrototypeOf(RSAsyncIterator.prototype, AsyncIteratorPrototype);
Object.defineProperty(RSAsyncIterator.prototype, Symbol.toStringTag, { value: 'ReadableStream AsyncIterator', configurable: true });

class ReadableStream {
    #r;
    constructor(underlyingSource = undefined, strategy = undefined) {
        if (underlyingSource === INTERNAL) { this.#r = strategy; strategy.pub = this; return; }
        const what = "Failed to construct 'ReadableStream'";
        if (underlyingSource !== undefined && !isObj(underlyingSource)) throw new TypeError(`${what}: The provided value is not of type 'object'.`);
        const st = toStrategy(strategy, what);
        const src = underlyingSource === undefined ? null : underlyingSource, d = { __proto__: null };
        if (src !== null) {
            const a = src.autoAllocateChunkSize;
            if (a !== undefined) d.autoAllocateChunkSize = enforceULL(a, `${what}: Failed to read the 'autoAllocateChunkSize' property from 'UnderlyingSource'`);
            d.cancel = fnMember(src, 'cancel', what, 'UnderlyingSource');
            d.pull = fnMember(src, 'pull', what, 'UnderlyingSource');
            d.start = fnMember(src, 'start', what, 'UnderlyingSource');
            const t = src.type;
            if (t !== undefined) {
                const ts = `${t}`;
                if (ts !== 'bytes') throw new TypeError(`${what}: Failed to read the 'type' property from 'UnderlyingSource': The provided value '${ts}' is not a valid enum value of type ReadableStreamType.`);
                d.type = ts;
            }
        }
        const r = this.#r = newRS(); r.pub = this;
        if (d.type === 'bytes') {
            if (st.size !== undefined) throw new RangeError(`${what}: The strategy for a byte stream cannot have a size function.`);
            setUpRBSCFromSource(r, src, d, extractHWM(st, 0));
        } else {
            const size = extractSize(st);
            setUpRSDCFromSource(r, src, d, extractHWM(st, 1), size);
        }
    }
    static { rsOf = o => o.#r; rsIs = o => isObj(o) && #r in o; }
    static from(asyncIterable) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'from' on 'ReadableStream': 1 argument required, but only 0 present.");
        return rsFromIterable(asyncIterable).pub;
    }
    get locked() { return isLocked(this.#r); }
    cancel(reason = undefined) {
        if (!rsIs(this)) return rejected(new TypeError("Failed to execute 'cancel' on 'ReadableStream': Illegal invocation"));
        const s = this.#r;
        if (isLocked(s)) return rejected(new TypeError("Failed to execute 'cancel' on 'ReadableStream': Cannot cancel a locked stream"));
        return rsCancel(s, reason);
    }
    getReader(options = undefined) {
        const s = this.#r;
        const d = dictOf(options, "Failed to execute 'getReader' on 'ReadableStream'", 'ReadableStreamGetReaderOptions');
        let mode;
        if (d) {
            const m = d.mode;
            if (m !== undefined) {
                mode = `${m}`;
                if (mode !== 'byob') throw new TypeError(`Failed to execute 'getReader' on 'ReadableStream': Failed to read the 'mode' property from 'ReadableStreamGetReaderOptions': The provided value '${mode}' is not a valid enum value of type ReadableStreamReaderMode.`);
            }
        }
        return (mode === undefined ? acquireDefaultReader(s) : acquireBYOBReader(s)).pub;
    }
    pipeThrough(transform, options = undefined) {
        const s = this.#r, what = "Failed to execute 'pipeThrough' on 'ReadableStream'";
        if (arguments.length < 1) throw new TypeError(`${what}: 1 argument required, but only 0 present.`);
        if (!isObj(transform)) throw new TypeError(`${what}: parameter 1 is not of type 'ReadableWritablePair'.`);
        const readable = transform.readable;
        if (readable === undefined) throw new TypeError(`${what}: Failed to read the 'readable' property from 'ReadableWritablePair': Required member is undefined.`);
        if (!rsIs(readable)) throw new TypeError(`${what}: Failed to read the 'readable' property from 'ReadableWritablePair': Failed to convert value to 'ReadableStream'.`);
        const writable = transform.writable;
        if (writable === undefined) throw new TypeError(`${what}: Failed to read the 'writable' property from 'ReadableWritablePair': Required member is undefined.`);
        if (!wsIs(writable)) throw new TypeError(`${what}: Failed to read the 'writable' property from 'ReadableWritablePair': Failed to convert value to 'WritableStream'.`);
        const o = toPipeOptions(options, what);
        if (isLocked(s)) throw new TypeError(`${what}: Cannot pipe a locked stream`);
        const w = wsOf(writable);
        if (wsLocked(w)) throw new TypeError(`${what}: Cannot pipe to a locked stream`);
        handled(rsPipeTo(s, w, o.preventClose, o.preventAbort, o.preventCancel, o.signal));
        return readable;
    }
    pipeTo(destination, options = undefined) {
        const what = "Failed to execute 'pipeTo' on 'ReadableStream'";
        if (!rsIs(this)) return rejected(new TypeError(`${what}: Illegal invocation`));
        const s = this.#r;
        if (arguments.length < 1) return rejected(new TypeError(`${what}: 1 argument required, but only 0 present.`));
        if (!wsIs(destination)) return rejected(new TypeError(`${what}: parameter 1 is not of type 'WritableStream'.`));
        let o;
        try { o = toPipeOptions(options, what); } catch (e) { return rejected(e); }
        if (isLocked(s)) return rejected(new TypeError(`${what}: Cannot pipe a locked stream`));
        const w = wsOf(destination);
        if (wsLocked(w)) return rejected(new TypeError(`${what}: Cannot pipe to a locked stream`));
        return rsPipeTo(s, w, o.preventClose, o.preventAbort, o.preventCancel, o.signal);
    }
    tee() { return rsTee(this.#r).map(b => b.pub); }
    values(options = undefined) {
        const s = this.#r;
        const d = dictOf(options, "Failed to execute 'values' on 'ReadableStream'", 'ReadableStreamIteratorOptions');
        const preventCancel = d ? !!d.preventCancel : false;
        const reader = acquireDefaultReader(s);
        return new RSAsyncIterator(INTERNAL, { reader, preventCancel, ongoing: null, finished: false });
    }
}
Object.defineProperty(ReadableStream.prototype, Symbol.asyncIterator, { value: ReadableStream.prototype.values, writable: true, configurable: true });

class ReadableStreamDefaultReader {
    #r;
    constructor(stream, rec = undefined) {
        if (stream === INTERNAL) { this.#r = rec; rec.pub = this; return; }
        if (arguments.length < 1) throw new TypeError("Failed to construct 'ReadableStreamDefaultReader': 1 argument required, but only 0 present.");
        if (!rsIs(stream)) throw new TypeError("Failed to construct 'ReadableStreamDefaultReader': parameter 1 is not of type 'ReadableStream'.");
        const r = this.#r = newReader(false); r.pub = this;
        setUpDefaultReader(r, rsOf(stream));
    }
    static { readerOf = o => o.#r; readerIs = o => isObj(o) && #r in o; }
    get closed() {
        if (!readerIs(this)) return rejected(new TypeError("Failed to read the 'closed' property from 'ReadableStreamDefaultReader': Illegal invocation"));
        return this.#r.closed.p;
    }
    cancel(reason = undefined) {
        if (!readerIs(this)) return rejected(new TypeError("Failed to execute 'cancel' on 'ReadableStreamDefaultReader': Illegal invocation"));
        const r = this.#r;
        if (r.s === undefined) return rejected(new TypeError("Failed to execute 'cancel' on 'ReadableStreamDefaultReader': This readable stream reader has been released and cannot be used to cancel its previous owner stream"));
        return readerGenericCancel(r, reason);
    }
    read() {
        if (!readerIs(this)) return rejected(new TypeError("Failed to execute 'read' on 'ReadableStreamDefaultReader': Illegal invocation"));
        const r = this.#r;
        if (r.s === undefined) return rejected(new TypeError("Failed to execute 'read' on 'ReadableStreamDefaultReader': This readable stream reader has been released and cannot be used to read from its previous owner stream"));
        const c = cap();
        defaultReaderRead(r, {
            chunk: v => capResolve(c, { value: v, done: false }),
            close: () => capResolve(c, { value: undefined, done: true }),
            error: e => capReject(c, e),
        });
        return c.p;
    }
    releaseLock() { const r = this.#r; if (r.s === undefined) return; defaultReaderRelease(r); }
}
class ReadableStreamBYOBReader {
    #r;
    constructor(stream, rec = undefined) {
        if (stream === INTERNAL) { this.#r = rec; rec.pub = this; return; }
        if (arguments.length < 1) throw new TypeError("Failed to construct 'ReadableStreamBYOBReader': 1 argument required, but only 0 present.");
        if (!rsIs(stream)) throw new TypeError("Failed to construct 'ReadableStreamBYOBReader': parameter 1 is not of type 'ReadableStream'.");
        const r = this.#r = newReader(true); r.pub = this;
        setUpBYOBReader(r, rsOf(stream));
    }
    static { byobReaderIs = o => isObj(o) && #r in o; }
    get closed() {
        if (!byobReaderIs(this)) return rejected(new TypeError("Failed to read the 'closed' property from 'ReadableStreamBYOBReader': Illegal invocation"));
        return this.#r.closed.p;
    }
    cancel(reason = undefined) {
        if (!byobReaderIs(this)) return rejected(new TypeError("Failed to execute 'cancel' on 'ReadableStreamBYOBReader': Illegal invocation"));
        const r = this.#r;
        if (r.s === undefined) return rejected(new TypeError("Failed to execute 'cancel' on 'ReadableStreamBYOBReader': This readable stream reader has been released and cannot be used to cancel its previous owner stream"));
        return readerGenericCancel(r, reason);
    }
    read(view, options = undefined) {
        const what = "Failed to execute 'read' on 'ReadableStreamBYOBReader'";
        if (!byobReaderIs(this)) return rejected(new TypeError(`${what}: Illegal invocation`));
        const r = this.#r;
        if (arguments.length < 1) return rejected(new TypeError(`${what}: 1 argument required, but only 0 present.`));
        let p, min = 1;
        try {
            p = toView(view, what);
            const d = dictOf(options, what, 'ReadableStreamBYOBReaderReadOptions');
            if (d) { const m = d.min; if (m !== undefined) min = enforceULL(m, `${what}: Failed to read the 'min' property from 'ReadableStreamBYOBReaderReadOptions'`); }
        } catch (e) { return rejected(e); }
        if (p.byteLength === 0) return rejected(new TypeError(`${what}: This readable stream reader cannot be used to read as the view has byte length equal to 0`));
        if (bufLen(p.buffer) === 0) return rejected(new TypeError(`${what}: This readable stream reader cannot be used to read as the viewed array buffer is 0-byte length`));
        if (isDetached(p.buffer)) return rejected(new TypeError(`${what}: This readable stream reader cannot be used to read as the viewed array buffer is detached`));
        if (min === 0) return rejected(new TypeError(`${what}: options.min must be greater than 0`));
        if (p.ta) { if (min > p.length) return rejected(new RangeError(`${what}: options.min must be less than or equal to view's length`)); }
        else if (min > p.byteLength) return rejected(new RangeError(`${what}: options.min must be less than or equal to view's byteLength`));
        if (r.s === undefined) return rejected(new TypeError(`${what}: This readable stream reader has been released and cannot be used to read from its previous owner stream`));
        const c = cap();
        byobReaderRead(r, view, min, {
            chunk: v => capResolve(c, { value: v, done: false }),
            close: v => capResolve(c, { value: v, done: true }),
            error: e => capReject(c, e),
        });
        return c.p;
    }
    releaseLock() { const r = this.#r; if (r.s === undefined) return; byobReaderRelease(r); }
}
class ReadableStreamDefaultController {
    #r;
    constructor(k = undefined, rec = undefined) { if (k !== INTERNAL) illegal(); this.#r = rec; rec.pub = this; }
    get desiredSize() { return rsdcDesiredSize(this.#r); }
    close() {
        const c = this.#r;
        if (!rsdcCanCloseOrEnqueue(c)) throw new TypeError("Failed to execute 'close' on 'ReadableStreamDefaultController': Cannot close a readable stream that has already been requested to be closed or is not readable");
        rsdcClose(c);
    }
    enqueue(chunk = undefined) {
        const c = this.#r;
        if (!rsdcCanCloseOrEnqueue(c)) throw new TypeError("Failed to execute 'enqueue' on 'ReadableStreamDefaultController': Cannot enqueue a chunk into a readable stream that is closed or has been requested to be closed");
        rsdcEnqueue(c, chunk);
    }
    error(e = undefined) { rsdcError(this.#r, e); }
}
class ReadableByteStreamController {
    #r;
    constructor(k = undefined, rec = undefined) { if (k !== INTERNAL) illegal(); this.#r = rec; rec.pub = this; }
    get byobRequest() { const r = rbscGetBYOBRequest(this.#r); return r === null ? null : r.pub; }
    get desiredSize() { return rbscDesiredSize(this.#r); }
    close() {
        const c = this.#r, what = "Failed to execute 'close' on 'ReadableByteStreamController'";
        if (c.closeReq) throw new TypeError(`${what}: The stream has already been closed; do not close it again!`);
        if (c.s.state !== 'readable') throw new TypeError(`${what}: The stream (in ${c.s.state} state) is not in the readable state and cannot be closed`);
        rbscClose(c);
    }
    enqueue(chunk) {
        const c = this.#r, what = "Failed to execute 'enqueue' on 'ReadableByteStreamController'";
        if (arguments.length < 1) throw new TypeError(`${what}: 1 argument required, but only 0 present.`);
        const p = toView(chunk, what);
        if (p.byteLength === 0) throw new TypeError(`${what}: chunk must have non-zero byteLength`);
        if (bufLen(p.buffer) === 0) throw new TypeError(`${what}: chunk's buffer must have non-zero byteLength`);
        if (c.closeReq) throw new TypeError(`${what}: stream is closed or draining`);
        if (c.s.state !== 'readable') throw new TypeError(`${what}: The stream (in ${c.s.state} state) is not in the readable state and cannot be enqueued to`);
        rbscEnqueue(c, chunk, p);
    }
    error(e = undefined) { rbscError(this.#r, e); }
}
class ReadableStreamBYOBRequest {
    #r;
    constructor(k = undefined, rec = undefined) { if (k !== INTERNAL) illegal(); this.#r = rec; rec.pub = this; }
    get view() { return this.#r.view; }
    respond(bytesWritten) {
        const r = this.#r, what = "Failed to execute 'respond' on 'ReadableStreamBYOBRequest'";
        if (arguments.length < 1) throw new TypeError(`${what}: 1 argument required, but only 0 present.`);
        const n = enforceULL(bytesWritten, what);
        if (r.ctl === undefined) throw new TypeError(`${what}: This BYOB request has been invalidated`);
        if (isDetached(viewParts(r.view).buffer)) throw new TypeError(`${what}: The BYOB request's buffer has been detached and so cannot be used as a response`);
        rbscRespond(r.ctl, n);
    }
    respondWithNewView(view) {
        const r = this.#r, what = "Failed to execute 'respondWithNewView' on 'ReadableStreamBYOBRequest'";
        if (arguments.length < 1) throw new TypeError(`${what}: 1 argument required, but only 0 present.`);
        const p = toView(view, what);
        if (r.ctl === undefined) throw new TypeError(`${what}: This BYOB request has been invalidated`);
        if (isDetached(p.buffer)) throw new TypeError(`${what}: The given view's buffer has been detached and so cannot be used as a response`);
        rbscRespondWithNewView(r.ctl, view, p);
    }
}

/* ===== WritableStream ===== */
const newWS = () => ({ state: 'writable', err: undefined, writer: undefined, ctl: undefined, inFlightWrite: undefined, closeReq: undefined,
                       inFlightClose: undefined, pendingAbort: undefined, writeReqs: new Q(), backpressure: false, pub: null });
const wsLocked = s => s.writer !== undefined;
const createWS = (start, write, close, abort, hwm = 1, size = one) => {
    const s = newWS(); new WritableStream(INTERNAL, s);
    const c = newWSDC();
    setUpWSDC(s, c, start, write, close, abort, hwm, size);
    return s;
};
const wsAbort = (s, reason) => {
    if (s.state === 'closed' || s.state === 'errored') return resolved(undefined);
    abortSignalRun(s.ctl.signal, reason);
    const state = s.state;
    if (state === 'closed' || state === 'errored') return resolved(undefined);
    if (s.pendingAbort !== undefined) return s.pendingAbort.c.p;
    let wasAlreadyErroring = false;
    if (state === 'erroring') { wasAlreadyErroring = true; reason = undefined; }
    const c = cap();
    s.pendingAbort = { c, reason, wasAlreadyErroring };
    if (!wasAlreadyErroring) wsStartErroring(s, reason);
    return c.p;
};
const wsClose = s => {
    const state = s.state;
    if (state === 'closed' || state === 'errored') return rejected(new TypeError(`The stream (in ${state} state) is not in the writable state and cannot be closed`));
    const c = cap();
    s.closeReq = c;
    const w = s.writer;
    if (w !== undefined && s.backpressure && state === 'writable') capResolve(w.ready, undefined);
    wsdcClose(s.ctl);
    return c.p;
};
const wsAddWriteRequest = s => { const c = cap(); s.writeReqs.push(c); return c.p; };
const wsDealWithRejection = (s, e) => { if (s.state === 'writable') { wsStartErroring(s, e); return; } wsFinishErroring(s); };
const wsStartErroring = (s, reason) => {
    const c = s.ctl;
    s.state = 'erroring'; s.err = reason;
    const w = s.writer;
    if (w !== undefined) writerEnsureReadyPromiseRejected(w, reason);
    if (!wsHasOperationMarkedInFlight(s) && c.started) wsFinishErroring(s);
};
const wsFinishErroring = s => {
    s.state = 'errored';
    s.ctl.errorSteps();
    const e = s.err;
    for (const r of s.writeReqs.take()) capReject(r, e);
    s.writeReqs = new Q();
    if (s.pendingAbort === undefined) { wsRejectCloseAndClosedPromiseIfNeeded(s); return; }
    const ar = s.pendingAbort; s.pendingAbort = undefined;
    if (ar.wasAlreadyErroring) { capReject(ar.c, e); wsRejectCloseAndClosedPromiseIfNeeded(s); return; }
    const p = s.ctl.abortSteps(ar.reason);
    upon(p, () => { capResolve(ar.c, undefined); wsRejectCloseAndClosedPromiseIfNeeded(s); },
            r => { capReject(ar.c, r); wsRejectCloseAndClosedPromiseIfNeeded(s); });
};
const wsFinishInFlightWrite = s => { capResolve(s.inFlightWrite, undefined); s.inFlightWrite = undefined; };
const wsFinishInFlightWriteWithError = (s, e) => { capReject(s.inFlightWrite, e); s.inFlightWrite = undefined; wsDealWithRejection(s, e); };
const wsFinishInFlightClose = s => {
    capResolve(s.inFlightClose, undefined); s.inFlightClose = undefined;
    if (s.state === 'erroring') {
        s.err = undefined;
        if (s.pendingAbort !== undefined) { capResolve(s.pendingAbort.c, undefined); s.pendingAbort = undefined; }
    }
    s.state = 'closed';
    const w = s.writer;
    if (w !== undefined) capResolve(w.closed, undefined);
};
const wsFinishInFlightCloseWithError = (s, e) => {
    capReject(s.inFlightClose, e); s.inFlightClose = undefined;
    if (s.pendingAbort !== undefined) { capReject(s.pendingAbort.c, e); s.pendingAbort = undefined; }
    wsDealWithRejection(s, e);
};
const wsCloseQueuedOrInFlight = s => s.closeReq !== undefined || s.inFlightClose !== undefined;
const wsHasOperationMarkedInFlight = s => s.inFlightWrite !== undefined || s.inFlightClose !== undefined;
const wsMarkCloseRequestInFlight = s => { s.inFlightClose = s.closeReq; s.closeReq = undefined; };
const wsMarkFirstWriteRequestInFlight = s => { s.inFlightWrite = s.writeReqs.shift(); };
const wsRejectCloseAndClosedPromiseIfNeeded = s => {
    if (s.closeReq !== undefined) { capReject(s.closeReq, s.err); s.closeReq = undefined; }
    const w = s.writer;
    if (w !== undefined) { capReject(w.closed, s.err); handled(w.closed.p); }
};
const wsUpdateBackpressure = (s, bp) => {
    const w = s.writer;
    if (w !== undefined && bp !== s.backpressure) {
        if (bp) w.ready = cap();
        else capResolve(w.ready, undefined);
    }
    s.backpressure = bp;
};

/* writer */
const newWriter = () => ({ s: undefined, ready: null, closed: null, pub: null });
const acquireWriter = s => { const w = newWriter(); new WritableStreamDefaultWriter(INTERNAL, w); setUpWriter(w, s); return w; };
const setUpWriter = (w, s) => {
    if (wsLocked(s)) throw new TypeError("Failed to construct 'WritableStreamDefaultWriter': Cannot create writer when WritableStream is locked");
    w.s = s; s.writer = w;
    const state = s.state;
    if (state === 'writable') {
        w.ready = !wsCloseQueuedOrInFlight(s) && s.backpressure ? cap() : capResolved(undefined);
        w.closed = cap();
    } else if (state === 'erroring') {
        w.ready = capRejected(s.err);
        w.closed = cap();
    } else if (state === 'closed') {
        w.ready = capResolved(undefined);
        w.closed = capResolved(undefined);
    } else {
        w.ready = capRejected(s.err);
        w.closed = capRejected(s.err);
    }
};
const writerAbort = (w, reason) => wsAbort(w.s, reason);
const writerClose = w => wsClose(w.s);
const writerCloseWithErrorPropagation = w => {
    const s = w.s, state = s.state;
    if (wsCloseQueuedOrInFlight(s) || state === 'closed') return resolved(undefined);
    if (state === 'errored') return rejected(s.err);
    return writerClose(w);
};
const writerEnsureClosedPromiseRejected = (w, e) => {
    if (w.closed.st === 0) capReject(w.closed, e); else w.closed = capRejected(e);
    handled(w.closed.p);
};
const writerEnsureReadyPromiseRejected = (w, e) => {
    if (w.ready.st === 0) capReject(w.ready, e); else w.ready = capRejected(e);
    handled(w.ready.p);
};
const writerDesiredSize = w => {
    const s = w.s, state = s.state;
    if (state === 'errored' || state === 'erroring') return null;
    if (state === 'closed') return 0;
    return wsdcDesiredSize(s.ctl);
};
const writerRelease = w => {
    const s = w.s;
    const e = new TypeError('Writer was released and can no longer be used to monitor the stream\'s closedness');
    writerEnsureReadyPromiseRejected(w, e);
    writerEnsureClosedPromiseRejected(w, e);
    s.writer = undefined; w.s = undefined;
};
const writerWrite = (w, chunk) => {
    const s = w.s, c = s.ctl;
    const chunkSize = wsdcGetChunkSize(c, chunk);
    if (s !== w.s) return rejected(new TypeError('Writer was released'));
    const state = s.state;
    if (state === 'errored') return rejected(s.err);
    if (wsCloseQueuedOrInFlight(s) || state === 'closed') return rejected(new TypeError('The stream is closing or closed and cannot be written to'));
    if (state === 'erroring') return rejected(s.err);
    const p = wsAddWriteRequest(s);
    wsdcWrite(c, chunk, chunkSize);
    return p;
};

/* writable controller */
const CLOSE_SENTINEL = Symbol('close sentinel');
const newWSDC = () => {
    const c = { s: undefined, q: null, qSize: 0, signal: undefined, started: false, hwm: 1, size: undefined,
                write: undefined, close: undefined, abort: undefined, pub: null };
    c.abortSteps = reason => { const r = c.abort(reason); wsdcClearAlgorithms(c); return r; };
    c.errorSteps = () => { resetQueue(c); };
    new WritableStreamDefaultController(INTERNAL, c);
    return c;
};
const setUpWSDC = (s, c, start, write, close, abort, hwm, size) => {
    c.s = s; s.ctl = c;
    resetQueue(c);
    c.signal = new AbortController().signal;
    c.started = false; c.size = size; c.hwm = hwm; c.write = write; c.close = close; c.abort = abort;
    wsUpdateBackpressure(s, wsdcGetBackpressure(c));
    const startResult = start();
    upon(resolved(startResult), () => { c.started = true; wsdcAdvanceQueueIfNeeded(c); },
                                r => { c.started = true; wsDealWithRejection(s, r); });
};
const setUpWSDCFromSink = (s, sink, d, hwm, size) => {
    const c = newWSDC();
    let start = noop, write = () => resolved(undefined), close = () => resolved(undefined), abort = () => resolved(undefined);
    if (d.start) start = () => Reflect.apply(d.start, sink, [c.pub]);
    if (d.write) write = chunk => promiseCall(d.write, sink, [chunk, c.pub]);
    if (d.close) close = () => promiseCall(d.close, sink, []);
    if (d.abort) abort = reason => promiseCall(d.abort, sink, [reason]);
    setUpWSDC(s, c, start, write, close, abort, hwm, size);
};
const wsdcAdvanceQueueIfNeeded = c => {
    const s = c.s;
    if (!c.started || s.inFlightWrite !== undefined) return;
    if (s.state === 'erroring') { wsFinishErroring(s); return; }
    if (c.q.length === 0) return;
    const value = c.q.peek().value;
    if (value === CLOSE_SENTINEL) wsdcProcessClose(c);
    else wsdcProcessWrite(c, value);
};
const wsdcClearAlgorithms = c => { c.write = c.close = c.abort = c.size = undefined; };
const wsdcClose = c => { enqueueValueWithSize(c, CLOSE_SENTINEL, 0); wsdcAdvanceQueueIfNeeded(c); };
const wsdcGetChunkSize = (c, chunk) => {
    if (c.size === undefined) return 1;
    try { return c.size(chunk); } catch (e) { wsdcErrorIfNeeded(c, e); return 1; }
};
const wsdcDesiredSize = c => c.hwm - c.qSize;
const wsdcWrite = (c, chunk, chunkSize) => {
    try { enqueueValueWithSize(c, chunk, chunkSize); } catch (e) { wsdcErrorIfNeeded(c, e); return; }
    const s = c.s;
    if (!wsCloseQueuedOrInFlight(s) && s.state === 'writable') wsUpdateBackpressure(s, wsdcGetBackpressure(c));
    wsdcAdvanceQueueIfNeeded(c);
};
const wsdcErrorIfNeeded = (c, e) => { if (c.s.state === 'writable') wsdcError(c, e); };
const wsdcProcessClose = c => {
    const s = c.s;
    wsMarkCloseRequestInFlight(s);
    dequeueValue(c);
    const p = c.close();
    wsdcClearAlgorithms(c);
    upon(p, () => { wsFinishInFlightClose(s); }, r => { wsFinishInFlightCloseWithError(s, r); });
};
const wsdcProcessWrite = (c, chunk) => {
    const s = c.s;
    wsMarkFirstWriteRequestInFlight(s);
    upon(c.write(chunk), () => {
        wsFinishInFlightWrite(s);
        const state = s.state;
        dequeueValue(c);
        if (!wsCloseQueuedOrInFlight(s) && state === 'writable') wsUpdateBackpressure(s, wsdcGetBackpressure(c));
        wsdcAdvanceQueueIfNeeded(c);
    }, r => {
        if (s.state === 'writable') wsdcClearAlgorithms(c);
        wsFinishInFlightWriteWithError(s, r);
    });
};
const wsdcGetBackpressure = c => wsdcDesiredSize(c) <= 0;
const wsdcError = (c, e) => { wsdcClearAlgorithms(c); wsStartErroring(c.s, e); };

class WritableStream {
    #r;
    constructor(underlyingSink = undefined, strategy = undefined) {
        if (underlyingSink === INTERNAL) { this.#r = strategy; strategy.pub = this; return; }
        const what = "Failed to construct 'WritableStream'";
        if (underlyingSink !== undefined && !isObj(underlyingSink)) throw new TypeError(`${what}: The provided value is not of type 'object'.`);
        const st = toStrategy(strategy, what);
        const sink = underlyingSink === undefined ? null : underlyingSink, d = { __proto__: null };
        if (sink !== null) {
            d.abort = fnMember(sink, 'abort', what, 'UnderlyingSink');
            d.close = fnMember(sink, 'close', what, 'UnderlyingSink');
            d.start = fnMember(sink, 'start', what, 'UnderlyingSink');
            d.type = sink.type;
            d.write = fnMember(sink, 'write', what, 'UnderlyingSink');
        }
        if (d.type !== undefined) throw new RangeError(`${what}: Invalid type is specified`);
        const s = this.#r = newWS(); s.pub = this;
        const size = extractSize(st), hwm = extractHWM(st, 1);
        setUpWSDCFromSink(s, sink, d, hwm, size);
    }
    static { wsOf = o => o.#r; wsIs = o => isObj(o) && #r in o; }
    get locked() { return wsLocked(this.#r); }
    abort(reason = undefined) {
        if (!wsIs(this)) return rejected(new TypeError("Failed to execute 'abort' on 'WritableStream': Illegal invocation"));
        const s = this.#r;
        if (wsLocked(s)) return rejected(new TypeError("Failed to execute 'abort' on 'WritableStream': Cannot abort a locked stream"));
        return wsAbort(s, reason);
    }
    close() {
        if (!wsIs(this)) return rejected(new TypeError("Failed to execute 'close' on 'WritableStream': Illegal invocation"));
        const s = this.#r;
        if (wsLocked(s)) return rejected(new TypeError("Failed to execute 'close' on 'WritableStream': Cannot close a locked stream"));
        if (wsCloseQueuedOrInFlight(s)) return rejected(new TypeError("Failed to execute 'close' on 'WritableStream': Cannot close an already-closing stream"));
        return wsClose(s);
    }
    getWriter() { return acquireWriter(this.#r).pub; }
}
let writerIs;
class WritableStreamDefaultWriter {
    #r;
    constructor(stream, rec = undefined) {
        if (stream === INTERNAL) { this.#r = rec; rec.pub = this; return; }
        if (arguments.length < 1) throw new TypeError("Failed to construct 'WritableStreamDefaultWriter': 1 argument required, but only 0 present.");
        if (!wsIs(stream)) throw new TypeError("Failed to construct 'WritableStreamDefaultWriter': parameter 1 is not of type 'WritableStream'.");
        const w = this.#r = newWriter(); w.pub = this;
        setUpWriter(w, wsOf(stream));
    }
    static { writerIs = o => isObj(o) && #r in o; }
    get closed() {
        if (!writerIs(this)) return rejected(new TypeError("Failed to read the 'closed' property from 'WritableStreamDefaultWriter': Illegal invocation"));
        return this.#r.closed.p;
    }
    get desiredSize() {
        const w = this.#r;
        if (w.s === undefined) throw new TypeError("Failed to read the 'desiredSize' property from 'WritableStreamDefaultWriter': This writable stream writer has been released and cannot be used to get the desired size");
        return writerDesiredSize(w);
    }
    get ready() {
        if (!writerIs(this)) return rejected(new TypeError("Failed to read the 'ready' property from 'WritableStreamDefaultWriter': Illegal invocation"));
        return this.#r.ready.p;
    }
    abort(reason = undefined) {
        if (!writerIs(this)) return rejected(new TypeError("Failed to execute 'abort' on 'WritableStreamDefaultWriter': Illegal invocation"));
        const w = this.#r;
        if (w.s === undefined) return rejected(new TypeError("Failed to execute 'abort' on 'WritableStreamDefaultWriter': This writable stream writer has been released and cannot be used to abort"));
        return writerAbort(w, reason);
    }
    close() {
        if (!writerIs(this)) return rejected(new TypeError("Failed to execute 'close' on 'WritableStreamDefaultWriter': Illegal invocation"));
        const w = this.#r, s = w.s;
        if (s === undefined) return rejected(new TypeError("Failed to execute 'close' on 'WritableStreamDefaultWriter': This writable stream writer has been released and cannot be used to close"));
        if (wsCloseQueuedOrInFlight(s)) return rejected(new TypeError("Failed to execute 'close' on 'WritableStreamDefaultWriter': Cannot close an already-closing stream"));
        return writerClose(w);
    }
    releaseLock() { const w = this.#r; if (w.s === undefined) return; writerRelease(w); }
    write(chunk = undefined) {
        if (!writerIs(this)) return rejected(new TypeError("Failed to execute 'write' on 'WritableStreamDefaultWriter': Illegal invocation"));
        const w = this.#r;
        if (w.s === undefined) return rejected(new TypeError("Failed to execute 'write' on 'WritableStreamDefaultWriter': This writable stream writer has been released and cannot be used to write"));
        return writerWrite(w, chunk);
    }
}
class WritableStreamDefaultController {
    #r;
    constructor(k = undefined, rec = undefined) { if (k !== INTERNAL) illegal(); this.#r = rec; rec.pub = this; }
    get signal() { return this.#r.signal; }
    error(e = undefined) { const c = this.#r; if (c.s.state !== 'writable') return; wsdcError(c, e); }
}

/* ===== TransformStream ===== */
const tsInitialize = (t, startP, wHWM, wSize, rHWM, rSize) => {
    const start = () => startP;
    t.writable = createWS(start, chunk => tsSinkWrite(t, chunk), () => tsSinkClose(t), reason => tsSinkAbort(t, reason), wHWM, wSize);
    t.readable = createRS(start, () => tsSourcePull(t), reason => tsSourceCancel(t, reason), rHWM, rSize);
    t.backpressure = undefined; t.bpChange = undefined;
    tsSetBackpressure(t, true);
    t.ctl = undefined;
};
const tsError = (t, e) => { rsdcError(t.readable.ctl, e); tsErrorWritableAndUnblockWrite(t, e); };
const tsErrorWritableAndUnblockWrite = (t, e) => { tsdcClearAlgorithms(t.ctl); wsdcErrorIfNeeded(t.writable.ctl, e); tsUnblockWrite(t); };
const tsUnblockWrite = t => { if (t.backpressure) tsSetBackpressure(t, false); };
const tsSetBackpressure = (t, bp) => {
    if (t.bpChange !== undefined) capResolve(t.bpChange, undefined);
    t.bpChange = cap();
    t.backpressure = bp;
};
const newTSDC = () => { const c = { s: undefined, transform: undefined, flush: undefined, cancel: undefined, finish: undefined, pub: null }; new TransformStreamDefaultController(INTERNAL, c); return c; };
const setUpTSDC = (t, c, transform, flush, cancel) => { c.s = t; t.ctl = c; c.transform = transform; c.flush = flush; c.cancel = cancel; };
const setUpTSDCFromTransformer = (t, tr, d) => {
    const c = newTSDC();
    let transform = chunk => { try { tsdcEnqueue(c, chunk); return resolved(undefined); } catch (e) { return rejected(e); } };
    let flush = () => resolved(undefined), cancel = () => resolved(undefined);
    if (d.transform) transform = chunk => promiseCall(d.transform, tr, [chunk, c.pub]);
    if (d.flush) flush = () => promiseCall(d.flush, tr, [c.pub]);
    if (d.cancel) cancel = reason => promiseCall(d.cancel, tr, [reason]);
    setUpTSDC(t, c, transform, flush, cancel);
};
const tsdcClearAlgorithms = c => { c.transform = c.flush = c.cancel = undefined; };
const tsdcEnqueue = (c, chunk) => {
    const t = c.s, rc = t.readable.ctl;
    if (!rsdcCanCloseOrEnqueue(rc)) throw new TypeError('Readable side is not in a state that permits enqueue');
    try { rsdcEnqueue(rc, chunk); } catch (e) { tsErrorWritableAndUnblockWrite(t, e); throw t.readable.err; }
    const bp = rsdcHasBackpressure(rc);
    if (bp !== t.backpressure) tsSetBackpressure(t, true);
};
const tsdcError = (c, e) => { tsError(c.s, e); };
const tsdcPerformTransform = (c, chunk) => react(c.transform(chunk), undefined, r => { tsError(c.s, r); throw r; });
const tsdcTerminate = c => {
    const t = c.s;
    rsdcClose(t.readable.ctl);
    tsErrorWritableAndUnblockWrite(t, new TypeError('TransformStream terminated'));
};
const tsSinkWrite = (t, chunk) => {
    const c = t.ctl;
    if (t.backpressure) {
        return react(t.bpChange.p, () => {
            const w = t.writable;
            if (w.state === 'erroring') throw w.err;
            return tsdcPerformTransform(c, chunk);
        });
    }
    return tsdcPerformTransform(c, chunk);
};
const tsSinkAbort = (t, reason) => {
    const c = t.ctl;
    if (c.finish !== undefined) return c.finish.p;
    const r = t.readable;
    c.finish = cap();
    const p = c.cancel(reason);
    tsdcClearAlgorithms(c);
    upon(p, () => {
        if (r.state === 'errored') capReject(c.finish, r.err);
        else { rsdcError(r.ctl, reason); capResolve(c.finish, undefined); }
    }, e => { rsdcError(r.ctl, e); capReject(c.finish, e); });
    return c.finish.p;
};
const tsSinkClose = t => {
    const c = t.ctl;
    if (c.finish !== undefined) return c.finish.p;
    const r = t.readable;
    c.finish = cap();
    const p = c.flush();
    tsdcClearAlgorithms(c);
    upon(p, () => {
        if (r.state === 'errored') capReject(c.finish, r.err);
        else { rsdcClose(r.ctl); capResolve(c.finish, undefined); }
    }, e => { rsdcError(r.ctl, e); capReject(c.finish, e); });
    return c.finish.p;
};
const tsSourceCancel = (t, reason) => {
    const c = t.ctl;
    if (c.finish !== undefined) return c.finish.p;
    const w = t.writable;
    c.finish = cap();
    const p = c.cancel(reason);
    tsdcClearAlgorithms(c);
    upon(p, () => {
        if (w.state === 'errored') capReject(c.finish, w.err);
        else { wsdcErrorIfNeeded(w.ctl, reason); tsUnblockWrite(t); capResolve(c.finish, undefined); }
    }, e => { wsdcErrorIfNeeded(w.ctl, e); tsUnblockWrite(t); capReject(c.finish, e); });
    return c.finish.p;
};
const tsSourcePull = t => { tsSetBackpressure(t, false); return t.bpChange.p; };

class TransformStream {
    #r;
    constructor(transformer = undefined, writableStrategy = undefined, readableStrategy = undefined) {
        const what = "Failed to construct 'TransformStream'";
        if (transformer !== undefined && !isObj(transformer)) throw new TypeError(`${what}: The provided value is not of type 'object'.`);
        const ws = toStrategy(writableStrategy, what), rs = toStrategy(readableStrategy, what);
        const tr = transformer === undefined ? null : transformer, d = { __proto__: null };
        if (tr !== null) {
            d.cancel = fnMember(tr, 'cancel', what, 'Transformer');
            d.flush = fnMember(tr, 'flush', what, 'Transformer');
            d.readableType = tr.readableType;
            d.start = fnMember(tr, 'start', what, 'Transformer');
            d.transform = fnMember(tr, 'transform', what, 'Transformer');
            d.writableType = tr.writableType;
        }
        if (d.readableType !== undefined) throw new RangeError(`${what}: Invalid readableType specified`);
        if (d.writableType !== undefined) throw new RangeError(`${what}: Invalid writableType specified`);
        const rHWM = extractHWM(rs, 0), rSize = extractSize(rs), wHWM = extractHWM(ws, 1), wSize = extractSize(ws);
        const t = this.#r = { readable: undefined, writable: undefined, ctl: undefined, backpressure: undefined, bpChange: undefined, pub: this };
        const startP = cap();
        tsInitialize(t, startP.p, wHWM, wSize, rHWM, rSize);
        setUpTSDCFromTransformer(t, tr, d);
        if (d.start) capResolve(startP, Reflect.apply(d.start, tr, [t.ctl.pub]));
        else capResolve(startP, undefined);
    }
    get readable() { return this.#r.readable.pub; }
    get writable() { return this.#r.writable.pub; }
}
class TransformStreamDefaultController {
    #r;
    constructor(k = undefined, rec = undefined) { if (k !== INTERNAL) illegal(); this.#r = rec; rec.pub = this; }
    get desiredSize() { return rsdcDesiredSize(this.#r.s.readable.ctl); }
    enqueue(chunk = undefined) { tsdcEnqueue(this.#r, chunk); }
    error(reason = undefined) { tsdcError(this.#r, reason); }
    terminate() { tsdcTerminate(this.#r); }
}

/* ===== queuing strategies ===== */
const strategyInit = (init, what) => {
    const d = dictOf(init, what, 'QueuingStrategyInit');
    const h = d ? d.highWaterMark : undefined;
    if (h === undefined) throw new TypeError(`${what}: Failed to read the 'highWaterMark' property from 'QueuingStrategyInit': Required member is undefined.`);
    return +h;
};
const byteLengthSize = ({ size(chunk) { return chunk.byteLength; } }).size;
const countSize = ({ size() { return 1; } }).size;
class ByteLengthQueuingStrategy {
    #hwm;
    constructor(init) {
        if (arguments.length < 1) throw new TypeError("Failed to construct 'ByteLengthQueuingStrategy': 1 argument required, but only 0 present.");
        this.#hwm = strategyInit(init, "Failed to construct 'ByteLengthQueuingStrategy'");
    }
    get highWaterMark() { return this.#hwm; }
    get size() { this.#hwm; return byteLengthSize; }
}
class CountQueuingStrategy {
    #hwm;
    constructor(init) {
        if (arguments.length < 1) throw new TypeError("Failed to construct 'CountQueuingStrategy': 1 argument required, but only 0 present.");
        this.#hwm = strategyInit(init, "Failed to construct 'CountQueuingStrategy'");
    }
    get highWaterMark() { return this.#hwm; }
    get size() { this.#hwm; return countSize; }
}

/* WebIDL shape: members enumerable, a class string per interface */
const classes = { ReadableStream, ReadableStreamDefaultReader, ReadableStreamBYOBReader, ReadableStreamDefaultController, ReadableByteStreamController,
                  ReadableStreamBYOBRequest, WritableStream, WritableStreamDefaultWriter, WritableStreamDefaultController, TransformStream,
                  TransformStreamDefaultController, ByteLengthQueuingStrategy, CountQueuingStrategy };
for (const [name, C] of Object.entries(classes)) {
    for (const o of [C.prototype, C]) for (const k of Object.getOwnPropertyNames(o)) {
        if (k === 'constructor' || k === 'prototype' || k === 'length' || k === 'name') continue;
        const d = Object.getOwnPropertyDescriptor(o, k);
        if (!d.enumerable) { d.enumerable = true; Object.defineProperty(o, k, d); }
    }
    Object.defineProperty(C.prototype, Symbol.toStringTag, { value: name, configurable: true });
}
for (const k of ['next', 'return']) { const d = Object.getOwnPropertyDescriptor(RSAsyncIterator.prototype, k); d.enumerable = true; Object.defineProperty(RSAsyncIterator.prototype, k, d); }

const streamDisturbed = s => rsIs(s) && rsOf(s).disturbed;
// Internal consumers must not invoke an author-overridden public tee method.
const streamTee = (s, cloneForBranch2) => {
    const branches = rsTee(rsOf(s), cloneForBranch2);
    return [branches[0].pub, branches[1].pub];
};
return { ...classes, streamDisturbed, streamTee };
})();
