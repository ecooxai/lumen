// These are deterministic JS/native-boundary fixtures, never network evidence.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const facadePath = process.env.LUME_WT_FACADE_SOURCE || (fs.existsSync(path.join(here, '07t-webtransport-native.js')) ? path.join(here, '07t-webtransport-native.js') : path.join(here, '../src/js/prelude/07t-webtransport-native.js'));
const source = fs.readFileSync(facadePath, 'utf8');
const streamPath = process.env.LUME_STREAM_SOURCE || (fs.existsSync(path.join(here, '../lume-streams-draft/07-web.js')) ? path.join(here, '../lume-streams-draft/07-web.js') : path.join(here, '../src/js/prelude/07-web.js'));
const streamSource = fs.readFileSync(streamPath, 'utf8');
const nativeStreams = streamSource.slice(streamSource.indexOf('const streamCtl ='), streamSource.indexOf('class TransformStream'));
const flush = async () => { for (let i = 0; i < 16; ++i) await Promise.resolve(); };
const named = name => error => error?.name === name;

function fixture(implementation, available = true) {
    let nextId = 1;
    const callbacks = new Map(), creates = [], calls = [];
    const N = {
        wtNativeAvailable: () => available,
        wtCreate(json, callback) { const id = nextId++; creates.push(JSON.parse(json)); callbacks.set(id, callback); return id; },
        wtCommand(sessionId, streamId, operation, requestId, code, data) {
            calls.push({sessionId, streamId, operation, requestId, code, data:data === undefined ? undefined : data.slice(0)});
            if (N.failCommand) throw Object.assign(new Error('fixture failure'), {name:N.failCommand});
            return true;
        }
    };
    const scoped = implementation === 'lume' ? `function def(object, key, value) { Object.defineProperty(object, key, {value, writable:true, configurable:true}); }\n${nativeStreams}\n` : '';
    const globals = new Function('N', `${scoped}${source}\nreturn {globals:nativeWebTransportGlobals, ReadableStream, WritableStream};`)(N);
    const transport = options => new globals.globals.WebTransport('https://127.0.0.1:4433/echo', options);
    const emit = (event, id = 1) => { const callback = callbacks.get(id); callback?.({sessionId:id, ...event}); if (event.type === 'closed') callbacks.delete(id); };
    const ready = (id = 1) => emit({type:'ready', alpn:'h3', maxDatagramSize:1200, reliability:'supports-unreliable'}, id);
    const of = operation => calls.filter(call => call.operation === operation);
    const complete = (call, extra = {}) => emit({type:'result', requestId:call.requestId, operation:call.operation, ...extra}, call.sessionId);
    const failure = (call, name, extra = {}) => emit({type:'error', requestId:call.requestId, name, ...extra}, call.sessionId);
    const next = async (operation, index = 0) => { await flush(); const call = of(operation)[index]; assert.ok(call, `Expected native operation ${operation} at index ${index}`); return call; };
    const close = async object => {
        object.close();
        emit({type:'closed', clean:true, localCloseRequested:true, closeCode:0, reason:''});
        await object.closed;
    };
    const pair = async object => {
        const index = of(3).length;
        const pending = object.createBidirectionalStream();
        const call = await next(3, index);
        complete(call, {streamId:101 + index, direction:'bidirectional'});
        return pending;
    };
    return {N, globals:globals.globals, streams:globals, transport, creates, calls, emit, ready, of, next, complete, failure, close, pair, callbacks};
}

