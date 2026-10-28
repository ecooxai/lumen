#!/usr/bin/env python3
"""Prepare a guarded shared-stream candidate from the verified source mirror."""
from pathlib import Path
import argparse
import datetime
import difflib
import hashlib
import json
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--source-root',type=Path,default=Path('remote-source-mirror'))
p.add_argument('--output',type=Path,default=Path('wpt_engine/text-streams-candidate'))
p.add_argument('--apply',action='store_true')
args=p.parse_args()
base=args.source_root.resolve()
destination=args.output.resolve()
paths=['src/js/prelude/07-web.js','src/js/worker.cc','src/js/idl_state.h','src/js/clone_brand.h','Makefile','tests/test_writable_streams.mjs']
expected={
    'src/js/prelude/07-web.js':'3ac188ebee6f1d486f8fc1c8e27b6a707fc60778df5bcbe963b4a776b74d11c4',
    'src/js/worker.cc':'130f0b43484630fc8249aea2920109c0fd06676e5e8af294719b19c3617fb661',
    'src/js/idl_state.h':'ad32a106f9ce8e01a926d9a98142da4be8127b996f7eb26ac21e89eb4ff44f17',
    'src/js/clone_brand.h':'5e14f2dd9c2e6bc2c8b6a5177f55a6df4851dc8d69da56f9262a3bdb86bbf5d6',
    'Makefile':'64ec44d5a7fc9228a4ee5de0089633eb7d609a35b70a2a13d231395f487cad30',
    'tests/test_writable_streams.mjs':'a566867594c982392321f34a4342c1925ba0e8c65630e7cf47b0f261f763ece4',
}
sha=lambda data:hashlib.sha256(data).hexdigest()
for name,digest in expected.items():
    if sha((base/name).read_bytes())!=digest:raise SystemExit('Source guard failed: '+name)
if (base/'src/js/prelude/06t-streams.js').exists():raise SystemExit('Shared stream source already exists')
destination.mkdir(parents=True,exist_ok=not args.apply)
before={name:(base/name).read_text() for name in paths}
after=dict(before)
def replace(name,old,new):
    if after[name].count(old)!=1:raise SystemExit('Patch anchor is not unique: '+name+' '+old[:100])
    after[name]=after[name].replace(old,new,1)

web=before['src/js/prelude/07-web.js']
start=web.index('const streamCtl = ')
stop=web.index('const objectURLs = ',start)
core=web[start:stop]
core=core[:core.index('class TextEncoderStream extends TransformStream')]
core=core.replace("if (s._done || s._err) throw new TypeError('Cannot enqueue a chunk into a closed stream');", "if (s._done || s._errored) throw new TypeError('Cannot enqueue a chunk into a closed stream');")
core=core.replace("close() { s._done = true; s._wake(); }, error(e) { if (!s._done) { s._err = e === undefined ? new TypeError('errored') : e; s._wake(); } },", "close() { if (s._done || s._errored) throw new TypeError('Cannot close this stream'); s._done = true; s._wake(); }, error(e) { if (!s._done && !s._errored) { s._errored = true; s._err = e; s._q.length = 0; s._wake(); } },")
core=core.replace("return s._err ? null : s._done ? 0 :", "return s._errored ? null : s._done ? 0 :")
core=core.replace("def(this, '_err', undefined); def(this, '_waits', []);", "def(this, '_err', undefined); def(this, '_errored', false); def(this, '_waits', []);")
core=core.replace("this._err = e; }\n        Promise.resolve(r)", "this._errored = true; this._err = e; }\n        Promise.resolve(r)")
core=core.replace("if (this._err !== undefined && !this._q.length) rd._cr[1](this._err);", "if (this._errored) rd._cr[1](this._err);")
core=core.replace("this._waits.length && this._err !== undefined", "this._waits.length && this._errored")
core=core.replace("!this._done && this._err === undefined && this._src.pull", "!this._done && !this._errored && this._src.pull")
core=core.replace("_cancel(r) { if (this._done", "_cancel(r) { if (this._errored) return Promise.reject(this._err); if (this._done")

transform_start=core.index('class TransformStream {')
transform_end=core.index('class ByteLengthQueuingStrategy',transform_start)
transform=r'''class TransformStream {
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
'''
core=core[:transform_start]+transform+core[transform_end:]

