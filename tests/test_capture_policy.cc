/* Browser-observable capture policy and resource lifetime regressions. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
extern "C" void font_init(void);
extern "C" bool cap_screen_active(void);
extern "C" void cap_cam_close(int id);

static int checks, failures;
#define CHECK(name, condition) do { bool ok_ = (condition); ++checks; printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); if (!ok_) ++failures; } while (0)
static bool value(JsCtx *c, const char *source) {
    JS_ENTER(c); v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    bool ok = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) && script->Run(ctx).ToLocal(&result);
    if (!ok && caught.HasCaught()) fprintf(stderr, "script: %s\n", jcstr(iso, caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint();
    return ok && result->BooleanValue(iso);
}
static void pump(JsCtx *c) { for (int n=0;n<12;++n) { js_tick(c); usleep(1000); } }
struct Fixture {
    Document *document; JsCtx *context;
    explicit Fixture(const char *url) {
        document=doc_new(url); const char *html="<!doctype html><html><body></body></html>";
        html_parse(document,html,strlen(html)); JsHost host{}; context=js_new(document,&host);
    }
    JsCtx *frame(const char *url, const char *allow=nullptr, const char *sandbox=nullptr, JsCtx *parent=nullptr) {
        if (!parent) parent=context;
        Node *el=node_new_element(parent->doc,"iframe",NS_HTML); node_set_attr(el,"src",url);
        if (allow) node_set_attr(el,"allow",allow); if (sandbox) node_set_attr(el,"sandbox",sandbox);
        node_append(parent->doc->body,el);
        Document *d=doc_new(url); const char *html="<!doctype html><html><body></body></html>";
        html_parse(d,html,strlen(html)); JsHost host{}; return js_new_ex(d,&host,parent,el);
    }
    ~Fixture() { if(context) js_free(context); doc_free(document); }
};
static bool permission(JsCtx *c, const char *name, const char *expected) {
    std::string script="globalThis.permissionResult='unset';navigator.permissions.query({name:'"+std::string(name)+"'}).then(p=>permissionResult=p.state);true";
    if (!value(c,script.c_str())) return false; pump(c);
    return value(c,("permissionResult==='"+std::string(expected)+"'").c_str());
}
int main(int argc, char **argv) {
    (void)argc; setenv("LUMEN_PERMISSIONS","grant",1);
    dom_init();net_init(2);font_init();js_global_init(argv[0]);
    {
        Fixture f("http://untrusted.example/");
        CHECK("insecure origins do not expose mediaDevices", value(f.context,"navigator.mediaDevices === undefined"));
        CHECK("insecure origins cannot obtain camera permission",permission(f.context,"camera","denied"));
        CHECK("insecure origins cannot obtain geolocation permission",permission(f.context,"geolocation","denied"));
        value(f.context,"globalThis.isSecureContext=true;true");
        CHECK("script cannot override native secure context decision",permission(f.context,"microphone","denied"));
        JsCtx *child=f.frame("https://capture.example/","camera *; microphone *");
        CHECK("HTTPS child of insecure ancestor stays blocked",permission(child,"camera","denied"));
    }
    {
        Fixture f("http://127.0.0.1:8765/");
        CHECK("loopback development origin exposes camera API",value(f.context,"isSecureContext && typeof navigator.mediaDevices.getUserMedia==='function'"));
        CHECK("trusted loopback can ask for microphone",permission(f.context,"microphone","granted"));
    }
    {
        Fixture f("https://capture.example/");
        CHECK("HTTPS top level keeps supported camera API",value(f.context,"typeof navigator.mediaDevices.getUserMedia==='function'"));
        JsCtx *same=f.frame("https://capture.example/child");
        CHECK("same-origin frame inherits camera permission",permission(same,"camera","granted"));
        JsCtx *cross=f.frame("https://embedded.example/");
        CHECK("cross-origin frame needs explicit camera delegation",permission(cross,"camera","denied"));
        CHECK("cross-origin frame needs explicit location delegation",permission(cross,"geolocation","denied"));
        JsCtx *delegated=f.frame("https://embedded.example/","camera; microphone; geolocation");
        CHECK("explicit frame source delegation enables camera",permission(delegated,"camera","granted"));
        CHECK("delegation is feature specific",permission(delegated,"display-capture","denied"));
        JsCtx *denied=f.frame("https://capture.example/denied","camera 'none'; microphone 'none'");
        CHECK("explicit none also disables same-origin camera",permission(denied,"camera","denied"));
        JsCtx *sandbox=f.frame("https://capture.example/sandbox","camera *; microphone *","allow-scripts");
        CHECK("opaque sandbox origin cannot request capture",permission(sandbox,"camera","denied"));
        JsCtx *nested=f.frame("https://nested.example/","camera *",nullptr,denied);
        CHECK("descendant cannot override ancestor capture denial",permission(nested,"camera","denied"));
        CHECK("unimplemented signal processing is not advertised",value(f.context,"!navigator.mediaDevices.getSupportedConstraints().echoCancellation && !navigator.mediaDevices.getSupportedConstraints().noiseSuppression && !navigator.mediaDevices.getSupportedConstraints().autoGainControl"));
    }
    {
        Fixture f("https://capture.example/");
        f.document->permissions_policy=strdup("camera=(), microphone=(self)");
        CHECK("response policy disables top-level camera",permission(f.context,"camera","denied"));
        CHECK("response policy self retains top-level microphone",permission(f.context,"microphone","granted"));
        JsCtx *child=f.frame("https://embedded.example/","camera *; microphone *");
        CHECK("frame allow cannot override response policy",permission(child,"camera","denied"));
        CHECK("response self excludes cross-origin microphone",permission(child,"microphone","denied"));
        free(f.document->permissions_policy);f.document->permissions_policy=strdup("camera=(self \"https://embedded.example\")");
        CHECK("response origin plus frame delegation grants camera",permission(child,"camera","granted"));
        CHECK("real MediaStream types remain available without RTC transport",value(f.context,"typeof MediaStream==='function' && typeof MediaStreamTrack==='function'"));
        CHECK("network WebRTC constructors are not falsely exposed",value(f.context,"typeof RTCPeerConnection==='undefined' && typeof RTCDataChannel==='undefined'"));
        CHECK("capability descriptor records unsupported transport",value(f.context,"navigator.lumenCapabilities.webrtcNetworkTransport===false && navigator.lumenCapabilities.experimentalRTC==='disabled' && Object.isFrozen(navigator.lumenCapabilities)"));
    }
    {
        setenv("LUMEN_EXPERIMENTAL_RTC_LOOPBACK","1",1);
        Fixture f("https://capture.example/");
        CHECK("RTC conformance shim requires explicit experimental opt-in",value(f.context,"typeof RTCPeerConnection==='function' && navigator.lumenCapabilities.experimentalRTC==='in-process-loopback' && navigator.lumenCapabilities.webrtcNetworkTransport===false"));
        unsetenv("LUMEN_EXPERIMENTAL_RTC_LOOPBACK");
    }
    {
        Fixture f("https://capture.example/"); js_window_activate(f.context);
        CHECK("screen request begins",value(f.context,"globalThis.screenTrack=null;navigator.mediaDevices.getDisplayMedia({video:true}).then(s=>screenTrack=s.getVideoTracks()[0]);true"));pump(f.context);
        CHECK("screen source is genuinely active",cap_screen_active() && value(f.context,"screenTrack && screenTrack.readyState==='live'"));
        CHECK("clone preserves source when original stops",value(f.context,"globalThis.screenClone=screenTrack.clone();screenTrack.stop();screenClone.readyState==='live' && screenClone.getSettings().displaySurface==='browser'") && cap_screen_active());
        value(f.context,"screenClone.stop();true");
        CHECK("stopping final clone releases native capture",!cap_screen_active());
        js_window_activate(f.context);
        value(f.context,"navigator.mediaDevices.getDisplayMedia({video:true}).then(s=>screenTrack=s.getVideoTracks()[0]);true");pump(f.context);
        CHECK("second screen source starts",cap_screen_active());
        js_free(f.context);f.context=nullptr;
        CHECK("page teardown releases un-stopped native source",!cap_screen_active());
        for(int id=1;id<=8;++id)cap_cam_close(id); /* avoid leaking the intentionally failing old implementation */
    }
    net_shutdown(); printf("RESULTS capture-policy %s (%d/%d checks)\n",failures?"FAIL":"PASS",checks-failures,checks);return failures?1:0;
}
