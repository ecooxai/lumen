/* Final native transform-angle consumption. Nested math remains unchanged. */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void yes(bool value, const char *label) {
    if (!value) { fprintf(stderr, "%s\n", label); abort(); }
    checks++;
}
static void near(float actual, float expected, float tolerance, const char *label) {
    if (!isfinite(actual) || fabsf(actual - expected) > tolerance)
        fprintf(stderr, "%s: %g expected %g\n", label, actual, expected);
    yes(isfinite(actual) && fabsf(actual - expected) <= tolerance, label);
}
static void actual_rect_and_hit(Node *n, Layout *l, const float expected[4]) {
    float r[4]; yes(jsg_vrect(n, r, 0, 0), "actual DOM rectangle exists");
    for (unsigned i = 0; i < 4; i++) near(r[i], expected[i], .02f, "actual transformed DOM rectangle");
    Box *hit = layout_hit(l, r[0] + r[2] / 2, r[1] + r[3] / 2, 0, 0);
    yes(hit && hit->node == n, "hit testing follows actual transformed geometry");
}
static void recalc(Document *d, StyleEngine *e, Layout *l) {
    style_recalc(e, &d->node, false); layout_run(l, d, 640, 480);
}
int main(void) {
    dom_init();
    Document *d = doc_new("https://nonfinite-transform.test/");
    const char *html = "<!doctype html><html><body><div id='parent'><div id='target'><div id='fixed'></div></div></div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d); e->media = (MediaCtx){640, 480, 1, false};
    const char *css = "html,body{margin:0;padding:0;border:0;font-size:16px}"
        "#parent{position:absolute;left:100px;top:80px;width:200px;height:200px;margin:0;padding:0;border:0}"
        "#target{position:absolute;left:20px;top:20px;width:20px;height:10px;margin:0;padding:0;border:0;"
        "font-size:16px;transform-origin:0 0;background:#123456}"
        "#fixed{display:none;position:fixed;left:3px;top:4px;width:4px;height:3px;margin:0;padding:0;border:0;background:#abcdef}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    Layout *l = layout_new(); Node *target = doc_get_element_by_id(d, "target"), *fixed = doc_get_element_by_id(d, "fixed");
    assert(target && fixed);
    const struct { const char *name, *value; float matrix[6], rect[4]; } cases[] = {
        {"+infinity deg keeps finite identity geometry", "rotate(calc(infinity * 1deg))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"-infinity deg keeps finite identity geometry", "rotate(calc(-infinity * 1deg))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"+infinity grad keeps finite identity geometry", "rotate(calc(infinity * 1grad))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"-infinity grad keeps finite identity geometry", "rotate(calc(-infinity * 1grad))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"+infinity rad keeps finite identity geometry", "rotate(calc(infinity * 1rad))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"-infinity rad keeps finite identity geometry", "rotate(calc(-infinity * 1rad))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"+infinity turn keeps finite identity geometry", "rotate(calc(infinity * 1turn))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"-infinity turn keeps finite identity geometry", "rotate(calc(-infinity * 1turn))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"positive zero denominator angle keeps finite identity geometry", "rotate(calc(1deg / sign(1em - 16px)))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"negative zero denominator angle keeps finite identity geometry", "rotate(calc(1deg / (-1 * sign(1em - 16px))))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"NaN completed angle keeps finite identity geometry", "rotate(calc(NaN * 1deg))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"ordinary finite ninety degree rotation is retained", "rotate(90deg)", {0, 1, -1, 0, 0, 0}, {110, 100, 10, 20}},
        {"negative finite turns preserve equivalent orientation", "rotate(-450deg)", {0, -1, 1, 0, 0, 0}, {120, 80, 10, 20}},
        {"nested atan sees positive infinity before final angle normalization", "rotate(atan(infinity))", {0, 1, -1, 0, 0, 0}, {110, 100, 10, 20}},
        {"nested atan preserves negative zero division sign", "rotate(atan(1 / (-1 * 0)))", {0, -1, 1, 0, 0, 0}, {120, 80, 10, 20}},
        {"finite translation survives later infinite rotation", "translate(13px, 7px) rotate(calc(infinity * 1deg))", {1, 0, 0, 1, 13, 7}, {133, 107, 20, 10}},
        {"rotateZ shares the completed angle boundary", "rotateZ(calc(-infinity * 1turn))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"skewX shares the completed angle boundary", "skewX(calc(infinity * 1rad))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
        {"skewY shares the completed angle boundary", "skewY(calc(-infinity * 1grad))", {1, 0, 0, 1, 0, 0}, {120, 100, 20, 10}},
    };
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        fprintf(stderr, "TRANSFORM_ANGLE_CASE %s\n", cases[k].name);
        char style[256]; snprintf(style, sizeof style, "transform:%s", cases[k].value);
        node_set_attr(target, "style", style); recalc(d, e, l); assert(target->style && target->box);
        yes(target->style->has_transform, "identity result retains the transform declaration");
        for (unsigned i = 0; i < 6; i++) near(target->style->transform[i], cases[k].matrix[i], .0001f, "actual finite matrix entry");
        near(target->box->x, 120, .02f, "untransformed layout x retained");
        near(target->box->y, 100, .02f, "untransformed layout y retained");
        near(target->box->w, 20, .02f, "untransformed layout width retained");
        near(target->box->h, 10, .02f, "untransformed layout height retained");
        actual_rect_and_hit(target, l, cases[k].rect);
    }
    assert(checks == 323);
    node_set_attr(target, "style", "transform:rotate(calc(infinity * 1deg))");
    node_set_attr(fixed, "style", "display:block"); recalc(d, e, l);
    yes(target->style->has_transform, "zero-turn transform remains a fixed containing block");
    const float transformed_fixed[] = {123, 104, 4, 3}; actual_rect_and_hit(fixed, l, transformed_fixed);
    node_set_attr(target, "style", "transform:none"); recalc(d, e, l);
    yes(!target->style->has_transform, "explicit none releases the fixed containing block");
    const float viewport_fixed[] = {3, 4, 4, 3}; actual_rect_and_hit(fixed, l, viewport_fixed);
    assert(checks == 337);
    layout_free(l); style_engine_free(e); doc_free(d);
    printf("PASS css_transform_angle_boundary %u native checks\n", checks); return 0;
}
