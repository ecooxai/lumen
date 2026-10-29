/* Actual form data: iframe navigation, origin/policy and lifetime controls.
 * Focused native regressions are not counted as upstream WPT assertions. */
#include "../src/js/js_int.h"
#include "../src/js/windows.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
extern "C" void font_init(void);
extern "C" void js_form_navigations_pending(size_t *, size_t *);
extern "C" bool js_workers_shutdown(unsigned);
static int checks, failures;
#define CHECK(label, expression) do { const bool okay = !!(expression); ++checks; \
    printf("%s %s\n", okay ? "PASS" : "FAIL", label); fflush(stdout); \
    if (!okay) ++failures; } while (0)

static bool evaluate(JsCtx *context, const std::string &source) {
    if (!context || context->dead || context->ctx.IsEmpty()) return false;
    JS_ENTER(context); v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    const bool okay = v8::Script::Compile(ctx, jstr(iso, source.c_str())).ToLocal(&script)
        && script->Run(ctx).ToLocal(&result);
    if (!okay && caught.HasCaught()) fprintf(stderr, "form data fixture: %s\n", jcstr(iso, caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint();
    return okay && result->BooleanValue(iso);
}
static std::string data_url(const std::string &body, const char *charset = "utf-8") {
    static const char hex[] = "0123456789ABCDEF";
    std::string url = std::string("data:text/html;charset=") + charset + ",";
    for (unsigned char ch : body) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') url += ch;
        else { url += '%'; url += hex[ch >> 4]; url += hex[ch & 15]; }
    }
    return url;
}
static bool queue_empty() {
    size_t count = 1, bytes = 1; js_form_navigations_pending(&count, &bytes);
    return count == 0 && bytes == 0;
}
static bool queue_one() {
    size_t count = 0, bytes = 0; js_form_navigations_pending(&count, &bytes);
    return count == 1 && bytes > 0;
}
struct Fixture {
    Document *document = nullptr; JsCtx *context = nullptr;
    unsigned gets = 0, posts = 0;
    static void navigate(void *opaque, const char *) { ++static_cast<Fixture *>(opaque)->gets; }
    static void navigate_post(void *opaque, const char *, const char *, size_t, const char *) { ++static_cast<Fixture *>(opaque)->posts; }
    explicit Fixture(const char *csp = nullptr, const char *target_sandbox = nullptr) {
        document = doc_new("https://form-data.example.test/owner");
        if (csp) document->csp = xstrdup(csp);
        const char *html = "<!doctype html><html><body><iframe id='target' name='target'></iframe>"
            "<form id='form' method='get' target='target'><input name='key' value='value'></form></body></html>";
        html_parse(document, html, strlen(html));
        Node *frame = nullptr;
        for (Node *n = document->body->first; n; n = n->next) if (node_is(n, A_iframe)) { frame = n; break; }
        if (target_sandbox) node_set_attr(frame, "sandbox", target_sandbox);
        JsHost host{}; host.ud = this; host.navigate = navigate; host.navigate_post = navigate_post;
        context = js_new(document, &host); frames_scan(context);
        evaluate(context, "globalThis.form=document.getElementById('form');globalThis.target=document.getElementById('target');"
            "globalThis.messages=[];globalThis.loads=0;target.onload=()=>loads++;"
            "addEventListener('message',event=>messages.push({data:event.data,origin:event.origin,source:event.source===target.contentWindow}));true");
    }
    ~Fixture() { js_release_pins(); js_free(context); doc_free(document); }
    bool untouched() const { return gets == 0 && posts == 0; }
    bool pump(const std::function<bool()> &done, double milliseconds = 900) {
        const double deadline = now_ms() + milliseconds;
        do { net_poll(); js_tick(context); if (done()) return true;
             std::this_thread::sleep_for(std::chrono::milliseconds(5)); } while (now_ms() < deadline);
        return done();
    }
    void settle() { pump([] { return false; }, 90); }
    JsCtx *target_context() {
        for (JsCtx *child : context->kids)
            if (!child->dead && child->frame_el && node_attr(child->frame_el, "id") &&
                !strcmp(node_attr(child->frame_el, "id"), "target")) return child;
        return nullptr;
    }
    JsCtx *source_context(const char *sandbox, const char *csp = nullptr,
                          const char *permissions = nullptr) {
        Node *frame = node_new_element(document, "iframe", NS_HTML);
        node_set_attr(frame, "src", "https://form-data.example.test/source"); node_set_attr(frame, "name", "source");
        if (sandbox) node_set_attr(frame, "sandbox", sandbox);
        node_append(document->body, frame);
        Document *child = doc_new("https://form-data.example.test/source");
        if (csp) child->csp = xstrdup(csp);
        if (permissions) child->permissions_policy = xstrdup(permissions);
        const char *html = "<!doctype html><html><body><form id='form' method='get' target='_self'></form></body></html>";
        html_parse(child, html, strlen(html));
        JsHost host{}; JsCtx *result = js_new_ex(child, &host, context, frame);
        result->frame_src = child->url;
        evaluate(result, "globalThis.form=document.getElementById('form');true"); return result;
    }
};
static std::string submit(const std::string &url) { return "form.action='" + url + "';form.submit();true"; }

int main(int argc, char **argv) {
    (void)argc; dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    const std::string body = "<!doctype html><html><body><p id='payload'>payload</p><script>"
        "globalThis.__executed=true;let blocked=false;try{void parent.document.body}catch(e){blocked=e.name==='SecurityError'}"
        "parent.postMessage({kind:'data-form',parentBlocked:blocked,origin:location.origin},'*');</script></body></html>";
    const std::string url = data_url(body);
    {
        Fixture f;
        CHECK("named-frame GET data navigation is deferred", evaluate(f.context, submit(url)) && queue_one() && f.untouched());
        CHECK("the actual data document executes and posts to its parent", f.pump([&] { return evaluate(f.context, "messages.length===1"); }));
        CHECK("data message retains opaque origin and genuine source WindowProxy", evaluate(f.context,
            "messages.length===1&&messages[0].origin==='null'&&messages[0].source&&messages[0].data.origin==='null'&&messages[0].data.parentBlocked"));
        CHECK("opaque data document cannot be read through parent DOM access", evaluate(f.context,
            "target.contentDocument===null&&(()=>{try{void target.contentWindow.document;return false}catch(e){return e.name==='SecurityError'}})()"));
        JsCtx *child = f.target_context();
        CHECK("native child origin remains opaque after real data navigation", child && child->origin == "null" && !js_windows_same_origin(f.context, child));
        CHECK("data navigation releases its queue and never invokes top host", queue_empty() && f.untouched());
    }
    {
        Fixture f;
        std::string encoded = "<!doctype html><p id='encoded'>";
        encoded += "\x1b$B\x24\x22\x1b(B";
        encoded += "</p><script>parent.postMessage({kind:'iso',text:document.getElementById('encoded').textContent,charset:document.characterSet},'*');</script>";
        CHECK("ISO-2022-JP form action schedules its real data bytes", evaluate(f.context, submit(data_url(encoded, "iso-2022-jp"))) && queue_one());
        CHECK("data response charset decoding survives form GET serialization", f.pump([&] { return evaluate(f.context,
            "messages.length===1&&messages[0].data.kind==='iso'&&messages[0].data.text==='\u3042'&&messages[0].data.charset==='ISO-2022-JP'"); }));
    }
    for (const char *target : {"_self", "_top", "_blank", "missing"}) {
        Fixture f;
        CHECK("unsupported top or new data targets are safely refused", evaluate(f.context, std::string("form.target='") + target + "';" + submit(url)));
        f.settle(); CHECK("refused data target leaves top host and queue unchanged", f.untouched() && queue_empty() && evaluate(f.context, "messages.length===0"));
    }
    {
        Fixture f;
        CHECK("existing product POST-data restriction remains explicit", evaluate(f.context, "form.method='post';" + submit(url)));
        f.settle(); CHECK("unsupported POST data never becomes a GET or top navigation", f.untouched() && queue_empty() && evaluate(f.context, "messages.length===0&&target.contentDocument.URL==='about:blank'"));
    }
    for (const char *policy : {"form-action 'none'", "frame-src 'none'", "frame-src 'self'"}) {
        Fixture f(policy);
        CHECK("policy-restricted data form submission returns safely", evaluate(f.context, submit(url)));
        f.settle(); CHECK("form-action or frame-src denial prevents data script execution", f.untouched() && queue_empty() && evaluate(f.context, "messages.length===0"));
    }
    {
        Fixture f("frame-src data:; form-action data:; script-src 'none'");
        CHECK("CSP permits only navigation to the data target", evaluate(f.context, submit(url)) && queue_one());
        CHECK("permitted data document loads under inherited script restrictions", f.pump([&] { return evaluate(f.context, "loads===1"); }));
        JsCtx *child = f.target_context();
        CHECK("local data document inherits the source CSP snapshot", child && child->doc->csp && strstr(child->doc->csp, "script-src 'none'"));
        CHECK("inherited CSP blocks parsed inline code in the data document", child && evaluate(child, "typeof globalThis.__executed==='undefined'") && evaluate(f.context, "messages.length===0"));
    }
    {
        Fixture f;
        JsCtx *source = f.source_context(nullptr, "form-action data:; script-src 'none'");
        CHECK("child source can submit to an existing same-origin sibling", evaluate(source, "form.target='target';" + submit(url)) && queue_one());
        CHECK("sibling target receives the policy-constrained data document", f.pump([&] { return evaluate(f.context, "loads===1"); }));
        JsCtx *child = f.target_context();
        CHECK("data target inherits initiating source CSP rather than a permissive parent", child && child->doc->csp && strstr(child->doc->csp, "script-src 'none'") && evaluate(child, "typeof globalThis.__executed==='undefined'") && evaluate(f.context, "messages.length===0"));
    }
    {
        Fixture f("frame-src data:; form-action data:; script-src 'unsafe-inline'");
        const std::string restricted = data_url(
            "<!doctype html><html><head><meta http-equiv='Content-Security-Policy' content=\"script-src 'none'\"></head>"
            "<body><p id='restricted'>restricted</p><script>globalThis.__executed=true;parent.postMessage('forbidden','*')</script></body></html>");
        CHECK("data document may add a stricter meta policy after inherited policy", evaluate(f.context, submit(restricted)) && queue_one());
        CHECK("meta-constrained data document still completes its real navigation", f.pump([&] { return evaluate(f.context, "loads===1"); }));
        JsCtx *child = f.target_context();
        CHECK("data meta script policy intersects and cannot be overridden by inherited allowance", child &&
            evaluate(child, "document.getElementById('restricted')!==null&&typeof globalThis.__executed==='undefined'") &&
            evaluate(f.context, "messages.length===0") && child->opaque_origin);
    }
    {
        Fixture f;
        const char *restricted_permissions = "camera=(), microphone=(), geolocation=()";
        JsCtx *source = f.source_context(nullptr, "form-action data:", restricted_permissions);
        CHECK("sibling form retains its initiating Permissions-Policy at queue time", evaluate(source, "form.target='target';" + submit(url)) && queue_one());
        // Changing the old source after submission must not rewrite the pending
        // navigation's captured policy container or borrow its freed buffer.
        free(source->doc->permissions_policy);
        source->doc->permissions_policy = xstrdup("camera=*, microphone=*, geolocation=*");
        CHECK("permission-constrained sibling data navigation completes", f.pump([&] { return evaluate(f.context, "loads===1"); }));
        JsCtx *child = f.target_context();
        CHECK("data document keeps the immutable initiator Permissions-Policy snapshot", child &&
            child->doc->permissions_policy && !strcmp(child->doc->permissions_policy, restricted_permissions));
        CHECK("preserving source policy does not grant the data document a source principal", child &&
            child->opaque_origin && child->origin == "null" && !js_windows_same_origin(f.context, child));
    }
    {
        Fixture f;
        JsCtx *source = f.source_context("allow-scripts allow-same-origin");
        CHECK("sandboxed source cannot queue a data form submission", evaluate(source, submit(url)) && queue_empty());
        node_set_attr(source->frame_el, "sandbox", "allow-scripts allow-same-origin allow-forms");
        CHECK("sandbox mutation does not relax the current source principal", evaluate(source, submit(url)) && queue_empty());
        f.settle(); CHECK("sandbox-denied data submission leaves no execution or request", evaluate(f.context, "messages.length===0") && f.untouched() && net_pending() == 0);
    }
    {
        Fixture f;
        JsCtx *source = f.source_context("allow-scripts allow-same-origin allow-forms");
        CHECK("allow-forms source may navigate its own real child frame", evaluate(source, submit(url)) && queue_one());
        CHECK("allowed sandbox form reaches the data document with an opaque origin", f.pump([&] { return evaluate(f.context,
            "messages.length===1&&messages[0].origin==='null'&&messages[0].data.parentBlocked"); }));
    }
    {
        Fixture f(nullptr, "allow-forms");
        CHECK("script-disabled target still accepts permitted form navigation", evaluate(f.context, submit(url)) && queue_one());
        CHECK("sandboxed data target loads without running scripts", f.pump([&] { return evaluate(f.context, "loads===1"); }) && evaluate(f.context, "messages.length===0"));
    }
    {
        Fixture f;
        CHECK("queued data target can be removed before delivery", evaluate(f.context, submit(url) + ";target.remove();true"));
        f.settle(); CHECK("target removal releases queued bytes and suppresses payload", queue_empty() && net_pending() == 0 && evaluate(f.context, "messages.length===0") && f.untouched());
    }
    {
        Fixture f;
        JsCtx *source = f.source_context(nullptr);
        CHECK("real source frame may own a queued sibling data navigation", evaluate(source, "form.target='target';" + submit(url)) && queue_one());
        node_remove(source->frame_el); f.settle();
        CHECK("source removal cancels its data navigation and native ownership", queue_empty() && net_pending() == 0 && evaluate(f.context, "messages.length===0") && f.untouched());
    }
    CHECK("all retired fixtures release form queue accounting", queue_empty());
    const double deadline = now_ms() + 2000;
    while (net_pending() && now_ms() < deadline) { net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    CHECK("all data requests finalize before subsystem teardown", net_pending() == 0);
    CHECK("worker ownership remains empty", js_workers_shutdown(3000));
    net_shutdown();
    printf("RESULTS form-data-navigation %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
