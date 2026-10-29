#include "layout/layout.h"
#include "js/jsglue.h"
#include <math.h>

/* These layout-only tests do not install the JS animation event hooks. */
void js_anim_event(Node *n, const char *type, const char *name, double delay, double elapsed, bool anim) {
    (void)n; (void)type; (void)name; (void)delay; (void)elapsed; (void)anim;
}
bool js_anim_cancel(Node *n, const char *name) { (void)n; (void)name; return false; }

static int failures;
static void check(const char *name, const char *rules, const char *expected) {
    Document *d = doc_new("http://fixture.test/");
    const char *html = "<body><div id=container><div id=player><div id=video></div></div>"
        "<div id=wrapper><div id=inner><div id=overlay><div id=button>Control</div></div></div></div></div></body>";
    html_parse(d, html, strlen(html));
    const char *base = "body{margin:0}#container{position:fixed;top:40px;width:400px;height:240px;z-index:2}"
        "#player{position:relative;width:400px;height:240px;z-index:0}"
        "#video{position:absolute;inset:0;z-index:10}"
        "#overlay{position:absolute;inset:0}#button{width:80px;height:80px}";
    StyleEngine *engine = style_engine_new(d);
    style_engine_add_sheet(engine, css_parse_sheet(base, strlen(base), d->url, 1, &engine->media));
    style_engine_add_sheet(engine, css_parse_sheet(rules, strlen(rules), d->url, 1, &engine->media));
    style_recalc(engine, &d->node, true);
    Layout *layout = layout_new();
    layout_run(layout, d, 540, 984);
    for (int i = 0; i < 2; i++) {
        float scroll = i ? 1200 : 0;
        Box *hit = layout_hit(layout, 20, 60 + scroll, 0, scroll);
        const char *id = hit && hit->node ? node_attr(hit->node, "id") : NULL;
        if (!id || strcmp(id, expected)) {
            fprintf(stderr, "%s scroll %.0f: expected %s, got %s\n", name, scroll, expected, id ? id : "(none)");
            failures++;
        }
    }
    layout_free(layout); style_engine_free(engine); doc_free(d);
}
static void expect_rect(Document *d, const char *id, float sx, float sy, float x, float y) {
    float r[4] = {0};
    if (!jsg_vrect(doc_get_element_by_id(d, id), r, sx, sy) || fabsf(r[0] - x) > .01f || fabsf(r[1] - y) > .01f) {
        fprintf(stderr, "%s viewport rect at scroll %.0f %.0f: expected %.0f %.0f, got %.0f %.0f\n", id, sx, sy, x, y, r[0], r[1]);
        failures++;
    }
}
static void check_viewport_rects(void) {
    Document *d = doc_new("http://fixture.test/");
    const char *html = "<body><div id=fixed><div id=child></div><div id=spacer></div></div><div id=normal></div></body>";
    const char *css = "body{margin:0}#fixed{position:fixed;left:20px;top:40px;width:400px;height:240px;overflow:auto}"
        "#child{margin-left:40px;margin-top:30px;width:80px;height:40px;transform:translate(10px,20px)}"
        "#spacer{width:800px;height:600px}#normal{position:absolute;left:80px;top:400px;width:100px;height:50px}";
    html_parse(d, html, strlen(html));
    StyleEngine *engine = style_engine_new(d);
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), d->url, 1, &engine->media));
    style_recalc(engine, &d->node, true);
    Layout *layout = layout_new(); layout_run(layout, d, 540, 984);
    expect_rect(d, "fixed", 0, 0, 20, 40);
    expect_rect(d, "fixed", 200, 1200, 20, 40);
    expect_rect(d, "child", 0, 0, 70, 90);
    expect_rect(d, "child", 200, 1200, 70, 90);
    expect_rect(d, "normal", 0, 0, 80, 400);
    expect_rect(d, "normal", 200, 1200, -120, -800);
    jsg_set_scroll(doc_get_element_by_id(d, "fixed"), 40, 30);
    expect_rect(d, "child", 200, 1200, 30, 60);
    layout_free(layout); style_engine_free(engine); doc_free(d);
}
int main(void) {
    dom_init(); font_init();
    check("mobile player overlay through three static wrappers", "", "button");
    check("higher underlying context wins", "#player{z-index:1}", "video");
    check("nested higher overlay wins", "#player{z-index:1}#overlay{z-index:2}", "button");
    check("equal layers use document order", "#player,#overlay{z-index:5}", "button");
    check("transparent overlay still receives input", "#overlay{opacity:0}", "button");
    check("pointer events none passes through", "#overlay{pointer-events:none}", "video");
    check("child can restore pointer events", "#overlay{pointer-events:none}#button{pointer-events:auto}", "button");
    check("hidden inline control passes through", "#overlay{visibility:hidden}#button{display:inline;font-size:40px;line-height:60px}", "video");
    check("hidden subtree passes through", "#overlay{visibility:hidden}", "video");
    check("visible child of hidden layer", "#overlay{visibility:hidden}#button{visibility:visible}", "button");
    check("wrapper creates lower opacity layer", "#wrapper{opacity:.5}#player{z-index:1}#overlay{z-index:20}", "video");
    check("wrapper creates transform layer", "#wrapper{transform:translateX(1px)}#player{z-index:1}#overlay{z-index:20}", "video");
    check("translated overlay", "#overlay{transform:translateX(100px)}", "video");
    check("negative layers use numeric order", "#player{z-index:-1}#overlay{z-index:-2}", "video");
    check("negative overlay behind player", "#overlay{z-index:-1}", "video");
    check_viewport_rects();
    if (!failures) puts("positioned hit targets pass at both scroll positions");
    return failures ? 1 : 0;
}
