/* Included after DItem. Bounds used to crop an affine source window must
 * contain actual ink, not a font-size or fragment-box approximation. */
#ifndef LUMEN_AFFINE_INK_H
#define LUMEN_AFFINE_INK_H

typedef struct DTransform {
    Affine2D matrix;
    double source[4], bounds[4];
    DItem *items;
    int count, opacity_depth;
    float ink_scale;
    bool ink_valid;
} DTransform;

/* Exact bitmap extent in device pixels, using the same rounded global glyph
 * origin as text(). The bitmap already includes synthesis and color scaling. */
static inline bool paint_text_bounds(const DItem *it, float scale, double out[4]) {
    out[0] = out[1] = INFINITY; out[2] = out[3] = -INFINITY;
    if (!(scale > 0) || !isfinite(scale)) return false;
    for (int i = 0; i < it->ng; i++) {
        const DGlyph *dg = &it->g[i];
        if (!dg->font) continue;
        const Glyph *glyph = font_glyph(dg->font, dg->gid, scale);
        if (!glyph || glyph->w <= 0 || glyph->h <= 0) continue;
        double x = round((double)dg->x * scale) + glyph->left;
        double y = round((double)dg->y * scale) - glyph->top;
        out[0] = fmin(out[0], x); out[1] = fmin(out[1], y);
        out[2] = fmax(out[2], x + glyph->w); out[3] = fmax(out[3], y + glyph->h);
    }
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]) && isfinite(out[3]) && out[2] > out[0] && out[3] > out[1];
}

static inline bool paint_item_bounds(const DItem *it, float scale, double out[4]);

/* Cached display-list bounds serve damage at its actual canvas scale. A
 * different raster scale is queried without modifying the shared list, so
 * independently rastered bands and iframe/test canvases remain safe. */
static inline bool paint_transform_ink(const DTransform *group, float scale,
                                       double source[4], double bounds[4]) {
    if (!(scale > 0) || !isfinite(scale)) scale = 1;
    if (group->ink_scale == scale) {
        memcpy(source, group->source, sizeof(double) * 4);
        memcpy(bounds, group->bounds, sizeof(double) * 4);
        return group->ink_valid;
    }
    source[0] = source[1] = INFINITY; source[2] = source[3] = -INFINITY;
    for (int i = 0; i < group->count; i++) {
        double ink[4];
        if (!paint_item_bounds(&group->items[i], scale, ink)) continue;
        source[0] = fmin(source[0], ink[0]); source[1] = fmin(source[1], ink[1]);
        source[2] = fmax(source[2], ink[2]); source[3] = fmax(source[3], ink[3]);
    }
    if (!(source[2] > source[0] && source[3] > source[1])) {
        memset(source, 0, sizeof(double) * 4); memset(bounds, 0, sizeof(double) * 4); return false;
    }
    /* Bilinear support is expanded in source pixels before transformation.
       A large scale must not clip the kernel to one destination pixel. */
    double padding = 1.0 / scale;
    double kernel[4] = {source[0] - padding, source[1] - padding,
                        source[2] + padding, source[3] + padding};
    if (!affine_bounds(group->matrix, kernel, bounds)) {
        memset(bounds, 0, sizeof(double) * 4); return false;
    }
    return bounds[2] > bounds[0] && bounds[3] > bounds[1];
}

/* CSS-pixel ink bounds at a specified raster scale. */
static inline bool paint_item_bounds(const DItem *it, float scale, double out[4]) {
    if (it->op > DO_TRANSFORM) return false;
    if (!(scale > 0) || !isfinite(scale)) scale = 1;
    if (it->op == DO_TRANSFORM) {
        double source[4];
        return it->transform && paint_transform_ink(it->transform, scale, source, out);
    }
    if (it->op == DO_TEXT) {
        if (!paint_text_bounds(it, scale, out)) return false;
        for (int k = 0; k < 4; k++) out[k] /= scale;
    } else {
        out[0] = it->x; out[1] = it->y;
        out[2] = (double)it->x + it->w; out[3] = (double)it->y + it->h;
        if (it->op == DO_SHADOW) {
            double pad = fabs((double)it->spread) + fmax(.5 / scale, it->blur);
            out[0] -= pad; out[1] -= pad; out[2] += pad; out[3] += pad;
        }
    }
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]) && isfinite(out[3]) && out[2] > out[0] && out[3] > out[1];
}
#endif
