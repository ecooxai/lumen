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

/* Owned by one layout box; sizes are final CSS pixels in logical track order. */
typedef struct GridUsedTracks {
    float *size[2];
    float offset[2], gap[2];  /* final content alignment and distributed gutters */
    int count[2], explicit_start[2], explicit_count[2], template_count[2];
} GridUsedTracks;

typedef struct Box {
    uint8_t kind, fmt, ctl;
    bool anon, abs, fixed, floated, bfc, scroller, placeholder, collapse_top, has_baseline, intr_ok, has_abs, is_marker;
    Node *node; ComputedStyle *st;
    struct Box *parent, *first, *last, *next, *prev;
    float x, y, w, h;           /* border box, document coordinates */
    float m[4], b[4], p[4];     /* top right bottom left */
    float eff_mb;
    char *text; int text_len; int *tsrc; Font *font; ShapedRun sh; bool shaped; struct ShapeEntry *shape_ref;   /* sh is shared with the shaping cache */
    TextFrag *frags; int nfrags, fcap;
    Line *lines; int nlines, lcap;
    IRect *ir; int nir, ircap;
    float baseline;
    GridUsedTracks *grid_used;
    float imin, imax;
    float scroll_w, scroll_h;
    int list_index;
    float sx, sy;
    struct Box *cb, *abs_next, *abs_head;
    uint8_t anchor_ok;   /* bit i: inset i (top right bottom left) resolved from anchor() */
    float mk[8]; int8_t mmode; bool memo; int alo, ahi; unsigned astamp; int dcalls;   /* inputs of the last layout_box, reused when called again with the same ones */
    uint64_t ink_gen; float ink0, ink1; bool defer_in;   /* paint culling: vertical extent of the normal-flow paint, and whether it holds boxes painted later (stacking); valid while doc_box_generation() + 1 == ink_gen */
} Box;

typedef struct FRect { float x, y, w, h; uint8_t side; } FRect;
typedef struct FloatCtx { FRect *v; int n, cap; } FloatCtx;

typedef struct Layout {
    Arena arena;
    Box *root;
    Document *doc;
    float vw, vh, dpr;
    float doc_w, doc_h;
    int nboxes;
    float inl_cbh;   /* containing-block height handed to layout_inline (floats/atomics resolve % heights against it) */
    double ms;
    struct Box **alog; int nalog, calog;   /* every add_abs of this run, so a reused subtree can re-register its abs boxes */
} Layout;

extern bool (*layout_image_size_hook)(Node *n, float *w, float *h);
Layout *layout_new(void);
void layout_free(Layout *L);
void layout_run(Layout *L, Document *d, float vw, float vh);
uint64_t layout_generation(void); /* changes after any completed document layout */
Box *layout_hit(Layout *layout, float x, float y, float scroll_x, float scroll_y);   /* document coordinates */
void box_sticky_offset(const Box *b, float scroll_x, float scroll_y, float *dx, float *dy);
Font *style_font(const ComputedStyle *s);
float style_line_height(const ComputedStyle *s, Font *f);
/* column-reverse scrollers start at the end: scrollTop 0 is the bottom, negative scrolls up */
static inline bool box_within(const Box *x, const Box *a) { for (; x; x = x->parent) if (x == a) return true; return false; }
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
void grid_absolute_containing_block(const Box *cb, const Box *a, float *x, float *y, float *w, float *h);
float grid_item_auto_min(Layout *L, Box *c, float maxlen);   /* contribution to a minmax(auto, maxlen) column */
float layout_table(Layout *L, Box *b, float cx, float cy, float cw, float chdef);
void table_intrinsic(Layout *L, Box *b, float *mn, float *mx);
void intrinsic(Layout *L, Box *b, float *mn, float *mx);
void intrinsic_with_basis(Layout *L, Box *b, float cbw, float *mn, float *mx);
void intrinsic_outer(Layout *L, Box *b, float *mn, float *mx);
/* fw/fh are resolved border-box sizes from flex/grid, or -1 when unforced. */
void replaced_size(Layout *L, Box *b, float cbw, float cbh, float fw, float fh, float *w, float *h);
void box_translate(Box *b, float dx, float dy);
bool layout_move(Layout *L, Node *n, const Length old[4]);
void add_abs(Layout *L, Box *c);
void text_ensure_shaped(Box *t);
void text_release_shaped(Box *t);
void text_shape_cache_sweep(void);   /* after a layout pass: free shaped runs no box used recently */
void text_shape_cache_stats(unsigned *hits, unsigned *misses, size_t *entries); /* resets the counters */
void fc_avail(FloatCtx *fc, float y, float h, float x0, float x1, float *l, float *r);
void fc_place(FloatCtx *fc, Box *b, float y, float x0, float x1);
float fc_clear(FloatCtx *fc, int side);
float fc_bottom(FloatCtx *fc);
static inline float collapse2(float a, float b) { if (a >= 0 && b >= 0) return LMAX(a, b); if (a < 0 && b < 0) return LMIN(a, b); return a + b; }
static inline float res(Length l, float ref) {
    if (l.kind != LK_LEN) return 0;
    return len_resolve(l, ref > 0 ? ref : 0);
}
static inline bool len_def(Length l, float ref) { return l.kind == LK_LEN && (!len_has_pct(l) || ref >= 0); }
static inline float hbp(const Box *b) { return b->b[1] + b->b[3] + b->p[1] + b->p[3]; }
static inline float vbp(const Box *b) { return b->b[0] + b->b[2] + b->p[0] + b->p[2]; }
static inline bool in_flow(const Box *b) { return !b->abs && !b->floated; }
#endif
