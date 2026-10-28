/* Public platform-brand serialization in real window, child and worker realms.
 * This uses the normal JS_SRC link and public finite worker shutdown entry.
 * These custom regression rows are never counted as official WPT assertions.
 */
#include "../src/js/js_int.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <thread>

extern "C" void font_init(void);
extern "C" bool js_workers_shutdown(unsigned timeout_ms);
static int checks, failures;
#define CHECK(name, condition) do { \
    bool okay_ = (condition); ++checks; \
    printf("%s %s\n", okay_ ? "PASS" : "FAIL", name); \
    if (!okay_) ++failures; fflush(stdout); \
} while (0)

static bool read_source(const char *path, std::string &source) {
    std::ifstream input(path, std::ios::binary);
    source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad() && !source.empty() && source.size() < 65536;
}

struct Fixture {
    Document *document;
    JsCtx *context;
    explicit Fixture(bool isolated = false) {
        document = doc_new("https://clone.test/start");
        document->cross_origin_isolated = isolated;
        const char *html = "<!doctype html><html><body><iframe id='cloneChild'></iframe></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{};
        context = js_new(document, &host);
    }
    ~Fixture() { if (context) js_free(context); doc_free(document); }
};

static bool evaluate(JsCtx *context, const char *source) {
    if (!context || context->ctx.IsEmpty()) return false;
    JS_ENTER(context);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    const bool okay = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result);
    if (!okay && caught.HasCaught())
        fprintf(stderr, "clone driver: %s\n", jcstr(iso, caught.Exception()).c_str());
    return okay && result->IsTrue();
}

