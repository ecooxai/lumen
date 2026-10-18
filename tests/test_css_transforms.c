#include "../src/css/css.h"
#include "../src/layout/layout.h"
#include <math.h>
#include <stdio.h>

static int failures, checks;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); failures++; } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < .01f; }
static StyleEngine env;

static void apply(ComputedStyle *style, const char *prop, const char *value) {
    css_apply_decl(style, NULL, prop, value, &env, NULL);
}
static void matrix(const ComputedStyle *s, const float expected[6]) {
    for (int i = 0; i < 6; i++) CHECK(near(s->transform[i], expected[i]));
}
static void translation(const ComputedStyle *s, float w, float h, float x, float y) {
    /* The percentage basis remains the element's box, even after rotation or
       scale maps a width contribution onto the y axis (or height onto x). */
    float tx, ty; css_transform_translation(s, w, h, &tx, &ty);
    CHECK(near(tx, x)); CHECK(near(ty, y));
}
static void computed(ComputedStyle *s, const char *property, const char *expected) {
    Box box = { .w = 400, .h = 200 };
    Node node = { .type = NODE_ELEMENT, .style = s, .box = &box };
    char *actual = css_get_computed_value(&node, property);
    if (strcmp(actual, expected)) fprintf(stderr, "%s: expected %s, got %s\n", property, expected, actual);
    CHECK(!strcmp(actual, expected)); free(actual);
}
static void permutations(int order[4], int n, unsigned used) {
    static const char *props[] = {"translate", "rotate", "scale", "transform"};
    static const char *values[] = {"10px 20px", "90deg", "2 3", "translate(7px, 11px) scale(5, 7)"};
    if (n < 4) {
        for (int i = 0; i < 4; i++) if (!(used & (1u << i))) { order[n] = i; permutations(order, n + 1, used | (1u << i)); }
        return;
    }
    ComputedStyle *s = style_new_default();
    for (int i = 0; i < 4; i++) apply(s, props[order[i]], values[order[i]]);
    const float expected[] = {0, 10, -21, 0, -23, 34};
    matrix(s, expected); CHECK(s->has_transform); style_free(s);
}

static void neutral_rotations(void) {
    /* Generated transform lists often retain identity X/Y rotations. They
       must preserve adjacent 2D transforms without requiring 3D rendering. */
    static const char *angles[] = {"0", "-0deg", "0rad", "0grad", "0turn", "360deg", "-2turn"};
    for (size_t i = 0; i < sizeof angles / sizeof *angles; i++) {
        ComputedStyle *s = style_new_default(); char value[256];
        snprintf(value, sizeof value, "translateX(-100%%) rotateX(%s) rotateY(%s) translateY(12px)", angles[i], angles[i]);
        apply(s, "transform", value);
        CHECK(s->has_transform); translation(s, 326.8f, 932, -326.8f, 12);
        style_free(s);
    }
    ComputedStyle *s = style_new_default();
    apply(s, "transform", "rotateX(0deg) rotateY(0deg)");
    CHECK(s->has_transform); /* an identity transform still forms a containing block */
    apply(s, "transform", "translate(9px, 11px)");
    static const char *invalid[] = {"rotateX(0bogus)", "rotateY(0%)", "rotateX(0deg, 0deg)", "rotateY()"};
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        apply(s, "transform", invalid[i]); translation(s, 326.8f, 932, 9, 11);
    }
    style_free(s);
}

