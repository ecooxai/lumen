/* Radius parsing, actual two-axis geometry and owned-style lifetime controls.
 * These are native regression checks, not WPT assertions or a browser score. */
#include "../src/paint/paint.h"
#include "../src/base/rounded-rect.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
extern long g_styles_live;
static void yes(bool condition, const char *label) {
    checks++;
    if (!condition) { failures++; fprintf(stderr, "FAIL radius: %s\n", label); }
}
static void near_value(double actual, double expected, double tolerance, const char *label) {
    checks++;
    if (!isfinite(actual) || fabs(actual - expected) > tolerance) {
        failures++; fprintf(stderr, "FAIL radius: %s: %.12g expected %.12g (+/- %.3g)\n",
                            label, actual, expected, tolerance);
    }
}
static void apply(ComputedStyle *s, const ComputedStyle *parent, const char *property, const char *value) {
    css_apply_decl(s, parent, property, value, NULL, NULL);
}
static char *computed(ComputedStyle *s, const char *property) {
    Node node = {0}; node.type = NODE_ELEMENT; node.style = s;
    return css_get_computed_value(&node, property);
}
static void value_is(ComputedStyle *s, const char *property, const char *expected, const char *label) {
    char *actual = computed(s, property);
    checks++;
    if (!actual || strcmp(actual, expected)) {
        failures++; fprintf(stderr, "FAIL radius: %s: [%s] expected [%s]\n",
                            label, actual ? actual : "(null)", expected);
    }
    free(actual);
}
#ifdef RADIUS_WRAP_MALLOC
static bool fail_radius_malloc;
static unsigned failed_radius_allocations;
void *__real_malloc(size_t bytes);
void *__wrap_malloc(size_t bytes) {
    if (fail_radius_malloc && bytes == 4 * sizeof(Length)) {
        fail_radius_malloc = false; failed_radius_allocations++; return NULL;
    }
    return __real_malloc(bytes);
}
#endif

static void syntax_and_computed(void) {
    ComputedStyle *s = style_new_default();
    value_is(s, "border-radius", "0px", "default canonical shorthand");
    yes(!s->border_radius_y, "default has no optional allocation");
    struct { const char *input, *expected; } valid[] = {
        {"10px", "10px"}, {"10px 20px", "10px 20px"},
        {"10px 20px 30px", "10px 20px 30px"},
        {"10px 20px 30px 40px", "10px 20px 30px 40px"},
        {"10px 20px 10px 20px", "10px 20px"},
        {"10px 20px 30px 20px", "10px 20px 30px"},
        {"10px 20px 30px 40px / 50px 60px 70px 80px",
         "10px 20px 30px 40px / 50px 60px 70px 80px"},
        {"10px 10px 10px 10px / 20px 20px 20px 20px", "10px / 20px"},
        {"10px 20px / 10px 20px", "10px 20px"},
        {"50%", "50%"}, {"0", "0px"}, {"calc(2px + 3px)", "5px"},
        {"calc(1px + 2px", "3px"},
    };
    for (unsigned i = 0; i < sizeof valid / sizeof *valid; i++) {
        yes(css_supports_native_value("border-radius", valid[i].input, false), "valid typed radius support");
        apply(s, NULL, "border-radius", valid[i].input);
        value_is(s, "border-radius", valid[i].expected, valid[i].input);
    }
    apply(s, NULL, "border-top-left-radius", "30px 40%");
    value_is(s, "border-top-left-radius", "30px 40%", "corner preserves percentage Y");
    apply(s, NULL, "-webkit-border-bottom-right-radius", "50% 60px");
    value_is(s, "-webkit-border-bottom-right-radius", "50% 60px", "prefixed corner aliases native pair");
    apply(s, NULL, "-webkit-border-radius", "7px / 9px");
    value_is(s, "border-radius", "7px / 9px", "prefixed shorthand aliases actual native axes");
    s->font_size = 40;
    apply(s, NULL, "border-top-left-radius", "calc(-0.5em + 10px)");
    value_is(s, "border-top-left-radius", "0px", "negative computed math clamps to zero");
    apply(s, NULL, "border-top-right-radius", "40px 0px");
    value_is(s, "border-top-right-radius", "40px 0px", "computed zero Y retains X");
    struct { const char *property, *input; } invalid[] = {
        {"border-radius", "auto"}, {"border-radius", "none"},
        {"border-radius", "-1px"}, {"border-radius", "-2%"},
        {"border-radius", "1px 2px 3px 4px 5px"}, {"border-radius", "1px /"},
        {"border-radius", "/ 1px"}, {"border-radius", "1px / 2px / 3px"},
        {"border-radius", "1"}, {"border-radius", "calc(0)"},
        {"border-radius", "min(0, 1)"}, {"border-radius", "max(0, 1)"},
        {"border-radius", "clamp(0, 1, 2)"}, {"border-radius", "calc(1px / 1px)"},
        {"border-radius", "1foo"}, {"border-radius", "1px, 2px"},
        {"border-top-left-radius", "1px 2px 3px"},
        {"border-bottom-right-radius", "1px / 2px"},
        {"-webkit-border-radius", "1px / 2px / 3px"},
    };
    apply(s, NULL, "border-radius", "11px 12px 13px 14px / 21px 22px 23px 24px");
    char *before = computed(s, "border-radius");
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        yes(!css_supports_native_value(invalid[i].property, invalid[i].input, false), invalid[i].input);
        apply(s, NULL, invalid[i].property, invalid[i].input);
        value_is(s, "border-radius", before, "invalid radius leaves all eight values unchanged");
    }
    free(before); style_free(s);
}

