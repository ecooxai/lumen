/* V8 embedding: platform, per-page isolate/context, node wrappers, script execution, timers, events */
#include "js_int.h"
#include <algorithm>
#include <cmath>
#include <unistd.h>

static std::unique_ptr<v8::Platform> g_platform;
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

static void run_source(JsCtx *c, const char *src, size_t n, const char *name) {
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::ScriptOrigin origin(jstr(iso, name ? name : ""));
    v8::Local<v8::Script> s;
    js_enter(c);
    if (v8::Script::Compile(ctx, jstr(iso, src, (int)n), &origin).ToLocal(&s)) (void)s->Run(ctx);
    js_leave(c);
    if (tc.HasCaught() && tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
    settle(c);
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
    fprintf(stderr, "[js warn] Unhandled promise rejection: %.600s\n", s.c_str());
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
    v8::V8::Initialize();
}

static std::vector<JsCtx *> g_ctxs;
static uint32_t g_next_ctx_id = 1;
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
        for (auto &kv : c->fetches) { net_cancel(kv.first); kv.second->c = nullptr; kv.second->cb.Reset(); }
        c->fetches.clear();
        c->timers.clear();
        c->rafs.clear();
        c->anims.clear();
        c->protos.clear();
        c->protoFor.Reset(); c->fire.Reset(); c->report.Reset(); c->mediaChanged.Reset();
        c->api.Reset(); c->node_tmpl.Reset();
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

void js_eval(JsCtx *c, const char *src, const char *name) {
    if (c && src) run_source(c, src, strlen(src), name);
}

bool js_dispatch(JsCtx *c, Node *target, const char *type, const char *kind, bool bubbles, bool cancelable, double x, double y, int button, const char *key) {
    if (!c || c->fire.IsEmpty()) return true;
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
