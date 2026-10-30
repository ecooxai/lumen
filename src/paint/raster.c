/* CPU rasterizer: premultiplied ARGB32, anti-aliased shapes, glyphs, images */
#include "paint.h"
#include <math.h>
#include <pthread.h>
#include <zlib.h>
#if defined(__aarch64__) && defined(__ARM_NEON) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_neon.h>
#define IMAGE_NEON 1
#endif

typedef struct { int x0, y0, x1, y1; float fx, fy, fw, fh, r[4]; bool round; } Clip;
typedef struct { int x0, y0, x1, y1; } LayerRect;
typedef struct { uint32_t *px; float alpha; int origin_x, origin_y, stride, height; } LayerSave;
typedef struct {
    Canvas *c; float s;
    Clip clips[64]; int nclip;
    LayerSave layers[16]; int nlayer;
    uint32_t *px;
    int origin_x, origin_y, stride, height; /* tightly bounded storage for opacity groups */
    bool quiet;                             /* a band after the first: leaves the canvas' punch results alone */
    uint8_t lut[256]; Color lut_color; bool lut_ok;
} R;

static inline uint32_t *raster_row(R *r, int y) {
    if (!r->nlayer && r->c->row_offset) return canvas_row(r->c, y);
    return r->px + (size_t)(y - r->origin_y) * (size_t)r->stride;
}

void canvas_init(Canvas *c, int w, int h, float scale) { c->w = w; c->h = h; c->stride = w; c->scale = scale; c->punch = NULL; c->punched = false; c->row_offset = 0; memset(c->punch_more, 0, sizeof c->punch_more); c->punched_more = 0; c->px = xcalloc((size_t)w * (size_t)h, 4); }
void canvas_free(Canvas *c) { free(c->px); c->px = NULL; }

static inline uint32_t premul(Color c, float cov) {
    uint32_t a = (uint32_t)(COLOR_A(c) * cov + 0.5f);
    if (!a) return 0;
    return (a << 24) | ((COLOR_R(c) * a / 255) << 16) | ((COLOR_G(c) * a / 255) << 8) | (COLOR_B(c) * a / 255);
}
static inline uint32_t scale_px(uint32_t p, uint32_t k) { /* k 0..256 */
    uint32_t rb = ((p & 0x00ff00ff) * k >> 8) & 0x00ff00ff, ag = (((p >> 8) & 0x00ff00ff) * k) & 0xff00ff00;
    return rb | ag;
}
static inline void over(uint32_t *d, uint32_t s) {
    uint32_t sa = s >> 24;
    if (sa == 255) { *d = s; return; }
    if (!sa && !s) return;
    *d = s + scale_px(*d, 256 - sa - (sa >> 7));
}
/* Straight-color coverage must be combined with the destination before
   quantization. Separately truncating premultiplied source and destination
   darkens text even when both colors are identical. Each packed 16-bit lane
   is at most 255*255, so this is exact nearest division by 255 without carry. */
