/* Native stylesheet math must agree with CSSOM without relying on JS folding. */
#include "../src/css/css.h"
#include "../src/text/font.h"
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
    dom_init(); font_init();
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
    equal("progress(50px, 0px, 100px)", .5f);
    equal("progress(150px, 0px, 100px)", 1);
    equal("progress(-50px, 0px, 100px)", 0);
    equal("progress(25deg, 100deg, 0deg)", .75f);
    equal("progress(50ms, 0s, 0.1s)", .5f);
    equal("progress(50%, 0%, 100%)", .5f);
    equal("progress(no-clamp 25%, 50%, 100%)", -.5f);
    equal("progress(1px, 10px, 10px)", 0);
    equal("progress(10px, 10px, 10px)", 0);
    equal("progress(100px, 10px, 10px)", 0);
    equal("progress(no-clamp 150px, 0px, 100px)", 1.5f);
    equal("progress(no-clamp -50px, 0px, 100px)", -.5f);
    equal("progress(no-clamp 1px, 10px, 10px)", -INFINITY);
    equal("progress(no-clamp 10px, 10px, 10px)", 0);
    equal("progress(no-clamp 100px, 10px, 10px)", INFINITY);
    equal("calc(10px * progress(50deg, 0deg, 100deg))", 5);
    equal("calc(atan2(1, 1) / 1deg)", 45);
    equal("calc(atan2(1px, 1px) / 1deg)", 45);
    equal("calc(atan2(-1 * 0, -1) / 1deg / 360 + .5)", 0);
    equal("calc(atan2(progress(no-clamp -1 * 0, 0, 1), -1) / 1deg / 360 + .5)", 0);
    const char *invalid_atan2[] = {
        "calc(atan2(1) / 1deg)", "calc(atan2(1, 1, 1) / 1deg)",
        "calc(atan2(1px, 1s) / 1deg)", "calc(atan22(1, 1) / 1deg)",
    };
    for (size_t i = 0; i < sizeof invalid_atan2 / sizeof *invalid_atan2; i++) {
        Length value; assert(!css_parse_length(invalid_atan2[i], &value, 16, 16, NULL)); checks++;
    }
    equal("abs(calc(10px + 0%))", 10);
    equal("hypot(calc(3px + 0%), 4px)", 5);
    Length cancelled_percentage;
    assert(css_parse_length("hypot(calc(3px + 0%), 4px)", &cancelled_percentage, 16, 16, NULL));
    assert(cancelled_percentage.pctu && cancelled_percentage.pct == 0 && cancelled_percentage.px == 5); checks++;
    equal("progress(abs(5%), hypot(3%, 4%), 10%)", 0);
    equal("progress(no-clamp abs(5%), hypot(3%, 4%), 10%)", 0);
    equal("calc(1px * progress(abs(10%), (10% - 10%), 100% / 10))", 1);
    equal("calc(1px * progress(no-clamp abs(10%), (10% - 10%), 100% / 10))", 1);
    equal("calc(progress(50%, 100px, 100px) * 10px)", 0);
    struct { const char *text; float at100, at200, at400; } progress_responsive[] = {
        { "calc(progress(50%, 0px, 100px) * 10px)", 5, 10, 10 },
        { "calc(progress(no-clamp 50%, 0px, 100px) * 10px)", 5, 10, 20 },
        { "calc(progress(50%, 100px, 0px) * 10px)", 5, 0, 0 },
        { "calc(-2 * progress(50%, 0px, 100px) * 10px + 1px)", -9, -19, -19 },
        { "calc(progress(50%, calc(10% + 10px), calc(10% + 110px)) * 10px)", 3, 7, 10 },
    };
    for (size_t i = 0; i < sizeof progress_responsive / sizeof *progress_responsive; i++) {
        Length value;
        assert(css_parse_length(progress_responsive[i].text, &value, 16, 16, NULL));
        assert(fabsf(len_resolve(value, 100) - progress_responsive[i].at100) < .0001f);
        assert(fabsf(len_resolve(value, 200) - progress_responsive[i].at200) < .0001f);
        assert(fabsf(len_resolve(value, 400) - progress_responsive[i].at400) < .0001f);
        checks += 3;
    }
    Length bounded_progress;
    assert(css_parse_length("calc(progress(no-clamp 50%, 0px, 100px) * 10px)", &bounded_progress, 16, 16, NULL));
    float large_progress = len_resolve(bounded_progress, 3e38f);
    assert(isfinite(large_progress) && fabsf(large_progress - 1.5e37f) <= 1.5e31f); checks++;
    assert(css_parse_length("calc(progress(no-clamp 50%, 0px, 100px) * (-1 * 0) * 1px)", &bounded_progress, 16, 16, NULL));
    float negative_zero_progress = len_resolve(bounded_progress, 100);
    assert(negative_zero_progress == 0 && signbit(negative_zero_progress)); checks++;
    assert(css_parse_length("calc(-10px * progress(50%, 0px, 100px))", &bounded_progress, 16, 16, NULL));
    assert(len_resolve(bounded_progress, 3e38f) == -10); checks++;
    const char *unrepresented_progress[] = {
        "calc(progress(50%, 0px, calc(100px + 10%)) * 10px)",
        "calc(progress(no-clamp 50%, 100px, 100px) * 10px)",
        "calc(progress(50%, 0px, 100px) * infinity * 1px)",
        "calc(progress(50%, 0px, 100px) / 0 * 1px)",
        "calc(progress(50%, 0px, 100px) * 1px + infinity * 1px)",
        "calc((progress(50%, 0px, 100px) + progress(25%, 0px, 100px)) * 1px)",
        "min(10px, calc(progress(50%, 0px, 100px) * 1px))",
    };
    for (size_t i = 0; i < sizeof unrepresented_progress / sizeof *unrepresented_progress; i++) {
        Length value; assert(!css_parse_length(unrepresented_progress[i], &value, 16, 16, NULL)); checks++;
    }
    const char *nan[] = {
        "round(infinity, NaN)", "round(NaN, infinity)", "round(0, 0)",
        "round(infinity, infinity)", "mod(infinity, 2)", "mod(-0, infinity)",
        "mod(0, -infinity)", "rem(infinity, 2)", "calc(0 / 0)",
        "calc(-1 * round(infinity, NaN))", "calc(0 * infinity)",
        "progress(NaN, 1, 1)", "progress(1, NaN, NaN)",
        "progress(infinity, -infinity, infinity)",
        "progress(NaN * 1px, 0%, 0px)",
    };
    for (size_t i = 0; i < sizeof nan / sizeof *nan; i++) assert(isnan(parse(nan[i])));
    const char *invalid[] = {
        "round(0px)", "round(1px, 2deg)", "round(1px, 2px, 3px)",
        "mod(1px)", "rem(1px, 2px, 3px)", "round(1px, 1px) garbage",
        "calc(round(1px, 1px)) trailing", "round(1px, nearest)",
        "progress()", "progress(1, 0)", "progress(1, 0, 1, 2)",
        "progress(1px, 0deg, 2px)", "progress(1px, 0, 2px)",
        "progress(1px * 1px, 0px * 0px, 2px * 2px)",
        "progress(no-clamp, 1, 0, 1)", "progress(1, 0, 1) trailing",
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
    ComputedStyle *parent = style_new_default(); parent->font_size = 80;
    css_apply_decl(style, parent, "font-size", "7px", NULL, NULL);
    css_apply_decl(style, parent, "font-size", "round(10% / 1px)", NULL, NULL);
    assert(style->font_size == 7); checks++;
    css_apply_decl(style, parent, "font-size", "calc(round(10% / 1px) * 1px)", NULL, NULL);
    assert(style->font_size == 8); checks++;
    css_apply_decl(style, parent, "line-height", "round(1.5)", NULL, NULL);
    assert(style->line_height_factor == 2 && style->line_height == 16); checks++;
    css_apply_decl(style, parent, "transform", "translateX(12px)", NULL, NULL);
    css_apply_decl(style, parent, "transform", "scale(progress(1px, 0deg, 2px))", NULL, NULL);
    assert(style->has_transform && style->transform[4] == 12); checks++;
    css_apply_decl(style, parent, "transform", "scale(progress(0em, 0rem, 0em))", NULL, NULL);
    assert(style->has_transform && style->transform[0] == 0 && style->transform[3] == 0); checks++;
    css_apply_decl(style, parent, "transform", "scale(progress(no-clamp 2em, 0px, 1rem))", NULL, NULL);
    assert(style->has_transform && style->transform[0] == 1 && style->transform[3] == 1); checks++;
    css_apply_decl(style, parent, "opacity", "calc(0.5 * progress(-100px, 0px, 100px))", NULL, NULL);
    assert(style->opacity == 0); checks++;
    css_apply_decl(style, parent, "transform", "translateX(12px)", NULL, NULL);
    const char *mixed_progress[] = {
        "scale(progress(10%, 0px, 100%))",
        "scale(progress(0px, 0%, 0px))",
        "scale(progress(calc(10% + 0px), 0px, 100px))",
        "scale(progress(calc(0% + 0px), 0px, 100px))"
    };
    for (size_t i = 0; i < sizeof mixed_progress / sizeof *mixed_progress; i++) {
        css_apply_decl(style, parent, "transform", mixed_progress[i], NULL, NULL);
        assert(style->has_transform && style->transform[4] == 12); checks++;
    }
    css_apply_decl(style, parent, "transform", "scale(progress(25%, 0%, 100%))", NULL, NULL);
    assert(style->has_transform && style->transform[0] == .25f); checks++;
    css_apply_decl(style, parent, "width", "calc(round(10% / 1px) * 1px)", NULL, NULL);
    assert(len_resolve(style->width, 80) == 8 && len_resolve(style->width, 200) == 20); checks++;
    css_apply_decl(style, parent, "order", "calc(2.5)", NULL, NULL);
    assert(style->order == 3); checks++;
    css_apply_decl(style, parent, "order", "calc(-1.5)", NULL, NULL);
    assert(style->order == -1); checks++;
    css_apply_decl(style, parent, "order", "2.5", NULL, NULL);
    assert(style->order == -1); checks++;
    css_apply_decl(style, parent, "order", "calc(2px)", NULL, NULL);
    assert(style->order == -1); checks++;
    css_apply_decl(style, parent, "order", "calc(progress(25px, 0px, 100px) * 10)", NULL, NULL);
    assert(style->order == 3); checks++;
    css_apply_decl(style, parent, "column-count", "calc(progress(50px, 0px, 100px) * 7)", NULL, NULL);
    assert(!strcmp(custom_get(style->gen_n, "column-count"), "4")); checks++;
    css_apply_decl(style, parent, "column-count", "0", NULL, NULL);
    assert(!strcmp(custom_get(style->gen_n, "column-count"), "4")); checks++;
    css_apply_decl(style, parent, "column-count", "calc(2px)", NULL, NULL);
    assert(!strcmp(custom_get(style->gen_n, "column-count"), "4")); checks++;
    css_apply_decl(style, parent, "column-count", "calc(-2)", NULL, NULL);
    assert(!strcmp(custom_get(style->gen_n, "column-count"), "1")); checks++;
    css_apply_decl(style, parent, "flex-grow", "progress(no-clamp 150px, 0px, 100px)", NULL, NULL);
    assert(style->flex_grow == 1.5f); checks++;
    css_apply_decl(style, parent, "flex-grow", "progress(1px, 0deg, 2px)", NULL, NULL);
    assert(style->flex_grow == 1.5f); checks++;
    assert(css_flex_valid("flex-grow", "progress(1rex, 0px, 2rex)")); checks++;
    assert(!css_flex_valid("flex-grow", "progress(1rex, 0deg, 2rex)")); checks++;
    style_free(parent); style_free(style);
    printf("PASS css_stepped_math %d native boundary checks\n", checks);
    return 0;
}
