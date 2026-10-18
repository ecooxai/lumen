#include "../src/layout/layout.h"
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <math.h>
#include <stdio.h>

static int failures, checks;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); failures++; } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < .02f; }
static Node *element(Document *doc, const char *id) {
    Node *n = doc_get_element_by_id(doc, id); CHECK(n && n->box); return n;
}
static void relative_rect(Node *n, Node *port, float x, float y) {
    float r[4], p[4]; CHECK(jsg_vrect(n, r)); CHECK(jsg_vrect(port, p));
    if (!near(r[0] - p[0], x) || !near(r[1] - p[1], y))
        fprintf(stderr, "%s relative to %s: got %g,%g expected %g,%g\n", node_attr(n, "id"), node_attr(port, "id"), r[0]-p[0], r[1]-p[1], x, y);
    CHECK(near(r[0] - p[0], x)); CHECK(near(r[1] - p[1], y));
}
static void painted(const DisplayList *dl, Color color, float x, float y) {
    const DItem *item = NULL;
    for (int i = 0; i < dl->items.n; i++) if (dl->items.v[i].op == DO_RECT && dl->items.v[i].color == color) { item = &dl->items.v[i]; break; }
    CHECK(item);
    if (item) { CHECK(near(item->x, x)); CHECK(near(item->y, y)); }
}

/* Reproduce the page-scroll pixel reuse path. Pinned content includes ink
   outside its border box, so its shadow must match a complete repaint. */
static void damage_and_lifecycle(void) {
    const char *html = "<html><body><div id='wrap'><div id='space'></div><div id='pin'></div></div></body></html>";
    const char *css = "html,body{margin:0;padding:0}#wrap{height:600px}#space{height:40px}"
        "#pin{position:sticky;top:10px;margin-left:30px;width:100px;height:20px;background:#123456;box-shadow:0 4px 8px 3px #000}";
    Document *d = doc_new("https://sticky.example/damage"); html_parse(d, html, strlen(html));
    StyleEngine *se = style_engine_new(d); se->media = (MediaCtx){200, 120, 1, false};
    style_engine_add_sheet(se, css_parse_sheet(css, strlen(css), d->url, 1, &se->media));
    style_recalc(se, &d->node, true);
    Layout *L = layout_new(); layout_run(L, d, 200, 120);
    CHECK(d->layout == L);
    DisplayList dl = {0}; dl_build(&dl, L, 0, 50, 200, 120);
    Canvas partial, full; canvas_init(&partial, 200, 120, 1); canvas_init(&full, 200, 120, 1);
    raster(&partial, &dl, RGBA(255,255,255,255));
    dl_build(&dl, L, 0, 60, 200, 120);
    CHECK(near(L->scroll_y, 60));
    raster(&full, &dl, RGBA(255,255,255,255));
    int shift = 10, rows = partial.h - shift;
    memmove(partial.px, partial.px + shift * partial.stride, (size_t)rows * partial.stride * sizeof *partial.px);
    raster_rect(&partial, &dl, RGBA(255,255,255,255), 0, rows, partial.w, partial.h);
    int x0 = LMAX(0, (int)floorf(dl.fix[0])), y0 = LMAX(0, (int)floorf(dl.fix[1]) - shift);
    int x1 = LMIN(partial.w, (int)ceilf(dl.fix[2])), y1 = LMIN(partial.h, (int)ceilf(dl.fix[3]));
    CHECK(dl.has_fixed && x1 > x0 && y1 > y0);
    raster_rect(&partial, &dl, RGBA(255,255,255,255), x0, y0, x1, y1);
    int changed = 0;
    for (int i = 0; i < partial.w * partial.h; i++) if (partial.px[i] != full.px[i]) changed++;
    if (changed) fprintf(stderr, "sticky scroll reuse differs from a full repaint at %d pixels\n", changed);
    CHECK(changed == 0);
    canvas_free(&partial); canvas_free(&full); dl_clear(&dl); vec_free(dl.items);

    /* Reusing a Layout must detach the old document before either is freed. */
    Document *next = doc_new("https://sticky.example/replacement"); html_parse(next, html, strlen(html));
    StyleEngine *next_se = style_engine_new(next); style_recalc(next_se, &next->node, true);
    layout_run(L, next, 200, 120);
    CHECK(!d->layout); CHECK(next->layout == L);
    layout_free(L); CHECK(!next->layout);
    style_engine_free(se); doc_free(d); style_engine_free(next_se); doc_free(next);
}

