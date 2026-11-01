/* Minimum-size syntax is shared by supports and native style application.
 * No JavaScript normalization is involved in these assertions. */
#include "../src/css/css.h"
#include "../src/text/font.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static const char *const props[] = {
    "min-width", "min-height", "min-inline-size", "min-block-size"
};
static Length minimum(ComputedStyle *style, unsigned index) {
    return index == 0 || index == 2 ? style->min_width : style->min_height;
}
static void supports(const char *prop, const char *value, bool expected) {
    char condition[1024]; snprintf(condition, sizeof condition, "(%s: %s)", prop, value);
    bool actual = css_supports(condition);
    if (actual != expected) fprintf(stderr, "supports %s was %d, expected %d\n", condition, actual, expected);
    assert(actual == expected); checks++;
}
int main(void) {
    dom_init(); font_init();
    const char *const invalid[] = {
        "none", "complex", "60", "10px 20%", "max-content 10px", "min-content max-content",
        "available", "10px border-box", "content-box 20%", "auto-suffix", "min-content-suffix",
        "fit-content(10px) 20px", "fit-content()", "fit-content(10px, 20px)", "fit-content(-1px)",
        "-10%", "-0.5em", "1py", "min(1py)", "min(1s)", "calc(0)", "fit-content(2)",
        "fit-content(1s)", "calc(1px + 1s)", "calc(1px/**/+/**/2px)", "1/**/px",
        "m\\69 n-content extra", "calc(1px))", "10px;min-width:0", "fit-content(0s)"
    };
    const char *const valid[] = {
        "auto", "min-content", "max-content", "fit-content", "stretch", "-webkit-fill-available",
        "0", "-0", "10%", "0.5em", "1e2px", "calc(10% - 0.5em)", "calc(-1px)",
        "min(10px, 20%)", "fit-content(10%)", "fit-content(0.5em)", "fit-content(calc(10% - 0.5em))",
        "inherit", "initial", "unset", "revert", "revert-layer", "var(--minimum)", "var(--missing, 17px)",
        "MIN-CONTENT", " /* lead */ 10px /* tail */ ", "m\\69 n-content", "10p\\78",
        "calc(10p\\78 + 2px)", "calc(10px + 2px", "clamp(none, 10px, 20px)",
        "clamp(0px, 10px, none)", "calc(1px * sign(1em - 18px))", "1rex", "1rcap"
    };
    for (unsigned p = 0; p < sizeof props / sizeof *props; p++) {
        ComputedStyle *style = style_new_default();
        for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
            supports(props[p], invalid[i], false);
            css_apply_decl(style, NULL, props[p], "17px", NULL, NULL);
            css_apply_decl(style, NULL, props[p], invalid[i], NULL, NULL);
            Length value = minimum(style, p);
            if (value.kind != LK_LEN || value.px != 17 || value.pct || value.mm)
                fprintf(stderr, "%s:%s replaced 17px (kind=%d px=%g pct=%g)\n", props[p], invalid[i], value.kind, value.px, value.pct);
            assert(value.kind == LK_LEN && value.px == 17 && !value.pct && !value.mm); checks++;
        }
        for (unsigned i = 0; i < sizeof valid / sizeof *valid; i++) supports(props[p], valid[i], true);
        css_apply_decl(style, NULL, props[p], "calc(-1px)", NULL, NULL);
        assert(minimum(style, p).kind == LK_LEN && minimum(style, p).px == 0); checks++;
        css_apply_decl(style, NULL, props[p], "10p\\78", NULL, NULL);
        assert(minimum(style, p).kind == LK_LEN && minimum(style, p).px == 10); checks++;
        css_apply_decl(style, NULL, props[p], "m\\69 n-content", NULL, NULL);
        assert(minimum(style, p).kind == LK_MIN_CONTENT); checks++;
        css_apply_decl(style, NULL, "--bad", "none", NULL, NULL);
        css_apply_decl(style, NULL, props[p], "17px", NULL, NULL);
        css_apply_decl(style, NULL, props[p], "var(--bad)", NULL, NULL);
        assert(minimum(style, p).kind == LK_AUTO); checks++;
        css_apply_decl(style, NULL, "--number", "10", NULL, NULL);
        css_apply_decl(style, NULL, props[p], "var(--number)px", NULL, NULL);
        assert(minimum(style, p).kind == LK_AUTO); checks++;
        style_free(style);
    }
    /* Maximum-size semantics remain distinct. */
    supports("max-width", "none", true); supports("max-height", "none", true);
    ComputedStyle *style = style_new_default();
    css_apply_decl(style, NULL, "max-width", "none", NULL, NULL);
    assert(style->max_width.kind == LK_NONE); checks++;
    style_free(style);
    printf("PASS css_min_size %u native checks\n", checks);
    return 0;
}
