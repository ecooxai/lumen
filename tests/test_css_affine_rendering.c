/* Actual affine pixels, geometry, hit testing, clipping and damage. The frozen
 * 337 angle-boundary checks remain an independent target without edits. */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "affine_ink_font.h"

enum { W = 640, H = 480 };
static const uint32_t red = 0xffe02030u, green = 0xff20a040u, white = 0xffffffffu;
static unsigned checks;
static const char *case_name;
typedef struct {
    Document *doc; StyleEngine *engine; Layout *layout;
    Node *parent, *target, *fixed, *spacer;
    DisplayList list; Canvas canvas;
} Fixture;
static void yes(bool value, const char *label) {
    checks++;
    if (!value) { fprintf(stderr, "%s: %s\n", case_name, label); abort(); }
}
static void near(float actual, float expected, const char *label) {
    checks++;
    if (!isfinite(actual) || fabsf(actual - expected) > .025f) {
        fprintf(stderr, "%s: %s %g expected %g\n", case_name, label, actual, expected); abort();
    }
}
static void recalc(Fixture *f) {
    style_recalc(f->engine, &f->doc->node, false);
    layout_run(f->layout, f->doc, W, H);
}
static void draw(Fixture *f) {
    dl_build(&f->list, f->layout, 0, 0, W, H);
    raster(&f->canvas, &f->list, white);
}
static void setup(Fixture *f, const char *parent, const char *target) {
    node_set_attr(f->parent, "style", parent);
    node_set_attr(f->target, "style", target);
    node_set_attr(f->fixed, "style", "display:none");
    node_set_attr(f->spacer, "style", "display:none");
    recalc(f); draw(f);
}
static void rectangle(Node *node, const float expected[4]) {
    float actual[4]; yes(jsg_vrect(node, actual, 0, 0), "actual DOM rectangle");
    for (int i = 0; i < 4; i++) near(actual[i], expected[i], "transformed corner union");
}
static void normal(Node *node, const float expected[4]) {
    Box *b = node->box; assert(b);
    near(b->x, expected[0], "normal layout x"); near(b->y, expected[1], "normal layout y");
    near(b->w, expected[2], "normal layout width"); near(b->h, expected[3], "normal layout height");
}
static void measured(Fixture *f, Node *node, const float expected[4], const float used[4],
                     int px, int py, int ox, int oy, uint32_t color) {
    rectangle(node, expected); normal(node, used);
    yes(f->canvas.px[py * W + px] == color, "actual interior raster pixel");
    yes(f->canvas.px[oy * W + ox] == white, "actual excluded raster pixel");
    Box *hit = layout_hit(f->layout, px + .5f, py + .5f, 0, 0);
    yes(hit && hit->node == node, "inverse hit selects painted interior");
    hit = layout_hit(f->layout, ox + .5f, oy + .5f, 0, 0);
    yes(!hit || hit->node != node, "inverse hit excludes unpainted location");
}
static Fixture create_fixture(void) {
    Fixture f = {0};
    f.doc = doc_new("https://affine-rendering.test/");
    const char *html = "<!doctype html><html><body><div id=parent><div id=target><div id=fixed></div></div><div id=spacer></div></div></body></html>";
    html_parse(f.doc, html, strlen(html));
    f.engine = style_engine_new(f.doc); f.engine->media = (MediaCtx){W, H, 1, false};
    const char *css = "html,body{margin:0;padding:0;border:0}"
        "#parent{position:absolute;left:100px;top:100px;width:200px;height:200px;transform-origin:0 0}"
        "#target{position:absolute;left:20px;top:20px;width:20px;height:10px;background:#e02030;transform-origin:0 0}"
        "#fixed{display:none;position:fixed;left:3px;top:4px;width:4px;height:3px;background:#20a040}"
        "#spacer{display:none;width:300px;height:300px;pointer-events:none}";
    style_engine_add_sheet(f.engine, css_parse_sheet(css, strlen(css), f.doc->url, 1, &f.engine->media));
    f.parent = doc_get_element_by_id(f.doc, "parent"); f.target = doc_get_element_by_id(f.doc, "target");
    f.fixed = doc_get_element_by_id(f.doc, "fixed"); f.spacer = doc_get_element_by_id(f.doc, "spacer");
    f.layout = layout_new(); canvas_init(&f.canvas, W, H, 1); return f;
}
static void destroy(Fixture *f) {
    canvas_free(&f->canvas); dl_clear(&f->list); free(f->list.items.v);
    layout_free(f->layout); style_engine_free(f->engine); doc_free(f->doc);
}

