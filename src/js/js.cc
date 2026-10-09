/* V8 embedding: platform, per-page isolate/context, node wrappers, script execution, timers, events */
#include "js_int.h"
#include <v8-profiler.h>
#include <algorithm>
#include <openssl/evp.h>
#include <cstring>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <unistd.h>

static std::unique_ptr<v8::Platform> g_platform;
v8::Platform *js_platform() { return g_platform.get(); }
int g_log_level = 3;
alignas(8) static char kNodeMagic;

static const char kPrelude[] =
#include "prelude.inc"
    ;

JsCtx *jctx(v8::Isolate *iso) {
    if (iso->InContext()) {
        v8::Local<v8::Context> cx = iso->GetCurrentContext();
        if (void *p = cx->GetAlignedPointerFromEmbedderData(1, kTag)) return static_cast<JsCtx *>(p);
    }
    return static_cast<JsCtx *>(iso->GetData(0));
}

v8::Local<v8::String> jstr(v8::Isolate *iso, const char *s, int n) {
    v8::Local<v8::String> r;
    if (!v8::String::NewFromUtf8(iso, s ? s : "", v8::NewStringType::kNormal, s ? n : 0).ToLocal(&r)) return v8::String::Empty(iso);
    return r;
}

std::string jcstr(v8::Isolate *iso, v8::Local<v8::Value> v) {
    if (v.IsEmpty()) return {};
    v8::String::Utf8Value u(iso, v);
    return *u ? std::string(*u, (size_t)u.length()) : std::string();
}

static v8::Local<v8::Object> proto_for(JsCtx *c, Node *n) {
    v8::Isolate *iso = c->iso;
    if (c->protoFor.IsEmpty()) return {};
    std::string key = std::to_string(n->type) + ":" + (n->type == NODE_ELEMENT && n->tag ? n->tag : "") + ":" + std::to_string(n->ns);
    auto it = c->protos.find(key);
    if (it != c->protos.end()) return it->second.Get(iso);
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::TryCatch tc(iso);
    v8::Local<v8::Value> argv[3] = { v8::Integer::New(iso, n->type), jstr(iso, n->type == NODE_ELEMENT ? n->tag : ""), v8::Integer::New(iso, n->ns) };
    v8::Local<v8::Value> r;
    if (!c->protoFor.Get(iso)->Call(ctx, v8::Undefined(iso), 3, argv).ToLocal(&r) || !r->IsObject()) return {};
    c->protos[key].Reset(iso, r.As<v8::Object>());
    return r.As<v8::Object>();
}

v8::Local<v8::Value> jwrap(JsCtx *c, Node *n) {
    v8::Isolate *iso = c->iso;
    if (!n) return v8::Null(iso);
    if (n->js) return static_cast<v8::Global<v8::Object> *>(n->js)->Get(iso);
    if (n->doc && n->doc != c->doc) if (JsCtx *o = owner_ctx(n->doc)) c = o;
    v8::EscapableHandleScope hs(iso);
    v8::Local<v8::Context> ctx = c->ctx.Get(iso);
    v8::Local<v8::Object> o;
    if (!c->node_tmpl.Get(iso)->NewInstance(ctx).ToLocal(&o)) return hs.Escape(v8::Local<v8::Value>(v8::Null(iso)));
    o->SetAlignedPointerInInternalField(0, &kNodeMagic, kTag);
    o->SetAlignedPointerInInternalField(1, n, kTag);
    v8::Local<v8::Object> proto = proto_for(c, n);
    if (!proto.IsEmpty()) (void)o->SetPrototype(ctx, proto);
    n->js = new v8::Global<v8::Object>(iso, o);
    c->wrapped.push_back(n);
    return hs.Escape(v8::Local<v8::Value>(o));
}

Node *junwrap(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject() || v->IsProxy()) return nullptr;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    if (o->InternalFieldCount() != 2) return nullptr;
    if (o->GetAlignedPointerFromInternalField(0, kTag) != &kNodeMagic) return nullptr;
    return static_cast<Node *>(o->GetAlignedPointerFromInternalField(1, kTag));
}

/* A single script task that runs longer than LUMEN_JS_TIMEOUT_MS is terminated so the UI stays responsive. */
static double g_js_timeout_ms = 10000;

static void js_enter(JsCtx *c) {
    if (!c->depth++) c->busy_since = now_ms();
}

static void js_leave(JsCtx *c) {
    if (--c->depth) return;
    c->busy_since = 0;
    if (c->wd_fired.exchange(false)) c->iso->CancelTerminateExecution();
}

static void on_watchdog(v8::Isolate *iso, void *) {
    fprintf(stderr, "lumen: script exceeded %.0f ms, terminating\n", g_js_timeout_ms);
    v8::HandleScope hs(iso);
    v8::Local<v8::StackTrace> st = v8::StackTrace::CurrentStackTrace(iso, 12);
    for (int i = 0; i < st->GetFrameCount(); i++) {
        v8::Local<v8::StackFrame> f = st->GetFrame(iso, i);
        fprintf(stderr, "    at %s (%s:%d:%d)\n", jcstr(iso, f->GetFunctionName()).c_str(), jcstr(iso, f->GetScriptName()).c_str(), f->GetLineNumber(), f->GetColumn());
    }
    iso->TerminateExecution();
}

static void watchdog_run(JsCtx *c) {
    std::unique_lock<std::mutex> lk(c->wd_mu);
    while (!c->wd_stop) {
        c->wd_cv.wait_for(lk, std::chrono::milliseconds(250));
        double t = c->busy_since;
        if (t > 0 && now_ms() - t > g_js_timeout_ms && !c->wd_fired.exchange(true)) c->iso->RequestInterrupt(on_watchdog, nullptr);
    }
}

