/* Internal shared state for the V8 binding translation units */
#pragma once
#include <v8.h>
#include <libplatform/libplatform.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
extern "C" {
#include "js.h"
#include "jsglue.h"
#include "../net/net.h"
#include "../base/url.h"
#include "../media/media.h"
extern const char *g_user_agent;
}

constexpr v8::EmbedderDataTypeTag kTag = v8::kEmbedderDataTypeTagDefault;
extern int g_log_level;

struct JsCtx;
struct Fetch {
    JsCtx *c;
    uint64_t id;
    v8::Global<v8::Function> cb;
    Node *script;
    v8::Global<v8::Function> head, chunk;
};
struct WsSock {
    JsCtx *c;
    uint32_t id;
    NetWs *ws;
    v8::Global<v8::Function> cb;
};
struct Timer {
    double due, interval;
    bool repeat;
    v8::Global<v8::Function> fn;
};
struct PromiseRejection {
    v8::Global<v8::Promise> promise;
    uint64_t order;
};
struct AnimEv {
    double due;
    Node *n;
    v8::Global<v8::Value> target;
    std::string type, name;
    double elapsed;
    bool anim;
};
struct JsCtx {
    v8::Isolate *iso = nullptr;
    uint64_t bus_seen = 0; std::string bus_origin;   /* BroadcastChannel / storage-event bus */
    bool bg = false, bg_gc = false; double bg_since = 0;   /* tab in the background: V8 favours memory, one idle GC */
    std::shared_ptr<v8::ArrayBuffer::Allocator> alloc; /* transferred backing stores may outlive this isolate */
    v8::Global<v8::Context> ctx;
    v8::Global<v8::ObjectTemplate> node_tmpl;
    v8::Global<v8::Object> api;
    v8::Global<v8::Function> protoFor, fire, report, mediaChanged, pick_cb;
    std::unordered_map<std::string, v8::Global<v8::Object>> protos;
    Document *doc = nullptr;
    JsHost host{};
    std::vector<Node *> wrapped;
    std::map<uint32_t, Timer> timers;
    uint32_t next_timer = 1;
    /* Retain only until a handler is added or the next completed checkpoint. */
    std::unordered_multimap<int, PromiseRejection> rejections;
    uint64_t next_rejection = 0;
    bool rejections_pending = false; /* fast path on the isolate's root context */
    std::map<uint32_t, v8::Global<v8::Function>> rafs;
    uint32_t next_raf = 1;
    double last_raf = 0, t0 = 0;
    std::map<uint64_t, Fetch *> fetches;
    std::map<uint32_t, struct WsSock *> sockets;
    uint32_t next_ws = 1;
    std::vector<Document *> docs;
    Node *current_script = nullptr;
    int depth = 0;
    /* Native capture ownership follows the document, including cloned tracks. */
    std::map<int, unsigned> capture_cameras, capture_mics;
    std::unordered_set<int> capture_recorders;
    bool capture_screen_authorized = false;
    std::map<uint32_t, MediaPlayer *> players;
    uint32_t next_player = 1;
    std::vector<AnimEv> anims;
    std::thread::id thread;
    std::atomic<double> busy_since{0};
    std::atomic<bool> wd_fired{false};
    bool wd_stop = false;
    std::mutex wd_mu;
    std::condition_variable wd_cv;
    std::thread watchdog;
    JsCtx *parent = nullptr;          /* nested browsing context: owning frame's context */
    Node *frame_el = nullptr;         /* the <iframe> in the parent document */
    std::vector<JsCtx *> kids;
    uint32_t id = 0;
    JsWindowId window_id = 0;
    uint64_t window_epoch = 0, opaque_origin = 0;
    std::string origin, inherited_origin_url; /* immutable document principal; never derived from pushState */
    std::string inherited_base_url; /* initial about:blank/srcdoc fallback before dynamic <base> resolution */
    uint32_t sandbox_flags = 0;
    std::unordered_set<JsWindowId> window_refs;
    bool dead = false;
    uint64_t frame_scan_ver = ~0ull;
    void *host_ud = nullptr;          /* child host data returned by frame_open */
    std::string frame_src;
    bool sb_opaque = false, sb_noscript = false;   /* <iframe sandbox> without allow-same-origin / allow-scripts */
    std::unordered_map<std::string, v8::Global<v8::Module>> mods;   /* ES module map, keyed by URL */
    std::vector<std::pair<std::string, std::string>> imap;          /* import map "imports" (scopes merged) */
    size_t imap_seen = 0;
    std::unordered_multimap<int, std::string> mod_url;              /* module identity hash -> key */
    int mod_seq = 0;
};

#define JS_ENTER(c)                                         \
    v8::Isolate *iso = (c)->iso;                            \
    v8::Isolate::Scope js_is_(iso);                         \
    v8::HandleScope js_hs_(iso);                            \
    v8::Local<v8::Context> ctx = (c)->ctx.Get(iso);         \
    v8::Context::Scope js_cs_(ctx)