static uint32_t image_pixels[20 * 20];
static Image image_fixture = {20, 20, 1, image_pixels, 1};
static Image *test_image(Node *node) { return node->tag == A_img ? &image_fixture : NULL; }
static void compare_pixels(const Canvas *a, const Canvas *b, const char *label) {
    bool same = a->w == b->w && a->h == b->h;
    for (int y = 0; same && y < a->h; y++) for (int x = 0; x < a->w; x++)
        if (canvas_row(a, y)[x] != canvas_row(b, y)[x]) {
            fprintf(stderr, "%s: %s first mismatch (%d,%d) %08x / %08x\n", case_name, label, x, y, canvas_row(a, y)[x], canvas_row(b, y)[x]);
            same = false; break;
        }
    yes(same, label);
}
static void primitives_and_damage(void) {
    case_name = "existing primitives use the same actual affine raster path";
    for (int y = 0; y < 20; y++) for (int x = 0; x < 20; x++)
        image_pixels[y * 20 + x] = y < 10 ? (x < 10 ? 0xffff0000u : 0xff00ff00u) : (x < 10 ? 0xff0000ffu : 0xffffff00u);
    paint_image_hook = test_image;
    Document *doc = doc_new("https://affine-primitives.test/");
    const char *html = "<!doctype html><html><body><div id=target><img width=20 height=20><span>AB</span></div></body></html>";
    html_parse(doc, html, strlen(html)); StyleEngine *engine = style_engine_new(doc);
    engine->media = (MediaCtx){W, H, 1, false};
    const char *css = "html,body{margin:0;padding:0}#target{position:absolute;left:120px;top:120px;width:80px;height:80px;"
        "background:linear-gradient(35deg,#1060c0,#f09020);border:3px solid #803020;border-radius:12px;"
        "box-shadow:4px 6px 8px #0008;transform-origin:0 0;opacity:.75}"
        "img{position:absolute;left:10px;top:10px}span{position:absolute;left:10px;top:40px;font:20px sans-serif;color:#102060}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Layout *layout = layout_new(); Node *target = doc_get_element_by_id(doc, "target");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, W, H);
    DisplayList reference = {0}, transformed = {0};
    Canvas a, b, expected; canvas_init(&a, W, H, 1); canvas_init(&b, W, H, 1); canvas_init(&expected, W, H, 1);
    dl_build(&reference, layout, 0, 0, W, H); raster(&a, &reference, white);
    unsigned ink = 0; for (int i = 0; i < W * H; i++) ink += a.px[i] != white;
    yes(ink > 1000, "source primitives actually painted nonwhite pixels");
    node_set_attr(target, "style", "transform:rotate(90deg)");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, W, H);
    dl_build(&transformed, layout, 0, 0, W, H); raster(&b, &transformed, white);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int sx = y, sy = 239 - x;
        expected.px[y * W + x] = sx >= 0 && sx < W && sy >= 0 && sy < H ? a.px[sy * W + sx] : white;
    }
    compare_pixels(&b, &expected, "ninety-degree primitive raster equals independent pixel permutation");
    const float rect[] = {34, 120, 86, 86}; rectangle(target, rect);

    case_name = "affine damage tiles and parallel bands retain every pixel";
    node_set_attr(target, "style", "transform:rotate(27deg)");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, W, H);
    dl_build(&transformed, layout, 0, 0, W, H);
    raster_set_threads(0); raster(&a, &transformed, white);
    for (int i = 0; i < W * H; i++) b.px[i] = 0x11223344u;
    const int x0 = 55, y0 = 105, x1 = 211, y1 = 257;
    raster_rect(&b, &transformed, white, x0, y0, x1, y1);
    bool partial = true;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        if (b.px[y * W + x] != (x >= x0 && x < x1 && y >= y0 && y < y1 ? a.px[y * W + x] : 0x11223344u)) partial = false;
    yes(partial, "partial damage matches full render and preserves every outside pixel");
    raster_set_threads(3); raster(&b, &transformed, white); raster_set_threads(0);
    compare_pixels(&a, &b, "parallel affine bands match single raster");
    Canvas fractional_a, fractional_b; canvas_init(&fractional_a, 800, 600, 1.25f); canvas_init(&fractional_b, 800, 600, 1.25f);
    raster(&fractional_a, &transformed, white);
    for (int y = 0; y < 600; y += 75) for (int x = 0; x < 800; x += 100)
        raster_rect(&fractional_b, &transformed, white, x, y, x + 100, y + 75);
    compare_pixels(&fractional_a, &fractional_b, "fractional-DPR partitioned damage matches full render");
    yes(g_affine_scratch_peak > 0 && g_affine_scratch_peak <= 4 * 1024 * 1024, "all nested affine heap scratch stays within per-band budget");
    yes(g_affine_windows > 0, "actual source-window renderer executed");
    canvas_free(&fractional_a); canvas_free(&fractional_b); canvas_free(&a); canvas_free(&b); canvas_free(&expected);
    dl_clear(&reference); dl_clear(&transformed); free(reference.items.v); free(transformed.items.v);
    layout_free(layout); style_engine_free(engine); doc_free(doc); paint_image_hook = NULL;
}

