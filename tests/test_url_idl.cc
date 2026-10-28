/* Public Web IDL, private receiver and real child/worker realm regressions.
 * Including worker.cc exposes its existing finite thread-drain check without
 * adding a production-only diagnostic API or emulating browser operations.
 */
#include "../src/js/worker.cc"
#include <fstream>
#include <iterator>
#include <set>
#include <thread>

extern "C" void font_init(void);
static int checks, failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); ++checks; \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) ++failures; \
} while (0)

static bool read_source(const char *path, std::string &source) {
    std::ifstream input(path, std::ios::binary);
    source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad() && !source.empty() && source.size() < 65536;
}

struct Fixture {
    Document *document;
    JsCtx *context;
    Fixture() {
        document = doc_new("https://idl.test/start");
        const char *html = "<!doctype html><html><body><iframe id='idlChild'></iframe></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{};
        context = js_new(document, &host);
    }
    ~Fixture() { js_free(context); doc_free(document); }
};

static bool result_rows(JsCtx *c, const char *expression, size_t expected) {
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> value;
    bool evaluated = v8::Script::Compile(ctx, jstr(iso, expression)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&value);
    CHECK("IDL regression returns a complete result array", evaluated && value->IsArray() && value.As<v8::Array>()->Length() == expected);
    if (!evaluated || !value->IsArray() || value.As<v8::Array>()->Length() != expected) {
        if (caught.HasCaught()) fprintf(stderr, "IDL driver: %s\n", jcstr(iso, caught.Exception()).c_str());
        return false;
    }
    auto rows = value.As<v8::Array>();
    std::set<std::string> names;
    bool valid = true;
    for (uint32_t index = 0; index < rows->Length(); ++index) {
        v8::Local<v8::Value> row, name, status, message;
        bool shape = rows->Get(ctx, index).ToLocal(&row) && row->IsObject();
        if (shape) {
            auto object = row.As<v8::Object>();
            shape = object->Get(ctx, jstr(iso, "name")).ToLocal(&name) && name->IsString() &&
                object->Get(ctx, jstr(iso, "status")).ToLocal(&status) && status->IsString();
        }
        const std::string label = shape ? jcstr(iso, name) : "malformed IDL regression row";
        const std::string outcome = shape ? jcstr(iso, status) : "";
        shape = shape && !label.empty() && names.insert(label).second && (outcome == "PASS" || outcome == "FAIL");
        valid = valid && shape;
        CHECK(label.c_str(), shape && outcome == "PASS");
        if (shape && outcome != "PASS" && row.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
            fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
    }
    CHECK("IDL result rows have unique names and explicit statuses", valid && names.size() == expected);
    return valid;
}

static void check_window_idl() {
    std::string common, cross_realm;
    const bool source_ok = read_source("tests/js/url-idl-regression.js", common) &&
        read_source("tests/js/url-idl-cross-realm-regression.js", cross_realm);
    CHECK("window IDL and cross-realm sources are present and bounded", source_ok);
    if (!source_ok) return;
    Fixture fixture;
    CHECK("actual parsed-iframe IDL fixture initializes", fixture.context && !fixture.context->ctx.IsEmpty());
    if (!fixture.context || fixture.context->ctx.IsEmpty()) return;
    js_eval(fixture.context, common.c_str(), "test:window-url-idl-source");
    result_rows(fixture.context, "runURLIDLRegression()", 83);
    js_eval(fixture.context, cross_realm.c_str(), "test:window-url-idl-cross-realm-source");
    result_rows(fixture.context, "runURLIDLRealmRegression(document.getElementById('idlChild').contentWindow)", 17);
    js_eval(fixture.context, R"JS(
(() => {
    const frame = document.getElementById('idlChild'), child = frame.contentWindow, Interface = child.URL;
    const instance = new Interface('https://detached.test/?before=one');
    globalThis.__urlIDLDetachedCallbacks = {timer: 0, raf: 0};
    child.setTimeout(() => __urlIDLDetachedCallbacks.timer++, 0);
    child.requestAnimationFrame(() => __urlIDLDetachedCallbacks.raf++);
    globalThis.__urlIDLDetached = { Interface, instance, params: instance.searchParams, frame,
        document: child.document, createElement: child.document.createElement,
        prototype: Interface.prototype,
        href: Object.getOwnPropertyDescriptor(Interface.prototype, 'href'),
        pathname: Object.getOwnPropertyDescriptor(Interface.prototype, 'pathname'),
        searchParams: Object.getOwnPropertyDescriptor(Interface.prototype, 'searchParams') };
    document.documentElement.setAttribute('data-url-idl-detached-ready', 'true');
})();
)JS", "test:window-url-idl-detach");
    const char *ready = node_attr(fixture.document->html, "data-url-idl-detached-ready");
    const bool prepared = ready && !strcmp(ready, "true");
    CHECK("detached URL regression retains actual child functions before removal", prepared);
    JsCtx *retained_child = js_frame_ctx(doc_get_element_by_id(fixture.document, "idlChild"));
    const bool pending_child_work = retained_child && !retained_child->dead &&
        !retained_child->timers.empty() && !retained_child->rafs.empty();
    CHECK("the real child has scheduled timer and animation work before host detachment", pending_child_work);
    js_eval(fixture.context, "globalThis.__urlIDLDetached.frame.remove();", "test:window-url-idl-remove-frame");
    for (int index = 0; index < 3; ++index) js_tick(fixture.context);
    size_t active_children = 0;
    bool retired_work_canceled = true, owner_still_retained = false;
    for (JsCtx *child : fixture.context->kids) {
        if (child == retained_child) owner_still_retained = true;
        if (!child->dead) ++active_children;
        else retired_work_canceled = retired_work_canceled && child->timers.empty() && child->rafs.empty();
    }
    const bool retained_owner_safe = owner_still_retained && retained_child->dead &&
        retained_child->doc && retained_child->frame_el &&
        !(retained_child->frame_el->flags & NF_CONNECTED) && js_frame_ctx(retained_child->frame_el) == nullptr;
    js_eval(fixture.context, R"JS(
(() => {
    const saved = globalThis.__urlIDLDetached, callbacks = globalThis.__urlIDLDetachedCallbacks;
    document.documentElement.setAttribute('data-url-idl-inactive',
        String(!saved.frame.isConnected && saved.frame.contentWindow === null && window.length === 0));
    document.documentElement.setAttribute('data-url-idl-work-canceled',
        String(callbacks.timer === 0 && callbacks.raf === 0));
})();
)JS", "test:window-url-idl-detached-state");
    const char *inactive = node_attr(fixture.document->html, "data-url-idl-inactive");
    const bool detached = retained_owner_safe && active_children == 0 && inactive && !strcmp(inactive, "true");
    CHECK("the removed child is inactive while its reachable native owner stays safe", detached);
    const char *canceled = node_attr(fixture.document->html, "data-url-idl-work-canceled");
    CHECK("removed child timer and animation work is canceled without invoking callbacks",
          prepared && pending_child_work && retired_work_canceled && canceled && !strcmp(canceled, "true"));
    // Retained child functions/DOM keep the realm reachable. This deliberately
    // does not require deleting its native owner or reclaiming its memory.
    if (prepared && detached)
        result_rows(fixture.context, "runURLIDLDetachedRegression(__urlIDLDetached)", 4);
    js_eval(fixture.context, "globalThis.__urlIDLDetached=null;globalThis.__urlIDLDetachedCallbacks=null;",
            "test:window-url-idl-detached-cleanup");
}

static void check_worker_idl() {
    std::string common, driver;
    const bool source_ok = read_source("tests/js/url-idl-regression.js", common) &&
        read_source("tests/js/worker-url-idl-regression.js", driver) && common.size() + driver.size() < 65536;
    CHECK("worker IDL sources are present and bounded", source_ok);
    if (!source_ok) return;
    {
        Fixture fixture;
        JsCtx *c = fixture.context;
        CHECK("actual worker owner initializes", c && !c->ctx.IsEmpty());
        if (!c || c->ctx.IsEmpty()) return;
        const std::string source = common + "\n" + driver;
        {
            JS_ENTER(c);
            (void)ctx->Global()->Set(ctx, jstr(iso, "__urlIDLWorkerSource"), jstr(iso, source.c_str()));
        }
        js_eval(c, R"JS(
globalThis.__urlIDLRows = null;
globalThis.__urlIDLReady = false;
globalThis.__urlIDLErrors = [];
globalThis.__urlIDLWorker = new Worker(URL.createObjectURL(new Blob([__urlIDLWorkerSource], {type: 'text/javascript'})));
const IDLFailure = message => {
    __urlIDLErrors.push(String(message));
    document.documentElement.setAttribute('data-worker-url-idl-error', __urlIDLErrors.join('; '));
};
__urlIDLWorker.onerror = event => IDLFailure('worker error: ' + event.message);
__urlIDLWorker.onmessageerror = () => IDLFailure('worker messageerror');
__urlIDLWorker.onmessage = event => {
    const data = event.data;
    if (!data || typeof data !== 'object') { IDLFailure('invalid message'); return; }
    if (data.kind === 'lumen-url-idl-ready') {
        if (__urlIDLReady || data.completed !== 83 || data.expected !== 84) {
            IDLFailure('invalid or repeated ready'); return;
        }
        __urlIDLReady = true;
        __urlIDLWorker.postMessage({kind: 'lumen-url-idl-ping', relative: '/after?q=kept', base: 'https://worker.test/start'});
    } else if (data.kind === 'lumen-url-idl-results') {
        if (!__urlIDLReady || __urlIDLRows !== null || data.expected !== 84 || !Array.isArray(data.rows) ||
            data.rows.length !== 84 || data.rows.some(row => !row || typeof row.name !== 'string' ||
                (row.status !== 'PASS' && row.status !== 'FAIL')) || new Set(data.rows.map(row => row.name)).size !== 84) {
            IDLFailure('invalid, duplicate, or incomplete rows'); return;
        }
        __urlIDLRows = data.rows;
        document.documentElement.setAttribute('data-worker-url-idl-complete', 'true');
    } else IDLFailure('unexpected worker message');
};
)JS", "test:worker-url-idl-driver");
        bool complete = false;
        const double deadline = now_ms() + 5000;
        while (now_ms() < deadline) {
            js_tick(c);
            const char *done = node_attr(fixture.document->html, "data-worker-url-idl-complete");
            complete = done && !strcmp(done, "true");
            if (complete || node_attr(fixture.document->html, "data-worker-url-idl-error")) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const char *error = node_attr(fixture.document->html, "data-worker-url-idl-error");
        CHECK("actual worker completes READY and all 84 result rows before deadline", complete && !error);
        if (error) fprintf(stderr, "worker IDL driver: %s\n", error);
        if (complete && !error) result_rows(c, "__urlIDLRows", 84);
        js_eval(c, "if(globalThis.__urlIDLWorker)__urlIDLWorker.terminate(); __urlIDLWorker=null; __urlIDLWorkerSource=null;",
                "test:worker-url-idl-cleanup");
    }
    CHECK("worker IDL regression releases its thread and owner", wait_worker_threads(5000));
}

int main(int argc, char **argv) {
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    const bool both = argc == 1;
    if (both || (argc == 2 && !strcmp(argv[1], "--window"))) check_window_idl();
    else if (argc != 2 || strcmp(argv[1], "--worker")) {
        fprintf(stderr, "usage: test_url_idl [--window|--worker]\n");
        net_shutdown(); return 2;
    }
    if (both || !strcmp(argv[1], "--worker")) check_worker_idl();
    net_shutdown();
    printf("RESULTS url-idl %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