static void ownership(void) {
    long initial = g_styles_live;
    ComputedStyle *parent = style_new_default();
    apply(parent, NULL, "border-radius", "10px 20px / 30px 40px");
    ComputedStyle *ordinary = style_inherit(parent);
    value_is(ordinary, "border-radius", "0px", "border radii do not inherit implicitly");
    ComputedStyle *child = style_new_default();
    apply(child, parent, "border-radius", "inherit");
    yes(child->border_radius_y && child->border_radius_y != parent->border_radius_y,
        "explicit inheritance owns an independent Y allocation");
    apply(parent, NULL, "border-top-left-radius", "99px 88px");
    value_is(child, "border-top-left-radius", "10px 30px", "parent edit cannot mutate inherited axes");
    style_free(parent);
    value_is(child, "border-radius", "10px 20px / 30px 40px", "inherited axes survive parent destruction");
    apply(child, NULL, "border-top-left-radius", "initial");
    value_is(child, "border-top-right-radius", "20px 40px", "corner reset preserves other Y corners");
    apply(child, NULL, "border-radius", "unset");
    value_is(child, "border-radius", "0px", "unset resets noninherited shorthand");
    yes(!child->border_radius_y, "reset releases redundant Y allocation");
    apply(child, NULL, "border-radius", "13px / 17px");
    apply(child, NULL, "border-radius", "13px");
    yes(!child->border_radius_y, "equal axes compact back to shared storage");
    style_ref(child); style_free(child);
    value_is(child, "border-radius", "13px", "remaining reference retains valid owned state");
    style_free(child); style_free(ordinary);
#ifdef RADIUS_WRAP_MALLOC
    ComputedStyle *atomic = style_new_default();
    apply(atomic, NULL, "border-radius", "11px");
    unsigned old_failed = failed_radius_allocations;
    fail_radius_malloc = true;
    apply(atomic, NULL, "border-radius", "15px / 25px");
    yes(!fail_radius_malloc && failed_radius_allocations == old_failed + 1,
        "exactly one intended optional Y allocation was rejected");
    fail_radius_malloc = false;
    value_is(atomic, "border-radius", "11px", "failed allocation preserves prior X and Y atomically");
    yes(!atomic->border_radius_y, "failed allocation preserves prior NULL ownership");
    style_free(atomic);
#endif
    for (unsigned i = 0; i < 1000; i++) {
        ComputedStyle *a = style_new_default(), *b = style_new_default();
        apply(a, NULL, "border-radius", i & 1 ? "4px 8px / 12px 16px" : "9% / 13%");
        apply(b, a, "border-radius", "inherit");
        if (!b->border_radius_y || b->border_radius_y == a->border_radius_y) {
            yes(false, "stress inheritance must own a distinct allocation"); style_free(a); style_free(b); break;
        }
        style_ref(a); style_free(a);
        apply(a, NULL, "border-radius", "initial");
        apply(b, NULL, "border-top-left-radius", "20px 30px");
        style_free(a); style_free(b);
    }
    yes(g_styles_live == initial, "1000 owned inheritance/reset/refcount cycles balance live styles");
}