static void glyph_ink_and_scale(void) {
    case_name = "actual glyph ink beyond two em survives affine source crop and damage";
    const char *url = "https://affine-ink.test/original.ttf";
    font_declare("LumenAffineInk", 400, false, url);
    yes(font_loaded(url, affine_ink_font, sizeof affine_ink_font), "actual original TrueType face loads");
    Font *font = font_get("LumenAffineInk", 400, false, 20);
    ShapedRun run = {0}; text_shape(font, "A", 1, 0, &run);
    yes(run.n == 1, "actual custom face shapes one glyph");
    const Glyph *glyph = font_glyph(run.g[0].font, run.g[0].gid, 1);
    yes(glyph && glyph->left > 2 * font->size && glyph->w > 0, "actual bitmap bearing exceeds old two-em heuristic");
    shaped_free(&run);
    Document *doc = doc_new("https://affine-ink.test/");
    const char *html = "<!doctype html><html><body><div id=target>A</div></body></html>";
    html_parse(doc, html, strlen(html)); StyleEngine *engine = style_engine_new(doc);
    engine->media = (MediaCtx){W, H, 1, false};
    const char *css = "html,body{margin:0;padding:0}#target{position:absolute;left:120px;top:120px;"
        "width:10px;height:20px;font:20px/20px LumenAffineInk;color:#102060;transform-origin:0 0}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Node *target = doc_get_element_by_id(doc, "target"); Layout *layout = layout_new();
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, W, H);
    DisplayList plain = {0}, transformed = {0};
    Canvas a, b, expected; canvas_init(&a, W, H, 1); canvas_init(&b, W, H, 1); canvas_init(&expected, W, H, 1);
    dl_build(&plain, layout, 0, 0, W, H); raster(&a, &plain, white);
    unsigned ink = 0; int left = W;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (a.px[y * W + x] != white) { ink++; left = LMIN(left, x); }
    yes(ink >= 100 && left >= 180, "real glyph pixels lie outside its fragment and two-em padding");
    near(target->box->w, 10, "glyph overflow leaves normal layout width unchanged");
    node_set_attr(target, "style", "transform:rotate(90deg)");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, W, H);
    dl_build(&transformed, layout, 0, 0, W, H); dl_prepare_ink(&transformed, 1);
    raster(&b, &transformed, white);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int sx = y, sy = 239 - x;
        expected.px[y * W + x] = sx >= 0 && sx < W && sy >= 0 && sy < H ? a.px[sy * W + sx] : white;
    }
    compare_pixels(&b, &expected, "far-bearing glyph raster equals independent ninety-degree permutation");
    DTransform *group = NULL;
    for (int i = 0; i < transformed.items.n; i++) if (transformed.items.v[i].op == DO_TRANSFORM) group = transformed.items.v[i].transform;
    yes(group && group->ink_scale == 1 && group->source[0] >= 180, "damage cache contains actual far-bearing ink at canvas scale");
    double saved[8], source[4], bounds[4]; memcpy(saved, group->source, sizeof(double) * 4); memcpy(saved + 4, group->bounds, sizeof(double) * 4);
    yes(paint_transform_ink(group, 1.25f, source, bounds) && source[0] >= 180, "different actual raster scale measures genuine glyph ink");
    yes(group->ink_scale == 1 && !memcmp(saved, group->source, sizeof(double) * 4) && !memcmp(saved + 4, group->bounds, sizeof(double) * 4), "different-scale query never mutates shared display-list bounds");
    Canvas fa, fb; canvas_init(&fa, 800, 600, 1.25f); canvas_init(&fb, 800, 600, 1.25f);
    raster(&fa, &transformed, white);
    for (int y = 0; y < 600; y += 75) for (int x = 0; x < 800; x += 100) raster_rect(&fb, &transformed, white, x, y, x + 100, y + 75);
    compare_pixels(&fa, &fb, "far-bearing glyph at fractional DPR survives all partitioned damage tiles");
    for (int i = 0; i < W * H; i++) expected.px[i] = 0x11223344u;
    raster_rect(&expected, &transformed, white, 80, 175, 150, 200);
    bool partial = true;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        if (expected.px[y * W + x] != (x >= 80 && x < 150 && y >= 175 && y < 200 ? b.px[y * W + x] : 0x11223344u)) partial = false;
    yes(partial, "glyph-only damaged region paints exact ink and preserves every outside pixel");
    yes(group->ink_scale == 1 && !memcmp(saved, group->source, sizeof(double) * 4) && !memcmp(saved + 4, group->bounds, sizeof(double) * 4), "actual fractional raster keeps original damage cache immutable");
    canvas_free(&fa); canvas_free(&fb); canvas_free(&a); canvas_free(&b); canvas_free(&expected);
    dl_clear(&plain); dl_clear(&transformed); free(plain.items.v); free(transformed.items.v);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
}

