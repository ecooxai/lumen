/* Internal shared state for the V8 binding translation units */
#pragma once
#include <v8.h>
#include <libplatform/libplatform.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <vector>
#include "viewport_state.h"
extern "C" {
#include "js.h"
#include "jsglue.h"
#include "../net/net.h"
#include "../net/download.h"
#include "../base/url.h"
#include "../base/encoding.h"
#include "../media/media.h"
extern const char *g_user_agent;
}

constexpr v8::EmbedderDataTypeTag kTag = v8::kEmbedderDataTypeTagDefault;
extern int g_log_level;

struct JsCtx;
class JsForegroundTasks;
struct JsBroadcastContext;
struct FrameScriptParser;
struct DocumentStream;
/* A prepared external classic script. Ready ordered jobs keep their response
   until every earlier job has executed; async jobs consume their response directly. */
struct ClassicScriptJob {
    Node *script = nullptr;
    Document *document = nullptr;
    uint64_t generation = 0, defer_order = 0;
    std::string source, url;
    bool ordered = false, deferred = false, ready = false, success = false;
};
struct Fetch {
    JsCtx *c;
    uint64_t id;
    v8::Global<v8::Function> cb;
    Node *script;
    v8::Global<v8::Function> head, chunk;
    std::shared_ptr<ClassicScriptJob> script_job;
    NetRequest *request = nullptr;
    std::function<void(JsCtx *, NetResponse *)> native_done;
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
    int nest;   /* HTML timer nesting level; 0 for posted tasks (MessagePort), which are never clamped */
    v8::Global<v8::Function> fn;
};
struct AnimEv {
    double due;
    Node *n;
    v8::Global<v8::Value> target;
    std::string type, name;
    double elapsed;
    bool anim;
};
/* A module graph being fetched off the main thread, for import() (R) or an inserted
   external module script (script); tick_one finishes it once done is set. */
struct ModJob {
    std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
    std::string url;
    v8::Global<v8::Promise::Resolver> R;
    Node *script = nullptr;
    uint64_t generation = 0;
    double at = 0;   /* when its fetch started */
};
/* import attributes (js.cc): module map keys for JSON/text/CSS modules and their synthetic modules */
std::string mod_mkey(const std::string &u, const std::string &t);
std::string mod_kurl(const std::string &k);
std::string mod_ktype(const std::string &k);
bool mod_attr_type(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::FixedArray> a, int stride, bool css, std::string &type);
/* MIME match for an already fetched supported module type; no content sniffing. */
bool mod_mime_matches(const std::string &type, const std::string &ctype);
void mod_synth_drop(v8::Isolate *iso);
v8::MaybeLocal<v8::Module> mod_synthetic(v8::Local<v8::Context> ctx, const std::string &u, const std::string &type, const std::string &body, const std::string &ctype);
/* Only a script's own parse/creation error is retained by its typed module-map
 * key. A dependency error is selected separately for each requested root. */
using ModErrorMap = std::unordered_map<std::string, v8::Global<v8::Value>>;
using ModGraph = std::unordered_map<std::string, std::vector<std::string>>;
using ModResolver = std::string (*)(const std::string &, const std::string &);
bool mod_rethrow_cached(v8::Isolate *iso, const ModErrorMap &errors, const std::string &key);
void mod_cache_script_error(v8::Isolate *iso, ModErrorMap &errors, const std::string &key, v8::TryCatch &caught);
bool mod_script_requests(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Module> module,
                         const std::string &key, bool css, ModResolver resolve, ModErrorMap &errors,
                         std::vector<std::string> &requests);
bool mod_rethrow_graph_error(v8::Isolate *iso, const ModErrorMap &errors, const ModGraph &graph,
                             const std::string &root);
enum class DocumentLoadPhase : uint8_t { Pending, InProgress, Complete };

