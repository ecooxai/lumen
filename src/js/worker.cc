/* Dedicated Web Workers: each worker runs on its own thread with its own V8 isolate.
   Messages cross threads as V8 ValueSerializer buffers. */
#include "js_int.h"
#include "jsglue.h"
#include "v8_tasks.h"
#include "clone.h"
#include <libplatform/libplatform.h>
#include <pthread.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
extern "C" {
#include "../net/net.h"
#include "../base/url.h"
}

void (*js_wakeup)(void) = nullptr;

namespace {
using Buf = JsSerializedValue;
enum { WM_MESSAGE, WM_ERROR, WM_LOADFAIL };
struct OutMsg { int kind; Buf data; std::string msg, file; int line = 0, col = 0; };
struct Worker {
    uint32_t id = 0; JsCtx *owner = nullptr;
    std::string url, src, ua, platform, name; bool have_src = false, module = false, shared = false, service = false;
    std::mutex mu; std::condition_variable cv;
    uint64_t task_version = 0;  // protected by mu, prevents lost foreground wakeups
    std::deque<Buf> in; std::deque<OutMsg> out;
    std::atomic<bool> term{false};
    v8::Isolate *iso = nullptr;
    std::atomic<size_t> heap{0}, ext{0};
};
struct WTimer { double due = 0, interval = 0; bool repeat = false; v8::Global<v8::Function> fn; std::vector<v8::Global<v8::Value>> args; };
struct WEnv {
    std::shared_ptr<Worker> w; v8::Isolate *iso = nullptr;
    v8::Global<v8::Context> ctx; v8::Global<v8::Function> onmsg, report;
    std::map<std::string, v8::Global<v8::Module>> mods; std::multimap<int, std::string> murl;
    std::map<uint32_t, WTimer> timers; uint32_t tseq = 0; bool closing = false, reporting = false; double t0 = 0;
};
std::map<uint32_t, std::shared_ptr<Worker>> g_workers;   /* main thread only */
uint32_t g_wseq;
const int kEnvSlot = 2;
v8::MaybeLocal<v8::Module> wm_compile(WEnv *E, const std::string &u, const std::string &src);
bool wm_graph(WEnv *E, v8::Local<v8::Module> m);
v8::MaybeLocal<v8::Module> wm_resolve_cb(v8::Local<v8::Context>, v8::Local<v8::String>, v8::Local<v8::FixedArray>, v8::Local<v8::Module>);
void wm_meta(v8::Local<v8::Context> ctx, v8::Local<v8::Module> m, v8::Local<v8::Object> meta);

void wake() { if (js_wakeup) js_wakeup(); }

bool bytes_of(v8::Local<v8::Value> v, const char *&p, size_t &n) {
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); p = (const char *)ab->Data(); n = ab->ByteLength(); return true; }
    if (v->IsArrayBufferView()) { auto av = v.As<v8::ArrayBufferView>(); p = (const char *)av->Buffer()->Data() + av->ByteOffset(); n = av->ByteLength(); return true; }
    return false;
}
v8::Local<v8::ArrayBuffer> make_ab(v8::Isolate *iso, const char *p, size_t n) {
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, n);
    if (n) memcpy(ab->Data(), p, n);
    return ab;
}

/* ---- worker thread ---- */
WEnv *wenv(v8::Isolate *iso) { return static_cast<WEnv *>(iso->GetData(kEnvSlot)); }

void post_out(WEnv *E, OutMsg &&m) { { std::lock_guard<std::mutex> lk(E->w->mu); E->w->out.push_back(std::move(m)); } wake(); }

/* An uncaught worker exception: the worker's own onerror/listeners may handle it, else it goes to the Worker object */
void werr(WEnv *E, v8::Local<v8::Value> exc, v8::Local<v8::Message> m) {
    if (E->w->term || E->reporting) return;
    v8::Isolate *iso = E->iso; v8::Local<v8::Context> ctx = E->ctx.Get(iso);
    std::string msg = m.IsEmpty() ? jcstr(iso, exc) : jcstr(iso, m->Get());
    std::string file = m.IsEmpty() ? E->w->url : jcstr(iso, m->GetScriptResourceName());
    int line = m.IsEmpty() ? 0 : m->GetLineNumber(ctx).FromMaybe(0);
    int col = m.IsEmpty() ? 0 : m->GetStartColumn(ctx).FromMaybe(0) + 1;
    bool handled = false;
    if (!E->report.IsEmpty()) {
        E->reporting = true;
        v8::TryCatch t2(iso);
        v8::Local<v8::Value> argv[5] = { exc, jstr(iso, msg.c_str()), jstr(iso, file.c_str()), v8::Integer::New(iso, line), v8::Integer::New(iso, col) };
        v8::Local<v8::Value> r;
        handled = E->report.Get(iso)->Call(ctx, v8::Undefined(iso), 5, argv).ToLocal(&r) && r->BooleanValue(iso);
        E->reporting = false;
    }
    if (!handled) post_out(E, OutMsg{ WM_ERROR, {}, msg, file, line, col });
}
void wcheck(WEnv *E, v8::TryCatch &tc) {
    if (!tc.HasCaught() || !tc.CanContinue()) return;
    werr(E, tc.Exception(), tc.Message());
}
void throw_dom(v8::Isolate *iso, v8::Local<v8::Context> ctx, const std::string &msg, const char *name) {
    v8::Local<v8::Value> ctor;
    v8::Local<v8::Value> argv[2] = { jstr(iso, msg.c_str()), jstr(iso, name) };
    v8::Local<v8::Object> e;
    if (ctx->Global()->Get(ctx, jstr(iso, "DOMException")).ToLocal(&ctor) && ctor->IsFunction() && ctor.As<v8::Function>()->NewInstance(ctx, 2, argv).ToLocal(&e))
        iso->ThrowException(e);
    else iso->ThrowException(v8::Exception::Error(jstr(iso, msg.c_str())));
}
std::string resolve(WEnv *E, const std::string &rel, const char *base = nullptr) {
    char *u = url_join(base ? base : E->w->url.c_str(), rel.c_str());
    std::string r = u ? u : ""; free(u); return r;
}

