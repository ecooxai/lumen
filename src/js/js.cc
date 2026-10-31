#include "broadcast.h"
#include "event_target.h"
#include <map>
#include <string>
/* V8 embedding: platform, per-page isolate/context, node wrappers, script execution, timers, events */
#include "js_int.h"
extern "C" {
#include "../gpu/webgl.h"
#include "../gpu/webgpu.h"
}
#include "v8_tasks.h"
#include "clone.h"
#include <v8-profiler.h>
#include <algorithm>
#include <openssl/evp.h>
#include <cstring>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <unordered_set>
#include <cmath>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <pthread.h>
#endif

/* Process lifetime: V8 worker threads may still run while static destructors do. */
static v8::Platform *g_platform;
static v8::Platform *g_task_platform;
v8::Platform *js_platform() { return g_platform; }
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

/* C strings can't hold U+0000, so attribute values store it as the overlong pair C0 80 (jsnative.cc); no valid UTF-8
   contains that pair, and here it turns back into U+0000 */
v8::Local<v8::String> jstr(v8::Isolate *iso, const char *s, int n) {
    v8::Local<v8::String> r;
    if (s) {
        size_t len = n < 0 ? strlen(s) : (size_t)n;
        const char *p = (const char *)memchr(s, 0xC0, len);
        while (p && !(p + 1 < s + len && (unsigned char)p[1] == 0x80)) p = (const char *)memchr(p + 1, 0xC0, (size_t)(s + len - p - 1));
        if (p) {
            std::string d; d.reserve(len);
            for (size_t i = 0; i < len; i++) {
                if ((unsigned char)s[i] == 0xC0 && i + 1 < len && (unsigned char)s[i + 1] == 0x80) { d += '\0'; i++; }
                else d += s[i];
            }
            if (!v8::String::NewFromUtf8(iso, d.data(), v8::NewStringType::kNormal, (int)d.size()).ToLocal(&r)) return v8::String::Empty(iso);
            return r;
        }
    }
    if (!v8::String::NewFromUtf8(iso, s ? s : "", v8::NewStringType::kNormal, s ? n : 0).ToLocal(&r)) return v8::String::Empty(iso);
    return r;
}

std::string jcstr(v8::Isolate *iso, v8::Local<v8::Value> v) {
    if (v.IsEmpty()) return {};
    v8::String::Utf8Value u(iso, v);
    return *u ? std::string(*u, (size_t)u.length()) : std::string();
}

v8::Local<v8::String> jbytes(v8::Isolate *iso, const char *s, int n) {
    v8::Local<v8::String> out;
    if (!v8::String::NewFromOneByte(iso, reinterpret_cast<const uint8_t *>(s ? s : ""),
            v8::NewStringType::kNormal, s ? n : 0).ToLocal(&out)) return v8::String::Empty(iso);
    return out;
}

std::string jcbytes(v8::Isolate *iso, v8::Local<v8::Value> v) {
    if (v.IsEmpty()) return {};
    v8::String::Value chars(iso, v);
    if (!*chars) return {};
    std::string out((size_t)chars.length(), '\0');
    for (int i = 0; i < chars.length(); ++i) out[(size_t)i] = static_cast<char>((*chars)[i]);
    return out;
}

static v8::Local<v8::Object> proto_for(JsCtx *c, Node *n) {
    v8::Isolate *iso = c->iso;
    if (c->protoFor.IsEmpty()) return {};
    /* a shadow root (a fragment with a host) is tagged "#shadow" */
    const char *tag = n->type == NODE_ELEMENT ? n->tag : n->type == NODE_FRAGMENT && n->host ? "#shadow"
        : n->type == NODE_DOCUMENT && ((Document *)n)->interface_kind == 2 ? "#document" : "";
    std::string key = std::to_string(n->type) + ":" + (tag ? tag : "") + ":" + std::to_string(n->ns);
    auto it = c->protos.find(key);
    if (it != c->protos.end()) return it->second.Get(iso);
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::TryCatch tc(iso);
    v8::Local<v8::Value> argv[3] = { v8::Integer::New(iso, n->type), jstr(iso, tag ? tag : ""), v8::Integer::New(iso, n->ns) };
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
    if (n->type == NODE_ELEMENT && (n->xns || n->xpfx || n->xln)) {   /* the names of an element from XML, a clone or createElementNS */
        if (n->xns) (void)o->DefineOwnProperty(ctx, jstr(iso, "__nsu"), jstr(iso, n->xns), v8::DontEnum);
        if (n->xpfx) (void)o->DefineOwnProperty(ctx, jstr(iso, "__pfx"), jstr(iso, n->xpfx), v8::DontEnum);
        if (n->xln) (void)o->DefineOwnProperty(ctx, jstr(iso, "__ln"), jstr(iso, n->xln), v8::DontEnum);
    }
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
#if defined(__ANDROID__) && !defined(NDEBUG)
    __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "watchdog terminated a task after %.0f ms (frames=%d)", g_js_timeout_ms, st->GetFrameCount());
#endif
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
            if (k == 9 || (u + k >= e + (k ? 0 : 1) && k)) { p->w.push_back(0xFFFD); u++; continue; }
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

/* ---- V8 code cache: bytecode of large external scripts and modules on disk, named by
   a hash of the source text (never the URL), plus the prelude's in memory. Inline
   scripts are not cached: they can embed user data. ---- */
static std::string g_code_dir;
static size_t g_code_max;
static std::atomic<int64_t> g_code_bytes{ -1 };
static std::atomic<bool> g_code_trimming{ false };
enum { kCodeCacheMin = 16 << 10 };
/* Least recently used files go first, down to 80% of the limit. Returns the bytes kept. */
static int64_t code_trim(void) {
    struct F { std::string name; time_t mtime; off_t size; };
    std::vector<F> v;
    int64_t total = 0;
    if (DIR *d = opendir(g_code_dir.c_str())) {
        for (struct dirent *e; (e = readdir(d));) {
            if (e->d_name[0] == '.') continue;
            std::string p = g_code_dir + "/" + e->d_name;
            struct stat st;
            if (stat(p.c_str(), &st) || !S_ISREG(st.st_mode)) continue;
            if (strchr(e->d_name, '~')) { if (time(nullptr) - st.st_mtime > 600) unlink(p.c_str()); continue; }
            v.push_back({ e->d_name, st.st_mtime, st.st_size });
            total += st.st_size;
        }
        closedir(d);
    }
    if (total > (int64_t)g_code_max) {
        std::sort(v.begin(), v.end(), [](const F &a, const F &b) { return a.mtime < b.mtime; });
        for (size_t i = 0; i < v.size() && total > (int64_t)(g_code_max / 10 * 8); i++)
            if (!unlink((g_code_dir + "/" + v[i].name).c_str())) total -= v[i].size;
    }
    return total;
}
void js_code_cache_init(const char *dir, size_t max_bytes) {
    if (!dir || !g_code_dir.empty()) return;
    mkdir(dir, 0700);
    g_code_dir = dir; g_code_max = max_bytes;
    std::thread([] { g_code_bytes = code_trim(); }).detach();
}
static std::string code_path(const char *s, size_t n) {
    uint64_t h = 0x9e3779b97f4a7c15ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t w; memcpy(&w, s + i, 8); h = (h ^ w) * 0x100000001b3ull; h ^= h >> 29; }
    for (; i < n; i++) h = (h ^ (uint8_t)s[i]) * 0x100000001b3ull;
    char b[48];
    snprintf(b, sizeof b, "/%016llx-%zx", (unsigned long long)h, n);
    return g_code_dir + b;
}
/* Cached bytecode for a source, or null; the Source that receives it owns it. */
static v8::ScriptCompiler::CachedData *code_load(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    struct stat st;
    if (fstat(fileno(f), &st) || st.st_size <= 0 || st.st_size > (64 << 20)) { fclose(f); return nullptr; }
    uint8_t *b = new uint8_t[(size_t)st.st_size];
    bool ok = fread(b, 1, (size_t)st.st_size, f) == (size_t)st.st_size;
    fclose(f);
    if (!ok) { delete[] b; return nullptr; }
    utimensat(AT_FDCWD, path.c_str(), nullptr, 0);   /* recently used */
    return new v8::ScriptCompiler::CachedData(b, (int)st.st_size, v8::ScriptCompiler::CachedData::BufferOwned);
}
/* CreateCodeCache, timed in debug Android builds (serialising a large module blocks the main thread) */
template <class T> static v8::ScriptCompiler::CachedData *code_create(T script, const char *site) {
#if defined(__ANDROID__) && !defined(NDEBUG)
    double t0 = now_ms();
    v8::ScriptCompiler::CachedData *cd = v8::ScriptCompiler::CreateCodeCache(script);
    if (now_ms() - t0 >= 20) __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "code_cache site=%s ms=%.0f kb=%d", site, now_ms() - t0, cd ? cd->length >> 10 : 0);
    return cd;
#else
    (void)site; return v8::ScriptCompiler::CreateCodeCache(script);
#endif
}
/* Writes cd (consumed) atomically off the main thread. */
static void code_store(const std::string &path, v8::ScriptCompiler::CachedData *cd) {
    if (!cd) return;
    std::string bytes((const char *)cd->data, (size_t)cd->length);
    delete cd;
    std::thread([path, bytes = std::move(bytes)] {
        static std::atomic<unsigned> seq{ 0 };
        std::string tmp = path + "~" + std::to_string(getpid()) + "." + std::to_string(++seq);
        FILE *f = fopen(tmp.c_str(), "wb");
        bool ok = f && fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        if (f && fclose(f)) ok = false;
        if (!ok || rename(tmp.c_str(), path.c_str())) { unlink(tmp.c_str()); return; }
        if (g_code_bytes >= 0 && (g_code_bytes += (int64_t)bytes.size()) > (int64_t)g_code_max && !g_code_trimming.exchange(true)) {
            g_code_bytes = code_trim();
            g_code_trimming = false;
        }
    }).detach();
}
/* Large external module sources compiled without usable cache, serialised once their
   graph has evaluated so that the functions it ran are included. */
static thread_local std::vector<std::pair<v8::Global<v8::Module>, std::string>> t_code_pending;

