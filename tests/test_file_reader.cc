/* Real-window, child-realm and worker FileReader regressions using normal
 * JS_SRC linkage and the public bounded worker-shutdown entry. These custom
 * regression rows are never counted as upstream WPT assertions. */
#include "../src/js/js_int.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <thread>

extern "C" void font_init(void);
extern "C" bool js_workers_shutdown(unsigned timeout_ms);
static int checks, failures;
#define CHECK(name, condition) do { \
    const bool okay_ = (condition); ++checks; \
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
    Fixture() {
        document = doc_new("https://reader.test/start");
        const char *html = "<!doctype html><html><body><iframe id='readerChild'></iframe></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{};
        context = js_new(document, &host);
    }
    ~Fixture() { if (context) js_free(context); doc_free(document); }
};

static bool result_rows(JsCtx *c, const char *expression, size_t expected) {
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> value;
    const bool evaluated = v8::Script::Compile(ctx, jstr(iso, expression)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&value);
    const bool complete = evaluated && value->IsArray() && value.As<v8::Array>()->Length() == expected;
    CHECK("FileReader driver returns its complete result array", complete);
    if (!complete) {
        if (caught.HasCaught()) fprintf(stderr, "FileReader driver: %s\n", jcstr(iso, caught.Exception()).c_str());
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
        const std::string label = shape ? jcstr(iso, name) : "malformed FileReader regression row";
        const std::string outcome = shape ? jcstr(iso, status) : "";
        shape = shape && !label.empty() && names.insert(label).second && (outcome == "PASS" || outcome == "FAIL");
        valid = valid && shape;
        CHECK(label.c_str(), shape && outcome == "PASS");
        if (shape && outcome != "PASS" && row.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
            fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
    }
    CHECK("FileReader rows have unique names and explicit statuses", valid && names.size() == expected);
    return valid;
}

static bool wait_attribute(Fixture &fixture, const char *done_name, const char *error_name, double timeout_ms) {
    const double deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        js_tick(fixture.context);
        const char *done = node_attr(fixture.document->html, done_name);
        if (done && !strcmp(done, "true")) return !node_attr(fixture.document->html, error_name);
        if (node_attr(fixture.document->html, error_name)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static void check_async_rows(Fixture &fixture, const char *expression, size_t expected, const char *label,
                             double timeout_ms = 90000) {
    node_remove_attr(fixture.document->html, "data-reader-complete");
    node_remove_attr(fixture.document->html, "data-reader-error");
    const std::string source = "globalThis.__readerRows = null; Promise.resolve().then(() => (" + std::string(expression) +
        ")).then(rows => { __readerRows = rows; document.documentElement.setAttribute('data-reader-complete','true'); })"
        ".catch(error => document.documentElement.setAttribute('data-reader-error',String(error && error.stack || error)));";
    js_eval(fixture.context, source.c_str(), "test:file-reader-async-driver");
    const bool complete = wait_attribute(fixture, "data-reader-complete", "data-reader-error", timeout_ms);
    CHECK(label, complete);
    const char *error = node_attr(fixture.document->html, "data-reader-error");
    if (error) fprintf(stderr, "FileReader driver: %s\n", error);
    if (complete) result_rows(fixture.context, "__readerRows", expected);
}

static void check_window_reader() {
    std::string common, realms;
    const bool sources = read_source("tests/js/file-reader-regression.js", common) &&
        read_source("tests/js/file-reader-realm-regression.js", realms);
    CHECK("window FileReader regression sources are present and bounded", sources);
    if (!sources) return;
    Fixture fixture;
    CHECK("actual parsed-child FileReader fixture initializes", fixture.context && !fixture.context->ctx.IsEmpty());
    if (!fixture.context || fixture.context->ctx.IsEmpty()) return;
    js_eval(fixture.context, common.c_str(), "test:window-file-reader-source");
    check_async_rows(fixture, "runFileReaderRegression('window')", 75, "window completes all 75 public reader cases before deadline");
    js_eval(fixture.context, realms.c_str(), "test:window-file-reader-realms");
    check_async_rows(fixture, "runFileReaderRealmRegression(document.getElementById('readerChild').contentWindow)", 12,
                     "the actual parsed child completes all 12 reader realm cases", 20000);
    js_eval(fixture.context, R"JS(
(() => {
    const frame = document.getElementById('readerChild'), child = frame.contentWindow;
    const reader = new child.FileReader(), prototype = child.FileReader.prototype;
    const readyDescriptor = Object.getOwnPropertyDescriptor(prototype, 'readyState');
    const resultDescriptor = Object.getOwnPropertyDescriptor(prototype, 'result');
    globalThis.__readerRetiredCallbacks = 0;
    reader.addEventListener('loadstart', () => ++__readerRetiredCallbacks);
    reader.addEventListener('load', () => ++__readerRetiredCallbacks);
    reader.readAsText(new child.Blob(['pending-child-read']));
    globalThis.__readerDetached = {frame, reader, document: child.document,
        createElement: child.document.createElement,
        getReady: readyDescriptor && readyDescriptor.get,
        getResult: resultDescriptor && resultDescriptor.get,
        abort: prototype.abort, readBuffer: prototype.readAsArrayBuffer};
    document.documentElement.setAttribute('data-reader-detach-ready','true');
})();
)JS", "test:file-reader-prepare-detachment");
    const char *ready = node_attr(fixture.document->html, "data-reader-detach-ready");
    const bool prepared = ready && !strcmp(ready, "true");
    CHECK("the real child reader is retained before frame removal", prepared);
    JsCtx *owner = js_frame_ctx(doc_get_element_by_id(fixture.document, "readerChild"));
    const bool queued = owner && !owner->dead && !owner->timers.empty();
    CHECK("the real child owns an actual queued read task before removal", queued);
    js_eval(fixture.context, "if(globalThis.__readerDetached)__readerDetached.frame.remove();", "test:file-reader-remove-frame");
    for (unsigned index = 0; index < 3; ++index) js_tick(fixture.context);
    bool retained = false, inactive = true;
    for (JsCtx *child : fixture.context->kids) {
        if (child == owner) retained = true;
        if (!child->dead) inactive = false;
    }
    const bool safe = retained && owner->dead && owner->doc && owner->frame_el &&
        !(owner->frame_el->flags & NF_CONNECTED) && js_frame_ctx(owner->frame_el) == nullptr;
    CHECK("removed reader frame is inactive with its reachable native owner retained", prepared && safe && inactive);
    js_eval(fixture.context,
        "document.documentElement.setAttribute('data-reader-detach-no-callback',String(globalThis.__readerRetiredCallbacks===0));",
        "test:file-reader-inactive-observation");
    const char *no_callback = node_attr(fixture.document->html, "data-reader-detach-no-callback");
    CHECK("frame removal cancels queued read work without invoking its callbacks",
          prepared && queued && safe && owner->timers.empty() && no_callback && !strcmp(no_callback, "true"));
    if (prepared && safe && inactive)
        check_async_rows(fixture, "runFileReaderDetachedRegression(__readerDetached)", 4,
                         "retained child callbacks complete all 4 reader and DOM safety checks", 10000);
    js_eval(fixture.context, "globalThis.__readerDetached=null;globalThis.__readerRows=null;", "test:file-reader-detached-cleanup");
}

static void check_worker_reader() {
    std::string common, driver;
    const bool sources = read_source("tests/js/file-reader-regression.js", common) &&
        read_source("tests/js/worker-file-reader-regression.js", driver) && common.size() + driver.size() < 65536;
    CHECK("worker FileReader source and protocol are present and bounded", sources);
    if (!sources) return;
    {
        Fixture fixture;
        JsCtx *context = fixture.context;
        CHECK("actual FileReader worker owner initializes", context && !context->ctx.IsEmpty());
        if (!context || context->ctx.IsEmpty()) return;
        const std::string source = common + "\n" + driver;
        {
            JS_ENTER(context);
            (void)ctx->Global()->Set(ctx, jstr(iso, "__readerWorkerSource"), jstr(iso, source.c_str()));
        }
        js_eval(context, R"JS(
globalThis.__readerWorkerRows = null;
globalThis.__readerWorkerReady = false;
globalThis.__readerWorkerErrors = [];
globalThis.__readerWorkerURL = URL.createObjectURL(new Blob([__readerWorkerSource], {type:'text/javascript'}));
globalThis.__readerWorker = new Worker(__readerWorkerURL);
const readerWorkerFailure = message => {
    __readerWorkerErrors.push(String(message));
    document.documentElement.setAttribute('data-worker-reader-error',__readerWorkerErrors.join('; '));
};
__readerWorker.onerror = event => readerWorkerFailure('worker error: ' + event.message);
__readerWorker.onmessageerror = () => readerWorkerFailure('worker messageerror');
__readerWorker.onmessage = event => {
    const data = event.data;
    if (!data || typeof data !== 'object') { readerWorkerFailure('invalid message'); return; }
    if (data.kind === 'lumen-reader-ready') {
        if (__readerWorkerReady || data.completed !== 113 || data.expected !== 114) {
            readerWorkerFailure('invalid or repeated READY'); return;
        }
        __readerWorkerReady = true;
        __readerWorker.postMessage({kind:'lumen-reader-ping',value:'after-reads'});
    } else if (data.kind === 'lumen-reader-results') {
        if (!__readerWorkerReady || __readerWorkerRows !== null || data.expected !== 114 ||
            !Array.isArray(data.rows) || data.rows.length !== 114 || new Set(data.rows.map(row => row.name)).size !== 114 ||
            data.rows.some(row => !row || typeof row.name !== 'string' || (row.status !== 'PASS' && row.status !== 'FAIL'))) {
            readerWorkerFailure('invalid, duplicate or incomplete result rows'); return;
        }
        __readerWorkerRows = data.rows;
        document.documentElement.setAttribute('data-worker-reader-complete','true');
    } else readerWorkerFailure(data.kind + ': ' + (data.message || 'unexpected message'));
};
)JS", "test:worker-file-reader-driver");
        const bool complete = wait_attribute(fixture, "data-worker-reader-complete", "data-worker-reader-error", 90000);
        CHECK("actual worker completes READY and all 114 reader results before deadline", complete);
        const char *error = node_attr(fixture.document->html, "data-worker-reader-error");
        if (error) fprintf(stderr, "Worker FileReader driver: %s\n", error);
        if (complete) result_rows(context, "__readerWorkerRows", 114);
        js_eval(context,
            "if(globalThis.__readerWorker)__readerWorker.terminate();if(globalThis.__readerWorkerURL)URL.revokeObjectURL(__readerWorkerURL);"
            "globalThis.__readerWorker=null;globalThis.__readerWorkerSource=null;globalThis.__readerWorkerURL=null;",
            "test:worker-file-reader-cleanup");
    }
}

int main(int argc, char **argv) {
    const bool both = argc == 1;
    if (!both && (argc != 2 || (strcmp(argv[1], "--window") && strcmp(argv[1], "--worker")))) {
        fprintf(stderr, "usage: test_file_reader [--window|--worker]\n");
        return 2;
    }
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    if (both || !strcmp(argv[1], "--window")) check_window_reader();
    if (both || !strcmp(argv[1], "--worker")) check_worker_reader();
    const bool drained = js_workers_shutdown(5000);
    CHECK("all actual FileReader workers drain before subsystem teardown", drained);
    printf("RESULTS file-reader %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    fflush(stdout);
    if (!drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