static void settle(JsCtx *c) {
    if (c->depth) return;
    js_enter(c);
    c->iso->PerformMicrotaskCheckpoint();
    js_leave(c);
}

void jreport(JsCtx *c, v8::Local<v8::Value> exc, v8::Local<v8::Message> msg) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    if (!c->report.IsEmpty()) {
        v8::TryCatch tc(iso);
        v8::Local<v8::Value> argv[1] = { exc };
        if (!c->report.Get(iso)->Call(ctx, v8::Undefined(iso), 1, argv).IsEmpty()) return;
    }
    std::string s = jcstr(iso, exc);
    v8::Local<v8::Value> st;
    if (exc->IsObject() && exc.As<v8::Object>()->Get(ctx, jstr(iso, "stack")).ToLocal(&st) && st->IsString()) s = jcstr(iso, st);
    if (!msg.IsEmpty()) s += "\n    at " + jcstr(iso, msg->GetScriptResourceName()) + ":" + std::to_string(msg->GetLineNumber(ctx).FromMaybe(0));
    fprintf(stderr, "[js] Uncaught %s\n", s.c_str());
}

v8::MaybeLocal<v8::Value> jcall(JsCtx *c, v8::Local<v8::Function> f, v8::Local<v8::Value> recv, int argc, v8::Local<v8::Value> *argv) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::TryCatch tc(iso);
    js_enter(c);
    v8::MaybeLocal<v8::Value> r = f->Call(ctx, recv, argc, argv);
    js_leave(c);
    if (tc.HasCaught() && tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
    settle(c);
    return r;
}

void jfire(JsCtx *c, Node *n, const char *type) {
    if (c->fire.IsEmpty()) return;
    v8::Isolate *iso = c->iso;
    v8::HandleScope hs(iso);
    v8::Local<v8::Value> argv[2] = { jwrap(c, n), jstr(iso, type) };
    (void)jcall(c, c->fire.Get(iso), v8::Undefined(iso), 2, argv);
}

namespace {
struct SrcBuf { bool one; std::string b; std::u16string w; };
std::mutex g_src_mu;
std::unordered_map<std::string, std::weak_ptr<SrcBuf>> g_src;
struct SrcOne : v8::String::ExternalOneByteStringResource {
    std::shared_ptr<SrcBuf> p; explicit SrcOne(std::shared_ptr<SrcBuf> q) : p(std::move(q)) {}
    const char *data() const override { return p->b.data(); } size_t length() const override { return p->b.size(); }
};
struct SrcTwo : v8::String::ExternalStringResource {
    std::shared_ptr<SrcBuf> p; explicit SrcTwo(std::shared_ptr<SrcBuf> q) : p(std::move(q)) {}
    const uint16_t *data() const override { return (const uint16_t *)p->w.data(); } size_t length() const override { return p->w.size(); }
};
std::shared_ptr<SrcBuf> src_buf(const char *s, size_t n) {
    unsigned char md[32]; unsigned int ml = 0;
    EVP_Digest(s, n, md, &ml, EVP_sha256(), nullptr);  /* collision-resistant: sources are shared across origins */
    std::string key((const char *)md, ml); key += std::to_string(n);
    std::lock_guard<std::mutex> lk(g_src_mu);
    if (auto p = g_src[key].lock()) return p;
    auto p = std::make_shared<SrcBuf>(); p->one = true;
    for (size_t i = 0; i < n; i++) if ((unsigned char)s[i] >= 0x80) { p->one = false; break; }
    if (p->one) p->b.assign(s, n);
    else {
        p->w.reserve(n);
        const unsigned char *u = (const unsigned char *)s, *e = u + n;
        while (u < e) {
            uint32_t c = *u, k = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 9;
            if (k == 9 || u + k >= e + (k ? 0 : 1) && k) { p->w.push_back(0xFFFD); u++; continue; }
            c &= k ? 0x3F >> k : 0x7F;
            bool bad = false;
            for (uint32_t j = 1; j <= k; j++) { if ((u[j] & 0xC0) != 0x80) { bad = true; break; } c = c << 6 | (u[j] & 0x3F); }
            if (bad) { p->w.push_back(0xFFFD); u++; continue; }
            u += k + 1;
            if (c >= 0x10000) { c -= 0x10000; p->w.push_back((char16_t)(0xD800 + (c >> 10))); p->w.push_back((char16_t)(0xDC00 + (c & 0x3FF))); }
            else p->w.push_back((char16_t)c);
        }
    }
    g_src[key] = p;
    for (auto it = g_src.begin(); it != g_src.end();) it = it->second.expired() ? g_src.erase(it) : std::next(it);
    return p;
}
}  // namespace

v8::Local<v8::String> jsrc(v8::Isolate *iso, const char *s, size_t n) {
    if (n < (64u << 10)) return jstr(iso, s, (int)n);
    auto p = src_buf(s, n);
    v8::MaybeLocal<v8::String> r = p->one ? v8::String::NewExternalOneByte(iso, new SrcOne(p)) : v8::String::NewExternalTwoByte(iso, new SrcTwo(p));
    v8::Local<v8::String> out;
    return r.ToLocal(&out) ? out : jstr(iso, s, (int)n);
}

