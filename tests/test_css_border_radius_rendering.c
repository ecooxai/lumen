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
static uint32_t checker_texels[64];
static Image checker_image = {8, 8, 1, checker_texels, 1};
static Image *url_image(const char *url) {
    if (strstr(url, "radius-checker")) return &checker_image;
    if (strstr(url, "radius-red")) return &red_image;
    if (strstr(url, "radius-white")) return &white_image;
    return NULL;
}
static Image *node_image(Node *node) { return node->tag == A_img ? &red_image : NULL; }
static void image_and_gradient(Fixture *f) {
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        checker_texels[y * 8 + x] = ((x < 4) == (y < 4)) ? white : 0;
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
    setup(f, "width:200px;height:100px;border-radius:50%;mask-image:url(radius-white);mask-size:cover");
    pixel(f, 40, 32, white, "explicit cover mask retains elliptical exterior");
    pixel(f, 65, 55, red, "explicit cover mask retains elliptical interior");
    setup(f, "width:200px;height:100px;border-radius:0;mask-image:url(radius-white);mask-repeat:no-repeat");
    pixel(f, 20, 20, red, "explicit no-repeat preserves the intrinsic first mask pixel");
    pixel(f, 65, 55, white, "explicit no-repeat does not tile an authored single image");
    const char *reset_repeat[] = {"initial", "unset"};
    for (unsigned i = 0; i < sizeof reset_repeat / sizeof *reset_repeat; i++) {
        char style[224];
        snprintf(style, sizeof style, "width:200px;height:100px;border-radius:0;mask-image:url(radius-white);"
                 "mask-repeat:no-repeat;mask-repeat:%s", reset_repeat[i]);
        setup(f, style);
        const char *stored = custom_get(f->target->style->gen_n, "mask-repeat");
        yes(stored && !*stored, "generic CSS-wide mask reset stores the existing empty initial sentinel");
        pixel(f, 65, 55, red, "resolved initial mask repeat tiles beyond the first intrinsic image");
    }
    const char *unsupported_repeat[] = {"space", "round"};
    for (unsigned i = 0; i < sizeof unsupported_repeat / sizeof *unsupported_repeat; i++) {
        char style[192];
        snprintf(style, sizeof style, "width:200px;height:100px;border-radius:0;mask-image:url(radius-white);mask-repeat:%s",
                 unsupported_repeat[i]);
        setup(f, style);
        bool ordinary_image = false, tiled_image = false;
        for (int j = 0; j < f->list.items.n; j++) {
            const DItem *it = &f->list.items.v[j];
            if (it->op == DO_IMAGE && it->img) {
                ordinary_image = true;
                tiled_image = tiled_image || (it->tile.w > 0 && it->tile.h > 0);
            }
        }
        yes(ordinary_image && !tiled_image,
            "unsupported explicit mask-repeat mode keeps its previous producer path (not conformance)");
    }
    setup(f, "width:200px;height:100px;border-radius:20px / 10px;mask-image:url(radius-checker);mask-repeat:repeat  repeat");
    pixel(f, 22, 62, red, "nonuniform mask opaque quadrant is painted");
    pixel(f, 26, 62, white, "nonuniform mask transparent quadrant stays clear");
    pixel(f, 30, 62, red, "nonuniform mask repeats at its intrinsic horizontal period");
    pixel(f, 22, 66, white, "nonuniform mask transparent vertical quadrant stays clear");
    pixel(f, 22, 70, red, "nonuniform mask repeats at its intrinsic vertical period");
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
static void deferred_clip_boundaries(Fixture *f) {
    /* The inner clip moves with the outer scroller, while neither clip moves
       with its own content. Independent boxes: outer=(20,20,100,100),
       inner=(40,20,100,100), red child=(40,10,200,200). */
    setup(f, "left:30px;top:0;background:transparent;border-radius:30px / 50px;overflow:hidden");
    node_set_attr(f->parent, "style", "left:20px;top:20px;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden");
    node_set_attr(f->child, "style", "display:block;width:200px;height:200px");
    recalc(f);
    f->parent->scroll_x = 10; f->target->scroll_y = 10;
    draw(f);
    pixel(f, 70, 45, red, "scrolled deferred clip intersection interior");
    pixel(f, 45, 30, white, "inner rounded clip uses its pre-scroll position");
    pixel(f, 100, 25, white, "outer rounded clip stays stationary during child scrolling");
    hit(f, f->child, 70, 45, true, "scrolled nested clip hit interior");
    hit(f, f->child, 45, 30, false, "scrolled inner clip excludes descendant hit");
    f->parent->scroll_x = 0; f->target->scroll_y = 0;

    /* The outer contour remains in document space. The transformed inner
       ellipse has X=60,Y=50, but the outer ellipse stays X=50,Y=30. */
    setup(f, "left:0;top:0;background:transparent;border-radius:30px / 50px;overflow:hidden;"
             "transform:matrix(2,0,0,1,0,0);transform-origin:0 0");
    node_set_attr(f->parent, "style", "left:20px;top:20px;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden");
    node_set_attr(f->child, "style", "display:block");
    recalc(f); draw(f);
    pixel(f, 60, 30, red, "affine child remains visible in both contours");
    pixel(f, 110, 30, white, "outer rounded contour is not transformed a second time");
    pixel(f, 24, 45, white, "transformed inner contour independently excludes its corner");

    setup(f, "left:0;top:0;border-radius:0");
    node_set_attr(f->parent, "style", "position:static;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden");
    recalc(f); draw(f);
    yes(f->target->box->cb == f->layout->root, "absolute fixture uses a containing block outside the clipping ancestor");
    pixel(f, 2, 2, red, "absolute containing-block escape is not clipped by the rounded ancestor");
    hit(f, f->target, 2, 2, true, "absolute containing-block escape remains hittable");

    setup(f, "position:fixed;left:0;top:0;border-radius:0");
    node_set_attr(f->parent, "style", "left:20px;top:20px;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden");
    recalc(f); draw(f);
    pixel(f, 2, 2, red, "viewport-fixed descendant escapes unrelated rounded clipping");
    hit(f, f->target, 2, 2, true, "viewport-fixed escaped descendant remains hittable");
}

static void deep_deferred_contexts(void) {
    enum { DEPTH = 16 };
    Fixture f = {0};
    f.doc = doc_new("https://radius-rendering.test/deep");
    char html[4096] = "<!doctype html><html><body>";
    for (int i = 0; i < DEPTH; i++) strcat(html, "<div class=layer>");
    strcat(html, "<div id=deep-ink></div>");
    for (int i = 0; i < DEPTH; i++) strcat(html, "</div>");
    strcat(html, "</body></html>");
    html_parse(f.doc, html, strlen(html));
    f.engine = style_engine_new(f.doc); f.engine->media = (MediaCtx){W, H, 1, false};
    const char *css = "html,body{margin:0;border:0}body{padding:20px}"
        ".layer{position:relative;z-index:0;width:100px;height:100px;overflow:hidden;border-radius:50px / 30px}"
        "#deep-ink{position:absolute;inset:0;background:red}";
    style_engine_add_sheet(f.engine, css_parse_sheet(css, strlen(css), f.doc->url, 1, &f.engine->media));
    f.layout = layout_new(); canvas_init(&f.canvas, W, H, 1);
    recalc(&f); draw(&f);
    Node *ink = doc_get_element_by_id(f.doc, "deep-ink");
    pixel(&f, 22, 22, white, "sixteen nested rounded contexts retain their exterior clipping");
    pixel(&f, 70, 70, red, "sixteen nested rounded contexts retain their common interior");
    hit(&f, ink, 22, 22, false, "deep rounded context exterior excludes descendant hit");
    hit(&f, ink, 70, 70, true, "deep rounded context interior accepts descendant hit");
    int depth = 0, peak = 0; bool balanced = true;
    for (int i = 0; i < f.list.items.n; i++) {
        if (f.list.items.v[i].op == DO_PUSH_CLIP) { depth++; if (depth > peak) peak = depth; }
        if (f.list.items.v[i].op == DO_POP_CLIP && --depth < 0) balanced = false;
    }
    yes(balanced && depth == 0 && peak <= 2 * DEPTH + 8,
        "nested contexts keep balanced linear clip depth without duplicating active prefixes");
    printf("radius nested contexts: depth=%d peak_active_clips=%d display_items=%d\n",
           DEPTH, peak, f.list.items.n);
    destroy(&f);
}

static void fixed_hit_paths(Fixture *f) {
    const char *clip = "left:20px;top:20px;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden";
    const char *fixed = "display:block;position:fixed;left:0;top:0;width:100px;height:100px;background:red";
    f->parent->scroll_x = f->parent->scroll_y = 0;
    f->target->scroll_x = f->target->scroll_y = 0;

    setup(f, "position:static;width:240px;height:240px;background:transparent");
    node_set_attr(f->parent, "style", clip); node_set_attr(f->child, "style", fixed);
    recalc(f);
    hit(f, f->child, 2, 2, true, "fixed descendant behind ordinary intermediary hits before paint");
    draw(f);
    pixel(f, 2, 2, red, "fixed descendant behind ordinary intermediary paints outside clip");
    hit(f, f->child, 2, 2, true, "fixed descendant behind ordinary intermediary hits after paint");
    f->parent->scroll_x = 30; f->parent->scroll_y = 40; draw(f);
    pixel(f, 2, 2, red, "viewport-fixed descendant does not follow ancestor scroll offsets");
    hit(f, f->child, 2, 2, true, "viewport-fixed hit uses viewport coordinates after ancestor scrolling");
    f->parent->scroll_x = f->parent->scroll_y = 0;

    setup(f, "display:inline;position:static;background:transparent");
    node_set_attr(f->parent, "style", clip); node_set_attr(f->child, "style", fixed);
    recalc(f);
    yes(f->target->box && f->target->box->kind == BX_INLINE &&
        f->child->box && box_fixed_to_viewport(f->child->box),
        "inline-only intermediary contains a viewport-fixed child");
    printf("radius inline-only fixture: target_nir=%d containing_block_nfrags=%d\n",
        f->target->box ? f->target->box->nir : -1,
        f->target->box && f->target->box->cb ? f->target->box->cb->nfrags : -1);
    hit(f, f->child, 2, 2, true, "inline-only fixed descendant hits before paint metadata exists");
    draw(f);
    pixel(f, 2, 2, red, "inline-only fixed descendant paints outside ancestor clipping");
    hit(f, f->child, 2, 2, true, "inline-only fixed descendant hits after paint metadata exists");

    setup(f, "position:static;width:240px;height:240px;background:transparent;overflow:hidden");
    node_set_attr(f->parent, "style", clip);
    node_set_attr(f->child, "style", "display:block;left:-20px;top:-20px;width:100px;height:100px");
    recalc(f); draw(f);
    pixel(f, 2, 2, white, "ordinary absolute content remains clipped outside its retained containing block");
    hit(f, f->child, 2, 2, false, "ordinary absolute content cannot escape a retained clipping containing block");

    setup(f, "position:static;width:240px;height:240px;background:transparent");
    node_set_attr(f->parent, "style", "left:20px;top:20px;width:100px;height:100px;border-radius:50px / 30px;"
                                      "overflow:hidden;transform:matrix(1,0,0,1,0,0)");
    node_set_attr(f->child, "style", fixed);
    recalc(f);
    yes(f->child->box && f->child->box->cb == f->parent->box && !box_fixed_to_viewport(f->child->box),
        "existing identity-transform fixed containing block is retained");
    draw(f);
    pixel(f, 2, 2, white, "element-contained fixed descendant does not paint in viewport coordinates");
    hit(f, f->child, 2, 2, false, "element-contained fixed descendant cannot escape its ancestor clip");
    pixel(f, 70, 70, red, "element-contained fixed descendant paints its independent interior");
    hit(f, f->child, 70, 70, true, "element-contained fixed descendant hits its independent interior");

    setup(f, "position:static;width:240px;height:240px;background:transparent");
    node_set_attr(f->parent, "style", "left:20px;top:20px;width:100px;height:100px;overflow:hidden;content-visibility:hidden");
    node_set_attr(f->child, "style", fixed);
    recalc(f); draw(f);
    pixel(f, 2, 2, white, "existing hidden-contents barrier suppresses fixed descendant paint");
    hit(f, f->child, 2, 2, false, "existing hidden-contents barrier suppresses fixed descendant hits");

    setup(f, "position:static;width:240px;height:240px;background:transparent;overflow:hidden");
    node_set_attr(f->parent, "style", "position:static;margin:20px;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden");
    node_set_attr(f->child, "style", "display:block;left:0;top:0;width:100px;height:100px");
    recalc(f);
    yes(f->child->box && f->child->box->cb == f->layout->root,
        "absolute escape fixture resolves its containing block outside both clipping ancestors");
    f->parent->scroll_x = 30; f->parent->scroll_y = 40; draw(f);
    pixel(f, 2, 2, red, "escaped absolute descendant ignores inapplicable clipping and scrolling");
    hit(f, f->child, 2, 2, true, "escaped absolute hit restores pre-scroll ancestor coordinates");
    f->parent->scroll_x = f->parent->scroll_y = 0;

    setup(f, "position:static;width:240px;height:240px;background:transparent;overflow:hidden");
    node_set_attr(f->parent, "style", "position:relative;margin:20px;width:100px;height:100px;border-radius:50px / 30px;overflow:hidden");
    node_set_attr(f->child, "style", "display:block;left:0;top:0;width:200px;height:200px");
    recalc(f);
    yes(f->child->box && f->child->box->cb == f->parent->box,
        "absolute retained fixture resolves to the outer clipping containing block");
    f->parent->scroll_x = 30; f->parent->scroll_y = 40; draw(f);
    pixel(f, 2, 2, white, "retained absolute containing block still clips scrolled exterior");
    hit(f, f->child, 2, 2, false, "retained absolute containing block rejects scrolled exterior hit");
    pixel(f, 70, 70, red, "retained absolute containing block paints scrolled interior");
    hit(f, f->child, 70, 70, true, "retained absolute containing block accepts scrolled interior hit");
    f->parent->scroll_x = f->parent->scroll_y = 0;

    setup(f, "position:static;width:100px;height:100px;background:transparent");
    node_set_attr(f->parent, "style", clip);
    node_set_attr(f->child, "style", "display:block;position:fixed;left:0;top:0;width:100px;height:100px;background:red;z-index:1");
    const char *overlay = "display:block;position:fixed;left:0;top:0;width:20px;height:20px;background:blue;z-index:2";
    node_set_attr(f->image, "style", overlay); recalc(f); draw(f);
    pixel(f, 2, 2, blue, "higher viewport-fixed overlay retains painting order");
    hit(f, f->image, 2, 2, true, "higher viewport-fixed overlay retains hit order");
    node_set_attr(f->image, "style", "display:block;position:fixed;left:0;top:0;width:20px;height:20px;"
                                  "background:blue;z-index:2;pointer-events:none");
    recalc(f); draw(f);
    pixel(f, 2, 2, blue, "pointer-events none does not hide the higher overlay");
    hit(f, f->child, 2, 2, true, "pointer-events none exposes the escaped fixed hit below");
}

static void clipped_text_hit(void) {
    Fixture f = {0}; f.doc = doc_new("https://radius-rendering.test/clipped-text");
    const char *html = "<!doctype html><html><body><div id=clip>MMMMMMMM</div></body></html>";
    html_parse(f.doc, html, strlen(html));
    f.engine = style_engine_new(f.doc); f.engine->media = (MediaCtx){W, H, 1, false};
    const char *css = "html,body{margin:0;padding:0;border:0}#clip{position:absolute;left:20px;top:20px;"
        "width:100px;height:100px;border-radius:50px / 30px;overflow:hidden;font:20px/20px sans-serif;background:red}";
    style_engine_add_sheet(f.engine, css_parse_sheet(css, strlen(css), f.doc->url, 1, &f.engine->media));
    f.layout = layout_new(); canvas_init(&f.canvas, W, H, 1); recalc(&f);
    Node *clip = doc_get_element_by_id(f.doc, "clip");
    hit(&f, clip, 22, 22, false, "clipped own text fragment cannot bypass rounded clipping before paint");
    draw(&f);
    pixel(&f, 22, 22, white, "own text remains outside the independent rounded corner");
    hit(&f, clip, 22, 22, false, "clipped own text fragment cannot bypass rounded clipping after paint");
    destroy(&f);
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
    rows_and_damage(&f); deferred_clip_boundaries(&f); fixed_hit_paths(&f); fragments(&f);
    destroy(&f); deep_deferred_contexts(); clipped_text_hit();
    printf("radius native rendering: %u/%u independently specified pixel/hit/damage checks passed\n",
           checks - failures, checks);
    return failures ? 1 : 0;
}
