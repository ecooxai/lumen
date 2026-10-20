/* WebGPU natives (the `N.gpu` object) over wgpu-native, on an instance of its own. The prelude's GPU* classes
   (07h-webgpu.js) keep the wrappers, defaults and promises; these build the wgpu descriptors. wgpu-native
   panics, aborting the browser, on much that a page may do (null or invalid handles, zero sizes, unknown bits,
   submitting invalid or mapped work, reading a range that is not mapped), so everything it would panic on is
   checked here first and becomes a WebGPU validation error and an invalid object instead. Errors go to the
   page's error scopes or uncapturederror events, never to the log. Objects are small ids into a table; a
   wrapper carries its id under a symbol private to the prelude, so descriptors can be read as the page gave them. */
#include "js_int.h"
extern "C" {
#include "../gpu/webgpu.h"
#include "../gpu/webgl.h"
extern int gpu_log_quiet;
}
#include <webgpu.h>
#include <wgpu.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

using FCI = v8::FunctionCallbackInfo<v8::Value>;

namespace {

enum Kind { K_NONE, K_ADAPTER, K_DEVICE, K_BUFFER, K_TEXTURE, K_VIEW, K_SAMPLER, K_BGL, K_BG, K_PL, K_SHADER, K_RPIPE, K_CPIPE, K_ENC, K_CB, K_RPASS, K_CPASS, K_QS };

struct Scope { int filter; bool has = false; int type = 0; std::string msg; };
struct MapRange { uint64_t off, size; v8::Global<v8::ArrayBuffer> ab; };

struct H {
    Kind kind = K_NONE;
    JsCtx *owner = nullptr;
    void *p = nullptr;                /* nullptr: an invalid object, refused before wgpu saw it */
    uint32_t dev = 0;                 /* the device of everything but adapters and devices */
    bool bad = false;                 /* wgpu reported an error creating it */
    bool destroyed = false, ended = false, consumed = false, canvas = false;
    /* buffers */
    int map_state = 0, map_mode = 0;  /* 0 unmapped, 1 pending, 2 mapped */
    uint32_t map_ticket = 0;
    uint64_t size = 0, usage = 0, map_off = 0, map_size = 0;
    std::vector<MapRange> ranges;
    /* textures */
    int fmt = 0;
    int64_t ext = 0;                  /* external memory reported to V8 */
    std::vector<uint32_t> uses;       /* buffers, textures, views, bind groups and query sets it reaches */
    uint32_t parent = 0;              /* pass: its encoder; encoder: its open pass */
    std::string err;                  /* shader: compilation message; encoder: first error found here */
    /* devices */
    std::vector<Scope> scopes;
    WGPUQueue q = nullptr;
    bool lost = false, want_release = false;
    int children = 0;
};

constexpr uint32_t SLOT_BITS = 20, SLOT_MASK = (1u << SLOT_BITS) - 1, GEN_MASK = 0xfff;
std::vector<H *> g_h(1, nullptr);
std::vector<uint32_t> g_gen(1, 0), g_free_slots;

H *hraw(uint32_t id) {
    uint32_t s = id & SLOT_MASK;
    if (!s || s >= g_h.size() || !g_h[s] || (g_gen[s] & GEN_MASK) != id >> SLOT_BITS) return nullptr;
    return g_h[s];
}
H *hget(JsCtx *c, uint32_t id, Kind k) { H *h = hraw(id); return h && h->owner == c && h->kind == k ? h : nullptr; }

uint32_t hnew(Kind k, JsCtx *c, void *p, uint32_t dev) {
    uint32_t slot;
    if (!g_free_slots.empty()) { slot = g_free_slots.back(); g_free_slots.pop_back(); }
    else {
        if (g_h.size() > SLOT_MASK) return 0;
        slot = (uint32_t)g_h.size(); g_h.push_back(nullptr); g_gen.push_back(0);
    }
    H *h = new H();
    h->kind = k; h->owner = c; h->p = p; h->dev = dev;
    g_h[slot] = h;
    if (H *d = dev ? hraw(dev) : nullptr) d->children++;
    return slot | (g_gen[slot] & GEN_MASK) << SLOT_BITS;
}

void release_ptr(Kind k, void *p) {
    if (!p) return;
    switch (k) {
    case K_ADAPTER: wgpuAdapterRelease((WGPUAdapter)p); break;
    case K_DEVICE: wgpuDeviceRelease((WGPUDevice)p); break;
    case K_BUFFER: wgpuBufferRelease((WGPUBuffer)p); break;
    case K_TEXTURE: wgpuTextureRelease((WGPUTexture)p); break;
    case K_VIEW: wgpuTextureViewRelease((WGPUTextureView)p); break;
    case K_SAMPLER: wgpuSamplerRelease((WGPUSampler)p); break;
    case K_BGL: wgpuBindGroupLayoutRelease((WGPUBindGroupLayout)p); break;
    case K_BG: wgpuBindGroupRelease((WGPUBindGroup)p); break;
    case K_PL: wgpuPipelineLayoutRelease((WGPUPipelineLayout)p); break;
    case K_SHADER: wgpuShaderModuleRelease((WGPUShaderModule)p); break;
    case K_RPIPE: wgpuRenderPipelineRelease((WGPURenderPipeline)p); break;
    case K_CPIPE: wgpuComputePipelineRelease((WGPUComputePipeline)p); break;
    case K_ENC: wgpuCommandEncoderRelease((WGPUCommandEncoder)p); break;
    case K_CB: wgpuCommandBufferRelease((WGPUCommandBuffer)p); break;
    case K_RPASS: wgpuRenderPassEncoderRelease((WGPURenderPassEncoder)p); break;
    case K_CPASS: wgpuComputePassEncoderRelease((WGPUComputePassEncoder)p); break;
    case K_QS: wgpuQuerySetRelease((WGPUQuerySet)p); break;
    default: break;
    }
}

bool g_closing = false;   /* webgpu_close_owner: the isolate is going away, so no memory accounting */

void ext_mem(H *h, int64_t bytes) {
    if (g_closing || !h->owner || !bytes) return;
    h->ext += bytes;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    h->owner->iso->AdjustAmountOfExternalAllocatedMemory(bytes);
#pragma GCC diagnostic pop
}

void hfree(uint32_t id) {
    H *h = hraw(id);
    if (!h) return;
    uint32_t s = id & SLOT_MASK;
    g_h[s] = nullptr; g_gen[s]++; g_free_slots.push_back(s);
    if (h->ext) ext_mem(h, -h->ext);
    for (MapRange &r : h->ranges) r.ab.Reset();
    if (h->kind == K_DEVICE && h->q) wgpuQueueRelease(h->q);
    release_ptr(h->kind, h->p);
    uint32_t dev = h->dev;
    delete h;
    if (H *d = dev ? hraw(dev) : nullptr)
        if (--d->children == 0 && d->want_release) hfree(dev);
}

/* errors of a wgpu call made on the page's behalf but checked here first (shaders, pipelines, finish) */
struct Cap;
Cap *g_cap = nullptr;
struct Cap {
    bool has = false; int type = 0; std::string msg; Cap *prev;
    Cap() : prev(g_cap) { g_cap = this; }
    ~Cap() { g_cap = prev; }
};

/* what the page hears about: tickets settled, uncaptured errors and lost devices */
struct Ev { int kind; uint32_t ticket; double a; int type; std::string msg; };   /* kind 0 ticket, 1 uncaptured error, 2 lost */
struct PerCtx {
    v8::Global<v8::Function> cb;
    v8::Global<v8::Symbol> sym;
    std::deque<Ev> evs;
    int outstanding = 0, uncaptured = 0;
    double since = 0;
    uint64_t serial = 0;
};
std::mutex g_mu;
std::map<JsCtx *, PerCtx> g_ctx;
uint64_t g_serial = 1;
uint32_t g_next_ticket = 1;
int g_outstanding = 0;
WGPUInstance g_inst = nullptr;
bool g_inst_failed = false;

struct Tk { JsCtx *c; uint64_t serial; uint32_t ticket, obj; };

PerCtx *ctx_of(JsCtx *c, uint64_t serial) {
    auto it = g_ctx.find(c);
    return it != g_ctx.end() && it->second.serial == serial ? &it->second : nullptr;
}
Tk *ticket_new(JsCtx *c, uint32_t obj) {
    PerCtx &pc = g_ctx[c];
    if (!pc.serial) pc.serial = g_serial++;
    if (!pc.outstanding++) pc.since = now_ms();
    g_outstanding++;
    return new Tk{ c, pc.serial, g_next_ticket++, obj };
}
void ticket_done(Tk *t, double a, std::string msg = std::string()) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (PerCtx *pc = ctx_of(t->c, t->serial)) { pc->outstanding--; pc->evs.push_back(Ev{ 0, t->ticket, a, 0, std::move(msg) }); }
        g_outstanding--;
    }
    delete t;
}
std::string sv(WGPUStringView s) { return s.data ? std::string(s.data, s.length == WGPU_STRLEN ? strlen(s.data) : s.length) : std::string(); }

int filter_of(int type) { return type == WGPUErrorType_OutOfMemory ? 1 : type == WGPUErrorType_Internal || type == WGPUErrorType_Unknown ? 2 : 0; }

void route_error(uint32_t did, int type, const std::string &msg) {
    if (g_cap) { if (!g_cap->has) { g_cap->has = true; g_cap->type = type; g_cap->msg = msg; } return; }
    H *d = hraw(did);
    if (!d || d->kind != K_DEVICE || d->lost) return;
    int f = filter_of(type);
    for (auto it = d->scopes.rbegin(); it != d->scopes.rend(); ++it)
        if (it->filter == f) { if (!it->has) { it->has = true; it->type = type; it->msg = msg; } return; }
    std::lock_guard<std::mutex> lk(g_mu);
    auto pc = g_ctx.find(d->owner);
    if (pc == g_ctx.end() || pc->second.uncaptured >= 256) return;
    pc->second.uncaptured++;
    pc->second.evs.push_back(Ev{ 1, did, 0, type, msg });
}
void verr(uint32_t did, const char *msg) { route_error(did, WGPUErrorType_Validation, msg); }

/* wgpu objects whose release waits until no callback is running */
std::vector<std::pair<Kind, void *>> g_dead;
void drain_dead() {
    while (!g_dead.empty()) { auto d = g_dead.back(); g_dead.pop_back(); release_ptr(d.first, d.second); }
}

void on_uncaptured(WGPUDevice const *, WGPUErrorType type, WGPUStringView msg, void *u1, void *) {
    route_error((uint32_t)(uintptr_t)u1, (int)type, sv(msg));
}
void on_lost(WGPUDevice const *, WGPUDeviceLostReason reason, WGPUStringView msg, void *u1, void *) {
    if (reason == WGPUDeviceLostReason_CallbackCancelled || reason == WGPUDeviceLostReason_FailedCreation) return;
    uint32_t did = (uint32_t)(uintptr_t)u1;
    H *d = hraw(did);
    if (!d || d->kind != K_DEVICE) return;
    d->lost = true;
    std::lock_guard<std::mutex> lk(g_mu);
    auto pc = g_ctx.find(d->owner);
    if (pc != g_ctx.end()) pc->second.evs.push_back(Ev{ 2, did, reason == WGPUDeviceLostReason_Destroyed ? 1.0 : 0.0, 0, sv(msg) });
}
void on_adapter(WGPURequestAdapterStatus st, WGPUAdapter ad, WGPUStringView, void *u1, void *) {
    Tk *t = (Tk *)u1;
    uint32_t id = 0;
    if (st == WGPURequestAdapterStatus_Success && ad) {
        if (ctx_of(t->c, t->serial)) id = hnew(K_ADAPTER, t->c, ad, 0);
        if (!id) g_dead.push_back({ K_ADAPTER, ad });
    }
    ticket_done(t, id);
}
void on_device(WGPURequestDeviceStatus st, WGPUDevice dev, WGPUStringView msg, void *u1, void *) {
    Tk *t = (Tk *)u1;
    H *d = hraw(t->obj);
    bool ok = st == WGPURequestDeviceStatus_Success && dev;
    if (ok && d && d->kind == K_DEVICE && !d->p) { d->p = dev; ticket_done(t, t->obj); return; }
    if (ok) g_dead.push_back({ K_DEVICE, dev });
    if (d && !d->p) hfree(t->obj);
    ticket_done(t, 0, sv(msg));
}
void on_map(WGPUMapAsyncStatus st, WGPUStringView, void *u1, void *) {
    Tk *t = (Tk *)u1;
    H *b = hraw(t->obj);
    bool cur = b && b->kind == K_BUFFER && b->map_ticket == t->ticket && b->map_state == 1;
    bool ok = cur && st == WGPUMapAsyncStatus_Success;
    if (cur) { b->map_state = ok ? 2 : 0; b->map_ticket = 0; }
    ticket_done(t, ok ? 1 : 0);
}
void on_done(WGPUQueueWorkDoneStatus, WGPUStringView, void *u1, void *) { ticket_done((Tk *)u1, 1); }

bool instance() {
    if (g_inst) return true;
    if (g_inst_failed) return false;
    WGPUInstanceExtras ex = {};
    ex.chain.sType = (WGPUSType)WGPUSType_InstanceExtras;
#ifdef __ANDROID__
    ex.backends = WGPUInstanceBackend_Primary;
#else
    ex.backends = WGPUInstanceBackend_Primary | WGPUInstanceBackend_GL;   /* desktops without a Vulkan driver */
#endif
    WGPUInstanceDescriptor d = WGPU_INSTANCE_DESCRIPTOR_INIT;
    d.nextInChain = &ex.chain;
    gpu_log_quiet = 1;
    g_inst = wgpuCreateInstance(&d);
    if (!g_inst) g_inst_failed = true;
    return g_inst != nullptr;
}

/* enums by WebGPU name, lowercase without hyphens */
struct EnumEntry { const char *type, *name; int value; };
const EnumEntry g_enums[] = {
#include "webgpu_enums.inc"
};
std::unordered_map<std::string, int> &enum_map() {
    static std::unordered_map<std::string, int> m;
    if (m.empty()) for (const EnumEntry &e : g_enums) m[std::string(e.type) + ":" + e.name] = e.value;
    return m;
}
const char *enum_name(const char *type, int v) {
    for (const EnumEntry &e : g_enums) if (e.value == v && !strcmp(e.type, type)) return e.name;
    return nullptr;
}

