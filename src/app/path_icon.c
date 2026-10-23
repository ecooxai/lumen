#include "path_icon.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x0, y0, x1, y1; } Seg;
typedef struct { Seg *s; int n, cap; } Segs;

static void seg_add(Segs *v, float x0, float y0, float x1, float y1) {
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 64; v->s = realloc(v->s, (size_t)v->cap * sizeof *v->s); }
    v->s[v->n++] = (Seg){ x0, y0, x1, y1 };
}
static void arc_add(Segs *v, float cx, float cy, float r, float start, float sweep) {
    int steps = (int)(fabsf(sweep) / 10) + 2;
    float a0 = start * (float)M_PI / 180, d = sweep * (float)M_PI / 180 / steps;
    for (int i = 0; i < steps; i++) {
        float a = a0 + d * i, b = a + d;
        seg_add(v, cx + r * cosf(a), cy + r * sinf(a), cx + r * cosf(b), cy + r * sinf(b));
    }
}
static bool parse(const char *p, Segs *v) {
    float cx = 0, cy = 0, sx = 0, sy = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char op = *p++; float a[5]; int need = op == 'M' || op == 'L' ? 2 : op == 'O' ? 3 : op == 'A' ? 5 : 0;
        if (op != 'Z' && !need) return false;
        for (int i = 0; i < need; i++) { char *e; a[i] = strtof(p, &e); if (e == p) return false; p = e; }
        if (op == 'M') { cx = sx = a[0]; cy = sy = a[1]; }
        else if (op == 'L') { seg_add(v, cx, cy, a[0], a[1]); cx = a[0]; cy = a[1]; }
        else if (op == 'Z') { seg_add(v, cx, cy, sx, sy); cx = sx; cy = sy; }
        else if (op == 'O') arc_add(v, a[0], a[1], a[2], 0, 360);
        else arc_add(v, a[0], a[1], a[2], a[3], a[4]);
    }
    return true;
}
static float seg_dist(const Seg *s, float x, float y) {
    float dx = s->x1 - s->x0, dy = s->y1 - s->y0, l = dx * dx + dy * dy;
    float t = l > 0 ? ((x - s->x0) * dx + (y - s->y0) * dy) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = s->x0 + t * dx - x, ey = s->y0 + t * dy - y;
    return sqrtf(ex * ex + ey * ey);
}
bool path_icon_rgba(const char *path, int size, float stroke, uint32_t rgb, uint32_t *pixels) {
    if (!path || !pixels || size < 8 || size > 512) return false;
    Segs v = { 0 };
    if (!parse(path, &v) || !v.n) { free(v.s); return false; }
    float unit = size / 24.0f, half = stroke * unit / 2;
    for (int i = 0; i < v.n; i++) { v.s[i].x0 *= unit; v.s[i].y0 *= unit; v.s[i].x1 *= unit; v.s[i].y1 *= unit; }
    for (int row = 0; row < size; row++) for (int col = 0; col < size; col++) {
        float x = col + .5f, y = row + .5f, d = 1e9f;
        for (int i = 0; i < v.n && d > 0; i++) { float k = seg_dist(&v.s[i], x, y); if (k < d) d = k; }
        float c = half + .5f - d; c = c < 0 ? 0 : c > 1 ? 1 : c;
        uint32_t al = (uint32_t)(c * 255 + .5f);
        pixels[(size_t)row * size + col] = al << 24 | (((rgb >> 16) & 255) * al / 255) << 16 | (((rgb >> 8) & 255) * al / 255) << 8 | (rgb & 255) * al / 255;
    }
    free(v.s);
    return true;
}
