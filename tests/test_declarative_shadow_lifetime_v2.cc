/* Version 2: detached-writer ownership with initial network shadow parsing disabled.
 * These are project regression checks, separate from unchanged upstream WPT.
 */
#include "../src/js/js_int.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

extern "C" void font_init(void);
extern "C" bool js_workers_shutdown(unsigned timeout_ms);
extern "C" long g_nodes_live;

static int checks, failures;
#define CHECK(label, expression) do { const bool okay_ = !!(expression); ++checks; \
    printf("%s %s\n", okay_ ? "PASS" : "FAIL", label); \
    if (!okay_) ++failures; fflush(stdout); } while (0)

static double current_rss_mib() {
#if defined(__APPLE__)
    mach_task_basic_info data{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&data), &count) == KERN_SUCCESS)
        return static_cast<double>(data.resident_size) / (1024.0 * 1024.0);
#endif
    return -1;
}

static bool evaluate(JsCtx *context, const char *source) {
    if (!context || context->ctx.IsEmpty()) return false;
    JS_ENTER(context);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    const bool okay = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result);
    if (!okay && caught.HasCaught())
        fprintf(stderr, "DSD lifetime driver: %s\n", jcstr(iso, caught.Exception()).c_str());
    return okay && result->IsTrue();
}

static const char *repeated_writes = R"JS(
(() => {
    const assert = (value, message) => { if (!value) throw new Error(message); };
    const networkHost = document.querySelector('#network');
    assert(networkHost.shadowRoot === null, 'initial network shadow attachment remains disabled');
    assert(networkHost.querySelector('template').content.textContent === 'network',
           'initial network template remains ordinary and intact');
    const d = document.cloneNode(false);
    const retained = [];
    for (let round = 0; round < 12; round++) {
        d.open();
        d.write('<!doctype html><div id="host"><template shadowrootmode="op');
        d.write('en"><span>round-' + round + '</span></template><i>light</i></div>');
        d.close();
        const oldRoot = d.documentElement;
        const oldHost = d.querySelector('#host');
        const oldParent = oldHost.parentNode;
        const oldShadow = oldHost.shadowRoot;
        assert(oldShadow instanceof ShadowRoot, 'detached root brand');
        assert(oldShadow.textContent === 'round-' + round, 'split write content');
        const oldChild = oldShadow.firstChild;
        retained.push({oldRoot, oldHost, oldParent, oldShadow, oldChild, round});

        d.open();
        d.write('<p>replacement-' + round + '</p>');
        d.close();
        assert(oldRoot.parentNode === null, 'open detaches old document element');
        assert(oldRoot.ownerDocument === d, 'open preserves old owner document');
        assert(oldHost.parentNode === oldParent, 'open preserves detached descendant parent');
        assert(oldRoot.contains(oldHost), 'open preserves detached subtree ownership');
        assert(oldShadow.host === oldHost, 'open preserves retained shadow host');
        assert(oldChild.parentNode === oldShadow, 'open preserves retained shadow subtree');
        assert(!oldHost.isConnected && !oldChild.isConnected, 'old subtree is disconnected');
        assert(d.querySelector('p').textContent === 'replacement-' + round,
               'replacement belongs to receiver document');
    }
    for (const row of retained) {
        assert(row.oldChild.textContent === 'round-' + row.round, 'held node remains readable');
        assert(row.oldShadow.ownerDocument === d, 'held shadow root keeps owner document');
        assert(row.oldHost.shadowRoot === row.oldShadow, 'native root identity survives reopen');
    }
    globalThis.__dsdLifetimeRetained = retained;
    globalThis.__dsdLifetimeDocument = d;
    return true;
})()
)JS";

