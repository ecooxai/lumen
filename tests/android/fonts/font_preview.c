#include "../../../src/text/font.h"
#include <jni.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PREVIEW_WIDTH = 960, PREVIEW_HEIGHT = 1060 };

static unsigned preview_text(uint32_t *pixels, const char *text, float size, float scale, int baseline) {
    Font *font = font_get("sans-serif", 400, false, size);
    ShapedRun run = {0};
    text_shape(font, text, strlen(text), 0, &run);
    unsigned clipped = 0;
    for (int index = 0; index < run.n; index++) {
        const GlyphPos *position = &run.g[index];
        const Glyph *glyph = font_glyph(position->font, position->gid, scale);
        if (!glyph) continue;
        int origin_left = 36 + (int)lroundf(position->x * scale) + glyph->left;
        int origin_top = baseline + (int)lroundf(position->y * scale) - glyph->top;
        for (int row = 0; row < glyph->h; row++) {
            for (int column = 0; column < glyph->w; column++) {
                size_t source = (size_t)row * glyph->w + column;
                uint32_t color = glyph->rgba ? glyph->rgba[source]
                    : glyph->alpha ? (uint32_t)glyph->alpha[source] << 24 : 0;
                unsigned alpha = color >> 24;
                if (!alpha) continue;
                int target_left = origin_left + column, target_top = origin_top + row;
                if (target_left < 0 || target_left >= PREVIEW_WIDTH || target_top < 0 || target_top >= PREVIEW_HEIGHT) {
                    clipped++;
                    continue;
                }
                size_t target = (size_t)target_top * PREVIEW_WIDTH + target_left;
                uint32_t background = pixels[target], blended = 0xff000000;
                for (unsigned shift = 0; shift <= 16; shift += 8) {
                    unsigned channel = ((color >> shift) & 255)
                        + (((background >> shift) & 255) * (255 - alpha) + 127) / 255;
                    blended |= channel << shift;
                }
                pixels[target] = blended;
            }
        }
    }
    shaped_free(&run);
    return clipped;
}

JNIEXPORT jintArray JNICALL Java_org_lumen_codex_fonttest_FontTestActivity_renderPreview(
    JNIEnv *environment, jclass activity) {
    (void)activity;
    size_t count = (size_t)PREVIEW_WIDTH * PREVIEW_HEIGHT;
    uint32_t *pixels = malloc(count * sizeof *pixels);
    if (!pixels) return NULL;
    for (size_t index = 0; index < count; index++) pixels[index] = 0xffedf6ff;
    font_init();
    unsigned clipped = preview_text(pixels, "Lumen Android - engine glyph preview", 28, 1, 44);
    clipped += preview_text(pixels, "HarfBuzz shaping / FreeType + Android color raster / no Java text substitution", 18, 1, 76);
    static const struct {
        const char *label, *text;
        float size, scale;
        int baseline;
    } rows[] = {
        {"12 px / 1x", "Ag \xf0\x9f\x98\x80 \xf0\x9f\x9a\x80 jyp", 12, 1, 148},
        {"24 px / 1x", "Ag \xf0\x9f\x98\x80 \xf0\x9f\x9a\x80 jyp", 24, 1, 236},
        {"48 px / 1x", "Ag \xf0\x9f\x98\x80 \xf0\x9f\x9a\x80 jyp", 48, 1, 356},
        {"64 px / 1x", "Ag \xf0\x9f\x98\x80 \xf0\x9f\x9a\x80 jyp", 64, 1, 506},
        {"24 px / 2x", "Ag \xf0\x9f\x98\x80 \xf0\x9f\x9a\x80 jyp", 24, 2, 646},
        {"Script fallback / 32 px", "A \xce\xa9 \xd0\x96 \xd8\xb3 \xe0\xa4\x95 \xe0\xb8\x81 \xe4\xb8\xad \xed\x95\x9c", 32, 1, 756},
        {"Skin tone / flag / joined profession - 40 px", "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd  \xf0\x9f\x87\xba\xf0\x9f\x87\xb8  \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", 40, 1, 876},
        {"Emoji presentation / keycap - 40 px", "\xe2\x9d\xa4\xef\xb8\x8f  1\xef\xb8\x8f\xe2\x83\xa3", 40, 1, 1006},
    };
    for (unsigned row = 0; row < sizeof rows / sizeof rows[0]; row++) {
        int baseline = rows[row].baseline;
        for (int column = 28; column < PREVIEW_WIDTH - 28; column++) {
            pixels[(size_t)baseline * PREVIEW_WIDTH + column] = 0xffbccbdc;
        }
        clipped += preview_text(pixels, rows[row].label, 18, 1,
            baseline - (int)ceilf(rows[row].size * rows[row].scale) - 22);
        clipped += preview_text(pixels, rows[row].text, rows[row].size, rows[row].scale, baseline);
    }
    jintArray result = (*environment)->NewIntArray(environment, (jsize)count + 2);
    if (result) {
        jint dimensions[2] = {PREVIEW_WIDTH, PREVIEW_HEIGHT};
        (*environment)->SetIntArrayRegion(environment, result, 0, 2, dimensions);
        if (!(*environment)->ExceptionCheck(environment)) {
            (*environment)->SetIntArrayRegion(environment, result, 2, (jsize)count, (const jint *)pixels);
        }
    }
    free(pixels);
    printf("FONT_PREVIEW_CLIPPED=%u\n", clipped);
    return result;
}
