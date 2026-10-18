/* Native retention checks for the deferred console-reporting queue. The HTML
 * fixture separately verifies public promise behavior and exact diagnostics. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>

extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); ++checks; \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) ++failures; \
} while (0)

/* Run without the host's checkpoint so queue retention is observable. */
static bool raw(JsCtx *c, const char *source) {
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> value;
    return v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) && script->Run(ctx).ToLocal(&value);
}

struct WeakPromise {
    v8::Global<v8::Promise> handle;
    bool collected = false;
};

static void weak_promise(const v8::WeakCallbackInfo<WeakPromise> &info) {
    auto *tracked = info.GetParameter();
    tracked->collected = true;
    tracked->handle.Reset();
}

static bool track_rejection(JsCtx *c, WeakPromise *tracked) {
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> value;
    if (!v8::Script::Compile(ctx, jstr(iso, "Promise.reject(new Uint8Array(32 * 1024 * 1024))")).ToLocal(&script) ||
        !script->Run(ctx).ToLocal(&value) || !value->IsPromise()) return false;
    tracked->handle.Reset(iso, value.As<v8::Promise>());
    tracked->handle.SetWeak(tracked, weak_promise, v8::WeakCallbackType::kParameter);
    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    g_log_level = 1;
    Document *d = doc_new("https://promise.example/");
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host{};
    JsCtx *c = js_new(d, &host);

    CHECK("rejected burst evaluates", raw(c,
        "globalThis.batch = []; for (let i = 0; i < 256; i++) batch.push(Promise.reject(new Uint8Array(4096)));"));
    CHECK("pending promises wait for a checkpoint", c->rejections.size() == 256);
    CHECK("handlers can attach before the checkpoint", raw(c, "for (const p of batch) p.catch(() => {}); batch = null;"));
    CHECK("handler-added cancellation immediately releases native entries", c->rejections.empty());
    js_tick(c);
    CHECK("canceled burst leaves no reporting work", c->rejections.empty() && !c->rejections_pending);

    WeakPromise tracked;
    CHECK("large genuine rejection evaluates", track_rejection(c, &tracked));
    CHECK("genuine failure stays pending before the checkpoint", c->rejections.size() == 1);
    js_tick(c);
    CHECK("reporting releases all native promise entries", c->rejections.empty() && !c->rejections_pending);
    {
        v8::Isolate::Scope scope(c->iso);
        c->iso->LowMemoryNotification();
        c->iso->LowMemoryNotification();
    }
    CHECK("reported promise and its 32 MiB reason can be collected", tracked.collected && tracked.handle.IsEmpty());
    tracked.handle.Reset();

    Node *frame = node_new_element(d, "iframe", NS_HTML);
    ++frame->refcount; /* The fixture owns this node after detaching it. */
    node_append(d->body, frame);
    Document *child_doc = doc_new("https://promise.example/child");
    html_parse(child_doc, html, strlen(html));
    JsCtx *child = js_new_ex(child_doc, &host, c, frame);
    CHECK("child rejection evaluates", raw(child, "globalThis.pending = Promise.reject('removed-frame');"));
    CHECK("child promise belongs to its own reporting queue", child->rejections.size() == 1 && c->rejections.empty());
    // Model a callback into the child while the parent still has an active
    // script task. The child's checkpoint must leave time for that task's catch.
    ++c->depth;
    js_eval(child, "0", "test:active-parent");
    CHECK("active parent task defers child reporting", child->rejections.size() == 1);
    --c->depth;
    node_remove(frame);
    frame_kill(child);
    CHECK("detaching a frame releases pending reports", child->rejections.empty());
    js_tick(c);
    CHECK("detached frame leaves no reporting work", !c->rejections_pending);

    // Also leave a queue populated at teardown; js_free must reset every V8
    // handle before disposing the isolate.
    CHECK("teardown rejection evaluates", raw(c, "Promise.reject('page-teardown');"));
    CHECK("teardown exercises a nonempty queue", c->rejections.size() == 1);
    js_free(c); --frame->refcount; node_free_tree(frame); doc_free(d); net_shutdown();
    printf("RESULTS promise-rejections %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
