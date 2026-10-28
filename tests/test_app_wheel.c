/* Exercise the real native input path. The counter measures whether layout
 * hit testing ran; offsets and DOM event targets verify its observable result. */
#include "../src/layout/layout.h"
static unsigned wheel_hit_calls;
static Box *counted_layout_hit(Layout *layout, float x, float y) {
    wheel_hit_calls++;
    return layout_hit(layout, x, y);
}
#define layout_hit counted_layout_hit
#define main lumen_app_main_for_wheel_test
#include "../src/app/main.c"
#undef main
#undef layout_hit

static int checks, failures;
static void wheel_check(bool passed, const char *name) {
    checks++;
    if (!passed) { failures++; fprintf(stderr, "FAIL app-wheel: %s\n", name); }
}

typedef struct WheelFixture { App app; Tab tab; Page page; } WheelFixture;

static void fixture_layout(WheelFixture *f) {
    style_recalc(f->page.e, &f->page.d->node, true);
    layout_run(f->page.L, f->page.d, f->app.vw, f->app.vh - BAR);
    page_scroll(&f->app, 0);
    f->page.scroll_pending = false;
}

static void fixture_init(WheelFixture *f, const char *body, bool events) {
    memset(f, 0, sizeof *f);
    g_compact = true; g_info_h = 0;
    f->app.t = &f->tab; f->app.tabs[0] = &f->tab; f->app.ntabs = 1;
    f->app.vw = 640; f->app.vh = 480; f->app.scale = 1;
    f->tab.cur = &f->page; g_app = &f->app;
    const char *prefix = "<!doctype html><html><head></head><body style='margin:0;height:1800px'>";
    const char *suffix = "</body></html>";
    size_t length = strlen(prefix) + strlen(body) + strlen(suffix);
    char *html = xmalloc(length + 1);
    snprintf(html, length + 1, "%s%s%s", prefix, body, suffix);
    f->page.d = doc_new("https://wheel-routing.example/");
    html_parse(f->page.d, html, length); free(html);
    f->page.e = style_engine_new(f->page.d);
    f->page.e->media = (MediaCtx){.vw = 640, .vh = 480 - BAR, .dpr = 1};
    f->page.L = layout_new(); f->page.L->dpr = 1;
    fixture_layout(f);
    if (events) {
        JsHost host = {.media = &f->page.e->media};
        f->page.js = js_new(f->page.d, &host);
        js_eval(f->page.js,
            "document.addEventListener('scroll',function(e){document.documentElement.setAttribute('data-scroll-target',e.target.id)},true)",
            "wheel-target-test");
    }
}

static void fixture_free(WheelFixture *f) {
    js_release_pins();
    js_free(f->page.js);
    layout_free(f->page.L);
    style_engine_free(f->page.e);
    doc_free(f->page.d);
    g_app = NULL;
}

static bool wheel_at(WheelFixture *f, float x, float y, float dx, float dy) {
    wheel_hit_calls = 0;
    return wheel_apply(&f->app, x, BAR + y, dx, dy);
}

static bool target_is(WheelFixture *f, const char *id) {
    const char *value = node_attr(f->page.d->html, "data-scroll-target");
    return value && !strcmp(value, id);
}

static void page_only(void) {
    WheelFixture f;
    fixture_init(&f, "<p>Plain document content</p>", false);
    wheel_check(wheel_at(&f, 50, 40, 0, 3.2f), "fractional wheel scrolls ordinary page");
    wheel_check(fabsf(f.tab.sy - 3.2f) < .0001f, "fractional page offset is preserved");
    wheel_check(wheel_hit_calls == 0, "ordinary page avoids full-tree hit search");
    wheel_check(f.page.scroll_pending, "ordinary document scroll event remains queued");
    wheel_check(!wheel_at(&f, -1, 40, 0, 20), "outside-viewport input remains ignored");
    wheel_check(wheel_hit_calls == 0 && fabsf(f.tab.sy - 3.2f) < .0001f, "outside input neither searches nor changes scroll");
    fixture_free(&f);
}

static void no_wheel_container(const char *overflow, int content_height) {
    WheelFixture f; char body[512];
    snprintf(body, sizeof body,
        "<div id='box' style='width:200px;height:100px;overflow:%s'><div style='height:%dpx'>Content</div></div>",
        overflow, content_height);
    fixture_init(&f, body, false);
    Node *box = doc_get_element_by_id(f.page.d, "box");
    wheel_check(wheel_at(&f, 30, 30, 0, 40), "nonconsuming element allows page scrolling");
    wheel_check(wheel_hit_calls == 0, "hidden clip and fitting auto boxes avoid hit search");
    wheel_check(box && box->scroll_y == 0 && f.tab.sy == 40, "nonconsuming box offset is unchanged");
    fixture_free(&f);
}