/* Mirrors tests/js/sticky-grid-scrollport.html for comparison with WebKit. */
static void grid_containing_block(void) {
    const char *html = "<html><body>"
        "<div id='direct' class='port grid'><div id='main' class='main'></div><div id='aside' class='aside'></div><div class='wide'></div></div>"
        "<div id='separate' class='port'><div id='separate-grid' class='grid'><div id='separate-main' class='main'></div></div><div class='wide'></div></div>"
        "</body></html>";
    const char *css = "html,body{margin:0}.port{width:430px;height:100px;overflow:auto hidden}"
        ".grid{display:grid;grid-template-columns:100%;grid-template-rows:100px}.grid>*{grid-row:1;grid-column:1}"
        ".main{position:sticky;inset:0 auto auto 0;width:430px;height:100px;background:#123456}"
        ".aside{position:sticky;inset:0 auto auto 0;width:326.8px;height:100px;transform:translateX(-100%);background:#654321}"
        ".wide{width:756.8px;height:1px;pointer-events:none}"
        "#separate-grid{width:430px;grid-template-columns:215px 215px}#separate-grid .main{width:100px}";
    Document *d = doc_new("https://sticky.example/grid"); html_parse(d, html, strlen(html));
    StyleEngine *se = style_engine_new(d); se->media = (MediaCtx){430, 400, 1, false};
    style_engine_add_sheet(se, css_parse_sheet(css, strlen(css), d->url, 1, &se->media));
    style_recalc(se, &d->node, true);
    Layout *L = layout_new(); layout_run(L, d, 430, 400);
    Node *direct = element(d, "direct"), *main = element(d, "main"), *aside = element(d, "aside");
    CHECK(jsg_set_scroll(direct, 326.8f, 0));
    relative_rect(main, direct, 0, 0); relative_rect(aside, direct, -326.8f, 0);
    Node *separate = element(d, "separate"), *separate_main = element(d, "separate-main");
    CHECK(jsg_set_scroll(separate, 250, 0)); relative_rect(separate_main, separate, 0, 0);
    DisplayList dl = {0}; dl_build(&dl, L, 0, 0, 430, 400);
    painted(&dl, RGBA(0x12,0x34,0x56,255), 0, 0);
    Box *hit = layout_hit(L, 200, 50); CHECK(hit && hit->node == main);
    dl_clear(&dl); vec_free(dl.items); layout_free(L); style_engine_free(se); doc_free(d);
}

