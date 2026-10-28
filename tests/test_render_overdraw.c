/* Compare optimized rendering against the unculled rasterizer, not snapshots
 * produced by the optimized implementation. Include raster.c to exercise its
 * private full-cover predicate and independent occlusion-disabled path. */
#define font_glyph test_font_glyph
#include "../src/paint/raster.c"
#undef font_glyph
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

/* Independent scalar reference is retained from the pre-optimization renderer.
 * Randomized cases exercise the actual private fill(), including fractional
 * geometry and byte-for-byte preservation outside clipped subviews. */
static void reference_fill(R *r, float x0, float y0, float x1, float y1, const float rad[4], Color col) {
    if (!COLOR_A(col) || x1 <= x0 || y1 <= y0) return;
    const Clip *c = clip(r);
    int ix0 = LMAX(c->x0, (int)floorf(x0)), iy0 = LMAX(c->y0, (int)floorf(y0)), ix1 = LMIN(c->x1, (int)ceilf(x1)), iy1 = LMIN(c->y1, (int)ceilf(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    bool round = rad && (rad[0] > 0 || rad[1] > 0 || rad[2] > 0 || rad[3] > 0);
    uint32_t solid = premul(col, 1);
    int W = r->c->stride;
    if (!round && !c->round) {
        bool opaque = (solid >> 24) == 255;
        int ax = LMAX(ix0, (int)ceilf(x0)), bx = LMIN(ix1, (int)floorf(x1)); if (bx < ax) bx = ax;
        for (int y = iy0; y < iy1; y++) {
            uint32_t *row = r->px + (size_t)y * (size_t)W;
            float cy = span_cov(y0, y1, y);
            bool full = cy >= 0.999f;
            for (int x = ix0; x < ix1; x++) {
                if (full && x == ax) {
                    if (opaque) for (; x < bx; x++) row[x] = solid; else for (; x < bx; x++) over(&row[x], solid);
                    if (x >= ix1) break;
                }
                float cov = cy * span_cov(x0, x1, x);
                if (cov <= 0) continue;
                over(&row[x], cov >= 0.999f ? solid : premul(col, cov));
            }
        }
        return;
    }
    for (int y = iy0; y < iy1; y++) {
        uint32_t *row = r->px + (size_t)y * (size_t)W;
        float cy = span_cov(y0, y1, y);
        float rmax = round ? LMAX(LMAX(rad[0], rad[1]), LMAX(rad[2], rad[3])) : 0;
        bool corner_row = round && (y < y0 + rmax + 1 || y + 1 > y1 - rmax - 1);
        for (int x = ix0; x < ix1; x++) {
            float cov;
            if (corner_row) { float d = sd_rrect(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad); cov = LCLAMP(0.5f - d, 0, 1); }
            else cov = cy * span_cov(x0, x1, x);
            if (clip(r)->round) cov *= clip_cov(r, x + 0.5f, y + 0.5f);
            if (cov <= 0) continue;
            if (cov >= 0.999f) over(&row[x], solid); else over(&row[x], premul(col, cov));
        }
    }
}

static uint32_t span_random(uint32_t *state) {
    uint32_t x = *state; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}
static void fractional_span_cases(void) {
    enum { W = 83, H = 69, VW = 61, VH = 47 };
    static const float phases[] = {0, .0005f, .00099f, .001f, .00101f, .17f, .2f, .25f, .5f, .8f, .999f, .9995f};
    uint32_t expected[W * H], actual[W * H], state = 0x3491d7abu;
    bool equal = true; unsigned completed = 0;
    for (unsigned i = 0; i < 12000; i++) {
        seed_pixels(expected, W * H); memcpy(actual, expected, sizeof actual);
        Canvas ref_cv = view(expected, W, 7, 11, VW, VH, 1);
        Canvas got_cv = view(actual, W, 7, 11, VW, VH, 1);
        R ref = {.c = &ref_cv, .s = 1, .nclip = 1, .px = ref_cv.px};
        R got = {.c = &got_cv, .s = 1, .nclip = 1, .px = got_cv.px};
        unsigned mask = span_random(&state);
        int cx0 = mask & 7, cy0 = (mask >> 3) & 7;
        int cx1 = VW - ((mask >> 6) & 7), cy1 = VH - ((mask >> 9) & 7);
        Clip clip = {.x0 = cx0, .y0 = cy0, .x1 = cx1, .y1 = cy1,
                     .fx = cx0 + .17f, .fy = cy0 + .25f, .fw = cx1-cx0-.34f, .fh = cy1-cy0-.5f,
                     .r = {3, 5, 7, 9}, .round = i % 19 == 0};
        ref.clips[0] = got.clips[0] = clip;
        float x0 = (int)(span_random(&state) % 100) - 20 + phases[i % 12];
        float y0 = (int)(span_random(&state) % 90) - 20 + phases[(i / 12) % 12];
        float width = (span_random(&state) % 1000 + 1) / 16.f;
        float height = (span_random(&state) % 1000 + 1) / 16.f;
        /* Every eighth case is less than one device pixel in both axes. */
        if (i % 8 == 0) { width *= .01f; height *= .01f; }
        uint32_t color = (span_random(&state) & 0xffffffu) | ((i % 256) << 24);
        float radii[4] = {0};
        if (i % 23 == 0) { radii[0] = 2; radii[1] = 5; radii[2] = 1; radii[3] = 7; }
        const float *rad = i % 3 ? radii : NULL;
        reference_fill(&ref, x0, y0, x0+width, y0+height, rad, color);
        fill(&got, x0, y0, x0+width, y0+height, rad, color);
        completed++;
        if (memcmp(expected, actual, sizeof actual)) {
            equal = false;
            fprintf(stderr, "Fractional span mismatch case=%u color=%08x box=%.9g,%.9g,%.9g,%.9g\n", i, color, x0, y0, width, height);
            break;
        }
    }
    check(equal && completed == 12000, "12000 fractional/alpha/clipped spans match independent scalar pixels");
}

static Font text_fonts[2];
static Glyph text_glyphs[5];
static uint8_t text_alpha[2][16 * 16];
static uint32_t text_rgba[16 * 16];
static unsigned glyph_lookups;
const Glyph *test_font_glyph(Font *font, uint32_t gid, float scale) {
    glyph_lookups++;
    if (gid == 0xffff) return NULL;
    if (gid == 0xfffe) return &text_glyphs[4];
    unsigned index = (gid ^ (gid >> 6)) + (font == &text_fonts[1] ? 1 : 0) + (scale > 1.5f ? 2 : 0);
    return &text_glyphs[index % 4];
}
/* Frozen original scalar text path. It intentionally performs every original
 * font lookup, opacity operation, clip calculation and integer rounding. */
static void reference_text(R *r, const DItem *it) {
    float s = r->s, gs = s * (it->ts > 0 ? it->ts : 1); const Clip *c = clip(r);
    uint32_t ca = COLOR_A(it->color);
    for (int i = 0; i < it->ng; i++) {
        const DGlyph *dg = &it->g[i];
        if (!dg->font) continue;
        const Glyph *gl = test_font_glyph(dg->font, dg->gid, gs);
        if (!gl || !gl->w) continue;
        int ox = (int)lroundf(dg->x * s) + gl->left, oy = (int)lroundf(dg->y * s) - gl->top;
        if (ox >= c->x1 || oy >= c->y1 || ox + gl->w <= c->x0 || oy + gl->h <= c->y0) continue;
        for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
            uint32_t *row = r->px + (size_t)(oy + y) * (size_t)r->c->stride;
            for (int x = LMAX(0, c->x0 - ox); x < gl->w && ox + x < c->x1; x++) {
                float cc = clip(r)->round ? clip_cov(r, ox + x + 0.5f, oy + y + 0.5f) : 1;
                if (gl->color && gl->rgba) { uint32_t p = gl->rgba[y * gl->w + x]; if (p >> 24) over(&row[ox + x], cc < 1 ? scale_px(p, (uint32_t)(cc * 256)) : p); }
                else if (gl->alpha) { uint32_t a = gl->alpha[y * gl->w + x]; if (a) { a = a * ca / 255; over(&row[ox + x], premul(it->color | 0xff000000u, a / 255.f * cc)); } }
            }
        }
    }
}
static void glyph_pixel_cases(void) {
    enum { W = 83, H = 69, VW = 61, VH = 47 };
    static const Color colors[] = {0x000000,0xffffff,0x0102fe,0x1747b3,0xff0080,0x80ff01,0x123456};
    uint32_t expected[W * H], actual[W * H];
    for (unsigned i = 0; i < 256; i++) {
        text_alpha[0][i] = i; text_alpha[1][i] = 255 - i;
        text_rgba[i] = premul(0xffe17935u, i / 255.f);
    }
    text_glyphs[0] = (Glyph){.w=16,.h=16,.left=-2,.top=11,.alpha=text_alpha[0]};
    text_glyphs[1] = (Glyph){.w=16,.h=16,.left=1,.top=9,.alpha=text_alpha[1]};
    text_glyphs[2] = (Glyph){.w=16,.h=16,.left=-1,.top=12,.color=true,.rgba=text_rgba};
    text_glyphs[3] = (Glyph){.w=16,.h=16,.left=0,.top=10,.color=true,.alpha=text_alpha[0]};
    Canvas reference = view(expected,W,7,11,VW,VH,1), canvas = view(actual,W,7,11,VW,VH,1);
    R ref = {.c=&reference,.s=1,.nclip=1,.px=reference.px};
    R got = {.c=&canvas,.s=1,.nclip=1,.px=canvas.px};
    bool equal = true;unsigned completed = 0;
    for (unsigned i = 0; i < 256 * 7 * 4; i++) {
        seed_pixels(expected,W*H);memcpy(actual,expected,sizeof actual);
        ref.s = got.s = 1 + ((i / (256 * 7)) % 4) * .25f;
        Clip clipping = {.x0=i%5,.y0=(i/5)%5,.x1=VW-((i/25)%5),.y1=VH-((i/125)%5),
                         .fx=.17f,.fy=.25f,.fw=VW-.34f,.fh=VH-.5f,.r={3,5,7,9},.round=i%11==0};
        ref.clips[0] = got.clips[0] = clipping;
        DGlyph glyphs[] = {
            {.gid=0,.x=-2.25f,.y=7.5f,.font=&text_fonts[i%2]},
            {.gid=1,.x=8.5f,.y=18.25f,.font=&text_fonts[(i+1)%2]},
            {.gid=65,.x=24.17f,.y=13.75f,.font=&text_fonts[i%2]},
            {.gid=2,.x=37.25f,.y=38.25f,.font=&text_fonts[(i+1)%2]},
            {.gid=3,.x=18.25f,.y=45.5f,.font=&text_fonts[i%2]},
            {.gid=0xffff,.x=12,.y=24,.font=&text_fonts[0]},
            {.gid=0xfffe,.x=12,.y=24,.font=&text_fonts[0]},
            {.gid=1,.x=12,.y=24,.font=NULL}
        };
        DItem item = {.op=DO_TEXT,.color=colors[(i/256)%7] | ((i%256)<<24),.g=glyphs,.ng=8,.ts=i%3==0?1.5f:1};
        reference_text(&ref,&item);text(&got,&item);completed++;
        if (memcmp(expected,actual,sizeof actual)) {
            equal=false;fprintf(stderr,"Glyph cache pixel mismatch case=%u color=%08x scale=%.9g round=%d\n",i,item.color,got.s,clipping.round);break;
        }
    }
    check(equal && completed==256*7*4,"7168 text/color/opacity/font/scale/rounded-clip cases match original scalar pixels");
    /* Cache repeated glyphs only within this single raster invocation. All
       colors and positions still pass through the real text path. */
    seed_pixels(expected,W*H);memcpy(actual,expected,sizeof actual);
    memset(&ref,0,sizeof ref);ref.c=&reference;ref.s=1;ref.nclip=1;ref.px=reference.px;
    memset(&got,0,sizeof got);got.c=&canvas;got.s=1;got.nclip=1;got.px=canvas.px;
    ref.clips[0]=got.clips[0]=(Clip){.x1=VW,.y1=VH};
    DGlyph repeated[100];
    for (unsigned i=0;i<100;i++) repeated[i]=(DGlyph){.gid=1,.x=4.25f,.y=18.5f,.font=&text_fonts[0]};
    DItem repeated_item={.op=DO_TEXT,.color=0xa53179bfu,.g=repeated,.ng=100};
    glyph_lookups=0;reference_text(&ref,&repeated_item);unsigned reference_lookups=glyph_lookups;
    glyph_lookups=0;text(&got,&repeated_item);unsigned actual_lookups=glyph_lookups;
    check(!memcmp(expected,actual,sizeof actual),"repeated translucent glyphs preserve exact accumulated pixels");
    check(reference_lookups==100 && actual_lookups==1,"one raster call reuses an exact repeated font/glyph/scale lookup");
    R fresh={.c=&canvas,.s=1,.nclip=1,.px=canvas.px};fresh.clips[0]=got.clips[0];
    glyph_lookups=0;text(&fresh,&repeated_item);
    check(glyph_lookups==1,"a fresh raster invocation never inherits a previous frame glyph pointer");
}
int main(void) {
    glyph_pixel_cases();
    fractional_span_cases();
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
