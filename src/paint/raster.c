/* CPU rasterizer: premultiplied ARGB32, anti-aliased shapes, glyphs, images */
#include "paint.h"
#include <math.h>
#include <zlib.h>

typedef struct { int x0, y0, x1, y1; float fx, fy, fw, fh, r[4]; bool round; } Clip;
typedef struct { int x0, y0, x1, y1; } LayerRect;
typedef struct { uint32_t *px; float alpha; int origin_x, origin_y, stride, height; } LayerSave;
typedef struct {
    Canvas *c; float s;
    Clip clips[64]; int nclip;
    LayerSave layers[16]; int nlayer;
    uint32_t *px;
    int origin_x, origin_y, stride, height; /* tightly bounded storage for opacity groups */
} R;

static inline uint32_t *raster_row(R *r, int y) {
    return r->px + (size_t)(y - r->origin_y) * (size_t)r->stride;
}

void canvas_init(Canvas *c, int w, int h, float scale) { c->w = w; c->h = h; c->stride = w; c->scale = scale; c->punch = NULL; c->punched = false; c->px = xcalloc((size_t)w * (size_t)h, 4); }
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

/* fill rect or rounded rect (device coords) */
static void fill(R *r, float x0, float y0, float x1, float y1, const float rad[4], Color col) {
    if (!COLOR_A(col) || x1 <= x0 || y1 <= y0) return;
    const Clip *c = clip(r);
    int ix0 = LMAX(c->x0, (int)floorf(x0)), iy0 = LMAX(c->y0, (int)floorf(y0)), ix1 = LMIN(c->x1, (int)ceilf(x1)), iy1 = LMIN(c->y1, (int)ceilf(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    bool round = rad && (rad[0] > 0 || rad[1] > 0 || rad[2] > 0 || rad[3] > 0);
    uint32_t solid = premul(col, 1);
    if (!round && !c->round) {
        int ax = LMAX(ix0, (int)ceilf(x0)), bx = LMIN(ix1, (int)floorf(x1)); if (bx < ax) bx = ax;
        for (int y = iy0; y < iy1; y++) {
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
        float rmax = round ? LMAX(LMAX(rad[0], rad[1]), LMAX(rad[2], rad[3])) : 0;
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
        for (int y = py0; y < py1; y++) for (int x = px0; x < px1; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            if (fx > ix0 + LMAX(ir[0], ir[3]) + 1 && fx < ix1 - LMAX(ir[1], ir[2]) - 1 && fy > iy0 + 1 && fy < iy1 - 1) { x = LMAX(x, (int)(ix1 - LMAX(ir[1], ir[2]) - 1) - 1); continue; }
            float co = LCLAMP(0.5f - sd_rrect(fx, fy, x0, y0, x1, y1, rad), 0, 1);
            float ci = ix1 > ix0 && iy1 > iy0 ? LCLAMP(0.5f - sd_rrect(fx, fy, ix0, iy0, ix1, iy1, ir), 0, 1) : 0;
            float cov = (co - ci) * clip_cov(r, fx, fy);
            if (cov > 0) over(&raster_row(r, y)[x - r->origin_x], premul(col, cov));
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

static void shadow(R *r, const DItem *it) {
    float s = r->s, sp = it->spread * s, bl = LMAX(0.5f, it->blur * s);
    float x0 = it->x * s - sp, y0 = it->y * s - sp, x1 = (it->x + it->w) * s + sp, y1 = (it->y + it->h) * s + sp;
    float rad[4]; for (int i = 0; i < 4; i++) rad[i] = LMAX(0, it->r[i] * s + sp);
    const Clip *c = clip(r);
    int px0 = LMAX(c->x0, (int)floorf(x0 - bl)), py0 = LMAX(c->y0, (int)floorf(y0 - bl)), px1 = LMIN(c->x1, (int)ceilf(x1 + bl)), py1 = LMIN(c->y1, (int)ceilf(y1 + bl));
    for (int y = py0; y < py1; y++) for (int x = px0; x < px1; x++) {
        float d = sd_rrect(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad);
        float t = LCLAMP((d + bl) / (2 * bl), 0, 1);
        float cov = 1 - t * t * (3 - 2 * t);
        cov *= clip_cov(r, x + 0.5f, y + 0.5f);
        if (cov > 0.002f) over(&raster_row(r, y)[x - r->origin_x], premul(it->color, cov));
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
static void gradient(R *r, const DItem *it) {
    const Gradient *g = it->grad; float s = r->s;
    float x0 = it->x * s, y0 = it->y * s, x1 = (it->x + it->w) * s, y1 = (it->y + it->h) * s, w = x1 - x0, h = y1 - y0;
    float rad[4]; for (int i = 0; i < 4; i++) rad[i] = it->r[i] * s;
    bool round = rad[0] > 0 || rad[1] > 0 || rad[2] > 0 || rad[3] > 0;
    float a = g->angle * (float)M_PI / 180.f, dxv = sinf(a), dyv = -cosf(a);
    /* Cardinal directions are exact. sinf(pi) otherwise leaves a small x
       component and can change a vertical gradient by a color step across
       a wide row. Exact vertical gradients also need just one color per row. */
    float angle = fmodf(g->angle, 360.f); if (angle < 0) angle += 360.f;
    if (angle == 0) { dxv = 0; dyv = -1; }
    else if (angle == 180) { dxv = 0; dyv = 1; }
    else if (angle == 90) { dxv = 1; dyv = 0; }
    else if (angle == 270) { dxv = -1; dyv = 0; }
    float L = fabsf(w * dxv) + fabsf(h * dyv); if (L < 1e-3f) L = 1;
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, R2 = sqrtf(w * w + h * h) / 2;
    const Clip *c = clip(r);
    int px0 = LMAX(c->x0, (int)floorf(x0)), py0 = LMAX(c->y0, (int)floorf(y0)), px1 = LMIN(c->x1, (int)ceilf(x1)), py1 = LMIN(c->y1, (int)ceilf(y1));
    if (g->type != 1 && dxv == 0 && !round && !c->round) {
        for (int y = py0; y < py1; y++) {
            float t = ((y + .5f - cy) * dyv) / L + .5f;
            fill(r, x0, LMAX(y0, (float)y), x1, LMIN(y1, (float)y + 1), NULL, grad_at(g, t));
        }
        return;
    }
    for (int y = py0; y < py1; y++) for (int x = px0; x < px1; x++) {
        float fx = x + 0.5f, fy = y + 0.5f;
        float t = g->type == 1 ? sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) / LMAX(1, R2) : ((fx - cx) * dxv + (fy - cy) * dyv) / L + 0.5f;
        float cov = span_cov(x0, x1, x) * span_cov(y0, y1, y);
        if (round) cov = LCLAMP(0.5f - sd_rrect(fx, fy, x0, y0, x1, y1, rad), 0, 1);
        cov *= clip_cov(r, fx, fy);
        if (cov > 0) over(&raster_row(r, y)[x - r->origin_x], premul(grad_at(g, t), cov));
    }
}

static void text(R *r, const DItem *it) {
    float s = r->s; const Clip *c = clip(r);
    uint32_t ca = COLOR_A(it->color);
    for (int i = 0; i < it->ng; i++) {
        const DGlyph *dg = &it->g[i];
        if (!dg->font) continue;
        const Glyph *gl = font_glyph(dg->font, dg->gid, s);
        if (!gl || !gl->w) continue;
        int ox = (int)lroundf(dg->x * s) + gl->left, oy = (int)lroundf(dg->y * s) - gl->top;
        if (ox >= c->x1 || oy >= c->y1 || ox + gl->w <= c->x0 || oy + gl->h <= c->y0) continue;
        for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
            uint32_t *row = raster_row(r, oy + y);
            for (int x = LMAX(0, c->x0 - ox); x < gl->w && ox + x < c->x1; x++) {
                float cc = clip(r)->round ? clip_cov(r, ox + x + 0.5f, oy + y + 0.5f) : 1;
                if (gl->color && gl->rgba) { uint32_t p = gl->rgba[y * gl->w + x]; if (p >> 24) over(&row[ox + x - r->origin_x], cc < 1 ? scale_px(p, (uint32_t)(cc * 256)) : p); }
                else if (gl->alpha) { uint32_t a = gl->alpha[y * gl->w + x]; if (a) { a = a * ca / 255; over(&row[ox + x - r->origin_x], premul(it->color | 0xff000000u, a / 255.f * cc)); } }
            }
        }
    }
}

typedef struct { int a, b; uint32_t w; float cov; } ImgCol;
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
    for (int x = 0; x < count; x++) {
        uint32_t p = scale_px(top[x], 256 - weight) + scale_px(bottom[x], weight);
        uint32_t alpha = p >> 24;
        dst[x] = p + scale_px(dst[x], 256 - alpha - (alpha >> 7));
    }
}
static void image(R *r, const DItem *it) {
    const Image *im = it->img; float s = r->s;
    float x0 = it->x * s, y0 = it->y * s, x1 = (it->x + it->w) * s, y1 = (it->y + it->h) * s;
    if (x1 - x0 < 0.5f || y1 - y0 < 0.5f) return;
    const Clip *c = clip(r);
    if (r->c->punch && (const void *)im == r->c->punch) {
        int qx0 = LMAX(c->x0, (int)floorf(x0 + 0.5f)), qy0 = LMAX(c->y0, (int)floorf(y0 + 0.5f)), qx1 = LMIN(c->x1, (int)floorf(x1 + 0.5f)), qy1 = LMIN(c->y1, (int)floorf(y1 + 0.5f));
        for (int y = qy0; qx1 > qx0 && y < qy1; y++) memset(raster_row(r, y) + qx0 - r->origin_x, 0, (size_t)(qx1 - qx0) * 4);
        r->c->punched = !r->nlayer && !c->round;
        return;
    }
    if (!im->px) { if (!im->yuv) return; image_yuv_materialize((Image *)im); }
    int px0 = LMAX(c->x0, (int)floorf(x0)), py0 = LMAX(c->y0, (int)floorf(y0)), px1 = LMIN(c->x1, (int)ceilf(x1)), py1 = LMIN(c->y1, (int)ceilf(y1));
    if (px1 <= px0 || py1 <= py0) return;
    float kx = im->w / (x1 - x0), ky = im->h / (y1 - y0);
    bool down = kx > 1.5f || ky > 1.5f, rclip = c->round;
    int inner0 = LMAX(px0, (int)ceilf(x0)), inner1 = LMIN(px1, (int)floorf(x1));
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
        float sx = (x + 0.5f - x0) * kx - 0.5f;
        if (down) { k->a = k->b = LCLAMP((int)(sx + 0.5f), 0, im->w - 1); k->w = 0; }
        else { int ix = (int)floorf(sx); k->a = LCLAMP(ix, 0, im->w - 1); k->b = LCLAMP(ix + 1, 0, im->w - 1); k->w = (uint32_t)((sx - ix) * 256); }
        k->cov = span_cov(x0, x1, x);
    }
    for (int y = py0; y < py1; y++) {
        float sy = (y + 0.5f - y0) * ky - 0.5f; int iy = (int)floorf(sy);
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
        bool span = !down && !rclip && cy >= 0.999f && inner1 > inner0;
        if (span) image_vertical(row + inner0 - r->origin_x, ra + inner0 - px0,
                                 rb + inner0 - px0, inner1 - inner0, wy);
        for (int x = px0; x < px1; x++) {
            if (span && x == inner0) { x = inner1; if (x >= px1) break; }
            const ImgCol *k = &cols[x - px0];
            uint32_t p;
            if (down) p = ra[k->a];
            else p = scale_px(ra[x - px0], 256 - wy) + scale_px(rb[x - px0], wy);
            float cov = k->cov * cy; if (rclip) cov *= clip_cov(r, x + 0.5f, y + 0.5f);
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
static LayerRect *layer_bounds(const DisplayList *dl, float scale, LayerRect damage) {
    LayerRect *bounds = NULL;
    int stack[16], depth = 0;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_LAYER) {
            if (!bounds) bounds = xmalloc(sizeof *bounds * (size_t)dl->items.n);
            if (depth < 16) { stack[depth] = i; bounds[i] = (LayerRect){0}; }
            depth++;
        } else if (it->op == DO_POP_LAYER) { if (depth) depth--; }
        else if (depth && it->op == DO_TEXT) {
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

void raster(Canvas *cv, const DisplayList *dl, Color clearc) { raster_rect(cv, dl, clearc, 0, 0, cv->w, cv->h); }
void raster_rect(Canvas *cv, const DisplayList *dl, Color clearc, int rx0, int ry0, int rx1, int ry1) {
    R r; memset(&r, 0, sizeof r); r.c = cv; r.s = cv->scale; r.px = cv->px; r.stride = cv->stride; r.height = cv->h;
    rx0 = LMAX(rx0, 0); ry0 = LMAX(ry0, 0); rx1 = LMIN(rx1, cv->w); ry1 = LMIN(ry1, cv->h);
    if (rx1 <= rx0 || ry1 <= ry0) return;
    uint32_t cl = premul(clearc, 1);
    uint32_t *clear_row = cv->px + (size_t)ry0 * (size_t)cv->stride + rx0;
    int clear_stride = cv->stride;
    for (int y = ry0; y < ry1; y++, clear_row += clear_stride) store_span(clear_row, rx1 - rx0, cl);
    r.clips[0] = (Clip){ rx0, ry0, rx1, ry1, 0, 0, (float)cv->w, (float)cv->h, {0}, false }; r.nclip = 1;
    float s = r.s;
    LayerRect *bounds = layer_bounds(dl, s, (LayerRect){rx0, ry0, rx1, ry1});
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
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
            r.origin_x = b.x0; r.origin_y = b.y0;
            break;
        }
        case DO_POP_LAYER: {
            if (!r.nlayer) break;
            r.nlayer--; if (r.nlayer >= 16) break;
            LayerSave saved = r.layers[r.nlayer];
            uint32_t *src = r.px, k = (uint32_t)(saved.alpha * 256);
            LayerRect b = {r.origin_x, r.origin_y, r.origin_x + r.stride, r.origin_y + r.height};
            for (int y = b.y0; y < b.y1; y++) for (int x = b.x0; x < b.x1; x++) {
                size_t sj = (size_t)(y - r.origin_y) * (size_t)r.stride + (size_t)(x - r.origin_x);
                size_t dj = (size_t)(y - saved.origin_y) * (size_t)saved.stride + (size_t)(x - saved.origin_x);
                if (src[sj]) over(&saved.px[dj], scale_px(src[sj], k));
            }
            free(src); r.px = saved.px; r.origin_x = saved.origin_x;
            r.origin_y = saved.origin_y; r.stride = saved.stride; r.height = saved.height; break;
        }
        }
    }
    while (r.nlayer > 0) { r.nlayer--; if (r.nlayer < 16) { free(r.px); r.px = r.layers[r.nlayer].px; r.origin_y = r.layers[r.nlayer].origin_y; } }
    free(bounds);
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
