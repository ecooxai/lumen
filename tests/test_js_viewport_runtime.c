#include "../src/paint/paint.h"
#include "../src/net/net.h"
#include "../src/js/js.h"
#include <assert.h>
#include <string.h>

typedef struct ViewportFixture {
    float width, height, dpr, scroll_x, scroll_y;
    MediaCtx media;
    unsigned syncs;
} ViewportFixture;

static void viewport(void *userdata, float *width, float *height,
                     float *scroll_x, float *scroll_y, float *dpr) {
    ViewportFixture *fixture = userdata;
    *width = fixture->width;
    *height = fixture->height;
    *scroll_x = fixture->scroll_x;
    *scroll_y = fixture->scroll_y;
    *dpr = fixture->dpr;
}

static void sync_viewport(void *userdata, Document *document, bool layout) {
    ViewportFixture *fixture = userdata;
    (void)document;
    assert(layout);
    fixture->media.vw = fixture->width;
    fixture->media.vh = fixture->height;
    fixture->media.dpr = fixture->dpr;
    fixture->syncs++;
}

static void expect(JsCtx *context, Document *document, const char *expression) {
    char script[4096];
    snprintf(script, sizeof script,
             "document.documentElement.setAttribute('data-result', String(Boolean(%s)))", expression);
    node_set_attr(document->html, "data-result", "unset");
    js_eval(context, script, "viewport:assert");
    const char *result = node_attr(document->html, "data-result");
    if (!result || strcmp(result, "true")) {
        fprintf(stderr, "FAILED: %s\n", expression);
        abort();
    }
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init();
    net_init(1);
    font_init();
    js_global_init(argv[0]);
    ViewportFixture fixture = { .width = 412, .height = 800, .dpr = 2,
                                .media = { .vw = 412, .vh = 800, .dpr = 2 } };
    JsHost host = { .ud = &fixture, .media = &fixture.media,
                    .viewport = viewport, .sync = sync_viewport };
    Document *document = doc_new("https://viewport.test/");
    const char *html = "<!doctype html><html><head></head><body></body></html>";
    html_parse(document, html, strlen(html));
    JsCtx *context = js_new(document, &host);
    expect(context, document, "visualViewport instanceof EventTarget && visualViewport instanceof VisualViewport");
    js_eval(context,
            "globalThis.order = []; globalThis.viewportErrors = [];"
            "globalThis.widthQuery = matchMedia('(min-width: 600px)');"
            "globalThis.dprQuery = matchMedia('(min-resolution: 3dppx)');"
            "addEventListener('resize', event => {"
            "order.push('window');"
            "if (!event.isTrusted || event.target !== window || event.bubbles || event.cancelable) viewportErrors.push('window');"
            "});"
            "visualViewport.addEventListener('resize', event => {"
            "order.push('visual');"
            "if (!event.isTrusted || event.target !== visualViewport) viewportErrors.push('visual');"
            "});"
            "widthQuery.onchange = event => { order.push('width:' + event.matches); if (!event.isTrusted) viewportErrors.push('media'); };"
            "dprQuery.addListener(event => order.push('dpr:' + event.matches));",
            "viewport:setup");
    js_tick(context);
    expect(context, document, "order.length === 0");
    assert(fixture.syncs == 0);
    fixture.width = 800;
    fixture.height = 412;
    js_eval(context, "setTimeout(() => order.push('timer'), 0); requestAnimationFrame(() => order.push('raf'));", "viewport:tasks");
    js_tick(context);
    expect(context, document, "order.join(',') === 'timer,window,visual,width:true,raf' && viewportErrors.length === 0");
    expect(context, document, "innerWidth === 800 && visualViewport.width === 800 && visualViewport.height === 412");
    assert(fixture.syncs == 1);
    js_eval(context, "order.length = 0", "viewport:reset");
    for (int iteration = 0; iteration < 100; iteration++) js_tick(context);
    expect(context, document, "order.length === 0");
    assert(fixture.syncs == 1);
    fixture.scroll_y = 123;
    fixture.scroll_x = 12;
    js_tick(context);
    expect(context, document, "visualViewport.pageTop === 123 && visualViewport.pageLeft === 12 && order.length === 0");
    assert(fixture.syncs == 1);
    fixture.dpr = 3;
    js_tick(context);
    expect(context, document, "order.join(',') === 'dpr:true' && visualViewport.scale === 1 && devicePixelRatio === 3");
    assert(fixture.syncs == 2);
    js_eval(context, "order.length = 0", "viewport:reset");
    fixture.height = 240;
    js_tick(context);
    expect(context, document, "order.join(',') === 'window,visual' && visualViewport.height === 240");
    js_eval(context, "order.length = 0", "viewport:reset");
    fixture.height = 412;
    js_tick(context);
    expect(context, document, "order.join(',') === 'window,visual' && visualViewport.height === 412");
    assert(fixture.syncs == 4);
    expect(context, document, "viewportErrors.length === 0");
    js_free(context);
    doc_free(document);
    puts("viewport runtime assertions passed");
    return 0;
}
