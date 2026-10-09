#ifndef LUMEN_PAINT_H
#define LUMEN_PAINT_H
#include "../layout/layout.h"

/* Decoded image: premultiplied 0xAARRGGBB pixels */
typedef struct Image { int w, h, refs; uint32_t *px; float scale; /* device px per CSS px, 0 = 1 */ struct ImgAnim *anim; uint8_t *yuv; int yuv_mat; /* NV12 video frame; mat: 1 = BT.709, 2 = full range */ } Image;
static inline float image_css_w(const Image *im) { return im->scale > 0 ? im->w / im->scale : (float)im->w; }
static inline float image_css_h(const Image *im) { return im->scale > 0 ? im->h / im->scale : (float)im->h; }

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

typedef struct DisplayList { VEC(DItem) items; Arena arena; float vw, vh; bool has_fixed; float fix[4]; } DisplayList;   /* fix: viewport bounds of position:fixed items */

extern Image *(*paint_image_hook)(Node *n);
extern Image *(*paint_url_image_hook)(const char *url);
extern const Node *(*svg_ext_ref_hook)(const char *url, const char *id); /* element #id of an external SVG document, NULL while it loads */
Image *image_decode(const uint8_t *data, size_t n);
void image_unref(Image *im);
void image_yuv_materialize(Image *im);
/* Advances animated images; true if any frame changed. *next_ms gets the next frame time (0 = none). */
bool image_anim_tick(double now_ms, double *next_ms);
Image *svg_image(Node *n, float cw, float ch, float dpr);
void dl_build(DisplayList *dl, Layout *L, float scroll_x, float scroll_y, float vw, float vh);
extern bool dl_caret_on; /* paint the caret in the focused text control */
void dl_clear(DisplayList *dl);

/* punch: image whose pixels raster leaves as alpha-0 holes (composited by the GPU); punched is set when it was hit. */
typedef struct Canvas { int w, h, stride; uint32_t *px; float scale; const void *punch; bool punched; } Canvas;
void canvas_init(Canvas *c, int w, int h, float scale);
void canvas_free(Canvas *c);
void raster(Canvas *c, const DisplayList *dl, Color clear);
/* Re-raster only device-pixel rect [x0,x1)x[y0,y1); pixels outside are left untouched. */
void raster_rect(Canvas *c, const DisplayList *dl, Color clear, int x0, int y0, int x1, int y1);
bool png_write(const char *path, const uint32_t *px, int w, int h, int stride);

/* native text selection (anchor/focus = text node + byte offset into its layout text) */
typedef struct TextSel { bool on; const Layout *L; Node *an, *fn; int ao, fo; } TextSel;
extern TextSel g_tsel;
bool tsel_point(Layout *L, float x, float y, float page_sy, Box *scope, Node **n, int *off);
void tsel_prepare(Layout *L);
bool tsel_range(const Box *t, int *s, int *e);
char *tsel_text(Layout *L);
void tsel_word(Layout *L, Node *n, int off);
void tsel_block(Layout *L, Node *n);
void tsel_all(Layout *L);
bool tsel_dom_point(Node *n, int o, bool end, Node **tn, int *to);   /* element boundary -> nearest text (end: the text before) */
bool node_within(const Node *n, const Node *anc);

#endif
