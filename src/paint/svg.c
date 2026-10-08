/* Inline <svg> rasteriser: paths and basic shapes to a premultiplied ARGB Image */
#include "paint.h"
#include "../dom/dom.h"
#include "../css/css.h"
#include "../base/util.h"
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct { float a, b, c, d, e, f; } Mat;
static Mat mmul(Mat m, Mat n) {
    return (Mat){ m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d, m.b * n.c + m.d * n.d,
                  m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f };
}

typedef struct { VEC(float) xy; VEC(int) st; VEC(uint8_t) cl; Mat m; float sc; } PathB;
static void pb_pt(PathB *p, float x, float y) { vec_push(p->xy, p->m.a * x + p->m.c * y + p->m.e); vec_push(p->xy, p->m.b * x + p->m.d * y + p->m.f); }
static void pb_move(PathB *p, float x, float y) { vec_push(p->st, p->xy.n / 2); vec_push(p->cl, 0); pb_pt(p, x, y); }
static void pb_line(PathB *p, float x, float y) { if (!p->st.n) pb_move(p, x, y); else pb_pt(p, x, y); }
static void pb_close(PathB *p) { if (p->cl.n) p->cl.v[p->cl.n - 1] = 1; }

static void cubic(PathB *p, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
    if (!p->st.n) pb_move(p, x0, y0);
    float len = hypotf(x1 - x0, y1 - y0) + hypotf(x2 - x1, y2 - y1) + hypotf(x3 - x2, y3 - y2);
    int n = (int)LMIN(64.f, LMAX(2.f, len * p->sc / 3));
    for (int i = 1; i <= n; i++) {
        float t = (float)i / n, u = 1 - t;
        pb_pt(p, u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3, u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3);
    }
}
static void quad(PathB *p, float x0, float y0, float qx, float qy, float x, float y) {
    cubic(p, x0, y0, x0 + 2.f / 3 * (qx - x0), y0 + 2.f / 3 * (qy - y0), x + 2.f / 3 * (qx - x), y + 2.f / 3 * (qy - y), x, y);
}
static void arc(PathB *p, float x1, float y1, float rx, float ry, float deg, bool fa, bool fs, float x2, float y2) {
    if (x1 == x2 && y1 == y2) return;
    rx = fabsf(rx); ry = fabsf(ry);
    if (rx == 0 || ry == 0) { pb_line(p, x2, y2); return; }
    float ph = deg * (float)M_PI / 180, cp = cosf(ph), sp = sinf(ph), dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    float xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
    float lam = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (lam > 1) { float s = sqrtf(lam); rx *= s; ry *= s; }
    float num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp, den = rx * rx * yp * yp + ry * ry * xp * xp;
    float co = den > 0 ? sqrtf(LMAX(0.f, num / den)) : 0;
    if (fa == fs) co = -co;
    float cxp = co * rx * yp / ry, cyp = -co * ry * xp / rx;
    float cx = cp * cxp - sp * cyp + (x1 + x2) / 2, cy = sp * cxp + cp * cyp + (y1 + y2) / 2;
    float t1 = atan2f((yp - cyp) / ry, (xp - cxp) / rx), t2 = atan2f((-yp - cyp) / ry, (-xp - cxp) / rx), dt = t2 - t1;
    if (fs && dt < 0) dt += 2 * (float)M_PI; else if (!fs && dt > 0) dt -= 2 * (float)M_PI;
    int n = (int)LMIN(128.f, LMAX(4.f, fabsf(dt) * LMAX(rx, ry) * p->sc / 2));
    if (!p->st.n) pb_move(p, x1, y1);
    for (int i = 1; i <= n; i++) { float t = t1 + dt * i / n, ex = rx * cosf(t), ey = ry * sinf(t); pb_pt(p, cp * ex - sp * ey + cx, sp * ex + cp * ey + cy); }
}

static const char *skipws(const char *s) { while (*s && (isspace((unsigned char)*s) || *s == ',')) s++; return s; }
static bool num(const char **s, float *v) { const char *p = skipws(*s); char *e; double d = strtod(p, &e); if (e == p) return false; *v = (float)d; *s = e; return true; }
static bool flag(const char **s, float *v) { const char *p = skipws(*s); if (*p != '0' && *p != '1') return false; *v = (float)(*p - '0'); *s = p + 1; return true; }