static void run_source(JsCtx *c, const char *src, size_t n, const char *name, bool cache) {
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::ScriptOrigin origin(jstr(iso, name ? name : ""));
    v8::Local<v8::Script> s;
    js_enter(c);
    cache = cache && n >= kCodeCacheMin && !g_code_dir.empty();
    std::string path = cache ? code_path(src, n) : std::string();
    v8::ScriptCompiler::CachedData *cd = cache ? code_load(path) : nullptr;
    v8::ScriptCompiler::Source source(jsrc(iso, src, n), origin, cd);
    if (v8::ScriptCompiler::Compile(ctx, &source, cd ? v8::ScriptCompiler::kConsumeCodeCache : v8::ScriptCompiler::kNoCompileOptions).ToLocal(&s)) {
        (void)s->Run(ctx);
        if (cache && (!cd || source.GetCachedData()->rejected)) code_store(path, code_create(s->GetUnboundScript(), "classic"));
    }
    js_leave(c);
    if (tc.HasCaught() && tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
    settle(c);
}

/* ---- ES modules: no import maps. A graph is fetched in parallel ahead of compilation
   (static imports found by a lexical scan), then compiled and linked on the main thread. ---- */
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
static bool mod_fetch(const std::string &u, const std::string &ref, std::string &out, std::string *ctype = nullptr) {
    NetRequest *rq = net_request_new("GET", u.c_str());
    { char rv[4200]; if (url_referrer(ref.c_str(), u.c_str(), NULL, rv, sizeof rv)) headers_add(&rq->headers, "Referer", rv); }
    NetResponse *r = net_fetch_sync(rq);
    bool ok = r && r->status >= 200 && r->status < 300;
    if (ok) out.assign(r->body ? r->body : "", r->body_len);
    if (ok && ctype) { const char *t = headers_get(&r->headers, "content-type"); *ctype = t ? t : ""; }
    if (r) net_response_free(r);
    return ok;
}

/* Static import specifiers: `from "x"` and `import "x"`. A miss only costs a later
   synchronous fetch, and a false hit one wasted request. */
static std::vector<std::string> mod_scan(const std::string &src) {
    std::vector<std::string> out;
    auto ident = [](char ch) { return isalnum((unsigned char)ch) || ch == '_' || ch == '$' || (unsigned char)ch >= 0x80; };
    const char *b = src.data(), *e = b + src.size();
    for (const char *p = b; p < e; p++) {
        size_t k = *p == 'f' ? 4 : *p == 'i' ? 6 : 0;
        if (!k || (size_t)(e - p) < k + 2 || memcmp(p, k == 4 ? "from" : "import", k) || (p > b && (ident(p[-1]) || p[-1] == '.'))) continue;
        const char *q = p + k;
        while (q < e && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
        if (q >= e || (*q != '"' && *q != '\'')) continue;
        const char *t = (const char *)memchr(q + 1, *q, (size_t)std::min<ptrdiff_t>(e - q - 1, 2048));
        if (t && !memchr(q + 1, '\n', (size_t)(t - q - 1))) out.emplace_back(q + 1, t);
        p = t ? t : q;
    }
    return out;
}

/* Fetched module sources awaiting compilation, shared by every context and the page
   loader thread. Each is consumed once; leftovers (false scan hits, modules another
   context compiled) expire. */
namespace {
struct ModSrc { std::string body, ctype; std::vector<std::string> kids; bool ok = false; double at = 0; };
std::mutex g_msrc_mu;
std::condition_variable g_msrc_cv;
std::unordered_map<std::string, std::shared_ptr<ModSrc>> g_msrc;
std::unordered_set<std::string> g_msrc_flying;
}  // namespace
/* retry: refetch a source whose earlier fetch failed (prefetch walks reuse the failure) */
static std::shared_ptr<ModSrc> msrc_get(const std::string &u, const std::string &ref, bool retry) {
    std::unique_lock<std::mutex> lk(g_msrc_mu);
    g_msrc_cv.wait(lk, [&] { return !g_msrc_flying.count(u); });
    auto it = g_msrc.find(u);
    if (it != g_msrc.end() && (it->second->ok || !retry)) return it->second;
    g_msrc_flying.insert(u);
    lk.unlock();
    auto m = std::make_shared<ModSrc>();
    m->ok = mod_fetch(u, ref, m->body, &m->ctype);
    if (m->ok) for (const std::string &sp : mod_scan(m->body)) { std::string k = mod_resolve(sp, u); if (!k.empty()) m->kids.push_back(k); }
    m->at = now_ms();
    lk.lock();
    for (auto i = g_msrc.begin(); i != g_msrc.end();) i = m->at - i->second->at > 120000 ? g_msrc.erase(i) : std::next(i);
    g_msrc[u] = m;
    g_msrc_flying.erase(u);
    g_msrc_cv.notify_all();
    return m;
}
/* Fetches roots and everything they statically import, eight at a time; known() prunes. */
static void mod_prefetch(const std::vector<std::string> &roots, const std::string &ref, const std::function<bool(const std::string &)> &known) {
    std::mutex mu; std::condition_variable cv;
    std::deque<std::string> q; std::unordered_set<std::string> seen; int active = 0;
    auto push = [&](const std::string &u) { if (seen.size() < 4000 && !known(u) && seen.insert(u).second) q.push_back(u); };
    for (const std::string &u : roots) push(u);
    auto work = [&] {
        std::unique_lock<std::mutex> lk(mu);
        for (;;) {
            cv.wait(lk, [&] { return !q.empty() || !active; });
            if (q.empty()) return;
            std::string u = std::move(q.front()); q.pop_front(); active++;
            lk.unlock();
            std::shared_ptr<ModSrc> m = msrc_get(u, ref, false);
            lk.lock();
            active--;
            for (const std::string &k : m->kids) push(k);
            cv.notify_all();
        }
    };
    if (q.empty()) return;
    std::vector<std::thread> th;
    for (int i = 1; i < 8; i++) th.emplace_back(work);
    work();
    for (std::thread &t : th) t.join();
}
/* Takes a module's source, fetching it now if no prefetch has. */
static bool mod_take(const std::string &u, const std::string &ref, std::string &out, std::string *ctype = nullptr) {
    std::shared_ptr<ModSrc> m = msrc_get(u, ref, true);
    { std::lock_guard<std::mutex> lk(g_msrc_mu); auto it = g_msrc.find(u); if (it != g_msrc.end() && it->second == m) g_msrc.erase(it); }
    if (m->ok && ctype) *ctype = m->ctype;
    if (m->ok) out = m.use_count() == 1 ? std::move(m->body) : m->body;
    return m->ok;
}
void js_module_prefetch(const char *const *urls, int n, const char *ref) {
    std::vector<std::string> roots;
    for (int i = 0; i < n; i++) if (urls[i]) roots.emplace_back(urls[i]);
    mod_prefetch(roots, ref ? ref : "", [](const std::string &) { return false; });
}
char *js_module_take(const char *url, const char *ref, size_t *len, char **ctype) {
    std::string body, ct;
    if (ctype) *ctype = nullptr;
    if (!mod_take(url, ref ? ref : "", body, &ct)) return nullptr;
    if (ctype) *ctype = xstrdup(ct.c_str());
    char *r = (char *)xmalloc(body.size() + 1);
    memcpy(r, body.data(), body.size()); r[body.size()] = 0;
    *len = body.size();
    return r;
}
static std::string mod_key(JsCtx *c, v8::Local<v8::Module> m) {
    auto r = c->mod_url.equal_range(m->GetIdentityHash());
    for (auto it = r.first; it != r.second; ++it) {
        auto f = c->mods.find(it->second);
        if (f != c->mods.end() && f->second.Get(c->iso) == m) return it->second;
    }
    return c->doc && c->doc->url ? c->doc->url : "";
}
bool mod_rethrow_cached(v8::Isolate *iso, const ModErrorMap &errors, const std::string &key) {
    auto it = errors.find(key);
    if (it == errors.end()) return false;
    iso->ThrowException(it->second.Get(iso));
    return true;
}
void mod_cache_script_error(v8::Isolate *iso, ModErrorMap &errors, const std::string &key, v8::TryCatch &caught) {
    if (!caught.HasCaught()) return;
    if (caught.CanContinue() && !errors.count(key)) errors.emplace(key, v8::Global<v8::Value>(iso, caught.Exception()));
    caught.ReThrow();
}
bool mod_script_requests(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Module> module,
                         const std::string &key, bool css, ModResolver resolve, ModErrorMap &errors,
                         std::vector<std::string> &requests) {
    v8::TryCatch caught(iso);
    v8::Local<v8::FixedArray> rq = module->GetModuleRequests();
    for (int i = 0; i < rq->Length(); i++) {
        v8::Local<v8::ModuleRequest> mr = rq->Get(i).As<v8::ModuleRequest>();
        std::string sp = jcstr(iso, mr->GetSpecifier()), ty;
        if (!mod_attr_type(iso, ctx, mr->GetImportAttributes(), 3, css, ty)) {
            mod_cache_script_error(iso, errors, key, caught); return false;
        }
        std::string url = resolve(sp, key);
        if (url.empty()) {
            iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("Failed to resolve module specifier \"" + sp + "\"").c_str())));
            mod_cache_script_error(iso, errors, key, caught); return false;
        }
        requests.push_back(mod_mkey(url, ty));
    }
    return true;
}
bool mod_rethrow_graph_error(v8::Isolate *iso, const ModErrorMap &errors, const ModGraph &graph,
                             const std::string &root) {
    /* Ordered depth-first traversal is root-specific, including cycles. Keep the
     * original script Error object without storing this selection on a parent. */
    std::unordered_set<std::string> visited;
    std::vector<std::string> pending{root};
    while (!pending.empty()) {
        std::string key = std::move(pending.back()); pending.pop_back();
        if (!visited.insert(key).second) continue;
        if (mod_rethrow_cached(iso, errors, key)) return true;
        auto it = graph.find(key);
        if (it != graph.end()) {
            for (auto edge = it->second.rbegin(); edge != it->second.rend(); ++edge) pending.push_back(*edge);
        }
    }
    return false;
}
static constexpr double kCodeRefreshQuietMs = 10000;   /* after the last module compile */
static constexpr double kCodeIdleMs = 4000;             /* after the last user input */
static std::atomic<double> g_input_at{ 0 };              /* last trusted pointer, touch, key or wheel event */
/* Serialising one large module takes 50-150 ms on a phone, so it waits for the user to pause. */
static bool user_idle() { return now_ms() - g_input_at.load() > kCodeIdleMs; }
/* Re-serialises cached modules in small slices once module loading has been quiet; a cache
   that grew (functions compiled since) replaces the stored one. */
static void code_refresh_tick(JsCtx *c) {
    if (c->code_refresh.empty() || now_ms() < c->code_refresh_due || !user_idle()) return;
    double t0 = now_ms();
    while (!c->code_refresh.empty() && now_ms() - t0 < 8) {
        JsCtx::CodeRefresh r = std::move(c->code_refresh.back());
        c->code_refresh.pop_back();
        v8::ScriptCompiler::CachedData *cd = code_create(r.s.Get(c->iso), "refresh");
        if (cd && cd->length > r.had + r.had / 8 + 1024) code_store(r.path, cd);
        else delete cd;
    }
}
static v8::MaybeLocal<v8::Module> mod_compile(JsCtx *c, const std::string &key, const std::string &src) {
    v8::Isolate *iso = c->iso;
    if (mod_rethrow_cached(iso, c->mod_errors, key)) return {};
    v8::TryCatch caught(iso);
    v8::ScriptOrigin origin(jstr(iso, key.c_str()), 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
    bool cache = src.size() >= kCodeCacheMin && !g_code_dir.empty() && key.find("#lumen-inline-") == std::string::npos;
    std::string path = cache ? code_path(src.data(), src.size()) : std::string();
    v8::ScriptCompiler::CachedData *cd = cache ? code_load(path) : nullptr;
    v8::ScriptCompiler::Source s(jsrc(iso, src.data(), src.size()), origin, cd);
    v8::Local<v8::Module> m;
    if (!v8::ScriptCompiler::CompileModule(iso, &s, cd ? v8::ScriptCompiler::kConsumeCodeCache : v8::ScriptCompiler::kNoCompileOptions).ToLocal(&m)) {
        mod_cache_script_error(iso, c->mod_errors, key, caught); return {};
    }
    if (cache && (!cd || s.GetCachedData()->rejected)) t_code_pending.emplace_back(v8::Global<v8::Module>(iso, m), path);
    if (cache) {
        int had = cd && !s.GetCachedData()->rejected ? s.GetCachedData()->length : 0;
        c->code_refresh.push_back({ v8::Global<v8::UnboundModuleScript>(iso, m->GetUnboundModuleScript()), path, had });
        c->code_refresh_due = now_ms() + kCodeRefreshQuietMs;
    }
    c->mods[key].Reset(iso, m);
    c->mod_url.emplace(m->GetIdentityHash(), key);
    return m;
}
/* Import attributes: a non-JavaScript module's map key is its URL + '\x01' + type (json, text, css). */
std::string mod_mkey(const std::string &u, const std::string &t) { return t.empty() || u.empty() ? u : u + '\x01' + t; }
std::string mod_kurl(const std::string &k) { size_t i = k.find('\x01'); return i == std::string::npos ? k : k.substr(0, i); }
std::string mod_ktype(const std::string &k) { size_t i = k.find('\x01'); return i == std::string::npos ? "" : k.substr(i + 1); }
/* the module type that import attributes ([key, value(, offset)]... by stride) ask for; throws and returns false for
   an unsupported key (SyntaxError) or type (TypeError) */
bool mod_attr_type(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::FixedArray> a, int stride, bool css, std::string &type) {
    type.clear();
    for (int i = 0; !a.IsEmpty() && i + 1 < a->Length(); i += stride) {
        std::string k = jcstr(iso, a->Get(i).As<v8::Value>()), v = jcstr(iso, a->Get(i + 1).As<v8::Value>());
        if (k != "type") { iso->ThrowException(v8::Exception::SyntaxError(jstr(iso, ("Unsupported import attribute \"" + k + "\"").c_str()))); return false; }
        if (v != "json" && v != "text" && !(css && v == "css")) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("\"" + v + "\" is not a valid module type.").c_str()))); return false; }
        type = v;
    }
    (void)ctx;
    return true;
}
/* MIME Sniffing's parse-a-MIME-type algorithm, through the essence. Parameters
   cannot invalidate an otherwise valid essence and are not needed for modules.
   HTTP whitespace deliberately excludes form feed and vertical tab. */
static std::string module_mime_essence(const std::string &ct) {
    const auto ws = [](unsigned char c) { return c == '\t' || c == '\n' || c == '\r' || c == ' '; };
    const auto token = [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c && std::strchr("!#$%&'*+-.^_`|~", c));
    };
    size_t begin = 0, end = ct.size();
    while (begin < end && ws((unsigned char)ct[begin])) ++begin;
    while (end > begin && ws((unsigned char)ct[end - 1])) --end;
    const size_t slash = ct.find('/', begin);
    if (slash == std::string::npos || slash == begin || slash >= end) return {};
    for (size_t i = begin; i < slash; ++i) if (!token((unsigned char)ct[i])) return {};
    const size_t semicolon = ct.find(';', slash + 1);
    if (semicolon != std::string::npos && semicolon < end) end = semicolon;
    while (end > slash + 1 && ws((unsigned char)ct[end - 1])) --end;
    if (end == slash + 1) return {};
    for (size_t i = slash + 1; i < end; ++i) if (!token((unsigned char)ct[i])) return {};
    std::string result = ct.substr(begin, end - begin);
    for (char &ch : result) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return result;
}
static bool json_mime(const std::string &ct) {
    const std::string t = module_mime_essence(ct);
    return t == "application/json" || t == "text/json" || (t.size() > 5 && t.compare(t.size() - 5, 5, "+json") == 0);
}
/* HTML module fetch uses the MIME essence, independent of filename or body.
   Text modules accept any MIME type; unsupported types are rejected earlier. */
bool mod_mime_matches(const std::string &type, const std::string &ctype) {
    if (type == "text") return true;
    if (type == "json") return json_mime(ctype);
    const std::string mime = module_mime_essence(ctype);
    if (type == "css") return mime == "text/css";
    if (!type.empty()) return false;
    static const char *const javascript[] = {
        "application/ecmascript", "application/javascript", "application/x-ecmascript", "application/x-javascript",
        "text/ecmascript", "text/javascript", "text/javascript1.0", "text/javascript1.1", "text/javascript1.2",
        "text/javascript1.3", "text/javascript1.4", "text/javascript1.5", "text/jscript", "text/livescript",
        "text/x-ecmascript", "text/x-javascript"
    };
    for (const char *known : javascript) if (mime == known) return true;
    return false;
}
/* each synthetic module's [value(, css flag)] until it evaluates, per isolate (the Android V8 has no
   host_defined_options on synthetic modules); mod_synth_drop clears an isolate's before it goes away */
namespace {
struct SynthEnt { v8::Global<v8::Module> m; v8::Global<v8::Array> d; };
std::mutex g_synth_mu;
std::unordered_map<v8::Isolate *, std::unordered_multimap<int, SynthEnt>> *g_synth = new std::unordered_map<v8::Isolate *, std::unordered_multimap<int, SynthEnt>>;
}  // namespace
void mod_synth_drop(v8::Isolate *iso) { std::lock_guard<std::mutex> lk(g_synth_mu); g_synth->erase(iso); }
#if V8_MAJOR_VERSION > 15 || (V8_MAJOR_VERSION == 15 && V8_MINOR_VERSION >= 6)
static v8::MaybeLocal<v8::Promise> synth_steps(v8::Local<v8::Context> ctx, v8::Local<v8::Module> m) {
#else
static v8::MaybeLocal<v8::Value> synth_steps(v8::Local<v8::Context> ctx, v8::Local<v8::Module> m) {
#endif
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    v8::Local<v8::Array> d;
    {
        std::lock_guard<std::mutex> lk(g_synth_mu);
        auto &tab = (*g_synth)[iso];
        auto r = tab.equal_range(m->GetIdentityHash());
        for (auto it = r.first; it != r.second; ++it) if (it->second.m.Get(iso) == m) { d = it->second.d.Get(iso); tab.erase(it); break; }
    }
    if (d.IsEmpty()) { mod_throw(iso, "Module source is no longer available"); return {}; }
    v8::Local<v8::Value> v = d->Get(ctx, 0).ToLocalChecked();
    if (d->Length() > 1) {   /* css: a constructed CSSStyleSheet */
        v8::Local<v8::Value> C, sheet, rs;
        if (!ctx->Global()->Get(ctx, jstr(iso, "CSSStyleSheet")).ToLocal(&C) || !C->IsFunction() || !C.As<v8::Function>()->NewInstance(ctx).ToLocal(&sheet)) return {};
        if (!sheet.As<v8::Object>()->Get(ctx, jstr(iso, "replaceSync")).ToLocal(&rs) || !rs->IsFunction() || rs.As<v8::Function>()->Call(ctx, sheet, 1, &v).IsEmpty()) return {};
        v = sheet;
    }
    if (m->SetSyntheticModuleExport(iso, jstr(iso, "default"), v).IsNothing()) return {};
    v8::Local<v8::Promise::Resolver> r;
    if (!v8::Promise::Resolver::New(ctx).ToLocal(&r)) return {};
    (void)r->Resolve(ctx, v8::Undefined(iso));
    return r->GetPromise();
}
/* a JSON, text or CSS module from a fetched body (JSON needs a JSON MIME type and must parse; CSS text/css) */
v8::MaybeLocal<v8::Module> mod_synthetic(v8::Local<v8::Context> ctx, const std::string &u, const std::string &type, const std::string &body, const std::string &ctype) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    /* Module scripts use UTF-8 decode: strip one leading UTF-8 BOM, without
       switching to UTF-16 when another encoding's BOM is present. */
    const size_t bom = body.size() >= 3 && !std::memcmp(body.data(), "\xEF\xBB\xBF", 3) ? 3 : 0;
    v8::Local<v8::String> src = jsrc(iso, body.data() + bom, body.size() - bom);
    v8::Local<v8::Value> v = src;
    if (type == "json") {
        if (!json_mime(ctype)) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("Failed to load module script: \"" + u + "\" is not a JSON MIME type.").c_str()))); return {}; }
        if (!v8::JSON::Parse(ctx, src).ToLocal(&v)) return {};
    } else if (type == "css") {
        if (module_mime_essence(ctype) != "text/css") { iso->ThrowException(v8::Exception::TypeError(jstr(iso, ("Failed to load module script: \"" + u + "\" is not a text/css MIME type.").c_str()))); return {}; }
    }
    v8::Local<v8::Array> d = v8::Array::New(iso, type == "css" ? 2 : 1);
    (void)d->Set(ctx, 0, v);
    if (type == "css") (void)d->Set(ctx, 1, v8::True(iso));
    v8::Local<v8::String> names[1] = { jstr(iso, "default") };