/* page values to wgpu ones; bad is set once an exception is pending */
struct Env {
    JsCtx *c; v8::Isolate *iso; v8::Local<v8::Context> ctx; v8::Local<v8::Symbol> sym;
    bool bad = false;
    explicit Env(v8::Isolate *i) : c(jctx(i)), iso(i), ctx(i->GetCurrentContext()) {
        auto it = g_ctx.find(c);
        if (it != g_ctx.end() && !it->second.sym.IsEmpty()) sym = it->second.sym.Get(iso);
    }
    void type_error(const std::string &m) {
        if (!bad) iso->ThrowException(v8::Exception::TypeError(jstr(iso, m.c_str())));
        bad = true;
    }
    void range_error(const char *m) {
        if (!bad) iso->ThrowException(v8::Exception::RangeError(jstr(iso, m)));
        bad = true;
    }
    v8::Local<v8::Value> get(v8::Local<v8::Value> o, const char *k) {
        v8::Local<v8::Value> v;
        if (bad || o.IsEmpty() || !o->IsObject()) return v8::Undefined(iso);
        if (!o.As<v8::Object>()->Get(ctx, jstr(iso, k)).ToLocal(&v)) { bad = true; return v8::Undefined(iso); }
        return v;
    }
    bool has(v8::Local<v8::Value> o, const char *k) { return !get(o, k)->IsUndefined(); }
    double num(v8::Local<v8::Value> v, double def) {
        if (bad || v.IsEmpty() || v->IsUndefined()) return def;
        if (v->IsNumber()) return v.As<v8::Number>()->Value();
        double d;
        if (!v->NumberValue(ctx).To(&d)) { bad = true; return def; }
        return d;
    }
    double num(v8::Local<v8::Value> o, const char *k, double def) { return num(get(o, k), def); }
    /* [EnforceRange] integers */
    double whole(v8::Local<v8::Value> v, double def, double max, const char *what, bool req = false) {
        if (!bad && (v.IsEmpty() || v->IsUndefined())) { if (req) type_error(std::string("Required member is undefined: ") + what); return def; }
        double d = num(v, def);
        if (bad) return def;
        if (!std::isfinite(d)) { type_error(std::string("Value is not finite: ") + what); return def; }
        d = std::trunc(d);
        if (d < 0 || d > max) { type_error(std::string("Value is outside the allowed range: ") + what); return def; }
        return d;
    }
    uint32_t u32(v8::Local<v8::Value> o, const char *k, uint32_t def, bool req = false) { return (uint32_t)whole(get(o, k), def, 4294967295.0, k, req); }
    uint64_t u64(v8::Local<v8::Value> o, const char *k, uint64_t def, bool req = false) { return (uint64_t)whole(get(o, k), (double)def, 9007199254740991.0, k, req); }
    uint32_t u32v(v8::Local<v8::Value> v, uint32_t def) { return (uint32_t)whole(v, def, 4294967295.0, "value"); }
    uint64_t u64v(v8::Local<v8::Value> v, uint64_t def) { return (uint64_t)whole(v, (double)def, 9007199254740991.0, "value"); }
    bool flag(v8::Local<v8::Value> o, const char *k, bool def) {
        v8::Local<v8::Value> v = get(o, k);
        return v->IsUndefined() ? def : v->BooleanValue(iso);
    }
    std::string str(v8::Local<v8::Value> v) {
        if (bad || v.IsEmpty() || v->IsUndefined()) return std::string();
        v8::Local<v8::String> s;
        if (!v->ToString(ctx).ToLocal(&s)) { bad = true; return std::string(); }
        return jcstr(iso, s);
    }
    int en_v(v8::Local<v8::Value> v, const char *type, int def, bool req = false) {
        if (bad) return def;
        if (v.IsEmpty() || v->IsUndefined()) { if (req) type_error(std::string("Required ") + type + " is undefined"); return def; }
        std::string s = str(v), key;
        if (bad) return def;
        bool ok = !s.empty();
        for (char ch : s) { if (ch == '-') continue; if ((ch >= 'A' && ch <= 'Z') || ch == '_') ok = false; key += ch; }
        auto &m = enum_map();
        auto it = ok ? m.find(std::string(type) + ":" + key) : m.end();
        if (it == m.end()) { type_error("The provided value is not a valid enum value of type GPU" + std::string(type)); return def; }
        return it->second;
    }
    int en(v8::Local<v8::Value> o, const char *k, const char *type, int def, bool req = false) { return en_v(get(o, k), type, def, req); }
    uint32_t hid(v8::Local<v8::Value> v) {
        if (bad || v.IsEmpty()) return 0;
        if (v->IsNumber()) return (uint32_t)(int64_t)v.As<v8::Number>()->Value();
        if (!v->IsObject() || sym.IsEmpty()) return 0;
        v8::Local<v8::Value> r;
        if (!v.As<v8::Object>()->Get(ctx, sym).ToLocal(&r)) { bad = true; return 0; }
        return r->IsNumber() ? (uint32_t)(int64_t)r.As<v8::Number>()->Value() : 0;
    }
    uint32_t hid(v8::Local<v8::Value> o, const char *k) { return hid(get(o, k)); }
    /* a sequence member as an array; undefined is empty */
    v8::Local<v8::Array> list(v8::Local<v8::Value> v, const char *what) {
        if (bad || v.IsEmpty() || v->IsUndefined() || v->IsNull()) return v8::Array::New(iso, 0);
        if (!v->IsArray()) { type_error(std::string("Value is not a sequence: ") + what); return v8::Array::New(iso, 0); }
        return v.As<v8::Array>();
    }
    v8::Local<v8::Array> list(v8::Local<v8::Value> o, const char *k, bool) { return list(get(o, k), k); }
    v8::Local<v8::Value> at(v8::Local<v8::Array> a, uint32_t i) {
        v8::Local<v8::Value> v;
        if (bad || !a->Get(ctx, i).ToLocal(&v)) { bad = true; return v8::Undefined(iso); }
        return v;
    }
    /* GPUExtent3D, GPUOrigin3D and GPUColor: a sequence or a dictionary */
    void xyz(v8::Local<v8::Value> v, const char *const names[3], const uint32_t defs[3], uint32_t out[3], bool first_req) {
        for (int i = 0; i < 3; i++) out[i] = defs[i];
        if (bad || v.IsEmpty() || v->IsUndefined()) { if (first_req && !bad) type_error("Required extent is undefined"); return; }
        if (v->IsArray()) {
            auto a = v.As<v8::Array>();
            if (a->Length() > 3 || (first_req && a->Length() < 1)) { type_error("Wrong number of dimensions"); return; }
            for (uint32_t i = 0; i < a->Length(); i++) out[i] = u32v(at(a, i), defs[i]);
            return;
        }
        if (!v->IsObject()) { type_error("Value is not a dictionary"); return; }
        for (int i = 0; i < 3; i++) out[i] = u32(v, names[i], defs[i], first_req && i == 0);
    }
    WGPUExtent3D extent(v8::Local<v8::Value> v) {
        static const char *const n[3] = { "width", "height", "depthOrArrayLayers" };
        static const uint32_t d[3] = { 1, 1, 1 };
        uint32_t o[3]; xyz(v, n, d, o, true);
        return WGPUExtent3D{ o[0], o[1], o[2] };
    }
    WGPUOrigin3D origin(v8::Local<v8::Value> v) {
        static const char *const n[3] = { "x", "y", "z" };
        static const uint32_t d[3] = { 0, 0, 0 };
        uint32_t o[3]; xyz(v, n, d, o, false);
        return WGPUOrigin3D{ o[0], o[1], o[2] };
    }
    WGPUColor color(v8::Local<v8::Value> v) {
        WGPUColor c = { 0, 0, 0, 0 };
        if (bad || v.IsEmpty() || v->IsUndefined()) return c;
        if (v->IsArray()) {
            auto a = v.As<v8::Array>();
            if (a->Length() != 4) { type_error("A color needs four components"); return c; }
            c.r = num(at(a, 0), 0); c.g = num(at(a, 1), 0); c.b = num(at(a, 2), 0); c.a = num(at(a, 3), 0);
            return c;
        }
        c.r = num(v, "r", 0); c.g = num(v, "g", 0); c.b = num(v, "b", 0); c.a = num(v, "a", 0);
        return c;
    }
};

#define GPUC                                 \
    Env e(a.GetIsolate());                   \
    JsCtx *c = e.c; (void)c;                 \
    v8::Isolate *iso = e.iso; (void)iso
#define RET(...) a.GetReturnValue().Set(__VA_ARGS__)
#define FN(nm) void w_##nm(const FCI &a)
#define ARG(i) ((i) < a.Length() ? a[i] : v8::Local<v8::Value>(v8::Undefined(iso)))
#define ID(i) e.hid(ARG(i))
inline double an(const FCI &a, int i) {
    if (i >= a.Length()) return 0;
    v8::Local<v8::Value> v = a[i];
    if (v->IsNumber()) return v.As<v8::Number>()->Value();
    return v->NumberValue(a.GetIsolate()->GetCurrentContext()).FromMaybe(0);
}
inline uint32_t au(const FCI &a, int i) { double d = an(a, i); return std::isfinite(d) && d > 0 ? d >= 4294967295.0 ? 0xffffffffu : (uint32_t)d : 0; }
inline uint64_t au64(const FCI &a, int i) { double d = an(a, i); return std::isfinite(d) && d > 0 ? (uint64_t)d : 0; }

WGPUStringView view_of(const std::string &s) { return WGPUStringView{ s.data(), s.size() }; }
const WGPUStringView NO_STR = { nullptr, WGPU_STRLEN };

/* the device a call is for: valid and not yet released */
H *device(Env &e, uint32_t id) {
    H *d = hget(e.c, id, K_DEVICE);
    return d && d->p ? d : nullptr;
}
WGPUQueue queue_of(H *d) {
    if (!d->q && d->p) d->q = wgpuDeviceGetQueue((WGPUDevice)d->p);
    return d->q;
}
/* a child object: the wgpu pointer when valid, else nullptr and *ok cleared */
void *need(Env &e, uint32_t id, Kind k, bool *ok) {
    H *h = hget(e.c, id, k);
    if (!h || !h->p) { *ok = false; return nullptr; }
    return h->p;
}
uint32_t finish_new(Env &e, Kind k, void *p, uint32_t did, Cap &cap) {
    uint32_t id = hnew(k, e.c, p, did);
    if (!id) { release_ptr(k, p); return 0; }
    if (cap.has) {
        hraw(id)->bad = true;
        std::string m = cap.msg; int t = cap.type;
        cap.has = false;
        Cap *saved = g_cap; g_cap = cap.prev; route_error(did, t, m); g_cap = saved;
    }
    return id;
}
uint32_t invalid(Env &e, Kind k, uint32_t did, const char *msg) {
    if (msg) verr(did, msg);
    return hnew(k, e.c, nullptr, did);
}
void add_use(H *h, uint32_t id) {
    if (!id) return;
    size_t n = h->uses.size();
    for (size_t i = n > 8 ? n - 8 : 0; i < n; i++) if (h->uses[i] == id) return;
    h->uses.push_back(id);
}

bool fmt_has_depth(int f) {
    return f == WGPUTextureFormat_Depth16Unorm || f == WGPUTextureFormat_Depth24Plus || f == WGPUTextureFormat_Depth24PlusStencil8 ||
           f == WGPUTextureFormat_Depth32Float || f == WGPUTextureFormat_Depth32FloatStencil8;
}
int fmt_bytes(int f) {   /* bytes per texel of the uncompressed color formats copyExternalImageToTexture writes; 0 others */
    switch (f) {
    case WGPUTextureFormat_R8Unorm: return 1;
    case WGPUTextureFormat_RG8Unorm: case WGPUTextureFormat_R16Float: return 2;
    case WGPUTextureFormat_RGBA8Unorm: case WGPUTextureFormat_RGBA8UnormSrgb: case WGPUTextureFormat_BGRA8Unorm: case WGPUTextureFormat_BGRA8UnormSrgb:
    case WGPUTextureFormat_RGB10A2Unorm: case WGPUTextureFormat_RG16Float: case WGPUTextureFormat_R32Float: return 4;
    case WGPUTextureFormat_RGBA16Float: case WGPUTextureFormat_RG32Float: return 8;
    case WGPUTextureFormat_RGBA32Float: return 16;
    default: return 0;
    }
}

/* ---- setup, adapters and devices ---- */

FN(init) {   /* (eventCallback, symbol): events arrive as cb(kind, ticket, a, message, type) */
    GPUC;
    PerCtx &pc = g_ctx[c];
    if (!pc.serial) pc.serial = g_serial++;
    if (a.Length() > 0 && a[0]->IsFunction()) pc.cb.Reset(iso, a[0].As<v8::Function>());
    if (a.Length() > 1 && a[1]->IsSymbol()) pc.sym.Reset(iso, a[1].As<v8::Symbol>());
}
FN(requestAdapter) {   /* (powerPreference, forceFallbackAdapter): ticket, settled with the adapter id or 0 */
    GPUC;
    int pref = e.en_v(ARG(0), "PowerPreference", WGPUPowerPreference_Undefined);
    if (e.bad) return;
    if (!instance()) { RET(0); return; }
    WGPURequestAdapterOptions o = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    o.powerPreference = (WGPUPowerPreference)pref;
    o.forceFallbackAdapter = ARG(1)->BooleanValue(iso);
    Tk *t = ticket_new(c, 0);
    uint32_t ticket = t->ticket;
    wgpuInstanceRequestAdapter(g_inst, &o, WGPURequestAdapterCallbackInfo{ nullptr, WGPUCallbackMode_AllowProcessEvents, on_adapter, t, nullptr });
    RET(ticket);
}

#define LIMITS(X)                                                                                                   \
    X(maxTextureDimension1D, 0) X(maxTextureDimension2D, 0) X(maxTextureDimension3D, 0) X(maxTextureArrayLayers, 0)  \
    X(maxBindGroups, 0) X(maxBindGroupsPlusVertexBuffers, 0) X(maxBindingsPerBindGroup, 0)                          \
    X(maxDynamicUniformBuffersPerPipelineLayout, 0) X(maxDynamicStorageBuffersPerPipelineLayout, 0)                 \
    X(maxSampledTexturesPerShaderStage, 0) X(maxSamplersPerShaderStage, 0) X(maxStorageBuffersPerShaderStage, 0)    \
    X(maxStorageTexturesPerShaderStage, 0) X(maxUniformBuffersPerShaderStage, 0) X(maxUniformBufferBindingSize, 1)  \
    X(maxStorageBufferBindingSize, 1) X(minUniformBufferOffsetAlignment, 0) X(minStorageBufferOffsetAlignment, 0)   \
    X(maxVertexBuffers, 0) X(maxBufferSize, 1) X(maxVertexAttributes, 0) X(maxVertexBufferArrayStride, 0)           \
    X(maxInterStageShaderVariables, 0) X(maxColorAttachments, 0) X(maxColorAttachmentBytesPerSample, 0)            \
    X(maxComputeWorkgroupStorageSize, 0) X(maxComputeInvocationsPerWorkgroup, 0) X(maxComputeWorkgroupSizeX, 0)     \
    X(maxComputeWorkgroupSizeY, 0) X(maxComputeWorkgroupSizeZ, 0) X(maxComputeWorkgroupsPerDimension, 0)

