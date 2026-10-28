/* Shared stream runtime for window and dedicated workers. Encoding streams
 * retain only an unfinished UTF scalar and use transform backpressure. */
function createWebStreamInterfaces(privateState, platform) {
const { TextEncoder, TextDecoder, AbortController, DOMException } = platform;
const def = (object, key, value) => Object.defineProperty(object, key, { value, writable: true, configurable: true, enumerable: false });
const streamCtl = (s) => ({
    enqueue(x) { if (s._done || s._errored) throw new TypeError('Cannot enqueue a chunk into a closed stream'); s._q.push(x); s._wake(); },
    close() { if (s._done || s._errored) throw new TypeError('Cannot close this stream'); s._done = true; s._wake(); }, error(e) { if (!s._done && !s._errored) { s._errored = true; s._err = e; s._q.length = 0; s._wake(); } },
    get desiredSize() { return s._errored ? null : s._done ? 0 : s._hwm - s._q.length; },
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
        def(this, '_q', []); def(this, '_done', false); def(this, '_err', undefined); def(this, '_errored', false); def(this, '_waits', []); def(this, '_reader', null); def(this, '_pulling', false); def(this, '_started', false);
        def(this, '_hwm', strategy && strategy.highWaterMark != null ? strategy.highWaterMark : 1); def(this, '_src', src);
        const c = streamCtl(this); def(this, '_c', c);
        let r; try { r = src.start ? src.start.call(src, c) : undefined; } catch (e) { this._errored = true; this._err = e; }
        Promise.resolve(r).then(() => { this._started = true; this._flush(); }, e => c.error(e));
    }
    _wake() { this._flush(); }
    _settle() { const rd = this._reader; if (!rd) return; if (this._errored) rd._cr[1](this._err); else if (this._done && !this._q.length) rd._cr[0](undefined); }
    _flush() {
        while (this._waits.length && this._q.length) this._waits.shift()[0]({ value: this._q.shift(), done: false });
        if (this._waits.length && this._errored) for (const [, j] of this._waits.splice(0)) j(this._err);
        else if (this._waits.length && this._done) for (const [r] of this._waits.splice(0)) r({ value: undefined, done: true });
        this._settle();
        if (this._started && !this._pulling && !this._done && !this._errored && this._src.pull && (this._waits.length || this._q.length < this._hwm)) {
            this._pulling = true;
            let p; try { p = this._src.pull.call(this._src, this._c); } catch (e) { p = Promise.reject(e); }
            Promise.resolve(p).then(() => { this._pulling = false; if (this._waits.length) this._flush(); }, e => { this._pulling = false; this._c.error(e); });
        }
    }
    _cancel(r) { if (this._errored) return Promise.reject(this._err); if (this._done && !this._q.length) return Promise.resolve(); this._q.length = 0; this._done = true; this._flush(); try { return Promise.resolve(this._src.cancel ? this._src.cancel.call(this._src, r) : undefined).then(() => {}); } catch (e) { return Promise.reject(e); } }
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
    constructor(transformer = {}, writableStrategy = {}, readableStrategy = {}) {
        transformer = transformer == null ? {} : transformer;
        if (typeof transformer !== 'object' && typeof transformer !== 'function') throw new TypeError('Transformer must be an object');
        const algorithms = {};
        for (const name of ['start', 'transform', 'flush', 'cancel']) {
            const method = transformer[name];
            if (method !== undefined && typeof method !== 'function') throw new TypeError('Transformer ' + name + ' must be callable');
            algorithms[name] = method;
        }
        let readableController, writableController, pulling, pressure = true;
        let ready = writableDeferred(true), errored = false, failure, cancelFinish;
        const finishPull = () => { if (pulling) { const resolve = pulling; pulling = null; resolve(); } };
        const setPressure = value => {
            if (pressure === value) return;
            pressure = value;
            if (value) { ready = writableDeferred(true); finishPull(); }
            else ready.resolve();
        };
        const errorBoth = reason => {
            if (errored) return;
            errored = true; failure = reason;
            readableController.error(reason);
            if (writableController) writableController.error(reason);
            setPressure(false); finishPull();
        };
        const cancel = (reason, source) => {
            if (cancelFinish) return cancelFinish.promise;
            cancelFinish = writableDeferred(true);
            let result;
            try { result = algorithms.cancel ? algorithms.cancel.call(transformer, reason) : undefined; }
            catch (error) { result = Promise.reject(error); }
            Promise.resolve(result).then(() => {
                if (source === 'readable' && writable._state === 'errored') { cancelFinish.reject(writable._err); return; }
                if (source === 'writable' && readable._errored) { cancelFinish.reject(readable._err); return; }
                errorBoth(reason); cancelFinish.resolve();
            }, error => { errorBoth(error); cancelFinish.reject(error); });
            return cancelFinish.promise;
        };
        // A pending pull remains asleep until output or termination. This
        // avoids a microtask polling loop while a decoder awaits more bytes.
        const readable = new ReadableStream({
            start(controller) { readableController = controller; },
            pull() {
                setPressure(false);
                return new Promise(resolve => { pulling = resolve; });
            },
            cancel(reason) { return cancel(reason, 'readable'); },
        }, Object.assign({ highWaterMark: 0 }, readableStrategy));
        const controller = {
            enqueue(chunk) {
                if (errored) throw failure;
                try { readableController.enqueue(chunk); }
                catch (error) { errorBoth(error); throw error; }
                if (readableController.desiredSize <= 0) setPressure(true);
            },
            error: errorBoth,
            terminate() {
                readableController.close();
                const error = new TypeError('Transform stream was terminated');
                errored = true; failure = error;
                if (writableController) writableController.error(error);
                setPressure(false); finishPull();
            },
            get desiredSize() { return readableController.desiredSize; },
        };
        const performTransform = chunk => {
            if (errored) return Promise.reject(failure);
            let result;
            try { result = algorithms.transform ? algorithms.transform.call(transformer, chunk, controller) : controller.enqueue(chunk); }
            catch (error) { errorBoth(error); return Promise.reject(error); }
            return Promise.resolve(result).catch(error => { errorBoth(error); throw error; });
        };
        const writable = new WritableStream({
            start(sinkController) {
                writableController = sinkController;
                // Only wake the blocked write here. Its original stored abort
                // reason can be undefined, unlike AbortSignal's default reason.
                sinkController.signal.addEventListener('abort', () => setPressure(false), { once: true });
                if (errored) sinkController.error(failure);
                const result = algorithms.start ? algorithms.start.call(transformer, controller) : undefined;
                return Promise.resolve(result).catch(error => { errorBoth(error); throw error; });
            },
            write(chunk) {
                if (!pressure) return performTransform(chunk);
                return ready.promise.then(() => {
                    if (writable._state === 'erroring' || writable._state === 'errored') throw writable._err;
                    return performTransform(chunk);
                });
            },
            close() {
                if (errored) return Promise.reject(failure);
                let result;
                try { result = algorithms.flush ? algorithms.flush.call(transformer, controller) : undefined; }
                catch (error) { errorBoth(error); throw error; }
                return Promise.resolve(result).then(() => {
                    if (errored) throw failure;
                    readableController.close(); finishPull();
                }, error => { errorBoth(error); throw error; });
            },
            abort(reason) { return cancel(reason, 'writable'); },
        }, writableStrategy);
        def(this, '_r', readable); def(this, '_w', writable);
    }
    get readable() { return this._r; }
    get writable() { return this._w; }
}
class ByteLengthQueuingStrategy { constructor(o) { this.highWaterMark = o.highWaterMark; } size(c) { return c.byteLength; } }
class CountQueuingStrategy { constructor(o) { this.highWaterMark = o.highWaterMark; } size() { return 1; } }

const localEncoderStreams = new WeakMap(), localDecoderStreams = new WeakMap();
const local = kind => kind === 8 ? localEncoderStreams : localDecoderStreams;
const put = privateState ? (kind, value, state) => privateState.put(kind, value, state) : (kind, value, state) => local(kind).set(value, state);
const get = privateState ? (kind, value) => privateState.get(kind, value) : (kind, value) => local(kind).get(value);
const stateOf = (kind, value) => {
    const state = get(kind, value);
    if (state === undefined) throw new TypeError('Invalid encoding stream receiver');
    return state;
};
const call = Function.prototype.call;
const encode = call.bind(TextEncoder.prototype.encode);
const decode = call.bind(TextDecoder.prototype.decode);
const decoderEncoding = call.bind(Object.getOwnPropertyDescriptor(TextDecoder.prototype, 'encoding').get);
const decoderFatal = call.bind(Object.getOwnPropertyDescriptor(TextDecoder.prototype, 'fatal').get);
const decoderIgnoreBOM = call.bind(Object.getOwnPropertyDescriptor(TextDecoder.prototype, 'ignoreBOM').get);
const charCodeAt = call.bind(String.prototype.charCodeAt);
class TextEncoderStream {
    constructor() {
        const encoder = new TextEncoder();
        let pending = '';
        const stream = new TransformStream({
            transform(chunk, controller) {
                if (typeof chunk === 'symbol') throw new TypeError('Cannot convert a Symbol to a string');
                let value = pending + String(chunk); pending = '';
                const last = value.length ? charCodeAt(value, value.length - 1) : 0;
                if (last >= 0xD800 && last <= 0xDBFF) { pending = value.slice(-1); value = value.slice(0, -1); }
                if (value.length) controller.enqueue(encode(encoder, value));
            },
            flush(controller) { if (pending) { controller.enqueue(encode(encoder, pending)); pending = ''; } },
        });
        put(8, this, { readable: stream.readable, writable: stream.writable });
    }
    get encoding() { stateOf(8, this); return 'utf-8'; }
    get readable() { return stateOf(8, this).readable; }
    get writable() { return stateOf(8, this).writable; }
}
class TextDecoderStream {
    constructor(label = 'utf-8', options = {}) {
        const decoder = new TextDecoder(label, options);
        const encoding = decoderEncoding(decoder);
        // Single-byte encodings have no cross-chunk decoder state. Keeping
        // their native fatal path also avoids weakening fatal error handling.
        const incremental = ['utf-8', 'utf-16le', 'utf-16be', 'gbk', 'gb18030',
            'big5', 'euc-jp', 'iso-2022-jp', 'shift_jis', 'euc-kr'].includes(encoding);
        const stream = new TransformStream({
            transform(chunk, controller) {
                // Unlike optional TextDecoder.decode(input), a stream chunk
                // must be an actual AllowSharedBufferSource.
                if (chunk === undefined) throw new TypeError('Decoder chunks must be a BufferSource');
                const value = decode(decoder, chunk, { stream: incremental });
                if (value.length) controller.enqueue(value);
            },
            flush(controller) { const value = decode(decoder); if (value.length) controller.enqueue(value); },
        });
        put(9, this, { readable: stream.readable, writable: stream.writable,
            encoding, fatal: decoderFatal(decoder), ignoreBOM: decoderIgnoreBOM(decoder) });
    }
    get encoding() { return stateOf(9, this).encoding; }
    get fatal() { return stateOf(9, this).fatal; }
    get ignoreBOM() { return stateOf(9, this).ignoreBOM; }
    get readable() { return stateOf(9, this).readable; }
    get writable() { return stateOf(9, this).writable; }
}
const interfaces = { ReadableStream, ReadableStreamDefaultReader, WritableStream,
    WritableStreamDefaultWriter, TransformStream, ByteLengthQueuingStrategy,
    CountQueuingStrategy, TextEncoderStream, TextDecoderStream };
for (const [name, Ctor] of Object.entries(interfaces)) {
    Object.defineProperty(Ctor.prototype, Symbol.toStringTag, { value: name, configurable: true });
    for (const key of Object.getOwnPropertyNames(Ctor.prototype)) {
        if (key === 'constructor' || key.startsWith('_')) continue;
        const descriptor = Object.getOwnPropertyDescriptor(Ctor.prototype, key);
        descriptor.enumerable = true; Object.defineProperty(Ctor.prototype, key, descriptor);
    }
}
return interfaces;
}