#if V8_MAJOR_VERSION > 15 || (V8_MAJOR_VERSION == 15 && V8_MINOR_VERSION >= 6)
    v8::Local<v8::Module> m = v8::Module::CreateSyntheticModule(iso, jstr(iso, u.c_str()), std::span<const v8::Local<v8::String>>(names, 1), synth_steps);
#else
    v8::Local<v8::Module> m = v8::Module::CreateSyntheticModule(iso, jstr(iso, u.c_str()), v8::MemorySpan<const v8::Local<v8::String>>(names, 1), synth_steps);
#endif
    std::lock_guard<std::mutex> lk(g_synth_mu);
    (*g_synth)[iso].emplace(m->GetIdentityHash(), SynthEnt{ v8::Global<v8::Module>(iso, m), v8::Global<v8::Array>(iso, d) });
    return m;
}
static v8::MaybeLocal<v8::Module> mod_build(JsCtx *c, const std::string &key, const std::string &body, const std::string &ctype, bool *load_failure = nullptr) {
    if (mod_rethrow_cached(c->iso, c->mod_errors, key)) return {};
    std::string t = mod_ktype(key);
    if (!mod_mime_matches(t, ctype)) {
        if (load_failure) *load_failure = true;
        mod_throw(c->iso, "Failed to load module script with an incompatible MIME type: " + mod_kurl(key));
        return {};
    }
    if (t.empty()) return mod_compile(c, key, body);
    v8::Local<v8::Module> m;
    v8::TryCatch caught(c->iso);
    if (!mod_synthetic(c->iso->GetCurrentContext(), mod_kurl(key), t, body, ctype).ToLocal(&m)) {
        mod_cache_script_error(c->iso, c->mod_errors, key, caught); return {};
    }
    c->mods[key].Reset(c->iso, m);
    c->mod_url.emplace(m->GetIdentityHash(), key);
    return m;
}
static bool mod_graph(JsCtx *c, v8::Local<v8::Module> root, bool *load_failure = nullptr) {
    v8::Isolate *iso = c->iso;
    std::string ref = c->doc && c->doc->url ? c->doc->url : "";
    const std::string root_key = mod_key(c, root);
    ModGraph graph;
    std::unordered_set<std::string> inspected;
    std::vector<v8::Local<v8::Module>> todo{root};
    while (!todo.empty()) {
        std::vector<std::string> need;
        std::vector<v8::Local<v8::Module>> next;
        for (v8::Local<v8::Module> m : todo) {
            std::string base = mod_key(c, m);
            if (!inspected.insert(base).second || c->mod_errors.count(base)) continue;
            std::vector<std::string> requests;
            {
                v8::TryCatch caught(iso);
                if (!mod_script_requests(iso, iso->GetCurrentContext(), m, base, true,
                                         mod_resolve, c->mod_errors, requests)) {
                    if (caught.HasCaught() && caught.CanContinue() && c->mod_errors.count(base)) continue;
                    if (caught.HasCaught()) caught.ReThrow();
                    return false;
                }
            }
            graph[base] = requests;
            for (const std::string &key : requests) {
                if (c->mod_errors.count(key)) continue;
                auto found = c->mods.find(key);
                if (found != c->mods.end()) next.push_back(found->second.Get(iso));
                else if (std::find(need.begin(), need.end(), key) == need.end()) need.push_back(key);
            }
        }
        double busy = c->busy_since.exchange(0);
        std::vector<std::string> urls;
        for (const std::string &key : need) urls.push_back(mod_kurl(key));
        mod_prefetch(urls, ref, [c](const std::string &u) { return c->mods.count(u) > 0; });
        if (busy > 0) c->busy_since = now_ms();
        for (size_t i = 0; i < need.size(); i++) {
            std::string body, ct;
            if (!mod_take(urls[i], ref, body, &ct)) {
                if (load_failure) *load_failure = true;
                return mod_throw(iso, "Failed to fetch module: " + urls[i]), false;
            }
            v8::Local<v8::Module> m;
            v8::TryCatch caught(iso);
            bool failed_load = false;
            if (!mod_build(c, need[i], body, ct, &failed_load).ToLocal(&m)) {
                if (failed_load && load_failure) *load_failure = true;
                /* Finish fetching the reachable graph before selecting a parse
                 * error: a network or MIME failure has priority over it. */
                if (!failed_load && caught.HasCaught() && caught.CanContinue() && c->mod_errors.count(need[i])) continue;
                if (caught.HasCaught()) caught.ReThrow();
                return false;
            }
            next.push_back(m);
        }
        todo = std::move(next);
    }
    return !mod_rethrow_graph_error(iso, c->mod_errors, graph, root_key);
}
static v8::MaybeLocal<v8::Module> mod_resolve_cb(v8::Local<v8::Context> ctx, v8::Local<v8::String> spec, v8::Local<v8::FixedArray> attrs, v8::Local<v8::Module> ref) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    JsCtx *c = ctx_of(ctx);
    std::string ty;
    if (!mod_attr_type(iso, ctx, attrs, 3, true, ty)) return {};
    auto it = c->mods.find(mod_mkey(mod_resolve(jcstr(iso, spec), mod_key(c, ref)), ty));
    if (it == c->mods.end()) return mod_throw(iso, "Failed to resolve module specifier \"" + jcstr(iso, spec) + "\""), v8::MaybeLocal<v8::Module>();
    return it->second.Get(iso);
}
static v8::MaybeLocal<v8::Promise> mod_eval(JsCtx *c, v8::Local<v8::Module> m, bool *load_failure = nullptr) {
    v8::Local<v8::Context> ctx = c->iso->GetCurrentContext();
    if (!mod_graph(c, m, load_failure)) return {};
    if (m->GetStatus() == v8::Module::kUninstantiated && !m->InstantiateModule(ctx, mod_resolve_cb).FromMaybe(false)) return {};
    /* a module importing itself (or a cycle member) while it is still being linked or evaluated: the running
       evaluation finishes it, and Evaluate() must not be re-entered */
    if (m->GetStatus() == v8::Module::kInstantiating || m->GetStatus() == v8::Module::kEvaluating) {
        v8::Local<v8::Promise::Resolver> r;
        if (!v8::Promise::Resolver::New(ctx).ToLocal(&r)) return {};
        (void)r->Resolve(ctx, v8::Undefined(c->iso));
        return r->GetPromise();
    }
    v8::Local<v8::Value> result;
    bool evaluated = m->Evaluate(ctx).ToLocal(&result);
    /* while the user interacts, the code refresh queue stores these later instead */
    if (user_idle()) for (auto &p : t_code_pending) {
        v8::Local<v8::Module> pm = p.first.Get(c->iso);
        if (pm->GetStatus() != v8::Module::kErrored) code_store(p.second, code_create(pm->GetUnboundModuleScript(), "module"));
    }
    t_code_pending.clear();
    if (!evaluated) return {};
    if (!result->IsPromise()) return mod_throw(c->iso, "Module evaluation did not return a promise"), v8::MaybeLocal<v8::Promise>();
    return result.As<v8::Promise>();
}
/* Starts fetching u's graph on a background thread; the page keeps running meanwhile. */
static void mod_job_start(JsCtx *c, std::unique_ptr<ModJob> j) {
    std::string ref = c->doc && c->doc->url ? c->doc->url : "";
    auto known = std::make_shared<std::unordered_set<std::string>>();
    for (auto &kv : c->mods) known->insert(kv.first);
    j->at = now_ms();
    std::thread([u = mod_kurl(j->url), ref, known, done = j->done] {
        mod_prefetch({ u }, ref, [&known](const std::string &k) { return known->count(k) > 0; });
        *done = true;
        if (js_wakeup) js_wakeup();
    }).detach();
    c->mod_jobs.push_back(std::move(j));
}
/* Compiles, links and evaluates u (its sources normally prefetched) and settles R with it. */
static void import_finish(JsCtx *c, v8::Local<v8::Promise::Resolver> R, const std::string &u) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    auto failure = c->mod_errors.find(u);
    if (failure != c->mod_errors.end()) { (void)R->Reject(ctx, failure->second.Get(iso)); return; }
    v8::TryCatch tc(iso);
    v8::Local<v8::Module> m;
    std::string body;
    auto it = c->mods.find(u);
    if (it != c->mods.end()) m = it->second.Get(iso);
    else {
        double busy = c->busy_since.exchange(0);
        std::string ref = c->doc && c->doc->url ? c->doc->url : "";
        std::string fu = mod_kurl(u), ct;
        mod_prefetch({ fu }, ref, [c](const std::string &k) { return c->mods.count(k) > 0; });
        bool ok = mod_take(fu, ref, body, &ct);
        if (busy > 0) c->busy_since = now_ms();
        if (ok) (void)mod_build(c, u, body, ct).ToLocal(&m);
        else mod_throw(iso, "Failed to fetch dynamically imported module: " + fu);
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
}
/* With the page console on: each import() as a number, a hash of the module and how it settled,
   so one that never settles shows (module URLs are not logged) */
static void import_report(const v8::FunctionCallbackInfo<v8::Value> &a, const char *how) {
    v8::Local<v8::Context> cx = a.GetIsolate()->GetCurrentContext();
    v8::Local<v8::Array> d = a.Data().As<v8::Array>();
    unsigned id = d->Get(cx, 0).ToLocalChecked()->Uint32Value(cx).FromMaybe(0);
    double ms = now_ms() - d->Get(cx, 1).ToLocalChecked()->NumberValue(cx).FromMaybe(0);
    std::string why;
    if (a.Length() && a[0]->IsNativeError()) {
        why = jcstr(a.GetIsolate(), a[0]);
        size_t cut = why.find("://");
        if (cut != std::string::npos) why.resize(why.rfind(' ', cut) == std::string::npos ? 0 : why.rfind(' ', cut));
        if (why.size() > 120) why.resize(120);
    }
    fprintf(stderr, "lumen: import #%u %s after %.0fms %s\n", id, how, ms, why.c_str());
}
/* a module as its disk-cache entry name (FNV-1a of the URL, see cache.c), never the URL */
static std::string mod_cache_name(const std::string &u) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : u) h = (h ^ ch) * 1099511628211ull;
    char k[24]; snprintf(k, sizeof k, "%016llx", (unsigned long long)h);
    return k;
}
static void import_trace(v8::Local<v8::Context> ctx, v8::Local<v8::Promise> p, const std::string &u, bool known) {
    static unsigned next;
    unsigned id = ++next;
    fprintf(stderr, "lumen: import #%u start %s%s\n", id, mod_cache_name(u).c_str(), known ? " compiled" : "");
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    v8::Local<v8::Array> d = v8::Array::New(iso, 2);
    (void)d->Set(ctx, 0, v8::Integer::NewFromUnsigned(iso, id));
    (void)d->Set(ctx, 1, v8::Number::New(iso, now_ms()));
    v8::Local<v8::Function> ok, bad;
    if (v8::Function::New(ctx, [](const v8::FunctionCallbackInfo<v8::Value> &a) { import_report(a, "resolved"); }, d).ToLocal(&ok) &&
        v8::Function::New(ctx, [](const v8::FunctionCallbackInfo<v8::Value> &a) { import_report(a, "rejected"); }, d).ToLocal(&bad))
        (void)p->Then(ctx, ok, bad);
}
static v8::MaybeLocal<v8::Promise> mod_dynamic(v8::Local<v8::Context> ctx, v8::Local<v8::Data>, v8::Local<v8::Value> res, v8::Local<v8::String> spec, v8::Local<v8::FixedArray> attrs) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    JsCtx *c = ctx_of(ctx);
    v8::Local<v8::Promise::Resolver> R;
    if (!v8::Promise::Resolver::New(ctx).ToLocal(&R)) return {};
    std::string base = res->IsString() ? jcstr(iso, res) : "";
    if (base.find(':') == std::string::npos || !base.compare(0, 6, "lumen:")) base = c->doc && c->doc->url ? c->doc->url : "";
    std::string ty;
    { v8::TryCatch tc(iso); if (!mod_attr_type(iso, ctx, attrs, 2, true, ty)) { (void)R->Reject(ctx, tc.Exception()); return R->GetPromise(); } }
    std::string u = mod_mkey(mod_resolve(jcstr(iso, spec), base), ty);
    if (g_log_level < 3) import_trace(ctx, R->GetPromise(), u, c->mods.count(u) > 0);
    if (u.empty()) {
        v8::TryCatch tc(iso);
        mod_throw(iso, "Failed to resolve module specifier \"" + jcstr(iso, spec) + "\"");
        (void)R->Reject(ctx, tc.Exception());
    } else if (c->mods.count(u) || c->mod_errors.count(u)) import_finish(c, R, u);
    else {
        auto j = std::make_unique<ModJob>();
        j->url = u;
        j->R.Reset(iso, R);
        mod_job_start(c, std::move(j));
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

static JsCtx *root_ctx(JsCtx *c) { while (c && c->parent) c = c->parent; return c; }
/* eval/Function source: strings reach here only while the realm's CSP restricts string compilation (evalGuard),
   code-like objects (TrustedScript) always. The ttEval hook returns the source to compile, false to block it,
   or undefined to leave it as is; a hook that throws blocks it (an EvalError). */
static v8::ModifyCodeGenerationFromStringsResult codegen_check(v8::Local<v8::Context> ctx, v8::Local<v8::Value> source, bool is_code_like) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    if (!source->IsString() && !(source->IsObject() && source.As<v8::Object>()->IsCodeLike(iso))) return { true, {} };
    JsCtx *c = ctx_of(ctx);
    if (!c || c->dead || c->tt_eval.IsEmpty()) return { true, source->IsString() ? source.As<v8::String>() : v8::MaybeLocal<v8::String>() };
    v8::TryCatch tc(iso);
    v8::Local<v8::Value> argv[2] = { source, v8::Boolean::New(iso, is_code_like) }, r;
    if (!c->tt_eval.Get(iso)->Call(ctx, v8::Undefined(iso), 2, argv).ToLocal(&r) || r->IsFalse()) return { false, {} };
    if (r->IsString()) return { true, r.As<v8::String>() };
    return { true, {} };
}
static void on_reject(v8::PromiseRejectMessage m) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    JsCtx *c = jctx(iso), *r = root_ctx(c);
    if (!r) return;
    v8::Local<v8::Promise> p = m.GetPromise();
    if (m.GetEvent() == v8::kPromiseHandlerAddedAfterReject) {
        for (size_t i = 0; i < r->rejs.size(); i++) if (r->rejs[i].p == p) { r->rejs.erase(r->rejs.begin() + (long)i); break; }
        return;
    }
    if (m.GetEvent() != v8::kPromiseRejectWithNoHandler) return;
    v8::Local<v8::Value> v = m.GetValue();
    std::string tr;
    bool has_stack = false;
    if (v->IsObject()) {
        v8::Local<v8::Value> st;
        has_stack = v.As<v8::Object>()->Get(iso->GetCurrentContext(), jstr(iso, "stack")).ToLocal(&st) && st->IsString() && jcstr(iso, st).find('\n') != std::string::npos;
    }
    if (!has_stack && g_log_level <= 2) {   /* where it was rejected; a later report has no stack */
        v8::Local<v8::StackTrace> t = v8::StackTrace::CurrentStackTrace(iso, 4);
        for (int i = 0; i < t->GetFrameCount(); i++) {
            v8::Local<v8::StackFrame> f = t->GetFrame(iso, i);
            v8::Local<v8::String> fn = f->GetFunctionName(), sn = f->GetScriptName();
            tr += "\n    at " + (fn.IsEmpty() ? std::string("?") : jcstr(iso, fn)) + " (" + (sn.IsEmpty() ? std::string("?") : jcstr(iso, sn)) + ":" + std::to_string(f->GetLineNumber()) + ":" + std::to_string(f->GetColumn()) + ")";
        }
    }
    if (r->rejs.size() < 256) r->rejs.push_back({ v8::Global<v8::Promise>(iso, p), v8::Global<v8::Value>(iso, v), c->id, tr });
}
/* After a task's microtasks: each still-unhandled rejection fires a cancelable unhandledrejection
   event at its window, and is logged unless the page prevented it. */