static inline uint32_t round_255_pair(uint32_t v) {
    v += 0x00800080u;
    return ((v + ((v >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
}
static inline void over_color_coverage(uint32_t *d, Color color, uint32_t a) {
    if (!a) return;
    color |= 0xff000000u;
    if (a == 255) { *d = color; return; }
    uint32_t inv = 255 - a, dst = *d;
    uint32_t rb = (color & 0x00ff00ffu) * a + (dst & 0x00ff00ffu) * inv;
    uint32_t ag = ((color >> 8) & 0x00ff00ffu) * a + ((dst >> 8) & 0x00ff00ffu) * inv;
    *d = round_255_pair(rb) | (round_255_pair(ag) << 8);
}
/* Keep constant-color spans independent of raster/clip metadata. Besides
   avoiding repeated alpha branches, these loops can be vectorized without
   reloading potentially aliased coordinates after each pixel store. */
static void store_span(uint32_t *dst, int count, uint32_t color) {
    for (int i = 0; i < count; i++) dst[i] = color;
}
static void over_span(uint32_t *dst, int count, uint32_t color) {
    uint32_t alpha = color >> 24;
    if (alpha == 255) { store_span(dst, count, color); return; }
    if (!color) return;
    uint32_t k = 256 - alpha - (alpha >> 7);
    for (int i = 0; i < count; i++) dst[i] = color + scale_px(dst[i], k);
}
static inline const Clip *clip(R *r) { return &r->clips[r->nclip - 1]; }

static float sd_rrect(float px, float py, float x0, float y0, float x1, float y1, const float rad[4]) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
    float qx = px - cx, qy = py - cy;
    float rr = qx < 0 ? (qy < 0 ? rad[0] : rad[3]) : (qy < 0 ? rad[1] : rad[2]);
    float ax = fabsf(qx) - hw + rr, ay = fabsf(qy) - hh + rr;
    float ox = LMAX(ax, 0), oy = LMAX(ay, 0);
    return LMIN(LMAX(ax, ay), 0) + sqrtf(ox * ox + oy * oy) - rr;
}
static inline float clip_cov(R *r, float px, float py) {
    const Clip *c = clip(r);
    if (!c->round) return 1;
    float d = sd_rrect(px, py, c->fx, c->fy, c->fx + c->fw, c->fy + c->fh, c->r);
    return LCLAMP(0.5f - d, 0, 1);
}
static inline float span_cov(float a0, float a1, int p) { float lo = LMAX(a0, (float)p), hi = LMIN(a1, (float)p + 1); return hi > lo ? hi - lo : 0; }
bool raster_pixel_rows;   /* test hook: evaluate every pixel of rounded rows and replay every item, the reference for the spans and culling below */
/* The first pixel in [lo, hi] whose center is past v (at or past it unless strict). sd_rrect picks a
   corner by comparing the center with the box's middle, so the spans below split exactly where it does. */
static int first_center(int lo, int hi, float v, bool strict) {
    int x = v <= (float)lo ? lo : v >= (float)hi ? hi : (int)floorf(v);
    while (x > lo && (strict ? (x - 1) + 0.5f > v : (x - 1) + 0.5f >= v)) x--;
    while (x < hi && !(strict ? x + 0.5f > v : x + 0.5f >= v)) x++;
    return x;
}
static inline int span_bound(float v, int lo, int hi) { return v <= (float)lo ? lo : v >= (float)hi ? hi : (int)v; }

/* fill rect or rounded rect (device coords) */
static void fill(R *r, float x0, float y0, float x1, float y1, const float rad[4], Color col) {
    if (!COLOR_A(col) || x1 <= x0 || y1 <= y0) return;
    const Clip *c = clip(r);
    int ix0 = LMAX(c->x0, (int)floorf(x0)), iy0 = LMAX(c->y0, (int)floorf(y0)), ix1 = LMIN(c->x1, (int)ceilf(x1)), iy1 = LMIN(c->y1, (int)ceilf(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    bool round = rad && (rad[0] > 0 || rad[1] > 0 || rad[2] > 0 || rad[3] > 0);
    uint32_t solid = premul(col, 1);
    float rmax = round ? LMAX(LMAX(rad[0], rad[1]), LMAX(rad[2], rad[3])) : 0;
    if (!c->round) {   /* rows clear of the corners are plain spans */
        int ax = LMAX(ix0, (int)ceilf(x0)), bx = LMIN(ix1, (int)floorf(x1)); if (bx < ax) bx = ax;
        for (int y = iy0; y < iy1; y++) {
            if (round && (y < y0 + rmax + 1 || y + 1 > y1 - rmax - 1)) {
                /* Pixels a pixel clear of both corner radii see only the top or bottom edge, so each half
                   of the row between the corners has one coverage: a span (pills are all corner rows). */
                uint32_t *row = raster_row(r, y); int ox = r->origin_x;
                int ml = span_bound(ceilf(x0 + LMAX(rad[0], rad[3]) + 1), ix0, ix1), mr = span_bound(floorf(x1 - LMAX(rad[1], rad[2]) - 1), ix0, ix1);
                if (raster_pixel_rows || mr <= ml) ml = mr = ix1;
                int mc = first_center(ml, mr, (x0 + x1) / 2, false);
                const int cut[5] = { ix0, ml, mc, mr, ix1 };
                for (int k = 0; k < 4; k++) {
                    int a = cut[k], b = cut[k + 1];
                    if (a >= b) continue;
                    if (k == 1 || k == 2) {
                        float cov = LCLAMP(0.5f - sd_rrect(a + 0.5f, y + 0.5f, x0, y0, x1, y1, rad), 0, 1);
                        if (cov > 0) over_span(row + a - ox, b - a, cov >= 0.999f ? solid : premul(col, cov));
                        continue;
                    }
                    for (int x = a; x < b; x++) {
                        float cov = LCLAMP(0.5f - sd_rrect(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad), 0, 1);
                        if (cov > 0) over(&row[x - ox], cov >= 0.999f ? solid : premul(col, cov));
                    }
                }
                continue;
            }
            uint32_t *row = raster_row(r, y) + ix0 - r->origin_x;
            float cy = span_cov(y0, y1, y);
            for (int x = ix0; x < ax; x++) {
                float cov = cy * span_cov(x0, x1, x);
                if (cov > 0) over(&row[x - ix0], cov >= 0.999f ? solid : premul(col, cov));
            }
            over_span(row + ax - ix0, bx - ax, cy >= 0.999f ? solid : premul(col, cy));
            for (int x = bx; x < ix1; x++) {
                float cov = cy * span_cov(x0, x1, x);
                if (cov > 0) over(&row[x - ix0], cov >= 0.999f ? solid : premul(col, cov));
            }
        }
        return;
    }
    for (int y = iy0; y < iy1; y++) {
        uint32_t *row = raster_row(r, y);
        float cy = span_cov(y0, y1, y);
        bool corner_row = round && (y < y0 + rmax + 1 || y + 1 > y1 - rmax - 1);
        for (int x = ix0; x < ix1; x++) {
            float cov;
            if (corner_row) { float d = sd_rrect(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad); cov = LCLAMP(0.5f - d, 0, 1); }
            else cov = cy * span_cov(x0, x1, x);
            if (clip(r)->round) cov *= clip_cov(r, x + 0.5f, y + 0.5f);
            if (cov <= 0) continue;
            if (cov >= 0.999f) over(&row[x - r->origin_x], solid); else over(&row[x - r->origin_x], premul(col, cov));
        }
    }
}

static void border(R *r, const DItem *it) {
    float s = r->s, x0 = it->x * s, y0 = it->y * s, x1 = (it->x + it->w) * s, y1 = (it->y + it->h) * s;
    float rad[4]; for (int i = 0; i < 4; i++) rad[i] = it->r[i] * s;
    float bw[4]; for (int i = 0; i < 4; i++) bw[i] = it->bw[i] > 0 ? LMAX(1, roundf(it->bw[i] * s)) : 0;
    bool round = rad[0] > 0 || rad[1] > 0 || rad[2] > 0 || rad[3] > 0;
    if (round) {
        /* ring: outer rrect minus inner rrect, single color */
        Color col = it->bc[0]; for (int i = 0; i < 4; i++) if (bw[i] > 0) { col = it->bc[i]; break; }
        float ir[4] = { LMAX(0, rad[0] - LMAX(bw[0], bw[3])), LMAX(0, rad[1] - LMAX(bw[0], bw[1])), LMAX(0, rad[2] - LMAX(bw[2], bw[1])), LMAX(0, rad[3] - LMAX(bw[2], bw[3])) };
        float ix0 = x0 + bw[3], iy0 = y0 + bw[0], ix1 = x1 - bw[1], iy1 = y1 - bw[2];
        const Clip *c = clip(r);
        int px0 = LMAX(c->x0, (int)floorf(x0)), py0 = LMAX(c->y0, (int)floorf(y0)), px1 = LMIN(c->x1, (int)ceilf(x1)), py1 = LMIN(c->y1, (int)ceilf(y1));
        if (px0 >= px1) return;
        bool inner = ix1 > ix0 && iy1 > iy0, flat = !c->round && !raster_pixel_rows;
        float rmax = LMAX(LMAX(rad[0], rad[1]), LMAX(rad[2], rad[3])), irl = LMAX(ir[0], ir[3]), irr = LMAX(ir[1], ir[2]), irmax = LMAX(irl, irr);
        float ocy = (y0 + y1) / 2, ohh = (y1 - y0) / 2, icy = (iy0 + iy1) / 2, ihh = (iy1 - iy0) / 2;
        int omid = first_center(px0, px1, (x0 + x1) / 2, false), imid = inner ? first_center(px0, px1, (ix0 + ix1) / 2, false) : omid;
        for (int y = py0; y < py1; y++) {
            float fy = y + 0.5f;
            uint32_t *row = raster_row(r, y); int ox = r->origin_x;
            /* Inside the inner edge both shapes cover fully: nothing to draw. Rows clear of the inner
               corners need only a pixel of margin from its sides. */
            int sa = px1, sb = px1;
            if (inner && fy > iy0 + 1 && fy < iy1 - 1) {
                bool sides = !raster_pixel_rows && fy > iy0 + irmax + 1 && fy < iy1 - irmax - 1;
                float kl = sides ? 1 : irl + 1, kr = sides ? 1 : irr + 1;
                sa = first_center(px0, px1, ix0 + kl, true); sb = first_center(px0, px1, ix1 - kr, false);
                if (sb <= sa) sa = sb = px1;
            }
            /* sd_rrect depends on y alone where a pixel is at least the corner radius from both vertical
               edges and no nearer to them than to the horizontal ones, within one quadrant: spans there. */
            float ho = LMAX(rmax, ohh - fabsf(fy - ocy)), hi = inner ? LMAX(rmax, ihh - fabsf(fy - icy)) : 0;
            int sl = span_bound(ceilf(LMAX(x0 + ho, inner ? ix0 + hi : x0) + 1), px0, px1);
            int sr = span_bound(floorf(LMIN(x1 - ho, inner ? ix1 - hi : x1) - 1), px0, px1);
            int cut[8] = { px0, px1, sa, sb, sl, sr, omid, imid }, n = 8;
            for (int i = 1; i < n; i++) for (int j = i; j > 0 && cut[j - 1] > cut[j]; j--) { int t = cut[j]; cut[j] = cut[j - 1]; cut[j - 1] = t; }
            for (int k = 0; k + 1 < n; k++) {
                int a = cut[k], b = cut[k + 1];
                if (a >= b || (a >= sa && b <= sb)) continue;
                bool span = flat && a >= sl && b <= sr;
                for (int x = a; x < b; x++) {
                    float fx = x + 0.5f;
                    float co = LCLAMP(0.5f - sd_rrect(fx, fy, x0, y0, x1, y1, rad), 0, 1);
                    float ci = inner ? LCLAMP(0.5f - sd_rrect(fx, fy, ix0, iy0, ix1, iy1, ir), 0, 1) : 0;
                    float cov = (co - ci) * clip_cov(r, fx, fy);
                    if (span) { if (cov > 0) over_span(row + a - ox, b - a, premul(col, cov)); break; }
                    if (cov > 0) over(&row[x - ox], premul(col, cov));
                }
            }
        }
        return;
    }
    /* straight sides */
    struct { float ax, ay, bx, by; int i; } sides[4] = {
        { x0, y0, x1, y0 + bw[0], 0 }, { x1 - bw[1], y0 + bw[0], x1, y1 - bw[2], 1 },
        { x0, y1 - bw[2], x1, y1, 2 }, { x0, y0 + bw[0], x0 + bw[3], y1 - bw[2], 3 } };
    for (int k = 0; k < 4; k++) {
        int i = sides[k].i; if (bw[i] <= 0) continue;
        Color col = it->bc[i]; int st = it->bs[i];
        if (st == BS_INSET || st == BS_GROOVE) col = (i == 0 || i == 3) ? RGBA(COLOR_R(col) * 2 / 3, COLOR_G(col) * 2 / 3, COLOR_B(col) * 2 / 3, COLOR_A(col)) : col;
        else if (st == BS_OUTSET || st == BS_RIDGE) col = (i == 1 || i == 2) ? RGBA(COLOR_R(col) * 2 / 3, COLOR_G(col) * 2 / 3, COLOR_B(col) * 2 / 3, COLOR_A(col)) : col;
        if (st == BS_DASHED || st == BS_DOTTED) {
            bool horiz = i == 0 || i == 2;
            float len = horiz ? sides[k].bx - sides[k].ax : sides[k].by - sides[k].ay;
            float seg = st == BS_DOTTED ? bw[i] : bw[i] * 3;
            int n = LMAX(1, (int)(len / (2 * seg)));
            float step = len / n;
            for (int j = 0; j < n; j++) {
                float a = (horiz ? sides[k].ax : sides[k].ay) + j * step, b = a + LMIN(seg, step);
                float dr[4] = { 0 }; if (st == BS_DOTTED) for (int q = 0; q < 4; q++) dr[q] = bw[i] / 2;
                if (horiz) fill(r, a, sides[k].ay, b, sides[k].by, dr, col); else fill(r, sides[k].ax, a, sides[k].bx, b, dr, col);
            }
        } else if (st == BS_DOUBLE && bw[i] >= 3) {
            float t = bw[i] / 3; bool horiz = i == 0 || i == 2;
            if (horiz) { fill(r, sides[k].ax, sides[k].ay, sides[k].bx, sides[k].ay + t, NULL, col); fill(r, sides[k].ax, sides[k].by - t, sides[k].bx, sides[k].by, NULL, col); }
            else { fill(r, sides[k].ax, sides[k].ay, sides[k].ax + t, sides[k].by, NULL, col); fill(r, sides[k].bx - t, sides[k].ay, sides[k].bx, sides[k].by, NULL, col); }
        } else fill(r, sides[k].ax, sides[k].ay, sides[k].bx, sides[k].by, NULL, col);
    }
}

typedef struct { float x0, y0, x1, y1, bl, bx0, by0, bx1, by1, rad[4], brad[4]; Color color; } ShadowGeom;
/* One shadow pixel: its premultiplied color, 0 when it adds nothing. */
static inline uint32_t shadow_px(R *r, const ShadowGeom *g, int x, float fy, bool box_row) {
    float fx = x + 0.5f;
    float d = sd_rrect(fx, fy, g->x0, g->y0, g->x1, g->y1, g->rad);
    float t = LCLAMP((d + g->bl) / (2 * g->bl), 0, 1);
    float cov = 1 - t * t * (3 - 2 * t);
    if (box_row && fx > g->bx0 - 0.5f && fx < g->bx1 + 0.5f) cov *= 1 - LCLAMP(0.5f - sd_rrect(fx, fy, g->bx0, g->by0, g->bx1, g->by1, g->brad), 0, 1);
    if (cov <= 0.002f) return 0;
    cov *= clip_cov(r, fx, fy);
    return cov > 0.002f ? premul(g->color, cov) : 0;
}
static void shadow(R *r, const DItem *it) {
    float s = r->s, sp = it->spread * s, bl = LMAX(0.5f, it->blur * s);
    float x0 = it->x * s - sp, y0 = it->y * s - sp, x1 = (it->x + it->w) * s + sp, y1 = (it->y + it->h) * s + sp;
    float rad[4]; for (int i = 0; i < 4; i++) rad[i] = LMAX(0, it->r[i] * s + sp);
    /* An outer shadow is clipped out inside the border box. Skipping its
     * straight interior avoids a distance evaluation for most pixels. */
    float bx0 = (it->x - it->ox) * s, by0 = (it->y - it->oy) * s, bx1 = bx0 + it->w * s, by1 = by0 + it->h * s;
    float brad[4], bmax = 0; for (int i = 0; i < 4; i++) { brad[i] = it->r[i] * s; bmax = LMAX(bmax, brad[i]); }
    const Clip *c = clip(r);
    int px0 = LMAX(c->x0, (int)floorf(x0 - bl)), py0 = LMAX(c->y0, (int)floorf(y0 - bl)), px1 = LMIN(c->x1, (int)ceilf(x1 + bl)), py1 = LMIN(c->y1, (int)ceilf(y1 + bl));
    if (px0 >= px1) return;
    ShadowGeom g = { x0, y0, x1, y1, bl, bx0, by0, bx1, by1, {rad[0], rad[1], rad[2], rad[3]}, {brad[0], brad[1], brad[2], brad[3]}, it->color };
    /* Pixels a pixel clear of the corner radii and of the blur see one horizontal edge (or are fully
       covered), and pixels clear of the border box's corners one of its edges. Where both hold, or the
       box is not in the way, a row's coverage is the same on each side of either box's middle. */
    int sl = span_bound(ceilf(x0 + LMAX(LMAX(rad[0], rad[3]), bl) + 1), px0, px1), sr = span_bound(floorf(x1 - LMAX(LMAX(rad[1], rad[2]), bl) - 1), px0, px1);
    int il = span_bound(ceilf(bx0 + LMAX(brad[0], brad[3]) + 1), px0, px1), ir = span_bound(floorf(bx1 - LMAX(brad[1], brad[2]) - 1), px0, px1);
    int smid = first_center(px0, px1, (x0 + x1) / 2, false), bmid = first_center(px0, px1, (bx0 + bx1) / 2, false);
    int in0 = first_center(px0, px1, bx0 - 0.5f, true), in1 = first_center(px0, px1, bx1 + 0.5f, false);   /* [in0, in1): inside the box */
    bool flat = !c->round && !raster_pixel_rows;
    for (int y = py0; y < py1; y++) {
        float fy = y + 0.5f;
        int skip0 = px1, skip1 = px1;
        if (y >= by0 + bmax && y + 1 <= by1 - bmax) { skip0 = LMAX(px0, (int)ceilf(bx0 + bmax)); skip1 = LMIN(px1, (int)floorf(bx1 - bmax)); }
        if (skip1 <= skip0) skip0 = skip1 = px1;
        bool box_row = fy > by0 - 0.5f && fy < by1 + 0.5f;
        uint32_t *row = raster_row(r, y); int ox = r->origin_x;
        int cut[12] = { px0, px1, skip0, skip1, sl, sr, smid }, n = 7;
        if (box_row) { cut[n++] = in0; cut[n++] = in1; cut[n++] = il; cut[n++] = ir; cut[n++] = bmid; }
        for (int i = 1; i < n; i++) for (int j = i; j > 0 && cut[j - 1] > cut[j]; j--) { int t = cut[j]; cut[j] = cut[j - 1]; cut[j - 1] = t; }
        for (int k = 0; k + 1 < n; k++) {
            int a = cut[k], b = cut[k + 1];
            if (a >= b || (a >= skip0 && b <= skip1)) continue;
            if (flat && a >= sl && b <= sr && (!box_row || b <= in0 || a >= in1 || (a >= il && b <= ir))) {
                uint32_t p = shadow_px(r, &g, a, fy, box_row);
                if (p) over_span(row + a - ox, b - a, p);
                continue;
            }
            for (int x = a; x < b; x++) { uint32_t p = shadow_px(r, &g, x, fy, box_row); if (p) over(&row[x - ox], p); }
        }
    }
}

static Color grad_at(const Gradient *g, float t) {
    if (g->repeating && g->nstops > 1) { float span = g->stops[g->nstops - 1].pos - g->stops[0].pos; if (span > 0) { t = fmodf(t - g->stops[0].pos, span); if (t < 0) t += span; t += g->stops[0].pos; } }
    if (g->nstops == 0) return 0;
    if (t <= g->stops[0].pos) return g->stops[0].color;
    for (int i = 1; i < g->nstops; i++) {
        if (t <= g->stops[i].pos) {
            float a = g->stops[i - 1].pos, b = g->stops[i].pos, k = b > a ? (t - a) / (b - a) : 1;
            Color c0 = g->stops[i - 1].color, c1 = g->stops[i].color;
            #define LERP(F) (int)((float)F(c0) + ((float)F(c1) - (float)F(c0)) * k)
            return RGBA(LERP(COLOR_R), LERP(COLOR_G), LERP(COLOR_B), LERP(COLOR_A));
            #undef LERP
        }
    }
    return g->stops[g->nstops - 1].color;
}
/* Background repeats are sampled within the visible damage, never expanded to
 * a display item or loop per tile. Space gaps retain fractional pixel coverage. */
typedef struct { float q, cov; } TileCoord;
static double tile_covered(double x, double size, double step) {
    double n = floor(x / step), q = x - n * step;
    return n * size + fmin(size, fmax(0, q));
}
static TileCoord tile_coord(int pixel, float origin, float size, float step) {
    double x = (double)pixel - origin, q = x + .5;
    if (!(step > 0)) return (TileCoord){ (float)q, 1 };
    q = fmod(q, step); if (q < 0) q += step;
    float cov = 1;
    if (step > size) {
        cov = (float)fmin(1, fmax(0, tile_covered(x + 1, size, step) - tile_covered(x, size, step)));
        /* A pixel can straddle a tile while its center is in a gap. Sample the
         * nearest tile edge; coverage, rather than a false opaque gap, blends it. */
        if (q > size) q = q - size < step - q ? size : 0;
    }
    return (TileCoord){ (float)q, cov };
}
static void gradient(R *r, const DItem *it) {
    const Gradient *g = it->grad; float s = r->s;
    float x0 = it->x * s, y0 = it->y * s, x1 = (it->x + it->w) * s, y1 = (it->y + it->h) * s;
    bool tiled = it->tile.w > 0 && it->tile.h > 0;
    float w = tiled ? it->tile.w * s : x1 - x0, h = tiled ? it->tile.h * s : y1 - y0;
    float tx = x0 + it->tile.x * s, ty = y0 + it->tile.y * s;
    float stepx = it->tile.step_x * s, stepy = it->tile.step_y * s;
    if (!(w > 0) || !(h > 0) || !isfinite(w) || !isfinite(h)) return;
    float rad[4]; for (int i = 0; i < 4; i++) rad[i] = it->r[i] * s;
    bool round = rad[0] > 0 || rad[1] > 0 || rad[2] > 0 || rad[3] > 0;
    float clipx = tiled ? x0 + it->tile.clip_x * s : x0, clipy = tiled ? y0 + it->tile.clip_y * s : y0;
    float clipr = tiled ? clipx + it->tile.clip_w * s : x1, clipb = tiled ? clipy + it->tile.clip_h * s : y1;
    float a = g->angle * (float)M_PI / 180.f, dxv = sinf(a), dyv = -cosf(a);
    /* Cardinal directions stay exact, including the fast vertical row path. */
    float angle = fmodf(g->angle, 360.f); if (angle < 0) angle += 360.f;
    if (angle == 0) { dxv = 0; dyv = -1; }
    else if (angle == 180) { dxv = 0; dyv = 1; }
    else if (angle == 90) { dxv = 1; dyv = 0; }
    else if (angle == 270) { dxv = -1; dyv = 0; }
    float L = fabsf(w * dxv) + fabsf(h * dyv); if (L < 1e-3f) L = 1;
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, R2 = hypotf(w, h) / 2;
    if (!isfinite(L) || !isfinite(R2)) return;
    const Clip *c = clip(r);
    /* Clip floating coordinates before integer conversion; CSS dimensions can
     * exceed the integer range even though visible raster damage cannot. */
    int px0 = (int)floorf(LCLAMP(x0, (float)c->x0, (float)c->x1));
    int py0 = (int)floorf(LCLAMP(y0, (float)c->y0, (float)c->y1));
    int px1 = (int)ceilf(LCLAMP(x1, (float)c->x0, (float)c->x1));
    int py1 = (int)ceilf(LCLAMP(y1, (float)c->y0, (float)c->y1));
    if (px1 <= px0 || py1 <= py0) return;
    if (g->type != 1 && dxv == 0 && !round && !c->round && (!tiled || !(stepx > w))) {
        for (int y = py0; y < py1; y++) {
            TileCoord q = tiled ? tile_coord(y, ty, h, stepy) : (TileCoord){ y + .5f - y0, 1 };
            if (!(q.cov > 0)) continue;
            float t = ((q.q - h / 2) * dyv) / L + .5f;
            Color col = grad_at(g, t);
            if (q.cov < .999f) col = RGBA(COLOR_R(col), COLOR_G(col), COLOR_B(col), (int)(COLOR_A(col) * q.cov));
            fill(r, LMAX(x0, (float)c->x0), LMAX(y0, (float)y), LMIN(x1, (float)c->x1), LMIN(y1, (float)y + 1), NULL, col);
        }
        return;
    }
    TileCoord *xs = tiled ? xmalloc(sizeof *xs * (size_t)(px1 - px0)) : NULL;
    if (xs) for (int x = px0; x < px1; x++) xs[x - px0] = tile_coord(x, tx, w, stepx);
    for (int y = py0; y < py1; y++) {
        TileCoord qy = tiled ? tile_coord(y, ty, h, stepy) : (TileCoord){ y + .5f - y0, 1 };
        if (!(qy.cov > 0)) continue;
        for (int x = px0; x < px1; x++) {
            float fx = x + .5f, fy = y + .5f;
            TileCoord qx = xs ? xs[x - px0] : (TileCoord){ fx - x0, 1 };
            float gx = tiled ? qx.q - w / 2 : fx - cx, gy = tiled ? qy.q - h / 2 : fy - cy;
            float t = g->type == 1 ? sqrtf(gx * gx + gy * gy) / LMAX(1, R2) : (gx * dxv + gy * dyv) / L + .5f;
            float cov = span_cov(x0, x1, x) * span_cov(y0, y1, y);
            if (round) {
                float rounded_cov = LCLAMP(.5f - sd_rrect(fx, fy, clipx, clipy, clipr, clipb, rad), 0, 1);
                cov = tiled ? LMIN(cov, rounded_cov) : rounded_cov;
            }
            cov *= qx.cov * qy.cov * clip_cov(r, fx, fy);
            if (cov > 0) over(&raster_row(r, y)[x - r->origin_x], premul(grad_at(g, t), cov));
        }
    }
    free(xs);
}

/* Effective alpha per glyph coverage; retain straight RGB until source-over. */
static const uint8_t *text_lut(R *r, Color color) {
    if (r->lut_ok && r->lut_color == color) return r->lut;
    uint32_t ca = COLOR_A(color);
    for (uint32_t a = 0; a < 256; a++) r->lut[a] = (uint8_t)(a * ca / 255);
    r->lut_color = color; r->lut_ok = true; return r->lut;
}
/* a text item whose fragment box, padded by twice its font size for ink overflow, misses [x0,x1)x[y0,y1) device px */
static bool text_outside(const DItem *it, float s, float x0, float y0, float x1, float y1) {
    if (!(it->w > 0) || !(it->h > 0) || !it->ng) return false;   /* list markers carry no box */
    float em = 2 * (it->g[0].font ? it->g[0].font->size : 16);
    return (it->x - em) * s >= x1 || (it->x + it->w + em) * s <= x0 || (it->y - em) * s >= y1 || (it->y + it->h + em) * s <= y0;
}
static void text(R *r, const DItem *it) {
    float s = r->s; const Clip *c = clip(r);
    if (text_outside(it, s, c->x0, c->y0, c->x1, c->y1)) return;
    uint32_t ca = COLOR_A(it->color);
    const uint8_t *lut = NULL;
    for (int i = 0; i < it->ng; i++) {
        const DGlyph *dg = &it->g[i];
        if (!dg->font) continue;
        const Glyph *gl = font_glyph(dg->font, dg->gid, s);
        if (!gl || !gl->w) continue;
        int ox = (int)lroundf(dg->x * s) + gl->left, oy = (int)lroundf(dg->y * s) - gl->top;
        if (ox >= c->x1 || oy >= c->y1 || ox + gl->w <= c->x0 || oy + gl->h <= c->y0) continue;
        int xs = LMAX(0, c->x0 - ox), xe = LMIN(gl->w, c->x1 - ox);
        if (!c->round && !(gl->color && gl->rgba) && gl->alpha) {   /* the common case: integer coverage through a lookup */
            if (!lut) lut = text_lut(r, it->color);
            for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
                uint32_t *row = raster_row(r, oy + y) + ox - r->origin_x;
                const uint8_t *src = gl->alpha + y * gl->w;
                for (int x = xs; x < xe; x++) { uint32_t a = src[x]; if (a) over_color_coverage(&row[x], it->color, lut[a]); }
            }
            continue;
        }
        for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
            uint32_t *row = raster_row(r, oy + y);
            for (int x = xs; x < xe; x++) {
                float cc = c->round ? clip_cov(r, ox + x + 0.5f, oy + y + 0.5f) : 1;
                if (gl->color && gl->rgba) { uint32_t p = gl->rgba[y * gl->w + x]; if (p >> 24) over(&row[ox + x - r->origin_x], cc < 1 ? scale_px(p, (uint32_t)(cc * 256)) : p); }
                else if (gl->alpha) { uint32_t a = gl->alpha[y * gl->w + x]; if (a) { a = a * ca / 255; a = (uint32_t)(a * cc + 0.5f); over_color_coverage(&row[ox + x - r->origin_x], it->color, a); } }
            }
        }
    }
}

typedef struct { int a, b; uint32_t w; float cov, gap; } ImgCol;
static void image_horizontal(uint32_t *dst, const uint32_t *src, const ImgCol *cols, int count) {
    for (int x = 0; x < count; x++) {
        const ImgCol *k = &cols[x];
        dst[x] = scale_px(src[k->a], 256 - k->w) + scale_px(src[k->b], k->w);
    }
}
/* Interior bilinear pixels have full coverage and contiguous cached sources.
   Keep this loop free of geometry/alpha branches so SIMD can process a span.
   The packed-channel truncation and source-over factors match the edge path. */
static void image_vertical(uint32_t *restrict dst, const uint32_t *restrict top,
                           const uint32_t *restrict bottom, int count, uint32_t weight) {
    int x = 0;
#ifdef IMAGE_NEON
    /* Deinterleave eight premultiplied pixels: each lane is one channel,
       avoiding repeated packed-channel masks. Truncate each interpolation
       product separately, exactly as scale_px does. A 16-bit blend factor
       also represents 256, preserving the destination for transparent pixels. */
    uint8x8_t wa = vdup_n_u8((uint8_t)(256 - weight)), wb = vdup_n_u8((uint8_t)weight);
    for (; x + 8 <= count; x += 8) {
        uint8x8x4_t p;
        if (!weight || weight == 256) p = vld4_u8((const uint8_t *)(weight ? bottom + x : top + x));
        else {
            uint8x8x4_t a = vld4_u8((const uint8_t *)(top + x));
            uint8x8x4_t b = vld4_u8((const uint8_t *)(bottom + x));
            for (int c = 0; c < 4; c++)
                p.val[c] = vadd_u8(vshrn_n_u16(vmull_u8(a.val[c], wa), 8),
                                   vshrn_n_u16(vmull_u8(b.val[c], wb), 8));
        }
        uint16x8_t alpha = vmovl_u8(p.val[3]);
        uint16x8_t k = vsubq_u16(vsubq_u16(vdupq_n_u16(256), alpha), vshrq_n_u16(alpha, 7));
        uint8x8x4_t d = vld4_u8((const uint8_t *)(dst + x));
        for (int c = 0; c < 4; c++)
            d.val[c] = vadd_u8(p.val[c], vshrn_n_u16(vmulq_u16(vmovl_u8(d.val[c]), k), 8));
        vst4_u8((uint8_t *)(dst + x), d);
    }
#endif
    for (; x < count; x++) {
        uint32_t p = scale_px(top[x], 256 - weight) + scale_px(bottom[x], weight);
        uint32_t alpha = p >> 24;
        dst[x] = p + scale_px(dst[x], 256 - alpha - (alpha >> 7));
    }
}
static void image(R *r, const DItem *it) {
    const Image *im = it->img; float s = r->s;
    bool tiled = it->tile.w > 0 && it->tile.h > 0;
    float x0 = it->x * s, y0 = it->y * s, x1 = (it->x + it->w) * s, y1 = (it->y + it->h) * s;
    if (tiled ? (!(x1 > x0) || !(y1 > y0)) : (x1 - x0 < .5f || y1 - y0 < .5f)) return;
    const Clip *c = clip(r);
    int more = -1;
    for (int i = 0; i < 3 && r->c->punch_more[i]; i++) if ((const void *)im == r->c->punch_more[i]) more = i;
    if (!tiled && ((r->c->punch && (const void *)im == r->c->punch) || more >= 0)) {
        int qx0 = LMAX(c->x0, (int)floorf(x0 + 0.5f)), qy0 = LMAX(c->y0, (int)floorf(y0 + 0.5f)), qx1 = LMIN(c->x1, (int)floorf(x1 + 0.5f)), qy1 = LMIN(c->y1, (int)floorf(y1 + 0.5f));
        for (int y = qy0; qx1 > qx0 && y < qy1; y++) memset(raster_row(r, y) + qx0 - r->origin_x, 0, (size_t)(qx1 - qx0) * 4);
        bool ok = !r->nlayer && !c->round;
        if (r->quiet) return;
        if (more < 0) r->c->punched = ok;
        else if (ok) r->c->punched_more |= 1u << more;
        else r->c->punched_more &= ~(1u << more);
        return;
    }
    if (!im->px) { if (!im->yuv && !im->gpu) return; image_yuv_materialize((Image *)im); if (!im->px) return; }
    /* Clip floating coordinates before integer conversion; CSS dimensions can
     * exceed the integer range even though visible raster damage cannot. */
    int px0 = (int)floorf(LCLAMP(x0, (float)c->x0, (float)c->x1));
    int py0 = (int)floorf(LCLAMP(y0, (float)c->y0, (float)c->y1));
    int px1 = (int)ceilf(LCLAMP(x1, (float)c->x0, (float)c->x1));
    int py1 = (int)ceilf(LCLAMP(y1, (float)c->y0, (float)c->y1));
    if (px1 <= px0 || py1 <= py0) return;
    float tw = tiled ? it->tile.w * s : x1 - x0, th = tiled ? it->tile.h * s : y1 - y0;
    float tx = x0 + it->tile.x * s, ty = y0 + it->tile.y * s;
    float stepx = it->tile.step_x * s, stepy = it->tile.step_y * s;
    if (!(tw > 0) || !(th > 0) || !isfinite(tw) || !isfinite(th)) return;
    float kx = im->w / (x1 - x0), ky = im->h / (y1 - y0);
    double tile_kx = tiled ? im->w / (double)tw : kx, tile_ky = tiled ? im->h / (double)th : ky;
    bool down = tile_kx > 1.5 || tile_ky > 1.5, rclip = c->round;
    int inner0 = span_bound(ceilf(x0), px0, px1), inner1 = span_bound(floorf(x1), px0, px1);
    float rad[4]; bool background_round = false;
    for (int i = 0; i < 4; i++) { rad[i] = it->r[i] * s; if (tiled && rad[i] > 0) background_round = true; }
    float clipx = x0 + it->tile.clip_x * s, clipy = y0 + it->tile.clip_y * s;
    float clipr = clipx + it->tile.clip_w * s, clipb = clipy + it->tile.clip_h * s;
    int width = px1 - px0;
    ImgCol *cols = xmalloc((sizeof *cols + (down ? 0 : 2 * sizeof(uint32_t))) * (size_t)width);
    /* Upscaled thumbnails reuse each source row several times. Cache just the
       two horizontally interpolated rows needed by the vertical filter; keep
       the original integer rounding at both stages. Storage is O(damage width),
       never a second full-size scaled image. */
    uint32_t *horizontal[2] = {(uint32_t *)(cols + width), (uint32_t *)(cols + width)};
    if (!down) horizontal[1] += width;
    int cached_y[2] = {-1, -1};
    for (int x = px0; x < px1; x++) {
        ImgCol *k = &cols[x - px0];
        TileCoord q = tiled ? tile_coord(x, tx, tw, stepx) : (TileCoord){ x + .5f - x0, 1 };
        float sx = tiled ? (float)fmin(im->w - .5, fmax(-.5, q.q * tile_kx - .5)) : q.q * kx - .5f;
        if (down) { k->a = k->b = LCLAMP((int)(sx + 0.5f), 0, im->w - 1); k->w = 0; }
        else { int ix = (int)floorf(sx); k->a = LCLAMP(ix, 0, im->w - 1); k->b = LCLAMP(ix + 1, 0, im->w - 1); k->w = (uint32_t)((sx - ix) * 256); }
        k->cov = span_cov(x0, x1, x); k->gap = q.cov;
    }
    for (int y = py0; y < py1; y++) {
        TileCoord q = tiled ? tile_coord(y, ty, th, stepy) : (TileCoord){ y + .5f - y0, 1 };
        if (!(q.cov > 0)) continue;
        float sy = tiled ? (float)fmin(im->h - .5, fmax(-.5, q.q * tile_ky - .5)) : q.q * ky - .5f; int iy = (int)floorf(sy);
        uint32_t wy = (uint32_t)((sy - iy) * 256);
        const uint32_t *ra, *rb;
        if (down) ra = rb = im->px + (size_t)LCLAMP((int)(sy + 0.5f), 0, im->h - 1) * (size_t)im->w;
        else {
            int ya = LCLAMP(iy, 0, im->h - 1), yb = LCLAMP(iy + 1, 0, im->h - 1);
            if (ya != cached_y[0] && ya == cached_y[1]) {
                uint32_t *swap = horizontal[0]; horizontal[0] = horizontal[1]; horizontal[1] = swap;
                int old = cached_y[0]; cached_y[0] = cached_y[1]; cached_y[1] = old;
            }
            if (ya != cached_y[0]) {
                image_horizontal(horizontal[0], im->px + (size_t)ya * (size_t)im->w, cols, width);
                cached_y[0] = ya;
            }
            ra = horizontal[0]; rb = ra;
            if (yb != ya) {
                if (yb != cached_y[1]) {
                    image_horizontal(horizontal[1], im->px + (size_t)yb * (size_t)im->w, cols, width);
                    cached_y[1] = yb;
                }
                rb = horizontal[1];
            }
        }
        float cy = span_cov(y0, y1, y);
        uint32_t *row = raster_row(r, y);
        bool span = !down && !rclip && !background_round && cy >= .999f && q.cov >= .999f && inner1 > inner0 && (!tiled || !(stepx > tw));
        if (span) image_vertical(row + inner0 - r->origin_x, ra + inner0 - px0,
                                 rb + inner0 - px0, inner1 - inner0, wy);
        for (int x = px0; x < px1; x++) {
            if (span && x == inner0) { x = inner1; if (x >= px1) break; }
            const ImgCol *k = &cols[x - px0];
            uint32_t p;
            if (down) p = ra[k->a];
            else p = scale_px(ra[x - px0], 256 - wy) + scale_px(rb[x - px0], wy);
            float cov = k->cov * cy;
            if (background_round) cov = LMIN(cov, LCLAMP(.5f - sd_rrect(x + .5f, y + .5f, clipx, clipy, clipr, clipb, rad), 0, 1));
            cov *= k->gap * q.cov;
            if (rclip) cov *= clip_cov(r, x + .5f, y + .5f);
            if (cov < 0.999f) p = scale_px(p, (uint32_t)(cov * 256));
            over(&row[x - r->origin_x], p);
        }
    }
    free(cols);
}

/* Bound the actual drawing commands, not the element box: descendants may
   overflow it, shadows extend beyond it, and glyph ink has its own bearings.
   Ignoring interior clips is conservative and avoids duplicating clip-stack
   semantics. Damage bounds are applied before converting coordinates to int. */
static void layer_include(LayerRect *bounds, int *stack, int depth,
                          LayerRect damage, float x0, float y0, float x1, float y1) {
    LayerRect b = {
        (int)fminf(damage.x1, fmaxf(damage.x0, floorf(x0))),
        (int)fminf(damage.y1, fmaxf(damage.y0, floorf(y0))),
        (int)fminf(damage.x1, fmaxf(damage.x0, ceilf(x1))),
        (int)fminf(damage.y1, fmaxf(damage.y0, ceilf(y1)))
    };
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return;
    for (int k = 0; k < LMIN(depth, 16); k++) {
        LayerRect *a = &bounds[stack[k]];
        if (a->x1 <= a->x0 || a->y1 <= a->y0) *a = b;
        else { a->x0 = LMIN(a->x0, b.x0); a->y0 = LMIN(a->y0, b.y0);
               a->x1 = LMAX(a->x1, b.x1); a->y1 = LMAX(a->y1, b.y1); }
    }
}
/* Device-pixel bounds of what each item can draw, built once per raster and shared by its bands, so a
   band replays only the items reaching its rows instead of walking the whole list twice. A clip whose
   rect misses the band skips to its pop (skip), unless a video punch inside must still be seen or
   layers do not nest within it. */
typedef struct { int x0, y0, x1, y1, skip; } CullBox;
static inline bool cull_miss(const CullBox *cb, int i, LayerRect d) {
    return cb && (cb[i].x1 <= d.x0 || cb[i].x0 >= d.x1 || cb[i].y1 <= d.y0 || cb[i].y0 >= d.y1);
}
static bool punch_image(const Canvas *cv, const Image *im) {
    return im && (im == cv->punch || im == cv->punch_more[0] || im == cv->punch_more[1] || im == cv->punch_more[2]);
}
static CullBox cull_rect(float x0, float y0, float x1, float y1) {
    const float big = 1e9f;
    if (!(x0 <= x1) || !(y0 <= y1)) return (CullBox){ -1000000000, -1000000000, 1000000000, 1000000000, -1 };
    return (CullBox){ (int)fmaxf(-big, floorf(x0) - 2), (int)fmaxf(-big, floorf(y0) - 2), (int)fminf(big, ceilf(x1) + 2), (int)fminf(big, ceilf(y1) + 2), -1 };
}
static CullBox *cull_build(const Canvas *cv, const DisplayList *dl, float s) {
    if (raster_pixel_rows || dl->items.n < 64) return NULL;
    CullBox *cb = xmalloc(sizeof *cb * (size_t)dl->items.n);
    enum { MAXOPEN = 256 };
    int open[MAXOPEN], open_layers[MAXOPEN]; bool bad[MAXOPEN]; int nopen = 0, layers = 0;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        float x0 = it->x * s, y0 = it->y * s, x1 = (it->x + it->w) * s, y1 = (it->y + it->h) * s;
        cb[i] = cull_rect(1, 1, 0, 0);
        switch (it->op) {
        case DO_RECT: case DO_LINE: case DO_BORDER: case DO_GRADIENT: cb[i] = cull_rect(x0, y0, x1, y1); break;
        case DO_IMAGE:
            if (!(it->tile.w > 0) && punch_image(cv, it->img)) { for (int k = 0; k < nopen; k++) bad[k] = true; break; }
            cb[i] = cull_rect(x0, y0, x1, y1); break;
        case DO_SHADOW: { float pad = fabsf(it->spread * s) + LMAX(.5f, it->blur * s); cb[i] = cull_rect(x0 - pad, y0 - pad, x1 + pad, y1 + pad); break; }
        case DO_TEXT:
            if (it->w > 0 && it->h > 0 && it->ng) {   /* as text_outside: glyph ink stays within 2em of the box */
                float em = 2 * (it->g[0].font ? it->g[0].font->size : 16) * s;
                cb[i] = cull_rect(x0 - em, y0 - em, x1 + em, y1 + em);
            }
            break;
        case DO_PUSH_CLIP:
            cb[i] = cull_rect(x0, y0, x1, y1);
            if (nopen < MAXOPEN) { open[nopen] = i; open_layers[nopen] = layers; bad[nopen] = false; }
            nopen++; break;
        case DO_POP_CLIP:
            if (!nopen) break;
            nopen--;
            if (nopen < MAXOPEN && !bad[nopen] && open_layers[nopen] == layers) cb[open[nopen]].skip = i;
            break;
        case DO_PUSH_LAYER: layers++; break;
        case DO_POP_LAYER:
            layers--;
            for (int k = 0; k < LMIN(nopen, MAXOPEN); k++) if (open_layers[k] > layers) bad[k] = true;
            break;
        }
    }
    return cb;
}
static LayerRect *layer_bounds(const DisplayList *dl, const CullBox *cb, float scale, LayerRect damage) {
    LayerRect *bounds = NULL;
    int stack[16], depth = 0;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP && cb && cb[i].skip >= 0 && cull_miss(cb, i, damage)) { i = cb[i].skip; continue; }
        if (it->op <= DO_LINE && cull_miss(cb, i, damage)) continue;
        if (it->op == DO_PUSH_LAYER) {
            if (!bounds) bounds = xmalloc(sizeof *bounds * (size_t)dl->items.n);
            if (depth < 16) { stack[depth] = i; bounds[i] = (LayerRect){0}; }
            depth++;
        } else if (it->op == DO_POP_LAYER) { if (depth) depth--; }
        else if (depth && it->op == DO_TEXT) {
            if (text_outside(it, scale, damage.x0, damage.y0, damage.x1, damage.y1)) continue;
            for (int j = 0; j < it->ng; j++) {
                const DGlyph *dg = &it->g[j];
                if (!dg->font) continue;
                const Glyph *gl = font_glyph(dg->font, dg->gid, scale);
                if (!gl || !gl->w || !gl->h) continue;
                int x = (int)lroundf(dg->x * scale) + gl->left;
                int y = (int)lroundf(dg->y * scale) - gl->top;
                layer_include(bounds, stack, depth, damage, x, y, x + gl->w, y + gl->h);
            }
        } else if (depth && it->op <= DO_LINE) {
            float pad = it->op == DO_SHADOW ? fabsf(it->spread * scale) + LMAX(.5f, it->blur * scale) : 0;
            layer_include(bounds, stack, depth, damage, it->x * scale - pad, it->y * scale - pad,
                          (it->x + it->w) * scale + pad, (it->y + it->h) * scale + pad);
        }
    }
    return bounds;
}