static void nested_containers(void) {
    WheelFixture f;
    fixture_init(&f,
        "<div id='outer' style='width:220px;height:180px;overflow:auto'>"
        "<div id='inner' style='width:160px;height:90px;overflow:auto'><div style='height:400px;width:400px'>Inner</div></div>"
        "<div style='height:600px'>Outer</div></div>", true);
    Node *outer = doc_get_element_by_id(f.page.d, "outer"), *inner = doc_get_element_by_id(f.page.d, "inner");
    wheel_check(wheel_at(&f, 40, 30, 0, 40), "nested wheel is handled");
    wheel_check(wheel_hit_calls > 0, "scrollable nested page retains hit testing");
    wheel_check(inner && outer && inner->scroll_y == 40 && outer->scroll_y == 0 && f.tab.sy == 0, "innermost scrollable element receives delta");
    wheel_check(target_is(&f, "inner"), "inner scroll event keeps its DOM target");
    if (inner && inner->box) inner->scroll_y = LMAX(0, ((Box *)inner->box)->scroll_h - ((Box *)inner->box)->h);
    wheel_check(wheel_at(&f, 40, 30, 0, 40), "saturated inner scroll chains to outer");
    wheel_check(outer && outer->scroll_y == 40 && f.tab.sy == 0, "outer offset changes without moving page");
    wheel_check(target_is(&f, "outer"), "chained scroll event targets outer element");
    wheel_check(wheel_at(&f, 400, 30, 0, 40), "wheel outside containers reaches page");
    wheel_check(wheel_hit_calls > 0 && f.tab.sy == 40 && outer && outer->scroll_y == 40, "positive layout flag does not redirect unrelated wheel input");
    fixture_free(&f);
}

static void horizontal_container(void) {
    WheelFixture f;
    fixture_init(&f,
        "<div id='horizontal' style='width:120px;height:100px;overflow-x:auto;overflow-y:hidden'>"
        "<div style='width:500px;height:50px'>Wide content</div></div>", true);
    Node *box = doc_get_element_by_id(f.page.d, "horizontal");
    wheel_check(wheel_at(&f, 25, 25, 35, 0), "horizontal wheel remains handled");
    wheel_check(wheel_hit_calls > 0 && box && box->scroll_x == 35 && box->scroll_y == 0, "horizontal container keeps exact offset");
    wheel_check(f.tab.sy == 0 && target_is(&f, "horizontal"), "horizontal event target and page position are preserved");
    fixture_free(&f);
}

static void fixed_container(void) {
    WheelFixture f;
    fixture_init(&f,
        "<div id='fixed' style='position:fixed;top:20px;left:15px;width:160px;height:100px;overflow:auto;z-index:2'>"
        "<div style='height:500px'>Fixed content</div></div>", true);
    Node *box = doc_get_element_by_id(f.page.d, "fixed");
    page_scroll(&f.app, 200);
    wheel_check(wheel_at(&f, 50, 40, 0, 40), "fixed-position scroll container remains hittable after page scroll");
    wheel_check(wheel_hit_calls > 0 && box && box->scroll_y == 40 && f.tab.sy == 200, "fixed container consumes delta without moving page");
    wheel_check(target_is(&f, "fixed"), "fixed container scroll event keeps its DOM target");
    fixture_free(&f);
}

static void relayout_invalidation(void) {
    WheelFixture f;
    fixture_init(&f,
        "<div id='box' style='width:200px;height:100px;overflow:visible'><div id='content' style='height:500px'>Content</div></div>",
        true);
    Node *box = doc_get_element_by_id(f.page.d, "box"), *content = doc_get_element_by_id(f.page.d, "content");
    wheel_check(wheel_at(&f, 30, 30, 0, 40) && wheel_hit_calls == 0 && f.tab.sy == 40, "initial non-scrollable layout skips search");
    node_set_attr(box, "style", "width:200px;height:100px;overflow:auto");
    fixture_layout(&f);
    wheel_check(wheel_at(&f, 30, 30, 0, 40) && wheel_hit_calls > 0, "relayout enables element hit testing after overflow change");
    wheel_check(box->scroll_y == 40 && f.tab.sy == 0 && target_is(&f, "box"), "new scroll container receives the delta and event");
    node_set_attr(content, "style", "height:20px");
    fixture_layout(&f);
    wheel_check(wheel_at(&f, 30, 30, 0, 40) && wheel_hit_calls == 0, "relayout removes obsolete scroll search after content shrinks");
    wheel_check(box->scroll_y == 0 && f.tab.sy == 40, "shrunk container clamps offset and restores page scrolling");
    fixture_free(&f);
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(1); font_init(); js_global_init(argv[0]);
    page_only(); no_wheel_container("hidden", 500); no_wheel_container("clip", 500);
    no_wheel_container("auto", 20); nested_containers(); horizontal_container(); fixed_container(); relayout_invalidation();
    net_shutdown();
    printf("RESULTS app-wheel %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
