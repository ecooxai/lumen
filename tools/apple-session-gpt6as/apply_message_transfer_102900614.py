#!/usr/bin/env python3
"""Apply the reviewed native transfer fix after the complete 15/68 RED run."""
from pathlib import Path
import datetime
import hashlib
import json
import subprocess
import time

started = time.monotonic()
started_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
root = Path.cwd()
paths = ['src/js/worker.cc', 'src/js/prelude/07b-net.js', 'src/js/prelude/08-window.js']
original = {p: (root / p).read_text() for p in paths}
updated = dict(original)

def replace(path, before, after):
    count = updated[path].count(before)
    if count != 1:
        raise SystemExit(f'Expected exactly one patch anchor in {path}, found {count}: {before[:120]}')
    updated[path] = updated[path].replace(before, after, 1)

worker = 'src/js/worker.cc'
net = 'src/js/prelude/07b-net.js'
window = 'src/js/prelude/08-window.js'

replace(worker,
    'struct Side { std::vector<v8::CompiledWasmModule> wasm; std::vector<std::shared_ptr<v8::BackingStore>> sab; };',
    '''struct Side {
    std::vector<v8::CompiledWasmModule> wasm;
    std::vector<std::shared_ptr<v8::BackingStore>> sab;
    /* Transfer envelopes retain storage, never source-isolate handles. The
     * allocator is shared by JsCtx and worker_main, so queued buffers may
     * safely outlive their creator or be discarded during termination. */
    std::vector<std::shared_ptr<v8::BackingStore>> buffers;
};''')

replace(worker, '''bool ser(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> v, Buf &out) {
    SerDel del(iso);
    v8::ValueSerializer s(iso, &del);
    s.WriteHeader();
    if (!s.WriteValue(ctx, v).FromMaybe(false)) return false;
    auto r = s.Release(); out.p = r.first; out.n = r.second;
    if (!del.side->wasm.empty() || !del.side->sab.empty()) out.side = del.side;
    return true;
}''', '''bool ser(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> v, Buf &out,
         v8::Local<v8::Value> transfer = {}) {
    std::vector<v8::Local<v8::ArrayBuffer>> buffers;
    if (!transfer.IsEmpty() && !transfer->IsUndefined()) {
        if (!transfer->IsArray()) {
            jsg_idl_type_error(iso, "Expected the normalized transfer list");
            return false;
        }
        auto list = transfer.As<v8::Array>();
        for (uint32_t i = 0; i < list->Length(); ++i) {
            v8::Local<v8::Value> value;
            if (!list->Get(ctx, i).ToLocal(&value)) return false;
            if (!value->IsArrayBuffer()) {
                jsg_throw_clone_exception(iso, jstr(iso, "This object is not transferable."));
                return false;
            }
            auto buffer = value.As<v8::ArrayBuffer>();
            if (std::any_of(buffers.begin(), buffers.end(), [&](auto other) { return buffer->StrictEquals(other); })) {
                jsg_throw_clone_exception(iso, jstr(iso, "The transfer list contains a duplicate buffer."));
                return false;
            }
            buffers.push_back(buffer);
        }
    }
    SerDel del(iso);
    v8::ValueSerializer s(iso, &del);
    for (uint32_t i = 0; i < buffers.size(); ++i) s.TransferArrayBuffer(i, buffers[i]);
    s.WriteHeader();
    if (!s.WriteValue(ctx, v).FromMaybe(false)) return false;
    /* HTML StructuredSerializeWithTransfer serializes before any detachment.
     * It then transfers in list order: a later detached item must not undo
     * earlier transfers. Getters may have detached/resized these buffers. */
    for (auto buffer : buffers) {
        if (buffer->WasDetached() || !buffer->IsDetachable()) {
            jsg_throw_clone_exception(iso, jstr(iso, "The transfer list contains a detached or non-transferable buffer."));
            return false;
        }
        del.side->buffers.push_back(buffer->GetBackingStore());
        if (!buffer->Detach(v8::Local<v8::Value>()).FromMaybe(false)) return false;
    }
    auto r = s.Release(); out.p = r.first; out.n = r.second;
    if (!del.side->wasm.empty() || !del.side->sab.empty() || !del.side->buffers.empty()) out.side = del.side;
    return true;
}''')