static void rej_flush(JsCtx *r) {
    if (r->rejs.empty() || r->depth) return;
    v8::Isolate *iso = r->iso;
    std::vector<JsCtx::Rej> list;
    list.swap(r->rejs);
    for (JsCtx::Rej &e : list) {
        JsCtx *t = js_ctx_by_id(e.ctx);
        if (!t || t->dead || t->api.IsEmpty()) continue;
        v8::HandleScope hs(iso);
        v8::Local<v8::Context> tc = t->ctx.Get(iso);
        v8::Context::Scope cs(tc);
        v8::Local<v8::Value> fn, prevented, reason = e.reason.Get(iso);
        v8::Local<v8::Value> argv[2] = { e.p.Get(iso), reason };
        if (t->api.Get(iso)->Get(tc, jstr(iso, "rejection")).ToLocal(&fn) && fn->IsFunction() &&
            jcall(t, fn.As<v8::Function>(), v8::Undefined(iso), 2, argv).ToLocal(&prevented) && prevented->BooleanValue(iso)) continue;
        if (g_log_level > 2) continue;
        std::string s = jcstr(iso, reason);
        if (reason->IsObject()) {
            v8::Local<v8::Value> st;
            if (reason.As<v8::Object>()->Get(tc, jstr(iso, "stack")).ToLocal(&st) && st->IsString()) s = jcstr(iso, st);
        }
        fprintf(stderr, "[js warn] Unhandled promise rejection: %.900s\n", (s + e.trace).c_str());
    }
    js_enter(r);
    iso->PerformMicrotaskCheckpoint();
    js_leave(r);
}

#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
/* Debug Android diagnostic, armed by an empty "jsprof" file next to v8cache: samples each new
   top-level page's JS for 90 s (or until 3 s after its first tap) and logs ticks per Lumen prelude function. A file
   reading "tail" ignores touches and also reports the last 15 s (the settled page). Page code is counted in
   aggregate, plus the line:column of its hottest functions; its function names and script URLs are
   never logged. */
#define JSPROF_MS 90000   /* the page window; a first tap ends it 3 s after */
static v8::CpuProfiler *g_prof; static JsCtx *g_prof_ctx; static double g_prof_end, g_prof_begun, g_prof_tap; static bool g_prof_tail;
static std::string jsprof_key(const v8::CpuProfileNode *n) {
    std::string sn = *v8::String::Utf8Value(g_prof_ctx->iso, n->GetScriptResourceName());
    std::string fn = n->GetFunctionNameStr();
    return sn == "lumen:prelude" ? "P " + (fn.empty() ? std::string("(anon)") : fn) + ":" + std::to_string(n->GetLineNumber())
         : sn.empty() && !fn.empty() ? "V " + fn : "<page>";
}
/* page code as its script's disk-cache entry name (FNV-1a of the URL, see cache.c) and line:column, never the URL */
static std::string jsprof_page_key(const v8::CpuProfileNode *n) {
    std::string sn = *v8::String::Utf8Value(g_prof_ctx->iso, n->GetScriptResourceName());
    uint64_t h = 1469598103934665603ull; for (unsigned char ch : sn) h = (h ^ ch) * 1099511628211ull;
    /* code without line numbers (WebAssembly, eval) also gets its URL scheme and V8's source type */
    char key[96], kind[40] = "";
    if (n->GetLineNumber() <= 0) {
        size_t c = sn.find(':'); std::string sch = c != std::string::npos && c < 12 ? sn.substr(0, c) : std::string("-");
        snprintf(kind, sizeof kind, " [%s type=%d len=%zu]", sch.c_str(), (int)n->GetSourceType(), sn.size());
    }
    snprintf(key, sizeof key, "@ %016llx:%d:%d%s", (unsigned long long)h, n->GetLineNumber(), n->GetColumnNumber(), kind);
    return key;
}
static void jsprof_walk(const v8::CpuProfileNode *n, std::unordered_map<std::string, int> &self,
                        std::unordered_map<std::string, int> &incl, std::vector<std::string> &stack, int &total) {
    std::string k = jsprof_key(n);
    int hits = (int)n->GetHitCount(); self[k] += hits; total += hits;
    if (k == "<page>" && hits) self[jsprof_page_key(n)] += hits;
    bool fresh = std::find(stack.begin(), stack.end(), k) == stack.end();
    if (fresh) stack.push_back(k);
    int before = total;
    for (int i = 0; i < n->GetChildrenCount(); i++) jsprof_walk(n->GetChild(i), self, incl, stack, total);
    if (fresh) { incl[k] += hits + (total - before); stack.pop_back(); }
}
static void jsprof_stop(bool report) {
    if (!g_prof) return;
    v8::Isolate *iso = g_prof_ctx->iso;
    v8::Isolate::Scope is(iso); v8::HandleScope hs(iso);
    v8::CpuProfile *pr = g_prof->StopProfiling(jstr(iso, "lumen"));
    if (pr && report) {
        std::unordered_map<std::string, int> self, incl; std::vector<std::string> stack; int total = 0;
        jsprof_walk(pr->GetTopDownRoot(), self, incl, stack, total);
        auto top = [&](std::unordered_map<std::string, int> &m, const char *what, size_t lim) {
            std::vector<std::pair<int, std::string>> v; for (auto &kv : m) v.push_back({ kv.second, kv.first });
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < lim; i++)
                __android_log_print(ANDROID_LOG_INFO, "LumenProf", "%s %5.1f%% %d %s", what, v[i].first * 100.0 / (total ? total : 1), v[i].first, v[i].second.c_str());
        };
        __android_log_print(ANDROID_LOG_INFO, "LumenProf", "jsprof ticks=%d ms=%.0f", total, (pr->GetEndTime() - pr->GetStartTime()) / 1000.0);
        top(self, "self", 60);
        incl.erase("<page>");
        top(incl, "incl", 60);
        if (g_prof_tap > 0 || g_prof_tail) {   /* the first tap's 2.5 s or the last 15 s, from the samples' own times */
            int64_t from = g_prof_tail ? pr->GetEndTime() - 15000000 : pr->GetStartTime() + (int64_t)((g_prof_tap - g_prof_begun) * 1000);
            int64_t to = g_prof_tail ? pr->GetEndTime() : from + 2500000;
            std::unordered_map<std::string, int> wself, wincl; int wtotal = 0;
            for (int i = 0; i < pr->GetSamplesCount(); i++) {
                int64_t t = pr->GetSampleTimestamp(i);
                if (t < from || t >= to) continue;
                const v8::CpuProfileNode *n = pr->GetSample(i);
                std::string k = jsprof_key(n);
                wself[k == "<page>" ? jsprof_page_key(n) : k]++; wtotal++;
                std::vector<std::string> seen;
                for (const v8::CpuProfileNode *u = n; u && u->GetParent(); u = u->GetParent()) {
                    std::string uk = jsprof_key(u);
                    if (uk == "<page>") uk = jsprof_page_key(u);
                    if (std::find(seen.begin(), seen.end(), uk) == seen.end()) { seen.push_back(uk); wincl[uk]++; }
                }
            }
            __android_log_print(ANDROID_LOG_INFO, "LumenProf", "%s samples=%d start_ms=%.0f", g_prof_tail ? "tail_window" : "tap_window", wtotal,
                                g_prof_tail ? (pr->GetEndTime() - pr->GetStartTime()) / 1000.0 - 15000 : g_prof_tap - g_prof_begun);
            total = wtotal;
            top(wself, "tself", 40);
            top(wincl, "tincl", 40);
        }
        js_native_report();
    }
    if (pr) pr->Delete();
    g_prof->Dispose(); g_prof = nullptr; g_prof_ctx = nullptr; g_prof_tap = 0;
}
static void jsprof_start(JsCtx *c) {
    if (g_prof) jsprof_stop(now_ms() > g_prof_end - JSPROF_MS + 5000);
    if (g_code_dir.size() < 7 || access((g_code_dir.substr(0, g_code_dir.size() - 7) + "jsprof").c_str(), F_OK)) return;
    { char mode[8] = ""; FILE *f = fopen((g_code_dir.substr(0, g_code_dir.size() - 7) + "jsprof").c_str(), "r");
      if (f) { if (!fgets(mode, sizeof mode, f)) mode[0] = 0; fclose(f); }
      g_prof_tail = !strncmp(mode, "tail", 4); }
    v8::Isolate::Scope is(c->iso); v8::HandleScope hs(c->iso);
    g_prof = v8::CpuProfiler::New(c->iso); g_prof_ctx = c; g_prof_end = now_ms() + JSPROF_MS;
    g_prof->SetSamplingInterval(500);
    g_prof->StartProfiling(jstr(c->iso, "lumen"), v8::CpuProfilingOptions(v8::kLeafNodeLineNumbers));
    g_prof_begun = now_ms();
}
/* a tap while the page's profile runs: report its window 3 s later instead of restarting */
static void jsprof_tap(JsCtx *c) {
    if (g_prof && g_prof_ctx == c) { if (!g_prof_tap && !g_prof_tail) { g_prof_tap = now_ms(); g_prof_end = g_prof_tap + 3000; } }
    else if (!g_prof_tail) jsprof_start(c);
}
bool jsprof_active(void) { return g_prof != nullptr; }
static void jsprof_tick(JsCtx *c) { if (g_prof && g_prof_ctx == c && now_ms() > g_prof_end) jsprof_stop(true); }
static void jsprof_free(JsCtx *c) { if (g_prof && g_prof_ctx == c) jsprof_stop(true); }
#else
static void jsprof_start(JsCtx *) {}
static void jsprof_tap(JsCtx *) {}
static void jsprof_tick(JsCtx *) {}
static void jsprof_free(JsCtx *) {}
#endif

#ifdef __ANDROID__
/* Android discards stderr; with LUMEN_CONSOLE set, the console and error reports go to logcat */
static void *stderr_pump(void *arg) {
    FILE *f = fdopen((int)(intptr_t)arg, "r");
    char line[2048];
    while (f && fgets(line, sizeof line, f)) __android_log_write(ANDROID_LOG_INFO, "LumenJS", line);
    return nullptr;
}
static void stderr_to_logcat() {
    int p[2];
    if (pipe(p)) return;
    setvbuf(stderr, nullptr, _IOLBF, 0);
    dup2(p[1], 2); close(p[1]);
    pthread_t t;
    if (pthread_create(&t, nullptr, stderr_pump, (void *)(intptr_t)p[0]) == 0) pthread_detach(t);
}
#endif

extern "C" {

void js_global_init(const char *argv0) {
    if (g_platform) return;
    const char *lv = getenv("LUMEN_CONSOLE");
    if (lv) {
        g_log_level = atoi(lv);
#ifdef __ANDROID__
        stderr_to_logcat();
#endif
    }
    const char *icu = getenv("LUMEN_ICU_DATA");
    if (!icu && access("/opt/homebrew/opt/v8/libexec/icudtl.dat", R_OK) == 0) icu = "/opt/homebrew/opt/v8/libexec/icudtl.dat";
    v8::V8::InitializeICUDefaultLocation(argv0, icu);
    v8::V8::InitializeExternalStartupData(argv0);
    g_platform = v8::platform::NewDefaultPlatform().release();
    g_task_platform = js_task_platform(g_platform).release();
    v8::V8::InitializePlatform(g_task_platform);
    /* page loads allocate in bursts: start the young generation at 4 MB instead of growing from 1 MB through many scavenges */
    v8::V8::SetFlagsFromString("--min-semi-space-size=4");
    if (const char *vf = getenv("LUMEN_V8_FLAGS")) v8::V8::SetFlagsFromString(vf);
    v8::V8::Initialize();
}

#if defined(__ANDROID__) && !defined(NDEBUG)
/* debug: main-thread GC pauses of 30 ms or more, by type and heap size */
static double g_gc_t0;
static void gc_pause_begin(v8::Isolate *, v8::GCType, v8::GCCallbackFlags) { g_gc_t0 = now_ms(); }
static void gc_pause_end(v8::Isolate *iso, v8::GCType type, v8::GCCallbackFlags flags) {
    double ms = now_ms() - g_gc_t0;
    if (ms < 30) return;
    v8::HeapStatistics hs; iso->GetHeapStatistics(&hs);
    __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "gc_pause ms=%.0f type=%d flags=%x used_mb=%zu total_mb=%zu external_mb=%zu",
        ms, (int)type, (unsigned)flags, hs.used_heap_size() >> 20, hs.total_heap_size() >> 20, hs.external_memory() >> 20);
}
#endif
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
JsCtx *js_ctx_by_id(uint32_t id) {
    for (JsCtx *c : g_ctxs) if (c->id == id) return c->dead ? nullptr : c;
    return nullptr;
}
JsCtx *js_ctx_of_window(uint32_t wid) {   /* the newest, while the old page lingers through a navigation */
    JsCtx *r = nullptr;
    for (JsCtx *c : g_ctxs) if (wid && c->wid == wid && !c->parent && !c->dead && (!r || c->id > r->id)) r = c;
    return r;
}
void js_set_window(JsCtx *c, uint32_t wid, uint32_t opener) { if (c) { c->wid = wid; c->opener = opener; } }
JsCtx *owner_ctx(Document *d) {
    if (!d) return nullptr;
    for (JsCtx *c : g_ctxs) if (c->doc == d) return c;
    for (JsCtx *c : g_ctxs) if (std::find(c->docs.begin(), c->docs.end(), d) != c->docs.end()) return c;
    /* Associated template owners have the original document's realm, but no
       browsing context. They are owned by that Document, never twice in docs. */
    for (JsCtx *c : g_ctxs) {
        if (c->doc && c->doc->template_contents_owner == d) return c;
        for (Document *owned : c->docs) if (owned->template_contents_owner == d) return c;
    }
    return nullptr;
}
bool js_read_blob(JsCtx *owner, const char *url, unsigned char **bytes_out, size_t *len_out, char **type_out) {
    if (bytes_out) *bytes_out = nullptr;
    if (len_out) *len_out = 0;
    if (type_out) *type_out = nullptr;
    if (!owner || owner->dead || owner->ctx.IsEmpty() || !url || strncmp(url, "blob:", 5)) return false;
    const std::string origin = ctx_origin_key(owner);
    for (JsCtx *c : g_ctxs) {
        if (c->dead || c->iso != owner->iso || c->ctx.IsEmpty() || ctx_origin_key(c) != origin) continue;
        JS_ENTER(c);
        v8::TryCatch tc(iso);
        v8::Local<v8::Value> realm, fn, result, bytes, type;
        if (!ctx->Global()->GetPrivate(ctx, v8::Private::ForApi(iso, jstr(iso, "lumen:realm"))).ToLocal(&realm) || !realm->IsObject()) continue;
        if (!realm.As<v8::Object>()->Get(ctx, jstr(iso, "blob")).ToLocal(&fn) || !fn->IsFunction()) continue;
        v8::Local<v8::Value> arg = jstr(iso, url);
        if (!fn.As<v8::Function>()->Call(ctx, realm, 1, &arg).ToLocal(&result) || !result->IsArray()) continue;
        auto r = result.As<v8::Array>();
        if (!r->Get(ctx, 0).ToLocal(&bytes) || !bytes->IsUint8Array() || !r->Get(ctx, 1).ToLocal(&type)) continue;
        auto view = bytes.As<v8::Uint8Array>();
        const size_t len = view->ByteLength();
        if (len_out) *len_out = len;
        if (bytes_out) {
            unsigned char *copy = (unsigned char *)xmalloc(len + 1);
            if (len) view->CopyContents(copy, len);
            copy[len] = 0; *bytes_out = copy;
        }
        if (type_out) *type_out = xstrdup(jcstr(iso, type).c_str());
        return true;
    }
    return false;
}