v8::Local<v8::Object> limits_obj(Env &e, const WGPULimits &l) {
    v8::Local<v8::Object> o = v8::Object::New(e.iso);
#define X(nm, wide) (void)o->Set(e.ctx, jstr(e.iso, #nm), v8::Number::New(e.iso, (double)l.nm));
    LIMITS(X)
#undef X
    return o;
}
v8::Local<v8::Value> features_arr(Env &e, const WGPUSupportedFeatures &f) {
    std::vector<v8::Local<v8::Value>> out;
    for (size_t i = 0; i < f.featureCount; i++)
        if (const char *n = enum_name("FeatureName", (int)f.features[i])) out.push_back(jstr(e.iso, n));
    return v8::Array::New(e.iso, out.data(), out.size());
}
FN(adapterInfo) {   /* (adapter): [vendor, architecture, isFallback, subgroupMin, subgroupMax]; no device names */
    GPUC;
    H *ad = hget(c, ID(0), K_ADAPTER);
    if (!ad) return;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo((WGPUAdapter)ad->p, &info) != WGPUStatus_Success) return;
    std::string vendor = sv(info.vendor), arch = sv(info.architecture);
    for (char &ch : vendor) ch = (char)tolower((unsigned char)ch);
    for (char &ch : arch) ch = (char)tolower((unsigned char)ch);
    v8::Local<v8::Value> v[5] = { jstr(iso, vendor.c_str()), jstr(iso, arch.c_str()), v8::Boolean::New(iso, info.adapterType == WGPUAdapterType_CPU),
                                  v8::Number::New(iso, info.subgroupMinSize), v8::Number::New(iso, info.subgroupMaxSize) };
    wgpuAdapterInfoFreeMembers(info);
    RET(v8::Array::New(iso, v, 5));
}
FN(limits) {   /* (adapter or device) */
    GPUC;
    uint32_t id = ID(0);
    WGPULimits l = WGPU_LIMITS_INIT;
    if (H *ad = hget(c, id, K_ADAPTER)) { if (wgpuAdapterGetLimits((WGPUAdapter)ad->p, &l) != WGPUStatus_Success) return; }
    else if (H *d = device(e, id)) { if (wgpuDeviceGetLimits((WGPUDevice)d->p, &l) != WGPUStatus_Success) return; }
    else return;
    RET(limits_obj(e, l));
}
FN(features) {   /* (adapter or device): feature names, lowercase without hyphens */
    GPUC;
    uint32_t id = ID(0);
    WGPUSupportedFeatures f = {};
    if (H *ad = hget(c, id, K_ADAPTER)) wgpuAdapterGetFeatures((WGPUAdapter)ad->p, &f);
    else if (H *d = device(e, id)) wgpuDeviceGetFeatures((WGPUDevice)d->p, &f);
    else return;
    RET(features_arr(e, f));
    wgpuSupportedFeaturesFreeMembers(f);
}
FN(requestDevice) {   /* (adapter, featureNames, limits): ticket, settled with the device id or 0 and a message */
    GPUC;
    H *ad = hget(c, ID(0), K_ADAPTER);
    if (!ad || !ad->p) { RET(0); return; }
    std::vector<WGPUFeatureName> feats;
    auto fl = e.list(ARG(1), "requiredFeatures");
    for (uint32_t i = 0; i < fl->Length() && !e.bad; i++) feats.push_back((WGPUFeatureName)e.en_v(e.at(fl, i), "FeatureName", 0));
    WGPULimits l = WGPU_LIMITS_INIT;
    v8::Local<v8::Value> lo = ARG(2);
#define X(nm, wide) if (e.has(lo, #nm)) l.nm = wide ? (decltype(l.nm))e.u64(lo, #nm, 0) : (decltype(l.nm))e.u32(lo, #nm, 0);
    LIMITS(X)
#undef X
    if (e.bad) return;
    uint32_t did = hnew(K_DEVICE, c, nullptr, 0);
    if (!did) { RET(0); return; }
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.requiredFeatureCount = feats.size(); dd.requiredFeatures = feats.data();
    dd.requiredLimits = &l;
    dd.deviceLostCallbackInfo = WGPUDeviceLostCallbackInfo{ nullptr, WGPUCallbackMode_AllowProcessEvents, on_lost, (void *)(uintptr_t)did, nullptr };
    dd.uncapturedErrorCallbackInfo = WGPUUncapturedErrorCallbackInfo{ nullptr, on_uncaptured, (void *)(uintptr_t)did, nullptr };
    Tk *t = ticket_new(c, did);
    uint32_t ticket = t->ticket;
    wgpuAdapterRequestDevice((WGPUAdapter)ad->p, &dd, WGPURequestDeviceCallbackInfo{ nullptr, WGPUCallbackMode_AllowProcessEvents, on_device, t, nullptr });
    RET(ticket);
}

/* every mapped range of the buffer's ArrayBuffers detached; the mapping itself is left to the caller */
void detach_ranges(H *b) {
    for (MapRange &r : b->ranges) {
        if (r.ab.IsEmpty()) continue;
        v8::Local<v8::ArrayBuffer> ab = r.ab.Get(b->owner->iso);
        if (ab->IsDetachable()) (void)ab->Detach(v8::Local<v8::Value>());
        r.ab.Reset();
    }
    b->ranges.clear();
}

FN(destroy) {   /* (device, buffer, texture or query set) */
    GPUC;
    uint32_t id = ID(0);
    H *h = hraw(id);
    if (!h || h->owner != c || h->destroyed || !h->p) { if (h && h->owner == c) h->destroyed = true; return; }
    switch (h->kind) {
    case K_DEVICE: {
        h->destroyed = h->lost = true;
        for (H *b : g_h)   /* destroying a device unmaps its buffers */
            if (b && b->kind == K_BUFFER && b->dev == id && b->map_state) { detach_ranges(b); b->map_state = 0; b->map_ticket = 0; }
        Cap cap;
        wgpuDeviceDestroy((WGPUDevice)h->p);
        break;
    }
    case K_BUFFER: {
        detach_ranges(h);
        h->map_state = 0; h->map_ticket = 0; h->destroyed = true;
        Cap cap;
        wgpuBufferDestroy((WGPUBuffer)h->p);
        break;
    }
    case K_TEXTURE:
        if (h->canvas) return;   /* the canvas's own: it goes with the canvas */
        h->destroyed = true;
        { Cap cap; wgpuTextureDestroy((WGPUTexture)h->p); }
        break;
    case K_QS: h->destroyed = true; { Cap cap; wgpuQuerySetDestroy((WGPUQuerySet)h->p); } break;
    default: break;
    }
}
FN(release) {   /* (id): the wrapper was collected */
    GPUC;
    uint32_t id = ID(0);
    H *h = hraw(id);
    if (!h || h->owner != c || h->canvas) return;
    if (h->kind == K_DEVICE) { h->want_release = true; if (!h->children) hfree(id); return; }
    hfree(id);
}

/* ---- error scopes ---- */

FN(pushErrorScope) {   /* (device, filter) */
    GPUC;
    H *d = hget(c, ID(0), K_DEVICE);
    int f = e.en_v(ARG(1), "ErrorFilter", 0, true);
    if (!d || e.bad) return;
    d->scopes.push_back(Scope{ f == WGPUErrorFilter_OutOfMemory ? 1 : f == WGPUErrorFilter_Internal ? 2 : 0 });
}
FN(popErrorScope) {   /* (device): undefined when the stack is empty, null when no error, else [type, message] */
    GPUC;
    H *d = hget(c, ID(0), K_DEVICE);
    if (!d || d->scopes.empty()) return;
    Scope s = std::move(d->scopes.back());
    d->scopes.pop_back();
    if (!s.has) { RET(v8::Null(iso)); return; }
    const char *type = s.type == WGPUErrorType_OutOfMemory ? "out-of-memory" : s.type == WGPUErrorType_Validation ? "validation" : "internal";
    v8::Local<v8::Value> v[2] = { jstr(iso, type), jstr(iso, s.msg.c_str(), (int)s.msg.size()) };
    RET(v8::Array::New(iso, v, 2));
}

/* ---- resources ---- */

FN(createBuffer) {   /* (device, {size, usage, mappedAtCreation}) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> o = ARG(1);
    uint64_t size = e.u64(o, "size", 0, true), usage = e.u64(o, "usage", 0, true);
    bool mac = e.flag(o, "mappedAtCreation", false);
    if (e.bad || !d) return;
    if (mac && size % 4) { e.range_error("mappedAtCreation needs a size that is a multiple of 4"); return; }
    uint32_t id;
    if (usage & ~0x3FFull) id = invalid(e, K_BUFFER, did, "Buffer usage has unknown bits");
    else if (mac && size > ((uint64_t)1 << 32)) { e.range_error("The buffer is too large to map"); return; }
    else {
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = size; bd.usage = usage; bd.mappedAtCreation = mac;
        Cap cap;
        WGPUBuffer b = wgpuDeviceCreateBuffer((WGPUDevice)d->p, &bd);
        if (!b && mac) { e.range_error("The buffer could not be allocated"); return; }
        if (!b && !cap.has) { cap.has = true; cap.type = WGPUErrorType_OutOfMemory; cap.msg = "The buffer could not be allocated"; }
        id = finish_new(e, K_BUFFER, b, did, cap);
    }
    H *h = hraw(id);
    if (!h) return;
    h->size = size; h->usage = usage;
    if (mac) { h->map_state = 2; h->map_mode = WGPUMapMode_Write; h->map_off = 0; h->map_size = size; }
    if (h->p) ext_mem(h, (int64_t)std::min<uint64_t>(size, (uint64_t)1 << 40));
    RET(id);
}
FN(mapAsync) {   /* (buffer, mode, offset, size): ticket, settled with 1 once mapped; 0 when refused */
    GPUC;
    uint32_t id = ID(0);
    H *b = hget(c, id, K_BUFFER);
    if (!b) { RET(0); return; }
    uint64_t mode = au64(a, 1), off = au64(a, 2);
    uint64_t size = ARG(3)->IsUndefined() ? (off < b->size ? b->size - off : 0) : au64(a, 3);
    H *d = hraw(b->dev);
    const char *why = nullptr;
    if (!d || d->lost) { RET(0); return; }
    if (b->map_state) why = "The buffer is already mapped or being mapped";
    else if (!b->p || b->bad || b->destroyed) why = "The buffer is invalid or destroyed";
    else if (mode != WGPUMapMode_Read && mode != WGPUMapMode_Write) why = "The map mode must be READ or WRITE";
    else if (!(b->usage & mode)) why = "The buffer was not created for that map mode";
    else if (off % 8 || size % 4) why = "The map offset must be a multiple of 8 and its size of 4";
    else if (off > b->size || size > b->size - off) why = "The map range is outside the buffer";
    if (why) { verr(b->dev, why); RET(0); return; }
    Tk *t = ticket_new(c, id);
    uint32_t ticket = t->ticket;
    b->map_state = 1; b->map_mode = (int)mode; b->map_off = off; b->map_size = size; b->map_ticket = ticket;
    wgpuBufferMapAsync((WGPUBuffer)b->p, mode, (size_t)off, (size_t)size, WGPUBufferMapCallbackInfo{ nullptr, WGPUCallbackMode_AllowProcessEvents, on_map, t, nullptr });
    RET(ticket);
}
FN(getMappedRange) {   /* (buffer, offset, size): an ArrayBuffer with the range's contents, undefined when not allowed */
    GPUC;
    H *b = hget(c, ID(0), K_BUFFER);
    if (!b || b->map_state != 2) return;
    uint64_t off = au64(a, 1);
    uint64_t end = b->map_off + b->map_size;
    uint64_t size = ARG(2)->IsUndefined() ? (off < end ? end - off : 0) : au64(a, 2);
    if (off % 8 || size % 4 || off < b->map_off || off > end || size > end - off) return;
    for (MapRange &r : b->ranges) if (off < r.off + r.size && r.off < off + size) return;   /* overlaps one handed out */
    if (size > ((uint64_t)1 << 32)) return;
    std::unique_ptr<v8::BackingStore> bs = v8::ArrayBuffer::NewBackingStore(iso, (size_t)size, v8::BackingStoreInitializationMode::kUninitialized,
                                                                                 v8::BackingStoreOnFailureMode::kReturnNull);
    if (!bs) { e.range_error("Out of memory"); return; }
    if (size && b->p && !b->bad && !b->destroyed) {
        const void *src = wgpuBufferGetMappedRange((WGPUBuffer)b->p, (size_t)off, (size_t)size);
        memcpy(bs->Data(), src, (size_t)size);
    } else if (size) memset(bs->Data(), 0, (size_t)size);
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, std::shared_ptr<v8::BackingStore>(std::move(bs)));
    b->ranges.push_back(MapRange{ off, size, v8::Global<v8::ArrayBuffer>(iso, ab) });
    RET(ab);
}
FN(unmap) {   /* (buffer): written ranges go back to the buffer */
    GPUC;
    H *b = hget(c, ID(0), K_BUFFER);
    if (!b || !b->map_state) return;
    bool live = b->p && !b->bad && !b->destroyed;
    Cap cap;
    if (b->map_state == 2 && live && b->map_mode == WGPUMapMode_Write)
        for (MapRange &r : b->ranges) {
            if (r.ab.IsEmpty() || !r.size) continue;
            v8::Local<v8::ArrayBuffer> ab = r.ab.Get(iso);
            if (ab->ByteLength() != r.size) continue;   /* detached by the page */
            void *dst = wgpuBufferGetMappedRange((WGPUBuffer)b->p, (size_t)r.off, (size_t)r.size);
            memcpy(dst, ab->Data(), (size_t)r.size);
        }
    detach_ranges(b);
    if (live) wgpuBufferUnmap((WGPUBuffer)b->p);
    b->map_state = 0; b->map_ticket = 0;
}