static void anonymous_transform_owners(void) {
    case_name = "anonymous inline wrapper does not duplicate its element transform";
    Fixture f = {0}; f.doc = doc_new("https://affine-owner.test/");
    const char *html = "<!doctype html><html><body><div id=parent><span id=target></span><div id=block></div></div></body></html>";
    html_parse(f.doc, html, strlen(html)); f.engine = style_engine_new(f.doc);
    f.engine->media = (MediaCtx){W,H,1,false};
    const char *css = "html,body{margin:0;padding:0}#parent{position:absolute;left:100px;top:100px;width:200px;height:200px;"
        "font-size:0;line-height:0;transform:rotate(90deg);transform-origin:0 0}"
        "#target{display:inline-block;vertical-align:top;width:20px;height:10px;background:#e02030}#block{height:1px}";
    style_engine_add_sheet(f.engine, css_parse_sheet(css, strlen(css), f.doc->url, 1, &f.engine->media));
    f.parent = doc_get_element_by_id(f.doc,"parent"); f.target = doc_get_element_by_id(f.doc,"target");
    f.layout=layout_new();canvas_init(&f.canvas,W,H,1);recalc(&f);draw(&f);
    Box *wrapper=f.target->box->parent;
    yes(wrapper && wrapper->anon && !wrapper->node && wrapper->st->has_transform,"actual anonymous wrapper retains shared parent transform style");
    measured(&f,f.target,(float[]){90,100,10,20},(float[]){100,100,20,10},95,110,115,105,red);

    case_name = "real pseudo box retains its element-owned affine transform";
    const char *pseudo = "#parent::before{content:'';position:absolute;left:20px;top:20px;width:20px;height:10px;"
        "background:#2040e0;transform:rotate(90deg);transform-origin:0 0}";
    style_engine_add_sheet(f.engine, css_parse_sheet(pseudo, strlen(pseudo), f.doc->url, 1, &f.engine->media));
    node_set_attr(f.parent,"style","transform:none");recalc(&f);draw(&f);
    Box *box=NULL;
    for(Box *p=f.parent->box->first;p;p=p->next)if(p->anon&&p->node==f.parent&&p->st->has_transform)box=p;
    yes(box && box->node->type==NODE_ELEMENT,"actual transformed pseudo keeps an element owner despite anon flag");
    near(box->x,120,"pseudo normal x");near(box->y,120,"pseudo normal y");near(box->w,20,"pseudo normal width");near(box->h,10,"pseudo normal height");
    yes(f.canvas.px[130*W+115]==0xff2040e0u,"pseudo's own affine transform paints the rotated pixel");
    yes(f.canvas.px[125*W+135]==white,"pseudo leaves its old untransformed location empty");
    Box *hit=layout_hit(f.layout,115.5f,130.5f,0,0);yes(hit&&hit->node==f.parent,"inverse pseudo hit resolves to its originating element");
    rectangle(f.parent,(float[]){100,100,200,200});
    destroy(&f);
}