int main(void) {
    dom_init(); env.media = (MediaCtx){430, 932, 3, false};
    for (int reverse = 0; reverse < 2; reverse++) {
        ComputedStyle *s = style_new_default();
        if (reverse) apply(s, "transform", "translateZ(0)");
        apply(s, "translate", "-100% 0");
        if (!reverse) apply(s, "transform", "translateZ(0)");
        CHECK(s->has_transform); translation(s, 326.8f, 932, -326.8f, 0);
        style_free(s);
    }
    int order[4]; permutations(order, 0, 0);
    neutral_rotations();

    ComputedStyle *s = style_new_default();
    apply(s, "translate", "10px 20px"); apply(s, "transform", "translate(7px, 11px)");
    translation(s, 400, 200, 17, 31);
    apply(s, "translate", "none"); translation(s, 400, 200, 7, 11);
    apply(s, "translate", "10px 20px"); apply(s, "transform", "none");
    CHECK(s->has_transform); translation(s, 400, 200, 10, 20);
    apply(s, "translate", "none"); CHECK(!s->has_transform);
    apply(s, "scale", "1"); CHECK(s->has_transform); /* identity still creates a containing block */
    apply(s, "scale", "none"); CHECK(!s->has_transform);
    apply(s, "rotate", "0deg"); CHECK(s->has_transform);
    apply(s, "rotate", "initial"); CHECK(!s->has_transform);
    apply(s, "transform", "translateX(5px)"); apply(s, "transform", "translateX(8px)");
    translation(s, 400, 200, 8, 0); /* cascade overrides do not append another transform */
    apply(s, "translate", "2px 4px"); apply(s, "translate", "nonsense");
    translation(s, 400, 200, 10, 4);
    apply(s, "transform", "lumen-unknown(3)"); translation(s, 400, 200, 10, 4);
    style_free(s);

    s = style_new_default();
    apply(s, "translate", "-100% 10%"); apply(s, "rotate", "90deg");
    apply(s, "scale", "2 3"); apply(s, "transform", "translate(50%, 25%)");
    translation(s, 400, 200, -550, 420);
    translation(s, 200, 400, -500, 240);
    style_free(s);
    s = style_new_default(); apply(s, "transform", "scale(2) translateX(50%)");
    translation(s, 400, 200, 400, 0);
    apply(s, "transform", "translateX(50%) scale(2)"); translation(s, 400, 200, 200, 0);
    style_free(s);

    s = style_new_default();
    apply(s, "translate", "-100% 10px"); apply(s, "rotate", "1.5turn"); apply(s, "scale", "2 3");
    computed(s, "transform", "none"); computed(s, "translate", "-100% 10px");
    computed(s, "rotate", "540deg"); computed(s, "scale", "2 3");
    apply(s, "transform", "scale(2) translateX(50%)");
    computed(s, "transform", "matrix(2, 0, 0, 2, 400, 0)");
    apply(s, "translate", "none"); computed(s, "translate", "none");
    style_free(s);

    /* Per-property transition detection must not confuse an independent change
       with transform, or miss changes that cancel in the composed matrix. */
    ComputedStyle *old = style_new_default(), *now = style_new_default();
    apply(old, "translate", "-100%"); apply(old, "transform", "translateX(100%)");
    apply(now, "translate", "-50%"); apply(now, "transform", "translateX(50%)");
    translation(old, 400, 200, 0, 0); translation(now, 400, 200, 0, 0);
    CHECK(css_transform_changed(old, now, "translate")); CHECK(css_transform_changed(old, now, "transform"));
    CHECK(!css_transform_changed(old, now, "rotate")); CHECK(!css_transform_changed(old, now, "scale"));
    apply(now, "transform", "translateX(100%)");
    CHECK(!css_transform_changed(old, now, "transform")); CHECK(css_transform_changed(old, now, "translate"));
    style_free(old); style_free(now);

    ComputedStyle *parent = style_new_default();
    apply(parent, "translate", "25% 10px"); apply(parent, "scale", "2");
    s = style_inherit(parent); CHECK(!s->has_transform); /* individual transforms do not inherit by default */
    css_apply_decl(s, parent, "translate", "inherit", &env, NULL);
    translation(s, 400, 200, 100, 10);
    css_apply_decl(s, parent, "scale", "inherit", &env, NULL);
    CHECK(near(s->transform[0], 2) && near(s->transform[3], 2));
    apply(s, "translate", "none"); translation(parent, 400, 200, 100, 10);
    style_free(s); style_free(parent);

    const char *html = "<html><body><div id='drawer' class='drawer' style='translate:25%;transform:scale(2)'></div>"
        "<div id='replacement'></div></body></html>";
    const char *css = ":root{--offset:-100%} .drawer{translate:0;transform:translateZ(0)}"
        ".drawer{translate:var(--offset) 0!important} #replacement{translate:10px;translate:30px;transform:translateX(5px)}";
    Document *doc = doc_new("https://transform.example/"); html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc); engine->media = env.media;
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    style_recalc(engine, &doc->node, true);
    Node *drawer = doc_get_element_by_id(doc, "drawer"), *replacement = doc_get_element_by_id(doc, "replacement");
    CHECK(drawer && drawer->style && replacement && replacement->style);
    if (drawer && drawer->style && replacement && replacement->style) {
        translation(drawer->style, 326.8f, 932, -326.8f, 0);
        CHECK(near(drawer->style->transform[0], 2));
        translation(replacement->style, 400, 200, 35, 0);
        /* Repeated restyles exercise style adoption and independent state ownership. */
        for (int i = 0; i < 8; i++) {
            node_set_attr(drawer, "style", i % 2 ? "--offset:-100%;transform:scale(2)" : "--offset:-50%;transform:scale(3)");
            style_recalc(engine, &doc->node, true);
            translation(drawer->style, 400, 200, i % 2 ? -400 : -200, 0);
            CHECK(near(drawer->style->transform[0], i % 2 ? 2 : 3));
        }
    }
    style_engine_free(engine); doc_free(doc);
    if (failures) { fprintf(stderr, "%d CSS transform failures\n", failures); return 1; }
    printf("CSS independent transform tests ok: %d checks\n", checks); return 0;
}
