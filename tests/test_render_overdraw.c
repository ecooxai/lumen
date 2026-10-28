/* Compare optimized rendering against the unculled rasterizer, not snapshots
 * produced by the optimized implementation. Include raster.c to exercise its
 * private full-cover predicate and independent occlusion-disabled path. */
#include "../src/paint/raster.c"
#include "../src/app/chrome_paint.h"

static int total, failed;
static void check(bool ok, const char *name) {
    total++;
    if (!ok) { failed++; fprintf(stderr, "FAIL render-overdraw: %s\n", name); }
}

static DItem rect(float x, float y, float w, float h, Color color) {
    DItem item = {0}; item.op = DO_RECT;
    item.x = x; item.y = y; item.w = w; item.h = h; item.color = color;
    return item;
}

static Canvas view(uint32_t *pixels, int stride, int x, int y,
                   int w, int h, float scale) {
    Canvas cv = {0}; cv.px = pixels + (size_t)y * stride + x;
    cv.w = w; cv.h = h; cv.stride = stride; cv.scale = scale;
    return cv;
}

static void seed_pixels(uint32_t *pixels, int count) {
    for (int i = 0; i < count; i++) pixels[i] = 0xff000000u | ((uint32_t)i * 2654435761u & 0xffffffu);
}

static void occlusion_case(const char *name, DItem *items, int count,
                           float scale, int expected, bool partial) {
    enum { W = 83, H = 69, VW = 61, VH = 47 };
    uint32_t reference[W * H], actual[W * H];
    seed_pixels(reference, W * H); memcpy(actual, reference, sizeof actual);
    Canvas ref = view(reference, W, 7, 11, VW, VH, scale);
    Canvas got = view(actual, W, 7, 11, VW, VH, scale);
    DisplayList dl = {0}; dl.items.v = items; dl.items.n = count;
    int x0 = partial ? 9 : 0, y0 = partial ? 5 : 0;
    int x1 = partial ? 48 : VW, y1 = partial ? 39 : VH;
    check(occluder(&got, &dl, scale, x0, y0, x1, y1) == expected, name);
    raster_rect_(&ref, &dl, RGBA(218, 231, 246, 255), x0, y0, x1, y1, false);
    raster_rect_(&got, &dl, RGBA(218, 231, 246, 255), x0, y0, x1, y1, true);
    check(!memcmp(reference, actual, sizeof actual), name);
}

static void occlusion_cases(void) {
    const Color solid = RGBA(31, 97, 173, 255);
    DItem items[12] = {0};
    items[0] = rect(0, 0, 61, 47, solid);
    occlusion_case("first solid item replaces clear", items, 1, 1, 0, false);
    items[0] = rect(0, 0, 61, 47, RGBA(201, 47, 89, 93));
    items[1] = rect(-2, -3, 80, 70, solid);
    items[2] = rect(3.25f, 4.75f, 19.5f, 12.25f, RGBA(255, 37, 91, 147));
    occlusion_case("solid rectangle hides previous translucent paint", items, 3, 1, 1, false);
    occlusion_case("fractional scale and subview stride", items, 3, 1.25f, 1, false);
    occlusion_case("partial repaint keeps surrounding pixels", items, 3, 1.5f, 1, true);
    items[1].color = RGBA(31, 97, 173, 254);
    occlusion_case("translucent color cannot occlude", items, 3, 1, -1, false);
    items[1].color = solid; items[1].r[0] = 9;
    occlusion_case("rounded rectangle cannot occlude", items, 3, 1, -1, false);
    items[1].r[0] = 0; items[1].x = .25f;
    occlusion_case("fractional uncovered left edge cannot occlude", items, 3, 1, -1, false);
    items[1] = rect(-.25f, -.75f, 62, 49, solid);
    occlusion_case("fractional bounds outside region fully cover", items, 3, 1, 1, false);
    items[1].w = 60.5f;
    occlusion_case("fractional uncovered right edge cannot occlude", items, 3, 1, -1, false);

    items[0] = (DItem){.op = DO_PUSH_CLIP, .x = 0, .y = 0, .w = 60, .h = 47};
    items[1] = rect(-2, -2, 80, 70, solid);
    items[2] = (DItem){.op = DO_POP_CLIP};
    occlusion_case("clip missing one pixel cannot occlude", items, 3, 1, -1, false);
    items[0].w = 61;
    occlusion_case("full unrounded clip preserves opaque coverage", items, 3, 1, 1, false);
    items[0].r[2] = 8;
    occlusion_case("rounded clip cannot occlude", items, 3, 1, -1, false);
    items[0] = (DItem){.op = DO_PUSH_LAYER, .alpha = .5f};
    items[2] = (DItem){.op = DO_POP_LAYER};
    occlusion_case("opacity layer cannot occlude parent pixels", items, 3, 1, -1, false);
    items[0].alpha = 1;
    occlusion_case("layer scope remains conservative at opacity one", items, 3, 1, -1, false);

    uint32_t image_pixels[4] = {0xff204060u, 0xff80a0c0u, 0xff102030u, 0xff456789u};
    Image image_data = {.w = 2, .h = 2, .px = image_pixels, .opaque = true};
    items[0] = (DItem){.op = DO_IMAGE, .x = 0, .y = 0, .w = 61, .h = 47, .img = &image_data};
    occlusion_case("first opaque image still replaces clear", items, 1, 1, 0, false);
    image_data.opaque = false;
    occlusion_case("nonopaque image remains unculled", items, 1, 1, -1, false);

    /* Vary device scale and edge phase; only whole-pixel coverage may skip
     * earlier paint. The comparison also catches fractional-edge rounding. */
    for (int k = 0; k < 24; k++) {
        float scale = 1 + (k % 4) * .25f;
        float inset = (k % 3) * .17f;
        items[0] = rect(0, 0, 100, 100, RGBA(201, 47, 89, 93));
        items[1] = rect(-inset, -inset, 61 / scale + 2 * inset, 47 / scale + 2 * inset, solid);
        items[2] = rect(4.1f, 7.3f, 13.7f, 18.4f, RGBA(41, 229, 167, 128));
        int expected = ceilf(items[1].x * scale) <= 0 && ceilf(items[1].y * scale) <= 0 &&
                       floorf((items[1].x + items[1].w) * scale) >= 61 &&
                       floorf((items[1].y + items[1].h) * scale) >= 47 ? 1 : -1;
        occlusion_case("scaled rectangle pixel equivalence", items, 3, scale, expected, false);
    }
}

