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
        paint_regions_add(&damage, bounds);
    }
    return damage;
}
#endif