codecs=r'''
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
'''
shared='''/* Shared stream runtime for window and dedicated workers. Encoding streams
 * retain only an unfinished UTF scalar and use transform backpressure. */
function createWebStreamInterfaces(privateState, platform) {
const { TextEncoder, TextDecoder, AbortController, DOMException } = platform;
const def = (object, key, value) => Object.defineProperty(object, key, { value, writable: true, configurable: true, enumerable: false });
'''+core+codecs+'}\n'
after['src/js/prelude/06t-streams.js']=shared
window_streams='''const { ReadableStream, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultWriter,
    TransformStream, ByteLengthQueuingStrategy, CountQueuingStrategy, TextEncoderStream, TextDecoderStream
} = createWebStreamInterfaces(nativeIDL, { TextEncoder, TextDecoder, AbortController, DOMException });
'''
replace('src/js/prelude/07-web.js',web[start:stop],window_streams)

worker_abort=r'''
// Dedicated workers need real abort signals for writable stream cancellation.
const workerAbortSignalState = new WeakMap(), workerAbortControllerState = new WeakMap(), workerAbortToken = {};
const abortState = signal => {
    const state = workerAbortSignalState.get(signal);
    if (!state) throw new TypeError('Invalid AbortSignal receiver');
    return state;
};
class AbortSignal extends EventTarget {
    constructor(...args) {
        if (args[0] !== workerAbortToken) throw new TypeError('Illegal constructor');
        super(); workerAbortSignalState.set(this, { aborted: false, reason: undefined }); this.onabort = null;
    }
    get aborted() { return abortState(this).aborted; }
    get reason() { return abortState(this).reason; }
    throwIfAborted() { const state = abortState(this); if (state.aborted) throw state.reason; }
    static abort(reason = undefined) { const controller = new AbortController(); controller.abort(reason); return controller.signal; }
    static timeout(milliseconds) {
        milliseconds = Number(milliseconds);
        if (!Number.isFinite(milliseconds) || milliseconds < 0 || milliseconds >= 18446744073709551616) throw new RangeError('Invalid timeout');
        const controller = new AbortController();
        setTimeout(() => controller.abort(new DOMException('Signal timed out', 'TimeoutError')), Math.trunc(milliseconds));
        return controller.signal;
    }
    static any(signals) {
        const list = Array.from(signals); for (const signal of list) abortState(signal);
        const controller = new AbortController(), listeners = [];
        const abort = signal => {
            controller.abort(abortState(signal).reason);
            for (const [source, listener] of listeners) source.removeEventListener('abort', listener);
            listeners.length = 0;
        };
        const already = list.find(signal => abortState(signal).aborted);
        if (already) abort(already);
        else for (const signal of new Set(list)) { const listener = () => abort(signal); listeners.push([signal, listener]); signal.addEventListener('abort', listener, { once: true }); }
        return controller.signal;
    }
}
class AbortController {
    constructor() { workerAbortControllerState.set(this, new AbortSignal(workerAbortToken)); }
    get signal() { const signal = workerAbortControllerState.get(this); if (!signal) throw new TypeError('Invalid AbortController receiver'); return signal; }
    abort(reason = undefined) {
        const signal = this.signal, state = abortState(signal);
        if (state.aborted) return;
        state.aborted = true; state.reason = reason === undefined ? new DOMException('Signal aborted', 'AbortError') : reason;
        const event = new Event('abort'); event.isTrusted = true; fireOn(signal, event);
    }
}
'''
replace('src/js/worker.cc','#include "headers.inc"\nR"JS(', '#include "headers.inc"\n#include "web_streams.inc"\nR"JS(')
anchor='const { TextEncoder, TextDecoder } = createTextCodecInterfaces(null, { label: W.textEncoding, decode: W.textDecode });'
replace('src/js/worker.cc',anchor,worker_abort+'\n'+anchor+'\n'+window_streams)
replace('src/js/worker.cc','    blobState: W.blobState, encode: urlSearchParamsOps.utf8Bytes, decode: W.decode,','''    blobState: W.blobState, encode: urlSearchParamsOps.utf8Bytes, decode: W.decode,
    stream: bytes => new ReadableStream({ start(controller) { controller.enqueue(bytes); controller.close(); } }),''')
replace('src/js/worker.cc','    btoa, atob, TextEncoder, TextDecoder, Blob, File, FileReader, FileReaderSync, URL, URLSearchParams, FormData, Headers, Request, Response, fetch, performance, console,','''    btoa, atob, TextEncoder, TextDecoder, TextEncoderStream, TextDecoderStream,
    ReadableStream, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultWriter,
    TransformStream, ByteLengthQueuingStrategy, CountQueuingStrategy, AbortController, AbortSignal,
    Blob, File, FileReader, FileReaderSync, URL, URLSearchParams, FormData, Headers, Request, Response, fetch, performance, console,''')

