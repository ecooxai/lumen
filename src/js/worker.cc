/* Dedicated Web Workers: each worker runs on its own thread with its own V8 isolate.
   Messages cross threads as V8 ValueSerializer buffers. */
#include "js_int.h"
#include "header_bytes.h"
#include "jsglue.h"
#include "url_parts.h"
#include "idl_state.h"
#include "blob_state.h"
#include "file_reader.h"
#include "text_codec.h"
#include "clone_brand.h"
#include <libplatform/libplatform.h>
#include <pthread.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <vector>
#include <memory>
extern "C" {
#include "../net/net.h"
#include "../base/url.h"
}

void (*js_wakeup)(void) = nullptr;

namespace {
struct Side { std::vector<v8::CompiledWasmModule> wasm; std::vector<std::shared_ptr<v8::BackingStore>> sab; };
struct Buf { uint8_t *p = nullptr; size_t n = 0; std::shared_ptr<Side> side; };
enum { WM_MESSAGE, WM_ERROR, WM_LOADFAIL };
struct OutMsg { int kind; Buf data; std::string msg, file; int line = 0, col = 0; };
struct Worker {
    uint32_t id = 0; JsCtx *owner = nullptr;
    bool creator_secure = false; /* native owner setting, captured before thread creation */
    std::string url, src, ua, platform, name, top; bool have_src = false, module = false, coi = false;
    std::string ocsp, odoc; bool has_ocsp = false;   /* creator document's CSP: worker-src check, inherited by blob:/data: workers */
    std::mutex mu; std::condition_variable cv;
    std::deque<Buf> in; std::deque<OutMsg> out;
    std::atomic<bool> term{false};
    std::atomic<bool> finished{false}; /* worker_main and its isolate/allocator cleanup have returned */
    bool accepting = true;  /* protected by mu; close() stops incoming tasks without aborting the current task */
    v8::Isolate *iso = nullptr;
    NetRequest *request = nullptr; /* protected by mu; owned by the synchronous worker_fetch call */
    std::atomic<size_t> heap{0}, ext{0};
};
struct WTimer { double due = 0, interval = 0; bool repeat = false; v8::Global<v8::Function> fn; std::vector<v8::Global<v8::Value>> args; };
struct WEnv {
    std::shared_ptr<Worker> w; v8::Isolate *iso = nullptr;
    v8::Global<v8::Context> ctx; v8::Global<v8::Function> onmsg, report, busfn;
    uint64_t bus_seen = 0; std::string bus_origin;
    std::string csp, cspdoc; bool has_csp = false;   /* the worker's own enforced CSP */
    std::map<std::string, v8::Global<v8::Module>> mods; std::multimap<int, std::string> murl;
    std::map<uint32_t, WTimer> timers; uint32_t tseq = 0; bool closing = false, reporting = false; double t0 = 0;
};
std::map<uint32_t, std::shared_ptr<Worker>> g_workers;   /* main thread only */
uint32_t g_wseq;

/* Per-page termination stays asynchronous. The process, however, must not
 * destroy the bus/platform while a detached worker is still using them. The
 * count includes startup and cleanup, even after its JS registry entry is gone.
 */
struct WorkerThreads {
    std::mutex mu;
    std::condition_variable cv;
    size_t running = 0;
    bool stopping = false;
};
void worker_exit();
WorkerThreads &worker_threads() {
    static WorkerThreads threads;
    /* Register AFTER construction: the callback must precede this object's
     * destructor, as well as the bus/platform globals constructed before main.
     * The explicit app shutdown runs before other subsystem cleanup; this is
     * also a backstop for command-line tools and other embedders.
     */
    static const bool registered = [] {
        if (std::atexit(worker_exit) != 0) {
            fprintf(stderr, "lumen: cannot register worker shutdown\n");
            std::abort();
        }
        return true;
    }();
    (void)registered;
    return threads;
}

bool wait_worker_threads(unsigned timeout_ms) {
    WorkerThreads &threads = worker_threads();
    std::unique_lock<std::mutex> lock(threads.mu);
    return threads.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return threads.running == 0; });
}

const int kEnvSlot = 2;
v8::MaybeLocal<v8::Module> wm_compile(WEnv *E, const std::string &u, const std::string &src);
bool wm_graph(WEnv *E, v8::Local<v8::Module> m);
v8::MaybeLocal<v8::Module> wm_resolve_cb(v8::Local<v8::Context>, v8::Local<v8::String>, v8::Local<v8::FixedArray>, v8::Local<v8::Module>);
void wm_meta(v8::Local<v8::Context> ctx, v8::Local<v8::Module> m, v8::Local<v8::Object> meta);

void wake() { if (js_wakeup) js_wakeup(); }

