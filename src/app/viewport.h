#ifndef LUMEN_APP_VIEWPORT_H
#define LUMEN_APP_VIEWPORT_H
#include <stdbool.h>
#include <math.h>

/* Chrome/page coordinates are relative to this safe region; SDL events remain
   window-relative. The rectangle is snapped inward to framebuffer pixels so the
   canvas subview and pointer translation use exactly the same origin. */
typedef struct {
    int window_w, window_h;
    int px_x, px_y, px_w, px_h;
    float scale, x, y, w, h;
} UiViewport;

static inline UiViewport ui_viewport(int ww, int wh, int pw, int ph,
                                    int sx, int sy, int sw, int sh) {
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;
    UiViewport v = { .window_w = ww, .window_h = wh, .scale = (float)pw / (float)ww };
    double left = fmax(0, sx), top = fmax(0, sy);
    double right = fmin(ww, (double)sx + sw), bottom = fmin(wh, (double)sy + sh);
    if (sw <= 0 || sh <= 0 || right <= left || bottom <= top) {
        left = top = 0; right = ww; bottom = wh;
    }
    v.px_x = (int)fmin(pw - 1, ceil(left * v.scale));
    v.px_y = (int)fmin(ph - 1, ceil(top * v.scale));
    int ex = right == ww ? pw : (int)fmin(pw, floor(right * v.scale));
    int ey = bottom == wh ? ph : (int)fmin(ph, floor(bottom * v.scale));
    v.px_w = ex > v.px_x ? ex - v.px_x : 1;
    v.px_h = ey > v.px_y ? ey - v.px_y : 1;
    v.x = (float)v.px_x / v.scale; v.y = (float)v.px_y / v.scale;
    v.w = (float)v.px_w / v.scale; v.h = (float)v.px_h / v.scale;
    return v;
}

static inline bool ui_viewport_point(const UiViewport *v, float wx, float wy, float *x, float *y) {
    *x = wx - v->x; *y = wy - v->y;
    return *x >= 0 && *y >= 0 && *x < v->w && *y < v->h;
}
#endif
