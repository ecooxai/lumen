#ifndef LUMEN_AFFINE_H
#define LUMEN_AFFINE_H
#include <float.h>
#include <math.h>
#include <stdbool.h>

/* CSS column-vector affine matrices. Keep the shared geometry in double:
 * a finite float transform can have a very large, but still finite, inverse. */
typedef struct Affine2D { double a, b, c, d, e, f; } Affine2D;
typedef struct AffinePoint { double x, y; } AffinePoint;
static inline Affine2D affine_identity(void) { return (Affine2D){1, 0, 0, 1, 0, 0}; }
static inline Affine2D affine_translation(double x, double y) { return (Affine2D){1, 0, 0, 1, x, y}; }
static inline bool affine_finite(Affine2D m) {
    return isfinite(m.a) && isfinite(m.b) && isfinite(m.c) && isfinite(m.d) && isfinite(m.e) && isfinite(m.f);
}
static inline Affine2D affine_multiply(Affine2D outer, Affine2D inner) {
    return (Affine2D){
        outer.a * inner.a + outer.c * inner.b,
        outer.b * inner.a + outer.d * inner.b,
        outer.a * inner.c + outer.c * inner.d,
        outer.b * inner.c + outer.d * inner.d,
        outer.a * inner.e + outer.c * inner.f + outer.e,
        outer.b * inner.e + outer.d * inner.f + outer.f
    };
}
static inline AffinePoint affine_point(Affine2D m, double x, double y) {
    return (AffinePoint){m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f};
}
static inline bool affine_inverse(Affine2D m, Affine2D *out) {
    if (!affine_finite(m)) return false;
    double k = fmax(fmax(fabs(m.a), fabs(m.b)), fmax(fabs(m.c), fabs(m.d)));
    if (!(k > 0)) return false;
    double a = m.a / k, b = m.b / k, c = m.c / k, d = m.d / k;
    double det = a * d - b * c;
    if (!isfinite(det) || det == 0) return false;
    Affine2D inverse = {d / det / k, -b / det / k, -c / det / k, a / det / k, 0, 0};
    inverse.e = -(inverse.a * m.e + inverse.c * m.f);
    inverse.f = -(inverse.b * m.e + inverse.d * m.f);
    if (!affine_finite(inverse)) return false;
    *out = inverse;
    return true;
}
/* Change the coordinate origin in both source and destination by (x,y).
 * This is also the conjugation needed by scroll-normalized paint signatures. */
static inline Affine2D affine_conjugate_translation(Affine2D m, double x, double y) {
    m.e += x - m.a * x - m.c * y;
    m.f += y - m.b * x - m.d * y;
    return m;
}
static inline bool affine_bounds(Affine2D m, const double in[4], double out[4]) {
    if (!affine_finite(m) || !(in[0] <= in[2]) || !(in[1] <= in[3])) return false;
    AffinePoint points[4] = {
        affine_point(m, in[0], in[1]), affine_point(m, in[2], in[1]),
        affine_point(m, in[2], in[3]), affine_point(m, in[0], in[3])
    };
    out[0] = out[2] = points[0].x; out[1] = out[3] = points[0].y;
    for (int i = 0; i < 4; i++) {
        if (!isfinite(points[i].x) || !isfinite(points[i].y)) return false;
        out[0] = fmin(out[0], points[i].x); out[2] = fmax(out[2], points[i].x);
        out[1] = fmin(out[1], points[i].y); out[3] = fmax(out[3], points[i].y);
    }
    return true;
}
static inline float affine_float(double value) {
    return (float)fmax(-FLT_MAX, fmin(FLT_MAX, value));
}
#endif