JsCtx *js_new(Document *d, const JsHost *host) { return js_new_ex(d, host, nullptr, nullptr); }

static unsigned viewport_changes(JsCtx *context, bool commit = true) {
    if (!context->host.viewport) return 0;
    float width = 0, height = 0, scroll_x = 0, scroll_y = 0, dpr = 1;
    context->host.viewport(context->host.ud, &width, &height, &scroll_x, &scroll_y, &dpr);
    return commit ? context->viewport_state.update(width, height, dpr, scroll_x, scroll_y)
                  : context->viewport_state.changes(width, height, dpr, scroll_x, scroll_y);
}

JsCtx *js_new_ex(Document *d, const JsHost *host, JsCtx *parent, Node *frame) {
    JsCtx *c = new JsCtx();
    c->id = g_next_ctx_id++;
    c->parent = parent; c->frame_el = frame;
    if (frame) frame->refcount++;
    if (parent) parent->kids.push_back(c);
    c->doc = d;
    if (host) c->host = *host;
    viewport_changes(c);
    c->t0 = now_ms();
    c->thread = std::this_thread::get_id();
    g_ctxs.push_back(c);
    jsg_install_hooks();
    if (parent) c->iso = parent->iso;
    else {
    v8::Isolate::CreateParams cp;
    c->alloc = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    cp.array_buffer_allocator = c->alloc;
    c->iso = v8::Isolate::Allocate();
    // V8 caches its foreground runner during initialization. The callback
    // captures only the process-lifetime wake function, never a page pointer.
    c->foreground_tasks = js_register_foreground_tasks(c->iso, [wake = js_wakeup] { if (wake) wake(); });
    v8::Isolate::Initialize(c->iso, cp);
    js_wasm_init(c->iso);
    c->iso->SetData(0, c);
    c->iso->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    c->iso->SetPromiseRejectCallback(on_reject);
#if defined(__ANDROID__) && !defined(NDEBUG)
    c->iso->AddGCPrologueCallback(gc_pause_begin);
    c->iso->AddGCEpilogueCallback(gc_pause_end);
#endif
    c->iso->SetHostImportModuleDynamicallyCallback(mod_dynamic);
    c->iso->SetModifyCodeGenerationFromStringsCallback(codegen_check);
    c->iso->SetHostInitializeImportMetaObjectCallback(mod_meta);
    }
    v8::Isolate *iso = c->iso;
    v8::Isolate::Scope is(iso);
    v8::HandleScope hs(iso);
    v8::Local<v8::Context> ctx = v8::Context::New(iso);
    c->ctx.Reset(iso, ctx);
    ctx->SetAlignedPointerInEmbedderData(1, c, kTag);
    if (!parent) jsprof_start(c);
    ctx->SetSecurityToken(v8::String::NewFromUtf8(iso, ctx_origin_key(c).c_str(), v8::NewStringType::kInternalized).ToLocalChecked());
    v8::Context::Scope cs(ctx);
    v8::Local<v8::ObjectTemplate> tmpl = v8::ObjectTemplate::New(iso);
    tmpl->SetInternalFieldCount(2);
    c->node_tmpl.Reset(iso, tmpl);
    v8::Local<v8::Object> N = v8::Object::New(iso);
    js_install_native(c, N);
    js_wasm_install(ctx, N);
    js_clone_install(ctx, N);
    js_event_target_install(ctx, N);
    JsCtx *bc_root = c; while (bc_root->parent) bc_root = bc_root->parent;
    c->broadcast = js_broadcast_install(ctx, N, js_broadcast_identity(c), bc_root->foreground_tasks,
                                      [c] { return js_context_fully_active(c); });
    js_url_install(ctx, N);
    js_crypto_install(ctx, N); js_encoding_install(ctx, N); js_compress_install(ctx, N);
    v8::TryCatch tc(iso);
    v8::ScriptOrigin origin(jstr(iso, "lumen:prelude"));
    v8::Local<v8::Script> s;
    v8::Local<v8::Value> fv, api;
    v8::Local<v8::Value> argv[2] = { N, ctx->Global() };
    /* The prelude's bytecode, made by the first context, spares every later page compiling it. */
    static std::mutex prelude_mu;
    static std::shared_ptr<std::string> prelude_code;
    std::shared_ptr<std::string> code;
    { std::lock_guard<std::mutex> lk(prelude_mu); code = prelude_code; }
    v8::ScriptCompiler::Source psrc(jstr(iso, kPrelude, (int)sizeof kPrelude - 1), origin,
        code ? new v8::ScriptCompiler::CachedData((const uint8_t *)code->data(), (int)code->size()) : nullptr);
    bool compiled = v8::ScriptCompiler::Compile(ctx, &psrc, code ? v8::ScriptCompiler::kConsumeCodeCache : v8::ScriptCompiler::kNoCompileOptions).ToLocal(&s);
    if (compiled && s->Run(ctx).ToLocal(&fv) && fv->IsFunction() &&
        fv.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 2, argv).ToLocal(&api) && api->IsObject()) {
        if (!code || psrc.GetCachedData()->rejected) {
            std::unique_ptr<v8::ScriptCompiler::CachedData> cd(v8::ScriptCompiler::CreateCodeCache(s->GetUnboundScript()));
            if (cd) { std::lock_guard<std::mutex> lk(prelude_mu); prelude_code = std::make_shared<std::string>((const char *)cd->data, (size_t)cd->length); }
        }
        v8::Local<v8::Object> ao = api.As<v8::Object>();
        c->api.Reset(iso, ao);
        auto get = [&](const char *k, v8::Global<v8::Function> &dst) {
            v8::Local<v8::Value> v;
            if (ao->Get(ctx, jstr(iso, k)).ToLocal(&v) && v->IsFunction()) dst.Reset(iso, v.As<v8::Function>());
        };
        get("protoFor", c->protoFor);
        get("fire", c->fire);
        get("report", c->report);
        get("viewportChanged", c->viewportChanged);
        get("ttScript", c->tt_script);
        get("ttParserScript", c->tt_parser_script);
        get("ttEval", c->tt_eval);
        /* the document's CSP: after protoFor, since reading the <meta> elements wraps nodes */
        v8::Local<v8::Value> ti;
        if (ao->Get(ctx, jstr(iso, "ttInit")).ToLocal(&ti) && ti->IsFunction()) (void)ti.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 0, nullptr);
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

void (*js_pick_files)(bool, const char *) = nullptr;
static std::mutex g_pick_mu;
static JsCtx *g_pick_ctx;
static bool g_pick_ready;
static std::vector<std::string> g_pick_paths;
void js_files_picked(const char *const *paths, int n) {
    {
        std::lock_guard<std::mutex> l(g_pick_mu);
        g_pick_paths.clear();
        for (int i = 0; i < n; i++) g_pick_paths.push_back(paths[i]);
        g_pick_ready = true;
    }
    if (js_wakeup) js_wakeup();
}
void js_pick_begin(JsCtx *c, bool multiple, const char *accept) {
    { std::lock_guard<std::mutex> l(g_pick_mu); g_pick_ctx = c; g_pick_ready = false; }
    if (const char *env = getenv("LUMEN_FILE_PICK")) {   /* automation: colon-separated paths instead of a dialog */
        std::vector<std::string> v; std::string s(env); size_t p = 0, q;
        while ((q = s.find(':', p)) != std::string::npos) { if (q > p) v.push_back(s.substr(p, q - p)); p = q + 1; }
        if (p < s.size()) v.push_back(s.substr(p));
        std::vector<const char *> pv; for (auto &x : v) pv.push_back(x.c_str());
        js_files_picked(pv.data(), (int)pv.size());
    } else if (js_pick_files) js_pick_files(multiple, accept);
    else js_files_picked(nullptr, -1);
}
static void pick_deliver(JsCtx *c) {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> l(g_pick_mu);
        if (g_pick_ctx != c || !g_pick_ready) return;
        g_pick_ready = false; g_pick_ctx = nullptr; paths.swap(g_pick_paths);
    }
    if (c->pick_cb.IsEmpty()) return;
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = c->ctx.Get(iso);
    v8::Local<v8::Function> fn = c->pick_cb.Get(iso);
    c->pick_cb.Reset();
    v8::Local<v8::Array> arr = v8::Array::New(iso);
    uint32_t k = 0;
    for (auto &p : paths) {
        size_t n = 0; char *b = read_file(p.c_str(), &n);
        if (!b) continue;
        v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, n);
        if (n) memcpy(ab->Data(), b, n);
        free(b);
        size_t sl = p.find_last_of('/');
        v8::Local<v8::Object> o = v8::Object::New(iso);
        (void)o->Set(ctx, jstr(iso, "name"), jstr(iso, sl == std::string::npos ? p.c_str() : p.c_str() + sl + 1));
        (void)o->Set(ctx, jstr(iso, "data"), ab);
        (void)arr->Set(ctx, k++, o);
    }
    v8::Local<v8::Value> argv[1] = { paths.empty() ? v8::Local<v8::Value>(v8::Null(iso)) : v8::Local<v8::Value>(arr) };
    (void)jcall(c, fn, ctx->Global(), 1, argv);
}
void js_debug_profile(JsCtx *c) { if (c && !c->parent) jsprof_tap(c); }
/* A node keeps the wrapper from its creation realm after adoption. Retire all
 * contexts in this subtree before releasing any of their DOM storage, so a
 * child document cannot invalidate a live ancestor's wrapped-node list. */
static void js_free_collect(JsCtx *c, std::vector<Node *> &roots, std::vector<Document *> &docs) {
    if (!c) return;
    jsprof_free(c);
    { std::lock_guard<std::mutex> l(g_pick_mu); if (g_pick_ctx == c) g_pick_ctx = nullptr; }
    workers_kill(c);
    for (JsCtx *k : std::vector<JsCtx *>(c->kids)) js_free_collect(k, roots, docs);
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
    wa_close_owner(c); mic_close_owner(c); cam_close_owner(c); screen_close_owner(c); rec_close_owner(c); gl_close_owner(c); webgpu_close_owner(c); rtc_ctx_free(c);
    g_ctxs.erase(std::remove(g_ctxs.begin(), g_ctxs.end(), c), g_ctxs.end());
    c->players.clear();
    docs.insert(docs.end(), c->docs.begin(), c->docs.end());
    c->docs.clear();
    if (c->parent) docs.push_back(c->doc);
    {
        v8::Isolate::Scope is(c->iso);
        js_broadcast_deactivate(c->broadcast);
        v8::HandleScope hs(c->iso);
        js_cancel_fetches(c);
        c->ordered_scripts.clear(); c->written_deferred.clear(); c->pending_scripts = 0;
        for (auto &kv : c->sockets) { net_ws_release(kv.second->ws); kv.second->cb.Reset(); delete kv.second; } c->sockets.clear();
        c->timer_clear();
        c->rafs.clear();
        c->video_frame_pump.Reset(); c->resize_pump.Reset(); c->intersection_pump.Reset(); c->media_pump.Reset();
        c->anims.clear();
        c->protos.clear();
        c->protoFor.Reset(); c->fire.Reset(); c->report.Reset(); c->viewportChanged.Reset(); c->pick_cb.Reset(); c->rejs.clear();   /* before Dispose: ~JsCtx must not release handles */
        c->tt_script.Reset(); c->tt_parser_script.Reset(); c->tt_eval.Reset(); c->code_like.Reset();
        c->api.Reset(); c->node_tmpl.Reset();
        c->mods.clear(); c->mod_errors.clear(); c->mod_url.clear(); c->mod_jobs.clear(); c->code_refresh.clear(); if (!c->parent) mod_synth_drop(c->iso);
        for (Node *n : c->wrapped) { delete static_cast<v8::Global<v8::Object> *>(n->js); n->js = nullptr; }
        for (Node *n : c->wrapped)
            if (!n->parent && n->type != NODE_DOCUMENT && !n->refcount && !n->host) roots.push_back(n);
        c->ctx.Reset();
    }
    if (!c->parent) {
        c->foreground_tasks->Close();
        // This isolate never owns a libplatform foreground runner. Its shutdown
        // helper assumes one exists; close our runner and unregister it instead.
        c->iso->Dispose();
        js_unregister_foreground_tasks(c->iso);
        delete c->alloc;
    }
    delete c;
}
void js_free(JsCtx *c) {
    if (!c) return;
    /* Same-origin frame bridges already retain shared contexts until their tab
     * retires. Keep that boundary explicit for callers other than js_tick. */
    if (c->parent && c->xref) { frame_kill(c); c->dead_at = 0; return; }
    Document *root_document = c->doc;
    const bool child_context = c->parent != nullptr;
    std::vector<Node *> roots;
    std::vector<Document *> docs;
    js_free_collect(c, roots, docs);
    /* Parent links were inspected before any destruction: document children,
     * shadow roots and retained template contents are not detached roots.
     * Deduplicate defensively, and keep every ownerDocument alive through the
     * detached-node release phase. The caller owns a top-level page document. */
    std::unordered_set<Node *> seen_roots;
    for (Node *n : roots) if (seen_roots.insert(n).second) node_free_tree(n);
    /* The Page can keep its DOM while its JavaScript context sleeps. Nodes in
     * that tree may still belong to an adopted auxiliary document, including
     * a template's associated inert owner. Transfer those native documents to
     * the root and let doc_free release the whole ownership graph in two
     * phases; no owner metadata can disappear before another tree's nodes. */
    for (Document *d : docs) if (d != root_document) doc_retire_owned(root_document, d);
    /* Drain every retired document's native resource references before any
     * retired tree is released; the Page's own DOM/resources remain alive. */
    doc_clear_retired_trees(root_document);
    if (child_context) doc_free(root_document);
}