FN(createTexture) {   /* (device, descriptor) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> o = ARG(1);
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.size = e.extent(e.get(o, "size"));
    td.format = (WGPUTextureFormat)e.en(o, "format", "TextureFormat", 0, true);
    uint64_t usage = e.u64(o, "usage", 0, true);
    td.dimension = (WGPUTextureDimension)e.en(o, "dimension", "TextureDimension", WGPUTextureDimension_2D);
    td.mipLevelCount = e.u32(o, "mipLevelCount", 1);
    td.sampleCount = e.u32(o, "sampleCount", 1);
    std::vector<WGPUTextureFormat> vf;
    auto vl = e.list(o, "viewFormats", true);
    for (uint32_t i = 0; i < vl->Length() && !e.bad; i++) vf.push_back((WGPUTextureFormat)e.en_v(e.at(vl, i), "TextureFormat", 0));
    if (e.bad || !d) return;
    uint32_t id;
    if (usage & ~0x3Full) id = invalid(e, K_TEXTURE, did, "Texture usage has unknown bits");
    else {
        td.usage = usage; td.viewFormatCount = vf.size(); td.viewFormats = vf.data();
        Cap cap;
        WGPUTexture t = wgpuDeviceCreateTexture((WGPUDevice)d->p, &td);
        id = finish_new(e, K_TEXTURE, t, did, cap);
    }
    H *h = hraw(id);
    if (!h) return;
    h->fmt = td.format; h->usage = usage;
    if (h->p && !h->bad) {
        double texels = (double)td.size.width * td.size.height * td.size.depthOrArrayLayers * std::max<uint32_t>(td.sampleCount, 1);
        if (td.mipLevelCount > 1) texels *= 4.0 / 3.0;
        int bpt = fmt_bytes(td.format);
        ext_mem(h, (int64_t)std::min(texels * (bpt ? bpt : 4), 1e12));
    }
    RET(id);
}
uint32_t make_view(Env &e, uint32_t tid, v8::Local<v8::Value> o) {
    H *t = hget(e.c, tid, K_TEXTURE);
    if (!t) return 0;
    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    vd.format = (WGPUTextureFormat)e.en(o, "format", "TextureFormat", WGPUTextureFormat_Undefined);
    vd.dimension = (WGPUTextureViewDimension)e.en(o, "dimension", "TextureViewDimension", WGPUTextureViewDimension_Undefined);
    vd.aspect = (WGPUTextureAspect)e.en(o, "aspect", "TextureAspect", WGPUTextureAspect_All);
    vd.baseMipLevel = e.u32(o, "baseMipLevel", 0);
    vd.mipLevelCount = e.u32(o, "mipLevelCount", WGPU_MIP_LEVEL_COUNT_UNDEFINED);
    vd.baseArrayLayer = e.u32(o, "baseArrayLayer", 0);
    vd.arrayLayerCount = e.u32(o, "arrayLayerCount", WGPU_ARRAY_LAYER_COUNT_UNDEFINED);
    uint64_t usage = e.u64(o, "usage", 0);
    if (e.bad) return 0;
    uint32_t id;
    if (!t->p) id = invalid(e, K_VIEW, t->dev, "The texture is invalid");
    else if (!vd.mipLevelCount || !vd.arrayLayerCount) id = invalid(e, K_VIEW, t->dev, "A texture view needs at least one mip level and array layer");
    else if (usage & ~0x3Full) id = invalid(e, K_VIEW, t->dev, "Texture view usage has unknown bits");
    else {
        vd.usage = usage;
        Cap cap;
        WGPUTextureView v = wgpuTextureCreateView((WGPUTexture)t->p, &vd);
        id = finish_new(e, K_VIEW, v, t->dev, cap);
    }
    if (H *v = hraw(id)) v->uses.push_back(tid);
    return id;
}
FN(createView) {   /* (texture, descriptor) */
    GPUC;
    uint32_t id = make_view(e, ID(0), ARG(1));
    if (id) RET(id);
}
FN(createSampler) {   /* (device, descriptor) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> o = ARG(1);
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sd.addressModeU = (WGPUAddressMode)e.en(o, "addressModeU", "AddressMode", WGPUAddressMode_ClampToEdge);
    sd.addressModeV = (WGPUAddressMode)e.en(o, "addressModeV", "AddressMode", WGPUAddressMode_ClampToEdge);
    sd.addressModeW = (WGPUAddressMode)e.en(o, "addressModeW", "AddressMode", WGPUAddressMode_ClampToEdge);
    sd.magFilter = (WGPUFilterMode)e.en(o, "magFilter", "FilterMode", WGPUFilterMode_Nearest);
    sd.minFilter = (WGPUFilterMode)e.en(o, "minFilter", "FilterMode", WGPUFilterMode_Nearest);
    sd.mipmapFilter = (WGPUMipmapFilterMode)e.en(o, "mipmapFilter", "MipmapFilterMode", WGPUMipmapFilterMode_Nearest);
    sd.lodMinClamp = (float)e.num(o, "lodMinClamp", 0);
    sd.lodMaxClamp = (float)e.num(o, "lodMaxClamp", 32);
    sd.compare = (WGPUCompareFunction)e.en(o, "compare", "CompareFunction", WGPUCompareFunction_Undefined);
    uint32_t aniso = e.u32(o, "maxAnisotropy", 1);
    if (e.bad || !d) return;
    sd.maxAnisotropy = (uint16_t)std::min<uint32_t>(aniso, 0xffff);
    Cap cap;
    WGPUSampler s = wgpuDeviceCreateSampler((WGPUDevice)d->p, &sd);
    RET(finish_new(e, K_SAMPLER, s, did, cap));
}
FN(createBindGroupLayout) {   /* (device, {entries}) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    auto el = e.list(ARG(1), "entries", true);
    std::vector<WGPUBindGroupLayoutEntry> ents;
    const char *why = nullptr;
    for (uint32_t i = 0; i < el->Length() && !e.bad; i++) {
        v8::Local<v8::Value> o = e.at(el, i);
        WGPUBindGroupLayoutEntry be = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        be.binding = e.u32(o, "binding", 0, true);
        uint64_t vis = e.u64(o, "visibility", 0, true);
        if (vis & ~7ull) why = "Binding visibility has unknown bits";
        be.visibility = vis & 7;
        int kinds = 0;
        v8::Local<v8::Value> v;
        if (!(v = e.get(o, "buffer"))->IsUndefined()) {
            kinds++;
            be.buffer.type = (WGPUBufferBindingType)e.en(v, "type", "BufferBindingType", WGPUBufferBindingType_Uniform);
            be.buffer.hasDynamicOffset = e.flag(v, "hasDynamicOffset", false);
            be.buffer.minBindingSize = e.u64(v, "minBindingSize", 0);
        }
        if (!(v = e.get(o, "sampler"))->IsUndefined()) {
            kinds++;
            be.sampler.type = (WGPUSamplerBindingType)e.en(v, "type", "SamplerBindingType", WGPUSamplerBindingType_Filtering);
        }
        if (!(v = e.get(o, "texture"))->IsUndefined()) {
            kinds++;
            be.texture.sampleType = (WGPUTextureSampleType)e.en(v, "sampleType", "TextureSampleType", WGPUTextureSampleType_Float);
            be.texture.viewDimension = (WGPUTextureViewDimension)e.en(v, "viewDimension", "TextureViewDimension", WGPUTextureViewDimension_2D);
            be.texture.multisampled = e.flag(v, "multisampled", false);
        }
        if (!(v = e.get(o, "storageTexture"))->IsUndefined()) {
            kinds++;
            be.storageTexture.access = (WGPUStorageTextureAccess)e.en(v, "access", "StorageTextureAccess", WGPUStorageTextureAccess_WriteOnly);
            be.storageTexture.format = (WGPUTextureFormat)e.en(v, "format", "TextureFormat", 0, true);
            be.storageTexture.viewDimension = (WGPUTextureViewDimension)e.en(v, "viewDimension", "TextureViewDimension", WGPUTextureViewDimension_2D);
        }
        if (!e.get(o, "externalTexture")->IsUndefined()) why = "External textures are not supported";
        if (kinds != 1 && !why) why = "A bind group layout entry needs exactly one binding type";
        ents.push_back(be);
    }
    if (e.bad || !d) return;
    if (why) { RET(invalid(e, K_BGL, did, why)); return; }
    WGPUBindGroupLayoutDescriptor bd = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bd.entryCount = ents.size(); bd.entries = ents.data();
    Cap cap;
    WGPUBindGroupLayout l = wgpuDeviceCreateBindGroupLayout((WGPUDevice)d->p, &bd);
    RET(finish_new(e, K_BGL, l, did, cap));
}
FN(createPipelineLayout) {   /* (device, {bindGroupLayouts}) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    auto ll = e.list(ARG(1), "bindGroupLayouts", true);
    std::vector<WGPUBindGroupLayout> ls;
    std::vector<WGPUBindGroupLayout> empties;
    bool ok = true;
    for (uint32_t i = 0; i < ll->Length() && !e.bad; i++) {
        v8::Local<v8::Value> v = e.at(ll, i);
        if (v->IsNullOrUndefined()) { ls.push_back(nullptr); continue; }
        ls.push_back((WGPUBindGroupLayout)need(e, e.hid(v), K_BGL, &ok));
    }
    if (e.bad || !d) return;
    if (!ok) { RET(invalid(e, K_PL, did, "A bind group layout is invalid")); return; }
    for (auto &l : ls)
        if (!l) {   /* a null entry is an empty layout */
            WGPUBindGroupLayoutDescriptor bd = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            l = wgpuDeviceCreateBindGroupLayout((WGPUDevice)d->p, &bd);
            empties.push_back(l);
        }
    WGPUPipelineLayoutDescriptor pd = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pd.bindGroupLayoutCount = ls.size(); pd.bindGroupLayouts = ls.data();
    Cap cap;
    WGPUPipelineLayout p = wgpuDeviceCreatePipelineLayout((WGPUDevice)d->p, &pd);
    for (auto l : empties) wgpuBindGroupLayoutRelease(l);
    RET(finish_new(e, K_PL, p, did, cap));
}
FN(createBindGroup) {   /* (device, {layout, entries: [{binding, resource}]}) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> o = ARG(1);
    uint32_t lid = e.hid(o, "layout");
    auto el = e.list(o, "entries", true);
    std::vector<WGPUBindGroupEntry> ents;
    std::vector<WGPUTextureView> temps;
    std::vector<uint32_t> uses;
    const char *why = nullptr;
    for (uint32_t i = 0; i < el->Length() && !e.bad; i++) {
        v8::Local<v8::Value> eo = e.at(el, i);
        WGPUBindGroupEntry be = WGPU_BIND_GROUP_ENTRY_INIT;
        be.binding = e.u32(eo, "binding", 0, true);
        v8::Local<v8::Value> r = e.get(eo, "resource");
        if (e.bad) break;
        if (r->IsUndefined()) { e.type_error("Required member is undefined: resource"); break; }
        uint32_t rid = e.hid(r);
        H *rh = rid ? hget(c, rid, hraw(rid) ? hraw(rid)->kind : K_NONE) : nullptr;
        uint32_t bid = 0;
        if (rh && rh->kind == K_SAMPLER) { if (!rh->p) why = "A sampler is invalid"; be.sampler = (WGPUSampler)rh->p; }
        else if (rh && rh->kind == K_VIEW) {
            if (!rh->p) why = "A texture view is invalid";
            be.textureView = (WGPUTextureView)rh->p;
            uses.insert(uses.end(), rh->uses.begin(), rh->uses.end());
        } else if (rh && rh->kind == K_TEXTURE) {   /* a texture binds its default view */
            if (!rh->p) why = "A texture is invalid";
            else {
                WGPUTextureView v = wgpuTextureCreateView((WGPUTexture)rh->p, nullptr);
                temps.push_back(v); be.textureView = v;
            }
            uses.push_back(rid);
        } else if (rh && rh->kind == K_BUFFER) bid = rid;
        else if (!rh && r->IsObject() && !rid) {   /* GPUBufferBinding */
            bid = e.hid(r, "buffer");
            if (!bid && !e.bad) { e.type_error("Required member is undefined: buffer"); break; }
            be.offset = e.u64(r, "offset", 0);
            v8::Local<v8::Value> sz = e.get(r, "size");
            be.size = sz->IsUndefined() ? WGPU_WHOLE_SIZE : e.u64v(sz, 0);
            if (!sz->IsUndefined() && be.size == 0) why = "A buffer binding cannot be empty";
        } else why = "A bind group resource is invalid";
        if (bid) {
            H *b = hget(c, bid, K_BUFFER);
            if (!b || !b->p) why = "A buffer is invalid";
            else be.buffer = (WGPUBuffer)b->p;
            uses.push_back(bid);
        }
        ents.push_back(be);
    }
    H *l = hget(c, lid, K_BGL);
    if (!e.bad && !lid) e.type_error("Required member is undefined: layout");
    if (e.bad || !d) { for (auto v : temps) wgpuTextureViewRelease(v); return; }
    uint32_t id;
    if (!l || !l->p) why = "The bind group layout is invalid";
    if (why) id = invalid(e, K_BG, did, why);
    else {
        WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bd.layout = (WGPUBindGroupLayout)l->p; bd.entryCount = ents.size(); bd.entries = ents.data();
        Cap cap;
        WGPUBindGroup g = wgpuDeviceCreateBindGroup((WGPUDevice)d->p, &bd);
        id = finish_new(e, K_BG, g, did, cap);
    }
    for (auto v : temps) wgpuTextureViewRelease(v);
    if (H *g = hraw(id)) {
        std::sort(uses.begin(), uses.end());
        uses.erase(std::unique(uses.begin(), uses.end()), uses.end());
        g->uses = std::move(uses);
    }
    RET(id);
}
FN(createShaderModule) {   /* (device, {code}) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> cv = e.get(ARG(1), "code");
    if (!e.bad && cv->IsUndefined()) e.type_error("Required member is undefined: code");
    std::string code = e.str(cv);
    if (e.bad || !d) return;
    WGPUShaderSourceWGSL src = WGPU_SHADER_SOURCE_WGSL_INIT;
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = view_of(code);
    WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    sd.nextInChain = &src.chain;
    Cap cap;
    WGPUShaderModule m = wgpuDeviceCreateShaderModule((WGPUDevice)d->p, &sd);
    std::string err = cap.has ? cap.msg : std::string();
    uint32_t id = finish_new(e, K_SHADER, m, did, cap);
    if (H *h = hraw(id)) h->err = err.empty() ? std::string() : err;
    RET(id);
}
FN(shaderInfo) {   /* (shader): its compilation error, "" when none */
    GPUC;
    H *h = hget(c, ID(0), K_SHADER);
    if (h) RET(jstr(iso, h->err.c_str(), (int)h->err.size()));
}

