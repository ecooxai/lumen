/* Real DOM removing steps, frame associations and retained native owners.
 * These custom regressions do not alter upstream WPT or its denominator. */
#include "../src/js/js_int.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
extern "C" void font_init(void);
extern "C" bool js_workers_shutdown(unsigned timeout_ms);

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
        fprintf(stderr, "frame removal regression: %s\n", jcstr(iso, caught.Exception()).c_str());
    return okay && result->IsTrue();
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Document *document = doc_new("https://frame-removal.example.test/parent");
    const char *html = "<!doctype html><html><head></head><body></body></html>";
    html_parse(document, html, strlen(html));
    JsHost host{};
    JsCtx *context = js_new(document, &host);
    const bool ready = context && !context->ctx.IsEmpty();
    CHECK("the real parent document and V8 realm initialize", ready);
    if (ready) {
        CHECK("connected srcdoc frame exposes a real child realm", evaluate(context, R"JS(
            globalThis.__removal = {};
            __removal.make = function(parent) {
                const frame = parent.ownerDocument.createElement('iframe');
                frame.srcdoc = '<!doctype html><a id="value" href="?saved">retained</a>';
                parent.appendChild(frame);
                return frame;
            };
            __removal.single = __removal.make(document.body);
            __removal.oldWindow = __removal.single.contentWindow;
            __removal.oldDocument = __removal.single.contentDocument;
            __removal.oldAnchor = __removal.oldDocument.getElementById('value');
            __removal.oldWindow.__previousRealm = true;
            !!__removal.oldWindow && !!__removal.oldAnchor;
        )JS"));
        CHECK("remove immediately clears the iframe contentWindow", evaluate(context,
            "__removal.single.remove();__removal.single.contentWindow===null"));
        CHECK("remove immediately clears the iframe contentDocument", evaluate(context,
            "__removal.single.contentDocument===null"));
        CHECK("a removed child Window reports closed", evaluate(context,
            "__removal.oldWindow.closed===true"));
        CHECK("retained child document and element remain usable after removal", evaluate(context,
            "__removal.oldDocument.getElementById('value')===__removal.oldAnchor&&"
            "__removal.oldAnchor.textContent==='retained'&&__removal.oldAnchor.search==='?saved'"));
        CHECK("reinsertion creates a fresh child Window and Document", evaluate(context, R"JS(
            document.body.appendChild(__removal.single);
            __removal.single.contentWindow!==__removal.oldWindow &&
            __removal.single.contentDocument!==__removal.oldDocument &&
            __removal.single.contentWindow.__previousRealm===undefined;
        )JS"));
        CHECK("same-turn removal and reinsertion cannot revive the previous realm", evaluate(context, R"JS(
            __removal.reinserted = __removal.single.contentWindow;
            __removal.single.remove();document.body.appendChild(__removal.single);
            __removal.single.contentWindow!==__removal.reinserted &&
            __removal.reinserted.closed===true;
        )JS"));
        CHECK("removing a reinserted frame clears its new association", evaluate(context,
            "__removal.single.remove();__removal.single.contentWindow===null&&"
            "__removal.single.contentDocument===null"));
        CHECK("an ancestor contains two independent live child frames", evaluate(context, R"JS(
            __removal.container=document.createElement('section');
            document.body.appendChild(__removal.container);
            __removal.left=__removal.make(__removal.container);
            __removal.right=__removal.make(__removal.container);
            __removal.leftWindow=__removal.left.contentWindow;
            __removal.rightWindow=__removal.right.contentWindow;
            !!__removal.leftWindow && !!__removal.rightWindow &&
            __removal.leftWindow!==__removal.rightWindow;
        )JS"));
        CHECK("ancestor removal clears both descendant iframe associations synchronously", evaluate(context,
            "__removal.container.remove();__removal.left.contentWindow===null&&"
            "__removal.right.contentDocument===null&&__removal.leftWindow.closed&&__removal.rightWindow.closed"));
        CHECK("ancestor reinsertion creates fresh descendant browsing contexts", evaluate(context,
            "document.body.appendChild(__removal.container);"
            "__removal.left.contentWindow!==__removal.leftWindow&&"
            "__removal.right.contentWindow!==__removal.rightWindow&&"
            "__removal.left.contentWindow!==null&&__removal.right.contentWindow!==null"));
        CHECK("textContent replacement invokes descendant frame removing steps", evaluate(context,
            "__removal.container.textContent='replacement';"
            "__removal.left.contentWindow===null&&__removal.right.contentDocument===null"));
        CHECK("a connected shadow subtree can own a live iframe", evaluate(context, R"JS(
            __removal.shadowHost=document.createElement('div');
            document.body.appendChild(__removal.shadowHost);
            __removal.shadowRoot=__removal.shadowHost.attachShadow({mode:'open'});
            __removal.shadowFrame=__removal.make(__removal.shadowRoot);
            __removal.shadowWindow=__removal.shadowFrame.contentWindow;
            !!__removal.shadowWindow;
        )JS"));
        CHECK("shadow-host removal synchronously retires its child iframe", evaluate(context,
            "__removal.shadowHost.remove();__removal.shadowFrame.contentWindow===null&&"
            "__removal.shadowWindow.closed===true"));
        CHECK("shadow-host reinsertion starts a fresh child realm", evaluate(context,
            "document.body.appendChild(__removal.shadowHost);"
            "__removal.shadowFrame.contentWindow!==null&&"
            "__removal.shadowFrame.contentWindow!==__removal.shadowWindow"));
        CHECK("moving a connected iframe within one document replaces its child realm", evaluate(context, R"JS(
            __removal.move=__removal.make(document.body);
            __removal.moveWindow=__removal.move.contentWindow;
            __removal.container.appendChild(__removal.move);
            __removal.move.contentWindow!==__removal.moveWindow &&
            __removal.moveWindow.closed===true;
        )JS"));
        CHECK("cross-document insertion retires the old document association", evaluate(context, R"JS(
            __removal.target=__removal.make(document.body);
            __removal.crossOld=__removal.move.contentWindow;
            __removal.target.contentDocument.body.appendChild(__removal.move);
            __removal.move.contentWindow!==null &&
            __removal.move.contentWindow!==__removal.crossOld &&
            __removal.crossOld.closed===true &&
            __removal.move.ownerDocument===__removal.target.contentDocument;
        )JS"));
        CHECK("retiring a parent browsing context also retires its nested child", evaluate(context,
            "__removal.crossNew=__removal.move.contentWindow;__removal.target.remove();"
            "__removal.move.contentWindow===null&&__removal.crossNew.closed===true"));
        CHECK("a never-connected iframe does not acquire a browsing context", evaluate(context,
            "__removal.detached=document.createElement('iframe');"
            "__removal.detached.srcdoc='<p>detached</p>';"
            "__removal.detached.contentWindow===null&&__removal.detached.contentDocument===null"));
        CHECK("repeated removal preserves closed associations without creating replacements", evaluate(context,
            "__removal.single.remove();__removal.target.remove();document.body.textContent='';"
            "__removal.single.contentWindow===null&&__removal.target.contentWindow===null&&window.length===0"));
        bool children_drained = true;
        for (JsCtx *child : context->kids) if (!child->dead) children_drained = false;
        CHECK("all native child contexts are inactive before any later host tick", children_drained);
    }
    js_release_pins();
    if (context) js_free(context);
    doc_free(document);
    const double deadline = now_ms() + 3000;
    while (net_pending() && now_ms() < deadline) {
        net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool network_drained = net_pending() == 0;
    CHECK("frame removal leaves no native network requests pending", network_drained);
    const bool workers_drained = js_workers_shutdown(5000);
    CHECK("frame removal leaves native worker ownership drained", workers_drained);
    printf("RESULTS frame-removal %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    fflush(stdout);
    if (!network_drained || !workers_drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
