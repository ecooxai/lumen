/* CPU rasterizer: premultiplied ARGB32, anti-aliased shapes, glyphs, images */
#include "paint.h"
#include <math.h>
#include <zlib.h>

typedef struct { int x0, y0, x1, y1; float fx, fy, fw, fh, r[4]; bool round; } Clip;
typedef struct { uint32_t *px; float alpha; int x0, y0, x1, y1; bool clipped; } LayerSave;
typedef struct {
    Canvas *c; float s;
    Clip clips[64]; int nclip;
    LayerSave layers[16]; int nlayer;
    uint32_t *px;
} R;

void canvas_init(Canvas *c, int w, int h, float scale) { c->w = w; c->h = h; c->stride = w; c->scale = scale; c->punch = NULL; c->punched = false; c->track_media = true; c->media_retry = false; c->media_frames = (PaintFrames){0}; c->px = xcalloc((size_t)w * (size_t)h, 4); }
void canvas_free(Canvas *c) { free(c->px); c->px = NULL; paint_frames_free(&c->media_frames); }

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
            if (cov > 0) over(&r->px[(size_t)y * (size_t)r->c->stride + (size_t)x], premul(col, cov));
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
    /* outer shadows are clipped out of the border box (CSS) — and skipping it keeps big cards cheap */
    float bx0 = (it->x - it->sx) * s, by0 = (it->y - it->sy) * s, bx1 = (it->x - it->sx + it->w) * s, by1 = (it->y - it->sy + it->h) * s, brad[4], mr = 0;
    for (int i = 0; i < 4; i++) { brad[i] = LMAX(0, it->r[i] * s); mr = LMAX(mr, brad[i]); }
    int ix0 = (int)ceilf(bx0 + mr), iy0 = (int)ceilf(by0 + mr), ix1 = (int)floorf(bx1 - mr), iy1 = (int)floorf(by1 - mr);
    for (int y = py0; y < py1; y++) for (int x = px0; x < px1; x++) {
        if (y >= iy0 && y < iy1 && x >= ix0 && x < ix1) { x = ix1 - 1; continue; }
        float out = LCLAMP(0.5f + sd_rrect(x + 0.5f, y + 0.5f, bx0, by0, bx1, by1, brad), 0, 1);
        if (out <= 0) continue;
        float d = sd_rrect(x + 0.5f, y + 0.5f, x0, y0, x1, y1, rad);
        float t = LCLAMP((d + bl) / (2 * bl), 0, 1);
        float cov = (1 - t * t * (3 - 2 * t)) * out;
        cov *= clip_cov(r, x + 0.5f, y + 0.5f);
        if (cov > 0.002f) over(&r->px[(size_t)y * (size_t)r->c->stride + (size_t)x], premul(it->color, cov));
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
    float L = fabsf(w * dxv) + fabsf(h * dyv); if (L < 1e-3f) L = 1;
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, R2 = sqrtf(w * w + h * h) / 2;
    const Clip *c = clip(r);
    int px0 = LMAX(c->x0, (int)floorf(x0)), py0 = LMAX(c->y0, (int)floorf(y0)), px1 = LMIN(c->x1, (int)ceilf(x1)), py1 = LMIN(c->y1, (int)ceilf(y1));
    if (fabsf(dxv) < 1e-6f) { dxv = 0; dyv = dyv < 0 ? -1 : 1; } else if (fabsf(dyv) < 1e-6f) { dyv = 0; dxv = dxv < 0 ? -1 : 1; }
    float lt = NAN; uint32_t lp = 0; Color lc = 0;   /* axis-aligned gradients repeat t along a row */
    Color *colc = g->type != 1 && dyv == 0 && px1 > px0 ? xmalloc(sizeof *colc * (size_t)(px1 - px0)) : NULL;   /* horizontal: t depends on x only */
    if (colc) for (int x = px0; x < px1; x++) colc[x - px0] = grad_at(g, ((x + 0.5f - cx) * dxv) / L + 0.5f);
    for (int y = py0; y < py1; y++) for (int x = px0; x < px1; x++) {
        float fx = x + 0.5f, fy = y + 0.5f;
        float t = g->type == 1 ? sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) / LMAX(1, R2) : ((fx - cx) * dxv + (fy - cy) * dyv) / L + 0.5f;
        float cov = span_cov(x0, x1, x) * span_cov(y0, y1, y);
        if (round) cov = LCLAMP(0.5f - sd_rrect(fx, fy, x0, y0, x1, y1, rad), 0, 1);
        cov *= clip_cov(r, fx, fy);
        if (cov <= 0) continue;
        if (colc) { Color cc = colc[x - px0]; if (cc != lc || t != lt) { lt = t; lc = cc; lp = premul(lc, 1); } }
        else if (t != lt) { lt = t; lc = grad_at(g, t); lp = premul(lc, 1); }
        over(&r->px[(size_t)y * (size_t)r->c->stride + (size_t)x], cov >= 1 ? lp : premul(lc, cov));
    }
    free(colc);
}