struct JsCtx {
    v8::Isolate *iso = nullptr;
    std::shared_ptr<JsBroadcastContext> broadcast;
    std::shared_ptr<JsForegroundTasks> foreground_tasks; // Root owns the shared frame isolate.
    bool bg = false, bg_gc = false; double bg_since = 0;   /* tab in the background: V8 favours memory, one idle GC */
    bool bg_seen = false;   /* the bg the page last got a visibilitychange for */
    double bg_timers = 0;   /* a background tab's timers run at most once a second: the last run */
    v8::ArrayBuffer::Allocator *alloc = nullptr;
    v8::Global<v8::Context> ctx;
    v8::Global<v8::ObjectTemplate> node_tmpl;
    v8::Global<v8::Object> api;
    v8::Global<v8::Function> protoFor, fire, report, viewportChanged, pick_cb;
    v8::Global<v8::Function> tt_script, tt_parser_script, tt_eval;   /* Trusted Types / CSP hooks: inline script text, string compilation */
    v8::Global<v8::ObjectTemplate> code_like;
    bool tt_on = false;                            /* inline scripts go through tt_script */
    std::unordered_map<std::string, v8::Global<v8::Object>> protos;
    Document *doc = nullptr;
    JsHost host{};
    JsViewportState viewport_state;
    double viewport_due = 0;
    std::vector<Node *> wrapped;
    std::map<uint32_t, Timer> timers;
    std::set<std::pair<double, uint32_t>> timer_due;   /* (due, id) of every timer, soonest first */
    uint32_t next_timer = 1;
    void timer_erase(std::map<uint32_t, Timer>::iterator it) { timer_due.erase({ it->second.due, it->first }); timers.erase(it); }
    void timer_clear() { timers.clear(); timer_due.clear(); }
    std::map<uint32_t, v8::Global<v8::Function>> rafs;
    uint32_t next_raf = 1;
    v8::Global<v8::Function> video_frame_pump;
    uint64_t video_frame_version = 0;
    v8::Global<v8::Function> media_pump;
    uint64_t media_version = 0;
    double media_due = INFINITY;
    v8::Global<v8::Function> resize_pump;
    uint64_t resize_version = 0;
    bool resize_pending = false;
    double resize_due = 0;
    v8::Global<v8::Function> intersection_pump;
    uint64_t intersection_version = 0;
    bool intersection_pending = false;
    double intersection_due = 0;
    double last_raf = 0, t0 = 0;
    /* rAF loop throttle (raf_gap in js.cc): when the current back-to-back run of frames began, and the
       shell activity generation (page paints, input) seen at the last frame with when it last changed */
    double raf_run_t = 0, raf_act_t = 0;
    uint64_t raf_act_seen = 0;
    std::map<uint64_t, Fetch *> fetches;
    std::deque<std::shared_ptr<ClassicScriptJob>> ordered_scripts, written_deferred;
    uint64_t parser_defer_next = 0;
    size_t pending_scripts = 0;
    bool draining_scripts = false;
    unsigned ignore_destructive_writes = 0;
    bool parser_write_point = false;
    Node *parser_write_script = nullptr;
    uint64_t parser_generation = 1;
    std::shared_ptr<FrameScriptParser> frame_parser;
    std::shared_ptr<DocumentStream> input_stream;
    std::map<uint32_t, struct WsSock *> sockets;
    uint32_t next_ws = 1;
    std::vector<Document *> docs;
    Node *current_script = nullptr;
    std::vector<Node *> written;   /* scripts document.write() added; they run after the script that wrote them */
    int depth = 0;
    int timer_nest = 0;   /* nesting level of the timer callback now running */
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
    bool fullscreen = false;          /* its document has a fullscreen element */
    std::vector<JsCtx *> kids;
    uint32_t id = 0;
    uint32_t wid = 0, opener = 0;     /* top context: its tab's window id, the context that opened the tab */
    bool dead = false;
    bool load_deferred = false;       /* its window load waits for its frames (HTML's "delay the load event") */
    bool load_synced = false;         /* laid out once before load, so its web fonts are requested */
    bool load_checking = false;       /* scan/layout/readiness can reenter completion */
    DocumentLoadPhase load_phase = DocumentLoadPhase::Pending;
    bool xref = false;                /* it or a descendant shared objects with another context: never freed before its tab */
    double dead_at = 0;               /* steady-clock seconds when it was removed; freed 10 s later (loads settled) */
    bool el_load = false;             /* a frame: its <iframe> gets a load event after its window's */
    uint64_t frame_scan_ver = ~0ull;
    void *host_ud = nullptr;          /* child host data returned by frame_open */
    std::string frame_src;
    bool frame_reload_pending = false; /* an attribute assignment starts navigation even when its value is unchanged */
    std::unordered_map<std::string, v8::Global<v8::Module>> mods;   /* ES module map, keyed by URL */
    ModErrorMap mod_errors;
    std::unordered_multimap<int, std::string> mod_url;              /* module identity hash -> key */
    int mod_seq = 0;
    std::vector<std::unique_ptr<ModJob>> mod_jobs;
    /* Promises rejected with no handler (root context, for all its frames); reported after the
       tick's microtask checkpoint unless a handler was attached meanwhile */
    struct Rej { v8::Global<v8::Promise> p; v8::Global<v8::Value> reason; uint32_t ctx; std::string trace; };
    std::vector<Rej> rejs;
    /* Cached modules re-serialised once the page has been quiet, so the cache also holds
       functions compiled lazily after evaluation: {script, cache path, bytes loaded}. */
    struct CodeRefresh { v8::Global<v8::UnboundModuleScript> s; std::string path; int had; };
    std::vector<CodeRefresh> code_refresh;
    double code_refresh_due = 0;
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
/* HTTP fields are WebIDL ByteStrings, not UTF-8 text. Outgoing values have
   already passed the Fetch/XHR ByteString and header grammar conversions. */
v8::Local<v8::String> jbytes(v8::Isolate *iso, const char *s, int n = -1);
std::string jcbytes(v8::Isolate *iso, v8::Local<v8::Value> v);
void js_url_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target); /* urljs.cc: urlParse, urlSet */
void js_c2d_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> W); /* jsnative.cc: c2dFill, c2dClip, c2dMeasure */
void js_crypto_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target); /* cryptojs.cc: the WebCrypto primitives */
void js_compress_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target); /* compressjs.cc: zlib streams for CompressionStream */
void js_encoding_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target); /* encodingjs.cc: TextDecoder, form and charset natives */
v8::Local<v8::Value> jwrap(JsCtx *c, Node *n);
Node *junwrap(v8::Local<v8::Value> v);
v8::MaybeLocal<v8::Value> jcall(JsCtx *c, v8::Local<v8::Function> f, v8::Local<v8::Value> recv, int argc, v8::Local<v8::Value> *argv);
void jfire(JsCtx *c, Node *n, const char *type);
void jreport(JsCtx *c, v8::Local<v8::Value> exc, v8::Local<v8::Message> msg);
void js_install_native(JsCtx *c, v8::Local<v8::Object> N);
#if defined(__ANDROID__) && !defined(NDEBUG)
bool jsprof_active(void);      /* the debug JS profile is running */
void js_native_report(void);   /* log and reset per-native call counts and time */
#endif
void js_run_inserted(JsCtx *c, Node *root);
extern "C" void js_run_dynamic_inline_script(JsCtx *c, Node *script, const char *src, size_t n, const char *url);
/* js_run_module for script-inserted modules: an external graph not loaded yet runs later, once fetched */
extern "C" void js_run_module_inserted(JsCtx *c, Node *script, const char *src, size_t n, const char *url);
extern "C" JsCtx *js_new_ex(Document *d, const JsHost *host, JsCtx *parent, Node *frame);
extern "C" JsCtx *owner_ctx(Document *d);
bool js_context_fully_active(JsCtx *c);
std::string ctx_origin(JsCtx *c);
std::string ctx_origin_key(JsCtx *c);   /* for same-origin checks: ctx_origin, but each file: document is its own origin, as in Chrome */
void frames_scan(JsCtx *c);
void frames_attribute_changed(Node *frame, const char *name, const char *ns);
void frames_inserted(JsCtx *c, Node *root);
void frames_forget(JsCtx *c);
void frames_parser_tick(JsCtx *c);
void frames_parser_cancel(JsCtx *c);
extern "C" Node *js_parser_custom_element(JsCtx *c, Node *element, Node *intended_parent);
extern "C" void js_parser_custom_inserted(JsCtx *c, Node *element, Node *parent, Node *previous, Node *next);
void document_stream_tick(JsCtx *c);
bool document_stream_write_point(JsCtx *c);
void document_stream_cancel(JsCtx *c);
void js_doc_write(JsCtx *c, const char *source, size_t length);
void js_doc_close(JsCtx *c);
extern "C" void js_document_open(JsCtx *c);
extern "C" void js_run_written_inline(JsCtx *c);
void js_queue_written_deferred(JsCtx *c, Node *script, const char *url);
void js_cancel_fetch(JsCtx *c, uint64_t id);
void js_cancel_fetches(JsCtx *c, bool scripts_only = false);
uint64_t js_fetch_parser_source(JsCtx *c, Node *script, const char *url,
                               std::function<void(JsCtx *, NetResponse *)> done);