static bool result_rows(JsCtx *context, const char *expression, size_t expected) {
    JS_ENTER(context);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> value;
    const bool evaluated = v8::Script::Compile(ctx, jstr(iso, expression)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&value);
    CHECK("clone regression returns a complete result array",
          evaluated && value->IsArray() && value.As<v8::Array>()->Length() == expected);
    if (!evaluated || !value->IsArray() || value.As<v8::Array>()->Length() != expected) {
        if (caught.HasCaught()) fprintf(stderr, "clone rows: %s\n", jcstr(iso, caught.Exception()).c_str());
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
        const std::string label = shape ? jcstr(iso, name) : "malformed clone regression row";
        const std::string outcome = shape ? jcstr(iso, status) : "";
        shape = shape && !label.empty() && names.insert(label).second && (outcome == "PASS" || outcome == "FAIL");
        valid = valid && shape;
        CHECK(label.c_str(), shape && outcome == "PASS");
        if (shape && outcome != "PASS" && row.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
            fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
    }
    CHECK("clone rows have unique names and explicit statuses", valid && names.size() == expected);
    return valid;
}

static void check_window_clone() {
    std::string common, realm;
    const bool source_ok = read_source("tests/js/clone-brand-regression.js", common) &&
        read_source("tests/js/clone-brand-window-regression.js", realm);
    CHECK("window and child clone sources are present and bounded", source_ok);
    if (!source_ok) return;
    Fixture fixture;
    JsCtx *context = fixture.context;
    CHECK("actual parsed-iframe clone fixture initializes", context && !context->ctx.IsEmpty());
    if (!context || context->ctx.IsEmpty()) return;
    js_eval(context, common.c_str(), "test:window-clone-brand-source");
    result_rows(context, "runCloneBrandRegression()", 42);
    js_eval(context, realm.c_str(), "test:window-clone-realm-source");
    result_rows(context, "runCloneWindowRegression(document.getElementById('cloneChild').contentWindow)", 18);
    const bool prepared = evaluate(context, R"JS(
(() => {
    const frame = document.getElementById('cloneChild'), child = frame.contentWindow;
    globalThis.__cloneDetachedCallbacks = {timer: 0, raf: 0};
    child.setTimeout(() => __cloneDetachedCallbacks.timer++, 0);
    child.requestAnimationFrame(() => __cloneDetachedCallbacks.raf++);
    globalThis.__cloneDetached = {frame, clone: child.structuredClone, Exception: child.DOMException,
        ObjectPrototype: child.Object.prototype, url: new child.URL('https://clone.test/retained'),
        params: new child.URLSearchParams('kept=one')};
    return true;
})()
)JS");
    CHECK("clone regression retains genuine child functions and instances before removal", prepared);
    JsCtx *retained_child = js_frame_ctx(doc_get_element_by_id(fixture.document, "cloneChild"));
    const bool pending = retained_child && !retained_child->dead &&
        !retained_child->timers.empty() && !retained_child->rafs.empty();
    CHECK("the real child has scheduled timer and animation work", pending);
    const bool removed = prepared && evaluate(context, "__cloneDetached.frame.remove(); true");
    for (int index = 0; index < 3; ++index) js_tick(context);
    size_t active_children = 0;
    bool owner_retained = false, canceled_native = true;
    for (JsCtx *child : context->kids) {
        if (child == retained_child) owner_retained = true;
        if (!child->dead) ++active_children;
        else canceled_native = canceled_native && child->timers.empty() && child->rafs.empty();
    }
    const bool safe_owner = owner_retained && retained_child->dead && retained_child->doc &&
        retained_child->frame_el && !(retained_child->frame_el->flags & NF_CONNECTED) &&
        js_frame_ctx(retained_child->frame_el) == nullptr;
    const bool detached = removed && safe_owner && active_children == 0 && evaluate(context,
        "!__cloneDetached.frame.isConnected && __cloneDetached.frame.contentWindow === null && window.length === 0");
    CHECK("the child is inactive while its reachable native owner remains safe", detached);
    CHECK("removed child work is canceled without firing callbacks", prepared && pending && canceled_native &&
        evaluate(context, "__cloneDetachedCallbacks.timer === 0 && __cloneDetachedCallbacks.raf === 0"));
    // Retained functions keep their realm reachable. Do not delete its native
    // owner or equate this observable safety test with memory reclamation.
    if (prepared && detached) result_rows(context, "runCloneDetachedRegression(__cloneDetached)", 4);
    js_eval(context, "globalThis.__cloneDetached=null;globalThis.__cloneDetachedCallbacks=null;",
            "test:window-clone-detached-cleanup");
}

static void check_worker_clone() {
    std::string common, driver;
    const bool source_ok = read_source("tests/js/clone-brand-regression.js", common) &&
        read_source("tests/js/worker-clone-brand-regression.js", driver) && common.size() + driver.size() < 65536;
    CHECK("worker clone sources are present and bounded", source_ok);
    if (!source_ok) return;
    Fixture fixture(true);
    JsCtx *context = fixture.context;
    CHECK("actual isolated worker owner initializes", context && !context->ctx.IsEmpty());
    if (!context || context->ctx.IsEmpty()) return;
    const std::string source = common + "\n" + driver;
    {
        JS_ENTER(context);
        (void)ctx->Global()->Set(ctx, jstr(iso, "__cloneWorkerSource"), jstr(iso, source.c_str()));
    }
    js_eval(context, R"JS(
globalThis.__cloneWorkerRows = null;
globalThis.__cloneParentRows = [];
globalThis.__cloneWorkerReady = false;
globalThis.__cloneUnexpectedDeliveries = 0;
globalThis.__cloneReturned = null;
globalThis.__cloneErrors = [];
globalThis.__cloneWorkerURL = URL.createObjectURL(new Blob([__cloneWorkerSource], {type: 'text/javascript'}));
globalThis.__cloneWorker = new Worker(__cloneWorkerURL);
const cloneFailure = message => {
    __cloneErrors.push(String(message));
    document.documentElement.setAttribute('data-worker-clone-error', __cloneErrors.join('; '));
};
__cloneWorker.onerror = event => { event.preventDefault(); cloneFailure('worker error: ' + event.message); };
__cloneWorker.onmessageerror = () => cloneFailure('worker messageerror');
__cloneWorker.onmessage = event => {
    const data = event.data;
    if (!data || typeof data !== 'object') { cloneFailure('invalid message'); return; }
    if (data.kind === 'lumen-clone-rejection-probe') { __cloneUnexpectedDeliveries++; return; }
    if (data.kind === 'lumen-clone-ready') {
        if (__cloneWorkerReady || data.completed !== 50 || data.expected !== 52) {
            cloneFailure('invalid or repeated ready'); return;
        }
        __cloneWorkerReady = true;
        for (const name of ['URL', 'URLSearchParams']) {
            let passed = false, message = 'Expected a genuine DataCloneError';
            try { __cloneWorker.postMessage({kind: 'lumen-clone-rejection-probe',
                value: new globalThis[name](name === 'URL' ? 'https://clone.test/outgoing' : 'q=one')}); }
            catch (error) { passed = error instanceof DOMException && error.name === 'DataCloneError' && error.code === 25;
                            message = String(error && error.stack || error); }
            __cloneParentRows.push({name: name + ' is rejected by the native owner-to-worker serializer',
                                   status: passed ? 'PASS' : 'FAIL', message});
        }
        try {
            const bytes = [0,97,115,109,1,0,0,0,1,5,1,96,0,1,127,3,2,1,0,7,9,1,5,118,97,108,117,101,0,0,10,6,1,4,0,65,7,11];
            const module = new WebAssembly.Module(new Uint8Array(bytes));
            const shared = globalThis.__cloneSentShared = new SharedArrayBuffer(8);
            new Uint8Array(shared)[0] = 17;
            const child = {value: 9}, graph = {first: child, second: child}; graph.self = graph;
            __cloneWorker.postMessage({kind: 'lumen-clone-ping', module, shared, graph});
        } catch (error) { cloneFailure('real payload send failed: ' + error); }
    } else if (data.kind === 'lumen-clone-results') {
        if (!__cloneWorkerReady || __cloneWorkerRows !== null || data.expected !== 52 || !Array.isArray(data.rows) ||
            data.rows.length !== 52 || data.rows.some(row => !row || typeof row.name !== 'string' ||
                (row.status !== 'PASS' && row.status !== 'FAIL')) || new Set(data.rows.map(row => row.name)).size !== 52) {
            cloneFailure('invalid, duplicate, or incomplete result rows'); return;
        }
        __cloneWorkerRows = data.rows; __cloneReturned = data;
        document.documentElement.setAttribute('data-worker-clone-complete', 'true');
    } else cloneFailure('unexpected worker message');
};
)JS", "test:worker-clone-driver");
    bool complete = false;
    const double deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        js_tick(context);
        const char *done = node_attr(fixture.document->html, "data-worker-clone-complete");
        complete = done && !strcmp(done, "true");
        if (complete || node_attr(fixture.document->html, "data-worker-clone-error")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const char *error = node_attr(fixture.document->html, "data-worker-clone-error");
    CHECK("actual worker completes READY and all52 unique rows before the deadline", complete && !error);
    if (error) fprintf(stderr, "worker clone driver: %s\n", error);
    if (complete && !error) {
        result_rows(context, "__cloneWorkerRows", 52);
        result_rows(context, "__cloneParentRows", 2);
    }
    CHECK("actual return message preserves compiled module and shared backing", complete && !error &&
        evaluate(context, "__cloneReturned.module instanceof WebAssembly.Module && "
            "new WebAssembly.Instance(__cloneReturned.module).exports.value() === 7 && "
            "__cloneReturned.shared instanceof SharedArrayBuffer && __cloneReturned.shared !== __cloneSentShared && "
            "new Uint8Array(__cloneReturned.shared)[0] === 23 && new Uint8Array(__cloneSentShared)[0] === 23"));
    CHECK("failed worker-to-owner serialization never enqueues rejected payloads", complete && !error &&
        evaluate(context, "__cloneUnexpectedDeliveries === 0"));
    js_eval(context, "if(globalThis.__cloneWorker)__cloneWorker.terminate(); "
        "URL.revokeObjectURL(__cloneWorkerURL); __cloneWorker=null; __cloneWorkerSource=null; "
        "__cloneSentShared=null; __cloneReturned=null;", "test:worker-clone-cleanup");
}

int main(int argc, char **argv) {
    const bool both = argc == 1;
    if (!both && (argc != 2 || (strcmp(argv[1], "--window") && strcmp(argv[1], "--worker")))) {
        fprintf(stderr, "usage: test_clone_brand [--window|--worker]\n");
        return 2;
    }
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    if (both || !strcmp(argv[1], "--window")) check_window_clone();
    if (both || !strcmp(argv[1], "--worker")) check_worker_clone();
    const bool drained = js_workers_shutdown(5000);
    CHECK("all actual clone-test workers drain before subsystem teardown", drained);
    printf("RESULTS clone-brand %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    fflush(stdout);
    if (!drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
