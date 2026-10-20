#ifndef LUMEN_PAINT_REGIONS_H
#define LUMEN_PAINT_REGIONS_H
#include <math.h>
#include <string.h>

/* Keep separated fixed headers/footers separate. Merge overlapping row spans
   because texture uploads operate on whole rows. Overflow is conservative. */
#define PAINT_REGION_MAX 8
typedef struct { float rect[PAINT_REGION_MAX][4]; int count; } PaintRegions;

static inline void paint_regions_add(PaintRegions *regions, const float bounds[4]) {
    float box[4]; memcpy(box, bounds, sizeof box);
    if (!(box[2] > box[0] && box[3] > box[1])) return;
    for (int i = 0; i < regions->count;) {
        float *other = regions->rect[i];
        if (box[1] <= other[3] && box[3] >= other[1]) {
            box[0] = fminf(box[0], other[0]); box[1] = fminf(box[1], other[1]);
            box[2] = fmaxf(box[2], other[2]); box[3] = fmaxf(box[3], other[3]);
            memcpy(other, regions->rect[--regions->count], sizeof box);
            i = 0;
        } else i++;
    }
    if (regions->count == PAINT_REGION_MAX) {
        for (int i = 0; i < regions->count; i++) {
            const float *other = regions->rect[i];
            box[0] = fminf(box[0], other[0]); box[1] = fminf(box[1], other[1]);
            box[2] = fmaxf(box[2], other[2]); box[3] = fmaxf(box[3], other[3]);
        }
        regions->count = 0;
    }
    memcpy(regions->rect[regions->count++], box, sizeof box);
}

/* Fixed boxes: a header band and a full-height side column (YouTube's masthead and guide) touch but
   must not become one screen-sized box, or every scroll repaints and uploads the whole page. Two
   boxes merge only when their bounding box adds no area; overlapping ones may stay separate, as
   repainting or uploading a region twice is harmless. When full, the new box joins the region it
   grows least (the union covers both, so nothing is lost). Also used for changed items' damage. */
static inline float paint_region_area(const float *b) { return (b[2] - b[0]) * (b[3] - b[1]); }
static inline void paint_fixed_add(PaintRegions *regions, const float bounds[4]) {
    float box[4]; memcpy(box, bounds, sizeof box);
    if (!(box[2] > box[0] && box[3] > box[1])) return;
    for (int i = 0; i < regions->count;) {
        float *other = regions->rect[i];
        float u[4] = { fminf(box[0], other[0]), fminf(box[1], other[1]), fmaxf(box[2], other[2]), fmaxf(box[3], other[3]) };
        if (paint_region_area(u) <= paint_region_area(box) + paint_region_area(other)) {
            memcpy(box, u, sizeof box);
            memcpy(other, regions->rect[--regions->count], sizeof box);
            i = 0;
        } else i++;
    }
    if (regions->count == PAINT_REGION_MAX) {   /* full: join the region whose bounding box grows least */
        int best = 0; float growth = INFINITY;
        for (int i = 0; i < regions->count; i++) {
            const float *other = regions->rect[i];
            float u[4] = { fminf(box[0], other[0]), fminf(box[1], other[1]), fmaxf(box[2], other[2]), fmaxf(box[3], other[3]) };
            float g = paint_region_area(u) - paint_region_area(other);
            if (g < growth) { growth = g; best = i; }
        }
        const float *other = regions->rect[best];
        float u[4] = { fminf(box[0], other[0]), fminf(box[1], other[1]), fmaxf(box[2], other[2]), fmaxf(box[3], other[3]) };
        memmove(regions->rect[best], regions->rect[--regions->count], sizeof box);
        paint_fixed_add(regions, u);   /* may merge further; never full again */
        return;
    }
    memcpy(regions->rect[regions->count++], box, sizeof box);
}

/* Area covered by the regions plus one extra box (pass NULL for none), overlaps counted once. */
static inline double paint_regions_union_area(const PaintRegions *regions, const float *extra) {
    const float *r[PAINT_REGION_MAX + 1]; int n = 0;
    for (int i = 0; i < regions->count; i++) if (paint_region_area(regions->rect[i]) > 0) r[n++] = regions->rect[i];
    if (extra && paint_region_area(extra) > 0) r[n++] = extra;
    float xs[2 * (PAINT_REGION_MAX + 1)]; int nx = 0;
    for (int i = 0; i < n; i++) { xs[nx++] = r[i][0]; xs[nx++] = r[i][2]; }
    for (int i = 1; i < nx; i++) for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    double area = 0;
    for (int k = 0; k + 1 < nx; k++) {   /* per x slab: the union of the y spans of the boxes covering it */
        float x0 = xs[k], x1 = xs[k + 1]; if (!(x1 > x0)) continue;
        float y[PAINT_REGION_MAX + 1][2]; int m = 0;
        for (int i = 0; i < n; i++) if (r[i][0] <= x0 && r[i][2] >= x1) { y[m][0] = r[i][1]; y[m][1] = r[i][3]; m++; }
        for (int i = 1; i < m; i++) for (int j = i; j > 0 && y[j - 1][0] > y[j][0]; j--) { float t0 = y[j][0], t1 = y[j][1]; y[j][0] = y[j - 1][0]; y[j][1] = y[j - 1][1]; y[j - 1][0] = t0; y[j - 1][1] = t1; }
        double cover = 0; float top = -INFINITY;
        for (int i = 0; i < m; i++) { float a = fmaxf(y[i][0], top); if (y[i][1] > a) { cover += y[i][1] - a; top = y[i][1]; } }
        area += cover * (x1 - x0);
    }
    return area;
}

static inline PaintRegions paint_scroll_regions(const PaintRegions *fixed, float scale,
                                                int width, int height, int delta) {
    PaintRegions damage = {0};
    for (int i = 0; i < fixed->count; i++) {
        const float *box = fixed->rect[i];
        /* Clamp in floating point before conversion, including offscreen boxes
           whose old pixels move into the viewport after a scroll. */
        float bounds[4] = {
            fminf(width, fmaxf(0, floorf(box[0] * scale))),
            fminf(height, fmaxf(0, floorf(box[1] * scale) - (delta > 0 ? delta : 0))),
            fminf(width, fmaxf(0, ceilf(box[2] * scale))),
            fminf(height, fmaxf(0, ceilf(box[3] * scale) - (delta < 0 ? delta : 0)))
        };
        paint_fixed_add(&damage, bounds);
    }
    return damage;
}
#endif
