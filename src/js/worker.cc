/* Dedicated Web Workers: each worker runs on its own thread with its own V8 isolate.
   Messages cross threads as V8 ValueSerializer buffers. */
#include "js_int.h"
#include "jsglue.h"
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
struct Buf { uint8_t *p = nullptr; size_t n = 0; };
enum { WM_MESSAGE, WM_ERROR, WM_LOADFAIL };
struct OutMsg { int kind; Buf data; std::string msg, file; int line = 0, col = 0; };
struct Worker {
    uint32_t id = 0; JsCtx *owner = nullptr;
    std::string url, src, ua, platform, name; bool have_src = false;
    std::mutex mu; std::condition_variable cv;
    std::deque<Buf> in; std::deque<OutMsg> out;
    std::atomic<bool> term{false};
    v8::Isolate *iso = nullptr;
};
struct WTimer { double due = 0, interval = 0; bool repeat = false; v8::Global<v8::Function> fn; std::vector<v8::Global<v8::Value>> args; };
struct WEnv {
    std::shared_ptr<Worker> w; v8::Isolate *iso = nullptr;
    v8::Global<v8::Context> ctx; v8::Global<v8::Function> onmsg, report;
    std::map<uint32_t, WTimer> timers; uint32_t tseq = 0; bool closing = false, reporting = false; double t0 = 0;
};
std::map<uint32_t, std::shared_ptr<Worker>> g_workers;   /* main thread only */
uint32_t g_wseq;
const int kEnvSlot = 2;

void wake() { if (js_wakeup) js_wakeup(); }

bool ser(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> v, Buf &out) {
    v8::ValueSerializer s(iso);   /* no delegate: V8 throws DataCloneError itself (V8 is built without RTTI) */
    s.WriteHeader();
    if (!s.WriteValue(ctx, v).FromMaybe(false)) return false;
    auto r = s.Release(); out.p = r.first; out.n = r.second; return true;
}
v8::MaybeLocal<v8::Value> deser(v8::Isolate *iso, v8::Local<v8::Context> ctx, const Buf &b) {
    v8::ValueDeserializer d(iso, b.p, b.n);
    if (!d.ReadHeader(ctx).FromMaybe(false)) return {};
    return d.ReadValue(ctx);
}
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
WFN(post) { WENV; Buf b; if (ser(iso, ctx, a[0], b)) post_out(E, OutMsg{ WM_MESSAGE, b, {}, {}, 0, 0 }); }
WFN(clone) { WENV; Buf b; if (!ser(iso, ctx, a[0], b)) return; v8::Local<v8::Value> v; if (deser(iso, ctx, b).ToLocal(&v)) a.GetReturnValue().Set(v); free(b.p); }
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
    NetResponse *r = net_fetch_sync(rq);
    if (!r || r->status == 0) { if (r) net_response_free(r); a.GetReturnValue().SetNull(); return; }
    v8::Local<v8::Value> out[3] = { v8::Integer::New(iso, r->status), jstr(iso, r->url ? r->url : u.c_str()), make_ab(iso, r->body, r->body_len) };
    net_response_free(r);
    a.GetReturnValue().Set(v8::Array::New(iso, out, 3));
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
    if (v8::Script::Compile(ctx, jstr(iso, src.data(), (int)src.size()), &origin).ToLocal(&s)) (void)s->Run(ctx).ToLocal(&res);
}
WFN(reportErr) { WENV; werr(E, a[0], v8::Exception::CreateMessage(iso, a[0])); }

