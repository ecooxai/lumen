/* Blob/File regressions with normal JS_SRC linkage, an actual child context,
 * an actual dedicated worker and bounded public worker shutdown. Custom rows
 * are kept separate from the unchanged upstream FileAPI WPT denominator. */
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
        document = doc_new("https://blob.test/start");
        const char *html = "<!doctype html><html><body><iframe id='blobChild'></iframe></body></html>";
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
    CHECK("Blob/File driver returns its complete result array", complete);
    if (!complete) {
        if (caught.HasCaught()) fprintf(stderr, "Blob/File driver: %s\n", jcstr(iso, caught.Exception()).c_str());
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
        const std::string label = shape ? jcstr(iso, name) : "malformed Blob/File regression row";
        const std::string outcome = shape ? jcstr(iso, status) : "";
        shape = shape && !label.empty() && names.insert(label).second && (outcome == "PASS" || outcome == "FAIL");
        valid = valid && shape;
        CHECK(label.c_str(), shape && outcome == "PASS");
        if (shape && outcome != "PASS" && row.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
            fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
    }
    CHECK("Blob/File rows have unique names and explicit statuses", valid && names.size() == expected);
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
                             double timeout_ms = 20000) {
    node_remove_attr(fixture.document->html, "data-blob-file-complete");
    node_remove_attr(fixture.document->html, "data-blob-file-error");
    const std::string source = "globalThis.__blobFileRows = null; Promise.resolve().then(() => (" + std::string(expression) +
        ")).then(rows => { __blobFileRows = rows; document.documentElement.setAttribute('data-blob-file-complete','true'); })"
        ".catch(error => document.documentElement.setAttribute('data-blob-file-error',String(error && error.stack || error)));";
    js_eval(fixture.context, source.c_str(), "test:blob-file-async-driver");
    const bool complete = wait_attribute(fixture, "data-blob-file-complete", "data-blob-file-error", timeout_ms);
    CHECK(label, complete);
    const char *error = node_attr(fixture.document->html, "data-blob-file-error");
    if (error) fprintf(stderr, "Blob/File driver: %s\n", error);
    if (complete) result_rows(fixture.context, "__blobFileRows", expected);
}

static void check_window_blob_file() {
    std::string common, realms;
    const bool sources = read_source("tests/js/blob-file-regression.js", common) &&
        read_source("tests/js/blob-file-realm-regression.js", realms);
    CHECK("window Blob/File regression sources are present and bounded", sources);
    if (!sources) return;
    Fixture fixture;
    CHECK("actual parsed-child Blob/File fixture initializes", fixture.context && !fixture.context->ctx.IsEmpty());
    if (!fixture.context || fixture.context->ctx.IsEmpty()) return;
    js_eval(fixture.context, common.c_str(), "test:window-blob-file-source");
    check_async_rows(fixture, "runBlobFileRegression('window')", 69,
        "window completes all 69 Blob/File cases before deadline");
    js_eval(fixture.context, realms.c_str(), "test:window-blob-file-realms");
    check_async_rows(fixture, "runBlobFileRealmRegression(document.getElementById('blobChild').contentWindow)", 20,
        "the actual parsed child completes all 20 Blob/File realm cases");
    check_async_rows(fixture, "runBlobFileWindowRegression(document.getElementById('blobChild').contentWindow)", 6,
        "window completes all 6 private-byte consumer cases");
    js_eval(fixture.context, "globalThis.__blobFileRows=null;", "test:window-blob-file-cleanup");
}

