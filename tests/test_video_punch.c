/* A GPU video overlay is permitted only if raster() actually left a supported
 * alpha hole. Rounded clips and opacity layers must preserve the normal CPU
 * image instead of erasing it and then declining to draw the overlay. */
#include "../src/paint/paint.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(name, expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); failures++; \
} } while (0)

static const Color backdrop = 0xff101820u, green = 0xff24b070u;
/* Integer bilinear interpolation can round each channel down twice. */
static bool near_rgb(uint32_t a, uint32_t b) {
    return abs((int)COLOR_R(a) - (int)COLOR_R(b)) <= 2 &&
        abs((int)COLOR_G(a) - (int)COLOR_G(b)) <= 2 && abs((int)COLOR_B(a) - (int)COLOR_B(b)) <= 2;
}
static DisplayList list(DItem *items, int count) {
    DisplayList dl = {0}; dl.items.v = items; dl.items.n = dl.items.cap = count;
    return dl;
}
static void fallback(const char *name, DItem *items, int count, Image *im, float scale, bool partial, bool blended) {
    DisplayList dl = list(items, count);
    int w = (int)(32 * scale), h = (int)(24 * scale), x = (int)(16 * scale), y = (int)(12 * scale);
    Canvas ordinary, candidate; canvas_init(&ordinary, w, h, scale); canvas_init(&candidate, w, h, scale);
    for (int i = 0; i < w * h; i++) ordinary.px[i] = candidate.px[i] = 0xffabcdefu;
    candidate.punch = im;
    if (partial) {
        raster_rect(&ordinary, &dl, backdrop, 5, 5, w - 5, h - 5);
        raster_rect(&candidate, &dl, backdrop, 5, 5, w - 5, h - 5);
    } else { raster(&ordinary, &dl, backdrop); raster(&candidate, &dl, backdrop); }
    uint32_t center = ordinary.px[(size_t)y * w + x];
    CHECK("ordinary reference draws the image", center != backdrop && center >> 24 == 255);
    CHECK("ordinary reference uses expected opacity", blended ? !near_rgb(center, green) : near_rgb(center, green));
    CHECK("unsupported overlay is declined", !candidate.punched);
    int mismatches = 0;
    for (int i = 0; i < w * h; i++) if (ordinary.px[i] != candidate.px[i]) mismatches++;
    if (mismatches) fprintf(stderr, "%s differs from ordinary rasterization at %d pixels\n", name, mismatches);
    CHECK(name, mismatches == 0);
    if (partial) CHECK("partial repaint preserves pixels outside damage", candidate.px[0] == 0xffabcdefu);
    canvas_free(&ordinary); canvas_free(&candidate);
}

