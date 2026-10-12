#include "../../src/text/font.h"
#include "../../src/text/android_fonts.h"
#include "../../src/text/android_font_raster.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

static void check_raster_input(void) {
    AndroidFontMatch match = {0};
    strcpy(match.path, "/system/fonts/NotoColorEmoji.ttf");
    Glyph output = {.w = 123, .adv = 7};
    Glyph unchanged = output;
    bool valid = !android_font_raster(NULL, 1, 24, &output)
        && !android_font_raster(&match, 1, 24, NULL)
        && !android_font_raster(&match, 0, 24, &output)
        && !android_font_raster(&match, 65536, 24, &output)
        && !android_font_raster(&match, 1, 0, &output)
        && !android_font_raster(&match, 1, NAN, &output)
        && !android_font_raster(&match, 1, INFINITY, &output)
        && !android_font_raster(&match, 1, 2049, &output);
    match.collection_index = -1;
    valid = valid && !android_font_raster(&match, 1, 24, &output);
    match.collection_index = 0;
    match.axis_count = ANDROID_FONT_MAX_AXES + 1;
    valid = valid && !android_font_raster(&match, 1, 24, &output);
    match.axis_count = 1;
    match.axes[0].tag = 0x77676874;
    match.axes[0].value = NAN;
    valid = valid && !android_font_raster(&match, 1, 24, &output);
    match.axis_count = 0;
    memset(match.path, 'x', sizeof match.path);
    valid = valid && !android_font_raster(&match, 1, 24, &output);
    strcpy(match.path, "/codex-font-that-does-not-exist.ttf");
    valid = valid && !android_font_raster(&match, 1, 24, &output)
        && !memcmp(&output, &unchanged, sizeof output);
    printf("%s color raster invalid input/output preservation\n", valid ? "PASS" : "FAIL");
    if (!valid) failures++;
}

static void check_color(const char *label, const char *text, float size, float scale) {
    Font *font = font_get("sans-serif", 400, false, size);
    ShapedRun run = {0};
    text_shape(font, text, strlen(text), 0, &run);
    bool valid = run.n == 1 && run.g[0].gid && isfinite(run.width) && run.width > 0;
    const Glyph *glyph = valid ? font_glyph(run.g[0].font, run.g[0].gid, scale) : NULL;
    valid = valid && glyph && glyph->color && glyph->rgba && glyph->w > 0 && glyph->h > 0
        && glyph->w <= 2048 && glyph->h <= 2048 && glyph->top > 0;
    unsigned visible = 0, colored = 0;
    if (valid) {
        for (int index = 0; index < glyph->w * glyph->h; index++) {
            uint32_t pixel = glyph->rgba[index];
            unsigned alpha = pixel >> 24, red = (pixel >> 16) & 255;
            unsigned green = (pixel >> 8) & 255, blue = pixel & 255;
            if (alpha) visible++;
            if (alpha && (red != green || green != blue)) colored++;
            if (red > alpha || green > alpha || blue > alpha) valid = false;
        }
        valid = valid && visible > 0 && colored > 0
            && font_glyph(run.g[0].font, run.g[0].gid, scale) == glyph;
    }
    printf("%s %s size=%.0f scale=%.0f raster=%dx%d visible=%u colored=%u\n",
        valid ? "PASS" : "FAIL", label, size, scale, glyph ? glyph->w : 0,
        glyph ? glyph->h : 0, visible, colored);
    if (!valid) failures++;
    shaped_free(&run);
}

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

static void check_keycap_context(Font *font) {
    const char *text = "A1\xef\xb8\x8f\xe2\x83\xa3Z";
    ShapedRun run = {0};
    text_shape(font, text, strlen(text), 0, &run);
    bool valid = run.n == 3 && run.g[0].font == font && run.g[2].font == font
        && run.g[0].cluster == 0 && run.g[1].cluster == 1 && run.g[2].cluster == 8
        && run.g[0].gid && run.g[1].gid && run.g[2].gid
        && run.g[1].x > run.g[0].x && run.g[2].x > run.g[1].x
        && isfinite(run.width) && run.width > run.g[2].x;
    const Glyph *glyph = valid ? font_glyph(run.g[1].font, run.g[1].gid, 1) : NULL;
    valid = valid && glyph && glyph->color && glyph->rgba && glyph->w > 0 && glyph->h > 0;
    printf("%s keycap surrounding text/clusters glyphs=%d\n", valid ? "PASS" : "FAIL", run.n);
    if (!valid) failures++;
    shaped_free(&run);
}

int font_smoke_run(void) {
    failures = 0;
    font_init();
    check_raster_input();
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
    check_color("emoji small", "\xf0\x9f\x98\x80", 12, 1);
    check_color("emoji regular", "\xf0\x9f\x98\x80", 24, 1);
    check_color("emoji high-DPI", "\xf0\x9f\x98\x80", 24, 2);
    check_color("emoji large", "\xf0\x9f\x98\x80", 64, 1);
    check_color("emoji rocket", "\xf0\x9f\x9a\x80", 24, 1);
    check_color("emoji skin tone", "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd", 24, 1);
    check_color("emoji flag", "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8", 24, 1);
    check_color("emoji ZWJ", "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", 24, 1);
    check_color("emoji VS16", "\xe2\x9d\xa4\xef\xb8\x8f", 24, 1);
    check_color("emoji keycap", "1\xef\xb8\x8f\xe2\x83\xa3", 24, 1);
    check_color("emoji keycap zero", "0\xef\xb8\x8f\xe2\x83\xa3", 24, 1);
    check_color("emoji keycap hash", "#\xef\xb8\x8f\xe2\x83\xa3", 24, 1);
    check_color("emoji keycap star", "*\xef\xb8\x8f\xe2\x83\xa3", 24, 1);
    check_color("emoji keycap without VS16", "1\xe2\x83\xa3", 24, 1);
    check_keycap_context(regular);
    check_text("Latin combining accent", regular, "e\xcc\x81");
    printf("Android engine font smoke: %u failures\n", failures);
    return failures ? 1 : 0;
}

#ifndef LUMEN_FONT_SMOKE_JNI
int main(void) { return font_smoke_run(); }
#endif