replace(worker, '''    v8::ValueDeserializer d(iso, b.p, b.n, &del);
    if (!d.ReadHeader(ctx).FromMaybe(false)) return {};''', '''    v8::ValueDeserializer d(iso, b.p, b.n, &del);
    if (b.side) for (uint32_t i = 0; i < b.side->buffers.size(); ++i)
        d.TransferArrayBuffer(i, v8::ArrayBuffer::New(iso, b.side->buffers[i]));
    if (!d.ReadHeader(ctx).FromMaybe(false)) return {};''')

replace(worker,
    'WFN(post) { WENV; Buf b; if (ser(iso, ctx, a[0], b)) post_out(E, OutMsg{ WM_MESSAGE, b, {}, {}, 0, 0 }); }',
    'WFN(post) { WENV; Buf b; if (ser(iso, ctx, a[0], b, a[1])) post_out(E, OutMsg{ WM_MESSAGE, b, {}, {}, 0, 0 }); }')
replace(worker,
    'WFN(clone) { WENV; Buf b; if (!ser(iso, ctx, a[0], b)) return; v8::Local<v8::Value> v; if (deser(iso, ctx, b).ToLocal(&v)) a.GetReturnValue().Set(v); free(b.p); }',
    'WFN(clone) { WENV; Buf b; if (!ser(iso, ctx, a[0], b, a[1])) return; v8::Local<v8::Value> v; if (deser(iso, ctx, b).ToLocal(&v)) a.GetReturnValue().Set(v); free(b.p); }')
replace(worker,
    '    if (!ser(iso, ctx, a[1], b)) return;\n    if (!w) { free(b.p); return; }',
    '    if (!ser(iso, ctx, a[1], b, a[2])) return;\n    if (!w) { free(b.p); return; }')
replace(worker, 'void n_workerTerm(const FCI &a) {', '''void n_transferClone(const FCI &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    Buf buffer;
    if (!ser(iso, ctx, a[0], buffer, a[1])) return;
    v8::Local<v8::Value> value;
    if (deser(iso, ctx, buffer).ToLocal(&value)) a.GetReturnValue().Set(value);
    free(buffer.p);
}
void n_workerTerm(const FCI &a) {''')
replace(worker,
    '    (void)N->Set(ctx, jstr(iso, "workerTerm"), v8::Function::New(ctx, n_workerTerm).ToLocalChecked());',
    '''    (void)N->Set(ctx, jstr(iso, "workerTerm"), v8::Function::New(ctx, n_workerTerm).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "transferClone"), v8::Function::New(ctx, n_transferClone).ToLocalChecked());''')

NORMALIZE = '''// Convert the Web IDL overload before serialization. In particular, do not
// silently discard non-port transferables or evaluate a transfer getter twice.
function cloneTransferList(options, legacy = false) {
    if (options === undefined || options === null) return [];
    if (typeof options !== 'object' && typeof options !== 'function') throw new TypeError('Transfer options must be an object.');
    let value, method;
    if (legacy) {
        method = options[Symbol.iterator];
        value = method === undefined || method === null ? options.transfer : options;
    } else value = options.transfer;
    if (value === undefined) return [];
    if (value === null || (typeof value !== 'object' && typeof value !== 'function')) throw new TypeError('Transfer must be a sequence of objects.');
    if (value !== options || method === undefined || method === null) method = value[Symbol.iterator];
    if (typeof method !== 'function') throw new TypeError('Transfer must be iterable.');
    const iterator = method.call(value);
    const list = [];
    for (const item of { [Symbol.iterator]() { return iterator; } }) {
        if (item === null || (typeof item !== 'object' && typeof item !== 'function')) throw new TypeError('A transferable must be an object.');
        list.push(item);
    }
    return list;
}
const portBufferLength = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'byteLength').get;
function portList(options) {
    const list = cloneTransferList(options, true), ports = [], buffers = [], seen = new Set();
    for (const item of list) {
        if (seen.has(item)) throw new DOMException('The transfer list contains a duplicate object.', 'DataCloneError');
        seen.add(item);
        if (item instanceof MessagePort) ports.push(item);
        else {
            // The intrinsic checks the ArrayBuffer brand, including detached
            // buffers; public properties and SharedArrayBuffer cannot spoof it.
            try { portBufferLength.call(item); }
            catch (_) { throw new DOMException('This object is not transferable.', 'DataCloneError'); }
            buffers.push(item);
        }
    }
    return { ports, buffers };
}
'''
replace(net,
    "const portList = t => (t && typeof t[Symbol.iterator] === 'function' ? [...t] : t && t.transfer ? [...t.transfer] : []).filter(p => p && typeof p.postMessage === 'function' && typeof p.start === 'function');",
    NORMALIZE.rstrip())
