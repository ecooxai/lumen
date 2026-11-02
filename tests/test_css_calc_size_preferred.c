/* Direct preferred-size calc-size foundation. Geometry is measured through
 * real DOM/style/layout, with authored numeric expectations. This deliberately
 * does not claim interpolation, min/max calc-size or flex-basis support. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
typedef struct {
    Document *doc;
    StyleEngine *engine;
    Layout *layout;
    Node *container, *probe, *content, *leaf;
} Fixture;
static void yes(const char *name, bool actual) {
    checks++;
    if (!actual) { fprintf(stderr, "%s\n", name); abort(); }
}
static void near(const char *name, float actual, float expected) {
    checks++;
    if (!isfinite(actual) || fabsf(actual - expected) > .02f) {
        fprintf(stderr, "%s: %g, expected %g\n", name, actual, expected); abort();
    }
}
static Box *box(Node *node) { assert(node && node->box); return node->box; }
static void refresh(Fixture *f) {
    style_recalc(f->engine, &f->doc->node, false);
    layout_run(f->layout, f->doc, f->engine->media.vw, f->engine->media.vh);
}
static void setup(Fixture *f, const char *declaration) {
    node_set_attr(f->container, "style", "width:500px;height:100px");
    node_set_attr(f->probe, "style", declaration);
    node_set_attr(f->content, "style", "width:70px;height:10px");
    node_set_attr(f->leaf, "style", "width:7px;height:10px");
    refresh(f);
}
static void css_pixels(Node *node, const char *property, float expected) {
    char *text = css_get_computed_value(node, property), *end = NULL;
    float value = text ? strtof(text, &end) : NAN;
    if (!text || end == text || strcmp(end, "px")) {
        fprintf(stderr, "%s has non-pixel resolved value '%s'\n", property, text ? text : "(null)");
        abort();
    }
    near(property, value, expected); free(text);
}
static void measured(Node *node, bool height, float border_size, float css_size) {
    Length value = height ? node->style->height : node->style->width;
    yes("preferred size retains a native sizing value", len_calc_size(value));
    near(height ? "real height" : "real width", height ? box(node)->h : box(node)->w, border_size);
    css_pixels(node, height ? "height" : "width", css_size);
}
static void snapshot(Node *node, const char *property, const char *expected) {
    char *value = css_get_animation_value(node, property);
    if (!value || strcmp(value, expected))
        fprintf(stderr, "%s snapshot '%s', expected '%s'\n", property, value ? value : "(null)", expected);
    yes("private snapshot preserves computed sizing expression", value && !strcmp(value, expected));
    free(value);
}

int main(void) {
    dom_init(); font_init();
    Fixture f = {0};
    f.doc = doc_new("https://calc-size-preferred.test/");
    const char *html = "<!doctype html><html><body><div id='container'><div id='probe'>"
        "<div id='content'><div id='leaf'></div></div></div></div></body></html>";
    html_parse(f.doc, html, strlen(html));
    f.engine = style_engine_new(f.doc); f.engine->media = (MediaCtx){800, 600, 1, false};
    const char *sheet = "html,body{margin:0;padding:0;font-size:16px}"
        "#container{position:relative;margin-left:20px;margin-top:30px;width:500px;height:100px}"
        "#probe,#content,#leaf{margin:0;padding:0;border:0 solid;font-size:20px}"
        "#content{width:70px;height:10px}#leaf{width:7px;height:10px}";
    style_engine_add_sheet(f.engine, css_parse_sheet(sheet, strlen(sheet), f.doc->url, 1, &f.engine->media));
    f.container = doc_get_element_by_id(f.doc, "container");
    f.probe = doc_get_element_by_id(f.doc, "probe");
    f.content = doc_get_element_by_id(f.doc, "content");
    f.leaf = doc_get_element_by_id(f.doc, "leaf");
    f.layout = layout_new(); refresh(&f);
    yes("Length storage remains 28 bytes", sizeof(Length) == 28);

    static const struct { const char *value; float expected; } widths[] = {
        {"calc-size(auto, size)", 500},
        {"calc-size(auto, size * .6 + 23px)", 323},
        {"calc-size(any, 357px)", 357},
        {"calc-size(any, 31%)", 155},
        {"calc-size(83px, size * 3)", 249},
        {"calc-size(25%, size * 2)", 250},
        {"calc-size(max-content, size * 1.2)", 84},
        {"calc-size(min-content, size / 2)", 35},
        {"calc-size(fit-content, size / 2 + 30px)", 65},
        {"calc-size(stretch, size / 2 - 10%)", 200},
        {"calc-size(calc-size(any, 300px), size + 1px)", 301},
        {"calc-size(calc-size(min-content, size / 2), size + 10%)", 85},
        {"calc-size(auto, size * 0 + 20px)", 20},
        {"calc-size(auto, -1 * size)", 0},
        {"calc-size(any, 2em + 10%)", 90},
        {"calc-size(calc(10px + 10%), size * 2)", 120},
        {"calc-size(calc-size(auto, size / 2), size + 20px)", 270}
    };
    char declaration[512];
    for (unsigned i = 0; i < sizeof widths / sizeof widths[0]; i++) {
        snprintf(declaration, sizeof declaration, "width:%s;height:40px", widths[i].value);
        setup(&f, declaration); measured(f.probe, false, widths[i].expected, widths[i].expected);
    }
    assert(checks == 52);

    static const struct { const char *value; float expected; } heights[] = {
        {"calc-size(auto, size)", 10},
        {"calc-size(auto, 2 * size + 5px)", 25},
        {"calc-size(any, 31%)", 31},
        {"calc-size(31%, size)", 31},
        {"calc-size(23px, 3 * size)", 69},
        {"calc-size(min-content, size / 2)", 5},
        {"calc-size(max-content, 31%)", 31},
        {"calc-size(fit-content, size / 2 + 30px)", 35},
        {"calc-size(calc-size(any, 20px), size * 2)", 40},
        {"calc-size(auto, size - 20px)", 0},
        {"calc-size(any, 0%)", 0},
        {"calc-size(calc(10px + 10%), size * 2)", 40},
        {"calc-size(stretch, size / 2)", 50}
    };
    for (unsigned i = 0; i < sizeof heights / sizeof heights[0]; i++) {
        snprintf(declaration, sizeof declaration, "width:80px;height:%s", heights[i].value);
        setup(&f, declaration); measured(f.probe, true, heights[i].expected, heights[i].expected);
    }
    assert(checks == 91);

    /* A percentage in the calculation becomes zero for an indefinite basis;
     * a percentage sizing basis retains ordinary automatic-height behavior. */
    static const struct { const char *value; float expected; } indefinite[] = {
        {"calc-size(31%, size)", 10}, {"calc-size(any, 31%)", 0},
        {"calc-size(max-content, 31%)", 0}, {"calc-size(auto, size / 2 + 30px)", 35}
    };
    for (unsigned i = 0; i < sizeof indefinite / sizeof indefinite[0]; i++) {
        snprintf(declaration, sizeof declaration, "width:80px;height:%s", indefinite[i].value);
        setup(&f, declaration); node_set_attr(f.container, "style", "width:500px;height:auto"); refresh(&f);
        measured(f.probe, true, indefinite[i].expected, indefinite[i].expected);
    }
    assert(checks == 103);

    /* Same arithmetic as the unchanged upstream box-sizing controls: 7px
     * content plus 12px total padding/border in each physical axis. */
    for (unsigned axis = 0; axis < 2; axis++) for (unsigned border = 0; border < 2; border++)
        for (unsigned numeric = 0; numeric < 2; numeric++) {
            snprintf(declaration, sizeof declaration,
                "display:inline-block;width:7px;height:7px;%s:calc-size(%s, size * 2);padding:4px;border:2px solid;box-sizing:%s",
                axis ? "height" : "width", numeric ? "11px" : "auto", border ? "border-box" : "content-box");
            setup(&f, declaration);
            node_set_attr(f.content, "style", "width:7px;height:7px");
            node_set_attr(f.leaf, "style", "width:7px;height:7px"); refresh(&f);
            float expected_border = numeric ? (border ? 22 : 34) : (border ? 38 : 26);
            measured(f.probe, axis != 0, expected_border, expected_border - (border ? 0 : 12));
        }
    assert(checks == 127);

    /* Preserve the ordinary non-calc border-box floor as a regression control. */
    setup(&f, "width:1px;height:1px;box-sizing:border-box;padding:4px;border:2px solid");
    near("ordinary narrow border box", box(f.probe)->w, 12); css_pixels(f.probe, "width", 12);
    near("ordinary short border box", box(f.probe)->h, 12); css_pixels(f.probe, "height", 12);
    assert(checks == 131);

    /* Preferred-size calculation precedes the element's own min/max limits. */
    setup(&f, "width:calc-size(auto, size / 2);max-width:100px;height:40px"); measured(f.probe, false, 100, 100);
    setup(&f, "display:inline-block;width:calc-size(auto, size * 2);min-width:100px;height:40px"); measured(f.probe, false, 140, 140);
    setup(&f, "width:80px;height:calc-size(auto, size / 2);max-height:8px"); measured(f.probe, true, 5, 5);
    setup(&f, "width:80px;height:calc-size(auto, size * 3);min-height:20px"); measured(f.probe, true, 30, 30);
    assert(checks == 143);

    /* The outer function keeps its basis classification even if the formula
     * is constant. Descendant percentage heights must see that distinction. */
    setup(&f, "width:80px;height:calc-size(auto, 100px)");
    node_set_attr(f.content, "style", "width:70px;height:50%"); refresh(&f);
    measured(f.probe, true, 100, 100); near("auto basis keeps child percentage indefinite", box(f.content)->h, 10);
    yes("auto basis does not become a definite constant", !len_def(f.probe->style->height, 100));
    setup(&f, "width:80px;height:calc-size(any, 100px)");
    node_set_attr(f.content, "style", "width:70px;height:50%"); refresh(&f);
    measured(f.probe, true, 100, 100); near("any basis gives child a definite height", box(f.content)->h, 50);
    yes("any basis is definite", len_def(f.probe->style->height, -1));
    assert(checks == 153);

    /* Incremental content changes recalculate the intrinsic basis. */
    setup(&f, "width:80px;height:calc-size(auto, 2 * size)"); measured(f.probe, true, 20, 20);
    node_set_attr(f.content, "style", "width:70px;height:30px"); refresh(&f); measured(f.probe, true, 60, 60);
    node_set_attr(f.content, "style", "width:70px;height:40px"); refresh(&f); measured(f.probe, true, 80, 80);
    setup(&f, "display:inline-block;width:calc-size(auto, 2 * size);height:40px"); measured(f.probe, false, 140, 140);
    node_set_attr(f.content, "style", "width:90px;height:10px"); refresh(&f); measured(f.probe, false, 180, 180);
    assert(checks == 168);

    /* Inherited calc-size contains already-computed font-relative lengths. */
    setup(&f, "font-size:10px;width:inherit;height:40px");
    node_set_attr(f.container, "style", "font-size:20px;width:calc-size(any, 2em);height:100px"); refresh(&f);
    measured(f.container, false, 40, 40); measured(f.probe, false, 40, 40);
    snapshot(f.probe, "width", "calc-size(any, 40px)");
    node_set_attr(f.container, "style", "font-size:30px;width:calc-size(any, 2em);height:100px"); refresh(&f);
    measured(f.container, false, 60, 60); measured(f.probe, false, 60, 60);
    snapshot(f.probe, "width", "calc-size(any, 60px)");
    assert(checks == 182);

    setup(&f, "width:calc-size(auto, size / 2 + 10%);height:40px"); measured(f.probe, false, 300, 300);
    char *before = css_get_animation_value(f.probe, "width");
    node_set_attr(f.container, "style", "width:600px;height:100px"); refresh(&f);
    measured(f.probe, false, 360, 360);
    char *after = css_get_animation_value(f.probe, "width");
    yes("live containing-block width does not replace computed percentages", before && after && !strcmp(before, after));
    free(before); free(after);
    assert(checks == 189);

    setup(&f, "width:calc-size(any, 10vh);height:40px"); measured(f.probe, false, 60, 60);
    yes("calculation records viewport-height dependency", f.probe->style->uses_vh);
    uint64_t version = f.doc->dom_version;
    f.engine->media.vh = 800; refresh(&f);
    yes("viewport resize needs no DOM mutation", f.doc->dom_version == version);
    measured(f.probe, false, 80, 80); f.engine->media.vh = 600;
    assert(checks == 197);

    /* Deferred values resolve in their actual style context, with IACVT
     * falling back to the initial preferred size rather than an older value. */
    setup(&f, "--basis:20px;--ratio:2;width:calc-size(var(--basis), var(--ratio) * size);height:40px");
    measured(f.probe, false, 40, 40);
    node_set_attr(f.probe, "style", "--basis:20px;--ratio:3;width:calc-size(var(--basis), var(--ratio) * size);height:40px");
    refresh(&f); measured(f.probe, false, 60, 60);
    node_set_attr(f.probe, "style", "--basis:20px;--ratio:invalid;width:60px;width:calc-size(var(--basis), var(--ratio) * size);height:40px");
    refresh(&f); near("invalid substitution resets to automatic width", box(f.probe)->w, 500);
    yes("invalid substitution clears the sizing representation", !len_calc_size(f.probe->style->width));
    assert(checks == 205);

    static const char *const invalid[] = {
        "calc-size(any, size)", "calc-size(none, size)", "calc-size(content, size)",
        "calc-size(0, 1px)", "calc-size(any, 0)", "calc-size(any, 1deg)",
        "calc-size(auto, size * size)", "calc-size(auto, size / size)", "calc-size(auto, 1px, 2px)"
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        snprintf(declaration, sizeof declaration, "width:60px;width:%s;height:40px", invalid[i]);
        setup(&f, declaration);
        near("invalid declaration retains prior native width", box(f.probe)->w, 60);
        yes("invalid declaration does not partially replace native value", !len_calc_size(f.probe->style->width));
    }
    assert(checks == 223);

    setup(&f, "font-size:10px;width:inherit;height:40px");
    node_set_attr(f.container, "style", "font-size:20px;width:calc-size(calc-size(any, 2em), size + 1px);height:100px");
    refresh(&f); measured(f.probe, false, 41, 41);
    snapshot(f.probe, "width", "calc-size(calc-size(any, 40px), 1px + size)");
    assert(checks == 227);

    setup(&f, "width:calc-size(calc-size(any, 300px), size + 1px);height:40px");
    snapshot(f.probe, "width", "calc-size(calc-size(any, 300px), 1px + size)");
    node_set_attr(f.probe, "style", "display:none;width:calc-size(calc-size(any, 300px), size + 1px)"); refresh(&f);
    char *computed = css_get_computed_value(f.probe, "width");
    yes("no-box computed value retains nested basis", computed && !strcmp(computed, "calc-size(calc-size(any, 300px), 1px + size)"));
    free(computed);
    setup(&f, "width:calc-size(auto, size * .6 + 23px);height:40px");
    Box *target = box(f.probe);
    Box *hit = layout_hit(f.layout, target->x + 100, target->y + 20, 0, 0);
    yes("native hit testing uses the calculated border box", hit && hit->node == f.probe);
    assert(checks == 230);

    /* Original source tokens must be validated before comments or signed
     * adjacency can be normalized into a different, valid calculation. */
    static const char *const unspaced[] = {
        "size +1px", "size+ 1px", "size/**/+/**/1px", "size/**/+ 1px", "size +/**/1px"
    };
    for (unsigned i = 0; i < sizeof unspaced / sizeof unspaced[0]; i++) {
        snprintf(declaration, sizeof declaration, "width:60px;width:calc-size(auto, %s);height:40px", unspaced[i]);
        setup(&f, declaration);
        near("raw invalid math spacing preserves previous declaration", box(f.probe)->w, 60);
        yes("raw invalid math spacing cannot manufacture sizing state", !len_calc_size(f.probe->style->width));
    }
    setup(&f, "width:calc-size(auto, size /*actual spaces*/ + /*actual spaces*/ 1px);height:40px");
    measured(f.probe, false, 501, 501);
    setup(&f, "--delta:1px/**/+/**/2px;width:60px;width:calc-size(any, var(--delta));height:40px");
    near("invalid substituted original math tokens take initial width", box(f.probe)->w, 500);
    yes("invalid substituted math tokens clear sizing state", !len_calc_size(f.probe->style->width));
    assert(checks == 245);

    /* Finite accepted coefficients can exceed the layout float range only
     * after resolving the real sizing basis. Clamp that completed value;
     * do not clamp an intermediate that a later operation brings in range. */
    setup(&f, "width:calc-size(1e38px, size * 4);height:40px");
    yes("large finite width retains sizing state", len_calc_size(f.probe->style->width));
    yes("completed large width remains finite", isfinite(box(f.probe)->w) && box(f.probe)->w >= 1e38f);
    setup(&f, "width:80px;height:calc-size(1e38px, size * 4)");
    yes("large finite height retains sizing state", len_calc_size(f.probe->style->height));
    yes("completed large height remains finite", isfinite(box(f.probe)->h) && box(f.probe)->h >= 1e38f);

    setup(&f, "width:calc-size(calc(1e38px + 1e38%), size * .25);height:40px");
    yes("wide numeric percentage basis retains sizing state", len_calc_size(f.probe->style->width));
    yes("large numeric width basis is reduced before final range conversion",
        isfinite(box(f.probe)->w) && fabs((double)box(f.probe)->w / 1.5e38 - 1) < .00001);
    setup(&f, "width:80px;height:calc-size(calc(1e38px + 1e38%), size * .25)");
    node_set_attr(f.container, "style", "width:500px;height:500px"); refresh(&f);
    yes("tall numeric percentage basis retains sizing state", len_calc_size(f.probe->style->height));
    yes("large numeric height basis is reduced before final range conversion",
        isfinite(box(f.probe)->h) && fabs((double)box(f.probe)->h / 1.5e38 - 1) < .00001);

    setup(&f, "width:calc-size(1e38px, size * 4 - 3e38px);height:40px");
    yes("width cancellation retains sizing state", len_calc_size(f.probe->style->width));
    yes("width arithmetic finishes before range conversion",
        isfinite(box(f.probe)->w) && fabs((double)box(f.probe)->w / 1e38 - 1) < .00001);
    setup(&f, "width:80px;height:calc-size(1e38px, size * 4 - 3e38px)");
    yes("height cancellation retains sizing state", len_calc_size(f.probe->style->height));
    yes("height arithmetic finishes before range conversion",
        isfinite(box(f.probe)->h) && fabs((double)box(f.probe)->h / 1e38 - 1) < .00001);
    assert(checks == 257);

    layout_free(f.layout); style_engine_free(f.engine); doc_free(f.doc);
    printf("PASS css_calc_size_preferred %u native checks\n", checks);
    return 0;
}
