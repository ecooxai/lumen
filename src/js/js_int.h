/* Internal shared state for the V8 binding translation units */
#pragma once
#include <v8.h>
#include <libplatform/libplatform.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
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
};
struct Timer {
    double due, interval;
    bool repeat;
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
struct JsCtx {
    v8::Isolate *iso = nullptr;
    v8::ArrayBuffer::Allocator *alloc = nullptr;
    v8::Global<v8::Context> ctx;
    v8::Global<v8::ObjectTemplate> node_tmpl;
    v8::Global<v8::Object> api;
    v8::Global<v8::Function> protoFor, fire, report, mediaChanged;
    std::unordered_map<std::string, v8::Global<v8::Object>> protos;
    Document *doc = nullptr;
    JsHost host{};
    std::vector<Node *> wrapped;
    std::map<uint32_t, Timer> timers;
    uint32_t next_timer = 1;
    std::map<uint32_t, v8::Global<v8::Function>> rafs;
    uint32_t next_raf = 1;
    double last_raf = 0, t0 = 0;
    std::map<uint64_t, Fetch *> fetches;
    std::vector<Document *> docs;
    Node *current_script = nullptr;
    int depth = 0;
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
};

#define JS_ENTER(c)                                         \
    v8::Isolate *iso = (c)->iso;                            \
    v8::Isolate::Scope js_is_(iso);                         \
    v8::HandleScope js_hs_(iso);                            \
    v8::Local<v8::Context> ctx = (c)->ctx.Get(iso);         \
    v8::Context::Scope js_cs_(ctx)

JsCtx *jctx(v8::Isolate *iso);
v8::Local<v8::String> jstr(v8::Isolate *iso, const char *s, int n = -1);
std::string jcstr(v8::Isolate *iso, v8::Local<v8::Value> v);
v8::Local<v8::Value> jwrap(JsCtx *c, Node *n);
Node *junwrap(v8::Local<v8::Value> v);
v8::MaybeLocal<v8::Value> jcall(JsCtx *c, v8::Local<v8::Function> f, v8::Local<v8::Value> recv, int argc, v8::Local<v8::Value> *argv);
void jfire(JsCtx *c, Node *n, const char *type);
void jreport(JsCtx *c, v8::Local<v8::Value> exc, v8::Local<v8::Message> msg);
void js_install_native(JsCtx *c, v8::Local<v8::Object> N);
void js_run_inserted(JsCtx *c, Node *root);
