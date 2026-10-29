/* Canvas 2D: device-space polygons and glyphs rasterised to coverage, then painted and composited
   into a premultiplied ARGB buffer (the same layout as Image.px) */
#ifndef LUMEN_CANVAS2D_H
#define LUMEN_CANVAS2D_H
#include <stdbool.h>
#include <stdint.h>
struct Node;
struct Image;

typedef struct C2DPaint {
    int kind;               /* 0 solid, 1 linear, 2 radial (two circles), 3 conic, 4 pattern */
    uint32_t col;           /* solid: premultiplied ARGB */
    float inv[6];           /* device pixel -> paint space (a b c d e f) */
    float g[6];             /* linear x0 y0 x1 y1; radial x0 y0 r0 x1 y1 r1; conic cx cy start */
    const uint32_t *lut;    /* gradients: 1024 premultiplied ARGB stops, t 0..1 */
    const uint32_t *img;    /* pattern: premultiplied ARGB */
    int iw, ih;
    bool rx, ry, smooth;
} C2DPaint;

/* composite operation codes, in the order of the globalCompositeOperation keywords the context accepts */
enum { C2D_OVER, C2D_SRC_IN, C2D_SRC_OUT, C2D_SRC_ATOP, C2D_DST_OVER, C2D_DST_IN, C2D_DST_OUT, C2D_DST_ATOP, C2D_LIGHTER, C2D_COPY, C2D_XOR,
       C2D_MULTIPLY, C2D_SCREEN, C2D_OVERLAY, C2D_DARKEN, C2D_LIGHTEN, C2D_DODGE, C2D_BURN, C2D_HARD, C2D_SOFT, C2D_DIFF, C2D_EXCL,
       C2D_HUE, C2D_SAT, C2D_COLOR, C2D_LUM };

/* coverage of polygons (sub[k] = first point of polygon k, n points in all) into cov (w*h), nonzero or even-odd;
   returns false when nothing is inside the canvas */
bool c2d_cover(float *cov, int w, int h, const float *xy, const int *sub, int nsub, int n, bool eo, int *y0, int *y1);
/* paint cov (NULL: everywhere) with p at alpha onto px with op, limited by clip (NULL: none), rows y0..y1-1 */
void c2d_paint(uint32_t *px, int w, int h, const float *cov, const C2DPaint *p, float alpha, int op, const float *clip, int y0, int y1);

/* the frame shown for a 2D canvas element: present copies the context's pixels into it */
struct Image *c2d_image_for(const void *node);
void c2d_present(const void *node, const uint32_t *px, int w, int h);
void c2d_forget(const void *node);
/* an <img>, <video> or canvas element's current frame as premultiplied ARGB (malloc), NULL when none */
uint32_t *c2d_node_pixels(struct Node *node, int *w, int *h);
#endif