static void used_geometry(void) {
    ComputedStyle *s = style_new_default(); float x[4], y[4];
    apply(s, NULL, "border-radius", "50%");
    yes(!s->border_radius_y, "identical percentage values need no Y allocation");
    css_used_border_radii(s, 200, 100, 15, x, y);
    near_value(x[0], 100, .0001, "percentage X resolves against width");
    near_value(y[0], 50, .0001, "shared percentage Y resolves independently against height");
    apply(s, NULL, "border-radius", "80px / 20px");
    css_used_border_radii(s, 100, 100, 15, x, y);
    near_value(x[0], 50, .0001, "one global overlap factor scales X");
    near_value(y[0], 12.5, .0001, "the same overlap factor scales Y");
    apply(s, NULL, "border-radius", "100px 100px 0px 0px / 0px 80px 0px 0px");
    css_used_border_radii(s, 100, 100, 15, x, y);
    near_value(x[0], 0, .0001, "zero Y corner becomes square after overlap");
    near_value(x[1], 50, .0001, "zero-axis original X still participates in overlap");
    near_value(y[1], 40, .0001, "neighbor Y uses the original-radii overlap factor");
    yes(rounded_rect_contains(70, 10, 0, 0, 100, 100, x, y), "independently pinned zero-axis point is inside");
    apply(s, NULL, "border-radius", "100px 0px 0px 0px");
    css_used_border_radii(s, 100, 100, 15, x, y);
    yes(rounded_rect_general(100, 100, x, y), "oversized circular corner bypasses midpoint fast path");
    yes(!rounded_rect_contains(60, 3, 0, 0, 100, 100, x, y), "single oversized circle excludes point past box midpoint");
    yes(rounded_rect_contains(99, 3, 0, 0, 100, 100, x, y), "single oversized circle includes top-right interior");
    float ox[4] = {0, 100, 0, 0}, oy[4] = {0, 100, 0, 0}, ix[4], iy[4];
    const float inset[4] = {10, 30, 10, 30};
    rounded_radii_inset(ox, oy, inset, ix, iy);
    near_value(ix[1], 70, .0001, "inset subtracts right width from X");
    near_value(iy[1], 90, .0001, "inset subtracts top width from Y");
    yes(!rounded_rect_contains(60, 20, 30, 10, 70, 90, ix, iy),
        "partial inset arc remains excluded beyond the opposite inset edge");
    yes(rounded_rect_contains(35, 70, 30, 10, 70, 90, ix, iy), "partial inset arc interior remains visible");
    apply(s, NULL, "border-radius", "80px / 20px");
    css_used_border_radii(s, 100, 100, 9, x, y);
    near_value(x[0], 80, .0001, "fragment mask precedes overlap normalization");
    near_value(x[1], 0, .0001, "absent first-fragment right corner is square");
    css_used_border_radii(s, 100, 100, 0, x, y);
    near_value(x[0] + x[1] + x[2] + x[3] + y[0] + y[1] + y[2] + y[3],
               0, .0001, "middle fragment has no rounded ends");
    style_free(s);

    near_value(rounded_ellipse_distance(8e7, 1e-35, 1e8, 1, 2), -.6, 2e-6,
               "extreme wide ellipse near major axis converges to independent analytic distance");
    near_value(rounded_ellipse_distance(1e-35, 8e7, 1, 1e8, 2), -.6, 2e-6,
               "extreme tall ellipse uses the same finite geometry");
    near_value(rounded_ellipse_distance(8e7, 1e-45, 1e8, 1e-8, 2), -6e-9, 1e-13,
               "extreme tiny minor axis retains the correct distance");
    near_value(rounded_ellipse_distance(100.001, 0, 100, 1, 2), .001, 1e-9,
               "major-axis exterior distance is exact");
    float ex[4] = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    float ey[4] = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    rounded_radii_normalize(100, 50, ex, ey);
    for (int i = 0; i < 4; i++) {
        near_value(ex[i], 25, .0001, "finite extreme original radii normalize without overflow");
        near_value(ey[i], 25, .0001, "finite extreme Y radii normalize without overflow");
    }
}

