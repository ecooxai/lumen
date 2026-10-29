/* Canvas 2D rasteriser: polygon coverage (svg.c's accumulator), paint sources and composite operations */
#include "canvas2d.h"
#include "paint.h"
#include "../base/util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void svg_acc_line(float *acc, int w, int h, float x0, float y0, float x1, float y1);

bool c2d_cover(float *cov, int w, int h, const float *xy, const int *sub, int nsub, int n, bool eo, int *py0, int *py1) {
    float ymin = INFINITY, ymax = -INFINITY;
    for (int i = 0; i < n; i++) { float y = xy[2 * i + 1]; if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
    int y0 = (int)LMAX(0.f, floorf(ymin)), y1 = (int)LMIN((float)h, ceilf(ymax));
    if (!(y1 > y0) || w <= 0) return false;
    int rows = y1 - y0, W = w + 2;
    float *acc = calloc((size_t)W * (size_t)rows, sizeof(float));
    if (!acc) return false;
    for (int k = 0; k < nsub; k++) {
        int a = sub[k], e = k + 1 < nsub ? sub[k + 1] : n;
        for (int i = a; e - a > 1 && i < e; i++) {
            int j = i + 1 < e ? i + 1 : a;
            float x0 = xy[2 * i], ya = xy[2 * i + 1] - y0, x1 = xy[2 * j], yb = xy[2 * j + 1] - y0;
            if (isfinite(x0) && isfinite(ya) && isfinite(x1) && isfinite(yb)) svg_acc_line(acc, w, rows, x0, ya, x1, yb);
        }
    }
    for (int y = 0; y < rows; y++) {
        float s = 0, *row = acc + (size_t)y * W, *o = cov + (size_t)(y + y0) * w;
        for (int x = 0; x < w; x++) {
            s += row[x];
            float k = fabsf(s);
            if (eo) { k = fmodf(k, 2); if (k > 1) k = 2 - k; } else if (k > 1) k = 1;
            o[x] = k < 1 / 1024.f ? 0 : k > 1 - 1 / 1024.f ? 1 : k;
        }
    }
    free(acc);
    *py0 = y0; *py1 = y1;
    return true;
}

typedef struct { float r, g, b, a; } F4;
static F4 unpack(uint32_t v) { return (F4){ ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, (v >> 24) / 255.f }; }
static uint32_t pack(F4 c) {
    float a = LCLAMP(c.a, 0.f, 1.f);
    uint32_t A = (uint32_t)(a * 255 + 0.5f);
    float m = A / 255.f;
    uint32_t R = (uint32_t)(LCLAMP(c.r, 0.f, m) * 255 + 0.5f), G = (uint32_t)(LCLAMP(c.g, 0.f, m) * 255 + 0.5f), B = (uint32_t)(LCLAMP(c.b, 0.f, m) * 255 + 0.5f);
    return A << 24 | R << 16 | G << 8 | B;
}
static uint32_t lut_at(const C2DPaint *p, float t) {
    if (!(t == t)) return 0;
    return p->lut[(int)(LCLAMP(t, 0.f, 1.f) * 1023 + 0.5f)];
}
static uint32_t texel(const C2DPaint *p, int x, int y) {
    if (p->rx) { x %= p->iw; if (x < 0) x += p->iw; } else if (x < 0 || x >= p->iw) return 0;
    if (p->ry) { y %= p->ih; if (y < 0) y += p->ih; } else if (y < 0 || y >= p->ih) return 0;
    return p->img[(size_t)y * p->iw + x];
}
static uint32_t sample(const C2DPaint *p, float px, float py) {
    if (p->kind == 0) return p->col;
    float u = p->inv[0] * px + p->inv[2] * py + p->inv[4], v = p->inv[1] * px + p->inv[3] * py + p->inv[5];
    const float *g = p->g;
    if (p->kind == 1) {
        float dx = g[2] - g[0], dy = g[3] - g[1], l = dx * dx + dy * dy;
        return l > 0 ? lut_at(p, ((u - g[0]) * dx + (v - g[1]) * dy) / l) : 0;
    }
    if (p->kind == 2) {   /* the largest t with r(t) >= 0 whose circle passes through the point */
        float cdx = g[3] - g[0], cdy = g[4] - g[1], dr = g[5] - g[2], pdx = u - g[0], pdy = v - g[1];
        float a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + g[2] * dr, c = pdx * pdx + pdy * pdy - g[2] * g[2], t;
        if (fabsf(a) < 1e-6f) {
            if (fabsf(b) < 1e-9f) return 0;
            t = c / (2 * b);
            if (g[2] + t * dr < 0) return 0;
        } else {
            float disc = b * b - a * c;
            if (disc < 0) return 0;
            float s = sqrtf(disc), t1 = (b + s) / a, t2 = (b - s) / a, hi = LMAX(t1, t2), lo = LMIN(t1, t2);
            if (g[2] + hi * dr >= 0) t = hi; else if (g[2] + lo * dr >= 0) t = lo; else return 0;
        }
        return lut_at(p, t);
    }
    if (p->kind == 3) {
        float t = (atan2f(v - g[1], u - g[0]) - g[2]) / (2 * (float)M_PI);
        return lut_at(p, t - floorf(t));
    }
    if (!p->img || p->iw <= 0 || p->ih <= 0) return 0;
    if (!p->smooth) return texel(p, (int)floorf(u), (int)floorf(v));
    float fx = u - 0.5f, fy = v - 0.5f, x0 = floorf(fx), y0 = floorf(fy), ax = fx - x0, ay = fy - y0;
    int ix = (int)x0, iy = (int)y0;
    if (!p->rx) { if (ix < 0 && u >= 0) { ix = 0; ax = 0; } if (ix >= p->iw - 1 && u <= p->iw) { ix = p->iw - 1; ax = 0; } }
    if (!p->ry) { if (iy < 0 && v >= 0) { iy = 0; ay = 0; } if (iy >= p->ih - 1 && v <= p->ih) { iy = p->ih - 1; ay = 0; } }
    F4 c00 = unpack(texel(p, ix, iy)), c10 = unpack(texel(p, ix + 1, iy)), c01 = unpack(texel(p, ix, iy + 1)), c11 = unpack(texel(p, ix + 1, iy + 1));
#define LERP2(f) ((c00.f * (1 - ax) + c10.f * ax) * (1 - ay) + (c01.f * (1 - ax) + c11.f * ax) * ay)
    F4 o = { LERP2(r), LERP2(g), LERP2(b), LERP2(a) };
#undef LERP2
    return pack(o);
}

static float blend1(int op, float s, float d) {
    switch (op) {
    case C2D_MULTIPLY: return s * d;
    case C2D_SCREEN: return s + d - s * d;
    case C2D_OVERLAY: return d <= 0.5f ? 2 * s * d : 1 - 2 * (1 - s) * (1 - d);
    case C2D_DARKEN: return LMIN(s, d);
    case C2D_LIGHTEN: return LMAX(s, d);
    case C2D_DODGE: return d <= 0 ? 0 : s >= 1 ? 1 : LMIN(1.f, d / (1 - s));
    case C2D_BURN: return d >= 1 ? 1 : s <= 0 ? 0 : 1 - LMIN(1.f, (1 - d) / s);
    case C2D_HARD: return s <= 0.5f ? 2 * s * d : 1 - 2 * (1 - s) * (1 - d);
    case C2D_SOFT: {
        if (s <= 0.5f) return d - (1 - 2 * s) * d * (1 - d);
        float D = d <= 0.25f ? ((16 * d - 12) * d + 4) * d : sqrtf(d);
        return d + (2 * s - 1) * (D - d);
    }
    case C2D_DIFF: return fabsf(s - d);
    case C2D_EXCL: return s + d - 2 * s * d;
    }
    return s;
}
static float lum(F4 c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }
static F4 clip_col(F4 c) {
    float l = lum(c), n = LMIN(c.r, LMIN(c.g, c.b)), x = LMAX(c.r, LMAX(c.g, c.b));
    if (n < 0) { c.r = l + (c.r - l) * l / (l - n); c.g = l + (c.g - l) * l / (l - n); c.b = l + (c.b - l) * l / (l - n); }
    if (x > 1) { c.r = l + (c.r - l) * (1 - l) / (x - l); c.g = l + (c.g - l) * (1 - l) / (x - l); c.b = l + (c.b - l) * (1 - l) / (x - l); }
    return c;
}
static F4 set_lum(F4 c, float l) { float d = l - lum(c); c.r += d; c.g += d; c.b += d; return clip_col(c); }
static float sat(F4 c) { return LMAX(c.r, LMAX(c.g, c.b)) - LMIN(c.r, LMIN(c.g, c.b)); }
static F4 set_sat(F4 c, float s) {
    float *v[3] = { &c.r, &c.g, &c.b };
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2 - i; j++) if (*v[j] > *v[j + 1]) { float *t = v[j]; v[j] = v[j + 1]; v[j + 1] = t; }
    if (*v[2] > *v[0]) { *v[1] = (*v[1] - *v[0]) * s / (*v[2] - *v[0]); *v[2] = s; } else *v[1] = *v[2] = 0;
    *v[0] = 0;
    return c;
}

