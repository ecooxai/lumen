/* CSS Backgrounds3 color is clipped by the last image layer; none is a layer.
 * Independent cascade regression: expected boxes are specified per case, not
 * calculated with the implementation's modulo/index helper. */
#include "css/css.h"
#include <stdio.h>
#include <stdlib.h>
static StyleEngine *engine;
static Node *element;
static unsigned checks;
static void apply(ComputedStyle *s, const ComputedStyle *p, const char *name, const char *value) {
    css_apply_decl(s, p, name, value, engine, element);
}
static void check(const char *name, const ComputedStyle *s, unsigned layers, unsigned clip) {
    checks++;
    if (s->bg_layers != layers || s->bg_color_clip != clip) {
        fprintf(stderr, "%s: layers %u expected %u, color clip %u expected %u\n", name, s->bg_layers, layers, s->bg_color_clip, clip);
        exit(1);
    }
}
static ComputedStyle *fresh(void) { return style_new_default(); }
int main(void) {
    dom_init(); Document *d = doc_new("https://background-clip.test/");
    element = node_new_element(d, "div", NS_HTML); node_append(&d->node, element);
    engine = style_engine_new(d); ComputedStyle *s = fresh(), *parent = fresh();
    check("default", s, 1, BG_BORDER_BOX);
    apply(s, NULL, "background-clip", "border-box, content-box, border-box");
    apply(s, NULL, "background-image", "none, none");
    check("clip before image; ignore surplus clip", s, 2, BG_CONTENT_BOX);
    style_free(s); s = fresh();
    apply(s, NULL, "background-image", "none, none");
    apply(s, NULL, "background-clip", "border-box, content-box, border-box");
    check("image before clip", s, 2, BG_CONTENT_BOX);
    apply(s, NULL, "background-clip", "border-box, content-box");
    apply(s, NULL, "background-image", "none, none, none");
    check("short clip list repeats to third image", s, 3, BG_BORDER_BOX);
    apply(s, NULL, "background-image", "none, none, none, none");
    check("short clip list repeats to fourth image", s, 4, BG_CONTENT_BOX);
    apply(s, NULL, "background-clip", "padding-box");
    check("singleton clip repeats", s, 4, BG_PADDING_BOX);
    apply(s, NULL, "background-image", "none");
    apply(s, NULL, "background-clip", "content-box, padding-box, border-box");
    check("single image ignores extra clips", s, 1, BG_CONTENT_BOX);
    apply(s, NULL, "background", "none padding-box, none border-box content-box green");
    check("shorthand last box belongs to color", s, 2, BG_CONTENT_BOX);
    if (s->bg_clip != BG_PADDING_BOX) return 2;
    checks++;
    apply(s, NULL, "background", "none");
    check("shorthand none resets lists", s, 1, BG_BORDER_BOX);
    apply(parent, NULL, "background", "none content-box, none padding-box green");
    check("parent shorthand two layers", parent, 2, BG_PADDING_BOX);
    apply(s, parent, "background", "inherit");
    check("whole shorthand inherit", s, 2, BG_PADDING_BOX);
    if (!s->parent_sensitive) return 3;
    checks++;
    apply(s, NULL, "background-clip", "content-box, border-box, content-box");
    apply(s, parent, "background-image", "inherit");
    check("image inherit uses child clip list", s, 2, BG_BORDER_BOX);
    apply(s, NULL, "background-image", "none, none, none");
    apply(s, parent, "background-clip", "inherit");
    check("clip inherit uses child image count", s, 3, BG_CONTENT_BOX);
    apply(s, parent, "background-image", "initial");
    check("image initial keeps inherited clip", s, 1, BG_CONTENT_BOX);
    apply(s, parent, "background-clip", "initial");
    check("clip initial", s, 1, BG_BORDER_BOX);
    apply(s, parent, "background", "inherit");
    apply(s, parent, "background", "initial");
    check("whole initial", s, 1, BG_BORDER_BOX);
    apply(s, parent, "background", "inherit");
    apply(s, parent, "background", "unset");
    check("whole unset is noninherited", s, 1, BG_BORDER_BOX);
    apply(s, parent, "background", "inherit");
    apply(s, parent, "background-clip", "unset");
    check("clip unset preserves image count", s, 2, BG_BORDER_BOX);
    apply(s, parent, "background", "inherit");
    apply(s, parent, "background-image", "unset");
    check("image unset preserves clip list", s, 1, BG_CONTENT_BOX);
    apply(s, NULL, "background-image", "url(\"data:image/svg+xml,a,b,c\"), none");
    apply(s, NULL, "background-clip", "content-box, padding-box");
    check("quoted URL commas are not layers", s, 2, BG_PADDING_BOX);
    apply(s, NULL, "background-image", "linear-gradient(red, blue), none");
    check("gradient commas are not layers", s, 2, BG_PADDING_BOX);
    apply(s, NULL, "background", "url(\"a,b\") border-box, none content-box green");
    check("shorthand URL commas preserve layer clips", s, 2, BG_CONTENT_BOX);
    apply(s, NULL, "background-clip", "padding-box, border-box");
    check("longhand after shorthand", s, 2, BG_BORDER_BOX);
    style_free(s); style_free(parent); style_engine_free(engine); doc_free(d);
    printf("background color clip: %u independent cascade checks passed\n", checks); return 0;
}
