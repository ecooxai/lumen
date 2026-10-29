/* Dedicated and shared Web Workers run on their own native thread/V8 isolate.
   Shared connections are distinct native routes, never dedicated-worker aliases.
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
struct Side {
    std::vector<v8::CompiledWasmModule> wasm;
    std::vector<std::shared_ptr<v8::BackingStore>> sab;
    /* Transfer envelopes retain storage, never source-isolate handles. The
     * allocator is shared by JsCtx and worker_main, so queued buffers may
     * safely outlive their creator or be discarded during termination. */
    std::vector<std::shared_ptr<v8::BackingStore>> buffers;
};
struct Buf { uint8_t *p = nullptr; size_t n = 0; std::shared_ptr<Side> side; };
enum { WI_MESSAGE, WI_CONNECT, WI_DISCONNECT };
struct InMsg { Buf data; uint32_t connection = 0; int kind = WI_MESSAGE; };
enum { WM_MESSAGE, WM_ERROR, WM_LOADFAIL };
struct OutMsg { int kind; Buf data; std::string msg, file; int line = 0, col = 0; uint32_t connection = 0; };
struct WNetworkResult {
    uint32_t id = 0;
    std::unique_ptr<NetResponse, decltype(&net_response_free)> response{nullptr, net_response_free};
};
struct Worker {
    uint32_t id = 0; JsCtx *owner = nullptr;
    bool creator_secure = false; /* native owner setting, captured before thread creation */
    std::string url, src, ua, platform, name, top; bool have_src = false, module = false, coi = false;
    bool shared = false;
    std::string creator_origin, top_origin, credentials = "same-origin";
    // Protected by mu. The worker thread only uses these IDs as routing
    // capabilities; it never dereferences an owning document pointer.
    std::map<uint32_t, JsCtx *> clients;
    std::string ocsp, odoc; bool has_ocsp = false;   /* creator document's CSP: worker-src check, inherited by blob:/data: workers */
    std::mutex mu; std::condition_variable cv;
    std::deque<InMsg> in; std::deque<OutMsg> out;
    // Network threads/main-loop callbacks own only native data. V8 handles stay
    // in WEnv and are entered exclusively by this worker's event loop.
    std::map<uint32_t, uint64_t> requests; // protected by mu
    std::deque<WNetworkResult> network;   // protected by mu
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
    std::map<uint32_t, v8::Global<v8::Function>> network_callbacks; uint32_t nseq = 0;
    std::map<uint32_t, WTimer> timers; uint32_t tseq = 0; bool closing = false, reporting = false; double t0 = 0;
};
std::map<uint32_t, std::shared_ptr<Worker>> g_workers;   /* main thread only */
struct SharedClient { std::shared_ptr<Worker> worker; JsCtx *owner; };
std::map<uint32_t, SharedClient> g_shared_clients;       /* main thread only */
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
bool ser(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> v, Buf &out,
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
}
v8::MaybeLocal<v8::Value> deser(v8::Isolate *iso, v8::Local<v8::Context> ctx, const Buf &b) {
    DesDel del(b.side);
    v8::ValueDeserializer d(iso, b.p, b.n, &del);
    if (b.side) for (uint32_t i = 0; i < b.side->buffers.size(); ++i)
        d.TransferArrayBuffer(i, v8::ArrayBuffer::New(iso, b.side->buffers[i]));
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
    for (InMsg &m : w->in) free(m.data.p);
    w->in.clear();
}
// Caller holds Worker::mu. net_cancel never enters a V8 isolate or invokes a
// completion callback synchronously, so this lock order cannot re-enter us.
void cancel_network_locked(Worker *w) {
    for (const auto &request : w->requests) net_cancel(request.second);
    w->requests.clear();
    w->network.clear();
}
struct WNetworkOwner { std::shared_ptr<Worker> worker; uint32_t id; };
void worker_network_done(NetRequest *, NetResponse *response, void *data) {
    auto *owner = static_cast<WNetworkOwner *>(data);
    auto &w = owner->worker;
    std::lock_guard<std::mutex> lock(w->mu);
    if (!w->requests.erase(owner->id) || w->term || !w->accepting) return;
    WNetworkResult result; result.id = owner->id;
    if (response) {
        // The network pool frees its response shell after this callback. Transfer the
        // owned payload instead of doubling a large response body in RAM.
        auto *owned = static_cast<NetResponse *>(xmalloc(sizeof(NetResponse)));
        *owned = *response; *response = {};
        result.response.reset(owned);
    }
    w->network.push_back(std::move(result));
    w->cv.notify_all();
}
void worker_network_release(void *data) {
    auto *owner = static_cast<WNetworkOwner *>(data);
    {
        std::lock_guard<std::mutex> lock(owner->worker->mu);
        owner->worker->requests.erase(owner->id);
    }
    delete owner;
}
void post_out(WEnv *E, OutMsg &&m) {
    {
        std::lock_guard<std::mutex> lk(E->w->mu);
        if (E->w->term) { free(m.data.p); return; }
        if (!E->w->shared) E->w->out.push_back(std::move(m));
        else if (m.connection) {
            if (E->w->clients.count(m.connection)) E->w->out.push_back(std::move(m));
            else free(m.data.p);
        } else {
            // Only errors broadcast. A serialized message belongs to exactly
            // one port; sharing its transfer buffer between clients is unsafe.
            if (m.kind == WM_MESSAGE) { free(m.data.p); return; }
            for (const auto &client : E->w->clients) {
                OutMsg copy{m.kind, {}, m.msg, m.file, m.line, m.col, client.first};
                E->w->out.push_back(std::move(copy));
            }
        }
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
WFN(post) {
    WENV; Buf b;
    uint32_t connection = E->w->shared && a.Length() > 2 ? a[2]->Uint32Value(ctx).FromMaybe(0) : 0;
    if (ser(iso, ctx, a[0], b, a[1])) post_out(E, OutMsg{ WM_MESSAGE, b, {}, {}, 0, 0, connection });
}
WFN(clone) { WENV; Buf b; if (!ser(iso, ctx, a[0], b, a[1])) return; v8::Local<v8::Value> v; if (deser(iso, ctx, b).ToLocal(&v)) a.GetReturnValue().Set(v); free(b.p); }
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
    cancel_network_locked(E->w.get());
}
WFN(now) { WENV; a.GetReturnValue().Set(now_ms() - E->t0); }
WFN(log) { WENV; if (getenv("LUMEN_CONSOLE")) fprintf(stderr, "[worker %d] %.2000s\n", a[0]->Int32Value(ctx).FromMaybe(1), jcstr(iso, a[1]).c_str()); }
WFN(blobState) { jsg_blob_state(a); }
WFN(fileReaderPackage) { jsg_file_reader_package(a); }
WFN(fileReaderCheckpoint) { jsg_file_reader_checkpoint(a); }
WFN(textEncoding) { jsg_text_encoding(a); }
WFN(textDecode) { jsg_text_decode(a); }
WFN(responseTextDecode) { jsg_response_text_decode(a); }
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

// Common byte-oriented transport for worker fetch and XMLHttpRequest. The
// asynchronous entry transfers request ownership to the existing network pool;
// the synchronous entry is reserved for explicit synchronous worker XHR.
NetRequest *worker_request(WEnv *E, const FCI &a) {
    auto *iso = E->iso; auto ctx = iso->GetCurrentContext();
    std::string method = jcstr(iso, a[0]), url = jcstr(iso, a[1]);
    NetRequest *rq = net_request_ctx(method.c_str(), url.c_str(), E->w->top.empty() ? nullptr : E->w->top.c_str());
    std::string credentials = jcstr(iso, a[4]);
    rq->no_cookies = credentials == "omit";
    if (!rq->no_cookies && credentials != "include") rq->credentials_origin = strdup(jcstr(iso, a[6]).c_str());
    if (E->has_csp) { rq->csp = strdup(E->csp.c_str()); rq->csp_doc = strdup(E->cspdoc.c_str()); rq->csp_dir = "connect-src"; }
    if (a[2]->IsArray()) {
        auto headers = a[2].As<v8::Array>();
        for (uint32_t i = 0; i + 1 < headers->Length(); i += 2) {
            v8::Local<v8::Value> k, v;
            if (!headers->Get(ctx, i).ToLocal(&k) || !headers->Get(ctx, i + 1).ToLocal(&v)) { net_request_free(rq); return nullptr; }
            std::string key, value;
            if (!jheader_bytes(iso, k, true, key) || !jheader_bytes(iso, v, false, value)) { net_request_free(rq); return nullptr; }
            headers_add(&rq->headers, key.c_str(), value.c_str());
        }
    }
    if (char *referrer = net_referrer(E->w->url.c_str(), url.c_str(), nullptr)) {
        headers_add(&rq->headers, "Referer", referrer); free(referrer);
    }
    const char *body; size_t size;
    if (bytes_of(a[3], body, size) && size) {
        rq->body = static_cast<char *>(xmalloc(size)); memcpy(rq->body, body, size); rq->body_len = size;
    }
    return rq;
}
void worker_response_args(v8::Isolate *iso, NetResponse *response, v8::Local<v8::Value> out[6]) {
    auto ctx = iso->GetCurrentContext();
    out[0] = v8::Integer::New(iso, response ? response->status : 0);
    out[1] = jstr(iso, response && response->status_text ? response->status_text : "");
    out[2] = jstr(iso, response && response->url ? response->url : "");
    auto headers = v8::Array::New(iso, response ? response->headers.n * 2 : 0);
    if (response) for (int i = 0; i < response->headers.n; ++i) {
        (void)headers->Set(ctx, 2 * i, jstr(iso, response->headers.v[i].name));
        (void)headers->Set(ctx, 2 * i + 1, jheader_string(iso, response->headers.v[i].value));
    }
    out[3] = headers;
    out[4] = make_ab(iso, response && response->body ? response->body : "", response && response->body ? response->body_len : 0);
    out[5] = v8::Boolean::New(iso, !response || response->status == 0);
}
WFN(send) {
    WENV;
    if (!a[5]->IsFunction() || E->closing || E->w->term) { a.GetReturnValue().Set(0); return; }
    NetRequest *rq = worker_request(E, a); if (!rq) return;
    uint32_t id;
    do { id = ++E->nseq; } while (!id || E->network_callbacks.count(id));
    E->network_callbacks.emplace(id, v8::Global<v8::Function>(iso, a[5].As<v8::Function>()));
    rq->done = worker_network_done; rq->release = worker_network_release;
    rq->background_done = true;
    rq->ud = new WNetworkOwner{E->w, id};
    {
        std::lock_guard<std::mutex> lock(E->w->mu);
        // Holding mu across enqueue and ID registration prevents a very fast
        // completion from overtaking ownership registration.
        if (E->w->term || !E->w->accepting) {
            rq->release = nullptr; delete static_cast<WNetworkOwner *>(rq->ud);
            net_request_free(rq); E->network_callbacks.erase(id);
            a.GetReturnValue().Set(0); return;
        }
        E->w->requests[id] = net_fetch(rq);
    }
    a.GetReturnValue().Set(id);
}
WFN(sendSync) {
    WENV;
    NetRequest *rq = worker_request(E, a); if (!rq) return;
    double timeout = a[5]->NumberValue(ctx).FromMaybe(0);
    double deadline = timeout > 0 ? now_ms() + timeout : 0;
    rq->deadline_ms = deadline;
    NetResponse *response = worker_fetch(E->w.get(), rq);
    if (E->w->term) { net_response_free(response); return; }
    v8::Local<v8::Value> result[7]; worker_response_args(iso, response, result);
    result[6] = v8::Boolean::New(iso, (!response || !response->status) && deadline > 0 && now_ms() >= deadline);
    net_response_free(response);
    a.GetReturnValue().Set(v8::Array::New(iso, result, 7));
}
WFN(abort) {
    WENV; uint32_t id = a[0]->Uint32Value(ctx).FromMaybe(0);
    E->network_callbacks.erase(id);
    std::lock_guard<std::mutex> lock(E->w->mu);
    auto request = E->w->requests.find(id);
    if (request != E->w->requests.end()) { net_cancel(request->second); E->w->requests.erase(request); }
    // A completion already queued before abort is suppressed when its missing
    // callback is inspected by the owning event loop, then freed normally.
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
class SharedWorkerGlobalScope extends WorkerGlobalScope {}
Object.setPrototypeOf(G, W.shared ? SharedWorkerGlobalScope.prototype : DedicatedWorkerGlobalScope.prototype);

)JS"
#include "dom_exception.inc"
#include "url_interface.inc"
#include "url_search_params.inc"
#include "file_reader.inc"
#include "blob_file.inc"
#include "text_codec.inc"
#include "headers.inc"
#include "web_streams.inc"
#include "worker_xhr.inc"
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

const { TextEncoder, TextDecoder } = createTextCodecInterfaces(null, { label: W.textEncoding, decode: W.textDecode });
const { ReadableStream, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultWriter,
    TransformStream, ByteLengthQueuingStrategy, CountQueuingStrategy, TextEncoderStream, TextDecoderStream
} = createWebStreamInterfaces(nativeIDL, { TextEncoder, TextDecoder, AbortController, DOMException });

const headerOps = createHeadersInterface();
const { Headers } = headerOps;
const blobFileOps = createBlobFileInterfaces(nativeIDL, {
    blobState: W.blobState, encode: urlSearchParamsOps.utf8Bytes, decode: W.decode,
    stream: bytes => new ReadableStream({ start(controller) { controller.enqueue(bytes); controller.close(); } }),
});
const {Blob, File} = blobFileOps;
const progressEventSlots = new WeakMap();
const progressEventState = event => {
    const state = progressEventSlots.get(event);
    if (!state) throw new TypeError('Illegal invocation');
    return state;
};
function progressUnsignedLongLong(value) {
    value = +value;
    if (!Number.isFinite(value) || value === 0) return 0;
    const remainder = Math.trunc(value) % 18446744073709551616;
    if (remainder === 0) return 0;
    return remainder < 0 ? remainder + 18446744073709551616 : remainder;
}
class ProgressEvent extends Event {
    constructor(type, init = {}) {
        if (!arguments.length) throw new TypeError('ProgressEvent requires a type');
        if (init !== null && typeof init !== 'object' && typeof init !== 'function')
            throw new TypeError('ProgressEvent options must be a dictionary');
        super(type, init); init = init || {};
        progressEventSlots.set(this, { lengthComputable: !!init.lengthComputable,
            loaded: progressUnsignedLongLong(init.loaded), total: progressUnsignedLongLong(init.total) });
    }
    get lengthComputable() { return progressEventState(this).lengthComputable; }
    get loaded() { return progressEventState(this).loaded; }
    get total() { return progressEventState(this).total; }
}
for (const name of ['lengthComputable', 'loaded', 'total']) {
    const descriptor = Object.getOwnPropertyDescriptor(ProgressEvent.prototype, name);
    descriptor.enumerable = true; Object.defineProperty(ProgressEvent.prototype, name, descriptor);
}
Object.defineProperty(ProgressEvent.prototype, Symbol.toStringTag, { value: 'ProgressEvent', configurable: true });
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
        const suppliedSignalValue = init.signal;
        const suppliedSignal = suppliedSignalValue === undefined ? source ? source.signal : null : suppliedSignalValue;
        const controller = new AbortController();
        if (suppliedSignal !== null) {
            abortState(suppliedSignal); // validate the actual AbortSignal brand
            if (suppliedSignal.aborted) controller.abort(suppliedSignal.reason);
            else suppliedSignal.addEventListener('abort', () => controller.abort(suppliedSignal.reason), { once: true });
        }
        state.signal = controller.signal;
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
    [Request, workerRequestSlots, ['url', 'method', 'headers', 'credentials', 'mode', 'cache', 'redirect', 'referrer', 'referrerPolicy', 'integrity', 'keepalive', 'priority', 'signal']],
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
const { XMLHttpRequest, XMLHttpRequestEventTarget, XMLHttpRequestUpload, transport: workerNetwork } = createWorkerXMLHttpRequestInterfaces({
    W, EventTarget, Event, ProgressEvent, DOMException, Headers, URL, Blob, URLSearchParams,
    headerOps, bodyInit: workerExtractBody, blobFileOps, objectURLs: workerObjectURLs,
    location, TextEncoder,
    dispatch(target, event) { event.isTrusted = true; return fireOn(target, event, true); },
});
function fetch(input, init) {
    try {
        const request = new Request(input, init), state = workerRequestSlots.get(request), body = workerBodySlots.get(request);
        const bytes = body.bytes;
        if (bytes !== null) { body.used = true; body.bytes = null; }
        if (state.signal.aborted) return Promise.reject(state.signal.reason);
        if (state.mode === 'same-origin' && new URL(state.url).origin !== location.origin)
            return Promise.reject(new TypeError('Cross-origin request forbidden by same-origin mode'));
        return new Promise((resolve, reject) => {
            let requestId = 0, settled = false;
            const finish = (failed, value) => {
                if (settled) return; settled = true;
                state.signal.removeEventListener('abort', abort);
                if (failed) reject(value); else resolve(value);
            };
            const abort = () => { if (requestId) workerNetwork.cancel(requestId); finish(true, state.signal.reason); };
            state.signal.addEventListener('abort', abort, { once: true });
            const flat = workerNetwork.withOrigin(headerOps.flat(state.headers), state.url, state.method, state.mode);
            const complete = (status, statusText, url, responseHeaders, responseBytes, error) => {
                requestId = 0; if (settled) return;
                try {
                    if (error || !status) throw new TypeError('Failed to fetch');
                    const cross = new URL(state.url).origin !== location.origin || new URL(url || state.url).origin !== location.origin;
                    const local = /^(data|blob):/.test(url || state.url);
                    if (state.mode !== 'no-cors' && !workerNetwork.allowed(state.url, url, responseHeaders, state.credentials === 'include'))
                        throw new TypeError('CORS response was not allowed');
                    if (state.redirect === 'error' && url !== state.url) throw new TypeError('Unexpected redirect');
                    const headers = new Headers();
                    const visibleHeaders = workerNetwork.responseHeaders(state.url, url, responseHeaders, state.credentials);
                    for (let index = 0; index + 1 < visibleHeaders.length; index += 2) headers.append(visibleHeaders[index], visibleHeaders[index + 1]);
                    let response;
                    if (cross && !local && state.mode === 'no-cors') {
                        response = Object.create(Response.prototype); workerResponse(response, null, { status: 0 }, true);
                        Object.assign(workerResponseSlots.get(response), { type: 'opaque', url: '' });
                    } else {
                        response = new Response([101, 103, 204, 205, 304].includes(status) ? null : responseBytes, { status, statusText, headers });
                        Object.assign(workerResponseSlots.get(response), { url, type: cross && !local ? 'cors' : 'basic', redirected: url !== state.url });
                    }
                    headerOps.setGuard(workerResponseSlots.get(response).headers, 'immutable');
                    finish(false, response);
                } catch (error) { finish(true, error); }
            };
            if (state.mode === 'no-cors') requestId = W.send(state.method, state.url, flat, bytes, state.credentials, complete, location.origin);
            else workerNetwork.send(state.credentials, id => { requestId = id; }, state.method, state.url, flat, bytes, complete);
            if (!requestId && !settled) finish(true, new TypeError('Worker is closing'));
        });
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
// Convert the Web IDL overload before serialization. In particular, do not
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
const wports = new Map(); let wpseq = 0;
const workerPortState = new WeakMap();
const workerPortKey = (connection, key) => connection + ':' + key;
function portState(port) {
    const state = workerPortState.get(port);
    if (!state) throw new TypeError('Illegal MessagePort receiver');
    return state;
}
class MessagePort extends EventTarget {
    constructor() {
        super();
        workerPortState.set(this, { started: false, closed: false, scheduled: false, queue: [], handler: null });
        this.onmessageerror = null;
        def(this, '_other', null); def(this, '_k', 0); def(this, '_client', 0); def(this, '_remote', false);
    }
    get onmessage() { return portState(this).handler; }
    set onmessage(value) {
        const state = portState(this); state.handler = typeof value === 'function' ? value : null;
        this.start();
    }
    postMessage(data, options) {
        const state = portState(this);
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage': 1 argument required.");
        const { ports, buffers } = portList(options);
        const endpoint = !state.closed && (this._remote ? this : this._other && this._other._remote ? this._other : null);
        if (endpoint) {
            const ids = ports.map(() => -(++wpseq)), connection = endpoint._client;
            W.post({ __lumenPort: endpoint._k, data: portSub(data, ports), xfer: ids }, buffers, connection);
            ports.forEach((port, i) => {
                port._k = ids[i]; port._client = connection; port._remote = true;
                if (port._other) wports.set(workerPortKey(connection, ids[i]), port._other);
            });
            return;
        }
        const other = !state.closed && this._other;
        const cloned = W.clone(portSub(data, ports), buffers);
        if (other) other._deliver(portRes(cloned, ports), ports);
    }
    _deliver(data, ports) {
        const state = portState(this); if (state.closed) return;
        state.queue.push({data, ports: ports || []}); this._flush();
    }
    _flush() {
        const state = portState(this);
        if (state.closed || !state.started || state.scheduled || !state.queue.length) return;
        state.scheduled = true;
        W.timer(() => {
            state.scheduled = false;
            if (state.closed || !state.started || !state.queue.length) return;
            const event = new MessageEvent('message', state.queue.shift()); event.isTrusted = true;
            fireOn(this, event); this._flush();
        }, 0, false, []);
    }
    start() { const state = portState(this); state.started = true; this._flush(); }
    close() {
        const state = portState(this); if (state.closed) return;
        state.closed = true; state.queue.length = 0; this._other = null;
        if (this._remote) wports.delete(workerPortKey(this._client, this._k));
        this._remote = false;
    }
}
function incomingPort(connection, key) {
    const port = new MessagePort(); port._client = connection; port._k = key; port._remote = true;
    wports.set(workerPortKey(connection, key), port); return port;
}
class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } }
const api = {
    self: G, globalThis: G, location, navigator, name: W.name, origin: location.origin, isSecureContext: !!W.secure, crossOriginIsolated: !!W.coi,
    onmessage: null, onmessageerror: null, onconnect: null, onerror: null, onunhandledrejection: null, onrejectionhandled: null, onlanguagechange: null, onoffline: null, ononline: null,
    postMessage(data, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'postMessage': 1 argument required.");
        const { ports, buffers } = portList(options);
        if (!ports.length) return W.post(data, buffers);
        const ids = ports.map(() => -(++wpseq));
        W.post({ __lumenXfer: ids, data: portSub(data, ports) }, buffers);
        ports.forEach((port, i) => { port._k = ids[i]; port._remote = true; if (port._other) wports.set(workerPortKey(0, ids[i]), port._other); });
    },
    close() { W.close(); },
    importScripts(...urls) { for (const u of urls) W.importScript(String(u)); },
    setTimeout: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, false, args), setInterval: (f, ms, ...args) => W.timer(toFn(f), +ms || 0, true, args),
    clearTimeout: id => W.clear(+id || 0), clearInterval: id => W.clear(+id || 0),
    queueMicrotask: f => { if (typeof f !== 'function') throw new TypeError('queueMicrotask callback must be callable'); Promise.resolve().then(() => { try { f(); } catch (error) { W.reportErr(error); } }); },
    structuredClone(value, options) {
        if (!arguments.length) throw new TypeError("Failed to execute 'structuredClone': 1 argument required.");
        return W.clone(value, cloneTransferList(options));
    },
    btoa, atob, TextEncoder, TextDecoder, TextEncoderStream, TextDecoderStream,
    ReadableStream, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultWriter,
    TransformStream, ByteLengthQueuingStrategy, CountQueuingStrategy, AbortController, AbortSignal,
    Blob, File, FileReader, FileReaderSync, URL, URLSearchParams, FormData, Headers, Request, Response, fetch, performance, console,
    XMLHttpRequest, XMLHttpRequestEventTarget, XMLHttpRequestUpload,
    Event, ProgressEvent, MessageEvent, MessagePort, MessageChannel, BroadcastChannel, ErrorEvent, CustomEvent, PromiseRejectionEvent, EventTarget, DOMException, WorkerGlobalScope, DedicatedWorkerGlobalScope, SharedWorkerGlobalScope, WorkerLocation, WorkerNavigator,
};
if (W.shared) { delete api.postMessage; delete api.onmessage; delete api.onmessageerror; delete api.DedicatedWorkerGlobalScope; }
else { delete api.onconnect; delete api.SharedWorkerGlobalScope; }
for (const k of Object.keys(api)) Object.defineProperty(G, k, { value: api[k], writable: true, configurable: true, enumerable: false });
return {
    busDeliver(kind, name, data, id, same) {
        if (kind !== 'bc') return;
        let first = true;
        for (const ch of [...bcAll]) if (!ch._closed && ch.name === name && !(same && ch._id === id)) { ch.dispatchEvent(new MessageEvent('message', { data: first ? data : W.clone(data), origin: location.origin })); first = false; }
    },
    onmsg(data, connection, kind) {
        if (W.shared && kind === 1) {
            const port = incomingPort(connection, 0);
            const event = new MessageEvent('connect', {data: '', ports: [port], source: port});
            event.isTrusted = true; fireOn(G, event); return;
        }
        if (W.shared && kind === 2) {
            for (const port of [...wports.values()]) if (port._client === connection) port.close();
            return;
        }
        let ports = [];
        if (data && typeof data === 'object') {
            if (Object.prototype.hasOwnProperty.call(data, '__lumenPort')) {
                const port = wports.get(workerPortKey(connection, data.__lumenPort));
                if (port) {
                    const incoming = (data.xfer || []).map(key => incomingPort(connection, key));
                    port._deliver(portRes(data.data, incoming), incoming);
                }
                return;
            }
            if (data.__lumenXfer) {
                ports = data.__lumenXfer.map(key => incomingPort(connection, key));
                data = portRes(data.data, ports);
            }
        }
        if (W.shared) return; // SharedWorkerGlobalScope has no global message port.
        const event = new MessageEvent('message', {data, ports}); event.isTrusted = true; fireOn(G, event);
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
        WREG(post); WREG(clone); WREG(timer); WREG(clear); WREG(close); WREG(now); WREG(log); WREG(decode); WREG(textEncoding); WREG(textDecode); WREG(responseTextDecode); WREG(blobState); WREG(fileReaderPackage); WREG(fileReaderCheckpoint); WREG(resolve); WREG(urlParse); WREG(urlSet); WREG(idlGet); WREG(idlPut); WREG(idlRegister); WREG(idlFallback); WREG(initCloneIntrinsic); WREG(send); WREG(sendSync); WREG(abort); WREG(importScript); WREG(reportErr); WREG(busOrigin); WREG(busPost);
        auto setS = [&](const char *k, const std::string &v) { (void)W->Set(ctx, jstr(iso, k), jstr(iso, v.c_str())); };
        setS("url", w->url); setS("ua", w->ua); setS("platform", w->platform); setS("name", w->name);
        setS("origin", w->creator_origin);
        (void)W->Set(ctx, jstr(iso, "shared"), v8::Boolean::New(iso, w->shared));
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
            NetRequest *rq = net_request_ctx("GET", w->url.c_str(), w->top.empty() ? nullptr : w->top.c_str());
            if (w->shared) {
                const std::string credentials = w->module ? w->credentials : "same-origin";
                rq->no_cookies = credentials == "omit";
                if (!rq->no_cookies && credentials != "include") rq->credentials_origin = strdup(w->creator_origin.c_str());
                if (w->has_ocsp) {
                    rq->csp = strdup(w->ocsp.c_str()); rq->csp_doc = strdup(w->odoc.c_str()); rq->csp_dir = "worker-src";
                }
            }
            NetResponse *r = worker_fetch(w.get(), rq);
            bool allowed_response = r && r->status >= 200 && r->status < 300;
            if (allowed_response && w->shared) {
                URL final_url{};
                const bool parsed = url_parse(r->url ? r->url : w->url.c_str(), &final_url);
                char *origin = parsed ? url_origin(&final_url) : nullptr;
                allowed_response = origin && w->creator_origin == origin;
                free(origin); url_free(&final_url);
                // A SharedWorker classic script uses a same-origin script
                // fetch. Do not execute HTML or a cross-origin redirect body.
                const char *header = headers_get(&r->headers, "content-type");
                std::string mime = header ? header : "";
                auto separator = mime.find(';'); if (separator != std::string::npos) mime.resize(separator);
                auto first = mime.find_first_not_of(" \t\r\n");
                auto last = mime.find_last_not_of(" \t\r\n");
                mime = first == std::string::npos ? "" : mime.substr(first, last - first + 1);
                for (char &ch : mime) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
                const bool javascript = mime == "text/javascript" || mime == "application/javascript" ||
                    mime == "text/ecmascript" || mime == "application/ecmascript" || mime == "application/x-ecmascript" ||
                    mime == "application/x-javascript" || mime == "text/x-ecmascript" || mime == "text/x-javascript" ||
                    mime == "text/jscript" || mime == "text/livescript" ||
                    (mime.size() == 18 && mime.compare(0, 17, "text/javascript1.") == 0 && mime[17] >= '0' && mime[17] <= '5');
                allowed_response = allowed_response && javascript;
            }
            if (allowed_response) {
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
            std::deque<InMsg> msgs;
            std::deque<WNetworkResult> network;
            bool bg = iso->HasPendingBackgroundTasks();   /* e.g. async WebAssembly compile: poll until it posts back */
            while (v8::platform::PumpMessageLoop(js_platform(), iso)) {}
            iso->PerformMicrotaskCheckpoint();
            {
                std::unique_lock<std::mutex> lk(w->mu);
                double next = bg ? now_ms() + 4 : 1e300;
                for (auto &kv : E->timers) next = std::min(next, kv.second.due);
                auto ready = [&] { return w->term.load() || !w->in.empty() || !w->network.empty() || (!E->bus_origin.empty() && js_bus_seq() != E->bus_seen); };
                if (!idle_gc) next = std::min(next, last_act + 10001);
                if (!ready()) {
                    if (next >= 1e299) w->cv.wait(lk, ready);
                    else { double ms = next - now_ms(); if (ms > 0) w->cv.wait_for(lk, std::chrono::microseconds((int64_t)(ms * 1000) + 1), ready); }
                }
                msgs.swap(w->in);
                network.swap(w->network);
            }
            if (!msgs.empty() || !network.empty()) { last_act = now_ms(); idle_gc = false; }
            for (InMsg &message : msgs) {
                Buf &b = message.data;
                if (!w->term && !E->closing) {
                    v8::HandleScope hs2(iso);
                    v8::TryCatch tc(iso);
                    v8::Local<v8::Value> value = v8::Undefined(iso);
                    bool decoded = message.kind != WI_MESSAGE || deser(iso, ctx, b).ToLocal(&value);
                    if (decoded && !E->onmsg.IsEmpty()) {
                        v8::Local<v8::Value> args[3] = {value, v8::Integer::NewFromUnsigned(iso, message.connection), v8::Integer::New(iso, message.kind)};
                        (void)E->onmsg.Get(iso)->Call(ctx, v8::Undefined(iso), 3, args); wcheck(E, tc);
                    }
                    iso->PerformMicrotaskCheckpoint();
                }
                free(b.p);
            }
            for (auto &result : network) {
                if (w->term || E->closing) break;
                auto callback = E->network_callbacks.find(result.id);
                if (callback == E->network_callbacks.end()) continue;
                v8::HandleScope handles(iso); v8::TryCatch caught(iso);
                auto fn = callback->second.Get(iso);
                E->network_callbacks.erase(callback);
                v8::Local<v8::Value> args[6]; worker_response_args(iso, result.response.get(), args);
                // ArrayBuffer now owns the copied bytes; release the native
                // body before user callbacks can allocate or fetch again.
                result.response.reset();
                (void)fn->Call(ctx, v8::Undefined(iso), 6, args);
                wcheck(E, caught); iso->PerformMicrotaskCheckpoint();
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
        { std::lock_guard<std::mutex> lock(w->mu); cancel_network_locked(w.get()); }
        E->network_callbacks.clear();
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
    cancel_network_locked(w);
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
bool start_worker(const std::shared_ptr<Worker> &w) {
    WorkerThreads &threads = worker_threads();
    g_workers[w->id] = w;
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8u << 20); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    auto *arg = new std::shared_ptr<Worker>(w);
    { std::lock_guard<std::mutex> lock(threads.mu); ++threads.running; }
    if (pthread_create(&th, &at, worker_thread, arg) != 0) {
        { std::lock_guard<std::mutex> lock(threads.mu); --threads.running; threads.cv.notify_all(); }
        delete arg; g_workers.erase(w->id); pthread_attr_destroy(&at); return false;
    }
    pthread_attr_destroy(&at);
    return true;
}
std::shared_ptr<Worker> worker_arguments(const FCI &a) {
    auto iso = a.GetIsolate();
    auto w = std::make_shared<Worker>();
    w->id = ++g_wseq; w->owner = jctx(iso); w->coi = w->owner && w->owner->doc && w->owner->doc->cross_origin_isolated;
    w->creator_secure = js_windows_is_secure(w->owner);
    if (const char *t = js_top_url(w->owner)) w->top = t;
    if (w->owner) {
        w->creator_origin = w->owner->origin;
        auto *top = w->owner; while (top->parent) top = top->parent;
        w->top_origin = top->origin;
    }
    if (w->owner && w->owner->doc && w->owner->doc->csp) { w->ocsp = w->owner->doc->csp; w->odoc = w->owner->doc->url ? w->owner->doc->url : ""; w->has_ocsp = true; }
    w->url = jcstr(iso, a[0]); w->have_src = a[1]->IsString(); if (w->have_src) w->src = jcstr(iso, a[1]);
    w->ua = jcstr(iso, a[2]); w->platform = jcstr(iso, a[3]); w->name = a[4]->IsString() ? jcstr(iso, a[4]) : ""; w->module = a.Length() > 5 && a[5]->IsTrue();
    return w;
}
void n_workerNew(const FCI &a) {
    WorkerThreads &threads = worker_threads();
    { std::lock_guard<std::mutex> lock(threads.mu); if (threads.stopping) { a.GetReturnValue().Set(0); return; } }
    auto w = worker_arguments(a);
    a.GetReturnValue().Set(start_worker(w) ? w->id : 0);
}
void n_sharedWorkerNew(const FCI &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    WorkerThreads &threads = worker_threads();
    { std::lock_guard<std::mutex> lock(threads.mu); if (threads.stopping) { a.GetReturnValue().Set(0); return; } }
    auto candidate = worker_arguments(a);
    auto *owner = candidate->owner;
    if (!owner || owner->dead || owner->opaque_origin || candidate->creator_origin.empty() || candidate->creator_origin == "null") {
        throw_dom(iso, ctx, "SharedWorker requires a non-opaque document origin.", "SecurityError"); return;
    }
    // The immutable native document principal is authoritative, including
    // about:blank/srcdoc inheritance and sandboxed origins. Public location,
    // URL constructors and JavaScript fields cannot grant a shared connection.
    URL parsed{};
    if (!url_parse_n(candidate->url.data(), candidate->url.size(), &parsed)) {
        url_free(&parsed); throw_dom(iso, ctx, "Invalid SharedWorker script URL.", "SyntaxError"); return;
    }
    char *raw_origin = url_origin(&parsed);
    const bool local_data = parsed.scheme && !strcmp(parsed.scheme, "data") && candidate->have_src;
    const bool same_origin = raw_origin && candidate->creator_origin == raw_origin;
    free(raw_origin); url_free(&parsed);
    if (!same_origin && !local_data) {
        throw_dom(iso, ctx, "SharedWorker script is not same-origin.", "SecurityError"); return;
    }
    // Every joining owner must pass worker-src, even when a permissive page
    // already created a worker with this identity. A denied join is async error.
    if (candidate->has_ocsp && !csp_allows_url(candidate->ocsp.c_str(), candidate->odoc.c_str(), candidate->url.c_str(), "worker-src")) {
        a.GetReturnValue().Set(0); return;
    }
    candidate->shared = true;
    candidate->credentials = a.Length() > 6 && a[6]->IsString() ? jcstr(iso, a[6]) : "same-origin";
    std::shared_ptr<Worker> w;
    for (const auto &entry : g_workers) {
        auto existing = entry.second;
        if (!existing->shared || existing->term || existing->finished ||
            existing->creator_origin != candidate->creator_origin || existing->top_origin != candidate->top_origin ||
            existing->url != candidate->url || existing->name != candidate->name) continue;
        std::lock_guard<std::mutex> lock(existing->mu);
        if (!existing->accepting) continue;
        if (existing->module != candidate->module || existing->credentials != candidate->credentials || existing->coi != candidate->coi) {
            a.GetReturnValue().Set(0); return;
        }
        w = std::move(existing); break;
    }
    bool created = !w;
    if (created) w = candidate;
    const uint32_t connection = candidate->id;
    {
        std::lock_guard<std::mutex> lock(w->mu);
        // A worker can close between identity lookup and connection creation.
        if (w->term || !w->accepting || w->finished) { a.GetReturnValue().Set(0); return; }
        w->clients.emplace(connection, owner);
        w->in.push_back(InMsg{{}, connection, WI_CONNECT});
    }
    g_shared_clients.emplace(connection, SharedClient{w, owner});
    if (created && !start_worker(w)) {
        g_shared_clients.erase(connection); a.GetReturnValue().Set(0); return;
    }
    w->cv.notify_all();
    a.GetReturnValue().Set(connection);
}
void close_shared_connection(uint32_t id, JsCtx *owner) {
    auto found = g_shared_clients.find(id);
    if (found == g_shared_clients.end() || found->second.owner != owner) return;
    auto w = found->second.worker;
    g_shared_clients.erase(found);
    bool empty;
    {
        std::lock_guard<std::mutex> lock(w->mu);
        w->clients.erase(id);
        for (auto it = w->out.begin(); it != w->out.end();)
            if (it->connection == id) { free(it->data.p); it = w->out.erase(it); } else ++it;
        for (auto it = w->in.begin(); it != w->in.end();)
            if (it->connection == id) { free(it->data.p); it = w->in.erase(it); } else ++it;
        empty = w->clients.empty();
        if (!empty && w->accepting) w->in.push_back(InMsg{{}, id, WI_DISCONNECT});
        if (w->owner == owner) w->owner = nullptr; // never retain a destroyed creator pointer
    }
    if (empty) { worker_terminate(w.get()); g_workers.erase(w->id); }
    else w->cv.notify_all();
}
void n_sharedWorkerClose(const FCI &a) {
    close_shared_connection(a[0]->Uint32Value(a.GetIsolate()->GetCurrentContext()).FromMaybe(0), jctx(a.GetIsolate()));
}
void n_workerPost(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    const uint32_t id = a[0]->Uint32Value(ctx).FromMaybe(0);
    auto it = g_workers.find(id);
    auto client = g_shared_clients.find(id);
    auto *owner = jctx(iso);
    /* Serialization invokes getters; one can close a port and erase registry
     * entries. Retain only the Worker, never iterators across JavaScript. */
    uint32_t connection = 0;
    std::shared_ptr<Worker> w;
    if (client != g_shared_clients.end() && client->second.owner == owner) {
        w = client->second.worker; connection = id;
    } else if (it != g_workers.end() && !it->second->shared && it->second->owner == owner) w = it->second;
    Buf b;
    if (!ser(iso, ctx, a[1], b, a[2])) return;
    if (!w) { free(b.p); return; }
    {
        std::lock_guard<std::mutex> lk(w->mu);
        /* Recheck under the queue lock: close/exit may race serialization. */
        if (w->term || !w->accepting || (connection && !w->clients.count(connection))) { free(b.p); return; }
        w->in.push_back(InMsg{std::move(b), connection, WI_MESSAGE});
    }
    w->cv.notify_all();
}
void n_transferClone(const FCI &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    Buf buffer;
    if (!ser(iso, ctx, a[0], buffer, a[1])) return;
    v8::Local<v8::Value> value;
    if (deser(iso, ctx, buffer).ToLocal(&value)) a.GetReturnValue().Set(value);
    free(buffer.p);
}
void n_workerTerm(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    auto it = g_workers.find(a[0]->Uint32Value(ctx).FromMaybe(0));
    if (it == g_workers.end() || it->second->shared || it->second->owner != jctx(iso)) return;
    worker_terminate(it->second.get());
    g_workers.erase(it);
}
} // namespace

extern "C" bool js_workers_shutdown(unsigned timeout_ms) {
    WorkerThreads &threads = worker_threads();
    { std::lock_guard<std::mutex> lock(threads.mu); threads.stopping = true; }
    for (auto &entry : g_workers) worker_terminate(entry.second.get());
    g_workers.clear(); g_shared_clients.clear();
    return wait_worker_threads(timeout_ms);
}

void js_install_workers(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    (void)N->Set(ctx, jstr(iso, "workerNew"), v8::Function::New(ctx, n_workerNew).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "sharedWorkerNew"), v8::Function::New(ctx, n_sharedWorkerNew).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "sharedWorkerClose"), v8::Function::New(ctx, n_sharedWorkerClose).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "workerPost"), v8::Function::New(ctx, n_workerPost).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "workerTerm"), v8::Function::New(ctx, n_workerTerm).ToLocalChecked());
    (void)N->Set(ctx, jstr(iso, "transferClone"), v8::Function::New(ctx, n_transferClone).ToLocalChecked());
}