replace('src/js/idl_state.h','JSG_IDL_BLOB = 6, JSG_IDL_FILE = 7 };','JSG_IDL_BLOB = 6, JSG_IDL_FILE = 7, JSG_IDL_TEXT_ENCODER_STREAM = 8, JSG_IDL_TEXT_DECODER_STREAM = 9 };')
replace('src/js/idl_state.h','kind > JSG_IDL_FILE)', 'kind > JSG_IDL_TEXT_DECODER_STREAM)')
replace('src/js/idl_state.h','''        case JSG_IDL_FILE:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.File.state.v1"));''','''        case JSG_IDL_FILE:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.File.state.v1"));
        case JSG_IDL_TEXT_ENCODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextEncoderStream.state.v1"));
        case JSG_IDL_TEXT_DECODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextDecoderStream.state.v1"));''')
replace('src/js/idl_state.h','''        case JSG_IDL_FILE:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.File.prototype.v1"));''','''        case JSG_IDL_FILE:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.File.prototype.v1"));
        case JSG_IDL_TEXT_ENCODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextEncoderStream.prototype.v1"));
        case JSG_IDL_TEXT_DECODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextDecoderStream.prototype.v1"));''')
replace('src/js/clone_brand.h','JSG_IDL_FILE_READER, JSG_IDL_FILE_READER_SYNC};','JSG_IDL_FILE_READER, JSG_IDL_FILE_READER_SYNC, JSG_IDL_TEXT_ENCODER_STREAM, JSG_IDL_TEXT_DECODER_STREAM};')
after['Makefile']+='''
# One stream and incremental Encoding Stream runtime for both JavaScript hosts.
$(BUILD)/src/js/web_streams.inc: src/js/prelude/06t-streams.js
\t@mkdir -p $(dir $@)
\t{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/web_streams.inc
'''
replace('tests/test_writable_streams.mjs', "new URL('../src/js/prelude/07-web.js',import.meta.url)", "new URL('../src/js/prelude/06t-streams.js',import.meta.url)")

payload={'before':{name:hashlib.sha256(value.encode()).hexdigest() for name,value in before.items()},'files':after}
for name,value in after.items():
    target=destination/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(value)
(destination/'payload.json').write_text(json.dumps(payload))
if args.apply:
    fixture=base/'tests/test_text_streams.py'
    if not fixture.is_file():raise SystemExit('Install the measured RED fixture before applying this source patch')
    fixtures={
        'tests/test_text_streams.py':'341f6f3ea9dd173bffd2e63db6cd5d41c9b95687a1917ae00897c6902a8f246c',
        'tests/test_transform_streams.mjs':'1c295a7765768cf8ade491091ef85c29727e20544122329a81120b4fb1959322',
    }
    for name,digest in fixtures.items():
        if sha((base/name).read_bytes())!=digest:raise SystemExit('The measured RED fixture changed: '+name)
    diff=''.join(''.join(difflib.unified_diff(before.get(name,'').splitlines(keepends=True),value.splitlines(keepends=True),fromfile='a/'+name,tofile='b/'+name)) for name,value in after.items())
    (destination/'source.diff').write_text(diff)
    receipt={'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=base,text=True).strip(),
             'before':payload['before'],'after':{name:sha(value.encode()) for name,value in after.items()},
             'fixtureSHA256':sha(fixture.read_bytes()),'fixturePath':'tests/test_text_streams.py',
             'testFixtures':fixtures,
             'red':{'encodingStreams':{'report':'output/session-102900614/text-streams-red-1.log','total':74,'passed':10,'failed':64},
                    'transformLifecycle':{'report':'output/session-102900614/transform-streams-red-2.log','total':13,'passed':5,'failed':6,'timedOut':2}},
             'scope':'Shared streams plus incremental Encoding streams in window and dedicated workers; no full Streams or legacy streaming conformance claim.'}
    for name,value in after.items():(base/name).write_text(value)
    receipt['finishedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (destination/'patch-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'applied':args.apply,'files':len(after),'payloadBytes':(destination/'payload.json').stat().st_size,'factoryBytes':len(shared.encode()),'before':payload['before']}))