const char kWorkerPrelude[] = R"JS((function (W, G) {
'use strict';
const def = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false });
const codes = { IndexSizeError: 1, HierarchyRequestError: 3, WrongDocumentError: 4, InvalidCharacterError: 5, NotFoundError: 8, NotSupportedError: 9, InvalidStateError: 11, SyntaxError: 12, InvalidModificationError: 13, NamespaceError: 14, InvalidAccessError: 15, SecurityError: 18, NetworkError: 19, AbortError: 20, URLMismatchError: 21, QuotaExceededError: 22, TimeoutError: 23, DataCloneError: 25 };
class DOMException extends Error { constructor(message = '', name = 'Error') { super(String(message)); def(this, 'name', String(name)); } get code() { return codes[this.name] || 0; } }
class Event {
    constructor(type, init) { init = init || {}; this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed; this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.eventPhase = 0; this.isTrusted = false; this.timeStamp = W.now(); def(this, '_stop', false); }
    preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
    stopPropagation() {} stopImmediatePropagation() { this._stop = true; }
    get srcElement() { return this.target; } get returnValue() { return !this.defaultPrevented; } get cancelBubble() { return false; }
    composedPath() { return this.currentTarget ? [this.currentTarget] : []; }
}
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data === undefined ? null : i.data; this.origin = i.origin || ''; this.lastEventId = i.lastEventId || ''; this.source = i.source || null; this.ports = i.ports || []; } }
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message || ''; this.filename = i.filename || ''; this.lineno = i.lineno || 0; this.colno = i.colno || 0; this.error = i.error; } }
class CustomEvent extends Event { constructor(t, i) { super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null; } }
class PromiseRejectionEvent extends Event { constructor(t, i) { super(t, i); this.promise = i && i.promise; this.reason = i && i.reason; } }
const lsn = new WeakMap();
const listeners = t => { let m = lsn.get(t); if (!m) lsn.set(t, m = new Map()); return m; };
function fireOn(t, e, skipHandler) {
    e.target = t; e.currentTarget = t; e.eventPhase = 2;
    const h = t['on' + e.type];
    if (!skipHandler && typeof h === 'function') { try { const r = h.call(t, e); if (r === false && e.type !== 'error') e.preventDefault(); } catch (x) { W.reportErr(x); } }
    for (const l of (listeners(t).get(e.type) || []).slice()) {
        if (l.removed) continue;
        if (l.once) t.removeEventListener(e.type, l.fn);
        try { if (typeof l.fn === 'function') l.fn.call(t, e); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(e); } catch (x) { W.reportErr(x); }
        if (e._stop) break;
    }
    e.currentTarget = null; e.eventPhase = 0;
    return !e.defaultPrevented;
}
class EventTarget {
    addEventListener(t, fn, o) { if (!fn) return; t = String(t); const L = listeners(this).get(t) || []; if (L.some(x => x.fn === fn)) return; L.push({ fn, once: !!(o && typeof o === 'object' && o.once) }); listeners(this).set(t, L); }
    removeEventListener(t, fn) { const L = listeners(this).get(String(t)); if (!L) return; const i = L.findIndex(x => x.fn === fn); if (i >= 0) { L[i].removed = true; L.splice(i, 1); } }
    dispatchEvent(e) { if (!(e instanceof Event)) throw new TypeError("parameter 1 is not of type 'Event'."); return fireOn(this, e); }
}
class WorkerGlobalScope extends EventTarget {}
class DedicatedWorkerGlobalScope extends WorkerGlobalScope {}
Object.setPrototypeOf(G, DedicatedWorkerGlobalScope.prototype);