replace(worker,
    'const wports = new Map(); let wpseq = 0;',
    NORMALIZE + 'const wports = new Map(); let wpseq = 0;')

replace(net,
    "class MessagePort extends EventTarget { constructor() { super(); this.onmessage = null; def(this, '_other', null); } postMessage(d, t) { const ps = portList(t), r = this._wk || (this._other && this._other._wk); if (r) { N.workerPost(r[0], { __lumenPort: r[1], data: portSub(d, ps), xfer: ps.map(p => { const k = ++portSeq; p._wk = [r[0], k]; if (p._other) remotePorts.set(k, p._other); return k; }) }); return; } const o = this._other; if (o) { const c = structuredClone(d); setTimeout(() => o._deliver(c, ps)); } } _deliver(data, ports) { dispatch(this, new MessageEvent('message', { data, ports: ports || [] }), true); } start() {} close() { this._other = null; } }",
    '''class MessagePort extends EventTarget {
    constructor() { super(); this.onmessage = null; def(this, '_other', null); }
    postMessage(data, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage': 1 argument required.");
        const { ports, buffers } = portList(options), remote = this._wk || (this._other && this._other._wk);
        if (remote) {
            const ids = ports.map(() => ++portSeq);
            N.workerPost(remote[0], { __lumenPort: remote[1], data: portSub(data, ports), xfer: ids }, buffers);
            ports.forEach((port, i) => { port._wk = [remote[0], ids[i]]; if (port._other) remotePorts.set(ids[i], port._other); });
            return;
        }
        const other = this._other;
        const cloned = structuredClone(portSub(data, ports), { transfer: buffers });
        if (other) setTimeout(() => other._deliver(portRes(cloned, ports), ports));
    }
    _deliver(data, ports) { dispatch(this, new MessageEvent('message', { data, ports: ports || [] }), true); }
    start() {}
    close() { this._other = null; }
}''')

replace(worker,
    "    postMessage(d, t) { const ps = (Array.isArray(t) ? t : t && t.transfer || []).filter(p => p instanceof MessagePort), k = this._k || (this._other && this._other._k); if (k) { W.post({ __lumenPort: k, data: portSub(d, ps), xfer: ps.map(p => { const j = -(++wpseq); p._k = j; if (p._other) wports.set(j, p._other); return j; }) }); return; } const o = this._other; if (o) { const c = W.clone(d); setTimeout(() => { const e = new MessageEvent('message', { data: c, ports: ps }); e.isTrusted = true; fireOn(o, e); }); } }",
    '''    postMessage(data, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage': 1 argument required.");
        const { ports, buffers } = portList(options), key = this._k || (this._other && this._other._k);
        if (key) {
            const ids = ports.map(() => -(++wpseq));
            W.post({ __lumenPort: key, data: portSub(data, ports), xfer: ids }, buffers);
            ports.forEach((port, i) => { port._k = ids[i]; if (port._other) wports.set(ids[i], port._other); });
            return;
        }
        const other = this._other;
        const cloned = W.clone(portSub(data, ports), buffers);
        if (other) setTimeout(() => {
            const event = new MessageEvent('message', { data: portRes(cloned, ports), ports });
            event.isTrusted = true; fireOn(other, event);
        });
    }''')

