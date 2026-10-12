#include "../../src/text/font.h"
#include "../../src/text/android_fonts.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

static void check_text(const char *label, Font *font, const char *text) {
    if (!font) {
        fprintf(stderr, "FAIL %s: no font\n", label);
        failures++;
        return;
    }
    ShapedRun run = {0};
    text_shape(font, text, strlen(text), 0, &run);
    bool valid = run.n > 0 && isfinite(run.width) && run.width > 0;
    for (int index = 0; index < run.n; index++) {
        GlyphPos *position = &run.g[index];
        if (!position->gid) valid = false;
        const Glyph *glyph = font_glyph(position->font, position->gid, 1);
        if (!glyph || glyph->w <= 0 || glyph->h <= 0 || (!glyph->alpha && !glyph->rgba)) {
            fprintf(stderr, "Raster failure %s: gid=%u size=%dx%d color=%d\n", label,
                position->gid, glyph ? glyph->w : 0, glyph ? glyph->h : 0, glyph ? glyph->color : 0);
            valid = false;
        }
    }
    printf("%s %s glyphs=%d width=%.2f\n", valid ? "PASS" : "FAIL", label, run.n, run.width);
    if (!valid) failures++;
    shaped_free(&run);
}

int font_smoke_run(void) {
    failures = 0;
    font_init();
    AndroidFontMatch selected;
    memset(&selected, 0x5a, sizeof selected);
    AndroidFontMatch unchanged = selected;
    bool invalid_rejected = !android_font_match(NULL, 400, false, 'A', &selected)
        && !android_font_match("", 400, false, 'A', &selected)
        && !android_font_match("sans-serif", 400, false, 0xd800, &selected)
        && !android_font_match("sans-serif", 400, false, 0xdfff, &selected)
        && !android_font_match("sans-serif", 400, false, 0x110000, &selected)
        && !android_font_match("sans-serif", 400, false, 'A', NULL)
        && !memcmp(&selected, &unchanged, sizeof selected);
    if (!invalid_rejected) { fprintf(stderr, "FAIL matcher invalid input\n"); failures++; }
    if (!android_font_match("sans-serif", 400, false, 0x1f600, &selected)) {
        fprintf(stderr, "FAIL supplementary character matcher\n"); failures++;
    } else printf("PASS supplementary matcher: %s#%d axes=%u\n", selected.path, selected.collection_index, selected.axis_count);
    static const struct { const char *label, *text; } samples[] = {
        {"Latin", "A"}, {"Greek", "\xce\xa9"}, {"Cyrillic", "\xd0\x96"},
        {"Arabic", "\xd8\xb3"}, {"Devanagari", "\xe0\xa4\x95"},
        {"Thai", "\xe0\xb8\x81"}, {"CJK", "\xe4\xb8\xad"},
        {"Hangul", "\xed\x95\x9c"}, {"Emoji", "\xf0\x9f\x98\x80"}
    };
    Font *regular = font_get("sans-serif", 400, false, 24);
    for (unsigned index = 0; index < sizeof samples / sizeof samples[0]; index++) {
        check_text(samples[index].label, regular, samples[index].text);
    }
    check_text("sans bold", font_get("sans-serif", 700, false, 24), "Hello");
    check_text("sans italic", font_get("sans-serif", 400, true, 24), "Hello");
    check_text("serif", font_get("serif", 400, false, 24), "Hello");
    if (font_get("codex-font-that-is-not-installed, serif", 400, false, 24)
        != font_get("serif", 400, false, 24)) {
        fprintf(stderr, "FAIL unknown family must continue CSS fallback list\n");
        failures++;
    }
    check_text("monospace", font_get("monospace", 400, false, 24), "Hello");
    check_text("system-ui", font_get("system-ui", 400, false, 24), "Hello");
    Font *medium = font_get("sans-serif", 500, false, 24);
    Font *bold = font_get("sans-serif", 700, false, 24);
    Font *italic = font_get("sans-serif", 400, true, 24);
    if (!medium || !bold || !italic || regular == medium || regular == bold || regular == italic || medium == bold) {
        fprintf(stderr, "FAIL distinct Android font styles\n"); failures++;
    }
    if (font_get("sans-serif", 400, false, 24) != regular) {
        fprintf(stderr, "FAIL font cache identity\n");
        failures++;
    }
    printf("Android engine font smoke: %u failures\n", failures);
    return failures ? 1 : 0;
}

#ifndef LUMEN_FONT_SMOKE_JNI
int main(void) { return font_smoke_run(); }
#endif