int main(void) {
    dom_init(); font_init(); Fixture f = create_fixture();
    static const struct { const char *name, *style; float rectangle[4]; int px, py, ox, oy; } cases[] = {
        {"positive off-diagonal rotation paints and hits", "transform:rotate(90deg)", {110,120,10,20},115,130,135,125},
        {"negative off-diagonal rotation paints and hits", "transform:rotate(-90deg)", {120,100,10,20},125,110,135,125},
        {"non-right rotation uses inverse polygon hit", "transform:rotate(45deg)", {112.928932,120,21.213203,21.213203},124,130,113,121},
        {"skew uses the full off-diagonal matrix", "transform:skewX(45deg)", {120,120,30,10},135,125,121,129},
        {"negative nonuniform scale paints the reflected box", "transform:scale(-2,1.5)", {80,120,40,15},100,127,135,125},
        {"length transform origin is shared", "transform:rotate(90deg);transform-origin:10px 5px", {125,115,10,20},130,125,121,121},
        {"percentage transform origin is shared", "transform:rotate(90deg);transform-origin:100% 100%", {140,110,10,20},145,120,125,125},
        {"full matrix retains both skew and translation", "transform:matrix(1,.5,.25,1,7,9)", {127,129,22.5,20},138,139,128,147},
    };
    const float ordinary[] = {120,120,20,10};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        case_name = cases[i].name; setup(&f, "", cases[i].style);
        measured(&f, f.target, cases[i].rectangle, ordinary, cases[i].px, cases[i].py, cases[i].ox, cases[i].oy, red);
    }
    assert(checks == 104);
    case_name = "opposing nested rotations form one final corner union";
    setup(&f, "transform:rotate(45deg)", "transform:rotate(-45deg)");
    measured(&f, f.target, (float[]){100,128.284271,20,10}, ordinary,110,133,125,125,red);
    case_name = "outer clip stays in destination coordinates";
    setup(&f, "width:100px;height:100px;overflow:hidden", "left:90px;top:80px;width:40px;height:20px;transform:rotate(90deg)");
    measured(&f, f.target, (float[]){170,180,20,40}, (float[]){190,180,40,20},180,190,180,210,red);
    case_name = "inner clip rotates with its transformed owner";
    setup(&f, "width:100px;height:100px;overflow:hidden;transform:rotate(90deg)", "left:80px;top:20px;width:50px;height:20px");
    measured(&f, f.target, (float[]){60,180,20,50}, (float[]){180,120,50,20},70,190,70,220,red);
    case_name = "source outside viewport is retained when transformed into view";
    setup(&f, "left:1200px", "left:0;top:0;width:40px;height:20px;transform:translateX(-1100px) rotate(90deg)");
    measured(&f, f.target, (float[]){80,100,20,40}, (float[]){1200,100,40,20},90,120,110,120,red);
    case_name = "fixed descendant uses its actual rotated containing block";
    setup(&f, "transform:rotate(90deg)", ""); node_set_attr(f.fixed,"style","display:block"); recalc(&f); draw(&f);
    measured(&f, f.fixed, (float[]){93,103,3,4}, (float[]){103,104,4,3},94,105,100,110,green);
    case_name = "element scrolling precedes its owner transform";
    setup(&f, "width:100px;height:100px;overflow:hidden;transform:rotate(90deg)", "top:30px");
    node_set_attr(f.spacer,"style","display:block"); recalc(&f); jsg_set_scroll(f.parent,10,20); draw(&f);
    measured(&f, f.target, (float[]){80,110,10,20}, (float[]){120,130,20,10},85,120,95,120,red);
    assert(checks == 182);
    case_name = "singular matrix has degenerate bounds and no painted or hit content";
    setup(&f, "", "transform:scale(0,1)"); rectangle(f.target,(float[]){120,120,0,10}); normal(f.target,ordinary);
    yes(f.canvas.px[125 * W + 120] == white,"singular transform paints no pixel");
    Box *hit = layout_hit(f.layout,120.5f,125.5f,0,0); yes(!hit || hit->node != f.target,"singular transform is not hittable");
    yes(f.target->style->has_transform,"singular transform retains containing-block identity");
    case_name = "nested opacity stays inside affine compositing";
    setup(&f,"transform:rotate(90deg);opacity:.5","opacity:.5"); rectangle(f.target,(float[]){70,120,10,20}); normal(f.target,ordinary);
    uint32_t pixel = f.canvas.px[130 * W + 75];
    yes((pixel >> 24)==255 && ((pixel >> 16)&255)>=247 && ((pixel >> 8)&255)>=198 && ((pixel >> 8)&255)<=201 && (pixel&255)>=202 && (pixel&255)<=205,"two actual opacity groups composite the rotated source color");
    yes(f.canvas.px[125 * W + 135]==white,"nested opacity does not retain old untransformed pixels");
    hit=layout_hit(f.layout,75.5f,130.5f,0,0);yes(hit&&hit->node==f.target,"opacity preserves inverse hit target");
    assert(checks == 206);
    primitives_and_damage();
    assert(checks == 218);
    case_name = "near-singular inverse footprint uses bounded exact sample fallback";
    setup(&f,"left:100px;top:100px","left:0;top:0;width:10000000px;height:10000000px;transform:scale(.00001)");
    rectangle(f.target,(float[]){100,100,100,100});
    yes(f.canvas.px[150*W+150]==red,"huge source actually paints destination interior");
    yes(f.canvas.px[150*W+220]==white,"huge source does not paint outside transformed extent");
    hit=layout_hit(f.layout,150.5f,150.5f,0,0);yes(hit&&hit->node==f.target,"near-singular inverse hit remains exact");
    near(f.target->box->w,10000000,"huge normal-flow width retained");
    yes(g_affine_sample_fallbacks>0,"adaptive tiles execute the constant-size source sample path");
    yes(g_affine_windows>0,"near-singular case uses real source rasterization");
    yes(g_affine_scratch_peak<=4*1024*1024,"near-singular and nested scratch remain bounded");
    assert(checks == 230);
    glyph_ink_and_scale();
    assert(checks == 242);
    anonymous_transform_owners();
    assert(checks == 269);
    destroy(&f); printf("PASS css_affine_rendering %u native checks\n",checks); return 0;
}
