/* CSS2.2 normal block constraints and Position3 static-position rectangles.
 * These assertions inspect native boxes, independent of CSSOM serialization. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { Document *doc; StyleEngine *engine; Layout *layout; } Fixture;
static unsigned checks;
extern int g_lb_hits;
static void number(float actual, float expected, const char *label) {
    if (fabsf(actual - expected) > .01f)
        fprintf(stderr, "%s: native %g, expected %g\n", label, actual, expected);
    assert(isfinite(actual) && fabsf(actual - expected) <= .01f); checks++;
}
static Fixture fixture(const char *body, const char *extra) {
    Fixture f = {0};
    f.doc = doc_new("https://horizontal-rtl-flow.test/");
    char html[4096], css[8192];
    snprintf(html, sizeof html, "<!doctype html><html><body>%s</body></html>", body);
    html_parse(f.doc, html, strlen(html));
    f.engine = style_engine_new(f.doc);
    snprintf(css, sizeof css, "body{margin:0}"
        "#cb{position:absolute;left:40px;top:30px;width:300px;height:160px;padding:10px;border:5px solid;transform:scale(1)}"
        "#flow{width:100px;height:40px;padding:3px;border:2px solid;margin:7px 13px 9px 11px}"
        "#lead{display:block;width:20px;height:12px;margin:0;padding:0;border:0}"
        "#target{position:absolute;display:block;width:20px;height:10px;margin:2px 3px;padding:0;border:0}"
        "%s", extra);
    style_engine_add_sheet(f.engine, css_parse_sheet(css, strlen(css), f.doc->url, 1, &f.engine->media));
    style_recalc(f.engine, &f.doc->node, true);
    f.layout = layout_new(); layout_run(f.layout, f.doc, 640, 480);
    return f;
}
static Box *box(Fixture *f, const char *id) {
    Node *n = doc_get_element_by_id(f->doc, id); assert(n && n->box); return n->box;
}
static void finish(Fixture *f) {
    layout_free(f->layout); style_engine_free(f->engine); doc_free(f->doc);
}
static void static_rectangles(void) {
    /* The actual absolute CB and the static parent have independently chosen
       directions. Changing the positioned child's own axes changes neither. */
    for (int cb_rtl = 0; cb_rtl < 2; cb_rtl++)
    for (int flow_rtl = 0; flow_rtl < 2; flow_rtl++)
    for (int fixed = 0; fixed < 2; fixed++)
    for (int lead = 0; lead < 2; lead++)
    for (int own_vertical = 0; own_vertical < 2; own_vertical++) {
        char css[1024];
        snprintf(css, sizeof css, "#cb{direction:%s}#flow{direction:%s}#lead{display:%s}"
            "#target{position:%s;writing-mode:%s;direction:%s}",
            cb_rtl ? "rtl" : "ltr", flow_rtl ? "rtl" : "ltr", lead ? "block" : "none",
            fixed ? "fixed" : "absolute", own_vertical ? "vertical-rl" : "horizontal-tb",
            own_vertical ? "rtl" : "ltr");
        Fixture f = fixture("<div id='cb'><div id='flow'><div id='lead'></div><div id='target'></div></div></div>", css);
        Box *cb = box(&f, "cb"), *flow = box(&f, "flow"), *target = box(&f, "target");
        float flow_x = cb_rtl ? 232 : 66;
        number(flow->x, flow_x, "static parent x"); number(flow->y, 52, "static parent y");
        number(flow->w, 110, "static parent border width"); number(flow->h, 50, "static parent border height");
        number(target->x, flow_x + (flow_rtl ? 82 : 8), "static target x");
        number(target->y, lead ? 71 : 59, "static target y after real normal flow");
        assert(target->cb == cb); checks++;
        finish(&f);
    }
}
static void margin_constraints(void) {
    /* Expected used margins come from the full margin+border-box constraint;
       negative space exercises the direction-dependent ignored end margin. */
    static const struct {
        const char *declaration;
        float width, ltr_left, ltr_right, rtl_left, rtl_right;
    } cases[] = {
        {"width:100px;margin-left:11px;margin-right:13px",110,11,179,177,13},
        {"width:100px;margin-left:auto;margin-right:13px",110,177,13,177,13},
        {"width:100px;margin-left:11px;margin-right:auto",110,11,179,11,179},
        {"width:100px;margin-left:auto;margin-right:auto",110,95,95,95,95},
        {"width:400px;margin-left:11px;margin-right:13px",410,11,-121,-123,13},
        {"width:400px;margin-left:auto;margin-right:auto",410,0,-110,-110,0},
        {"width:400px;margin-left:11px;margin-right:auto",410,11,-121,-110,0},
        {"width:400px;margin-left:auto;margin-right:13px",410,0,-110,-123,13},
        {"width:280px;margin-left:5px;margin-right:5px",290,5,5,5,5},
        {"width:auto;max-width:100px;margin-left:11px;margin-right:13px",110,11,179,177,13},
        {"width:auto;margin-left:11px;margin-right:13px",276,11,13,11,13}
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
    for (int parent_rtl = 0; parent_rtl < 2; parent_rtl++)
    for (int child_rtl = 0; child_rtl < 2; child_rtl++) {
        char css[1024];
        snprintf(css, sizeof css, "#cb{direction:%s}#flow{%s;direction:%s}",
                 parent_rtl ? "rtl" : "ltr", cases[i].declaration, child_rtl ? "rtl" : "ltr");
        Fixture f = fixture("<div id='cb'><div id='flow'></div></div>", css);
        Box *flow = box(&f, "flow");
        float left = parent_rtl ? cases[i].rtl_left : cases[i].ltr_left;
        float right = parent_rtl ? cases[i].rtl_right : cases[i].ltr_right;
        number(flow->w, cases[i].width, "constrained border width");
        number(flow->m[3], left, "used margin-left"); number(flow->m[1], right, "used margin-right");
        number(flow->x, 55 + left, "used normal-flow position");
        number(flow->y, 52, "normal-flow vertical position unchanged");
        finish(&f);
    }
}
static void resized_container(void) {
    for (int parent_rtl = 0; parent_rtl < 2; parent_rtl++)
    for (int flow_rtl = 0; flow_rtl < 2; flow_rtl++) {
        char css[256]; snprintf(css, sizeof css, "#cb{direction:%s}#flow{direction:%s}",
            parent_rtl ? "rtl" : "ltr", flow_rtl ? "rtl" : "ltr");
        Fixture f = fixture("<div id='cb'><div id='flow'><div id='target'></div></div></div>", css);
        node_set_attr(doc_get_element_by_id(f.doc, "cb"), "style", "width:200px");
        style_recalc(f.engine, &f.doc->node, false); layout_run(f.layout, f.doc, 640, 480);
        float x = parent_rtl ? 132 : 66;
        number(box(&f,"flow")->x, x, "resized static parent");
        number(box(&f,"target")->x, x + (flow_rtl ? 82 : 8), "resized static child");
        number(box(&f,"target")->y, 59, "resized block axis unchanged");
        finish(&f);
    }
}
static void cached_subtree(void) {
    Fixture f = fixture("<div id='cb'><div id='flow'><div id='lead'></div></div></div>",
                        "#cb{direction:rtl}#flow{direction:ltr}");
    Box *flow = box(&f,"flow"), *lead = box(&f,"lead");
    number(flow->x,232,"memo initial parent"); number(lead->x,237,"memo initial descendant");
    /* Run the public no-float layout entry at its authored-margin input,
       then force an actual memo hit after the normal author-margin reset. */
    flow->memo = false;
    compute_mbp(flow,300);
    number(flow->m[3],11,"author margin reset before layout");
    layout_box(f.layout,flow,66,52,300,160,NULL,SZ_FILL,0,-1);
    assert(flow->memo); checks++;
    number(flow->mk[0],166,"memo stores resolved position offset");
    int hits = g_lb_hits;
    compute_mbp(flow,300);
    layout_box(f.layout,flow,66,52,300,160,NULL,SZ_FILL,0,-1);
    assert(g_lb_hits==hits+1); checks++;
    number(flow->x,232,"memo repeated parent has no drift"); number(lead->x,237,"memo repeated descendant has no drift");
    hits=g_lb_hits;
    layout_box(f.layout,flow,29,33,300,160,NULL,SZ_FILL,0,-1);
    assert(g_lb_hits==hits+1); checks++;
    number(flow->x,195,"memo negative translation parent"); number(lead->x,200,"memo negative translation descendant");
    number(flow->y,33,"memo negative block translation parent"); number(lead->y,38,"memo negative block translation descendant");
    finish(&f);
}
static void original_inline_cursor(void) {
    Fixture f = fixture("<div id='cb'><div id='flow'><span id='text'>inline</span><span id='target'></span></div></div>",
                        "#cb{direction:rtl}#flow{direction:rtl;text-align:left}#target{display:inline;margin:0}");
    Box *target=box(&f,"target");
    assert(target->kind==BX_INLINE && target->st->display==D_INLINE); checks++;
    number(target->x,target->sx,"original inline keeps native recorded cursor");
    number(target->y,target->sy,"original inline keeps native recorded line");
    finish(&f);
}
static void original_inline_block_cursor(void) {
    Fixture f = fixture("<div id='cb'><div id='flow'><span id='text'>inline</span><span id='target'></span></div></div>",
                        "#cb{direction:rtl}#flow{direction:rtl;text-align:left}#target{display:inline-block;margin:0}");
    Box *target=box(&f,"target");
    assert(target->kind==BX_ATOMIC && target->st->display==D_INLINE_BLOCK); checks++;
    number(target->x,target->sx,"original inline-block keeps native recorded cursor");
    number(target->y,target->sy,"original inline-block keeps native recorded line");
    finish(&f);
}
static void positioned_pseudo_inline_cursor(void) {
    Fixture f = fixture("<div id='cb'><div id='flow'><span id='text'>inline</span></div></div>",
                        "#cb{direction:rtl}#flow{direction:rtl;text-align:left}"
                        "#flow::after{content:'';display:inline-block;position:absolute;width:20px;height:10px;margin:0}");
    Box *pseudo=box(&f,"flow")->last;
    assert(pseudo && pseudo->anon && pseudo->abs && pseudo->kind==BX_BLOCK && pseudo->st->display==D_INLINE_BLOCK); checks++;
    number(pseudo->x,pseudo->sx,"blockified pseudo retains original inline cursor");
    number(pseudo->y,pseudo->sy,"blockified pseudo retains recorded line");
    finish(&f);
}
int main(void) {
    dom_init(); font_init();
    static_rectangles(); margin_constraints(); resized_container(); cached_subtree(); original_inline_cursor();
    original_inline_block_cursor(); positioned_pseudo_inline_cursor();
    printf("PASS horizontal_rtl_flow %u native checks\n",checks);
    return 0;
}
