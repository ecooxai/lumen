/* Real V8 realms behind a bounded, deferred native-window host. The fixture
 * never performs a navigation or frees a Page from an open/close callback.
 * It tests public Window APIs and the registry's exact owned-byte counters. */
#include "../src/js/js_int.h"
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" void font_init(void);

static int checks, failures;
static bool trace_checks;
#define CHECK(name, condition) do { ++checks; \
    if (trace_checks) fprintf(stderr, "CHECK %d %s\n", checks, name); \
    if (!(condition)) { \
    ++failures; fprintf(stderr, "FAIL %s\n", name); \
} } while (0)

/* Mutations use the real host entry point, including its task depth and
 * microtask checkpoint. Reading this test-only result does not run script. */
static bool evaluate(JsCtx *c, const std::string &body) {
    if (!c || c->dead) return false;
    std::string source = "globalThis.__windowTestResult=false;"
        "globalThis.__windowTestResult=(()=>{" + body + "\n})();";
    js_eval(c, source.c_str(), "test:window-lifecycle");
    JS_ENTER(c);
    v8::Local<v8::Value> result;
    return ctx->Global()->Get(ctx, jstr(iso, "__windowTestResult")).ToLocal(&result) && result->IsTrue();
}
static bool expression(JsCtx *c, const std::string &value) { return evaluate(c, "return (" + value + ");"); }

struct Pending {
    size_t count = 0, bytes = 0;
    Pending() { js_windows_pending(&count, &bytes); }
};
static bool empty_queue() { Pending p; return p.count == 0 && p.bytes == 0; }
static bool expose_pending(JsCtx *c) {
    JS_ENTER(c);
    auto read = [](const v8::FunctionCallbackInfo<v8::Value> &a) {
        Pending p; auto *iso = a.GetIsolate();
        v8::Local<v8::Value> values[] = {v8::Number::New(iso, double(p.count)), v8::Number::New(iso, double(p.bytes))};
        a.GetReturnValue().Set(v8::Array::New(iso, values, 2));
    };
    return ctx->Global()->Set(ctx, jstr(iso, "windowTestPending"), v8::Function::New(ctx, read).ToLocalChecked()).FromMaybe(false);
}

/* A real V8 backing-store deleter proves release independently of the queue's
 * accounting counters. Its heap-owned observer remains valid even on failure. */
using ReleaseObserver = std::shared_ptr<std::atomic<unsigned>>;
static ReleaseObserver tracked_buffer(JsCtx *c, size_t bytes) {
    auto released = std::make_shared<std::atomic<unsigned>>(0);
    void *data = calloc(1, bytes);
    if (!data) return {};
    auto *token = new ReleaseObserver(released);
    auto deleter = [](void *p, size_t, void *opaque) {
        std::unique_ptr<ReleaseObserver> observer(static_cast<ReleaseObserver *>(opaque));
        free(p); (**observer).fetch_add(1);
    };
    JS_ENTER(c);
    std::shared_ptr<v8::BackingStore> backing = v8::ArrayBuffer::NewBackingStore(data, bytes, deleter, token);
    auto buffer = v8::ArrayBuffer::New(iso, backing);
    if (!ctx->Global()->Set(ctx, jstr(iso, "trackedBuffer"), buffer).FromMaybe(false)) return {};
    return released;
}

static Document *document(const char *url) {
    Document *d = doc_new(url);
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    return d;
}

struct World {
    struct Window {
        JsWindowId id = 0;
        Document *doc = nullptr;
        JsCtx *context = nullptr;
        bool live = true;
    };
    struct Open {
        JsWindowId id, source, source_top, opener;
        std::string name, url, referrer;
        bool has_referrer, noreferrer;
        explicit Open(const JsWindowOpen &r) : id(r.id), source(r.source), source_top(r.source_top), opener(r.opener),
            name(r.name ? r.name : ""), url(r.url ? r.url : ""), referrer(r.referrer ? r.referrer : ""),
            has_referrer(r.referrer != nullptr), noreferrer(r.noreferrer) {}
    };
    struct Action {
        enum Kind { OPEN, NAVIGATE, CLOSE } kind;
        JsWindowId id;
        std::string url, referrer;
        bool replace = false;
    };
    std::vector<std::unique_ptr<Window>> windows;
    std::vector<Open> opened;
    std::vector<Action> navigated;
    std::deque<Action> actions;
    std::vector<std::pair<JsCtx *, Document *>> retired;
    size_t capacity = 12;
    unsigned closes = 0, focuses = 0;
    bool reject_open = false, reject_navigate = false;

    size_t live_count() const {
        size_t n = 0; for (const auto &w : windows) n += w->live; return n;
    }
    Window *find(JsWindowId id) const {
        for (const auto &w : windows) if (w->id == id && w->live) return w.get();
        return nullptr;
    }
    Window *reserve(JsWindowId id) {
        auto w = std::make_unique<Window>(); w->id = id;
        Window *result = w.get(); windows.push_back(std::move(w)); return result;
    }
    static bool open(void *opaque, const JsWindowOpen *r) {
        auto &self = *static_cast<World *>(opaque);
        if (self.reject_open || self.live_count() >= self.capacity) return false;
        self.opened.emplace_back(*r); self.reserve(r->id);
        self.actions.push_back({Action::OPEN, r->id, r->url ? r->url : "about:blank", r->referrer ? r->referrer : ""});
        return true;
    }
    static bool navigate(void *opaque, const JsWindowNavigate *r) {
        auto &self = *static_cast<World *>(opaque);
        if (self.reject_navigate || !self.find(r->id)) return false;
        Action a{Action::NAVIGATE, r->id, r->url ? r->url : "", r->referrer ? r->referrer : "", r->replace};
        self.navigated.push_back(a); self.actions.push_back(std::move(a)); return true;
    }
    static bool close(void *opaque, JsWindowId id) {
        auto &self = *static_cast<World *>(opaque);
        if (!self.find(id)) return false;
        ++self.closes; self.actions.push_back({Action::CLOSE, id, "", ""}); return true;
    }
    static bool focus(void *opaque, JsWindowId id) {
        auto &self = *static_cast<World *>(opaque);
        if (!self.find(id)) return false;
        ++self.focuses; return true;
    }
    JsWindowHost host() { return {this, open, navigate, close, focus}; }