static bool anonymous_radius_initial(Box *b, const ComputedStyle *owner, unsigned *count) {
    bool valid = true;
    if (b->anon && b->st && b->st != owner) {
        (*count)++;
        valid = b->st->border_radius_y == NULL;
        for (int i = 0; i < 4; i++) {
            Length r = b->st->border_radius[i];
            valid = valid && r.kind == LK_LEN && r.px == 0 && r.pct == 0 && !r.mm;
        }
        /* style.c is instrumented by the targeted sanitizer build. This used
           read must never follow a detached alias freed by parent adoption. */
        if (valid) {
            float x[4], y[4]; css_used_border_radii(b->st, 180, 100, 15, x, y);
            for (int i = 0; i < 4; i++) valid = valid && x[i] == 0 && y[i] == 0;
        }
    }
    for (Box *child = b->first; child; child = child->next)
        valid = anonymous_radius_initial(child, owner, count) && valid;
    return valid;
}
static void real_style_lifetime(void) {
    long initial = g_styles_live;
    Document *doc = doc_new("https://radius-native.test/");
    const char *html = "<!doctype html><html><body><div id=target></div>"
                       "<div id=plain><span>A</span><div>block</div><span>B</span></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc); engine->media = (MediaCtx){320, 240, 1, false};
    const char *css = "#plain{width:180px}#target::before{content:\"\";display:block;border-radius:inherit}"
                      "#target::after{content:\"\";display:block;border-radius:inherit}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Node *target = doc_get_element_by_id(doc, "target"), *plain = doc_get_element_by_id(doc, "plain");
    Layout *layout = layout_new(); DisplayList list = {0}; Canvas canvas;
    canvas_init(&canvas, 320, 240, 1);
    bool pseudo_owned = true, adopted = true, anonymous_valid = true;
    unsigned anonymous_observations = 0;
    for (unsigned i = 0; i < 200; i++) {
        const char *radius = i & 1 ? "border-radius:7px / 9px" : "border-radius:11px / 13px";
        node_set_attr(target, "style", radius); node_set_attr(plain, "style", radius);
        style_recalc(engine, &doc->node, false);
        ComputedStyle *s = target->style;
        pseudo_owned = pseudo_owned && s && s->border_radius_y && s->before && s->after &&
                       s->before->border_radius_y && s->after->border_radius_y &&
                       s->before->border_radius_y != s->border_radius_y &&
                       s->after->border_radius_y != s->border_radius_y &&
                       s->before->border_radius_y != s->after->border_radius_y;
        if (!pseudo_owned) break;
        layout_run(layout, doc, 320, 240);
        anonymous_valid = anonymous_radius_initial(plain->box, plain->style, &anonymous_observations) && anonymous_valid;
        ComputedStyle *identity = plain->style; Box *retained_box = plain->box;
        node_set_attr(plain, "style", i & 1 ? "border-radius:7px / 9px;background-color:red"
                                           : "border-radius:11px / 13px;background-color:blue");
        style_recalc(engine, &doc->node, false);
        adopted = adopted && plain->style == identity && engine->stats_paint > 0 &&
                  plain->style->border_radius_y && plain->box == retained_box;
        anonymous_valid = anonymous_radius_initial(plain->box, plain->style, &anonymous_observations) && anonymous_valid;
        dl_build(&list, layout, 0, 0, 320, 240); raster(&canvas, &list, 0xffffffffu);
        (void)layout_hit(layout, 5, 5, 0, 0);
    }
    yes(pseudo_owned, "200 real pseudo restyles own separate before/after axes");
    yes(adopted, "200 paint-only adoptions retain identity and valid transferred Y");
    yes(anonymous_valid && anonymous_observations >= 800,
        "detached mixed inline/block wrappers retain initial radii across parent adoption");
    node_set_attr(target, "style", "border-radius:7px / 9px");
    style_recalc(engine, &doc->node, false);
    css_set_anim_style(target, "border-top-left-radius:20px 40px");
    style_recalc(engine, &doc->node, false);
    value_is(target->style, "border-top-left-radius", "20px 40px", "native animation declaration retains both axes");
    css_set_anim_style(target, "");
    style_recalc(engine, &doc->node, false);
    value_is(target->style, "border-top-left-radius", "7px 9px", "animation removal restores authored pair");
    canvas_free(&canvas); dl_clear(&list); free(list.items.v); layout_free(layout);
    style_engine_free(engine); doc_free(doc);
    yes(g_styles_live == initial, "real style and pseudo ownership returns to initial live count");
}
int main(void) {
    dom_init(); font_init();
    long initial = g_styles_live;
    syntax_and_computed(); ownership(); used_geometry(); real_style_lifetime();
    yes(g_styles_live == initial, "all radius regression fixtures release their styles");
    printf("radius native: %u/%u checks passed; ownership stress 1000 cycles, pseudo/adoption 200 cycles\n",
           checks - failures, checks);
    printf("radius ABI: ComputedStyle=%zu Length=%zu optional_Y=%zu Node=%zu DItem=%zu\n",
           sizeof(ComputedStyle), sizeof(Length), 4 * sizeof(Length), sizeof(Node), sizeof(DItem));
    return failures ? 1 : 0;
}
