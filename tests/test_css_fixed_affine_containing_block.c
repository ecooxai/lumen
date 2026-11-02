/* The actual fixed containing block selects one coordinate frame for layout,
 * viewport geometry, clipping, rasterization and inverse hit testing. */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 640, H = 480 };
static unsigned checks;
static const char *case_name;
static const uint32_t green = 0xff20a040u, white = 0xffffffffu;
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

int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://fixed-affine-containing-block.test/");
    const char *html = "<!doctype html><html><body><div id=parent><div id=middle>"
        "<div id=fixed></div></div><div id=spacer></div></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    engine->media = (MediaCtx){W, H, 1, false};
    const char *css = "html,body{margin:0;padding:0;border:0;width:1000px;height:1200px}"
        "#parent{position:absolute;left:100px;top:100px;width:100px;height:100px;transform-origin:0 0}"
        "#middle{position:absolute;left:20px;top:20px;width:20px;height:10px}"
        "#fixed{position:fixed;left:30px;top:40px;width:12px;height:8px;background:#20a040}"
        "#spacer{width:300px;height:300px;pointer-events:none}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Node *parent = doc_get_element_by_id(doc, "parent");
    Node *fixed = doc_get_element_by_id(doc, "fixed");
    Layout *layout = layout_new(); DisplayList list = {0}; Canvas canvas;
    canvas_init(&canvas, W, H, 1);
    enum { PARENT_CB, VIEWPORT_CB, HTML_CB };
    static const struct {
        const char *name, *parent_style, *fixed_style, *html_style;
        float rect[4], normal[4], scroll_x, scroll_y, element_x, element_y;
        int px, py, ox, oy, cb; bool viewport;
    } cases[] = {
        {"element-fixed follows the actual rotated containing block", "transform:rotate(90deg)", "", "",
         {52,130,8,12}, {130,140,12,8}, 0,0,0,0, 56,135,136,144, PARENT_CB,false},
        {"element-fixed retains document scrolling outside the transform", "transform:rotate(90deg)", "", "",
         {32,100,8,12}, {130,140,12,8}, 20,30,0,0, 36,105,116,114, PARENT_CB,false},
        {"element-fixed scrolls inside its transformed containing block", "transform:rotate(90deg);overflow:hidden", "", "",
         {72,120,8,12}, {130,140,12,8}, 0,0,10,20, 76,125,56,135, PARENT_CB,false},
        {"element-fixed combines element and document scroll in their own frames", "transform:rotate(90deg);overflow:hidden", "", "",
         {52,90,8,12}, {130,140,12,8}, 20,30,10,20, 56,95,36,105, PARENT_CB,false},
        {"element-fixed keeps its actual containing block clip", "transform:rotate(90deg);overflow:hidden", "left:90px;width:20px", "",
         {52,190,8,20}, {190,140,20,8}, 0,0,0,0, 56,195,56,205, PARENT_CB,false},
        {"identity transform remains an element fixed containing block", "transform:rotate(0deg)", "", "",
         {110,110,12,8}, {130,140,12,8}, 20,30,0,0, 115,114,135,144, PARENT_CB,false},
        {"true viewport-fixed still ignores document scroll", "transform:none", "", "",
         {30,40,12,8}, {30,40,12,8}, 20,30,0,0, 35,44,115,114, VIEWPORT_CB,true},
        {"element-fixed inside a viewport-fixed transform inherits the outer fixed frame", "position:fixed;left:80px;top:90px;transform:rotate(90deg)", "", "",
         {32,120,8,12}, {110,130,12,8}, 20,30,0,0, 36,125,115,134, PARENT_CB,false},
        {"real transformed html is distinct from the synthetic viewport root", "transform:none", "", "transform:translate(11px,13px)",
         {21,23,12,8}, {30,40,12,8}, 20,30,0,0, 25,27,35,44, HTML_CB,false},
    };
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        case_name = cases[k].name;
        node_set_attr(parent, "style", cases[k].parent_style);
        node_set_attr(fixed, "style", cases[k].fixed_style);
        node_set_attr(doc->html, "style", cases[k].html_style);
        parent->scroll_x = parent->scroll_y = 0;
        style_recalc(engine, &doc->node, false); layout_run(layout, doc, W, H);
        parent->scroll_x = cases[k].element_x; parent->scroll_y = cases[k].element_y;
        Box *box = fixed->box;
        Box *cb = cases[k].cb == PARENT_CB ? parent->box : cases[k].cb == HTML_CB ? doc->html->box : layout->root;
        yes(box && box->cb == cb, "layout selected the expected actual fixed containing block");
        yes(box_fixed_to_viewport(box) == cases[k].viewport, "shared viewport-fixed classification matches that containing block");
        dl_build(&list, layout, cases[k].scroll_x, cases[k].scroll_y, W, H);
        raster(&canvas, &list, white);
        const float normal[] = {box->x,box->y,box->w,box->h};
        for (int i = 0; i < 4; i++) near(normal[i], cases[k].normal[i], "unmodified normal layout coordinates");
        float r[4]; yes(jsg_vrect(fixed, r, cases[k].scroll_x, cases[k].scroll_y), "actual viewport rectangle exists");
        for (int i = 0; i < 4; i++) near(r[i], cases[k].rect[i], "actual fixed viewport corner union");
        yes(canvas.px[cases[k].py*W+cases[k].px] == green, "actual painted interior uses the same fixed frame");
        yes(canvas.px[cases[k].oy*W+cases[k].ox] == white, "actual excluded or clipped pixel stays empty");
        Box *hit = layout_hit(layout, cases[k].px+.5f+cases[k].scroll_x,
                            cases[k].py+.5f+cases[k].scroll_y, cases[k].scroll_x, cases[k].scroll_y);
        yes(hit && hit->node == fixed, "inverse hit agrees with the actual interior pixel");
        hit = layout_hit(layout, cases[k].ox+.5f+cases[k].scroll_x,
                         cases[k].oy+.5f+cases[k].scroll_y, cases[k].scroll_x, cases[k].scroll_y);
        yes(!hit || hit->node != fixed, "inverse hit excludes the unpainted or clipped point");
        near(parent->scroll_x, cases[k].element_x, "stored horizontal scroll was not mutated");
        near(parent->scroll_y, cases[k].element_y, "stored vertical scroll was not mutated");
        if (k == 4) {
            float clip[4]; yes(jsg_vclip(fixed, NULL, clip, 0, 0), "element-fixed clipping walks its actual containing block");
            const float expected[] = {0,100,100,200};
            for (int i = 0; i < 4; i++) near(clip[i], expected[i], "actual transformed containing block clip");
        }
        if (cases[k].cb == HTML_CB)
            yes(doc->html->box->parent == layout->root && layout->root->node->type == NODE_DOCUMENT,
                "real root element remains separate from the synthetic document root");
    }
    assert(checks == 159);
    canvas_free(&canvas); dl_clear(&list); free(list.items.v);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_fixed_affine_containing_block %u native checks\n", checks); return 0;
}
