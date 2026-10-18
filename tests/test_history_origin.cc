/* The History API must not turn a URL rewrite into a new document principal.
 * This uses existing public JS APIs and a native host that records URL writes,
 * so it can run against the old implementation before popup APIs are added. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
#include <string>

extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { \
    ++failures; fprintf(stderr, "FAIL %s\n", name); \
} } while (0)

static bool value(JsCtx *c, const char *source) {
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    return v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result) && result->BooleanValue(iso);
}

struct Fixture {
    Document *document = nullptr;
    JsCtx *context = nullptr;
    int url_writes = 0;
    bool last_push = false;
    std::string last_url;

    static void set_url(void *opaque, const char *url, bool push) {
        auto *self = static_cast<Fixture *>(opaque);
        ++self->url_writes; self->last_push = push; self->last_url = url;
    }
    Fixture() {
        document = doc_new("https://sender.test/start");
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{}; host.ud = this; host.set_url = set_url;
        context = js_new(document, &host);
    }
    ~Fixture() { js_free(context); doc_free(document); }
};

int main(int argc, char **argv) {
    (void)argc; dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    const char *targets[] = {
        "https://trusted.test/callback", /* different host */
        "http://sender.test/callback",   /* different scheme */
        "https://sender.test:444/callback", /* different port */
        "https://user@sender.test/callback", /* credentials are not rewritable */
        "data:text/html,callback",       /* a tuple principal cannot become opaque */
        "https://[",                     /* invalid URL must not mutate state */
    };
    for (const char *method : {"pushState", "replaceState"}) for (const char *target : targets) {
        Fixture f;
        CHECK("initial history state is installed", value(f.context,
            "history.replaceState({keep:'original'},'',location.href);"
            "globalThis.savedState=history.state;globalThis.errorName='';true"));
        f.url_writes = 0;
        std::string script = "try { history." + std::string(method) +
            "({changed:true},'',\"" + target + "\"); } catch(e) { errorName=e.name; } true";
        bool evaluated = value(f.context, script.c_str());
        if (!evaluated || !value(f.context, "errorName==='SecurityError'"))
            fprintf(stderr, "history case: %s %s\n", method, target);
        CHECK("prohibited rewrite throws SecurityError", evaluated && value(f.context, "errorName==='SecurityError'"));
        CHECK("prohibited rewrite preserves the native document URL", !strcmp(f.document->url, "https://sender.test/start"));
        CHECK("prohibited rewrite preserves the native principal", ctx_origin(f.context) == "https://sender.test");
        CHECK("prohibited rewrite preserves visible URL and state object", value(f.context,
            "location.href==='https://sender.test/start' && history.state===savedState && history.state.keep==='original'"));
        CHECK("prohibited rewrite does not notify the native host", f.url_writes == 0);
    }

    {
        Fixture f;
        CHECK("canonical same-origin URL is accepted", value(f.context,
            "globalThis.payload={step:1};history.pushState(payload,'','https://SENDER.test:443/a?x=1#one');"
            "payload.step=9;location.href==='https://sender.test/a?x=1#one' && history.state.step===1"));
        CHECK("valid push preserves origin and notifies host once", ctx_origin(f.context) == "https://sender.test" &&
            f.url_writes == 1 && f.last_push && f.last_url == "https://sender.test/a?x=1#one");
        CHECK("relative same-origin replacement is accepted", value(f.context,
            "history.replaceState({step:2},'','../b#two');location.href==='https://sender.test/b#two' && history.state.step===2"));
        CHECK("valid replacement is not reported as a push", f.url_writes == 2 && !f.last_push &&
            f.last_url == "https://sender.test/b#two" && ctx_origin(f.context) == "https://sender.test");
        CHECK("an omitted replacement URL leaves the URL unchanged", value(f.context,
            "history.replaceState({step:3},'');location.href==='https://sender.test/b#two' && history.state.step===3"));
        int previous_writes = f.url_writes;
        CHECK("clone errors preserve URL and state", value(f.context,
            "globalThis.savedState=history.state;let cloneError='';"
            "try { history.pushState(function(){},'','/not-installed'); } catch(e) { cloneError=e.name; }"
            "cloneError==='DataCloneError' && history.state===savedState && location.href==='https://sender.test/b#two'"));
        CHECK("clone failure does not reach the native URL host", f.url_writes == previous_writes);
    }
    net_shutdown();
    printf("RESULTS history-origin %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