static const char *unfinished_writes = R"JS(
(() => {
    const assert = (value, message) => { if (!value) throw new Error(message); };
    const a = document.cloneNode(false), b = document.cloneNode(false);
    a.write('<section><template shadowrootmode=open><b>A');
    b.write('<section><template shadowrootmode=open><b>B');
    a.write('1'); b.write('2');
    assert(a.querySelector('section').shadowRoot.textContent === 'A1', 'first independent parser');
    assert(b.querySelector('section').shadowRoot.textContent === 'B2', 'second independent parser');
    const moving = document.cloneNode(false);
    moving.write('<section id=move><template shadowrootmode=open><b>before');
    const removed = moving.querySelector('#move');
    const removedRoot = removed.shadowRoot;
    removed.remove();
    moving.write('after</b></template></section><p>tail</p>');
    moving.close();
    assert(removedRoot.textContent === 'beforeafter', 'parser retains detached insertion target');
    assert(removedRoot.host === removed && !removed.isConnected, 'removed shadow host lifetime');
    assert(moving.querySelector('p').textContent === 'tail', 'parser resumes outside removed host');
    const buffered = document.cloneNode(false);
    buffered.write('<di');
    const reopened = document.cloneNode(false);
    reopened.write('<article><template shadowrootmode=open><b>unfinished');
    reopened.open();
    reopened.write('<p>recovered</p>');
    reopened.close();
    assert(reopened.querySelector('p').textContent === 'recovered', 'open disposes unfinished parser');
    globalThis.__dsdLifetimeUnfinished = [a, b, buffered, moving, removed, removedRoot, reopened];
    return true;
})()
)JS";

int main(int argc, char **argv) {
    (void)argc;
    constexpr int cycles = 24;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    const long baseline = g_nodes_live;
    const double started = now_ms();
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const double cycle_started = now_ms();
        const std::string url = "https://lifetime.invalid/cycle-" + std::to_string(cycle);
        Document *document = doc_new(url.c_str());
        const char *html = "<!doctype html><html><head></head><body><div id=network>"
            "<template shadowrootmode=open>network</template></div></body></html>";
        CHECK("native HTML byte document parses", html_parse_bytes(
            document, html, strlen(html), "text/html; charset=utf-8"));
        JsHost host{};
        JsCtx *context = js_new(document, &host);
        const bool ready = context && !context->ctx.IsEmpty();
        CHECK("a fresh real V8 realm initializes", ready);
        CHECK("repeated open and split writes preserve held subtree wrappers",
              ready && evaluate(context, repeated_writes));
        CHECK("interleaved unfinished parsers retain their own receiver and insertion targets",
              ready && evaluate(context, unfinished_writes));
        const long peak_nodes = g_nodes_live;
        const double live_rss = current_rss_mib();
        js_release_pins();
        if (context) js_free(context);
        doc_free(document);
        CHECK("all native nodes return to the exact pre-realm baseline", g_nodes_live == baseline);
        printf("DSD_LIFETIME_CYCLE {\"cycle\":%d,\"baselineNodes\":%ld,\"liveNodesBeforeTeardown\":%ld,"
               "\"liveNodesAfterTeardown\":%ld,\"rssBeforeTeardownMiB\":%.6f,"
               "\"rssAfterTeardownMiB\":%.6f,\"durationMs\":%.3f}\n",
               cycle, baseline, peak_nodes, g_nodes_live, live_rss,
               current_rss_mib(), now_ms() - cycle_started);
        fflush(stdout);
    }
    const double drain_deadline = now_ms() + 3000;
    while (net_pending() && now_ms() < drain_deadline) {
        net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool network_drained = net_pending() == 0;
    CHECK("no native network requests remain at teardown", network_drained);
    const bool workers_drained = js_workers_shutdown(5000);
    CHECK("no worker ownership remains at teardown", workers_drained);
    printf("DSD_LIFETIME_RESULTS {\"cycles\":%d,\"checks\":%d,\"passed\":%d,\"failed\":%d,"
           "\"baselineNodes\":%ld,\"finalNodes\":%ld,\"durationMs\":%.3f}\n",
           cycles, checks, checks - failures, failures, baseline, g_nodes_live, now_ms() - started);
    fflush(stdout);
    if (!network_drained || !workers_drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