/* WebAssembly.Module and SharedArrayBuffer cross threads by reference (compiled code / backing store). */
struct SerDel : JsgCloneDelegate {
    v8::Isolate *iso; std::shared_ptr<Side> side = std::make_shared<Side>();
    explicit SerDel(v8::Isolate *i) : JsgCloneDelegate(i), iso(i) {}
    v8::Maybe<uint32_t> GetWasmModuleTransferId(v8::Isolate *, v8::Local<v8::WasmModuleObject> m) override {
        side->wasm.push_back(m->GetCompiledModule()); return v8::Just<uint32_t>((uint32_t)side->wasm.size() - 1);
    }
    v8::Maybe<uint32_t> GetSharedArrayBufferId(v8::Isolate *, v8::Local<v8::SharedArrayBuffer> b) override {
        side->sab.push_back(b->GetBackingStore()); return v8::Just<uint32_t>((uint32_t)side->sab.size() - 1);
    }
};
struct DesDel : v8::ValueDeserializer::Delegate {
    std::shared_ptr<Side> side;
    explicit DesDel(std::shared_ptr<Side> s) : side(std::move(s)) {}
    v8::MaybeLocal<v8::WasmModuleObject> GetWasmModuleFromId(v8::Isolate *iso, uint32_t id) override {
        if (!side || id >= side->wasm.size()) return {};
        return v8::WasmModuleObject::FromCompiledModule(iso, side->wasm[id]);
    }
    v8::MaybeLocal<v8::SharedArrayBuffer> GetSharedArrayBufferFromId(v8::Isolate *iso, uint32_t id) override {
        if (!side || id >= side->sab.size()) return {};
        return v8::SharedArrayBuffer::New(iso, side->sab[id]);
    }
};
bool ser(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> v, Buf &out) {
    SerDel del(iso);
    v8::ValueSerializer s(iso, &del);
    s.WriteHeader();
    if (!s.WriteValue(ctx, v).FromMaybe(false)) return false;
    auto r = s.Release(); out.p = r.first; out.n = r.second;
    if (!del.side->wasm.empty() || !del.side->sab.empty()) out.side = del.side;
    return true;
}
v8::MaybeLocal<v8::Value> deser(v8::Isolate *iso, v8::Local<v8::Context> ctx, const Buf &b) {
    DesDel del(b.side);
    v8::ValueDeserializer d(iso, b.p, b.n, &del);
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

/* Caller holds mu. Pending serializer buffers own native allocations, not V8
 * handles, so they must be released even when no worker thread remains. */
void close_input_locked(Worker *w) {
    w->accepting = false;
    for (Buf &b : w->in) free(b.p);
    w->in.clear();
}
void post_out(WEnv *E, OutMsg &&m) {
    {
        std::lock_guard<std::mutex> lk(E->w->mu);
        if (E->w->term) { free(m.data.p); return; }
        E->w->out.push_back(std::move(m));
    }
    wake();
}

/* The request remains alive while terminate() can see it. Clearing the active
 * pointer under the same lock before freeing prevents cancellation from racing
 * request destruction or a later request reusing its allocation/socket.
 */
NetResponse *worker_fetch(Worker *w, NetRequest *request) {
    {
        std::lock_guard<std::mutex> lock(w->mu);
        if (w->term) { net_request_free(request); return nullptr; }
        w->request = request;
    }
    NetResponse *response = net_fetch_sync(request);
    bool terminated;
    {
        std::lock_guard<std::mutex> lock(w->mu);
        w->request = nullptr;
        terminated = w->term;
    }
    net_request_free(request);
    if (terminated) { net_response_free(response); return nullptr; }
    return response;
}

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
    char *u = url_join_n(base ? base : E->w->url.c_str(), rel.data(), rel.size());
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
WFN(close) {
    WENV; E->closing = true;
    std::lock_guard<std::mutex> lk(E->w->mu);
    close_input_locked(E->w.get());
}
WFN(now) { WENV; a.GetReturnValue().Set(now_ms() - E->t0); }
WFN(log) { WENV; if (getenv("LUMEN_CONSOLE")) fprintf(stderr, "[worker %d] %.2000s\n", a[0]->Int32Value(ctx).FromMaybe(1), jcstr(iso, a[1]).c_str()); }
WFN(blobState) { jsg_blob_state(a); }
WFN(fileReaderPackage) { jsg_file_reader_package(a); }
WFN(fileReaderCheckpoint) { jsg_file_reader_checkpoint(a); }
WFN(textEncoding) { jsg_text_encoding(a); }
WFN(textDecode) { jsg_text_decode(a); }
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
// URL construction has an optional base; fetch/import resolution keeps the
// separate worker-relative W.resolve path. Both URL realms share the native
// parser, state overrides and record serialization.
WFN(initCloneIntrinsic) { jsg_init_clone_intrinsic(a); }
WFN(idlGet) { jsg_idl_get(a); }
WFN(idlPut) { jsg_idl_put(a); }
WFN(idlRegister) { jsg_idl_register(a); }
WFN(idlFallback) { jsg_idl_fallback(a); }
WFN(urlParse) {
    WENV; std::string input = jcstr(iso, a[0]);
    URL url = {}, base = {};
    bool ok;
    if (a[1]->IsNullOrUndefined()) ok = url_parse_n(input.data(), input.size(), &url);
    else {
        std::string value = jcstr(iso, a[1]);
        ok = url_parse_n(value.data(), value.size(), &base) &&
             url_resolve_n(&base, input.data(), input.size(), &url);
        url_free(&base);
    }
    if (!ok || !url.scheme) { url_free(&url); a.GetReturnValue().SetNull(); return; }
    auto result = jsg_url_parts(iso, ctx, url);
    url_free(&url);
    a.GetReturnValue().Set(result);
}
WFN(urlSet) {
    WENV; std::string href = jcstr(iso, a[0]), value = jcstr(iso, a[2]);
    URL url = {};
    if (!url_parse_n(href.data(), href.size(), &url)) {
        url_free(&url); a.GetReturnValue().SetNull(); return;
    }
    int component = a[1]->Int32Value(ctx).FromMaybe(0);
    // A valid host may commit before an invalid port: return the actual record.
    (void)url_set_component_n(&url, (URLComponent)component, value.data(), value.size());
    auto result = jsg_url_parts(iso, ctx, url);
    url_free(&url);
    a.GetReturnValue().Set(result);
}

WFN(fetch) {
    WENV; std::string u = jcstr(iso, a[0]), m = a[1]->IsString() ? jcstr(iso, a[1]) : "GET";
    NetRequest *rq = net_request_ctx(m.c_str(), u.c_str(), E->w->top.empty() ? nullptr : E->w->top.c_str());
    if (E->has_csp) { rq->csp = strdup(E->csp.c_str()); rq->csp_doc = strdup(E->cspdoc.c_str()); rq->csp_dir = "connect-src"; }
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
            if (!h->Get(ctx, i).ToLocal(&k) || !h->Get(ctx, i + 1).ToLocal(&v)) { net_request_free(rq); return; }
            std::string key, value;
            if (!jheader_bytes(iso, k, true, key) || !jheader_bytes(iso, v, false, value)) { net_request_free(rq); return; }
            headers_add(&rq->headers, key.c_str(), value.c_str());
        }
    }
    if (a[4]->IsTrue()) rq->no_cookies = true;
    NetResponse *r = worker_fetch(E->w.get(), rq);
    if (!r || r->status == 0 || E->w->term) { if (r) net_response_free(r); a.GetReturnValue().SetNull(); return; }
    v8::Local<v8::Array> hs = v8::Array::New(iso);
    for (int i = 0; i < r->headers.n; i++) { (void)hs->Set(ctx, (uint32_t)(2 * i), jstr(iso, r->headers.v[i].name)); (void)hs->Set(ctx, (uint32_t)(2 * i + 1), jheader_string(iso, r->headers.v[i].value)); }
    v8::Local<v8::Value> out[4] = { v8::Integer::New(iso, r->status), jstr(iso, r->url ? r->url : u.c_str()), make_ab(iso, r->body, r->body_len), hs };
    net_response_free(r);
    a.GetReturnValue().Set(v8::Array::New(iso, out, 4));
}
WFN(importScript) {
    WENV; std::string u = resolve(E, jcstr(iso, a[0]));
    if (u.empty()) { throw_dom(iso, ctx, "Failed to execute 'importScripts': The URL '" + jcstr(iso, a[0]) + "' is invalid.", "SyntaxError"); return; }
    if (E->has_csp && !csp_allows_url(E->csp.c_str(), E->cspdoc.c_str(), u.c_str(), "script-src")) {
        fprintf(stderr, "lumen: CSP script-src blocked importScripts %.120s\n", u.c_str());
        throw_dom(iso, ctx, "Failed to execute 'importScripts': The script at '" + u + "' failed to load.", "NetworkError"); return;
    }
    NetRequest *rq = net_request_new("GET", u.c_str());
    NetResponse *r = worker_fetch(E->w.get(), rq);
    if (E->w->term) { net_response_free(r); return; }
    if (!r || r->status < 200 || r->status >= 300) {
        if (r) net_response_free(r);
        throw_dom(iso, ctx, "Failed to execute 'importScripts': The script at '" + u + "' failed to load.", "NetworkError"); return;
    }
    std::string src(r->body ? r->body : "", r->body_len); net_response_free(r);
    v8::ScriptOrigin origin(jstr(iso, u.c_str()));
    v8::Local<v8::Script> s; v8::Local<v8::Value> res;
    if (v8::Script::Compile(ctx, jsrc(iso, src.data(), src.size()), &origin).ToLocal(&s)) (void)s->Run(ctx).ToLocal(&res);
}
WFN(busOrigin) {
    WENV; E->bus_origin = jcstr(iso, a[0]); E->bus_seen = js_bus_seq();
    std::shared_ptr<Worker> w = E->w;
    js_bus_listen(E, [w] { std::lock_guard<std::mutex> lk(w->mu); w->cv.notify_all(); });
}
WFN(busPost) { WENV; (void)js_bus_post(iso, ctx, a[2], E->bus_origin, "bc", jcstr(iso, a[0]), E, a[1]->NumberValue(ctx).FromMaybe(0)); }
WFN(reportErr) { WENV; werr(E, a[0], v8::Exception::CreateMessage(iso, a[0])); }

