/* Native host-to-JS capability regression: desktop, touch, hybrid and iframe. */
#include "../src/js/js_int.h"
#include "../src/css/css.h"
#include <cstdio>
#include <cstring>
extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { bool ok_ = (condition); ++checks; printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); if (!ok_) ++failures; } while (0)
static bool value(JsCtx *c, const char *source) {
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    return v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) && script->Run(ctx).ToLocal(&result) && result->BooleanValue(iso);
}
struct HostState { MediaCtx media; DeviceInfo *device; };
static void viewport(void *ud, float *w, float *h, float *x, float *y, float *dpr) {
    HostState *s = (HostState *)ud; *w = s->media.vw; *h = s->media.vh; *dpr = s->media.dpr; *x = *y = 0;
}
static void device(void *ud, DeviceInfo *out) { *out = *((HostState *)ud)->device; }
static JsHost host(HostState *s) { JsHost h{}; h.ud = s; h.media = &s->media; h.viewport = viewport; h.device_info = device; return h; }
static void inputs(HostState *s) { s->media.touch = s->device->max_touch_points > 0; s->media.any_fine = s->device->any_fine; }

int main(int argc, char **argv) {
    (void)argc; dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    const char *html = "<!doctype html><html><body></body></html>";
    Document *d = doc_new("https://capabilities.example/"); html_parse(d, html, strlen(html));
    DeviceInfo info = {1920, 1080, 1920, 1040, 1280, 800, DEVICE_LANDSCAPE_PRIMARY, 0, 0, true};
    HostState s = {{1280, 756, 2, false}, &info}; inputs(&s);
    JsHost h = host(&s); JsCtx *c = js_new(d, &h);
    CHECK("desktop keeps fine pointer and hover", value(c, "navigator.maxTouchPoints === 0 && matchMedia('(pointer: fine)').matches && matchMedia('(hover: hover)').matches"));
    CHECK("screen uses host display instead of a fabricated minimum", value(c, "screen.width === 1920 && screen.height === 1080 && screen.availWidth === 1920 && screen.availHeight === 1040"));
    CHECK("outer window includes real browser chrome", value(c, "innerWidth === 1280 && innerHeight === 756 && outerWidth === 1280 && outerHeight === 800"));
    CHECK("screen orientation is an event target", value(c, "screen.orientation instanceof EventTarget && typeof screen.orientation.removeEventListener === 'function'"));
    CHECK("listeners are installed", value(c,
        "globalThis.orientationEvents=[]; globalThis.orientationHandlerEvents=0; globalThis.resizeEvents=0; globalThis.hoverEvents=[];"
        "globalThis.orientationObject=screen.orientation;"
        "screen.orientation.addEventListener('change',e=>orientationEvents.push([e.target.type,e.target.angle]));"
        "screen.orientation.onchange=()=>orientationHandlerEvents++;"
        "addEventListener('resize',()=>resizeEvents++);"
        "globalThis.hoverQuery=matchMedia('(any-hover: hover)'); hoverQuery.addEventListener('change',e=>hoverEvents.push(e.matches)); true"));

    info = {402, 874, 402, 874, 402, 874, DEVICE_PORTRAIT_PRIMARY, 0, 0, false};
    device_apple_input(&info, true, false, false); inputs(&s); s.media.vw=402; s.media.vh=734; s.media.dpr=3;
    js_viewport_changed(c);
    CHECK("phone reports touch and no primary hover", value(c, "navigator.maxTouchPoints === 5 && matchMedia('(pointer: coarse)').matches && matchMedia('(hover: none)').matches && !matchMedia('(hover)').matches"));
    CHECK("phone screen remains larger than safe page viewport", value(c, "screen.width === 402 && screen.height === 874 && innerWidth === 402 && innerHeight === 734 && outerHeight === 874 && devicePixelRatio === 3"));
    CHECK("CSS resolution agrees with native DPR", value(c, "matchMedia('(resolution: 3dppx)').matches && !matchMedia('(resolution: 2dppx)').matches"));
    CHECK("portrait orientation updates live", value(c, "screen.orientation === orientationObject && screen.orientation.type === 'portrait-primary' && screen.orientation.angle === 0"));
    CHECK("orientation and media changes dispatch once", value(c, "orientationEvents.length === 1 && orientationHandlerEvents === 1 && hoverEvents.length === 1 && hoverEvents[0] === false && resizeEvents === 1"));

    device_apple_input(&info, true, false, true); inputs(&s); js_device_changed(c);
    CHECK("trackpad adds fine/hover support while retaining direct touch", value(c, "navigator.maxTouchPoints === 5 && matchMedia('(pointer: coarse)').matches && matchMedia('(hover: none)').matches && matchMedia('(any-pointer: coarse)').matches && matchMedia('(any-pointer: fine)').matches && matchMedia('(any-hover: hover)').matches"));
    CHECK("input attachment fires media change without resize or rotation", value(c, "hoverEvents.length === 2 && hoverEvents[1] === true && resizeEvents === 1 && orientationEvents.length === 1"));
    js_device_changed(c);
    CHECK("unchanged device does not emit duplicate changes", value(c, "hoverEvents.length === 2 && orientationEvents.length === 1 && resizeEvents === 1"));

    info.screen_w=info.avail_w=info.outer_w=874; info.screen_h=info.avail_h=info.outer_h=402;
    info.orientation=DEVICE_LANDSCAPE_PRIMARY; info.angle=90; s.media.vw=750; s.media.vh=338;
    js_viewport_changed(c);
    CHECK("rotated screen uses display coordinates, page retains safe insets", value(c, "screen.width === 874 && screen.height === 402 && innerWidth === 750 && innerHeight === 338 && matchMedia('(orientation: landscape)').matches"));
    CHECK("rotation change carries current type and angle", value(c, "screen.orientation.type === 'landscape-primary' && screen.orientation.angle === 90 && orientationEvents.length === 2 && orientationEvents[1][0] === 'landscape-primary' && orientationEvents[1][1] === 90 && orientationHandlerEvents === 2"));
    info.orientation=DEVICE_LANDSCAPE_SECONDARY; info.angle=270; js_device_changed(c);
    CHECK("half turn updates orientation without a false resize", value(c, "screen.orientation.type === 'landscape-secondary' && screen.orientation.angle === 270 && orientationEvents.length === 3 && resizeEvents === 2"));

    Node *frame = node_new_element(d, "iframe", NS_HTML); node_append(d->body, frame);
    Document *fd = doc_new("https://capabilities.example/frame"); html_parse(fd, html, strlen(html));
    HostState fs = {{280, 150, 3, false}, &info}; inputs(&fs); JsHost fh=host(&fs);
    JsCtx *child = js_new_ex(fd, &fh, c, frame);
    CHECK("iframe retains its own viewport", value(child, "innerWidth === 280 && innerHeight === 150"));
    CHECK("iframe shares physical display, orientation and touch", value(child, "screen.width === 874 && screen.height === 402 && screen.orientation.angle === 270 && navigator.maxTouchPoints === 5 && outerWidth === 874 && outerHeight === 402"));
    CHECK("iframe listeners are installed", value(child, "globalThis.childOrientationEvents=0; globalThis.childResizeEvents=0; screen.orientation.addEventListener('change',()=>childOrientationEvents++); addEventListener('resize',()=>childResizeEvents++); true"));
    info.orientation=DEVICE_PORTRAIT_SECONDARY; info.angle=180;
    info.screen_w=info.avail_w=info.outer_w=402; info.screen_h=info.avail_h=info.outer_h=874;
    js_device_changed(c);
    CHECK("top-level device changes reach an iframe without resizing it", value(child, "screen.orientation.type === 'portrait-secondary' && screen.orientation.angle === 180 && childOrientationEvents === 1 && childResizeEvents === 0 && matchMedia('(orientation: landscape)').matches"));

    device_apple_input(&info, true, true, false); inputs(&s); inputs(&fs); js_device_changed(c); js_device_changed(child);
    CHECK("same UIKit iPad idiom on Mac does not report a touchscreen", value(c, "navigator.maxTouchPoints === 0 && matchMedia('(pointer: fine)').matches && matchMedia('(hover: hover)').matches && !matchMedia('(any-pointer: coarse)').matches"));
    CHECK("iframe sees the same runtime Mac capabilities", value(child, "navigator.maxTouchPoints === 0 && matchMedia('(pointer: fine)').matches"));
    js_free(c); doc_free(d); net_shutdown();
    printf("RESULTS js-device-capabilities %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