static void text(R *r, const DItem *it) {
    float s = r->s, gs = s * (it->ts > 0 ? it->ts : 1); const Clip *c = clip(r);
    uint32_t ca = COLOR_A(it->color);
    for (int i = 0; i < it->ng; i++) {
        const DGlyph *dg = &it->g[i];
        if (!dg->font) continue;
        const Glyph *gl = font_glyph(dg->font, dg->gid, gs);
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

typedef struct { int a, b; uint32_t w; float cov; } ImgCol;
static void image(R *r, const DItem *it) {
    const Image *im = it->img; float s = r->s;
    float x0 = it->x * s, y0 = it->y * s, x1 = (it->x + it->w) * s, y1 = (it->y + it->h) * s;
    if (x1 - x0 < 0.5f || y1 - y0 < 0.5f) return;
    const Clip *c = clip(r);
    /* The GPU underlay cannot reproduce rounded clipping or group opacity.
       Keep the ordinary image when either prevents an actual alpha hole. */
    if (r->c->punch && (const void *)im == r->c->punch && !r->nlayer && !c->round) {
        int qx0 = LMAX(c->x0, (int)floorf(x0 + 0.5f)), qy0 = LMAX(c->y0, (int)floorf(y0 + 0.5f)), qx1 = LMIN(c->x1, (int)floorf(x1 + 0.5f)), qy1 = LMIN(c->y1, (int)floorf(y1 + 0.5f));
        if (qx1 <= qx0 || qy1 <= qy0) return;
        for (int y = qy0; y < qy1; y++) memset(r->px + (size_t)y * (size_t)r->c->stride + qx0, 0, (size_t)(qx1 - qx0) * 4);
        r->c->punched = true;
        return;
    }
    int px0 = LMAX(c->x0, (int)floorf(x0)), py0 = LMAX(c->y0, (int)floorf(y0)), px1 = LMIN(c->x1, (int)ceilf(x1)), py1 = LMIN(c->y1, (int)ceilf(y1));
    if (px1 <= px0 || py1 <= py0) return;
    if (!im->px) { if (!im->yuv) return; image_yuv_materialize((Image *)im); }
    /* This draw admitted the source frame/surface to the compositor. Track
     * one token per media owner, outside all pixel loops. Final submission,
     * not rasterization or physical pixel visibility, commits the counters. */
    if (r->c->track_media) {
        paint_frames_add(&r->c->media_frames, (PaintFrame){ im->media_owner, im->media_frame });
        if (im->sampled_canvas) paint_frames_merge(&r->c->media_frames, &im->sampled_canvas->media_frames);
    }
    float kx = im->w / (x1 - x0), ky = im->h / (y1 - y0);
    bool down = kx > 1.5f || ky > 1.5f, rclip = c->round;
    ImgCol *cols = xmalloc(sizeof *cols * (size_t)(px1 - px0));
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
        else { ra = im->px + (size_t)LCLAMP(iy, 0, im->h - 1) * (size_t)im->w; rb = im->px + (size_t)LCLAMP(iy + 1, 0, im->h - 1) * (size_t)im->w; }
        float cy = span_cov(y0, y1, y);
        uint32_t *row = r->px + (size_t)y * (size_t)r->c->stride;
        for (int x = px0; x < px1; x++) {
            const ImgCol *k = &cols[x - px0];
            uint32_t p;
            if (down) p = ra[k->a];
            else {
                uint32_t top = scale_px(ra[k->a], 256 - k->w) + scale_px(ra[k->b], k->w), bot = scale_px(rb[k->a], 256 - k->w) + scale_px(rb[k->b], k->w);
                p = scale_px(top, 256 - wy) + scale_px(bot, wy);
            }
            if (im->opaque) p |= 0xff000000u;
            float cov = k->cov * cy; if (rclip) cov *= clip_cov(r, x + 0.5f, y + 0.5f);
            if (cov < 0.999f) p = scale_px(p, (uint32_t)(cov * 256));
            over(&row[x], p);
        }
    }
    free(cols);
}

/* Index of the last opaque image that fully covers the raster rect (0 = none): everything drawn before it is hidden */
static int occluder(const Canvas *cv, const DisplayList *dl, float s, int rx0, int ry0, int rx1, int ry1) {
    int cb[64][4], nc = 1, nl = 0, best = 0; bool rnd[64];
    cb[0][0] = rx0; cb[0][1] = ry0; cb[0][2] = rx1; cb[0][3] = ry1; rnd[0] = false;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP) {
            if (nc >= 64) continue;
            const int *p = cb[nc - 1]; int *c = cb[nc];
            c[0] = LMAX(p[0], (int)floorf(it->x * s)); c[1] = LMAX(p[1], (int)floorf(it->y * s));
            c[2] = LMIN(p[2], (int)ceilf((it->x + it->w) * s)); c[3] = LMIN(p[3], (int)ceilf((it->y + it->h) * s));
            rnd[nc] = rnd[nc - 1] || it->r[0] > 0 || it->r[1] > 0 || it->r[2] > 0 || it->r[3] > 0;
            nc++;
        } else if (it->op == DO_POP_CLIP) { if (nc > 1) nc--; }
        else if (it->op == DO_PUSH_LAYER) nl++;
        else if (it->op == DO_POP_LAYER) { if (nl) nl--; }
        else if (it->op == DO_IMAGE && !nl && !rnd[nc - 1] && it->img && it->img->opaque && (const void *)it->img != cv->punch) {
            const int *c = cb[nc - 1];
            if (c[0] <= rx0 && c[1] <= ry0 && c[2] >= rx1 && c[3] >= ry1 &&
                ceilf(it->x * s) <= rx0 && ceilf(it->y * s) <= ry0 && floorf((it->x + it->w) * s) >= rx1 && floorf((it->y + it->h) * s) >= ry1) best = i;
        }
    }
    return best;
}

