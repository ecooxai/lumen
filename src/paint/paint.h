#ifndef LUMEN_PAINT_H
#define LUMEN_PAINT_H
#include "../layout/layout.h"
#include "regions.h"

/* Decoded image: premultiplied 0xAARRGGBB pixels */
typedef struct Image { int w, h, refs; uint32_t *px; float scale; /* device px per CSS px, 0 = 1 */ struct ImgAnim *anim; uint8_t *yuv; int yuv_mat; /* NV12 video frame; mat: 1 = BT.709, 2 = full range */ uint64_t content_version; /* images updated in place, e.g. iframe canvases */ bool translucent; /* a canvas frame with alpha below 1 somewhere: blended, not shown through a hole */ struct ImgGpu *gpu; } Image;
/* gpu: a frame left on the GPU (ImgGpu, gpu/webgpu.h); px stays NULL until image_yuv_materialize reads it back */
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
    float ox, oy;               /* DO_SHADOW offset: the border box is at (x - ox, y - oy) */
    bool inset, fixed; /* fixed items keep their viewport position during document scrolling */
    bool sticky;                /* fixed only because inside a position:sticky box that still moves with the page scroll */
    const void *mv;             /* innermost scroller box the item moves with when it scrolls; NULL = the document */
    const void *sc;             /* DO_PUSH_CLIP: the scroller box whose clip this is */
} DItem;

typedef struct DisplayList { VEC(DItem) items; Arena arena; float vw, vh; PaintRegions fixed; } DisplayList;

extern Image *(*paint_image_hook)(Node *n);
extern Image *(*paint_url_image_hook)(const char *url);
extern const Node *(*svg_ext_ref_hook)(const char *url, const char *id); /* element #id of an external SVG document, NULL while it loads */
Image *image_decode(const uint8_t *data, size_t n);
Image *image_decode_bitmap(const uint8_t *data, size_t n);   /* PNG, JPEG, WebP, GIF: safe on any thread (SVG needs the DOM) */
void image_unref(Image *im);
void image_yuv_materialize(Image *im);   /* px of a video frame (yuv) or a GPU one (gpu); may leave it NULL */
/* Copy planar 8-bit 4:2:0 into the presenter's NV12 layout, without RGB conversion.
 * Plane pointers address the first displayed row; strides may be negative. */
Image *image_yuv420p(int w, int h, const uint8_t *const planes[3], const int strides[3], int mat);
/* Advances animated images; true if any frame changed. *next_ms gets the next frame time (0 = none). */
bool image_anim_tick(double now_ms, double *next_ms);
Image *svg_image(Node *n, float cw, float ch, float dpr);
Node *svg_hit(Node *n, float x, float y, float cw, float ch);   /* shape at content-box point (x, y), or NULL */
void dl_build(DisplayList *dl, Layout *L, float scroll_x, float scroll_y, float vw, float vh);
extern bool dl_caret_on; /* paint the caret in the focused text control */
extern bool dl_cull; /* skip normal-flow subtrees outside the viewport (off only to compare, in tests) */
void dl_clear(DisplayList *dl);

/* punch: image whose pixels raster leaves as alpha-0 holes (composited by the GPU); punched is set when it was hit.
   punch_more: up to three more such images (NULL-terminated); bit i of punched_more is set when punch_more[i] was hit. */
typedef struct Canvas { int w, h, stride; uint32_t *px; float scale; const void *punch; bool punched; int row_offset; const void *punch_more[3]; unsigned punched_more; } Canvas;
/* Logical page rows can wrap in the existing allocation during GPU scrolling. */
static inline uint32_t *canvas_row(const Canvas *c, int y) {
    int row = y + c->row_offset;
    if (row >= c->h) row -= c->h;
    return c->px + (size_t)row * (size_t)c->stride;
}
void canvas_init(Canvas *c, int w, int h, float scale);
void canvas_free(Canvas *c);
void raster(Canvas *c, const DisplayList *dl, Color clear);
/* Re-raster only device-pixel rect [x0,x1)x[y0,y1); pixels outside are left untouched. */
void raster_rect(Canvas *c, const DisplayList *dl, Color clear, int x0, int y0, int x1, int y1);
/* Extra threads that paint bands of large rasters with the caller (0, the default: one thread).
   Threads are never destroyed; a smaller n leaves the others parked. */
void raster_set_threads(int n);
extern bool raster_pixel_rows;   /* test hook: no constant-coverage spans for rounded rects and shadows, no item culling */
extern unsigned long long g_raster_px, g_layer_px;   /* paint profile: pixels rastered / composited from opacity layers */
extern unsigned g_raster_calls, g_layer_n;
bool png_write(const char *path, const uint32_t *px, int w, int h, int stride);

/* native text selection (anchor/focus = text node + byte offset into its layout text) */
typedef struct TextSel { bool on; const Layout *L; Node *an, *fn; int ao, fo; } TextSel;
extern TextSel g_tsel;
extern bool g_tsel_fwd;   /* after tsel_prepare: the anchor comes first in document order */
/* selection start/end carets in viewport CSS px from the last dl_build of g_tsel.L; ok = painted */
typedef struct TselCaret { bool ok; float x, y, h; int cluster; } TselCaret;
extern TselCaret g_tsel_caret[2];
bool tsel_point(Layout *L, float x, float y, float page_sy, Box *scope, Node **n, int *off);
/* like tsel_point, but fails unless text is on the point's line within maxd px horizontally */
bool tsel_point_near(Layout *L, float x, float y, float page_sy, Box *scope, float maxd, Node **n, int *off);
int tsel_edge(const Box *t);   /* bit 0: t holds the selection start, bit 1: the end */
void tsel_prepare(Layout *L);
bool tsel_range(const Box *t, int *s, int *e);
char *tsel_text(Layout *L);
void tsel_word(Layout *L, Node *n, int off);
void tsel_block(Layout *L, Node *n);
void tsel_all(Layout *L);
bool tsel_dom_point(Node *n, int o, bool end, Node **tn, int *to);   /* element boundary -> nearest text (end: the text before) */
bool node_within(const Node *n, const Node *anc);
/* <input>/<textarea> selection (Document.csel, codepoints of the value) */
int u8_cp_byte(const char *s, int len, int cp);   /* codepoint index -> byte offset, clamped to len */
int u8_byte_cp(const char *s, int byte);
const Box *ctl_text_box(const Box *cb);   /* the control's shown value text; NULL when empty (placeholder) */
int ctl_hit(const Box *cb, float x, float y, float page_sy);   /* caret position nearest the layout point */
bool ctl_caret(const Box *cb, int cp, float *x, float *y, float *h);   /* caret rect, box coordinates */
int ctl_vmove(const Box *cb, int cp, int dir);   /* Up (dir < 0) / Down from caret cp */
bool ctl_range(const Box *t, int *s, int *e);   /* t is the focused control's text: selected bytes */

#endif