/* Written inline scripts execute in the nested parser before write() returns.
   A pending external blocking script still follows the existing synchronous
   document-write fetch path after the enclosing script returns. */
static void run_written_script(JsCtx *c, Node *script) {
    if (!js_prepare_script(script)) return;
    const char *src = jsg_script_src(script);
    char *url = src && *src ? url_join(doc_base(c->doc), src) : nullptr;
    if (jsg_module_script(script)) {
        size_t length = 0; char *text = src ? nullptr : jsg_script_text(script, &length);
        js_run_module_inserted(c, script, src ? nullptr : text, length, src ? (url ? url : "") : c->doc->url);
        free(text); free(url); return;
    }
    if (!src) {
        size_t length = 0; char *text = jsg_script_text(script, &length);
        js_run_prepared_classic(c, script, text ? text : "", length, c->doc->url, false, true);
        free(text); return;
    }
    if (node_has_attr(script, "async")) {
        js_queue_classic_script(c, script, url, false); free(url); return;
    }
    if (node_has_attr(script, "defer")) {
        js_queue_written_deferred(c, script, url); free(url); return;
    }
    NetResponse *response = nullptr;
    if (url) {
        NetRequest *request = net_request_new("GET", url);
        net_request_set_fetch_metadata(request, c->doc->url, "script", node_has_attr(script, "crossorigin") ? "cors" : "no-cors");
        char ref[4200];
        if (doc_referrer(c->doc, url, node_attr(script, "referrerpolicy"), ref, sizeof ref)) headers_add(&request->headers, "Referer", ref);
        response = net_fetch_sync(request);
    }
    if (response && !response->error && response->status >= 200 && response->status < 300 &&
        net_response_mime_allowed(response, NET_MIME_SCRIPT))
        js_run_prepared_classic(c, script, response->body ? response->body : "", response->body ? response->body_len : 0,
                                response->url ? response->url : url, true, true);
    else js_parser_script_error(c, script, true);
    if (response) net_response_free(response);
    free(url);
}
static void run_written(JsCtx *c, size_t from) {
    if (from >= c->written.size()) return;
    std::vector<Node *> list(c->written.begin() + (std::ptrdiff_t)from, c->written.end());
    c->written.resize(from);
    const uint64_t generation = c->parser_generation;
    for (Node *script : list) {
        if (c->dead || generation != c->parser_generation) return;
        run_written_script(c, script);
    }
}
void js_run_written_inline(JsCtx *c) {
    const uint64_t generation = c->parser_generation;
    while (!c->dead && generation == c->parser_generation && !c->written.empty()) {
        Node *script = c->written.front();
        if (jsg_module_script(script) || (jsg_script_src(script) && !node_has_attr(script, "async") && !node_has_attr(script, "defer"))) break;
        c->written.erase(c->written.begin());
        run_written_script(c, script);
    }
}
/* Trusted Types: an inline script runs its script text slot's value (the ttScript hook); false when blocked */
static bool tt_script_text(JsCtx *c, Node *script, const char *&src, size_t &n, std::string &buf) {
    if (!c->tt_on || !script || !src || c->tt_script.IsEmpty() || node_attr(script, "src")) return true;
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::Local<v8::Value> argv[2] = { jwrap(c, script), jsrc(iso, src, n) }, r;
    if (!c->tt_script.Get(iso)->Call(ctx, v8::Undefined(iso), 2, argv).ToLocal(&r) || !r->IsString()) return false;
    buf = jcstr(iso, r); src = buf.c_str(); n = buf.size();
    return true;
}
uint64_t js_parser_generation(JsCtx *c) { return c ? c->parser_generation : 0; }
void js_document_open(JsCtx *c) {
    ++c->parser_generation; c->parser_defer_next = 0;
    frames_parser_cancel(c);
    js_cancel_fetches(c, true);
    c->ordered_scripts.clear(); c->written_deferred.clear(); c->pending_scripts = 0;
    c->written.clear();
    c->doc->html = c->doc->head = c->doc->body = nullptr;
    c->load_deferred = false; c->load_synced = false;
    /* Opening during load/owner-load/pageshow replaces the parser, not the
       active completion cycle. A later open starts another cycle. */
    if (c->load_phase != DocumentLoadPhase::InProgress)
        c->load_phase = DocumentLoadPhase::Pending;
    js_set_ready_state(c, 0);
}
Node *js_parser_custom_element(JsCtx *c, Node *element, Node *intended_parent) {
    if (!c || c->dead || !element || !intended_parent) return nullptr;
    for (Node *node = intended_parent; node; node = node->parent ? node->parent : node->host)
        if (node->flags & NF_INERT) return nullptr;
    JsCtx *owner = owner_ctx(intended_parent->doc);
    if (!owner || owner->dead || owner->api.IsEmpty()) return nullptr;
    JS_ENTER(owner);
    v8::Local<v8::Value> function, result;
    if (!owner->api.Get(iso)->Get(ctx, jstr(iso, "ceParserCreate")).ToLocal(&function) || !function->IsFunction()) return nullptr;
    auto attributes = v8::Array::New(iso, element->nattrs);
    for (int i = 0; i < element->nattrs; ++i) {
        const Attr &attribute = element->attrs[i];
        v8::Local<v8::Value> pair[2] = { jstr(iso, attribute.name), jstr(iso, attribute.value) };
        (void)attributes->CreateDataProperty(ctx, (uint32_t)i, v8::Array::New(iso, pair, 2));
    }
    v8::Local<v8::Value> args[4] = { jstr(iso, element->tag),
        element->is_value ? v8::Local<v8::Value>(jsrc(iso, element->is_value, element->is_value_len)) : v8::Local<v8::Value>(v8::Null(iso)),
        jwrap(owner, &intended_parent->doc->node), attributes };
    if (!jcall(owner, function.As<v8::Function>(), v8::Undefined(iso), 4, args).ToLocal(&result)) return nullptr;
    return junwrap(result);
}
void js_parser_custom_inserted(JsCtx *c, Node *element, Node *parent, Node *previous, Node *next) {
    if (!c || c->dead || !element || !parent) return;
    JsCtx *owner = owner_ctx(parent->doc);
    if (!owner || owner->dead || owner->api.IsEmpty()) return;
    JS_ENTER(owner);
    v8::Local<v8::Value> function, result;
    if (!owner->api.Get(iso)->Get(ctx, jstr(iso, "ceParserInserted")).ToLocal(&function) || !function->IsFunction()) return;
    v8::Local<v8::Value> args[4] = { jwrap(owner, element), jwrap(owner, parent),
        previous ? v8::Local<v8::Value>(jwrap(owner, previous)) : v8::Local<v8::Value>(v8::Null(iso)),
        next ? v8::Local<v8::Value>(jwrap(owner, next)) : v8::Local<v8::Value>(v8::Null(iso)) };
    (void)jcall(owner, function.As<v8::Function>(), v8::Undefined(iso), 4, args).ToLocal(&result);
}
bool js_parser_script_closed(JsCtx *c, Node *script, const char *source, size_t length) {
    if (!c || c->dead) return false;
    if (c->tt_parser_script.IsEmpty()) return !c->tt_on;
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Value> args[2] = { jwrap(c, script), jsrc(iso, source ? source : "", source ? length : 0) }, result;
    return c->tt_parser_script.Get(iso)->Call(ctx, v8::Undefined(iso), 2, args).ToLocal(&result);
}
/* HTML preparation clears parser state before its early returns. An empty or
   unsupported parser script can therefore be populated and run dynamically later. */
bool js_prepare_script(Node *script) {
    if (!script || !jsg_is_script(script) || (script->flags & NF_SCRIPT_STARTED)) return false;
    const bool parser = (script->flags & NF_PARSER_INSERTED) != 0;
    script->flags &= ~NF_PARSER_INSERTED;
    if (parser && !node_has_attr(script, "async")) script->flags &= ~NF_SCRIPT_NO_FORCE_ASYNC;
    bool text = false;
    for (Node *n = script->first; n && !text; n = n->next)
        text = (n->type == NODE_TEXT || n->type == NODE_CDATA) && n->text_len != 0;
    if (!jsg_script_src(script) && !text) return false;
    if (!(script->flags & NF_CONNECTED)) return false;
    const bool module = jsg_module_script(script), classic = jsg_classic_script_type(script);
    if (!module && !classic) return false;
    if (parser) script->flags |= NF_PARSER_INSERTED | NF_SCRIPT_NO_FORCE_ASYNC;
    script->flags |= NF_SCRIPT_STARTED;
    if (classic && node_has_attr(script, "nomodule")) return false;
    return true;
}
/* Parser execution retains its insertion point through the element load event.
   The destructive-write counter covers script evaluation, not that event. */
static void run_classic_script(JsCtx *c, Node *script, const char *src, size_t n, const char *name,
                               bool parser_point, bool external, bool preserve_anchor = false) {
    if (!c || c->dead) return;
    if (script && !(script->flags & NF_SCRIPT_STARTED) && !js_prepare_script(script)) return;
    if (script) { JS_ENTER(c); jwrap(c, script); } // open() may retire the parser record during evaluation.
    std::string tt_buf;
    if (!external && !tt_script_text(c, script, src, n, tt_buf)) return;
    Node *prev = c->current_script;
    const bool previous_point = c->parser_write_point;
    Node *previous_anchor = c->parser_write_script;
    if (!preserve_anchor) c->parser_write_script = parser_point ? script : nullptr;
    size_t written = c->written.size();
    c->current_script = script; c->parser_write_point = parser_point;
    if (external) c->ignore_destructive_writes++;
    run_source(c, src, n, name, external);
    c->current_script = prev;
    if (c->written.size() > written) run_written(c, written);
    if (external) c->ignore_destructive_writes--;
    if (!c->dead && script && external) {
        JS_ENTER(c);
        jfire(c, script, "load");
    }
    if (c->written.size() > written) run_written(c, written);
    c->parser_write_point = previous_point;
    c->parser_write_script = previous_anchor;
}
void js_parser_script_error(JsCtx *c, Node *script, bool parser_point) {
    if (!c || c->dead || !script) return;
    Node *previous_script = c->current_script, *previous_anchor = c->parser_write_script;
    const bool previous_point = c->parser_write_point;
    const size_t written = c->written.size();
    c->current_script = nullptr; c->parser_write_point = parser_point;
    c->parser_write_script = parser_point ? script : nullptr;
    { JS_ENTER(c); jwrap(c, script); jfire(c, script, "error"); }
    if (c->written.size() > written) run_written(c, written);
    c->current_script = previous_script; c->parser_write_point = previous_point;
    c->parser_write_script = previous_anchor;
}
void js_run_prepared_classic(JsCtx *c, Node *script, const char *src, size_t n, const char *url,
                              bool external, bool parser_point) {
    run_classic_script(c, script, src, n, url, parser_point, external);
}
void js_run_script(JsCtx *c, Node *script, const char *src, size_t n, const char *name) {
    run_classic_script(c, script, src, n, name, true, script && jsg_script_src(script));
}
/* A dynamically inserted inline script shares an existing parser insertion
   point, but does not create one or suppress writes merely by being inline. */
void js_run_dynamic_inline_script(JsCtx *c, Node *script, const char *src, size_t n, const char *url) {
    run_classic_script(c, script, src, n, url, c && c->parser_write_point, false, true);
}
/* Without a parser insertion point an external/deferred script cannot implicitly
   reopen the document through document.write(). Explicit document.open still works. */
void js_run_deferred_script(JsCtx *c, Node *script, const char *src, size_t n, const char *url) {
    run_classic_script(c, script, src, n, url, false, true);
}

/* may_defer: an external module whose graph is not loaded yet is fetched in the
   background and run from tick_one, instead of blocking the page on the network. */