int main(void) {
    dom_init(); font_init();
    const char *html = "<html><body>"
        "<div id='horizontal'><div id='track'><div id='main'></div><div id='aside'></div></div></div>"
        "<div id='direct'><div id='direct-sticky'></div><div id='direct-spacer'></div></div>"
        "<div id='vertical'><div id='vwrap'><div id='before'></div><div id='vsticky'></div></div><div id='after'></div></div>"
        "<div id='nested'><div id='nwrap'><div id='nspace'></div><div id='parent'><div id='child'></div></div></div></div>"
        "<div id='auto'><div id='auto-wrap'><div id='auto-sticky'></div></div></div>"
        "<div id='bottom'><div id='bottom-wrap'><div id='bottom-sticky'></div></div></div>"
        "<div id='oversized'><div id='oversized-wrap'><div id='oversized-sticky'></div></div></div>"
        "<div id='viewport-wrap'><div id='viewport-sticky'></div></div>"
        "</body></html>";
    const char *css = "body{margin:0}"
        "#horizontal{width:430px;height:100px;overflow:auto}"
        "#track{display:grid;width:756.8px;height:100px;grid-template-columns:756.8px;grid-template-rows:100px}"
        "#main,#aside{position:sticky;left:0;grid-column:1;grid-row:1;height:100px}"
        "#main{width:430px;background:#123456;z-index:1}"
        "#aside{width:326.8px;transform:translateX(-100%);background:#654321;z-index:2}"
        "#direct{width:430px;height:100px;overflow:auto}"
        "#direct-sticky{position:sticky;left:0;top:0;width:430px;height:20px;background:#abcdef}"
        "#direct-spacer{width:756.8px;height:400px}"
        "#vertical{width:200px;height:100px;overflow:auto}#vwrap{height:200px}"
        "#before{height:40px}#vsticky{position:sticky;top:10px;height:30px;background:#345678}#after{height:300px}"
        "#nested{width:200px;height:100px;overflow:auto}#nwrap{height:500px}#nspace{height:40px}"
        "#parent{position:sticky;top:0;height:100px;padding-top:20px;box-sizing:border-box}"
        "#child{position:sticky;top:0;height:30px;background:#456789}"
        "#auto{width:200px;height:100px;overflow:auto}#auto-wrap{width:600px;height:400px}"
        "#auto-sticky{position:sticky;width:50px;height:30px}"
        "#bottom{width:200px;height:100px;overflow:auto}"
        "#bottom-wrap{display:flex;flex-direction:column;justify-content:flex-end;height:300px}"
        "#bottom-sticky{position:sticky;bottom:10px;flex:none;height:30px;width:100px}"
        "#oversized{width:200px;height:100px;overflow:auto}#oversized-wrap{height:500px}"
        "#oversized-sticky{position:sticky;top:10px;bottom:10px;height:150px;width:100px}"
        "#viewport-wrap{height:1400px}#viewport-sticky{position:sticky;top:0;height:30px;background:#789abc}";
    Document *d = doc_new("https://sticky.example/"); html_parse(d, html, strlen(html));
    StyleEngine *se = style_engine_new(d); se->media = (MediaCtx){430, 932, 1, false};
    style_engine_add_sheet(se, css_parse_sheet(css, strlen(css), d->url, 1, &se->media));
    style_recalc(se, &d->node, true);
    Layout *L = layout_new(); layout_run(L, d, 430, 932);
    Node *horizontal = element(d, "horizontal"), *main = element(d, "main"), *aside = element(d, "aside");
    float original_x = main->box->x;
    CHECK(jsg_set_scroll(horizontal, 326.8f, 0));
    relative_rect(main, horizontal, 0, 0);
    relative_rect(aside, horizontal, -326.8f, 0);
    CHECK(near(main->box->x, original_x)); /* sticky is a visual offset, not a relayout */
    DisplayList dl = {0}; dl_build(&dl, L, 0, 0, 430, 932);
    painted(&dl, RGBA(0x12,0x34,0x56,255), 0, 0);
    Box *hit = layout_hit(L, 200, 50); CHECK(hit && hit->node == main);
    CHECK(dl.has_fixed); /* viewport-scroll damage must include sticky items */

    Node *direct = element(d, "direct"), *direct_sticky = element(d, "direct-sticky");
    CHECK(jsg_set_scroll(direct, 326.8f, 200));
    relative_rect(direct_sticky, direct, 0, 0); /* containing block is the scrollable content area */
    Node *vertical = element(d, "vertical"), *vsticky = element(d, "vsticky");
    CHECK(jsg_set_scroll(vertical, 0, 20)); relative_rect(vsticky, vertical, 0, 20); /* before threshold */
    CHECK(jsg_set_scroll(vertical, 0, 90)); relative_rect(vsticky, vertical, 0, 10);
    CHECK(jsg_set_scroll(vertical, 0, 250)); relative_rect(vsticky, vertical, 0, -80); /* containing-block bottom */
    Node *nested = element(d, "nested"), *parent = element(d, "parent"), *child = element(d, "child");
    CHECK(jsg_set_scroll(nested, 0, 90));
    relative_rect(parent, nested, 0, 0); relative_rect(child, nested, 0, 20); /* inherit parent's sticky shift once */
    Node *auto_port = element(d, "auto"), *auto_sticky = element(d, "auto-sticky");
    CHECK(jsg_set_scroll(auto_port, 80, 60)); relative_rect(auto_sticky, auto_port, -80, -60);
    Node *bottom = element(d, "bottom"), *bottom_sticky = element(d, "bottom-sticky");
    relative_rect(bottom_sticky, bottom, 0, 60); /* bottom-only sticks upward */
    CHECK(jsg_set_scroll(bottom, 0, 200)); relative_rect(bottom_sticky, bottom, 0, 60);
    Node *oversized = element(d, "oversized"), *oversized_sticky = element(d, "oversized-sticky");
    CHECK(jsg_set_scroll(oversized, 0, 50)); relative_rect(oversized_sticky, oversized, 0, 10);

    /* A translated sticky drawer uses the same offset for painting and targets. */
    node_set_attr(aside, "style", "transform:translateX(-50%)");
    style_recalc(se, &d->node, true); layout_run(L, d, 430, 932);
    relative_rect(aside, horizontal, -163.4f, 0);
    dl_build(&dl, L, 0, 0, 430, 932);
    painted(&dl, RGBA(0x65,0x43,0x21,255), -163.4f, 0);
    hit = layout_hit(L, 50, 50); CHECK(hit && hit->node == aside);

    Node *viewport_sticky = element(d, "viewport-sticky");
    float sy = viewport_sticky->box->y + 100;
    L->scroll_y = sy;
    float r[4]; CHECK(jsg_vrect(viewport_sticky, r)); CHECK(near(r[1] - sy, 0));
    dl_build(&dl, L, 0, sy, 430, 932);
    painted(&dl, RGBA(0x78,0x9a,0xbc,255), 0, 0);
    CHECK(dl.has_fixed && dl.fix[1] <= 0 && dl.fix[3] >= 30);
    hit = layout_hit(L, 10, sy + 10); CHECK(hit && hit->node == viewport_sticky);
    dl_clear(&dl); vec_free(dl.items);
    layout_free(L); style_engine_free(se); doc_free(d);
    damage_and_lifecycle();
    grid_containing_block();
    if (failures) { fprintf(stderr, "%d/%d sticky position failures\n", failures, checks); return 1; }
    printf("sticky position tests ok: %d checks\n", checks); return 0;
}