static void run_source(JsCtx *c, const char *src, size_t n, const char *name) {
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::ScriptOrigin origin(jstr(iso, name ? name : ""));
    v8::Local<v8::Script> s;
    js_enter(c);
    if (v8::Script::Compile(ctx, jsrc(iso, src, n), &origin).ToLocal(&s)) (void)s->Run(ctx);
    js_leave(c);
    if (tc.HasCaught() && tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
    settle(c);
}

/* ---- ES modules: fetched synchronously (graph levels in parallel), no import maps ---- */
static JsCtx *ctx_of(v8::Local<v8::Context> ctx) { return static_cast<JsCtx *>(ctx->GetAlignedPointerFromEmbedderData(1, kTag)); }
static void mod_throw(v8::Isolate *iso, const std::string &m) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, m.c_str()))); }
static std::string mod_resolve(const std::string &spec, const std::string &base) {
    bool rel = !spec.compare(0, 1, "/") || !spec.compare(0, 2, "./") || !spec.compare(0, 3, "../");
    bool abs = spec.find("://") != std::string::npos || !spec.compare(0, 5, "data:") || !spec.compare(0, 5, "blob:");
    if (!rel && !abs) return "";
    char *u = url_join(base.c_str(), spec.c_str());
    std::string r = u ? u : "";
    free(u);
    return r;
}
static bool mod_fetch(const std::string &u, const std::string &ref, std::string &out) {
    NetRequest *rq = net_request_new("GET", u.c_str());
    if (!ref.compare(0, 4, "http")) headers_add(&rq->headers, "Referer", ref.c_str());
    NetResponse *r = net_fetch_sync(rq);
    bool ok = r && r->status >= 200 && r->status < 300;
    if (ok) out.assign(r->body ? r->body : "", r->body_len);
    if (r) net_response_free(r);
    return ok;
}
static std::string mod_key(JsCtx *c, v8::Local<v8::Module> m) {
    auto r = c->mod_url.equal_range(m->GetIdentityHash());
    for (auto it = r.first; it != r.second; ++it) {
        auto f = c->mods.find(it->second);
        if (f != c->mods.end() && f->second.Get(c->iso) == m) return it->second;
    }
    return c->doc && c->doc->url ? c->doc->url : "";
}
static v8::MaybeLocal<v8::Module> mod_compile(JsCtx *c, const std::string &key, const std::string &src) {
    v8::Isolate *iso = c->iso;
    v8::ScriptOrigin origin(jstr(iso, key.c_str()), 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
    v8::ScriptCompiler::Source s(jsrc(iso, src.data(), src.size()), origin);
    v8::Local<v8::Module> m;
    if (!v8::ScriptCompiler::CompileModule(iso, &s).ToLocal(&m)) return {};
    c->mods[key].Reset(iso, m);
    c->mod_url.emplace(m->GetIdentityHash(), key);
    return m;
}
static bool mod_graph(JsCtx *c, v8::Local<v8::Module> root) {
    v8::Isolate *iso = c->iso;
    std::string ref = c->doc && c->doc->url ? c->doc->url : "";
    std::vector<v8::Local<v8::Module>> todo{ root };
    while (!todo.empty()) {
        std::vector<std::string> need;
        for (v8::Local<v8::Module> m : todo) {
            std::string base = mod_key(c, m);
            v8::Local<v8::FixedArray> rq = m->GetModuleRequests();
            for (int i = 0; i < rq->Length(); i++) {
                std::string sp = jcstr(iso, rq->Get(i).As<v8::ModuleRequest>()->GetSpecifier());
                std::string u = mod_resolve(sp, base);
                if (u.empty()) return mod_throw(iso, "Failed to resolve module specifier \"" + sp + "\""), false;
                if (!c->mods.count(u) && std::find(need.begin(), need.end(), u) == need.end()) need.push_back(u);
            }
        }
        todo.clear();
        std::vector<std::string> body(need.size());
        std::vector<char> ok(need.size(), 0);
        double busy = c->busy_since.exchange(0);   /* network time is not script time */
        for (size_t b = 0; b < need.size(); b += 16) {
            std::vector<std::thread> th;
            for (size_t i = b; i < need.size() && i < b + 16; i++) th.emplace_back([&, i] { ok[i] = mod_fetch(need[i], ref, body[i]); });
            for (std::thread &t : th) t.join();
        }
        if (busy > 0) c->busy_since = now_ms();
        for (size_t i = 0; i < need.size(); i++) {
            if (!ok[i]) return mod_throw(iso, "Failed to fetch module: " + need[i]), false;
            v8::Local<v8::Module> m;
            if (!mod_compile(c, need[i], body[i]).ToLocal(&m)) return false;
            todo.push_back(m);
        }
    }
    return true;
}
static v8::MaybeLocal<v8::Module> mod_resolve_cb(v8::Local<v8::Context> ctx, v8::Local<v8::String> spec, v8::Local<v8::FixedArray>, v8::Local<v8::Module> ref) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    JsCtx *c = ctx_of(ctx);
    auto it = c->mods.find(mod_resolve(jcstr(iso, spec), mod_key(c, ref)));
    if (it == c->mods.end()) return mod_throw(iso, "Failed to resolve module specifier \"" + jcstr(iso, spec) + "\""), v8::MaybeLocal<v8::Module>();
    return it->second.Get(iso);
}
static v8::MaybeLocal<v8::Promise> mod_eval(JsCtx *c, v8::Local<v8::Module> m) {
    v8::Local<v8::Context> ctx = c->iso->GetCurrentContext();
    if (!mod_graph(c, m)) return {};
    if (m->GetStatus() == v8::Module::kUninstantiated && !m->InstantiateModule(ctx, mod_resolve_cb).FromMaybe(false)) return {};
    return m->Evaluate(ctx);
}
static v8::MaybeLocal<v8::Promise> mod_dynamic(v8::Local<v8::Context> ctx, v8::Local<v8::Data>, v8::Local<v8::Value> res, v8::Local<v8::String> spec, v8::Local<v8::FixedArray>) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    JsCtx *c = ctx_of(ctx);
    v8::Local<v8::Promise::Resolver> R;
    if (!v8::Promise::Resolver::New(ctx).ToLocal(&R)) return {};
    std::string base = res->IsString() ? jcstr(iso, res) : "";
    if (base.find(':') == std::string::npos || !base.compare(0, 6, "lumen:")) base = c->doc && c->doc->url ? c->doc->url : "";
    std::string u = mod_resolve(jcstr(iso, spec), base);
    v8::TryCatch tc(iso);
    v8::Local<v8::Module> m;
    std::string body;
    auto it = c->mods.find(u);
    if (u.empty()) mod_throw(iso, "Failed to resolve module specifier \"" + jcstr(iso, spec) + "\"");
    else if (it != c->mods.end()) m = it->second.Get(iso);
    else {
        double busy = c->busy_since.exchange(0);
        bool ok = mod_fetch(u, c->doc && c->doc->url ? c->doc->url : "", body);
        if (busy > 0) c->busy_since = now_ms();
        if (ok) (void)mod_compile(c, u, body).ToLocal(&m);
        else mod_throw(iso, "Failed to fetch dynamically imported module: " + u);
    }
    v8::Local<v8::Promise> p;
    if (!m.IsEmpty() && mod_eval(c, m).ToLocal(&p)) {
        v8::Local<v8::Array> d = v8::Array::New(iso, 3);
        (void)d->Set(ctx, 0, R);
        (void)d->Set(ctx, 1, m->GetModuleNamespace());
        (void)d->Set(ctx, 2, jstr(iso, u.c_str()));
        auto done = [](const v8::FunctionCallbackInfo<v8::Value> &a) {
            v8::Local<v8::Context> cx = a.GetIsolate()->GetCurrentContext();
            v8::Local<v8::Array> d = a.Data().As<v8::Array>();
            v8::Local<v8::Promise::Resolver> r = d->Get(cx, 0).ToLocalChecked().As<v8::Promise::Resolver>();
            (void)r->Resolve(cx, d->Get(cx, 1).ToLocalChecked());
        };
        auto fail = [](const v8::FunctionCallbackInfo<v8::Value> &a) {
            v8::Local<v8::Context> cx = a.GetIsolate()->GetCurrentContext();
            if (getenv("LUMEN_DEBUG_MODULES")) {
                v8::Local<v8::Value> e = a[0];
                if (e->IsObject()) { v8::Local<v8::Value> st; if (e.As<v8::Object>()->Get(cx, jstr(a.GetIsolate(), "stack")).ToLocal(&st)) e = st; }
                fprintf(stderr, "[module] dynamic import rejected %s: %.600s\n", jcstr(a.GetIsolate(), a.Data().As<v8::Array>()->Get(cx, 2).ToLocalChecked()).c_str(), jcstr(a.GetIsolate(), e).c_str());
            }
            (void)a.Data().As<v8::Array>()->Get(cx, 0).ToLocalChecked().As<v8::Promise::Resolver>()->Reject(cx, a[0]);
        };
        v8::Local<v8::Function> f1, f2;
        if (v8::Function::New(ctx, done, d).ToLocal(&f1) && v8::Function::New(ctx, fail, d).ToLocal(&f2)) (void)p->Then(ctx, f1, f2);
    } else if (tc.HasCaught()) {
        if (getenv("LUMEN_DEBUG_MODULES")) fprintf(stderr, "[module] dynamic import failed %s: %.600s\n", u.c_str(), jcstr(iso, tc.Exception()).c_str());
        (void)R->Reject(ctx, tc.Exception());
        tc.Reset();
    }
    return R->GetPromise();
}
static void mod_meta(v8::Local<v8::Context> ctx, v8::Local<v8::Module> m, v8::Local<v8::Object> meta) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    std::string u = mod_key(ctx_of(ctx), m);
    size_t h = u.find("#lumen-inline-");
    if (h != std::string::npos) u.resize(h);
    (void)meta->CreateDataProperty(ctx, jstr(iso, "url"), jstr(iso, u.c_str()));
    auto resolve = [](const v8::FunctionCallbackInfo<v8::Value> &a) {
        v8::Isolate *iso = a.GetIsolate();
        std::string r = mod_resolve(jcstr(iso, a[0]), jcstr(iso, a.Data()));
        if (r.empty()) return mod_throw(iso, "Failed to resolve module specifier \"" + jcstr(iso, a[0]) + "\"");
        a.GetReturnValue().Set(jstr(iso, r.c_str()));
    };
    v8::Local<v8::Function> f;
    if (v8::Function::New(ctx, resolve, jstr(iso, u.c_str())).ToLocal(&f)) (void)meta->CreateDataProperty(ctx, jstr(iso, "resolve"), f);
}
static void mod_report(const v8::FunctionCallbackInfo<v8::Value> &a) {
    v8::Isolate *iso = a.GetIsolate();
    if (JsCtx *c = jctx(iso)) jreport(c, a[0], v8::Exception::CreateMessage(iso, a[0]));
}

