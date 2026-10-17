#include <map>
#include <string>
/* V8 embedding: platform, per-page isolate/context, node wrappers, script execution, timers, events */
#include "js_int.h"
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

static std::unique_ptr<v8::Platform> g_platform;
static std::unique_ptr<v8::Platform> g_task_platform;
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
        if (cache && (!cd || source.GetCachedData()->rejected)) code_store(path, v8::ScriptCompiler::CreateCodeCache(s->GetUnboundScript()));
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
static bool mod_fetch(const std::string &u, const std::string &ref, std::string &out) {
    NetRequest *rq = net_request_new("GET", u.c_str());
    if (!ref.compare(0, 4, "http")) headers_add(&rq->headers, "Referer", ref.c_str());
    NetResponse *r = net_fetch_sync(rq);
    bool ok = r && r->status >= 200 && r->status < 300;
    if (ok) out.assign(r->body ? r->body : "", r->body_len);
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
struct ModSrc { std::string body; std::vector<std::string> kids; bool ok = false; double at = 0; };
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
    m->ok = mod_fetch(u, ref, m->body);
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
static bool mod_take(const std::string &u, const std::string &ref, std::string &out) {
    std::shared_ptr<ModSrc> m = msrc_get(u, ref, true);
    { std::lock_guard<std::mutex> lk(g_msrc_mu); auto it = g_msrc.find(u); if (it != g_msrc.end() && it->second == m) g_msrc.erase(it); }
    if (m->ok) out = m.use_count() == 1 ? std::move(m->body) : m->body;
    return m->ok;
}
void js_module_prefetch(const char *const *urls, int n, const char *ref) {
    std::vector<std::string> roots;
    for (int i = 0; i < n; i++) if (urls[i]) roots.emplace_back(urls[i]);
    mod_prefetch(roots, ref ? ref : "", [](const std::string &) { return false; });
}
char *js_module_take(const char *url, const char *ref, size_t *len) {
    std::string body;
    if (!mod_take(url, ref ? ref : "", body)) return nullptr;
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
static v8::MaybeLocal<v8::Module> mod_compile(JsCtx *c, const std::string &key, const std::string &src) {
    v8::Isolate *iso = c->iso;
    v8::ScriptOrigin origin(jstr(iso, key.c_str()), 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
    bool cache = src.size() >= kCodeCacheMin && !g_code_dir.empty() && key.find("#lumen-inline-") == std::string::npos;
    std::string path = cache ? code_path(src.data(), src.size()) : std::string();
    v8::ScriptCompiler::CachedData *cd = cache ? code_load(path) : nullptr;
    v8::ScriptCompiler::Source s(jsrc(iso, src.data(), src.size()), origin, cd);
    v8::Local<v8::Module> m;
    if (!v8::ScriptCompiler::CompileModule(iso, &s, cd ? v8::ScriptCompiler::kConsumeCodeCache : v8::ScriptCompiler::kNoCompileOptions).ToLocal(&m)) return {};
    if (cache && (!cd || s.GetCachedData()->rejected)) t_code_pending.emplace_back(v8::Global<v8::Module>(iso, m), path);
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
        double busy = c->busy_since.exchange(0);   /* network time is not script time */
        mod_prefetch(need, ref, [c](const std::string &u) { return c->mods.count(u) > 0; });
        if (busy > 0) c->busy_since = now_ms();
        for (size_t i = 0; i < need.size(); i++) {
            std::string body;
            if (!mod_take(need[i], ref, body)) return mod_throw(iso, "Failed to fetch module: " + need[i]), false;
            v8::Local<v8::Module> m;
            if (!mod_compile(c, need[i], body).ToLocal(&m)) return false;
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
    v8::Local<v8::Value> result;
    bool evaluated = m->Evaluate(ctx).ToLocal(&result);
    for (auto &p : t_code_pending) {
        v8::Local<v8::Module> pm = p.first.Get(c->iso);
        if (pm->GetStatus() != v8::Module::kErrored) code_store(p.second, v8::ScriptCompiler::CreateCodeCache(pm->GetUnboundModuleScript()));
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
    std::thread([u = j->url, ref, known, done = j->done] {
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
    v8::TryCatch tc(iso);
    v8::Local<v8::Module> m;
    std::string body;
    auto it = c->mods.find(u);
    if (it != c->mods.end()) m = it->second.Get(iso);
    else {
        double busy = c->busy_since.exchange(0);
        std::string ref = c->doc && c->doc->url ? c->doc->url : "";
        mod_prefetch({ u }, ref, [c](const std::string &k) { return c->mods.count(k) > 0; });
        bool ok = mod_take(u, ref, body);
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
}
static v8::MaybeLocal<v8::Promise> mod_dynamic(v8::Local<v8::Context> ctx, v8::Local<v8::Data>, v8::Local<v8::Value> res, v8::Local<v8::String> spec, v8::Local<v8::FixedArray>) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    JsCtx *c = ctx_of(ctx);
    v8::Local<v8::Promise::Resolver> R;
    if (!v8::Promise::Resolver::New(ctx).ToLocal(&R)) return {};
    std::string base = res->IsString() ? jcstr(iso, res) : "";
    if (base.find(':') == std::string::npos || !base.compare(0, 6, "lumen:")) base = c->doc && c->doc->url ? c->doc->url : "";
    std::string u = mod_resolve(jcstr(iso, spec), base);
    if (u.empty()) {
        v8::TryCatch tc(iso);
        mod_throw(iso, "Failed to resolve module specifier \"" + jcstr(iso, spec) + "\"");
        (void)R->Reject(ctx, tc.Exception());
    } else if (c->mods.count(u)) import_finish(c, R, u);
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
    g_task_platform = js_task_platform(g_platform.get());
    v8::V8::InitializePlatform(g_task_platform.get());
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
    js_wasm_install(ctx, N);
    js_clone_install(ctx, N);
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
void js_free(JsCtx *c) {
    if (!c) return;
    { std::lock_guard<std::mutex> l(g_pick_mu); if (g_pick_ctx == c) g_pick_ctx = nullptr; }
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
    wa_close_owner(c);
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
        c->video_frame_pump.Reset(); c->resize_pump.Reset(); c->intersection_pump.Reset(); c->media_pump.Reset();
        c->anims.clear();
        c->protos.clear();
        c->protoFor.Reset(); c->fire.Reset(); c->report.Reset(); c->viewportChanged.Reset();
        c->api.Reset(); c->node_tmpl.Reset();
        c->mods.clear(); c->mod_url.clear(); c->mod_jobs.clear();
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
    for (Node *n : roots) node_free_tree(n);
    for (Document *d : docs) doc_free(d);
    delete c;
}

void js_run_script(JsCtx *c, Node *script, const char *src, size_t n, const char *name) {
    if (!c) return;
    if (script) script->flags |= NF_SCRIPT_STARTED;
    Node *prev = c->current_script;
    c->current_script = script;
    run_source(c, src, n, name, script && node_attr(script, "src"));
    c->current_script = prev;
    if (script && node_attr(script, "src")) {
        JS_ENTER(c);
        jfire(c, script, "load");
    }
}

/* may_defer: an external module whose graph is not loaded yet is fetched in the
   background and run from tick_one, instead of blocking the page on the network. */
static void module_script_run(JsCtx *c, Node *script, const char *src, size_t n, const char *url, bool may_defer) {
    if (!c) return;
    if (script) script->flags |= NF_SCRIPT_STARTED;
    bool ext = script && node_attr(script, "src"), ok = false;
    if (may_defer && ext && !src && url && !c->mods.count(url)) {
        { JS_ENTER(c); jwrap(c, script); }   /* keeps the element alive until it runs */
        auto j = std::make_unique<ModJob>();
        j->url = url;
        j->script = script;
        mod_job_start(c, std::move(j));
        return;
    }
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
            if (!src) {
                double busy = c->busy_since.exchange(0);
                std::string ref = c->doc && c->doc->url ? c->doc->url : "";
                mod_prefetch({ key }, ref, [c](const std::string &k) { return c->mods.count(k) > 0; });
                ok = mod_take(key, ref, body);
                if (busy > 0) c->busy_since = now_ms();
            } else ok = true;
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
void js_run_module(JsCtx *c, Node *script, const char *src, size_t n, const char *url) {
    module_script_run(c, script, src, n, url, false);
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
        if (j->script) { module_script_run(c, j->script, nullptr, 0, j->url.c_str(), false); continue; }
        JS_ENTER(c);
        js_enter(c);
        import_finish(c, j->R.Get(iso), j->url);
        js_leave(c);
    }
}

void js_eval(JsCtx *c, const char *src, const char *name) {
    if (c && src) run_source(c, src, strlen(src), name, false);
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

bool js_touch_compat(JsCtx *c, Node *target, const char *type, const char *kind, double x, double y) {
    return js_dispatch_impl(c, target, type, kind, true, true, x, y, 0, nullptr, true);
}

unsigned js_touch(JsCtx *c, Node *target, const char *phase, uint64_t device, uint64_t finger, double x, double y, double pressure) {
    if (!c || c->api.IsEmpty()) return 0;
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

void js_set_ready_state(JsCtx *c, int state) {
    if (!c) return;
    c->doc->ready_state = state;
    JS_ENTER(c);
    jfire(c, &c->doc->node, "readystatechange");
}

double js_next_deadline(JsCtx *c) {
    if (!c) return INFINITY;
    double d = INFINITY;
    // A scroll/resize callback or animation frame can move the viewport after
    // its notification snapshot. Schedule the next notification without an
    // idle timer or waiting for the shell's one-second fallback.
    if (viewport_changes(c, false)) d = std::min(d, c->viewport_due);
    if (c->foreground_tasks) d = std::min(d, now_ms() + c->foreground_tasks->NextDelayMs());
    for (auto &kv : c->timers) d = std::min(d, kv.second.due);
    for (auto &e : c->anims) d = std::min(d, e.due);
    if (!c->rafs.empty()) d = std::min(d, c->last_raf + 16);
    if (!c->media_pump.IsEmpty()) d = std::min(d, c->media_version != media_state_version() ? now_ms() : c->media_due);
    if (!c->video_frame_pump.IsEmpty() && c->video_frame_version != media_frame_version()) d = std::min(d, now_ms());
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
    tick_one(c);
    frames_scan(c);
    for (JsCtx *k : std::vector<JsCtx *>(c->kids)) if (!k->dead) js_tick(k);
}
static void tick_one(JsCtx *c) {
    JS_ENTER(c);
    // Frames share the root isolate. Drain once at the root event-loop level,
    // including a checkpoint after each task so promise completions run now.
    if (c->foreground_tasks) while (c->foreground_tasks->RunOne()) iso->PerformMicrotaskCheckpoint();
    if (!c->mod_jobs.empty()) { mod_jobs_finish(c); settle(c); }
    pick_deliver(c);
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
    viewport_update(c);
    if (!c->media_pump.IsEmpty() && (c->media_version != media_state_version() || now >= c->media_due)) {
        c->media_version = media_state_version();
        c->media_due = INFINITY;
        v8::Local<v8::Value> playing;
        if (jcall(c, c->media_pump.Get(iso), v8::Undefined(iso), 0, nullptr).ToLocal(&playing) && playing->BooleanValue(iso))
            c->media_due = now_ms() + 50;
    }
    if (!c->video_frame_pump.IsEmpty() && c->video_frame_version != media_frame_version()) {
        c->video_frame_version = media_frame_version();
        v8::Local<v8::Value> ts = v8::Number::New(iso, now - c->t0);
        (void)jcall(c, c->video_frame_pump.Get(iso), v8::Undefined(iso), 1, &ts);
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
    // Layout completion, not a repeating timer, drives size observation. Keep
    // callback-induced layout changes pending for another frame to bound loops.
    if (!c->resize_pump.IsEmpty() && now_ms() >= c->resize_due &&
        (c->resize_pending || c->resize_version != jsg_layout_generation())) {
        if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
        c->resize_version = jsg_layout_generation();
        c->resize_pending = false;
        c->resize_due = now_ms() + 16;
        (void)jcall(c, c->resize_pump.Get(iso), v8::Undefined(iso), 0, nullptr);
    }
    // Snapshot before callbacks: mutations/new observations in a callback must
    // survive for the next delivery. Bound scanning during continuous scrolling
    // to 20 Hz; unchanged pages have no timer or geometry reads.
    if (!c->intersection_pump.IsEmpty() && now_ms() >= c->intersection_due &&
        (c->intersection_pending || c->intersection_version != jsg_visual_generation())) {
        if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
        c->intersection_version = jsg_visual_generation();
        c->intersection_pending = false;
        c->intersection_due = now_ms() + 50;
        (void)jcall(c, c->intersection_pump.Get(iso), v8::Undefined(iso), 0, nullptr);
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

bool js_busy(JsCtx *c) { return c && (!c->fetches.empty() || !c->mod_jobs.empty()); }
