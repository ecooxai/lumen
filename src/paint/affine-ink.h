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
    if (it->transform) {
        double source[4];
        return paint_transform_ink(it->transform, scale, source, out);
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
/* Application-generated damage uses the same exact cache as transformed
 * raster ink. Ordinary preexisting text keeps its legacy estimate; flattened
 * resized text has its envelope refreshed at the actual canvas scale. */
static inline void paint_item_damage_bounds(const DItem *it, float *b) {
    if (it->transform) {
        for (int k = 0; k < 4; k++) b[k] = affine_float(it->transform->bounds[k] + (k < 2 ? -2 : 2));
        return;
    }
    float x0 = it->x, y0 = it->y, x1 = it->x + it->w, y1 = it->y + it->h, e = 2;
    if (it->op == DO_SHADOW) e += fabsf(it->spread) + fabsf(it->blur);
    if (it->op == DO_TEXT && !it->precise_text_ink) {
        x0 = y0 = 1e30f; x1 = y1 = -1e30f;
        for (int i = 0; i < it->ng; i++) { const DGlyph *g = &it->g[i]; float em = g->font ? g->font->size : 16; x0 = LMIN(x0, g->x - em); x1 = LMAX(x1, g->x + 2 * em); y0 = LMIN(y0, g->y - 1.5f * em); y1 = LMAX(y1, g->y + em); }
    }
    if (it->op == DO_POP_CLIP || it->op == DO_POP_LAYER || it->op == DO_PUSH_LAYER || x1 <= x0 || y1 <= y0) { b[0] = b[1] = 1e30f; b[2] = b[3] = -1e30f; return; }
    b[0] = x0 - e; b[1] = y0 - e; b[2] = x1 + e; b[3] = y1 + e;
}
#endif
