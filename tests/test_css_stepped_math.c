/* Native stylesheet math must agree with CSSOM without relying on JS folding. */
#include "../src/css/css.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int checks;
static float parse(const char *text) {
    Length value;
    if (!css_parse_length(text, &value, 16, 16, NULL)) {
        fprintf(stderr, "CSS math rejected: %s\n", text); assert(0);
    }
    assert(value.kind == LK_LEN && !value.mm && !value.pct);
    checks++;
    return value.px;
}
static void equal(const char *text, float expected) {
    float value = parse(text);
    if (value != expected || (value == 0 && !!signbit(value) != !!signbit(expected))) {
        fprintf(stderr, "%s = %g (sign %d), wanted %g (sign %d)\n",
                text, value, !!signbit(value), expected, !!signbit(expected));
        assert(0);
    }
}
int main(void) {
    dom_init();
    equal("round(15px, -10px)", 20);
    equal("round(-15px, -10px)", -10);
    equal("round(up, -13px, -10px)", -10);
    equal("round(down, 13px, -10px)", 10);
    equal("round(to-zero, -13px, -10px)", -10);
    equal("round(-0, 2)", -0.0f);
    equal("round(-0.1, 2)", -0.0f);
    equal("round(up, -0.1, 2)", -0.0f);
    equal("round(down, 0.1, 2)", 0.0f);
    equal("mod(0, -2)", -0.0f);
    equal("mod(4, -2)", -0.0f);
    equal("mod(-4, 2)", 0.0f);
    equal("rem(-4, 2)", -0.0f);
    equal("rem(4, -2)", 0.0f);
    equal("mod(-19, 5)", 1);
    equal("rem(-19, 5)", -4);
    equal("calc(1 / round(-0.1, 2))", -INFINITY);
    equal("calc(1 / round(0.1, 2))", INFINITY);
    equal("round(infinity, 1)", INFINITY);
    equal("round(-infinity, -1)", -INFINITY);
    equal("round(1, infinity)", 0);
    equal("round(-1, -infinity)", -0.0f);
    equal("round(up, 1, infinity)", INFINITY);
    equal("round(down, -1, infinity)", -INFINITY);
    equal("calc(-1 * round(-1, infinity))", 0);
    const char *nan[] = {
        "round(infinity, NaN)", "round(NaN, infinity)", "round(0, 0)",
        "round(infinity, infinity)", "mod(infinity, 2)", "mod(-0, infinity)",
        "mod(0, -infinity)", "rem(infinity, 2)", "calc(0 / 0)",
        "calc(-1 * round(infinity, NaN))", "calc(0 * infinity)",
    };
    for (size_t i = 0; i < sizeof nan / sizeof *nan; i++) assert(isnan(parse(nan[i])));
    const char *invalid[] = {
        "round(0px)", "round(1px, 2deg)", "round(1px, 2px, 3px)",
        "mod(1px)", "rem(1px, 2px, 3px)", "round(1px, 1px) garbage",
        "calc(round(1px, 1px)) trailing", "round(1px, nearest)",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        Length value; checks++;
        assert(!css_parse_length(invalid[i], &value, 16, 16, NULL));
    }
    assert(sizeof(Length) == 28); checks++;
    struct { const char *text; float at75, at150; } responsive[] = {
        { "round(10%, 1px)", 8, 15 },
        { "round(10%, 5px)", 10, 15 },
        { "mod(10%, 5px)", 2.5f, 0 },
        { "rem(10%, 5px)", 2.5f, 0 },
        { "mod(18px, 100% / 15)", 3, 8 },
        { "mod(-18px, 100% / 10)", 4.5f, 12 },
        { "mod(-19%, 5%)", .75f, 1.5f },
        { "calc(round(10%, 1px) * -1)", -8, -15 },
        { "calc(round(10%, 1px) * -1 * -1)", 8, 15 },
        { "calc(round(10%, 1px) * 2 + 5% + 2px)", 21.75f, 39.5f },
        { "calc(1px - round(10%, 1px))", -7, -14 },
        { "calc(round(up, 10%, 1px) * -1)", -8, -15 },
        { "calc(round(down, 10%, 1px) * -1)", -7, -15 },
        { "calc(mod(10%, 5px) * -2 + 1px)", -4, 1 },
        { "calc(rem(-10%, 5px) * -2 + 1px)", 6, 1 },
    };
    for (size_t i = 0; i < sizeof responsive / sizeof *responsive; i++) {
        Length value;
        assert(css_parse_length(responsive[i].text, &value, 16, 16, NULL));
        assert(len_math_stepped(value.mm));
        assert(fabsf(len_resolve(value, 75) - responsive[i].at75) < .0001f);
        assert(fabsf(len_resolve(value, 150) - responsive[i].at150) < .0001f);
        checks += 2;
    }
    Length typed_default;
    assert(css_parse_length("round(10% / 1px)", &typed_default, 16, 16, NULL));
    assert(len_math_stepped(typed_default.mm));
    assert(len_resolve(typed_default, 80) == 8);
    assert(len_resolve(typed_default, 200) == 20); checks += 2;
    const char *constant_nested[] = {
        "min(10px, round(1px + 0%, 1px))",
        "calc(round(1px + 0%, 1px) + round(2px + 0%, 1px))",
        "round(round(1px + 0%, 1px), 2px)",
    };
    float nested_expected[] = { 1, 3, 2 };
    for (size_t i = 0; i < sizeof constant_nested / sizeof *constant_nested; i++) {
        Length value; assert(css_parse_length(constant_nested[i], &value, 16, 16, NULL));
        assert(len_resolve(value, 80) == nested_expected[i]); checks++;
    }
    /* Unsupported nonlinear trees are rejected instead of approximated. */
    const char *unrepresented[] = {
        "round(round(10%, 1px), 2px)",
        "calc(round(10%, 1px) + round(20%, 2px))",
        "calc(round(10%, 1px) * 0)",
        "calc(round(10%, 1px) * infinity)",
    };
    for (size_t i = 0; i < sizeof unrepresented / sizeof *unrepresented; i++) {
        Length value; assert(!css_parse_length(unrepresented[i], &value, 16, 16, NULL)); checks++;
    }
    ComputedStyle *style = style_new_default();
    css_apply_decl(style, NULL, "opacity", "round(0.6, 0.25)", NULL, NULL);
    assert(style->opacity == .5f); checks++;
    css_apply_decl(style, NULL, "opacity", "calc(NaN)", NULL, NULL);
    assert(style->opacity == 0); checks++;
    css_apply_decl(style, NULL, "transform", "scale(round(0.6, 0.25))", NULL, NULL);
    assert(style->has_transform && style->transform[0] == .5f && style->transform[3] == .5f); checks++;
    css_apply_decl(style, NULL, "transform", "scale(1)", NULL, NULL);
    assert(style->has_transform && style->transform[0] == 1); checks++;
    css_apply_decl(style, NULL, "transition-delay", "round(10ms, 6ms)", NULL, NULL);
    assert(fabsf(style->tr_delay - .012f) < .000001f); checks++;
    css_apply_decl(style, NULL, "transform", "translateX(12px)", NULL, NULL);
    assert(style->has_transform && style->transform[4] == 12); checks++;
    css_apply_decl(style, NULL, "transform", "bogus(2)", NULL, NULL);
    assert(style->has_transform && style->transform[4] == 12); checks++;
    css_apply_decl(style, NULL, "transform", "scale() translateX(5px)", NULL, NULL);
    assert(style->has_transform && style->transform[4] == 12); checks++;
    css_apply_decl(style, NULL, "transform", "scale(2) garbage", NULL, NULL);
    assert(style->has_transform && style->transform[4] == 12); checks++;
    const char *malformed_transform[] = {
        "translateX(auto)", "translate(none)",
        "matrix(100%, 0, 0, 1, 0, 0)", "scale3d(1, 1, 2px)",
        "translate3d(1px, 2px, 5%)", "translate(1px,,2px)"
    };
    for (size_t i = 0; i < sizeof malformed_transform / sizeof *malformed_transform; i++) {
        css_apply_decl(style, NULL, "transform", malformed_transform[i], NULL, NULL);
        assert(style->has_transform && style->transform[4] == 12); checks++;
    }
    css_apply_decl(style, NULL, "width", "7px", NULL, NULL);
    css_apply_decl(style, NULL, "width", "round(10% / 1px)", NULL, NULL);
    assert(!style->width.mm && style->width.px == 7); checks++;
    css_apply_decl(style, NULL, "width", "calc(round(10% / 1px) * 1px)", NULL, NULL);
    assert(len_math_stepped(style->width.mm));
    assert(len_resolve(style->width, 80) == 8);
    assert(len_resolve(style->width, 200) == 20); checks += 2;
    css_apply_decl(style, NULL, "font-size", "round(110%, 3px)", NULL, NULL);
    assert(style->font_size == 18); checks++;
    css_apply_decl(style, NULL, "line-height", "20px", NULL, NULL);
    css_apply_decl(style, NULL, "vertical-align", "round(50%, 3px)", NULL, NULL);
    assert(style->vertical_align_len == 9); checks++;
    style_free(style);
    printf("PASS css_stepped_math %d native boundary checks\n", checks);
    return 0;
}
