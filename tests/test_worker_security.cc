/* Public secure-context inheritance checks in real dedicated worker isolates.
 * The worker reports its own observable state over an actual two-way message
 * exchange. No test result or expected security value is passed to the worker.
 */
#include "../src/js/js_int.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

extern "C" void font_init(void);
extern "C" bool js_workers_shutdown(unsigned timeout_ms);

static int checks, failures;
static void check(const char *name, bool passed) {
    ++checks;
    if (!passed) ++failures;
    printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    fflush(stdout);
}

static bool value(JsCtx *context, const char *source) {
    if (!context || context->ctx.IsEmpty()) return false;
    JS_ENTER(context);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    bool okay = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result);
    if (!okay && caught.HasCaught())
        fprintf(stderr, "worker-security script: %s\n", jcstr(iso, caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint();
    return okay && result->IsTrue();
}

struct Fixture {
    Document *document = nullptr;
    JsCtx *context = nullptr;
    explicit Fixture(const char *url) {
        document = doc_new(url);
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{};
        context = js_new(document, &host);
    }
    JsCtx *frame(const char *url) {
        if (!context) return nullptr;
        Node *element = node_new_element(document, "iframe", NS_HTML);
        node_set_attr(element, "src", url);
        node_append(document->body, element);
        Document *child = doc_new(url);
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(child, html, strlen(html));
        JsHost host{};
        return js_new_ex(child, &host, context, element);
    }
    ~Fixture() {
        if (context) js_free(context);
        if (document) doc_free(document);
    }
};

struct Case {
    const char *name;
    const char *top_url;
    const char *child_url;
    bool blob;
    bool secure;
    bool spoof_before;
    const char *blob_origin;
};

static void exercise(const Case &test) {
    printf("CASE worker-security %s\n", test.name);
    Fixture fixture(test.top_url);
    JsCtx *owner = test.child_url ? fixture.frame(test.child_url) : fixture.context;
    const bool initialized = owner && !owner->ctx.IsEmpty();
    std::string label = std::string(test.name) + ": creator has the expected native security state";
    check(label.c_str(), initialized && js_windows_is_secure(owner) == test.secure);

    std::string script = "(() => {\n";
    script += "globalThis.__securityRows = []; globalThis.__securityError = '';\n";
    script += "const fail = message => { __securityError += String(message); document.documentElement.setAttribute('data-worker-security-done', 'error'); };\n";
    if (test.spoof_before) {
        script += "Object.defineProperty(globalThis, 'isSecureContext', {value: ";
        script += test.secure ? "false" : "true";
        script += ", configurable: true, writable: true});\n";
    }
    script += R"JS(
const source = `
function report(kind) {
    postMessage({kind, secure: isSecureContext, origin: self.origin,
        locationOrigin: location.origin, protocol: location.protocol});
}
onmessage = event => {
    if (event.data === 'security-ping') { report('pong'); close(); }
};
report('ready');`;
)JS";
    script += "const url = ";
    script += test.blob ? "URL.createObjectURL(new Blob([source], {type: 'text/javascript'}));\n" :
        "'data:text/javascript,' + encodeURIComponent(source);\n";
    script += R"JS(
globalThis.__securityURL = url;
const worker = globalThis.__securityWorker = new Worker(url);
worker.onerror = event => { event.preventDefault(); fail('worker error: ' + event.message); };
worker.onmessageerror = () => fail('worker messageerror');
worker.onmessage = event => {
    const row = event.data;
    const wanted = __securityRows.length === 0 ? 'ready' : 'pong';
    if (__securityRows.length >= 2 || !row || typeof row !== 'object' || row.kind !== wanted) {
        fail('unexpected, malformed or duplicate worker message'); return;
    }
    __securityRows.push(row);
    if (row.kind === 'ready') {
)JS";
    // Mutating the public creator property cannot change a worker's settings.
    script += "Object.defineProperty(globalThis, 'isSecureContext', {value: ";
    script += test.secure ? "false" : "true";
    script += ", configurable: true, writable: true});\n";
    script += R"JS(
        worker.postMessage('security-ping');
    } else document.documentElement.setAttribute('data-worker-security-done', 'true');
};
return true;
})()
)JS";
    const bool started = initialized && value(owner, script.c_str());
    label = std::string(test.name) + ": real dedicated worker is constructed";
    check(label.c_str(), started);

    bool complete = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (started && std::chrono::steady_clock::now() < deadline) {
        js_tick(owner);
        const char *done = node_attr(owner->doc->html, "data-worker-security-done");
        if (done) { complete = !strcmp(done, "true"); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool protocol_ok = complete && value(owner,
        "__securityError === '' && __securityRows.length === 2 && "
        "__securityRows[0].kind === 'ready' && __securityRows[1].kind === 'pong'");
    label = std::string(test.name) + ": exactly two real messages complete without errors";
    check(label.c_str(), protocol_ok);

    const std::string security_check = "__securityRows.every(row => typeof row.secure === 'boolean' && row.secure === " +
        std::string(test.secure ? "true" : "false") + ")";
    label = std::string(test.name) + ": worker retains the native creator security decision";
    check(label.c_str(), protocol_ok && value(owner, security_check.c_str()));

    const std::string wanted_origin = test.blob ? test.blob_origin : "null";
    const std::string origin_check = "__securityRows.every(row => row.origin === '" + wanted_origin +
        "' && row.locationOrigin === '" + wanted_origin + "' && row.protocol === '" +
        (test.blob ? "blob:" : "data:") + "')";
    label = std::string(test.name) + ": security inheritance does not replace the URL origin";
    check(label.c_str(), protocol_ok && value(owner, origin_check.c_str()));

    if (initialized) {
        value(owner,
            "(() => { if (globalThis.__securityError) console.error(__securityError); "
            "if (globalThis.__securityWorker) __securityWorker.terminate(); "
            "if (globalThis.__securityURL && __securityURL.startsWith('blob:')) URL.revokeObjectURL(__securityURL); "
            "return true; })()");
    }
    // Fixture destruction tears down its actual owner and children even on RED.
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    const Case cases[] = {
        {"HTTPS data", "https://security.test/start", nullptr, false, true, false, nullptr},
        {"HTTP data", "http://untrusted.test/start", nullptr, false, false, false, nullptr},
        {"loopback data", "http://127.0.0.1:8765/start", nullptr, false, true, false, nullptr},
        {"HTTPS blob", "https://security.test/start", nullptr, true, true, false, "https://security.test"},
        {"HTTP blob", "http://untrusted.test/start", nullptr, true, false, false, "http://untrusted.test"},
        {"insecure ancestor", "http://untrusted.test/start", "https://security.test/child", false, false, false, nullptr},
        {"secure inherited blank", "https://security.test/start", "about:blank", false, true, false, nullptr},
        {"insecure inherited blank", "http://untrusted.test/start", "about:blank", false, false, false, nullptr},
        {"HTTPS public property spoof", "https://security.test/start", nullptr, false, true, true, nullptr},
        {"HTTP public property spoof", "http://untrusted.test/start", nullptr, false, false, true, nullptr},
    };
    for (const auto &test : cases) exercise(test);
    const bool drained = js_workers_shutdown(5000);
    check("all worker threads drain before network subsystem teardown", drained);
    printf("RESULTS worker-security %s (%d/%d checks)\n",
           failures ? "FAIL" : "PASS", checks - failures, checks);
    fflush(stdout);
    if (!drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
