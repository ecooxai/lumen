/* Native background-position type checking, independent of JS CSSOM folding.
 * Invalid declarations must preserve both stored axes. */
#include "../src/css/css.h"
#include "../src/text/font.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;
static StyleEngine *engine;
static Node *element;
static void apply(ComputedStyle *s, const char *property, const char *value) {
    css_apply_decl(s, NULL, property, value, engine, element);
}
static void position(const char *name, const ComputedStyle *s,
                     float xp, float xq, float yp, float yq) {
    const Length *x = &s->bg_pos[0], *y = &s->bg_pos[1];
    checks++;
    if (x->kind != LK_LEN || y->kind != LK_LEN ||
        !isfinite(x->px) || !isfinite(x->pct) ||
        !isfinite(y->px) || !isfinite(y->pct) ||
        fabsf(x->px - xp) > .0001f || fabsf(x->pct - xq) > .0001f ||
        fabsf(y->px - yp) > .0001f || fabsf(y->pct - yq) > .0001f) {
        fprintf(stderr, "%s: (%gpx,%g%%;%gpx,%g%%), expected (%gpx,%g%%;%gpx,%g%%)\n",
                name, x->px, x->pct, y->px, y->pct, xp, xq, yp, yq);
        failures++;
    }
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://background-position-types.test/");
    element = node_new_element(doc, "div", NS_HTML);
    node_append(&doc->node, element); engine = style_engine_new(doc);
    static const char *const bad[] = {
        "calc(2)", "calc(0)", "min(1, 2)", "max(0, 0)",
        "clamp(0, 1, 2)", "calc(2px / 1px)"
    };
    static const char *const properties[] = {
        "background-position", "background-position-x", "background-position-y"
    };
    for (unsigned p = 0; p < 3; p++) for (unsigned i = 0; i < 6; i++) {
        ComputedStyle *s = style_new_default();
        char label[192]; snprintf(label, sizeof label, "%s rejects %s", properties[p], bad[i]);
        apply(s, "background-position", "17px 23px");
        apply(s, properties[p], bad[i]);
        position(label, s, 17, 0, 23, 0); style_free(s);
    }
    static const char *const offsets[] = {
        "left calc(0) top 3px", "left 3px top calc(2)"
    };
    for (unsigned i = 0; i < 2; i++) {
        ComputedStyle *s = style_new_default();
        apply(s, "background-position", "17px 23px");
        apply(s, "background-position", offsets[i]);
        position(offsets[i], s, 17, 0, 23, 0); style_free(s);
    }
    static const struct { const char *value; float xp, xq, yp, yq; } valid[] = {
        {"0 -0", 0, 0, 0, 0},
        {"10px 20px", 10, 0, 20, 0},
        {"25% 75%", 0, 25, 0, 75},
        {"calc(2px + 3px) calc(4px + 6px)", 5, 0, 10, 0},
        {"calc(10px + 20%) calc(5px + 30%)", 10, 20, 5, 30},
        {"left 3px top 4px", 3, 0, 4, 0},
        {"right 3px bottom 4px", -3, 100, -4, 100},
        {"right 0px bottom 0px", 0, 100, 0, 100}
    };
    for (unsigned i = 0; i < sizeof valid / sizeof *valid; i++) {
        ComputedStyle *s = style_new_default();
        apply(s, "background-position", valid[i].value);
        position(valid[i].value, s, valid[i].xp, valid[i].xq, valid[i].yp, valid[i].yq);
        style_free(s);
    }
    ComputedStyle *s = style_new_default();
    apply(s, "background-position", "17px 23px");
    apply(s, "background-position-x", "calc(4px + 10%)");
    position("valid x function preserves y", s, 4, 10, 23, 0);
    apply(s, "background-position-y", "calc(8px + 20%)");
    position("valid y function preserves x", s, 4, 10, 8, 20);
    style_free(s); style_engine_free(engine); doc_free(doc);
    printf("background-position native types: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