const char kWorkerPrelude[] = R"JS((function (W, G) {
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
'use strict';
const def = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false });
const nativeIDL = { get: W.idlGet, put: W.idlPut, register: W.idlRegister, fallback: W.idlFallback };
const DOMException = createDOMExceptionInterface(nativeIDL);
W.initCloneIntrinsic((message, name) => new DOMException(message, name));
const eventStates = new WeakMap();
const eventState = event => {
    const state = eventStates.get(event);
    if (state === undefined) throw new TypeError('The object does not implement Event');
    return state;
};
function initializeEvent(event, type, bubbles, cancelable) {
    const state = eventState(event);
    if (state.dispatching) return false;
    state.initialized = true;
    event._stop = event._imm = event.defaultPrevented = false;
    event.isTrusted = false; event.target = null;
    event.type = type; event.bubbles = bubbles; event.cancelable = cancelable;
    return true;
}
class Event {
    constructor(type, init) {
        if (!arguments.length) throw new TypeError('Event requires a type');
        init = init || {}; eventStates.set(this, { initialized: true, dispatching: false });
        this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed;
        this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.eventPhase = 0; this.isTrusted = false; this.timeStamp = W.now();
        def(this, '_stop', false); def(this, '_imm', false); def(this, '_passive', false);
    }
    preventDefault() { if (this.cancelable && !this._passive) this.defaultPrevented = true; }
    stopPropagation() { this._stop = true; } stopImmediatePropagation() { this._stop = this._imm = true; }
    get srcElement() { return this.target; }
    get returnValue() { return !this.defaultPrevented; } set returnValue(value) { if (!value) this.preventDefault(); }
    get cancelBubble() { return this._stop; } set cancelBubble(value) { if (value) this._stop = true; }
    composedPath() { return this.currentTarget ? [this.currentTarget] : []; }
    initEvent(type, bubbles = false, cancelable = false) {
        eventState(this);
        if (!arguments.length) throw new TypeError('initEvent requires a type');
        initializeEvent(this, String(type), !!bubbles, !!cancelable);
    }
}
for (const [name, value] of Object.entries({ NONE: 0, CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3 })) {
    Object.defineProperty(Event, name, { value, enumerable: true });
    Object.defineProperty(Event.prototype, name, { value, enumerable: true });
}
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data === undefined ? null : i.data; this.origin = i.origin || ''; this.lastEventId = i.lastEventId || ''; this.source = i.source || null; this.ports = i.ports || []; } }
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message || ''; this.filename = i.filename || ''; this.lineno = i.lineno || 0; this.colno = i.colno || 0; this.error = i.error; } }
class CustomEvent extends Event {
    constructor(t, i) { if (!arguments.length) throw new TypeError('CustomEvent requires a type'); super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null; }
    initCustomEvent(t, b = false, c = false, d = null) {
        eventState(this);
        if (!arguments.length) throw new TypeError('initCustomEvent requires a type');
        if (initializeEvent(this, String(t), !!b, !!c)) this.detail = d;
    }
}
class PromiseRejectionEvent extends Event { constructor(t, i) { super(t, i); this.promise = i && i.promise; this.reason = i && i.reason; } }
// Web IDL operations substitute this realm's global for a null/undefined
// receiver, then require an object that implements the interface. The map
// brands actual EventTargets; inheriting their prototype is not sufficient.
const lsn = new WeakMap([[G, new Map()]]);
const eventTargetReceiver = receiver => {
    const target = receiver === null || receiver === undefined ? G : receiver;
    if (!lsn.has(target)) throw new TypeError('Illegal invocation');
    return target;
};
const listeners = t => lsn.get(t);
function fireOn(t, e, skipHandler, checkpoint) {
    const state = eventState(e);
    if (state.dispatching || !state.initialized) throw new DOMException('The event is uninitialized or already dispatching.', 'InvalidStateError');
    state.dispatching = true;
    try {
        e.target = t; e.currentTarget = t; e.eventPhase = 2;
        const invoke = capture => {
            for (const l of (listeners(t).get(e.type) || []).slice()) {
                if (l.removed || l.capture !== capture) continue;
                if (l.once) t.removeEventListener(e.type, l.fn, l.capture);
                e._passive = l.passive;
                try {
                    try { if (typeof l.fn === 'function') l.fn.call(t, e); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(e); }
                    finally { if (checkpoint) checkpoint(); }
                } catch (error) { W.reportErr(error); if (checkpoint) checkpoint(); }
                e._passive = false;
                if (e._imm) break;
            }
        };
        if (!e._stop) invoke(true);
        if (!e._stop) {
          if (!skipHandler) {
            const h = t['on' + e.type];
            if (typeof h === 'function') {
                try {
                    let result;
                    try { result = h.call(t, e); }
                    finally { if (checkpoint) checkpoint(); }
                    if (result === false && e.type !== 'error') e.preventDefault();
                } catch (error) { W.reportErr(error); if (checkpoint) checkpoint(); }
            }
        }
          if (!e._imm) invoke(false);
        }
        return !e.defaultPrevented;
    } finally {
        state.dispatching = false;
        e.currentTarget = null; e.eventPhase = 0; e._stop = e._imm = e._passive = false;
    }
}
class EventTarget {
    constructor() { lsn.set(this, new Map()); }
    addEventListener(t, fn, o) {
        const target = eventTargetReceiver(this); if (!fn) return; t = String(t);
        const options = o && typeof o === 'object' ? o : {}, capture = typeof o === 'boolean' ? o : !!options.capture;
        if (options.signal && options.signal.aborted) return;
        const L = listeners(target).get(t) || [];
        if (L.some(x => x.fn === fn && x.capture === capture)) return;
        L.push({ fn, capture, once: !!options.once, passive: !!options.passive, removed: false }); listeners(target).set(t, L);
        if (options.signal) options.signal.addEventListener('abort', () => target.removeEventListener(t, fn, capture));
    }
    removeEventListener(t, fn, o) {
        const target = eventTargetReceiver(this), capture = typeof o === 'boolean' ? o : !!(o && o.capture);
        const L = listeners(target).get(String(t)); if (!L) return;
        const i = L.findIndex(x => x.fn === fn && x.capture === capture);
        if (i >= 0) { L[i].removed = true; L.splice(i, 1); }
    }
    dispatchEvent(e) {
        const target = eventTargetReceiver(this); eventState(e);
        if (eventState(e).dispatching) throw new DOMException('The event is already dispatching.', 'InvalidStateError');
        e.isTrusted = false; return fireOn(target, e, nativeIDL.get(4, target) !== undefined);
    }
}
class WorkerGlobalScope extends EventTarget {}
class DedicatedWorkerGlobalScope extends WorkerGlobalScope {}
Object.setPrototypeOf(G, DedicatedWorkerGlobalScope.prototype);

)JS"
#include "dom_exception.inc"
#include "url_interface.inc"
#include "url_search_params.inc"
#include "file_reader.inc"
#include "blob_file.inc"
#include "text_codec.inc"
#include "headers.inc"
R"JS(
const urlSearchParamsOps = createURLSearchParams(nativeIDL);
const { URLSearchParams } = urlSearchParamsOps;
const workerObjectURLs = new Map(); let workerObjectURLSeq = 0;
const { URL, keys: URL_KEYS } = createURLInterface(W.urlParse, W.urlSet, urlSearchParamsOps, {
    create(blob) {
        if (!blobFileOps.isBlob(blob)) throw new TypeError('Object URL requires a Blob');
        const href = 'blob:' + self.location.origin + '/' + (++workerObjectURLSeq).toString(16).padStart(8, '0') + '-lumenw';
        workerObjectURLs.set(href, blob); return href;
    },
    revoke(input) { workerObjectURLs.delete(input); },
}, nativeIDL);