const URLRE = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\/\/(?:[^@\/?#]*@)?(\[[^\]]*\]|[^\/?#:]*)(?::(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/;
function parts(href) {
    const m = URLRE.exec(href) || [];
    const protocol = m[1] || '', hostname = m[2] || '', port = m[3] || '', host = hostname + (port ? ':' + port : '');
    const origin = /^(https?|wss?|ftp):$/.test(protocol) ? protocol + '//' + host : protocol === 'blob:' ? parts(m[4] || '').origin : 'null';
    return { href, protocol, host, hostname, port, pathname: m[4] || '', search: m[5] && m[5] !== '?' ? m[5] : '', hash: m[6] && m[6] !== '#' ? m[6] : '', origin };
}
class URLSearchParams {
    constructor(q) { def(this, '_p', []); q = String(q ?? '').replace(/^\?/, ''); if (q) for (const kv of q.split('&')) { const i = kv.indexOf('='), d = s => decodeURIComponent(s.replace(/\+/g, ' ')); this._p.push(i < 0 ? [d(kv), ''] : [d(kv.slice(0, i)), d(kv.slice(i + 1))]); } }
    get(k) { const e = this._p.find(p => p[0] === k); return e ? e[1] : null; } getAll(k) { return this._p.filter(p => p[0] === k).map(p => p[1]); } has(k) { return this._p.some(p => p[0] === k); }
    append(k, v) { this._p.push([String(k), String(v)]); } set(k, v) { this.delete(k); this.append(k, v); } delete(k) { this._p = this._p.filter(p => p[0] !== k); }
    forEach(f) { for (const [k, v] of this._p) f(v, k, this); } entries() { return this._p[Symbol.iterator](); } [Symbol.iterator]() { return this._p[Symbol.iterator](); }
    toString() { return this._p.map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(v)).join('&'); }
}
class URL {
    constructor(u, base) { const h = W.resolve(String(u), base === undefined ? undefined : String(base)); if (h == null) throw new TypeError("Failed to construct 'URL': Invalid URL"); Object.assign(this, parts(h)); this.searchParams = new URLSearchParams(this.search); }
    toString() { return this.href; } toJSON() { return this.href; }
}
class WorkerLocation { toString() { return this.href; } }
const location = Object.assign(Object.create(WorkerLocation.prototype), parts(W.url)); delete location.searchParams;
class WorkerNavigator {}
const ua = W.ua, navigator = Object.assign(Object.create(WorkerNavigator.prototype), { userAgent: ua, appVersion: ua.replace(/^Mozilla\//, ''), appName: 'Netscape', appCodeName: 'Mozilla', product: 'Gecko', platform: W.platform, language: 'en-US', languages: ['en-US'], onLine: true, hardwareConcurrency: W.cores });

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
class TextEncoder {
    get encoding() { return 'utf-8'; }
    encode(s = '') {
        s = String(s); const out = [];
        for (let i = 0; i < s.length; i++) {
            let c = s.charCodeAt(i);
            if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.length) { const d = s.charCodeAt(i + 1); if (d >= 0xdc00 && d < 0xe000) { c = 0x10000 + (c - 0xd800 << 10) + (d - 0xdc00); i++; } }
            if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
            if (c < 0x80) out.push(c); else if (c < 0x800) out.push(0xc0 | c >> 6, 0x80 | c & 63);
            else if (c < 0x10000) out.push(0xe0 | c >> 12, 0x80 | c >> 6 & 63, 0x80 | c & 63);
            else out.push(0xf0 | c >> 18, 0x80 | c >> 12 & 63, 0x80 | c >> 6 & 63, 0x80 | c & 63);
        }
        return new Uint8Array(out);
    }
}
class TextDecoder { constructor(label = 'utf-8') { this.encoding = 'utf-8'; this.fatal = false; this.ignoreBOM = false; } decode(b) { return b == null ? '' : W.decode(b); } }
class Headers {
    constructor(h) { def(this, '_m', new Map()); if (h) for (const [k, v] of (h instanceof Headers ? h._m : Array.isArray(h) ? h : Object.entries(h))) this.append(k, v); }
    get(k) { return this._m.get(String(k).toLowerCase()) ?? null; } has(k) { return this._m.has(String(k).toLowerCase()); }
    set(k, v) { this._m.set(String(k).toLowerCase(), String(v)); } append(k, v) { k = String(k).toLowerCase(); this._m.set(k, this._m.has(k) ? this._m.get(k) + ', ' + v : String(v)); }
    delete(k) { this._m.delete(String(k).toLowerCase()); } forEach(f) { this._m.forEach((v, k) => f(v, k, this)); }
    entries() { return this._m.entries(); } keys() { return this._m.keys(); } values() { return this._m.values(); } [Symbol.iterator]() { return this._m.entries(); }
}
class Response {
    constructor(body = null, init = {}) { def(this, '_buf', body); this.status = init.status ?? 200; this.statusText = init.statusText || ''; this.url = init.url || ''; this.headers = new Headers(init.headers); this.bodyUsed = false; this.type = 'basic'; }
    get ok() { return this.status >= 200 && this.status < 300; }
    arrayBuffer() { this.bodyUsed = true; const b = this._buf; return Promise.resolve(b instanceof ArrayBuffer ? b : ArrayBuffer.isView(b) ? b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength) : new TextEncoder().encode(b == null ? '' : String(b)).buffer); }
    async text() { return W.decode(await this.arrayBuffer()); }
    async json() { return JSON.parse(await this.text()); }
    clone() { return new Response(this._buf, this); }
}
function fetch(input, init) {
    init = init || {};
    try {
        const u = W.resolve(String(input && input.url || input));
        if (u == null) throw new TypeError('Failed to fetch: invalid URL');
        const r = W.fetch(u, init.method ? String(init.method).toUpperCase() : 'GET', init.body == null ? undefined : String(init.body));
        if (!r) return Promise.reject(new TypeError('Failed to fetch'));
        return Promise.resolve(new Response(r[2], { status: r[0], url: r[1] }));
    } catch (e) { return Promise.reject(e); }
}
const toFn = f => typeof f === 'function' ? f : (0, eval)('(function(){' + String(f) + '\n})');
const performance = { now: () => W.now(), timeOrigin: W.timeOrigin, mark() {}, measure() {}, getEntriesByName() { return []; }, getEntriesByType() { return []; }, toJSON() { return { timeOrigin: W.timeOrigin }; } };
const fmt = a => a.map(x => typeof x === 'string' ? x : x instanceof Error ? x.stack || String(x) : (() => { try { return JSON.stringify(x); } catch (e) { return String(x); } })()).join(' ');
const console = { log: (...a) => W.log(1, fmt(a)), info: (...a) => W.log(1, fmt(a)), debug: (...a) => W.log(0, fmt(a)), warn: (...a) => W.log(2, fmt(a)), error: (...a) => W.log(3, fmt(a)), trace() {}, group() {}, groupEnd() {}, time() {}, timeEnd() {}, assert: (c, ...a) => { if (!c) W.log(3, 'Assertion failed: ' + fmt(a)); } };
const api = {
    self: G, globalThis: G, location, navigator, name: W.name, origin: location.origin, isSecureContext: location.protocol === 'https:', crossOriginIsolated: false,
    onmessage: null, onmessageerror: null, onerror: null, onunhandledrejection: null, onrejectionhandled: null, onlanguagechange: null, onoffline: null, ononline: null,
    postMessage(m) { if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage': 1 argument required, but only 0 present."); W.post(m); },
    close() { W.close(); },
    importScripts(...urls) { for (const u of urls) W.importScript(String(u)); },
    setTimeout: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, false, args), setInterval: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, true, args),
    clearTimeout: id => W.clear(+id || 0), clearInterval: id => W.clear(+id || 0),
    queueMicrotask: f => { Promise.resolve().then(() => f()); }, structuredClone: v => W.clone(v),
    btoa, atob, TextEncoder, TextDecoder, URL, URLSearchParams, Headers, Response, fetch, performance, console,
    Event, MessageEvent, ErrorEvent, CustomEvent, PromiseRejectionEvent, EventTarget, DOMException, WorkerGlobalScope, DedicatedWorkerGlobalScope, WorkerLocation, WorkerNavigator,
};
for (const k of Object.keys(api)) Object.defineProperty(G, k, { value: api[k], writable: true, configurable: true, enumerable: false });
return {
    onmsg(data) { fireOn(G, new MessageEvent('message', { data })); },
    report(error, message, filename, lineno, colno) {
        const e = new ErrorEvent('error', { message, filename, lineno, colno, error, cancelable: true });
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
    v8::Isolate *iso = v8::Isolate::New(cp);
    { std::lock_guard<std::mutex> lk(w->mu); w->iso = iso; }
    iso->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    WEnv *E = new WEnv; E->w = w; E->iso = iso; E->t0 = now_ms();
    iso->SetData(kEnvSlot, E);
    {
        v8::Isolate::Scope is(iso);
        v8::HandleScope hs(iso);
        v8::Local<v8::Context> ctx = v8::Context::New(iso);
        E->ctx.Reset(iso, ctx);
        v8::Context::Scope cs(ctx);
        v8::Local<v8::Object> W = v8::Object::New(iso);
#define WREG(nm) (void)W->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, w_##nm).ToLocalChecked())
        WREG(post); WREG(clone); WREG(timer); WREG(clear); WREG(close); WREG(now); WREG(log); WREG(decode); WREG(resolve); WREG(fetch); WREG(importScript); WREG(reportErr);
        auto setS = [&](const char *k, const std::string &v) { (void)W->Set(ctx, jstr(iso, k), jstr(iso, v.c_str())); };
        setS("url", w->url); setS("ua", w->ua); setS("platform", w->platform); setS("name", w->name);
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
            if (!v8::Script::Compile(ctx, jstr(iso, src.data(), (int)src.size()), &origin).ToLocal(&s)) {
                ok = false;   /* classic-script parse errors fire a plain error Event on the Worker */
                post_out(E, OutMsg{ WM_LOADFAIL, {}, {}, {}, 0, 0 });
            } else {
                (void)s->Run(ctx).ToLocal(&r);
                wcheck(E, tc);
                iso->PerformMicrotaskCheckpoint();
            }
        }
        while (ok && !w->term && !E->closing) {
            std::deque<Buf> msgs;
            {
                std::unique_lock<std::mutex> lk(w->mu);
                double next = 1e300;
                for (auto &kv : E->timers) next = std::min(next, kv.second.due);
                auto ready = [&] { return w->term.load() || !w->in.empty(); };
                if (!ready()) {
                    if (next >= 1e299) w->cv.wait(lk, ready);
                    else { double ms = next - now_ms(); if (ms > 0) w->cv.wait_for(lk, std::chrono::microseconds((int64_t)(ms * 1000) + 1), ready); }
                }
                msgs.swap(w->in);
            }
            for (Buf &b : msgs) {
                if (!w->term && !E->closing) {
                    v8::HandleScope hs2(iso);
                    v8::TryCatch tc(iso);
                    v8::Local<v8::Value> v;
                    if (deser(iso, ctx, b).ToLocal(&v) && !E->onmsg.IsEmpty()) { (void)E->onmsg.Get(iso)->Call(ctx, v8::Undefined(iso), 1, &v); wcheck(E, tc); }
                    iso->PerformMicrotaskCheckpoint();
                }
                free(b.p);
            }
            if (w->term) break;
            run_timers(E);
            while (v8::platform::PumpMessageLoop(js_platform(), iso)) {}
        }
        E->timers.clear(); E->onmsg.Reset(); E->report.Reset(); E->ctx.Reset();
    }
    {
        std::lock_guard<std::mutex> lk(w->mu);
        w->iso = nullptr;
        for (Buf &b : w->in) free(b.p);
        w->in.clear();
    }
    iso->Dispose();
    delete E;
}
void *worker_thread(void *p) {
    std::unique_ptr<std::shared_ptr<Worker>> sp(static_cast<std::shared_ptr<Worker> *>(p));
    worker_main(*sp);
    return nullptr;
}

void worker_terminate(Worker *w) {
    std::lock_guard<std::mutex> lk(w->mu);
    w->term = true;
    if (w->iso) w->iso->TerminateExecution();
    for (OutMsg &m : w->out) free(m.data.p);
    w->out.clear();
    w->cv.notify_all();
}

/* ---- main thread ---- */
void n_workerNew(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    auto w = std::make_shared<Worker>();
    w->id = ++g_wseq; w->owner = jctx(iso);
    w->url = jcstr(iso, a[0]); w->have_src = a[1]->IsString(); if (w->have_src) w->src = jcstr(iso, a[1]);
    w->ua = jcstr(iso, a[2]); w->platform = jcstr(iso, a[3]); w->name = a[4]->IsString() ? jcstr(iso, a[4]) : "";
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
    auto it = g_workers.find(a[0]->Uint32Value(ctx).FromMaybe(0));
    Buf b;
    if (!ser(iso, ctx, a[1], b)) return;
    if (it == g_workers.end() || it->second->term) { free(b.p); return; }
    Worker *w = it->second.get();
    { std::lock_guard<std::mutex> lk(w->mu); w->in.push_back(b); }
    w->cv.notify_all();
}
void n_workerTerm(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    auto it = g_workers.find(a[0]->Uint32Value(ctx).FromMaybe(0));
    if (it == g_workers.end()) return;
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
    std::vector<std::pair<uint32_t, std::deque<OutMsg>>> batch;
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
                if (kind == WM_MESSAGE) { v8::TryCatch tc(iso); if (!deser(iso, ctx, m.data).ToLocal(&data)) { data = v8::Undefined(iso); kind = 3; } }
                v8::Local<v8::Value> argv[7] = { v8::Integer::NewFromUnsigned(iso, b.first), v8::Integer::New(iso, kind), data, jstr(iso, m.msg.c_str()), jstr(iso, m.file.c_str()), v8::Integer::New(iso, m.line), v8::Integer::New(iso, m.col) };
                (void)jcall(c, fv.As<v8::Function>(), v8::Undefined(iso), 7, argv);
            }
            free(m.data.p);
        }
}

void workers_kill(JsCtx *c) {
    for (auto it = g_workers.begin(); it != g_workers.end();)
        if (it->second->owner == c) { worker_terminate(it->second.get()); it = g_workers.erase(it); }
        else ++it;
}