double workers_deadline(JsCtx *c) {
    for (auto &kv : g_workers) {
        auto &w = kv.second;
        std::lock_guard<std::mutex> lk(w->mu);
        for (const auto &message : w->out) {
            if (!w->shared && w->owner == c) return now_ms();
            auto client = w->clients.find(message.connection);
            if (w->shared && client != w->clients.end() && client->second == c) return now_ms();
        }
    }
    return 1e300;
}

void workers_pump(JsCtx *c) {
    std::vector<std::pair<uint32_t, OutMsg>> batch;
    for (auto &kv : g_workers) {
        auto &w = kv.second;
        std::lock_guard<std::mutex> lk(w->mu);
        for (auto it = w->out.begin(); it != w->out.end();) {
            bool belongs = !w->shared && w->owner == c;
            if (w->shared) {
                auto client = w->clients.find(it->connection);
                belongs = client != w->clients.end() && client->second == c;
            }
            if (!belongs) { ++it; continue; }
            batch.push_back({w->shared ? it->connection : w->id, std::move(*it)});
            it = w->out.erase(it);
        }
    }
    if (batch.empty()) return;
    JS_ENTER(c);
    v8::Local<v8::Value> fv;
    bool have = !c->api.IsEmpty() && c->api.Get(iso)->Get(ctx, jstr(iso, "workerEvent")).ToLocal(&fv) && fv->IsFunction();
    for (auto &item : batch) {
        auto &m = item.second;
        if (have && (g_workers.count(item.first) || g_shared_clients.count(item.first))) {
            v8::HandleScope hs(iso);
            v8::Local<v8::Value> data = v8::Undefined(iso);
            int kind = m.kind;
            if (kind == WM_MESSAGE) { v8::TryCatch tc(iso); if (!deser(iso, ctx, m.data).ToLocal(&data)) { data = v8::Undefined(iso); kind = 3; } }
            v8::Local<v8::Value> argv[7] = { v8::Integer::NewFromUnsigned(iso, item.first), v8::Integer::New(iso, kind), data, jstr(iso, m.msg.c_str()), jstr(iso, m.file.c_str()), v8::Integer::New(iso, m.line), v8::Integer::New(iso, m.col) };
            (void)jcall(c, fv.As<v8::Function>(), v8::Undefined(iso), 7, argv);
        }
        free(m.data.p);
    }
}

void workers_kill(JsCtx *c) {
    std::vector<uint32_t> connections;
    for (const auto &client : g_shared_clients) if (client.second.owner == c) connections.push_back(client.first);
    for (uint32_t connection : connections) close_shared_connection(connection, c);
    for (auto it = g_workers.begin(); it != g_workers.end();)
        if (!it->second->shared && it->second->owner == c) { worker_terminate(it->second.get()); it = g_workers.erase(it); }
        else ++it;
}

extern "C" void worker_mem_stats(size_t *heap, size_t *ext, int *n) {
    *heap = *ext = 0; *n = 0;
    for (auto &kv : g_workers) { *heap += kv.second->heap; *ext += kv.second->ext; ++*n; }
}