static void module_script_run(JsCtx *c, Node *script, const char *src, size_t n, const char *url, bool may_defer,
                              bool fetched = false, const char *ctype = nullptr) {
    if (!c || c->dead) return;
    if (script && !(script->flags & NF_SCRIPT_STARTED) && !js_prepare_script(script)) return;
    if (script) { JS_ENTER(c); jwrap(c, script); }
    bool ext = script && node_attr(script, "src"), load_failure = false;
    std::string tt_buf;
    if (!tt_script_text(c, script, src, n, tt_buf)) return;
    if (may_defer && ext && !src && url && !c->mods.count(url) && !c->mod_errors.count(url)) {
        { JS_ENTER(c); jwrap(c, script); }
        auto j = std::make_unique<ModJob>();
        j->url = url;
        j->script = script; j->generation = c->parser_generation;
        mod_job_start(c, std::move(j));
        return;
    }
    const bool previous_point = c->parser_write_point;
    c->parser_write_point = false;
    c->ignore_destructive_writes++;
    {
        JS_ENTER(c);
        v8::TryCatch tc(iso);
        std::string key = url ? url : "", body, ct;
        if (!ext) key += "#lumen-inline-" + std::to_string(++c->mod_seq);
        v8::Local<v8::Module> m;
        js_enter(c);
        auto it = c->mods.find(key);
        if (mod_rethrow_cached(iso, c->mod_errors, key)) { /* report the retained module-script error below */ }
        else if (it != c->mods.end()) m = it->second.Get(iso);
        else {
            bool ok = src != nullptr;
            if (src) body.assign(src, n);
            if (ext && fetched) ct = ctype ? ctype : "";
            else if (!src) {
                double busy = c->busy_since.exchange(0);
                std::string ref = c->doc && c->doc->url ? c->doc->url : "";
                mod_prefetch({ key }, ref, [c](const std::string &k) { return c->mods.count(k) > 0; });
                ok = mod_take(key, ref, body, &ct);
                if (busy > 0) c->busy_since = now_ms();
            }
            if (!ok) load_failure = true;
            else if (ext) (void)mod_build(c, key, body, ct, &load_failure).ToLocal(&m);
            else (void)mod_compile(c, key, body).ToLocal(&m);
        }
        v8::Local<v8::Promise> p;
        v8::Local<v8::Function> f;
        if (!m.IsEmpty() && mod_eval(c, m, &load_failure).ToLocal(&p) && v8::Function::New(ctx, mod_report).ToLocal(&f)) (void)p->Catch(ctx, f);
        js_leave(c);
        /* A fetch/MIME failure makes the script result null. Parse, linking,
           evaluation and import-attribute errors remain errors to rethrow. */
        if (!load_failure && tc.HasCaught() && tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
        if (load_failure) tc.Reset();
        settle(c);
    }
    c->ignore_destructive_writes--;
    c->parser_write_point = previous_point;
    if (!c->dead && script && (ext || load_failure)) { JS_ENTER(c); jfire(c, script, load_failure ? "error" : "load"); }
}
void js_run_module(JsCtx *c, Node *script, const char *src, size_t n, const char *url) {
    module_script_run(c, script, src, n, url, false);
}
void js_run_module_fetched(JsCtx *c, Node *script, const char *src, size_t n, const char *url, const char *ctype) {
    module_script_run(c, script, src, n, url, false, true, ctype);
}
void js_run_module_inserted(JsCtx *c, Node *script, const char *src, size_t n, const char *url) {
    module_script_run(c, script, src, n, url, true);
}
/* Runs the module loads whose background fetches have completed, in start order. */
static void mod_jobs_finish(JsCtx *c) {
    std::vector<std::unique_ptr<ModJob>> ready;
    for (auto it = c->mod_jobs.begin(); it != c->mod_jobs.end();)
        if (*(*it)->done) { ready.push_back(std::move(*it)); it = c->mod_jobs.erase(it); }
        else ++it;
    for (auto &j : ready) {
        if (c->dead) return;
        if (g_log_level < 3) fprintf(stderr, "lumen: module graph %s fetched after %.0fms\n", mod_cache_name(j->url).c_str(), now_ms() - j->at);
        if (j->script) {
            if (j->generation == c->parser_generation && j->script->doc == c->doc)
                module_script_run(c, j->script, nullptr, 0, j->url.c_str(), false);
            continue;
        }
        JS_ENTER(c);
        js_enter(c);
        import_finish(c, j->R.Get(iso), j->url);
        js_leave(c);
    }
}

void js_eval(JsCtx *c, const char *src, const char *name) {
    if (c && src) run_source(c, src, strlen(src), name, false);
}
void js_fullscreen_exit(void) {
    std::vector<JsCtx *> fs;
    for (JsCtx *c : g_ctxs) if (c->fullscreen && !c->dead) fs.push_back(c);
    for (JsCtx *c : fs) { c->fullscreen = false; js_eval(c, "__lumenFullscreenExit()", "lumen:fullscreen"); }
}
char *js_eval_result(JsCtx *c, const char *src, const char *name, bool *failed) {
    *failed = false;
    if (!c || !src) return NULL;
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::ScriptOrigin origin(jstr(iso, name ? name : ""));
    v8::Local<v8::Script> s; v8::Local<v8::Value> v;
    js_enter(c);
    bool ok = v8::Script::Compile(ctx, jstr(iso, src), &origin).ToLocal(&s) && s->Run(ctx).ToLocal(&v);
    js_leave(c);
    std::string out;
    if (!ok && tc.HasCaught()) {
        *failed = true;
        v8::String::Utf8Value m(iso, tc.Exception());
        out = *m ? *m : "Error";
        if (tc.CanContinue()) jreport(c, tc.Exception(), tc.Message());
    } else if (ok && !v->IsUndefined()) {
        v8::Local<v8::String> t;
        if (v->ToString(ctx).ToLocal(&t)) { v8::String::Utf8Value m(iso, t); if (*m) out = *m; }
    }
    settle(c);
    if (out.empty() && !*failed) return NULL;
    if (out.size() > 200) { out.resize(200); while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back(); if (!out.empty() && (unsigned char)out.back() >= 0xC0) out.pop_back(); out += "\xE2\x80\xA6"; }
    char *r = (char *)malloc(out.size() + 1); memcpy(r, out.c_str(), out.size() + 1);
    return r;
}

/* Native callers keep using a dispatch target after its handlers ran; handlers may detach it and a GC may
   then free it. Targets stay pinned until the host's event batch ends (js_release_pins). */
static std::vector<Node *> g_pins;
void js_release_pins(void) {
    std::vector<Node *> v; v.swap(g_pins);
    for (Node *n : v) node_release(n);
}
int js_key_mods;

/* KeyboardEvent.code for a US layout key value */
static std::string key_code(const char *k) {
    if (!strcmp(k, " ")) return "Space";
    if (!k[0] || k[1]) return k;
    char c = k[0];
    if (c >= 'a' && c <= 'z') return std::string("Key") + (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return std::string("Key") + c;
    if (c >= '0' && c <= '9') return std::string("Digit") + c;
    const char *sd = strchr(")!@#$%^&*(", c);
    if (sd) return std::string("Digit") + (char)('0' + (sd - ")!@#$%^&*("));
    static const char *punct[][2] = { {"-_", "Minus"}, {"=+", "Equal"}, {"[{", "BracketLeft"}, {"]}", "BracketRight"}, {"\\|", "Backslash"},
        {";:", "Semicolon"}, {"'\"", "Quote"}, {",<", "Comma"}, {".>", "Period"}, {"/?", "Slash"}, {"`~", "Backquote"} };
    for (auto &p : punct) if (strchr(p[0], c)) return p[1];
    return k;
}

/* Legacy keyCode describes a key, not its Unicode character. In particular,
 * '.' must not become 46 (Delete) in editors using legacy keyboard handlers. */
static int legacy_key_code(const std::string &code) {
    if (code.size() == 4 && code.compare(0, 3, "Key") == 0) return code[3];
    if (code.size() == 6 && code.compare(0, 5, "Digit") == 0) return code[5];
    static const struct { const char *code; int value; } keys[] = {
        {"Backspace",8}, {"Tab",9}, {"Enter",13}, {"Shift",16}, {"Control",17}, {"Alt",18},
        {"Pause",19}, {"CapsLock",20}, {"Escape",27}, {"Space",32}, {"PageUp",33},
        {"PageDown",34}, {"End",35}, {"Home",36}, {"ArrowLeft",37}, {"ArrowUp",38},
        {"ArrowRight",39}, {"ArrowDown",40}, {"Insert",45}, {"Delete",46}, {"Meta",91},
        {"Semicolon",186}, {"Equal",187}, {"Comma",188}, {"Minus",189}, {"Period",190},
        {"Slash",191}, {"Backquote",192}, {"BracketLeft",219}, {"Backslash",220},
        {"BracketRight",221}, {"Quote",222}
    };
    for (const auto &k : keys) if (code == k.code) return k.value;
    return 0;
}

static bool js_dispatch_impl(JsCtx *c, Node *target, const char *type, const char *kind, bool bubbles, bool cancelable, double x, double y, int button, const char *key, bool touch) {
    if (touch || (kind && (!strcmp(kind, "MouseEvent") || !strcmp(kind, "KeyboardEvent") || !strcmp(kind, "WheelEvent") || !strcmp(kind, "PointerEvent")))) g_input_at = now_ms();
    if (!c || c->fire.IsEmpty()) return true;
    if (target && target->parent && target->parent->type != NODE_DOCUMENT) { node_retain(target); g_pins.push_back(target); }
    JS_ENTER(c);
    v8::Local<v8::Object> init = v8::Object::New(iso);
    auto set = [&](const char *k, v8::Local<v8::Value> v) { (void)init->Set(ctx, jstr(iso, k), v); };
    set("bubbles", v8::Boolean::New(iso, bubbles));
    set("cancelable", v8::Boolean::New(iso, cancelable));
    set("composed", v8::True(iso));
    set("view", ctx->Global());
    if (touch) set("__touch", v8::True(iso));
    set("clientX", v8::Number::New(iso, x));
    set("clientY", v8::Number::New(iso, y));
    set("screenX", v8::Number::New(iso, x));
    set("screenY", v8::Number::New(iso, y));
    set("button", v8::Integer::New(iso, button));
    bool hover = type && (!strcmp(type, "pointermove") || !strcmp(type, "mousemove") || !strcmp(type, "pointerover") || !strcmp(type, "mouseover"));
    bool released = type && (hover || !strcmp(type, "pointerup") || !strcmp(type, "mouseup") || !strcmp(type, "click") || !strcmp(type, "auxclick") || !strcmp(type, "contextmenu"));
    set("buttons", v8::Integer::New(iso, released ? 0 : button == 0 ? 1 : button == 2 ? 2 : 4));
    if (type && !strcmp(type, "pointerdown")) set("pressure", v8::Number::New(iso, 0.5));
    set("detail", v8::Integer::New(iso, 1));
    set("shiftKey", v8::Boolean::New(iso, js_key_mods & 1));
    set("ctrlKey", v8::Boolean::New(iso, js_key_mods & 2));
    set("altKey", v8::Boolean::New(iso, js_key_mods & 4));
    set("metaKey", v8::Boolean::New(iso, js_key_mods & 8));
    if (key) {
        std::string code = key_code(key);
        set("key", jstr(iso, key)); set("code", jstr(iso, code.c_str()));
        set("keyCode", v8::Integer::New(iso, legacy_key_code(code)));
        set("which", v8::Integer::New(iso, legacy_key_code(code)));
    }
    v8::Local<v8::Value> ctor = v8::Undefined(iso);
    if (kind && !c->api.IsEmpty()) (void)c->api.Get(iso)->Get(ctx, jstr(iso, kind)).ToLocal(&ctor);
    v8::Local<v8::Value> argv[4] = { target ? jwrap(c, target) : v8::Local<v8::Value>(ctx->Global()), jstr(iso, type), init, ctor };
    v8::Local<v8::Value> r;
    static bool dbg = getenv("LUMEN_DEBUG_JS"); double t0 = dbg ? now_ms() : 0;
    bool ok = jcall(c, c->fire.Get(iso), v8::Undefined(iso), 4, argv).ToLocal(&r);
    if (dbg) {   /* per event type: dispatches and ms, printed every second */
        static std::map<std::string, std::pair<int, double>> acc; static double last;
        auto &e = acc[type]; e.first++; e.second += now_ms() - t0;
        if (now_ms() - last > 1000) { last = now_ms(); std::string o; char b[96]; for (auto &kv : acc) { snprintf(b, sizeof b, " %s=%d/%.1fms", kv.first.c_str(), kv.second.first, kv.second.second); o += b; } fprintf(stderr, "lumen-js:%s\n", o.c_str()); acc.clear(); }
    }
    if (!ok) return true;
    return r->BooleanValue(iso);
}

bool js_dispatch(JsCtx *c, Node *target, const char *type, const char *kind, bool bubbles, bool cancelable, double x, double y, int button, const char *key) {
    return js_dispatch_impl(c, target, type, kind, bubbles, cancelable, x, y, button, key, false);
}

bool js_dispatch_input(JsCtx *c, Node *target, const char *type, const char *input_type, const char *data, int lo16, int hi16, int n16) {
    if (!c || c->fire.IsEmpty() || !target) return true;
    if (target->parent && target->parent->type != NODE_DOCUMENT) { node_retain(target); g_pins.push_back(target); }
    JS_ENTER(c);
    v8::Local<v8::Object> init = v8::Object::New(iso);
    auto set = [&](const char *k, v8::Local<v8::Value> v) { (void)init->Set(ctx, jstr(iso, k), v); };
    bool before = !strcmp(type, "beforeinput");
    set("bubbles", v8::True(iso)); set("cancelable", v8::Boolean::New(iso, before)); set("composed", v8::True(iso));
    set("view", ctx->Global());
    set("inputType", jstr(iso, input_type ? input_type : ""));
    set("data", data ? v8::Local<v8::Value>(jstr(iso, data)) : v8::Local<v8::Value>(v8::Null(iso)));
    if (lo16 >= 0) {
        v8::Local<v8::Array> e = v8::Array::New(iso, 3);
        (void)e->Set(ctx, 0, v8::Integer::New(iso, lo16)); (void)e->Set(ctx, 1, v8::Integer::New(iso, hi16)); (void)e->Set(ctx, 2, v8::Integer::New(iso, n16));
        set("__edit", e);
    }
    v8::Local<v8::Value> ctor = v8::Undefined(iso);
    if (!c->api.IsEmpty()) (void)c->api.Get(iso)->Get(ctx, jstr(iso, "InputEvent")).ToLocal(&ctor);
    v8::Local<v8::Value> argv[4] = { jwrap(c, target), jstr(iso, type), init, ctor }, r;
    if (!jcall(c, c->fire.Get(iso), v8::Undefined(iso), 4, argv).ToLocal(&r)) return true;
    return r->BooleanValue(iso);
}
bool js_touch_compat(JsCtx *c, Node *target, const char *type, const char *kind, double x, double y) {
    return js_dispatch_impl(c, target, type, kind, true, true, x, y, 0, nullptr, true);
}

unsigned js_touch(JsCtx *c, Node *target, const char *phase, uint64_t device, uint64_t finger, double x, double y, double pressure) {
    if (!c || c->api.IsEmpty()) return 0;
    g_input_at = now_ms();
    JS_ENTER(c);
    v8::Local<v8::Value> fn, result;
    if (!c->api.Get(iso)->Get(ctx, jstr(iso, "nativeTouch")).ToLocal(&fn) || !fn->IsFunction()) return 0;
    char id[64]; snprintf(id, sizeof id, "%llu:%llu", (unsigned long long)device, (unsigned long long)finger);
    v8::Local<v8::Value> args[] = { target ? jwrap(c, target) : v8::Local<v8::Value>(v8::Null(iso)),
        jstr(iso, phase), jstr(iso, id), v8::Number::New(iso, x), v8::Number::New(iso, y), v8::Number::New(iso, pressure) };
    if (!jcall(c, fn.As<v8::Function>(), v8::Undefined(iso), 6, args).ToLocal(&result)) return 0;
    return result->Uint32Value(ctx).FromMaybe(0);
}

bool js_dispatch_window(JsCtx *c, const char *type) {
    return js_dispatch(c, nullptr, type, "Event", false, false, 0, 0, 0, nullptr);
}

void js_dispatch_document_event(JsCtx *c, JsDocumentEvent event) {
    if (!c || c->dead || c->fire.IsEmpty()) return;
    const bool dcl = event == JS_DOCUMENT_DOM_CONTENT_LOADED;
    const bool pageshow = event == JS_DOCUMENT_PAGESHOW;
    const char *type = dcl ? "DOMContentLoaded" : pageshow ? "pageshow" : "load";
    JS_ENTER(c);
    auto init = v8::Object::New(iso);
    (void)init->Set(ctx, jstr(iso, "bubbles"), v8::Boolean::New(iso, dcl || pageshow));
    (void)init->Set(ctx, jstr(iso, "cancelable"), v8::Boolean::New(iso, pageshow));
    (void)init->Set(ctx, jstr(iso, "composed"), v8::False(iso));
    if (pageshow) (void)init->Set(ctx, jstr(iso, "persisted"), v8::False(iso));
    v8::Local<v8::Value> ctor = v8::Undefined(iso), result;
    if (!c->api.IsEmpty())
        (void)c->api.Get(iso)->Get(ctx, jstr(iso, pageshow ? "PageTransitionEvent" : "Event")).ToLocal(&ctor);
    v8::Local<v8::Value> args[] = {
        dcl ? v8::Local<v8::Value>(jwrap(c, &c->doc->node)) : v8::Local<v8::Value>(ctx->Global()),
        jstr(iso, type), init, ctor, v8::Boolean::New(iso, !dcl)
    };
    /* The page-transition event is cancelable for compatibility, but canceling
       it does not cancel document loading or the parent's completion. */
    (void)jcall(c, c->fire.Get(iso), v8::Undefined(iso), 5, args).ToLocal(&result);
}

void js_set_ready_state(JsCtx *c, int state) {
    if (!c) return;
    c->doc->ready_state = state;
    JS_ENTER(c);
    jfire(c, &c->doc->node, "readystatechange");
}
/* Chrome's background throttling: a hidden tab's timers wake it at most once a second, unless it
   plays media or holds a WebRTC call */
static bool timers_throttled(JsCtx *c) {
    if (!c->bg || rtc_active(c)) return false;
    for (auto &kv : c->players) { MpState s; mp_state(kv.second, &s); if (!s.paused && !s.ended) return false; }
    return true;
}

/* Milliseconds between animation frames. A rAF loop that has run back to back for over a second while
   nothing was repainted and no input arrived for a second (a carousel or scroll poller re-reading the same
   geometry, as on Google results) gets 15 frames a second instead of 60; the next paint or input restores
   the full rate. A one-off frame request after a quiet spell is not a run and is not delayed. */
static double raf_gap(JsCtx *c, double now) {
    bool run = c->last_raf - c->raf_run_t > 1000;   /* the last frame ended a run of over a second */
    bool quiet = jsg_activity_generation() == c->raf_act_seen && now - c->raf_act_t > 1000;
    return run && quiet ? 66 : 15.5;
}

double js_next_deadline(JsCtx *c) {
    if (!c) return INFINITY;
    double d = INFINITY;
    // A scroll/resize callback or animation frame can move the viewport after
    // its notification snapshot. Schedule the next notification without an
    // idle timer or waiting for the shell's one-second fallback.
    if (viewport_changes(c, false)) d = std::min(d, c->viewport_due);
    if (c->foreground_tasks) d = std::min(d, now_ms() + c->foreground_tasks->NextDelayMs());
    if (!c->timer_due.empty()) d = std::min(d, timers_throttled(c) ? std::max(c->timer_due.begin()->first, c->bg_timers + 1000) : c->timer_due.begin()->first);
    for (auto &e : c->anims) d = std::min(d, e.due);
    if (!c->rafs.empty() && !c->bg) d = std::min(d, c->last_raf + std::ceil(raf_gap(c, now_ms())));
    if (c->bg_seen != c->bg) d = std::min(d, now_ms());
    if (!c->media_pump.IsEmpty()) d = std::min(d, c->media_version != media_state_version() ? now_ms() : c->media_due);
    if (!c->video_frame_pump.IsEmpty() && c->video_frame_version != media_frame_version()) d = std::min(d, now_ms());
    if (rtc_pending(c) || xmsg_waiting(c) || !c->rejs.empty()) d = std::min(d, now_ms());
    d = std::min(d, webgpu_deadline(c));
    if (!c->resize_pump.IsEmpty() && (c->resize_pending || c->resize_version != jsg_layout_generation())) d = std::min(d, c->resize_due);
    if (!c->intersection_pump.IsEmpty() && (c->intersection_pending || c->intersection_version != jsg_visual_generation())) d = std::min(d, c->intersection_due);
    for (JsCtx *k : c->kids) if (!k->dead) d = std::min(d, js_next_deadline(k));
    d = std::min(d, workers_deadline(c));
    return d;
}

static void viewport_update(JsCtx *context) {
    unsigned changes = viewport_changes(context);
    if (changes) jsg_visual_changed();
    if (!changes || context->viewportChanged.IsEmpty()) return;
    context->viewport_due = now_ms() + 16;
    bool media = (changes & (JsViewportState::RESIZE | JsViewportState::MEDIA)) != 0;
    if (media && context->host.sync) context->host.sync(context->host.ud, context->doc, true);
    JS_ENTER(context);
    v8::Local<v8::Value> args[] = {
        v8::Boolean::New(iso, (changes & JsViewportState::RESIZE) != 0),
        v8::Boolean::New(iso, (changes & JsViewportState::SCROLL) != 0),
        v8::Boolean::New(iso, media)
    };
    (void)jcall(context, context->viewportChanged.Get(iso), v8::Undefined(iso), 3, args);
}

static void tick_one(JsCtx *c);
void js_tick(JsCtx *c) {
    if (!c || c->dead) return;
    jsprof_tick(c);
    tick_one(c);
    frames_scan(c);
    for (JsCtx *k : std::vector<JsCtx *>(c->kids)) if (!k->dead) js_tick(k);
    frames_load_check(c);
    /* removed frames that never shared objects with another context are freed once their loads settled, not with their tab */
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (JsCtx *k : std::vector<JsCtx *>(c->kids)) if (k->dead && k->dead_at && now - k->dead_at > 10) {
        if (getenv("LUMEN_DEBUG_FRAMES")) fprintf(stderr, "lumen: frame %u removed %.0f s ago, %s\n", k->id, now - k->dead_at, k->xref ? "kept (shared objects)" : "freed");
        if (k->xref) k->dead_at = 0; else js_free(k);
    }
}
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
/* Debug Android: phases of a JS tick that took >=100 ms (tasks, module jobs, pick+timers,
   viewport, media, raf, anims, resize, intersection, then settle). Numbers only. */
#define TICK_BEGIN() double tk_t[10]; int tk_i = 0; tk_t[0] = now_ms(); size_t tk_mods = c->mod_jobs.size()
#define TICK_MARK(i) (tk_t[++tk_i] = now_ms())
#define TICK_END(ntimers) do { double tk_e = now_ms(); jst_phases(tk_t, tk_i, tk_e); if (tk_e - tk_t[0] >= 100) { char tk_b[256]; int tk_o = 0; \
        for (int k = 1; k <= tk_i; k++) tk_o += snprintf(tk_b + tk_o, sizeof tk_b - (size_t)tk_o, " %.0f", tk_t[k] - tk_t[k - 1]); \
        __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "tick_ms=%.0f mods=%zu timers=%zu phases=%s settle=%.0f", tk_e - tk_t[0], tk_mods, (size_t)(ntimers), tk_b, tk_e - tk_t[tk_i]); } } while (0)