static void parse_d(PathB *p, const char *s) {
    float cx = 0, cy = 0, sx = 0, sy = 0, qx = 0, qy = 0, kx = 0, ky = 0; char cmd = 0, prev = 0;
    for (;;) {
        s = skipws(s); if (!*s) break;
        if (isalpha((unsigned char)*s)) cmd = *s++; else if (!cmd) break;
        bool rel = islower((unsigned char)cmd); char C = (char)toupper((unsigned char)cmd);
        if (C == 'Z') { pb_close(p); cx = sx; cy = sy; prev = 'Z'; cmd = 0; continue; }
        int need = C == 'M' || C == 'L' || C == 'T' ? 2 : C == 'H' || C == 'V' ? 1 : C == 'C' ? 6 : C == 'S' || C == 'Q' ? 4 : C == 'A' ? 7 : -1;
        if (need < 0) break;
        float a[7]; bool ok = true;
        for (int i = 0; i < need && ok; i++) ok = C == 'A' && (i == 3 || i == 4) ? flag(&s, &a[i]) : num(&s, &a[i]);
        if (!ok) break;
        float ox = rel ? cx : 0, oy = rel ? cy : 0;
        switch (C) {
        case 'M': cx = a[0] + ox; cy = a[1] + oy; pb_move(p, cx, cy); sx = cx; sy = cy; cmd = rel ? 'l' : 'L'; break;
        case 'L': cx = a[0] + ox; cy = a[1] + oy; pb_line(p, cx, cy); break;
        case 'H': cx = a[0] + ox; pb_line(p, cx, cy); break;
        case 'V': cy = a[0] + oy; pb_line(p, cx, cy); break;
        case 'C': cubic(p, cx, cy, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy, a[4] + ox, a[5] + oy); kx = a[2] + ox; ky = a[3] + oy; cx = a[4] + ox; cy = a[5] + oy; break;
        case 'S': { float x1 = prev == 'C' || prev == 'S' ? 2 * cx - kx : cx, y1 = prev == 'C' || prev == 'S' ? 2 * cy - ky : cy;
            cubic(p, cx, cy, x1, y1, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy); kx = a[0] + ox; ky = a[1] + oy; cx = a[2] + ox; cy = a[3] + oy; break; }
        case 'Q': qx = a[0] + ox; qy = a[1] + oy; quad(p, cx, cy, qx, qy, a[2] + ox, a[3] + oy); cx = a[2] + ox; cy = a[3] + oy; break;
        case 'T': qx = prev == 'Q' || prev == 'T' ? 2 * cx - qx : cx; qy = prev == 'Q' || prev == 'T' ? 2 * cy - qy : cy;
            quad(p, cx, cy, qx, qy, a[0] + ox, a[1] + oy); cx = a[0] + ox; cy = a[1] + oy; break;
        case 'A': arc(p, cx, cy, a[0], a[1], a[2], a[3] != 0, a[4] != 0, a[5] + ox, a[6] + oy); cx = a[5] + ox; cy = a[6] + oy; break;
        }
        prev = C;
    }
}

static Mat parse_transform(const char *s, Mat m) {
    while (*s) {
        while (*s && !isalpha((unsigned char)*s)) s++;
        const char *nm = s; while (isalpha((unsigned char)*s)) s++;
        size_t nl = (size_t)(s - nm);
        while (*s && *s != '(') s++;
        if (!*s) break;
        s++;
        float v[6] = { 0 }; int k = 0; while (k < 6 && num(&s, &v[k])) k++;
        while (*s && *s != ')') s++;
        if (*s) s++;
        Mat t = { 1, 0, 0, 1, 0, 0 };
        if (nl == 9 && !strncmp(nm, "translate", 9)) { t.e = v[0]; t.f = k > 1 ? v[1] : 0; }
        else if (nl == 5 && !strncmp(nm, "scale", 5)) { t.a = v[0]; t.d = k > 1 ? v[1] : v[0]; }
        else if (nl == 6 && !strncmp(nm, "matrix", 6) && k == 6) t = (Mat){ v[0], v[1], v[2], v[3], v[4], v[5] };
        else if (nl == 6 && !strncmp(nm, "rotate", 6)) {
            float r = v[0] * (float)M_PI / 180; t = (Mat){ cosf(r), sinf(r), -sinf(r), cosf(r), 0, 0 };
            if (k == 3) t = mmul(mmul((Mat){ 1, 0, 0, 1, v[1], v[2] }, t), (Mat){ 1, 0, 0, 1, -v[1], -v[2] });
        }
        m = mmul(m, t);
    }
    return m;
}

typedef struct { float *acc; int w, h; uint32_t *px; float *clip, vw, vh; } Ctx;
/* Paint source: solid colour or gradient (inv maps device pixels into gradient space; LUT is non-premultiplied) */
typedef struct { Color col; int kind, spread; Mat inv; float x1, y1, x2, y2, r; Color lut[256]; } Src;

static Mat minv(Mat m) {
    float d = m.a * m.d - m.b * m.c;
    if (fabsf(d) < 1e-12f) return (Mat){ 0 };
    float k = 1 / d;
    return (Mat){ m.d * k, -m.b * k, -m.c * k, m.a * k, (m.c * m.f - m.d * m.e) * k, (m.b * m.e - m.a * m.f) * k };
}
static Color src_at(const Src *s, float px, float py) {
    float gx = s->inv.a * px + s->inv.c * py + s->inv.e, gy = s->inv.b * px + s->inv.d * py + s->inv.f, t;
    if (s->kind == 1) { float dx = s->x2 - s->x1, dy = s->y2 - s->y1, l = dx * dx + dy * dy; t = l > 0 ? ((gx - s->x1) * dx + (gy - s->y1) * dy) / l : 0; }
    else t = s->r > 0 ? hypotf(gx - s->x1, gy - s->y1) / s->r : 1;
    if (s->spread == 1) { t = fmodf(fabsf(t), 2); if (t > 1) t = 2 - t; }
    else if (s->spread == 2) t -= floorf(t);
    return s->lut[(int)(LCLAMP(t, 0.f, 1.f) * 255 + 0.5f)];
}