static F4 composite(int op, F4 S, F4 D) {
    float sa = S.a, da = D.a, fa, fb;
    if (op >= C2D_MULTIPLY) {
        if (sa <= 0) return D;
        F4 cs = { S.r / sa, S.g / sa, S.b / sa, 1 }, cd = da > 0 ? (F4){ D.r / da, D.g / da, D.b / da, 1 } : (F4){ 0, 0, 0, 1 }, B;
        if (op <= C2D_EXCL) B = (F4){ blend1(op, cs.r, cd.r), blend1(op, cs.g, cd.g), blend1(op, cs.b, cd.b), 1 };
        else if (op == C2D_HUE) B = set_lum(set_sat(cs, sat(cd)), lum(cd));
        else if (op == C2D_SAT) B = set_lum(set_sat(cd, sat(cs)), lum(cd));
        else if (op == C2D_COLOR) B = set_lum(cs, lum(cd));
        else B = set_lum(cd, lum(cs));
        float k = sa * da;
        return (F4){ (1 - da) * S.r + (1 - sa) * D.r + k * B.r, (1 - da) * S.g + (1 - sa) * D.g + k * B.g, (1 - da) * S.b + (1 - sa) * D.b + k * B.b, sa + da - k };
    }
    switch (op) {
    case C2D_SRC_IN: fa = da; fb = 0; break;
    case C2D_SRC_OUT: fa = 1 - da; fb = 0; break;
    case C2D_SRC_ATOP: fa = da; fb = 1 - sa; break;
    case C2D_DST_OVER: fa = 1 - da; fb = 1; break;
    case C2D_DST_IN: fa = 0; fb = sa; break;
    case C2D_DST_OUT: fa = 0; fb = 1 - sa; break;
    case C2D_DST_ATOP: fa = 1 - da; fb = sa; break;
    case C2D_LIGHTER: fa = 1; fb = 1; break;
    case C2D_COPY: fa = 1; fb = 0; break;
    case C2D_XOR: fa = 1 - da; fb = 1 - sa; break;
    default: fa = 1; fb = 1 - sa;
    }
    F4 o = { S.r * fa + D.r * fb, S.g * fa + D.g * fb, S.b * fa + D.b * fb, S.a * fa + D.a * fb };
    if (op == C2D_LIGHTER) { o.a = LMIN(o.a, 1.f); o.r = LMIN(o.r, o.a); o.g = LMIN(o.g, o.a); o.b = LMIN(o.b, o.a); }
    return o;
}

