/* Shared box-shadow grammar and the existing native selected-shadow consumer. */
#include "../src/css/css.h"
#include "../src/text/font.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void require(bool ok, const char *label) {
    if (!ok) fprintf(stderr, "box-shadow failed: %s\n", label);
    assert(ok); checks++;
}
static bool same_shadow(const Shadow *a, const Shadow *b) {
    return a->x == b->x && a->y == b->y && a->blur == b->blur &&
           a->spread == b->spread && a->color == b->color && a->inset == b->inset;
}
static void shadow(ComputedStyle *style, const char *value, float x, float y, float blur, float spread, bool inset) {
    css_apply_decl(style, NULL, "box-shadow", value, NULL, NULL);
    const Shadow *s = &style->box_shadow;
    if (!style->has_shadow || fabsf(s->x - x) > .001f || fabsf(s->y - y) > .001f ||
        fabsf(s->blur - blur) > .001f || fabsf(s->spread - spread) > .001f || s->inset != inset)
        fprintf(stderr, "%s yielded [%g %g %g %g] inset=%d visible=%d\n", value,
                s->x, s->y, s->blur, s->spread, s->inset, style->has_shadow);
    require(style->has_shadow && fabsf(s->x - x) < .001f && fabsf(s->y - y) < .001f &&
            fabsf(s->blur - blur) < .001f && fabsf(s->spread - spread) < .001f && s->inset == inset, value);
}
int main(void) {
    dom_init(); font_init();
    const char *const valid[][2] = {
        {"none", "none"}, {"INITIAL", "initial"}, {"inherit", "inherit"}, {"unset", "unset"},
        {"revert", "revert"}, {"revert-layer", "revert-layer"}, {"0 -0", "0px 0px"},
        {"INSET +1PX -2px RED", "red 1px -2px inset"},
        {"r\\65 d 1px 2p\\78", "red 1px 2px"},
        {"1px/**/2px#F00", "#f00 1px 2px"},
        {"1px 2px 3px -4px currentColor", "currentcolor 1px 2px 3px -4px"},
        {"rgba(1,2,3,.5) 0 0", "rgba(1, 2, 3, 0.5) 0px 0px"},
        {"0 0 rgb(1 2 3 / 50%)", "rgb(1 2 3 / 50%) 0px 0px"},
        {"0 0 color-mix(in srgb, red, blue)", "color-mix(in srgb, red, blue) 0px 0px"},
        {"0 0 rgb(from currentcolor r g b)", "rgb(from currentcolor r g b) 0px 0px"},
        {"0 0 light-dark(red, blue)", "light-dark(red, blue) 0px 0px"},
        {"1px 2px calc(-3px) -4px", "1px 2px calc(-3px) -4px"},
        {"min(1px, 2px) max(3px, 4px)", "min(1px, 2px) max(3px, 4px)"},
        {"round(5px, 2px) calc(6px / 2)", "round(5px, 2px) calc(6px / 2)"},
        {"calc(1px + -2px) 3px", "calc(1px + -2px) 3px"},
        {"calc(1px / -0) 3px", "calc(1px / -0) 3px"},
        {"1px calc(2px", "1px calc(2px)"},
        {"INSET 1px 2px, 3px 4px BLUE", "1px 2px inset, blue 3px 4px"}
    };
    for (unsigned i = 0; i < sizeof valid / sizeof *valid; i++) {
        char *parsed = css_box_shadow_parse(valid[i][0]);
        if (!parsed || strcmp(parsed, valid[i][1])) fprintf(stderr, "%s parsed '%s', expected '%s'\n", valid[i][0], parsed ? parsed : "NULL", valid[i][1]);
        require(parsed && !strcmp(parsed, valid[i][1]), valid[i][0]);
        char *twice = css_box_shadow_parse(parsed);
        require(twice && !strcmp(twice, parsed), "parser round-trip");
        char condition[1024]; snprintf(condition, sizeof condition, "(box-shadow:%s)", valid[i][0]);
        require(css_supports(condition), "native support-query accepts grammar");
        free(parsed); free(twice);
    }
    const char *const invalid[] = {
        "", " ", "auto", "1 2", "1% 2px", "1px calc(2px + 0%)", "1px", "1px 2px 3px 4px 5px",
        "red 1px 2px blue", "inset 1px 2px inset", "1px red 2px", "1px 2px inset 3px",
        "1px 2px red 3px", "1px 2px -3px", "none, 1px 2px", "inherit 1px 2px", "1px 2px, initial",
        "1px 2px,", ",1px 2px", "1px 2px,,3px 4px", "1px 2wat", "1px 2p\\78\\,",
        "1px 2deg", "0 0 calc(1)", "1px calc(2px+3px)", "1px calc(2px +3px)",
        "1px calc(2px/**/+/**/3px)", "var()", "1px 2px !important", "1px 2px;color:red", "1px 2px)"
    };
    ComputedStyle *style = style_new_default();
    shadow(style, "red 4px 5px 6px -7px inset", 4, 5, 6, -7, true);
    Shadow before = style->box_shadow;
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        char *parsed = css_box_shadow_parse(invalid[i]);
        require(!parsed, invalid[i]); free(parsed);
        char condition[1024]; snprintf(condition, sizeof condition, "(box-shadow:%s)", invalid[i]);
        require(!css_supports(condition), "native support-query rejects grammar");
        css_apply_decl(style, NULL, "box-shadow", invalid[i], NULL, NULL);
        require(style->has_shadow && same_shadow(&style->box_shadow, &before), "invalid authored value retains previous selected shadow");
        if (!strchr(invalid[i], ';') && !strchr(invalid[i], '!')) {
            char text[2048]; snprintf(text, sizeof text, "box-shadow:red 4px 5px;color:blue;box-shadow:%s", invalid[i]);
            DeclList *list = css_parse_decls(text, strlen(text));
            require(list && list->n == 2 && !strcmp(list->v[0].value, "red 4px 5px"), "invalid declaration preserves preceding declarations");
            css_decls_free(list);
        }
    }
    const char *const pending[] = {"var(--Shadow)", "env(shadow, 1px 2px)", "attr(data-shadow type(<length>), 1px) 2px", "--Shadow()", "if(else: 1px 2px)"};
    for (unsigned i = 0; i < sizeof pending / sizeof *pending; i++) {
        char *parsed = css_box_shadow_parse(pending[i]);
        require(parsed && !strcmp(parsed, pending[i]), "pending token identity"); free(parsed);
    }
    css_apply_decl(style, NULL, "font-size", "20px", NULL, NULL);
    shadow(style, "1em 2em min(4px, 8px) calc(-2px)", 20, 40, 4, -2, false);
    shadow(style, "round(5px, 2px) max(3px, 4px) calc(-3px) -4px", 6, 4, 0, -4, false);
    css_apply_decl(style, NULL, "color", "rgb(10, 20, 30)", NULL, NULL);
    shadow(style, "currentcolor 1px 2px", 1, 2, 0, 0, false);
    require(style->box_shadow.color == style->color, "currentcolor follows declaration context");
    /* Preserve the existing renderer's selected-layer policy while validating
       the whole list. Multi-layer painting is outside this grammar repair. */
    shadow(style, "0 0 #0000, inset 5px 6px red, blue 7px 8px", 7, 8, 0, 0, false);
    shadow(style, "0 0 #0000, inset 5px 6px red", 5, 6, 0, 0, true);
    ComputedStyle *parent = style_new_default();
    shadow(parent, "blue 9px 10px 11px 12px inset", 9, 10, 11, 12, true);
    css_apply_decl(style, parent, "box-shadow", "inherit", NULL, NULL);
    require(style->has_shadow && same_shadow(&style->box_shadow, &parent->box_shadow) && style->parent_sensitive, "inherit copies the computed selected shadow");
    css_apply_decl(parent, NULL, "font-size", "20px", NULL, NULL);
    css_apply_decl(parent, NULL, "color", "blue", NULL, NULL);
    css_apply_decl(style, NULL, "font-size", "10px", NULL, NULL);
    css_apply_decl(style, NULL, "color", "red", NULL, NULL);
    shadow(parent, "currentcolor 1em 2em", 20, 40, 0, 0, false);
    css_apply_decl(style, parent, "box-shadow", "inherit", NULL, NULL);
    require(style->has_shadow && style->box_shadow.x == 20 && style->box_shadow.y == 40 && style->box_shadow.color == style->color, "currentcolor inherits token identity and computed parent lengths");
    shadow(parent, "rgb(from currentcolor r g b) 1em 2em", 20, 40, 0, 0, false);
    css_apply_decl(style, parent, "box-shadow", "inherit", NULL, NULL);
    require(style->has_shadow && style->box_shadow.x == 20 && style->box_shadow.y == 40 && style->box_shadow.color == style->color, "relative currentcolor inherits child context");
    css_apply_decl(parent, NULL, "color", "transparent", NULL, NULL);
    css_apply_decl(parent, NULL, "box-shadow", "1em 2em", NULL, NULL);
    require(!parent->has_shadow && parent->box_shadow.color_tokens && parent->box_shadow.x == 20, "transparent shadow retains color identity and geometry");
    css_apply_decl(style, parent, "box-shadow", "inherit", NULL, NULL);
    require(style->has_shadow && style->box_shadow.x == 20 && style->box_shadow.y == 40 && style->box_shadow.color == style->color, "omitted currentcolor becomes visible in inheriting child");
    css_apply_decl(style, NULL, "box-shadow", "calc(infinity * 1px) calc(NaN * 1px) 0", NULL, NULL);
    require(style->has_shadow && isfinite(style->box_shadow.x) && style->box_shadow.x > 0 && style->box_shadow.y == 0, "nonfinite math cannot enter selected shadow geometry");
    css_apply_decl(style, NULL, "box-shadow", "calc(-infinity * 1px) 1px calc(-infinity * 1px)", NULL, NULL);
    require(style->has_shadow && isfinite(style->box_shadow.x) && style->box_shadow.x < 0 && style->box_shadow.blur == 0, "negative infinity offset is bounded and blur is clamped");
    css_apply_decl(style, parent, "box-shadow", "unset", NULL, NULL);
    require(!style->has_shadow, "non-inherited unset takes initial none");
    css_apply_decl(style, NULL, "--Shadow", "green 1px 2px", NULL, NULL);
    shadow(style, "var(--Shadow)", 1, 2, 0, 0, false);
    css_apply_decl(style, NULL, "--Shadow", "1px 2px -3px", NULL, NULL);
    css_apply_decl(style, NULL, "box-shadow", "var(--Shadow)", NULL, NULL);
    require(!style->has_shadow, "invalid substituted value takes initial none");
    shadow(style, "1px 2px", 1, 2, 0, 0, false);
    css_apply_decl(style, NULL, "box-shadow", "initial", NULL, NULL);
    require(!style->has_shadow, "initial clears selected shadow");
    require(css_box_shadow_parse(NULL) == NULL, "null input");
    require(css_box_shadow_valid("color", "untouched-other-property"), "other-property validation unchanged");
    style_free(parent); style_free(style);
    printf("PASS css_box_shadow %u native checks\n", checks);
    return 0;
}
