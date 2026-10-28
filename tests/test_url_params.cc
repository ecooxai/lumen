/* Real-window regressions for shared URLSearchParams and URLencoded bodies. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>
#include <chrono>
#include <thread>

extern "C" void font_init(void);
static int checks, failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); ++checks; \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) ++failures; \
} while (0)

struct Fixture {
    Document *document;
    JsCtx *context;
    Fixture() {
        document = doc_new("https://query.test/start");
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{};
        context = js_new(document, &host);
    }
    ~Fixture() { js_free(context); doc_free(document); }
};

static void check_window_query_bodies() {
    std::ifstream input("tests/js/url-params-body-regression.js", std::ios::binary);
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK("window query/body regression source is available", !input.bad() && !source.empty() && source.size() < 65536);
    if (input.bad() || source.empty() || source.size() >= 65536) return;
    Fixture f;
    js_eval(f.context, source.c_str(), "test:window-query-body-source");
    js_eval(f.context, R"JS(
globalThis.__queryBodyRows = null;
globalThis.__queryBodyError = null;
Promise.resolve().then(() => runURLParamsBodyRegression('window')).then(rows => {
    if (!Array.isArray(rows) || rows.length !== 59 ||
        rows.some(row => !row || typeof row.name !== 'string' || (row.status !== 'PASS' && row.status !== 'FAIL')) ||
        new Set(rows.map(row => row.name)).size !== 59) throw new Error('Invalid, duplicate, or incomplete result rows');
    __queryBodyRows = rows;
    document.documentElement.setAttribute('data-query-body-complete', 'true');
}).catch(error => {
    __queryBodyError = String(error && error.stack || error);
    document.documentElement.setAttribute('data-query-body-error', __queryBodyError);
});
)JS", "test:window-query-body-driver");
    bool complete = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        js_tick(f.context);
        const char *done = node_attr(f.document->html, "data-query-body-complete");
        complete = done && !strcmp(done, "true");
        if (complete || node_attr(f.document->html, "data-query-body-error")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const char *error = node_attr(f.document->html, "data-query-body-error");
    CHECK("window completes all 59 unique query/body cases before the deadline", complete && !error);
    if (error) fprintf(stderr, "window query/body driver error: %s\n", error);
    if (complete && !error) {
        JsCtx *c = f.context;
        JS_ENTER(c);
        v8::Local<v8::Value> result;
        bool found = ctx->Global()->Get(ctx, jstr(iso, "__queryBodyRows")).ToLocal(&result) && result->IsArray();
        CHECK("window query/body results remain an intact 59-row array", found && result.As<v8::Array>()->Length() == 59);
        if (found && result.As<v8::Array>()->Length() == 59) {
            auto rows = result.As<v8::Array>();
            for (uint32_t index = 0; index < rows->Length(); ++index) {
                v8::Local<v8::Value> item, name, status;
                bool shape = rows->Get(ctx, index).ToLocal(&item) && item->IsObject();
                if (shape) {
                    auto object = item.As<v8::Object>();
                    shape = object->Get(ctx, jstr(iso, "name")).ToLocal(&name) &&
                        object->Get(ctx, jstr(iso, "status")).ToLocal(&status);
                }
                std::string label = shape ? jcstr(iso, name) : "malformed window query/body row";
                bool passed = shape && jcstr(iso, status) == "PASS";
                CHECK(label.c_str(), passed);
                if (!passed && shape) {
                    v8::Local<v8::Value> message;
                    if (item.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
                        fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    check_window_query_bodies();
    net_shutdown();
    printf("RESULTS url-params-body %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
