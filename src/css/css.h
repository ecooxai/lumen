#ifndef LUMEN_CSS_H
#define LUMEN_CSS_H
#include "../dom/dom.h"

/* ---------------- values ---------------- */
enum { LK_LEN, LK_AUTO, LK_NONE, LK_MIN_CONTENT, LK_MAX_CONTENT, LK_FIT_CONTENT };
typedef struct { float px, pct; uint8_t kind; bool pctu; uint8_t mm; float px2, pct2; } Length;  /* pctu: written with % (so 0% != 0px); mm 1/2: min/max(px+pct, px2+pct2), px/pct alone is a best guess */
/* Length value = px + pct/100 * reference */  /* value = px + pct/100 * reference */
static inline Length L_px(float v) { Length l = { v, 0, LK_LEN }; return l; }
static inline Length L_auto(void) { Length l = { 0, 0, LK_AUTO }; return l; }
static inline float len_resolve(Length l, float ref) { return l.px + l.pct * ref / 100.f; }
static inline bool len_is_auto(Length l) { return l.kind == LK_AUTO; }
static inline bool len_has_pct(Length l) { return l.kind == LK_LEN && l.pct != 0; }

typedef uint32_t Color; /* 0xAARRGGBB */
#define COLOR_A(c) (((c) >> 24) & 0xff)
#define COLOR_R(c) (((c) >> 16) & 0xff)
#define COLOR_G(c) (((c) >> 8) & 0xff)
#define COLOR_B(c) ((c) & 0xff)
#define RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

enum Display { D_NONE, D_INLINE, D_BLOCK, D_INLINE_BLOCK, D_FLEX, D_INLINE_FLEX, D_GRID, D_INLINE_GRID, D_LIST_ITEM, D_TABLE, D_INLINE_TABLE,
    D_TABLE_ROW, D_TABLE_CELL, D_TABLE_ROW_GROUP, D_TABLE_HEADER_GROUP, D_TABLE_FOOTER_GROUP, D_TABLE_COLUMN, D_TABLE_COLUMN_GROUP, D_TABLE_CAPTION, D_CONTENTS, D_FLOW_ROOT };
enum Position { P_STATIC, P_RELATIVE, P_ABSOLUTE, P_FIXED, P_STICKY };
enum Float { F_NONE, F_LEFT, F_RIGHT };
enum { CLEAR_NONE, CLEAR_LEFT, CLEAR_RIGHT, CLEAR_BOTH };
enum Overflow { OV_VISIBLE, OV_HIDDEN, OV_SCROLL, OV_AUTO, OV_CLIP };
enum TextAlign { TA_START, TA_LEFT, TA_RIGHT, TA_CENTER, TA_JUSTIFY, TA_END };
enum WhiteSpace { WS_NORMAL, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE, WS_BREAK_SPACES };
enum BorderStyle { BS_NONE, BS_HIDDEN, BS_SOLID, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_GROOVE, BS_RIDGE, BS_INSET, BS_OUTSET };
enum FlexDir { FD_ROW, FD_ROW_REVERSE, FD_COLUMN, FD_COLUMN_REVERSE };
enum Align { AL_AUTO, AL_NORMAL, AL_STRETCH, AL_FLEX_START, AL_FLEX_END, AL_CENTER, AL_BASELINE, AL_SPACE_BETWEEN, AL_SPACE_AROUND, AL_SPACE_EVENLY, AL_START, AL_END };
enum VAlign { VA_BASELINE, VA_TOP, VA_MIDDLE, VA_BOTTOM, VA_TEXT_TOP, VA_TEXT_BOTTOM, VA_SUB, VA_SUPER, VA_LENGTH };
enum { TT_NONE, TT_UPPER, TT_LOWER, TT_CAPITALIZE };
enum { TD_UNDERLINE = 1, TD_OVERLINE = 2, TD_LINE_THROUGH = 4 };
enum { BOX_CONTENT, BOX_BORDER };
enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE };
enum { BG_REPEAT = 0, BG_NO_REPEAT = 1, BG_REPEAT_X = 2, BG_REPEAT_Y = 3 };
enum { BGS_AUTO, BGS_COVER, BGS_CONTAIN, BGS_LEN };
enum { OF_FILL, OF_CONTAIN, OF_COVER, OF_NONE, OF_SCALE_DOWN };
enum { LST_NONE, LST_DISC, LST_CIRCLE, LST_SQUARE, LST_DECIMAL, LST_LOWER_ALPHA, LST_UPPER_ALPHA, LST_LOWER_ROMAN, LST_UPPER_ROMAN };
enum { CUR_DEFAULT, CUR_POINTER, CUR_TEXT, CUR_MOVE, CUR_NOT_ALLOWED, CUR_GRAB };
enum { TO_CLIP, TO_ELLIPSIS };