static void acc_line(Ctx *c, float x0, float y0, float x1, float y1) {
    int w = c->w, h = c->h, W = w + 2;
    if (y0 == y1) return;
    float dir = 1;
    if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
    if (y1 <= 0 || y0 >= h) return;
    float dxdy = (x1 - x0) / (y1 - y0), x = x0;
    if (y0 < 0) { x -= y0 * dxdy; y0 = 0; }
    if (y1 > h) y1 = (float)h;
    for (int y = (int)y0; y < (int)ceilf(y1); y++) {
        float dy = LMIN((float)(y + 1), y1) - LMAX((float)y, y0), xn = x + dxdy * dy, d = dy * dir;
        float xa = LCLAMP(LMIN(x, xn), 0.f, (float)w), xb = LCLAMP(LMAX(x, xn), 0.f, (float)w);
        float *row = c->acc + (size_t)y * W, xaf = floorf(xa), xbc = ceilf(xb);
        int xai = (int)xaf, xbi = (int)xbc;
        if (xbi <= xai + 1) { float xm = 0.5f * (xa + xb) - xaf; row[xai] += d - d * xm; row[xai + 1] += d * xm; }
        else {
            float s = 1 / (xb - xa), f0 = xa - xaf, a0 = 0.5f * s * (1 - f0) * (1 - f0), f1 = xb - xbc + 1, am = 0.5f * s * f1 * f1;
            row[xai] += d * a0;
            if (xbi == xai + 2) row[xai + 1] += d * (1 - a0 - am);
            else {
                float a1 = s * (1.5f - f0); row[xai + 1] += d * (a1 - a0);
                for (int xi = xai + 2; xi < xbi - 1; xi++) row[xi] += d * s;
                float a2 = a1 + (float)(xbi - xai - 3) * s; row[xbi - 1] += d * (1 - a2 - am);
            }
            row[xbi] += d * am;
        }
        x = xn;
    }
}

static void comp(Ctx *c, const Src *sr, float op, bool eo) {
    int W = c->w + 2;
    if (op > 0 && (sr->kind || COLOR_A(sr->col)))
        for (int y = 0; y < c->h; y++) {
            float s = 0, *row = c->acc + (size_t)y * W; uint32_t *d = c->px + (size_t)y * c->w;
            const float *cl = c->clip ? c->clip + (size_t)y * c->w : NULL;
            for (int x = 0; x < c->w; x++) {
                s += row[x];
                float k = fabsf(s);
                if (eo) { k = fmodf(k, 2); if (k > 1) k = 2 - k; } else if (k > 1) k = 1;
                if (cl) k *= cl[x];
                if (k < 1 / 512.f) continue;
                Color col = sr->kind ? src_at(sr, x + 0.5f, y + 0.5f) : sr->col;
                float a = k * COLOR_A(col) / 255.f * op; if (a < 1 / 512.f) continue;
                uint32_t sa = (uint32_t)(a * 255 + 0.5f), inv = 255 - sa, dp = d[x];
                uint32_t r = (uint32_t)(COLOR_R(col) * a + 0.5f) + ((dp >> 16) & 255) * inv / 255;
                uint32_t g = (uint32_t)(COLOR_G(col) * a + 0.5f) + ((dp >> 8) & 255) * inv / 255;
                uint32_t b = (uint32_t)(COLOR_B(col) * a + 0.5f) + (dp & 255) * inv / 255;
                d[x] = ((sa + (dp >> 24) * inv / 255) << 24) | (LMIN(r, 255u) << 16) | (LMIN(g, 255u) << 8) | LMIN(b, 255u);
            }
        }
    memset(c->acc, 0, sizeof(float) * (size_t)W * (size_t)c->h);
}

/* Accumulated coverage into dst (union by max), for building clip masks */
static void cover(Ctx *c, float *dst, bool eo) {
    int W = c->w + 2;
    for (int y = 0; y < c->h; y++) {
        float s = 0, *row = c->acc + (size_t)y * W, *o = dst + (size_t)y * c->w;
        for (int x = 0; x < c->w; x++) {
            s += row[x];
            float k = fabsf(s);
            if (eo) { k = fmodf(k, 2); if (k > 1) k = 2 - k; } else if (k > 1) k = 1;
            if (k > o[x]) o[x] = k;
        }
    }
    memset(c->acc, 0, sizeof(float) * (size_t)W * (size_t)c->h);
}

static void sub_range(const PathB *p, int k, int *a, int *e) { *a = p->st.v[k]; *e = k + 1 < p->st.n ? p->st.v[k + 1] : p->xy.n / 2; }