static void on_reject(v8::PromiseRejectMessage m) {
    if (m.GetEvent() != v8::kPromiseRejectWithNoHandler || g_log_level > 2) return;
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    v8::Local<v8::Value> v = m.GetValue();
    std::string s = jcstr(iso, v);
    if (v->IsObject()) {
        v8::Local<v8::Value> st;
        if (v.As<v8::Object>()->Get(iso->GetCurrentContext(), jstr(iso, "stack")).ToLocal(&st) && st->IsString()) s = jcstr(iso, st);
    }
    if (!v->IsObject() || s.find('\n') == std::string::npos) {
        v8::Local<v8::StackTrace> tr = v8::StackTrace::CurrentStackTrace(iso, 4);
        for (int i = 0; i < tr->GetFrameCount(); i++) {
            v8::Local<v8::StackFrame> f = tr->GetFrame(iso, i);
            v8::Local<v8::String> fn = f->GetFunctionName(), sn = f->GetScriptName();
            s += "\n    at " + (fn.IsEmpty() ? std::string("?") : jcstr(iso, fn)) + " (" + (sn.IsEmpty() ? std::string("?") : jcstr(iso, sn)) + ":" + std::to_string(f->GetLineNumber()) + ":" + std::to_string(f->GetColumn()) + ")";
        }
    }
    fprintf(stderr, "[js warn] Unhandled promise rejection: %.900s\n", s.c_str());
}