typedef v8::FunctionCallbackInfo<v8::Value> FCI;
#define WFN(nm) void w_##nm(const FCI &a)
#define WENV WEnv *E = wenv(a.GetIsolate()); v8::Isolate *iso = E->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext(); (void)ctx
WFN(post) { WENV; Buf b; if (js_serialize(iso, ctx, a[0], b)) post_out(E, OutMsg{ WM_MESSAGE, std::move(b), {}, {}, 0, 0 }); }
WFN(clone) { WENV; Buf b; if (!js_serialize(iso, ctx, a[0], b)) return; v8::Local<v8::Value> v; if (js_deserialize(iso, ctx, b).ToLocal(&v)) a.GetReturnValue().Set(v); }
WFN(timer) {
    WENV; if (!a[0]->IsFunction()) return;
    uint32_t id = ++E->tseq; WTimer &t = E->timers[id];
    double ms = a[1]->NumberValue(ctx).FromMaybe(0); if (!(ms > 0)) ms = 0;
    t.interval = ms; t.repeat = a[2]->BooleanValue(iso); t.due = now_ms() + ms;
    t.fn.Reset(iso, a[0].As<v8::Function>());
    if (a[3]->IsArray()) { auto arr = a[3].As<v8::Array>(); for (uint32_t i = 0; i < arr->Length(); i++) t.args.emplace_back(iso, arr->Get(ctx, i).ToLocalChecked()); }
    a.GetReturnValue().Set(id);
}
WFN(clear) { WENV; E->timers.erase(a[0]->Uint32Value(ctx).FromMaybe(0)); }
WFN(close) { WENV; E->closing = true; }
WFN(now) { WENV; a.GetReturnValue().Set(now_ms() - E->t0); }
WFN(log) { WENV; if (getenv("LUMEN_CONSOLE")) fprintf(stderr, "[worker %d] %.2000s\n", a[0]->Int32Value(ctx).FromMaybe(1), jcstr(iso, a[1]).c_str()); }
WFN(decode) {
    WENV; const char *p; size_t n;
    if (!bytes_of(a[0], p, n)) { a.GetReturnValue().Set(jstr(iso, "")); return; }
    if (n >= 3 && !memcmp(p, "\xEF\xBB\xBF", 3)) { p += 3; n -= 3; }
    v8::Local<v8::String> s;
    if (v8::String::NewFromUtf8(iso, p, v8::NewStringType::kNormal, (int)n).ToLocal(&s)) a.GetReturnValue().Set(s);
}
WFN(resolve) {
    WENV; std::string base = a[1]->IsString() ? jcstr(iso, a[1]) : E->w->url;
    std::string r = resolve(E, jcstr(iso, a[0]), base.c_str());
    if (r.empty()) a.GetReturnValue().SetNull(); else a.GetReturnValue().Set(jstr(iso, r.c_str()));
}
WFN(fetch) {
    WENV; std::string u = jcstr(iso, a[0]), m = a[1]->IsString() ? jcstr(iso, a[1]) : "GET";
    NetRequest *rq = net_request_new(m.c_str(), u.c_str());
    if (a[2]->IsString()) { std::string b = jcstr(iso, a[2]); rq->body = (char *)malloc(b.size() + 1); memcpy(rq->body, b.c_str(), b.size() + 1); rq->body_len = b.size(); }
    else if (a[2]->IsArrayBuffer() || a[2]->IsArrayBufferView()) {
        std::shared_ptr<v8::BackingStore> bs; size_t off = 0, n = 0;
        if (a[2]->IsArrayBuffer()) { bs = a[2].As<v8::ArrayBuffer>()->GetBackingStore(); n = bs->ByteLength(); }
        else { v8::Local<v8::ArrayBufferView> v = a[2].As<v8::ArrayBufferView>(); bs = v->Buffer()->GetBackingStore(); off = v->ByteOffset(); n = v->ByteLength(); }
        rq->body = (char *)malloc(n + 1); if (n) memcpy(rq->body, (char *)bs->Data() + off, n); rq->body[n] = 0; rq->body_len = n;
    }
    if (a[3]->IsArray()) {
        v8::Local<v8::Array> h = a[3].As<v8::Array>();
        for (uint32_t i = 0; i + 1 < h->Length(); i += 2) {
            v8::Local<v8::Value> k, v;
            if (h->Get(ctx, i).ToLocal(&k) && h->Get(ctx, i + 1).ToLocal(&v)) headers_add(&rq->headers, jcstr(iso, k).c_str(), jcstr(iso, v).c_str());
        }
    }
    if (a[4]->IsTrue()) rq->no_cookies = true;
    NetResponse *r = net_fetch_sync(rq);
    if (!r || r->status == 0 || r->error) { if (r) net_response_free(r); a.GetReturnValue().SetNull(); return; }
    v8::Local<v8::Array> hs = v8::Array::New(iso);
    for (int i = 0; i < r->headers.n; i++) { (void)hs->Set(ctx, (uint32_t)(2 * i), jstr(iso, r->headers.v[i].name)); (void)hs->Set(ctx, (uint32_t)(2 * i + 1), jstr(iso, r->headers.v[i].value)); }
    v8::Local<v8::Value> out[4] = { v8::Integer::New(iso, r->status), jstr(iso, r->url ? r->url : u.c_str()), make_ab(iso, r->body, r->body_len), hs };
    net_response_free(r);
    a.GetReturnValue().Set(v8::Array::New(iso, out, 4));
}
WFN(importScript) {
    WENV; std::string u = resolve(E, jcstr(iso, a[0]));
    if (u.empty()) { throw_dom(iso, ctx, "Failed to execute 'importScripts': The URL '" + jcstr(iso, a[0]) + "' is invalid.", "SyntaxError"); return; }
    NetResponse *r = net_fetch_sync(net_request_new("GET", u.c_str()));
    if (!r || r->status < 200 || r->status >= 300) {
        if (r) net_response_free(r);
        throw_dom(iso, ctx, "Failed to execute 'importScripts': The script at '" + u + "' failed to load.", "NetworkError"); return;
    }
    std::string src(r->body ? r->body : "", r->body_len); net_response_free(r);
    v8::ScriptOrigin origin(jstr(iso, u.c_str()));
    v8::Local<v8::Script> s; v8::Local<v8::Value> res;
    if (v8::Script::Compile(ctx, jsrc(iso, src.data(), src.size()), &origin).ToLocal(&s)) (void)s->Run(ctx).ToLocal(&res);
}
WFN(reportErr) { WENV; werr(E, a[0], v8::Exception::CreateMessage(iso, a[0])); }

const char kWorkerPrelude[] = R"JS((function (W, G) {
'use strict';
const def = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false });
)JS"
#include "domexception_shared.inc"
R"JS(
W.cloneError(DOMException);
)JS"
#include "quota_shared.inc"
R"JS(
const EVN = W;
)JS"
#include "event_shared.inc"
R"JS(
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data === undefined ? null : i.data; this.origin = i.origin || ''; this.lastEventId = i.lastEventId || ''; this.source = i.source || null; this.ports = i.ports || []; } }
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message || ''; this.filename = i.filename || ''; this.lineno = i.lineno || 0; this.colno = i.colno || 0; this.error = i.error; } }
class PromiseRejectionEvent extends Event { constructor(t, i) { super(t, i); this.promise = i && i.promise; this.reason = i && i.reason; } }
const lsn = new WeakMap();
const listeners = t => { let m = lsn.get(t); if (!m) lsn.set(t, m = new Map()); return m; };
/* EventTarget per the DOM standard. A worker has no tree: an event goes only to its target, after the target's
   on<type> handler. A bare call (no receiver) targets the worker global, as WebIDL operations on the global do. */
const etThis = (o, op) => {
    if (o == null) return G;
    if ((typeof o === 'object' || typeof o === 'function') && (o instanceof EventTarget || o === G)) return o;
    throw new TypeError(`Failed to execute '${op}' on 'EventTarget': Illegal invocation`);
};
const etArgs = (op, n, have) => { if (have < n) throw new TypeError(`Failed to execute '${op}' on 'EventTarget': ${n} argument${n > 1 ? 's' : ''} required, but only ${have} present.`); };
function fireOn(t, e, skipHandler) {
    evTarget(e, t); evCurrent(e, t); evPhase(e, 2); e._stop = e._imm = false; e._path = [t];
    const h = skipHandler ? null : t['on' + e.type];
    if (typeof h === 'function') { try { const r = h.call(t, e); if (r === false && e.type !== 'error') e.preventDefault(); } catch (x) { W.reportErr(x); } }
    const L = !e._imm && listeners(t).get(e.type);
    if (L) for (const l of L.slice()) {
        if (l.removed) continue;
        if (l.once) EventTarget.prototype.removeEventListener.call(t, e.type, l.fn, l.capture);
        e._passive = l.passive;
        try { if (typeof l.fn === 'function') l.fn.call(t, e); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(e); } catch (x) { W.reportErr(x); }
        e._passive = false;
        if (e._imm) break;
    }
    evCurrent(e, null); evPhase(e, 0);
    return !e.defaultPrevented;
}
class EventTarget {
    addEventListener(type, callback, options = undefined) {
        const self = etThis(this, 'addEventListener');
        etArgs('addEventListener', 2, arguments.length);
        type = String(type);
        if (callback !== null && callback !== undefined && typeof callback !== 'object' && typeof callback !== 'function')
            throw new TypeError("Failed to execute 'addEventListener' on 'EventTarget': parameter 2 is not of type 'EventListener'.");
        const o = typeof options === 'boolean' ? { capture: options } : options !== null && typeof options === 'object' ? options : {};
        const capture = !!o.capture, once = !!o.once, passive = !!o.passive, sig = o.signal;
        if (sig !== undefined && !(sig instanceof AbortSignal)) throw new TypeError("Failed to execute 'addEventListener' on 'EventTarget': Failed to read the 'signal' property from 'AddEventListenerOptions': Failed to convert value to 'AbortSignal'.");
        if (callback == null || (sig && sig.aborted)) return;
        const m = listeners(self); let L = m.get(type); if (!L) m.set(type, L = []);
        if (L.some(l => l.fn === callback && l.capture === capture)) return;
        L.push({ fn: callback, capture, once, passive, removed: false });
        if (sig) abortSignalAdd(sig, () => EventTarget.prototype.removeEventListener.call(self, type, callback, capture));
    }
    removeEventListener(type, callback, options = undefined) {
        const self = etThis(this, 'removeEventListener');
        etArgs('removeEventListener', 2, arguments.length);
        const capture = typeof options === 'boolean' ? options : !!(options !== null && typeof options === 'object' && options.capture);
        const L = listeners(self).get(String(type)); if (!L) return;
        const i = L.findIndex(l => l.fn === callback && l.capture === capture);
        if (i >= 0) { L[i].removed = true; L.splice(i, 1); }
    }
    dispatchEvent(event) {
        const self = etThis(this, 'dispatchEvent');
        etArgs('dispatchEvent', 1, arguments.length);
        if (!evIs(event)) throw new TypeError("Failed to execute 'dispatchEvent' on 'EventTarget': parameter 1 is not of type 'Event'.");
        if (event._uninit || event.eventPhase !== 0) throw new DOMException("Failed to execute 'dispatchEvent' on 'EventTarget': The event is already being dispatched.", 'InvalidStateError');
        return fireOn(self, event);
    }
}
const ABORT_FIRE = (t, ev) => { evTrusted(ev); fireOn(t, ev); };
)JS"
#include "abort_shared.inc"
R"JS(
const OBS_REPORT = e => W.reportErr(e);
)JS"
#include "observable_shared.inc"
R"JS(
class WorkerGlobalScope extends EventTarget {}
class DedicatedWorkerGlobalScope extends WorkerGlobalScope {}
class SharedWorkerGlobalScope extends WorkerGlobalScope {}
class ServiceWorkerGlobalScope extends WorkerGlobalScope {}
Object.setPrototypeOf(G, (W.service ? ServiceWorkerGlobalScope : W.shared ? SharedWorkerGlobalScope : DedicatedWorkerGlobalScope).prototype);