class WorkerLocation { toString() { return this.href; } }
const locationRecord = W.urlParse(W.url, null);
if (!locationRecord) throw new TypeError('Invalid worker location');
const location = Object.create(WorkerLocation.prototype);
for (let index = 0; index < URL_KEYS.length; index++) {
    const key = URL_KEYS[index];
    if (key !== 'username' && key !== 'password') {
        Object.defineProperty(location, key, { value: locationRecord[index], enumerable: true });
    }
}

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
const { TextEncoder, TextDecoder } = createTextCodecInterfaces(null, { label: W.textEncoding, decode: W.textDecode });
const headerOps = createHeadersInterface();
const { Headers } = headerOps;
const blobFileOps = createBlobFileInterfaces(nativeIDL, {
    blobState: W.blobState, encode: urlSearchParamsOps.utf8Bytes, decode: W.decode,
});
const {Blob, File} = blobFileOps;
class ProgressEvent extends Event {
    constructor(type, init) { super(type, init); init = init || {}; this.lengthComputable = !!init.lengthComputable; this.loaded = init.loaded || 0; this.total = init.total || 0; }
}
const { FileReader, FileReaderSync } = createFileReaderInterfaces(nativeIDL, {
    schedule: callback => W.timer(callback, 0, false, []),
    cancel: W.clear,
    blobState: W.blobState,
    package: W.fileReaderPackage,
    fire(reader, name, loaded, total, queued) {
        const event = new ProgressEvent(name, { lengthComputable: total > 0, loaded, total });
        event.isTrusted = true;
        fireOn(reader, event, true, queued ? W.fileReaderCheckpoint : undefined);
    },
}, true);
// Dedicated-worker byte bodies. These classes share the URLencoded codec with
// window URLSearchParams; body parsing never strips a query-data BOM first.
const workerFormSlots = new WeakMap(), workerBodySlots = new WeakMap();
const workerRequestSlots = new WeakMap(), workerResponseSlots = new WeakMap();
function workerSlot(map, object) {
    const state = map.get(object);
    if (!state) throw new TypeError('Illegal invocation');
    return state;
}
function workerArguments(count, minimum) {
    if (count < minimum) throw new TypeError('Not enough arguments');
}
const workerUSV = urlSearchParamsOps.usv;
const workerPairIteratorSlots = new WeakMap();
const workerFormIteratorPrototype = Object.create(Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())));
Object.defineProperties(workerFormIteratorPrototype, {
    next: { value: { next() {
        const iterator = workerSlot(workerPairIteratorSlots, this);
        const list = workerSlot(workerFormSlots, iterator.target).list;
        if (iterator.index >= list.length) return { value: undefined, done: true };
        const pair = list[iterator.index++];
        return { value: iterator.kind === 0 ? pair[0] : iterator.kind === 1 ? pair[1] : [pair[0], pair[1]], done: false };
    } }.next, writable: true, enumerable: true, configurable: true },
    [Symbol.toStringTag]: { value: 'FormData Iterator', configurable: true },
});
function workerFormIterator(target, kind) {
    workerSlot(workerFormSlots, target);
    const value = Object.create(workerFormIteratorPrototype);
    workerPairIteratorSlots.set(value, { target, kind, index: 0 });
    return value;
}
function workerFormValue(value, filename, hasFilename) {
    if (hasFilename && !blobFileOps.isBlob(value)) throw new TypeError('A filename requires a Blob value');
    if (!blobFileOps.isBlob(value)) return workerUSV(value);
    if (filename !== undefined) return new File([value], workerUSV(filename), { type: blobFileOps.type(value), lastModified: blobFileOps.isFile(value) ? blobFileOps.modified(value) : undefined });
    return blobFileOps.isFile(value) ? value : new File([value], 'blob', { type: blobFileOps.type(value) });
}
class FormData {
    constructor(form = undefined) {
        if (form !== undefined) throw new TypeError('A worker has no HTML form elements');
        workerFormSlots.set(this, { list: [] });
    }
    append(name, value, filename = undefined) {
        const state = workerSlot(workerFormSlots, this); workerArguments(arguments.length, 2);
        name = workerUSV(name); value = workerFormValue(value, filename, arguments.length >= 3);
        state.list.push([name, value]);
    }
    delete(name) {
        const state = workerSlot(workerFormSlots, this); workerArguments(arguments.length, 1);
        name = workerUSV(name); state.list = state.list.filter(pair => pair[0] !== name);
    }
    get(name) {
        const state = workerSlot(workerFormSlots, this); workerArguments(arguments.length, 1);
        name = workerUSV(name); const pair = state.list.find(pair => pair[0] === name);
        return pair ? pair[1] : null;
    }
    getAll(name) {
        const state = workerSlot(workerFormSlots, this); workerArguments(arguments.length, 1);
        name = workerUSV(name); return state.list.filter(pair => pair[0] === name).map(pair => pair[1]);
    }
    has(name) {
        const state = workerSlot(workerFormSlots, this); workerArguments(arguments.length, 1);
        name = workerUSV(name); return state.list.some(pair => pair[0] === name);
    }
    set(name, value, filename = undefined) {
        const state = workerSlot(workerFormSlots, this); workerArguments(arguments.length, 2);
        name = workerUSV(name); value = workerFormValue(value, filename, arguments.length >= 3);
        const list = []; let found = false;
        for (const pair of state.list) {
            if (pair[0] !== name) list.push(pair);
            else if (!found) { list.push([name, value]); found = true; }
        }
        if (!found) list.push([name, value]);
        state.list = list;
    }
    forEach(callback, thisArg = undefined) {
        const state = workerSlot(workerFormSlots, this); workerArguments(arguments.length, 1);
        if (typeof callback !== 'function') throw new TypeError('Callback is not callable');
        for (let index = 0; index < state.list.length; index++) {
            const pair = state.list[index]; Reflect.apply(callback, thisArg, [pair[1], pair[0], this]);
        }
    }
    keys() { return workerFormIterator(this, 0); }
    values() { return workerFormIterator(this, 1); }
    entries() { return workerFormIterator(this, 2); }
}
Object.defineProperty(FormData.prototype, Symbol.iterator, {
    value: FormData.prototype.entries, writable: true, configurable: true,
});