extern "C" {

void js_global_init(const char *argv0) {
    if (g_platform) return;
    const char *lv = getenv("LUMEN_CONSOLE");
    if (lv) g_log_level = atoi(lv);
    const char *icu = getenv("LUMEN_ICU_DATA");
    if (!icu && access("/opt/homebrew/opt/v8/libexec/icudtl.dat", R_OK) == 0) icu = "/opt/homebrew/opt/v8/libexec/icudtl.dat";
    v8::V8::InitializeICUDefaultLocation(argv0, icu);
    v8::V8::InitializeExternalStartupData(argv0);
    g_platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(g_platform.get());
    if (const char *vf = getenv("LUMEN_V8_FLAGS")) v8::V8::SetFlagsFromString(vf);
    v8::V8::Initialize();
}

static std::vector<JsCtx *> g_ctxs;
static uint32_t g_next_ctx_id = 1;
void js_mem_stats(size_t *heap, size_t *external) {
    size_t h = 0, e = 0; std::vector<v8::Isolate *> seen;
    for (JsCtx *c : g_ctxs) {
        if (!c->iso || std::find(seen.begin(), seen.end(), c->iso) != seen.end()) continue;
        seen.push_back(c->iso);
        if (getenv("LUMEN_MEM_GC")) { v8::Isolate::Scope is(c->iso); c->iso->LowMemoryNotification(); }
        v8::HeapStatistics hs; c->iso->GetHeapStatistics(&hs); h += hs.used_heap_size(); e += hs.external_memory();
        if (getenv("LUMEN_MEM_DETAIL")) {
            std::string sp;
            for (size_t i = 0; i < c->iso->NumberOfHeapSpaces(); i++) {
                v8::HeapSpaceStatistics ss; c->iso->GetHeapSpaceStatistics(&ss, i);
                char b[96]; snprintf(b, sizeof b, " %s=%.1f/%.1f", ss.space_name(), ss.space_used_size() / 1048576.0, ss.physical_space_size() / 1048576.0); sp += b;
            }
            static bool snapped;
            if (seen.size() == 1 && getenv("LUMEN_HEAP_SNAP") && !snapped && now_ms() - c->t0 > 50000) {
                snapped = true;
                struct Out : v8::OutputStream { FILE *f; void EndOfStream() override {} WriteResult WriteAsciiChunk(char *d, int n) override { fwrite(d, 1, n, f); return kContinue; } } o;
                o.f = fopen(getenv("LUMEN_HEAP_SNAP"), "w");
                if (o.f) { v8::Isolate::Scope is(c->iso); v8::HandleScope hs(c->iso); const v8::HeapSnapshot *hsn = c->iso->GetHeapProfiler()->TakeHeapSnapshot(); hsn->Serialize(&o); fclose(o.f); const_cast<v8::HeapSnapshot *>(hsn)->Delete(); fprintf(stderr, "lumen-mem: heap snapshot written\n"); }
            }
            if (seen.size() == 1) {
                std::vector<std::pair<size_t, std::string>> ty;
                for (size_t i = 0; i < c->iso->NumberOfTrackedHeapObjectTypes(); i++) {
                    v8::HeapObjectStatistics os; if (!c->iso->GetHeapObjectStatisticsAtLastGC(&os, i) || !os.object_size()) continue;
                    ty.push_back({os.object_size(), std::string(os.object_type()) + "/" + os.object_sub_type() + " n=" + std::to_string(os.object_count())});
                }
                std::sort(ty.rbegin(), ty.rend());
                for (size_t i = 0; i < ty.size() && i < 25; i++) fprintf(stderr, "lumen-mem-type: %.2fMB %s\n", ty[i].first / 1048576.0, ty[i].second.c_str());
            }
            v8::HeapCodeStatistics cs; c->iso->GetHeapCodeAndMetadataStatistics(&cs);
            fprintf(stderr, "lumen-mem-iso: %s used=%.1fMB total=%.1fMB phys=%.1fMB ext=%.1fMB code=%.1fMB bc=%.1fMB%s\n", ctx_origin(c).c_str(), hs.used_heap_size() / 1048576.0, hs.total_heap_size() / 1048576.0, hs.total_physical_size() / 1048576.0, hs.external_memory() / 1048576.0, cs.code_and_metadata_size() / 1048576.0, cs.bytecode_and_metadata_size() / 1048576.0, sp.c_str());
        }
    }
    *heap = h; *external = e;
}
JsCtx *owner_ctx(Document *d) {
    for (JsCtx *c : g_ctxs) if (c->doc == d) return c;
    for (JsCtx *c : g_ctxs) if (std::find(c->docs.begin(), c->docs.end(), d) != c->docs.end()) return c;
    return nullptr;
}
JsCtx *js_new(Document *d, const JsHost *host) { return js_new_ex(d, host, nullptr, nullptr); }

JsCtx *js_new_ex(Document *d, const JsHost *host, JsCtx *parent, Node *frame) {
    JsCtx *c = new JsCtx();
    c->id = g_next_ctx_id++;
    c->parent = parent; c->frame_el = frame;
    if (frame) frame->refcount++;
    if (parent) parent->kids.push_back(c);
    c->doc = d;
    if (host) c->host = *host;
    c->t0 = now_ms();
    c->thread = std::this_thread::get_id();
    g_ctxs.push_back(c);
    jsg_install_hooks();
    if (parent) c->iso = parent->iso;
    else {
    v8::Isolate::CreateParams cp;
    c->alloc = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    cp.array_buffer_allocator = c->alloc;
    c->iso = v8::Isolate::New(cp);
    c->iso->SetData(0, c);
    c->iso->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    c->iso->SetPromiseRejectCallback(on_reject);
    c->iso->SetHostImportModuleDynamicallyCallback(mod_dynamic);
    c->iso->SetHostInitializeImportMetaObjectCallback(mod_meta);
    }
    v8::Isolate *iso = c->iso;
    v8::Isolate::Scope is(iso);
    v8::HandleScope hs(iso);
    v8::Local<v8::Context> ctx = v8::Context::New(iso);
    c->ctx.Reset(iso, ctx);
    ctx->SetAlignedPointerInEmbedderData(1, c, kTag);
    ctx->SetSecurityToken(v8::String::NewFromUtf8(iso, ctx_origin(c).c_str(), v8::NewStringType::kInternalized).ToLocalChecked());
    v8::Context::Scope cs(ctx);
    v8::Local<v8::ObjectTemplate> tmpl = v8::ObjectTemplate::New(iso);
    tmpl->SetInternalFieldCount(2);
    c->node_tmpl.Reset(iso, tmpl);
    v8::Local<v8::Object> N = v8::Object::New(iso);
    js_install_native(c, N);
    v8::TryCatch tc(iso);
    v8::ScriptOrigin origin(jstr(iso, "lumen:prelude"));
    v8::Local<v8::Script> s;
    v8::Local<v8::Value> fv, api;
    v8::Local<v8::Value> argv[2] = { N, ctx->Global() };
    if (v8::Script::Compile(ctx, jstr(iso, kPrelude, (int)sizeof kPrelude - 1), &origin).ToLocal(&s) && s->Run(ctx).ToLocal(&fv) && fv->IsFunction() &&
        fv.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 2, argv).ToLocal(&api) && api->IsObject()) {
        v8::Local<v8::Object> ao = api.As<v8::Object>();
        c->api.Reset(iso, ao);
        auto get = [&](const char *k, v8::Global<v8::Function> &dst) {
            v8::Local<v8::Value> v;
            if (ao->Get(ctx, jstr(iso, k)).ToLocal(&v) && v->IsFunction()) dst.Reset(iso, v.As<v8::Function>());
        };
        get("protoFor", c->protoFor);
        get("fire", c->fire);
        get("report", c->report);
        get("mediaChanged", c->mediaChanged);
    }
    if (tc.HasCaught()) {
        std::string m = jcstr(iso, tc.Exception());
        v8::Local<v8::Value> st;
        if (tc.StackTrace(ctx).ToLocal(&st)) m = jcstr(iso, st);
        int line = tc.Message().IsEmpty() ? 0 : tc.Message()->GetLineNumber(ctx).FromMaybe(0);
        fprintf(stderr, "lumen: JS prelude failed (line %d): %s\n", line, m.c_str());
    }
    iso->PerformMicrotaskCheckpoint();
    if (const char *to = getenv("LUMEN_JS_TIMEOUT_MS")) g_js_timeout_ms = atof(to);
    if (g_js_timeout_ms > 0 && !parent) c->watchdog = std::thread(watchdog_run, c);
    return c;
}