/* Like occluder(), but for an opaque image that covers most of the rect and misses only its edges (a video whose clip ends
   a row short of its box): returns its index and the covered part, so the caller can raster that part and the edge strips apart */
static int occluder_inner(const DisplayList *dl, float s, int rx0, int ry0, int rx1, int ry1, int *ib) {
    int cb[64][4], nc = 1, nl = 0, best = 0; bool rnd[64]; long need = (long)(rx1 - rx0) * (ry1 - ry0) * 9 / 10;
    cb[0][0] = rx0; cb[0][1] = ry0; cb[0][2] = rx1; cb[0][3] = ry1; rnd[0] = false;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP) {
            if (nc >= 64) continue;
            const int *p = cb[nc - 1]; int *c = cb[nc];
            c[0] = LMAX(p[0], (int)floorf(it->x * s)); c[1] = LMAX(p[1], (int)floorf(it->y * s));
            c[2] = LMIN(p[2], (int)ceilf((it->x + it->w) * s)); c[3] = LMIN(p[3], (int)ceilf((it->y + it->h) * s));
            rnd[nc] = rnd[nc - 1] || it->r[0] > 0 || it->r[1] > 0 || it->r[2] > 0 || it->r[3] > 0;
            nc++;
        } else if (it->op == DO_POP_CLIP) { if (nc > 1) nc--; }
        else if (it->op == DO_PUSH_LAYER) nl++;
        else if (it->op == DO_POP_LAYER) { if (nl) nl--; }
        else if (it->op == DO_IMAGE && !nl && !rnd[nc - 1] && it->img && it->img->opaque) {
            const int *c = cb[nc - 1];
            int x0 = LMAX(c[0], (int)ceilf(it->x * s)), y0 = LMAX(c[1], (int)ceilf(it->y * s)), x1 = LMIN(c[2], (int)floorf((it->x + it->w) * s)), y1 = LMIN(c[3], (int)floorf((it->y + it->h) * s));
            if (x1 > x0 && y1 > y0 && (long)(x1 - x0) * (y1 - y0) >= need) { best = i; ib[0] = x0; ib[1] = y0; ib[2] = x1; ib[3] = y1; }
        }
    }
    return best;
}
void raster(Canvas *cv, const DisplayList *dl, Color clearc) { raster_rect(cv, dl, clearc, 0, 0, cv->w, cv->h); }
static void raster_rect_(Canvas *cv, const DisplayList *dl, Color clearc, int rx0, int ry0, int rx1, int ry1, bool occl);
void raster_rect(Canvas *cv, const DisplayList *dl, Color clearc, int rx0, int ry0, int rx1, int ry1) {
    /* Full repaint replaces the retained surface. Partial repaint retains
     * its pending tokens; the media owner prunes dead/superseded identities. */
    if (cv->track_media && rx0 <= 0 && ry0 <= 0 && rx1 >= cv->w && ry1 >= cv->h) cv->media_frames.n = 0;
    static int chk = -1; if (chk < 0) chk = getenv("LUMEN_CHECK_OCCLUDE") != NULL;
    if (!chk) {
        int x0 = LMAX(rx0, 0), y0 = LMAX(ry0, 0), x1 = LMIN(rx1, cv->w), y1 = LMIN(ry1, cv->h), ib[4];
        if (x1 > x0 && y1 > y0 && !cv->punch && !occluder(cv, dl, cv->scale, x0, y0, x1, y1) && occluder_inner(dl, cv->scale, x0, y0, x1, y1, ib)) {
            raster_rect_(cv, dl, clearc, ib[0], ib[1], ib[2], ib[3], true);
            raster_rect_(cv, dl, clearc, x0, y0, x1, ib[1], true); raster_rect_(cv, dl, clearc, x0, ib[3], x1, y1, true);
            raster_rect_(cv, dl, clearc, x0, ib[1], ib[0], ib[3], true); raster_rect_(cv, dl, clearc, ib[2], ib[1], x1, ib[3], true);
            return;
        }
        raster_rect_(cv, dl, clearc, rx0, ry0, rx1, ry1, true); return;
    }
    int x0 = LMAX(rx0, 0), y0 = LMAX(ry0, 0), x1 = LMIN(rx1, cv->w), y1 = LMIN(ry1, cv->h);
    int first = x1 > x0 && y1 > y0 ? occluder(cv, dl, cv->scale, x0, y0, x1, y1) : 0;
    if (!first) { raster_rect_(cv, dl, clearc, rx0, ry0, rx1, ry1, false); return; }
    raster_rect_(cv, dl, clearc, rx0, ry0, rx1, ry1, false);
    Canvas t = *cv; t.media_frames = (PaintFrames){0}; t.track_media = false; t.media_retry = false; t.px = xmalloc((size_t)cv->stride * (size_t)cv->h * 4); memcpy(t.px, cv->px, (size_t)cv->stride * (size_t)cv->h * 4);
    raster_rect_(&t, dl, clearc, rx0, ry0, rx1, ry1, true);
    long bad = 0; for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) bad += t.px[(size_t)y * (size_t)cv->stride + (size_t)x] != cv->px[(size_t)y * (size_t)cv->stride + (size_t)x];
    fprintf(stderr, "lumen: occlude first=%d %dx%d at %d,%d mismatched px=%ld\n", first, x1 - x0, y1 - y0, x0, y0, bad);
    free(t.px);
}
/* device-pixel bounds of what a layer draws (items from its push to the matching pop), or empty; returns the pop index */
static int layer_bounds(const DisplayList *dl, int i, float s, int *b) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f; int depth = 0;
    for (i++; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_LAYER) { depth++; continue; }
        if (it->op == DO_POP_LAYER) { if (!depth--) break; continue; }
        if (it->op == DO_PUSH_CLIP || it->op == DO_POP_CLIP) continue;
        float a0 = it->x, b0 = it->y, a1 = it->x + it->w, b1 = it->y + it->h, e = 2;
        if (it->op == DO_SHADOW) e += fabsf(it->spread) + fabsf(it->blur) + fabsf(it->sx) + fabsf(it->sy);
        if (it->op == DO_BORDER) e += it->bw[0] + it->bw[1] + it->bw[2] + it->bw[3];
        if (it->op == DO_TEXT) {
            a0 = b0 = 1e30f; a1 = b1 = -1e30f;
            for (int k = 0; k < it->ng; k++) { const DGlyph *g = &it->g[k]; float em = g->font ? g->font->size : 16; a0 = LMIN(a0, g->x - 2 * em); a1 = LMAX(a1, g->x + 3 * em); b0 = LMIN(b0, g->y - 2 * em); b1 = LMAX(b1, g->y + 2 * em); }
        }
        if (a1 <= a0 || b1 <= b0) continue;
        x0 = LMIN(x0, a0 - e); y0 = LMIN(y0, b0 - e); x1 = LMAX(x1, a1 + e); y1 = LMAX(y1, b1 + e);
    }
    if (x1 <= x0) { b[0] = b[1] = b[2] = b[3] = 0; return i; }
    b[0] = (int)floorf(x0 * s) - 1; b[1] = (int)floorf(y0 * s) - 1; b[2] = (int)ceilf(x1 * s) + 1; b[3] = (int)ceilf(y1 * s) + 1;
    return i;
}
static void raster_rect_(Canvas *cv, const DisplayList *dl, Color clearc, int rx0, int ry0, int rx1, int ry1, bool occl) {
    R r; memset(&r, 0, sizeof r); r.c = cv; r.s = cv->scale; r.px = cv->px;
    rx0 = LMAX(rx0, 0); ry0 = LMAX(ry0, 0); rx1 = LMIN(rx1, cv->w); ry1 = LMIN(ry1, cv->h);
    if (rx1 <= rx0 || ry1 <= ry0) return;
    static int noocc = -1; if (noocc < 0) noocc = getenv("LUMEN_NO_OCCLUDE") != NULL;
    int first = noocc || !occl ? 0 : occluder(cv, dl, r.s, rx0, ry0, rx1, ry1);
    uint32_t cl = premul(clearc, 1);
    if (!first) for (int y = ry0; y < ry1; y++) for (int x = rx0; x < rx1; x++) cv->px[(size_t)y * (size_t)cv->stride + (size_t)x] = cl;
    r.clips[0] = (Clip){ rx0, ry0, rx1, ry1, 0, 0, (float)cv->w, (float)cv->h, {0}, false }; r.nclip = 1;
    float s = r.s;
    /* Opt-in single-pixel paint-order evidence, at most once per second. The
       page buffer is always owned here, including while a temporary layer is
       active. Do not inspect layer storage outside that layer's allocation. */
    static int video_trace = -1; static double video_trace_next;
    if (video_trace < 0) { const char *e = getenv("LUMEN_VIDEO_PAINT_TRACE"); video_trace = e && *e && strcmp(e, "0"); }
    bool trace = false; int tx = 0, ty = 0, trace_changes = 0;
    if (video_trace && cv->punch && now_ms() >= video_trace_next) {
        for (int i = 0; i < dl->items.n; i++) {
            const DItem *it = &dl->items.v[i];
            if (it->op != DO_IMAGE || (const void *)it->img != cv->punch) continue;
            double x = ((double)it->x + it->w * .5) * s, y = ((double)it->y + it->h * .5) * s;
            if (!isfinite(x) || !isfinite(y)) break;
            tx = (int)LCLAMP(x, 0, cv->w - 1); ty = (int)LCLAMP(y, 0, cv->h - 1);
            trace = tx >= rx0 && tx < rx1 && ty >= ry0 && ty < ry1;
            break;
        }
        if (trace) {
            video_trace_next = now_ms() + 1000;
            fprintf(stderr, "lumen-video-paint: begin center=%d,%d dirty=%d,%d-%d,%d items=%d first=%d\n", tx, ty, rx0, ry0, rx1, ry1, dl->items.n, first);
        }
    }
    static int dbg = -1; if (dbg < 0) dbg = getenv("LUMEN_DEBUG_RASTER") != NULL;
    double area[16] = {0}; int cnt[16] = {0};
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (i < first && it->op != DO_PUSH_CLIP && it->op != DO_POP_CLIP) continue;
        uint32_t before = 0; Clip before_clip = {0}; int before_layers = 0;
        if (trace) { before = cv->px[(size_t)ty * (size_t)cv->stride + (size_t)tx]; before_clip = *clip(&r); before_layers = r.nlayer; }
        if (dbg && it->op < 16) { const Clip *c = clip(&r); float ax0 = LMAX(c->x0, it->x * s), ay0 = LMAX(c->y0, it->y * s), ax1 = LMIN(c->x1, (it->x + it->w) * s), ay1 = LMIN(c->y1, (it->y + it->h) * s);
            if (ax1 > ax0 && ay1 > ay0) { area[it->op] += (ax1 - ax0) * (ay1 - ay0); cnt[it->op]++; } }
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
            /* the layer only needs the part of the current clip its contents can touch; one that touches nothing is skipped */
            int lb[4], pop = layer_bounds(dl, i, s, lb); const Clip *p = clip(&r);
            int lx0 = LMAX(p->x0, lb[0]), ly0 = LMAX(p->y0, lb[1]), lx1 = LMIN(p->x1, lb[2]), ly1 = LMIN(p->y1, lb[3]);
            if (lx1 <= lx0 || ly1 <= ly0 || it->alpha <= 0) { i = pop; break; }
            LayerSave *L = &r.layers[r.nlayer++]; L->px = r.px; L->alpha = it->alpha; L->clipped = r.nclip < 64;
            if (!L->clipped) { lx0 = rx0; ly0 = ry0; lx1 = rx1; ly1 = ry1; }
            else { Clip c = *p; c.x0 = lx0; c.y0 = ly0; c.x1 = lx1; c.y1 = ly1; r.clips[r.nclip++] = c; }
            L->x0 = lx0; L->y0 = ly0; L->x1 = lx1; L->y1 = ly1;
            r.px = (uint32_t *)xcalloc((size_t)cv->stride * (size_t)(ly1 - ly0), 4) - (size_t)ly0 * (size_t)cv->stride;   /* rows the layer can touch only */
            break;
        }
        case DO_POP_LAYER: {
            if (!r.nlayer) break;
            r.nlayer--; if (r.nlayer >= 16) break;
            LayerSave *L = &r.layers[r.nlayer];
            uint32_t *dst = L->px, *src = r.px; uint32_t k = (uint32_t)(L->alpha * 256);
            for (int y = L->y0; y < L->y1; y++) for (int x = L->x0; x < L->x1; x++) { size_t j = (size_t)y * (size_t)cv->stride + (size_t)x; if (src[j]) over(&dst[j], scale_px(src[j], k)); }
            free(src + (size_t)L->y0 * (size_t)cv->stride); r.px = dst;
            if (L->clipped && r.nclip > 1) r.nclip--;
            break;
        }
        }
        if (trace) {
            uint32_t after = cv->px[(size_t)ty * (size_t)cv->stride + (size_t)tx];
            bool punched_item = it->op == DO_IMAGE && (const void *)it->img == cv->punch;
            if (after != before) trace_changes++;
            if (punched_item || (after != before && trace_changes <= 12))
                fprintf(stderr, "lumen-video-paint: item=%d op=%u punch=%d box=%.2f,%.2f,%.2f,%.2f color=%08x clip=%d,%d-%d,%d round=%d layers=%d argb=%08x->%08x\n",
                    i, (unsigned)it->op, punched_item, it->x, it->y, it->w, it->h, it->color,
                    before_clip.x0, before_clip.y0, before_clip.x1, before_clip.y1, before_clip.round, before_layers, before, after);
        }
    }
    if (trace) fprintf(stderr, "lumen-video-paint: end punched=%d changes=%d argb=%08x\n", cv->punched, trace_changes, cv->px[(size_t)ty * (size_t)cv->stride + (size_t)tx]);
    if (dbg && (rx1 - rx0) * (ry1 - ry0) > 10000) { fprintf(stderr, "lumen: raster %d,%d-%d,%d first=%d", rx0, ry0, rx1, ry1, first); for (int k = 0; k < 16; k++) if (cnt[k]) fprintf(stderr, " op%d:%d/%.0fk", k, cnt[k], area[k] / 1000); fprintf(stderr, "\n"); }
    while (r.nlayer > 0) { r.nlayer--; if (r.nlayer < 16) { free(r.px + (size_t)r.layers[r.nlayer].y0 * (size_t)cv->stride); r.px = r.layers[r.nlayer].px; } }
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