const URLN = W;
)JS"
#include "url_shared.inc"
#include "urlpattern_shared.inc"
R"JS(
const blobURLs = new Map(); let blobURLSeq = 0;
Object.defineProperties(URL, {
    createObjectURL: { value: function createObjectURL(b) { const u = 'blob:' + self.location.origin + '/' + (++blobURLSeq).toString(16).padStart(8, '0') + '-lumenw'; blobURLs.set(u, b); return u; }, writable: true, enumerable: true, configurable: true },
    revokeObjectURL: { value: function revokeObjectURL(u) { blobURLs.delete(String(u)); }, writable: true, enumerable: true, configurable: true },
});
class WorkerLocation { toString() { return this.href; } }
{
    const p = W.urlParse(W.url, null) || [W.url, '', '', '', '', '', '', '', '', '', 'null'];
    ['href', 'protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin'].forEach((k, i) => {
        if (i !== 2 && i !== 3) Object.defineProperty(WorkerLocation.prototype, k, { get() { return p[i]; }, enumerable: true, configurable: true });
    });
}
const location = Object.create(WorkerLocation.prototype);
class WorkerNavigator {}
const ua = W.ua, navigator = Object.assign(Object.create(WorkerNavigator.prototype), { userAgent: ua, appVersion: ua.replace(/^Mozilla\//, ''), appName: 'Netscape', appCodeName: 'Mozilla', product: 'Gecko', platform: W.platform, language: 'en-US', languages: ['en-US'], onLine: true, hardwareConcurrency: W.cores });
/* a secure context: https, file, or a loopback host (as Chrome treats them) */
const secureContext = (l => {
    if (l.protocol === 'blob:') { try { l = new URL(l.pathname); } catch (e) { return true; } }   /* the origin it was made in (blob:null from a file page) */
    return l.protocol === 'https:' || l.protocol === 'file:' || /^(localhost|127(\.\d+){3}|\[::1\])$|\.localhost$/.test(l.hostname);
})(location);
const CN = W, CRYPTO_SECURE = () => secureContext;
)JS"
#include "crypto_shared.inc"
R"JS(

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) {
    s = String(s); let o = '';
    for (let i = 0; i < s.length; i += 3) {
        const a = s.charCodeAt(i), b = i + 1 < s.length ? s.charCodeAt(i + 1) : 0, c = i + 2 < s.length ? s.charCodeAt(i + 2) : 0;
        if (a > 255 || b > 255 || c > 255) throw new DOMException("Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range.", 'InvalidCharacterError');
        const n = a << 16 | b << 8 | c;
        o += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < s.length ? B64[n >> 6 & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '=');
    }
    return o;
}
function atob(s) {
    s = String(s).replace(/[\t\n\f\r ]/g, '');
    if (s.length % 4 === 0) s = s.replace(/==?$/, '');
    if (s.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(s)) throw new DOMException("Failed to execute 'atob': The string to be decoded is not correctly encoded.", 'InvalidCharacterError');
    let o = '', bits = 0, n = 0;
    for (const ch of s) { n = (n << 6 | B64.indexOf(ch)) & 0xffffff; bits += 6; if (bits >= 8) { bits -= 8; o += String.fromCharCode(n >> bits & 255); } }
    return o;
}
)JS"
#include "streams_shared.inc"
R"JS(
const ENCN = W;
)JS"
#include "encoding_shared.inc"
R"JS(
const ZN = W;
)JS"
#include "compression_shared.inc"
R"JS(
class Headers {
    constructor(h) { def(this, '_m', new Map()); if (h) for (const [k, v] of (h instanceof Headers ? h._m : Array.isArray(h) ? h : Object.entries(h))) this.append(k, v); }
    get(k) { return this._m.get(String(k).toLowerCase()) ?? null; } has(k) { return this._m.has(String(k).toLowerCase()); }
    set(k, v) { this._m.set(String(k).toLowerCase(), String(v)); } append(k, v) { k = String(k).toLowerCase(); this._m.set(k, this._m.has(k) ? this._m.get(k) + ', ' + v : String(v)); }
    delete(k) { this._m.delete(String(k).toLowerCase()); } forEach(f) { this._m.forEach((v, k) => f(v, k, this)); }
    entries() { return this._m.entries(); } keys() { return this._m.keys(); } values() { return this._m.values(); } [Symbol.iterator]() { return this._m.entries(); }
}
const blobBytes = parts => {
    const bufs = (parts || []).map(x => x instanceof Blob ? x._b : x instanceof ArrayBuffer ? new Uint8Array(x) : ArrayBuffer.isView(x) ? new Uint8Array(x.buffer, x.byteOffset, x.byteLength) : new TextEncoder().encode(String(x)));
    const out = new Uint8Array(bufs.reduce((n, b) => n + b.length, 0)); let o = 0; for (const b of bufs) { out.set(b, o); o += b.length; } return out;
};
class Blob {
    constructor(parts, opts) { Object.defineProperty(this, '_b', { value: blobBytes(parts) }); this.type = opts && opts.type ? String(opts.type).toLowerCase() : ''; }
    get size() { return this._b.length; }
    slice(a = 0, b = this.size, type = '') { const n = this.size, f = x => x < 0 ? Math.max(n + x, 0) : Math.min(x, n); return new Blob([this._b.slice(f(a), Math.max(f(a), f(b)))], { type }); }
    arrayBuffer() { return Promise.resolve(this._b.slice().buffer); }
    bytes() { return Promise.resolve(this._b.slice()); }
    text() { return Promise.resolve(W.decode(this._b.slice().buffer)); }
    get [Symbol.toStringTag]() { return 'Blob'; }
}
class File extends Blob { constructor(parts, name, opts) { super(parts, opts); this.name = String(name); this.lastModified = opts && opts.lastModified || Date.now(); } }
const responseState = new WeakMap();
const responseInfo = r => { const s = responseState.get(r); if (!s) throw new TypeError('Expected a Response'); return s; };
function responseTake(response) {
    const s = responseInfo(response);
    if (s.used) throw new TypeError('Body has already been consumed.');
    if (s.bytes === null) return null;
    s.used = true; let done = false;
    return {read() { if (done) return Promise.resolve({done:true}); done = true; return Promise.resolve({done:false,value:s.bytes.slice()}); }, cancel() { done = true; return Promise.resolve(); }, releaseLock() {}};
}
class Response {
    constructor(body = null, init = {}) { responseState.set(this, {bytes: body === null ? null : blobBytes([body]), status: init.status ?? 200, statusText: init.statusText || '', url: init.url || '', headers: new Headers(init.headers), used: false, type: 'basic'}); W.wasmResponseBrand(this); }
    get ok() { const s = responseInfo(this); return s.status >= 200 && s.status < 300; }
    get bodyUsed() { return responseInfo(this).used; }
    async arrayBuffer() { const reader = responseTake(this); return reader ? (await reader.read()).value.buffer : new ArrayBuffer(0); }
    async text() { return W.decode(await this.arrayBuffer()); }
    async json() { return JSON.parse(await this.text()); }
    clone() { const s = responseInfo(this); if (s.used) throw new TypeError('Body has already been consumed.'); return new Response(s.bytes, s); }
}
for (const key of ['status','statusText','url','headers','type']) Object.defineProperty(Response.prototype, key, {configurable:true, enumerable:true, get() { return responseInfo(this)[key]; }});
W.wasmResponse(response => {
    const s = responseInfo(response);
    return {mime: Headers.prototype.get.call(s.headers, 'content-type') || '', status: s.status, type: s.type, url: s.url, take: () => responseTake(response)};
});
function fetch(input, init) {
    init = init || {};
    try {
        const u = W.resolve(String(input && input.url || input));
        if (u == null) throw new TypeError('Failed to fetch: invalid URL');
        const method = String(init.method || (input && input.method) || 'GET').toUpperCase();
        const hdrs = new Headers(init.headers || (input && input.headers) || undefined);
        let body = init.body == null ? undefined : init.body;
        if (body instanceof URLSearchParams) { if (!hdrs.has('content-type')) hdrs.set('content-type', 'application/x-www-form-urlencoded;charset=UTF-8'); body = body.toString(); }
        else if (typeof body === 'string') { if (!hdrs.has('content-type')) hdrs.set('content-type', 'text/plain;charset=UTF-8'); }
        else if (body !== undefined && !(body instanceof ArrayBuffer) && !ArrayBuffer.isView(body)) body = String(body);
        const flat = []; hdrs.forEach((v, k) => flat.push(k, v));
        const r = W.fetch(u, method, body, flat, init.credentials === 'omit');
        if (!r) return Promise.reject(new TypeError('Failed to fetch'));
        const rh = new Headers(); for (let i = 0; i + 1 < r[3].length; i += 2) rh.append(r[3][i], r[3][i + 1]);
        return Promise.resolve(new Response(r[2], { status: r[0], url: r[1], headers: rh }));
    } catch (e) { return Promise.reject(e); }
}
const CACHE_URL = input => W.resolve(String(input !== null && typeof input === 'object' && input.url !== undefined ? input.url : input));
const CACHE_RESP = e => { const r = new Response(e.bytes, { status: e.status, statusText: e.statusText, headers: e.headers, url: e.url }); responseInfo(r).type = e.type; return r; };
)JS"
#include "cache_shared.inc"
R"JS(
class XMLHttpRequestEventTarget extends EventTarget {}
class XMLHttpRequestUpload extends XMLHttpRequestEventTarget {}
class ProgressEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.lengthComputable = !!i.lengthComputable; this.loaded = i.loaded || 0; this.total = i.total || 0; } }
class XMLHttpRequest extends XMLHttpRequestEventTarget {   /* over W.fetch, which blocks this worker's thread; async requests finish on a later task */
    constructor() { super(); def(this, '_x', { rs: 0, h: [], rh: null, st: 0, stt: '', url: '', body: null, gen: 0 }); this.responseType = ''; this.timeout = 0; this.withCredentials = false; this.upload = new XMLHttpRequestUpload(); for (const k of ['onreadystatechange', 'onloadstart', 'onprogress', 'onload', 'onloadend', 'onerror', 'onabort', 'ontimeout']) this[k] = null; }
    get readyState() { return this._x.rs; } get status() { return this._x.st; } get statusText() { return this._x.stt; } get responseURL() { return this._x.url; }
    get responseText() { const x = this._x; if (this.responseType && this.responseType !== 'text') throw new DOMException("Failed to read 'responseText': the object's responseType is '" + this.responseType + "'.", 'InvalidStateError'); return x.body ? W.decode(x.body) : ''; }
    get response() {
        const x = this._x, t = this.responseType;
        if (t === '' || t === 'text') return this.responseText;
        if (x.rs !== 4 || !x.body) return null;
        if (t === 'json') { try { return JSON.parse(W.decode(x.body)); } catch (e) { return null; } }
        if (t === 'blob') return new Blob([x.body], { type: x.rh && x.rh.get('content-type') || '' });
        return t === 'arraybuffer' ? x.body : null;
    }
    open(m, u, async = true) { const x = this._x; x.gen++; Object.assign(x, { m: String(m).toUpperCase(), u: W.resolve(String(u)), async: async !== false, h: [], rh: null, st: 0, stt: '', url: '', body: null, sent: false }); if (x.u == null) throw new DOMException("Failed to execute 'open': Invalid URL", 'SyntaxError'); this._state(1); }
    setRequestHeader(k, v) { if (this._x.rs !== 1 || this._x.sent) throw new DOMException("Failed to execute 'setRequestHeader': The object's state must be OPENED.", 'InvalidStateError'); this._x.h.push(String(k), String(v)); }
    getResponseHeader(k) { const h = this._x.rh; return h ? h.get(k) : null; }
    getAllResponseHeaders() { const h = this._x.rh; if (!h) return ''; let s = ''; h.forEach((v, k) => s += k + ': ' + v + '\r\n'); return s; }
    overrideMimeType() {}
    abort() { const x = this._x; x.gen++; if (x.sent && x.rs !== 4) { x.sent = false; x.rs = 4; this._fire('readystatechange'); this._fire('abort'); this._fire('loadend'); } x.rs = 0; }
    send(body) {
        const x = this._x;
        if (x.rs !== 1 || x.sent) throw new DOMException("Failed to execute 'send': The object's state must be OPENED.", 'InvalidStateError');
        x.sent = true; const gen = x.gen;
        if (body instanceof URLSearchParams) { if (!x.h.some((k, i) => !(i & 1) && k.toLowerCase() === 'content-type')) x.h.push('content-type', 'application/x-www-form-urlencoded;charset=UTF-8'); body = body.toString(); }
        else if (body instanceof Blob) body = body._b;
        else if (body != null && typeof body !== 'string' && !(body instanceof ArrayBuffer) && !ArrayBuffer.isView(body)) body = String(body);
        const run = () => {
            if (gen !== x.gen) return;
            const r = W.fetch(x.u, x.m, x.m === 'GET' || x.m === 'HEAD' || body == null ? undefined : body, x.h, false);
            if (!r) { x.rs = 4; x.sent = false; this._fire('readystatechange'); if (!x.async) throw new DOMException("Failed to execute 'send': Failed to load '" + x.u + "'.", 'NetworkError'); this._fire('error'); this._fire('loadend'); return; }
            x.st = r[0]; x.url = r[1]; x.body = r[2]; x.rh = new Headers(); for (let i = 0; i + 1 < r[3].length; i += 2) x.rh.append(r[3][i], r[3][i + 1]);
            const n = x.body.byteLength;
            this._state(2); this._state(3); this._fire('progress', n); x.rs = 4; x.sent = false; this._fire('readystatechange'); this._fire('load', n); this._fire('loadend', n);
        };
        if (!x.async) return run();
        this._fire('loadstart'); setTimeout(run, 0);
    }
    _state(s) { this._x.rs = s; this._fire('readystatechange'); }
    _fire(t, n) { fireOn(this, t === 'readystatechange' ? new Event(t) : new ProgressEvent(t, { lengthComputable: n > 0, loaded: n || 0, total: n || 0 })); }
}
for (const [i, k] of ['UNSENT', 'OPENED', 'HEADERS_RECEIVED', 'LOADING', 'DONE'].entries()) { XMLHttpRequest[k] = i; XMLHttpRequest.prototype[k] = i; }
const toFn =f => typeof f === 'function' ? f : (0, eval)('(function(){' + String(f) + '\n})');
const performance = { now: () => W.now(), timeOrigin: W.timeOrigin, mark() {}, measure() {}, getEntriesByName() { return []; }, getEntriesByType() { return []; }, toJSON() { return { timeOrigin: W.timeOrigin }; } };
const fmt = a => a.map(x => typeof x === 'string' ? x : x instanceof Error ? x.stack || String(x) : (() => { try { return JSON.stringify(x); } catch (e) { return String(x); } })()).join(' ');
const console = { log: (...a) => W.log(1, fmt(a)), info: (...a) => W.log(1, fmt(a)), debug: (...a) => W.log(0, fmt(a)), warn: (...a) => W.log(2, fmt(a)), error: (...a) => W.log(3, fmt(a)), trace() {}, group() {}, groupEnd() {}, time() {}, timeEnd() {}, assert: (c, ...a) => { if (!c) W.log(3, 'Assertion failed: ' + fmt(a)); } };
const wports = new Map(); let wpseq = 0;
class MessagePort extends EventTarget {
    constructor() { super(); this.onmessage = null; this.onmessageerror = null; Object.defineProperty(this, '_other', { value: null, writable: true }); Object.defineProperty(this, '_k', { value: 0, writable: true }); }
    postMessage(d, t) { const ps = (Array.isArray(t) ? t : t && t.transfer || []).filter(p => p instanceof MessagePort), k = this._k || (this._other && this._other._k); if (k) { W.post({ __lumenPort: k, data: d, xfer: ps.map(p => { const j = -(++wpseq); p._k = j; if (p._other) wports.set(j, p._other); return j; }) }); return; } const o = this._other; if (o) { const c = W.clone(d); setTimeout(() => { const e = new MessageEvent('message', { data: c, ports: ps }); evTrusted(e); fireOn(o, e); }); } }
    start() {} close() { this._other = null; }
}
class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } }
/* A service worker's side of the registration. The page drives the lifecycle with {__lumenSW} messages (install,
   activate, state, msg) and this side answers with evaluated / installed / activated, and cmsg for Client.postMessage. */