void js_doc_reparse(JsCtx *k, const char *html, size_t n);   /* document.close() of a script-opened document */
void frames_load_check(JsCtx *c);   /* fires a deferred window load once no frame delays it */
void frame_kill(JsCtx *k);
void frame_nav(JsCtx *k, const char *u);
void js_install_frames(JsCtx *c, v8::Local<v8::Object> N);
extern "C" JsCtx *js_ctx_by_id(uint32_t id);        /* any live context, any tab */
extern "C" JsCtx *js_ctx_of_window(uint32_t wid);   /* the top context a tab shows now */
bool xmsg_waiting(JsCtx *c);             /* another tab's postMessage waits for c */
void xmsg_deliver(JsCtx *c);
void js_install_workers(JsCtx *c, v8::Local<v8::Object> N);
void js_wasm_init(v8::Isolate *iso);
void js_wasm_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> native);
void workers_pump(JsCtx *c);
void workers_kill(JsCtx *c);
double workers_deadline(JsCtx *c);
void js_install_rtc(JsCtx *c, v8::Local<v8::Object> N);   /* rtc.cc: native WebRTC transport */
void js_install_webgl(JsCtx *c, v8::Local<v8::Object> N);   /* webgl.cc: N.gl, WebGL over GLES 3 */
void js_install_webgpu(JsCtx *c, v8::Local<v8::Object> N);   /* webgpu.cc: N.gpu, WebGPU over wgpu */
bool rtc_pending(JsCtx *c);
bool rtc_active(JsCtx *c);   /* the page has used native WebRTC */
void rtc_pump(JsCtx *c);
void rtc_ctx_free(JsCtx *c);
double webgpu_deadline(JsCtx *c);   /* when webgpu_pump has work: queued events now, GPU work in flight soon */
void webgpu_pump(JsCtx *c);
v8::Platform *js_platform();

/* a request body argument: an ArrayBuffer or view as its bytes, anything else as its UTF-8 string */
inline std::string js_body_bytes(v8::Isolate *iso, v8::Local<v8::Value> v) {
    if (v->IsArrayBufferView()) { auto w = v.As<v8::ArrayBufferView>(); std::string b(w->ByteLength(), '\0'); w->CopyContents(b.data(), b.size()); return b; }
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); return std::string((const char *)ab->Data(), ab->ByteLength()); }
    return jcstr(iso, v);
}
