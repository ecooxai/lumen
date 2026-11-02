/* Forced absolute/fixed preferred-size calc-size integration. The original
 * 257-check foundation and full upstream family remain separate controls. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static const char *case_name;
typedef struct { Document *doc; StyleEngine *engine; Layout *layout; Node *container, *probe; } Fixture;
static void yes(const char *name, bool actual) {
    checks++;
    if (!actual) { fprintf(stderr, "%s: %s\n", case_name, name); abort(); }
}
static void near(const char *name, float actual, float expected) {
    checks++;
    if (!isfinite(actual) || fabsf(actual - expected) > .02f) {
        fprintf(stderr, "%s: %s %g, expected %g\n", case_name, name, actual, expected); abort();
    }
}
static Box *box(Node *node) { assert(node && node->box); return node->box; }
static void refresh(Fixture *f) {
    style_recalc(f->engine, &f->doc->node, false);
    layout_run(f->layout, f->doc, f->engine->media.vw, f->engine->media.vh);
}
static void setup(Fixture *f, const char *declaration) {
    node_set_attr(f->container, "style", "width:400px;height:300px");
    node_set_attr(f->probe, "style", declaration); refresh(f);
}
static void measured(Fixture *f, bool height, bool calculation, float border_size,
                     float css_size, float x, float y) {
    Box *target = box(f->probe);
    yes("real positioned box", target->abs);
    yes("preferred-size representation", len_calc_size(height ? f->probe->style->height : f->probe->style->width) == calculation);
    near("border-box size", height ? target->h : target->w, border_size);
    char *text = css_get_computed_value(f->probe, height ? "height" : "width"), *end = NULL;
    float value = text ? strtof(text, &end) : NAN;
    yes("resolved CSSOM pixels", text && end != text && !strcmp(end, "px"));
    near("resolved CSSOM size", value, css_size); free(text);
    near("real x", target->x, x); near("real y", target->y, y);
    Box *hit = layout_hit(f->layout, x + 1, y + 1, 0, 0);
    yes("real calculated border-box hit", hit && hit->node == f->probe);
}

int main(void) {
    dom_init(); font_init(); Fixture f = {0};
    f.doc = doc_new("https://calc-size-absolute.test/");
    const char *html = "<!doctype html><html><body><div id='container'><div id='probe'><div id='content'></div></div></div></body></html>";
    html_parse(f.doc, html, strlen(html));
    f.engine = style_engine_new(f.doc); f.engine->media = (MediaCtx){800, 600, 1, false};
    const char *sheet = "html,body{margin:0;padding:0}"
        "#container{position:relative;margin-left:20px;margin-top:30px;width:400px;height:300px}"
        "#probe{position:absolute;left:0;top:0;width:40px;height:40px;margin:0;padding:0;border:0 solid}"
        "#content{width:100px;height:20px;pointer-events:none}";
    style_engine_add_sheet(f.engine, css_parse_sheet(sheet, strlen(sheet), f.doc->url, 1, &f.engine->media));
    f.container = doc_get_element_by_id(f.doc, "container"); f.probe = doc_get_element_by_id(f.doc, "probe");
    f.layout = layout_new();
    static const struct { const char *name, *style; bool height; float size, css, x, y; } simple[] = {
        {"absolute stretch width applies division", "width:calc-size(stretch, size / 4)", false, 100, 100, 20, 30},
        {"opposing inline insets provide auto width basis", "right:0;width:calc-size(auto, size / 2)", false, 200, 200, 20, 30},
        {"absolute stretch height applies division", "height:calc-size(stretch, size / 3)", true, 100, 100, 20, 30},
        {"opposing block insets provide auto height basis", "bottom:0;height:calc-size(auto, size / 2)", true, 150, 150, 20, 30},
    };
    for (unsigned i = 0; i < sizeof simple / sizeof simple[0]; i++) {
        case_name = simple[i].name; setup(&f, simple[i].style);
        measured(&f, simple[i].height, true, simple[i].size, simple[i].css, simple[i].x, simple[i].y);
    }
    assert(checks == 32);

    /* Insets and margins reduce the keyword basis, while the calculation's
     * percentage still uses the original 400px/300px containing block. */
    char declaration[512], name[128];
    for (unsigned axis = 0; axis < 2; axis++) for (unsigned border = 0; border < 2; border++)
        for (unsigned automatic = 0; automatic < 2; automatic++) {
            snprintf(name, sizeof name, "%s %s %s retains separate sizing bases", axis ? "height" : "width",
                border ? "border-box" : "content-box", automatic ? "auto" : "stretch"); case_name = name;
            snprintf(declaration, sizeof declaration,
                "%s;padding:4px;border:2px solid;box-sizing:%s;%s:calc-size(%s, size / 2 + 10%%)",
                axis ? "top:20px;bottom:30px;margin-top:10px;margin-bottom:14px" :
                       "left:20px;right:30px;margin-left:10px;margin-right:14px",
                border ? "border-box" : "content-box", axis ? "height" : "width", automatic ? "auto" : "stretch");
            setup(&f, declaration);
            float expected = axis ? (border ? 143 : 149) : (border ? 203 : 209);
            measured(&f, axis != 0, true, expected, expected - (border ? 0 : 12), axis ? 20 : 50, axis ? 60 : 30);
        }
    assert(checks == 96);

    case_name = "negative calculation keeps content-box padding floor";
    setup(&f, "right:0;width:calc-size(stretch, -1 * size);padding:4px;border:2px solid");
    measured(&f, false, true, 12, 0, 20, 30);
    case_name = "negative inset space floors basis before border-box arithmetic";
    setup(&f, "top:300px;bottom:300px;height:calc-size(auto, size * 2);padding:4px;border:2px solid;box-sizing:border-box");
    measured(&f, true, true, 24, 24, 20, 330);
    case_name = "fixed auto width uses viewport basis";
    setup(&f, "position:fixed;left:10px;right:30px;top:20px;width:calc-size(auto, size / 2)");
    measured(&f, false, true, 380, 380, 10, 20);
    case_name = "fixed stretch height uses viewport basis";
    setup(&f, "position:fixed;left:20px;top:10px;bottom:30px;height:calc-size(stretch, size / 4)");
    measured(&f, true, true, 140, 140, 20, 10);
    assert(checks == 128);

    case_name = "width constraints follow calculation";
    setup(&f, "width:calc-size(stretch, size / 2);max-width:250px"); measured(&f, false, true, 200, 200, 20, 30);
    setup(&f, "width:calc-size(stretch, size / 2);max-width:180px"); measured(&f, false, true, 180, 180, 20, 30);
    setup(&f, "width:calc-size(stretch, size / 2);min-width:230px"); measured(&f, false, true, 230, 230, 20, 30);
    case_name = "height constraints follow calculation";
    setup(&f, "bottom:0;height:calc-size(auto, size / 2);max-height:200px"); measured(&f, true, true, 150, 150, 20, 30);
    setup(&f, "bottom:0;height:calc-size(auto, size / 2);max-height:120px"); measured(&f, true, true, 120, 120, 20, 30);
    setup(&f, "bottom:0;height:calc-size(auto, size / 2);min-height:180px"); measured(&f, true, true, 180, 180, 20, 30);
    assert(checks == 176);

    case_name = "ordinary forced width paths remain unchanged";
    setup(&f, "width:stretch"); measured(&f, false, false, 400, 400, 20, 30);
    setup(&f, "right:0;width:auto"); measured(&f, false, false, 400, 400, 20, 30);
    case_name = "ordinary forced height limits remain unchanged";
    setup(&f, "height:stretch;max-height:200px"); measured(&f, true, false, 200, 200, 20, 30);
    setup(&f, "bottom:0;height:auto;max-height:200px"); measured(&f, true, false, 200, 200, 20, 30);
    assert(checks == 208);

    case_name = "live containing block and insets recalculate forced width";
    setup(&f, "left:20px;right:30px;width:calc-size(auto, size / 2 + 10%)"); measured(&f, false, true, 215, 215, 40, 30);
    node_set_attr(f.container, "style", "width:600px;height:300px"); refresh(&f); measured(&f, false, true, 335, 335, 40, 30);
    node_set_attr(f.probe, "style", "left:20px;right:100px;width:calc-size(auto, size / 2 + 10%)"); refresh(&f); measured(&f, false, true, 300, 300, 40, 30);
    case_name = "live containing block and insets recalculate forced height";
    setup(&f, "top:20px;bottom:30px;height:calc-size(stretch, size / 2 + 10%)"); measured(&f, true, true, 155, 155, 20, 50);
    node_set_attr(f.container, "style", "width:400px;height:500px"); refresh(&f); measured(&f, true, true, 275, 275, 20, 50);
    node_set_attr(f.probe, "style", "top:20px;bottom:100px;height:calc-size(stretch, size / 2 + 10%)"); refresh(&f); measured(&f, true, true, 240, 240, 20, 50);
    case_name = "nested stretch calculation applies exactly once";
    setup(&f, "width:calc-size(calc-size(stretch, size / 2), size + 10%)"); measured(&f, false, true, 240, 240, 20, 30);
    assert(checks == 264);
    layout_free(f.layout); style_engine_free(f.engine); doc_free(f.doc);
    printf("PASS css_calc_size_absolute %u native checks\n", checks); return 0;
}