function workerMultipart(form, boundary) {
    const parts = [], newline = value => value.replace(/\r\n|\r|\n/g, '\r\n');
    const escaped = value => newline(value).replace(/\n/g, '%0A').replace(/\r/g, '%0D').replace(/"/g, '%22');
    for (const [name, value] of workerSlot(workerFormSlots, form).list) {
        let header = '--' + boundary + '\r\nContent-Disposition: form-data; name="' + escaped(name) + '"';
        if (blobFileOps.isFile(value)) {
            header += '; filename="' + escaped(blobFileOps.name(value)) + '"\r\nContent-Type: ' + (blobFileOps.type(value) || 'application/octet-stream');
            parts.push(urlSearchParamsOps.utf8Bytes(header + '\r\n\r\n'), new Uint8Array(blobFileOps.copyBuffer(value)), new Uint8Array([13, 10]));
        } else parts.push(urlSearchParamsOps.utf8Bytes(header + '\r\n\r\n' + newline(value) + '\r\n'));
    }
    parts.push(urlSearchParamsOps.utf8Bytes('--' + boundary + '--\r\n'));
    const bytes = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
    let offset = 0;
    for (const part of parts) { bytes.set(part, offset); offset += part.length; }
    return bytes;
}
function workerExtractBody(value, headers) {
    if (value === null || value === undefined) return null;
    let bytes, type = null;
    if (urlSearchParamsOps.isURLSearchParams(value)) {
        bytes = urlSearchParamsOps.utf8Bytes(urlSearchParamsOps.serializeParams(value));
        type = 'application/x-www-form-urlencoded;charset=UTF-8';
    } else if (workerFormSlots.has(value)) {
        const boundary = '----LumenWorkerFormBoundary' + Math.random().toString(36).slice(2) + Date.now().toString(36);
        bytes = workerMultipart(value, boundary); type = 'multipart/form-data; boundary=' + boundary;
    } else if (blobFileOps.isBlob(value)) {
        bytes = new Uint8Array(blobFileOps.copyBuffer(value)); type = blobFileOps.type(value) || null;
    } else if (value instanceof ArrayBuffer) bytes = new Uint8Array(value.slice(0));
    else if (ArrayBuffer.isView(value)) {
        if (!(value.buffer instanceof ArrayBuffer)) throw new TypeError('A shared buffer is not a BodyInit');
        bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
    } else {
        bytes = urlSearchParamsOps.utf8Bytes(workerUSV(value)); type = 'text/plain;charset=UTF-8';
    }
    if (type !== null && !headers.has('content-type')) headers.set('content-type', type);
    return bytes;
}
function workerConsumeBody(object, convert) {
    let bytes;
    try {
        const state = workerSlot(workerBodySlots, object);
        if (state.used) throw new TypeError('Body has already been consumed');
        bytes = state.bytes === null ? new Uint8Array(0) : state.bytes;
        if (state.bytes !== null) {
            state.used = true;
            // The pending conversion owns these bytes. Keeping the consumed
            // Request/Response alive must not retain another body reference.
            state.bytes = null;
        }
    } catch (error) { return Promise.reject(error); }
    return Promise.resolve().then(() => convert(bytes));
}
function workerBodyHeaders(object) {
    const state = workerRequestSlots.get(object) || workerResponseSlots.get(object);
    if (!state) throw new TypeError('Illegal invocation');
    return state.headers;
}
function workerDecodedText(bytes) {
    const text = urlSearchParamsOps.utf8Decode(bytes);
    return text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
}
const workerBodyMethods = {
    get bodyUsed() { return workerSlot(workerBodySlots, this).used; },
    arrayBuffer() { return workerConsumeBody(this, bytes => bytes.buffer); },
    bytes() { return workerConsumeBody(this, bytes => bytes); },
    text() { return workerConsumeBody(this, workerDecodedText); },
    json() { return workerConsumeBody(this, bytes => JSON.parse(workerDecodedText(bytes))); },
    blob() { return workerConsumeBody(this, bytes => new Blob([bytes], { type: workerBodyHeaders(this).get('content-type') || '' })); },
    formData() {
        return workerConsumeBody(this, bytes => {
            const essence = (workerBodyHeaders(this).get('content-type') || '').split(';', 1)[0].trim().toLowerCase();
            if (essence !== 'application/x-www-form-urlencoded') throw new TypeError('Unsupported form-data media type');
            const form = new FormData();
            workerFormSlots.get(form).list = urlSearchParamsOps.parseBytes(bytes);
            return form;
        });
    },
};
function workerDictionary(value) {
    if (value === undefined || value === null) return {};
    if (typeof value !== 'object' && typeof value !== 'function') throw new TypeError('Expected a dictionary');
    return value;
}
function workerMethod(value) {
    value = workerUSV(value);
    if (!/^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/.test(value)) throw new TypeError('Invalid HTTP method');
    const upper = value.toUpperCase();
    if (['CONNECT', 'TRACE', 'TRACK'].includes(upper)) throw new TypeError('Forbidden HTTP method');
    return ['DELETE', 'GET', 'HEAD', 'OPTIONS', 'POST', 'PUT'].includes(upper) ? upper : value;
}
class Request {
    constructor(input, init = {}) {
        workerArguments(arguments.length, 1); init = workerDictionary(init);
        const source = workerRequestSlots.get(input), sourceBody = source && workerSlot(workerBodySlots, input);
        const url = new URL(source ? source.url : workerUSV(input), location.href);
        if (url.username || url.password) throw new TypeError('Request URLs cannot contain credentials');
        const suppliedMethod = init.method;
        const method = workerMethod(suppliedMethod === undefined ? source ? source.method : 'GET' : suppliedMethod);
        const suppliedHeaders = init.headers;
        const headerInit = suppliedHeaders === undefined ? source ? source.headers : undefined : suppliedHeaders;
        const state = { url: url.href, method };
        for (const [key, fallback] of Object.entries({ credentials: 'same-origin', mode: 'cors', cache: 'default',
            redirect: 'follow', referrer: 'about:client', referrerPolicy: '', integrity: '', keepalive: false, priority: 'auto' })) {
            const suppliedValue = init[key];
            const value = suppliedValue === undefined ? source ? source[key] : fallback : suppliedValue;
            state[key] = key === 'keepalive' ? !!value : workerUSV(value);
        }
        if (!['omit', 'same-origin', 'include'].includes(state.credentials)) throw new TypeError('Invalid credentials mode');
        if (!['navigate', 'same-origin', 'no-cors', 'cors'].includes(state.mode) || state.mode === 'navigate') throw new TypeError('Invalid Request mode');
        if (!['default', 'no-store', 'reload', 'no-cache', 'force-cache', 'only-if-cached'].includes(state.cache)) throw new TypeError('Invalid Request cache mode');
        if (state.cache === 'only-if-cached' && state.mode !== 'same-origin') throw new TypeError('Cache mode requires same-origin mode');
        if (!['follow', 'error', 'manual'].includes(state.redirect)) throw new TypeError('Invalid Request redirect mode');
        if (!['high', 'low', 'auto'].includes(state.priority)) throw new TypeError('Invalid Request priority');
        if (state.mode === 'no-cors' && !['GET', 'HEAD', 'POST'].includes(method)) throw new TypeError('Invalid no-cors method');
        const headers = headerOps.create(headerInit, state.mode === 'no-cors' ? 'request-no-cors' : 'request');
        state.headers = headers;
        const suppliedBody = init.body, supplied = suppliedBody !== undefined && suppliedBody !== null;
        if (!supplied && sourceBody && sourceBody.used) throw new TypeError('Source request body has been consumed');
        const bytes = supplied ? workerExtractBody(suppliedBody, headers) : sourceBody ? sourceBody.bytes : null;
        if ((method === 'GET' || method === 'HEAD') && bytes !== null) throw new TypeError('GET and HEAD requests cannot have a body');
        workerRequestSlots.set(this, state); workerBodySlots.set(this, { bytes, used: false });
        if (!supplied && sourceBody && bytes !== null) { sourceBody.used = true; sourceBody.bytes = null; }
    }
    clone() {
        const state = workerSlot(workerRequestSlots, this), body = workerSlot(workerBodySlots, this);
        if (body.used) throw new TypeError('Body has already been consumed');
        const copy = Object.create(Request.prototype);
        workerRequestSlots.set(copy, { ...state, headers: headerOps.clone(state.headers) });
        workerBodySlots.set(copy, { bytes: body.bytes === null ? null : body.bytes.slice(), used: false });
        return copy;
    }
}
function workerResponse(object, body, init, internal = false) {
    init = workerDictionary(init);
    const headers = headerOps.create(init.headers, 'response'), suppliedStatus = init.status;
    const number = suppliedStatus === undefined ? 200 : +suppliedStatus;
    const status = Number.isFinite(number) ? ((Math.trunc(number) % 65536) + 65536) % 65536 : 0;
    if (!internal && (status < 200 || status > 599)) throw new RangeError('Invalid response status');
    const suppliedStatusText = init.statusText;
    const statusText = workerUSV(suppliedStatusText === undefined ? '' : suppliedStatusText);
    if (/[^\x00-\xFF]|[\r\n]/.test(statusText)) throw new TypeError('Invalid response status text');
    const bytes = workerExtractBody(body, headers);
    if ([101, 103, 204, 205, 304].includes(status) && bytes !== null) throw new TypeError('This response status cannot have a body');
    workerResponseSlots.set(object, { status, statusText, headers, type: 'default', url: '', redirected: false });
    workerBodySlots.set(object, { bytes, used: false });
}
class Response {
    constructor(body = null, init = {}) { workerResponse(this, body, init); }
    get ok() { const status = workerSlot(workerResponseSlots, this).status; return status >= 200 && status < 300; }
    clone() {
        const state = workerSlot(workerResponseSlots, this), body = workerSlot(workerBodySlots, this);
        if (body.used) throw new TypeError('Body has already been consumed');
        const copy = Object.create(Response.prototype);
        workerResponseSlots.set(copy, { ...state, headers: headerOps.clone(state.headers) });
        workerBodySlots.set(copy, { bytes: body.bytes === null ? null : body.bytes.slice(), used: false });
        return copy;
    }
    static error() {
        const response = Object.create(Response.prototype); workerResponse(response, null, { status: 0 }, true);
        workerResponseSlots.get(response).type = 'error'; headerOps.setGuard(workerResponseSlots.get(response).headers, 'immutable'); return response;
    }
    static redirect(url, status = 302) {
        workerArguments(arguments.length, 1);
        const parsed = new URL(workerUSV(url), location.href), number = +status;
        const converted = Number.isFinite(number) ? ((Math.trunc(number) % 65536) + 65536) % 65536 : 0;
        if (![301, 302, 303, 307, 308].includes(converted)) throw new RangeError('Invalid redirect status');
        const response = new Response(null, { status: converted, headers: { location: parsed.href } });
        headerOps.setGuard(workerResponseSlots.get(response).headers, 'immutable'); return response;
    }
    static json(data, init = {}) {
        workerArguments(arguments.length, 1);
        const encoded = JSON.stringify(data);
        if (encoded === undefined) throw new TypeError('Value cannot be represented as JSON');
        init = workerDictionary(init);
        const headers = new Headers(init.headers);
        if (!headers.has('content-type')) headers.set('content-type', 'application/json');
        return new Response(encoded, { headers, status: init.status, statusText: init.statusText });
    }
}
for (const [Constructor, map, names] of [
    [Request, workerRequestSlots, ['url', 'method', 'headers', 'credentials', 'mode', 'cache', 'redirect', 'referrer', 'referrerPolicy', 'integrity', 'keepalive', 'priority']],
    [Response, workerResponseSlots, ['status', 'statusText', 'headers', 'type', 'url', 'redirected']],
]) {
    for (const name of names) Object.defineProperty(Constructor.prototype, name, {
        get() { return workerSlot(map, this)[name]; }, enumerable: true, configurable: true,
    });
    Object.defineProperties(Constructor.prototype, Object.getOwnPropertyDescriptors(workerBodyMethods));
}
for (const Constructor of [FormData, Request, Response]) {
    Object.defineProperty(Constructor.prototype, Symbol.toStringTag, { value: Constructor.name, configurable: true });
    for (const name of Object.getOwnPropertyNames(Constructor.prototype)) {
        if (name !== 'constructor') Object.defineProperty(Constructor.prototype, name, {
            ...Object.getOwnPropertyDescriptor(Constructor.prototype, name), enumerable: true,
        });
    }
}
for (const name of ['error', 'redirect', 'json']) Object.defineProperty(Response, name, {
    ...Object.getOwnPropertyDescriptor(Response, name), enumerable: true,
});
function fetch(input, init) {
    try {
        const request = new Request(input, init), state = workerRequestSlots.get(request), body = workerBodySlots.get(request);
        const flat = headerOps.flat(state.headers);
        const bytes = body.bytes;
        if (bytes !== null) { body.used = true; body.bytes = null; }
        const result = W.fetch(state.url, state.method, bytes === null ? undefined : bytes, flat, state.credentials === 'omit');
        if (!result) throw new TypeError('Failed to fetch');
        const headers = new Headers();
        for (let index = 0; index + 1 < result[3].length; index += 2) headers.append(result[3][index], result[3][index + 1]);
        const response = new Response([101, 103, 204, 205, 304].includes(result[0]) ? null : result[2], { status: result[0], headers });
        Object.assign(workerResponseSlots.get(response), { url: result[1], type: 'basic', redirected: result[1] !== state.url });
        headerOps.setGuard(workerResponseSlots.get(response).headers, 'immutable');
        return Promise.resolve(response);
    } catch (error) { return Promise.reject(error); }
}

const toFn = f => typeof f === 'function' ? f : (0, eval)('(function(){' + String(f) + '\n})');
const performance = { now: () => W.now(), timeOrigin: W.timeOrigin, mark() {}, measure() {}, getEntriesByName() { return []; }, getEntriesByType() { return []; }, toJSON() { return { timeOrigin: W.timeOrigin }; } };
const fmt = a => a.map(x => typeof x === 'string' ? x : x instanceof Error ? x.stack || String(x) : (() => { try { return JSON.stringify(x); } catch (e) { return String(x); } })()).join(' ');
const console = { log: (...a) => W.log(1, fmt(a)), info: (...a) => W.log(1, fmt(a)), debug: (...a) => W.log(0, fmt(a)), warn: (...a) => W.log(2, fmt(a)), error: (...a) => W.log(3, fmt(a)), trace() {}, group() {}, groupEnd() {}, time() {}, timeEnd() {}, assert: (c, ...a) => { if (!c) W.log(3, 'Assertion failed: ' + fmt(a)); } };
const bcAll = new Set(); let bcIds = 0, bcOn = false;
class BroadcastChannel extends EventTarget {
    constructor(n) {
        super();
        if (!arguments.length) throw new TypeError("Failed to construct 'BroadcastChannel': 1 argument required");
        Object.defineProperty(this, '_id', { value: ++bcIds }); Object.defineProperty(this, '_closed', { value: false, writable: true });
        this.name = String(n); this.onmessage = null; this.onmessageerror = null; bcAll.add(this);
        if (!bcOn) { bcOn = true; W.busOrigin(location.origin); }
    }
    postMessage(m) {
        if (this._closed) throw new DOMException("Failed to execute 'postMessage' on 'BroadcastChannel': Channel is closed", 'InvalidStateError');
        try { W.busPost(this.name, this._id, m); } catch (e) { throw new DOMException(String(e && e.message || e), 'DataCloneError'); }
    }
    close() { this._closed = true; bcAll.delete(this); }
}
const portSub = (v, ps, seen = new Set()) => { if (!ps.length || !v || typeof v !== 'object' || seen.has(v)) return v; const i = ps.indexOf(v); if (i >= 0) return { __lumenPortRef: i }; seen.add(v); if (Array.isArray(v)) return v.map(x => portSub(x, ps, seen)); const pr = Object.getPrototypeOf(v); if (pr !== Object.prototype && pr !== null) return v; const o = {}; for (const k of Object.keys(v)) o[k] = portSub(v[k], ps, seen); return o; };
const portRes = (v, ps, seen = new Set()) => { if (!ps.length || !v || typeof v !== 'object' || seen.has(v)) return v; if (typeof v.__lumenPortRef === 'number' && Object.keys(v).length === 1) return ps[v.__lumenPortRef]; seen.add(v); for (const k of Object.keys(v)) v[k] = portRes(v[k], ps, seen); return v; };
const wports = new Map(); let wpseq = 0;
class MessagePort extends EventTarget {
    constructor() { super(); this.onmessage = null; this.onmessageerror = null; Object.defineProperty(this, '_other', { value: null, writable: true }); Object.defineProperty(this, '_k', { value: 0, writable: true }); }
    postMessage(d, t) { const ps = (Array.isArray(t) ? t : t && t.transfer || []).filter(p => p instanceof MessagePort), k = this._k || (this._other && this._other._k); if (k) { W.post({ __lumenPort: k, data: portSub(d, ps), xfer: ps.map(p => { const j = -(++wpseq); p._k = j; if (p._other) wports.set(j, p._other); return j; }) }); return; } const o = this._other; if (o) { const c = W.clone(d); setTimeout(() => { const e = new MessageEvent('message', { data: c, ports: ps }); e.isTrusted = true; fireOn(o, e); }); } }
    start() {} close() { this._other = null; }
}
class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } }
const api = {
    self: G, globalThis: G, location, navigator, name: W.name, origin: location.origin, isSecureContext: !!W.secure, crossOriginIsolated: !!W.coi,
    onmessage: null, onmessageerror: null, onerror: null, onunhandledrejection: null, onrejectionhandled: null, onlanguagechange: null, onoffline: null, ononline: null,
    postMessage(m, t) { if (arguments.length < 1) throw new TypeError("Failed to execute 'postMessage': 1 argument required, but only 0 present."); const ps = (Array.isArray(t) ? t : t && t.transfer || []).filter(p => p instanceof MessagePort); if (!ps.length) return W.post(m); W.post({ __lumenXfer: ps.map(p => { const k = -(++wpseq); p._k = k; if (p._other) wports.set(k, p._other); return k; }), data: portSub(m, ps) }); },
    close() { W.close(); },
    importScripts(...urls) { for (const u of urls) W.importScript(String(u)); },
    setTimeout: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, false, args), setInterval: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, true, args),
    clearTimeout: id => W.clear(+id || 0), clearInterval: id => W.clear(+id || 0),
    queueMicrotask: f => { if (typeof f !== 'function') throw new TypeError('queueMicrotask callback must be callable'); Promise.resolve().then(() => { try { f(); } catch (error) { W.reportErr(error); } }); }, structuredClone: v => W.clone(v),
    btoa, atob, TextEncoder, TextDecoder, Blob, File, FileReader, FileReaderSync, URL, URLSearchParams, FormData, Headers, Request, Response, fetch, performance, console,
    Event, ProgressEvent, MessageEvent, MessagePort, MessageChannel, BroadcastChannel, ErrorEvent, CustomEvent, PromiseRejectionEvent, EventTarget, DOMException, WorkerGlobalScope, DedicatedWorkerGlobalScope, WorkerLocation, WorkerNavigator,
};
for (const k of Object.keys(api)) Object.defineProperty(G, k, { value: api[k], writable: true, configurable: true, enumerable: false });
return {
    busDeliver(kind, name, data, id, same) {
        if (kind !== 'bc') return;
        let first = true;
        for (const ch of [...bcAll]) if (!ch._closed && ch.name === name && !(same && ch._id === id)) { ch.dispatchEvent(new MessageEvent('message', { data: first ? data : W.clone(data), origin: location.origin })); first = false; }
    },
    onmsg(data) {
        let ports = [];
        if (data && typeof data === 'object') {
            if (data.__lumenPort) { const p = wports.get(data.__lumenPort); if (p) { const qs = (data.xfer || []).map(k => { const q = new MessagePort(); q._k = k; wports.set(k, q); return q; }); const e = new MessageEvent('message', { data: portRes(data.data, qs), ports: qs }); e.isTrusted = true; fireOn(p, e); } return; }
            if (data.__lumenXfer) { ports = data.__lumenXfer.map(k => { const q = new MessagePort(); q._k = k; wports.set(k, q); return q; }); data = portRes(data.data, ports); }
        }
        const e = new MessageEvent('message', { data, ports }); e.isTrusted = true; fireOn(G, e);
    },
    report(error, message, filename, lineno, colno) {
        const e = new ErrorEvent('error', { message, filename, lineno, colno, error, cancelable: true });
        e.isTrusted = true;
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

static v8::ModifyCodeGenerationFromStringsResult w_eval_cb(v8::Local<v8::Context>, v8::Local<v8::Value>, bool) {
    WEnv *E = (WEnv *)v8::Isolate::GetCurrent()->GetData(kEnvSlot);
    v8::ModifyCodeGenerationFromStringsResult r;
    r.codegen_allowed = !E || !E->has_csp || csp_allows_eval(E->csp.c_str(), false);
    if (!r.codegen_allowed) fprintf(stderr, "lumen: CSP blocked eval in worker\n");
    return r;
}
static bool w_wasm_cb(v8::Local<v8::Context>, v8::Local<v8::String>) {
    WEnv *E = (WEnv *)v8::Isolate::GetCurrent()->GetData(kEnvSlot);
    bool ok = !E || !E->has_csp || csp_allows_eval(E->csp.c_str(), true);
    if (!ok) fprintf(stderr, "lumen: CSP blocked WebAssembly compile in worker\n");
    return ok;
}
void worker_main(std::shared_ptr<Worker> w) {
    if (w->term) return;
    v8::Isolate::CreateParams cp;
    /* SharedArrayBuffer messages can outlive this worker's isolate and thread.
     * Let each backing store retain the allocator until its final receiver or
     * undelivered message releases it. */
    cp.array_buffer_allocator_shared.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate *iso = v8::Isolate::New(cp);
    bool stopped;
    { std::lock_guard<std::mutex> lk(w->mu); stopped = w->term; if (!stopped) w->iso = iso; }
    if (stopped) { iso->Dispose(); return; }
    iso->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    WEnv *E = new WEnv; E->w = w; E->iso = iso; E->t0 = now_ms();
    iso->SetData(kEnvSlot, E);
    iso->SetHostInitializeImportMetaObjectCallback(wm_meta);
    iso->SetModifyCodeGenerationFromStringsCallback(w_eval_cb);
    iso->SetAllowWasmCodeGenerationCallback(w_wasm_cb);
    {
        v8::Isolate::Scope is(iso);
        v8::HandleScope hs(iso);
        v8::Local<v8::Context> ctx = v8::Context::New(iso);
        E->ctx.Reset(iso, ctx);
        ctx->AllowCodeGenerationFromStrings(false);
        v8::Context::Scope cs(ctx);
        v8::Local<v8::Object> W = v8::Object::New(iso);
#define WREG(nm) (void)W->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, w_##nm).ToLocalChecked())
        WREG(post); WREG(clone); WREG(timer); WREG(clear); WREG(close); WREG(now); WREG(log); WREG(decode); WREG(textEncoding); WREG(textDecode); WREG(blobState); WREG(fileReaderPackage); WREG(fileReaderCheckpoint); WREG(resolve); WREG(urlParse); WREG(urlSet); WREG(idlGet); WREG(idlPut); WREG(idlRegister); WREG(idlFallback); WREG(initCloneIntrinsic); WREG(fetch); WREG(importScript); WREG(reportErr); WREG(busOrigin); WREG(busPost);
        auto setS = [&](const char *k, const std::string &v) { (void)W->Set(ctx, jstr(iso, k), jstr(iso, v.c_str())); };
        setS("url", w->url); setS("ua", w->ua); setS("platform", w->platform); setS("name", w->name);
        (void)W->Set(ctx, jstr(iso, "coi"), v8::Boolean::New(iso, w->coi));
        (void)W->Set(ctx, jstr(iso, "secure"), v8::Boolean::New(iso, w->creator_secure));
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
                if (api.As<v8::Object>()->Get(ctx, jstr(iso, "busDeliver")).ToLocal(&f) && f->IsFunction()) E->busfn.Reset(iso, f.As<v8::Function>());
            }
            if (tc.HasCaught()) fprintf(stderr, "lumen: worker prelude failed: %s\n", jcstr(iso, tc.Exception()).c_str());
        }
        std::string src = w->src; bool ok = w->have_src;
        if (w->has_ocsp && !csp_allows_url(w->ocsp.c_str(), w->odoc.c_str(), w->url.c_str(), "worker-src")) {
            fprintf(stderr, "lumen: CSP worker-src blocked %.120s\n", w->url.c_str()); ok = false;
        } else if (ok) { E->csp = w->ocsp; E->cspdoc = w->odoc; E->has_csp = w->has_ocsp; }
        else if (!w->term) {
            NetRequest *rq = net_request_new("GET", w->url.c_str());
            NetResponse *r = worker_fetch(w.get(), rq);
            if (r && r->status >= 200 && r->status < 300) {
                src.assign(r->body ? r->body : "", r->body_len); ok = true;
                if (const char *h = headers_get(&r->headers, "content-security-policy")) { E->csp = h; E->cspdoc = w->url; E->has_csp = true; }
            }
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
            bool bg = iso->HasPendingBackgroundTasks();   /* e.g. async WebAssembly compile: poll until it posts back */
            while (v8::platform::PumpMessageLoop(js_platform(), iso)) {}
            iso->PerformMicrotaskCheckpoint();
            {
                std::unique_lock<std::mutex> lk(w->mu);
                double next = bg ? now_ms() + 4 : 1e300;
                for (auto &kv : E->timers) next = std::min(next, kv.second.due);
                auto ready = [&] { return w->term.load() || !w->in.empty() || (!E->bus_origin.empty() && js_bus_seq() != E->bus_seen); };
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
                    if (deser(iso, ctx, b).ToLocal(&v) && !E->onmsg.IsEmpty()) { (void)E->onmsg.Get(iso)->Call(ctx, v8::Undefined(iso), 1, &v); wcheck(E, tc); }
                    iso->PerformMicrotaskCheckpoint();
                }
                free(b.p);
            }
            if (!E->busfn.IsEmpty() && !w->term && !E->closing) {
                std::vector<BusMsg> bm;
                if (js_bus_take(E->bus_origin, E, E->bus_seen, bm)) {
                    last_act = now_ms(); idle_gc = false;
                    v8::HandleScope hs3(iso);
                    v8::Local<v8::Function> bf = E->busfn.Get(iso);
                    for (auto &m : bm) {
                        v8::TryCatch tc(iso);
                        v8::Local<v8::Value> argv[5], r;
                        if (js_bus_args(iso, ctx, m, E, argv)) { (void)bf->Call(ctx, v8::Undefined(iso), 5, argv).ToLocal(&r); wcheck(E, tc); }
                        iso->PerformMicrotaskCheckpoint();
                    }
                }
            }
            if (w->term) break;
            run_timers(E);
        }
        js_bus_listen(E, nullptr);
        E->timers.clear(); E->mods.clear(); E->murl.clear(); E->onmsg.Reset(); E->report.Reset(); E->busfn.Reset(); E->ctx.Reset();
    }
    {
        std::lock_guard<std::mutex> lk(w->mu);
        w->iso = nullptr;
        close_input_locked(w.get());
    }
    iso->Dispose();
    w->heap = 0; w->ext = 0;
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
    char *u = url_join_n(base.c_str(), spec.data(), spec.size()); std::string r = u ? u : ""; free(u); return r;
}
bool wm_graph(WEnv *E, v8::Local<v8::Module> m) {
    v8::Isolate *iso = E->iso;
    std::string base = wm_key(E, m);
    v8::Local<v8::FixedArray> rq = m->GetModuleRequests();
    for (int i = 0; i < rq->Length(); i++) {
        if (E->w->term) return false;
        std::string sp = jcstr(iso, rq->Get(i).As<v8::ModuleRequest>()->GetSpecifier()), u = wm_resolve(sp, base);
        if (u.empty()) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("Failed to resolve module specifier \"" + sp + "\"").c_str()))); return false; }
        if (E->mods.count(u)) continue;
        if (E->has_csp && !csp_allows_url(E->csp.c_str(), E->cspdoc.c_str(), u.c_str(), "script-src")) {
            fprintf(stderr, "lumen: CSP script-src blocked module %.120s\n", u.c_str());
            iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("Failed to fetch module: " + u).c_str()))); return false;
        }
        NetRequest *rq = net_request_new("GET", u.c_str());
        NetResponse *r = worker_fetch(E->w.get(), rq);
        if (E->w->term) { net_response_free(r); return false; }
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
    WorkerThreads &threads = worker_threads();
    {
        std::unique_ptr<std::shared_ptr<Worker>> sp(static_cast<std::shared_ptr<Worker> *>(p));
        worker_main(*sp);
        (*sp)->finished = true;
    } /* Release the thread's Worker reference before signaling completion. */
    {
        std::lock_guard<std::mutex> lock(threads.mu);
        --threads.running;
        threads.cv.notify_all();
    } /* No Lumen/V8/global access after this point. */
    return nullptr;
}

