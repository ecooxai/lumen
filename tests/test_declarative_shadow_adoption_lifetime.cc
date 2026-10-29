/* A retired written tree adopted into an earlier-created document must have
 * exactly one disposal owner, including when the receiver is reopened.
 */
#include "../src/js/js_int.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

extern "C" void font_init(void);
extern "C" bool js_workers_shutdown(unsigned timeout_ms);
extern "C" long g_nodes_live;

static int checks, failures;
#define CHECK(label, expression) do { const bool okay_ = !!(expression); ++checks; \
    printf("%s %s\n", okay_ ? "PASS" : "FAIL", label); \
    if (!okay_) ++failures; fflush(stdout); } while (0)

static bool evaluate(JsCtx *context, const char *source) {
    if (!context || context->ctx.IsEmpty()) return false;
    JS_ENTER(context);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    const bool okay = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result);
    if (!okay && caught.HasCaught())
        fprintf(stderr, "DSD adoption lifetime: %s\n", jcstr(iso, caught.Exception()).c_str());
    return okay && result->IsTrue();
}

static const char *adoption = R"JS(
(() => {
    const assert = (value, message) => { if (!value) throw new Error(message); };
    const held = [];
    for (const disposition of ['detached', 'attached', 'reopened', 'unfinished']) {
        // Destructor order deliberately starts with B, the adoption receiver.
        const b = document.cloneNode(false);
        const a = document.cloneNode(false);
        a.write('<main><section id=host><template shadowrootmode=open><b>adoption</b></template></section></main>');
        a.close();
        const tree = a.documentElement;
        const host = a.querySelector('#host');
        const shadow = host.shadowRoot;
        const leaf = shadow.firstChild;
        assert(shadow instanceof ShadowRoot, 'declarative source root exists');
        a.open(); a.write('<p>source replacement</p>'); a.close();
        assert(tree.parentNode === null, 'source tree is retired before adoption');
        b.adoptNode(tree);
        assert(tree.ownerDocument === b && host.ownerDocument === b &&
               shadow.ownerDocument === b && leaf.ownerDocument === b,
               'adoption updates all shadow ownership');
        assert(tree.parentNode === null && shadow.host === host, 'detached adopted ownership remains intact');
        if (disposition !== 'detached') b.appendChild(tree);
        if (disposition === 'reopened' || disposition === 'unfinished') {
            b.open();
            if (disposition === 'reopened') {
                b.write('<p>receiver replacement</p>'); b.close();
                assert(b.querySelector('p').textContent === 'receiver replacement', 'receiver reopen completes');
            } else {
                b.write('<div><template shadowrootmode=open><i>receiver unfinished');
                assert(b.querySelector('div').shadowRoot.textContent === 'receiver unfinished',
                       'unfinished receiver parser owns its independent root');
            }
            assert(tree.parentNode === null, 'reopened receiver retires the adopted tree');
        }
        assert(leaf.textContent === 'adoption' && leaf.parentNode === shadow && shadow.host === host,
               'held adopted shadow subtree remains usable');
        assert(a.querySelector('p').textContent === 'source replacement', 'source replacement is separate');
        held.push({a, b, tree, host, shadow, leaf, disposition});
    }
    globalThis.__dsdAdoptionLifetimeHeld = held;
    return held.length === 4;
})()
)JS";

int main(int argc, char **argv) {
    (void)argc;
    constexpr int cycles = 8;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    const long baseline = g_nodes_live;
    const double started = now_ms();
    for (int cycle = 0; cycle < cycles; ++cycle) {
        Document *document = doc_new("https://adoption-lifetime.invalid/");
        const char *html = "<!doctype html><html><head></head><body></body></html>";
        CHECK("the adoption control has a native HTML document", html_parse_bytes(
            document, html, strlen(html), "text/html; charset=utf-8"));
        JsHost host{};
        JsCtx *context = js_new(document, &host);
        const bool ready = context && !context->ctx.IsEmpty();
        CHECK("the adoption control has a real V8 realm", ready);
        CHECK("all four earlier-owner adoption dispositions preserve live wrappers",
              ready && evaluate(context, adoption));
        const long live = g_nodes_live;
        js_release_pins();
        if (context) js_free(context);
        doc_free(document);
        CHECK("adopted retired roots are disposed once with no native node residue", g_nodes_live == baseline);
        printf("DSD_ADOPTION_CYCLE {\"cycle\":%d,\"baselineNodes\":%ld,\"beforeTeardown\":%ld,\"afterTeardown\":%ld}\n",
               cycle, baseline, live, g_nodes_live);
        fflush(stdout);
    }
    const double drain_deadline = now_ms() + 3000;
    while (net_pending() && now_ms() < drain_deadline) {
        net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool network_drained = net_pending() == 0;
    CHECK("adoption control leaves no native network request", network_drained);
    const bool workers_drained = js_workers_shutdown(5000);
    CHECK("adoption control leaves no worker ownership", workers_drained);
    printf("DSD_ADOPTION_RESULTS {\"cycles\":%d,\"dispositionsPerCycle\":4,\"checks\":%d,\"passed\":%d,"
           "\"failed\":%d,\"baselineNodes\":%ld,\"finalNodes\":%ld,\"durationMs\":%.3f}\n",
           cycles, checks, checks - failures, failures, baseline, g_nodes_live, now_ms() - started);
    fflush(stdout);
    if (!network_drained || !workers_drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