typedef struct { float pos; Color color; } GradStop;
typedef struct Gradient { uint8_t type; /* 0 linear 1 radial */ float angle; int nstops; GradStop stops[8]; bool repeating; } Gradient;

typedef struct { float x, y, blur, spread; Color color; bool inset; } Shadow;

typedef struct CustomProps { int refs, depth; HMap map; struct CustomProps *parent; } CustomProps; /* own name -> char* value, then parent's */
const char *custom_get(const CustomProps *c, const char *name);

typedef struct GridTrack { Length size; float fr; Length min; } GridTrack;

typedef struct ComputedStyle {
    int refs;
    uint8_t display, position, float_, clear, overflow_x, overflow_y, visibility, box_sizing;
    uint8_t text_align, white_space, text_transform, text_decoration, font_style, list_style, cursor, pointer_events;
    uint8_t flex_direction, flex_wrap, justify_content, align_items, align_self, align_content, justify_items, justify_self;
    uint8_t vertical_align, bg_repeat, bg_size_kind, object_fit, text_overflow, word_break, overflow_wrap, direction;
    uint8_t border_style[4];
    uint8_t outline_style, user_select, appearance, isolation, table_layout, border_collapse, container_type, resize, writing_mode;
    const char *container_name; /* atom: space-separated names, or NULL */
    int16_t font_weight;
    int z_index; bool z_auto;
    int order;
    float font_size, line_height; bool line_height_normal; float line_height_factor; /* factor>0 when unitless */
    float letter_spacing, word_spacing, text_indent;
    float opacity;
    float flex_grow, flex_shrink;
    Length flex_basis;
    float row_gap, column_gap; Length row_gap_l, column_gap_l;
    float vertical_align_len;
    Length width, height, min_width, min_height, max_width, max_height;
    Length margin[4], padding[4], inset[4]; /* top right bottom left */
    float border_width[4];
    Color border_color[4];
    Length border_radius[4]; /* tl tr br bl */
    float outline_width, outline_offset; Color outline_color;
    Color color, bg_color;
    char *bg_image;         /* url (owned) */
    Gradient *bg_gradient;  /* owned */
    char *mask_image;       /* url (owned) */
    uint8_t mask_fit;       /* 0 auto, 1 contain, 2 cover */
    Length bg_size[2], bg_pos[2];
    const char *font_family; /* atom of the full family list */
    Shadow box_shadow; bool has_shadow;
    Shadow text_shadow; bool has_text_shadow;
    float transform[6]; bool has_transform; Length transform_origin[2];
    Length translate_pending[2]; /* % translate resolved at layout */
    float aspect_ratio;
    char *content;          /* for ::before/::after */
    int grid_ncols; GridTrack *grid_cols; int grid_nrows; GridTrack *grid_rows; Length grid_auto_rows;
    int grid_col_start, grid_col_span, grid_row_start, grid_row_span;
    char *grid_areas;       /* template areas: cells space-separated, rows '/'-separated */
    char *grid_area;        /* named grid-area of an item */
    float filter_blur; float filter_brightness; float backdrop_blur;
    float line_clamp;
    Color caret_color, fill, stroke; float stroke_width;
    const char *anim_name; float anim_dur, anim_delay, anim_iter; /* first animation layer; name is an atom */
    const char *tr_prop; float tr_dur, tr_delay;                 /* transition-property list atom (NULL = all), max times */
    CustomProps *custom;
    struct ComputedStyle *before, *after; /* pseudo element styles */
} ComputedStyle;

/* called on every element restyle (old may be NULL); used to fire animation/transition events */
extern void (*css_style_change_hook)(Node *n, const ComputedStyle *old, const ComputedStyle *now);

/* ---------------- stylesheet ---------------- */
typedef struct Decl { const char *prop; char *value; bool important; } Decl;
typedef VEC(Decl) DeclList;

enum SelKind { SK_TYPE, SK_UNIVERSAL, SK_ID, SK_CLASS, SK_ATTR, SK_PSEUDO, SK_PSEUDO_EL };
enum Combinator { CB_NONE, CB_DESC, CB_CHILD, CB_ADJ, CB_SIB };
typedef struct SelList SelList;
typedef struct SimpleSel {
    uint8_t kind, op, ci; /* attr op: 0 exists, '=', '~', '|', '^', '$', '*' */
    const char *name;     /* atom: tag/id/class/attr/pseudo */
    char *value;
    int a, b;             /* nth */
    SelList *sub;         /* :not/:is/:where/:has/nth-of */
} SimpleSel;
typedef struct Compound { SimpleSel *s; int n; uint8_t comb; /* combinator to the LEFT (towards previous compound) */ } Compound;
typedef struct Selector { Compound *c; int n; uint32_t spec; uint8_t pseudo_el; /* 0 none 1 before 2 after 3 other */ } Selector;
struct SelList { Selector *v; int n; };

