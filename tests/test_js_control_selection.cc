/* Native V8 integration of the public-API-only WebKit-compatible fixture. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unistd.h>
extern "C" void font_init(void);

static bool finished(JsCtx *c) {
    JS_ENTER(c);
    v8::Local<v8::Value> value;
    v8::Local<v8::Script> script;
    return v8::Script::Compile(ctx, jstr(iso, "!!globalThis.__controlResult?.finished")).ToLocal(&script) && script->Run(ctx).ToLocal(&value) && value->BooleanValue(iso);
}
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "tests/js/control-selection.js";
    std::ifstream file(path); std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (source.empty()) { fprintf(stderr, "cannot read control selection fixture: %s\n", path); return 2; }
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Document *d = doc_new("https://control.test/");
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host{}; JsCtx *c = js_new(d, &host);
    js_eval(c, source.c_str(), "control-selection.js");
    for (int i = 0; i < 250 && !finished(c); i++) { js_tick(c); usleep(1000); }
    int failures = 1, checks = 0;
    if (finished(c)) {
        JS_ENTER(c);
        v8::Local<v8::Value> value;
        if (ctx->Global()->Get(ctx, jstr(iso, "__controlResult")).ToLocal(&value) && value->IsObject()) {
            auto result = value.As<v8::Object>();
            if (result->Get(ctx, jstr(iso, "checks")).ToLocal(&value)) checks = value->Int32Value(ctx).FromMaybe(0);
            if (result->Get(ctx, jstr(iso, "failures")).ToLocal(&value) && value->IsArray()) {
                auto errors = value.As<v8::Array>(); failures = (int)errors->Length();
                for (int i = 0; i < failures; i++) if (errors->Get(ctx, i).ToLocal(&value)) fprintf(stderr, "FAIL %s\n", jcstr(iso, value).c_str());
            }
        }
    } else fprintf(stderr, "FAIL selection fixture did not finish\n");
    js_release_pins(); js_free(c); doc_free(d); net_shutdown();
    printf("RESULTS native-control-selection %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
