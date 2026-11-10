/* Actual radius pixels and hit targets. Expected probe points are independent
 * ellipse geometry, not values computed by the production contains helper.
 * The fragment fixture tests existing first/last flags; upstream -012 separately
 * tests the real inline layout that produces those flags. */
#include "../src/paint/paint.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 320, H = 240 };
static const uint32_t red = 0xffff0000u, blue = 0xff0000ffu, white = 0xffffffffu;
static unsigned checks, failures;
typedef struct {
    Document *doc; StyleEngine *engine; Layout *layout;
    Node *parent, *target, *child, *image;
    DisplayList list; Canvas canvas;
} Fixture;
static void yes(bool condition, const char *label) {
    checks++;
    if (!condition) { failures++; fprintf(stderr, "FAIL radius rendering: %s\n", label); }
}
static void pixel(Fixture *f, int x, int y, uint32_t expected, const char *label) {
    uint32_t actual = canvas_row(&f->canvas, y)[x];
    checks++;
    if (actual != expected) {
        failures++; fprintf(stderr, "FAIL radius rendering: %s (%d,%d): %08x expected %08x\n",
                            label, x, y, actual, expected);
    }
}
static void hit(Fixture *f, Node *node, int x, int y, bool expected, const char *label) {
    Box *b = layout_hit(f->layout, x + .5f, y + .5f, 0, 0);
    yes((b && b->node == node) == expected, label);
}
static void recalc(Fixture *f) {
    style_recalc(f->engine, &f->doc->node, false);
    layout_run(f->layout, f->doc, W, H);
}
static void draw(Fixture *f) {
    dl_build(&f->list, f->layout, 0, 0, W, H);
    raster(&f->canvas, &f->list, white);
}
static void setup(Fixture *f, const char *style) {
    node_set_attr(f->parent, "style", "");
    node_set_attr(f->target, "style", style);
    node_set_attr(f->child, "style", "display:none");
    node_set_attr(f->image, "style", "display:none");
    recalc(f); draw(f);
}
static Fixture create_fixture(void) {
    Fixture f = {0};
    f.doc = doc_new("https://radius-rendering.test/");
    const char *html = "<!doctype html><html><body><div id=parent><div id=target><div id=child></div></div><img id=image></div></body></html>";
    html_parse(f.doc, html, strlen(html));
    f.engine = style_engine_new(f.doc); f.engine->media = (MediaCtx){W, H, 1, false};
    const char *css = "html,body{margin:0;padding:0;border:0}"
        "#parent{position:absolute;left:0;top:0;width:320px;height:240px}"
        "#target{position:absolute;left:20px;top:20px;width:100px;height:100px;background:red}"
        "#child{display:none;position:absolute;left:0;top:0;width:100px;height:100px;background:red}"
        "#image{display:none;position:absolute;left:20px;top:20px;width:200px;height:100px}";
    style_engine_add_sheet(f.engine, css_parse_sheet(css, strlen(css), f.doc->url, 1, &f.engine->media));
    f.parent = doc_get_element_by_id(f.doc, "parent"); f.target = doc_get_element_by_id(f.doc, "target");
    f.child = doc_get_element_by_id(f.doc, "child"); f.image = doc_get_element_by_id(f.doc, "image");
    f.layout = layout_new(); canvas_init(&f.canvas, W, H, 1);
    return f;
}
static void destroy(Fixture *f) {
    canvas_free(&f->canvas); dl_clear(&f->list); free(f->list.items.v);
    layout_free(f->layout); style_engine_free(f->engine); doc_free(f->doc);
}
static bool same_canvas(const Canvas *a, const Canvas *b) {
    if (a->w != b->w || a->h != b->h) return false;
    for (int y = 0; y < a->h; y++)
        if (memcmp(canvas_row(a, y), canvas_row(b, y), (size_t)a->w * sizeof(uint32_t))) return false;
    return true;
}
static void basic_geometry(Fixture *f) {
    setup(f, "border-top-left-radius:80px 20px");
    pixel(f, 30, 35, red, "wide ellipse independently known interior");
    hit(f, f->target, 30, 35, true, "wide ellipse interior is hit");
    setup(f, "border-top-left-radius:20px 60px");
    pixel(f, 24, 35, white, "tall ellipse independently known exterior");
    hit(f, f->target, 24, 35, false, "tall ellipse exterior is not hit");
    setup(f, "width:200px;height:100px;border-radius:50%");
    pixel(f, 40, 32, white, "percentage X/Y use different box dimensions");
    pixel(f, 65, 55, red, "percentage ellipse interior");
    hit(f, f->target, 40, 32, false, "percentage ellipse corner excludes hit");
    hit(f, f->target, 65, 55, true, "percentage ellipse interior accepts hit");
    setup(f, "border-radius:40px / 0px");
    pixel(f, 20, 20, red, "zero vertical radius paints a square corner");
    hit(f, f->target, 20, 20, true, "zero vertical radius has square hit geometry");
    setup(f, "border-radius:100px 100px 0px 0px / 0px 80px 0px 0px");
    pixel(f, 90, 30, red, "original zero-axis X participates in global overlap");
    pixel(f, 119, 20, white, "neighbor normalized elliptical corner excludes exterior");
    hit(f, f->target, 90, 30, true, "zero-axis overlap hit matches pinned Chromium point");
    setup(f, "border-radius:100px 0px 0px 0px");
    pixel(f, 80, 23, white, "single oversized circular corner extends beyond midpoint");
    pixel(f, 119, 23, red, "single oversized circular corner has independent interior");
    hit(f, f->target, 80, 23, false, "oversized circular corner excludes hit past midpoint");
    hit(f, f->target, 119, 23, true, "oversized circular corner includes right interior");

    setup(f, "width:100px;height:70px;border-style:solid;border-color:blue;"
             "border-width:10px 30px 20px 5px;border-radius:60px / 40px;background-clip:padding-box");
    pixel(f, 35, 40, blue, "unequal border widths create separate inner X/Y ellipse");
    pixel(f, 80, 60, red, "padding background remains visible inside inner ellipse");
}
static uint32_t red_texel = 0xffff0000u, white_texel = 0xffffffffu;
static Image red_image = {1, 1, 1, &red_texel, 1}, white_image = {1, 1, 1, &white_texel, 1};
static Image *url_image(const char *url) {
    if (strstr(url, "radius-red")) return &red_image;
    if (strstr(url, "radius-white")) return &white_image;
    return NULL;
}
static Image *node_image(Node *node) { return node->tag == A_img ? &red_image : NULL; }
static void image_and_gradient(Fixture *f) {
    paint_url_image_hook = url_image; paint_image_hook = node_image;
    const char *styles[] = {
        "width:200px;height:100px;border-radius:50%;background:linear-gradient(red,red)",
        "width:200px;height:100px;border-radius:50%;background-image:url(radius-red)",
        "width:200px;height:100px;border-radius:50%;mask-image:url(radius-white)"
    };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        setup(f, styles[i]);
        pixel(f, 40, 32, white, "gradient/image/mask honors elliptical exterior");
        pixel(f, 65, 55, red, "gradient/image/mask preserves elliptical interior");
    }
    setup(f, "display:none");
    node_set_attr(f->image, "style", "display:block;border-radius:50%");
    recalc(f); draw(f);
    pixel(f, 40, 32, white, "replaced image honors elliptical content clip");
    pixel(f, 65, 55, red, "replaced image paints elliptical interior");
    paint_url_image_hook = NULL; paint_image_hook = NULL;
}
static void nested_clips(Fixture *f) {
    setup(f, "left:30px;top:0;background:transparent;border-radius:30px / 50px;overflow:hidden");
    node_set_attr(f->parent, "style", "left:20px;top:20px;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden");
    node_set_attr(f->child, "style", "display:block");
    recalc(f); draw(f);
    pixel(f, 80, 45, red, "nested rounded clip intersection interior");
    pixel(f, 55, 30, white, "inner rounded ancestor excludes independently known point");
    pixel(f, 100, 25, white, "outer rounded ancestor remains active after inner push");
    hit(f, f->child, 80, 45, true, "nested rounded clip hit interior");
    hit(f, f->child, 55, 30, false, "inner rounded clip excludes descendant hit");
    hit(f, f->child, 100, 25, false, "outer rounded clip excludes descendant hit");

    DItem items[7] = {0};
    items[0].op = DO_PUSH_CLIP; items[0].x = items[0].y = 20; items[0].w = items[0].h = 100;
    const float outer_x[4] = {50,50,50,50}, outer_y[4] = {30,30,30,30};
    ditem_set_radii(&items[0], outer_x, outer_y);
    items[1].op = DO_PUSH_CLIP; items[1].w = items[1].h = 200; /* rectangular ancestor is skipped */
    items[2].op = DO_PUSH_CLIP; items[2].x = 50; items[2].y = 20; items[2].w = items[2].h = 100;
    const float inner_x[4] = {30,30,30,30}, inner_y[4] = {50,50,50,50};
    ditem_set_radii(&items[2], inner_x, inner_y);
    items[3].op = DO_RECT; items[3].w = items[3].h = 200; items[3].color = red;
    items[4].op = items[5].op = items[6].op = DO_POP_CLIP;
    DisplayList direct = {0}; direct.items.v = items; direct.items.n = 7;
    raster(&f->canvas, &direct, white);
    pixel(f, 80, 45, red, "direct nested clip interior with intervening rectangular ancestor");
    pixel(f, 55, 30, white, "direct inner ellipse excludes exterior");
    pixel(f, 100, 25, white, "direct outer ellipse excludes exterior");

    setup(f, "border-radius:50%;background:transparent;overflow:visible");
    node_set_attr(f->child, "style", "display:block;width:15px;height:15px");
    recalc(f); draw(f);
    pixel(f, 22, 22, red, "overflow-visible child is not clipped by parent's rounded border");
    hit(f, f->child, 22, 22, true, "overflow-visible child remains independently hittable");
}
static void fragments(Fixture *f) {
    setup(f, "height:140px;border-radius:30px / 15px");
    Box *b = f->target->box;
    yes(b != NULL, "fragment fixture has an actual styled layout box");
    if (!b) return;
    b->kind = BX_INLINE;
    b->ir = xrealloc(b->ir, 3 * sizeof *b->ir); b->nir = b->ircap = 3;
    b->ir[0] = (IRect){20, 20, 100, 40, true, false, 0};
    b->ir[1] = (IRect){20, 70, 100, 40, false, false, 1};
    b->ir[2] = (IRect){20, 120, 100, 40, false, true, 2};
    draw(f);
    pixel(f, 21, 21, white, "first fragment rounds its leading corner");
    pixel(f, 118, 21, red, "first fragment keeps trailing corner square");
    pixel(f, 21, 71, red, "middle fragment leading corner is square");
    pixel(f, 118, 71, red, "middle fragment trailing corner is square");
    pixel(f, 21, 121, red, "last fragment leading corner is square");
    pixel(f, 118, 121, white, "last fragment rounds its trailing corner");
    hit(f, f->target, 21, 21, false, "first fragment rounded exterior excludes hit");
    hit(f, f->target, 118, 21, true, "first fragment square end includes hit");
    hit(f, f->target, 21, 71, true, "middle fragment square end includes hit");
    hit(f, f->target, 21, 121, true, "last fragment square leading end includes hit");
    hit(f, f->target, 118, 121, false, "last fragment rounded exterior excludes hit");
}
static void rows_and_damage(Fixture *f) {
    setup(f, "border-radius:40px / 10px");
    pixel(f, 25, 30, red, "Y-only change starts with painted wide-corner interior");
    Canvas expected; canvas_init(&expected, W, H, 1);
    node_set_attr(f->target, "style", "border-radius:40px / 40px");
    recalc(f); dl_build(&f->list, f->layout, 0, 0, W, H);
    raster(&expected, &f->list, white);
    raster_rect(&f->canvas, &f->list, white, 20, 20, 120, 120);
    pixel(f, 25, 30, white, "Y-only change invalidated previously painted corner");
    yes(same_canvas(&f->canvas, &expected), "Y-only radius partial damage matches fresh full raster");
    setup(f, "width:200px;height:100px;border-radius:80px 30px / 20px 45px");
    raster_pixel_rows = true; raster(&expected, &f->list, white); raster_pixel_rows = false;
    yes(same_canvas(&f->canvas, &expected), "optimized elliptical spans equal scalar pixel rows");
    setup(f, "border-radius:20px");
    raster_pixel_rows = true; raster(&expected, &f->list, white); raster_pixel_rows = false;
    yes(same_canvas(&f->canvas, &expected), "legacy circular fast spans equal scalar pixel rows");
    canvas_free(&expected);
}
int main(void) {
    dom_init(); font_init(); Fixture f = create_fixture();
    basic_geometry(&f); image_and_gradient(&f); nested_clips(&f);
    rows_and_damage(&f); fragments(&f);
    destroy(&f);
    printf("radius native rendering: %u/%u independently specified pixel/hit/damage checks passed\n",
           checks - failures, checks);
    return failures ? 1 : 0;
}