unsigned long long g_raster_px, g_layer_px;
unsigned g_raster_calls, g_layer_n;
static void raster_band(Canvas *cv, const DisplayList *dl, const CullBox *cb, Color clearc, int rx0, int ry0, int rx1, int ry1, bool quiet) {
    R r; memset(&r, 0, sizeof r); r.c = cv; r.s = cv->scale; r.px = cv->px; r.stride = cv->stride; r.height = cv->h; r.quiet = quiet;
    uint32_t cl = premul(clearc, 1);
    /* An opaque canvas background covering the band stores every pixel anyway: no clear first. */
    const DItem *bg = dl->items.n ? &dl->items.v[0] : NULL;
    bool covered = bg && bg->op == DO_RECT && COLOR_A(bg->color) == 255 && !bg->r[0] && !bg->r[1] && !bg->r[2] && !bg->r[3] &&
        bg->x * r.s <= rx0 && bg->y * r.s <= ry0 && (bg->x + bg->w) * r.s >= rx1 && (bg->y + bg->h) * r.s >= ry1;
    for (int y = ry0; !covered && y < ry1; y++) store_span(canvas_row(cv, y) + rx0, rx1 - rx0, cl);
    r.clips[0] = (Clip){ rx0, ry0, rx1, ry1, 0, 0, (float)cv->w, (float)cv->h, {0}, false }; r.nclip = 1;
    float s = r.s;
    LayerRect band = {rx0, ry0, rx1, ry1};
    LayerRect *bounds = layer_bounds(dl, cb, s, band);
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP && cb && cb[i].skip >= 0 && cull_miss(cb, i, band)) { i = cb[i].skip; continue; }
        if (it->op <= DO_LINE && cull_miss(cb, i, band)) continue;
        switch (it->op) {
        case DO_RECT: case DO_LINE: { float rad[4]; for (int k = 0; k < 4; k++) rad[k] = it->r[k] * s; fill(&r, it->x * s, it->y * s, (it->x + it->w) * s, (it->y + it->h) * s, rad, it->color); break; }
        case DO_BORDER: border(&r, it); break;
        case DO_SHADOW: shadow(&r, it); break;
        case DO_GRADIENT: gradient(&r, it); break;
        case DO_TEXT: text(&r, it); break;
        case DO_IMAGE: image(&r, it); break;
        case DO_PUSH_CLIP: {
            if (r.nclip >= 64) break;
            const Clip *p = clip(&r); Clip c;
            c.x0 = LMAX(p->x0, (int)floorf(it->x * s)); c.y0 = LMAX(p->y0, (int)floorf(it->y * s));
            c.x1 = LMIN(p->x1, (int)ceilf((it->x + it->w) * s)); c.y1 = LMIN(p->y1, (int)ceilf((it->y + it->h) * s));
            if (c.x1 < c.x0) c.x1 = c.x0; if (c.y1 < c.y0) c.y1 = c.y0;
            c.fx = it->x * s; c.fy = it->y * s; c.fw = it->w * s; c.fh = it->h * s;
            c.round = false; for (int k = 0; k < 4; k++) { c.r[k] = it->r[k] * s; if (c.r[k] > 0) c.round = true; }
            if (!c.round && p->round) { c.round = true; c.fx = p->fx; c.fy = p->fy; c.fw = p->fw; c.fh = p->fh; memcpy(c.r, p->r, sizeof c.r); }
            r.clips[r.nclip++] = c; break;
        }
        case DO_POP_CLIP: if (r.nclip > 1) r.nclip--; break;
        case DO_PUSH_LAYER: {
            if (r.nlayer >= 16) { r.nlayer++; break; }
            r.layers[r.nlayer] = (LayerSave){r.px, it->alpha, r.origin_x, r.origin_y, r.stride, r.height};
            r.nlayer++;
            LayerRect b = bounds[i];
            r.stride = b.x1 - b.x0; r.height = b.y1 - b.y0;
            r.px = xcalloc((size_t)r.stride * (size_t)(b.y1 - b.y0), 4);
            __atomic_fetch_add(&g_layer_n, 1, __ATOMIC_RELAXED); __atomic_fetch_add(&g_layer_px, (unsigned long long)LMAX(r.stride, 0) * (unsigned long long)LMAX(r.height, 0), __ATOMIC_RELAXED);
            r.origin_x = b.x0; r.origin_y = b.y0;
            break;
        }
        case DO_POP_LAYER: {
            if (!r.nlayer) break;
            r.nlayer--; if (r.nlayer >= 16) break;
            LayerSave saved = r.layers[r.nlayer];
            uint32_t *src = r.px, k = (uint32_t)(saved.alpha * 256);
            LayerRect b = {r.origin_x, r.origin_y, r.origin_x + r.stride, r.origin_y + r.height};
            for (int y = b.y0; y < b.y1; y++) {
                const uint32_t *sr = src + (size_t)(y - r.origin_y) * (size_t)r.stride;
                uint32_t *dr = !r.nlayer ? canvas_row(cv, y) + b.x0
                                         : saved.px + (size_t)(y - saved.origin_y) * (size_t)saved.stride + (size_t)(b.x0 - saved.origin_x);
                int w = b.x1 - b.x0;
                if (k >= 256) { for (int x = 0; x < w; x++) if (sr[x]) over(&dr[x], sr[x]); }
                else for (int x = 0; x < w; x++) if (sr[x]) over(&dr[x], scale_px(sr[x], k));
            }
            free(src); r.px = saved.px; r.origin_x = saved.origin_x;
            r.origin_y = saved.origin_y; r.stride = saved.stride; r.height = saved.height; break;
        }
        }
    }
    while (r.nlayer > 0) { r.nlayer--; if (r.nlayer < 16) { free(r.px); r.px = r.layers[r.nlayer].px; r.origin_y = r.layers[r.nlayer].origin_y; } }
    free(bounds);
}