static void copy_page(Canvas *cv, int page_x, int page_y) {
    for (int y = page_y; y < cv->h; y++) for (int x = page_x; x < cv->w; x++)
        /* Include transparent holes: chrome must be overwritten by memcpy
         * semantics even when the replacement page pixel is alpha zero. */
        cv->px[(size_t)y * cv->stride + x] = (x + y) % 7 == 0 ? 0 : 0xff000000u | ((uint32_t)(x * 3 + y * 11) * 104729u & 0xffffffu);
}

static void chrome_case(int sidebar, int toolbar, int offset_x, int offset_y,
                        float scale, bool overlay) {
    enum { W = 101, H = 87, VW = 79, VH = 63 };
    uint32_t reference[W * H], actual[W * H], seed[W * H];
    seed_pixels(seed, W * H); memcpy(reference, seed, sizeof seed); memcpy(actual, seed, sizeof seed);
    Canvas ref = view(reference, W, offset_x, offset_y, VW, VH, scale);
    Canvas got = view(actual, W, offset_x, offset_y, VW, VH, scale);
    DItem items[8] = {0};
    items[0] = rect(0, 0, 101, 87, RGBA(240, 242, 248, 255));
    items[1] = rect(4.25f, 2.5f, 56.75f, 19.25f, RGBA(33, 87, 214, 181));
    items[1].r[0] = items[1].r[1] = items[1].r[2] = items[1].r[3] = 4;
    items[2] = (DItem){.op = DO_PUSH_LAYER, .alpha = .65f};
    items[3] = rect(3, 9, 31, 56, RGBA(179, 73, 119, 255));
    items[4] = (DItem){.op = DO_POP_LAYER};
    DisplayList dl = {0}; dl.items.v = items; dl.items.n = 5;
    raster(&ref, &dl, RGBA(255, 255, 255, 255));
    paint_browser_chrome(&got, &dl, RGBA(255, 255, 255, 255), sidebar, toolbar);
    bool page_untouched = true;
    for (int y = toolbar; y < VH; y++) for (int x = sidebar; x < VW; x++) {
        size_t index = (size_t)(offset_y + y) * W + offset_x + x;
        page_untouched &= actual[index] == seed[index];
    }
    check(page_untouched, "chrome never writes page pixels that will be replaced");
    copy_page(&ref, sidebar, toolbar); copy_page(&got, sidebar, toolbar);
    if (overlay) {
        DItem floating = rect((sidebar - 4) / scale, (toolbar - 3) / scale, 29 / scale, 24 / scale, RGBA(15, 48, 91, 181));
        floating.r[0] = floating.r[1] = floating.r[2] = floating.r[3] = 3;
        DisplayList floats = {0}; floats.items.v = &floating; floats.items.n = 1;
        raster_rect(&ref, &floats, RGBA(255, 255, 255, 255), sidebar - 4, toolbar - 3, sidebar + 25, toolbar + 21);
        raster_rect(&got, &floats, RGBA(255, 255, 255, 255), sidebar - 4, toolbar - 3, sidebar + 25, toolbar + 21);
    }
    check(!memcmp(reference, actual, sizeof actual), "chrome clipping preserves every final pixel and safe-area padding");
}

int main(void) {
    occlusion_cases();
    chrome_case(0, 17, 0, 0, 1, false);
    chrome_case(19, 17, 0, 0, 1, false);
    chrome_case(0, 17, 9, 13, 1.25f, true);
    chrome_case(19, 17, 9, 13, 1.5f, true);
    chrome_case(1, 1, 3, 7, 2, true);
    chrome_case(78, 62, 9, 13, 1.25f, false);
    printf("RESULTS render-overdraw %s (%d/%d checks)\n", failed ? "FAIL" : "PASS", total - failed, total);
    return failed ? 1 : 0;
}