/* What woke the page's JS per 2 s, all frames together: timers by delay (0, <=16, <=50, <=250 ms, longer) and
   their ms, animation frames, media pumps (and how many found media playing), intersection and resize pumps */
static struct { double t0, timer_ms, ph[10]; unsigned ticks, timers, tb[5], rafs, media, playing, io, ro; } g_jst;
/* ms per tick phase (as TICK_END's), the last one being code refresh, workers and settle */
static void jst_phases(const double *t, int n, double end) {
    for (int k = 1; k <= n && k < 10; k++) g_jst.ph[k - 1] += t[k] - t[k - 1];
    g_jst.ph[9] += end - t[n];
}
#define JST(stmt) do { stmt; } while (0)
static double jst_timer_begin(double interval) { g_jst.tb[interval <= 0 ? 0 : interval <= 16 ? 1 : interval <= 50 ? 2 : interval <= 250 ? 3 : 4]++; return now_ms(); }
static void jst_timer_end(double t) { g_jst.timer_ms += now_ms() - t; g_jst.timers++; }
static void jst_report() {
    double now = now_ms();
    if (!g_jst.t0) g_jst.t0 = now;
    if (now - g_jst.t0 < 2000) return;
    __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "jsstat window_ms=%.0f ticks=%u timers=%u (0:%u 16:%u 50:%u 250:%u long:%u) timer_ms=%.0f rafs=%u media=%u playing=%u io=%u ro=%u"
        " phase_ms=%.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f settle=%.0f",
        now - g_jst.t0, g_jst.ticks, g_jst.timers, g_jst.tb[0], g_jst.tb[1], g_jst.tb[2], g_jst.tb[3], g_jst.tb[4], g_jst.timer_ms, g_jst.rafs, g_jst.media, g_jst.playing, g_jst.io, g_jst.ro,
        g_jst.ph[0], g_jst.ph[1], g_jst.ph[2], g_jst.ph[3], g_jst.ph[4], g_jst.ph[5], g_jst.ph[6], g_jst.ph[7], g_jst.ph[8], g_jst.ph[9]);
    memset(&g_jst, 0, sizeof g_jst); g_jst.t0 = now;
}
#else
#define TICK_BEGIN() do {} while (0)
#define TICK_MARK(i) do {} while (0)
#define TICK_END(ntimers) do { (void)(ntimers); } while (0)
#define JST(stmt) do {} while (0)
static inline double jst_timer_begin(double) { return 0; }
static inline void jst_timer_end(double) {}
static inline void jst_report() {}
#endif
static void tick_one(JsCtx *c) {
    JS_ENTER(c);
    TICK_BEGIN();
    JST(g_jst.ticks++); if (!c->parent) jst_report();
    // Frames share the root isolate. Drain once at the root event-loop level,
    // including a checkpoint after each task so promise completions run now.
    if (c->foreground_tasks) while (c->foreground_tasks->RunOne()) iso->PerformMicrotaskCheckpoint();
    TICK_MARK(0);
    if (!c->mod_jobs.empty()) { mod_jobs_finish(c); settle(c); }
    frames_parser_tick(c);
    document_stream_tick(c);
    if (c->dead) return;
    TICK_MARK(1);
    pick_deliver(c);
    xmsg_deliver(c);
    double now = now_ms(), timers_t0 = now;
    std::vector<std::pair<double, uint32_t>> due;
    size_t ntimers = 0;
    bool throttled = timers_throttled(c);
    if (throttled && now - c->bg_timers < 1000) timers_t0 = -1;   /* not this turn */
    else if (throttled && !c->timer_due.empty() && c->timer_due.begin()->first <= now) c->bg_timers = now;
    // Like Chrome, run tasks that become due meanwhile (MessageChannel hops, zero
    // timeouts) back to back for up to 8 ms rather than one batch per frame.
    for (; timers_t0 >= 0;) {
        due.clear();
        for (auto &d : c->timer_due) { if (d.first > now) break; due.push_back(d); }
        if (due.empty()) break;
        for (auto &d : due) {
            auto it = c->timers.find(d.second);
            if (it == c->timers.end() || it->second.due != d.first) continue;
            v8::Local<v8::Function> fn = it->second.fn.Get(iso);
            int nest = it->second.nest; double interval = it->second.interval;
            if (it->second.repeat) {
                c->timer_due.erase(d);
                it->second.due = now_ms() + it->second.interval;
                c->timer_due.insert({ it->second.due, it->first });
            } else c->timer_erase(it);
            c->timer_nest = nest;
            double stat_t = jst_timer_begin(interval);
            (void)jcall(c, fn, ctx->Global(), 0, nullptr);
            jst_timer_end(stat_t);
            c->timer_nest = 0;
            ntimers++;
        }
        now = now_ms();
        if (now - timers_t0 >= 8 || c->dead) break;
    }
    now = now_ms();
    TICK_MARK(2);
    viewport_update(c);
    TICK_MARK(3);
    if (!c->media_pump.IsEmpty() && (c->media_version != media_state_version() || now >= c->media_due)) {
        c->media_version = media_state_version();
        c->media_due = INFINITY;
        v8::Local<v8::Value> playing;
        JST(g_jst.media++);
        if (jcall(c, c->media_pump.Get(iso), v8::Undefined(iso), 0, nullptr).ToLocal(&playing) && playing->BooleanValue(iso)) {
            c->media_due = now_ms() + 50; JST(g_jst.playing++);
        }
    }
    rtc_pump(c);
    webgpu_pump(c);
    if (!c->video_frame_pump.IsEmpty() && c->video_frame_version != media_frame_version()) {
        c->video_frame_version = media_frame_version();
        v8::Local<v8::Value> ts = v8::Number::New(iso, now - c->t0);
        (void)jcall(c, c->video_frame_pump.Get(iso), v8::Undefined(iso), 1, &ts);
    }
    TICK_MARK(4);
    if (c->bg_seen != c->bg) { c->bg_seen = c->bg; jfire(c, &c->doc->node, "visibilitychange"); }
    /* a background tab gets no animation frames, as in Chrome: a hidden WebGL demo or animated page
       would otherwise keep drawing at 60 Hz */
    bool ran_raf = false;
    if (!c->rafs.empty() && !c->bg && now - c->last_raf >= raf_gap(c, now)) {
        ran_raf = true;
        if (now - c->last_raf > 100) c->raf_run_t = now;   /* a new run of frames */
        uint64_t act = jsg_activity_generation();
        if (act != c->raf_act_seen) { c->raf_act_seen = act; c->raf_act_t = now; }
        c->last_raf = now;
        std::map<uint32_t, v8::Global<v8::Function>> list;
        list.swap(c->rafs);
        v8::Local<v8::Value> ts = v8::Number::New(iso, now - c->t0);
        for (auto &kv : list) (void)jcall(c, kv.second.Get(iso), ctx->Global(), 1, &ts);
        JST(g_jst.rafs += (unsigned)list.size());
    }
    TICK_MARK(5);
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
    TICK_MARK(6);
    // Layout completion, not a repeating timer, drives size observation. Keep
    // callback-induced layout changes pending for another frame to bound loops.
    if (!c->resize_pump.IsEmpty() && now_ms() >= c->resize_due &&
        (c->resize_pending || c->resize_version != jsg_layout_generation())) {
        if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
        c->resize_version = jsg_layout_generation();
        c->resize_pending = false;
        c->resize_due = now_ms() + 16; JST(g_jst.ro++);
        (void)jcall(c, c->resize_pump.Get(iso), v8::Undefined(iso), 0, nullptr);
    }
    TICK_MARK(7);
    // Dirty observations update after a rendering opportunity; pace other
    // changes like size observation. Unchanged pages have no deadline or scan.
    // Snapshot before callbacks so their mutations/new targets stay pending.
    if (!c->intersection_pump.IsEmpty() &&
        (c->intersection_pending || c->intersection_version != jsg_visual_generation()) &&
        (ran_raf || now_ms() >= c->intersection_due)) {
        if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
        c->intersection_version = jsg_visual_generation();
        c->intersection_pending = false;
        c->intersection_due = now_ms() + 16; JST(g_jst.io++);
        (void)jcall(c, c->intersection_pump.Get(iso), v8::Undefined(iso), 0, nullptr);
    }
    TICK_MARK(8);
    code_refresh_tick(c);
    workers_pump(c);
    settle(c);
    if (!c->parent) rej_flush(c);
    TICK_END(ntimers);
}

bool js_wants_frame(JsCtx *c) {
    if (!c || c->dead) return false;
    if (!c->rafs.empty() && !c->bg) return true;
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

/* a CSS transition for the JS side to start (taken by fireAnim or synchronously by N.trTake) */
static void js_style_job(Node *n, const char *type, std::string name) {
    JsCtx *c = ctx_for(n);
    if (!c || c->ctx.IsEmpty()) return;
    JS_ENTER(c);
    AnimEv e;
    e.due = now_ms(); e.n = n; e.target.Reset(iso, jwrap(c, n));
    e.type = type; e.name = std::move(name); e.elapsed = 0; e.anim = false;
    c->anims.push_back(std::move(e));
}
void js_transition_start(Node *n, const char *prop, const char *from, const char *to) { js_style_job(n, "lumen-transition", std::string(prop) + '\x01' + from + '\x01' + to); }
/* the element's animation-name list changed: the JS side (re)builds its CSSAnimations */
void js_animation_names(Node *n, const char *names) { js_style_job(n, "lumen-animation", names); }

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

bool js_busy(JsCtx *c) { return c && (!c->fetches.empty() || !c->mod_jobs.empty()); }