for (const implementation of ['reference', 'lume']) {
    const label = name => `${implementation} streams: ${name}`;
    test(label('disabled backend does not expose a transport constructor'), () => {
        assert.equal(fixture(implementation, false).globals, null);
    });
    test(label('invalid URLs, unsupported options and invalid pins allocate no native session'), () => {
        const h = fixture(implementation), {WebTransport} = h.globals;
        for (const url of ['http://example.test', '/relative', 'https://user:pass@example.test', 'https://example.test/#']) assert.throws(() => new WebTransport(url), named('SyntaxError'));
        for (const options of [{allowPooling:true}, {congestionControl:'low-latency'}, {protocols:['test']}, {headers:{'x-test':'yes'}}, {datagramsReadableType:'bytes'}, {serverCertificateHashes:[{algorithm:'unknown', value:new Uint8Array(32)}]}]) assert.throws(() => h.transport(options), named('NotSupportedError'));
        assert.throws(() => h.transport({serverCertificateHashes:[{algorithm:'sha-256', value:new Uint8Array(31)}]}), named('TypeError'));
        assert.equal(h.creates.length, 0);
    });
    test(label('pins copy the exact view and an empty list uses native roots'), async () => {
        const h = fixture(implementation), hash = Uint8Array.from({length:34}, (_, index) => index);
        const transport = h.transport({serverCertificateHashes:[{algorithm:'SHA-256', value:hash.subarray(1, 33)}]});
        hash.fill(255);
        assert.deepEqual(h.creates[0].certificateHashes[0], Array.from({length:32}, (_, index) => index + 1));
        assert.equal('origin' in h.creates[0], false);
        h.ready(); await transport.ready; await h.close(transport);
        const other = h.transport({serverCertificateHashes:[]});
        assert.equal('certificateHashes' in h.creates[1], false);
        h.ready(2); await other.ready; other.close(); h.emit({type:'closed', clean:true, localCloseRequested:true}, 2); await other.closed;
    });
    test(label('ready waits for a real native handshake and creates no background pulls'), async () => {
        const h = fixture(implementation), transport = h.transport();
        let ready = false; transport.ready.then(() => { ready = true; });
        await flush(); assert.equal(ready, false); assert.equal(h.calls.length, 0);
        assert.equal(transport.reliability, 'pending'); assert.equal(transport.datagrams.maxDatagramSize, 0);
        h.ready(); await transport.ready; await flush();
        assert.equal(transport.reliability, 'supports-unreliable'); assert.equal(transport.datagrams.maxDatagramSize, 1200); assert.equal(h.calls.length, 0);
        await h.close(transport);
    });
    test(label('datagram writes copy view bytes and settle only on native acknowledgement'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const writer = transport.datagrams.writable.getWriter(), payload = new Uint8Array([9, 10, 11, 12]);
        let settled = false; const writing = writer.write(payload.subarray(1, 3)).then(() => { settled = true; });
        const call = await h.next(1); payload.fill(0);
        assert.deepEqual(Array.from(new Uint8Array(call.data)), [10, 11]); assert.equal(settled, false);
        h.complete(call); await writing; assert.equal(settled, true);
        await writer.close(); writer.releaseLock(); await h.close(transport);
    });
    test(label('empty datagrams are data and reads remain demand driven'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const reader = transport.datagrams.readable.getReader();
        const pending = reader.read(), call = await h.next(2);
        h.complete(call, {data:new ArrayBuffer(0), done:false});
        const item = await pending; assert.equal(item.done, false); assert.equal(item.value.byteLength, 0);
        await flush(); assert.equal(h.of(2).length, 1);
        const cancellation = reader.cancel(); h.complete(await h.next(14)); await cancellation;
        reader.releaseLock(); await h.close(transport);
    });
    test(label('oversized datagrams reject without a native send'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const writer = transport.datagrams.writable.getWriter();
        await assert.rejects(writer.write(new Uint8Array(1201)), named('RangeError'));
        assert.equal(h.of(1).length, 0); writer.releaseLock(); await h.close(transport);
    });
    test(label('stream writes enforce byte backpressure and FIN waits for its own completion'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const pair = await h.pair(transport), writer = pair.writable.getWriter();
        assert.ok(pair.writable instanceof h.streams.WritableStream);
        const write = writer.write(new Uint8Array(65537).fill(19));
        const first = await h.next(8); assert.equal(first.data.byteLength, 65536); assert.equal(writer.desiredSize, -1);
        let relieved = false; writer.ready.then(() => { relieved = true; }); await flush(); assert.equal(relieved, false);
        h.complete(first); const second = await h.next(8, 1); assert.equal(second.data.byteLength, 1); h.complete(second);
        await write; await writer.ready; assert.equal(relieved, true);
        let closed = false; const closing = writer.close().then(() => { closed = true; });
        const fin = await h.next(9); assert.equal(closed, false); h.complete(fin); await closing;
        assert.equal(closed, true); writer.releaseLock(); await h.close(transport);
    });
    test(label('a rejected reliable write resets its native send half and preserves the original error'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const pair = await h.pair(transport), writer = pair.writable.getWriter();
        const writing = writer.write(new Uint8Array(4 * 1024 * 1024 + 1)); writing.catch(() => {});
        const reset = await h.next(11);
        assert.equal(reset.streamId, 101); assert.equal(reset.code, 0); assert.equal(h.of(8).length, 0);
        h.failure(reset, 'InvalidStateError');
        await assert.rejects(writing, named('QuotaExceededError'));
        await assert.rejects(writer.write(new Uint8Array([1])), named('QuotaExceededError'));
        assert.equal(h.of(11).length, 1); assert.equal(h.of(8).length, 0);
        const reader = pair.readable.getReader(), reading = reader.read();
        h.complete(await h.next(7), {data:new Uint8Array([9]).buffer, done:false});
        assert.deepEqual(Array.from((await reading).value), [9]);
        const cancelling = reader.cancel(); h.complete(await h.next(10)); await cancelling;
        reader.releaseLock(); writer.releaseLock(); await h.close(transport);
    });
    test(label('reliable reads distinguish partial data from actual EOF'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const pair = await h.pair(transport), reader = pair.readable.getReader();
        const first = reader.read(); h.complete(await h.next(7), {data:new Uint8Array([3, 4]).buffer, done:false});
        assert.deepEqual(Array.from((await first).value), [3, 4]);
        const final = reader.read(); h.complete(await h.next(7, 1), {data:new ArrayBuffer(0), done:true});
        assert.equal((await final).done, true); assert.equal((await reader.read()).done, true);
        assert.equal(h.of(7).length, 2); reader.releaseLock(); await h.close(transport);
    });
    test(label('stream-credit waiting is explicit and default credit checks are immediate'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const immediate = transport.createBidirectionalStream(); immediate.catch(() => {});
        const first = await h.next(3); assert.equal(first.code, 0); h.failure(first, 'QuotaExceededError');
        await assert.rejects(immediate, named('QuotaExceededError'));
        const waiting = transport.createUnidirectionalStream({waitUntilAvailable:true}), next = await h.next(4);
        assert.equal(next.code, 1); h.complete(next, {streamId:500, direction:'send'});
        await waiting; await h.close(transport);
    });
    test(label('aggregate owned write copies across sessions stay within4MiB and release after abort'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const other = h.transport(); h.ready(2); await other.ready;
        const first = (await h.pair(transport)).writable.getWriter();
        const second = (await h.pair(other)).writable.getWriter();
        const third = (await h.pair(other)).writable.getWriter();
        const writing = first.write(new Uint8Array(4 * 1024 * 1024)); writing.catch(() => {});
        const firstChunk = await h.next(8);
        const rejected = second.write(new Uint8Array(1)); rejected.catch(() => {});
        const failedReset = await h.next(11); h.complete(failedReset);
        await assert.rejects(rejected, named('QuotaExceededError'));
        assert.equal(h.of(8).length, 1);
        const aborting = first.abort(), abort = await h.next(11, 1);
        h.failure(firstChunk, 'AbortError'); h.complete(abort);
        await assert.rejects(writing, named('AbortError')); await aborting;
        const after = third.write(new Uint8Array([1])); h.complete(await h.next(8, 1)); await after;
        first.releaseLock(); second.releaseLock(); third.releaseLock(); await h.close(transport);
        other.close(); h.emit({type:'closed', clean:true, localCloseRequested:true}, 2); await other.closed;
    });
    test(label('reader cancel interrupts the native read and forwards its application error code'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const pair = await h.pair(transport), reader = pair.readable.getReader();
        const pending = reader.read(), read = await h.next(7);
        const cancellation = reader.cancel(new h.globals.WebTransportError('fixture', {streamErrorCode:31}));
        const cancel = await h.next(10); assert.equal(cancel.code, 31);
        h.failure(read, 'AbortError'); h.complete(cancel); await cancellation;
        assert.equal((await pending).done, true); reader.releaseLock(); await h.close(transport);
    });
    test(label('writer abort interrupts flow-controlled I/O before sink abort can run'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const pair = await h.pair(transport), writer = pair.writable.getWriter();
        const writing = writer.write(new Uint8Array([1, 2])); writing.catch(() => {});
        const write = await h.next(8), reason = new h.globals.WebTransportError('fixture', {streamErrorCode:37});
        const aborted = writer.abort(reason), abort = await h.next(11);
        assert.equal(abort.code, 37);
        h.failure(write, 'AbortError'); h.complete(abort);
        await assert.rejects(writing, named('AbortError')); await aborted;
        assert.equal(h.of(11).length, 1); writer.releaseLock(); await h.close(transport);
    });
    test(label('canceling an incoming route preserves unrelated datagram I/O'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const incoming = transport.incomingBidirectionalStreams.getReader(), pending = incoming.read(), accept = await h.next(5);
        const cancelled = incoming.cancel(new h.globals.WebTransportError('fixture', {streamErrorCode:7})), cancel = await h.next(15);
        assert.equal(cancel.code, 0);
        h.failure(accept, 'AbortError'); h.complete(cancel); await cancelled; assert.equal((await pending).done, true);
        const writer = transport.datagrams.writable.getWriter(), writing = writer.write(new Uint8Array([7])); h.complete(await h.next(1)); await writing;
        await writer.close(); writer.releaseLock(); incoming.releaseLock(); await h.close(transport);
    });
    test(label('a stream accepted concurrently with cancel is explicitly stopped and reset'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const reader = transport.incomingBidirectionalStreams.getReader(), pending = reader.read(), accept = await h.next(5);
        const cancelled = reader.cancel(), cancel = await h.next(15);
        h.complete(accept, {streamId:77, direction:'bidirectional'}); h.complete(cancel);
        h.complete(await h.next(10)); h.complete(await h.next(11));
        await cancelled; assert.equal((await pending).done, true); reader.releaseLock(); await h.close(transport);
    });
    test(label('actual stream reset codes become WebTransportError values'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const pair = await h.pair(transport), reader = pair.readable.getReader(), reading = reader.read();
        h.failure(await h.next(7), 'NetworkError', {streamErrorCode:1000});
        await assert.rejects(reading, error => error.name === 'WebTransportError' && error.source === 'stream' && error.streamErrorCode === 1000);
        reader.releaseLock(); await h.close(transport);
    });
    test(label('close settles only after actual native terminal cleanup and ignores late events'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const reader = transport.datagrams.readable.getReader(), reading = reader.read(); reading.catch(() => {});
        const read = await h.next(2); let closed = false; transport.closed.then(() => { closed = true; });
        transport.close({closeCode:4294967297, reason:'é'.repeat(600)});
        const close = await h.next(12); assert.equal(close.code, 1); assert.equal(close.data.byteLength, 1024); assert.equal(closed, false);
        await assert.rejects(reading, named('AbortError'));
        h.complete(read, {data:new Uint8Array([9]).buffer, done:false});
        h.emit({type:'closed', clean:true, localCloseRequested:true, closeCode:1, reason:'é'.repeat(512)});
        const result = await transport.closed; assert.equal(result.closeCode, 1); assert.equal(closed, true);
        transport.close(); assert.equal(h.of(12).length, 1); assert.equal(h.callbacks.size, 0); reader.releaseLock();
    });
    test(label('negative TLS is certificate-specific and rejects both lifecycle promises'), async () => {
        const h = fixture(implementation), transport = h.transport();
        h.emit({type:'closed', clean:false, localCloseRequested:false, errorName:'SecurityError'});
        await assert.rejects(transport.ready, named('SecurityError')); await assert.rejects(transport.closed, named('SecurityError'));
        assert.equal(h.calls.length, 0); assert.equal(h.callbacks.size, 0);
    });
    test(label('closing before ready rejects lifecycle promises after native cleanup'), async () => {
        const h = fixture(implementation), transport = h.transport(); transport.close();
        await assert.rejects(transport.ready, named('WebTransportError'));
        let settled = false; transport.closed.catch(() => { settled = true; }); await flush(); assert.equal(settled, false);
        h.emit({type:'closed', clean:true, localCloseRequested:true}); await assert.rejects(transport.closed, named('WebTransportError'));
        assert.equal(h.of(12).length, 1);
    });
    test(label('session failure rejects all pending operations and blocks new ones'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const creating = transport.createBidirectionalStream(); creating.catch(() => {}); await h.next(3);
        h.emit({type:'closed', clean:false, localCloseRequested:false, errorName:'QuotaExceededError'});
        await assert.rejects(creating, named('QuotaExceededError')); await assert.rejects(transport.closed, named('QuotaExceededError'));
        await assert.rejects(transport.createUnidirectionalStream(), named('InvalidStateError'));
        assert.equal(h.of(4).length, 0);
    });
    test(label('result mismatch rejects the request and closes the actual session'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        const creating = transport.createUnidirectionalStream(); creating.catch(() => {}); const create = await h.next(4);
        h.complete(create, {operation:3, streamId:1, direction:'send'});
        await assert.rejects(creating, named('OperationError')); await h.next(12);
        h.emit({type:'closed', clean:true, localCloseRequested:true}); await assert.rejects(transport.closed, named('OperationError'));
    });
    test(label('unsupported advanced features remain absent instead of returning fabricated results'), async () => {
        const h = fixture(implementation), transport = h.transport(); h.ready(); await transport.ready;
        assert.equal('getStats' in transport, false); assert.equal('draining' in transport, false);
        await assert.rejects(transport.createBidirectionalStream({sendOrder:10}), named('NotSupportedError'));
        assert.throws(() => transport.datagrams.readable.getReader({mode:'byob'}), named('TypeError'));
        assert.equal(h.of(3).length, 0); await h.close(transport);
    });
    test(label('WebTransportError clamps codes with WebIDL tie-to-even semantics'), () => {
        const h = fixture(implementation), {WebTransportError} = h.globals;
        assert.equal(new WebTransportError('x', {streamErrorCode:2.5}).streamErrorCode, 2);
        assert.equal(new WebTransportError('x', {streamErrorCode:3.5}).streamErrorCode, 4);
        assert.equal(new WebTransportError('x', {streamErrorCode:Infinity}).streamErrorCode, 4294967295);
        assert.equal(new WebTransportError('x', {streamErrorCode:-5}).streamErrorCode, 0);
        assert.equal(new WebTransportError('x').streamErrorCode, null);
        assert.equal(new WebTransportError('x').source, 'stream');
        assert.throws(() => new WebTransportError('x', {source:'invented'}), named('TypeError'));
    });
}