/* source outside the shape counts as transparent black: these operations change pixels there too */
static bool unbounded(int op) { return op == C2D_SRC_IN || op == C2D_SRC_OUT || op == C2D_DST_IN || op == C2D_DST_ATOP || op == C2D_COPY; }

void c2d_paint(uint32_t *px, int w, int h, const float *cov, const C2DPaint *p, float alpha, int op, const float *clip, int y0, int y1) {
    if (unbounded(op) || !cov) { y0 = 0; y1 = h; }
    bool ub = unbounded(op);
    for (int y = LMAX(0, y0); y < LMIN(h, y1); y++) {
        uint32_t *row = px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            float k = cov ? cov[(size_t)y * w + x] : 1, cl = clip ? clip[(size_t)y * w + x] : 1;
            if (cl <= 0 || (k <= 0 && !ub)) continue;
            uint32_t dv = row[x];
            if (op == C2D_OVER && k >= 1 && cl >= 1 && alpha >= 1 && p->kind == 0 && (p->col >> 24) == 255) { row[x] = p->col; continue; }
            F4 S = k > 0 ? unpack(sample(p, x + 0.5f, y + 0.5f)) : (F4){ 0, 0, 0, 0 }, D = unpack(dv);
            float m = k * alpha; S.r *= m; S.g *= m; S.b *= m; S.a *= m;
            F4 R = composite(op, S, D);
            if (cl < 1) { R.r = R.r * cl + D.r * (1 - cl); R.g = R.g * cl + D.g * (1 - cl); R.b = R.b * cl + D.b * (1 - cl); R.a = R.a * cl + D.a * (1 - cl); }
            row[x] = pack(R);
        }
    }
}