typedef struct ContainerCond { char *name, *query; struct ContainerCond *outer; } ContainerCond;
/* container query environment: the container's content size and its custom properties (style()) */
typedef struct CQEnv { float w, h; bool size, block; const char *(*var)(const void *ud, const char *name); const void *ud; } CQEnv;
bool css_container_eval(const char *q, const CQEnv *env);

typedef struct Rule {
    Selector sel; /* single selector (selector lists are split) */
    DeclList *decls; /* shared between split selectors */
    uint32_t order;
    uint8_t origin; /* 0 UA, 1 author */
    uint8_t layer;
    const ContainerCond *cq; /* innermost @container, or NULL */
} Rule;

typedef struct MediaCtx { float vw, vh, dpr; bool dark; } MediaCtx;

typedef struct StyleSheet {
    VEC(Rule) rules;
    VEC(DeclList *) decl_lists;
    VEC(char *) imports;
    VEC(ContainerCond *) conds;
    VEC(char *) font_faces; /* raw @font-face src urls, family pairs */
    char *base_url;
    bool disabled;
    Node *owner;
    int origin;
} StyleSheet;

typedef struct RuleIndex {
    HMap by_id, by_class, by_tag; /* -> RuleVec* */
    VEC(Rule *) universal;
    uint32_t count;
} RuleIndex;

typedef struct StyleEngine {
    VEC(StyleSheet *) sheets;   /* document order */
    RuleIndex idx; bool idx_dirty;
    MediaCtx media;
    uint32_t order_counter;
    Document *doc;
    uint64_t generation;
    int stats_matched, stats_paint;
    bool layout_dirty;          /* a recalc changed something other than paint-only properties */
} StyleEngine;

StyleSheet *css_parse_sheet(const char *src, size_t n, const char *base_url, int origin, const MediaCtx *mc);
void css_sheet_free(StyleSheet *s);
DeclList *css_parse_decls(const char *src, size_t n);
void css_decls_free(DeclList *d);
bool css_parse_selector_list(const char *src, SelList *out);
void css_sellist_free(SelList *l);
char *css_selector_text(const char *src); /* canonical CSSOM serialization; NULL if invalid */
bool css_match_selector_list(const SelList *l, Node *el);
bool css_match_selector(const Selector *s, Node *el, Node *scope);
typedef VEC(Node *) NodeVec;
Node *css_query(Node *root, const char *sel, bool all, NodeVec *out);
bool css_media_matches(const char *q, const MediaCtx *mc);
bool css_supports(const char *cond);

StyleEngine *style_engine_new(Document *d);
void style_engine_free(StyleEngine *e);
void style_engine_add_sheet(StyleEngine *e, StyleSheet *s);
extern void (*css_sheet_added_hook)(StyleSheet *s);
void style_engine_remove_owner(StyleEngine *e, Node *owner);
void style_engine_invalidate(StyleEngine *e);
void style_recalc(StyleEngine *e, Node *root, bool force);
ComputedStyle *style_for_text(Node *text);

ComputedStyle *style_new_default(void);
ComputedStyle *style_inherit(const ComputedStyle *parent);
void style_ref(ComputedStyle *s);
void style_free(ComputedStyle *s);
bool css_parse_color(const char *s, Color *out, Color current);
bool css_parse_length(const char *s, Length *out, float em, float rem, const MediaCtx *mc);
void css_apply_decl(ComputedStyle *st, const ComputedStyle *parent, const char *prop, const char *value, StyleEngine *e, Node *el);
char *css_get_computed_value(Node *el, const char *prop);
extern const char *css_ua_sheet;

/* tokenizer (exposed for value parsing) */
enum CTok { CT_EOF, CT_IDENT, CT_FUNC, CT_AT, CT_HASH, CT_STRING, CT_URL, CT_DELIM, CT_NUMBER, CT_PERCENT, CT_DIM, CT_WS, CT_COLON, CT_SEMI, CT_COMMA,
    CT_LBRACK, CT_RBRACK, CT_LPAREN, CT_RPAREN, CT_LBRACE, CT_RBRACE, CT_CDO, CT_CDC, CT_BAD };
typedef struct { int type; const char *start; size_t len; double num; char unit[12]; char *str; bool is_int; } CTok;
typedef struct { const char *s; size_t n, i; } CLexer;
void clex_init(CLexer *l, const char *s, size_t n);
CTok clex_next(CLexer *l); /* tok.str must be freed when non-NULL */
#endif
