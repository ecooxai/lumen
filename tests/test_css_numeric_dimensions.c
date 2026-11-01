/* Native numeric declaration grammar: no JavaScript normalization is involved. */
#include "../src/css/css.h"
#include "../src/text/font.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void supports(const char *property, const char *value, bool expected) {
    char condition[1024]; snprintf(condition, sizeof condition, "(%s: %s)", property, value);
    bool actual = css_supports(condition);
    if (actual != expected) fprintf(stderr, "%s was %d, expected %d\n", condition, actual, expected);
    assert(actual == expected); checks++;
}
static float stored(const ComputedStyle *style, unsigned index) {
    static const unsigned side[] = { 0, 1, 2, 3, 3, 1, 0, 2 };
    if (index < 8) return style->border_width[side[index]];
    if (index == 8) return style->outline_width;
    if (index == 9) return style->tr_delay;
    if (index == 10) return style->tr_dur;
    if (index == 11) return style->anim_delay;
    return style->anim_dur;
}
int main(void) {
    dom_init(); font_init();
    static const char *const token_cases[][2] = {
        { "calc(1px + 2px", "calc(1px + 2px)" },
        { "rotate(calc(min(90deg, 1.58rad) / 2", "rotate(calc(min(90deg, 1.58rad) / 2))" },
        { "max(20px, calc(5px + 5px", "max(20px, calc(5px + 5px))" },
        { "calc(1px + 2px)", "calc(1px + 2px)" },
        { "calc(1px /*tail", "calc(1px /*tail*/)" },
        { "f(\"open", "f(\"open\")" },
        { "f([{}]", "f([{}])" },
        { "calc(1px))", NULL }, { "calc(1px]", NULL },
        { "calc(1px);color:red", NULL }, { "calc(1px) !important", NULL },
        { "calc(1px/**/+/**/2px)", NULL }, { "calc(1px+2px)", NULL }
    };
    for (unsigned i = 0; i < sizeof token_cases / sizeof *token_cases; i++) {
        char *actual = css_math_value_text(token_cases[i][0]);
        if (token_cases[i][1]) {
            if (!actual || strcmp(actual, token_cases[i][1])) fprintf(stderr, "token closure %s got %s expected %s\n", token_cases[i][0], actual ? actual : "NULL", token_cases[i][1]);
            assert(actual && !strcmp(actual, token_cases[i][1]));
            char *twice = css_math_value_text(actual); assert(twice && !strcmp(twice, actual)); free(twice); checks++;
        } else assert(!actual);
        free(actual); checks++;
    }
    assert(css_math_value_text(NULL) == NULL); checks++;

    static const char *const properties[] = {
        "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
        "border-inline-start-width", "border-inline-end-width", "border-block-start-width", "border-block-end-width",
        "outline-width", "transition-delay", "transition-duration", "animation-delay", "animation-duration"
    };
    static const char *const bad_length[] = {
        "min(1py)", "max(1py)", "min(0)", "min(0%)", "min(0s)", "min(0Hz)", "min(0dpi)", "min(0fr)",
        "min(1px, 0%)", "max(1px, 0%)", "calc(1px + 1s)", "-1px", "2", "1py", "none", "auto",
        "min(1px) * 2", "1px + 2px", "min(1px) 2px", "1/**/px", "calc(1p\\78 + 2px)",
        "0x1p2px", "1.px", "1e+px", "calc(1px/**/+/**/2px)", "if()", "if(foo)"
    };
    static const char *const bad_time[] = {
        "min(1mt)", "max(1dag)", "min(0)", "min(0%)", "min(0px)", "min(0deg)", "min(0Hz)", "min(0dpi)",
        "min(0fr)", "max(0)", "max(0%)", "calc(1s + 1px)", "0", "none", "1s 2s", "1s,", ",1s", "1s,,2s",
        "min(1s) * 2", "min(1s), min(2px)", "1/**/s", "0x1p2s", "1.s", "1e+s", "if()", "if(foo)"
    };
    static const char *const good_length[] = {
        "0", "-0", "1px", ".5px", "1e2px", "1em", "1rex", "thin", "medium", "THICK",
        "min(1px, 2px)", "max(1em, 2px)", "calc(-1px)", "calc(1px * 2)", "clamp(none, 1px, 2px)",
        "10p\\78", "m\\69 n(1px, 2px)", "calc(10p\\78  + 2px)", "calc(1px + 2px",
        "calc(sibling-index() * 1px)", "calc(1px * sign(1em - 18px))"
    };
    static const char *const good_time[] = {
        "0s", "1s", "1ms", ".5s", "1e2ms", "min(1s, 200ms)", "max(1s, 2s)", "calc(-1s)",
        "clamp(none, 1s, 2s)", "1s , 2s", "min(1s) , max(2s)", "10m\\73", "calc(10m\\73  + 2s)",
        "calc(1s + 2s", "calc(sibling-index() * 1s)"
    };
    static const char *const pending[] = { "var(--Value)", "env(safe-area-inset-left, 10px)",
        "attr(data-value type(<length>), 10px)", "if(style(--x: yes): 10px; else: 20px)",
        "--MiXeD(10px)", "calc(--foo() * 1px)" };
    for (unsigned p = 0; p < sizeof properties / sizeof *properties; p++) {
        const char *property = properties[p], *seed = p < 9 ? "17px" : "17s";
        const char *const *invalid = p < 9 ? bad_length : bad_time;
        size_t invalid_count = p < 9 ? sizeof bad_length / sizeof *bad_length : sizeof bad_time / sizeof *bad_time;
        ComputedStyle *style = style_new_default();
        for (size_t i = 0; i < invalid_count; i++) {
            supports(property, invalid[i], false);
            css_apply_decl(style, NULL, property, seed, NULL, NULL);
            css_apply_decl(style, NULL, property, invalid[i], NULL, NULL);
            if (stored(style, p) != 17) fprintf(stderr, "%s:%s changed prior value to %g\n", property, invalid[i], stored(style, p));
            assert(stored(style, p) == 17); checks++;
            char declarations[1024]; snprintf(declarations, sizeof declarations, "%s:%s;%s:%s", property, seed, property, invalid[i]);
            DeclList *list = css_parse_decls(declarations, strlen(declarations));
            assert(list && list->n == 1 && !strcmp(list->v[0].prop, property));
            css_decls_free(list); checks++;
        }
        const char *const *valid = p < 9 ? good_length : good_time;
        size_t valid_count = p < 9 ? sizeof good_length / sizeof *good_length : sizeof good_time / sizeof *good_time;
        for (size_t i = 0; i < valid_count; i++) supports(property, valid[i], true);
        for (size_t i = 0; i < sizeof pending / sizeof *pending; i++) {
            supports(property, pending[i], true);
            char *parsed = css_numeric_dimension_parse(property, pending[i]);
            assert(parsed && !strcmp(parsed, pending[i])); free(parsed); checks++;
        }
        for (const char *const *wide = (const char *const[]){ "initial", "inherit", "unset", "revert", "revert-layer", NULL }; *wide; wide++)
            supports(property, *wide, true);
        if (p >= 9) {
            bool duration = strstr(property, "duration") != NULL;
            supports(property, "-1s", !duration);
            supports(property, "auto", !strcmp(property, "animation-duration"));
            char *parsed = css_numeric_dimension_parse(property, "1s , 2s");
            assert(parsed && !strcmp(parsed, "1s , 2s")); free(parsed); checks++;
        }
        style_free(style);
    }
    ComputedStyle *style = style_new_default();
    css_apply_decl(style, NULL, "--bad", "min(0%)", NULL, NULL);
    css_apply_decl(style, NULL, "border-left-width", "17px", NULL, NULL);
    css_apply_decl(style, NULL, "border-left-width", "var(--bad)", NULL, NULL);
    assert(style->border_width[3] == 3); checks++;
    css_apply_decl(style, NULL, "--bad-time", "min(1mt)", NULL, NULL);
    css_apply_decl(style, NULL, "transition-delay", "17s", NULL, NULL);
    css_apply_decl(style, NULL, "transition-delay", "var(--bad-time)", NULL, NULL);
    assert(style->tr_delay == 0); checks++;
    css_apply_decl(style, NULL, "border-left-width", "10p\\78", NULL, NULL);
    assert(style->border_width[3] == 10); checks++;
    css_apply_decl(style, NULL, "animation-delay", "10m\\73", NULL, NULL);
    assert(fabsf(style->anim_delay - .01f) < .000001f); checks++;
    style_free(style);
    assert(!css_numeric_dimension_property("border-width"));
    assert(!css_numeric_dimension_property("width"));
    assert(css_numeric_dimension_valid("width", "min(1px, 10%)")); checks += 3;
    printf("numeric dimension native checks: %u PASS\n", checks);
    return 0;
}
