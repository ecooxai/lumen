#ifndef LUMEN_LAYOUT_H
#define LUMEN_LAYOUT_H
#include "../css/css.h"
#include "../text/font.h"

typedef struct ArenaChunk ArenaChunk;
typedef struct Arena { ArenaChunk *head; size_t total; } Arena;
void *arena_alloc(Arena *a, size_t n);
void arena_reset(Arena *a);

enum BoxKind { BX_BLOCK, BX_INLINE, BX_TEXT, BX_ATOMIC, BX_BR };
enum Fmt { FMT_FLOW, FMT_INLINE, FMT_FLEX, FMT_GRID, FMT_TABLE, FMT_REPLACED };
enum { CTL_NONE, CTL_TEXT, CTL_CHECK, CTL_RADIO, CTL_BUTTON, CTL_SELECT, CTL_TEXTAREA, CTL_RANGE };

typedef struct TextFrag { struct Box *box; float x, y, w, h, base; int g0, g1; } TextFrag;
typedef struct Line { float y, h, base; int f0, nf; } Line;
typedef struct IRect { float x, y, w, h; bool first, last; int line; } IRect;

typedef struct Box {
    uint8_t kind, fmt, ctl;
    bool anon, abs, fixed, floated, bfc, scroller, placeholder, collapse_top, has_baseline, intr_ok, has_abs, is_marker;
    Node *node; ComputedStyle *st;
    struct Box *parent, *first, *last, *next, *prev;
    float x, y, w, h;           /* border box, document coordinates */
    float m[4], b[4], p[4];     /* top right bottom left */
    float eff_mb;
    char *text; int text_len; Font *font; ShapedRun sh; bool shaped;
    TextFrag *frags; int nfrags, fcap;
    Line *lines; int nlines, lcap;
    IRect *ir; int nir, ircap;
    float baseline;
    float imin, imax;
    float scroll_w, scroll_h;
    int list_index;
    float sx, sy;
    struct Box *cb, *abs_next, *abs_head;
} Box;

typedef struct FRect { float x, y, w, h; uint8_t side; } FRect;
typedef struct FloatCtx { FRect *v; int n, cap; } FloatCtx;

typedef struct Layout {
    Arena arena;
    Box *root;
    Document *doc;
    float vw, vh, dpr;
    float doc_w, doc_h;
    float scroll_x, scroll_y;
    int nboxes;
    float inl_cbh;   /* containing-block height handed to layout_inline (floats/atomics resolve % heights against it) */
    double ms;
} Layout;

extern bool (*layout_image_size_hook)(Node *n, float *w, float *h);
Layout *layout_new(void);
void layout_free(Layout *L);
void layout_run(Layout *L, Document *d, float vw, float vh);
Box *layout_hit(Layout *L, float x, float y);   /* document coordinates */
Font *style_font(const ComputedStyle *s);
float style_line_height(const ComputedStyle *s, Font *f);
/* column-reverse scrollers start at the end: scrollTop 0 is the bottom, negative scrolls up */
static inline bool box_scroll_from_end(const Box *b) { return b->st && (b->st->display == D_FLEX || b->st->display == D_INLINE_FLEX) && b->st->flex_direction == FD_COLUMN_REVERSE; }
static inline float box_scroll_y(const Box *b) { return b->node->scroll_y + (box_scroll_from_end(b) ? LMAX(0, b->scroll_h - b->h) : 0); }
void layout_dump(Box *b, int depth, int maxdepth);

/* internal */
enum { SZ_FILL, SZ_SHRINK, SZ_FORCED };
Box *build_box_tree(Layout *L, Document *d);
void compute_mbp(Box *b, float cbw);
void layout_box(Layout *L, Box *b, float x, float y, float cbw, float cbh, FloatCtx *fc, int mode, float fw, float fh);
float layout_inline(Layout *L, Box *b, float x0, float y0, float W, FloatCtx *fc);
float layout_flow(Layout *L, Box *b, float cx, float cy, float cw, float cbh, FloatCtx *fc);
float layout_flex(Layout *L, Box *b, float cx, float cy, float cw, float chdef);
float layout_grid(Layout *L, Box *b, float cx, float cy, float cw, float chdef);
float layout_table(Layout *L, Box *b, float cx, float cy, float cw, float chdef);
void table_intrinsic(Layout *L, Box *b, float *mn, float *mx);
void intrinsic(Layout *L, Box *b, float *mn, float *mx);
void intrinsic_outer(Layout *L, Box *b, float *mn, float *mx);
void replaced_size(Layout *L, Box *b, float cbw, float *w, float *h);
void box_translate(Box *b, float dx, float dy);
void add_abs(Layout *L, Box *c);
void text_ensure_shaped(Box *t);
void fc_avail(FloatCtx *fc, float y, float h, float x0, float x1, float *l, float *r);
void fc_place(FloatCtx *fc, Box *b, float y, float x0, float x1);
float fc_clear(FloatCtx *fc, int side);
float fc_bottom(FloatCtx *fc);
static inline float collapse2(float a, float b) { if (a >= 0 && b >= 0) return LMAX(a, b); if (a < 0 && b < 0) return LMIN(a, b); return a + b; }
static inline float res(Length l, float ref) { return l.kind == LK_LEN ? l.px + (ref > 0 ? l.pct * ref / 100.f : 0) : 0; }
static inline bool len_def(Length l, float ref) { return l.kind == LK_LEN && (l.pct == 0 || ref >= 0); }
static inline float hbp(const Box *b) { return b->b[1] + b->b[3] + b->p[1] + b->p[3]; }
static inline float vbp(const Box *b) { return b->b[0] + b->b[2] + b->p[0] + b->p[2]; }
static inline bool in_flow(const Box *b) { return !b->abs && !b->floated; }
#endif