/* a programmable stage: module, entry point and constants, kept alive in st */
struct Stage { std::string entry; std::vector<std::string> keys; std::vector<WGPUConstantEntry> consts; };
WGPUShaderModule stage(Env &e, v8::Local<v8::Value> o, Stage &st, WGPUStringView *entry, bool *ok) {
    uint32_t mid = e.hid(o, "module");
    if (!mid && !e.bad) { e.type_error("Required member is undefined: module"); return nullptr; }
    v8::Local<v8::Value> ep = e.get(o, "entryPoint");
    if (!ep->IsUndefined()) { st.entry = e.str(ep); *entry = view_of(st.entry); } else *entry = NO_STR;
    v8::Local<v8::Value> co = e.get(o, "constants");
    if (!e.bad && co->IsObject()) {
        v8::Local<v8::Array> names;
        if (!co.As<v8::Object>()->GetOwnPropertyNames(e.ctx).ToLocal(&names)) { e.bad = true; return nullptr; }
        st.keys.reserve(names->Length());
        for (uint32_t i = 0; i < names->Length() && !e.bad; i++) {
            v8::Local<v8::Value> k = e.at(names, i);
            st.keys.push_back(e.str(k));
            v8::Local<v8::Value> v;
            if (!co.As<v8::Object>()->Get(e.ctx, k).ToLocal(&v)) { e.bad = true; break; }
            double d = e.num(v, 0);
            if (!std::isfinite(d)) { e.type_error("A pipeline constant is not finite"); break; }
            st.consts.push_back(WGPUConstantEntry{ nullptr, NO_STR, d });
        }
        for (size_t i = 0; i < st.consts.size(); i++) st.consts[i].key = view_of(st.keys[i]);
    }
    H *m = hget(e.c, mid, K_SHADER);
    if (!m || !m->p) { *ok = false; return nullptr; }
    return (WGPUShaderModule)m->p;
}
/* layout: "auto" or a GPUPipelineLayout */
WGPUPipelineLayout layout_of(Env &e, v8::Local<v8::Value> o, bool *ok) {
    v8::Local<v8::Value> l = e.get(o, "layout");
    if (e.bad) return nullptr;
    if (l->IsString()) {
        if (e.str(l) != "auto") e.type_error("The layout must be a GPUPipelineLayout or \"auto\"");
        return nullptr;
    }
    if (l->IsUndefined()) { e.type_error("Required member is undefined: layout"); return nullptr; }
    H *p = hget(e.c, e.hid(l), K_PL);
    if (!p || !p->p) { *ok = false; return nullptr; }
    return (WGPUPipelineLayout)p->p;
}
WGPUBlendComponent blend_comp(Env &e, v8::Local<v8::Value> o) {
    WGPUBlendComponent b;
    b.operation = (WGPUBlendOperation)e.en(o, "operation", "BlendOperation", WGPUBlendOperation_Add);
    b.srcFactor = (WGPUBlendFactor)e.en(o, "srcFactor", "BlendFactor", WGPUBlendFactor_One);
    b.dstFactor = (WGPUBlendFactor)e.en(o, "dstFactor", "BlendFactor", WGPUBlendFactor_Zero);
    return b;
}
WGPUStencilFaceState stencil_face(Env &e, v8::Local<v8::Value> o) {
    WGPUStencilFaceState s;
    s.compare = (WGPUCompareFunction)e.en(o, "compare", "CompareFunction", WGPUCompareFunction_Always);
    s.failOp = (WGPUStencilOperation)e.en(o, "failOp", "StencilOperation", WGPUStencilOperation_Keep);
    s.depthFailOp = (WGPUStencilOperation)e.en(o, "depthFailOp", "StencilOperation", WGPUStencilOperation_Keep);
    s.passOp = (WGPUStencilOperation)e.en(o, "passOp", "StencilOperation", WGPUStencilOperation_Keep);
    return s;
}
/* a pipeline that was made, or its error: async creation rejects with [message, internal] instead of erring */
void pipeline_done(const FCI &a, Env &e, Kind k, void *p, uint32_t did, Cap &cap, bool async) {
    if (async && cap.has) {
        release_ptr(k, p);
        v8::Local<v8::Value> v[2] = { jstr(e.iso, cap.msg.c_str(), (int)cap.msg.size()), v8::Boolean::New(e.iso, filter_of(cap.type) != 0) };
        cap.has = false;
        RET(v8::Array::New(e.iso, v, 2));
        return;
    }
    RET(finish_new(e, k, p, did, cap));
}
void pipeline_refused(const FCI &a, Env &e, Kind k, uint32_t did, const char *why, bool async) {
    if (async) {
        v8::Local<v8::Value> v[2] = { jstr(e.iso, why), v8::False(e.iso) };
        RET(v8::Array::New(e.iso, v, 2));
        return;
    }
    RET(invalid(e, k, did, why));
}
FN(createRenderPipeline) {   /* (device, descriptor, async) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> o = ARG(1);
    bool async = ARG(2)->BooleanValue(iso);
    bool ok = true;
    const char *why = nullptr;
    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.layout = layout_of(e, o, &ok);
    /* vertex */
    v8::Local<v8::Value> vo = e.get(o, "vertex");
    if (!e.bad && vo->IsUndefined()) e.type_error("Required member is undefined: vertex");
    Stage vs, fs;
    pd.vertex.module = stage(e, vo, vs, &pd.vertex.entryPoint, &ok);
    pd.vertex.constantCount = vs.consts.size(); pd.vertex.constants = vs.consts.data();
    auto bl = e.list(vo, "buffers", true);
    std::vector<WGPUVertexBufferLayout> bufs(bl->Length());
    std::vector<std::vector<WGPUVertexAttribute>> attrs(bl->Length());
    for (uint32_t i = 0; i < bl->Length() && !e.bad; i++) {
        v8::Local<v8::Value> b = e.at(bl, i);
        WGPUVertexBufferLayout &L = bufs[i];
        L = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        L.stepMode = WGPUVertexStepMode_Vertex;   /* an unused slot too: wgpu-native wants a step mode */
        if (b->IsNullOrUndefined()) continue;
        L.arrayStride = e.u64(b, "arrayStride", 0, true);
        L.stepMode = (WGPUVertexStepMode)e.en(b, "stepMode", "VertexStepMode", WGPUVertexStepMode_Vertex);
        auto al = e.list(b, "attributes", true);
        for (uint32_t j = 0; j < al->Length() && !e.bad; j++) {
            v8::Local<v8::Value> at = e.at(al, j);
            WGPUVertexAttribute A = WGPU_VERTEX_ATTRIBUTE_INIT;
            A.format = (WGPUVertexFormat)e.en(at, "format", "VertexFormat", 0, true);
            A.offset = e.u64(at, "offset", 0, true);
            A.shaderLocation = e.u32(at, "shaderLocation", 0, true);
            attrs[i].push_back(A);
        }
        L.attributeCount = attrs[i].size(); L.attributes = attrs[i].data();
    }
    pd.vertex.bufferCount = bufs.size(); pd.vertex.buffers = bufs.data();
    /* primitive */
    v8::Local<v8::Value> po = e.get(o, "primitive");
    pd.primitive.topology = (WGPUPrimitiveTopology)e.en(po, "topology", "PrimitiveTopology", WGPUPrimitiveTopology_TriangleList);
    pd.primitive.stripIndexFormat = (WGPUIndexFormat)e.en(po, "stripIndexFormat", "IndexFormat", WGPUIndexFormat_Undefined);
    pd.primitive.frontFace = (WGPUFrontFace)e.en(po, "frontFace", "FrontFace", WGPUFrontFace_CCW);
    pd.primitive.cullMode = (WGPUCullMode)e.en(po, "cullMode", "CullMode", WGPUCullMode_None);
    pd.primitive.unclippedDepth = e.flag(po, "unclippedDepth", false);
    /* depth and stencil */
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    v8::Local<v8::Value> dso = e.get(o, "depthStencil");
    if (!e.bad && !dso->IsUndefined()) {
        ds.format = (WGPUTextureFormat)e.en(dso, "format", "TextureFormat", 0, true);
        v8::Local<v8::Value> dw = e.get(dso, "depthWriteEnabled");
        ds.depthWriteEnabled = dw->IsUndefined() ? WGPUOptionalBool_Undefined : dw->BooleanValue(iso) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        ds.depthCompare = (WGPUCompareFunction)e.en(dso, "depthCompare", "CompareFunction", WGPUCompareFunction_Undefined);
        ds.stencilFront = stencil_face(e, e.get(dso, "stencilFront"));
        ds.stencilBack = stencil_face(e, e.get(dso, "stencilBack"));
        ds.stencilReadMask = e.u32(dso, "stencilReadMask", 0xffffffffu);
        ds.stencilWriteMask = e.u32(dso, "stencilWriteMask", 0xffffffffu);
        ds.depthBias = (int32_t)e.num(dso, "depthBias", 0);
        ds.depthBiasSlopeScale = (float)e.num(dso, "depthBiasSlopeScale", 0);
        ds.depthBiasClamp = (float)e.num(dso, "depthBiasClamp", 0);
        if (!e.bad) {
            if (fmt_has_depth(ds.format)) {
                if (ds.depthWriteEnabled == WGPUOptionalBool_Undefined) why = "depthWriteEnabled is required for a depth format";
            } else if (ds.format != WGPUTextureFormat_Stencil8) why = "The depth-stencil format is not a depth or stencil format";
            else if (ds.depthWriteEnabled == WGPUOptionalBool_True) why = "depthWriteEnabled needs a depth format";
            if (ds.depthWriteEnabled == WGPUOptionalBool_Undefined) ds.depthWriteEnabled = WGPUOptionalBool_False;
        }
        pd.depthStencil = &ds;
    }
    /* multisample */
    v8::Local<v8::Value> mo = e.get(o, "multisample");
    pd.multisample.count = e.u32(mo, "count", 1);
    pd.multisample.mask = e.u32(mo, "mask", 0xffffffffu);
    pd.multisample.alphaToCoverageEnabled = e.flag(mo, "alphaToCoverageEnabled", false);
    /* fragment */
    WGPUFragmentState fst = WGPU_FRAGMENT_STATE_INIT;
    v8::Local<v8::Value> fo = e.get(o, "fragment");
    std::vector<WGPUColorTargetState> targets;
    std::vector<WGPUBlendState> blends;
    if (!e.bad && !fo->IsUndefined()) {
        fst.module = stage(e, fo, fs, &fst.entryPoint, &ok);
        fst.constantCount = fs.consts.size(); fst.constants = fs.consts.data();
        auto tl = e.list(fo, "targets", true);
        targets.resize(tl->Length()); blends.resize(tl->Length());
        for (uint32_t i = 0; i < tl->Length() && !e.bad; i++) {
            v8::Local<v8::Value> t = e.at(tl, i);
            WGPUColorTargetState &T = targets[i];
            T = WGPU_COLOR_TARGET_STATE_INIT;
            if (t->IsNullOrUndefined()) { T.format = WGPUTextureFormat_Undefined; T.writeMask = 0; continue; }
            T.format = (WGPUTextureFormat)e.en(t, "format", "TextureFormat", 0, true);
            uint64_t wm = e.u64(t, "writeMask", 0xF);
            if (wm > 0xF) why = "A color write mask has unknown bits";
            T.writeMask = wm & 0xF;
            v8::Local<v8::Value> bo = e.get(t, "blend");
            if (!e.bad && !bo->IsUndefined()) {
                v8::Local<v8::Value> bc = e.get(bo, "color"), ba = e.get(bo, "alpha");
                if (!e.bad && (bc->IsUndefined() || ba->IsUndefined())) e.type_error("A blend state needs color and alpha");
                blends[i].color = blend_comp(e, bc); blends[i].alpha = blend_comp(e, ba);
                T.blend = &blends[i];
            }
        }
        fst.targetCount = targets.size(); fst.targets = targets.data();
        pd.fragment = &fst;
    }
    if (e.bad || !d) return;
    if (!ok) why = "A shader module or the pipeline layout is invalid";
    if (why) { pipeline_refused(a, e, K_RPIPE, did, why, async); return; }
    Cap cap;
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline((WGPUDevice)d->p, &pd);
    pipeline_done(a, e, K_RPIPE, p, did, cap, async);
}
FN(createComputePipeline) {   /* (device, {layout, compute}, async) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> o = ARG(1);
    bool async = ARG(2)->BooleanValue(iso);
    bool ok = true;
    WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pd.layout = layout_of(e, o, &ok);
    v8::Local<v8::Value> co = e.get(o, "compute");
    if (!e.bad && co->IsUndefined()) e.type_error("Required member is undefined: compute");
    Stage cs;
    pd.compute.module = stage(e, co, cs, &pd.compute.entryPoint, &ok);
    pd.compute.constantCount = cs.consts.size(); pd.compute.constants = cs.consts.data();
    if (e.bad || !d) return;
    if (!ok) { pipeline_refused(a, e, K_CPIPE, did, "A shader module or the pipeline layout is invalid", async); return; }
    Cap cap;
    WGPUComputePipeline p = wgpuDeviceCreateComputePipeline((WGPUDevice)d->p, &pd);
    pipeline_done(a, e, K_CPIPE, p, did, cap, async);
}
FN(getBindGroupLayout) {   /* (pipeline, index) */
    GPUC;
    uint32_t pid = ID(0);
    H *p = hraw(pid);
    if (!p || p->owner != c || (p->kind != K_RPIPE && p->kind != K_CPIPE)) return;
    uint32_t idx = au(a, 1);
    if (!p->p) { RET(invalid(e, K_BGL, p->dev, "The pipeline is invalid")); return; }
    Cap cap;
    WGPUBindGroupLayout l = p->kind == K_RPIPE ? wgpuRenderPipelineGetBindGroupLayout((WGPURenderPipeline)p->p, idx)
                                               : wgpuComputePipelineGetBindGroupLayout((WGPUComputePipeline)p->p, idx);
    if (!l) { cap.has = false; RET(invalid(e, K_BGL, p->dev, "The bind group index is out of range")); return; }
    RET(finish_new(e, K_BGL, l, p->dev, cap));
}
FN(createQuerySet) {   /* (device, {type, count}) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    v8::Local<v8::Value> o = ARG(1);
    WGPUQuerySetDescriptor qd = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    qd.type = (WGPUQueryType)e.en(o, "type", "QueryType", 0, true);
    qd.count = e.u32(o, "count", 0, true);
    if (e.bad || !d) return;
    Cap cap;
    WGPUQuerySet q = wgpuDeviceCreateQuerySet((WGPUDevice)d->p, &qd);
    RET(finish_new(e, K_QS, q, did, cap));
}

/* ---- command encoding ---- */

FN(createCommandEncoder) {   /* (device) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    if (!d) return;
    Cap cap;
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder((WGPUDevice)d->p, nullptr);
    RET(finish_new(e, K_ENC, enc, did, cap));
}
/* the encoder a call records into, or nullptr after a validation error when it may not record any more */
H *encoder(Env &e, uint32_t id) {
    H *enc = hget(e.c, id, K_ENC);
    if (!enc) return nullptr;
    if (enc->consumed) { verr(enc->dev, "The command encoder has finished"); return nullptr; }
    if (enc->parent) { enc->err = enc->err.empty() ? "The command encoder is locked by an open pass" : enc->err; return nullptr; }
    if (!enc->p) return nullptr;
    return enc;
}
void enc_fail(H *enc, const char *msg) { if (enc && enc->err.empty()) enc->err = msg; }

struct ColorAtt { std::vector<WGPURenderPassColorAttachment> v; };
FN(beginRenderPass) {   /* (encoder, descriptor): a pass id; an ended encoder gives an invalid pass */
    GPUC;
    uint32_t eid = ID(0);
    H *enc = hget(c, eid, K_ENC);
    if (!enc) return;
    v8::Local<v8::Value> o = ARG(1);
    std::vector<WGPURenderPassColorAttachment> cas;
    bool ok = true;
    std::vector<uint32_t> uses;
    auto cl = e.list(o, "colorAttachments", true);
    if (!e.bad && e.get(o, "colorAttachments")->IsUndefined()) e.type_error("Required member is undefined: colorAttachments");
    for (uint32_t i = 0; i < cl->Length() && !e.bad; i++) {
        v8::Local<v8::Value> ca = e.at(cl, i);
        WGPURenderPassColorAttachment A = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        if (ca->IsNullOrUndefined()) { cas.push_back(A); continue; }
        uint32_t vid = e.hid(ca, "view");
        if (!vid && !e.bad) { e.type_error("Required member is undefined: view"); break; }
        A.view = (WGPUTextureView)need(e, vid, K_VIEW, &ok);
        uses.push_back(vid);
        uint32_t rid = e.hid(ca, "resolveTarget");
        if (rid) { A.resolveTarget = (WGPUTextureView)need(e, rid, K_VIEW, &ok); uses.push_back(rid); }
        v8::Local<v8::Value> dsl = e.get(ca, "depthSlice");
        A.depthSlice = dsl->IsUndefined() ? WGPU_DEPTH_SLICE_UNDEFINED : e.u32v(dsl, 0);
        A.clearValue = e.color(e.get(ca, "clearValue"));
        A.loadOp = (WGPULoadOp)e.en(ca, "loadOp", "LoadOp", 0, true);
        A.storeOp = (WGPUStoreOp)e.en(ca, "storeOp", "StoreOp", 0, true);
        cas.push_back(A);
    }
    WGPURenderPassDepthStencilAttachment dsa = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    v8::Local<v8::Value> dso = e.get(o, "depthStencilAttachment");
    bool has_ds = !e.bad && !dso->IsUndefined();
    if (has_ds) {
        uint32_t vid = e.hid(dso, "view");
        if (!vid && !e.bad) e.type_error("Required member is undefined: view");
        dsa.view = (WGPUTextureView)need(e, vid, K_VIEW, &ok);
        uses.push_back(vid);
        dsa.depthClearValue = (float)e.num(dso, "depthClearValue", NAN);
        dsa.depthLoadOp = (WGPULoadOp)e.en(dso, "depthLoadOp", "LoadOp", WGPULoadOp_Undefined);
        dsa.depthStoreOp = (WGPUStoreOp)e.en(dso, "depthStoreOp", "StoreOp", WGPUStoreOp_Undefined);
        dsa.depthReadOnly = e.flag(dso, "depthReadOnly", false);
        dsa.stencilClearValue = e.u32(dso, "stencilClearValue", 0);
        dsa.stencilLoadOp = (WGPULoadOp)e.en(dso, "stencilLoadOp", "LoadOp", WGPULoadOp_Undefined);
        dsa.stencilStoreOp = (WGPUStoreOp)e.en(dso, "stencilStoreOp", "StoreOp", WGPUStoreOp_Undefined);
        dsa.stencilReadOnly = e.flag(dso, "stencilReadOnly", false);
    }
    uint32_t oq = e.hid(o, "occlusionQuerySet");
    WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    v8::Local<v8::Value> two = e.get(o, "timestampWrites");
    uint32_t tq = 0;
    if (!e.bad && !two->IsUndefined()) {
        tq = e.hid(two, "querySet");
        tw.beginningOfPassWriteIndex = e.u32(two, "beginningOfPassWriteIndex", WGPU_QUERY_SET_INDEX_UNDEFINED);
        tw.endOfPassWriteIndex = e.u32(two, "endOfPassWriteIndex", WGPU_QUERY_SET_INDEX_UNDEFINED);
    }
    if (e.bad) return;
    WGPURenderPassDescriptor rd = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rd.colorAttachmentCount = cas.size(); rd.colorAttachments = cas.data();
    if (has_ds) rd.depthStencilAttachment = &dsa;
    if (oq) { rd.occlusionQuerySet = (WGPUQuerySet)need(e, oq, K_QS, &ok); uses.push_back(oq); }
    if (tq) { tw.querySet = (WGPUQuerySet)need(e, tq, K_QS, &ok); rd.timestampWrites = &tw; uses.push_back(tq); }
    else if (!two->IsUndefined()) ok = false;
    H *en = encoder(e, eid);
    uint32_t pid = hnew(K_RPASS, c, nullptr, enc->dev);
    H *ps = hraw(pid);
    if (!ps) return;
    ps->parent = eid;
    if (en) {
        if (!ok) enc_fail(en, "A render pass attachment or query set is invalid");
        else {
            for (uint32_t u : uses) add_use(en, u);
            ps->p = wgpuCommandEncoderBeginRenderPass((WGPUCommandEncoder)en->p, &rd);
        }
        en->parent = pid;
    }
    RET(pid);
}
FN(beginComputePass) {   /* (encoder, {timestampWrites}) */
    GPUC;
    uint32_t eid = ID(0);
    H *enc = hget(c, eid, K_ENC);
    if (!enc) return;
    v8::Local<v8::Value> o = ARG(1);
    WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    v8::Local<v8::Value> two = e.get(o, "timestampWrites");
    uint32_t tq = 0;
    bool ok = true;
    if (!e.bad && !two->IsUndefined()) {
        tq = e.hid(two, "querySet");
        tw.beginningOfPassWriteIndex = e.u32(two, "beginningOfPassWriteIndex", WGPU_QUERY_SET_INDEX_UNDEFINED);
        tw.endOfPassWriteIndex = e.u32(two, "endOfPassWriteIndex", WGPU_QUERY_SET_INDEX_UNDEFINED);
        tw.querySet = (WGPUQuerySet)need(e, tq, K_QS, &ok);
    }
    if (e.bad) return;
    WGPUComputePassDescriptor cd = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    if (tq) cd.timestampWrites = &tw;
    H *en = encoder(e, eid);
    uint32_t pid = hnew(K_CPASS, c, nullptr, enc->dev);
    H *ps = hraw(pid);
    if (!ps) return;
    ps->parent = eid;
    if (en) {
        if (!ok) enc_fail(en, "The timestamp query set is invalid");
        else { if (tq) add_use(en, tq); ps->p = wgpuCommandEncoderBeginComputePass((WGPUCommandEncoder)en->p, &cd); }
        en->parent = pid;
    }
    RET(pid);
}
FN(copyBufferToBuffer) {   /* (encoder, src, srcOffset, dst, dstOffset, size) */
    GPUC;
    H *en = encoder(e, ID(0));
    if (!en) return;
    uint32_t sid = ID(1), did = ID(3);
    bool ok = true;
    WGPUBuffer s = (WGPUBuffer)need(e, sid, K_BUFFER, &ok), d = (WGPUBuffer)need(e, did, K_BUFFER, &ok);
    if (!ok) { enc_fail(en, "A copy buffer is invalid"); return; }
    add_use(en, sid); add_use(en, did);
    wgpuCommandEncoderCopyBufferToBuffer((WGPUCommandEncoder)en->p, s, au64(a, 2), d, au64(a, 4), au64(a, 5));
}
bool buffer_info(Env &e, v8::Local<v8::Value> o, WGPUTexelCopyBufferInfo *bi, uint32_t *bid, bool *ok) {
    *bid = e.hid(o, "buffer");
    if (!*bid && !e.bad) { e.type_error("Required member is undefined: buffer"); return false; }
    bi->buffer = (WGPUBuffer)need(e, *bid, K_BUFFER, ok);
    bi->layout.offset = e.u64(o, "offset", 0);
    bi->layout.bytesPerRow = e.u32(o, "bytesPerRow", WGPU_COPY_STRIDE_UNDEFINED);
    bi->layout.rowsPerImage = e.u32(o, "rowsPerImage", WGPU_COPY_STRIDE_UNDEFINED);
    if (!bi->layout.bytesPerRow || !bi->layout.rowsPerImage) *ok = false;   /* wgpu-native panics on 0 */
    return !e.bad;
}
bool texture_info(Env &e, v8::Local<v8::Value> o, WGPUTexelCopyTextureInfo *ti, uint32_t *tid, bool *ok) {
    *tid = e.hid(o, "texture");
    if (!*tid && !e.bad) { e.type_error("Required member is undefined: texture"); return false; }
    ti->texture = (WGPUTexture)need(e, *tid, K_TEXTURE, ok);
    ti->mipLevel = e.u32(o, "mipLevel", 0);
    ti->origin = e.origin(e.get(o, "origin"));
    ti->aspect = (WGPUTextureAspect)e.en(o, "aspect", "TextureAspect", WGPUTextureAspect_All);
    return !e.bad;
}
FN(copyBufferToTexture) {   /* (encoder, source, destination, size) */
    GPUC;
    WGPUTexelCopyBufferInfo bi = {}; WGPUTexelCopyTextureInfo ti = {};
    uint32_t bid, tid; bool ok = true;
    if (!buffer_info(e, ARG(1), &bi, &bid, &ok) || !texture_info(e, ARG(2), &ti, &tid, &ok)) return;
    WGPUExtent3D sz = e.extent(ARG(3));
    if (e.bad) return;
    H *en = encoder(e, ID(0));
    if (!en) return;
    if (!ok) { enc_fail(en, "A copy buffer or texture is invalid"); return; }
    add_use(en, bid); add_use(en, tid);
    wgpuCommandEncoderCopyBufferToTexture((WGPUCommandEncoder)en->p, &bi, &ti, &sz);
}
FN(copyTextureToBuffer) {   /* (encoder, source, destination, size) */
    GPUC;
    WGPUTexelCopyBufferInfo bi = {}; WGPUTexelCopyTextureInfo ti = {};
    uint32_t bid, tid; bool ok = true;
    if (!texture_info(e, ARG(1), &ti, &tid, &ok) || !buffer_info(e, ARG(2), &bi, &bid, &ok)) return;
    WGPUExtent3D sz = e.extent(ARG(3));
    if (e.bad) return;
    H *en = encoder(e, ID(0));
    if (!en) return;
    if (!ok) { enc_fail(en, "A copy buffer or texture is invalid"); return; }
    add_use(en, bid); add_use(en, tid);
    wgpuCommandEncoderCopyTextureToBuffer((WGPUCommandEncoder)en->p, &ti, &bi, &sz);
}
FN(copyTextureToTexture) {   /* (encoder, source, destination, size) */
    GPUC;
    WGPUTexelCopyTextureInfo si = {}, di = {};
    uint32_t sid, did; bool ok = true;
    if (!texture_info(e, ARG(1), &si, &sid, &ok) || !texture_info(e, ARG(2), &di, &did, &ok)) return;
    WGPUExtent3D sz = e.extent(ARG(3));
    if (e.bad) return;
    H *en = encoder(e, ID(0));
    if (!en) return;
    if (!ok) { enc_fail(en, "A copy texture is invalid"); return; }
    add_use(en, sid); add_use(en, did);
    wgpuCommandEncoderCopyTextureToTexture((WGPUCommandEncoder)en->p, &si, &di, &sz);
}
FN(clearBuffer) {   /* (encoder, buffer, offset, size | undefined) */
    GPUC;
    H *en = encoder(e, ID(0));
    if (!en) return;
    uint32_t bid = ID(1);
    bool ok = true;
    WGPUBuffer b = (WGPUBuffer)need(e, bid, K_BUFFER, &ok);
    if (!ok) { enc_fail(en, "The buffer is invalid"); return; }
    uint64_t size = ARG(3)->IsUndefined() ? WGPU_WHOLE_SIZE : au64(a, 3);
    if (!size) return;
    add_use(en, bid);
    wgpuCommandEncoderClearBuffer((WGPUCommandEncoder)en->p, b, au64(a, 2), size);
}
FN(resolveQuerySet) {   /* (encoder, querySet, first, count, destination, destinationOffset) */
    GPUC;
    H *en = encoder(e, ID(0));
    if (!en) return;
    uint32_t qid = ID(1), bid = ID(4);
    bool ok = true;
    WGPUQuerySet q = (WGPUQuerySet)need(e, qid, K_QS, &ok);
    WGPUBuffer b = (WGPUBuffer)need(e, bid, K_BUFFER, &ok);
    if (!ok) { enc_fail(en, "The query set or buffer is invalid"); return; }
    add_use(en, qid); add_use(en, bid);
    wgpuCommandEncoderResolveQuerySet((WGPUCommandEncoder)en->p, q, au(a, 2), au(a, 3), b, au64(a, 5));
}
FN(finish) {   /* (encoder): a command buffer; one that may not be submitted when the encoding failed */
    GPUC;
    uint32_t eid = ID(0);
    H *en = hget(c, eid, K_ENC);
    if (!en) return;
    uint32_t did = en->dev;
    if (en->consumed) { RET(invalid(e, K_CB, did, "The command encoder has already finished")); return; }
    en->consumed = true;
    std::string err = en->parent ? "A pass is still open" : en->err;
    uint32_t id;
    if (!en->p) id = invalid(e, K_CB, did, nullptr);
    else if (!err.empty()) id = invalid(e, K_CB, did, err.c_str());
    else {
        Cap cap;
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish((WGPUCommandEncoder)en->p, nullptr);
        id = finish_new(e, K_CB, cb, did, cap);
    }
    if (H *cb = hraw(id)) cb->uses = std::move(en->uses);
    release_ptr(K_ENC, en->p);
    en->p = nullptr;
    RET(id);
}

/* ---- passes ---- */

H *pass(Env &e, const FCI &a, Kind k) {
    H *ps = hget(e.c, e.hid(a.Length() ? a[0] : v8::Local<v8::Value>(v8::Undefined(e.iso))), k);
    if (!ps) return nullptr;
    if (ps->ended) { verr(ps->dev, "The pass has ended"); return nullptr; }
    return ps->p ? ps : nullptr;
}
H *pass_enc(H *ps) { return hraw(ps->parent); }
#define RP H *ps = pass(e, a, K_RPASS); if (!ps) return; WGPURenderPassEncoder rp = (WGPURenderPassEncoder)ps->p; (void)rp
#define CP H *ps = pass(e, a, K_CPASS); if (!ps) return; WGPUComputePassEncoder cp = (WGPUComputePassEncoder)ps->p; (void)cp

std::vector<uint32_t> offsets(Env &e, const FCI &a, int i) {   /* (offsets, start, length): an array or a Uint32Array range */
    std::vector<uint32_t> out;
    if (i >= a.Length() || a[i]->IsNullOrUndefined()) return out;
    if (a[i]->IsUint32Array()) {
        auto ta = a[i].As<v8::Uint32Array>();
        size_t n = ta->Length(), st = (size_t)au64(a, i + 1), len = i + 2 < a.Length() && !a[i + 2]->IsUndefined() ? (size_t)au64(a, i + 2) : n - std::min(st, n);
        if (st > n || len > n - st) { e.range_error("The dynamic offsets range is outside the array"); return out; }
        out.resize(len);
        const uint32_t *p = (const uint32_t *)((const char *)ta->Buffer()->Data() + ta->ByteOffset());
        for (size_t k = 0; k < len; k++) out[k] = p[st + k];
        return out;
    }
    auto arr = e.list(a[i], "dynamicOffsets");
    for (uint32_t k = 0; k < arr->Length() && !e.bad; k++) out.push_back(e.u32v(e.at(arr, k), 0));
    return out;
}
FN(setPipeline) {   /* (pass, pipeline) */
    GPUC;
    uint32_t pid = ID(1);
    H *ps = hget(c, ID(0), K_RPASS) ? pass(e, a, K_RPASS) : pass(e, a, K_CPASS);
    if (!ps) return;
    H *p = hget(c, pid, ps->kind == K_RPASS ? K_RPIPE : K_CPIPE);
    if (!p || !p->p) { enc_fail(pass_enc(ps), "The pipeline is invalid"); return; }
    if (ps->kind == K_RPASS) wgpuRenderPassEncoderSetPipeline((WGPURenderPassEncoder)ps->p, (WGPURenderPipeline)p->p);
    else wgpuComputePassEncoderSetPipeline((WGPUComputePassEncoder)ps->p, (WGPUComputePipeline)p->p);
}
FN(setBindGroup) {   /* (pass, index, group | null, offsets, start, length) */
    GPUC;
    H *ps = hget(c, ID(0), K_RPASS) ? pass(e, a, K_RPASS) : pass(e, a, K_CPASS);
    if (!ps) return;
    uint32_t gid = ID(2);
    WGPUBindGroup g = nullptr;
    if (!ARG(2)->IsNull()) {
        H *bg = hget(c, gid, K_BG);
        if (!bg || !bg->p) { enc_fail(pass_enc(ps), "The bind group is invalid"); return; }
        g = (WGPUBindGroup)bg->p;
        if (H *en = pass_enc(ps)) add_use(en, gid);
    }
    std::vector<uint32_t> offs = offsets(e, a, 3);
    if (e.bad) return;
    if (ps->kind == K_RPASS) wgpuRenderPassEncoderSetBindGroup((WGPURenderPassEncoder)ps->p, au(a, 1), g, offs.size(), offs.data());
    else wgpuComputePassEncoderSetBindGroup((WGPUComputePassEncoder)ps->p, au(a, 1), g, offs.size(), offs.data());
}
FN(setVertexBuffer) {   /* (pass, slot, buffer | null, offset, size | undefined) */
    GPUC; RP;
    uint64_t size = ARG(4)->IsUndefined() ? WGPU_WHOLE_SIZE : au64(a, 4);
    if (!size) return;   /* wgpu-native panics on an empty binding */
    WGPUBuffer b = nullptr;
    if (!ARG(2)->IsNull()) {
        uint32_t bid = ID(2);
        H *bh = hget(c, bid, K_BUFFER);
        if (!bh || !bh->p) { enc_fail(pass_enc(ps), "The vertex buffer is invalid"); return; }
        b = (WGPUBuffer)bh->p;
        if (H *en = pass_enc(ps)) add_use(en, bid);
    }
    wgpuRenderPassEncoderSetVertexBuffer(rp, au(a, 1), b, au64(a, 3), size);
}
FN(setIndexBuffer) {   /* (pass, buffer, format, offset, size | undefined) */
    GPUC; RP;
    int fmt = e.en_v(ARG(2), "IndexFormat", 0, true);
    if (e.bad) return;
    uint64_t size = ARG(4)->IsUndefined() ? WGPU_WHOLE_SIZE : au64(a, 4);
    if (!size) return;
    uint32_t bid = ID(1);
    H *bh = hget(c, bid, K_BUFFER);
    if (!bh || !bh->p) { enc_fail(pass_enc(ps), "The index buffer is invalid"); return; }
    if (H *en = pass_enc(ps)) add_use(en, bid);
    wgpuRenderPassEncoderSetIndexBuffer(rp, (WGPUBuffer)bh->p, (WGPUIndexFormat)fmt, au64(a, 3), size);
}
FN(draw) { GPUC; RP; wgpuRenderPassEncoderDraw(rp, au(a, 1), au(a, 2), au(a, 3), au(a, 4)); }   /* (pass, vertices, instances, firstVertex, firstInstance) */
FN(drawIndexed) {   /* (pass, indices, instances, firstIndex, baseVertex, firstInstance) */
    GPUC; RP;
    double bv = an(a, 4);
    wgpuRenderPassEncoderDrawIndexed(rp, au(a, 1), au(a, 2), au(a, 3), std::isfinite(bv) ? (int32_t)std::clamp(bv, -2147483648.0, 2147483647.0) : 0, au(a, 5));
}
H *indirect(Env &e, H *ps, uint32_t bid) {
    H *b = hget(e.c, bid, K_BUFFER);
    if (!b || !b->p) { enc_fail(pass_enc(ps), "The indirect buffer is invalid"); return nullptr; }
    if (H *en = pass_enc(ps)) add_use(en, bid);
    return b;
}
FN(drawIndirect) { GPUC; RP; if (H *b = indirect(e, ps, ID(1))) wgpuRenderPassEncoderDrawIndirect(rp, (WGPUBuffer)b->p, au64(a, 2)); }
FN(drawIndexedIndirect) { GPUC; RP; if (H *b = indirect(e, ps, ID(1))) wgpuRenderPassEncoderDrawIndexedIndirect(rp, (WGPUBuffer)b->p, au64(a, 2)); }
FN(setViewport) { GPUC; RP; wgpuRenderPassEncoderSetViewport(rp, (float)an(a, 1), (float)an(a, 2), (float)an(a, 3), (float)an(a, 4), (float)an(a, 5), (float)an(a, 6)); }
FN(setScissorRect) { GPUC; RP; wgpuRenderPassEncoderSetScissorRect(rp, au(a, 1), au(a, 2), au(a, 3), au(a, 4)); }
FN(setBlendConstant) { GPUC; RP; WGPUColor col = e.color(ARG(1)); if (!e.bad) wgpuRenderPassEncoderSetBlendConstant(rp, &col); }
FN(setStencilReference) { GPUC; RP; wgpuRenderPassEncoderSetStencilReference(rp, au(a, 1)); }
FN(beginOcclusionQuery) { GPUC; RP; wgpuRenderPassEncoderBeginOcclusionQuery(rp, au(a, 1)); }
FN(endOcclusionQuery) { GPUC; RP; wgpuRenderPassEncoderEndOcclusionQuery(rp); }
FN(dispatchWorkgroups) { GPUC; CP; wgpuComputePassEncoderDispatchWorkgroups(cp, au(a, 1), au(a, 2), au(a, 3)); }
FN(dispatchWorkgroupsIndirect) { GPUC; CP; if (H *b = indirect(e, ps, ID(1))) wgpuComputePassEncoderDispatchWorkgroupsIndirect(cp, (WGPUBuffer)b->p, au64(a, 2)); }
FN(end) {   /* (pass) */
    GPUC;
    uint32_t pid = ID(0);
    H *ps = hget(c, pid, K_RPASS);
    if (!ps) ps = hget(c, pid, K_CPASS);
    if (!ps) return;
    if (ps->ended) { verr(ps->dev, "The pass has already ended"); return; }
    ps->ended = true;
    H *en = hraw(ps->parent);
    if (en && en->kind == K_ENC && en->parent == pid) en->parent = 0;
    if (!ps->p) return;
    if (ps->kind == K_RPASS) wgpuRenderPassEncoderEnd((WGPURenderPassEncoder)ps->p);
    else wgpuComputePassEncoderEnd((WGPUComputePassEncoder)ps->p);
    release_ptr(ps->kind, ps->p);
    ps->p = nullptr;
}

/* ---- the queue ---- */

/* why a command buffer may not run: it reaches a destroyed or mapped resource */
const char *blocked(uint32_t id, int depth) {
    H *h = hraw(id);
    if (!h) return nullptr;
    switch (h->kind) {
    case K_BUFFER:
        if (h->destroyed) return "A submitted buffer is destroyed";
        if (h->map_state) return "A submitted buffer is mapped";
        return nullptr;
    case K_TEXTURE: return h->destroyed ? "A submitted texture is destroyed" : nullptr;
    case K_QS: return h->destroyed ? "A submitted query set is destroyed" : nullptr;
    case K_VIEW: case K_BG:
        if (depth > 2) return nullptr;
        for (uint32_t u : h->uses) if (const char *w = blocked(u, depth + 1)) return w;
        return nullptr;
    default: return nullptr;
    }
}
FN(submit) {   /* (device, [commandBuffers]) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    auto l = e.list(ARG(1), "commandBuffers");
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < l->Length() && !e.bad; i++) ids.push_back(e.hid(e.at(l, i)));
    if (e.bad || !d) return;
    const char *why = nullptr;
    std::vector<WGPUCommandBuffer> cbs;
    for (uint32_t id : ids) {
        H *cb = hget(c, id, K_CB);
        if (!cb) { why = "A command buffer is invalid"; continue; }
        if (cb->consumed) why = "A command buffer was already submitted";
        else if (!cb->p || cb->bad) why = "A command buffer is invalid";
        else if (cb->dev != did) why = "A command buffer is from another device";
        else for (uint32_t u : cb->uses) if ((why = blocked(u, 0))) break;
        if (cb->p && !why) cbs.push_back((WGPUCommandBuffer)cb->p);
    }
    if (!why && !d->lost && !cbs.empty()) {
        Cap cap;
        wgpuQueueSubmit(queue_of(d), cbs.size(), cbs.data());
    } else if (why) verr(did, why);
    for (uint32_t id : ids)
        if (H *cb = hget(c, id, K_CB)) { cb->consumed = true; release_ptr(K_CB, cb->p); cb->p = nullptr; cb->uses.clear(); }
}
bool bytes_of(v8::Local<v8::Value> v, const char **p, size_t *n, size_t *elem) {
    if (v->IsArrayBufferView()) {
        auto vw = v.As<v8::ArrayBufferView>();
        *p = (const char *)vw->Buffer()->Data() + vw->ByteOffset(); *n = vw->ByteLength();
        *elem = 1;
        if (v->IsTypedArray()) *elem = *n ? *n / v.As<v8::TypedArray>()->Length() : 1;
        return true;
    }
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); *p = (const char *)ab->Data(); *n = ab->ByteLength(); *elem = 1; return true; }
    if (v->IsSharedArrayBuffer()) { auto ab = v.As<v8::SharedArrayBuffer>(); *p = (const char *)ab->Data(); *n = ab->ByteLength(); *elem = 1; return true; }
    return false;
}
FN(writeBuffer) {   /* (device, buffer, bufferOffset, data, dataOffset, size): offsets and size in elements of a typed array */
    GPUC;
    uint32_t did = ID(0), bid = ID(1);
    H *d = device(e, did);
    if (!d) return;
    const char *p; size_t n, elem;
    if (!bytes_of(ARG(3), &p, &n, &elem)) { e.type_error("The data is not a BufferSource"); return; }
    uint64_t off = au64(a, 4) * elem, size = ARG(5)->IsUndefined() ? (off <= n ? n - off : 0) : au64(a, 5) * elem;
    if (off > n || size > n - off) { iso->ThrowException(v8::Exception::Error(jstr(iso, "OperationError: the data range is outside the source"))); return; }
    if (size % 4) { iso->ThrowException(v8::Exception::Error(jstr(iso, "OperationError: the write size must be a multiple of 4"))); return; }
    H *b = hget(c, bid, K_BUFFER);
    if (!b || !b->p) { verr(did, "The buffer is invalid"); return; }
    if (d->lost || !size) return;
    wgpuQueueWriteBuffer(queue_of(d), (WGPUBuffer)b->p, au64(a, 2), p + off, (size_t)size);
}
FN(writeTexture) {   /* (device, destination, data, dataLayout, size) */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    WGPUTexelCopyTextureInfo ti = {};
    uint32_t tid; bool ok = true;
    if (!texture_info(e, ARG(1), &ti, &tid, &ok)) return;
    const char *p; size_t n, elem;
    if (!bytes_of(ARG(2), &p, &n, &elem)) { e.type_error("The data is not a BufferSource"); return; }
    v8::Local<v8::Value> lo = ARG(3);
    WGPUTexelCopyBufferLayout L;
    L.offset = e.u64(lo, "offset", 0);
    L.bytesPerRow = e.u32(lo, "bytesPerRow", WGPU_COPY_STRIDE_UNDEFINED);
    L.rowsPerImage = e.u32(lo, "rowsPerImage", WGPU_COPY_STRIDE_UNDEFINED);
    WGPUExtent3D sz = e.extent(ARG(4));
    if (e.bad || !d) return;
    if (!ok) { verr(did, "The texture is invalid"); return; }
    if (!L.bytesPerRow || !L.rowsPerImage) { verr(did, "bytesPerRow and rowsPerImage must not be 0"); return; }
    if (L.offset > n) { verr(did, "The data offset is outside the data"); return; }
    if (d->lost) return;
    wgpuQueueWriteTexture(queue_of(d), &ti, p, n, &L, &sz);
}
FN(onSubmittedWorkDone) {   /* (device): ticket */
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    if (!d) { RET(0); return; }
    Tk *t = ticket_new(c, did);
    uint32_t ticket = t->ticket;
    wgpuQueueOnSubmittedWorkDone(queue_of(d), WGPUQueueWorkDoneCallbackInfo{ nullptr, WGPUCallbackMode_AllowProcessEvents, on_done, t, nullptr });
    RET(ticket);
}
uint16_t half(float f) {
    uint32_t x; memcpy(&x, &f, 4);
    uint32_t s = (x >> 16) & 0x8000, ex = (x >> 23) & 0xff, m = x & 0x7fffff;
    if (ex < 113) return (uint16_t)s;
    if (ex > 142) return (uint16_t)(s | 0x7c00);
    return (uint16_t)(s | (ex - 112) << 10 | m >> 13);
}
/* (device, source, sourceW, sourceH, originX, originY, flipY, destination, premultiplied, width, height)
   source: an element (img, video, canvas, ImageBitmap's) or RGBA pixels (ImageData) of sourceW x sourceH */
FN(copyExternalImageToTexture) {
    GPUC;
    uint32_t did = ID(0);
    H *d = device(e, did);
    WGPUTexelCopyTextureInfo ti = {};
    uint32_t tid; bool ok = true;
    if (!texture_info(e, ARG(7), &ti, &tid, &ok)) return;
    if (!d) return;
    H *t = hget(c, tid, K_TEXTURE);
    if (!ok || !t) { verr(did, "The destination texture is invalid"); return; }
    bool premul = ARG(8)->BooleanValue(iso), flip = ARG(6)->BooleanValue(iso);
    int sw = 0, sh = 0;
    uint8_t *rgba = nullptr;
    if (Node *n = junwrap(ARG(1))) rgba = gl_node_rgba(n, &sw, &sh, premul);
    else {
        const char *p; size_t nb, elem;
        sw = (int)au(a, 2); sh = (int)au(a, 3);
        if (bytes_of(ARG(1), &p, &nb, &elem) && sw > 0 && sh > 0 && nb >= (size_t)sw * sh * 4) {
            rgba = (uint8_t *)malloc((size_t)sw * sh * 4);
            memcpy(rgba, p, (size_t)sw * sh * 4);
            if (premul) for (size_t i = 0; i < (size_t)sw * sh; i++) { uint8_t *q = rgba + i * 4; q[0] = (uint8_t)(q[0] * q[3] / 255); q[1] = (uint8_t)(q[1] * q[3] / 255); q[2] = (uint8_t)(q[2] * q[3] / 255); }
        }
    }
    uint32_t ox = au(a, 4), oy = au(a, 5), w = au(a, 9), h = au(a, 10);
    if (!rgba) { if (w && h) verr(did, "The source image is not ready"); return; }
    if (ox > (uint32_t)sw || oy > (uint32_t)sh || w > (uint32_t)sw - ox || h > (uint32_t)sh - oy) { free(rgba); iso->ThrowException(v8::Exception::Error(jstr(iso, "OperationError: the copy is outside the source"))); return; }
    int bpt = fmt_bytes(t->fmt);
    if (!bpt) { free(rgba); verr(did, "The destination format cannot take an external image"); return; }
    if (!w || !h || d->lost) { free(rgba); return; }
    std::vector<uint8_t> out((size_t)w * h * bpt);
    for (uint32_t y = 0; y < h; y++) {
        uint32_t sy = flip ? oy + h - 1 - y : oy + y;
        const uint8_t *s = rgba + ((size_t)sy * sw + ox) * 4;
        uint8_t *o = out.data() + (size_t)y * w * bpt;
        for (uint32_t x = 0; x < w; x++, s += 4) {
            switch (t->fmt) {
            case WGPUTextureFormat_R8Unorm: o[x] = s[0]; break;
            case WGPUTextureFormat_RG8Unorm: o[x * 2] = s[0]; o[x * 2 + 1] = s[1]; break;
            case WGPUTextureFormat_RGBA8Unorm: case WGPUTextureFormat_RGBA8UnormSrgb: memcpy(o + x * 4, s, 4); break;
            case WGPUTextureFormat_BGRA8Unorm: case WGPUTextureFormat_BGRA8UnormSrgb: o[x * 4] = s[2]; o[x * 4 + 1] = s[1]; o[x * 4 + 2] = s[0]; o[x * 4 + 3] = s[3]; break;
            case WGPUTextureFormat_RGB10A2Unorm: {
                uint32_t v = (uint32_t)(s[0] * 1023 / 255) | (uint32_t)(s[1] * 1023 / 255) << 10 | (uint32_t)(s[2] * 1023 / 255) << 20 | (uint32_t)(s[3] * 3 / 255) << 30;
                memcpy(o + x * 4, &v, 4);
                break;
            }
            case WGPUTextureFormat_R16Float: { uint16_t v = half(s[0] / 255.0f); memcpy(o + x * 2, &v, 2); break; }
            case WGPUTextureFormat_RG16Float: { uint16_t v[2] = { half(s[0] / 255.0f), half(s[1] / 255.0f) }; memcpy(o + x * 4, v, 4); break; }
            case WGPUTextureFormat_RGBA16Float: { uint16_t v[4] = { half(s[0] / 255.0f), half(s[1] / 255.0f), half(s[2] / 255.0f), half(s[3] / 255.0f) }; memcpy(o + x * 8, v, 8); break; }
            case WGPUTextureFormat_R32Float: { float v = s[0] / 255.0f; memcpy(o + x * 4, &v, 4); break; }
            case WGPUTextureFormat_RG32Float: { float v[2] = { s[0] / 255.0f, s[1] / 255.0f }; memcpy(o + x * 8, v, 8); break; }
            case WGPUTextureFormat_RGBA32Float: { float v[4] = { s[0] / 255.0f, s[1] / 255.0f, s[2] / 255.0f, s[3] / 255.0f }; memcpy(o + x * 16, v, 16); break; }
            }
        }
    }
    free(rgba);
    WGPUTexelCopyBufferLayout L = { 0, w * (uint32_t)bpt, h };
    WGPUExtent3D sz = { w, h, 1 };
    wgpuQueueWriteTexture(queue_of(d), &ti, out.data(), out.size(), &L, &sz);
}

/* ---- canvases ---- */

struct Rb { uint32_t buf = 0, w = 0, h = 0, stride = 0, ticket = 0; int state = 0; uint64_t seq = 0; };   /* state: 0 free, 1 mapping, 2 mapped */
struct Canvas {
    JsCtx *owner; Node *node; MediaPlayer *player; WgFrames *frames;
    int capture = 0;
    uint32_t dev = 0, tex = 0, tw = 0, th = 0;
    int fmt = 0, wf = 0;
    uint64_t usage = 0;
    std::vector<WGPUTextureFormat> view_fmts;
    bool opaque = true;
    Rb rb[3];
    uint64_t seq = 0, shown = 0;
    uint32_t dropped = 0;
};
std::map<uint32_t, Canvas *> g_canvas;
uint32_t g_next_canvas = 1, g_next_rb = 1;
int g_rb_busy = 0;   /* readbacks mapping or mapped, in all canvases */

void rb_state(Rb &r, int s) {
    g_rb_busy += (s != 0) - (r.state != 0);
    r.state = s;
}
void on_canvas_map(WGPUMapAsyncStatus st, WGPUStringView, void *u1, void *u2) {
    auto it = g_canvas.find((uint32_t)(uintptr_t)u1);
    uintptr_t v = (uintptr_t)u2;
    if (it == g_canvas.end()) return;
    Rb &r = it->second->rb[v & 3];
    if (r.state == 1 && r.ticket == (uint32_t)(v >> 2)) rb_state(r, st == WGPUMapAsyncStatus_Success ? 2 : 0);
}
void canvas_drop_tex(Canvas *cv) {
    if (cv->tex) hfree(cv->tex);
    cv->tex = 0; cv->tw = cv->th = 0;
}
void canvas_drop_rbs(Canvas *cv) {
    for (Rb &r : cv->rb) {
        if (r.buf) hfree(r.buf);   /* releasing a buffer still mapping aborts the map; on_canvas_map ignores it */
        rb_state(r, 0);
        r = Rb();
    }
}
void canvas_free(uint32_t id) {
    auto it = g_canvas.find(id);
    if (it == g_canvas.end()) return;
    Canvas *cv = it->second;
    g_canvas.erase(it);
    canvas_drop_tex(cv); canvas_drop_rbs(cv);
    mp_free(cv->player);
    if (cv->capture > 0) live_close(cv->capture);
    wgf_free(cv->frames);
    delete cv;
}
Canvas *canvas(JsCtx *c, uint32_t id) {
    auto it = g_canvas.find(id);
    return it != g_canvas.end() && it->second->owner == c ? it->second : nullptr;
}
/* the newest mapped readback of each canvas to its player; older ones are just unmapped */
void canvas_finish() {
    if (!g_rb_busy) return;
    for (auto &kv : g_canvas) {
        Canvas *cv = kv.second;
        int best = -1;
        for (int k = 0; k < 3; k++) if (cv->rb[k].state == 2 && (best < 0 || cv->rb[k].seq > cv->rb[best].seq)) best = k;
        if (best < 0) continue;
        H *d = hraw(cv->dev);
        bool live = d && !d->lost;
        Cap cap;
        for (int k = 0; k < 3; k++) {
            Rb &r = cv->rb[k];
            if (r.state != 2) continue;
            H *b = hraw(r.buf);
            if (k == best && live && b && b->p && r.seq > cv->shown) {
                const uint8_t *rows = (const uint8_t *)wgpuBufferGetMappedRange((WGPUBuffer)b->p, 0, (size_t)r.stride * r.h);
                wgf_push(cv->frames, cv->player, cv->capture, rows, r.stride, (int)r.w, (int)r.h, cv->wf, cv->opaque);
                cv->shown = r.seq;
            }
            if (live && b && b->p) wgpuBufferUnmap((WGPUBuffer)b->p);
            rb_state(r, 0);
        }
    }
}

FN(canvasConfigure) {   /* (canvasElement, device, format, usage, opaque, viewFormats, canvasId | 0): canvas id */
    GPUC;
    Node *n = junwrap(ARG(0));
    uint32_t did = ID(1);
    H *d = hget(c, did, K_DEVICE);
    int fmt = e.en_v(ARG(2), "TextureFormat", 0, true);
    uint64_t usage = au64(a, 3);
    std::vector<WGPUTextureFormat> vf;
    auto vl = e.list(ARG(5), "viewFormats");
    for (uint32_t i = 0; i < vl->Length() && !e.bad; i++) vf.push_back((WGPUTextureFormat)e.en_v(e.at(vl, i), "TextureFormat", 0));
    if (e.bad || !n || !d) return;
    if (fmt != WGPUTextureFormat_BGRA8Unorm && fmt != WGPUTextureFormat_RGBA8Unorm && fmt != WGPUTextureFormat_RGBA16Float) { e.type_error("Unsupported canvas format"); return; }
    if (usage & ~0x3Full) { e.type_error("Canvas usage has unknown bits"); return; }
    uint32_t id = au(a, 6);
    Canvas *cv = canvas(c, id);
    if (!cv) {
        cv = new Canvas();
        cv->owner = c; cv->node = n; cv->player = mp_new(n); cv->frames = wgf_new();
        id = g_next_canvas++;
        g_canvas[id] = cv;
    }
    canvas_drop_tex(cv);
    if (cv->dev != did) canvas_drop_rbs(cv);
    cv->dev = did; cv->fmt = fmt; cv->usage = usage; cv->view_fmts = vf;
    cv->wf = fmt == WGPUTextureFormat_BGRA8Unorm ? WGF_BGRA8 : fmt == WGPUTextureFormat_RGBA8Unorm ? WGF_RGBA8 : WGF_RGBA16F;
    cv->opaque = ARG(4)->BooleanValue(iso);
    RET(id);
}
FN(canvasUnconfigure) { GPUC; if (canvas(c, au(a, 0))) canvas_free(au(a, 0)); }
FN(canvasTexture) {   /* (canvasId, width, height): the texture to draw this frame into */
    GPUC;
    Canvas *cv = canvas(c, au(a, 0));
    if (!cv) return;
    uint32_t w = std::max<uint32_t>(au(a, 1), 1), h = std::max<uint32_t>(au(a, 2), 1);
    H *t = hraw(cv->tex);
    if (t && cv->tw == w && cv->th == h) { RET(cv->tex); return; }
    canvas_drop_tex(cv);
    H *d = hraw(cv->dev);
    if (!d || !d->p) return;
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.usage = cv->usage | WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    td.size = WGPUExtent3D{ w, h, 1 };
    td.format = (WGPUTextureFormat)cv->fmt;
    td.viewFormatCount = cv->view_fmts.size(); td.viewFormats = cv->view_fmts.data();
    Cap cap;
    WGPUTexture tex = wgpuDeviceCreateTexture((WGPUDevice)d->p, &td);
    uint32_t id = finish_new(e, K_TEXTURE, tex, cv->dev, cap);
    H *th = hraw(id);
    if (!th) return;
    th->canvas = true; th->fmt = cv->fmt; th->usage = cv->usage;
    cv->tex = id; cv->tw = w; cv->th = h;
    RET(id);
}
FN(canvasPresent) {   /* (canvasId): reads the texture back for the canvas's player */
    GPUC;
    Canvas *cv = canvas(c, au(a, 0));
    if (!cv) return;
    H *d = hraw(cv->dev), *t = hraw(cv->tex);
    if (!d || !d->p || d->lost || !t || !t->p || t->bad) return;
    int k = -1;
    for (int i = 0; i < 3; i++) if (!cv->rb[i].state && (k < 0 || (cv->rb[i].w == cv->tw && cv->rb[i].h == cv->th))) k = i;
    if (k < 0) { cv->dropped++; return; }   /* three frames in flight already */
    Rb &r = cv->rb[k];
    uint32_t bpp = cv->wf == WGF_RGBA16F ? 8 : 4, stride = (cv->tw * bpp + 255) / 256 * 256;
    Cap cap;
    if (!r.buf || r.w != cv->tw || r.h != cv->th) {
        if (r.buf) hfree(r.buf);
        r = Rb();
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = (uint64_t)stride * cv->th; bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer b = wgpuDeviceCreateBuffer((WGPUDevice)d->p, &bd);
        if (!b || cap.has) { if (b) wgpuBufferRelease(b); return; }
        r.buf = hnew(K_BUFFER, c, b, cv->dev);
        if (!r.buf) { wgpuBufferRelease(b); return; }
        H *bh = hraw(r.buf);
        bh->canvas = true; bh->size = bd.size; bh->usage = bd.usage;
        r.w = cv->tw; r.h = cv->th; r.stride = stride;
    }
    H *bh = hraw(r.buf);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder((WGPUDevice)d->p, nullptr);
    WGPUTexelCopyTextureInfo src = { (WGPUTexture)t->p, 0, { 0, 0, 0 }, WGPUTextureAspect_All };
    WGPUTexelCopyBufferInfo dst = { { 0, r.stride, r.h }, (WGPUBuffer)bh->p };
    WGPUExtent3D ext = { r.w, r.h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuCommandEncoderRelease(enc);
    if (!cap.has) wgpuQueueSubmit(queue_of(d), 1, &cb);
    wgpuCommandBufferRelease(cb);
    if (cap.has) return;
    r.ticket = g_next_rb++ & 0x3fffffff; r.seq = ++cv->seq;
    rb_state(r, 1);
    wgpuBufferMapAsync((WGPUBuffer)bh->p, WGPUMapMode_Read, 0, (size_t)r.stride * r.h,
                       WGPUBufferMapCallbackInfo{ nullptr, WGPUCallbackMode_AllowProcessEvents, on_canvas_map, (void *)(uintptr_t)au(a, 0), (void *)(uintptr_t)(k | (uintptr_t)r.ticket << 2) });
    if (media_wakeup) media_wakeup();
}
FN(canvasCapture) {   /* (canvasId): the live source captureStream() shows */
    GPUC;
    Canvas *cv = canvas(c, au(a, 0));
    if (!cv) return;
    if (cv->capture <= 0) cv->capture = live_open();
    RET(cv->capture);
}

}  // namespace

/* ---- the event loop ---- */

double webgpu_deadline(JsCtx *c) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_ctx.find(c);
    if (it == g_ctx.end()) return INFINITY;
    double now = now_ms();
    if (!it->second.evs.empty()) return now;
    if (it->second.outstanding > 0) {
        double age = now - it->second.since;
        return now + (age < 100 ? 1 : age < 1000 ? 4 : 16);
    }
    return INFINITY;
}

void webgpu_pump(JsCtx *c) {
    if (!g_inst) return;
    if (g_outstanding > 0 || g_rb_busy) {
        wgpuInstanceProcessEvents(g_inst);
        canvas_finish();
    }
    drain_dead();
    size_t n;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_ctx.find(c);
        if (it == g_ctx.end() || it->second.evs.empty() || it->second.cb.IsEmpty()) return;
        n = it->second.evs.size();   /* those queued now; later ones wait for the next turn */
    }
    JS_ENTER(c);
    while (n--) {
        Ev ev;
        v8::Local<v8::Function> cb;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            auto it = g_ctx.find(c);
            if (it == g_ctx.end() || it->second.evs.empty()) return;
            ev = std::move(it->second.evs.front());
            it->second.evs.pop_front();
            if (ev.kind == 1) it->second.uncaptured--;
            cb = it->second.cb.Get(iso);
        }
        const char *type = ev.type == WGPUErrorType_OutOfMemory ? "out-of-memory" : ev.type == WGPUErrorType_Validation ? "validation" : "internal";
        v8::Local<v8::Value> argv[5] = { v8::Number::New(iso, ev.kind), v8::Number::New(iso, ev.ticket), v8::Number::New(iso, ev.a),
                                         jstr(iso, ev.msg.c_str(), (int)ev.msg.size()), jstr(iso, type) };
        (void)jcall(c, cb, v8::Undefined(iso), 5, argv);
    }
}

