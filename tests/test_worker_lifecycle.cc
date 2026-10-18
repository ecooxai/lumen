/* Native resource regression for self-closing workers. Link like jsrun while
 * replacing src/js/worker.cc with this file. Including the implementation lets
 * the test check an undeliverable message queue directly instead of trusting a
 * noisy process-RSS threshold or adding a production-only diagnostics API.
 */
#include "../src/js/worker.cc"
#include <thread>

extern "C" void font_init(void);

static int failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) failures++; \
} while (0)

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Document *d = doc_new("file:///worker-lifecycle.html");
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host = {};
    JsCtx *c = js_new(d, &host);
    js_eval(c,
        "globalThis.received = []; globalThis.closedWorker = new Worker(URL.createObjectURL(new Blob(["
        "\"postMessage('before-close'); close(); postMessage('after-close');\""
        "], {type:'text/javascript'}))); closedWorker.onmessage = e => received.push(e.data);",
        "test:worker-close");
    CHECK("worker is registered", g_workers.size() == 1);
    if (g_workers.size() != 1) return 1;
    auto w = g_workers.begin()->second;

    bool stopped = false;
    double end = now_ms() + 5000;
    while (now_ms() < end) {
        { std::lock_guard<std::mutex> lk(w->mu); stopped = w->iso == nullptr && w->out.size() == 2; }
        if (stopped) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK("close preserves messages from the current task", stopped);

    js_eval(c,
        "globalThis.closePayload = new Uint8Array(1024*1024);"
        "for(let i=0;i<32;i++) closedWorker.postMessage(closePayload);",
        "test:post-after-close");
    size_t queued = 0;
    { std::lock_guard<std::mutex> lk(w->mu); queued = w->in.size(); }
    CHECK("closed worker discards incoming messages", queued == 0);
    if (queued) fprintf(stderr, "closed worker retained %zu one-MiB messages\n", queued);
    js_tick(c);
    js_eval(c,
        "document.documentElement.setAttribute('data-events', received.join(','));"
        "closedWorker.terminate(); closePayload = null;",
        "test:worker-close-events");
    const char *events = node_attr(d->html, "data-events");
    CHECK("both current-task messages reach the owner", events && !strcmp(events, "before-close,after-close"));
    js_eval(c,
        "globalThis.getterSeen = false;"
        "const duringClone = new Worker(URL.createObjectURL(new Blob(['onmessage = () => {};'])));"
        "duringClone.postMessage({get value() { getterSeen = true; duringClone.terminate(); return 1; }});"
        "document.documentElement.setAttribute('data-getter', String(getterSeen));",
        "test:terminate-during-serialization");
    const char *getter = node_attr(d->html, "data-getter");
    CHECK("a clone getter can terminate its worker safely", getter && !strcmp(getter, "true"));

    auto terminated = std::make_shared<Worker>();
    WEnv env; env.w = terminated;
    worker_terminate(terminated.get());
    Buf late; late.p = static_cast<uint8_t *>(malloc(128)); late.n = 128;
    post_out(&env, OutMsg{ WM_MESSAGE, late, {}, {}, 0, 0 });
    CHECK("terminate discards racing outgoing messages", terminated->out.empty());
    /* Keep the regression itself leak-free even when run against the old code. */
    for (OutMsg &m : terminated->out) free(m.data.p);
    terminated->out.clear();
    for (Buf &b : w->in) free(b.p);
    w->in.clear();
    js_free(c); doc_free(d); net_shutdown();
    printf("RESULTS worker-lifecycle %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
