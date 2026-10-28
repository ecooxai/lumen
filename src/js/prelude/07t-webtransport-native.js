// Real HTTP/3 I/O lives behind N. This facade never substitutes an echo shim.
// See docs/webtransport-native.md for the deliberately incomplete API profile.
const nativeWebTransportGlobals = (() => {
    if (typeof N.wtNativeAvailable !== 'function' || !N.wtNativeAvailable()) return null;
    const stateOf = new WeakMap(), errorOf = new WeakMap(), duplexOf = new WeakMap(), pairOf = new WeakMap();
    const secret = Symbol('native WebTransport'), utf8 = new TextEncoder();
    const documentBudget = {copiedBytes:0};
    const OP = Object.freeze({datagramWrite:1, datagramRead:2, createBi:3, createUni:4, acceptBi:5, acceptUni:6, read:7, write:8, finish:9, cancelRead:10, abortWrite:11, close:12, stats:13, cancelDatagrams:14, cancelIncomingBi:15, cancelIncomingUni:16});
    const CHUNK = 65536, WRITE_LIMIT = 4 * 1024 * 1024;
    const exception = (name, message) => new DOMException(message || name, name);
    const unsupported = message => exception('NotSupportedError', message);
    const lookup = (map, target) => { const value = map.get(target); if (!value) throw new TypeError('Illegal invocation'); return value; };
    const dictionary = value => {
        if (value == null) return {};
        if (!['object', 'function'].includes(typeof value)) throw new TypeError('Expected an options dictionary');
        return value;
    };
    const sequence = value => {
        if (value == null || typeof value[Symbol.iterator] !== 'function') throw new TypeError('Expected an iterable sequence');
        return Array.from(value);
    };
    const bytes = value => {
        let view;
        if (value instanceof ArrayBuffer) view = new Uint8Array(value);
        else if (ArrayBuffer.isView(value) && value.buffer instanceof ArrayBuffer) view = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
        else throw new TypeError('Expected a non-shared ArrayBuffer or ArrayBuffer view');
        return view;
    };
    const uint32 = value => Number(value) >>> 0;
    const clamp32 = value => {
        const n = Number(value);
        if (Number.isNaN(n) || n <= 0) return 0;
        if (n >= 4294967295) return 4294967295;
        const floor = Math.floor(n), half = n - floor;
        return half < .5 ? floor : half > .5 ? floor + 1 : floor % 2 === 0 ? floor : floor + 1;
    };
    const deferred = () => {
        const slot = {pending:true};
        slot.promise = new Promise((resolve, reject) => {
            slot.resolve = value => { if (slot.pending) { slot.pending = false; resolve(value); } };
            slot.reject = reason => { if (slot.pending) { slot.pending = false; reject(reason); } };
        });
        // A transport may fail before the page starts observing these promises.
        slot.promise.catch(() => {});
        return slot;
    };

    class WebTransportError extends DOMException {
        constructor(message = '', options = {}) {
            options = dictionary(options);
            const source = options.source === undefined ? 'stream' : String(options.source);
            if (!['stream', 'session'].includes(source)) throw new TypeError('Invalid WebTransportError source');
            super(String(message), 'WebTransportError');
            errorOf.set(this, {source, code:options.streamErrorCode == null ? null : clamp32(options.streamErrorCode)});
        }
        get source() { return lookup(errorOf, this).source; }
        get streamErrorCode() { return lookup(errorOf, this).code; }
    }
    const streamCode = reason => errorOf.has(reason) && reason.streamErrorCode !== null ? reason.streamErrorCode : 0;
    const eventError = (event, source = 'session') => {
        if (Number.isInteger(event.streamErrorCode) && event.streamErrorCode >= 0 && event.streamErrorCode <= 4294967295) return new WebTransportError('The peer reset this stream.', {source:'stream', streamErrorCode:event.streamErrorCode});
        if (event.name === 'TypeError') return new TypeError('The native transport rejected the operation.');
        if (event.name === 'RangeError') return new RangeError('The payload exceeds the negotiated datagram size.');
        if (['SecurityError', 'QuotaExceededError', 'InvalidStateError', 'NotSupportedError', 'AbortError', 'TimeoutError', 'OperationError'].includes(event.name)) return exception(event.name, event.name === 'SecurityError' ? 'Native TLS certificate verification rejected the peer.' : 'The native transport operation could not complete.');
        return new WebTransportError('The native transport connection or stream failed.', {source});
    };
    const optionsFor = (address, value) => {
        const options = dictionary(value);
        let url;
        try { url = new URL(String(address)); } catch { throw exception('SyntaxError', 'WebTransport requires an absolute HTTPS URL.'); }
        if (url.protocol !== 'https:' || url.username || url.password || url.href.includes('#')) throw exception('SyntaxError', 'WebTransport requires HTTPS without credentials or a fragment.');
        if (utf8.encode(url.href).byteLength > 4096) throw new TypeError('The WebTransport URL exceeds its supported length.');
        if (options.allowPooling) throw unsupported('Connection pooling is not implemented.');
        if (options.congestionControl !== undefined && String(options.congestionControl) !== 'default') throw unsupported('Only the native default congestion policy is implemented.');
        if (options.protocols !== undefined && sequence(options.protocols).length) throw unsupported('Application protocol negotiation is not implemented.');
        if (options.headers !== undefined && Array.from(new Headers(options.headers)).length) throw unsupported('Custom WebTransport request headers are not implemented.');
        if (options.datagramsReadableType !== undefined) throw unsupported('WebTransport BYOB byte streams are not implemented.');
        for (const name of ['anticipatedConcurrentIncomingUnidirectionalStreams', 'anticipatedConcurrentIncomingBidirectionalStreams']) if (options[name] != null) throw unsupported('Custom incoming stream credit is not implemented.');
        const result = {url:url.href, requireUnreliable:!!options.requireUnreliable};
        if (options.serverCertificateHashes !== undefined) {
            const entries = sequence(options.serverCertificateHashes);
            if (entries.length > 8) throw new TypeError('At most eight certificate hashes are supported.');
            if (entries.length) result.certificateHashes = entries.map(entry => {
                entry = dictionary(entry);
                // Never silently replace an unsupported pin with normal-root trust.
                if (String(entry.algorithm).toLowerCase() !== 'sha-256') throw unsupported('Only SHA-256 certificate hashes are implemented.');
                const value = bytes(entry.value);
                if (value.byteLength !== 32) throw new TypeError('A SHA-256 certificate hash must contain 32 bytes.');
                return Array.from(value);
            });
        }
        return result;
    };
    const streamOptions = value => {
        const options = dictionary(value);
        if (options.sendGroup != null || (options.sendOrder !== undefined && Number(options.sendOrder) !== 0)) throw unsupported('Stream send groups and priorities are not implemented.');
        return options.waitUntilAvailable ? 1 : 0;
    };
    const requireReady = state => {
        if (state.status !== 'ready') throw exception('InvalidStateError', 'The WebTransport session is not open.');
    };
    function command(state, operation, stream = 0, code = 0, payload) {
        try {
            requireReady(state);
            if (state.pending.size >= 128 || state.request >= Number.MAX_SAFE_INTEGER) throw exception('QuotaExceededError', 'Too many pending native operations.');
            const id = ++state.request, slot = deferred();
            state.pending.set(id, {slot, operation});
            try { N.wtCommand(state.id, stream, operation, id, code, payload); }
            catch (cause) { state.pending.delete(id); slot.reject(eventError(cause)); }
            return slot.promise;
        } catch (error) { return Promise.reject(error); }
    }
    async function afterReady(state, work) {
        await state.ready.promise;
        requireReady(state);
        return work();
    }
    function unregister(state, item) {
        state.resources.delete(item);
        if (item.signal && item.onAbort) item.signal.removeEventListener('abort', item.onAbort);
    }
    function rejectOperations(state, reason) {
        for (const {slot} of state.pending.values()) slot.reject(reason);
        state.pending.clear();
        for (const item of Array.from(state.resources)) {
            item.stopped = true;
            try { item.controller.error(reason); } catch {}
            unregister(state, item);
        }
    }
    function nativeEvent(state, event) {
        if (!event || event.sessionId !== state.id || state.status === 'closed' || state.status === 'failed') return;
        if (event.type === 'ready') {
            if (state.status !== 'connecting') return;
            if (event.alpn !== 'h3' || !Number.isInteger(event.maxDatagramSize) || event.maxDatagramSize < 0 || !['supports-unreliable', 'reliable-only'].includes(event.reliability)) {
                closeTransport(state, {closeCode:0, reason:''}, exception('OperationError', 'Invalid native handshake metadata.'));
                return;
            }
            state.status = 'ready'; state.maxDatagramSize = event.maxDatagramSize; state.reliability = event.reliability;
            state.ready.resolve(); return;
        }
        if (event.type === 'closed') {
            const failure = state.closeFailure || (!event.clean || !event.localCloseRequested ? eventError({name:event.errorName}, 'session') : null);
            state.status = failure ? 'failed' : 'closed';
            const reason = failure || exception('AbortError', 'The WebTransport session was closed.');
            state.ready.reject(reason); rejectOperations(state, reason);
            if (failure) state.closed.reject(failure);
            else state.closed.resolve({closeCode:uint32(event.closeCode), reason:String(event.reason || '')});
            return;
        }
        if (!['result', 'error', 'stats'].includes(event.type) || !Number.isSafeInteger(event.requestId)) return;
        const pending = state.pending.get(event.requestId);
        if (!pending) return;
        state.pending.delete(event.requestId);
        if (event.type === 'error') pending.slot.reject(eventError(event, 'stream'));
        else if (event.type === 'result' && event.operation !== pending.operation) {
            const error = exception('OperationError', 'Native operation result did not match its request.');
            pending.slot.reject(error); closeTransport(state, {closeCode:0, reason:''}, error);
        } else pending.slot.resolve(event);
    }
    function closeTransport(state, info, failure = null) {
        if (['closing', 'closed', 'failed'].includes(state.status)) return;
        if (state.status === 'connecting' && !failure) failure = new WebTransportError('The session was closed before it connected.', {source:'session'});
        state.closeFailure = failure;
        state.status = 'closing';
        const reason = failure || exception('AbortError', 'The WebTransport session is closing.');
        state.ready.reject(reason); rejectOperations(state, reason);
        try { N.wtCommand(state.id, 0, OP.close, 0, uint32(info.closeCode), utf8.encode(info.reason).buffer); }
        catch (cause) {
            state.status = 'failed';
            state.closed.reject(eventError(cause));
        }
    }
    function readable(state, operation, stream = 0, cancelOperation = OP.cancelRead, convert) {
        const item = {stopped:false, controller:null};
        const source = {
            start(controller) { item.controller = controller; state.resources.add(item); },
            async pull(controller) {
                try {
                    await state.ready.promise;
                    if (item.stopped) return;
                    const result = await command(state, operation, stream);
                    if (item.stopped) {
                        if (convert) await discardIncoming(state, result);
                        return;
                    }
                    if (result.done) { item.stopped = true; unregister(state, item); controller.close(); }
                    else if (convert) controller.enqueue(convert(result));
                    else {
                        if (!(result.data instanceof ArrayBuffer)) throw exception('OperationError', 'A native read did not provide a binary payload.');
                        controller.enqueue(new Uint8Array(result.data));
                    }
                } catch (error) {
                    if (!item.stopped) { item.stopped = true; unregister(state, item); controller.error(error); }
                }
            },
            async cancel(reason) {
                if (item.stopped) return;
                item.stopped = true; unregister(state, item);
                if (state.status === 'ready') await command(state, cancelOperation, stream, cancelOperation === OP.cancelRead ? streamCode(reason) : 0);
            }
        };
        return source;
    }
    function writable(state, operation, stream = 0) {
        const datagram = operation === OP.datagramWrite;
        const item = {stopped:false, controller:null, abortPromise:null, signal:null, onAbort:null};
        const abort = reason => {
            if (item.abortPromise) return item.abortPromise;
            item.stopped = true;
            unregister(state, item);
            item.abortPromise = state.status === 'ready' && !datagram ? command(state, OP.abortWrite, stream, streamCode(reason)) : Promise.resolve();
            item.abortPromise.catch(() => {});
            return item.abortPromise;
        };
        return {
            start(controller) {
                item.controller = controller; state.resources.add(item);
                item.signal = controller.signal;
                // AbortSignal fires while a write is still blocked. Waiting for
                // the sink's later abort() would deadlock on QUIC flow control.
                item.onAbort = () => { abort(controller.signal.reason); };
                if (item.signal) item.signal.addEventListener('abort', item.onAbort, {once:true});
                return state.ready.promise;
            },
            async write(chunk) {
                if (item.stopped) throw exception('AbortError', 'The transport writer is stopped.');
                let reservation = 0;
                try {
                    const view = bytes(chunk);
                    if (view.byteLength > WRITE_LIMIT - documentBudget.copiedBytes) throw exception('QuotaExceededError', 'The document transport copy budget is 4MiB.');
                    if (datagram && (view.byteLength > state.maxDatagramSize || view.byteLength > CHUNK)) throw new RangeError('The datagram exceeds the negotiated maximum size.');
                    reservation = view.byteLength; documentBudget.copiedBytes += reservation;
                    // Snapshot the exact view before any async I/O. Reserve
                    // across all session streams, not independently per write.
                    const payload = view.slice();
                    if (datagram) await command(state, operation, stream, 0, payload.buffer);
                    else for (let offset = 0; offset < payload.byteLength; offset += CHUNK) {
                        if (item.stopped) throw exception('AbortError', 'The transport writer is stopped.');
                        await command(state, operation, stream, 0, payload.slice(offset, offset + CHUNK).buffer);
                    }
                } catch (error) {
                    // Erroring the JS sink must also release its native send
                    // half. An already-reset/closed native half cannot replace
                    // the original validation, quota or transport rejection.
                    await abort(error).catch(() => {});
                    throw error;
                } finally { documentBudget.copiedBytes -= reservation; }
            },
            async close() {
                if (item.stopped) throw exception('InvalidStateError', 'The transport writer is stopped.');
                try { if (!datagram) await command(state, OP.finish, stream); }
                finally { item.stopped = true; unregister(state, item); }
            },
            abort
        };
    }
    const strategy = {highWaterMark:CHUNK, size(chunk) { return bytes(chunk).byteLength; }};
    class WebTransportSendStream extends WritableStream {
        constructor(key, state, stream) {
            if (key !== secret) throw new TypeError('Illegal constructor');
            super(writable(state, OP.write, stream), strategy);
        }
    }
    class WebTransportReceiveStream extends ReadableStream {
        constructor(key, state, stream) {
            if (key !== secret) throw new TypeError('Illegal constructor');
            super(readable(state, OP.read, stream), {highWaterMark:0});
        }
    }
    class WebTransportBidirectionalStream {
        constructor(key, state, stream) {
            if (key !== secret) throw new TypeError('Illegal constructor');
            pairOf.set(this, {readable:new WebTransportReceiveStream(secret, state, stream), writable:new WebTransportSendStream(secret, state, stream)});
        }
        get readable() { return lookup(pairOf, this).readable; }
        get writable() { return lookup(pairOf, this).writable; }
    }
    function createdStream(state, result, direction) {
        if (!Number.isSafeInteger(result.streamId) || result.streamId <= 0 || result.direction !== direction) throw exception('OperationError', 'Invalid native stream result.');
        return direction === 'bidirectional' ? new WebTransportBidirectionalStream(secret, state, result.streamId) : direction === 'send' ? new WebTransportSendStream(secret, state, result.streamId) : new WebTransportReceiveStream(secret, state, result.streamId);
    }
    async function discardIncoming(state, result) {
        if (state.status !== 'ready' || !Number.isSafeInteger(result.streamId)) return;
        const work = [command(state, OP.cancelRead, result.streamId)];
        if (result.direction === 'bidirectional') work.push(command(state, OP.abortWrite, result.streamId));
        await Promise.all(work.map(promise => promise.catch(() => {})));
    }
    class WebTransportDatagramDuplexStream {
        constructor(key, state) {
            if (key !== secret) throw new TypeError('Illegal constructor');
            duplexOf.set(this, {state, readable:new ReadableStream(readable(state, OP.datagramRead, 0, OP.cancelDatagrams), {highWaterMark:0}), writable:new WritableStream(writable(state, OP.datagramWrite), strategy)});
        }
        get readable() { return lookup(duplexOf, this).readable; }
        get writable() { return lookup(duplexOf, this).writable; }
        get maxDatagramSize() { return lookup(duplexOf, this).state.maxDatagramSize; }
    }
    class WebTransport {
        constructor(address, options = {}) {
            if (arguments.length === 0) throw new TypeError('A WebTransport URL is required.');
            const config = optionsFor(address, options);
            const state = {id:0, status:'connecting', request:0, pending:new Map(), resources:new Set(), ready:deferred(), closed:deferred(), maxDatagramSize:0, reliability:'pending', closeFailure:null};
            stateOf.set(this, state);
            try { state.id = N.wtCreate(JSON.stringify(config), event => nativeEvent(state, event)); }
            catch (cause) { stateOf.delete(this); throw eventError(cause); }
            if (!Number.isSafeInteger(state.id) || state.id <= 0) throw exception('OperationError', 'The native transport handle is invalid.');
            state.datagrams = new WebTransportDatagramDuplexStream(secret, state);
            state.incomingBi = new ReadableStream(readable(state, OP.acceptBi, 0, OP.cancelIncomingBi, event => createdStream(state, event, 'bidirectional')), {highWaterMark:0});
            state.incomingUni = new ReadableStream(readable(state, OP.acceptUni, 0, OP.cancelIncomingUni, event => createdStream(state, event, 'receive')), {highWaterMark:0});
        }
        get ready() { return lookup(stateOf, this).ready.promise; }
        get closed() { return lookup(stateOf, this).closed.promise; }
        get datagrams() { return lookup(stateOf, this).datagrams; }
        get incomingBidirectionalStreams() { return lookup(stateOf, this).incomingBi; }
        get incomingUnidirectionalStreams() { return lookup(stateOf, this).incomingUni; }
        get reliability() { return lookup(stateOf, this).reliability; }
        get congestionControl() { lookup(stateOf, this); return 'default'; }
        createBidirectionalStream(options = {}) {
            const state = lookup(stateOf, this);
            let code;
            try { code = streamOptions(options); } catch (error) { return Promise.reject(error); }
            return afterReady(state, async () => createdStream(state, await command(state, OP.createBi, 0, code), 'bidirectional'));
        }
        createUnidirectionalStream(options = {}) {
            const state = lookup(stateOf, this);
            let code;
            try { code = streamOptions(options); } catch (error) { return Promise.reject(error); }
            return afterReady(state, async () => createdStream(state, await command(state, OP.createUni, 0, code), 'send'));
        }
        close(options = {}) {
            const state = lookup(stateOf, this), info = dictionary(options);
            const input = String(info.reason ?? '');
            // Preserve complete Unicode scalars while bounding the wire reason.
            let reason = '', length = 0;
            for (const character of input) {
                const size = utf8.encode(character).byteLength;
                if (length + size > 1024) break;
                reason += character; length += size;
            }
            closeTransport(state, {closeCode:uint32(info.closeCode ?? 0), reason});
        }
    }
    const globals = {WebTransport, WebTransportError, WebTransportSendStream, WebTransportReceiveStream, WebTransportBidirectionalStream, WebTransportDatagramDuplexStream};
    for (const [name, Class] of Object.entries(globals)) Object.defineProperty(Class.prototype, Symbol.toStringTag, {value:name, configurable:true});
    return Object.freeze(globals);
})();