int main(void) {
    uint32_t pixels[16]; for (int i = 0; i < 16; i++) pixels[i] = green;
    Image im = { .w = 4, .h = 4, .refs = 1, .px = pixels, .opaque = true };
    DItem image = { .op = DO_IMAGE, .x = 4, .y = 4, .w = 24, .h = 16, .img = &im };
    DItem rounded = { .op = DO_PUSH_CLIP, .x = 4, .y = 4, .w = 24, .h = 16, .r = {6, 6, 6, 6} };
    DItem rectangle = { .op = DO_PUSH_CLIP, .x = 10, .y = 6, .w = 12, .h = 12 };
    DItem pop_clip = { .op = DO_POP_CLIP }, layer = { .op = DO_PUSH_LAYER, .alpha = .5f };
    DItem pop_layer = { .op = DO_POP_LAYER };
    DItem round_items[] = { rounded, image, pop_clip };
    fallback("rounded clip retains the image", round_items, 3, &im, 1, false, false);
    fallback("rounded clip at 2x retains the image", round_items, 3, &im, 2, false, false);
    fallback("rounded clip partial repaint retains the image", round_items, 3, &im, 1, true, false);
    DItem layer_items[] = { layer, image, pop_layer };
    fallback("opacity layer retains the image", layer_items, 3, &im, 1, false, true);
    DItem nested_items[] = { rounded, rectangle, image, pop_clip, pop_clip };
    fallback("inherited rounded clip retains the image", nested_items, 5, &im, 1, false, false);
    DItem both_items[] = { layer, rounded, image, pop_clip, pop_layer };
    fallback("opacity and rounded clip retain the image", both_items, 5, &im, 1, false, true);

    /* Unclipped and rectangular-clipped overlays still avoid CPU video raster. */
    Canvas cv; canvas_init(&cv, 32, 24, 1); cv.punch = &im;
    DisplayList dl = list(&image, 1); raster(&cv, &dl, backdrop);
    CHECK("plain video is punched", cv.punched);
    CHECK("plain video leaves a transparent center", cv.px[12 * 32 + 16] == 0);
    CHECK("plain video preserves its surroundings", cv.px[0] == backdrop);
    DItem rect_items[] = { rectangle, image, pop_clip }; dl = list(rect_items, 3); cv.punched = false;
    raster(&cv, &dl, backdrop);
    CHECK("rectangular clip remains eligible", cv.punched);
    CHECK("rectangular clip leaves a transparent center", cv.px[12 * 32 + 16] == 0);
    CHECK("rectangular clip preserves excluded video pixels", cv.px[12 * 32 + 5] == backdrop);
    Image other = {0}; cv.punch = &other; cv.punched = false;
    dl = list(&image, 1); raster(&cv, &dl, backdrop);
    CHECK("a different requested image is not punched", !cv.punched);
    CHECK("a different requested image cannot suppress this video", near_rgb(cv.px[12 * 32 + 16], green));

    /* Seeing an image item is not enough: an empty effective clip must not
       claim a GPU hole while every page pixel remains opaque. */
    cv.punch = &im;
    DItem empty = { .op = DO_PUSH_CLIP, .x = 4, .y = 4, .w = 24, .h = 0 };
    DItem empty_items[] = { empty, image, pop_clip }; dl = list(empty_items, 3);
    cv.punched = false; raster(&cv, &dl, backdrop);
    CHECK("zero-height clip cannot enable the overlay", !cv.punched);
    CHECK("zero-height clip leaves the page opaque", cv.px[12 * 32 + 16] == backdrop);
    empty_items[0] = (DItem){ .op = DO_PUSH_CLIP, .x = 29, .y = 4, .w = 3, .h = 16 };
    cv.punched = false; raster(&cv, &dl, backdrop);
    CHECK("non-overlapping clip cannot enable the overlay", !cv.punched);
    CHECK("non-overlapping clip leaves the page opaque", cv.px[12 * 32 + 16] == backdrop);
    DItem disjoint[] = { rectangle, empty_items[0], image, pop_clip, pop_clip };
    dl = list(disjoint, 5); cv.punched = false; raster(&cv, &dl, backdrop);
    CHECK("empty intersection of nested clips cannot enable the overlay", !cv.punched);
    dl = list(&image, 1); cv.punched = false;
    raster_rect(&cv, &dl, backdrop, 0, 0, 3, 3);
    CHECK("damage outside the image cannot claim a new hole", !cv.punched);
    raster(&cv, &dl, backdrop);
    CHECK("visible repaint recovers the overlay after empty clipping", cv.punched && cv.px[12 * 32 + 16] == 0);
    raster_rect(&cv, &dl, backdrop, 0, 0, 3, 3);
    CHECK("unrelated damage preserves an existing hole", cv.punched && cv.px[12 * 32 + 16] == 0);

    /* A fully clipped NV12 frame needs neither an overlay nor a retained
       BGRA copy, even on the normal CPU fallback path. */
    uint8_t nv12[24]; memset(nv12, 128, sizeof nv12);
    Image yuv = { .w = 4, .h = 4, .refs = 1, .yuv = nv12, .opaque = true };
    empty_items[0] = empty; empty_items[1].img = &yuv;
    dl = list(empty_items, 3); cv.punch = NULL; cv.punched = false;
    raster(&cv, &dl, backdrop);
    CHECK("fully clipped CPU video does not allocate a BGRA copy", yuv.px == NULL);
    CHECK("fully clipped CPU video preserves the page", cv.px[12 * 32 + 16] == backdrop);
    free(yuv.px); yuv.px = NULL;
    empty_items[0].r[0] = 4; cv.punch = &yuv;
    raster(&cv, &dl, backdrop);
    CHECK("fully clipped rounded fallback does not allocate a BGRA copy", yuv.px == NULL);
    CHECK("fully clipped rounded fallback cannot enable the overlay", !cv.punched);
    free(yuv.px);
    canvas_free(&cv);
    printf("video punch: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