/* Large rasters are cut into horizontal bands painted on all cores. Each band replays the whole
   display list clipped to its rows; every pixel operation is per pixel, so the result is identical
   to one pass. The bands outnumber the threads so big and little cores finish together. */
static struct {
    pthread_mutex_t run, mu; pthread_cond_t go, done;
    int threads, active; unsigned gen;
    uint64_t ticket;   /* generation << 32 | next band: a thread late from one job cannot claim a band of the next */
    Canvas *cv; const DisplayList *dl; const CullBox *cull; Color clear; int x0, y0, x1, y1, nband, left;
} pool = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

static void bands_work(unsigned gen) {
    for (;;) {
        uint64_t t = __atomic_load_n(&pool.ticket, __ATOMIC_ACQUIRE);
        if ((unsigned)(t >> 32) != gen || (int)(uint32_t)t >= pool.nband) return;
        if (!__atomic_compare_exchange_n(&pool.ticket, &t, t + 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) continue;
        int b = (int)(uint32_t)t, h = pool.y1 - pool.y0;
        raster_band(pool.cv, pool.dl, pool.cull, pool.clear, pool.x0, pool.y0 + (int)((int64_t)h * b / pool.nband),
                    pool.x1, pool.y0 + (int)((int64_t)h * (b + 1) / pool.nband), b > 0);
        pthread_mutex_lock(&pool.mu);
        if (!--pool.left) pthread_cond_broadcast(&pool.done);
        pthread_mutex_unlock(&pool.mu);
    }
}
static void *band_thread(void *arg) {
    (void)arg; unsigned seen = 0;
    for (;;) {
        pthread_mutex_lock(&pool.mu);
        while (pool.gen == seen) pthread_cond_wait(&pool.go, &pool.mu);
        seen = pool.gen;
        pthread_mutex_unlock(&pool.mu);
        bands_work(seen);
    }
    return NULL;
}
void raster_set_threads(int n) {
    pthread_mutex_lock(&pool.run);
    for (n = LCLAMP(n, 0, 15); pool.threads < n; pool.threads++) {
        pthread_t t; if (pthread_create(&t, NULL, band_thread, NULL)) break;
        pthread_detach(t);
    }
    pool.active = LMIN(n, pool.threads);   /* fewer: the rest stay parked */
    pthread_mutex_unlock(&pool.run);
}

void raster(Canvas *cv, const DisplayList *dl, Color clearc) { raster_rect(cv, dl, clearc, 0, 0, cv->w, cv->h); }
void raster_rect(Canvas *cv, const DisplayList *dl, Color clearc, int rx0, int ry0, int rx1, int ry1) {
    rx0 = LMAX(rx0, 0); ry0 = LMAX(ry0, 0); rx1 = LMIN(rx1, cv->w); ry1 = LMIN(ry1, cv->h);
    if (rx1 <= rx0 || ry1 <= ry0) return;
    g_raster_calls++; g_raster_px += (unsigned long long)(rx1 - rx0) * (unsigned long long)(ry1 - ry0);
    int nband = LMIN(3 * (pool.active + 1), (ry1 - ry0) / 24);
    /* small damage (a caret, a hover) costs less than waking the pool; a raster running on another
       thread, or a nested one, paints alone */
    if (!pool.active || (int64_t)(rx1 - rx0) * (ry1 - ry0) < 160000 || nband < 2 || pthread_mutex_trylock(&pool.run)) {
        CullBox *cb = cull_build(cv, dl, cv->scale);
        raster_band(cv, dl, cb, clearc, rx0, ry0, rx1, ry1, false); free(cb); return;
    }
    /* video frames convert to RGB once, before the bands read them; GPU frames are read back */
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i]; Image *im = it->img;
        if (it->op != DO_IMAGE || !im || im->px || (!im->yuv && !im->gpu)) continue;
        if (!(it->tile.w > 0) && punch_image(cv, im)) continue;
        if (it->w > 0 && it->h > 0) image_yuv_materialize(im);
    }
    pthread_mutex_lock(&pool.mu);
    CullBox *cb = cull_build(cv, dl, cv->scale);
    pool.cv = cv; pool.dl = dl; pool.cull = cb; pool.clear = clearc; pool.x0 = rx0; pool.y0 = ry0; pool.x1 = rx1; pool.y1 = ry1;
    pool.nband = nband; pool.left = nband; unsigned gen = ++pool.gen;
    __atomic_store_n(&pool.ticket, (uint64_t)gen << 32, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&pool.go);
    pthread_mutex_unlock(&pool.mu);
    bands_work(gen);
    pthread_mutex_lock(&pool.mu);
    while (pool.left) pthread_cond_wait(&pool.done, &pool.mu);
    pthread_mutex_unlock(&pool.mu);
    pthread_mutex_unlock(&pool.run);
    free(cb);
}