const xferOut = (t, op) => {
    const transfer = Array.isArray(t) ? t : t && typeof t === 'object' && t.transfer || [];
    for (const item of transfer) if (W.isWasmModule(item)) throw new DOMException(`Failed to execute '${op}': WebAssembly.Module is not transferable.`, 'DataCloneError');
    return transfer.filter(p => p instanceof MessagePort).map(p => { const k = -(++wpseq); p._k = k; if (p._other) wports.set(k, p._other); return k; });
};
const swPost = (m, keys) => keys.length ? W.post({ __lumenXfer: keys, data: m }) : W.post(m);
const extPending = new WeakMap();
class ExtendableEvent extends Event {
    constructor(type, init = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to construct 'ExtendableEvent': 1 argument required, but only 0 present.");
        super(type, init);
        extPending.set(this, { list: [], n: 0 });
    }
    waitUntil(p) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'waitUntil' on 'ExtendableEvent': 1 argument required, but only 0 present.");
        const st = extPending.get(this);
        if (!st) throw new TypeError('Illegal invocation');
        if (!this.isTrusted) throw new DOMException("Failed to execute 'waitUntil' on 'ExtendableEvent': The event is not trusted.", 'InvalidStateError');
        if (this.eventPhase === 0 && st.n === 0) throw new DOMException("Failed to execute 'waitUntil' on 'ExtendableEvent': The event handler is already finished.", 'InvalidStateError');
        st.n++;
        const q = Promise.resolve(p);
        st.list.push(q);
        q.then(() => { setTimeout(() => st.n--); }, () => { setTimeout(() => st.n--); });
    }
}
/* the promises an extendable event's handlers passed to waitUntil, including ones added while those settle */
const extSettle = async e => {
    const st = extPending.get(e); let ok = true, seen = 0;
    while (seen < st.list.length) {
        const batch = st.list.slice(seen); seen = st.list.length;
        for (const r of await Promise.allSettled(batch)) if (r.status === 'rejected') ok = false;
    }
    return ok;
};
class ExtendableMessageEvent extends ExtendableEvent {
    #data; #origin; #lastEventId; #source; #ports;
    constructor(type, init = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to construct 'ExtendableMessageEvent': 1 argument required, but only 0 present.");
        super(type, init);
        const i = init == null ? {} : init;
        this.#data = i.data === undefined ? null : i.data;
        this.#origin = i.origin === undefined ? '' : String(i.origin);
        this.#lastEventId = i.lastEventId === undefined ? '' : String(i.lastEventId);
        const src = i.source === undefined ? null : i.source;
        if (src !== null && !(src instanceof Client || src instanceof ServiceWorker || src instanceof MessagePort))
            throw new TypeError("Failed to construct 'ExtendableMessageEvent': The provided value is not of type '(Client or ServiceWorker or MessagePort)'.");
        this.#source = src;
        const ports = i.ports === undefined ? [] : [...i.ports];
        for (const p of ports) if (!(p instanceof MessagePort)) throw new TypeError("Failed to construct 'ExtendableMessageEvent': Failed to read the 'ports' property from 'ExtendableMessageEventInit': Failed to convert value to 'MessagePort'.");
        this.#ports = Object.freeze(ports);
    }
    get data() { return this.#data; } get origin() { return this.#origin; } get lastEventId() { return this.#lastEventId; }
    get source() { return this.#source; } get ports() { return this.#ports; }
}
const CLIENT_KEY = Symbol('Client');
class Client {
    #info;
    constructor(key, info) { if (key !== CLIENT_KEY) throw new TypeError('Illegal constructor'); this.#info = info; }
    get url() { return this.#info.url; } get frameType() { return this.#info.frameType || 'top-level'; }
    get id() { return this.#info.id; } get type() { return this.#info.type || 'window'; }
    postMessage(message, transfer = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage' on 'Client': 1 argument required, but only 0 present.");
        const keys = xferOut(transfer, 'postMessage');
        swPost({ __lumenSW: 'cmsg', client: this.#info.id, data: message }, keys);
    }
}
class WindowClient extends Client {
    get visibilityState() { return swInfo.client && swInfo.client.visibility || 'visible'; }
    get focused() { return !!(swInfo.client && swInfo.client.focused); }
    get ancestorOrigins() { return Object.freeze([]); }
    focus() { return Promise.reject(new DOMException("Failed to execute 'focus' on 'WindowClient': Not allowed to focus a window.", 'InvalidAccessError')); }
    navigate(url) { return Promise.reject(new TypeError("Failed to execute 'navigate' on 'WindowClient': Cannot navigate a client that is not controlled.")); }
}
const swInfo = { scope: '', client: null, controlled: false, claimed: false };
const clientObjs = new Map();
const clientOf = info => { let c = clientObjs.get(info.id); if (!c) clientObjs.set(info.id, c = new WindowClient(CLIENT_KEY, info)); return c; };
class Clients {
    constructor(key) { if (key !== CLIENT_KEY) throw new TypeError('Illegal constructor'); }
    get(id) {
        if (arguments.length < 1) return Promise.reject(new TypeError("Failed to execute 'get' on 'Clients': 1 argument required, but only 0 present."));
        const c = swInfo.client;
        return Promise.resolve(c && c.id === String(id) ? clientOf(c) : undefined);
    }
    matchAll(options = undefined) {
        const o = options == null ? {} : options, type = o.type === undefined ? 'window' : String(o.type);
        if (!['window', 'worker', 'sharedworker', 'all'].includes(type)) return Promise.reject(new TypeError(`Failed to execute 'matchAll' on 'Clients': The provided value '${type}' is not a valid enum value of type ClientType.`));
        const c = swInfo.client, want = type === 'window' || type === 'all';
        return Promise.resolve(c && want && (o.includeUncontrolled || swInfo.controlled) ? [clientOf(c)] : []);
    }
    openWindow(url) { return Promise.reject(new DOMException("Failed to execute 'openWindow' on 'Clients': Not allowed to open a window.", 'InvalidAccessError')); }
    claim() {
        if (swSelf.state !== 'activating' && swSelf.state !== 'activated') return Promise.reject(new DOMException("Failed to execute 'claim' on 'Clients': Only the active worker can claim clients.", 'InvalidStateError'));
        W.post({ __lumenSW: 'claim' });
        swInfo.controlled = true;
        return Promise.resolve();
    }
}
const SW_KEY = Symbol('ServiceWorker');
class ServiceWorker extends EventTarget {
    #url; #state = 'parsed';
    constructor(key, url) { if (key !== SW_KEY) throw new TypeError('Illegal constructor'); super(); this.#url = url; this.onstatechange = null; this.onerror = null; }
    get scriptURL() { return this.#url; } get state() { return this.#state; }
    postMessage(message, transfer = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage' on 'ServiceWorker': 1 argument required, but only 0 present.");
        if (this.#state === 'redundant') throw new DOMException("Failed to execute 'postMessage' on 'ServiceWorker': The ServiceWorker is in redundant state.", 'InvalidStateError');
        const data = W.clone(message), src = this;
        setTimeout(() => { const e = new ExtendableMessageEvent('message', { data, origin: location.origin, source: src }); evTrusted(e); fireOn(G, e); });
    }
    static { swSetState = (w, s) => { if (w.#state === s) return; w.#state = s; const e = new Event('statechange'); evTrusted(e); fireOn(w, e); }; }
}
var swSetState;
class NavigationPreloadManager {
    #on = false; #hdr = 'true';
    constructor(key) { if (key !== SW_KEY) throw new TypeError('Illegal constructor'); }
    enable() { this.#on = true; return Promise.resolve(); }
    disable() { this.#on = false; return Promise.resolve(); }
    setHeaderValue(v) { this.#hdr = String(v); return Promise.resolve(); }
    getState() { return Promise.resolve({ enabled: this.#on, headerValue: this.#hdr }); }
}
class ServiceWorkerRegistration extends EventTarget {
    constructor(key) { if (key !== SW_KEY) throw new TypeError('Illegal constructor'); super(); this.onupdatefound = null; def(this, '_nav', new NavigationPreloadManager(SW_KEY)); }
    get installing() { return swSelf.state === 'installing' ? swSelf : null; }
    get waiting() { return swSelf.state === 'installed' ? swSelf : null; }
    get active() { return swSelf.state === 'activating' || swSelf.state === 'activated' ? swSelf : null; }
    get scope() { return swInfo.scope; } get updateViaCache() { return swInfo.updateViaCache || 'imports'; }
    get navigationPreload() { return this._nav; }
    update() { return Promise.resolve(this); }
    unregister() { W.post({ __lumenSW: 'unregister' }); return Promise.resolve(true); }
    showNotification() { return Promise.reject(new TypeError('No notification permission has been granted for this origin.')); }
    getNotifications() { return Promise.resolve([]); }
}
class FetchEvent extends ExtendableEvent {
    #request; #clientId; #resultingClientId; #handled;
    constructor(type, init) {
        if (arguments.length < 2) throw new TypeError(`Failed to construct 'FetchEvent': 2 arguments required, but only ${arguments.length} present.`);
        super(type, init);
        if (init == null || init.request === undefined) throw new TypeError("Failed to construct 'FetchEvent': Failed to read the 'request' property from 'FetchEventInit': Required member is undefined.");
        this.#request = init.request; this.#clientId = init.clientId === undefined ? '' : String(init.clientId);
        this.#resultingClientId = init.resultingClientId === undefined ? '' : String(init.resultingClientId);
        this.#handled = init.handled instanceof Promise ? init.handled : Promise.resolve();
    }
    get request() { return this.#request; } get clientId() { return this.#clientId; } get resultingClientId() { return this.#resultingClientId; }
    get handled() { return this.#handled; } get preloadResponse() { return Promise.resolve(undefined); }
    respondWith(r) { if (!this.isTrusted) throw new DOMException("Failed to execute 'respondWith' on 'FetchEvent': The event is not trusted.", 'InvalidStateError'); }
}
const swSelf = W.service ? new ServiceWorker(SW_KEY, location.href) : null;
const swReg = W.service ? new ServiceWorkerRegistration(SW_KEY) : null;
const swClients = W.service ? new Clients(CLIENT_KEY) : null;
const swFire = async (type, slot) => {
    const e = new ExtendableEvent(type); evTrusted(e); fireOn(G, e);
    return extSettle(e);
};
const swHandle = async (d, ports) => {
    if (d.client) swInfo.client = d.client;
    if (d.scope) { swInfo.scope = d.scope; swInfo.updateViaCache = d.updateViaCache; }
    if (d.controlled !== undefined) swInfo.controlled = d.controlled;
    switch (d.__lumenSW) {
    case 'install':
        swSetState(swSelf, 'installing');
        W.post({ __lumenSW: 'evaluated' });
        W.post({ __lumenSW: 'installed', ok: await swFire('install') });
        break;
    case 'wake': swSetState(swSelf, d.state); break;
    case 'activate':
        swSetState(swSelf, 'installed'); swSetState(swSelf, 'activating');
        await swFire('activate');
        swSetState(swSelf, 'activated');
        W.post({ __lumenSW: 'activated' });
        break;
    case 'state': swSetState(swSelf, d.state); break;
    case 'msg': {
        const c = d.client ? clientOf(d.client) : null;
        const e = new ExtendableMessageEvent('message', { data: d.data, origin: c ? new URL(c.url).origin : '', source: c, ports });
        evTrusted(e); fireOn(G, e);
        break;
    }
    case 'msgerror': { const e = new ExtendableMessageEvent('messageerror', {}); evTrusted(e); fireOn(G, e); break; }
    }
};
const api = {
    self: G, globalThis: G, location, navigator, name: W.name, origin: location.origin, isSecureContext: secureContext, crossOriginIsolated: false,
    onmessage: null, onmessageerror: null, onerror: null, onunhandledrejection: null, onrejectionhandled: null, onlanguagechange: null, onoffline: null, ononline: null,
    postMessage(m, t) { if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage': 1 argument required, but only 0 present."); const transfer = Array.isArray(t) ? t : t && t.transfer || []; for (const item of transfer) if (W.isWasmModule(item)) throw new DOMException('WebAssembly.Module is not transferable.', 'DataCloneError'); const ps = transfer.filter(p => p instanceof MessagePort); if (!ps.length) return W.post(m); W.post({ __lumenXfer: ps.map(p => { const k = -(++wpseq); p._k = k; if (p._other) wports.set(k, p._other); return k; }), data: m }); },
    close() { W.close(); },
    reportError(e) { if (!arguments.length) throw new TypeError("Failed to execute 'reportError' on 'WorkerGlobalScope': 1 argument required, but only 0 present."); W.reportErr(e); },
    importScripts(...urls) { for (const u of urls) W.importScript(String(u)); },
    setTimeout: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, false, args), setInterval: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, true, args),
    clearTimeout: id => W.clear(+id || 0), clearInterval: id => W.clear(+id || 0),
    queueMicrotask: f => { Promise.resolve().then(() => f()); }, structuredClone: (v, options) => { if (options && options.transfer) for (const item of options.transfer) if (W.isWasmModule(item)) throw new DOMException('WebAssembly.Module is not transferable.', 'DataCloneError'); if (v instanceof URL || v instanceof URLSearchParams) throw new DOMException(`${v[Symbol.toStringTag]} object could not be cloned.`, 'DataCloneError'); return W.clone(v); },
    btoa, atob, TextEncoder, TextDecoder, TextEncoderStream, TextDecoderStream, CompressionStream, DecompressionStream, ReadableStream, ReadableStreamDefaultReader, ReadableStreamBYOBReader,
    ReadableStreamDefaultController, ReadableByteStreamController, ReadableStreamBYOBRequest, WritableStream, WritableStreamDefaultWriter,
    WritableStreamDefaultController, TransformStream, TransformStreamDefaultController, ByteLengthQueuingStrategy, CountQueuingStrategy, Blob, File, URL, URLSearchParams, Headers, Response, fetch, Cache, CacheStorage, caches, URLPattern, XMLHttpRequest, XMLHttpRequestEventTarget, XMLHttpRequestUpload, ProgressEvent, performance, console,
    Event, MessageEvent, MessagePort, MessageChannel, ErrorEvent, AbortController, AbortSignal, Observable, Subscriber, CustomEvent, PromiseRejectionEvent, EventTarget, DOMException, QuotaExceededError, Crypto,
    ...(secureContext ? { CryptoKey, SubtleCrypto } : {}), WorkerGlobalScope, DedicatedWorkerGlobalScope, WorkerLocation, WorkerNavigator,
};
if (W.shared) {   /* a shared worker talks only through the ports its connect events bring */
    delete api.postMessage; delete api.onmessage; delete api.onmessageerror; delete api.DedicatedWorkerGlobalScope;
    Object.assign(api, { onconnect: null, SharedWorkerGlobalScope });
}
if (W.service) {   /* a service worker: no postMessage of its own; the page's clients and registration */
    delete api.postMessage; delete api.DedicatedWorkerGlobalScope;
    Object.assign(api, { oninstall: null, onactivate: null, onfetch: null, onmessage: null, onmessageerror: null, oncontentdelete: null, onnotificationclick: null, onnotificationclose: null, onpush: null, onpushsubscriptionchange: null, onsync: null, onperiodicsync: null, onbackgroundfetchsuccess: null, onbackgroundfetchfail: null, onbackgroundfetchabort: null, onbackgroundfetchclick: null, oncookiechange: null,
        clients: swClients, registration: swReg, serviceWorker: swSelf, skipWaiting() { return Promise.resolve(); },
        ServiceWorkerGlobalScope, ExtendableEvent, ExtendableMessageEvent, FetchEvent, Client, WindowClient, Clients, ServiceWorker, ServiceWorkerRegistration, NavigationPreloadManager });
}
for (const k of Object.keys(api)) Object.defineProperty(G, k, { value: api[k], writable: true, configurable: true, enumerable: false });
{   /* WebIDL shape, as on the page: members are enumerable and constants read-only */
    const CONST = /^[A-Z][A-Z0-9_]*$/;
    const shape = (o, skip) => {
        for (const k of Object.getOwnPropertyNames(o)) {
            if (skip.has(k) || k[0] === '_') continue;
            const d = Object.getOwnPropertyDescriptor(o, k);
            if (!d.configurable) continue;
            if (CONST.test(k) && typeof d.value === 'number') Object.defineProperty(o, k, { value: d.value, writable: false, enumerable: true, configurable: false });
            else if (!d.enumerable) { d.enumerable = true; Object.defineProperty(o, k, d); }
        }
    };
    const PROTO_SKIP = new Set(['constructor']), STATIC_SKIP = new Set(['length', 'name', 'prototype', 'arguments', 'caller']);
    for (const C of Object.values(api)) {
        if (typeof C !== 'function' || !/^[A-Z]/.test(C.name) || !C.prototype || typeof C.prototype !== 'object') continue;
        shape(C.prototype, PROTO_SKIP);
        shape(C, STATIC_SKIP);
    }
}
Object.defineProperty(WorkerGlobalScope.prototype, 'crypto', { get: Object.getOwnPropertyDescriptor({ get crypto() { if (this !== globalThis) throw new TypeError('Illegal invocation'); return crypto; } }, 'crypto').get, enumerable: true, configurable: true });
return {
    onmsg(data) {
        let ports = [];
        if (data && typeof data === 'object') {
            if (data.__lumenPort) { const p = wports.get(data.__lumenPort); if (p) { const e = new MessageEvent('message', { data: data.data, ports: (data.xfer || []).map(k => { const q = new MessagePort(); q._k = k; wports.set(k, q); return q; }) }); evTrusted(e); fireOn(p, e); } return; }
            if (data.__lumenXfer) { ports = data.__lumenXfer.map(k => { const q = new MessagePort(); q._k = k; wports.set(k, q); return q; }); data = data.data; }
        }
        if (W.service) { if (data && typeof data === 'object' && data.__lumenSW) swHandle(data, ports); return; }
        if (W.shared) {   /* a new SharedWorker object connected: its port */
            if (data && data.__lumenConnect && ports.length) { const e = new MessageEvent('connect', { data: '', ports, source: ports[0] }); evTrusted(e); fireOn(G, e); }
            return;
        }
        const e = new MessageEvent('message', { data, ports }); evTrusted(e); fireOn(G, e);
    },
    report(error, message, filename, lineno, colno) {
        const e = new ErrorEvent('error', { message, filename, lineno, colno, error, cancelable: true });
        evTrusted(e);
        if (typeof G.onerror === 'function') { try { if (G.onerror.call(G, message, filename, lineno, colno, error) === true) e.preventDefault(); } catch (x) {} }
        fireOn(G, e, true);
        return e.defaultPrevented;
    },
};
}))JS";

void run_timers(WEnv *E) {
    v8::Isolate *iso = E->iso; v8::HandleScope hs(iso); v8::Local<v8::Context> ctx = E->ctx.Get(iso);
    double now = now_ms();
    std::vector<std::pair<double, uint32_t>> due;
    for (auto &kv : E->timers) if (kv.second.due <= now) due.push_back({ kv.second.due, kv.first });
    std::sort(due.begin(), due.end());
    for (auto &d : due) {
        if (E->w->term || E->closing) return;
        auto it = E->timers.find(d.second);
        if (it == E->timers.end()) continue;
        v8::Local<v8::Function> fn = it->second.fn.Get(iso);
        std::vector<v8::Local<v8::Value>> args;
        for (auto &g : it->second.args) args.push_back(g.Get(iso));
        if (it->second.repeat) it->second.due = now_ms() + std::max(it->second.interval, 1.0);
        else E->timers.erase(it);
        v8::TryCatch tc(iso);
        (void)fn->Call(ctx, ctx->Global(), (int)args.size(), args.data());
        wcheck(E, tc);
        iso->PerformMicrotaskCheckpoint();
    }
}

void worker_main(std::shared_ptr<Worker> w) {
    v8::Isolate::CreateParams cp;
    std::unique_ptr<v8::ArrayBuffer::Allocator> al(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    cp.array_buffer_allocator = al.get();
    v8::Isolate *iso = v8::Isolate::Allocate();
    auto tasks = js_register_foreground_tasks(iso, [weak = std::weak_ptr<Worker>(w)] {
        if (auto owner = weak.lock()) {
            { std::lock_guard<std::mutex> lk(owner->mu); ++owner->task_version; }
            owner->cv.notify_one();
        }
    });
    v8::Isolate::Initialize(iso, cp);
    js_wasm_init(iso);
    { std::lock_guard<std::mutex> lk(w->mu); w->iso = iso; }
    iso->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    WEnv *E = new WEnv; E->w = w; E->iso = iso; E->t0 = now_ms();
    iso->SetData(kEnvSlot, E);
    iso->SetHostInitializeImportMetaObjectCallback(wm_meta);
    {
        v8::Isolate::Scope is(iso);
        v8::HandleScope hs(iso);
        v8::Local<v8::Context> ctx = v8::Context::New(iso);
        E->ctx.Reset(iso, ctx);
        v8::Context::Scope cs(ctx);
        v8::Local<v8::Object> W = v8::Object::New(iso);
        js_wasm_install(ctx, W);
        js_clone_install(ctx, W);
        js_url_install(ctx, W);
        js_crypto_install(ctx, W); js_encoding_install(ctx, W); js_compress_install(ctx, W);
#define WREG(nm) (void)W->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, w_##nm).ToLocalChecked())
        WREG(post); WREG(clone); WREG(timer); WREG(clear); WREG(close); WREG(now); WREG(log); WREG(decode); WREG(resolve); WREG(fetch); WREG(importScript); WREG(reportErr);
        auto setS = [&](const char *k, const std::string &v) { (void)W->Set(ctx, jstr(iso, k), jstr(iso, v.c_str())); };
        setS("url", w->url); setS("ua", w->ua); setS("platform", w->platform); setS("name", w->name);
        (void)W->Set(ctx, jstr(iso, "shared"), v8::Boolean::New(iso, w->shared));
        (void)W->Set(ctx, jstr(iso, "service"), v8::Boolean::New(iso, w->service));
        (void)W->Set(ctx, jstr(iso, "cores"), v8::Integer::New(iso, (int)std::max(1u, std::thread::hardware_concurrency())));
        double wall = (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / 1000.0;
        (void)W->Set(ctx, jstr(iso, "timeOrigin"), v8::Number::New(iso, wall));
        {
            v8::TryCatch tc(iso);
            v8::ScriptOrigin origin(jstr(iso, "lumen:worker-prelude"));
            v8::Local<v8::Script> s; v8::Local<v8::Value> fv, api;
            v8::Local<v8::Value> argv[2] = { W, ctx->Global() };
            if (v8::Script::Compile(ctx, jstr(iso, kWorkerPrelude, (int)sizeof kWorkerPrelude - 1), &origin).ToLocal(&s) && s->Run(ctx).ToLocal(&fv) && fv->IsFunction() &&
                fv.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 2, argv).ToLocal(&api) && api->IsObject()) {
                v8::Local<v8::Value> f;
                if (api.As<v8::Object>()->Get(ctx, jstr(iso, "onmsg")).ToLocal(&f) && f->IsFunction()) E->onmsg.Reset(iso, f.As<v8::Function>());
                if (api.As<v8::Object>()->Get(ctx, jstr(iso, "report")).ToLocal(&f) && f->IsFunction()) E->report.Reset(iso, f.As<v8::Function>());
            }
            if (tc.HasCaught()) fprintf(stderr, "lumen: worker prelude failed: %s\n", jcstr(iso, tc.Exception()).c_str());
        }
        std::string src = w->src; bool ok = w->have_src;
        if (!ok && !w->term) {
            NetResponse *r = net_fetch_sync(net_request_new("GET", w->url.c_str()));
            if (r && r->status >= 200 && r->status < 300) { src.assign(r->body ? r->body : "", r->body_len); ok = true; }
            if (r) net_response_free(r);
        }
        if (!ok) post_out(E, OutMsg{ WM_LOADFAIL, {}, {}, {}, 0, 0 });
        else if (!w->term) {
            v8::HandleScope hs2(iso);
            v8::TryCatch tc(iso);
            v8::ScriptOrigin origin(jstr(iso, w->url.c_str()));
            v8::Local<v8::Script> s; v8::Local<v8::Value> r;
            if (w->module) {
                v8::Local<v8::Module> m;
                if (!wm_compile(E, w->url, src).ToLocal(&m)) { ok = false; post_out(E, OutMsg{ WM_LOADFAIL, {}, {}, {}, 0, 0 }); }
                else {
                    if (wm_graph(E, m) && m->InstantiateModule(ctx, wm_resolve_cb).FromMaybe(false)) (void)m->Evaluate(ctx).ToLocal(&r);
                    wcheck(E, tc);
                    iso->PerformMicrotaskCheckpoint();
                    if (m->GetStatus() == v8::Module::kErrored) { v8::Local<v8::Value> ex = m->GetException(); fprintf(stderr, "lumen: worker module failed: %s\n", jcstr(iso, ex).c_str()); }
                }
            } else if (!v8::Script::Compile(ctx, jsrc(iso, src.data(), src.size()), &origin).ToLocal(&s)) {
                ok = false;   /* classic-script parse errors fire a plain error Event on the Worker */
                post_out(E, OutMsg{ WM_LOADFAIL, {}, {}, {}, 0, 0 });
            } else {
                (void)s->Run(ctx).ToLocal(&r);
                wcheck(E, tc);
                iso->PerformMicrotaskCheckpoint();
            }
        }
        double last_act = now_ms(); bool idle_gc = false;
        while (ok && !w->term && !E->closing) {
            { v8::HeapStatistics hs; iso->GetHeapStatistics(&hs); w->heap = hs.total_heap_size(); w->ext = hs.external_memory(); }
            if (!idle_gc && now_ms() - last_act > 10000) { iso->LowMemoryNotification(); idle_gc = true; continue; }  /* idle worker: give memory back once */
            std::deque<Buf> msgs;
            {
                std::unique_lock<std::mutex> lk(w->mu);
                uint64_t version = w->task_version;
                double next = std::min(1e300, now_ms() + tasks->NextDelayMs());
                for (auto &kv : E->timers) next = std::min(next, kv.second.due);
                auto ready = [&] { return w->term.load() || !w->in.empty() || w->task_version != version; };
                if (!idle_gc) next = std::min(next, last_act + 10001);
                if (!ready()) {
                    if (next >= 1e299) w->cv.wait(lk, ready);
                    else { double ms = next - now_ms(); if (ms > 0) w->cv.wait_for(lk, std::chrono::microseconds((int64_t)(ms * 1000) + 1), ready); }
                }
                msgs.swap(w->in);
            }
            if (!msgs.empty()) { last_act = now_ms(); idle_gc = false; }
            for (Buf &b : msgs) {
                if (!w->term && !E->closing) {
                    v8::HandleScope hs2(iso);
                    v8::TryCatch tc(iso);
                    v8::Local<v8::Value> v;
                    if (js_deserialize(iso, ctx, b).ToLocal(&v) && !E->onmsg.IsEmpty()) { (void)E->onmsg.Get(iso)->Call(ctx, v8::Undefined(iso), 1, &v); wcheck(E, tc); }
                    iso->PerformMicrotaskCheckpoint();
                }
            }
            if (w->term) break;
            run_timers(E);
            while (!w->term && !E->closing) {
                v8::HandleScope hs2(iso);
                v8::TryCatch tc(iso);
                if (!tasks->RunOne()) break;
                iso->PerformMicrotaskCheckpoint();
                wcheck(E, tc);
                last_act = now_ms(); idle_gc = false;
            }
        }
        E->timers.clear(); E->mods.clear(); E->murl.clear(); E->onmsg.Reset(); E->report.Reset(); E->ctx.Reset();
    }
    std::deque<Buf> abandoned;
    {
        std::lock_guard<std::mutex> lk(w->mu);
        w->iso = nullptr;
        abandoned.swap(w->in);
    }
    abandoned.clear(); // Release retained native code outside the worker lock.
    tasks->Close();
    iso->Dispose();
    js_unregister_foreground_tasks(iso);
    delete E;
}

std::string wm_key(WEnv *E, v8::Local<v8::Module> m);
void wm_meta(v8::Local<v8::Context> ctx, v8::Local<v8::Module> m, v8::Local<v8::Object> meta) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    (void)meta->Set(ctx, jstr(iso, "url"), jstr(iso, wm_key((WEnv *)iso->GetData(kEnvSlot), m).c_str()));
}
std::string wm_key(WEnv *E, v8::Local<v8::Module> m) {
    auto r = E->murl.equal_range(m->GetIdentityHash());
    for (auto it = r.first; it != r.second; ++it) { auto f = E->mods.find(it->second); if (f != E->mods.end() && f->second.Get(E->iso) == m) return it->second; }
    return E->w->url;
}
v8::MaybeLocal<v8::Module> wm_compile(WEnv *E, const std::string &u, const std::string &src) {
    v8::Isolate *iso = E->iso;
    v8::ScriptOrigin origin(jstr(iso, u.c_str()), 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
    v8::ScriptCompiler::Source so(jsrc(iso, src.data(), src.size()), origin);
    v8::Local<v8::Module> m;
    if (!v8::ScriptCompiler::CompileModule(iso, &so).ToLocal(&m)) return {};
    E->mods[u].Reset(iso, m); E->murl.emplace(m->GetIdentityHash(), u);
    return m;
}
std::string wm_resolve(const std::string &spec, const std::string &base) {
    bool rel = !spec.compare(0, 1, "/") || !spec.compare(0, 2, "./") || !spec.compare(0, 3, "../");
    if (!rel && spec.find(':') == std::string::npos) return "";
    char *u = url_join(base.c_str(), spec.c_str()); std::string r = u ? u : ""; free(u); return r;
}
bool wm_graph(WEnv *E, v8::Local<v8::Module> m) {
    v8::Isolate *iso = E->iso;
    std::string base = wm_key(E, m);
    v8::Local<v8::FixedArray> rq = m->GetModuleRequests();
    for (int i = 0; i < rq->Length(); i++) {
        std::string sp = jcstr(iso, rq->Get(i).As<v8::ModuleRequest>()->GetSpecifier()), u = wm_resolve(sp, base);
        if (u.empty()) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("Failed to resolve module specifier \"" + sp + "\"").c_str()))); return false; }
        if (E->mods.count(u)) continue;
        NetResponse *r = net_fetch_sync(net_request_new("GET", u.c_str()));
        bool ok = r && r->status >= 200 && r->status < 300; std::string body;
        if (ok) body.assign(r->body ? r->body : "", r->body_len);
        if (r) net_response_free(r);
        if (!ok) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("Failed to fetch module: " + u).c_str()))); return false; }
        v8::Local<v8::Module> d;
        if (!wm_compile(E, u, body).ToLocal(&d) || !wm_graph(E, d)) return false;
    }
    return true;
}
v8::MaybeLocal<v8::Module> wm_resolve_cb(v8::Local<v8::Context>, v8::Local<v8::String> spec, v8::Local<v8::FixedArray>, v8::Local<v8::Module> ref) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    WEnv *E = (WEnv *)iso->GetData(kEnvSlot);
    auto it = E->mods.find(wm_resolve(jcstr(iso, spec), wm_key(E, ref)));
    if (it == E->mods.end()) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Failed to resolve module"))); return {}; }
    return it->second.Get(iso);
}

void *worker_thread(void *p) {
    std::unique_ptr<std::shared_ptr<Worker>> sp(static_cast<std::shared_ptr<Worker> *>(p));
    worker_main(*sp);
    return nullptr;
}

void worker_terminate(Worker *w) {
    std::deque<OutMsg> abandoned;
    {
        std::lock_guard<std::mutex> lk(w->mu);
        w->term = true;
        if (w->iso) w->iso->TerminateExecution();
        abandoned.swap(w->out);
    }
    w->cv.notify_all();
}

/* ---- main thread ---- */
void n_workerNew(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    auto w = std::make_shared<Worker>();
    w->id = ++g_wseq; w->owner = jctx(iso);
    w->url = jcstr(iso, a[0]); w->have_src = a[1]->IsString(); if (w->have_src) w->src = jcstr(iso, a[1]);
    w->ua = jcstr(iso, a[2]); w->platform = jcstr(iso, a[3]); w->name = a[4]->IsString() ? jcstr(iso, a[4]) : ""; w->module = a.Length() > 5 && a[5]->IsTrue();
    w->shared = a.Length() > 6 && a[6]->IsTrue();   /* a SharedWorker's global */
    w->service = a.Length() > 7 && a[7]->IsTrue();  /* a ServiceWorker's global */
    g_workers[w->id] = w;
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8u << 20); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    auto *arg = new std::shared_ptr<Worker>(w);
    if (pthread_create(&th, &at, worker_thread, arg) != 0) { delete arg; g_workers.erase(w->id); a.GetReturnValue().Set(0); pthread_attr_destroy(&at); return; }
    pthread_attr_destroy(&at);
    a.GetReturnValue().Set(w->id);
}
void n_workerPost(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    auto id = a[0]->Uint32Value(ctx);
    if (id.IsNothing()) return;
    Buf b;
    if (!js_serialize(iso, ctx, a[1], b)) return;
    // Serialization invokes author getters, which can terminate this worker.
    // Never retain a map iterator across that reentrant JavaScript.
    auto it = g_workers.find(id.FromJust());
    if (it == g_workers.end() || it->second->term || it->second->owner != jctx(iso)) return;
    Worker *w = it->second.get();
    { std::lock_guard<std::mutex> lk(w->mu); w->in.push_back(std::move(b)); }
    w->cv.notify_all();
}
void n_workerTerm(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    auto it = g_workers.find(a[0]->Uint32Value(ctx).FromMaybe(0));
    if (it == g_workers.end() || it->second->owner != jctx(iso)) return;
    worker_terminate(it->second.get());
    g_workers.erase(it);
}
} // namespace

void js_install_workers(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    (void)N->Set(ctx, jstr(iso, "workerNew"), v8::Function::New(ctx, n_workerNew).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "workerPost"), v8::Function::New(ctx, n_workerPost).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "workerTerm"), v8::Function::New(ctx, n_workerTerm).ToLocalChecked());
}

double workers_deadline(JsCtx *c) {
    for (auto &kv : g_workers) {
        if (kv.second->owner != c) continue;
        std::lock_guard<std::mutex> lk(kv.second->mu);
        if (!kv.second->out.empty()) return now_ms();
    }
    return 1e300;
}

void workers_pump(JsCtx *c) {
    std::deque<std::pair<uint32_t, std::deque<OutMsg>>> batch;
    for (auto &kv : g_workers) {
        if (kv.second->owner != c) continue;
        std::deque<OutMsg> q;
        { std::lock_guard<std::mutex> lk(kv.second->mu); q.swap(kv.second->out); }
        if (!q.empty()) batch.push_back({ kv.first, std::move(q) });
    }
    if (batch.empty()) return;
    JS_ENTER(c);
    v8::Local<v8::Value> fv;
    bool have = !c->api.IsEmpty() && c->api.Get(iso)->Get(ctx, jstr(iso, "workerEvent")).ToLocal(&fv) && fv->IsFunction();
    for (auto &b : batch)
        for (OutMsg &m : b.second) {
            if (have && g_workers.count(b.first)) {
                v8::HandleScope hs(iso);
                v8::Local<v8::Value> data = v8::Undefined(iso);
                int kind = m.kind;
                if (kind == WM_MESSAGE) { v8::TryCatch tc(iso); if (!js_deserialize(iso, ctx, m.data).ToLocal(&data)) { data = v8::Undefined(iso); kind = 3; } }
                v8::Local<v8::Value> argv[7] = { v8::Integer::NewFromUnsigned(iso, b.first), v8::Integer::New(iso, kind), data, jstr(iso, m.msg.c_str()), jstr(iso, m.file.c_str()), v8::Integer::New(iso, m.line), v8::Integer::New(iso, m.col) };
                (void)jcall(c, fv.As<v8::Function>(), v8::Undefined(iso), 7, argv);
            }
        }
}

void workers_kill(JsCtx *c) {
    for (auto it = g_workers.begin(); it != g_workers.end();)
        if (it->second->owner == c) { worker_terminate(it->second.get()); it = g_workers.erase(it); }
        else ++it;
}

extern "C" void worker_mem_stats(size_t *heap, size_t *ext, int *n) {
    *heap = *ext = 0; *n = 0;
    for (auto &kv : g_workers) { *heap += kv.second->heap; *ext += kv.second->ext; ++*n; }
}