extern "C" void webgpu_tick(void) {
    if (!g_inst || !g_rb_busy) return;
    wgpuInstanceProcessEvents(g_inst);
    canvas_finish();
    drain_dead();
}
extern "C" int webgpu_timeout_ms(void) { return g_inst && g_rb_busy ? 1 : -1; }

extern "C" void webgpu_close_owner(const void *owner) {
    JsCtx *c = (JsCtx *)owner;
    g_closing = true;
    std::vector<uint32_t> cvs;
    for (auto &kv : g_canvas) if (kv.second->owner == c) cvs.push_back(kv.first);
    for (uint32_t id : cvs) canvas_free(id);
    /* everything but devices first, then the devices, which wait for their GPU work */
    std::vector<uint32_t> devs;
    for (size_t s = 1; s < g_h.size(); s++) {
        H *h = g_h[s];
        if (!h || h->owner != c) continue;
        uint32_t id = (uint32_t)s | (g_gen[s] & GEN_MASK) << SLOT_BITS;
        if (h->kind == K_DEVICE) { devs.push_back(id); continue; }
        hfree(id);
    }
    for (uint32_t id : devs) if (H *d = hraw(id)) { d->want_release = true; d->children = 0; hfree(id); }
    drain_dead();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_ctx.find(c);
        if (it != g_ctx.end()) {
            /* tickets still out stay in g_outstanding: their callbacks find no context and only free them */
            it->second.cb.Reset(); it->second.sym.Reset();
            g_ctx.erase(it);
        }
    }
    g_closing = false;
}