static void acc_path(Ctx *c, const PathB *p) {
    for (int k = 0; k < p->st.n; k++) {
        int a, e; sub_range(p, k, &a, &e);
        for (int i = a; e - a > 1 && i < e; i++) { int j = i + 1 < e ? i + 1 : a; acc_line(c, p->xy.v[2 * i], p->xy.v[2 * i + 1], p->xy.v[2 * j], p->xy.v[2 * j + 1]); }
    }
}
static void fill_path(Ctx *c, const PathB *p, const Src *sr, float op, bool eo) { acc_path(c, p); comp(c, sr, op, eo); }

static void acc_poly(Ctx *c, const float *q, int n) {
    float ar = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; ar += q[2 * i] * q[2 * j + 1] - q[2 * j] * q[2 * i + 1]; }
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; if (ar < 0) acc_line(c, q[2 * j], q[2 * j + 1], q[2 * i], q[2 * i + 1]); else acc_line(c, q[2 * i], q[2 * i + 1], q[2 * j], q[2 * j + 1]); }
}

static void stroke_path(Ctx *c, const PathB *p, const Src *sr, float hw, float op) {
    for (int k = 0; k < p->st.n; k++) {
        int a, e; sub_range(p, k, &a, &e);
        int nseg = p->cl.v[k] ? e - a : e - a - 1;
        for (int s = 0; s < nseg; s++) {
            int i = a + s, j = i + 1 < e ? i + 1 : a;
            float x0 = p->xy.v[2 * i], y0 = p->xy.v[2 * i + 1], x1 = p->xy.v[2 * j], y1 = p->xy.v[2 * j + 1], L = hypotf(x1 - x0, y1 - y0);
            if (L < 1e-4f) continue;
            float nx = -(y1 - y0) / L * hw, ny = (x1 - x0) / L * hw;
            float q[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
            acc_poly(c, q, 4);
        }
        for (int i = a; hw > 0.6f && i < e; i++) {
            float q[16];
            for (int t = 0; t < 8; t++) { q[2 * t] = p->xy.v[2 * i] + hw * cosf(t * (float)M_PI / 4); q[2 * t + 1] = p->xy.v[2 * i + 1] + hw * sinf(t * (float)M_PI / 4); }
            acc_poly(c, q, 8);
        }
    }
    comp(c, sr, op, false);
}

static float fattr(const Node *n, const char *k) { const char *v = node_attr(n, k); return v ? (float)atof(v) : 0; }

static void build_shape(PathB *p, const Node *n) {
    const char *t = n->tag;
    if (!strcmp(t, "path")) { const char *d = node_attr(n, "d"); if (d) parse_d(p, d); }
    else if (!strcmp(t, "rect")) {
        float x = fattr(n, "x"), y = fattr(n, "y"), w = fattr(n, "width"), h = fattr(n, "height"), rx = fattr(n, "rx"), ry = fattr(n, "ry");
        if (w <= 0 || h <= 0) return;
        if (!node_attr(n, "rx")) rx = ry;
        if (!node_attr(n, "ry")) ry = rx;
        rx = LMIN(rx, w / 2); ry = LMIN(ry, h / 2);
        if (rx > 0 && ry > 0) {
            pb_move(p, x + rx, y); pb_line(p, x + w - rx, y); arc(p, x + w - rx, y, rx, ry, 0, 0, 1, x + w, y + ry);
            pb_line(p, x + w, y + h - ry); arc(p, x + w, y + h - ry, rx, ry, 0, 0, 1, x + w - rx, y + h);
            pb_line(p, x + rx, y + h); arc(p, x + rx, y + h, rx, ry, 0, 0, 1, x, y + h - ry);
            pb_line(p, x, y + ry); arc(p, x, y + ry, rx, ry, 0, 0, 1, x + rx, y);
        } else { pb_move(p, x, y); pb_line(p, x + w, y); pb_line(p, x + w, y + h); pb_line(p, x, y + h); }
        pb_close(p);
    } else if (!strcmp(t, "circle") || !strcmp(t, "ellipse")) {
        float cx = fattr(n, "cx"), cy = fattr(n, "cy"), rx = t[0] == 'c' ? fattr(n, "r") : fattr(n, "rx"), ry = t[0] == 'c' ? rx : fattr(n, "ry");
        if (rx <= 0 || ry <= 0) return;
        pb_move(p, cx + rx, cy); arc(p, cx + rx, cy, rx, ry, 0, 0, 1, cx - rx, cy); arc(p, cx - rx, cy, rx, ry, 0, 0, 1, cx + rx, cy); pb_close(p);
    } else if (!strcmp(t, "polygon") || !strcmp(t, "polyline")) {
        const char *s = node_attr(n, "points"); float x, y; bool first = true;
        while (s && num(&s, &x) && num(&s, &y)) { if (first) pb_move(p, x, y); else pb_line(p, x, y); first = false; }
        if (t[4] == 'g') pb_close(p);
    } else if (!strcmp(t, "line")) { pb_move(p, fattr(n, "x1"), fattr(n, "y1")); pb_line(p, fattr(n, "x2"), fattr(n, "y2")); }
}

static bool attr_hidden(const Node *n) {
    const char *v;
    if (n->style && n->style->display == D_NONE) return true;
    if ((v = node_attr(n, "display")) && !strcmp(v, "none")) return true;
    if ((v = node_attr(n, "style")) && strstr(v, "display: none")) return true;
    return (v = node_attr(n, "style")) && strstr(v, "display:none");
}

static const Node *url_ref(const Node *n, const char *v) {
    char id[128]; const char *h = strchr(v, '#'), *e;
    if (!h || !n->doc) return NULL;
    h++; e = h; while (*e && *e != ')' && *e != '"' && *e != '\'') e++;
    if (e == h || e - h >= (int)sizeof id) return NULL;
    memcpy(id, h, (size_t)(e - h)); id[e - h] = 0;
    return doc_get_element_by_id(n->doc, id);
}

static bool has_geometry(const Node *n, int depth) {
    if (depth > 16 || attr_hidden(n)) return false;
    const char *t = n->tag, *v;
    if (!strcmp(t, "use")) { const Node *r = (v = node_attr(n, "href")) || (v = node_attr(n, "xlink:href")) ? url_ref(n, v) : NULL; return r && has_geometry(r, depth + 1); }
    if (!strcmp(t, "path")) return (v = node_attr(n, "d")) && *skipws(v);
    if (!strcmp(t, "rect")) return fattr(n, "width") > 0 && fattr(n, "height") > 0;
    if (!strcmp(t, "circle")) return fattr(n, "r") > 0;
    if (!strcmp(t, "ellipse")) return fattr(n, "rx") > 0 && fattr(n, "ry") > 0;
    if (!strcmp(t, "polygon") || !strcmp(t, "polyline") || !strcmp(t, "line") || !strcmp(t, "text") || !strcmp(t, "image")) return true;
    for (const Node *k = n->first; k; k = k->next)
        if (k->type == NODE_ELEMENT && has_geometry(k, depth + 1)) return true;
    return false;
}

typedef struct { Color fill, stroke; float sw, op, fop, sop; bool eo, used; const Node *fg, *sg; } Paint;

static Color attr_color(const Node *n, const char *k, Color def, Color cur) {
    const char *v = node_attr(n, k); Color c;
    if (!v) return def;
    if (str_ieq(v, "none")) return 0;
    return css_parse_color(v, &c, cur) ? c : def;
}

/* A declaration from the style attribute, which overrides the presentation attribute of the same name */
static const char *prop(const Node *n, const char *name, char *buf, size_t bn) {
    const char *st = node_attr(n, "style"); size_t nl = strlen(name);
    for (const char *q = st; q && *q;) {
        while (*q == ';' || isspace((unsigned char)*q)) q++;
        const char *e = strchr(q, ';'); if (!e) e = q + strlen(q);
        const char *c = q; while (c < e && *c != ':') c++;
        const char *ke = c; while (ke > q && isspace((unsigned char)ke[-1])) ke--;
        if (c < e && (size_t)(ke - q) == nl && !strncasecmp(q, name, nl)) {
            c++; while (c < e && isspace((unsigned char)*c)) c++;
            size_t l = LMIN((size_t)(e - c), bn - 1); memcpy(buf, c, l); buf[l] = 0;
            return buf;
        }
        q = e;
    }
    return node_attr(n, name);
}
static bool is_grad(const Node *g) { return g && (!strcmp(g->tag, "linearGradient") || !strcmp(g->tag, "radialGradient")); }
static const Node *href_of(const Node *n) { const char *h = node_attr(n, "href"); if (!h) h = node_attr(n, "xlink:href"); return h ? url_ref(n, h) : NULL; }
/* Gradient attributes inherit along the href chain */
static const char *gattr(const Node *g, const char *k) {
    for (int i = 0; g && i < 8; i++, g = href_of(g)) { const char *v = node_attr(g, k); if (v) return v; }
    return NULL;
}
static float glen(const char *v, float def, float ref) { if (!v) return def; float f = (float)atof(v); return strchr(v, '%') ? f / 100 * ref : f; }

static bool make_src(Src *s, const Node *g, Color solid, const Ctx *c, const PathB *p) {
    memset(s, 0, sizeof *s);
    if (!g) { s->col = solid; return true; }
    const Node *sg = g;
    for (int i = 0; sg && i < 8; i++, sg = href_of(sg)) {
        bool any = false; for (const Node *k = sg->first; k; k = k->next) if (k->type == NODE_ELEMENT && !strcmp(k->tag, "stop")) any = true;
        if (any) break;
    }
    float off[32]; Color col[32]; int n = 0; char buf[128];
    for (const Node *k = sg ? sg->first : NULL; k && n < 32; k = k->next) {
        if (k->type != NODE_ELEMENT || strcmp(k->tag, "stop")) continue;
        const char *v = node_attr(k, "offset"); float o = glen(v, 0, 1);
        o = LCLAMP(o, 0.f, 1.f); if (n && o < off[n - 1]) o = off[n - 1];
        Color cc = RGBA(0, 0, 0, 255);
        if ((v = prop(k, "stop-color", buf, sizeof buf))) css_parse_color(v, &cc, RGBA(0, 0, 0, 255));
        float so = (v = prop(k, "stop-opacity", buf, sizeof buf)) ? LCLAMP((float)atof(v), 0.f, 1.f) : 1;
        off[n] = o; col[n++] = (cc & 0xffffff) | ((uint32_t)(COLOR_A(cc) * so + 0.5f) << 24);
    }
    if (!n) return false;
    if (n == 1) { s->col = col[0]; return true; }
    const char *v; bool bb = !(v = gattr(g, "gradientUnits")) || strcmp(v, "userSpaceOnUse");
    float rx = bb ? 1 : c->vw, ry = bb ? 1 : c->vh, rr = bb ? 1 : sqrtf((c->vw * c->vw + c->vh * c->vh) / 2);
    if (g->tag[0] == 'l') {
        s->kind = 1;
        s->x1 = glen(gattr(g, "x1"), 0, rx); s->y1 = glen(gattr(g, "y1"), 0, ry);
        s->x2 = glen(gattr(g, "x2"), rx, rx); s->y2 = glen(gattr(g, "y2"), 0, ry);
    } else {   /* focal point (fx, fy) is approximated by the centre */
        s->kind = 2;
        s->x1 = glen(gattr(g, "cx"), 0.5f * rx, rx); s->y1 = glen(gattr(g, "cy"), 0.5f * ry, ry); s->r = glen(gattr(g, "r"), 0.5f * rr, rr);
    }
    Mat G = p->m;
    if (bb) {
        Mat iv = minv(p->m); float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (int i = 0; i < p->xy.n / 2; i++) {
            float dx = p->xy.v[2 * i], dy = p->xy.v[2 * i + 1], ux = iv.a * dx + iv.c * dy + iv.e, uy = iv.b * dx + iv.d * dy + iv.f;
            x0 = LMIN(x0, ux); y0 = LMIN(y0, uy); x1 = LMAX(x1, ux); y1 = LMAX(y1, uy);
        }
        if (x1 - x0 <= 0 || y1 - y0 <= 0) return false;
        G = mmul(G, (Mat){ x1 - x0, 0, 0, y1 - y0, x0, y0 });
    }
    if ((v = gattr(g, "gradientTransform"))) G = parse_transform(v, G);
    s->inv = minv(G);
    s->spread = (v = gattr(g, "spreadMethod")) ? !strcmp(v, "reflect") ? 1 : !strcmp(v, "repeat") ? 2 : 0 : 0;
    for (int i = 0, j = 0; i < 256; i++) {
        float t = i / 255.f;
        while (j < n - 2 && t >= off[j + 1]) j++;
        if (t <= off[0]) { s->lut[i] = col[0]; continue; }
        if (t >= off[n - 1]) { s->lut[i] = col[n - 1]; continue; }
        float d = off[j + 1] - off[j], f = d > 0 ? LCLAMP((t - off[j]) / d, 0.f, 1.f) : 1;
        Color a = col[j], b = col[j + 1];
#define LERP(sh) ((uint32_t)(((a >> sh) & 255) + (((float)((b >> sh) & 255) - (float)((a >> sh) & 255)) * f) + 0.5f) << sh)
        s->lut[i] = LERP(24) | LERP(16) | LERP(8) | LERP(0);
#undef LERP
    }
    return true;
}

/* fill/stroke: url(#gradient) sets the gradient, any other value clears it; an unresolvable url paints nothing */
static void paint_server(const Node *k, const char *name, const Node **g, Color *col) {
    char buf[256]; const char *v = prop(k, name, buf, sizeof buf);
    if (!v) return;
    if (!strstr(v, "url(")) { *g = NULL; return; }
    const Node *r = url_ref(k, v);
    *g = is_grad(r) ? r : NULL;
    if (!*g) *col = 0;
}

static void clip_add(Ctx *c, const Node *n, Mat m, float *dst, int depth) {
    if (depth > 8) return;
    for (const Node *k = n->first; k; k = k->next) {
        if (k->type != NODE_ELEMENT || attr_hidden(k)) continue;
        const char *v; char buf[32];
        Mat km = (v = node_attr(k, "transform")) ? parse_transform(v, m) : m;
        const Node *t = k;
        if (!strcmp(k->tag, "use")) {
            if (!(t = href_of(k))) continue;
            km = mmul(km, (Mat){ 1, 0, 0, 1, fattr(k, "x"), fattr(k, "y") });
            if ((v = node_attr(t, "transform"))) km = parse_transform(v, km);
        }
        PathB p = { 0 }; p.m = km; p.sc = sqrtf(fabsf(km.a * km.d - km.b * km.c)); build_shape(&p, t);
        if (p.xy.n >= 4) { acc_path(c, &p); v = prop(t, "clip-rule", buf, sizeof buf); cover(c, dst, v && !strcmp(v, "evenodd")); }
        free(p.xy.v); free(p.st.v); free(p.cl.v);
    }
}

static void walk(Ctx *c, const Node *n, Mat m, Paint pt, int depth);

static void draw_el(Ctx *c, const Node *k, Mat m, Paint pt, int depth) {
    static const char *const skip[] = { "defs", "clipPath", "mask", "symbol", "linearGradient", "radialGradient", "pattern", "filter", "title", "desc", "style", "script", "foreignObject", "metadata", "marker", NULL };
    if (depth > 32) return;
    const char *t = k->tag;
    for (int i = 0; skip[i]; i++) if (!strcmp(t, skip[i])) return;
    Paint q = pt; const ComputedStyle *s = k->style; Color cur = s ? s->color : RGBA(0, 0, 0, 255);
    char pb[64];
    if (s) {
        if (s->display == D_NONE) return;
        if (!q.used || prop(k, "fill", pb, sizeof pb)) q.fill = s->fill;
        if (!q.used || prop(k, "stroke", pb, sizeof pb)) q.stroke = s->stroke;
        if (!q.used || prop(k, "stroke-width", pb, sizeof pb)) q.sw = s->stroke_width;
        q.op = pt.op * s->opacity;
    }
    else { const char *d = node_attr(k, "display"); if (d && !strcmp(d, "none")) return; q.fill = attr_color(k, "fill", q.fill, cur); const char *o = node_attr(k, "opacity"); if (o) q.op *= (float)atof(o); }
    if (!s || !COLOR_A(q.stroke)) q.stroke = attr_color(k, "stroke", q.stroke, cur);
    if (s && q.used && (!strcmp(t, "use") || prop(k, "fill", pb, sizeof pb))) q.fill = attr_color(k, "fill", q.fill, cur);
    paint_server(k, "fill", &q.fg, &q.fill);
    paint_server(k, "stroke", &q.sg, &q.stroke);
    const char *av; char buf[256];
    if (s && (av = node_attr(k, "opacity"))) q.op *= (float)atof(av);
    if ((av = node_attr(k, "visibility")) && !strcmp(av, "hidden")) return;
    if (s && (av = node_attr(k, "display")) && !strcmp(av, "none")) return;
    if (q.op <= 0.002f) return;
    if ((av = node_attr(k, "mask")) && strstr(av, "url(")) { const Node *mk = url_ref(k, av); if (mk && !has_geometry(mk, 0)) return; }
    if (node_attr(k, "stroke-width") && (!s || s->stroke_width == 1)) q.sw = fattr(k, "stroke-width");
    const char *v;
    if ((v = node_attr(k, "fill-rule"))) q.eo = !strcmp(v, "evenodd");
    if ((v = node_attr(k, "fill-opacity"))) q.fop = (float)atof(v);
    if ((v = node_attr(k, "stroke-opacity"))) q.sop = (float)atof(v);
    Mat km = (v = node_attr(k, "transform")) ? parse_transform(v, m) : m;
    float *old = c->clip, *mask = NULL;
    if ((v = prop(k, "clip-path", buf, sizeof buf)) && strstr(v, "url(")) {
        const Node *cp = url_ref(k, v);
        if (cp && !strcmp(cp->tag, "clipPath")) {
            size_t np = (size_t)c->w * (size_t)c->h;
            mask = xcalloc(np, sizeof(float));
            clip_add(c, cp, (v = node_attr(cp, "transform")) ? parse_transform(v, km) : km, mask, 0);
            if (old) for (size_t i = 0; i < np; i++) mask[i] *= old[i];
            c->clip = mask;
        }
    }
    if (!strcmp(t, "g") || !strcmp(t, "a") || !strcmp(t, "svg") || !strcmp(t, "switch")) walk(c, k, km, q, depth + 1);
    else if (!strcmp(t, "use")) {
        const Node *r = href_of(k);
        if (r && r != k) {
            Mat um = mmul(km, (Mat){ 1, 0, 0, 1, fattr(k, "x"), fattr(k, "y") });
            q.used = true;
            if (!strcmp(r->tag, "symbol") || !strcmp(r->tag, "svg")) {
                float vx, vy, vw, vh, w = fattr(k, "width"), h = fattr(k, "height"); const char *vb = node_attr(r, "viewBox");
                if (vb && w > 0 && h > 0 && sscanf(vb, "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &vx, &vy, &vw, &vh) == 4 && vw > 0 && vh > 0) {
                    float kk = LMIN(w / vw, h / vh);
                    um = mmul(um, (Mat){ kk, 0, 0, kk, (w - vw * kk) / 2 - vx * kk, (h - vh * kk) / 2 - vy * kk });
                }
                walk(c, r, um, q, depth + 1);
            } else draw_el(c, r, um, q, depth + 1);
        }
    } else {
        PathB p = { 0 }; p.m = km; p.sc = sqrtf(fabsf(km.a * km.d - km.b * km.c));
        build_shape(&p, k);
        if (p.xy.n >= 4) {
            Src sr;
            if ((q.fg || COLOR_A(q.fill)) && strcmp(t, "line") && make_src(&sr, q.fg, q.fill, c, &p)) fill_path(c, &p, &sr, q.op * q.fop, q.eo);
            if ((q.sg || COLOR_A(q.stroke)) && q.sw > 0 && make_src(&sr, q.sg, q.stroke, c, &p)) stroke_path(c, &p, &sr, LMAX(0.5f, q.sw * p.sc / 2), q.op * q.sop);
        }
        free(p.xy.v); free(p.st.v); free(p.cl.v);
    }
    if (mask) { c->clip = old; free(mask); }
}

static void walk(Ctx *c, const Node *n, Mat m, Paint pt, int depth) {
    if (depth > 32) return;
    for (const Node *k = n->first; k; k = k->next)
        if (k->type == NODE_ELEMENT && k->ns == NS_SVG) draw_el(c, k, m, pt, depth);
}

static uint64_t hmix(uint64_t h, const char *s) { if (!s) return h * 1099511628211ull; while (*s) h = (h ^ (uint8_t)*s++) * 1099511628211ull; return (h ^ 0xff) * 1099511628211ull; }
static uint64_t tree_hash(const Node *n, uint64_t h, int depth) {
    static const char *const keys[] = { "d", "points", "x", "y", "width", "height", "r", "rx", "ry", "cx", "cy", "x1", "y1", "x2", "y2", "transform", "fill", "stroke", "stroke-width", "viewBox", "opacity", "fill-rule", "display", "visibility", "fill-opacity", "stroke-opacity", "style", "mask", "href", "xlink:href", "clip-path", "offset", "stop-color", "stop-opacity", "gradientTransform", "gradientUnits", NULL };
    if (depth > 32) return h;
    for (const Node *k = n->first; k; k = k->next) {
        if (k->type != NODE_ELEMENT) continue;
        h = hmix(h, k->tag);
        for (int i = 0; keys[i]; i++) h = hmix(h, node_attr(k, keys[i]));
        if (k->style) h = (h ^ k->style->fill ^ ((uint64_t)k->style->stroke << 32) ^ ((uint64_t)k->style->display << 8)) * 1099511628211ull;
        h = tree_hash(k, h, depth + 1);
    }
    return h;
}

typedef struct { Image *im; uint64_t key; } SvgCache;
static Image *graveyard[64]; static int grave_i;
static void bury(Image *im) { image_unref(graveyard[grave_i]); graveyard[grave_i] = im; grave_i = (grave_i + 1) % 64; }
static void svgc_free(void *p) { SvgCache *sc = p; if (sc->im) bury(sc->im); free(sc); }

Image *svg_image(Node *n, float cw, float ch, float dpr) {
    if (cw < 0.5f || ch < 0.5f) return NULL;
    float k = dpr; if (cw * k > 1024 || ch * k > 1024) k = 1024 / LMAX(cw, ch);
    int W = (int)ceilf(cw * k), H = (int)ceilf(ch * k);
    const ComputedStyle *s = n->style;
    uint64_t key = tree_hash(n, 1469598103934665603ull, 0);
    key = hmix(key, node_attr(n, "viewBox")); key = hmix(key, node_attr(n, "preserveAspectRatio"));
    key = (key ^ (uint64_t)W ^ ((uint64_t)H << 16) ^ (s ? (uint64_t)s->fill << 32 ^ s->stroke : 0)) * 1099511628211ull;
    SvgCache *sc = n->ext_free == svgc_free ? n->ext : NULL;
    if (sc && sc->im && sc->key == key) return sc->im;
    if (!sc) { if (n->ext) return NULL; sc = xcalloc(1, sizeof *sc); n->ext = sc; n->ext_free = svgc_free; }
    Image *im = xcalloc(1, sizeof *im); im->w = W; im->h = H; im->refs = 1; im->px = xcalloc((size_t)W * (size_t)H, 4);
    float vx = 0, vy = 0, vw = cw, vh = ch; const char *vb = node_attr(n, "viewBox"), *par = node_attr(n, "preserveAspectRatio");
    if (vb && sscanf(vb, "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &vx, &vy, &vw, &vh) == 4 && vw > 0 && vh > 0) {} else { vx = vy = 0; vw = cw; vh = ch; }
    float kx = W / vw, ky = H / vh, tx = 0, ty = 0;
    if (!par || strncmp(par, "none", 4)) {
        bool slice = par && strstr(par, "slice"); float kk = slice ? LMAX(kx, ky) : LMIN(kx, ky);
        float ax = !par || strstr(par, "xMid") ? 0.5f : strstr(par, "xMax") ? 1 : 0, ay = !par || strstr(par, "YMid") ? 0.5f : strstr(par, "YMax") ? 1 : 0;
        tx = (W - vw * kk) * ax; ty = (H - vh * kk) * ay; kx = ky = kk;
    }
    Mat m = { kx, 0, 0, ky, tx - vx * kx, ty - vy * ky };
    Ctx c = { xcalloc((size_t)(W + 2) * (size_t)H, sizeof(float)), W, H, im->px, NULL, vw, vh };
    Paint pt = { s ? s->fill : RGBA(0, 0, 0, 255), s ? s->stroke : 0, s ? s->stroke_width : 1, 1, 1, 1, false };
    walk(&c, n, m, pt, 0);
    free(c.acc);
    if (sc->im) bury(sc->im);
    sc->im = im; sc->key = key;
    return im;
}
