#ifndef LUMEN_PAINT_H
#define LUMEN_PAINT_H
#include "../layout/layout.h"

/* Decoded image: premultiplied 0xAARRGGBB pixels */
typedef struct Image { int w, h, refs; uint32_t *px; } Image;

enum DOp { DO_RECT, DO_BORDER, DO_TEXT, DO_IMAGE, DO_GRADIENT, DO_SHADOW, DO_LINE,
           DO_PUSH_CLIP, DO_POP_CLIP, DO_PUSH_LAYER, DO_POP_LAYER };

typedef struct DGlyph { uint32_t gid; float x, y; Font *font; } DGlyph;

typedef struct DItem {
    uint8_t op;
    float x, y, w, h;           /* viewport CSS px */
    float r[4];                 /* corner radii tl tr br bl */
    Color color;
    float bw[4]; Color bc[4]; uint8_t bs[4];
    DGlyph *g; int ng;
    Image *img;
    Gradient *grad;
    float blur, spread, alpha;
    bool inset;
} DItem;

typedef struct DisplayList { VEC(DItem) items; Arena arena; float vw, vh; } DisplayList;

extern Image *(*paint_image_hook)(Node *n);
extern Image *(*paint_url_image_hook)(const char *url);
Image *image_decode(const uint8_t *data, size_t n);
void image_unref(Image *im);
void dl_build(DisplayList *dl, Layout *L, float scroll_x, float scroll_y, float vw, float vh);
void dl_clear(DisplayList *dl);

typedef struct Canvas { int w, h, stride; uint32_t *px; float scale; } Canvas;
void canvas_init(Canvas *c, int w, int h, float scale);
void canvas_free(Canvas *c);
void raster(Canvas *c, const DisplayList *dl, Color clear);
bool png_write(const char *path, const uint32_t *px, int w, int h, int stride);
#endif