replace(net,
    '    postMessage(m, t) { if (arguments.length < 1) throw new TypeError("Failed to execute \'postMessage\' on \'Worker\': 1 argument required, but only 0 present."); if (!this._id) return; const ps = (Array.isArray(t) ? t : t && t.transfer || []).filter(p => p instanceof MessagePort); if (!ps.length) return N.workerPost(this._id, m); N.workerPost(this._id, { __lumenXfer: ps.map(p => { const k = ++portSeq; p._wk = [this._id, k]; if (p._other) remotePorts.set(k, p._other); return k; }), data: portSub(m, ps) }); }',
    '''    postMessage(data, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage' on 'Worker': 1 argument required.");
        const { ports, buffers } = portList(options);
        if (!ports.length) return N.workerPost(this._id, data, buffers);
        const ids = ports.map(() => ++portSeq);
        N.workerPost(this._id, { __lumenXfer: ids, data: portSub(data, ports) }, buffers);
        ports.forEach((port, i) => { port._wk = [this._id, ids[i]]; if (port._other) remotePorts.set(ids[i], port._other); });
    }''')
replace(worker,
    '    postMessage(m, t) { if (arguments.length < 1) throw new TypeError("Failed to execute \'postMessage\': 1 argument required, but only 0 present."); const ps = (Array.isArray(t) ? t : t && t.transfer || []).filter(p => p instanceof MessagePort); if (!ps.length) return W.post(m); W.post({ __lumenXfer: ps.map(p => { const k = -(++wpseq); p._k = k; if (p._other) wports.set(k, p._other); return k; }), data: portSub(m, ps) }); },',
    '''    postMessage(data, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage': 1 argument required.");
        const { ports, buffers } = portList(options);
        if (!ports.length) return W.post(data, buffers);
        const ids = ports.map(() => -(++wpseq));
        W.post({ __lumenXfer: ids, data: portSub(data, ports) }, buffers);
        ports.forEach((port, i) => { port._k = ids[i]; if (port._other) wports.set(ids[i], port._other); });
    },''')
replace(worker,
    "}, structuredClone: v => W.clone(v),",
    '''},
    structuredClone(value, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'structuredClone': 1 argument required.");
        return W.clone(value, cloneTransferList(options));
    },''')

old_start = updated[window].index('function structuredClone(v, seen = new Map()) {')
old_end = updated[window].index('\nconst xferPorts = ', old_start)
old_clone = updated[window][old_start:old_end]
copy_value = old_clone.replace('structuredClone(', 'cloneValue(')
wrapper = '''function structuredClone(value, options) {
    if (!arguments.length) throw new TypeError("Failed to execute 'structuredClone': 1 argument required.");
    const transfer = cloneTransferList(options);
    // Preserve the established platform-value clone path when no transfers
    // are requested; transferable graph reconstruction uses the native codec.
    return transfer.length ? N.transferClone(value, transfer) : cloneValue(value);
}
'''
replace(window, old_clone, wrapper + copy_value)

out = root / 'output/session-102900614/message-transfer-candidate-1'
out.mkdir(parents=True, exist_ok=False)
receipt = {'startedUTC': started_utc, 'sourceBaseCommit': subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
           'fixtureSHA256': hashlib.sha256((root/'tests/test_message_transfer.py').read_bytes()).hexdigest(),
           'red': {'report':'output/session-102900614/message-transfer-red-1.log','total':68,'passed':15,'failed':53},
           'before': {}, 'after': {}, 'standards':'https://html.spec.whatwg.org/multipage/structured-data.html#structuredserializewithtransfer'}
for path in paths:
    receipt['before'][path] = hashlib.sha256(original[path].encode()).hexdigest()
    (root/path).write_text(updated[path])
    receipt['after'][path] = hashlib.sha256(updated[path].encode()).hexdigest()
receipt['finishedUTC'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
receipt['durationSeconds'] = round(time.monotonic()-started,3)
(out/'patch-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
subprocess.run(['git','diff','--check','--',*paths],check=True)
(out/'source.diff').write_bytes(subprocess.check_output(['git','diff','--',*paths]))
print(json.dumps(receipt))