    /* retain_old models the allowed commit/attach/free order precisely. It is
     * used to prove that a late old-context detach cannot clear its successor. */
    bool install(Window &w, const char *url, JsWindowCoop coop = JS_COOP_UNSAFE_NONE,
                 bool retain_old = false, const char *referrer = nullptr) {
        Document *d = document(url);
        if (referrer && *referrer) d->referrer = strdup(referrer);
        JsWindowId id = js_window_commit(w.id, d, coop);
        if (!id) { doc_free(d); return false; }
        JsCtx *old = w.context; Document *old_doc = w.doc;
        w.id = id;
        if (!retain_old) { js_free(old); if (old_doc) doc_free(old_doc); }
        else if (old) retired.emplace_back(old, old_doc);
        w.doc = d;
        JsHost h{}; h.window_id = id;
        w.context = js_new(d, &h);
        return w.context != nullptr;
    }
    Window *user(const char *url = "https://app.test/start", JsWindowCoop coop = JS_COOP_UNSAFE_NONE) {
        JsWindowHost h = host();
        Window *w = reserve(js_window_new(&h));
        if (!w->id || !install(*w, url, coop)) {
            CHECK("native fixture creates a registered page", false); return nullptr;
        }
        return w;
    }
    void release_retired() {
        for (auto &p : retired) { js_free(p.first); doc_free(p.second); }
        retired.clear();
    }
    void destroy(Window &w) {
        if (!w.live) return;
        js_window_destroy(w.id); w.live = false;
        js_free(w.context); w.context = nullptr;
        if (w.doc) doc_free(w.doc);
        w.doc = nullptr;
    }
    void drain() {
        while (!actions.empty()) {
            Action a = std::move(actions.front()); actions.pop_front();
            Window *w = find(a.id);
            if (!w) continue;
            if (a.kind == Action::CLOSE || js_window_is_closed(a.id)) { destroy(*w); continue; }
            if (a.kind == Action::OPEN) {
                CHECK("accepted open installs a real initial blank page", install(*w, "about:blank"));
                if (!a.url.empty() && a.url != "about:blank")
                    CHECK("requested popup document installs after blank", install(*w, a.url.c_str(), JS_COOP_UNSAFE_NONE, false, a.referrer.c_str()));
            } else CHECK("deferred navigation commits", install(*w, a.url.c_str(), JS_COOP_UNSAFE_NONE, false, a.referrer.c_str()));
        }
    }
    void pump(unsigned rounds = 8) {
        for (unsigned i = 0; i < rounds; ++i) {
            drain();
            std::vector<Window *> active;
            for (const auto &w : windows) if (w->live && w->context) active.push_back(w.get());
            for (Window *w : active) if (w->live && w->context) js_tick(w->context);
            drain();
            /* queueMessage uses a timer; yielding makes its eventual task due. */
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    Window *popup(JsCtx *source, const char *variable = "popup", const char *url = "about:blank",
                  const char *name = "_blank", const char *features = "") {
        size_t previous = opened.size();
        js_window_activate(source);
        std::string body = "globalThis." + std::string(variable) + "=window.open('" + url + "','" + name + "','" + features + "');return " + variable + "!==null;";
        bool returned = evaluate(source, body);
        CHECK("trusted open returns a live popup proxy", returned && opened.size() == previous + 1);
        if (!returned || opened.size() != previous + 1) return nullptr;
        JsWindowId id = opened.back().id; drain(); return find(id);
    }
    ~World() {
        actions.clear();
        for (auto &w : windows) destroy(*w);
        release_retired();
        CHECK("world teardown owns no directed message bytes", empty_queue());
    }
};

static bool receiver(JsCtx *c) {
    return evaluate(c, "globalThis.events=[];onmessage=e=>events.push(e);return true;");
}

static void admission_and_blank() {
    World w; auto *a = w.user(); if (!a) return;
    CHECK("user page exposes its stable native ID", js_window_id(a->context) == a->id);
    CHECK("user page starts without an opener", expression(a->context, "opener===null && !closed"));
    CHECK("unrelated idle user page may sleep", js_window_can_sleep(a->id));
    CHECK("synthetic click does not authorize a popup", evaluate(a->context,
        "let b=document.createElement('button');document.body.appendChild(b);"
        "b.onclick=()=>globalThis.denied=window.open('about:blank');b.click();return denied===null;"));
    CHECK("blocked synthetic open reaches no host reservation", w.opened.empty());
    a->doc->csp = strdup("script-src 'self'");
    auto *p = w.popup(a->context, "popup", "about:blank", "login"); if (!p) return;
    CHECK("initial blank inherits the creator principal", ctx_origin(p->context) == "https://app.test");
    CHECK("initial blank inherits the creator base URL", expression(p->context,
        "location.href==='about:blank' && document.baseURI==='https://app.test/start' && origin==='https://app.test'"));
    CHECK("initial blank inherits the creator CSP", p->doc->csp && !strcmp(p->doc->csp, a->doc->csp));
    CHECK("separate native top tabs use separate V8 isolates", p->context->iso != a->context->iso);
    CHECK("popup relationships keep their destinations awake", !js_window_can_sleep(a->id) && !js_window_can_sleep(p->id));
    CHECK("popup proxy identity and top-level relationships are stable", expression(a->context,
        "popup===popup.window && popup===popup.self && popup===popup.top && popup===popup.parent && !popup.closed"));
    CHECK("popup sees a stable opener handle", expression(p->context, "opener!==null && opener===opener.window && !opener.closed"));
    CHECK("one trusted activation cannot create two popups", expression(a->context, "window.open('about:blank')===null"));
    size_t opened = w.opened.size();
    CHECK("a familiar named target reuses its existing proxy", expression(a->context, "window.open('', 'login')===popup"));
    CHECK("named reuse does not allocate another native reservation", w.opened.size() == opened);
    w.drain();
    CHECK("focus is a copied native request", evaluate(a->context, "popup.focus();return true;") && w.focuses == 1);
    CHECK("close returns before native page destruction", evaluate(a->context, "popup.close();return popup.closed;") && p->context && p->live);
    CHECK("close marks the registry unavailable immediately", js_window_is_closed(p->id));
    w.drain();
    CHECK("native drain destroys the closed page", !p->live && p->context == nullptr);
    CHECK("closed proxy stays safe after its JsCtx is freed", expression(a->context, "popup.closed && popup===popup.window"));
    CHECK("closed proxy location and arbitrary property checks reject safely", evaluate(a->context,
        "let blocked=0;for(const read of [()=>popup.location.href,()=>popup.privateState]){"
        "try{read();}catch(e){if(e.name==='SecurityError')blocked++;}}return blocked===2;"));
    CHECK("closed auxiliary releases the opener sleep gate", js_window_can_sleep(a->id));

    w.capacity = w.live_count(); js_window_activate(a->context);
    CHECK("host capacity rejection returns null", expression(a->context, "window.open('about:blank')===null"));
    CHECK("failed admission leaves no active relationship or queue", js_window_can_sleep(a->id) && empty_queue());
}

static void dynamic_blank_base() {
    World w; auto *a = w.user(); if (!a) return;
    CHECK("creator can establish a dynamic document base before opening", evaluate(a->context,
        "globalThis.creatorBase=document.createElement('base');creatorBase.setAttribute('href','https://base.test/root/');"
        "document.head.appendChild(creatorBase);return document.baseURI==='https://base.test/root/';"));
    auto *p = w.popup(a->context, "popup", "about:blank"); if (!p) return;
    CHECK("initial blank snapshots the creator's current dynamic base", expression(p->context,
        "document.baseURI==='https://base.test/root/' && location.href==='about:blank' && origin==='https://app.test'"));
    CHECK("a later creator base mutation succeeds", evaluate(a->context,
        "creatorBase.setAttribute('href','https://later.test/changed/');return document.baseURI==='https://later.test/changed/';"));
    CHECK("creator changes do not mutate the popup's inherited base snapshot", expression(p->context,
        "document.baseURI==='https://base.test/root/'"));
    CHECK("popup-local relative base resolves against the inherited fallback", evaluate(p->context,
        "globalThis.popupBase=document.createElement('base');popupBase.setAttribute('href','child/');"
        "document.head.appendChild(popupBase);return document.baseURI==='https://base.test/root/child/';"));
    size_t previous = w.opened.size(); js_window_activate(p->context);
    CHECK("relative popup navigation uses the same public dynamic base", evaluate(p->context,
        "globalThis.nested=window.open('relative-login','_blank');return nested!==null;") &&
        w.opened.size() == previous + 1 && w.opened.back().url == "https://base.test/root/child/relative-login");
    w.drain();
    CHECK("removing the local base restores its immutable inherited fallback", evaluate(p->context,
        "popupBase.remove();return document.baseURI==='https://base.test/root/';"));
}

static void message_and_clone() {
    World w; auto *a = w.user(); auto *bystander = w.user(); if (!a || !bystander) return;
    auto *p = w.popup(a->context); if (!p) return;
    CHECK("receiver setup succeeds", receiver(p->context) && receiver(bystander->context));
    CHECK("sending copies cyclic data and typed-array bytes", evaluate(a->context,
        "globalThis.payload={tag:'copy',bytes:new Uint8Array([4,5,6])};payload.self=payload;"
        "popup.postMessage(payload,'https://APP.test:443/some/path');payload.tag='changed';payload.bytes[0]=99;return true;"));
    Pending queued;
    CHECK("one owned serialized message exists before delivery", queued.count == 1 && queued.bytes > 3);
    CHECK("postMessage never calls the receiver synchronously", expression(p->context, "events.length===0"));
    w.pump();
    CHECK("clone keeps cycles and the send-time bytes", expression(p->context,
        "events.length===1 && events[0].data.tag==='copy' && events[0].data.self===events[0].data && "
        "events[0].data.bytes[0]===4 && events[0].data.bytes[2]===6"));
    CHECK("message source, origin and trust are correct", expression(p->context,
        "events[0].source===opener && events[0].origin==='https://app.test' && events[0].isTrusted"));
    CHECK("directed messages do not broadcast to an unrelated same-origin tab", expression(bystander->context, "events.length===0"));
    CHECK("delivered clones release all native queue ownership", empty_queue());

    CHECK("default origin and options dictionary both work", evaluate(a->context,
        "popup.postMessage('default');popup.postMessage('options',{targetOrigin:'https://app.test'});return true;"));
    w.pump();
    CHECK("messages preserve send ordering", expression(p->context, "events.length===3 && events[1].data==='default' && events[2].data==='options'"));
    CHECK("origin mismatch still performs structured serialization", evaluate(a->context,
        "globalThis.getterCalls=0;popup.postMessage({get value(){getterCalls++;return 1;}},'https://other.test');return getterCalls===1;"));
    w.pump();
    CHECK("a mismatching explicit target origin receives nothing", expression(p->context, "events.length===3") && empty_queue());
    CHECK("malformed target origin throws SyntaxError", evaluate(a->context,
        "try{popup.postMessage(1,'https://[');}catch(e){return e.name==='SyntaxError';}return false;"));
    CHECK("uncloneable functions fail without an owned queue entry", evaluate(a->context,
        "try{popup.postMessage(()=>{},'*');}catch(e){return e.name==='DataCloneError';}return false;") && empty_queue());
    CHECK("DOM nodes cannot cross the isolate serialization boundary", evaluate(a->context,
        "try{popup.postMessage(document.body,'*');}catch(e){return e.name==='DataCloneError';}return false;") && empty_queue());

    CHECK("ArrayBuffer transfer detaches only after admission", evaluate(a->context,
        "globalThis.transferBuffer=new ArrayBuffer(65536);new Uint8Array(transferBuffer)[65535]=173;"
        "popup.postMessage(transferBuffer,{targetOrigin:'*',transfer:[transferBuffer]});return transferBuffer.byteLength===0;"));
    Pending transfer;
    CHECK("queued bytes include transferred backing-store ownership", transfer.count == 1 && transfer.bytes >= 65536 && transfer.bytes < 65536 + 1024);
    w.pump();
    CHECK("transferred buffer reaches its destination intact", expression(p->context,
        "events.length===4 && events[3].data.byteLength===65536 && new Uint8Array(events[3].data)[65535]===173"));
    CHECK("delivery releases transferred native backing-store references", empty_queue());
}

static void oauth_source_lifetime() {
    World w; auto *a = w.user(); if (!a) return;
    auto *p = w.popup(a->context, "popup", "https://accounts.test/login"); if (!p) return;
    CHECK("OAuth receiver setup succeeds", receiver(a->context));
    CHECK("cross-origin proxy cannot expose a document", evaluate(a->context,
        "try{return popup.document==null;}catch(e){return e.name==='SecurityError';}"));
    CHECK("OAuth result can be posted and the popup closed in one task", evaluate(p->context,
        "opener.postMessage({code:'one-time-result'},'https://app.test');close();return closed;"));
    Pending queued;
    CHECK("close defers native teardown until after the sending task", p->live && p->context && queued.count == 1);
    JsWindowId old_source = p->id; w.drain();
    CHECK("source destruction preserves its already accepted message", !p->live && Pending().count == 1);
    w.pump();
    CHECK("OAuth result arrives after its sender has been freed", expression(a->context,
        "events.length===1 && events[0].data.code==='one-time-result' && events[0].origin==='https://accounts.test'"));
    CHECK("event.source is the original closed proxy", expression(a->context, "events[0].source===popup && events[0].source.closed"));
    CHECK("retired native source IDs cannot become live again", js_window_is_closed(old_source));
    CHECK("OAuth delivery releases every retained byte", empty_queue());

    auto *q = w.popup(a->context, "second", "https://accounts.test/first"); if (!q) return;
    JsWindowId source = q->id;
    CHECK("source queues a result before navigation", evaluate(q->context,
        "opener.postMessage('before-navigation','https://app.test');return true;"));
    CHECK("source navigation installs an independent new realm", w.install(*q, "https://next.test/landing") && q->id == source);
    w.pump();
    CHECK("queued origin is captured at send time, not delivery", expression(a->context,
        "events.length===2 && events[1].data==='before-navigation' && events[1].origin==='https://accounts.test' && events[1].source===second && !second.closed"));
    CHECK("new source principal is immutable under history rewrite", evaluate(q->context,
        "try{history.replaceState(null,'','https://accounts.test/spoof');}catch(e){return e.name==='SecurityError' && origin==='https://next.test';}return false;"));
}

static void destination_lifetime() {
    World w; auto *a = w.user(); if (!a) return;
    auto *p = w.popup(a->context); if (!p) return;
    CHECK("replacement receiver setup succeeds", receiver(p->context));
    Document *old_document = p->doc;
    CHECK("a location request is deferred by the native host", evaluate(a->context,
        "popup.location.href='https://app.test/requested';return true;") &&
        w.navigated.size() == 1 && p->doc == old_document);
    CHECK("a started navigation still targets the installed old document", evaluate(a->context,
        "popup.postMessage('old-document-still-current','https://app.test');return true;"));
    js_tick(p->context);
    CHECK("starting a load does not discard old-document messages", expression(p->context,
        "events.length===1 && events[0].data==='old-document-still-current'") && empty_queue());
    w.drain();
    CHECK("native navigation commit installs the copied destination", p->doc != old_document &&
        !strcmp(p->doc->url, "https://app.test/requested"));
    CHECK("post-navigation receiver setup succeeds", receiver(p->context));
    CHECK("queue owns transferred bytes before destination replacement", evaluate(a->context,
        "let b=new ArrayBuffer(131072);popup.postMessage(b,'*',[b]);return b.byteLength===0;") && Pending().bytes >= 131072);
    CHECK("actual destination commit replaces the document", w.install(*p, "https://app.test/replaced"));
    CHECK("destination replacement immediately purges old serialized ownership", empty_queue());
    CHECK("new destination receiver setup succeeds", receiver(p->context));
    w.pump();
    CHECK("new document cannot receive the old document's message", expression(p->context, "events.length===0"));

    CHECK("navigation commit may attach a successor before freeing its old JsCtx", w.install(*p, "https://app.test/latest", JS_COOP_UNSAFE_NONE, true));
    CHECK("successor receiver setup succeeds", receiver(p->context));
    CHECK("message targets the installed successor", evaluate(a->context, "popup.postMessage('successor','https://app.test');return true;") && Pending().count == 1);
    w.release_retired();
    CHECK("late old-context destruction preserves successor messages", Pending().count == 1 && !js_window_is_closed(p->id));
    w.pump();
    CHECK("successor remains registered and receives its message", expression(p->context, "events.length===1 && events[0].data==='successor'") && empty_queue());

    CHECK("serialization getters may close their destination safely", evaluate(a->context,
        "popup.postMessage({get value(){popup.close();return 7;}},'*');return popup.closed;"));
    CHECK("closing during serialization retains no destination message", empty_queue());
    w.drain();
    CHECK("closed destination may be referenced without use after free", expression(a->context, "popup.closed"));

    auto *q = w.popup(a->context, "next"); if (!q) return;
    CHECK("new browsing context IDs never reuse closed targets", q->id != p->id);
    CHECK("queued backing store is accounted before native destination close", evaluate(a->context,
        "let b=new ArrayBuffer(262144);next.postMessage(b,{targetOrigin:'*',transfer:[b]});return b.byteLength===0;") && Pending().bytes >= 262144);
    w.destroy(*q);
    CHECK("native destination close frees both bytes and queued records", empty_queue());
    js_window_destroy(q->id);
    CHECK("destroy is idempotent and does not resurrect a target", empty_queue() && js_window_is_closed(q->id));
}

static void opaque_and_features() {
    {
        World w; auto *a = w.user("data:text/html,creator"); if (!a) return;
        auto *blank = w.popup(a->context, "blank"); if (!blank) return;
        CHECK("opaque creator and initial blank share an origin identity", receiver(blank->context) &&
            evaluate(a->context, "blank.postMessage('inherited');return true;"));
        w.pump();
        CHECK("inherited opaque default-origin message is delivered", expression(blank->context,
            "events.length===1 && events[0].data==='inherited' && events[0].origin==='null'"));
        auto *p = w.popup(a->context, "other"); if (!p) return;
        /* Native document installation can supply an opaque principal even
         * though this bounded auxiliary URL API intentionally rejects data:. */
        CHECK("native navigation installs a different opaque document", w.install(*p, "data:text/html,separate"));
        CHECK("independent opaque receiver setup succeeds", receiver(p->context));
        CHECK("two opaque origins can serialize as null without being equal", ctx_origin(a->context) == "null" && ctx_origin(p->context) == "null");
        CHECK("opaque sends use default and wildcard target origins", evaluate(a->context,
            "other.postMessage('must-drop');other.postMessage('wildcard','*');return true;"));
        w.pump();
        CHECK("default origin cannot authenticate a different opaque principal", expression(p->context,
            "events.length===1 && events[0].data==='wildcard' && events[0].origin==='null'"));
    }
    {
        World w; auto *a = w.user(); if (!a) return;
        js_window_activate(a->context);
        CHECK("noopener returns null while admitting the independent tab", expression(a->context,
            "window.open('about:blank','no-opener','noopener')===null"));
        CHECK("noopener is explicit in the copied host request", w.opened.size() == 1 && w.opened.back().opener == 0);
        if (w.opened.empty()) return;
        JsWindowId no_opener = w.opened.back().id; w.drain(); auto *p = w.find(no_opener);
        CHECK("noopener page is installed with no opener", p && expression(p->context, "opener===null"));
        CHECK("noopener does not pin an otherwise idle creator", js_window_can_sleep(a->id));
        js_window_activate(a->context);
        CHECK("noreferrer returns null", expression(a->context,
            "window.open('https://accounts.test/','private','noreferrer')===null"));
        CHECK("noreferrer omits native referrer and implies noopener", w.opened.size() == 2 &&
            w.opened.back().noreferrer && !w.opened.back().has_referrer && w.opened.back().opener == 0);
        w.drain();
        auto *normal = w.popup(a->context, "normal", "about:blank", "with-opener", "noopener=false,noreferrer=0");
        CHECK("false boolean feature values preserve the opener", normal && expression(normal->context, "opener!==null") &&
            !w.opened.back().noreferrer && w.opened.back().opener == a->id);
    }
}

static void queue_budgets() {
    {
        World w; auto *a = w.user(); if (!a) return;
        std::vector<World::Window *> targets;
        for (unsigned i = 0; i < 5; ++i) {
            std::string name = "p" + std::to_string(i);
            auto *p = w.popup(a->context, name.c_str()); if (!p) return;
            targets.push_back(p);
        }
        CHECK("one target admits exactly its per-target message count", evaluate(a->context,
            "for(let i=0;i<" + std::to_string(JS_WINDOW_TARGET_MAX_COUNT) + ";i++)p0.postMessage(i,'*');return true;") &&
            Pending().count == JS_WINDOW_TARGET_MAX_COUNT);
        Pending full_target;
        CHECK("per-target overflow throws without changing ownership", evaluate(a->context,
            "try{p0.postMessage('overflow','*');}catch(e){return e.name==='QuotaExceededError';}return false;") &&
            Pending().count == full_target.count && Pending().bytes == full_target.bytes);
        CHECK("other targets can fill the remaining global count", evaluate(a->context,
            "for(const p of [p1,p2,p3])for(let i=0;i<" + std::to_string(JS_WINDOW_TARGET_MAX_COUNT) + ";i++)p.postMessage(i,'*');return true;") &&
            Pending().count == JS_WINDOW_PENDING_MAX_COUNT);
        Pending full_global;
        CHECK("global count rejects a message to an otherwise empty target", evaluate(a->context,
            "try{p4.postMessage('overflow','*');}catch(e){return e.name==='QuotaExceededError';}return false;") &&
            Pending().count == full_global.count && Pending().bytes == full_global.bytes);
        w.destroy(*targets[0]);
        CHECK("closing one target frees only that target's queued records", Pending().count == JS_WINDOW_PENDING_MAX_COUNT - JS_WINDOW_TARGET_MAX_COUNT);
        CHECK("released count capacity can be reused", evaluate(a->context, "p4.postMessage('available','*');return true;") &&
            Pending().count == JS_WINDOW_PENDING_MAX_COUNT - JS_WINDOW_TARGET_MAX_COUNT + 1);
    }
    {
        World w; auto *a = w.user(); if (!a) return;
        auto *p = w.popup(a->context); if (!p) return;
        CHECK("single-message byte limit fails without detaching the caller buffer", evaluate(a->context,
            "let b=new ArrayBuffer(" + std::to_string(JS_WINDOW_MESSAGE_MAX_BYTES + 1u) + ");"
            "try{popup.postMessage(b,'*',[b]);}catch(e){return e.name==='DataCloneError' && b.byteLength===" +
            std::to_string(JS_WINDOW_MESSAGE_MAX_BYTES + 1u) + ";}return false;") && empty_queue());
        const unsigned chunk = 7u * 1024u * 1024u;
        CHECK("four transferred buffers are admitted below the global byte limit", evaluate(a->context,
            "for(let i=0;i<4;i++){let b=new ArrayBuffer(" + std::to_string(chunk) + ");"
            "popup.postMessage(b,'*',[b]);if(b.byteLength!==0)return false;}return true;"));
        Pending held;
        CHECK("retained transfer storage is included in the global byte total", held.count == 4 &&
            held.bytes >= size_t(chunk) * 4 && held.bytes <= JS_WINDOW_PENDING_MAX_BYTES);
        CHECK("backing-store transfer cannot bypass the global memory budget", evaluate(a->context,
            "let b=new ArrayBuffer(" + std::to_string(chunk) + ");"
            "try{popup.postMessage(b,'*',[b]);}catch(e){return e.name==='QuotaExceededError' && b.byteLength===" +
            std::to_string(chunk) + ";}return false;") && Pending().count == held.count && Pending().bytes == held.bytes);
        CHECK("navigation frees a full queue of transferred backing stores", w.install(*p, "https://app.test/after-budget") && empty_queue());
        CHECK("byte capacity is available immediately after navigation", evaluate(a->context,
            "let b=new ArrayBuffer(1048576);popup.postMessage(b,'*',[b]);return b.byteLength===0;") && Pending().count == 1);
        w.destroy(*p);
        CHECK("close releases the final transferred backing store", empty_queue());
    }
}

static void backing_store_release() {
    for (bool replace : {false, true}) {
        World w; auto *a = w.user(); if (!a) return;
        auto *p = w.popup(a->context); if (!p) return;
        auto released = tracked_buffer(a->context, 2u * 1024u * 1024u);
        CHECK("instrumented V8 backing store is allocated", bool(released));
        if (!released) continue;
        CHECK("instrumented buffer enters the real transfer queue", evaluate(a->context,
            "popup.postMessage(trackedBuffer,'*',[trackedBuffer]);return trackedBuffer.byteLength===0;") &&
            Pending().count == 1 && Pending().bytes >= 2u * 1024u * 1024u);
        w.destroy(*a); /* No source realm or test-held shared_ptr owns the store. */
        CHECK("source teardown keeps the accepted destination task", Pending().count == 1);
        CHECK("queued destination work prevents premature tab sleep", !js_window_can_sleep(p->id));
        if (replace) CHECK("replacement installs while the sender is already gone", w.install(*p, "https://app.test/new-document"));
        else w.destroy(*p);
        CHECK("discarded destination task releases its actual V8 backing store", empty_queue() && released->load() == 1);
        if (replace) CHECK("a dead opener does not pin its surviving popup realm", js_window_can_sleep(p->id));
    }
}

static void default_allocator_lifetime() {
    for (bool close_before_delivery : {false, true}) {
        World w; auto *a = w.user(); if (!a) return;
        auto *p = w.popup(a->context); if (!p) return;
        std::weak_ptr<v8::ArrayBuffer::Allocator> allocator = a->context->alloc;
        std::weak_ptr<v8::BackingStore> backing;
        CHECK("receiver retains a normally allocated transferred buffer", evaluate(p->context,
            "globalThis.received=null;onmessage=e=>{received=e.data;};return true;"));
        CHECK("source creates its normal default-allocator buffer", evaluate(a->context,
            "globalThis.transferBuffer=new ArrayBuffer(2097152);let v=new Uint8Array(transferBuffer);"
            "v[0]=17;v[v.length-1]=231;return true;"));
        {
            JS_ENTER(a->context);
            v8::Local<v8::Value> value;
            bool found = ctx->Global()->Get(ctx, jstr(iso, "transferBuffer")).ToLocal(&value) && value->IsArrayBuffer();
            CHECK("normal buffer exposes its actual backing-store lifetime", found);
            if (found) backing = value.As<v8::ArrayBuffer>()->GetBackingStore();
        }
        CHECK("normal buffer detaches into the directed transfer queue", evaluate(a->context,
            "popup.postMessage(transferBuffer,'*',[transferBuffer]);return transferBuffer.byteLength===0;") && Pending().count == 1);
        if (!close_before_delivery) w.pump();
        w.destroy(*a);
        CHECK("source allocator survives while another window owns its backing store", !allocator.expired() && !backing.expired());
        w.pump();
        CHECK("receiver bytes remain usable after the source isolate is gone", expression(p->context,
            "received instanceof ArrayBuffer && received.byteLength===2097152 && "
            "new Uint8Array(received)[0]===17 && new Uint8Array(received)[2097151]===231") && empty_queue());
        CHECK("receiver can drop the final transferred JavaScript reference", evaluate(p->context,
            "received=null;onmessage=null;return true;"));
        for (int i = 0; i < 3 && !backing.expired(); i++) {
            JS_ENTER(p->context);
            iso->LowMemoryNotification();
        }
        CHECK("receiver collection releases the source store and its allocator", backing.expired() && allocator.expired());
        w.destroy(*p);
        CHECK("receiver teardown leaves no transferred allocator ownership", backing.expired() && allocator.expired() && empty_queue());
    }
}

static void callback_accounting() {
    {
        World w; auto *a = w.user(); if (!a) return;
        auto *p = w.popup(a->context); if (!p) return;
        CHECK("callback-time queue ownership observer installs", expose_pending(p->context));
        CHECK("callback closes its target after observing queued successors", evaluate(p->context,
            "globalThis.delivered=0;onmessage=()=>{delivered++;globalThis.beforeClose=windowTestPending();"
            "close();globalThis.afterClose=windowTestPending();};return true;"));
        CHECK("four independent backing stores await the receiver task", evaluate(a->context,
            "for(let i=0;i<4;i++){let b=new ArrayBuffer(1048576);popup.postMessage(b,'*',[b]);}return true;") &&
            Pending().count == 4);
        js_tick(p->context);
        CHECK("handler counters include all not-yet-delivered native ownership", expression(p->context,
            "delivered===1 && beforeClose[0]===3 && beforeClose[1]>=3*1048576"));
        CHECK("handler close purges its still-pending successor tasks immediately", expression(p->context,
            "afterClose[0]===0 && afterClose[1]===0") && empty_queue());
        w.drain();
    }
    {
        World w; auto *a = w.user(); if (!a) return;
        auto *p = w.popup(a->context); if (!p) return;
        CHECK("reentrant message handler installs", evaluate(p->context,
            "globalThis.delivered=[];onmessage=e=>{delivered.push(e.data);if(e.data===0)postMessage(1,'*');};return true;"));
        CHECK("initial reentrant message is sent", evaluate(a->context, "popup.postMessage(0,'*');return true;"));
        js_tick(p->context);
        CHECK("a handler's new message waits for another native pump", expression(p->context,
            "delivered.length===1 && delivered[0]===0") && Pending().count == 1);
        js_tick(p->context);
        CHECK("the next pump delivers the deferred successor once", expression(p->context,
            "delivered.length===2 && delivered[1]===1") && empty_queue());
    }
}

static void frame_source() {
    World w; auto *a = w.user(); if (!a) return;
    Node *f = node_new_element(a->doc, "iframe", NS_HTML);
    node_append(a->doc->body, f);
    JsHost h{};
    JsCtx *child = js_new_ex(document("https://frame.test/login"), &h, a->context, f);
    CHECK("iframe has a distinct stable browsing context ID", js_window_id(child) && js_window_id(child) != a->id);
    auto *p = w.popup(child, "popup", "https://accounts.test/frame-flow"); if (!p) return;
    CHECK("open records the actual iframe caller and its native top tab", w.opened.back().source == js_window_id(child) &&
        w.opened.back().source_top == a->id && w.opened.back().opener == js_window_id(child));
    CHECK("iframe receiver setup succeeds", receiver(child));
    CHECK("OAuth popup directs its result to its actual iframe opener", evaluate(p->context,
        "opener.postMessage('frame-result','https://frame.test');return true;"));
    w.pump();
    CHECK("iframe receives the result with the correct popup source", expression(child,
        "events.length===1 && events[0].data==='frame-result' && events[0].source===popup && events[0].origin==='https://accounts.test'"));
    CHECK("queued iframe destination owns a transferred buffer", evaluate(p->context,
        "let b=new ArrayBuffer(131072);opener.postMessage(b,'*',[b]);return b.byteLength===0;") && Pending().bytes >= 131072);
    JsWindowId frame_id = js_window_id(child);
    ++f->refcount; /* Explicit fixture ownership after detachment. */
    node_remove(f); frame_kill(child);
    CHECK("frame detachment purges queued destination ownership", empty_queue() && js_window_is_closed(frame_id));
    CHECK("popup safely observes a detached iframe opener", expression(p->context, "opener.closed"));
    js_free(child);
    --f->refcount; node_free_tree(f);

    Node *sandbox = node_new_element(a->doc, "iframe", NS_HTML);
    node_set_attr(sandbox, "sandbox", "allow-scripts");
    node_append(a->doc->body, sandbox);
    JsCtx *limited = js_new_ex(document("https://frame.test/sandbox"), &h, a->context, sandbox);
    size_t before = w.opened.size(); js_window_activate(limited);
    CHECK("sandbox without allow-popups blocks even trusted popup creation", expression(limited, "window.open('about:blank')===null") && w.opened.size() == before);
}

static void frame_navigation() {
    World w; auto *a = w.user(); if (!a) return;
    Node *f = node_new_element(a->doc, "iframe", NS_HTML);
    node_append(a->doc->body, f);
    JsHost h{};
    JsCtx *old = js_new_ex(document("https://app.test/frame-before"), &h, a->context, f);
    JsWindowId id = js_window_id(old); uint32_t epoch = old->id;
    auto *p = w.popup(old, "popup", "https://accounts.test/frame-navigation"); if (!p) return;
    CHECK("popup retains its actual frame opener before navigation", evaluate(p->context,
        "globalThis.savedOpener=opener;globalThis.events=[];onmessage=e=>events.push(e);return savedOpener!==null && !savedOpener.closed;"));
    CHECK("old frame sends a callback before replacing its document", evaluate(old,
        "popup.postMessage('sent-before-navigation','https://accounts.test');return true;"));
    CHECK("old frame destination owns a pending transferred buffer", evaluate(p->context,
        "let b=new ArrayBuffer(262144);opener.postMessage(b,'*',[b]);return b.byteLength===0;") &&
        Pending().count == 2 && Pending().bytes >= 262144);

    /* This is the actual frame_load/frame_create path, not a synthetic registry
     * commit. Same-origin about:blank avoids network timing in the native gate. */
    js_frame_navigate(f, "about:blank");
    JsCtx *next = js_frame_ctx(f);
    CHECK("frame navigation installs a different live document realm", next && next != old && !next->dead &&
        next->id != epoch && old->dead && next->doc && !strcmp(next->doc->url, "about:blank"));
    if (!next || next == old || next->dead) return;
    CHECK("document navigation preserves the actual frame browsing context", js_window_id(next) == id && !js_window_is_closed(id));
    CHECK("frame commit purges only the replaced destination's native bytes", Pending().count == 1 && Pending().bytes < 262144);
    CHECK("popup opener identity remains live across frame navigation", expression(p->context,
        "opener===savedOpener && !savedOpener.closed"));
    CHECK("new frame installs its own message receiver", evaluate(next,
        "globalThis.events=[];onmessage=e=>{events.push(e);e.source.postMessage('reply-from-new-frame','https://accounts.test');};return true;"));

    /* The retired shell is still in the parent's child vector. Destroy it now
     * to prove that late teardown cannot detach the newly installed document. */
    js_free(old);
    CHECK("late old frame free cannot close the successor", js_frame_ctx(f) == next &&
        js_window_id(next) == id && !js_window_is_closed(id));
    w.pump();
    CHECK("accepted source message survives its frame document replacement", expression(p->context,
        "events.length===1 && events[0].data==='sent-before-navigation' && events[0].origin==='https://app.test' && "
        "events[0].source===savedOpener && !events[0].source.closed"));
    CHECK("replacement receiver never receives the old document's queued task", expression(next, "events.length===0"));
    CHECK("existing opener proxy targets the new frame document", evaluate(p->context,
        "savedOpener.postMessage('after-navigation','https://app.test');return true;"));
    w.pump();
    CHECK("new frame receives the callback with the actual popup source", expression(next,
        "events.length===1 && events[0].data==='after-navigation' && events[0].origin==='https://accounts.test' && !events[0].source.closed"));
    CHECK("new frame reply keeps the same stable source identity", expression(p->context,
        "events.length===2 && events[1].data==='reply-from-new-frame' && events[1].origin==='https://app.test' && events[1].source===savedOpener") &&
        empty_queue());
}

static void sandbox_script_entry() {
    for (bool allow : {false, true}) {
        Document *d = document("https://app.test/sandbox-script");
        d->csp = strdup(allow ? "sandbox allow-same-origin allow-scripts" : "sandbox allow-same-origin");
        JsHost h{}; JsCtx *c = js_new(d, &h);
        CHECK("top-level CSP sandbox reaches the native script entry state", c && c->sb_noscript == !allow);
        if (!c) { doc_free(d); continue; }
        Node *classic = node_new_element(d, "script", NS_HTML), *module = node_new_element(d, "script", NS_HTML);
        node_set_attr(module, "type", "module");
        node_append(d->body, classic); node_append(d->body, module);
        classic->flags |= NF_PARSER_INSERTED; module->flags |= NF_PARSER_INSERTED;
        const char script[] = "globalThis.classicSandboxRan=1;";
        const char mod[] = "globalThis.moduleSandboxRan=1;export const ran=1;";
        js_run_script(c, classic, script, sizeof script - 1, "https://app.test/sandbox-classic");
        js_run_module(c, module, mod, sizeof mod - 1, "https://app.test/sandbox-module");
        CHECK(allow ? "allow-scripts permits a parser classic script" : "CSP sandbox blocks a parser classic script",
            expression(c, allow ? "classicSandboxRan===1" : "typeof classicSandboxRan==='undefined'"));
        CHECK(allow ? "allow-scripts permits a parser module script" : "CSP sandbox blocks a parser module script",
            expression(c, allow ? "moduleSandboxRan===1" : "typeof moduleSandboxRan==='undefined'"));
        js_free(c); doc_free(d);
        CHECK("sandbox script fixture releases its browsing context ownership", empty_queue());
    }
}

static void coop_frame_creation() {
    struct Case { JsWindowCoop policy; bool same_origin, no_opener, keep_unsafe; };
    const Case cases[] = {
        {JS_COOP_SAME_ORIGIN, true, false, false},
        {JS_COOP_SAME_ORIGIN_PLUS_COEP, true, false, false},
        {JS_COOP_SAME_ORIGIN_ALLOW_POPUPS, true, false, true},
        {JS_COOP_NOOPENER_ALLOW_POPUPS, true, false, true},
        {JS_COOP_SAME_ORIGIN, false, true, true},
        {JS_COOP_SAME_ORIGIN_PLUS_COEP, false, true, true},
        {JS_COOP_SAME_ORIGIN_ALLOW_POPUPS, false, false, true},
        {JS_COOP_NOOPENER_ALLOW_POPUPS, false, false, true},
        {JS_COOP_UNSAFE_NONE, false, false, true},
    };
    for (const auto &test : cases) {
        World w; auto *a = w.user("https://app.test/top", test.policy); if (!a) continue;
        Node *f = node_new_element(a->doc, "iframe", NS_HTML);
        node_set_attr(f, "name", "existing-frame-context"); node_append(a->doc->body, f);
        JsHost h{}; const char *url = test.same_origin ? "https://app.test/frame" : "https://frame.test/frame";
        JsCtx *child = js_new_ex(document(url), &h, a->context, f);
        CHECK("COOP creation restriction does not change existing frame target reuse", evaluate(child,
            "return window.open('','_self')===window && window.open('','existing-frame-context')===window;") && w.opened.empty());
        js_window_activate(child);
        CHECK("frame popup applies the top policy only at new-context creation", evaluate(child,
            std::string("globalThis.framePopup=window.open('about:blank','frame-policy-popup');return framePopup") +
            (test.no_opener ? "===null;" : "!==null;")) && w.opened.size() == 1);
        if (w.opened.size() != 1) continue;
        const World::Open request = w.opened.back();
        CHECK("policy handling preserves the actual opener frame and top-tab source IDs", request.source == js_window_id(child) && request.source_top == a->id);
        CHECK("strict cross-origin frame creation severs only the requested opener", request.opener == (test.no_opener ? 0 : js_window_id(child)) &&
            (!test.no_opener || request.name.empty()));
        w.drain(); auto *p = w.find(request.id);
        CHECK("policy-filtered frame popup still materializes its initial blank", p && p->context && p->doc);
        if (!p || !p->context) continue;
        if (!test.no_opener) CHECK("initial popup principal still comes from its actual creator frame", ctx_origin(p->context) == ctx_origin(child));
        CHECK("initial frame popup exposes the expected opener relationship", expression(p->context,
            test.no_opener ? "opener===null" : "opener!==null && !opener.closed"));
        JsWindowId initial = p->id;
        bool installed = w.install(*p, test.same_origin ? "https://app.test/response" : "https://frame.test/response", JS_COOP_UNSAFE_NONE);
        if (!installed || (p->id == initial) != test.keep_unsafe)
            fprintf(stderr, "frame COOP case: top=%d same=%d noopener=%d keep_unsafe=%d\n", int(test.policy), test.same_origin, test.no_opener, test.keep_unsafe);
        CHECK("initial frame popup compares the inherited top policy with its response", installed && (p->id == initial) == test.keep_unsafe);
        if (!test.no_opener) CHECK("creator frame observes the committed policy split through its original handle", expression(child,
            test.keep_unsafe ? "!framePopup.closed" : "framePopup.closed"));
        CHECK("committed frame popup does not regain a severed opener", expression(p->context,
            test.no_opener || !test.keep_unsafe ? "opener===null" : "opener!==null && !opener.closed"));
    }
}

static void coop_policies() {
    struct HeaderCase { const char *url, *coop, *coep; JsWindowCoop expected; };
    const HeaderCase cases[] = {
        {"https://app.test/", "same-origin", nullptr, JS_COOP_SAME_ORIGIN},
        {"https://app.test/", "same-origin", "require-corp", JS_COOP_SAME_ORIGIN_PLUS_COEP},
        {"https://app.test/", "same-origin", "credentialless", JS_COOP_SAME_ORIGIN_PLUS_COEP},
        {"https://app.test/", "same-origin-allow-popups", nullptr, JS_COOP_SAME_ORIGIN_ALLOW_POPUPS},
        {"https://app.test/", "noopener-allow-popups", nullptr, JS_COOP_NOOPENER_ALLOW_POPUPS},
        {"https://app.test/", "same-origin; report-to=endpoint", nullptr, JS_COOP_SAME_ORIGIN},
        {"https://app.test/", "same-origin-evil", nullptr, JS_COOP_UNSAFE_NONE},
        {"https://app.test/", "Same-Origin", nullptr, JS_COOP_UNSAFE_NONE},
        {"https://app.test/", "same-origin, unsafe-none", nullptr, JS_COOP_UNSAFE_NONE},
        {"http://app.test/", "same-origin", nullptr, JS_COOP_UNSAFE_NONE},
        {"http://localhost/", "same-origin", nullptr, JS_COOP_SAME_ORIGIN},
        {"http://127.0.0.1/", "same-origin", nullptr, JS_COOP_SAME_ORIGIN},
        {"http://localhost.evil.test/", "same-origin", nullptr, JS_COOP_UNSAFE_NONE},
    };
    for (const auto &test : cases) {
        bool ok = js_window_coop(test.url, test.coop, test.coep) == test.expected;
        if (!ok) fprintf(stderr, "COOP case: %s | %s | %s\n", test.url, test.coop, test.coep ? test.coep : "");
        CHECK("COOP parsing recognizes exact policies only on trustworthy URLs", ok);
    }
    struct Pair { JsWindowCoop opener, response; bool same_origin, keep; };
    const Pair pairs[] = {
        {JS_COOP_UNSAFE_NONE, JS_COOP_UNSAFE_NONE, false, true},
        {JS_COOP_SAME_ORIGIN_ALLOW_POPUPS, JS_COOP_UNSAFE_NONE, false, true},
        {JS_COOP_NOOPENER_ALLOW_POPUPS, JS_COOP_UNSAFE_NONE, false, true},
        {JS_COOP_SAME_ORIGIN, JS_COOP_UNSAFE_NONE, true, false},
        {JS_COOP_SAME_ORIGIN_ALLOW_POPUPS, JS_COOP_SAME_ORIGIN_ALLOW_POPUPS, true, true},
        {JS_COOP_SAME_ORIGIN_ALLOW_POPUPS, JS_COOP_SAME_ORIGIN_ALLOW_POPUPS, false, false},
        {JS_COOP_SAME_ORIGIN, JS_COOP_SAME_ORIGIN, true, true},
        {JS_COOP_SAME_ORIGIN, JS_COOP_SAME_ORIGIN, false, false},
        {JS_COOP_SAME_ORIGIN_PLUS_COEP, JS_COOP_SAME_ORIGIN_PLUS_COEP, true, true},
        {JS_COOP_SAME_ORIGIN_PLUS_COEP, JS_COOP_SAME_ORIGIN, true, false},
        {JS_COOP_NOOPENER_ALLOW_POPUPS, JS_COOP_NOOPENER_ALLOW_POPUPS, true, false},
    };
    for (const auto &test : pairs) {
        World w; auto *a = w.user("https://app.test/start", test.opener); if (!a) continue;
        auto *p = w.popup(a->context); if (!p) continue;
        JsWindowId initial = p->id;
        bool installed = w.install(*p, test.same_origin ? "https://app.test/response" : "https://accounts.test/response", test.response);
        if (!installed || (p->id == initial) != test.keep)
            fprintf(stderr, "COOP pair: opener=%d response=%d same=%d expected_keep=%d\n",
                int(test.opener), int(test.response), test.same_origin, test.keep);
        CHECK("initial popup response applies the opener policy pair", installed && (p->id == initial) == test.keep);
        CHECK("COOP split severs both sides of the opener relationship", expression(a->context,
            test.keep ? "!popup.closed" : "popup.closed") && expression(p->context, test.keep ? "opener!==null" : "opener===null"));
        if (!test.keep) CHECK("stale policy-split proxy rejects location access safely", evaluate(a->context,
            "try{void popup.location.href;}catch(e){return e.name==='SecurityError';}return false;"));
    }
    /* The initial auxiliary SOAP -> unsafe-none exception must not be reused
     * when an already-installed ordinary page later changes policy. */
    World w; auto *a = w.user(); if (!a) return;
    JsWindowId initial = a->id;
    CHECK("ordinary same-policy navigation keeps its browsing context", w.install(*a, "https://else.test/page") && a->id == initial);
    CHECK("ordinary navigation into SOAP separates the old group", w.install(*a, "https://else.test/isolated", JS_COOP_SAME_ORIGIN_ALLOW_POPUPS) && a->id != initial);
    initial = a->id;
    CHECK("ordinary SOAP to unsafe-none does not use the popup exception", w.install(*a, "https://else.test/next") && a->id != initial);
    CHECK("ordinary navigation installs noopener-allow-popups", w.install(*a, "https://else.test/noopener", JS_COOP_NOOPENER_ALLOW_POPUPS));
    initial = a->id;
    CHECK("ordinary same-origin NAP to NAP preserves its browsing context", w.install(*a,
        "https://else.test/noopener-next", JS_COOP_NOOPENER_ALLOW_POPUPS) && a->id == initial);
    CHECK("ordinary cross-origin NAP to NAP creates a new group", w.install(*a,
        "https://different.test/noopener", JS_COOP_NOOPENER_ALLOW_POPUPS) && a->id != initial);
}

int main(int argc, char **argv) {
    (void)argc; trace_checks = getenv("LUMEN_WINDOW_TEST_TRACE") != nullptr;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    CHECK("directed-message queue starts empty", empty_queue());
    admission_and_blank();
    dynamic_blank_base();
    message_and_clone();
    oauth_source_lifetime();
    destination_lifetime();
    opaque_and_features();
    queue_budgets();
    backing_store_release();
    default_allocator_lifetime();
    callback_accounting();
    frame_source();
    frame_navigation();
    sandbox_script_entry();
    coop_frame_creation();
    coop_policies();
    CHECK("all window fixtures release queue ownership", empty_queue());
    CHECK("fixture process has no unfinished worker cleanup", js_workers_shutdown(5000));
    net_shutdown();
    printf("RESULTS window-lifecycle %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