void js_free(JsCtx *c) {
    if (!c) return;
    workers_kill(c);
    for (JsCtx *k : std::vector<JsCtx *>(c->kids)) js_free(k);
    frames_forget(c);
    if (c->parent) {
        frame_kill(c);
        auto &pk = c->parent->kids; pk.erase(std::remove(pk.begin(), pk.end(), c), pk.end());
        if (c->frame_el && c->frame_el->refcount) c->frame_el->refcount--;
    }
    if (c->watchdog.joinable()) {
        { std::lock_guard<std::mutex> lk(c->wd_mu); c->wd_stop = true; }
        c->wd_cv.notify_all();
        c->watchdog.join();
    }
    for (auto &kv : c->players) mp_free(kv.second);
    g_ctxs.erase(std::remove(g_ctxs.begin(), g_ctxs.end(), c), g_ctxs.end());
    c->players.clear();
    std::vector<Node *> roots;
    std::vector<Document *> docs = std::move(c->docs);
    if (c->parent) docs.push_back(c->doc);
    {
        v8::Isolate::Scope is(c->iso);
        v8::HandleScope hs(c->iso);
        for (auto &kv : c->fetches) { net_cancel(kv.first); kv.second->c = nullptr; kv.second->cb.Reset(); kv.second->head.Reset(); kv.second->chunk.Reset(); }
        c->fetches.clear(); for (auto &kv : c->sockets) { net_ws_release(kv.second->ws); kv.second->cb.Reset(); delete kv.second; } c->sockets.clear();
        c->timers.clear();
        c->rafs.clear();
        c->anims.clear();
        c->protos.clear();
        c->protoFor.Reset(); c->fire.Reset(); c->report.Reset(); c->mediaChanged.Reset();
        c->api.Reset(); c->node_tmpl.Reset();
        c->mods.clear(); c->mod_url.clear();
        for (Node *n : c->wrapped) { delete static_cast<v8::Global<v8::Object> *>(n->js); n->js = nullptr; }
        for (Node *n : c->wrapped)
            if (!n->parent && n->type != NODE_DOCUMENT && !n->refcount && !n->host) roots.push_back(n);
        c->ctx.Reset();
    }
    if (!c->parent) { c->iso->Dispose(); delete c->alloc; }
    for (Node *n : roots) node_free_tree(n);
    for (Document *d : docs) doc_free(d);
    delete c;
}