/* ---------------- PNG writer (for tests/screenshots) ---------------- */
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
    uint8_t h[8]; be32(h, n); memcpy(h + 4, type, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uLong crc = crc32(0, (const Bytef *)type, 4); if (n) crc = crc32(crc, data, n);
    uint8_t c[4]; be32(c, (uint32_t)crc); fwrite(c, 1, 4, f);
}
bool png_write(const char *path, const uint32_t *px, int w, int h, int stride) {
    size_t rawn = (size_t)h * ((size_t)w * 4 + 1);
    uint8_t *raw = xmalloc(rawn), *o = raw;
    for (int y = 0; y < h; y++) {
        *o++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t p = px[(size_t)y * (size_t)stride + (size_t)x], a = p >> 24;
            uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
            *o++ = (uint8_t)LMIN(r, 255); *o++ = (uint8_t)LMIN(g, 255); *o++ = (uint8_t)LMIN(b, 255); *o++ = (uint8_t)a;
        }
    }
    uLongf zn = compressBound((uLong)rawn); uint8_t *z = xmalloc(zn);
    compress2(z, &zn, raw, (uLong)rawn, 6);
    FILE *f = fopen(path, "wb"); if (!f) { free(raw); free(z); return false; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13]; be32(ih, (uint32_t)w); be32(ih + 4, (uint32_t)h); ih[8] = 8; ih[9] = 6; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, (uint32_t)zn); chunk(f, "IEND", NULL, 0);
    fclose(f); free(raw); free(z); return true;
}