static void check_worker_blob_file() {
    std::string common, driver;
    const bool sources = read_source("tests/js/blob-file-regression.js", common) &&
        read_source("tests/js/worker-blob-file-regression.js", driver) && common.size() + driver.size() < 65536;
    CHECK("worker Blob/File source and protocol are present and bounded", sources);
    if (!sources) return;
    {
        Fixture fixture;
        JsCtx *context = fixture.context;
        CHECK("actual Blob/File worker owner initializes", context && !context->ctx.IsEmpty());
        if (!context || context->ctx.IsEmpty()) return;
        const std::string source = common + "\n" + driver;
        {
            JS_ENTER(context);
            (void)ctx->Global()->Set(ctx, jstr(iso, "__blobFileWorkerSource"), jstr(iso, source.c_str()));
        }
        js_eval(context, R"JS(
globalThis.__blobFileWorkerRows = null;
globalThis.__blobFileWorkerReady = false;
globalThis.__blobFileWorkerErrors = [];
globalThis.__blobFileWorkerURL = URL.createObjectURL(new Blob([__blobFileWorkerSource], {type:'text/javascript'}));
globalThis.__blobFileWorker = new Worker(__blobFileWorkerURL);
const blobFileWorkerFailure = message => {
    __blobFileWorkerErrors.push(String(message));
    document.documentElement.setAttribute('data-worker-blob-file-error',__blobFileWorkerErrors.join('; '));
};
__blobFileWorker.onerror = event => blobFileWorkerFailure('worker error: ' + event.message);
__blobFileWorker.onmessageerror = () => blobFileWorkerFailure('worker messageerror');
__blobFileWorker.onmessage = event => {
    const data = event.data;
    if (!data || typeof data !== 'object') { blobFileWorkerFailure('invalid message'); return; }
    if (data.kind === 'lumen-blob-file-ready') {
        if (__blobFileWorkerReady || data.completed !== 71 || data.expected !== 72) {
            blobFileWorkerFailure('invalid or repeated READY'); return;
        }
        __blobFileWorkerReady = true;
        __blobFileWorker.postMessage({kind:'lumen-blob-file-ping',value:'after-private-reads'});
    } else if (data.kind === 'lumen-blob-file-results') {
        if (!__blobFileWorkerReady || __blobFileWorkerRows !== null || data.expected !== 72 ||
            !Array.isArray(data.rows) || data.rows.length !== 72 || new Set(data.rows.map(row => row.name)).size !== 72 ||
            data.rows.some(row => !row || typeof row.name !== 'string' || (row.status !== 'PASS' && row.status !== 'FAIL'))) {
            blobFileWorkerFailure('invalid, duplicate or incomplete result rows'); return;
        }
        __blobFileWorkerRows = data.rows;
        document.documentElement.setAttribute('data-worker-blob-file-complete','true');
    } else blobFileWorkerFailure(data.kind + ': ' + (data.message || 'unexpected message'));
};
)JS", "test:worker-blob-file-driver");
        const bool complete = wait_attribute(fixture, "data-worker-blob-file-complete", "data-worker-blob-file-error", 90000);
        CHECK("actual worker completes READY and all 72 Blob/File results before deadline", complete);
        const char *error = node_attr(fixture.document->html, "data-worker-blob-file-error");
        if (error) fprintf(stderr, "Worker Blob/File driver: %s\n", error);
        if (complete) result_rows(context, "__blobFileWorkerRows", 72);
        js_eval(context,
            "if(globalThis.__blobFileWorker)__blobFileWorker.terminate();if(globalThis.__blobFileWorkerURL)URL.revokeObjectURL(__blobFileWorkerURL);"
            "globalThis.__blobFileWorker=null;globalThis.__blobFileWorkerSource=null;globalThis.__blobFileWorkerURL=null;",
            "test:worker-blob-file-cleanup");
    }
}

int main(int argc, char **argv) {
    const bool both = argc == 1;
    if (!both && (argc != 2 || (strcmp(argv[1], "--window") && strcmp(argv[1], "--worker")))) {
        fprintf(stderr, "usage: test_blob_file [--window|--worker]\n");
        return 2;
    }
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    if (both || !strcmp(argv[1], "--window")) check_window_blob_file();
    if (both || !strcmp(argv[1], "--worker")) check_worker_blob_file();
    const bool drained = js_workers_shutdown(5000);
    CHECK("all actual Blob/File workers drain before subsystem teardown", drained);
    printf("RESULTS blob-file %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    fflush(stdout);
    if (!drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