void js_run_script(JsCtx *c, Node *script, const char *src, size_t n, const char *name) {
    if (!c) return;
    if (script) script->flags |= NF_SCRIPT_STARTED;
    Node *prev = c->current_script;
    c->current_script = script;
    run_source(c, src, n, name);
    c->current_script = prev;
    if (script && node_attr(script, "src")) {
        JS_ENTER(c);
        jfire(c, script, "load");
    }
}

void js_run_module(JsCtx *c, Node *script, const char *src, size_t n, const char *url) {
    if (!c) return;
    if (script) script->flags |= NF_SCRIPT_STARTED;
    bool ext = script && node_attr(script, "src"), ok = false;
    {
        JS_ENTER(c);
        v8::TryCatch tc(iso);
        std::string key = url ? url : "", body;
        if (!ext) key += "#lumen-inline-" + std::to_string(++c->mod_seq);
        v8::Local<v8::Module> m;
        js_enter(c);
        auto it = c->mods.find(key);
        if (it != c->mods.end()) { m = it->second.Get(iso); ok = true; }
        else {
            if (src) body.assign(src, n);
            ok = src || mod_fetch(key, c->doc && c->doc->url ? c->doc->url : "", body);
            if (ok) (void)mod_compile(c, key, body).ToLocal(&m);
        }
        v8::Local<v8::Promise> p;
        v8::Local<v8::Function> f;
        if (!m.IsEmpty() && mod_eval(c, m).ToLocal(&p) && v8::Function::New(ctx, mod_report).ToLocal(&f)) (void)p->Catch(ctx, f);
        js_leave(c);
        if (tc.HasCaught() && tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
        settle(c);
    }
    if (ext) { JS_ENTER(c); jfire(c, script, ok ? "load" : "error"); }
}

void js_eval(JsCtx *c, const char *src, const char *name) {
    if (c && src) run_source(c, src, strlen(src), name);
}

/* Native callers keep using a dispatch target after its handlers ran; handlers may detach it and a GC may
   then free it. Targets stay pinned until the host's event batch ends (js_release_pins). */
static std::vector<Node *> g_pins;
void js_release_pins(void) {
    std::vector<Node *> v; v.swap(g_pins);
    for (Node *n : v) node_release(n);
}
bool js_dispatch(JsCtx *c, Node *target, const char *type, const char *kind, bool bubbles, bool cancelable, double x, double y, int button, const char *key) {
    if (!c || c->fire.IsEmpty()) return true;
    if (target && target->parent && target->parent->type != NODE_DOCUMENT) { node_retain(target); g_pins.push_back(target); }
    JS_ENTER(c);
    v8::Local<v8::Object> init = v8::Object::New(iso);
    auto set = [&](const char *k, v8::Local<v8::Value> v) { (void)init->Set(ctx, jstr(iso, k), v); };
    set("bubbles", v8::Boolean::New(iso, bubbles));
    set("cancelable", v8::Boolean::New(iso, cancelable));
    set("composed", v8::True(iso));
    set("view", ctx->Global());
    set("clientX", v8::Number::New(iso, x));
    set("clientY", v8::Number::New(iso, y));
    set("screenX", v8::Number::New(iso, x));
    set("screenY", v8::Number::New(iso, y));
    set("button", v8::Integer::New(iso, button));
    set("buttons", v8::Integer::New(iso, button == 0 ? 1 : button == 2 ? 2 : 4));
    set("detail", v8::Integer::New(iso, 1));
    if (key) { set("key", jstr(iso, key)); set("code", jstr(iso, key)); }
    v8::Local<v8::Value> ctor = v8::Undefined(iso);
    if (kind && !c->api.IsEmpty()) (void)c->api.Get(iso)->Get(ctx, jstr(iso, kind)).ToLocal(&ctor);
    v8::Local<v8::Value> argv[4] = { target ? jwrap(c, target) : v8::Local<v8::Value>(ctx->Global()), jstr(iso, type), init, ctor };
    v8::Local<v8::Value> r;
    if (!jcall(c, c->fire.Get(iso), v8::Undefined(iso), 4, argv).ToLocal(&r)) return true;
    return r->BooleanValue(iso);
}

bool js_dispatch_window(JsCtx *c, const char *type) {
    return js_dispatch(c, nullptr, type, "Event", false, false, 0, 0, 0, nullptr);
}

void js_set_ready_state(JsCtx *c, int state) {
    if (!c) return;
    c->doc->ready_state = state;
    JS_ENTER(c);
    jfire(c, &c->doc->node, "readystatechange");
}

double js_next_deadline(JsCtx *c) {
    if (!c) return INFINITY;
    double d = INFINITY;
    for (auto &kv : c->timers) d = std::min(d, kv.second.due);
    for (auto &e : c->anims) d = std::min(d, e.due);
    if (!c->rafs.empty()) d = std::min(d, c->last_raf + 16);
    for (JsCtx *k : c->kids) if (!k->dead) d = std::min(d, js_next_deadline(k));
    d = std::min(d, workers_deadline(c));
    return d;
}

static void tick_one(JsCtx *c);
void js_tick(JsCtx *c) {
    if (!c || c->dead) return;
    tick_one(c);
    frames_scan(c);
    for (JsCtx *k : std::vector<JsCtx *>(c->kids)) if (!k->dead) js_tick(k);
}
static void tick_one(JsCtx *c) {
    JS_ENTER(c);
    while (v8::platform::PumpMessageLoop(g_platform.get(), iso)) {}
    double now = now_ms();
    std::vector<std::pair<double, uint32_t>> due;
    for (auto &kv : c->timers) if (kv.second.due <= now) due.push_back({ kv.second.due, kv.first });
    std::sort(due.begin(), due.end());
    for (auto &d : due) {
        auto it = c->timers.find(d.second);
        if (it == c->timers.end()) continue;
        v8::Local<v8::Function> fn = it->second.fn.Get(iso);
        if (it->second.repeat) it->second.due = now_ms() + it->second.interval;
        else c->timers.erase(it);
        (void)jcall(c, fn, ctx->Global(), 0, nullptr);
    }
    if (!c->rafs.empty() && now - c->last_raf >= 15.5) {
        c->last_raf = now;
        std::map<uint32_t, v8::Global<v8::Function>> list;
        list.swap(c->rafs);
        v8::Local<v8::Value> ts = v8::Number::New(iso, now - c->t0);
        for (auto &kv : list) (void)jcall(c, kv.second.Get(iso), ctx->Global(), 1, &ts);
    }
    if (!c->anims.empty()) {
        std::vector<AnimEv> due_ev;
        for (size_t i = 0; i < c->anims.size();)
            if (c->anims[i].due <= now) { due_ev.push_back(std::move(c->anims[i])); c->anims.erase(c->anims.begin() + (long)i); }
            else i++;
        std::stable_sort(due_ev.begin(), due_ev.end(), [](const AnimEv &x, const AnimEv &y) { return x.due < y.due; });
        v8::Local<v8::Value> fa;
        if (!due_ev.empty() && !c->api.IsEmpty() && c->api.Get(iso)->Get(ctx, jstr(iso, "fireAnim")).ToLocal(&fa) && fa->IsFunction())
            for (auto &e : due_ev) {
                v8::Local<v8::Value> argv[5] = { e.target.Get(iso), jstr(iso, e.type.c_str()), jstr(iso, e.name.c_str()), v8::Number::New(iso, e.elapsed), v8::Boolean::New(iso, e.anim) };
                (void)jcall(c, fa.As<v8::Function>(), v8::Undefined(iso), 5, argv);
            }
    }
    workers_pump(c);
    settle(c);
}

bool js_wants_frame(JsCtx *c) {
    if (!c || c->dead) return false;
    if (!c->rafs.empty()) return true;
    for (JsCtx *k : c->kids) if (js_wants_frame(k)) return true;
    return false;
}

static JsCtx *ctx_for(Node *n) {
    for (JsCtx *c : g_ctxs)
        if (c->thread == std::this_thread::get_id() && (n->doc == c->doc || std::find(c->docs.begin(), c->docs.end(), n->doc) != c->docs.end())) return c;
    return nullptr;
}

void js_anim_event(Node *n, const char *type, const char *name, double delay, double elapsed, bool anim) {
    JsCtx *c = ctx_for(n);
    if (!c || c->ctx.IsEmpty()) return;
    JS_ENTER(c);
    AnimEv e;
    e.due = now_ms() + delay; e.n = n; e.target.Reset(iso, jwrap(c, n));
    e.type = type; e.name = name; e.elapsed = elapsed; e.anim = anim;
    c->anims.push_back(std::move(e));
}

bool js_anim_cancel(Node *n, const char *name) {
    JsCtx *c = ctx_for(n);
    if (!c) return false;
    bool had = false;
    for (size_t i = 0; i < c->anims.size();)
        if (c->anims[i].n == n && c->anims[i].anim && c->anims[i].name == name) { c->anims.erase(c->anims.begin() + (long)i); had = true; }
        else i++;
    if (had) js_anim_event(n, "animationcancel", name, 0, 0, true);
    return had;
}

}

void js_set_background(JsCtx *c, bool bg) {
    if (!c || !c->iso) return;
    double t = now_ms();
    if (bg != c->bg) {
        c->bg = bg; c->bg_since = t; c->bg_gc = false;
        if (bg) c->iso->IsolateInBackgroundNotification(); else c->iso->IsolateInForegroundNotification();
    }
    if (bg && !c->bg_gc && t - c->bg_since > 15000) { c->bg_gc = true; v8::Isolate::Scope is(c->iso); c->iso->LowMemoryNotification(); }
}

bool js_busy(JsCtx *c) { return c && !c->fetches.empty(); }