JsCtx *jctx(v8::Isolate *iso);
v8::Local<v8::String> jstr(v8::Isolate *iso, const char *s, int n = -1);
/* script source; big ones become external strings shared by every isolate (tabs, frames, workers) */
v8::Local<v8::String> jsrc(v8::Isolate *iso, const char *s, size_t n);
std::string jcstr(v8::Isolate *iso, v8::Local<v8::Value> v);
v8::Local<v8::Value> jwrap(JsCtx *c, Node *n);
Node *junwrap(v8::Local<v8::Value> v);
void js_bus_deliver(JsCtx *c);
uint64_t js_bus_seq();
struct BusMsg { uint64_t seq; double t; std::string origin, kind, name; std::vector<uint8_t> data; const void *sender; double id; };
bool js_bus_post(v8::Isolate *iso, v8::Local<v8::Context> cx, v8::Local<v8::Value> v, const std::string &origin,
                 const std::string &kind, const std::string &name, const void *sender, double id);
bool js_bus_take(const std::string &origin, const void *self, uint64_t &seen, std::vector<BusMsg> &out);
bool js_bus_args(v8::Isolate *iso, v8::Local<v8::Context> ctx, const BusMsg &m, const void *self, v8::Local<v8::Value> argv[5]);
void js_bus_listen(const void *key, std::function<void()> wake);   /* worker threads; null wake unregisters */
v8::MaybeLocal<v8::Value> jcall(JsCtx *c, v8::Local<v8::Function> f, v8::Local<v8::Value> recv, int argc, v8::Local<v8::Value> *argv);
void jfire(JsCtx *c, Node *n, const char *type);
void jreport(JsCtx *c, v8::Local<v8::Value> exc, v8::Local<v8::Message> msg);
void js_install_native(JsCtx *c, v8::Local<v8::Object> N);
void js_install_rtc(JsCtx *c, v8::Local<v8::Object> N);
void js_rtc_close(JsCtx *c);
double js_rtc_deadline(JsCtx *c);
void js_rtc_deliver(JsCtx *c);
size_t js_rtc_live_peers();
void js_run_inserted(JsCtx *c, Node *root);
extern "C" JsCtx *js_new_ex(Document *d, const JsHost *host, JsCtx *parent, Node *frame);
extern "C" JsCtx *owner_ctx(Document *d);
/* URL of the top-level document this context belongs to (site for cookies) */
static inline const char *js_top_url(const JsCtx *c) { while (c && c->parent) c = c->parent; return c && c->doc ? c->doc->url : nullptr; }
std::string ctx_origin(JsCtx *c);
void frames_scan(JsCtx *c);
void frames_inserted(JsCtx *c, Node *root);
void frames_forget(JsCtx *c);
void frame_kill(JsCtx *k);
void frame_nav(JsCtx *k, const char *u);
void js_install_frames(JsCtx *c, v8::Local<v8::Object> N);
void js_install_windows(JsCtx *c, v8::Local<v8::Object> N);
void js_windows_attach(JsCtx *c);
void js_windows_detach(JsCtx *c, bool close_frame = false);
void js_windows_deliver(JsCtx *c);
double js_windows_deadline(JsCtx *c);
bool js_windows_same_origin(const JsCtx *a, const JsCtx *b);
JsCtx *js_window_caller(v8::Isolate *iso);
std::string js_windows_base_url(JsCtx *c, bool include_element);
bool js_windows_navigate(JsCtx *source, JsCtx *target, const std::string &url, bool replace);
bool js_windows_target_origin(JsCtx *source, JsCtx *target, const std::string &wanted, bool *valid);
v8::Local<v8::Value> js_window_value(JsCtx *viewer, JsWindowId id);
JsCtx *js_window_context(JsCtx *viewer, v8::Local<v8::Value> value);
const char *js_window_session_get(JsCtx *c, const std::string &origin);
void js_window_session_set(JsCtx *c, const std::string &origin, const std::string &value);
void js_window_session_forget(void *host_ud);
void js_install_workers(JsCtx *c, v8::Local<v8::Object> N);
void js_install_canvas(JsCtx *c, v8::Local<v8::Object> N);
void js_install_webgpu(JsCtx *c, v8::Local<v8::Object> N);
void js_install_capture(JsCtx *c, v8::Local<v8::Object> N);
void js_capture_close(JsCtx *c);
bool js_capture_allowed(const JsCtx *c, const char *kind);
bool js_windows_is_secure(const JsCtx *c);
bool js_windows_has_activation(const JsCtx *c);
void workers_pump(JsCtx *c);
void workers_kill(JsCtx *c);
double workers_deadline(JsCtx *c);
v8::Platform *js_platform();