void worker_terminate(Worker *w) {
    std::lock_guard<std::mutex> lk(w->mu);
    w->term = true;
    close_input_locked(w);
    if (w->request) net_request_cancel(w->request);
    if (w->iso) w->iso->TerminateExecution();
    for (OutMsg &m : w->out) free(m.data.p);
    w->out.clear();
    w->cv.notify_all();
}

void worker_exit() {
    if (js_workers_shutdown(5000)) return;
    /* DNS/native library work cannot always be interrupted. Never continue
     * into static destruction with live workers or pretend exit succeeded.
     */
    fprintf(stderr, "lumen: worker shutdown exceeded 5 seconds; terminating before unsafe static cleanup\n");
    fflush(stderr);
    std::_Exit(EXIT_FAILURE);
}

/* ---- main thread ---- */
void n_workerNew(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    WorkerThreads &threads = worker_threads();
    { std::lock_guard<std::mutex> lock(threads.mu); if (threads.stopping) { a.GetReturnValue().Set(0); return; } }
    auto w = std::make_shared<Worker>();
    w->id = ++g_wseq; w->owner = jctx(iso); w->coi = w->owner && w->owner->doc && w->owner->doc->cross_origin_isolated;
    w->creator_secure = js_windows_is_secure(w->owner);
    if (const char *t = js_top_url(w->owner)) w->top = t;
    if (w->owner && w->owner->doc && w->owner->doc->csp) { w->ocsp = w->owner->doc->csp; w->odoc = w->owner->doc->url ? w->owner->doc->url : ""; w->has_ocsp = true; }
    w->url = jcstr(iso, a[0]); w->have_src = a[1]->IsString(); if (w->have_src) w->src = jcstr(iso, a[1]);
    w->ua = jcstr(iso, a[2]); w->platform = jcstr(iso, a[3]); w->name = a[4]->IsString() ? jcstr(iso, a[4]) : ""; w->module = a.Length() > 5 && a[5]->IsTrue();
    g_workers[w->id] = w;
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8u << 20); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    auto *arg = new std::shared_ptr<Worker>(w);
    { std::lock_guard<std::mutex> lock(threads.mu); ++threads.running; }
    if (pthread_create(&th, &at, worker_thread, arg) != 0) {
        { std::lock_guard<std::mutex> lock(threads.mu); --threads.running; threads.cv.notify_all(); }
        delete arg; g_workers.erase(w->id); a.GetReturnValue().Set(0); pthread_attr_destroy(&at); return;
    }
    pthread_attr_destroy(&at);
    a.GetReturnValue().Set(w->id);
}
void n_workerPost(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    auto it = g_workers.find(a[0]->Uint32Value(ctx).FromMaybe(0));
    /* Serialization invokes getters; one can call terminate() and erase the
     * registry entry. Keep the Worker alive, never retain an iterator across JS. */
    std::shared_ptr<Worker> w = it == g_workers.end() ? nullptr : it->second;
    Buf b;
    if (!ser(iso, ctx, a[1], b)) return;
    if (!w) { free(b.p); return; }
    {
        std::lock_guard<std::mutex> lk(w->mu);
        /* Recheck under the queue lock: close/exit may race serialization. */
        if (w->term || !w->accepting) { free(b.p); return; }
        w->in.push_back(std::move(b));
    }
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

extern "C" bool js_workers_shutdown(unsigned timeout_ms) {
    WorkerThreads &threads = worker_threads();
    { std::lock_guard<std::mutex> lock(threads.mu); threads.stopping = true; }
    for (auto &entry : g_workers) worker_terminate(entry.second.get());
    g_workers.clear();
    return wait_worker_threads(timeout_ms);
}

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

extern "C" void worker_mem_stats(size_t *heap, size_t *ext, int *n) {
    *heap = *ext = 0; *n = 0;
    for (auto &kv : g_workers) { *heap += kv.second->heap; *ext += kv.second->ext; ++*n; }
}
