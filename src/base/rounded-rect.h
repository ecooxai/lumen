/* Shared used border-radius geometry. Computed values keep their own axes.
 * Insets preserve an outer ellipse's center; only the outer box normalizes. */
#ifndef LUMEN_ROUNDED_RECT_H
#define LUMEN_ROUNDED_RECT_H
#include <stdbool.h>
#include <float.h>
#include <math.h>

static inline float rounded_nonnegative(float value) {
    return !(value > 0) ? 0 : isfinite(value) ? value : FLT_MAX;
}
static inline bool rounded_axes_differ(const float x[4], const float y[4]) {
    for (int i = 0; i < 4; i++) if (x[i] != y[i]) return true;
    return false;
}
static inline void rounded_square_corners(float x[4], float y[4]) {
    for (int i = 0; i < 4; i++) {
        x[i] = rounded_nonnegative(x[i]); y[i] = rounded_nonnegative(y[i]);
        if (!x[i] || !y[i]) x[i] = y[i] = 0;
    }
}
/* Scale the original nonnegative axes before squaring zero-axis corners:
 * an otherwise-square corner's nonzero axis still participates in overlap. */
static inline void rounded_radii_normalize(float width, float height, float x[4], float y[4]) {
    width = rounded_nonnegative(width); height = rounded_nonnegative(height);
    for (int i = 0; i < 4; i++) {
        x[i] = rounded_nonnegative(x[i]); y[i] = rounded_nonnegative(y[i]);
    }
    double scale = 1, sums[4] = {(double)x[0] + x[1], (double)x[3] + x[2],
                                (double)y[0] + y[3], (double)y[1] + y[2]};
    for (int i = 0; i < 4; i++) {
        double side = i < 2 ? width : height;
        if (sums[i] > side) scale = fmin(scale, side / sums[i]);
    }
    if (scale < 1) for (int i = 0; i < 4; i++) {
        x[i] = (float)(x[i] * scale); y[i] = (float)(y[i] * scale);
    }
    rounded_square_corners(x, y);
}
static inline void rounded_radii_inset(const float ox[4], const float oy[4],
                                      const float side[4], float x[4], float y[4]) {
    static const int horizontal[4] = {3, 1, 1, 3}, vertical[4] = {0, 0, 2, 2};
    for (int i = 0; i < 4; i++) {
        x[i] = fmaxf(0, ox[i] - side[horizontal[i]]);
        y[i] = fmaxf(0, oy[i] - side[vertical[i]]);
    }
    rounded_square_corners(x, y);
}
static inline bool rounded_rect_contains(double px, double py, double x0, double y0,
                                         double x1, double y1, const float rx[4],
                                         const float ry[4]) {
    if (!(px >= x0 && px < x1 && py >= y0 && py < y1)) return false;
    for (int i = 0; i < 4; i++) {
        double a = rx[i], b = ry ? ry[i] : a;
        if (!(a > 0 && b > 0)) continue;
        bool left = i == 0 || i == 3, top = i < 2;
        double u = left ? x0 + a - px : px - (x1 - a);
        double v = top ? y0 + b - py : py - (y1 - b);
        if (u > 0 && v > 0 && hypot(u / a, v / b) > 1) return false;
    }
    return true;
}
/* The old circular midpoint SDF is exact for this common bounded case. */
static inline bool rounded_rect_general(float width, float height,
                                        const float x[4], const float y[4]) {
    for (int i = 0; i < 4; i++)
        if ((y && x[i] != y[i]) || x[i] > width * .5f || x[i] > height * .5f) return true;
    return false;
}
/* Signed distance to a quadrant ellipse. Far from the requested coverage band
 * a Lipschitz lower bound is enough to saturate coverage and avoids root work.
 * Near the edge, a monotone closest-point equation has one bracketed root.
 * Axis cases avoid the pole; all intermediates are double and iteration bounded. */
static inline double rounded_ellipse_distance(double u, double v, double a, double b,
                                               double band) {
    if (a < b) { double t = a; a = b; b = t; t = u; u = v; v = t; }
    if (!(a > 0 && b > 0)) return hypot(u, v);
    if (a == b) return hypot(u, v) - a;
    double norm = hypot(u / a, v / b), lower = (norm - 1) * b;
    if (fabs(lower) > band) return lower;
    if (norm == 1) return 0;
    bool inside = norm < 1;
    if (u == 0) return v - b;
    double aa = a * a, bb = b * b, delta = aa - bb, qx, qy;
    if (v == 0) {
        if (a * u < delta) {
            qx = aa * u / delta;
            qy = b * sqrt(fmax(0, 1 - (qx / a) * (qx / a)));
        } else { qx = a; qy = 0; }
    } else {
        double lo = inside ? b * v : bb, hi = inside ? bb : bb + hypot(a * u, b * v);
        double t = (lo + hi) * .5;
        for (int i = 0; i < 48; i++) {
            t = lo > 0 && hi > lo * 4 ? sqrt(lo) * sqrt(hi) : lo + (hi - lo) * .5;
            if (!(t > lo && t < hi)) break;
            double ax = a * u / (t + delta), by = b * v / t;
            double value = ax * ax + by * by;
            if (value > 1) lo = t; else hi = t;
            if (fabs(value - 1) < 1e-12) break;
        }
        if (!(t > 0)) { qx = a; qy = 0; }
        else { qx = aa * u / (t + delta); qy = bb * v / t; }
    }
    /* A residual fallback stays on the finite ellipse even at a precision
       limit; geometric bracketing above avoids the tiny-minor-axis pole. */
    double residual = hypot(qx / a, qy / b);
    if (isfinite(residual) && residual > 0 && fabs(residual - 1) > 1e-10) {
        qx /= residual; qy /= residual;
    }
    double distance = hypot(u - qx, v - qy);
    return inside ? -distance : distance;
}
static inline float rounded_rect_distance(float px, float py, float x0, float y0,
                                          float x1, float y1, const float rx[4],
                                          const float ry[4], float band) {
    if (!(x1 > x0 && y1 > y0)) return FLT_MAX;
    double dx = fmax((double)x0 - px, (double)px - x1);
    double dy = fmax((double)y0 - py, (double)py - y1);
    double distance = fmin(fmax(dx, dy), 0) + hypot(fmax(dx, 0), fmax(dy, 0));
    for (int i = 0; i < 4; i++) {
        double a = rx[i], b = ry ? ry[i] : a;
        if (!(a > 0 && b > 0)) continue;
        bool left = i == 0 || i == 3, top = i < 2;
        double u = left ? (double)x0 + a - px : (double)px - ((double)x1 - a);
        double v = top ? (double)y0 + b - py : (double)py - ((double)y1 - b);
        if (u >= 0 && v >= 0)
            distance = fmax(distance, rounded_ellipse_distance(u, v, a, b, band));
    }
    return (float)distance;
}
#endif