void js_install_webgpu(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Object> G = v8::Object::New(iso);
#define REG(nm) (void)G->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, w_##nm).ToLocalChecked())
    REG(init); REG(requestAdapter); REG(adapterInfo); REG(limits); REG(features); REG(requestDevice); REG(destroy); REG(release);
    REG(pushErrorScope); REG(popErrorScope); REG(createBuffer); REG(mapAsync); REG(getMappedRange); REG(unmap);
    REG(createTexture); REG(createView); REG(createSampler); REG(createBindGroupLayout); REG(createPipelineLayout); REG(createBindGroup);
    REG(createShaderModule); REG(shaderInfo); REG(createRenderPipeline); REG(createComputePipeline); REG(getBindGroupLayout); REG(createQuerySet);
    REG(createCommandEncoder); REG(beginRenderPass); REG(beginComputePass); REG(copyBufferToBuffer); REG(copyBufferToTexture);
    REG(copyTextureToBuffer); REG(copyTextureToTexture); REG(clearBuffer); REG(resolveQuerySet); REG(finish);
    REG(setPipeline); REG(setBindGroup); REG(setVertexBuffer); REG(setIndexBuffer); REG(draw); REG(drawIndexed); REG(drawIndirect);
    REG(drawIndexedIndirect); REG(setViewport); REG(setScissorRect); REG(setBlendConstant); REG(setStencilReference);
    REG(beginOcclusionQuery); REG(endOcclusionQuery); REG(dispatchWorkgroups); REG(dispatchWorkgroupsIndirect); REG(end);
    REG(submit); REG(writeBuffer); REG(writeTexture); REG(onSubmittedWorkDone); REG(copyExternalImageToTexture);
    REG(canvasConfigure); REG(canvasUnconfigure); REG(canvasTexture); REG(canvasPresent); REG(canvasCapture);
#undef REG
    (void)N->Set(ctx, jstr(iso, "gpu"), G);
}
