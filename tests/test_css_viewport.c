#include "../src/css/css.h"
#include <math.h>
#include <stdio.h>
static int failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); failures++; } } while (0)
static void length(const char *text, const MediaCtx *mc, float expected) {
    Length value = {0};
    bool parsed = css_parse_length(text, &value, 16, 16, mc);
    if (!parsed || value.kind != LK_LEN || fabsf(value.px - expected) > .02f || value.pct != 0) {
        fprintf(stderr, "FAIL length %s: parsed=%d px=%.2f expected=%.2f\n", text, parsed, value.px, expected); failures++;
    }
}
int main(void) {
    dom_init();
    MediaCtx mc = {430, 888, 2, false};
    const char *inline_units[] = {"100vi", "100svi", "100lvi", "100dvi"};
    const char *block_units[] = {"100vb", "100svb", "100lvb", "100dvb"};
    for (size_t i = 0; i < 4; i++) { length(inline_units[i], &mc, 430); length(block_units[i], &mc, 888); }
    length("100vw", &mc, 430); length("100vh", &mc, 888);
    length("100svmin", &mc, 430); length("100dvmax", &mc, 888);
    length("calc(100dvb - 2rem)", &mc, 856);
    length("max(0px, calc(100svb - 100dvb))", &mc, 0);
    length("min(100dvi, 600px)", &mc, 430);
    mc.vw = 888; mc.vh = 430;
    length("100dvb", &mc, 430); length("100dvi", &mc, 888);
    mc.root_vertical = true;
    length("100dvb", &mc, 888); length("100dvi", &mc, 430);
    length("100dvh", &mc, 430); length("100dvw", &mc, 888);
    CHECK(css_supports("(height: 100dvb)"));
    CHECK(css_supports("(height: calc(100dvb - 2rem))"));
    CHECK(css_supports("not (height: 100notarealunit)"));
    CHECK(css_supports("(height: auto)"));
    CHECK(css_supports("(height: var(--page-height))"));
    CHECK(css_supports("(display: flex)"));
    CHECK(!css_supports("(height: nonsense)"));
    if (failures) { fprintf(stderr, "%d viewport CSS failures\n", failures); return 1; }
    puts("viewport CSS tests ok"); return 0;
}
