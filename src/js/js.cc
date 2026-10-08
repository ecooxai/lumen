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

JsCtx *jctx(v8::Isolate *iso) { return static_cast<JsCtx *>(iso->GetData(0)); }

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
    v8::EscapableHandleScope hs(iso);
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
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

static void settle(JsCtx *c) {
    if (c->depth) return;
    c->depth++;
    c->iso->PerformMicrotaskCheckpoint();
    c->depth--;
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
    c->depth++;
    v8::MaybeLocal<v8::Value> r = f->Call(ctx, recv, argc, argv);
    c->depth--;
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
    c->depth++;
    if (v8::Script::Compile(ctx, jstr(iso, src, (int)n), &origin).ToLocal(&s)) (void)s->Run(ctx);
    c->depth--;
    if (tc.HasCaught() && tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
    settle(c);
}

static void on_reject(v8::PromiseRejectMessage m) {
    if (m.GetEvent() != v8::kPromiseRejectWithNoHandler || g_log_level > 2) return;
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    std::string s = jcstr(iso, m.GetValue());
    fprintf(stderr, "[js warn] Unhandled promise rejection: %.300s\n", s.c_str());
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

JsCtx *js_new(Document *d, const JsHost *host) {
    JsCtx *c = new JsCtx();
    c->doc = d;
    if (host) c->host = *host;
    c->t0 = now_ms();
    v8::Isolate::CreateParams cp;
    c->alloc = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    cp.array_buffer_allocator = c->alloc;
    c->iso = v8::Isolate::New(cp);
    c->iso->SetData(0, c);
    c->iso->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    c->iso->SetPromiseRejectCallback(on_reject);
    v8::Isolate *iso = c->iso;
    v8::Isolate::Scope is(iso);
    v8::HandleScope hs(iso);
    v8::Local<v8::Context> ctx = v8::Context::New(iso);
    c->ctx.Reset(iso, ctx);
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
    return c;
}

void js_free(JsCtx *c) {
    if (!c) return;
    std::vector<Node *> roots;
    std::vector<Document *> docs = std::move(c->docs);
    {
        v8::Isolate::Scope is(c->iso);
        v8::HandleScope hs(c->iso);
        for (auto &kv : c->fetches) { net_cancel(kv.first); kv.second->c = nullptr; kv.second->cb.Reset(); }
        c->fetches.clear();
        c->timers.clear();
        c->rafs.clear();
        c->protos.clear();
        c->protoFor.Reset(); c->fire.Reset(); c->report.Reset(); c->mediaChanged.Reset();
        c->api.Reset(); c->node_tmpl.Reset();
        for (Node *n : c->wrapped) { delete static_cast<v8::Global<v8::Object> *>(n->js); n->js = nullptr; }
        for (Node *n : c->wrapped)
            if (!n->parent && n->type != NODE_DOCUMENT && !n->refcount && !n->host) roots.push_back(n);
        c->ctx.Reset();
    }
    c->iso->Dispose();
    delete c->alloc;
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
    if (!c->rafs.empty()) d = std::min(d, c->last_raf + 16);
    return d;
}

void js_tick(JsCtx *c) {
    if (!c) return;
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
    settle(c);
}

bool js_wants_frame(JsCtx *c) { return c && !c->rafs.empty(); }

}