/* canvas element -> its shown frame; replaced frames wait in a ring before being freed, as the
   raster threads may still hold the previous display list */
#define C2D_MAX 512
static struct { const void *node; Image *im; } g_ci[C2D_MAX];
static Image *g_grave[32];
static int g_grave_i;
static void bury(Image *im) { if (!im) return; image_unref(g_grave[g_grave_i]); g_grave[g_grave_i] = im; g_grave_i = (g_grave_i + 1) % 32; }
Image *c2d_image_for(const void *node) {
    for (int i = 0; i < C2D_MAX; i++) if (g_ci[i].node == node) return g_ci[i].im;
    return NULL;
}
void c2d_present(const void *node, const uint32_t *px, int w, int h) {
    int k = -1, f = -1;
    for (int i = 0; i < C2D_MAX && k < 0; i++) { if (g_ci[i].node == node) k = i; else if (!g_ci[i].node && f < 0) f = i; }
    if (k < 0) { if (f < 0) return; k = f; g_ci[k].node = node; }
    Image *im = g_ci[k].im;
    if (!im || im->w != w || im->h != h) {
        bury(im);
        im = xcalloc(1, sizeof *im); im->w = w; im->h = h; im->refs = 1; im->px = xmalloc((size_t)w * h * 4);
        g_ci[k].im = im;
    }
    memcpy(im->px, px, (size_t)w * h * 4);
    bool tr = false;
    for (size_t i = 0, n = (size_t)w * h; i < n && !tr; i++) tr = (px[i] >> 24) != 255;
    im->translucent = tr;
    im->content_version++;
}
void c2d_forget(const void *node) {
    for (int i = 0; i < C2D_MAX; i++) if (g_ci[i].node == node) { bury(g_ci[i].im); g_ci[i].node = NULL; g_ci[i].im = NULL; }
}
uint32_t *c2d_node_pixels(Node *n, int *w, int *h) {
    Image *im = paint_image_hook ? paint_image_hook(n) : NULL;
    if (im && !im->px && (im->yuv || im->gpu)) image_yuv_materialize(im);
    if (!im || !im->px || im->w <= 0 || im->h <= 0) return NULL;
    *w = im->w; *h = im->h;
    uint32_t *o = xmalloc((size_t)im->w * im->h * 4);
    memcpy(o, im->px, (size_t)im->w * im->h * 4);
    return o;
}
