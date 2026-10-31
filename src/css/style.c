#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#include <sched.h>
#include <time.h>
#endif
/* Cascade, inheritance and computed values */
#include "css.h"
#include "color.h"
#include "../layout/layout.h"
#include "../base/url.h"
#include <math.h>
#include <ctype.h>
#include <strings.h>
#include <limits.h>

typedef VEC(Rule *) RuleVec;

/* ---------------- style objects ---------------- */
long g_styles_live;
static void match_memo_free(void *memo);
void node_free_inline_style(Node *n) { css_decls_free(n->inline_decls); n->inline_decls = NULL; css_decls_free(n->anim_decls); n->anim_decls = NULL; match_memo_free(n->match_memo); n->match_memo = NULL; }
void css_set_anim_style(Node *n, const char *css) {
    css_decls_free(n->anim_decls);
    n->anim_decls = css && *css ? css_parse_decls(css, strlen(css)) : NULL;
    if (n->doc) doc_mark_style_dirty(n->doc, n);
}
static const char *display_canon(const char *val);
ComputedStyle *style_new_default(void) {
    ComputedStyle *s = xcalloc(1, sizeof *s); g_styles_live++;
    s->refs = 1; s->anim_iter = 1;
    s->display = D_INLINE; s->font_size = 16; s->font_weight = 400; s->line_height_normal = true;
    s->color = RGBA(0, 0, 0, 255); s->opacity = 1; s->flex_shrink = 1; s->flex_basis = L_auto(); s->column_width = L_auto(); s->column_gap_l = L_auto();
    s->width = s->height = L_auto(); s->min_width = s->min_height = L_auto();
    s->max_width.kind = s->max_height.kind = LK_NONE;
    for (int i = 0; i < 4; i++) { s->inset[i] = L_auto(); s->border_color[i] = s->color; s->border_width[i] = 3; }
    s->z_auto = true; s->font_family = atom("sans-serif");
    s->list_style = LST_DISC; s->align_items = AL_NORMAL; s->align_self = AL_AUTO; s->justify_content = AL_NORMAL; s->align_content = AL_NORMAL;
    s->bg_size[0] = s->bg_size[1] = L_auto();
    s->bg_origin = BG_PADDING_BOX; s->bg_clip = s->bg_color_clip = BG_BORDER_BOX; s->bg_layers = 1;
    s->transform_origin[0].pct = 50; s->transform_origin[1].pct = 50;
    s->filter_brightness = 1; s->caret_color = 0; s->fill = RGBA(0, 0, 0, 255); s->stroke_width = 1;
    s->grid_auto_rows = L_auto();
    s->pointer_events = 1;
    return s;
}
static CustomProps *custom_ref(CustomProps *c) { if (c) c->refs++; return c; }
static void custom_unref(CustomProps *c) {
    while (c && --c->refs == 0) { CustomProps *up = c->parent; hm_free(&c->map, free); free(c); c = up; }
}
/* Debug counters for the pass log: custom_changes calls, non-local calls,
 * calls without a shared map, entries visited, largest map, lookup levels,
 * most changed-mask bits at a custom-dependency block. */
static uint64_t g_cc[7];
static const char *custom_geth(const CustomProps *c, const char *name, size_t n, uint32_t hash) {
    for (; c; c = c->parent) { g_cc[5]++; const char *v = hm_geth((HMap *)&c->map, name, n, hash); if (v) return v; }
    return NULL;
}
const char *custom_get(const CustomProps *c, const char *name) {
    if (!c) return NULL;
    size_t n = strlen(name);
    return custom_geth(c, name, n, hash_str(name, n));
}
#define CUSTOM_MAX_DEPTH 64

/* Properties Chrome supports, for CSS.supports and the generic value store */
typedef struct { const char *name; uint8_t inherited, native; const char *initial; } GenProp;
static const GenProp gen_props[] = {
#include "props_generated.inc"
};
static const GenProp *gen_prop(const char *p) {
    int lo = 0, hi = (int)(sizeof gen_props / sizeof *gen_props) - 1;
    while (lo <= hi) { int m = (lo + hi) / 2, c = strcmp(p, gen_props[m].name); if (!c) return &gen_props[m]; if (c < 0) hi = m - 1; else lo = m + 1; }
    return NULL;
}
/* sets prop to v (taking ownership) in a copy-on-write chain of maps */
static void gen_put(CustomProps **slot, const char *prop, char *v) {
    CustomProps *c = *slot;
    const char *cur = custom_get(c, prop);
    if (cur && !strcmp(cur, v)) { free(v); return; }
    if (!c || c->refs > 1) {
        CustomProps *n = xcalloc(1, sizeof *n); n->refs = 1;
        if (c && c->depth < CUSTOM_MAX_DEPTH) { n->parent = c; n->depth = c->depth + 1; }
        else if (c) {
            const CustomProps *chain[CUSTOM_MAX_DEPTH + 2]; int nc = 0;
            for (const CustomProps *x = c; x && nc < CUSTOM_MAX_DEPTH + 2; x = x->parent) chain[nc++] = x;
            while (nc-- > 0) hm_foreach((HMap *)&chain[nc]->map, ent) { char *prev = hm_get(&n->map, ent->key); hm_put(&n->map, ent->key, xstrdup((char *)ent->val)); free(prev); }
            custom_unref(c);
        }
        *slot = c = n;
    }
    char *old = hm_get(&c->map, prop); hm_put(&c->map, prop, v); free(old);
}
static const char *gen_value(const ComputedStyle *s, const GenProp *g) {
    const char *v = custom_get(g->inherited ? s->gen_i : s->gen_n, g->name);
    return v && *v ? v : NULL;
}

ComputedStyle *style_inherit(const ComputedStyle *p) {
    ComputedStyle *s = style_new_default();
    if (!p) return s;
    s->color = p->color; s->color_ser = p->color_ser; s->font_size = p->font_size; s->font_weight = p->font_weight; s->font_style = p->font_style; s->font_family = p->font_family;
    s->line_height = p->line_height; s->line_height_normal = p->line_height_normal; s->line_height_factor = p->line_height_factor;
    s->text_align = p->text_align; s->white_space = p->white_space; s->text_transform = p->text_transform; s->visibility = p->visibility;
    s->letter_spacing = p->letter_spacing; s->word_spacing = p->word_spacing; s->text_indent = p->text_indent; s->list_style = p->list_style;
    s->cursor = p->cursor; s->pointer_events = p->pointer_events; s->word_break = p->word_break; s->overflow_wrap = p->overflow_wrap; s->direction = p->direction;
    s->has_text_shadow = p->has_text_shadow; s->text_shadow = p->text_shadow; s->caret_color = p->caret_color; s->fill = p->fill; s->stroke = p->stroke; s->stroke_width = p->stroke_width;
    s->user_select = p->user_select; s->border_collapse = p->border_collapse; s->writing_mode = p->writing_mode; s->inert = p->inert;
    memcpy(s->border_spacing, p->border_spacing, sizeof s->border_spacing);
    s->custom = custom_ref(p->custom); s->gen_i = custom_ref(p->gen_i);
    for (int i = 0; i < 4; i++) s->border_color[i] = s->color;
    s->outline_color = s->color;
    return s;
}
void style_ref(ComputedStyle *s) { if (s) s->refs++; }
void style_free(ComputedStyle *s) {
    if (!s || --s->refs > 0) return;
    g_styles_live--;
    free(s->anchor_name); free(s->pos_anchor); free(s->pos_area); free(s->pos_try); for (int i = 0; i < 4; i++) free(s->anchor_ins[i]); free(s->anchor_sz[0]); free(s->anchor_sz[1]);
    free(s->bg_image); free(s->mask_image); free(s->bg_gradient); free(s->content); free(s->grid_cols); free(s->grid_rows); free(s->grid_areas); free(s->grid_area);
    custom_unref(s->custom); custom_unref(s->gen_i); custom_unref(s->gen_n);
    if (s->before) style_free(s->before);
    if (s->marker) style_free(s->marker);
    if (s->after) style_free(s->after);
    free(s);
}

/* ---------------- colors ---------------- */
static const struct { const char *n; uint32_t c; } named_colors[] = {
{"aliceblue",0xf0f8ff},{"antiquewhite",0xfaebd7},{"aqua",0x00ffff},{"aquamarine",0x7fffd4},{"azure",0xf0ffff},{"beige",0xf5f5dc},{"bisque",0xffe4c4},{"black",0x000000},{"blanchedalmond",0xffebcd},{"blue",0x0000ff},{"blueviolet",0x8a2be2},{"brown",0xa52a2a},{"burlywood",0xdeb887},{"cadetblue",0x5f9ea0},{"chartreuse",0x7fff00},{"chocolate",0xd2691e},{"coral",0xff7f50},{"cornflowerblue",0x6495ed},{"cornsilk",0xfff8dc},{"crimson",0xdc143c},{"cyan",0x00ffff},{"darkblue",0x00008b},{"darkcyan",0x008b8b},{"darkgoldenrod",0xb8860b},{"darkgray",0xa9a9a9},{"darkgreen",0x006400},{"darkgrey",0xa9a9a9},{"darkkhaki",0xbdb76b},{"darkmagenta",0x8b008b},{"darkolivegreen",0x556b2f},{"darkorange",0xff8c00},{"darkorchid",0x9932cc},{"darkred",0x8b0000},{"darksalmon",0xe9967a},{"darkseagreen",0x8fbc8f},{"darkslateblue",0x483d8b},{"darkslategray",0x2f4f4f},{"darkslategrey",0x2f4f4f},{"darkturquoise",0x00ced1},{"darkviolet",0x9400d3},{"deeppink",0xff1493},{"deepskyblue",0x00bfff},{"dimgray",0x696969},{"dimgrey",0x696969},{"dodgerblue",0x1e90ff},{"firebrick",0xb22222},{"floralwhite",0xfffaf0},{"forestgreen",0x228b22},{"fuchsia",0xff00ff},{"gainsboro",0xdcdcdc},{"ghostwhite",0xf8f8ff},{"gold",0xffd700},{"goldenrod",0xdaa520},{"gray",0x808080},{"green",0x008000},{"greenyellow",0xadff2f},{"grey",0x808080},{"honeydew",0xf0fff0},{"hotpink",0xff69b4},{"indianred",0xcd5c5c},{"indigo",0x4b0082},{"ivory",0xfffff0},{"khaki",0xf0e68c},{"lavender",0xe6e6fa},{"lavenderblush",0xfff0f5},{"lawngreen",0x7cfc00},{"lemonchiffon",0xfffacd},{"lightblue",0xadd8e6},{"lightcoral",0xf08080},{"lightcyan",0xe0ffff},{"lightgoldenrodyellow",0xfafad2},{"lightgray",0xd3d3d3},{"lightgreen",0x90ee90},{"lightgrey",0xd3d3d3},{"lightpink",0xffb6c1},{"lightsalmon",0xffa07a},{"lightseagreen",0x20b2aa},{"lightskyblue",0x87cefa},{"lightslategray",0x778899},{"lightslategrey",0x778899},{"lightsteelblue",0xb0c4de},{"lightyellow",0xffffe0},{"lime",0x00ff00},{"limegreen",0x32cd32},{"linen",0xfaf0e6},{"magenta",0xff00ff},{"maroon",0x800000},{"mediumaquamarine",0x66cdaa},{"mediumblue",0x0000cd},{"mediumorchid",0xba55d3},{"mediumpurple",0x9370db},{"mediumseagreen",0x3cb371},{"mediumslateblue",0x7b68ee},{"mediumspringgreen",0x00fa9a},{"mediumturquoise",0x48d1cc},{"mediumvioletred",0xc71585},{"midnightblue",0x191970},{"mintcream",0xf5fffa},{"mistyrose",0xffe4e1},{"moccasin",0xffe4b5},{"navajowhite",0xffdead},{"navy",0x000080},{"oldlace",0xfdf5e6},{"olive",0x808000},{"olivedrab",0x6b8e23},{"orange",0xffa500},{"orangered",0xff4500},{"orchid",0xda70d6},{"palegoldenrod",0xeee8aa},{"palegreen",0x98fb98},{"paleturquoise",0xafeeee},{"palevioletred",0xdb7093},{"papayawhip",0xffefd5},{"peachpuff",0xffdab9},{"peru",0xcd853f},{"pink",0xffc0cb},{"plum",0xdda0dd},{"powderblue",0xb0e0e6},{"purple",0x800080},{"rebeccapurple",0x663399},{"red",0xff0000},{"rosybrown",0xbc8f8f},{"royalblue",0x4169e1},{"saddlebrown",0x8b4513},{"salmon",0xfa8072},{"sandybrown",0xf4a460},{"seagreen",0x2e8b57},{"seashell",0xfff5ee},{"sienna",0xa0522d},{"silver",0xc0c0c0},{"skyblue",0x87ceeb},{"slateblue",0x6a5acd},{"slategray",0x708090},{"slategrey",0x708090},{"snow",0xfffafa},{"springgreen",0x00ff7f},{"steelblue",0x4682b4},{"tan",0xd2b48c},{"teal",0x008080},{"thistle",0xd8bfd8},{"tomato",0xff6347},{"turquoise",0x40e0d0},{"violet",0xee82ee},{"wheat",0xf5deb3},{"white",0xffffff},{"whitesmoke",0xf5f5f5},{"yellow",0xffff00},{"yellowgreen",0x9acd32},
{"canvas",0xffffff},{"canvastext",0x000000},{"linktext",0x0000ee},{"buttonface",0xefefef},{"buttontext",0x000000},{"field",0xffffff},{"fieldtext",0x000000},{"graytext",0x808080},{"highlight",0x3390ff},{"highlighttext",0xffffff},{"buttonborder",0x767676},{"windowtext",0x000000},{"window",0xffffff},{"menu",0xffffff},{"accentcolor",0x0075ff},{"accentcolortext",0xffffff},{"activetext",0xff0000},{"mark",0xffff00},{"marktext",0x000000},{"selecteditem",0x0075ff},{"selecteditemtext",0xffffff},{"visitedtext",0x551a8b},{"activeborder",0x767676},{"activecaption",0xffffff},{"appworkspace",0xffffff},{"background",0xffffff},{"buttonhighlight",0xefefef},{"buttonshadow",0xefefef},{"captiontext",0x000000},{"inactiveborder",0x767676},{"inactivecaption",0xffffff},{"inactivecaptiontext",0x808080},{"infobackground",0xffffff},{"infotext",0x000000},{"menutext",0x000000},{"scrollbar",0xffffff},{"threeddarkshadow",0x767676},{"threedface",0xefefef},{"threedhighlight",0x767676},{"threedlightshadow",0x767676},{"threedshadow",0x767676},{"windowframe",0x767676}};


bool css_named_color(const char *name, uint32_t *rgb) {
    for (size_t i = 0; i < ARRLEN(named_colors); i++) if (!strcmp(named_colors[i].n, name)) { *rgb = named_colors[i].c; return true; }
    return false;
}

static CssColor color_of(Color c) { CssColor r = { CSPACE_SRGB, 0, true, { COLOR_R(c) / 255.0, COLOR_G(c) / 255.0, COLOR_B(c) / 255.0 }, COLOR_A(c) / 255.0 }; return r; }
/* the colour value of st's color property, in full precision */
static CssColor cur_color(const ComputedStyle *st) { CssColor c; if (st->color_ser && css_color_parse(st->color_ser, &c, NULL)) return c; return color_of(st->color); }
/* parses a colour into *out and *ser (the computed value's serialization when 8-bit sRGB can't express it) */
static bool parse_color_ser(const char *in, Color *out, const char **ser, const CssColor *cur) {
    CssColor c;
    if (!css_color_parse(in, &c, cur)) return false;
    *out = css_color_rgba8(&c);
    if (c.legacy && !c.none) *ser = NULL;
    else { char buf[160]; css_color_serialize(&c, buf, sizeof buf); *ser = atom(buf); }
    return true;
}
bool css_parse_color(const char *in, Color *out, Color current) {
    CssColor cur = color_of(current), c;
    if (!css_color_parse(in, &c, &cur)) return false;
    *out = css_color_rgba8(&c);
    return true;
}

/* ---------------- lengths ---------------- */
/* Temporary calculation types: values keep their compact affine storage.
   Tracking exponents also preserves unit cancellation in products/quotients. */
enum { MATH_NUMBER = -1, MATH_LENGTH, MATH_ANGLE, MATH_TIME, MATH_FLEX, MATH_PERCENT, MATH_DIMENSIONS };
typedef struct { int8_t power[MATH_DIMENSIONS]; } MathType;
static MathType math_type_unit(int unit) {
    MathType type = {{0}}; if (unit >= 0) type.power[unit] = 1; return type;
}
static bool math_type_equal(MathType a, MathType b) { return !memcmp(&a, &b, sizeof a); }
static bool math_type_product(MathType a, MathType b, int sign, MathType *out) {
    for (int i = 0; i < MATH_DIMENSIONS; i++) {
        int power = a.power[i] + sign * b.power[i];
        if (power < INT8_MIN || power > INT8_MAX) return false;
        out->power[i] = (int8_t)power;
    }
    return true;
}
typedef struct {
    float em, rem; const MediaCtx *mc;
    const ComputedStyle *font_basis; bool actual_font; bool *used_font_units;
    MathType math_type;
    /* Scalar destinations have no length percentage hint. Keep even zero
       percentage tokens distinct through nested expressions and ratios. */
    bool scalar_percentage_type;
    /* Optional scalar/numeric font context, borrowed only for this parse. */
    const ComputedStyle *root_font_basis;
    bool resolve_font_metrics;
    bool *used_root_font_units;
} LCtx;
static float ch_advance(LCtx *c) {
    if (c->used_font_units) *c->used_font_units = true;
    const ComputedStyle *s = c->font_basis;
    const char *orientation = s ? custom_get(s->gen_i, "text-orientation") : NULL;
    bool upright = s && s->writing_mode && orientation && str_ieq(orientation, "upright");
    if (c->em <= 0) return 0;
    if (!c->actual_font) return c->em * (upright ? 1.f : .5f);
    Font *font = font_get(s ? s->font_family : "sans-serif", s ? s->font_weight : 400,
                          s && s->font_style, c->em);
    return font ? font_zero_advance(font, upright) : c->em * (upright ? 1.f : .5f);
}
/* nearest query container's size from the last layout (layout re-marks style when it changes); viewport otherwise */
static float g_cq[2];
/* set when a length used the viewport height, so a height-only viewport change (the on-screen
   keyboard) restyles just those elements */
static bool g_vh_used;
static float cq_dim(const LCtx *c, int h) { if (g_cq[h] > 0) return g_cq[h]; if (h) { g_vh_used = true; return c->mc ? c->mc->vh : 768; } return c->mc ? c->mc->vw : 1024; }
static bool calc_expr(const char **pp, Length *out, LCtx *c);
static float calculation_font_metric(LCtx *c, bool root, bool line_height) {
    const ComputedStyle *s = root ? c->root_font_basis : c->font_basis;
    float size = root ? c->rem : c->em;
    if (c->used_font_units) *c->used_font_units = true;
    if (root && c->used_root_font_units) *c->used_root_font_units = true;
    Font *font = c->actual_font ? font_get(s ? s->font_family : "sans-serif",
        s ? s->font_weight : 400, s && s->font_style, size) : NULL;
    if (line_height) {
        if (s && !s->line_height_normal) return s->line_height;
        return font ? ceilf(font->ascent + font->descent + font->line_gap) : size * 1.2f;
    }
    return font ? font->x_height : size * .5f;
}
static double calculation_progress(double value, double start, double end, bool no_clamp) {
    if (isnan(value) || isnan(start) || isnan(end)) return NAN;
    if (start == end) return !no_clamp || value == start ? 0 : value < start ? -INFINITY : INFINITY;
    double ratio = (value - start) / (end - start);
    return no_clamp || isnan(ratio) ? ratio : fmax(0, fmin(1, ratio));
}
/* CSS Values 4 math functions and constants on resolved (percentage-free) values; angles are in degrees, a
   trig argument without an angle unit in radians */
static bool math_fn(const char **pp, Length *out, LCtx *c) {
    const char *p = *pp; char id[16]; int k = 0;
    while ((isalnum((unsigned char)*p) || (k && *p == '-')) && k < (int)sizeof id - 1) id[k++] = (char)lc(*p++);
    id[k] = 0;
    Length l = { 0, 0, LK_LEN };
    if (*p != '(') {
        if (!strcmp(id, "pi")) l.px = (float)M_PI; else if (!strcmp(id, "e")) l.px = (float)M_E; else if (!strcmp(id, "infinity")) l.px = INFINITY;
        else if (!strcmp(id, "nan")) l.px = NAN; else return false;
        c->math_type = math_type_unit(MATH_NUMBER);
        *out = l; *pp = p; return true;
    }
    static const char *const fns[] = { "round", "mod", "rem", "abs", "sign", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "pow", "sqrt", "hypot", "log", "exp", "progress" };
    int f = -1; for (int i = 0; i < (int)ARRLEN(fns); i++) if (!strcmp(id, fns[i])) f = i;
    if (f < 0) return false;
    p++;
    int strat = 0;   /* round: nearest, up, down, to-zero */
    bool no_clamp = false;
    if (f == 17) {
        const char *q = p; while (is_ws((unsigned char)*q)) q++;
        if (!strncasecmp(q, "no-clamp", 8) && is_ws((unsigned char)q[8])) {
            no_clamp = true; p = q + 8;
        }
    }
    if (f == 0) {
        static const char *const st[] = { "nearest", "up", "down", "to-zero" };
        const char *q = p; while (is_ws((unsigned char)*q)) q++;
        for (int i = 0; i < 4; i++) { size_t n = strlen(st[i]); if (!strncasecmp(q, st[i], n) && (is_ws((unsigned char)q[n]) || q[n] == ',')) { q += n; while (is_ws((unsigned char)*q)) q++; if (*q == ',') { strat = i; p = q + 1; } break; } }
    }
    Length v[4] = {{0}}; MathType types[4]; bool ang[4] = { 0 }; int n = 0;
    while (n < 4) {
        if (!calc_expr(&p, &v[n], c)) return false;
        types[n] = c->math_type;
        ang[n] = math_type_equal(types[n], math_type_unit(MATH_ANGLE));
        if (v[n].mm || (v[n].math_pct_mask & LEN_MATH_EXACT_PROGRESS) ||
            (f > 2 && f != 3 && f != 14 && f != 17 && v[n].pct)) return false;
        n++; while (is_ws((unsigned char)*p)) p++;
        if (*p == ',') { p++; continue; }
        break;
    }
    if (*p != ')') return false;
    p++;
    /* Native stylesheets take this path without CSSOM's JS validator. */
    if ((f == 0 && (n < 1 || n > 2)) ||
        ((f == 1 || f == 2 || f == 11 || f == 12) && n != 2) ||
        ((f == 3 || (f >= 4 && f <= 10) || f == 13 || f == 16) && n != 1) ||
        (f == 15 && (n < 1 || n > 2)) || (f == 17 && n != 3)) return false;
    MathType result_type = types[0], number_type = math_type_unit(MATH_NUMBER);
    if (f == 0 && n == 1) {
        if (!math_type_equal(types[0], number_type)) return false;
        /* The default number interval is real initialized storage even when
           dimensional division leaves the number dependent on percentages. */
        v[1] = L_px(1); types[1] = number_type;
    }
    if (f == 0 || f == 1 || f == 2 || f == 11 || f == 14 || f == 17) {
        for (int i = 1; i < n; i++) if (!math_type_equal(types[0], types[i])) return false;
        if (f == 11) result_type = math_type_unit(MATH_ANGLE);
        if (f == 17) {
            int dimensions = 0;
            for (int i = 0; i < MATH_DIMENSIONS; i++) {
                if (types[0].power[i] && types[0].power[i] != 1) return false;
                dimensions += types[0].power[i] != 0;
            }
            if (dimensions > 1) return false;
            result_type = number_type;
        }
    } else if (f == 4) result_type = number_type;
    else if (f >= 5 && f <= 7) {
        if (!ang[0] && !math_type_equal(types[0], number_type)) return false;
        result_type = number_type;
    } else if (f >= 8 && f <= 10) {
        if (!math_type_equal(types[0], number_type)) return false;
        result_type = math_type_unit(MATH_ANGLE);
    } else if (f != 3) {
        for (int i = 0; i < n; i++) if (!math_type_equal(types[i], number_type)) return false;
        result_type = number_type;
    }
    bool percentage_math = false, percentage_provenance = false;
    if (f == 3 || f == 14) {
        bool pixels = false;
        for (int i = 0; i < n; i++) {
            percentage_math |= v[i].pct != 0;
            percentage_provenance |= v[i].pctu;
            pixels |= v[i].px != 0;
        }
        /* A cancelled 0% coefficient is constant even though its provenance
           remains relevant to typing. Pure percentages keep their unit;
           genuinely mixed nonlinear operands remain unsupported. */
        percentage_math |= percentage_provenance && !pixels;
        if (percentage_math && pixels) return false;
    }
    uint8_t stepped = f == 1 ? LEN_MATH_MOD : f == 2 ? LEN_MATH_REM :
        strat == 1 ? LEN_MATH_UP : strat == 2 ? LEN_MATH_DOWN :
        strat == 3 ? LEN_MATH_TO_ZERO : LEN_MATH_ROUND;
    if (f <= 2 && (v[0].pct || v[1].pct)) {
        /* A layout reference is required. Keep exact affine operands rather
           than substituting a guessed parent width at computed-value time. */
        l = v[0]; l.mm = stepped;
        l.px2 = v[1].px; l.pct2 = v[1].pct; l.px3 = l.pct3 = 0;
        l.math_pct_mask = (v[0].pctu ? 1 : 0) | (v[1].pctu ? 2 : 0);
        l.pctu = v[0].pctu || v[1].pctu;
        c->math_type = result_type; *out = l; *pp = p; return true;
    }
    double a = percentage_math ? v[0].pct : v[0].px;
    double b = n > 1 ? (percentage_math ? v[1].pct : v[1].px) : 1;
    double end = n > 2 ? v[2].px : 0, r, rad = M_PI / 180;
    bool has_nan = false;
    for (int i = 0; i < n; i++) has_nan |= isnan(v[i].px) || ((f == 17 || percentage_math) && isnan(v[i].pct));
    if (f == 17 && !has_nan && (v[0].pct || v[1].pct || v[2].pct)) {
        bool pure_percentage = true;
        for (int i = 0; i < 3; i++) pure_percentage &= v[i].px == 0;
        if (pure_percentage) {
            a = v[0].pct; b = v[1].pct; end = v[2].pct;
        } else {
            /* A constant finite denominator leaves an exact affine ratio.
               Existing clamp storage preserves its used-value dependence;
               no guessed layout basis or retained expression tree is needed. */
            double denominator = (double)v[2].px - v[1].px;
            if (v[2].pct != v[1].pct || !isfinite(denominator)) return false;
            for (int i = 0; i < 3; i++) if (!isfinite(v[i].px) || !isfinite(v[i].pct)) return false;
            if (denominator == 0) {
                if (no_clamp) return false;
                c->math_type = result_type; *out = L_px(0); *pp = p; return true;
            }
            l.px = (float)(((double)v[0].px - v[1].px) / denominator);
            l.pct = (float)(((double)v[0].pct - v[1].pct) / denominator);
            if (!isfinite(l.px) || !isfinite(l.pct)) return false;
            l.pctu = l.pct != 0;
            if (!l.pct) l.px = (float)calculation_progress(l.px, 0, 1, no_clamp);
            else if (!no_clamp) { l.mm = 3; l.px2 = 0; l.px3 = 1; l.math_pct_mask = 1; }
            if (l.pct) l.math_pct_mask |= LEN_MATH_EXACT_PROGRESS;
            c->math_type = result_type; *out = l; *pp = p; return true;
        }
    }
    if (has_nan) r = NAN;
    else switch (f) {
    case 0: case 1: case 2: r = len_stepped_value(a, b, stepped); break;
    case 3: r = fabs(a); break;
    case 4: r = a > 0 ? 1 : a < 0 ? -1 : a; break;
    case 5: r = sin(ang[0] ? a * rad : a); break;
    case 6: r = cos(ang[0] ? a * rad : a); break;
    case 7: r = tan(ang[0] ? a * rad : a); break;
    case 8: r = asin(a) / rad; break;
    case 9: r = acos(a) / rad; break;
    case 10: r = atan(a) / rad; break;
    case 11: r = atan2(a, b) / rad; break;
    case 12: r = pow(a, b); break;
    case 13: r = sqrt(a); break;
    case 14: { double magnitude = 0; for (int i = 0; i < n; i++) magnitude = hypot(magnitude, percentage_math ? v[i].pct : v[i].px); r = magnitude; break; }
    case 15: r = n > 1 ? log(a) / log(b) : log(a); break;
    case 16: r = exp(a); break;
    default: r = calculation_progress(a, b, end, no_clamp); break;
    }
    c->math_type = result_type;
    if (percentage_math) { l.pct = (float)r; l.pctu = true; }
    else l.px = (float)r;
    if (percentage_provenance) l.pctu = true;
    if (f <= 2) l.pctu = v[0].pctu || v[1].pctu;
    *out = l; *pp = p; return true;
}
static bool parse_dim(const char **pp, Length *out, LCtx *c) {
    const char *p = *pp; while (is_ws((unsigned char)*p)) p++;
    if (*p == '(') { p++; if (!calc_expr(&p, out, c)) return false; while (is_ws((unsigned char)*p)) p++; if (*p == ')') p++; *pp = p; return true; }
    if (str_istarts(p, "calc(") || str_istarts(p, "-webkit-calc(")) { p = strchr(p, '(') + 1; if (!calc_expr(&p, out, c)) return false; while (is_ws((unsigned char)*p)) p++; if (*p == ')') p++; *pp = p; return true; }
    if (str_istarts(p, "min(") || str_istarts(p, "max(") || str_istarts(p, "clamp(")) {
        int kind = lc(p[1]) == 'i' ? 0 : lc(p[1]) == 'a' ? 1 : 2;
        p = strchr(p, '(') + 1; Length v[3]; MathType common_type; int n = 0;
        while (n < 3) {
            if (!calc_expr(&p, &v[n], c)) return false;
            /* The compact comparison form cannot contain a nonlinear child. */
            if (len_math_stepped(v[n].mm)) return false;
            if (n && !math_type_equal(common_type, c->math_type)) return false;
            common_type = c->math_type;
            n++; while (is_ws((unsigned char)*p)) p++;
            if (*p == ',') { if (kind == 2 && n == 3) return false; p++; continue; }
            break;
        }
        /* Unlike min/max, clamp has exactly three arguments. Never turn a
           malformed two-arm value into a different comparison operation. */
        if (kind == 2 && (n != 3 || *p != ')')) return false;
        if (*p == ')') p++;
        *pp = p;
        /* A second nonlinear parent cannot be encoded in this compact
           deferred-progress representation. Keep legacy cases unchanged. */
        for (int i = 0; i < n; i++) if (v[i].math_pct_mask & LEN_MATH_EXACT_PROGRESS) return false;
        if (kind == 2 && n == 3 && !v[0].mm && !v[1].mm && !v[2].mm) {
            Length r = v[1];
            r.mm = 3; r.px2 = v[0].px; r.pct2 = v[0].pct;
            r.px3 = v[2].px; r.pct3 = v[2].pct;
            r.math_pct_mask = (v[1].pctu ? 1 : 0) | (v[0].pctu ? 2 : 0) | (v[2].pctu ? 4 : 0);
            r.pctu |= v[0].pctu || v[2].pctu;
            /* A constant clamp can be folded now; otherwise both bounds
               must remain available after a containing-block resize. */
            if (!len_has_pct(r) && !r.pctu) r = L_px(len_resolve(r, 0));
            *out = r; return true;
        }
        /* More deeply nested comparison expressions retain the legacy
           approximation. Three affine clamp arms are represented exactly;
           this compact Length is not a general CSS expression tree. */
        if (kind == 2 && n == 3) { Length r = v[1]; if (!r.pct && !v[0].pct && r.px < v[0].px) r = v[0]; if (!r.pct && !v[2].pct && r.px > v[2].px) r = v[2]; *out = r; return true; }
        Length best = v[0]; int bi = 0;
        for (int i = 1; i < n; i++) { if (v[i].pct || best.pct) { if (kind == 0 && v[i].pct && !best.pct) {} else if (!v[i].pct && kind != 0) { best = v[i]; bi = i; } continue; } if (kind == 0 ? v[i].px < best.px : v[i].px > best.px) { best = v[i]; bi = i; } }
        *out = best;
        if (n == 2 && (v[0].pct || v[1].pct) && !v[0].mm && !v[1].mm) { const Length *o = &v[1 - bi]; out->mm = (uint8_t)(kind + 1); out->px2 = o->px; out->pct2 = o->pct; out->math_pct_mask = (out->pctu ? 1 : 0) | (o->pctu ? 2 : 0); out->pctu |= o->pctu; }
        return true;
    }
    if (str_istarts(p, "var(")) return false;
    if (isalpha((unsigned char)*p)) { *pp = p; return math_fn(pp, out, c); }
    char *e; float v = strtof(p, &e);
    if (e == p) return false;
    p = e;
    Length l = { 0, 0, LK_LEN };
    char u[8] = {0}; int k = 0; while (isalpha((unsigned char)*p) && k < 7) u[k++] = (char)lc(*p++);
    int unit = MATH_LENGTH;
    if (*p == '%') { l.pct = v; l.pctu = true; if (c->scalar_percentage_type) unit = MATH_PERCENT; p++; }
    else if (!k || !strcmp(u, "px")) { l.px = v; if (!k) unit = MATH_NUMBER; }
    else if (!strcmp(u, "em")) l.px = v * c->em;
    else if (!strcmp(u, "rem")) l.px = v * c->rem;
    else if (!strcmp(u, "ex")) l.px = v * (c->resolve_font_metrics ? calculation_font_metric(c, false, false) : c->em * .5f);
    else if (!strcmp(u, "rex")) {
        if (!c->resolve_font_metrics || !c->root_font_basis) return false;
        l.px = v * calculation_font_metric(c, true, false);
    }
    else if (!strcmp(u, "ch")) l.px = v * ch_advance(c);
    else if (!strcmp(u, "lh")) l.px = v * (c->resolve_font_metrics ? calculation_font_metric(c, false, true) : c->em * 1.2f);
    else if (!strcmp(u, "cap") || !strcmp(u, "ic")) l.px = v * c->em * .7f;
    else if (!strcmp(u, "pt")) l.px = v * 4.f / 3.f;
    else if (!strcmp(u, "pc")) l.px = v * 16;
    else if (!strcmp(u, "in")) l.px = v * 96;
    else if (!strcmp(u, "cm")) l.px = v * 96 / 2.54f;
    else if (!strcmp(u, "mm")) l.px = v * 96 / 25.4f;
    else if (!strcmp(u, "q")) l.px = v * 96 / 101.6f;
    else if (!strcmp(u, "vw") || !strcmp(u, "svw") || !strcmp(u, "lvw") || !strcmp(u, "dvw")) l.px = v * (c->mc ? c->mc->vw : 1024) / 100;
    else if (!strcmp(u, "vh") || !strcmp(u, "svh") || !strcmp(u, "lvh") || !strcmp(u, "dvh")) { l.px = v * (c->mc ? c->mc->vh : 768) / 100; g_vh_used = true; }
    else if (!strcmp(u, "cqw") || !strcmp(u, "cqi")) l.px = v * cq_dim(c, 0) / 100;
    else if (!strcmp(u, "cqh") || !strcmp(u, "cqb")) l.px = v * cq_dim(c, 1) / 100;
    else if (!strcmp(u, "cqmin")) l.px = v * LMIN(cq_dim(c, 0), cq_dim(c, 1)) / 100;
    else if (!strcmp(u, "cqmax")) l.px = v * LMAX(cq_dim(c, 0), cq_dim(c, 1)) / 100;
    else if (!strcmp(u, "vmin")) { l.px = v * LMIN(c->mc ? c->mc->vw : 1024, c->mc ? c->mc->vh : 768) / 100; g_vh_used = true; }
    else if (!strcmp(u, "vmax")) { l.px = v * LMAX(c->mc ? c->mc->vw : 1024, c->mc ? c->mc->vh : 768) / 100; g_vh_used = true; }
    else if (!strcmp(u, "fr")) { l.px = v; unit = MATH_FLEX; }
    else if (!strcmp(u, "deg")) { l.px = v; unit = MATH_ANGLE; }
    else if (!strcmp(u, "s")) { l.px = v; unit = MATH_TIME; }
    else if (!strcmp(u, "ms")) { l.px = v / 1000.f; unit = MATH_TIME; }
    else if (!strcmp(u, "rad")) { l.px = v * 180 / (float)M_PI; unit = MATH_ANGLE; }
    else if (!strcmp(u, "grad")) { l.px = v * 0.9f; unit = MATH_ANGLE; }
    else if (!strcmp(u, "turn")) { l.px = v * 360; unit = MATH_ANGLE; }
    else return false;
    c->math_type = math_type_unit(unit);
    *out = l; *pp = p; return true;
}
static bool calc_scale(Length *value, float scale) {
    /* Homogeneity is exact for a finite nonzero multiplier. Zero and
       nonfinite multipliers require a full expression tree; never turn
       a finite rounded result times zero into round(0,0). */
    if (len_math_stepped(value->mm) && (!isfinite(scale) || scale == 0)) return false;
    if ((value->math_pct_mask & LEN_MATH_EXACT_PROGRESS) && !isfinite(scale)) return false;
    value->px *= scale;
    /* An absent percentage coefficient is not an authored zero operand:
       multiplying it by infinity would manufacture a NaN dependency. */
    if (value->pct || value->pctu) value->pct *= scale;
    if (value->mm) { value->px2 *= scale; if (value->pct2 || (value->math_pct_mask & 2)) value->pct2 *= scale; }
    if (value->mm >= 3) { value->px3 *= scale; if (value->pct3 || (value->math_pct_mask & 4)) value->pct3 *= scale; }
    if (scale < 0) value->mm = len_math_flip(value->mm);
    if ((value->math_pct_mask & LEN_MATH_EXACT_PROGRESS) &&
        (!isfinite(value->px) || !isfinite(value->pct) ||
         (value->mm && (!isfinite(value->px2) || !isfinite(value->pct2))) ||
         (value->mm >= 3 && (!isfinite(value->px3) || !isfinite(value->pct3))))) return false;
    return true;
}
static bool calc_add_linear(Length *value, float px, float pct, bool pctu) {
    if ((value->math_pct_mask & LEN_MATH_EXACT_PROGRESS) && (!isfinite(px) || !isfinite(pct))) return false;
    if (len_math_stepped(value->mm)) {
        value->px3 += px; value->pct3 += pct;
        if (pctu) { value->pctu = true; value->math_pct_mask |= 4; }
        return !(value->math_pct_mask & LEN_MATH_EXACT_PROGRESS) ||
            (isfinite(value->px) && isfinite(value->pct) &&
             isfinite(value->px2) && isfinite(value->pct2) &&
             isfinite(value->px3) && isfinite(value->pct3));
    }
    value->px += px; value->pct += pct;
    if (value->mm) { value->px2 += px; value->pct2 += pct; }
    if (value->mm >= 3) { value->px3 += px; value->pct3 += pct; }
    if (pctu) { value->pctu = true; if (value->mm) value->math_pct_mask |= value->mm >= 3 ? 7 : 3; }
    if ((value->math_pct_mask & LEN_MATH_EXACT_PROGRESS) &&
        (!isfinite(value->px) || !isfinite(value->pct) ||
         (value->mm && (!isfinite(value->px2) || !isfinite(value->pct2))) ||
         (value->mm >= 3 && (!isfinite(value->px3) || !isfinite(value->pct3))))) return false;
    return true;
}
static bool calc_term(const char **pp, Length *out, LCtx *c) {
    if (!parse_dim(pp, out, c)) return false;
    for (;;) {
        const char *p = *pp; while (is_ws((unsigned char)*p)) p++;
        if (*p == '*' || *p == '/') {
            char op = *p++; MathType left_type = c->math_type;
            Length r; if (!parse_dim(&p, &r, c)) return false;
            if (!math_type_product(left_type, c->math_type, op == '*' ? 1 : -1, &c->math_type)) return false;
            if (op == '*' && (out->mm || r.mm)) {
                if (r.mm && !out->mm && !out->pct) { float k = out->px; *out = r; r = L_px(k); }
                if (!r.mm && !r.pct && !r.pctu) { if (!calc_scale(out, r.px)) return false; *pp = p; continue; }
                if (len_math_stepped(out->mm) || len_math_stepped(r.mm) ||
                    ((out->math_pct_mask | r.math_pct_mask) & LEN_MATH_EXACT_PROGRESS)) return false;
                out->mm = 0;
            }
            if (op == '*') {
                if (r.pct || r.pctu) {
                    if (out->pct || out->pctu || out->mm) return false;
                    float k = out->px; *out = r; if (!calc_scale(out, k)) return false;
                } else if (!calc_scale(out, r.px)) return false;
            }
            else { if (r.mm || r.pct || r.pctu) return false; if (!calc_scale(out, 1.f / r.px)) return false; }
            *pp = p;
        } else break;
    }
    return true;
}
static bool calc_expr(const char **pp, Length *out, LCtx *c) {
    if (!calc_term(pp, out, c)) return false;
    for (;;) {
        const char *p = *pp; while (is_ws((unsigned char)*p)) p++;
        if ((*p == '+' || *p == '-') && (p[1] == ' ' || p[1] == '\t' || p[1] == '\n')) {
            char op = *p++; MathType left_type = c->math_type;
            Length r; if (!calc_term(&p, &r, c)) return false;
            if (!math_type_equal(left_type, c->math_type)) return false;
            if (op == '-' && !calc_scale(&r, -1)) return false;
            uint8_t exact = (out->math_pct_mask | r.math_pct_mask) & LEN_MATH_EXACT_PROGRESS;
            if (out->mm && r.mm && (len_math_stepped(out->mm) || len_math_stepped(r.mm) || exact)) return false;
            if (r.mm && !out->mm) {
                float bx = out->px, bp = out->pct; bool pu = out->pctu; *out = r; out->math_pct_mask |= exact;
                if (!calc_add_linear(out, bx, bp, pu)) return false;
            } else {
                if (r.mm) out->mm = 0;
                out->math_pct_mask |= exact;
                if (!calc_add_linear(out, r.px, r.pct, r.pctu)) return false;
            }
            out->pctu |= r.pctu;
            *pp = p;
        } else break;
    }
    return true;
}
static bool css_parse_length_context(const char *s, Length *out, float em, float rem, const MediaCtx *mc,
                                     const ComputedStyle *basis, bool actual, bool *used, MathType *type_out) {
    if (type_out) *type_out = math_type_unit(MATH_LENGTH);
    while (is_ws((unsigned char)*s)) s++;
    if (str_istarts(s, "auto")) { *out = L_auto(); return true; }
    if (str_istarts(s, "none")) { *out = (Length){0, 0, LK_NONE}; return true; }
    if (str_istarts(s, "min-content")) { *out = (Length){0, 0, LK_MIN_CONTENT}; return true; }
    if (str_istarts(s, "max-content")) { *out = (Length){0, 0, LK_MAX_CONTENT}; return true; }
    if (str_istarts(s, "fit-content") || str_istarts(s, "-webkit-fit-content") || str_istarts(s, "-moz-fit-content")) { *out = (Length){0, 0, LK_FIT_CONTENT}; return true; }
    LCtx c = { em, rem, mc, basis, actual, used };
    const char *p = s; Length parsed;
    if (!parse_dim(&p, &parsed, &c)) return false;
    /* Length consumers must not accept another dimension merely because its
       numeric representation uses the same px slot. Preserve unitless callers. */
    if (!math_type_equal(c.math_type, math_type_unit(MATH_LENGTH)) &&
        !math_type_equal(c.math_type, math_type_unit(MATH_NUMBER))) return false;
    while (is_ws((unsigned char)*p)) p++;
    if (*p) return false;
    if (type_out) *type_out = c.math_type;
    *out = parsed;
    return true;
}

bool css_parse_length(const char *s, Length *out, float em, float rem, const MediaCtx *mc) {
    return css_parse_length_context(s, out, em, rem, mc, NULL, false, NULL, NULL);
}

/* Typed scalar consumers share the same evaluator as native length math. */
static bool css_scalar_math_context(const char *text, int unit, LCtx c, float *out) {
    c.scalar_percentage_type = true;
    Length value; const char *p = text;
    if (!parse_dim(&p, &value, &c) || value.mm || value.pct || value.pctu ||
        !math_type_equal(c.math_type, math_type_unit(unit))) return false;
    while (is_ws((unsigned char)*p)) p++;
    if (*p) return false;
    *out = value.px; return true;
}
static bool css_scalar_math(const char *text, int unit, float em, float rem, float *out) {
    LCtx c = { .em = em, .rem = rem };
    return css_scalar_math_context(text, unit, c, out);
}

/* ---------------- var() substitution ---------------- */
typedef struct RegProp { char *name, *init; struct RegProp *next; } RegProp;
static RegProp *g_regprops[256];
static uint64_t g_regprop_version; /* a sheet's @property can change any unset var() */
static unsigned regprop_hash(const char *s) { unsigned h = 5381; while (*s) h = h * 33 + (unsigned char)*s++; return h & 255; }
typedef struct PTry { char *name, *body; struct PTry *next; } PTry;
static PTry *g_ptry;
void css_register_position_try(const char *name, const char *body) {
    if (!name || strncmp(name, "--", 2) || !body) return;
    for (PTry *p = g_ptry; p; p = p->next) if (!strcmp(p->name, name)) { free(p->body); p->body = xstrdup(body); return; }
    PTry *p = xmalloc(sizeof *p); p->name = xstrdup(name); p->body = xstrdup(body); p->next = g_ptry; g_ptry = p;
}
ComputedStyle *css_position_try_style(const ComputedStyle *s, const char *name, Node *el) {
    static const char *const ok[] = { "top", "right", "bottom", "left", "inset", "inset-block", "inset-inline", "inset-block-start", "inset-block-end",
        "inset-inline-start", "inset-inline-end", "margin", "margin-top", "margin-right", "margin-bottom", "margin-left", "margin-block", "margin-inline",
        "margin-block-start", "margin-block-end", "margin-inline-start", "margin-inline-end", "width", "height", "min-width", "min-height", "max-width",
        "max-height", "inline-size", "block-size", "min-inline-size", "min-block-size", "max-inline-size", "max-block-size", "justify-self", "align-self",
        "place-self", "position-anchor", "position-area", NULL };
    const PTry *p = g_ptry; while (name && p && strcmp(p->name, name)) p = p->next;
    if (name && !p) return NULL;
    ComputedStyle *t = xmalloc(sizeof *t); *t = *s;
    /* the fields the allowed declarations may replace are owned by the copy */
    for (int i = 0; i < 4; i++) t->anchor_ins[i] = s->anchor_ins[i] ? xstrdup(s->anchor_ins[i]) : NULL;
    for (int i = 0; i < 2; i++) t->anchor_sz[i] = s->anchor_sz[i] ? xstrdup(s->anchor_sz[i]) : NULL;
    t->pos_area = s->pos_area ? xstrdup(s->pos_area) : NULL; t->pos_anchor = s->pos_anchor ? xstrdup(s->pos_anchor) : NULL;
    const ComputedStyle *par = el && el->parent && el->parent->type == NODE_ELEMENT ? el->parent->style : NULL;
    char *b = xstrdup(name ? p->body : "");
    for (char *c0; (c0 = strstr(b, "/*")); ) { char *c1 = strstr(c0 + 2, "*/"); if (!c1) { *c0 = 0; break; } memmove(c0, c1 + 2, strlen(c1 + 2) + 1); }
    for (char *d = b, *n; d; d = n) {
        n = strchr(d, ';'); if (n) *n++ = 0;
        char *c = strchr(d, ':'); if (!c) continue;
        *c = 0; char *k = str_trim(d), *v = str_trim(c + 1), *imp = strstr(v, "!important"); if (imp) { *imp = 0; v = str_trim(v); }
        for (int i = 0; ok[i]; i++) if (!strcmp(k, ok[i])) { css_apply_decl(t, par, k, v, NULL, el); break; }
    }
    free(b);
    return t;
}
void css_position_try_free(ComputedStyle *t) {
    if (!t) return;
    for (int i = 0; i < 4; i++) free(t->anchor_ins[i]);
    free(t->anchor_sz[0]); free(t->anchor_sz[1]); free(t->pos_area); free(t->pos_anchor); free(t);
}
void css_register_property(const char *name, const char *initial) {
    if (!name || strncmp(name, "--", 2) || !initial) return;
    RegProp **b = &g_regprops[regprop_hash(name)];
    for (RegProp *r = *b; r; r = r->next) if (!strcmp(r->name, name)) {
        if (strcmp(r->init, initial)) { free(r->init); r->init = xstrdup(initial); g_regprop_version++; }
        return;
    }
    g_regprop_version++;
    RegProp *r = xcalloc(1, sizeof *r); r->name = xstrdup(name); r->init = xstrdup(initial); r->next = *b; *b = r;
}
const char *css_property_initial(const char *name) {
    for (RegProp *r = g_regprops[regprop_hash(name)]; r; r = r->next) if (!strcmp(r->name, name)) return r->init;
    return NULL;
}
static uint64_t custom_bit(const char *name) { uint32_t h = hash_str(name, strlen(name)); return UINT64_C(1) << ((h ^ (h >> 16)) & 63); }
/* 256 bits keep false var() dependency hits rare for Tailwind elements reading ~20 names. */
static void cm_add(CustomMask *m, uint32_t hash) { unsigned b = (hash ^ (hash >> 16)) & 255; m->w[b >> 6] |= UINT64_C(1) << (b & 63); }
static bool cm_has(const CustomMask *m, uint32_t hash) { unsigned b = (hash ^ (hash >> 16)) & 255; return (m->w[b >> 6] >> (b & 63)) & 1; }
static void cm_add_name(CustomMask *m, const char *name) { cm_add(m, hash_str(name, strlen(name))); }
static bool cm_any(const CustomMask *a, const CustomMask *b) {
    return ((a->w[0] & b->w[0]) | (a->w[1] & b->w[1]) | (a->w[2] & b->w[2]) | (a->w[3] & b->w[3])) != 0;
}
static void cm_or(CustomMask *a, const CustomMask *b) { for (int i = 0; i < 4; i++) a->w[i] |= b->w[i]; }
/* Immutable custom-property resolution. Only selected fallbacks add edges. */
#define VAR_EXPANSION_LIMIT (1u << 16)
#define VAR_RECURSION_LIMIT 256
typedef struct VarEntry {
    const char *name, *raw;
    char *value;
    unsigned state;
    bool cycle;
} VarEntry;
typedef struct VarResolver {
    ComputedStyle *style;
    const ComputedStyle *parent;
    const CustomProps *origin_base;
    HMap entries;
    VEC(VarEntry *) stack;
} VarResolver;
typedef struct VarOutput {
    SB text;
    size_t last_start, last_length;
    int last_type;
    bool have_token, separated;
} VarOutput;
static bool var_boundary_merges(const VarOutput *out, const CTok *first) {
    SB joined = {0};
    sb_put(&joined, out->text.s + out->last_start, out->last_length);
    sb_put(&joined, first->start, first->len);
    CLexer l; clex_init(&l, joined.s, joined.n);
    CTok a = clex_next(&l), b = clex_next(&l);
    bool merges = a.start != joined.s || a.type != out->last_type ||
        a.len != out->last_length || b.type != first->type ||
        b.start != joined.s + out->last_length || b.len != first->len;
    free(a.str); free(b.str); sb_free(&joined);
    return merges;
}
static bool var_append(VarOutput *out, const char *source, size_t length) {
    if (!length) return true;
    CLexer l; clex_init(&l, source, length);
    CTok first = {0}, last = {0}; bool have = false;
    for (;;) {
        CTok t = clex_next(&l); int type = t.type;
        if (type != CT_EOF && type != CT_WS) {
            if (!have) first = t;
            last = t; have = true;
        }
        free(t.str);
        if (type == CT_EOF) break;
    }
    bool separator = have && out->have_token && !out->separated &&
        first.start == source && var_boundary_merges(out, &first);
    if (out->text.n + length + (separator ? 4 : 0) > VAR_EXPANSION_LIMIT) return false;
    if (separator) sb_puts(&out->text, "/**/");
    size_t start = out->text.n; sb_put(&out->text, source, length);
    if (have) {
        out->have_token = true;
        out->last_start = start + (size_t)(last.start - source);
        out->last_length = last.len; out->last_type = last.type;
        out->separated = (size_t)(last.start - source) + last.len < length;
    } else out->separated = true;
    return true;
}
static char *var_substitute(VarResolver *, VarEntry *, const char *, unsigned);
static const char *var_resolve_entry(VarResolver *r, VarEntry *entry, unsigned depth) {
    if (entry->state == 2) return entry->value;
    if (entry->state == 1) {
        bool cycle = false;
        for (int i = 0; i < r->stack.n; i++) {
            if (r->stack.v[i] == entry) cycle = true;
            if (cycle) r->stack.v[i]->cycle = true;
        }
        return NULL;
    }
    if (depth > VAR_RECURSION_LIMIT) { entry->state = 2; return NULL; }
    entry->state = 1; vec_push(r->stack, entry);
    char *value = var_substitute(r, entry, entry->raw, depth + 1);
    r->stack.n--;
    if (entry->cycle) { free(value); value = NULL; }
    if (value) {
        CLexer l; clex_init(&l, value, strlen(value)); CTok keyword, after;
        do { keyword = clex_next(&l); if (keyword.type == CT_WS) free(keyword.str); } while (keyword.type == CT_WS);
        do { after = clex_next(&l); if (after.type == CT_WS) free(after.str); } while (after.type == CT_WS);
        if (keyword.type == CT_IDENT && keyword.str && after.type == CT_EOF) {
            if (str_ieq(keyword.str, "initial")) { free(value); value = NULL; }
            else if (str_ieq(keyword.str, "inherit") || str_ieq(keyword.str, "unset") || str_ieq(keyword.str, "revert")) {
                cm_add_name(&r->style->custom_dependencies, entry->name);
                const char *parent = str_ieq(keyword.str, "revert") ? custom_get(r->origin_base, entry->name) :
                    r->parent ? custom_get(r->parent->custom, entry->name) : NULL;
                free(value); value = parent && *parent ? xstrdup(parent) : NULL;
            }
        }
        free(keyword.str); free(after.str);
    }
    if (value && !*value) { free(value); value = xstrdup(" "); }
    entry->value = value; entry->state = 2; return value;
}
static const char *var_lookup(VarResolver *r, const char *name, unsigned depth) {
    cm_add_name(&r->style->custom_dependencies, name);
    VarEntry *entry = hm_get(&r->entries, name);
    if (entry) return var_resolve_entry(r, entry, depth);
    const char *value = custom_get(r->style->custom, name);
    if (!value) value = css_property_initial(name);
    return value && *value ? value : NULL;
}
static char *var_substitute(VarResolver *r, VarEntry *owner, const char *source, unsigned depth) {
    if (depth > VAR_RECURSION_LIMIT) return NULL;
    if (!css_value_has_var(source)) return xstrdup(source);
    CLexer l; clex_init(&l, source, strlen(source));
    VarOutput out = {0}; size_t copied = 0; bool invalid = false;
    for (;;) {
        CTok t = clex_next(&l); int type = t.type;
        bool is_var = type == CT_FUNC && t.str && str_ieq(t.str, "var");
        free(t.str);
        if (type == CT_EOF) break;
        if (!is_var) continue;
        size_t start = (size_t)(t.start - source), argument_start = l.i;
        if (!var_append(&out, source + copied, start - copied)) invalid = true;
        size_t argument_end, fallback, content_end;
        if (!css_var_arguments(&l, &argument_end, &fallback, &content_end)) { invalid = true; break; }
        copied = l.i;
        char *argument = css_var_argument(source + argument_start, argument_end - argument_start, false);
        char *name_value = argument ? var_substitute(r, owner, argument, depth + 1) : NULL;
        free(argument); char *name = NULL;
        if (name_value) {
            CLexer names; clex_init(&names, name_value, strlen(name_value)); CTok first, end;
            do { first = clex_next(&names); if (first.type == CT_WS) free(first.str); } while (first.type == CT_WS);
            do { end = clex_next(&names); if (end.type == CT_WS) free(end.str); } while (end.type == CT_WS);
            if (first.type == CT_IDENT && first.str && first.str[0] == '-' && first.str[1] == '-' && first.str[2] && end.type == CT_EOF) {
                name = first.str; first.str = NULL;
            }
            free(first.str); free(end.str);
        }
        free(name_value);
        const char *value = name ? var_lookup(r, name, depth + 1) : NULL;
        char *owned = NULL;
        /* Continue direct siblings after invalidity. A cycle-marked owner
         * must not introduce new edges through a fallback. */
        if (!value && !(owner && owner->cycle) && fallback != SIZE_MAX) {
            char *raw = css_var_argument(source + fallback, content_end - fallback, true);
            owned = raw ? var_substitute(r, owner, raw, depth + 1) : NULL;
            free(raw); value = owned;
        }
        if (!value || (owner && owner->cycle)) invalid = true;
        else if (!var_append(&out, value, strlen(value))) invalid = true;
        free(owned); free(name);
    }
    if (!var_append(&out, source + copied, strlen(source) - copied)) invalid = true;
    if (invalid) { sb_free(&out.text); return NULL; }
    return sb_take(&out.text);
}
static char *subst_vars(const char *value, ComputedStyle *style, int depth) {
    VarResolver r = { .style = style };
    return var_substitute(&r, NULL, value, (unsigned)depth);
}
static void custom_resolve_values(ComputedStyle *style, const ComputedStyle *parent, const CustomProps *origin_base) {
    if (!style->custom || (parent && style->custom == parent->custom)) return;
    VarResolver r = { .style = style, .parent = parent, .origin_base = origin_base };
    VEC(VarEntry *) values = {0};
    hm_foreach(&style->custom->map, item) {
        const char *raw = item->val;
        if (!css_value_has_var(raw)) continue;
        VarEntry *entry = xcalloc(1, sizeof *entry); entry->name = item->key; entry->raw = raw;
        hm_put(&r.entries, item->key, entry); vec_push(values, entry);
    }
    for (int i = 0; i < values.n; i++) var_resolve_entry(&r, values.v[i], 0);
    for (int i = 0; i < values.n; i++) {
        VarEntry *entry = values.v[i];
        char *old = hm_get(&style->custom->map, entry->name);
        hm_put(&style->custom->map, entry->name, entry->value ? entry->value : xstrdup(""));
        free(old); free(entry);
    }
    hm_free(&r.entries, NULL); vec_free(values); vec_free(r.stack);
}
/* CSS-wide values are dispatched by the existing keyword paths, not by the
 * invalid-at-computed-value fallback. Full rule/layer rollback still requires
 * cascade provenance beyond this value parser. */
static const char *var_wide_keyword(const char *value) {
    static const char *const names[] = { "initial", "inherit", "unset", "revert", "revert-layer", "revert-rule" };
    CLexer l; clex_init(&l, value, strlen(value)); CTok first, end;
    do { first = clex_next(&l); if (first.type == CT_WS) free(first.str); } while (first.type == CT_WS);
    do { end = clex_next(&l); if (end.type == CT_WS) free(end.str); } while (end.type == CT_WS);
    const char *keyword = NULL;
    if (first.type == CT_IDENT && first.str && end.type == CT_EOF)
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (str_ieq(first.str, names[i])) { keyword = names[i]; break; }
    free(first.str); free(end.str); return keyword;
}
/* Convert tokenizer-skipped comments into spaces for native value parsers;
 * this preserves distinct tokens after var() substitution. */
static char *var_parser_text(const char *value) {
    CLexer l; clex_init(&l, value, strlen(value)); SB out = {0}; size_t copied = 0;
    for (;;) {
        CTok t = clex_next(&l); int type = t.type;
        size_t offset = (size_t)(t.start - value);
        if (offset > copied) sb_putc(&out, ' ');
        if (type != CT_EOF) sb_put(&out, t.start, t.len);
        copied = l.i; free(t.str);
        if (type == CT_EOF) break;
    }
    return sb_take(&out);
}



/* ---------------- property application ---------------- */
static int kw(const char *v, const char *const *list) { for (int i = 0; list[i]; i++) if (str_ieq(v, list[i])) return i; return -1; }
static char *next_token(const char **pp) {
    const char *p = *pp; while (is_ws((unsigned char)*p)) p++;
    if (!*p) return NULL;
    const char *s = p; int d = 0;
    /* a function ends its token even without whitespace after it: minifiers write minmax(0,1fr)10px */
    while (*p && (d || !is_ws((unsigned char)*p))) { if (*p == '(') d++; else if (*p == ')' && !--d) { p++; break; } else if (*p == '"' || *p == '\'') { char q = *p++; while (*p && *p != q) p++; if (!*p) break; } p++; }
    *pp = p; return xstrndup(s, (size_t)(p - s));
}
static int split_ws(const char *v, char **out, int max) { int n = 0; const char *p = v; char *t; while (n < max && (t = next_token(&p))) out[n++] = t; return n; }
static void free_toks(char **t, int n) { for (int i = 0; i < n; i++) free(t[i]); }

typedef struct { ComputedStyle *st; const ComputedStyle *par; StyleEngine *e; Node *el; float em, rem; const MediaCtx *mc; bool validation_only; } ACtx;
static LCtx application_scalar_context(ACtx *c, bool *root_used) {
    const ComputedStyle *root = c->e && c->e->doc && c->e->doc->html ? c->e->doc->html->style : NULL;
    if (c->e && c->e->doc && c->el == c->e->doc->html) root = c->st;
    LCtx result = { .em = c->em, .rem = root ? root->font_size : c->rem, .mc = c->mc,
        .font_basis = c->st, .actual_font = c->st != NULL && !c->validation_only,
        .used_font_units = c->e ? &c->e->uses_font_units : NULL,
        .root_font_basis = root, .resolve_font_metrics = true, .used_root_font_units = root_used };
    return result;
}
static bool application_scalar_math(ACtx *c, const char *text, int unit, float *out) {
    bool root_used = false; LCtx context = application_scalar_context(c, &root_used);
    bool ok = css_scalar_math_context(text, unit, context, out);
    if (root_used && c->st) c->st->inherited_dynamic = true;
    return ok;
}
static bool css_transform_number(ACtx *c, const char *text, float *out, bool percentage) {
    float value;
    if (!application_scalar_math(c, text, MATH_NUMBER, &value)) {
        char *end; value = strtof(text, &end);
        if (!percentage || end == text || *end != '%' || end[1]) return false;
        value /= 100;
    }
    *out = isnan(value) ? 0 : isinf(value) ? copysignf(3.4028234663852886e38f, value) : value;
    return true;
}
static bool alen(ACtx *c, const char *v, Length *out) {
    Length value; MathType type;
    if (!css_parse_length_context(v, &value, c->em, c->rem, c->mc, c->st, true,
                                  c->e ? &c->e->uses_font_units : NULL, &type)) return false;
    /* Numeric internal callers use css_scalar_math. A length property cannot
       consume a number-valued function, even when its storage contains %. */
    if (value.kind == LK_LEN && strchr(v, '(') &&
        math_type_equal(type, math_type_unit(MATH_NUMBER))) return false;
    *out = value; return true;
}
/* Font declarations precede their length consumers. A winning line-height
   retains its existing value buffer until the final font is known. */
typedef struct FontPhase { ComputedStyle *style; char *owned; const char *value; } FontPhase;
static _Thread_local FontPhase *font_phase;
static bool font_basis_prop(const char *p) {
    if (*p == 'f') return !strcmp(p, "font") || !strcmp(p, "font-size") || !strcmp(p, "font-family")
        || !strcmp(p, "font-weight") || !strcmp(p, "font-style");
    return *p == 'l' && !strcmp(p, "line-height");
}
static bool line_height_valid(const char *v) {
    if (str_ieq(v, "normal") || str_ieq(v, "inherit") || str_ieq(v, "initial") || str_ieq(v, "unset")
        || str_ieq(v, "revert") || str_ieq(v, "revert-layer")) return true;
    Length length;
    return css_parse_length(v, &length, 16, 16, NULL) && length.kind == LK_LEN;
}
/* Sizing math is range-clamped once its computed length is independent of a
 * percentage reference. Keep unresolved percentages/comparisons intact for
 * layout; margin and inset values deliberately use the unrestricted parser. */
static bool sizing_stretch_keyword(const char *v) {
    return str_ieq(v, "stretch") || str_ieq(v, "-webkit-fill-available");
}
static bool alen_size(ACtx *c, const char *v, Length *out) {
    /* This keyword belongs to sizing destinations, never generic Length users. */
    if (sizing_stretch_keyword(v)) { *out = (Length){ .kind = LK_STRETCH }; return true; }
    Length value = *out;
    if (!alen(c, v, &value)) return false;
    if (value.kind == LK_LEN) {
        /* Only negative components need the literal-versus-math distinction.
           Keep common nonnegative sizes on the existing parsed-value path. */
        if (value.px < 0 || value.pct < 0) {
            while (is_ws((unsigned char)*v)) v++;
            bool math = (isalpha((unsigned char)*v) || str_istarts(v, "-webkit-")) && strchr(v, '(');
            /* Invalid negative literals must not replace a prior declaration. */
            if (!math) return false;
            if (!value.pct && !value.pctu && !value.mm && value.px < 0) value.px = 0;
        }
        /* Canonicalize signed zero while retaining the percentage unit hint. */
        if (!value.mm) { if (value.px == 0) value.px = 0.0f; if (value.pct == 0) value.pct = 0.0f; }
    }
    *out = value;
    return true;
}
/* CSS Sizing4 5.2: optional auto precedes one none/nonnegative length.
 * Keep parsing at style computation; layout only reads these compact values. */
static int contain_intrinsic_property(const char *p) {
    if (!strcmp(p, "contain-intrinsic-size")) return -1;
    if (!strcmp(p, "contain-intrinsic-width") || !strcmp(p, "contain-intrinsic-inline-size")) return 0;
    if (!strcmp(p, "contain-intrinsic-height") || !strcmp(p, "contain-intrinsic-block-size")) return 1;
    return -2;
}
static bool contain_intrinsic_parse(ACtx *c, const char *text, bool shorthand, ContainIntrinsicSize out[2]) {
    char *tokens[5]; int count = split_ws(text, tokens, 5), i = 0, n = 0;
    bool ok = count > 0 && count < 5;
    ContainIntrinsicSize parsed[2] = {{0}, {0}};
    while (ok && i < count && n < (shorthand ? 2 : 1)) {
        if (str_ieq(tokens[i], "auto")) { parsed[n].flags |= CIS_AUTO; i++; }
        if (i == count) { ok = false; break; }
        const char *token = tokens[i++];
        if (!str_ieq(token, "none")) {
            Length length; MathType type;
            ok = css_parse_length_context(token, &length, c->em, c->rem, c->mc, c->st,
                !c->validation_only, c->e ? &c->e->uses_font_units : NULL, &type) &&
                length.kind == LK_LEN && !len_has_pct(length);
            if (ok && math_type_equal(type, math_type_unit(MATH_NUMBER))) {
                char *end; double number = strtod(token, &end);
                ok = end != token && !*end && number == 0; /* bare zero only */
            }
            if (ok) {
                float value = len_resolve(length, 0);
                bool math = strchr(token, '(') != NULL;
                if (value < 0 && !math) ok = false;
                else { parsed[n].length = isnan(value) || value <= 0 ? 0 : isinf(value) ? 3.4028234663852886e38f : value; parsed[n].flags |= CIS_LENGTH; }
            }
        }
        n++;
    }
    ok = ok && i == count && n > 0;
    free_toks(tokens, count);
    if (ok) { out[0] = parsed[0]; out[1] = parsed[n == 2 ? 1 : 0]; }
    return ok;
}
static bool contain_size_parse(const char *text, uint8_t *size) {
    if (str_ieq(text, "strict")) { *size = CONTAIN_INLINE_SIZE | CONTAIN_BLOCK_SIZE; return true; }
    if (str_ieq(text, "none") || str_ieq(text, "content")) { *size = 0; return true; }
    char *tokens[5]; int n = split_ws(text, tokens, 5); unsigned seen = 0; bool ok = n > 0 && n < 5;
    uint8_t value = 0;
    for (int i = 0; ok && i < n; i++) {
        unsigned bit = str_ieq(tokens[i], "size") ? 1 : str_ieq(tokens[i], "inline-size") ? 2 :
            str_ieq(tokens[i], "layout") ? 4 : str_ieq(tokens[i], "style") ? 8 : str_ieq(tokens[i], "paint") ? 16 : 0;
        if (!bit || (seen & bit) || ((bit & 3) && (seen & 3))) ok = false;
        seen |= bit;
        if (bit == 1) value = CONTAIN_INLINE_SIZE | CONTAIN_BLOCK_SIZE;
        else if (bit == 2) value = CONTAIN_INLINE_SIZE;
    }
    free_toks(tokens, n); if (ok) *size = value; return ok;
}
bool css_containment_valid(const char *prop, const char *value) {
    int axis = contain_intrinsic_property(prop);
    if (axis == -2 && strcmp(prop, "contain")) return true;
    if (str_ieq(value, "initial") || str_ieq(value, "inherit") || str_ieq(value, "unset") ||
        str_ieq(value, "revert") || str_ieq(value, "revert-layer") || strstr(value, "var(") ||
        strstr(value, "env(") || strstr(value, "attr(")) return true;
    if (axis == -2) { uint8_t bits; return contain_size_parse(value, &bits); }
    ACtx c = { .em = 16, .rem = 16, .validation_only = true }; ContainIntrinsicSize sizes[2];
    return contain_intrinsic_parse(&c, value, axis < 0, sizes);
}
static float alen_px(ACtx *c, const char *v, float def) {
    Length l;
    if (!alen(c, v, &l) || l.kind != LK_LEN) return def;
    /* This scalar-only consumer has no containing-block reference. */
    if (len_math_stepped(l.mm)) return len_has_pct(l) ? def : len_resolve(l, 0);
    return l.px;
}

static void set4(Length *dst, ACtx *c, const char *v) {
    char *t[4]; int n = split_ws(v, t, 4); Length l[4];
    for (int i = 0; i < n; i++) if (!alen(c, t[i], &l[i])) { free_toks(t, n); return; }
    if (n == 1) l[1] = l[2] = l[3] = l[0]; else if (n == 2) { l[2] = l[0]; l[3] = l[1]; } else if (n == 3) l[3] = l[1];
    if (n) for (int i = 0; i < 4; i++) dst[i] = l[i];
    free_toks(t, n);
}
static float border_w(ACtx *c, const char *v) { if (str_ieq(v, "thin")) return 1; if (str_ieq(v, "medium")) return 3; if (str_ieq(v, "thick")) return 5; return alen_px(c, v, -1); }
static const char *const bstyles[] = { "none", "hidden", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", NULL };
static void apply_border_side(ACtx *c, int side, const char *v) {
    char *t[6]; int n = split_ws(v, t, 6);
    bool sw = false, ss = false, sc = false;
    for (int i = 0; i < n; i++) {
        int k = kw(t[i], bstyles); Color col; float w;
        if (k >= 0) { c->st->border_style[side] = (uint8_t)k; ss = true; }
        else if ((w = border_w(c, t[i])) >= 0) { c->st->border_width[side] = w; sw = true; }
        else if (css_parse_color(t[i], &col, c->st->color)) { c->st->border_color[side] = col; sc = true; }
    }
    if (!sw) c->st->border_width[side] = 3;
    if (!ss) c->st->border_style[side] = BS_NONE;
    if (!sc) c->st->border_color[side] = c->st->color;
    free_toks(t, n);
}
static void color_str(SB *b, Color c);
static bool str_iends(const char *s, const char *suf) { size_t n = strlen(s), k = strlen(suf); return n >= k && !strcasecmp(s + n - k, suf); }
/* a gradient length or percentage as computed: lengths in px, percentages kept; anything else as written */
static void grad_len(ACtx *c, SB *b, const char *t) {
    size_t n = strlen(t); Length l;
    if (n && t[n - 1] == '%' && !strchr(t, '(')) { sb_printf(b, "%g%%", strtof(t, NULL)); return; }
    if (n > 3 && (str_iends(t, "deg") || str_iends(t, "rad") || str_iends(t, "turn"))) { sb_puts(b, t); return; }   /* angles as written */
    if (!strchr(t, '(') && alen(c, t, &l) && !l.pct && !l.pctu) { sb_printf(b, "%gpx", l.px); return; }
    if (str_istarts(t, "calc(") && !strstr(t, "deg") && !strstr(t, "turn") && !strstr(t, "rad") && alen(c, t, &l) && !l.mm) {   /* calc() down to one unit */
        if (l.pctu && !l.px) { sb_printf(b, "%g%%", l.pct); return; }
        if (!l.pctu && !l.pct) { sb_printf(b, "%gpx", l.px); return; }
    }
    sb_puts(b, t);
}
/* a <position> after "at": keywords become percentages, keyword-offset pairs (left 10px) their offset */
static void grad_pos(ACtx *c, SB *b, char **t, int n) {
    const char *x = NULL, *y = NULL; char xb[64], yb[64];
    for (int i = 0; i < n; i++) {
        const char *k = t[i]; bool hz = str_ieq(k, "left") || str_ieq(k, "right"), vt = str_ieq(k, "top") || str_ieq(k, "bottom");
        const char *off = i + 1 < n && !str_ieq(t[i + 1], "center") && !str_ieq(t[i + 1], "left") && !str_ieq(t[i + 1], "right") && !str_ieq(t[i + 1], "top") && !str_ieq(t[i + 1], "bottom") && n == 4 ? t[i + 1] : NULL;
        char *dst = NULL; const char **slot = NULL;
        if (hz) { slot = &x; dst = xb; } else if (vt) { slot = &y; dst = yb; }
        if (slot) {
            bool far = str_ieq(k, "right") || str_ieq(k, "bottom");
            if (off && !far) { snprintf(dst, 64, "%s", off); i++; }
            else if (off) { snprintf(dst, 64, "calc(100%% - %s)", off); i++; }
            else snprintf(dst, 64, far ? "100%%" : "0%%");
            *slot = dst;
        } else if (str_ieq(k, "center")) { if (!x) x = "50%"; else y = "50%"; }
        else if (!x) x = k; else y = k;
    }
    if (!x) x = "50%"; if (!y) y = "50%";
    grad_len(c, b, x); sb_putc(b, ' '); grad_len(c, b, y);
}
/* the computed value of a gradient (CSS Images 4): colors computed, lengths absolute, positions only where given,
   and the color interpolation method unless it is the default (srgb for legacy colors, else oklab) */
static char *gradient_ser(ACtx *c, const char *fn, char **args, int na, int start) {
    SB b; sb_init(&b); sb_puts(&b, fn); sb_putc(&b, '(');
    bool legacy = true;
    for (int i = start; i < na; i++) {
        char *t[3]; int n = split_ws(args[i], t, 3); CssColor cc, cur = cur_color(c->st);
        if (n && css_color_parse(t[0], &cc, &cur) && !cc.legacy) legacy = false;
        free_toks(t, n);
    }
    if (start) {
        char *t[24]; int n = split_ws(args[0], t, 24); char *rest[24]; int nr = 0; char meth[64] = "";
        for (int i = 0; i < n; i++) {
            if (str_ieq(t[i], "in") && i + 1 < n) {
                char sp[24]; snprintf(sp, sizeof sp, "%s", t[i + 1]); for (char *q = sp; *q; q++) *q = (char)tolower((unsigned char)*q);
                if (!strcmp(sp, "xyz")) snprintf(sp, sizeof sp, "xyz-d65");
                i++;
                const char *hue = NULL;
                if (i + 2 < n && str_ieq(t[i + 2], "hue")) { hue = t[i + 1]; i += 2; }
                if (hue && !str_ieq(hue, "shorter")) snprintf(meth, sizeof meth, "%s %s hue", sp, hue);
                else if (strcmp(sp, legacy ? "srgb" : "oklab")) snprintf(meth, sizeof meth, "%s", sp);
                for (char *q = meth; *q; q++) *q = (char)tolower((unsigned char)*q);
            } else rest[nr++] = t[i];
        }
        bool any = false;
        for (int i = 0; i < nr; i++) {
            if (str_ieq(rest[i], "ellipse") || str_ieq(rest[i], "farthest-corner")) continue;   /* the defaults */
            if (any) sb_putc(&b, ' ');
            any = true;
            if (str_ieq(rest[i], "at")) { sb_puts(&b, "at "); grad_pos(c, &b, rest + i + 1, nr - i - 1); break; }
            if (str_ieq(rest[i], "from") || str_ieq(rest[i], "to") || isalpha((unsigned char)rest[i][0])) { for (char *q = rest[i]; *q; q++) *q = (char)tolower((unsigned char)*q); sb_puts(&b, rest[i]); }
            else grad_len(c, &b, rest[i]);
        }
        if (*meth) { if (any) sb_putc(&b, ' '); sb_puts(&b, "in "); sb_puts(&b, meth); any = true; }
        if (any) sb_puts(&b, ", ");
        free_toks(t, n);
    }
    for (int i = start; i < na; i++) {
        char *t[3]; int n = split_ws(args[i], t, 3);
        if (i > start) sb_puts(&b, ", ");
        Color col; const char *ser = NULL; CssColor cur = cur_color(c->st);
        int k = 0;
        if (n && parse_color_ser(t[0], &col, &ser, &cur)) { if (ser) sb_puts(&b, ser); else color_str(&b, col); k = 1; }
        for (; k < n; k++) { if (k) sb_putc(&b, ' '); grad_len(c, &b, t[k]); }
        free_toks(t, n);
    }
    sb_putc(&b, ')');
    return sb_take(&b);
}
static Gradient *parse_gradient(ACtx *c, const char *v) {
    bool rep = str_istarts(v, "repeating-");
    const char *f = rep ? v + 10 : v;
    bool radial = str_istarts(f, "radial-gradient(") || str_istarts(f, "conic-gradient(");
    if (!str_istarts(f, "linear-gradient(") && !str_istarts(f, "-webkit-linear-gradient(") && !radial) return NULL;
    const char *a = strchr(f, '(') + 1;
    Gradient *g = xcalloc(1, sizeof *g); g->angle = 180; g->type = radial; g->repeating = rep;
    /* split args at depth-0 commas */
    char *args[16]; int na = 0; const char *s = a; int d = 0;
    for (const char *p = a; *p && na < 16; p++) {
        if (*p == '(') d++; else if (*p == ')') { if (!d) { args[na++] = xstrndup(s, (size_t)(p - s)); break; } d--; }
        else if (*p == ',' && !d) { args[na++] = xstrndup(s, (size_t)(p - s)); s = p + 1; }
    }
    int start = 0;
    { char *t0[1]; int n0 = na ? split_ws(args[0], t0, 1) : 0; Color cc; if (n0 && !css_parse_color(t0[0], &cc, c->st->color)) start = 1; free_toks(t0, n0); }
    char fnb[48]; snprintf(fnb, sizeof fnb, "%s%.*s", rep ? "repeating-" : "", (int)(a - 1 - f), f); for (char *q = fnb; *q; q++) *q = (char)tolower((unsigned char)*q);
    char *ser = na && !str_istarts(f, "-webkit-") ? gradient_ser(c, fnb, args, na, start) : NULL;
    start = 0;
    if (na) {
        char *a0 = str_trim(args[0]);
        if (str_istarts(a0, "to ")) {
            const char *dir = a0 + 3;
            bool top = strstr(dir, "top"), bottom = strstr(dir, "bottom"), left = strstr(dir, "left"), right = strstr(dir, "right");
            g->angle = top ? (left ? 315 : right ? 45 : 0) : bottom ? (left ? 225 : right ? 135 : 180) : left ? 270 : 90;
            start = 1;
        } else if (strstr(a0, "deg") || strstr(a0, "turn") || strstr(a0, "rad")) {
            float x = strtof(a0, NULL); if (strstr(a0, "turn")) x *= 360; else if (strstr(a0, "rad") && !strstr(a0, "grad")) x *= 57.2958f;
            g->angle = x; start = 1;
        } else if (radial && (strstr(a0, "circle") || strstr(a0, "ellipse") || strstr(a0, " at ") || str_istarts(a0, "at ") || strstr(a0, "closest") || strstr(a0, "farthest") || str_istarts(a0, "from"))) start = 1;
    }
    for (int i = start; i < na && g->nstops < 8; i++) {
        char *t[3]; int n = split_ws(args[i], t, 3);
        Color col; if (n && css_parse_color(t[0], &col, c->st->color)) {
            g->stops[g->nstops].color = col; g->stops[g->nstops].pos = -1;
            if (n > 1) { Length l; if (alen(c, t[1], &l)) g->stops[g->nstops].pos = l.pct ? l.pct / 100 : -2 - l.px; }
            g->nstops++;
            if (n > 2 && g->nstops < 8) { Length l; if (alen(c, t[2], &l)) { g->stops[g->nstops] = g->stops[g->nstops - 1]; g->stops[g->nstops].pos = l.pct ? l.pct / 100 : -2 - l.px; g->nstops++; } }
        }
        free_toks(t, n);
    }
    for (int i = 0; i < na; i++) free(args[i]);
    if (g->nstops < 2) { if (g->nstops == 1) { g->stops[1] = g->stops[0]; g->nstops = 2; } else { free(g); free(ser); return NULL; } }
    if (ser) { size_t n = strlen(ser); g = xrealloc(g, sizeof *g + n + 1); memcpy(g + 1, ser, n + 1); g->ser = (const char *)(g + 1); free(ser); }
    /* distribute missing positions (px positions encoded as -2-px kept as-is for layout) */
    if (g->stops[0].pos == -1) g->stops[0].pos = 0;
    if (g->stops[g->nstops - 1].pos == -1) g->stops[g->nstops - 1].pos = 1;
    for (int i = 1; i < g->nstops - 1; i++) if (g->stops[i].pos == -1) {
        int j = i; while (j < g->nstops && g->stops[j].pos == -1) j++;
        float a0 = g->stops[i - 1].pos < -1 ? 0 : g->stops[i - 1].pos, b0 = g->stops[j].pos < -1 ? 1 : g->stops[j].pos;
        for (int k = i; k < j; k++) g->stops[k].pos = a0 + (b0 - a0) * (float)(k - i + 1) / (float)(j - i + 1);
    }
    return g;
}
static char *extract_url(const char *v) {
    const char *u = strstr(v, "url("); if (!u) return NULL;
    u += 4; while (is_ws((unsigned char)*u)) u++;
    char q = (*u == '"' || *u == '\'') ? *u++ : 0;
    const char *e = u; while (*e && (q ? *e != q : *e != ')')) e++;
    char *r = xstrndup(u, (size_t)(e - u)); char *t = str_trim(r); memmove(r, t, strlen(t) + 1); return r;
}
/* url() against the document base, remembered: restyles resolve the same (often long data:) URLs again. */
static char *style_url_join(const char *base, const char *rel) {
    static _Thread_local struct { char *base, *rel, *abs; } memo[32];
    size_t n = strlen(rel); uint32_t h = (uint32_t)n * 2654435761u;
    for (size_t i = 0; i < n && i < 48; i++) h = (h ^ (unsigned char)rel[i]) * 16777619u;
    for (size_t i = n > 48 ? n - 48 : n; i < n; i++) h = (h ^ (unsigned char)rel[i]) * 16777619u;
    if (base) for (const char *q = base; *q; q++) h = (h ^ (unsigned char)*q) * 16777619u;
    __typeof__(&memo[0]) m = &memo[h & 31];
    if (m->rel && !strcmp(m->rel, rel) && (base ? m->base && !strcmp(m->base, base) : !m->base)) return m->abs ? xstrdup(m->abs) : NULL;
    char *abs = url_join(base, rel);
    free(m->base); free(m->rel); free(m->abs);
    m->base = base ? xstrdup(base) : NULL; m->rel = xstrdup(rel); m->abs = abs ? xstrdup(abs) : NULL;
    return abs;
}
/* Top-level layer boundaries ignore commas in URLs, functions and strings. */
static const char *bg_layer_end(const char *v) {
    int depth = 0; char quote = 0;
    for (const char *p = v; *p; p++) {
        if (*p == '\\' && p[1]) { p++; continue; }
        if (quote) { if (*p == quote) quote = 0; continue; }
        if (*p == '\'' || *p == '"') { quote = *p; continue; }
        if (*p == '(') depth++;
        else if (*p == ')' && depth) depth--;
        else if (*p == ',' && !depth) return p;
    }
    return v + strlen(v);
}
static uint32_t bg_layer_count(const char *v) {
    uint32_t count = 1;
    for (const char *p = v; *(p = bg_layer_end(p)); p++) if (count < UINT32_MAX) count++;
    return count;
}
static void apply_background_image(ACtx *c, const char *v) {
    c->st->bg_layers = bg_layer_count(v);
    free(c->st->bg_image); c->st->bg_image = NULL; free(c->st->bg_gradient); c->st->bg_gradient = NULL;
    if (str_ieq(v, "none")) return;
    if (strstr(v, "gradient(")) {
        const char *g = strstr(v, "linear-gradient("); if (!g) g = strstr(v, "radial-gradient("); if (!g) g = strstr(v, "conic-gradient(");
        if (g && g - v >= 10 && !strncmp(g - 10, "repeating-", 10)) g -= 10;
        if (g) c->st->bg_gradient = parse_gradient(c, g);
        if (c->st->bg_gradient && (!strstr(v, "url(") || strstr(v, "url(") > strstr(v, "gradient("))) return;
    }
    char *u = extract_url(v);
    if (u) {
        const char *base = c->e && c->e->doc ? (c->e->doc->base_url ? c->e->doc->base_url : c->e->doc->url) : NULL;
        char *abs = style_url_join(base, u); free(u); c->st->bg_image = abs;
    }
}
/* Background positions keep their computed length/percentage pair for every
   layer. The painter currently consumes the first layer; CSSOM and animation
   must retain the complete lists independently of the background-image count. */
static const char *const bg_axis_props[] = { "background-position-x", "background-position-y" };
static Length bg_percent(float value) { return (Length){ .pct = value, .kind = LK_LEN, .pctu = true }; }
static bool bg_length(ACtx *c, const char *text, Length *out) {
    const char *p = text; LCtx lc = { c->em, c->rem, c->mc, c->st, true, c->e ? &c->e->uses_font_units : NULL };
    char *end; float number = strtof(text, &end);
    if (end != text) {
        if (!*end && number != 0) return false;
        if (*end && *end != '%' && !str_ieq(end, "px") && !str_ieq(end, "em") && !str_ieq(end, "rem")
            && !str_ieq(end, "ex") && !str_ieq(end, "ch") && !str_ieq(end, "cap") && !str_ieq(end, "ic")
            && !str_ieq(end, "lh") && !str_ieq(end, "pt") && !str_ieq(end, "pc") && !str_ieq(end, "in")
            && !str_ieq(end, "cm") && !str_ieq(end, "mm") && !str_ieq(end, "q")
            && !str_ieq(end, "vw") && !str_ieq(end, "vh") && !str_ieq(end, "vmin") && !str_ieq(end, "vmax")
            && !str_ieq(end, "svw") && !str_ieq(end, "svh") && !str_ieq(end, "lvw") && !str_ieq(end, "lvh")
            && !str_ieq(end, "dvw") && !str_ieq(end, "dvh") && !str_ieq(end, "cqw") && !str_ieq(end, "cqh")
            && !str_ieq(end, "cqi") && !str_ieq(end, "cqb") && !str_ieq(end, "cqmin") && !str_ieq(end, "cqmax")) return false;
    } else if (!str_istarts(text, "calc(") && !str_istarts(text, "min(") && !str_istarts(text, "max(")
               && !str_istarts(text, "clamp(") && !str_istarts(text, "round(") && !str_istarts(text, "mod(")
               && !str_istarts(text, "rem(") && !str_istarts(text, "abs(") && !str_istarts(text, "hypot(")) return false;
    if (!parse_dim(&p, out, &lc) || out->kind != LK_LEN) return false;
    if (!math_type_equal(lc.math_type, math_type_unit(MATH_LENGTH)) &&
        !math_type_equal(lc.math_type, math_type_unit(MATH_NUMBER))) return false;
    while (is_ws((unsigned char)*p)) p++;
    return !*p;
}
/* 0: start edge; 1: center; 2: end edge. Logical axis keywords are
   interpreted in the element's writing direction, while physical edges stay fixed. */
static int bg_keyword(const ACtx *c, const char *text, int axis, bool logical) {
    if (str_ieq(text, "center")) return 1;
    if (str_ieq(text, axis ? "top" : "left")) return 0;
    if (str_ieq(text, axis ? "bottom" : "right")) return 2;
    if (logical && (str_ieq(text, axis ? "y-start" : "x-start") || str_ieq(text, axis ? "y-end" : "x-end"))) {
        bool reversed = axis ? (c->st->writing_mode != 0 && c->st->direction) : (c->st->writing_mode ? c->st->writing_mode == 1 : c->st->direction);
        bool end = str_ieq(text, axis ? "y-end" : "x-end");
        return (reversed != end) ? 2 : 0;
    }
    return -1;
}
static Length bg_offset(Length value, int edge) {
    if (edge != 2) return value;
    calc_scale(&value, -1); calc_add_linear(&value, 0, 100, true);
    return value;
}
static bool bg_component(ACtx *c, const char *text, int axis, bool logical, Length *out) {
    int edge = bg_keyword(c, text, axis, logical);
    if (edge >= 0) { *out = bg_percent(edge * 50); return true; }
    return bg_length(c, text, out);
}
static bool bg_position_item(ACtx *c, const char *text, int axis, Length out[2]) {
    char *t[5]; int n = split_ws(text, t, 5); bool ok = false;
    out[0] = out[1] = bg_percent(50);
    if (axis >= 0) {
        if (n == 1) ok = bg_component(c, t[0], axis, true, &out[axis]);
        else if (n == 2) { int edge = bg_keyword(c, t[0], axis, true); ok = edge >= 0 && edge != 1 && bg_length(c, t[1], &out[axis]); if (ok) out[axis] = bg_offset(out[axis], edge); }
    } else if (n == 1) {
        if (bg_keyword(c, t[0], 1, false) >= 0 && !str_ieq(t[0], "center")) ok = bg_component(c, t[0], 1, false, &out[1]);
        else ok = bg_component(c, t[0], 0, false, &out[0]);
    } else if (n == 2) {
        ok = bg_component(c, t[0], 0, false, &out[0]) && bg_component(c, t[1], 1, false, &out[1]);
        if (!ok && bg_keyword(c, t[0], 1, false) >= 0 && bg_keyword(c, t[1], 0, false) >= 0)
            ok = bg_component(c, t[1], 0, false, &out[0]) && bg_component(c, t[0], 1, false, &out[1]);
    } else if (n == 3 || n == 4) {
        bool set[2] = { false, false }; int center = 0; ok = true;
        for (int i = 0; i < n && ok; i++) {
            if (str_ieq(t[i], "center")) { center++; continue; }
            int ax = bg_keyword(c, t[i], 0, false) >= 0 ? 0 : bg_keyword(c, t[i], 1, false) >= 0 ? 1 : -1;
            if (ax < 0 || set[ax]) { ok = false; break; }
            int edge = bg_keyword(c, t[i], ax, false); Length offset = bg_percent(0);
            if (i + 1 < n && bg_length(c, t[i + 1], &offset)) i++;
            out[ax] = bg_offset(offset, edge); set[ax] = true;
        }
        if (center == 1 && set[0] != set[1]) set[0] = set[1] = true;
        else if (center) ok = false;
        ok = ok && set[0] && set[1];
    }
    free_toks(t, n); return ok;
}
static void bg_length_str(SB *b, Length value) {
    if (len_math_stepped(value.mm)) {
        Length a = L_px(value.px), z = L_px(value.px2), offset = L_px(value.px3);
        a.pct = value.pct; a.pctu = !!(value.math_pct_mask & 1);
        z.pct = value.pct2; z.pctu = !!(value.math_pct_mask & 2);
        offset.pct = value.pct3; offset.pctu = !!(value.math_pct_mask & 4);
        bool shift = offset.px || offset.pct || offset.pctu;
        bool lower = value.mm == LEN_MATH_ROUND_LOWER;
        if (shift || lower) sb_puts(b, "calc(");
        if (lower) { sb_puts(b, "-1 * "); calc_scale(&a, -1); calc_scale(&z, -1); }
        const char *fn = value.mm == LEN_MATH_MOD ? "mod(" : value.mm == LEN_MATH_REM ? "rem(" : "round(";
        sb_puts(b, fn);
        if (value.mm == LEN_MATH_UP) sb_puts(b, "up, ");
        else if (value.mm == LEN_MATH_DOWN) sb_puts(b, "down, ");
        else if (value.mm == LEN_MATH_TO_ZERO) sb_puts(b, "to-zero, ");
        bg_length_str(b, a); sb_puts(b, ", "); bg_length_str(b, z); sb_putc(b, ')');
        if (shift) { sb_puts(b, " + "); bg_length_str(b, offset); }
        if (shift || lower) sb_putc(b, ')');
        return;
    }
    if (value.mm >= 3) {
        Length preferred = value, lower = value, upper = value;
        preferred.mm = lower.mm = upper.mm = 0;
        preferred.pctu = !!(value.math_pct_mask & 1);
        lower.pctu = !!(value.math_pct_mask & 2);
        upper.pctu = !!(value.math_pct_mask & 4);
        lower.px = value.px2; lower.pct = value.pct2;
        upper.px = value.px3; upper.pct = value.pct3;
        if (value.mm == 3) {
            sb_puts(b, "clamp("); bg_length_str(b, lower); sb_puts(b, ", ");
            bg_length_str(b, preferred); sb_puts(b, ", "); bg_length_str(b, upper); sb_putc(b, ')');
        } else {
            sb_puts(b, "min("); bg_length_str(b, lower); sb_puts(b, ", max(");
            bg_length_str(b, preferred); sb_puts(b, ", "); bg_length_str(b, upper); sb_puts(b, "))");
        }
        return;
    }
    if (value.mm) { Length a = value, z = value; a.mm = z.mm = 0; z.px = value.px2; z.pct = value.pct2;
        if (value.math_pct_mask) { a.pctu = !!(value.math_pct_mask & 1); z.pctu = !!(value.math_pct_mask & 2); }
        sb_puts(b, value.mm == 1 ? "min(" : "max("); bg_length_str(b, a); sb_puts(b, ", "); bg_length_str(b, z); sb_putc(b, ')'); return; }
    /* A zero percentage still carries a percentage basis, including when a
       length is present. Do not collapse calc(0% + 10px) to a length. */
    if ((value.pctu || value.pct) && value.px) sb_printf(b, "calc(%g%% %c %gpx)", value.pct, value.px < 0 ? '-' : '+', fabsf(value.px));
    else if (value.pctu || value.pct) sb_printf(b, "%g%%", value.pct);
    else sb_printf(b, "%gpx", value.px == 0 ? 0.0 : (double)value.px);
}
/* Logical origins remain part of an axis longhand's computed value, including
   in lists. Keep its computed offset separate from the physical used position. */
static bool bg_logical_axis_str(SB *out, ACtx *c, const char *text, int axis) {
    char *t[3]; int n = split_ws(text, t, 3);
    const char *start = axis ? "y-start" : "x-start", *end = axis ? "y-end" : "x-end";
    bool logical = n > 0 && (str_ieq(t[0], start) || str_ieq(t[0], end));
    if (logical) {
        sb_puts(out, str_ieq(t[0], start) ? start : end);
        if (n == 2) { Length offset; if (bg_length(c, t[1], &offset)) { sb_putc(out, ' '); bg_length_str(out, offset); } }
    }
    free_toks(t, n); return logical;
}
static bool bg_axis_is_logical(const char *text, int axis) {
    if (!text) return false;
    while (is_ws((unsigned char)*text)) text++;
    return str_istarts(text, axis ? "y-start" : "x-start") || str_istarts(text, axis ? "y-end" : "x-end");
}
static bool bg_resolve_axis(const ComputedStyle *st, const char *text, size_t length, int axis, Length *out) {
    /* The stored offset is already computed (px/percentage), so resolving its
       origin never re-reads font, viewport, or container-query unit state. */
    ACtx c = {0}; c.st = (ComputedStyle *)st; c.em = c.rem = st->font_size;
    char *item = xstrndup(text, length); Length pair[2];
    bool ok = bg_position_item(&c, item, axis, pair); free(item);
    if (ok) *out = pair[axis]; return ok;
}
static void bg_position_resolve_logical(ComputedStyle *st) {
    for (int axis = 0; axis < 2; axis++) {
        const char *text = custom_get(st->gen_n, bg_axis_props[axis]);
        if (!bg_axis_is_logical(text, axis)) continue;
        const char *end = text; int depth = 0;
        while (*end && (depth || *end != ',')) { if (*end == '(') depth++; else if (*end == ')') depth--; end++; }
        Length value; if (bg_resolve_axis(st, text, (size_t)(end - text), axis, &value)) st->bg_pos[axis] = value;
    }
}
static void bg_resolved_axis_str(SB *out, const ComputedStyle *st, const char *text, size_t length, int axis) {
    Length value;
    if (bg_axis_is_logical(text, axis) && bg_resolve_axis(st, text, length, axis, &value)) bg_length_str(out, value);
    else sb_put(out, text, length);
}
static bool apply_bg_position(ACtx *c, const char *value, int axis) {
    SB out[2]; sb_init(&out[0]); sb_init(&out[1]); Length first[2]; int count = 0; bool ok = true;
    for (const char *p = value;;) {
        const char *end = p; int depth = 0;
        while (*end && (depth || *end != ',')) { if (*end == '(') depth++; else if (*end == ')') depth--; end++; }
        char *item = xstrndup(p, (size_t)(end - p)); Length pair[2];
        ok = !depth && bg_position_item(c, item, axis, pair); if (!ok) { free(item); break; }
        for (int a = 0; a < 2; a++) if (axis < 0 || axis == a) { if (count) sb_puts(&out[a], ", "); if (axis < 0 || !bg_logical_axis_str(&out[a], c, item, a)) bg_length_str(&out[a], pair[a]); }
        free(item);
        if (!count) memcpy(first, pair, sizeof first); count++;
        if (!*end) break; p = end + 1;
    }
    if (ok) for (int a = 0; a < 2; a++) if (axis < 0 || axis == a) { c->st->bg_pos[a] = first[a]; gen_put(&c->st->gen_n, bg_axis_props[a], sb_take(&out[a])); }
    sb_free(&out[0]); sb_free(&out[1]); return ok;
}
static void apply_bg_pos(ACtx *c, const char *v) { (void)apply_bg_position(c, v, -1); }
static void bg_position_copy(ComputedStyle *st, const ComputedStyle *from, int axis) {
    for (int a = 0; a < 2; a++) if (axis < 0 || axis == a) {
        const char *v = from ? gen_value(from, gen_prop(bg_axis_props[a])) : NULL;
        gen_put(&st->gen_n, bg_axis_props[a], xstrdup(v ? v : "0%"));
        st->bg_pos[a] = from ? from->bg_pos[a] : bg_percent(0);
    }
}
static char *bg_position_computed(const ComputedStyle *s, int axis) {
    const char *x = gen_value(s, gen_prop(bg_axis_props[0])), *y = gen_value(s, gen_prop(bg_axis_props[1]));
    if (!x) x = "0%"; if (!y) y = "0%";
    if (axis >= 0) return xstrdup(axis ? y : x);
    SB out; sb_init(&out); const char *xp = x, *yp = y; int count = 0;
    do {
        const char *xe = xp, *ye = yp; int depth = 0;
        while (*xe && (depth || *xe != ',')) { if (*xe == '(') depth++; else if (*xe == ')') depth--; xe++; }
        depth = 0; while (*ye && (depth || *ye != ',')) { if (*ye == '(') depth++; else if (*ye == ')') depth--; ye++; }
        if (count++) sb_puts(&out, ", "); bg_resolved_axis_str(&out, s, xp, (size_t)(xe - xp), 0); sb_putc(&out, ' '); bg_resolved_axis_str(&out, s, yp, (size_t)(ye - yp), 1);
        if (!*xe && !*ye) break;
        /* A shorthand cannot exactly represent unequal longhand list lengths. */
        if (!*xe || !*ye) { sb_free(&out); return xstrdup(""); }
        xp = xe + 1; yp = ye + 1;
        while (is_ws((unsigned char)*xp)) xp++; while (is_ws((unsigned char)*yp)) yp++;
    } while (true);
    return sb_take(&out);
}
/* Geometry is currently painted for the first layer; keep comma-separated
 * declaration text available to CSSOM rather than treating nested commas as
 * layer separators. */
static char *bg_first_layer(const char *value) {
    const char *start = value, *p = value; int depth = 0; char quote = 0;
    while (*p) {
        if (quote) { if (*p == '\\' && p[1]) p++; else if (*p == quote) quote = 0; }
        else if (*p == '\'' || *p == '"') quote = *p;
        else if (*p == '(') depth++;
        else if (*p == ')' && depth) depth--;
        else if (*p == ',' && !depth) break;
        p++;
    }
    while (start < p && is_ws((unsigned char)*start)) start++;
    while (p > start && is_ws((unsigned char)p[-1])) p--;
    return xstrndup(start, (size_t)(p - start));
}
static int bg_repeat_mode(const char *value) {
    if (str_ieq(value, "repeat")) return BGR_REPEAT;
    if (str_ieq(value, "no-repeat")) return BGR_NO_REPEAT;
    if (str_ieq(value, "round")) return BGR_ROUND;
    if (str_ieq(value, "space")) return BGR_SPACE;
    return -1;
}
static void apply_bg_repeat(ACtx *c, const char *value) {
    char *first = bg_first_layer(value), *t[3]; int n = split_ws(first, t, 3), x = -1, y = -1;
    if (n == 1 && str_ieq(t[0], "repeat-x")) { x = BGR_REPEAT; y = BGR_NO_REPEAT; }
    else if (n == 1 && str_ieq(t[0], "repeat-y")) { x = BGR_NO_REPEAT; y = BGR_REPEAT; }
    else if (n == 1 || n == 2) { x = bg_repeat_mode(t[0]); y = bg_repeat_mode(t[n - 1]); }
    if (x >= 0 && y >= 0) c->st->bg_repeat = (uint8_t)(x | (y << 2));
    free_toks(t, n); free(first);
}
static int bg_box_kind(const char *value) {
    if (str_ieq(value, "border-box")) return BG_BORDER_BOX;
    if (str_ieq(value, "padding-box")) return BG_PADDING_BOX;
    if (str_ieq(value, "content-box")) return BG_CONTENT_BOX;
    return -1;
}
/* Color belongs below the last image layer. Resolve list repetition/truncation
 * once during the cascade, independently of our first-image paint support. */
static void bg_resolve_color_clip(ComputedStyle *st) {
    const char *v = gen_value(st, gen_prop("background-clip"));
    if (!v || !*v) v = "border-box";
    uint32_t n = bg_layer_count(v), index = ((st->bg_layers ? st->bg_layers : 1) - 1) % n;
    const char *p = v;
    while (index--) p = bg_layer_end(p) + 1;
    char *item = xstrndup(p, (size_t)(bg_layer_end(p) - p));
    int box = bg_box_kind(str_trim(item)); free(item);
    st->bg_color_clip = (uint8_t)(box >= 0 ? box : BG_BORDER_BOX);
}
static void bg_shorthand_clips(ComputedStyle *st, const char *v) {
    static const char *const names[] = { "border-box", "padding-box", "content-box" };
    SB clips; sb_init(&clips);
    for (const char *p = v;;) {
        const char *end = bg_layer_end(p);
        char *layer = xstrndup(p, (size_t)(end - p)), *t; const char *q = layer;
        int clip = BG_BORDER_BOX;
        while ((t = next_token(&q))) { int box = bg_box_kind(t); if (box >= 0) clip = box; free(t); }
        if (clips.n) sb_puts(&clips, ", ");
        sb_puts(&clips, names[clip]); free(layer);
        if (!*end) break;
        p = end + 1;
    }
    gen_put(&st->gen_n, "background-clip", sb_take(&clips));
}
static void apply_bg_box(ACtx *c, const char *value, bool clip) {
    char *first = bg_first_layer(value); int box = bg_box_kind(first); free(first);
    if (box >= 0) { if (clip) c->st->bg_clip = (uint8_t)box; else c->st->bg_origin = (uint8_t)box; }
}
static void apply_bg_size(ACtx *c, const char *v) {
    char *first = bg_first_layer(v);
    if (str_ieq(first, "cover") || str_ieq(first, "contain")) { c->st->bg_size_kind = str_ieq(first, "cover") ? BGS_COVER : BGS_CONTAIN; free(first); return; }
    char *t[3]; int n = split_ws(first, t, 3); Length size[2] = {L_auto(), L_auto()}; bool ok = n == 1 || n == 2;
    for (int i = 0; ok && i < n; i++) ok = alen(c, t[i], &size[i]) && (size[i].kind == LK_LEN || size[i].kind == LK_AUTO);
    if (ok) { c->st->bg_size_kind = BGS_LEN; c->st->bg_size[0] = size[0]; c->st->bg_size[1] = size[1]; }
    free_toks(t, n); free(first);
}
static void bg_shorthand_geometry(ACtx *c, const char *value) {
    char *first = bg_first_layer(value); char *slash = NULL; int depth = 0; char quote = 0;
    for (char *p = first; *p; p++) {
        if (quote) { if (*p == '\\' && p[1]) p++; else if (*p == quote) quote = 0; }
        else if (*p == '\'' || *p == '"') quote = *p;
        else if (*p == '(') depth++;
        else if (*p == ')' && depth) depth--;
        else if (*p == '/' && !depth) { slash = p; break; }
    }
    if (slash) {
        const char *q = slash + 1; SB size; sb_init(&size); char *t; int count = 0;
        while (count < 2 && (t = next_token(&q))) {
            Length l; bool keyword = str_ieq(t, "cover") || str_ieq(t, "contain");
            bool valid = keyword || (alen(c, t, &l) && (l.kind == LK_LEN || l.kind == LK_AUTO));
            if (!valid || (count && keyword)) { free(t); break; }
            if (size.n) sb_putc(&size, ' '); sb_puts(&size, t); free(t); count++;
            if (keyword) break;
        }
        if (size.n) apply_bg_size(c, size.s);
        sb_free(&size); *slash = ' ';
    }
    const char *q = first; char *t; SB repeats; sb_init(&repeats); int boxes = 0;
    while ((t = next_token(&q))) {
        int box = bg_box_kind(t);
        if (box >= 0 && boxes < 2) {
            if (!boxes++) c->st->bg_origin = c->st->bg_clip = (uint8_t)box;
            else c->st->bg_clip = (uint8_t)box;
        } else if (bg_repeat_mode(t) >= 0 || str_ieq(t, "repeat-x") || str_ieq(t, "repeat-y")) {
            if (repeats.n) sb_putc(&repeats, ' '); sb_puts(&repeats, t);
        }
        free(t);
    }
    if (repeats.n) apply_bg_repeat(c, repeats.s);
    sb_free(&repeats); free(first);
}
static void bg_shorthand_positions(ACtx *c, const char *value) {
    SB positions; sb_init(&positions); int layer = 0;
    for (const char *p = value;;) {
        const char *end = p, *slash = NULL; int depth = 0;
        while (*end && (depth || *end != ',')) {
            if (*end == '(') depth++; else if (*end == ')') depth--;
            else if (!depth && *end == '/' && !slash) slash = end;
            end++;
        }
        char *head = xstrndup(p, (size_t)((slash ? slash : end) - p)); const char *q = head; char *token;
        SB pair; sb_init(&pair);
        while ((token = next_token(&q))) {
            Length l;
            if (bg_keyword(c, token, 0, false) >= 0 || bg_keyword(c, token, 1, false) >= 0 || bg_length(c, token, &l)) {
                if (pair.n) sb_putc(&pair, ' '); sb_puts(&pair, token);
            }
            free(token);
        }
        if (layer++) sb_puts(&positions, ", "); sb_puts(&positions, pair.n ? pair.s : "0% 0%");
        sb_free(&pair); free(head);
        if (!*end) break; p = end + 1;
    }
    apply_bg_pos(c, positions.s ? positions.s : "0% 0%"); sb_free(&positions);
}
static void apply_background(ACtx *c, const char *v) {
    c->st->bg_layers = bg_layer_count(v);
    bg_shorthand_clips(c->st, v);
    /* use last layer for color, first for image */
    c->st->bg_color = 0; c->st->bg_ser = NULL; free(c->st->bg_image); c->st->bg_image = NULL; free(c->st->bg_gradient); c->st->bg_gradient = NULL;
    c->st->bg_repeat = BG_REPEAT; c->st->bg_size_kind = BGS_AUTO; c->st->bg_size[0] = c->st->bg_size[1] = L_auto(); c->st->bg_origin = BG_PADDING_BOX; c->st->bg_clip = BG_BORDER_BOX; bg_position_copy(c->st, NULL, -1);
    if (str_ieq(v, "none")) return;
    if (strstr(v, "url(") || strstr(v, "gradient(")) apply_background_image(c, v);
    const char *p = v; char *t;
    while ((t = next_token(&p))) {
        size_t tl = strlen(t);
        char *tt = t; if (tl && tt[tl - 1] == ',') tt[tl - 1] = 0;
        Color col;
        if (strstr(tt, "url(") || strstr(tt, "gradient(")) {}
        else if (!strcmp(tt, "/")) {}
        else if (css_parse_color(tt, &col, c->st->color)) { CssColor cc = cur_color(c->st); const char *ser = NULL; parse_color_ser(tt, &col, &ser, &cc); c->st->bg_color = col; c->st->bg_ser = ser; }
        free(t);
    }
    bg_shorthand_geometry(c, v);
    bg_shorthand_positions(c, v);
}
static void apply_font(ACtx *c, const char *v) {
    /* [style] [variant] [weight] size[/line-height] family */
    if (str_ieq(v, "caption") || str_ieq(v, "menu") || str_ieq(v, "message-box") || str_ieq(v, "status-bar") || str_ieq(v, "icon") || str_ieq(v, "small-caption")) { c->st->font_family = atom("system-ui"); return; }
    const char *p = v; char *t;
    c->st->font_style = 0; c->st->font_weight = 400; c->st->line_height_normal = true; c->st->line_height_factor = 0;
    if (font_phase && font_phase->style == c->st) {
        free(font_phase->owned); font_phase->owned = NULL; font_phase->value = NULL;
    }
    css_apply_decl(c->st, c->par, "font-stretch", "normal", c->e, c->el); css_apply_decl(c->st, c->par, "font-variant-caps", "normal", c->e, c->el);
    while ((t = next_token(&p))) {
        if (str_ieq(t, "italic") || str_ieq(t, "oblique")) c->st->font_style = 1;
        else if (str_ieq(t, "bold")) c->st->font_weight = 700;
        else if (str_ieq(t, "bolder") || str_ieq(t, "lighter")) css_apply_decl(c->st, c->par, "font-weight", t, c->e, c->el);
        else if (str_ieq(t, "small-caps")) css_apply_decl(c->st, c->par, "font-variant-caps", t, c->e, c->el);
        else if (str_ieq(t, "ultra-condensed") || str_ieq(t, "extra-condensed") || str_ieq(t, "condensed") || str_ieq(t, "semi-condensed") ||
                 str_ieq(t, "semi-expanded") || str_ieq(t, "expanded") || str_ieq(t, "extra-expanded") || str_ieq(t, "ultra-expanded")) css_apply_decl(c->st, c->par, "font-stretch", t, c->e, c->el);
        else if (str_ieq(t, "normal")) {}
        else if (isdigit((unsigned char)t[0]) && !strpbrk(t, "%a-z/") && atoi(t) >= 100 && atoi(t) <= 1000 && strlen(t) == 3) c->st->font_weight = (int16_t)atoi(t);
        else {
            /* size[/lh] */
            char *slash = strchr(t, '/');
            if (slash) *slash = 0;
            css_apply_decl(c->st, c->par, "font-size", t, c->e, c->el);
            c->em = c->st->font_size;
            if (slash && slash[1]) css_apply_decl(c->st, c->par, "line-height", slash + 1, c->e, c->el);
            else if (slash) { char *lh = next_token(&p); if (lh) { css_apply_decl(c->st, c->par, "line-height", lh, c->e, c->el); free(lh); } }
            while (is_ws((unsigned char)*p)) p++;
            if (*p == '/') { p++; char *lh = next_token(&p); if (lh) { css_apply_decl(c->st, c->par, "line-height", lh, c->e, c->el); free(lh); } }
            while (is_ws((unsigned char)*p)) p++;
            if (*p) c->st->font_family = atom(p);
            free(t); return;
        }
        free(t);
    }
}
static bool transform_angle(ACtx *c, const char *text, float *out) {
    if (!application_scalar_math(c, text, MATH_ANGLE, out)) {
        /* Unitless zero is allowed as a literal, not as a number-valued calc. */
        char *end; float zero = strtof(text, &end);
        if (end == text || *end || zero != 0) return false;
        *out = 0;
    }
    if (isnan(*out)) *out = 0;
    return true;
}
static void parse_transform(ACtx *c, const char *v) {
    float m[6] = { 1, 0, 0, 1, 0, 0 };
    Length pending[2] = { {0, 0, LK_LEN}, {0, 0, LK_LEN} };
    if (str_ieq(v, "none")) {
        c->st->has_transform = false;
        c->st->translate_pending[0] = pending[0]; c->st->translate_pending[1] = pending[1];
        return;
    }
    const char *p = v; bool parsed = false;
    while (*p) {
        while (is_ws((unsigned char)*p)) p++;
        if (!*p) break;
        const char *lp = strchr(p, '('); if (!lp || lp - p >= 32) return;
        char fn[32]; snprintf(fn, sizeof fn, "%.*s", (int)(lp - p), p);
        const char *rp = lp + 1; for (int dp = 1; *rp; rp++) { if (*rp == '(') dp++; else if (*rp == ')' && !--dp) break; }
        if (!*rp) return;
        char *args = xstrndup(lp + 1, (size_t)(rp - lp - 1));
        bool empty_argument = false, component = false, comma = false;
        { int dp = 0;
          for (char *q = args; *q; q++) {
              if (*q == '(') { dp++; component = true; }
              else if (*q == ')') dp--;
              else if (*q == ',' && !dp) {
                  if (!component) empty_argument = true;
                  component = false; comma = true; *q = ' ';
              } else if (!is_ws((unsigned char)*q)) component = true;
          }
        }
        if (empty_argument || (comma && !component)) { free(args); return; }
        char *t[7]; int n = split_ws(args, t, 7);
        float a = 1, b = 0, cc = 0, d = 1, e = 0, f = 0;
        bool valid = true;
        for (char *q = fn; *q; q++) *q = (char)lc(*q);
        if (!strcmp(fn, "translate") || !strcmp(fn, "translatex") || !strcmp(fn, "translatey") || !strcmp(fn, "translate3d")) {
            bool only_y = !strcmp(fn, "translatey"), only_x = !strcmp(fn, "translatex"), three = !strcmp(fn, "translate3d");
            valid = three ? n == 3 : only_x || only_y ? n == 1 : n == 1 || n == 2;
            Length lx = L_px(0), ly = L_px(0);
            if (valid) {
                if (only_y) valid = alen(c, t[0], &ly);
                else { valid = alen(c, t[0], &lx); if (valid && n > 1 && !only_x) valid = alen(c, t[1], &ly); }
            }
            /* Keywords are not transform lengths; this path currently stores
               only affine translations. */
            if (lx.kind != LK_LEN || ly.kind != LK_LEN ||
                len_math_stepped(lx.mm) || len_math_stepped(ly.mm)) valid = false;
            if (valid && three) {
                float z;
                if (!css_scalar_math(t[2], MATH_LENGTH, c->em, c->rem, &z)) {
                    char *end; z = strtof(t[2], &end);
                    valid = end != t[2] && !*end && z == 0;
                }
            }
            if (valid) { e = lx.px; f = ly.px; pending[0].pct += lx.pct; pending[1].pct += ly.pct; }
        } else if (!strcmp(fn, "scale") || !strcmp(fn, "scale3d")) {
            valid = !strcmp(fn, "scale3d") ? n == 3 : n == 1 || n == 2;
            if (valid) {
                valid = css_transform_number(c, t[0], &a, true); d = a;
                if (valid && n > 1) valid = css_transform_number(c, t[1], &d, true);
                if (valid && n == 3) { float z; valid = css_transform_number(c, t[2], &z, true); }
            }
        } else if (!strcmp(fn, "scalex")) valid = n == 1 && css_transform_number(c, t[0], &a, true);
        else if (!strcmp(fn, "scaley")) valid = n == 1 && css_transform_number(c, t[0], &d, true);
        else if (!strcmp(fn, "rotate") || !strcmp(fn, "rotatez")) {
            float angle = 0; valid = n == 1 && transform_angle(c, t[0], &angle);
            if (valid) { float r = angle * (float)M_PI / 180; a = cosf(r); b = sinf(r); cc = -sinf(r); d = cosf(r); }
        } else if (!strcmp(fn, "matrix")) {
            valid = n == 6;
            if (valid) valid = css_transform_number(c, t[0], &a, false) && css_transform_number(c, t[1], &b, false) &&
                css_transform_number(c, t[2], &cc, false) && css_transform_number(c, t[3], &d, false) &&
                css_transform_number(c, t[4], &e, false) && css_transform_number(c, t[5], &f, false);
        } else if (!strcmp(fn, "skewx") || !strcmp(fn, "skewy")) {
            float angle = 0; valid = n == 1 && transform_angle(c, t[0], &angle);
            if (valid) { float skew = tanf(angle * (float)M_PI / 180); if (fn[4] == 'x') cc = skew; else b = skew; }
        } else valid = false;
        free_toks(t, n); free(args);
        /* A malformed whole value must preserve the preceding declaration,
           including its containing-block behavior and translation state. */
        if (!valid) return;
        float r0 = m[0] * a + m[2] * b, r1 = m[1] * a + m[3] * b, r2 = m[0] * cc + m[2] * d, r3 = m[1] * cc + m[3] * d, r4 = m[0] * e + m[2] * f + m[4], r5 = m[1] * e + m[3] * f + m[5];
        m[0] = r0; m[1] = r1; m[2] = r2; m[3] = r3; m[4] = r4; m[5] = r5;
        parsed = true; p = rp + 1;
    }
    if (!parsed) return;
    memcpy(c->st->transform, m, sizeof m);
    c->st->translate_pending[0] = pending[0]; c->st->translate_pending[1] = pending[1];
    c->st->has_transform = true; /* A specified identity matrix is still a transform. */
}
static bool parse_one_shadow(ACtx *c, char *v, Shadow *sh) {
    char *t[7]; int n = split_ws(v, t, 7); float nums[4] = {0}; int nn = 0;
    memset(sh, 0, sizeof *sh); sh->color = c->st->color;
    for (int i = 0; i < n; i++) {
        Color col;
        if (str_ieq(t[i], "inset")) sh->inset = true;
        else if (nn < 4 && (isdigit((unsigned char)t[i][0]) || t[i][0] == '-' || t[i][0] == '.' || str_istarts(t[i], "calc"))) nums[nn++] = alen_px(c, t[i], 0);
        else if (css_parse_color(t[i], &col, c->st->color)) sh->color = col;
    }
    sh->x = nums[0]; sh->y = nums[1]; sh->blur = nums[2]; sh->spread = nums[3];
    free_toks(t, n);
    return nn >= 2 && COLOR_A(sh->color) > 0;
}
/* one shadow is painted: the first visible outer one, else the first visible inset one
   (Tailwind stacks several, most of them `0 0 #0000`) */
static void parse_shadow(ACtx *c, const char *v, Shadow *sh, bool *has) {
    *has = false;
    if (str_ieq(v, "none")) return;
    char *all = xstrdup(v), *seg = all; int d = 0; Shadow cur, inset = {0}; bool have_inset = false;
    for (char *q = all; ; q++) {
        if (*q == '(') d++; else if (*q == ')') d--;
        else if ((*q == ',' && !d) || !*q) {
            bool end = !*q; *q = 0;
            if (parse_one_shadow(c, seg, &cur)) {
                if (!cur.inset) { *sh = cur; *has = true; break; }
                if (!have_inset) { inset = cur; have_inset = true; }
            }
            if (end) break;
            seg = q + 1;
        }
    }
    if (!*has && have_inset) { *sh = inset; *has = true; }
    free(all);
}
/* Grid1/2 automatic repetition requires fixed-size tracks throughout the
 * list. Reuse the track parser's dimensions, rather than recognizing a set
 * of failing strings. Grid3 intrinsic automatic repetition is not supported.
 * https://drafts.csswg.org/css-grid-2/#repeat-syntax */
static int split_top(char *s, char sep, char **out, int max);
static bool angle_or_time(const char *v);
#include "numeric-count.h"
static bool application_number(ACtx *c, const char *text, bool length_hint, CssNumber *out) {
    bool root_used = false; LCtx context = application_scalar_context(c, &root_used);
    bool ok = css_number_parse_context(text, c->em, context.rem, c->mc, length_hint,
                                      &context, c->validation_only, out);
    if (root_used && c->st) c->st->inherited_dynamic = true;
    return ok;
}
static bool application_integer(ACtx *c, const char *text, int minimum, int *out) {
    bool math = strchr(text, '(') != NULL;
    if (!math) {
        const char *p = text; if (*p == '+' || *p == '-') p++;
        if (!isdigit((unsigned char)*p)) return false;
        while (isdigit((unsigned char)*p)) p++;
        if (*p) return false;
    }
    CssNumber value;
    if (!application_number(c, text, false, &value) || !css_number_scalar(&value)) return false;
    if (!math && minimum != INT_MIN && value.value < minimum) return false;
    double rounded = isnan(value.value) ? 0 : floor(value.value + .5);
    *out = rounded <= minimum ? minimum : rounded >= INT_MAX ? INT_MAX : (int)rounded;
    return true;
}
/* Flex's two factors are numbers, not lengths. Keep the range restriction
 * separate from math typing: a negative literal is invalid, while a valid
 * calculation is clamped at computed-value time. */
static bool flex_factor(ACtx *c, const char *v, float *out) {
    bool math = strchr(v, '(') != NULL;
    if (!math && !isdigit((unsigned char)*v) && *v != '+' && *v != '-' && *v != '.') return false;
    CssNumber n;
    if (!application_number(c, v, false, &n) || !css_number_scalar(&n)) return false;
    if (!math && (!isfinite(n.value) || n.value < 0)) return false;
    double value = isnan(n.value) ? 0 : n.value;
    *out = (float)LMIN(3.4028234663852886e38, LMAX(0.0, value));
    return true;
}
static bool flex_basis(ACtx *c, const char *v, Length *out) {
    if (sizing_stretch_keyword(v)) return alen_size(c, v, out);
    if (str_ieq(v, "content")) { *out = (Length){ .kind = LK_CONTENT }; return true; }
    if (str_ieq(v, "auto") || str_ieq(v, "min-content") ||
        str_ieq(v, "max-content") || str_ieq(v, "fit-content")) return alen(c, v, out);
    bool math = strchr(v, '(') != NULL;
    if (!math && !isdigit((unsigned char)*v) && *v != '+' && *v != '-' && *v != '.') return false;
    CssNumber n;
    if (!css_number_parse_hint(v, c->em, c->rem, c->mc, true, &n)) return false;
    bool length = n.power[CN_LENGTH] == 1;
    for (int i = 1; i < CN_DIMS; i++) if (n.power[i]) length = false;
    if (!length && !(css_number_scalar(&n) && !math && n.value == 0)) return false;
    if (!math && (!isfinite(n.value) || n.value < 0)) return false;
    Length result = L_px(0);
    if (!alen_size(c, v, &result)) return false;
    *out = result;
    return true;
}
static bool flex_parse(ACtx *c, const char *value, float *grow, float *shrink, Length *basis) {
    if (str_ieq(value, "none")) { *grow = *shrink = 0; *basis = L_auto(); return true; }
    char *tokens[4]; int count = split_ws(value, tokens, 4), number_end = -1;
    bool have_grow = false, have_shrink = false, have_basis = false, valid = count > 0 && count < 4;
    float g = 1, s = 1; Length b = L_px(0); b.pctu = true;
    for (int i = 0; valid && i < count; i++) {
        float factor;
        bool number = flex_factor(c, tokens[i], &factor);
        if (number && !have_grow) { g = factor; have_grow = true; number_end = i; }
        else if (number && !have_shrink && i == number_end + 1) { s = factor; have_shrink = true; number_end = i; }
        else if (!have_basis && flex_basis(c, tokens[i], &b)) have_basis = true;
        else valid = false;
    }
    free_toks(tokens, count);
    if (valid) { *grow = g; *shrink = s; *basis = b; }
    return valid;
}
bool css_flex_valid(const char *prop, const char *value) {
    if (strcmp(prop, "flex") && strcmp(prop, "flex-grow") && strcmp(prop, "flex-shrink") && strcmp(prop, "flex-basis")) return true;
    if (str_ieq(value, "initial") || str_ieq(value, "inherit") || str_ieq(value, "unset") ||
        str_ieq(value, "revert") || str_ieq(value, "revert-layer") || strstr(value, "var(") || strstr(value, "env(") || strstr(value, "attr(")) return true;
    ACtx c = { .em = 16, .rem = 16, .validation_only = true }; float g, sh; Length b;
    return !strcmp(prop, "flex") ? flex_parse(&c, value, &g, &sh, &b) :
        !strcmp(prop, "flex-basis") ? flex_basis(&c, value, &b) : flex_factor(&c, value, &g);
}

/* Literal integer tokens enforce the grammar range immediately; math functions
 * are typed as numbers, rounded toward +infinity at ties, then range-clamped.
 * Materialized tracks use the same 1000-track limit as the surrounding parser. */
static bool grid_repeat_count(ACtx *c, const char *text, long *count) {
    const char *p = text; if (*p == '+') p++;
    if (isdigit((unsigned char)*p)) {
        long value = 0;
        while (isdigit((unsigned char)*p)) { if (value < 1000) value = LMIN(1000, value * 10 + *p - '0'); p++; }
        if (!*p) { if (!value) return false; *count = value; return true; }
    }
    if (!(isalpha((unsigned char)*text) || *text == '-') || !strchr(text, '(')) return false;
    CssNumber value;
    if (!application_number(c, text, false, &value) || !css_number_scalar(&value)) return false;
    double rounded = isnan(value.value) ? 0 : floor(value.value + .5);
    *count = rounded <= 1 ? 1 : rounded >= 1000 ? 1000 : (long)rounded;
    return true;
}
static bool column_property(const char *prop) {
    return !strcmp(prop, "columns") || !strcmp(prop, "column-width") || !strcmp(prop, "column-count");
}
static bool column_count_value(ACtx *c, const char *text, unsigned *count) {
    int value;
    if (!application_integer(c, text, 1, &value)) return false;
    *count = (unsigned)value; return true;
}
static bool columns_parse(ACtx *c, const char *prop, const char *text, Length *width, unsigned *count) {
    char *tokens[3]; int n = split_ws(text, tokens, 3);
    bool shorthand = !strcmp(prop, "columns"), have_width = false, have_count = false;
    bool ok = n > 0 && n <= (shorthand ? 2 : 1);
    Length w = L_auto(); unsigned number = 0;
    for (int i = 0; ok && i < n; i++) {
        if (str_ieq(tokens[i], "auto")) continue;
        unsigned integer; ContainIntrinsicSize length[2];
        if (strcmp(prop, "column-width") && !have_count && column_count_value(c, tokens[i], &integer)) {
            number = integer; have_count = true;
        } else if (strcmp(prop, "column-count") && !have_width &&
            contain_intrinsic_parse(c, tokens[i], false, length) && length[0].flags == CIS_LENGTH) {
            w = L_px(length[0].length); have_width = true;
        } else ok = false;
    }
    free_toks(tokens, n);
    if (ok) { *width = w; *count = number; } return ok;
}
bool css_columns_valid(const char *prop, const char *value) {
    if (!column_property(prop)) return true;
    if (var_wide_keyword(value) || css_value_has_var(value)) return true;
    ACtx c = { .em = 16, .rem = 16, .validation_only = true }; Length width; unsigned count;
    return columns_parse(&c, prop, value, &width, &count);
}
typedef struct { bool fixed, automatic, repeated; } GridTrackParseInfo;
static bool grid_fixed_dimension(const char *text, Length value, bool parsed) {
    if (!parsed || value.kind != LK_LEN || angle_or_time(text)) return false;
    /* Literal negative sizes are invalid; math ranges are checked after
       computation, so a calc() remains a fixed-size grammar component. */
    if (*text == '+' || *text == '-' || *text == '.' || isdigit((unsigned char)*text)) {
        char *end; float number = strtof(text, &end);
        if (end != text && (number < 0 || (!*end && number != 0))) return false;
    }
    return true;
}
static const char *grid_name_end(const char *p);
static int parse_grid_tracks_info(ACtx *c, const char *v, GridTrack **out, GridTrackParseInfo *info, bool repeated_body) {
    VEC(GridTrack) tr = {0};
    const char *p = v; char *t = NULL;
    bool fixed = true, automatic = false, repeated = false, have_track = false, subgrid = false;
    if (out) *out = NULL;
    for (;;) {
        while (is_ws((unsigned char)*p)) p++;
        /* A bracketed line-name list is one component even with spaces. */
        if (*p == '[') { const char *end = grid_name_end(p + 1); if (!end) goto invalid; p = end + 1; continue; }
        t = next_token(&p); if (!t) break;
        size_t tl = strlen(t);
        if (!have_track && str_ieq(t, "subgrid")) subgrid = true;
        if (str_istarts(t, "repeat(")) {
            /* Subgrid repeats line names, not track sizes; retain that separate
               existing syntax path without applying the automatic-track rule. */
            if (subgrid) { free(t); t = NULL; continue; }
            if (repeated_body) goto invalid;
            if (tl < 9 || t[tl - 1] != ')') goto invalid;
            char *inner = xstrndup(t + 7, tl - 8), *parts[3];
            int np = split_top(inner, ',', parts, 3);
            if (np != 2) { free(inner); goto invalid; }
            char *count = str_trim(parts[0]);
            bool auto_count = str_ieq(count, "auto-fill") || str_ieq(count, "auto-fit");
            long repeat = 1;
            if (!auto_count && !grid_repeat_count(c, count, &repeat)) { free(inner); goto invalid; }
            GridTrack *sub = NULL; GridTrackParseInfo si;
            int ns = parse_grid_tracks_info(c, parts[1], out ? &sub : NULL, &si, true);
            if (ns <= 0 || si.automatic || (auto_count && (automatic || !si.fixed || si.repeated))) { free(sub); free(inner); goto invalid; }
            fixed &= si.fixed; automatic |= auto_count; repeated = true; have_track = true;
            if (out) {
                if (auto_count) {
                    /* Retain the complete pattern and its actual sizing
                       functions. Repetition depends on layout's available
                       axis size; a fit-content value is not a repeat marker. */
                    int keep = LMIN(ns, 1000 - tr.n);
                    for (int j = 0; j < keep; j++) {
                        GridTrack g = sub[j]; g.repeat_span = j ? 0 : (uint16_t)keep;
                        g.repeat_kind = str_ieq(count, "auto-fit") ? GRID_REPEAT_FIT : GRID_REPEAT_FILL;
                        vec_push(tr, g);
                    }
                } else {
                    /* Layout's implicit placement is bounded to1000 tracks;
                       parsed repetition must obey the same storage bound. */
                    for (long k = 0; k < repeat && tr.n < 1000; k++) for (int j = 0; j < ns && tr.n < 1000; j++) vec_push(tr, sub[j]);
                }
            }
            free(sub); free(inner); free(t); t = NULL; continue;
        }
        GridTrack g; memset(&g, 0, sizeof g); g.size = L_auto(); g.min = L_auto();
        bool track_fixed = false;
        if (tl > 2 && str_ieq(t + tl - 2, "fr")) { g.fr = strtof(t, NULL); g.min.kind = LK_LEN; }
        else if (str_istarts(t, "minmax(")) {
            if (tl < 9 || t[tl - 1] != ')') goto invalid;
            char *inner = xstrndup(t + 7, tl - 8), *parts[3];
            int np = split_top(inner, ',', parts, 3);
            if (np == 2) {
                char *mn = str_trim(parts[0]), *mx = str_trim(parts[1]);
                bool min_ok;
                if (str_ieq(mn, "auto")) { g.min.kind = LK_MIN_CONTENT; min_ok = true; }
                else min_ok = alen(c, mn, &g.min);
                size_t ml = strlen(mx); bool max_ok;
                if (ml > 2 && str_ieq(mx + ml - 2, "fr")) {
                    char *end; g.fr = strtof(mx, &end); max_ok = end != mx && str_ieq(end, "fr") && isfinite(g.fr) && g.fr >= 0;
                } else max_ok = alen(c, mx, &g.size);
                size_t nl = strlen(mn); if (nl > 2 && str_ieq(mn + nl - 2, "fr")) min_ok = false;
                bool min_fixed = grid_fixed_dimension(mn, g.min, min_ok), max_fixed = grid_fixed_dimension(mx, g.size, max_ok);
                if (g.min.kind == LK_LEN && !min_fixed) min_ok = false;
                if (g.size.kind == LK_LEN && !max_fixed) max_ok = false;
                bool min_inflexible = min_ok && (g.min.kind == LK_LEN || g.min.kind == LK_AUTO || g.min.kind == LK_MIN_CONTENT || g.min.kind == LK_MAX_CONTENT);
                track_fixed = min_ok && max_ok && (min_fixed || (min_inflexible && max_fixed));
            }
            free(inner);
        } else { bool ok = alen(c, t, &g.size); track_fixed = grid_fixed_dimension(t, g.size, ok); }
        fixed &= track_fixed; have_track = true;
        if (out && tr.n < 1000) vec_push(tr, g);
        free(t); t = NULL;
    }
    if (automatic && !fixed) goto invalid;
    *info = (GridTrackParseInfo){fixed, automatic, repeated};
    if (out) { *out = tr.v; return tr.n; }
    return have_track ? 1 : 0;
invalid:
    free(t); free(tr.v); *info = (GridTrackParseInfo){false, false, false};
    return -1;
}
static int parse_grid_tracks(ACtx *c, const char *v, GridTrack **out) {
    GridTrackParseInfo info;
    return parse_grid_tracks_info(c, v, out, &info, false);
}
static bool grid_repeat_value_valid(const char *value) {
    /* Unsubstituted values remain pending at parse/support-query time. */
    for (const char *p = value; *p; p++) if (str_istarts(p, "var(") || str_istarts(p, "env(") || str_istarts(p, "attr(")) return true;
    char *copy = xstrdup(value), *comment;
    while ((comment = strstr(copy, "/*"))) { char *end = strstr(comment + 2, "*/"); if (!end) { *comment = 0; break; } *comment = ' '; memmove(comment + 1, end + 2, strlen(end + 2) + 1); }
    ACtx c = {0}; c.em = c.rem = 16; GridTrackParseInfo info;
    bool valid = parse_grid_tracks_info(&c, copy, NULL, &info, false) >= 0;
    free(copy); return valid;
}
bool css_grid_repeat_valid(const char *property, const char *value) {
    if (!strcmp(property, "grid-template-columns") || !strcmp(property, "grid-template-rows")) return grid_repeat_value_valid(value);
    if (strcmp(property, "grid") && strcmp(property, "grid-template")) return true;
    char *copy = xstrdup(value), *parts[3]; int n = split_top(copy, '/', parts, 3); bool valid = true;
    for (int i = 0; i < n; i++) if (!grid_repeat_value_valid(parts[i])) valid = false;
    free(copy); return valid;
}

int css_parse_grid_tracks(const char *value, float em, float rem, const MediaCtx *media, GridTrack **out) {
    ACtx context = { .em = em, .rem = rem, .mc = media };
    *out = NULL;
    int count = parse_grid_tracks(&context, value, out);
    if (count < 0) { free(*out); *out = NULL; return 0; }
    return count;
}
static GridLine parse_grid_component(const char *v) {
    bool is_span = false; int number = 0;
    for (const char *p = v; *p; ) {
        while (isspace((unsigned char)*p)) p++;
        const char *q = p; while (*q && !isspace((unsigned char)*q)) q++;
        size_t n = (size_t)(q - p);
        if (n == 4 && !strncasecmp(p, "span", 4)) is_span = true;
        else if (n) {
            char *end; long value = strtol(p, &end, 10);
            if (end == q && end != p) number = (int)LMAX(-10000L, LMIN(value, 10000L));
        }
        p = q;
    }
    /* Named-line lookup remains a separate placement feature. Preserve the
       numeric ordinal/span when an identifier accompanies it. */
    return is_span ? (GridLine){ .span = LMAX(1, number) } : (GridLine){ .number = number };
}
static void parse_grid_line(const char *v, GridAxis *axis) {
    char *s = xstrdup(v), *slash = strchr(s, '/');
    if (slash) *slash++ = 0;
    axis->start = parse_grid_component(s);
    axis->end = slash ? parse_grid_component(slash) : (GridLine){0};
    free(s);
}
/* alen() also takes angles and times (for calc and transforms); a gap with one is invalid */
static bool angle_or_time(const char *v) {
    static const char *const u[] = { "deg", "grad", "rad", "turn", "ms", "s" };
    size_t n = strlen(v);
    for (size_t i = 0; i < sizeof u / sizeof *u; i++) { size_t k = strlen(u[i]); if (n > k && !strcasecmp(v + n - k, u[i]) && (isdigit((unsigned char)v[n - k - 1]) || v[n - k - 1] == '.')) return true; }
    return false;
}
/* Physical left/right keywords only belong to justify-* in CSS Box Alignment.
 * Reject the entire align-* declaration before updating either the compact
 * value or its qualifier metadata, so an earlier valid declaration survives. */
static bool align_axis_value(const char *v) {
    while (*v) {
        while (isspace((unsigned char)*v)) v++;
        const char *start = v;
        while (*v && !isspace((unsigned char)*v)) v++;
        size_t n = (size_t)(v - start);
        if ((n == 4 && !strncasecmp(start, "left", n)) ||
            (n == 5 && !strncasecmp(start, "right", n))) return false;
    }
    return true;
}
static uint8_t parse_align(const char *v) {
    static const char *const al[] = { "auto", "normal", "stretch", "flex-start", "flex-end", "center", "baseline", "space-between", "space-around", "space-evenly", "start", "end", NULL };
    const char *x = v; if (str_istarts(x, "safe ")) x += 5; else if (str_istarts(x, "unsafe ")) x += 7;
    if (str_istarts(x, "first baseline") || str_istarts(x, "last baseline")) return AL_BASELINE;
    int k = kw(x, al);
    if (k < 0) { if (str_ieq(x, "left") || str_ieq(x, "self-start")) return AL_START; if (str_ieq(x, "right") || str_ieq(x, "self-end")) return AL_END; return AL_NORMAL; }
    return (uint8_t)k;
}

/* Independent inherited properties: no other value derives from them, so a
 * parent change reaches children that did not set them without rematching. */
enum { INDEP_POINTER = 1, INDEP_VISIBILITY = 2, INDEP_CURSOR = 4, INDEP_USER_SELECT = 8,
    INDEP_WHITE_SPACE = 16, INDEP_TEXT_TRANSFORM = 32, INDEP_WORD_BREAK = 64, INDEP_OVERFLOW_WRAP = 128,
    INH_OTHER = 256, INH_UNCOMPARED = 512 /* a value `inherit` copies without inherited_diff comparing it */ };
static unsigned indep_bit(const char *P) {
    switch (P[0]) {
    case 'p': return !strcmp(P, "pointer-events") ? INDEP_POINTER : 0;
    case 'v': return !strcmp(P, "visibility") ? INDEP_VISIBILITY : 0;
    case 'c': return !strcmp(P, "cursor") ? INDEP_CURSOR : 0;
    case 'u': return !strcmp(P, "user-select") ? INDEP_USER_SELECT : 0;
    case '-': return !strcmp(P, "-webkit-user-select") ? INDEP_USER_SELECT : 0;
    case 'w': return !strncmp(P, "white-space", 11) ? INDEP_WHITE_SPACE : !strcmp(P, "word-break") ? INDEP_WORD_BREAK :
        !strcmp(P, "word-wrap") ? INDEP_OVERFLOW_WRAP : 0;
    case 't': return !strcmp(P, "text-transform") ? INDEP_TEXT_TRANSFORM : !strncmp(P, "text-wrap", 9) ? INDEP_WHITE_SPACE : 0;
    case 'o': return !strcmp(P, "overflow-wrap") ? INDEP_OVERFLOW_WRAP : 0;
    case 'a': return !strcmp(P, "all") ? 0xff : 0;
    }
    return 0;
}
/* The non-inherited fields an explicit inherit copies from the parent below;
 * inherited_diff() doesn't compare them. inherit of the remaining properties
 * either copies a compared field or is ignored, so it adds no dependency. */
/* border-radius: 4, a corner longhand: its index (tl tr br bl), else -1 */
static int border_corner(const char *P) {
    static const char *const corners[] = { "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius", "border-bottom-left-radius" };
    if (!strcmp(P, "border-radius")) return 4;
    for (int i = 0; i < 4; i++) if (!strcmp(P, corners[i])) return i;
    return -1;
}
static bool inherit_copies_uncompared(const char *P) {
    static const char *const props[] = { "background-color","background","width","height","margin","padding","inset","border","position", NULL };
    for (int i = 0; props[i]; i++) if (!strcmp(P, props[i])) return true;
    return !strncmp(P, "border-", 7) || border_corner(P) >= 0 || !strcmp(P, "contain") || contain_intrinsic_property(P) != -2 || column_property(P);
}
static const char *const known_props[] = { "animation","animation-name","animation-duration","animation-delay","animation-iteration-count","animation-timing-function","animation-fill-mode","animation-direction","animation-play-state","transition","transition-property","transition-duration","transition-delay","transition-timing-function","display","position","float","clear","width","height","min-width","min-height","max-width","max-height","margin","margin-top","margin-right","margin-bottom","margin-left","padding","padding-top","padding-right","padding-bottom","padding-left","border","border-width","border-style","border-color","border-top","border-right","border-bottom","border-left","border-radius","color","background","background-color","background-image","font","font-size","font-weight","font-family","font-style","line-height","text-align","text-decoration","white-space","overflow","overflow-x","overflow-y","visibility","opacity","z-index","top","right","bottom","left","inset","flex","flex-direction","flex-wrap","flex-grow","flex-shrink","flex-basis","justify-content","align-items","align-self","align-content","gap","row-gap","column-gap","order","grid","grid-template-columns","grid-template-rows","grid-column","grid-row","grid-area","transform","transition","animation","box-shadow","text-shadow","box-sizing","cursor","pointer-events","content","list-style","list-style-type","vertical-align","text-transform","letter-spacing","word-spacing","text-indent","text-overflow","word-break","overflow-wrap","word-wrap","object-fit","aspect-ratio","filter","outline","user-select","appearance","will-change","contain","isolation","mix-blend-mode","place-items","place-content","place-self","justify-items","justify-self","table-layout","border-collapse","border-spacing","clip-path","mask","mask-image","mask-size","-webkit-mask","-webkit-mask-image","-webkit-mask-size","resize","scroll-behavior","overscroll-behavior","touch-action","font-variant","text-rendering","-webkit-font-smoothing","fill","stroke","caret-color","accent-color","color-scheme","translate","scale","rotate","container-type","backdrop-filter","line-clamp","-webkit-line-clamp","text-wrap","hyphens","tab-size","direction","unicode-bidi","writing-mode","inset-inline","inset-block","margin-inline","margin-block","padding-inline","padding-block","inline-size","block-size","min-inline-size","max-inline-size","min-block-size","max-block-size", NULL };
bool css_property_known(const char *p) { return kw(p, known_props) >= 0 || gen_prop(p); }

static char *parse_grid_areas(const char *v) {
    if (str_ieq(v, "none")) return NULL;
    SB b; sb_init(&b); bool any = false;
    for (const char *q = v; *q; q++) {
        if (*q != '"' && *q != '\'') continue;
        char qc = *q++; if (any) sb_putc(&b, '/');
        bool sp = true;
        for (; *q && *q != qc; q++) { if (*q == ' ' || *q == '\t') { if (!sp) sb_putc(&b, ' '); sp = true; } else { sb_putc(&b, *q); sp = false; } }
        while (b.n && b.s[b.n - 1] == ' ') b.n--;
        any = true; if (!*q) break;
    }
    if (!any) { sb_free(&b); return NULL; }
    return sb_take(&b);
}

void (*css_style_change_hook)(Node *n, const ComputedStyle *old, const ComputedStyle *now);

static int split_top(char *s, char sep, char **out, int max) {
    int n = 0, depth = 0; char *start = s;
    for (char *p = s;; p++) {
        bool end = !*p;
        if (*p == '(') depth++; else if (*p == ')') depth--;
        if (end || (depth == 0 && (sep == ' ' ? is_ws((unsigned char)*p) : *p == sep))) {
            *p = 0;
            char *t = str_trim(start);
            if (*t && n < max) out[n++] = t;
            start = p + 1;
        }
        if (end) break;
    }
    return n;
}
static bool css_time(const char *t, float *out) {
    return css_scalar_math(t, MATH_TIME, 16, 16, out);
}
static bool anim_kw(const char *t) {
    static const char *const k[] = { "linear", "ease", "ease-in", "ease-out", "ease-in-out", "step-start", "step-end", "normal", "reverse", "alternate", "alternate-reverse", "forwards", "backwards", "both", "running", "paused", "allow-discrete" };
    for (size_t i = 0; i < sizeof k / sizeof *k; i++) if (str_ieq(t, k[i])) return true;
    return strchr(t, '(') != NULL;
}
static const char *layer_list(char **layer, int nl) {
    if (!nl) return NULL;
    SB b; sb_init(&b);
    for (int l = 0; l < nl; l++) { if (l) sb_puts(&b, ","); sb_puts(&b, layer[l]); }
    char *s = sb_take(&b); const char *a = atom(s); free(s);
    return a;
}
/* `animation` / `transition` shorthands: the raw longhand lists for the generic store (the JS animation side reads them) */
static void anim_shorthand_gen(ComputedStyle *st, const char *Q, const char *val) {
    bool tr = Q[0] == 't';
    static const char *const an[] = { "animation-duration", "animation-delay", "animation-timing-function", "animation-iteration-count", "animation-direction", "animation-fill-mode", "animation-play-state", "animation-name" };
    static const char *const tn[] = { "transition-duration", "transition-delay", "transition-timing-function", "transition-behavior", "transition-property" };
    static const char *const ad[] = { "0s", "0s", "ease", "1", "normal", "none", "running", "none" }, *const td[] = { "0s", "0s", "ease", "normal", "all" };
    const char *const *names = tr ? tn : an, *const *defs = tr ? td : ad;
    int nf = tr ? 5 : 8;
    SB out[8]; for (int f = 0; f < nf; f++) sb_init(&out[f]);
    const char *p = val; int layer = 0;
    while (*p) {
        const char *e = p; int d = 0;
        for (; *e && (d || *e != ','); e++) { if (*e == '(') d++; else if (*e == ')') d--; }
        char *ly = xstrndup(p, (size_t)(e - p)); const char *v[8] = { 0 }; char *tok[8]; int nt = 0, ntime = 0;
        for (char *q = ly; *q && nt < 8;) {
            while (*q == ' ') q++;
            if (!*q) break;
            char *t = q; int dd = 0;
            for (; *q && (dd || *q != ' '); q++) { if (*q == '(') dd++; else if (*q == ')') dd--; }
            if (*q) *q++ = 0;
            tok[nt++] = t;
        }
        for (int i = 0; i < nt; i++) {
            const char *t = tok[i]; float f;
            bool ease = !strcmp(t, "linear") || !strncmp(t, "ease", 4) || !strncmp(t, "step-", 5) || !strncmp(t, "cubic-bezier(", 13) || !strncmp(t, "steps(", 6) || !strncmp(t, "linear(", 7);
            if (css_time(t, &f) && ntime < 2) v[ntime++] = t;
            else if (ease && !v[2]) v[2] = t;
            else if (tr) { if (!strcmp(t, "allow-discrete") || (!strcmp(t, "normal") && !v[3])) v[3] = t; else v[4] = t; }
            else if ((!strcmp(t, "infinite") || isdigit((unsigned char)t[0]) || t[0] == '.') && !v[3]) v[3] = t;
            else if ((!strcmp(t, "reverse") || !strcmp(t, "alternate") || !strcmp(t, "alternate-reverse") || (!strcmp(t, "normal") && !v[4])) && !v[4]) v[4] = t;
            else if ((!strcmp(t, "forwards") || !strcmp(t, "backwards") || !strcmp(t, "both") || (!strcmp(t, "none") && !v[5] && i + 1 < nt)) && !v[5]) v[5] = t;
            else if ((!strcmp(t, "running") || !strcmp(t, "paused")) && !v[6]) v[6] = t;
            else v[7] = t;
        }
        for (int f = 0; f < nf; f++) { if (layer) sb_puts(&out[f], ", "); sb_puts(&out[f], v[f] ? v[f] : defs[f]); }
        free(ly); layer++;
        p = *e ? e + 1 : e;
    }
    for (int f = 0; f < nf; f++) gen_put(&st->gen_n, names[f], sb_take(&out[f]));
}
static void anim_decl(ComputedStyle *st, const char *Q, const char *val) {
    char *buf = xstrdup(val), *layer[32], *tok[16];
    int nl = split_top(buf, ',', layer, 32);
    float f;
    if (!strcmp(Q, "animation")) {
        st->anim_name = NULL; st->anim_dur = st->anim_delay = 0; st->anim_iter = 1;
        int nt = nl ? split_top(layer[0], ' ', tok, 16) : 0, ntime = 0;
        for (int i = 0; i < nt; i++) {
            char *e;
            if (css_time(tok[i], &f)) { if (ntime++ == 0) st->anim_dur = f; else st->anim_delay = f; }
            else if (str_ieq(tok[i], "infinite")) st->anim_iter = INFINITY;
            else if ((f = strtof(tok[i], &e)), e != tok[i] && !*e) st->anim_iter = f;
            else if (!anim_kw(tok[i]) && !str_ieq(tok[i], "none")) st->anim_name = atom(tok[i]);
        }
    } else if (!strcmp(Q, "animation-name")) st->anim_name = nl && !str_ieq(layer[0], "none") ? atom(layer[0]) : NULL;
    else if (!strcmp(Q, "animation-duration")) st->anim_dur = nl && css_time(layer[0], &f) ? f : 0;
    else if (!strcmp(Q, "animation-delay")) st->anim_delay = nl && css_time(layer[0], &f) ? f : 0;
    else if (!strcmp(Q, "animation-iteration-count")) st->anim_iter = !nl ? 1 : str_ieq(layer[0], "infinite") ? INFINITY : strtof(layer[0], NULL);
    else if (!strcmp(Q, "transition")) {
        st->tr_dur = st->tr_delay = 0;
        for (int l = 0; l < nl; l++) {
            int nt = split_top(layer[l], ' ', tok, 16), ntime = 0; char *prop = (char *)"all";
            for (int i = 0; i < nt; i++) {
                if (css_time(tok[i], &f)) { if (ntime++ == 0) { if (f > st->tr_dur) st->tr_dur = f; } else if (f > st->tr_delay) st->tr_delay = f; }
                else if (!anim_kw(tok[i])) prop = tok[i];
            }
            layer[l] = prop;
        }
        st->tr_prop = layer_list(layer, nl);
    } else if (!strcmp(Q, "transition-property")) st->tr_prop = layer_list(layer, nl);
    else if (!strcmp(Q, "transition-duration") || !strcmp(Q, "transition-delay")) {
        float m = 0;
        for (int l = 0; l < nl; l++) if (css_time(layer[l], &f) && f > m) m = f;
        if (!strcmp(Q, "transition-duration")) st->tr_dur = m; else st->tr_delay = m;
    }
    free(buf);
}

/* Logical sizes map to the physical ones; in vertical writing modes inline is the height */
static const char *logical_size_prop(const char *p, bool vert) {
    /* Most callers query ordinary physical properties. Dispatch by the first
       character instead of scanning every name for a suffix on the hot path. */
    if (p[0] == 'i') {
        if (!strcmp(p, "inline-size")) return vert ? "height" : "width";
    } else if (p[0] == 'b') {
        if (!strcmp(p, "block-size")) return vert ? "width" : "height";
    } else if (p[0] == 'c') {
        if (!strcmp(p, "contain-intrinsic-inline-size")) return vert ? "contain-intrinsic-height" : "contain-intrinsic-width";
        if (!strcmp(p, "contain-intrinsic-block-size")) return vert ? "contain-intrinsic-width" : "contain-intrinsic-height";
    } else if (p[0] == 'm') {
        if (!strcmp(p, "min-inline-size")) return vert ? "min-height" : "min-width";
        if (!strcmp(p, "max-inline-size")) return vert ? "max-height" : "max-width";
        if (!strcmp(p, "min-block-size")) return vert ? "min-width" : "min-height";
        if (!strcmp(p, "max-block-size")) return vert ? "max-width" : "max-height";
    }
    return p;
}

/* Flow-relative box edges share the physical slots selected by the final
 * writing mode. Values remain distinct until their declarations cascade. */
static bool flow_basis_prop(const char *p) {
    return (p[0] == 'w' && !strcmp(p, "writing-mode")) ||
           (p[0] == 'd' && !strcmp(p, "direction")) ||
           (p[0] == 't' && !strcmp(p, "text-orientation"));
}
static int logical_edge_side(const ComputedStyle *s, bool block, bool end) {
    bool vertical = s && s->writing_mode != 0, rtl = s && s->direction;
    if (vertical && rtl) {
        const GenProp *g = gen_prop("text-orientation");
        const char *orientation = g ? gen_value(s, g) : NULL;
        if (orientation && str_ieq(orientation, "upright")) rtl = false;
    }
    int side = block ? (!vertical ? 0 : s->writing_mode == 2 ? 1 : 3)
                     : (!vertical ? (rtl ? 1 : 3) : (rtl ? 2 : 0));
    return end ? (side + 2) % 4 : side;
}
static int box_edge_group(const char *p, const char **tail) {
    if (p[0] == 'm' && !strncmp(p, "margin-", 7)) { *tail = p + 7; return 0; }
    if (p[0] == 'p' && !strncmp(p, "padding-", 8)) { *tail = p + 8; return 1; }
    if (p[0] == 'i' && !strncmp(p, "inset-", 6)) { *tail = p + 6; return 2; }
    return -1;
}
static const char *const physical_edge_props[3][4] = {
    { "margin-top", "margin-right", "margin-bottom", "margin-left" },
    { "padding-top", "padding-right", "padding-bottom", "padding-left" },
    { "top", "right", "bottom", "left" }
};
static bool logical_edge_pair(const char *p, int *group, bool *block) {
    const char *tail = NULL; int g = box_edge_group(p, &tail);
    if (g < 0 || (strcmp(tail, "block") && strcmp(tail, "inline"))) return false;
    *group = g; *block = tail[0] == 'b'; return true;
}
static const char *const physical_border_props[3][4] = {
    { "border-top-width", "border-right-width", "border-bottom-width", "border-left-width" },
    { "border-top-style", "border-right-style", "border-bottom-style", "border-left-style" },
    { "border-top-color", "border-right-color", "border-bottom-color", "border-left-color" }
};
static bool physical_border_prop(const char *p, int *kind, int *side) {
    if (strncmp(p, "border-", 7)) return false;
    for (int k = 0; k < 3; k++) for (int i = 0; i < 4; i++)
        if (!strcmp(p, physical_border_props[k][i])) { *kind = k; *side = i; return true; }
    return false;
}
static const char *logical_border_prop(const char *p, const ComputedStyle *s) {
    if (p[0] != 'b' || strncmp(p, "border-", 7)) return p;
    const char *tail = p + 7; bool block, end;
    if (!strncmp(tail, "block-", 6)) { block = true; tail += 6; }
    else if (!strncmp(tail, "inline-", 7)) { block = false; tail += 7; }
    else return p;
    if (!strncmp(tail, "start-", 6)) { end = false; tail += 6; }
    else if (!strncmp(tail, "end-", 4)) { end = true; tail += 4; }
    else return p;
    int kind = !strcmp(tail, "width") ? 0 : !strcmp(tail, "style") ? 1 : !strcmp(tail, "color") ? 2 : -1;
    return kind >= 0 ? physical_border_props[kind][logical_edge_side(s, block, end)] : p;
}
static const char *logical_box_prop(const char *p, const ComputedStyle *s) {
    const char *size = logical_size_prop(p, s && s->writing_mode);
    if (size != p) return size;
    const char *border = logical_border_prop(p, s); if (border != p) return border;
    const char *tail = NULL; int group = box_edge_group(p, &tail);
    if (group < 0) return p;
    bool block;
    if (!strncmp(tail, "block-", 6)) { block = true; tail += 6; }
    else if (!strncmp(tail, "inline-", 7)) { block = false; tail += 7; }
    else return p;
    if (strcmp(tail, "start") && strcmp(tail, "end")) return p;
    return physical_edge_props[group][logical_edge_side(s, block, tail[0] == 'e')];
}
static Length *style_edge_values(ComputedStyle *s, int group) {
    return group == 0 ? s->margin : group == 1 ? s->padding : s->inset;
}
static const Length *style_edge_values_const(const ComputedStyle *s, int group) {
    return group == 0 ? s->margin : group == 1 ? s->padding : s->inset;
}
static bool physical_edge_prop(const char *p, int *group, int *side) {
    if (p[0] != 'm' && p[0] != 'p' && p[0] != 't' && p[0] != 'r' && p[0] != 'b' && p[0] != 'l') return false;
    for (int g = 0; g < 3; g++) for (int i = 0; i < 4; i++)
        if (!strcmp(p, physical_edge_props[g][i])) { *group = g; *side = i; return true; }
    return false;
}
/* A logical pair accepts one or two values atomically; invalid second values
 * must not apply a half-declaration. The ordinary physical parsers stay shared. */
static void apply_logical_edge_pair(ACtx *c, const char *value, int group, bool block) {
    char *t[3]; int n = split_ws(value, t, 3); Length a, z;
    if (n > 0 && n < 3 && alen(c, t[0], &a) && alen(c, t[n - 1], &z)) {
        Length *v = style_edge_values(c->st, group);
        v[logical_edge_side(c->st, block, false)] = a;
        v[logical_edge_side(c->st, block, true)] = z;
    }
    free_toks(t, n);
}

static bool custom_defer;
static _Thread_local const CustomProps *custom_origin_base;
static char *animation_value_of(Node *n, const ComputedStyle *s, const char *prop);
static char *grid_computed_tokens(const char *raw, float em, float rem, const MediaCtx *mc, const ComputedStyle *basis, bool *used);
void css_apply_decl(ComputedStyle *st, const ComputedStyle *par, const char *prop, const char *value_in, StyleEngine *e, Node *el) {
    if (prop[0] == '-' && prop[1] == '-') {
        char *kb = xstrdup(value_in), *k = str_trim(kb), *v;
        /* CSS-wide keywords: "" is the guaranteed-invalid value */
        if (str_ieq(k, "inherit") || str_ieq(k, "unset") || str_ieq(k, "revert-layer")) {
            cm_add_name(&st->custom_dependencies, prop);
            const char *pv = par ? custom_get(par->custom, prop) : NULL; v = xstrdup(pv ? pv : "");
        } else if (str_ieq(k, "revert")) {
            cm_add_name(&st->custom_dependencies, prop);
            const char *pv = custom_origin_base ? custom_get(custom_origin_base, prop) : par ? custom_get(par->custom, prop) : NULL;
            v = xstrdup(pv ? pv : "");
        } else if (str_ieq(k, "initial")) v = xstrdup("");
        else if (!*k) v = xstrdup(" ");   /* valid empty value, unlike "" */
        else v = custom_defer ? xstrdup(value_in) : subst_vars(value_in, st, 0);
        free(kb);
        if (!v) v = xstrdup("");
        /* Re-declaring the value already in effect (Tailwind's universal --tw-* resets on every
           element) changes nothing: keep sharing the inherited map instead of copying it. */
        const char *cur = custom_get(st->custom, prop);
        if (cur && !strcmp(cur, v) && !css_value_has_var(v)) { free(v); return; }
        /* custom property: copy-on-write map */
        if (!st->custom || st->custom->refs > 1 || (par && st->custom == par->custom)) {
            CustomProps *n = xcalloc(1, sizeof *n); n->refs = 1;
            if (st->custom && st->custom->depth < CUSTOM_MAX_DEPTH) { n->parent = st->custom; n->depth = st->custom->depth + 1; }
            else if (st->custom) {
                /* Flatten below the chain's root map (usually :root's design tokens):
                 * copying stays small and old and new chains keep a shared ancestor. */
                const CustomProps *chain[CUSTOM_MAX_DEPTH + 1], *c = st->custom; int nc = 0;
                for (; c && c->parent && nc <= CUSTOM_MAX_DEPTH; c = c->parent) chain[nc++] = c;
                if (c && c->parent) { c = NULL; nc = 0; for (const CustomProps *x = st->custom; x && nc <= CUSTOM_MAX_DEPTH; x = x->parent) chain[nc++] = x; }
                if (c) { n->parent = custom_ref((CustomProps *)c); n->depth = c->depth + 1; }
                while (nc-- > 0) hm_foreach((HMap *)&chain[nc]->map, ent) { char *prev = hm_get(&n->map, ent->key); hm_put(&n->map, ent->key, xstrdup((char *)ent->val)); free(prev); }
                custom_unref(st->custom);
            }
            st->custom = n;
        }
        char *old = hm_get(&st->custom->map, prop);
        hm_put(&st->custom->map, prop, v);
        free(old);
        return;
    }
    char *subst = NULL;
    const char *v = value_in;
    if (strstr(v, "cq")) st->inherited_dynamic = true;
    if (css_value_has_var(v)) {
        subst = subst_vars(v, st, 0);
        if (subst && !css_math_spacing_valid(subst)) { free(subst); subst = NULL; }
        /* A failed substitution or property grammar takes unset; recognized
         * CSS-wide keywords keep their existing dedicated dispatch. */
        const char *wide = subst ? var_wide_keyword(subst) : NULL;
        if (!wide) css_apply_decl(st, par, prop, "unset", e, el);
        if (!subst) return;
        char *parsed = wide ? xstrdup(wide) : var_parser_text(subst); free(subst); subst = parsed;
        v = subst;
    }
    char *vbuf = xstrdup(v); char *val = str_trim(vbuf);
    ACtx c = { st, par, e, el, st->font_size, 16, e ? &e->media : NULL };
    g_cq[0] = g_cq[1] = 0;
    if (el && strstr(v, "cq")) for (const Node *a = el->parent; a; a = a->parent)
        if (a->type == NODE_ELEMENT && a->style && a->style->container_type) { g_cq[0] = a->cq_w; g_cq[1] = a->style->container_type == 2 ? a->cq_h : 0; break; }
    if (e && e->doc && e->doc->html && e->doc->html->style && el != e->doc->html) c.rem = e->doc->html->style->font_size;
    const char *P = logical_box_prop(prop, st);
    if (font_phase && font_phase->style == st && !strcmp(P, "line-height")) {
        if (line_height_valid(val)) {
            free(font_phase->owned); font_phase->owned = vbuf; font_phase->value = val;
        } else free(vbuf);
        free(subst); return;
    }
    Length l; Color col; int k, border_kind, border_side;
    bool inherit = str_ieq(val, "inherit"), initial = str_ieq(val, "initial") || str_ieq(val, "revert") || str_ieq(val, "revert-layer"), unset = str_ieq(val, "unset");
    if (flow_basis_prop(P) && !inherit && !initial && !unset) {
        static const char *const modes[] = { "horizontal-tb", "vertical-rl", "vertical-lr", "sideways-rl", "sideways-lr", NULL };
        static const char *const directions[] = { "ltr", "rtl", NULL };
        static const char *const orientations[] = { "mixed", "upright", "sideways", NULL };
        const char *const *values = P[0] == 'w' ? modes : P[0] == 'd' ? directions : orientations;
        if (kw(val, values) < 0) {
            /* Literal invalid declarations are ignored. Invalid substitution
               is invalid at computed-value time, so this inherited property
               takes its unset value instead of resurrecting a lower priority. */
            bool invalid_substitution = subst != NULL;
            free(vbuf); free(subst);
            if (invalid_substitution) css_apply_decl(st, par, P, "unset", e, el);
            return;
        }
    }
    bool flex_value = !strcmp(P, "flex") || !strcmp(P, "flex-grow") ||
                      !strcmp(P, "flex-shrink") || !strcmp(P, "flex-basis");
    if (flex_value && !inherit && !initial && !unset && !css_flex_valid(P, val)) {
        bool invalid_substitution = subst != NULL;
        free(vbuf); free(subst);
        if (invalid_substitution) css_apply_decl(st, par, P, "unset", e, el);
        return;
    }
    bool grid_tracks = !strcmp(P, "grid-template-columns") || !strcmp(P, "grid-template-rows");
    GridTrack *prepared_tracks = NULL; int prepared_count = 0;
    if (!inherit && !initial && !unset) {
        if (grid_tracks && !str_ieq(val, "none")) {
            prepared_count = parse_grid_tracks(&c, val, &prepared_tracks);
            if (prepared_count < 0) goto out;
        } else if ((!strcmp(P, "grid") || !strcmp(P, "grid-template")) && !css_grid_repeat_valid(P, val)) goto out;
    }
    if (column_property(P)) {
        Length width = L_auto(); unsigned count = 0;
        if (inherit && par) { width = par->column_width; count = par->column_count; st->parent_sensitive = true; }
        else if (!inherit && !initial && !unset && !columns_parse(&c, P, val, &width, &count)) goto out;
        if (strcmp(P, "column-count")) st->column_width = width;
        if (strcmp(P, "column-width")) {
            st->column_count = count;
            /* Preserve canonical generic storage for established numeric
               and CSSOM consumers, including shorthand and CSS-wide updates. */
            char canonical[32];
            if (count) snprintf(canonical, sizeof canonical, "%u", count);
            gen_put(&st->gen_n, "column-count", xstrdup(count ? canonical : "auto"));
        }
        goto out;
    }
    /* Intrinsic-size longhands share the final physical slots in cascade order.
       Inherit of a logical longhand uses the parent's corresponding axis. */
    int intrinsic_axis = contain_intrinsic_property(P);
    if (intrinsic_axis != -2) {
        ContainIntrinsicSize values[2] = {{0}, {0}};
        if (inherit && par) {
            if (intrinsic_axis < 0) memcpy(values, par->contain_intrinsic, sizeof values);
            else { int from = contain_intrinsic_property(logical_box_prop(prop, par)); values[0] = par->contain_intrinsic[from]; }
            st->parent_sensitive = true;
        } else if (!inherit && !initial && !unset && !contain_intrinsic_parse(&c, val, intrinsic_axis < 0, values)) goto out;
        if (intrinsic_axis < 0) memcpy(st->contain_intrinsic, values, sizeof values);
        else st->contain_intrinsic[intrinsic_axis] = values[0];
        goto out;
    }
    if (!strcmp(P, "content-visibility")) {
        uint8_t value = CV_VISIBLE;
        if (inherit && par) { value = par->content_visibility; st->parent_sensitive = true; }
        else if (!inherit && !initial && !unset) {
            if (str_ieq(val, "hidden")) value = CV_HIDDEN;
            else if (str_ieq(val, "auto")) value = CV_AUTO;
            else if (!str_ieq(val, "visible")) goto out;
        }
        st->content_visibility = value;
    }
    if (!strcmp(P, "contain")) {
        uint8_t value = 0;
        if (inherit && par) { value = par->size_containment; st->parent_sensitive = true; }
        else if (!inherit && !initial && !unset && !contain_size_parse(val, &value)) goto out;
        st->size_containment = value;
    }
    /* Reject unsupported repetition before replacing arrays or generic values. */
    st->indep_set |= indep_bit(P);
    if (inherit && inherit_copies_uncompared(P)) st->parent_sensitive = true;
    {   /* anchor() insets wait for layout; anchor-name is kept for it */
        if (!strcmp(P, "inset")) for (int k = 0; k < 4; k++) { free(st->anchor_ins[k]); st->anchor_ins[k] = NULL; }
        if (!strcmp(P, "inset") && (inherit || initial || unset)) { for (int k = 0; k < 4; k++) st->inset[k] = inherit && par ? par->inset[k] : (Length){ .kind = LK_AUTO }; free(vbuf); free(subst); return; }
        if (!strcmp(P, "inset") && strstr(val, "anchor(")) {   /* each side may be (or replace) an anchor() inset */
            char *t[4]; int n = split_ws(val, t, 4);
            if (n) { static const char *const sd[4] = { "top", "right", "bottom", "left" }; int ix[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };
                     for (int k = 0; k < 4; k++) css_apply_decl(st, par, sd[k], t[ix[n - 1][k]], e, el); }
            free_toks(t, n);
            if (n) return;
        }
        int ai = !strcmp(P, "top") ? 0 : !strcmp(P, "right") ? 1 : !strcmp(P, "bottom") ? 2 : !strcmp(P, "left") ? 3 : -1;
        if (ai >= 0) { free(st->anchor_ins[ai]); st->anchor_ins[ai] = NULL; if (strstr(val, "anchor(")) { st->anchor_ins[ai] = xstrdup(val); st->inset[ai] = L_auto(); return; } }
        int si = !strcmp(P, "width") ? 0 : !strcmp(P, "height") ? 1 : -1;
        if (si >= 0) { free(st->anchor_sz[si]); st->anchor_sz[si] = NULL; if (strstr(val, "anchor-size(")) { st->anchor_sz[si] = xstrdup(val); if (si) st->height = L_auto(); else st->width = L_auto(); return; } }
        if (!strcmp(P, "position-try-fallbacks") || !strcmp(P, "position-try")) {
            const char *v = val;
            if (P[12] == 0) { static const char *const ord[] = { "normal", "most-width", "most-height", "most-block-size", "most-inline-size" }; static const uint8_t om[] = { 0, 1, 2, 2, 1 }; st->pos_try_order = 0; for (int k = 0; k < 5; k++) { size_t L = strlen(ord[k]); if (str_istarts(v, ord[k]) && is_ws((unsigned char)v[L])) { v += L; while (is_ws((unsigned char)*v)) v++; st->pos_try_order = om[k]; break; } } }
            free(st->pos_try); st->pos_try = str_ieq(v, "none") || str_ieq(v, "normal") || inherit || initial || unset ? NULL : xstrdup(v);
        }
        if (!strcmp(P, "position-try-order")) st->pos_try_order = str_ieq(val, "most-width") || str_ieq(val, "most-inline-size") ? 1 : str_ieq(val, "most-height") || str_ieq(val, "most-block-size") ? 2 : 0;
        if (!strcmp(P, "position-area")) { free(st->pos_area); st->pos_area = str_ieq(val, "none") || inherit || initial || unset ? NULL : xstrdup(val); }
        if (!strcmp(P, "position-anchor")) { free(st->pos_anchor); st->pos_anchor = strncmp(val, "--", 2) || inherit || initial || unset ? NULL : xstrdup(val); if (st->pos_anchor) return; }
        if (!strcmp(P, "anchor-name")) { free(st->anchor_name); st->anchor_name = str_ieq(val, "none") || inherit || initial || unset ? NULL : xstrdup(val); return; }
    }
    if (!strcmp(P, "background-position") || !strcmp(P, "background-position-x") || !strcmp(P, "background-position-y")) {
        int axis = !strcmp(P, "background-position-x") ? 0 : !strcmp(P, "background-position-y") ? 1 : -1;
        if (inherit || initial || unset) { if (inherit) st->parent_sensitive = true; bg_position_copy(st, inherit ? par : NULL, axis); }
        else (void)apply_bg_position(&c, val, axis);
        free(vbuf); free(subst); return;
    }
    char integer_value[32]; const char *generic_value = val;
    if (!strcmp(P, "column-count") && !inherit && !initial && !unset && !str_ieq(val, "auto")) {
        int count;
        if (!application_integer(&c, val, 1, &count)) goto out;
        snprintf(integer_value, sizeof integer_value, "%d", count); generic_value = integer_value;
    }
    const GenProp *gp = gen_prop(P);
    bool anim_longhand = !strncmp(P, "transition-", 11) || !strncmp(P, "animation-", 10);
    /* Retain the computed track-list structure for animation: the layout arrays
       expand repeat() and cannot preserve line names or all minmax components. */
    /* Keep qualifiers used by grid alignment and absolute flex static positions.
       The compact enums still serve public CSSOM and existing in-flow layout. */
    bool abs_align = !strcmp(P, "align-self") || !strcmp(P, "align-items") || !strcmp(P, "align-content") ||
                     !strcmp(P, "justify-self") || !strcmp(P, "justify-items") || !strcmp(P, "justify-content");
    if (abs_align && P[0] == 'a' && !align_axis_value(val)) goto out;
    const char *place_align = !strcmp(P, "place-self") ? "align-self" : !strcmp(P, "place-items") ? "align-items" : !strcmp(P, "place-content") ? "justify-content" : NULL;
    if (inherit && (abs_align || place_align)) st->parent_sensitive = true;
    if (place_align && (inherit || initial || unset)) {
        const char *pv = inherit && par ? gen_value(par, gen_prop(place_align)) : NULL;
        gen_put(&st->gen_n, place_align, xstrdup(pv ? pv : ""));
    }
    if (gp && (!gp->native || anim_longhand || grid_tracks || abs_align)) {
        CustomProps **slot = gp->inherited ? &st->gen_i : &st->gen_n;
        const char *pv = par ? gen_value(par, gp) : NULL;
        if (inherit || (unset && gp->inherited)) gen_put(slot, P, grid_tracks && par ? animation_value_of(el, par, P) : xstrdup(pv ? pv : ""));
        else if (initial || unset) gen_put(slot, P, xstrdup(""));
        else gen_put(slot, P, grid_tracks ? grid_computed_tokens(val, c.em, c.rem, c.mc, st, e ? &e->uses_font_units : NULL) : xstrdup(generic_value));
    }
    {
        const char *Q = strncmp(P, "-webkit-", 8) ? P : P + 8;
        if (!strcmp(Q, "animation") || !strcmp(Q, "transition")) anim_shorthand_gen(st, Q, inherit || initial || unset ? "" : val);
        if (!strncmp(Q, "animation", 9) || !strncmp(Q, "transition", 10)) { anim_decl(st, Q, inherit || initial || unset ? "" : val); free(vbuf); free(subst); return; }
    }
    if (inherit || initial || unset) {
        static ComputedStyle *def; if (!def) def = style_new_default();
        const ComputedStyle *src = inherit ? (par ? par : def) : def;
        /* Native longhands use their actual inheritance metadata. Keep the
         * existing shorthand inheritance path for compound declarations. */
        bool inherited_unset = gp && gp->native ? gp->inherited :
            (strstr(P, "color") == P || strstr(P, "font") || strstr(P, "text") || strstr(P, "line-height") || strstr(P, "white-space") || strstr(P, "visibility") || strstr(P, "cursor") || strstr(P, "letter"));
        if (unset && par && inherited_unset) src = par;
        if (!strcmp(P, "color")) { st->color = src->color; st->color_ser = src->color_ser; }
        else if (!strcmp(P, "font-size")) st->font_size = src->font_size;
        else if (!strcmp(P, "font-weight")) st->font_weight = src->font_weight;
        else if (!strcmp(P, "font-family")) st->font_family = src->font_family;
        else if (!strcmp(P, "line-height")) { st->line_height = src->line_height; st->line_height_normal = src->line_height_normal; st->line_height_factor = src->line_height_factor; }
        else if (!strcmp(P, "background-repeat")) st->bg_repeat = src->bg_repeat;
        else if (!strcmp(P, "background-origin")) st->bg_origin = src->bg_origin;
        else if (!strcmp(P, "background-clip")) st->bg_clip = src->bg_clip;
        else if (!strcmp(P, "background-image")) st->bg_layers = src->bg_layers;
        else if (!strcmp(P, "background-size")) { st->bg_size_kind = src->bg_size_kind; memcpy(st->bg_size, src->bg_size, sizeof st->bg_size); }
        else if (!strcmp(P, "background-color") || !strcmp(P, "background")) { st->bg_color = src->bg_color; st->bg_ser = src->bg_ser; if (!strcmp(P, "background")) { st->bg_repeat = src->bg_repeat; st->bg_origin = src->bg_origin; st->bg_clip = src->bg_clip; st->bg_layers = src->bg_layers; const char *clip = gen_value(src, gen_prop("background-clip")); gen_put(&st->gen_n, "background-clip", xstrdup(clip ? clip : "border-box")); st->bg_size_kind = src->bg_size_kind; memcpy(st->bg_size, src->bg_size, sizeof st->bg_size); bg_position_copy(st, inherit ? par : NULL, -1); free(st->bg_image); st->bg_image = NULL; free(st->bg_gradient); st->bg_gradient = NULL; } }
        else if (!strcmp(P, "display")) { st->display = src->display; st->display_text = src->display_text; }
        else if (!strcmp(P, "text-align")) st->text_align = src->text_align;
        else if (!strcmp(P, "writing-mode")) st->writing_mode = (unset && par ? par : src)->writing_mode;
        else if (!strcmp(P, "direction")) st->direction = (unset && par ? par : src)->direction;
        else if (!strcmp(P, "width") || !strcmp(P, "height") ||
                 !strcmp(P, "min-width") || !strcmp(P, "min-height") ||
                 !strcmp(P, "max-width") || !strcmp(P, "max-height")) {
            /* Logical inherit takes the parent's corresponding logical value,
               whose physical axis can differ from the child's. */
            const char *from = inherit ? logical_box_prop(prop, src) : P;
            const Length *v = !strcmp(from, "width") ? &src->width : !strcmp(from, "height") ? &src->height :
                !strcmp(from, "min-width") ? &src->min_width : !strcmp(from, "min-height") ? &src->min_height :
                !strcmp(from, "max-width") ? &src->max_width : &src->max_height;
            Length *dst = !strcmp(P, "width") ? &st->width : !strcmp(P, "height") ? &st->height :
                !strcmp(P, "min-width") ? &st->min_width : !strcmp(P, "min-height") ? &st->min_height :
                !strcmp(P, "max-width") ? &st->max_width : &st->max_height;
            *dst = *v; if (inherit) st->parent_sensitive = true;
        }
        else if (!strcmp(P, "grid-column-start")) { st->grid_column.start = src->grid_column.start; if (inherit) st->parent_sensitive = true; }
        else if (!strcmp(P, "grid-column-end")) { st->grid_column.end = src->grid_column.end; if (inherit) st->parent_sensitive = true; }
        else if (!strcmp(P, "grid-row-start")) { st->grid_row.start = src->grid_row.start; if (inherit) st->parent_sensitive = true; }
        else if (!strcmp(P, "grid-row-end")) { st->grid_row.end = src->grid_row.end; if (inherit) st->parent_sensitive = true; }
        else if (!strcmp(P, "grid-column")) { st->grid_column = src->grid_column; if (inherit) st->parent_sensitive = true; }
        else if (!strcmp(P, "grid-row")) { st->grid_row = src->grid_row; if (inherit) st->parent_sensitive = true; }
        else if (!strcmp(P, "grid-area")) {
            st->grid_column = src->grid_column; st->grid_row = src->grid_row;
            free(st->grid_area); st->grid_area = src->grid_area ? xstrdup(src->grid_area) : NULL;
            if (inherit) st->parent_sensitive = true;
        }
        else if (grid_tracks) {
            bool rows = !strcmp(P, "grid-template-rows");
            GridTrack **dst = rows ? &st->grid_rows : &st->grid_cols;
            int count = rows ? src->grid_nrows : src->grid_ncols;
            const GridTrack *tracks = rows ? src->grid_rows : src->grid_cols;
            free(*dst); *dst = count ? xmalloc((size_t)count * sizeof **dst) : NULL;
            if (count) memcpy(*dst, tracks, (size_t)count * sizeof **dst);
            if (rows) st->grid_nrows = count; else st->grid_ncols = count;
        }
        else if (!strcmp(P, "margin")) memcpy(st->margin, src->margin, sizeof st->margin);
        else if (!strcmp(P, "padding")) memcpy(st->padding, src->padding, sizeof st->padding);
        else if (!strcmp(P, "border-spacing")) {
            const ComputedStyle *from = unset && par ? par : src;
            memcpy(st->border_spacing, from->border_spacing, sizeof st->border_spacing);
        }
        else if (!strcmp(P, "inset")) memcpy(st->inset, src->inset, sizeof st->inset);
        else if (!strncmp(P, "margin-", 7) || !strncmp(P, "padding-", 8) || !strncmp(P, "inset-", 6) ||
                 !strcmp(P, "top") || !strcmp(P, "right") || !strcmp(P, "bottom") || !strcmp(P, "left")) {
            int group, side; bool block;
            if (logical_edge_pair(P, &group, &block)) {
                Length *dst = style_edge_values(st, group);
                const Length *v = style_edge_values_const(src, group);
                for (int end = 0; end < 2; end++)
                    dst[logical_edge_side(st, block, end)] = v[logical_edge_side(inherit ? src : st, block, end)];
                if (inherit) st->parent_sensitive = true;
            } else if (physical_edge_prop(P, &group, &side)) {
                const char *from = inherit ? logical_box_prop(prop, src) : P; int from_group, from_side;
                if (physical_edge_prop(from, &from_group, &from_side))
                    style_edge_values(st, group)[side] = style_edge_values_const(src, from_group)[from_side];
                if (inherit) st->parent_sensitive = true;
            }
        }
        else if (physical_border_prop(P, &border_kind, &border_side)) {
            int from_kind, from_side;
            const char *from = inherit ? logical_box_prop(prop, src) : P;
            if (physical_border_prop(from, &from_kind, &from_side)) {
                if (border_kind == 0) st->border_width[border_side] = src->border_width[from_side];
                else if (border_kind == 1) st->border_style[border_side] = src->border_style[from_side];
                else st->border_color[border_side] = inherit ? src->border_color[from_side] : st->color;
                if (inherit) st->parent_sensitive = true;
            }
        }
        else if (!strcmp(P, "border") || !strcmp(P, "border-width") ||
                 !strcmp(P, "border-style") || !strcmp(P, "border-color")) {
            bool all = !strcmp(P, "border");
            if (all || !strcmp(P, "border-width"))
                memcpy(st->border_width, src->border_width, sizeof st->border_width);
            if (all || !strcmp(P, "border-style"))
                memcpy(st->border_style, src->border_style, sizeof st->border_style);
            if (all || !strcmp(P, "border-color"))
                for (int i = 0; i < 4; i++)
                    st->border_color[i] = inherit ? src->border_color[i] : st->color;
            if (inherit) st->parent_sensitive = true;
        }
        else if (flex_value) {
            if (!strcmp(P, "flex") || !strcmp(P, "flex-grow")) st->flex_grow = src->flex_grow;
            if (!strcmp(P, "flex") || !strcmp(P, "flex-shrink")) st->flex_shrink = src->flex_shrink;
            if (!strcmp(P, "flex") || !strcmp(P, "flex-basis")) st->flex_basis = src->flex_basis;
            if (inherit) st->parent_sensitive = true;
        }
        else if (!strcmp(P, "white-space")) st->white_space = src->white_space;
        else if (!strcmp(P, "visibility")) st->visibility = src->visibility;
        else if (!strcmp(P, "position")) st->position = src->position;
        else if (!strcmp(P, "cursor")) st->cursor = src->cursor;
        else if (!strcmp(P, "fill")) st->fill = src->fill;
        else if ((k = border_corner(P)) >= 0) {   /* Google's search box: a white inner box with border-radius: inherit */
            if (k == 4) memcpy(st->border_radius, src->border_radius, sizeof st->border_radius);
            else st->border_radius[k] = src->border_radius[k];
        }
        else if (!strcmp(P, "all")) {
            /* Extend all only to this newly implemented non-inherited family.
               Its resolved CSS-wide grammar is checked above; arbitrary all
               values must never become intrinsic lengths. */
            css_apply_decl(st, par, "contain-intrinsic-size", val, e, el);
        }
        else if (!inherit && !(unset && gp && gp->inherited) && gp && gp->native && gp->initial && *gp->initial) {
            /* other natively modelled properties: apply the textual initial value */
            free(vbuf); vbuf = xstrdup(gp->initial); val = vbuf; inherit = initial = unset = false;
            goto apply;
        }
        goto out;
    }
apply:
    if (!strcmp(P, "mask-image") || !strcmp(P, "-webkit-mask-image") || !strcmp(P, "mask") || !strcmp(P, "-webkit-mask")) {
        free(st->mask_image); st->mask_image = NULL;
        char *u = strstr(val, "url(") ? extract_url(val) : NULL;
        if (u) { const char *base = c.e && c.e->doc ? (c.e->doc->base_url ? c.e->doc->base_url : c.e->doc->url) : NULL; st->mask_image = style_url_join(base, u); free(u); }
        if (P[strlen(P) - 1] == 'k') st->mask_fit = strstr(val, "contain") ? 1 : strstr(val, "cover") ? 2 : 0;
        goto out;
    }
    if (!strcmp(P, "mask-size") || !strcmp(P, "-webkit-mask-size")) { st->mask_fit = str_ieq(val, "contain") ? 1 : str_ieq(val, "cover") ? 2 : 0; goto out; }
    if (!strcmp(P, "container-name") || !strcmp(P, "container")) {
        char *sl = strchr(val, '/'); size_t nl = sl ? (size_t)(sl - val) : strlen(val);
        while (nl && isspace((unsigned char)val[nl - 1])) nl--;
        st->container_name = nl && !str_ieqn(val, "none", 4) ? atomn(val, nl) : NULL;
        if (P[9] == 0) { const char *ty = sl ? sl + 1 : "normal"; while (isspace((unsigned char)*ty)) ty++; st->container_type = str_istarts(ty, "size") ? 2 : str_istarts(ty, "inline-size") ? 1 : 0; }
        goto out;
    }
    if (!strcmp(P, "container-type")) { st->container_type = str_ieq(val, "size") ? 2 : str_ieq(val, "inline-size") ? 1 : 0; goto out; }
    switch (P[0]) {
    case 'a':
        if (!strcmp(P, "align-items")) st->align_items = parse_align(val);
        else if (!strcmp(P, "align-self")) st->align_self = parse_align(val);
        else if (!strcmp(P, "align-content")) st->align_content = parse_align(val);
        else if (!strcmp(P, "aspect-ratio")) { float a = 0, b = 1; const char *q = val; if (str_istarts(q, "auto")) q += 4; while (is_ws((unsigned char)*q)) q++; if (sscanf(q, "%f / %f", &a, &b) >= 1 && b > 0) st->aspect_ratio = a / b; else st->aspect_ratio = 0; }
        else if (!strcmp(P, "appearance") || !strcmp(P, "-webkit-appearance")) st->appearance = str_ieq(val, "none") ? 1 : 0;
        break;
    case 'b':
        if (!strcmp(P, "background-color")) { CssColor cc = cur_color(st); const char *ser; if (parse_color_ser(val, &col, &ser, &cc)) { st->bg_color = col; st->bg_ser = ser; } }
        else if (!strcmp(P, "background")) apply_background(&c, val);
        else if (!strcmp(P, "background-image")) apply_background_image(&c, val);
        else if (!strcmp(P, "background-repeat")) apply_bg_repeat(&c, val);
        else if (!strcmp(P, "background-origin")) apply_bg_box(&c, val, false);
        else if (!strcmp(P, "background-clip")) apply_bg_box(&c, val, true);
        else if (!strcmp(P, "background-size")) apply_bg_size(&c, val);
        else if (!strcmp(P, "background-position")) apply_bg_pos(&c, val);
        else if (!strcmp(P, "box-sizing")) st->box_sizing = str_ieq(val, "border-box") ? BOX_BORDER : BOX_CONTENT;
        else if (!strcmp(P, "box-shadow")) parse_shadow(&c, val, &st->box_shadow, &st->has_shadow);
        else if (!strcmp(P, "bottom")) alen(&c, val, &st->inset[2]);
        else if (!strcmp(P, "border")) { for (int i = 0; i < 4; i++) apply_border_side(&c, i, val); }
        else if (!strcmp(P, "border-top")) apply_border_side(&c, 0, val);
        else if (!strcmp(P, "border-right")) apply_border_side(&c, 1, val);
        else if (!strcmp(P, "border-bottom")) apply_border_side(&c, 2, val);
        else if (!strcmp(P, "border-left")) apply_border_side(&c, 3, val);
        else if (!strcmp(P, "border-inline")) { apply_border_side(&c, 1, val); apply_border_side(&c, 3, val); }
        else if (!strcmp(P, "border-block")) { apply_border_side(&c, 0, val); apply_border_side(&c, 2, val); }
        else if (!strcmp(P, "border-inline-start")) apply_border_side(&c, 3, val);
        else if (!strcmp(P, "border-inline-end")) apply_border_side(&c, 1, val);
        else if (!strcmp(P, "border-block-start")) apply_border_side(&c, 0, val);
        else if (!strcmp(P, "border-block-end")) apply_border_side(&c, 2, val);
        else if (!strcmp(P, "border-width")) { char *t[4]; int n = split_ws(val, t, 4); float w[4]; for (int i = 0; i < n; i++) w[i] = LMAX(0, border_w(&c, t[i])); if (n == 1) w[1] = w[2] = w[3] = w[0]; else if (n == 2) { w[2] = w[0]; w[3] = w[1]; } else if (n == 3) w[3] = w[1]; if (n) memcpy(st->border_width, w, sizeof w); free_toks(t, n); }
        else if (!strcmp(P, "border-style")) { char *t[4]; int n = split_ws(val, t, 4); int s[4]; for (int i = 0; i < n; i++) { s[i] = kw(t[i], bstyles); if (s[i] < 0) s[i] = 0; } if (n == 1) s[1] = s[2] = s[3] = s[0]; else if (n == 2) { s[2] = s[0]; s[3] = s[1]; } else if (n == 3) s[3] = s[1]; for (int i = 0; i < 4 && n; i++) st->border_style[i] = (uint8_t)s[i]; free_toks(t, n); }
        else if (!strcmp(P, "border-color")) { char *t[4]; int n = split_ws(val, t, 4); Color cc[4] = {0}; for (int i = 0; i < n; i++) css_parse_color(t[i], &cc[i], st->color); if (n == 1) cc[1] = cc[2] = cc[3] = cc[0]; else if (n == 2) { cc[2] = cc[0]; cc[3] = cc[1]; } else if (n == 3) cc[3] = cc[1]; if (n) memcpy(st->border_color, cc, sizeof cc); free_toks(t, n); }
        else if (!strcmp(P, "border-radius")) { char *sl = strchr(val, '/'); if (sl) *sl = 0; set4(st->border_radius, &c, val); }
        else if (!strcmp(P, "border-spacing")) {
            char *t[3]; int n = split_ws(val, t, 3); float gaps[2] = {0};
            bool ok = n == 1 || n == 2;
            for (int i = 0; i < n && ok; i++) {
                Length length = L_px(0); CssNumber number;
                bool math = strchr(t[i], '(') != NULL;
                ok = css_number_parse(t[i], c.em, c.rem, c.mc, &number);
                bool length_type = ok && number.power[CN_LENGTH] == 1;
                if (ok) for (int j = 1; j < CN_DIMS; j++) if (number.power[j]) length_type = false;
                /* Only a literal zero may omit its length unit. Percentages and
                   incompatible dimensions are invalid before range clamping. */
                ok = ok && (length_type || (!math && css_number_scalar(&number) && number.value == 0));
                ok = ok && alen_size(&c, t[i], &length) &&
                     length.kind == LK_LEN && !length.pctu && !len_has_pct(length);
                if (ok) { gaps[i] = LMAX(0, len_resolve(length, 0)); ok = isfinite(gaps[i]); }
            }
            if (ok) { if (n == 1) gaps[1] = gaps[0]; memcpy(st->border_spacing, gaps, sizeof gaps); }
            free_toks(t, n);
        }
        else if (!strncmp(P, "border-", 7)) {
            static const char *const sides[] = { "top", "right", "bottom", "left" };
            static const char *const corners[] = { "top-left", "top-right", "bottom-right", "bottom-left" };
            for (int i = 0; i < 4; i++) {
                size_t sl = strlen(sides[i]);
                if (!strncmp(P + 7, sides[i], sl) && P[7 + sl] == '-') {
                    const char *rest = P + 8 + sl;
                    if (!strcmp(rest, "width")) { float w = border_w(&c, val); if (w >= 0) st->border_width[i] = w; }
                    else if (!strcmp(rest, "style")) { k = kw(val, bstyles); if (k >= 0) st->border_style[i] = (uint8_t)k; }
                    else if (!strcmp(rest, "color")) { if (css_parse_color(val, &col, st->color)) st->border_color[i] = col; }
                }
                size_t cl = strlen(corners[i]);
                if (!strncmp(P + 7, corners[i], cl) && !strcmp(P + 7 + cl, "-radius")) { char *t[2]; int n = split_ws(val, t, 2); if (n) alen(&c, t[0], &st->border_radius[i]); free_toks(t, n); }
            }
            if (!strcmp(P, "border-collapse")) st->border_collapse = str_ieq(val, "collapse");
            if (!strcmp(P, "border-start-start-radius")) alen(&c, val, &st->border_radius[0]);
            if (!strcmp(P, "border-start-end-radius")) alen(&c, val, &st->border_radius[1]);
            if (!strcmp(P, "border-end-end-radius")) alen(&c, val, &st->border_radius[2]);
            if (!strcmp(P, "border-end-start-radius")) alen(&c, val, &st->border_radius[3]);
        }
        else if (!strcmp(P, "backdrop-filter") || !strcmp(P, "-webkit-backdrop-filter")) { const char *b = strstr(val, "blur("); st->backdrop_blur = b ? alen_px(&c, b + 5, 0) : 0; }
        break;
    case 'c':
        if (!strcmp(P, "color")) { CssColor cc = cur_color(par ? par : st); const char *ser; if (parse_color_ser(val, &col, &ser, &cc)) { st->color = col; st->color_ser = ser; } }
        else if (!strcmp(P, "clear")) { static const char *const cl[] = { "none", "left", "right", "both", NULL }; k = kw(val, cl); if (k >= 0) st->clear = (uint8_t)k; else if (str_ieq(val, "inline-start")) st->clear = CLEAR_LEFT; }
        else if (!strcmp(P, "cursor")) { st->cursor = str_ieq(val, "pointer") ? CUR_POINTER : str_ieq(val, "text") ? CUR_TEXT : str_ieq(val, "move") ? CUR_MOVE : str_ieq(val, "not-allowed") ? CUR_NOT_ALLOWED : str_istarts(val, "grab") ? CUR_GRAB : CUR_DEFAULT; }
        else if (!strcmp(P, "content")) { free(st->content); st->content = NULL; if (!str_ieq(val, "none") && !str_ieq(val, "normal")) { SB b; sb_init(&b); const char *q = val; while (*q) { if (*q == '"' || *q == '\'') { char qc = *q++; while (*q && *q != qc) { if (*q == '\\' && q[1]) { q++; if (isxdigit((unsigned char)*q)) { uint32_t cp = (uint32_t)strtoul(q, (char **)&q, 16); sb_utf8(&b, cp); if (*q == ' ') q++; continue; } } sb_putc(&b, *q++); } if (*q) q++; } else if (str_istarts(q, "attr(")) { const char *e2 = strchr(q, ')'); char an[64]; snprintf(an, sizeof an, "%.*s", (int)(e2 ? e2 - q - 5 : 0), q + 5); const char *attribute = str_trim(an); st->attribute_dependencies |= custom_bit(atom_lower(attribute, strlen(attribute))); const char *av = el ? node_attr(el, attribute) : NULL; if (av) sb_puts(&b, av); q = e2 ? e2 + 1 : q + strlen(q); } else if (str_istarts(q, "open-quote")) { sb_puts(&b, "\xe2\x80\x9c"); q += 10; } else if (str_istarts(q, "close-quote")) { sb_puts(&b, "\xe2\x80\x9d"); q += 11; } else q++; } st->content = sb_take(&b); } }
        else if (!strcmp(P, "column-gap")) { if (str_ieq(val, "normal")) { st->column_gap = 0; st->column_gap_l = L_auto(); } else if (!angle_or_time(val) && alen(&c, val, &l)) { st->column_gap = l.px; st->column_gap_l = l; } }
        else if (!strcmp(P, "caret-color")) { css_parse_color(val, &st->caret_color, st->color); }
        break;
    case 'd':
        if (!strcmp(P, "display")) {
            static const char *const ds[] = { "none", "inline", "block", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "list-item", "table", "inline-table", "table-row", "table-cell", "table-row-group", "table-header-group", "table-footer-group", "table-column", "table-column-group", "table-caption", "contents", "flow-root", NULL };
            k = kw(val, ds); const char *dt = NULL;
            st->display_text = NULL;
            if (k >= 0) st->display = (uint8_t)k;
            else if ((dt = display_canon(val))) {
                st->display_text = dt;
                k = kw(dt, ds);
                st->display = k >= 0 ? (uint8_t)k : strstr(dt, "list-item") ? D_LIST_ITEM : str_starts(dt, "block") || str_starts(dt, "run-in") ? D_BLOCK : D_INLINE;
                if (k >= 0) st->display_text = NULL;
            }
            else if (str_ieq(val, "-webkit-box") || str_ieq(val, "-webkit-flex") || str_ieq(val, "-ms-flexbox")) st->display = D_FLEX;
            else if (str_ieq(val, "-webkit-inline-box") || str_ieq(val, "-webkit-inline-flex")) st->display = D_INLINE_FLEX;
            else if (str_ieq(val, "block flow") || str_ieq(val, "block flow-root")) st->display = D_BLOCK;
            else if (str_ieq(val, "inline flow-root")) st->display = D_INLINE_BLOCK;
            else if (str_ieq(val, "inline flex")) st->display = D_INLINE_FLEX;
            else if (str_ieq(val, "block flex")) st->display = D_FLEX;
            else if (str_ieq(val, "ruby") || str_ieq(val, "ruby-text")) st->display = D_INLINE;
        }
        else if (!strcmp(P, "direction")) st->direction = str_ieq(val, "rtl");
        break;
    case 'f':
        if (!strcmp(P, "font-size")) {
            float pf = par ? par->font_size : 16; MathType font_type;
            static const char *const sz[] = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large", NULL };
            static const float szv[] = { 9, 10, 13, 16, 18, 24, 32, 48 };
            k = kw(val, sz);
            if (k >= 0) st->font_size = szv[k];
            else if (str_ieq(val, "smaller")) st->font_size = pf / 1.2f;
            else if (str_ieq(val, "larger")) st->font_size = pf * 1.2f;
            else if (css_parse_length_context(val, &l, pf, c.rem, c.mc, par, true,
                     e ? &e->uses_font_units : NULL, &font_type) && l.kind == LK_LEN &&
                     (!strchr(val, '(') || !math_type_equal(font_type, math_type_unit(MATH_NUMBER))))
                st->font_size = LMAX(0, len_resolve(l, pf));
            c.em = st->font_size;
        }
        else if (!strcmp(P, "font-weight")) { if (str_ieq(val, "bold")) st->font_weight = 700; else if (str_ieq(val, "normal")) st->font_weight = 400; else if (str_ieq(val, "bolder")) st->font_weight = (int16_t)(par && par->font_weight >= 600 ? 900 : 700); else if (str_ieq(val, "lighter")) st->font_weight = 300; else { int w = atoi(val); if (w > 0) st->font_weight = (int16_t)w; } }
        else if (!strcmp(P, "font-family")) st->font_family = atom(val);
        else if (!strcmp(P, "font-style")) st->font_style = str_ieq(val, "italic") || str_istarts(val, "oblique");
        else if (!strcmp(P, "font")) apply_font(&c, val);
        else if (!strcmp(P, "float")) { st->float_ = str_ieq(val, "left") || str_ieq(val, "inline-start") ? F_LEFT : str_ieq(val, "right") || str_ieq(val, "inline-end") ? F_RIGHT : F_NONE; }
        else if (!strcmp(P, "flex")) (void)flex_parse(&c, val, &st->flex_grow, &st->flex_shrink, &st->flex_basis);
        else if (!strcmp(P, "flex-grow")) (void)flex_factor(&c, val, &st->flex_grow);
        else if (!strcmp(P, "flex-shrink")) (void)flex_factor(&c, val, &st->flex_shrink);
        else if (!strcmp(P, "flex-basis")) (void)flex_basis(&c, val, &st->flex_basis);
        else if (!strcmp(P, "flex-direction")) { static const char *const fd[] = { "row", "row-reverse", "column", "column-reverse", NULL }; k = kw(val, fd); if (k >= 0) st->flex_direction = (uint8_t)k; }
        else if (!strcmp(P, "flex-wrap")) st->flex_wrap = str_ieq(val, "wrap") ? 1 : str_ieq(val, "wrap-reverse") ? 2 : 0;
        else if (!strcmp(P, "flex-flow")) { char *t[2]; int n = split_ws(val, t, 2); for (int i = 0; i < n; i++) { if (strstr(t[i], "wrap")) css_apply_decl(st, par, "flex-wrap", t[i], e, el); else css_apply_decl(st, par, "flex-direction", t[i], e, el); } free_toks(t, n); }
        else if (!strcmp(P, "fill")) { if (css_parse_color(val, &col, st->color)) st->fill = col; else if (str_ieq(val, "none")) st->fill = 0; }
        else if (!strcmp(P, "filter")) { const char *b = strstr(val, "blur("); st->filter_blur = b ? alen_px(&c, b + 5, 0) : 0; const char *br = strstr(val, "brightness("); st->filter_brightness = br ? strtof(br + 11, NULL) : 1; }
        break;
    case 'g':
        if (!strcmp(P, "gap") || !strcmp(P, "grid-gap")) { char *t[2]; int n = split_ws(val, t, 2); bool ok = n && !angle_or_time(t[0]) && !(n > 1 && angle_or_time(t[1])); if (ok && alen(&c, t[0], &l)) { st->row_gap = l.px; st->row_gap_l = l; st->column_gap = l.px; st->column_gap_l = l; } if (ok && n > 1 && alen(&c, t[1], &l)) { st->column_gap = l.px; st->column_gap_l = l; } free_toks(t, n); }
        else if (!strcmp(P, "grid-template-columns")) { free(st->grid_cols); st->grid_cols = prepared_tracks; st->grid_ncols = prepared_count; prepared_tracks = NULL; }
        else if (!strcmp(P, "grid-template-rows")) { free(st->grid_rows); st->grid_rows = prepared_tracks; st->grid_nrows = prepared_count; prepared_tracks = NULL; }
        else if (!strcmp(P, "grid") && !strstr(val, "auto-flow")) css_apply_decl(st, par, "grid-template", val, e, el);
        else if (!strcmp(P, "grid-column-end")) st->grid_column.end = parse_grid_component(val);
        else if (!strcmp(P, "grid-row-end")) st->grid_row.end = parse_grid_component(val);
        else if (!strcmp(P, "grid-area") && strchr(val, '/')) {
            char *s = xstrdup(val), *part[4] = {0}; int np = 0;
            for (char *q = s; q && np < 4; ) { part[np++] = q; q = strchr(q, '/'); if (q) *q++ = 0; }
            char buf[160];
            snprintf(buf, sizeof buf, "%s%s%s", str_trim(part[0]), np > 2 ? " / " : "", np > 2 ? str_trim(part[2]) : "");
            parse_grid_line(buf, &st->grid_row);
            snprintf(buf, sizeof buf, "%s%s%s", np > 1 ? str_trim(part[1]) : "auto", np > 3 ? " / " : "", np > 3 ? str_trim(part[3]) : "");
            parse_grid_line(buf, &st->grid_column);
            free(st->grid_area); st->grid_area = NULL; free(s);
        }
        else if (!strcmp(P, "grid-template")) { if (strchr(val, '"') || strchr(val, '\'')) { free(st->grid_areas); st->grid_areas = parse_grid_areas(val); } char *sl = strchr(val, '/'); if (sl) { *sl = 0; css_apply_decl(st, par, "grid-template-rows", val, e, el); css_apply_decl(st, par, "grid-template-columns", sl + 1, e, el); } }
        else if (!strcmp(P, "grid-column")) parse_grid_line(val, &st->grid_column);
        else if (!strcmp(P, "grid-row")) parse_grid_line(val, &st->grid_row);
        else if (!strcmp(P, "grid-column-start")) st->grid_column.start = parse_grid_component(val);
        else if (!strcmp(P, "grid-row-start")) st->grid_row.start = parse_grid_component(val);
        else if (!strcmp(P, "grid-auto-rows")) alen(&c, val, &st->grid_auto_rows);
        else if (!strcmp(P, "grid-area")) {
            free(st->grid_area); st->grid_area = NULL;
            st->grid_column = (GridAxis){0}; st->grid_row = (GridAxis){0};
            st->grid_row.start = parse_grid_component(val);
            if (!st->grid_row.start.number && !st->grid_row.start.span && !str_ieq(val, "auto") &&
                (isalpha((unsigned char)*val) || *val == '_' || *val == '-')) {
                size_t k = strcspn(val, " /"); st->grid_area = xstrndup(val, k);
            }
        }
        else if (!strcmp(P, "grid-template-areas")) { free(st->grid_areas); st->grid_areas = parse_grid_areas(val); }
        break;
    case 'h':
        if (!strcmp(P, "height")) alen_size(&c, val, &st->height);
        break;
    case 'i':
        if (!strcmp(P, "inset")) set4(st->inset, &c, val);
        else if (!strcmp(P, "inset-inline-start")) alen(&c, val, &st->inset[3]);
        else if (!strcmp(P, "inset-inline-end")) alen(&c, val, &st->inset[1]);
        else if (!strcmp(P, "inset-block-start")) alen(&c, val, &st->inset[0]);
        else if (!strcmp(P, "inset-block-end")) alen(&c, val, &st->inset[2]);
        else if (!strcmp(P, "inset-inline")) apply_logical_edge_pair(&c, val, 2, false);
        else if (!strcmp(P, "inset-block")) apply_logical_edge_pair(&c, val, 2, true);
        else if (!strcmp(P, "isolation")) st->isolation = str_ieq(val, "isolate");
        break;
    case 'j':
        if (!strcmp(P, "justify-content")) st->justify_content = parse_align(val);
        else if (!strcmp(P, "justify-items")) st->justify_items = parse_align(val);
        else if (!strcmp(P, "justify-self")) st->justify_self = parse_align(val);
        break;
    case 'l':
        if (!strcmp(P, "left")) alen(&c, val, &st->inset[3]);
        else if (!strcmp(P, "line-height")) {
            char *end; float x = strtof(val, &end);
            if (str_ieq(val, "normal")) { st->line_height_normal = true; st->line_height_factor = 0; }
            else if (end != val && !*end) { st->line_height_normal = false; st->line_height_factor = x; st->line_height = x * st->font_size; }
            else if (css_scalar_math(val, MATH_NUMBER, c.em, c.rem, &x)) {
                if (isnan(x) || x < 0) x = 0;
                st->line_height_normal = false; st->line_height_factor = x; st->line_height = x * st->font_size;
            }
            else if (alen(&c, val, &l) && l.kind == LK_LEN) { st->line_height_normal = false; st->line_height_factor = 0; st->line_height = len_resolve(l, st->font_size); }
        }
        else if (!strcmp(P, "letter-spacing")) st->letter_spacing = str_ieq(val, "normal") ? 0 : alen_px(&c, val, 0);
        else if (!strcmp(P, "list-style-type") || !strcmp(P, "list-style")) {
            static const char *const ls[] = { "none", "disc", "circle", "square", "decimal", "lower-alpha", "upper-alpha", "lower-roman", "upper-roman", NULL };
            char *t[3]; int n = split_ws(val, t, 3); for (int i = 0; i < n; i++) { k = kw(t[i], ls); if (k >= 0) st->list_style = (uint8_t)k; else if (str_ieq(t[i], "lower-latin")) st->list_style = LST_LOWER_ALPHA; else if (str_ieq(t[i], "upper-latin")) st->list_style = LST_UPPER_ALPHA; } free_toks(t, n);
        }
        else if (!strcmp(P, "line-clamp") || !strcmp(P, "-webkit-line-clamp")) st->line_clamp = str_ieq(val, "none") ? 0 : strtof(val, NULL);
        break;
    case 'm':
        if (!strcmp(P, "margin")) set4(st->margin, &c, val);
        else if (!strcmp(P, "margin-top")) alen(&c, val, &st->margin[0]);
        else if (!strcmp(P, "margin-right") || !strcmp(P, "margin-inline-end")) alen(&c, val, &st->margin[1]);
        else if (!strcmp(P, "margin-bottom")) alen(&c, val, &st->margin[2]);
        else if (!strcmp(P, "margin-left") || !strcmp(P, "margin-inline-start")) alen(&c, val, &st->margin[3]);
        else if (!strcmp(P, "margin-block-start")) alen(&c, val, &st->margin[0]);
        else if (!strcmp(P, "margin-block-end")) alen(&c, val, &st->margin[2]);
        else if (!strcmp(P, "margin-inline")) apply_logical_edge_pair(&c, val, 0, false);
        else if (!strcmp(P, "margin-block")) apply_logical_edge_pair(&c, val, 0, true);
        else if (!strcmp(P, "min-width")) { alen_size(&c, val, &st->min_width); }
        else if (!strcmp(P, "min-height")) alen_size(&c, val, &st->min_height);
        else if (!strcmp(P, "max-width")) alen_size(&c, val, &st->max_width);
        else if (!strcmp(P, "max-height")) alen_size(&c, val, &st->max_height);
        break;
    case 'o':
        if (!strcmp(P, "opacity")) {
            float x;
            if (!application_scalar_math(&c, val, MATH_NUMBER, &x)) {
                char *end; x = strtof(val, &end);
                if (end == val || *end != '%' || end[1]) goto out;
                x /= 100;
            }
            st->opacity = isnan(x) ? 0 : LCLAMP(x, 0, 1);
        }
        else if (!strcmp(P, "overflow") || !strcmp(P, "overflow-x") || !strcmp(P, "overflow-y")) {
            static const char *const ov[] = { "visible", "hidden", "scroll", "auto", "clip", NULL };
            char *t[2]; int n = split_ws(val, t, 2);
            int a = n ? kw(t[0], ov) : -1, b = n > 1 ? kw(t[1], ov) : a;
            if (n && str_ieq(t[0], "overlay")) a = b = OV_AUTO;
            char axis = P[8] == '-' ? P[9] : 0;
            if (a >= 0) { if (axis != 'y') st->overflow_x = (uint8_t)a; if (axis != 'x') st->overflow_y = (uint8_t)(axis == 'y' ? a : b >= 0 ? b : a); }
            free_toks(t, n);
        }
        else if (!strcmp(P, "order")) { int value; if (application_integer(&c, val, INT_MIN, &value)) st->order = value; }
        else if (!strcmp(P, "object-fit")) { static const char *const of[] = { "fill", "contain", "cover", "none", "scale-down", NULL }; k = kw(val, of); if (k >= 0) st->object_fit = (uint8_t)k; }
        else if (!strcmp(P, "outline")) { char *t[3]; int n = split_ws(val, t, 3); st->outline_style = 0; st->outline_width = 3; for (int i = 0; i < n; i++) { int s = kw(t[i], bstyles); float w; if (s >= 0) st->outline_style = (uint8_t)s; else if (str_ieq(t[i], "auto")) st->outline_style = BS_SOLID; else if ((w = border_w(&c, t[i])) >= 0) st->outline_width = w; else css_parse_color(t[i], &st->outline_color, st->color); } free_toks(t, n); }
        else if (!strcmp(P, "outline-width")) st->outline_width = LMAX(0, border_w(&c, val));
        else if (!strcmp(P, "outline-style")) { k = kw(val, bstyles); st->outline_style = (uint8_t)(k >= 0 ? k : str_ieq(val, "auto") ? BS_SOLID : 0); }
        else if (!strcmp(P, "outline-color")) css_parse_color(val, &st->outline_color, st->color);
        else if (!strcmp(P, "outline-offset")) st->outline_offset = alen_px(&c, val, 0);
        else if (!strcmp(P, "overflow-wrap") || !strcmp(P, "word-wrap")) st->overflow_wrap = !str_ieq(val, "normal");
        break;
    case 'p':
        if (!strcmp(P, "padding")) set4(st->padding, &c, val);
        else if (!strcmp(P, "padding-top") || !strcmp(P, "padding-block-start")) alen(&c, val, &st->padding[0]);
        else if (!strcmp(P, "padding-right") || !strcmp(P, "padding-inline-end")) alen(&c, val, &st->padding[1]);
        else if (!strcmp(P, "padding-bottom") || !strcmp(P, "padding-block-end")) alen(&c, val, &st->padding[2]);
        else if (!strcmp(P, "padding-left") || !strcmp(P, "padding-inline-start")) alen(&c, val, &st->padding[3]);
        else if (!strcmp(P, "padding-inline")) apply_logical_edge_pair(&c, val, 1, false);
        else if (!strcmp(P, "padding-block")) apply_logical_edge_pair(&c, val, 1, true);
        else if (!strcmp(P, "position")) { static const char *const ps[] = { "static", "relative", "absolute", "fixed", "sticky", NULL }; k = kw(val, ps); if (k >= 0) st->position = (uint8_t)k; else if (str_ieq(val, "-webkit-sticky")) st->position = P_STICKY; }
        else if (!strcmp(P, "pointer-events")) st->pointer_events = !str_ieq(val, "none");
        else if (!strcmp(P, "place-items")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { st->align_items = parse_align(t[0]); st->justify_items = parse_align(t[n - 1]); gen_put(&st->gen_n, "align-items", xstrdup(t[0])); } free_toks(t, n); }
        else if (!strcmp(P, "place-content")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { st->align_content = parse_align(t[0]); st->justify_content = parse_align(t[n - 1]); gen_put(&st->gen_n, "justify-content", xstrdup(t[n - 1])); } free_toks(t, n); }
        else if (!strcmp(P, "place-self")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { st->align_self = parse_align(t[0]); st->justify_self = parse_align(t[n - 1]); gen_put(&st->gen_n, "align-self", xstrdup(t[0])); } free_toks(t, n); }
        break;
    case 'r':
        if (!strcmp(P, "right")) alen(&c, val, &st->inset[1]);
        else if (!strcmp(P, "row-gap")) { if (str_ieq(val, "normal")) { st->row_gap = 0; memset(&st->row_gap_l, 0, sizeof st->row_gap_l); } else if (!angle_or_time(val) && alen(&c, val, &l)) { st->row_gap = l.px; st->row_gap_l = l; } }
        else if (!strcmp(P, "rotate")) { char buf[64]; snprintf(buf, sizeof buf, "rotate(%s)", val); if (!str_ieq(val, "none")) parse_transform(&c, buf); }
        break;
    case 's':
        if (!strcmp(P, "stroke")) { if (css_parse_color(val, &col, st->color)) st->stroke = col; else st->stroke = 0; }
        else if (!strcmp(P, "stroke-width")) st->stroke_width = alen_px(&c, val, 1);
        else if (!strcmp(P, "scale")) { char buf[64]; snprintf(buf, sizeof buf, "scale(%s)", val); if (!str_ieq(val, "none")) parse_transform(&c, buf); }
        break;
    case 't':
        if (!strcmp(P, "top")) alen(&c, val, &st->inset[0]);
        else if (!strcmp(P, "text-align")) { static const char *const ta[] = { "start", "left", "right", "center", "justify", "end", NULL }; k = kw(val, ta); if (k >= 0) st->text_align = (uint8_t)k; else if (strstr(val, "center")) st->text_align = TA_CENTER; else if (strstr(val, "right")) st->text_align = TA_RIGHT; else if (strstr(val, "left")) st->text_align = TA_LEFT; }
        else if (!strcmp(P, "text-decoration") || !strcmp(P, "text-decoration-line")) { st->text_decoration = 0; if (strstr(val, "underline")) st->text_decoration |= TD_UNDERLINE; if (strstr(val, "overline")) st->text_decoration |= TD_OVERLINE; if (strstr(val, "line-through")) st->text_decoration |= TD_LINE_THROUGH; }
        else if (!strcmp(P, "text-transform")) st->text_transform = str_ieq(val, "uppercase") ? TT_UPPER : str_ieq(val, "lowercase") ? TT_LOWER : str_ieq(val, "capitalize") ? TT_CAPITALIZE : TT_NONE;
        else if (!strcmp(P, "text-indent")) st->text_indent = alen_px(&c, val, 0);
        else if (!strcmp(P, "text-overflow")) st->text_overflow = str_ieq(val, "ellipsis") ? TO_ELLIPSIS : TO_CLIP;
        else if (!strcmp(P, "touch-action")) {   /* pinch-zoom alone allows no panning; manipulation pans both ways */
            bool x = str_ieq(val, "auto") || strstr(val, "manipulation") || strstr(val, "pan-x") || strstr(val, "pan-left") || strstr(val, "pan-right");
            bool y = str_ieq(val, "auto") || strstr(val, "manipulation") || strstr(val, "pan-y") || strstr(val, "pan-up") || strstr(val, "pan-down");
            st->touch_block = (x ? 0 : TOUCH_NO_X) | (y ? 0 : TOUCH_NO_Y);
        }
        else if (!strcmp(P, "text-shadow")) parse_shadow(&c, val, &st->text_shadow, &st->has_text_shadow);
        else if (!strcmp(P, "transform") || !strcmp(P, "-webkit-transform")) parse_transform(&c, val);
        else if (!strcmp(P, "translate")) { char buf[256]; snprintf(buf, sizeof buf, "translate(%s)", val); if (!str_ieq(val, "none")) parse_transform(&c, buf); }
        else if (!strcmp(P, "transform-origin")) { char *t[2]; int n = split_ws(val, t, 2); for (int i = 0; i < n; i++) { if (str_ieq(t[i], "left") || str_ieq(t[i], "top")) st->transform_origin[i] = (Length){0, 0, LK_LEN}; else if (str_ieq(t[i], "right") || str_ieq(t[i], "bottom")) st->transform_origin[i] = (Length){0, 100, LK_LEN}; else if (str_ieq(t[i], "center")) st->transform_origin[i] = (Length){0, 50, LK_LEN}; else alen(&c, t[i], &st->transform_origin[i]); } free_toks(t, n); }
        else if (!strcmp(P, "table-layout")) st->table_layout = str_ieq(val, "fixed");
        else if (!strcmp(P, "text-wrap") || !strcmp(P, "text-wrap-mode")) { if (str_ieq(val, "nowrap")) st->white_space = WS_NOWRAP; }
        break;
    case 'u':
        if (!strcmp(P, "user-select") || !strcmp(P, "-webkit-user-select")) st->user_select = str_ieq(val, "none");
        break;
    case 'v':
        if (!strcmp(P, "visibility")) st->visibility = str_ieq(val, "hidden") ? VIS_HIDDEN : str_ieq(val, "collapse") ? VIS_COLLAPSE : VIS_VISIBLE;
        else if (!strcmp(P, "vertical-align")) { static const char *const va[] = { "baseline", "top", "middle", "bottom", "text-top", "text-bottom", "sub", "super", NULL }; k = kw(val, va); if (k >= 0) st->vertical_align = (uint8_t)k; else if (alen(&c, val, &l)) { st->vertical_align = VA_LENGTH; st->vertical_align_len = len_resolve(l, st->line_height_normal ? st->font_size * 1.2f : st->line_height); } }
        break;
    case 'w':
        if (!strcmp(P, "width")) alen_size(&c, val, &st->width);
        else if (!strcmp(P, "white-space") || !strcmp(P, "white-space-collapse")) { static const char *const ws[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line", "break-spaces", NULL }; k = kw(val, ws); if (k >= 0) st->white_space = (uint8_t)k; else if (str_ieq(val, "preserve")) st->white_space = WS_PRE_WRAP; }
        else if (!strcmp(P, "word-break")) st->word_break = str_ieq(val, "break-all") ? 1 : str_ieq(val, "break-word") ? 2 : 0;
        else if (!strcmp(P, "word-spacing")) st->word_spacing = str_ieq(val, "normal") ? 0 : alen_px(&c, val, 0);
        else if (!strcmp(P, "writing-mode")) st->writing_mode = str_ieq(val, "vertical-rl") ? 2 : str_ieq(val, "vertical-lr") ? 1 : 0;   /* 2: vertical-rl; sideways layout remains unsupported */
        break;
    case 'z':
        if (!strcmp(P, "z-index")) { if (str_ieq(val, "auto")) st->z_auto = true; else { st->z_auto = false; st->z_index = atoi(val); if (isalpha((unsigned char)val[0])) { LCtx lc = { st->font_size, 16, NULL }; const char *q = val; Length L; if (parse_dim(&q, &L, &lc) && !L.pct && isfinite(L.px)) st->z_index = (int)lroundf(L.px); } } }
        break;
    }
out:
    free(prepared_tracks);
    if (!strcmp(P, "background") || !strcmp(P, "background-image") || !strcmp(P, "background-clip")) {
        if (inherit) st->parent_sensitive = true;
        bg_resolve_color_clip(st);
    }
    free(vbuf); free(subst);
}

/* ---------------- rule index ---------------- */
static void idx_add(HMap *m, const char *key, Rule *r) {
    RuleVec *v = hm_get(m, key);
    if (!v) { v = xcalloc(1, sizeof *v); hm_put(m, key, v); }
    vec_push(*v, r);
}
static void rv_free(void *p) { RuleVec *v = p; vec_free(*v); free(v); }
static void nofree(void *p) { (void)p; }
typedef struct HovDesc { VEC(const char *) keys; bool univ; } HovDesc;
typedef struct AttributeCandidate { Selector candidate; const Rule *source; const char *subject, *bearer; } AttributeCandidate;
typedef VEC(AttributeCandidate) AttributeCandidates;
static void attribute_candidates_free(void *p) {
    AttributeCandidates *v = p;
    for (int i = 0; i < v->n; i++) {
        Selector *s = &v->v[i].candidate;
        for (int j = 0; j < s->n; j++) free(s->c[j].s);
        free(s->c);
    }
    vec_free(*v); free(v);
}
static void hd_free(void *p) { HovDesc *h = p; if (!h) return; vec_free(h->keys); free(h); }
static const char *hov_skey; /* subject key of the rule being indexed; NULL = universal */
static void desc_add(HMap *m, void **u, const char *key, const char *skey) {
    HovDesc *h = key ? hm_get(m, key) : *u;
    if (!h) { h = xcalloc(1, sizeof *h); if (key) hm_put(m, key, h); else *u = h; }
    if (!skey) { h->univ = true; return; }
    for (int i = 0; i < h->keys.n; i++) if (!strcmp(h->keys.v[i], skey)) return;
    vec_push(h->keys, skey);
}
static void idx_clear(RuleIndex *ix) {
    for (int i = 0; i < ix->empty_candidates.n; i++) {
        Selector *s = &ix->empty_candidates.v[i].candidate;
        for (int j = 0; j < s->n; j++) free(s->c[j].s);
        free(s->c);
    }
    vec_free(ix->empty_candidates);
    for (int i = 0; i < ix->tree_candidates.n; i++) {
        Selector *s = &ix->tree_candidates.v[i].candidate;
        for (int j = 0; j < s->n; j++) free(s->c[j].s);
        free(s->c);
        s = &ix->tree_candidates.v[i].changed_candidate;
        for (int j = 0; j < s->n; j++) free(s->c[j].s);
        free(s->c);
    }
    vec_free(ix->tree_candidates);
    for (int i = 0; i < ix->tree_candidate_lists.n; i++) {
        SelList *list = ix->tree_candidate_lists.v[i];
        for (int j = 0; j < list->n; j++) {
            for (int k = 0; k < list->v[j].n; k++) free(list->v[j].c[k].s);
            free(list->v[j].c);
        }
        free(list->v); free(list);
    }
    vec_free(ix->tree_candidate_lists);
    hm_free(&ix->attribute_candidates, attribute_candidates_free);
    hm_free(&ix->attribute_subjects, hd_free); hm_free(&ix->self_attributes, NULL); hm_free(&ix->attribute_bearers, hd_free);
    hm_free(&ix->empty_ids, NULL); hm_free(&ix->empty_classes, NULL); hm_free(&ix->empty_tags, NULL); ix->empty_universal = false;
    for (int i = 0; i < 1024; i++) if (ix->ancestor_buckets[i]) { rv_free(ix->ancestor_buckets[i]); ix->ancestor_buckets[i] = NULL; }
    memset(ix->ancestor_bucket_bits, 0, sizeof ix->ancestor_bucket_bits); hm_free(&ix->by_id, rv_free); hm_free(&ix->by_class, rv_free); hm_free(&ix->by_tag, rv_free); hm_free(&ix->by_attr, rv_free); hm_free(&ix->by_parent_tag, rv_free); hm_free(&ix->by_parent_class, rv_free); hm_free(&ix->by_parent_attr, rv_free); vec_free(ix->root_rules); vec_free(ix->fw_rules); vec_free(ix->fw_keys); vec_free(ix->fw_far); vec_free(ix->fw_far_keys); ix->fw_far_unkeyed = 0; ix->fw_complex = false; hm_free(&ix->hov, nofree); hm_free(&ix->hov_anc, nofree); hm_free(&ix->hov_ach, nofree); hm_free(&ix->hov_desc, hd_free); hm_free(&ix->anc_desc, hd_free); hd_free(ix->hov_desc_u); hd_free(ix->anc_desc_u); ix->hov_desc_u = ix->anc_desc_u = NULL; vec_free(ix->universal); ix->count = 0; ix->hov_univ = 0; ix->hov_has = ix->hov_sib = ix->focus_within = ix->style_values = false;
    memset(ix->has_keys, 0, sizeof ix->has_keys); memset(ix->reselect_keys, 0, sizeof ix->reselect_keys);
    for (int i = 0; i < 1024; i++) vec_free(ix->reselect_subjects[i]);
    memset(ix->sib_keys, 0, sizeof ix->sib_keys); ix->sib_any = ix->sib_state = ix->sib_desc = ix->sib_style = false;
    memset(ix->sib_general_keys, 0, sizeof ix->sib_general_keys); ix->sib_general_state = ix->sib_general_style = false; }
static bool is_pseudo(const SimpleSel *s, const char *n) { return s->kind == SK_PSEUDO && s->name && !strcmp(s->name, n); }
static const char *attr_key(const char *name) { char b[128]; snprintf(b, sizeof b, "[%s", name); return atom(b); }
static const char *cmp_key(const Compound *c) {
    const char *id = NULL, *cls = NULL, *tag = NULL, *attr = NULL;
    for (int k = 0; k < c->n; k++) {
        const SimpleSel *s = &c->s[k];
        if (s->kind == SK_ID) id = s->name; else if (s->kind == SK_CLASS && !cls) cls = s->name; else if (s->kind == SK_TYPE) tag = s->value;
        else if (s->kind == SK_ATTR && !attr && s->name) attr = s->name;
    }
    if (!id && !cls && !tag)
        for (int k = 0; k < c->n; k++) {
            const SimpleSel *s = &c->s[k];
            if ((is_pseudo(s, "is") || is_pseudo(s, "where")) && s->sub && s->sub->n) {
                const char *k0 = NULL;
                for (int j = 0; j < s->sub->n; j++) {
                    const char *kj = s->sub->v[j].n ? cmp_key(&s->sub->v[j].c[s->sub->v[j].n - 1]) : NULL;
                    if (!kj || (k0 && strcmp(k0, kj))) { k0 = NULL; break; }
                    k0 = kj;
                }
                if (k0) return k0;
            }
        }
    return id ? id : cls ? cls : tag ? tag : attr ? attr_key(attr) : NULL;
}
static bool list_has_hover(const SelList *l) {
    for (int j = 0; j < l->n; j++)
        for (int i = 0; i < l->v[j].n; i++)
            for (int k = 0; k < l->v[j].c[i].n; k++) {
                const SimpleSel *s = &l->v[j].c[i].s[k];
                if (is_pseudo(s, "hover") || (s->sub && list_has_hover(s->sub))) return true;
            }
    return false;
}
static void anc_add(RuleIndex *ix, const char *key, int f) {
    if (!key) { ix->hov_has = true; return; }
    if (f & 6) desc_add(&ix->anc_desc, &ix->anc_desc_u, key, hov_skey);
    if (f & 48) ix->hov_sib = true;
    HMap *m = f & 64 ? &ix->hov_ach : &ix->hov_anc;
    hm_put(m, key, (void *)((uintptr_t)hm_get(m, key) | (uintptr_t)(f & ~64)));
}
static void hov_add(RuleIndex *ix, const char *key, int f) {
    if (f & 6) desc_add(&ix->hov_desc, &ix->hov_desc_u, key, hov_skey);
    if (!key) { ix->hov_univ |= (uint8_t)f; return; }
    hm_put(&ix->hov, key, (void *)((uintptr_t)hm_get(&ix->hov, key) | (uintptr_t)f));
}
/* add c's keys (its own, or every alternative of a keyed :is()/:where() list); false if c has none */
static bool add_keys(RuleIndex *ix, const Compound *c, int f, void (*add)(RuleIndex *, const char *, int)) {
    const char *k = cmp_key(c);
    if (k) { add(ix, k, f); return true; }
    for (int i = 0; i < c->n; i++) {
        const SimpleSel *s = &c->s[i];
        if (!(is_pseudo(s, "is") || is_pseudo(s, "where")) || !s->sub || !s->sub->n) continue;
        int j = 0;
        while (j < s->sub->n && s->sub->v[j].n && cmp_key(&s->sub->v[j].c[s->sub->v[j].n - 1])) j++;
        if (j < s->sub->n) continue;
        for (j = 0; j < s->sub->n; j++) add(ix, cmp_key(&s->sub->v[j].c[s->sub->v[j].n - 1]), f);
        return true;
    }
    return false;
}
/* okey: key of the compound a nested selector list belongs to, used when the nested subject has none */
static void hov_scan(RuleIndex *ix, const Selector *sel, int outer, const char *okey) {
    for (int i = 0; i < sel->n; i++) {
        const Compound *c = &sel->c[i];
        int f = outer | (i == sel->n - 1 ? (outer ? 0 : 1) : sel->c[i + 1].comb == CB_ADJ || sel->c[i + 1].comb == CB_SIB ? 4 : 2);
        const char *fk = cmp_key(c);
        if (!fk && i == sel->n - 1) fk = okey;
        for (int k = 0; k < c->n; k++) {
            const SimpleSel *s = &c->s[k];
            if (is_pseudo(s, "hover") && !add_keys(ix, c, f, hov_add)) hov_add(ix, fk, f);
            if (is_pseudo(s, "has") && s->sub) {
                if (!list_has_hover(s->sub)) continue;
                int g = f;
                for (int j = 0; j < s->sub->n; j++) if (s->sub->v[j].n) g |= s->sub->v[j].c[0].comb == CB_ADJ ? 16 : s->sub->v[j].c[0].comb == CB_SIB ? 32 : 0;
                const char *pk;
                if (add_keys(ix, c, g, anc_add)) continue;
                if (fk) anc_add(ix, fk, g);
                else if (i > 0 && c->comb == CB_CHILD && (pk = cmp_key(&sel->c[i - 1]))) anc_add(ix, pk, g | 64);
                else ix->hov_has = true;
            } else if (s->kind == SK_PSEUDO && s->sub) for (int j = 0; j < s->sub->n; j++) hov_scan(ix, &s->sub->v[j], f, fk);
        }
    }
}
static uintptr_t hov_lookup(HMap *m, Node *el) {
    uintptr_t f = 0;
    if (el->id) f |= (uintptr_t)hm_get(m, el->id);
    const char *cls = node_attr(el, "class");
    if (cls) { const char *p = cls; while (*p) { while (is_ws((unsigned char)*p)) p++; const char *s = p; while (*p && !is_ws((unsigned char)*p)) p++; if (p > s) f |= (uintptr_t)hm_getn(m, s, (size_t)(p - s)); } }
    if (el->tag) f |= (uintptr_t)hm_get(m, el->tag);
    for (int i = 0; i < el->nattrs; i++) {
        char b[128]; int n = snprintf(b, sizeof b, "[%s", el->attrs[i].name);
        if (n > 0 && n < (int)sizeof b) f |= (uintptr_t)hm_getn(m, b, (size_t)n);
    }
    return f;
}
static void idx_build(StyleEngine *e);
/* Shortcuts consult the current rules; the next pass restyles what changed. */
static void idx_ensure(StyleEngine *e) { if (e->idx_dirty) idx_build(e); }
int style_hover_affects(StyleEngine *e, Node *el) {
    RuleIndex *ix = &e->idx;
    idx_ensure(e);
    if (ix->hov_has) return 4;
    return (int)((ix->hov_univ | hov_lookup(&ix->hov, el)) & 7);
}
static bool has_key(Node *el, const char *k) {
    if ((el->id && !strcmp(el->id, k)) || (el->tag && !strcmp(el->tag, k))) return true;
    if (k[0] == '[') return node_has_attr(el, k + 1);
    const char *cls = node_attr(el, "class");
    size_t kn = strlen(k);
    if (cls) for (const char *p = cls; *p;) { while (is_ws((unsigned char)*p)) p++; const char *s = p; while (*p && !is_ws((unsigned char)*p)) p++; if ((size_t)(p - s) == kn && !memcmp(s, k, kn)) return true; }
    return false;
}
static int desc_collect(HMap *m, Node *el, HovDesc **out, int n) {
    HovDesc *h;
    if (el->id && (h = hm_get(m, el->id)) && n < 32) out[n++] = h;
    const char *cls = node_attr(el, "class");
    if (cls) for (const char *p = cls; *p;) { while (is_ws((unsigned char)*p)) p++; const char *s = p; while (*p && !is_ws((unsigned char)*p)) p++; if (p > s && (h = hm_getn(m, s, (size_t)(p - s))) && n < 32) out[n++] = h; }
    if (el->tag && (h = hm_get(m, el->tag)) && n < 32) out[n++] = h;
    for (int i = 0; i < el->nattrs; i++) {
        char b[128]; int k = snprintf(b, sizeof b, "[%s", el->attrs[i].name);
        if (k > 0 && k < (int)sizeof b && (h = hm_getn(m, b, (size_t)k)) && n < 32) out[n++] = h;
    }
    return n;
}
void style_hover_desc(StyleEngine *e, Document *d, Node *key, Node *root, int which) {
    RuleIndex *ix = &e->idx;
    idx_ensure(e);
    HovDesc *hs[33]; int n = 0;
    void *u = which ? ix->anc_desc_u : ix->hov_desc_u;
    if (u && (which || (ix->hov_univ & 6))) hs[n++] = u;
    n = desc_collect(which ? &ix->anc_desc : &ix->hov_desc, key, hs, n);
    bool all = ix->hov_has || n >= 32;
    for (int i = 0; i < n && !all; i++) all = hs[i]->univ;
    if (all) { doc_mark_state_dirty(d, root, true); return; }
    for (Node *c = root->first; c;) {
        bool hit = false;
        if (c->type == NODE_ELEMENT)
            for (int i = 0; i < n && !hit; i++)
                for (int j = 0; j < hs[i]->keys.n && !hit; j++) hit = has_key(c, hs[i]->keys.v[j]);
        if (hit) doc_mark_state_dirty(d, c, true);
        if (!hit && c->first) { c = c->first; continue; }
        while (c != root && !c->next) c = c->parent;
        c = c == root ? NULL : c->next;
    }
}
int style_hover_has(StyleEngine *e, Node *el, bool child) {
    RuleIndex *ix = &e->idx;
    idx_ensure(e);
    if (ix->hov_has) return 0;
    return (int)hov_lookup(child ? &ix->hov_ach : &ix->hov_anc, el);
}
static Node *tree_scope(Node *n) {
    if (!n) return NULL;
    while (n->parent) n = n->parent;
    return n->type == NODE_FRAGMENT && n->host ? n : NULL;
}
static bool sel_uses_pseudo(const Selector *sel, const char *name) {
    for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++) {
        const SimpleSel *s = &sel->c[i].s[j];
        if (is_pseudo(s, name)) return true;
        if (s->sub) for (int k = 0; k < s->sub->n; k++) if (sel_uses_pseudo(&s->sub->v[k], name)) return true;
    }
    return false;
}
/* A :focus-within dependency that the old/new comparison in doc_focus_within_moved
 * cannot bound to the subtrees of the elements whose state changed. */
static bool fw_needs_more(const Selector *sel) {
    for (int i = 0; i < sel->n; i++) {
        if (sel->c[i].comb == CB_ADJ || sel->c[i].comb == CB_SIB) return true;
        for (int j = 0; j < sel->c[i].n; j++) {
            const SimpleSel *s = &sel->c[i].s[j];
            if (is_pseudo(s, "has") || is_pseudo(s, "host") || is_pseudo(s, "host-context") || is_pseudo(s, "slotted")) return true;
            if (s->sub) for (int k = 0; k < s->sub->n; k++) if (fw_needs_more(&s->sub->v[k])) return true;
        }
    }
    return false;
}
bool doc_uses_focus_within(Document *d) {
    StyleEngine *e = d ? d->stylesheets : NULL;
    if (!e) return false;
    idx_ensure(e);
    return e->idx.focus_within;
}
static bool sel_has_host(const Selector *sel) {
    const char *host = atom("host"), *context = atom("host-context");
    for (int i = 0; i < sel->n; i++) for (int k = 0; k < sel->c[i].n; k++)
        if (sel->c[i].s[k].kind == SK_PSEUDO && (sel->c[i].s[k].name == host || sel->c[i].s[k].name == context)) return true;
    return false;
}
/* A positive class required on the subject or an ancestor. Never infer a
   requirement from :not/:has. A compound left of a sibling combinator is a
   sibling, not an ancestor, so it is skipped; a compound left of a
   descendant/child combinator is always an ancestor (in `A > B + C`, A is C's
   parent too). :is/:where alternatives only provide a requirement when they all agree. */
static const char *required_class(const Selector *sel, bool ancestors) {
    const char *required = NULL;
    for (int i = sel->n - 1; i >= 0; i--) {
        const Compound *c = &sel->c[i];
        if (i < sel->n - 1 && (sel->c[i + 1].comb == CB_ADJ || sel->c[i + 1].comb == CB_SIB)) continue;
        /* Prefer ancestors over the subject: the ordinary rule index already
           guarantees a subject class, whereas a missing ancestor rejects the
           entire otherwise-expensive ancestor walk. Positive nested selectors
           can expose an ancestor even within the subject compound. */
        for (int k = 0; k < c->n; k++) if (c->s[k].kind == SK_CLASS) { required = c->s[k].name; if (!ancestors) return required; break; }
        for (int k = 0; k < c->n; k++) {
            const SimpleSel *s = &c->s[k];
            if (!(is_pseudo(s, "is") || is_pseudo(s, "where")) || !s->sub || !s->sub->n) continue;
            const char *key = required_class(&s->sub->v[0], ancestors);
            for (int j = 1; key && j < s->sub->n; j++) if (required_class(&s->sub->v[j], ancestors) != key) key = NULL;
            if (key) { required = key; if (!ancestors) return required; }
        }
    }
    return required;
}
static unsigned class_filter_bit(const char *name) {
    uintptr_t h = (uintptr_t)name >> 3;
    h ^= h >> 13; h *= 0x9e3779b1u;
    return (unsigned)((h ^ (h >> 16)) & 1023);
}
static void add_class_filter(uint64_t bits[16], const Node *n) {
    for (int i = 0; i < n->nclasses; i++) {
        unsigned bit = class_filter_bit(n->classes[i]);
        bits[bit >> 6] |= UINT64_C(1) << (bit & 63);
    }
}
static void prepare_nested_filters(Selector *sel) {
    const char *required = required_class(sel, true);
    sel->required_class_filter = required ? (uint16_t)(class_filter_bit(required) + 1) : 0;
    for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++) {
        SelList *sub = sel->c[i].s[j].sub;
        if (sub) for (int k = 0; k < sub->n; k++) prepare_nested_filters(&sub->v[k]);
    }
}
typedef struct { const char *name; uint8_t kind; } SubjectKey;
/* A positive functional selector can supply an ordinary subject index key.
   Every alternative must guarantee the same key on its rightmost compound. */
/* Unkeyed subjects the element itself still narrows: 1 never matches (a
 * vendor pseudo-class), 2 :root, 3 a required attribute, 4 a parent type. */
typedef struct { int kind; const char *name; } LocalKey;
static bool vendor_pseudo(const SimpleSel *s) {
    return s->kind == SK_PSEUDO && s->name && (str_starts(s->name, "-moz-") || str_starts(s->name, "-webkit-") ||
        str_starts(s->name, "-ms-")) && strcmp(s->name, "-moz-any") && strcmp(s->name, "-webkit-any");
}
static LocalKey local_key(const Selector *sel) {
    for (int i = 0; i < sel->n; i++) for (int k = 0; k < sel->c[i].n; k++)
        if (vendor_pseudo(&sel->c[i].s[k])) return (LocalKey){1, NULL};
    const Compound *last = &sel->c[sel->n - 1];
    for (int k = 0; k < last->n; k++) if (is_pseudo(&last->s[k], "root")) return (LocalKey){2, NULL};
    for (int k = 0; k < last->n; k++)
        if (last->s[k].kind == SK_ATTR) return (LocalKey){3, atom_lower(last->s[k].name, strlen(last->s[k].name))};
    if (sel->n > 1 && last->comb == CB_CHILD) {
        const Compound *up = &sel->c[sel->n - 2];
        for (int k = 0; k < up->n; k++) if (up->s[k].kind == SK_CLASS) return (LocalKey){5, up->s[k].name};
        for (int k = 0; k < up->n; k++) if (up->s[k].kind == SK_TYPE) return (LocalKey){4, up->s[k].value};
        for (int k = 0; k < up->n; k++) if (up->s[k].kind == SK_ATTR) return (LocalKey){6, atom_lower(up->s[k].name, strlen(up->s[k].name))};
    }
    return (LocalKey){0};
}
static SubjectKey subject_key(const Selector *sel) {
    SubjectKey key = {0};
    if (!sel->n) return key;
    const Compound *last = &sel->c[sel->n - 1];
    for (int k = 0; k < last->n; k++) {
        const SimpleSel *s = &last->s[k];
        if (s->kind == SK_ID) return (SubjectKey){s->name, SK_ID};
        if (s->kind == SK_CLASS && key.kind != SK_CLASS) key = (SubjectKey){s->name, SK_CLASS};
        else if (s->kind == SK_TYPE && !key.name) key = (SubjectKey){s->value, SK_TYPE};
    }
    if (key.name) return key;
    for (int k = 0; k < last->n; k++) {
        const SimpleSel *s = &last->s[k];
        if (!(is_pseudo(s, "is") || is_pseudo(s, "where")) || !s->sub || !s->sub->n) continue;
        key = subject_key(&s->sub->v[0]);
        for (int j = 1; key.name && j < s->sub->n; j++) {
            SubjectKey other = subject_key(&s->sub->v[j]);
            if (key.name != other.name || key.kind != other.kind) key.name = NULL;
        }
        if (key.name) return key;
    }
    return (SubjectKey){0};
}
/* Keys of a subject :is()/:where() whose every branch is keyed: the rule
 * can match only elements carrying one of them. */
static int subject_keys(const Selector *sel, SubjectKey out[8]) {
    if (!sel->n) return 0;
    const Compound *last = &sel->c[sel->n - 1];
    for (int k = 0; k < last->n; k++) {
        const SimpleSel *s = &last->s[k];
        if (!(is_pseudo(s, "is") || is_pseudo(s, "where")) || !s->sub || !s->sub->n) continue;
        int n = 0;
        for (int j = 0; j < s->sub->n; j++) {
            SubjectKey alt = subject_key(&s->sub->v[j]);
            if (!alt.name) {
                LocalKey local = local_key(&s->sub->v[j]);
                if (local.kind == 3) alt = (SubjectKey){local.name, SK_ATTR};
            }
            bool dup = false;
            for (int i = 0; alt.name && i < n; i++) dup |= out[i].name == alt.name && out[i].kind == alt.kind;
            if (!alt.name || (!dup && n == 8)) { n = 0; break; }
            if (!dup) out[n++] = alt;
        }
        if (n) return n;
    }
    return 0;
}
/* Keys one of which the subject's parent must carry: those of the compound
 * left of a child combinator, or of every branch of a subject :is()/:where()
 * such as Tailwind's :where(.space-y-4 > :not(:last-child)). */
static int parent_keys(const Selector *sel, SubjectKey out[4]) {
    if (!sel->n || sel->pseudo_el) return 0;
    if (sel->n > 1 && sel->c[sel->n - 1].comb == CB_CHILD) {
        const Compound *up = &sel->c[sel->n - 2];
        Selector one = {.c = (Compound *)up, .n = 1};
        SubjectKey key = subject_key(&one), many[8];
        if (key.name) { out[0] = key; return 1; }
        for (int k = 0; k < up->n; k++)
            if (up->s[k].kind == SK_ATTR) { out[0] = (SubjectKey){atom_lower(up->s[k].name, strlen(up->s[k].name)), SK_ATTR}; return 1; }
        int n = subject_keys(&one, many);
        if (n > 4) return 0;
        for (int i = 0; i < n; i++) out[i] = many[i];
        return n;
    }
    const Compound *last = &sel->c[sel->n - 1];
    for (int k = 0; k < last->n; k++) {
        const SimpleSel *s = &last->s[k];
        if (!(is_pseudo(s, "is") || is_pseudo(s, "where")) || !s->sub || !s->sub->n) continue;
        int n = 0;
        for (int j = 0; j < s->sub->n; j++) {
            SubjectKey alt[4]; int m = parent_keys(&s->sub->v[j], alt);
            if (!m) { n = 0; break; }
            for (int a = 0; a < m; a++) {
                bool dup = false;
                for (int i = 0; i < n; i++) dup |= out[i].name == alt[a].name && out[i].kind == alt[a].kind;
                if (dup) continue;
                if (n == 4) { n = 0; break; }
                out[n++] = alt[a];
            }
            if (!n) break;
        }
        if (n) return n;
    }
    return 0;
}
static void tree_parent_keys(TreeDependency *dep, const Selector *sel) {
    SubjectKey keys[4]; int n = dep->scope ? 0 : parent_keys(sel, keys);
    for (int i = 0; i < n; i++) { dep->parent_names[i] = keys[i].name; dep->parent_kinds[i] = keys[i].kind; }
    dep->nparent = (uint8_t)n;
}
/* Bit 1: subject emptiness; bit 2: another element's emptiness. A text
 * transition already dirties its parent, so only bit 2 needs wider work. */
static unsigned empty_dependencies(const Selector *sel) {
    unsigned result = 0;
    for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++) {
        const SimpleSel *s = &sel->c[i].s[j];
        unsigned dependency = is_pseudo(s, "empty") ? 1 : 0;
        if (s->sub) {
            for (int k = 0; k < s->sub->n; k++) dependency |= empty_dependencies(&s->sub->v[k]);
            if (dependency && !(is_pseudo(s,"is") || is_pseudo(s,"where") || is_pseudo(s,"not"))) dependency = 2;
        }
        result |= dependency && i != sel->n - 1 ? 2 : dependency;
    }
    return result;
}
/* Removing an entire functional test that consults :empty is conservative even
 * under :not. Retain independent compounds/combinators: e.g. an unkeyed child
 * of [data-spacing] cannot affect html or unrelated conversation history. The
 * retained simple selectors borrow their immutable payloads from the sheet. */
static Selector empty_candidate(const Selector *sel) {
    Selector result = *sel;
    result.required_class_filter = 0;
    result.c = xcalloc((size_t)sel->n, sizeof *result.c);
    for (int i = 0; i < sel->n; i++) {
        result.c[i].comb = sel->c[i].comb;
        result.c[i].s = xcalloc((size_t)sel->c[i].n, sizeof *result.c[i].s);
        for (int j = 0; j < sel->c[i].n; j++) {
            const SimpleSel *s = &sel->c[i].s[j];
            bool dependent = is_pseudo(s, "empty");
            if (s->sub) for (int k = 0; k < s->sub->n; k++) dependent |= empty_dependencies(&s->sub->v[k]) != 0;
            if (!dependent) result.c[i].s[result.c[i].n++] = *s;
            else if (is_pseudo(s, "host")) {
                /* Keep the shadow-boundary traversal marker, dropping only
                 * its condition. A bare universal cannot cross to the host. */
                SimpleSel host = *s; host.sub = NULL;
                result.c[i].s[result.c[i].n++] = host;
            }
        }
    }
    return result;
}
/* How a candidate reaches the changed element, for precise invalidation.
 * flags: 1 :empty on this element, 2 through :has, 4 a sibling combinator in
 * :has, 8 anything else (nth-of, :host, combinators inside :is): scan all. */
static unsigned empty_reach(const SimpleSel *s);
static unsigned empty_reach_selector(const Selector *sel, bool relative) {
    unsigned r = 0;
    for (int i = 0; i < sel->n; i++) {
        if (relative && (sel->c[i].comb == CB_ADJ || sel->c[i].comb == CB_SIB)) r |= 4;
        for (int j = 0; j < sel->c[i].n; j++) {
            unsigned x = empty_reach(&sel->c[i].s[j]);
            if (x && !relative && i != sel->n - 1) x |= 8;
            r |= x;
        }
    }
    return r;
}
static unsigned empty_reach(const SimpleSel *s) {
    if (is_pseudo(s, "empty")) return 1;
    if (!s->sub) return 0;
    bool has = is_pseudo(s, "has"), plain = is_pseudo(s, "is") || is_pseudo(s, "where") || is_pseudo(s, "not");
    unsigned r = 0;
    for (int k = 0; k < s->sub->n; k++) r |= empty_reach_selector(&s->sub->v[k], has);
    if (!r) return 0;
    if (has) return 2 | (r & 4) | (r & 8);
    return plain ? r : r | 8;
}
static void empty_classify(const Selector *sel, EmptyDependency *d) {
    d->mode = 0; d->dep = -1; d->sib = false;
    unsigned all = 0; int found = -1, count = 0;
    for (int i = 0; i < sel->n; i++) {
        unsigned r = 0;
        for (int j = 0; j < sel->c[i].n; j++) {
            r |= empty_reach(&sel->c[i].s[j]);
            if (is_pseudo(&sel->c[i].s[j], "host") || is_pseudo(&sel->c[i].s[j], "host-context") || is_pseudo(&sel->c[i].s[j], "slotted")) all |= 8;
        }
        if (r) { found = i; count++; all |= r; }
    }
    if (count != 1 || (all & 8)) return;
    d->dep = found; d->mode = all & 2 ? 2 : 1; d->sib = (all & 4) != 0;
}
/* Bit 1: dependencies contained in a changed parent's subtree. Bit 2:
 * dependencies which can affect ancestors or their following siblings too.
 * Bit 4: emptiness, already covered by the separate transition index.
 * Nested functional tests are classified recursively, including negation. */
static unsigned tree_dependencies(const Selector *sel);
static unsigned tree_simple_dependencies(const SimpleSel *s) {
    unsigned result = 0;
    if (s->kind == SK_PSEUDO) {
        if (is_pseudo(s, "has") || is_pseudo(s, "focus-within")) result = 2;
        else if (is_pseudo(s, "empty")) result = 4;
        else if (is_pseudo(s, "first-child") || is_pseudo(s, "last-child") || is_pseudo(s, "only-child") ||
                is_pseudo(s, "first-of-type") || is_pseudo(s, "last-of-type") || is_pseudo(s, "only-of-type") ||
                (s->name && !strncmp(s->name, "nth-", 4))) result = 1;
    }
    if (s->sub) for (int i = 0; i < s->sub->n; i++) result |= tree_dependencies(&s->sub->v[i]);
    return result;
}
static unsigned tree_dependencies(const Selector *sel) {
    unsigned result = 0;
    for (int i = 0; i < sel->n; i++) {
        if (sel->c[i].comb == CB_ADJ || sel->c[i].comb == CB_SIB) result |= 1;
        for (int j = 0; j < sel->c[i].n; j++) result |= tree_simple_dependencies(&sel->c[i].s[j]);
    }
    return result;
}
/* Where can this functional test change? 1 = changed ancestors, 2 = also
 * preceding siblings, 4 = an embedded non-subject dependency (fallback).
 * :has arguments are relative; only a leading sibling relation widens the
 * affected subjects beyond ancestors of the child-list change. */
static unsigned tree_global_scope(const SimpleSel *s) {
    if (is_pseudo(s, "focus-within")) return 1;
    if (is_pseudo(s, "has")) {
        unsigned scope = 1;
        if (s->sub) for (int i = 0; i < s->sub->n; i++) {
            Selector *sel = &s->sub->v[i];
            if (sel->n > 1 && (sel->c[1].comb == CB_ADJ || sel->c[1].comb == CB_SIB)) scope |= 2;
        }
        return scope;
    }
    if (!(tree_simple_dependencies(s) & 2)) return 0;
    if (!(is_pseudo(s, "is") || is_pseudo(s, "where") || is_pseudo(s, "not") || is_pseudo(s, "host") ||
            is_pseudo(s, "matches") || is_pseudo(s, "-webkit-any") || is_pseudo(s, "-moz-any"))) return 4;
    unsigned scope = 0;
    if (s->sub) for (int i = 0; i < s->sub->n; i++) {
        Selector *sel = &s->sub->v[i];
        for (int j = 0; j < sel->n; j++) for (int k = 0; k < sel->c[j].n; k++) {
            unsigned sub = tree_global_scope(&sel->c[j].s[k]);
            scope |= sub && j != sel->n - 1 ? 4 : sub;
        }
    }
    return scope;
}
static unsigned tree_local_scope(const SimpleSel *s) {
    if (is_pseudo(s, "has") || !(tree_simple_dependencies(s) & 1)) return 0;
    if (is_pseudo(s, "first-child") || is_pseudo(s, "last-child") || is_pseudo(s, "only-child") ||
            is_pseudo(s, "first-of-type") || is_pseudo(s, "last-of-type") || is_pseudo(s, "only-of-type") ||
            (s->kind == SK_PSEUDO && s->name && !strncmp(s->name, "nth-", 4))) return 1;
    unsigned scope = 0;
    if (s->sub) for (int i = 0; i < s->sub->n; i++) {
        Selector *sel = &s->sub->v[i];
        for (int j = 0; j < sel->n; j++) {
            unsigned sub = sel->c[j].comb == CB_ADJ || sel->c[j].comb == CB_SIB ? 1 : 0;
            for (int k = 0; k < sel->c[j].n; k++) sub |= tree_local_scope(&sel->c[j].s[k]);
            scope |= sub && j != sel->n - 1 ? 4 : sub;
        }
    }
    return scope;
}
/* Descendant/child queries can use a changed-subtree key filter. State changes
 * independently invalidate selectors and disable this filter for that pass
 * (inline style edits included: CSSOM writes go through the style attribute).
 * Positional, sibling and nested :has tests can change an existing witness
 * outside the edited subtree and must fall back. */
static bool tree_witness_query_safe(const Selector *sel) {
    for (int i = 0; i < sel->n; i++) {
        if (sel->c[i].comb == CB_ADJ || sel->c[i].comb == CB_SIB) return false;
        for (int j = 0; j < sel->c[i].n; j++) {
            const SimpleSel *s = &sel->c[i].s[j];
            if (s->kind != SK_PSEUDO) continue;
            if (is_pseudo(s, "scope") || is_pseudo(s, "focus") || is_pseudo(s, "focus-visible") ||
                    is_pseudo(s, "hover") || is_pseudo(s, "active")) continue;
            if (!(is_pseudo(s, "is") || is_pseudo(s, "where") || is_pseudo(s, "not"))) return false;
            if (s->sub) for (int k = 0; k < s->sub->n; k++)
                if (!tree_witness_query_safe(&s->sub->v[k])) return false;
        }
    }
    return true;
}
static bool tree_witness_keys(const SimpleSel *s, uint64_t keys[16]) {
    if (is_pseudo(s, "has")) {
        if (!s->sub || !s->sub->n) return false;
        for (int i = 0; i < s->sub->n; i++) {
            const Selector *sel = &s->sub->v[i];
            if (!tree_witness_query_safe(sel)) return false;
            SubjectKey key = subject_key(sel);
            if (!key.name && sel->n) {
                const Compound *last = &sel->c[sel->n - 1];
                for (int j = 0; j < last->n; j++) if (last->s[j].kind == SK_ATTR) {
                    key.name = atom_lower(last->s[j].name, strlen(last->s[j].name)); break;
                }
            }
            if (!key.name) return false;
            const char *name = key.kind == SK_TYPE ? atom_lower(key.name, strlen(key.name)) : key.name;
            unsigned bit = dom_tree_key_bit(name); keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
        }
        return true;
    }
    if (!(is_pseudo(s, "is") || is_pseudo(s, "where") || is_pseudo(s, "not"))) return false;
    bool found = false;
    if (s->sub) for (int i = 0; i < s->sub->n; i++) {
        Selector *sel = &s->sub->v[i];
        for (int j = 0; j < sel->n; j++) for (int k = 0; k < sel->c[j].n; k++) {
            SimpleSel *part = &sel->c[j].s[k];
            if (!(tree_simple_dependencies(part) & 2)) continue;
            found = true;
            if (!tree_witness_keys(part, keys)) return false;
        }
    }
    return found;
}
static bool tree_focus_only(const SimpleSel *s) {
    if (is_pseudo(s, "focus-within")) return true;
    if (!(is_pseudo(s, "is") || is_pseudo(s, "where") || is_pseudo(s, "not"))) return false;
    bool found = false;
    if (s->sub) for (int i = 0; i < s->sub->n; i++) {
        Selector *sel = &s->sub->v[i];
        for (int j = 0; j < sel->n; j++) for (int k = 0; k < sel->c[j].n; k++) {
            SimpleSel *part = &sel->c[j].s[k];
            if (!(tree_simple_dependencies(part) & 2)) continue;
            found = true; if (!tree_focus_only(part)) return false;
        }
    }
    return found;
}
static const char *selector_attribute_key(const SimpleSel *s) {
    /* Token membership and exact IDs depend on the same keys as .class/#id.
     * Other operators (including case-insensitive tests) need the whole
     * attribute: reordering tokens can change [class=...] without changing
     * membership. */
    if (s->value && s->ci != 1 &&
            ((str_ieq(s->name, "class") && s->op == '~') ||
             (str_ieq(s->name, "id") && s->op == '='))) return atom(s->value);
    return atom_lower(s->name, strlen(s->name));
}
/* subject: skip the subject compound's own keys. Changing them marks the
 * subject element itself, which then rematches against the current tree. */
static void selector_change_dependencies(const Selector *sel, uint64_t keys[16], bool subject) {
    for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++) {
        const SimpleSel *s = &sel->c[i].s[j];
        bool self = subject && i == sel->n - 1;
        const char *key = is_pseudo(s, "target") ? "id" : self ? NULL : is_pseudo(s, "focus-within") ? ":focus-within" :
            s->kind == SK_CLASS || s->kind == SK_ID ? s->name :
            s->kind == SK_ATTR ? selector_attribute_key(s) : NULL;
        if (key) { unsigned bit = dom_tree_key_bit(key); keys[bit >> 6] |= UINT64_C(1) << (bit & 63); }
        if (s->sub) for (int k = 0; k < s->sub->n; k++) selector_change_dependencies(&s->sub->v[k], keys, false);
    }
}
/* Keys whose change on an element can alter its descendants' or following
 * siblings' matches: tests left of the subject compound, outside :has(). */
static void reselect_dependencies(const Selector *sel, uint64_t keys[16], bool subject) {
    for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++) {
        const SimpleSel *s = &sel->c[i].s[j];
        bool self = subject && i == sel->n - 1;
        const char *key = is_pseudo(s, "target") ? "id" : self ? NULL : is_pseudo(s, "focus-within") ? ":focus-within" :
            s->kind == SK_CLASS || s->kind == SK_ID ? s->name :
            s->kind == SK_ATTR ? selector_attribute_key(s) : NULL;
        if (key) { unsigned bit = dom_tree_key_bit(key); keys[bit >> 6] |= UINT64_C(1) << (bit & 63); }
        if (s->sub && !is_pseudo(s, "has")) for (int k = 0; k < s->sub->n; k++) reselect_dependencies(&s->sub->v[k], keys, self);
    }
}
/* Descendant invalidation sets: a change of key K rematches, below the
 * changed element, only subjects of rules that test K left of the subject. */
static void reselect_subjects_add(RuleIndex *ix, const uint64_t keys[16], unsigned subject) {
    for (int k = 0; k < 16; k++) for (uint64_t bits = keys[k]; bits; bits &= bits - 1) {
        unsigned bit = (unsigned)k * 64 + (unsigned)__builtin_ctzll(bits);
        ix->reselect_keys[k] |= bits & -bits;
        if (!ix->reselect_subjects[bit].n || ix->reselect_subjects[bit].v[ix->reselect_subjects[bit].n - 1] != subject)
            vec_push(ix->reselect_subjects[bit], (uint16_t)subject);
    }
}
static bool keyed_bit(const StyleEngine *e, const char *key) {
    unsigned bit = dom_tree_key_bit(key);
    return e->keyed_subjects[bit >> 6] >> (bit & 63) & 1;
}
static bool keyed_hit(const StyleEngine *e, const Node *n) {
    if (e->keyed_whole) return true;
    if (n->id && keyed_bit(e, n->id)) return true;
    for (int i = 0; i < n->nclasses; i++) if (keyed_bit(e, n->classes[i])) return true;
    if (keyed_bit(e, n->ns == NS_HTML ? n->tag : atom_lower(n->tag, strlen(n->tag)))) return true;
    for (int i = 0; i < n->nattrs; i++) if (keyed_bit(e, atom_lower(n->attrs[i].name, strlen(n->attrs[i].name)))) return true;
    return false;
}
/* Class/ID keys a :has() argument tests. Without a tree edit, their change
 * reaches subjects outside the changed element's subtree only through :has. */
static void has_argument_keys(const Selector *sel, uint64_t keys[16], bool inside) {
    for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++) {
        const SimpleSel *s = &sel->c[i].s[j];
        if (inside && (s->kind == SK_CLASS || s->kind == SK_ID)) {
            unsigned bit = dom_tree_key_bit(s->name); keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
        }
        if (s->sub) for (int k = 0; k < s->sub->n; k++) has_argument_keys(&s->sub->v[k], keys, inside || is_pseudo(s, "has"));
    }
}
static Selector tree_candidate(RuleIndex *ix, const Selector *sel, bool global, int anchor, unsigned scope) {
    Selector result = *sel;
    result.required_class_filter = 0;
    result.n = 0;
    result.c = xcalloc((size_t)sel->n, sizeof *result.c);
    for (int i = 0; i < sel->n; i++) {
        uint8_t comb = sel->c[i].comb;
        bool mark_anchor = i == anchor && !(scope & 4);
        /* A sibling may be inserted/removed. Keep the shared parent/ancestor
         * constraint: `.group > p ~ div span` becomes `.group > div span`. */
        if (!global && (comb == CB_ADJ || comb == CB_SIB) && result.n) {
            Compound *left = &result.c[--result.n];
            comb = left->comb;
            /* Siblings share the same parent: carry a positional anchor
             * across a collapsed sibling chain instead of dropping it. */
            for (int k = 0; k < left->n; k++) if (left->s[k].kind == SK_TREE_RELATED) mark_anchor = true;
            free(left->s);
        }
        Compound *c = &result.c[result.n++];
        *c = (Compound){.comb = comb};
        c->s = xcalloc((size_t)sel->c[i].n + 1, sizeof *c->s);
        if (mark_anchor)
            c->s[c->n++] = (SimpleSel){.kind = SK_TREE_RELATED, .op = global ? (scope & 2) != 0 : 2};
        for (int j = 0; j < sel->c[i].n; j++) {
            const SimpleSel *s = &sel->c[i].s[j];
            if (!tree_simple_dependencies(s)) c->s[c->n++] = *s;
            else if (s->sub && (is_pseudo(s, "is") || is_pseudo(s, "where") ||
                    is_pseudo(s, "matches") || is_pseudo(s, "-webkit-any") || is_pseudo(s, "-moz-any"))) {
                /* Positive alternatives may be widened recursively. Preserve
                 * their independent attributes/ancestry, even when they have
                 * no class/id/type key. Dropping :where(menu:has(...) item)
                 * outright would turn a menu dependency into a universal. */
                SimpleSel positive = *s;
                positive.sub = xcalloc(1, sizeof *positive.sub);
                positive.sub->n = s->sub->n;
                positive.sub->v = xcalloc((size_t)s->sub->n, sizeof *positive.sub->v);
                vec_push(ix->tree_candidate_lists, positive.sub);
                for (int k = 0; k < s->sub->n; k++)
                    positive.sub->v[k] = tree_candidate(ix, &s->sub->v[k], global, -1, 4);
                c->s[c->n++] = positive;
            } else if (is_pseudo(s, "host")) {
                SimpleSel host = *s; host.sub = NULL;
                c->s[c->n++] = host;
            }
        }
    }
    return result;
}
/* The attribute a simple selector tests: [name], or disabled for :disabled/:enabled. */
static const char *simple_attribute(const SimpleSel *s) {
    if (s->kind == SK_ATTR) return s->name;
    return is_pseudo(s, "disabled") || is_pseudo(s, "enabled") ? "disabled" : NULL;
}
static bool simple_uses_attribute(const SimpleSel *s, const char *name) {
    /* NULL builds a coalesced class/ID candidate, including subjects outside
     * the child-list mutation's ancestor chain. */
    if (!name && (s->kind == SK_CLASS || s->kind == SK_ID || s->kind == SK_TREE_RELATED ||
            (s->kind == SK_ATTR && (str_ieq(s->name, "class") || str_ieq(s->name, "id"))) ||
            is_pseudo(s, "target"))) return true;
    if (name && simple_attribute(s) && str_ieq(simple_attribute(s), name)) return true;
    if (s->sub) for (int k = 0; k < s->sub->n; k++) {
        const Selector *sel = &s->sub->v[k];
        for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++)
            if (simple_uses_attribute(&sel->c[i].s[j], name)) return true;
    }
    return false;
}
/* Attributes cannot alter the tree's combinators. Widen only dependent tests,
 * retaining container constraints even inside unkeyed positive alternatives.
 * Negative and positional tests must be removed as a whole. */
static Selector attribute_candidate(RuleIndex *ix, const Selector *sel, const char *name) {
    Selector out = *sel; out.required_class_filter = 0;
    out.c = xcalloc((size_t)sel->n, sizeof *out.c);
    for (int i = 0; i < sel->n; i++) {
        Compound *c = &out.c[i]; c->comb = sel->c[i].comb;
        c->s = xcalloc((size_t)sel->c[i].n, sizeof *c->s);
        for (int j = 0; j < sel->c[i].n; j++) {
            const SimpleSel *s = &sel->c[i].s[j];
            if (!simple_uses_attribute(s, name)) c->s[c->n++] = *s;
            else if (!name && (s->kind == SK_CLASS || s->kind == SK_ID)) {
                SimpleSel keyed = *s; keyed.kind = SK_CHANGED_KEY; keyed.op = s->kind;
                c->s[c->n++] = keyed;
            }
            else if (s->sub && (is_pseudo(s, "is") || is_pseudo(s, "where") ||
                    is_pseudo(s, "matches") || is_pseudo(s, "-webkit-any") ||
                    is_pseudo(s, "-moz-any") || is_pseudo(s, "has"))) {
                SimpleSel positive = *s;
                positive.sub = xcalloc(1, sizeof *positive.sub);
                positive.sub->n = s->sub->n;
                positive.sub->v = xcalloc((size_t)s->sub->n, sizeof *positive.sub->v);
                vec_push(ix->tree_candidate_lists, positive.sub);
                for (int k = 0; k < s->sub->n; k++)
                    positive.sub->v[k] = attribute_candidate(ix, &s->sub->v[k], name);
                c->s[c->n++] = positive;
            } else if (is_pseudo(s, "host")) {
                SimpleSel host = *s; host.sub = NULL; c->s[c->n++] = host;
            }
        }
    }
    return out;
}
/* bearer: the key an element changing the attribute must have for this rule to see it, or NULL. */
static void add_attribute_candidate(RuleIndex *ix, const char *name, const Rule *rule, const char *subject, const char *bearer) {
    AttributeCandidates *v = hm_get(&ix->attribute_candidates, name);
    if (!v) { v = xcalloc(1, sizeof *v); hm_put(&ix->attribute_candidates, name, v); }
    for (int i = 0; i < v->n; i++) if (v->v[i].source == rule) {
        if (v->v[i].bearer != bearer) v->v[i].bearer = NULL;   /* tested in two compounds */
        return;
    }
    AttributeCandidate candidate = {attribute_candidate(ix, &rule->sel, name), rule, subject, bearer};
    vec_push(*v, candidate);
}
/* The id, class or type a compound requires of its element; NULL when it requires none of them. */
static const char *bearer_key(const Compound *c) {
    const char *cls = NULL, *tag = NULL;
    for (int k = 0; k < c->n; k++) {
        const SimpleSel *s = &c->s[k];
        if (s->kind == SK_ID) return s->name;
        if (s->kind == SK_CLASS && !cls) cls = s->name; else if (s->kind == SK_TYPE) tag = s->value;
    }
    return cls ? cls : tag;
}
static void index_attribute_subjects(RuleIndex *ix, const Selector *sel, const char *subject, bool relative, const Rule *rule) {
    for (int i = 0; i < sel->n; i++) for (int j = 0; j < sel->c[i].n; j++) {
        const SimpleSel *s = &sel->c[i].s[j];
        if (simple_attribute(s)) {
            /* Lowercase is conservative for XML/SVG and required for HTML. */
            const char *name = atom_lower(simple_attribute(s), strlen(simple_attribute(s)));
            if (relative || i != sel->n - 1) {
                /* Only elements this compound can match observe the attribute for other
                 * elements' styles (an edited element restyles itself). One nested in
                 * :is()/:not() is borne by that argument's compound, which may require nothing. */
                const char *bearer = bearer_key(&sel->c[i]);
                desc_add(&ix->attribute_bearers, NULL, name, bearer);
                desc_add(&ix->attribute_subjects, NULL, name, subject);
                add_attribute_candidate(ix, name, rule, subject, bearer);
            }
            else hm_put(&ix->self_attributes, name, (void *)1);
        }
        if (s->sub) for (int k = 0; k < s->sub->n; k++)
            index_attribute_subjects(ix, &s->sub->v[k], subject, relative || i != sel->n - 1 ||
                !(is_pseudo(s, "is") || is_pseudo(s, "where") || is_pseudo(s, "not") ||
                  is_pseudo(s, "matches") || is_pseudo(s, "-webkit-any") || is_pseudo(s, "-moz-any")), rule);
    }
}
#if defined(__ANDROID__) && !defined(NDEBUG)
static int g_html_line; static const char *g_html_key;   /* debug: the last invalidation that dirtied <html> */
#define DBG_HTML(n, key) do { if ((n)->doc && (n) == (n)->doc->html) { g_html_line = __LINE__; g_html_key = (key); } } while (0)
#else
#define DBG_HTML(n, key) ((void)0)
#endif
#define mark_style_subject(n, key) do { DBG_HTML(n, key); mark_style_subject_(n); } while (0)
static void mark_style_subject_(Node *n) {
    n->flags |= NF_STYLE_DIRTY;
    for (Node *p = n->parent ? n->parent : n->host; p; p = p->parent ? p->parent : p->host)
        p->flags |= NF_CHILD_STYLE_DIRTY;
}
/* Focus moves from old to n. Rules testing :focus-within left of their subject
 * (`.group:focus-within .x`, `:focus-within > *`) can only change below the
 * elements whose :focus-within state flips. Rematch just those rules there, with
 * the old and the new focus, and restyle the elements whose result differs;
 * an unkeyed subject would otherwise restyle the whole changed subtree. Rules
 * that also need siblings or :has (`.peer:focus-within ~ .x`) are rematched the
 * same way over the whole document, on the elements carrying their subject key.
 * False: not bounded this way, the caller falls back to the reselect key. */
static bool subject_key_on(const Node *el, const char *tag, const char *k);
static bool fw_rule_changed(Document *d, Node *el, const char *tag, Rule *const *rules, const char *const *keys, int nrules, Node *old, Node *n) {
    for (int i = 0; i < nrules; i++) {
        Rule *r = rules[i];
        if (r->dead || (keys[i] && !subject_key_on(el, tag, keys[i]))) continue;
        d->focus = old; bool was = css_match_selector(&r->sel, el, NULL);
        d->focus = n; bool now = css_match_selector(&r->sel, el, NULL);
        if (was != now) return true;
    }
    return false;
}
bool doc_focus_within_moved(Document *d, Node *old, Node *n) {
    StyleEngine *e = d ? d->stylesheets : NULL;
    if (!e) return false;
    idx_ensure(e);
    RuleIndex *ix = &e->idx;
    if (ix->fw_complex) return false;
    if (!ix->fw_rules.n && !ix->fw_far.n) return true;
    Node *tops[2] = {NULL, NULL};
    for (int side = 0; side < 2; side++)
        for (Node *a = side ? n : old; a; a = a->parent) {
            if (!a->parent && a->host) return false;   /* crosses a shadow boundary */
            Node *other = side ? old : n;
            if (other && node_is_inclusive_ancestor(a, other)) break;
            if (a->type == NODE_ELEMENT) tops[side] = a;
        }
    if (tops[0] && tops[1] && (node_is_inclusive_ancestor(tops[0], tops[1]) || node_is_inclusive_ancestor(tops[1], tops[0]))) return false;
    CssMatchCache *cache = css_match_cache_suspend();
    Node *focus = d->focus;
    for (int side = 0; side < 2 && ix->fw_rules.n; side++) {
        Node *top = tops[side];
        for (Node *el = top; el; ) {
            if (el->type == NODE_ELEMENT) {
                const char *tag = el->ns == NS_HTML ? el->tag : atom_lower(el->tag, strlen(el->tag));
                if (fw_rule_changed(d, el, tag, ix->fw_rules.v, ix->fw_keys.v, ix->fw_rules.n, old, n)) mark_style_subject(el, "fw");
            }
            if (el->first && el->type == NODE_ELEMENT) { el = el->first; continue; }
            while (el != top && !el->next) el = el->parent;
            el = el == top ? NULL : el->next;
        }
    }
    if (ix->fw_far.n && (tops[0] || tops[1]))
        for (Node *el = d->node.first; el; el = node_next_deep(el, &d->node)) {
            if (el->type != NODE_ELEMENT) continue;
            const char *tag = el->ns == NS_HTML ? el->tag : atom_lower(el->tag, strlen(el->tag));
            if (fw_rule_changed(d, el, tag, ix->fw_far.v, ix->fw_far_keys.v, ix->fw_far.n, old, n)) mark_style_subject(el, "fw_far");
        }
    d->focus = focus;
    css_match_cache_end(cache);
    return true;
}
/* Sibling combinators: a selector-relevant change to an element can alter
 * the match of its following siblings (`.peer:checked ~ .x`, `[title] + p`). */
static void sib_key(RuleIndex *ix, const char *key, bool general) {
    unsigned bit = dom_tree_key_bit(key);
    ix->sib_keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
    if (general) ix->sib_general_keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
}
static bool sib_logical(const SimpleSel *s) {
    static const char *const names[] = { "not", "is", "where", "matches", "-webkit-any", "-moz-any",
        "nth-child", "nth-last-child", "nth-of-type", "nth-last-of-type", "first-child", "last-child",
        "only-child", "first-of-type", "last-of-type", "only-of-type", "root", "scope", "host",
        "hover", "link", "any-link", "visited" }; /* structure has the tree index; :hover its own */
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (!strcmp(s->name, names[i])) return true;
    return false;
}
static void sib_referenced(RuleIndex *ix, const Compound *c, bool keys, bool general) {
    for (int k = 0; k < c->n; k++) {
        const SimpleSel *x = &c->s[k];
        if (x->kind == SK_ATTR && str_ieq(x->name, "style")) { ix->sib_style = true; ix->sib_general_style |= general; }
        if (!keys) ;
        else if (x->kind == SK_TYPE) sib_key(ix, atom_lower(x->name, strlen(x->name)), general);
        else if (x->kind == SK_ID || x->kind == SK_CLASS) sib_key(ix, x->name, general);
        else if (x->kind == SK_ATTR) sib_key(ix, atom_lower(x->name, strlen(x->name)), general);
        else if (x->kind == SK_PSEUDO && x->name && !sib_logical(x)) { ix->sib_state = true; ix->sib_general_state |= general; }
        if (x->sub && !is_pseudo(x, "has")) for (int i = 0; i < x->sub->n; i++)
            for (int j = 0; j < x->sub->v[i].n; j++) sib_referenced(ix, &x->sub->v[i].c[j], keys, general);
    }
}
static void sib_scan(RuleIndex *ix, const Selector *sel, bool outer_desc) {
    for (int i = 0; i < sel->n; i++) {
        const Compound *c = &sel->c[i];
        for (int k = 0; k < c->n; k++) if (c->s[k].sub && !is_pseudo(&c->s[k], "has"))
            for (int j = 0; j < c->s[k].sub->n; j++) sib_scan(ix, &c->s[k].sub->v[j], outer_desc || i < sel->n - 1);
        if (i == sel->n - 1 || (sel->c[i + 1].comb != CB_ADJ && sel->c[i + 1].comb != CB_SIB)) continue;
        ix->sib_any = true;
        for (int j = i + 2; j < sel->n; j++) if (sel->c[j].comb == CB_DESC || sel->c[j].comb == CB_CHILD) ix->sib_desc = true;
        if (outer_desc) ix->sib_desc = true;
        /* `a + b` reaches only the next element; `~` or a chain reaches any later one */
        bool general = sel->c[i + 1].comb == CB_SIB || outer_desc ||
            (i + 2 < sel->n && (sel->c[i + 2].comb == CB_ADJ || sel->c[i + 2].comb == CB_SIB));
        /* A required id/class/tag bounds the elements whose match can flip. */
        bool required = false;
        for (int k = 0; k < c->n; k++) {
            const SimpleSel *x = &c->s[k];
            if (x->kind == SK_TYPE) { sib_key(ix, atom_lower(x->name, strlen(x->name)), general); required = true; }
            else if (x->kind == SK_ID || x->kind == SK_CLASS) { sib_key(ix, x->name, general); required = true; }
        }
        sib_referenced(ix, c, !required, general);
    }
}
static bool sib_bit(const uint64_t keys[16], const char *key) {
    unsigned bit = dom_tree_key_bit(key);
    return (keys[bit >> 6] >> (bit & 63)) & 1;
}
/* 1: a sibling relation can see n's change; 2: beyond the next element */
static int sib_hit(const RuleIndex *ix, const Document *d, const Node *n, int mode) {
    int hit = 0;
    for (int pass = 0; pass < 2; pass++) {
        const uint64_t *keys = pass ? ix->sib_general_keys : ix->sib_keys;
        bool h = mode == 0 ? (pass ? ix->sib_general_state : ix->sib_state) :
            mode == 2 && (pass ? ix->sib_general_style : ix->sib_style) && sib_bit(keys, "style");
        for (int k = 0; !h && mode && k < 16; k++) h = (keys[k] & d->selector_change_keys[k]) != 0;
        if (!h) h = sib_bit(keys, n->ns == NS_HTML ? n->tag : atom_lower(n->tag, strlen(n->tag))) || (n->id && sib_bit(keys, n->id));
        for (int i = 0; !h && i < n->nclasses; i++) h = sib_bit(keys, n->classes[i]);
        if (!h) break;
        hit = pass + 1;
    }
    return hit;
}
/* mode 0: element state (focus, checked, value); 1: attribute; 2: inline style */
void node_sibling_selectors_changed(Node *n, int mode) {
    Document *d = n->doc;
    StyleEngine *e = d ? d->stylesheets : NULL;
    if (e) idx_ensure(e);
    if (!e || !e->idx.sib_any || n->type != NODE_ELEMENT || !(n->flags & NF_CONNECTED) || !n->next) return;
    const RuleIndex *ix = &e->idx;
    if (mode == 2 && !ix->sib_style) return;
    int hit = sib_hit(ix, d, n, mode);
    if (!hit) return;
    bool any = false;
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (d->debug_mutations) __android_log_print(ANDROID_LOG_INFO, "LumenMutation", "kind=sibling mode=%d hit=%d desc=%d tag=%s next=%s",
        mode, hit, ix->sib_desc, n->tag ? n->tag : "-", n->next->tag ? n->next->tag : "-");
#endif
    for (Node *s = n->next; s; s = s->next) if (s->type == NODE_ELEMENT) {
        s->flags |= NF_STYLE_DIRTY | (ix->sib_desc ? NF_RESELECT_SUBTREE : 0); DBG_HTML(s, "sibling");
        any = true;
        if (hit == 1) break;
    }
    if (any) for (Node *p = n->parent ? n->parent : n->host; p; p = p->parent ? p->parent : p->host) p->flags |= NF_CHILD_STYLE_DIRTY;
}
/* HTML attributes other than the ones listed here have no built-in layout, presentation, state
 * or resource behavior beyond selectors: data-/aria-, page attributes such as jsaction, and the
 * UA's [hidden] rule (disabled also flips :disabled/:enabled, indexed as the attribute, and dom.c
 * reselects a fieldset's descendants). Their stylesheet uses can affect ancestors, siblings and
 * shadow trees, so collect conservative subjects over the whole tree instead of just the changed
 * node. Image sources (src, srcset, sizes, poster) are also selector-only for style; dom.c still
 * marks their element's layout. The listed ones (presentational hints, form and element state,
 * links, direction and language, shadow and slot wiring) retain the ordinary invalidation path. */
static int attribute_name_cmp(const void *key, const void *entry) { return strcasecmp(key, *(const char *const *)entry); }
bool css_selector_only_attribute(const char *name) {
    static const char *const builtin[] = {   /* sorted */
        "accept", "align", "alink", "allow", "allowfullscreen", "alt", "as", "async", "autofocus", "autoplay", "background",
        "behavior", "bgcolor", "blocking", "border", "bottommargin", "cellpadding", "cellspacing", "char", "charoff", "charset",
        "checked", "class", "classid", "clear", "codebase", "color", "cols", "colspan", "compact", "content", "contenteditable",
        "controls", "controlslist", "coords", "crossorigin", "data", "default", "defer", "dir", "direction", "dirname",
        "exportparts", "face", "form", "formnovalidate", "frame", "frameborder", "headers", "height", "high", "href", "hspace",
        "http-equiv", "id", "integrity", "is", "ismap", "kind", "label", "lang", "leftmargin", "link", "list", "loop", "low",
        "marginheight", "marginwidth", "max", "maxlength", "media", "min", "minlength", "multiple", "muted", "nomodule",
        "noshade", "novalidate", "nowrap", "open", "optimum", "part", "pattern", "placeholder", "playsinline", "popover",
        "popovertarget", "popovertargetaction", "preload", "readonly", "rel", "required", "reversed", "rightmargin",
        "rows", "rowspan", "rules", "sandbox", "scope", "scrollamount", "scrolldelay", "scrolling", "selected",
        "shadowrootclonable", "shadowrootdelegatesfocus", "shadowrootmode", "shape", "size", "slot", "span",
        "srcdoc", "srclang", "start", "step", "style", "text", "topmargin", "truespeed", "type", "usemap", "valign",
        "value", "vlink", "vspace", "width", "wrap", "xlink:href", "xml:lang", "xml:space", "xmlns",
    };
    return !bsearch(name, builtin, sizeof builtin / sizeof *builtin, sizeof *builtin, attribute_name_cmp);
}
static bool subject_key_on(const Node *el, const char *tag, const char *k) {
    return tag == k || (el->id && !strcmp(el->id, k)) || node_has_class_atom(el, k);
}
#if defined(__ANDROID__) && !defined(NDEBUG)
static void debug_selector_shape(const Selector *s, SB *out);
static int g_html_shapes;   /* debug: selector shapes logged for invalidations of <html> */
static void debug_html_shape(const char *why, const char *key, const Selector *s, const Selector *cand) {
    if (g_html_shapes >= 12) return;
    g_html_shapes++;
    SB a, b; sb_init(&a); sb_init(&b); debug_selector_shape(s, &a); if (cand) debug_selector_shape(cand, &b);
    __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "html_shape why=%s key=%s source=%.1500s candidate=%.600s", why, key ? key : "-", a.s ? a.s : "", b.s ? b.s : "");
    sb_free(&a); sb_free(&b);
}
#endif
static bool key_bit_in(const uint64_t bits[16], const char *key) { unsigned b = dom_tree_key_bit(key); return (bits[b >> 6] >> (b & 63)) & 1; }
static void key_bit_add(uint64_t bits[16], const char *key) { if (key) { unsigned b = dom_tree_key_bit(key); bits[b >> 6] |= UINT64_C(1) << (b & 63); } }
/* Marks the elements whose match can flip with the pending attributes, in one tree walk. */
static void attribute_subjects_flush(StyleEngine *e) {
    int n = e->npending_attrs; e->npending_attrs = 0;
    if (!n) return;
    HovDesc *subjects[16]; AttributeCandidates *candidates[16]; int live = 0, live_slot[16];
    for (int k = 0; k < n; k++) {   /* the index may have been rebuilt since */
        HovDesc *s = hm_get(&e->idx.attribute_subjects, e->pending_attrs[k]);
        if (s) { subjects[live] = s; live_slot[live] = k; candidates[live++] = hm_get(&e->idx.attribute_candidates, e->pending_attrs[k]); }
    }
    Document *d = e->doc;
    if (live) for (Node *el = d->node.first; el; el = node_next_deep(el, &d->node)) {
        if (el->type != NODE_ELEMENT || (el->flags & NF_STYLE_DIRTY)) continue;
        const char *tag = el->ns == NS_HTML ? el->tag : atom_lower(el->tag, strlen(el->tag));
        for (int k = 0; k < live; k++) {
            bool affected = subjects[k]->univ;
            for (int i = 0; !affected && i < subjects[k]->keys.n; i++) affected = subject_key_on(el, tag, subjects[k]->keys.v[i]);
            if (!affected) continue;
            bool match = !candidates[k]; /* conservative fallback */
            const uint64_t *bearers = e->pending_bearers[live_slot[k]];
            for (int i = 0; candidates[k] && !match && i < candidates[k]->n; i++)   /* a keyed subject skips other elements */
                match = (!candidates[k]->v[i].bearer || key_bit_in(bearers, candidates[k]->v[i].bearer)) &&
                    (!candidates[k]->v[i].subject || subject_key_on(el, tag, candidates[k]->v[i].subject)) &&
                    css_match_selector(&candidates[k]->v[i].candidate, el, candidates[k]->v[i].source->scope);
            if (match) {
#if defined(__ANDROID__) && !defined(NDEBUG)
                if (el == d->html) for (int i = 0; candidates[k] && i < candidates[k]->n; i++)
                    if ((!candidates[k]->v[i].bearer || key_bit_in(bearers, candidates[k]->v[i].bearer)) && (!candidates[k]->v[i].subject || subject_key_on(el, tag, candidates[k]->v[i].subject)) && css_match_selector(&candidates[k]->v[i].candidate, el, candidates[k]->v[i].source->scope)) {
                        debug_html_shape("attr", e->pending_attrs[k], &candidates[k]->v[i].source->sel, &candidates[k]->v[i].candidate); break;
                    }
#endif
                mark_style_subject(el, e->pending_attrs[k]); break;
            }
        }
    }
}
static void attribute_subjects_queue(StyleEngine *e, Node *n, const char *key);
/* Whether a selector can test the attribute name on n: n has the id, class or type of
 * some compound testing it. Other elements' edits of it cannot change any match. */
static bool attribute_borne(StyleEngine *e, const Node *n, const char *key) {
    const HovDesc *b = hm_get(&e->idx.attribute_bearers, key);
    if (!b) return false;
    if (b->univ || n->ns != NS_HTML) return true;
    for (int i = 0; i < b->keys.n; i++) if (subject_key_on(n, n->tag, b->keys.v[i])) return true;
    return false;
}
bool css_attribute_observed(Node *n, const char *name) {
    Document *d = n->doc;
    StyleEngine *e = d ? d->stylesheets : NULL;
    if (!e) return true;
    idx_ensure(e);
#if defined(__ANDROID__) && !defined(NDEBUG)
    bool borne = attribute_borne(e, n, atom_lower(name, strlen(name)));
    static int shown;   /* debug: why an attribute edit counts as observed */
    if (borne && shown < 8) {
        shown++;
        const HovDesc *b = hm_get(&e->idx.attribute_bearers, atom_lower(name, strlen(name)));
        __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "attr_observed name=%s tag=%s ns=%d classes=%d univ=%d keys=%d k0=%s k1=%s k2=%s",
            name, n->tag ? n->tag : "-", (int)n->ns, n->nclasses, b ? b->univ : -1, b ? b->keys.n : -1,
            b && b->keys.n > 0 ? b->keys.v[0] : "-", b && b->keys.n > 1 ? b->keys.v[1] : "-", b && b->keys.n > 2 ? b->keys.v[2] : "-");
    }
    return borne;
#else
    return attribute_borne(e, n, atom_lower(name, strlen(name)));
#endif
}
bool node_attribute_style_changed(Node *n, const char *name, bool value_only) {
    Document *d = n->doc;
    StyleEngine *e = d ? d->stylesheets : NULL;
    if (e) idx_ensure(e);
    /* A pending sheet change needs no fallback: the current index covers the
     * current rules and sheet_delta marks every subject of the new ones. */
    if (!e || n->ns != NS_HTML || !(n->flags & NF_CONNECTED) ||
            !(value_only || css_selector_only_attribute(name))) return false;
    attribute_subjects_queue(e, n, atom_lower(name, strlen(name)));
    return true;
}
/* Inline style edits skip selector invalidation, so the style attribute's selector tests are
 * invalidated like other attributes': presence tests (`:not([style])`) when it comes or goes, value
 * tests (`[style*=x]`) on every edit. Returns whether such a test may have flipped. */
bool node_style_attribute_changed(Node *n, bool presence);
bool node_style_attribute_changed(Node *n, bool presence) {
    Document *d = n->doc;
    StyleEngine *e = d ? d->stylesheets : NULL;
    if (!e) return false;
    idx_ensure(e);
    const char *key = atom_lower("style", 5);
    if (!presence && !e->idx.style_values) return false;
    if (!hm_get(&e->idx.attribute_subjects, key) && !hm_get(&e->idx.self_attributes, key)) return false;
    if (n->flags & NF_CONNECTED) attribute_subjects_queue(e, n, key);
    return true;
}
static void attribute_subjects_queue(StyleEngine *e, Node *n, const char *key) {
    HovDesc *subjects = hm_get(&e->idx.attribute_subjects, key);
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (e->debug_style_passes) __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style attribute name=%s self=%d relative=%d universal=%d\n", key, hm_get(&e->idx.self_attributes, key) != NULL, subjects ? subjects->keys.n : 0, subjects ? subjects->univ : 0);
#endif
    /* Candidates ignore the attribute's value, so one walk at the next pass
     * covers every set before it; other edits in between invalidate themselves. */
    if (subjects && attribute_borne(e, n, key)) {
        int i = 0; while (i < e->npending_attrs && e->pending_attrs[i] != key) i++;
        if (i == e->npending_attrs) {
            if (i == 16) { attribute_subjects_flush(e); i = 0; }
            e->pending_attrs[e->npending_attrs++] = key;
            memset(e->pending_bearers[i], 0, sizeof e->pending_bearers[i]);
        }
        uint64_t *bits = e->pending_bearers[i];
        if (n->ns != NS_HTML) memset(bits, 0xff, sizeof e->pending_bearers[i]);
        else { key_bit_add(bits, n->tag); key_bit_add(bits, n->id); for (int c = 0; c < n->nclasses; c++) key_bit_add(bits, n->classes[c]); }
    }
    if (!n->style || hm_get(&e->idx.self_attributes, key) || (n->style->attribute_dependencies & custom_bit(key))) mark_style_subject(n, key);
}
/* Index one sheet's rules after everything indexed so far in the cascade. */
static void idx_add_sheet(StyleEngine *e, StyleSheet *sh) {
    if (sh->disabled || sh->media_off) return;
    unsigned fw_bit = dom_tree_key_bit(":focus-within");
    {
        for (int i = 0; i < sh->rules.n; i++) {
            Rule *r = &sh->rules.v[i];
            if (!r->filters_ready) {   /* sheets come and go; rebuilding the index keeps these */
                prepare_nested_filters(&r->sel);
                const char *required = required_class(&r->sel, false);
                const char *ancestor = required_class(&r->sel, true);
                r->ancestor_class_filter2 = ancestor && ancestor != required ? (uint16_t)class_filter_bit(ancestor) : 1024;
                r->ancestor_class_filter = required ? (uint16_t)class_filter_bit(required) : 1024;
                r->filters_ready = true;
            }
            r->order = (uint64_t)sh->rank << 32 | (uint64_t)LMIN(i, 0xffffff) << 8;
            r->scope = tree_scope(sh->owner);
            if (sh->owner && !r->scope && sel_has_host(&r->sel)) continue;
            SubjectKey key = subject_key(&r->sel);
            LocalKey local = key.name ? (LocalKey){0} : local_key(&r->sel);
            if (local.kind == 1) continue;
            index_attribute_subjects(&e->idx, &r->sel, key.name, false, r);
            if (r->sel.reads_style_attribute) e->idx.style_values = true;
            if (!e->idx.focus_within && sel_uses_pseudo(&r->sel, "focus-within")) e->idx.focus_within = true;
            uint64_t reselect[16] = {0};
            reselect_dependencies(&r->sel, reselect, true);
            /* :has(:focus-within) is no reselect key, but its subjects flip as well */
            if (sel_uses_pseudo(&r->sel, "focus-within") && ((reselect[fw_bit >> 6] >> (fw_bit & 63) & 1) || fw_needs_more(&r->sel))) {
                SubjectKey fk = subject_key(&r->sel);
                const char *fkey = !fk.name ? NULL : fk.kind == SK_TYPE ? atom_lower(fk.name, strlen(fk.name)) : fk.name;
                if (r->scope) e->idx.fw_complex = true;
                else if (fw_needs_more(&r->sel)) {
                    vec_push(e->idx.fw_far, r); vec_push(e->idx.fw_far_keys, fkey);
                    if (!fkey && ++e->idx.fw_far_unkeyed > 8) e->idx.fw_complex = true;
                } else { vec_push(e->idx.fw_rules, r); vec_push(e->idx.fw_keys, fkey); }
            }
            SubjectKey keys[8]; int nkeys = key.name ? 0 : subject_keys(&r->sel, keys);
            r->multi_key = nkeys > 1;
            if (key.name) { keys[0] = key; nkeys = 1; }
            for (int j = 0; j < nkeys; j++)
                reselect_subjects_add(&e->idx, reselect, dom_tree_key_bit(keys[j].kind == SK_TYPE ? atom_lower(keys[j].name, strlen(keys[j].name)) : keys[j].name));
            if (!nkeys) reselect_subjects_add(&e->idx, reselect, local.kind == 3 ? dom_tree_key_bit(local.name) : 1024);
            unsigned tree = tree_dependencies(&r->sel);
            if (tree & 1) for (int j = 0; j < r->sel.n; j++) {
                Compound *c = &r->sel.c[j];
                unsigned scope = c->comb == CB_ADJ || c->comb == CB_SIB ? 1 : 0;
                for (int k = 0; k < c->n; k++) scope |= tree_local_scope(&c->s[k]);
                if (!scope) continue;
                TreeDependency dependency = {tree_candidate(&e->idx, &r->sel, false, j, scope), r->scope, false, key.name, key.kind, &r->sel};
                selector_change_dependencies(&r->sel, dependency.selector_keys, true);
                tree_parent_keys(&dependency, &r->sel);
                vec_push(e->idx.tree_candidates, dependency);
            }
            if (tree & 2) for (int j = 0; j < r->sel.n; j++) {
                unsigned scope = 0;
                for (int k = 0; k < r->sel.c[j].n; k++) scope |= tree_global_scope(&r->sel.c[j].s[k]);
                if (!scope) continue;
                /* Separate candidates form a union: a change to either of
                 * two :has compounds suffices, not necessarily both. */
                TreeDependency dependency = {.candidate = tree_candidate(&e->idx, &r->sel, true, j, scope),
                    .scope = r->scope, .global = true, .subject = key.name, .subject_kind = key.kind, .source = &r->sel,
                    .witness_filter = true, .focus_tree_filter = true};
                selector_change_dependencies(&r->sel, dependency.selector_keys, true);
                has_argument_keys(&r->sel, e->idx.has_keys, false);
                for (int k = 0; k < r->sel.c[j].n; k++) {
                    SimpleSel *part = &r->sel.c[j].s[k];
                    if (!(tree_simple_dependencies(part) & 2)) continue;
                    if (!tree_witness_keys(part, dependency.witness_keys)) dependency.witness_filter = false;
                    if (!tree_focus_only(part)) dependency.focus_tree_filter = false;
                }
                tree_parent_keys(&dependency, &r->sel);
                vec_push(e->idx.tree_candidates, dependency);
            }
            if (empty_dependencies(&r->sel) & 2) {
                EmptyDependency dependency = {empty_candidate(&r->sel), r->scope, -1, 0, false};
                empty_classify(&r->sel, &dependency);
                vec_push(e->idx.empty_candidates, dependency);
                if (dependency.mode) {}
                else if (!key.name) e->idx.empty_universal = true;
                else if (key.kind == SK_ID) hm_put(&e->idx.empty_ids, key.name, (void *)1);
                else if (key.kind == SK_CLASS) hm_put(&e->idx.empty_classes, key.name, (void *)1);
                else if (key.kind == SK_TYPE) hm_put(&e->idx.empty_tags, key.name, (void *)1);
            }
            if (nkeys) for (int j = 0; j < nkeys; j++)
                idx_add(keys[j].kind == SK_ID ? &e->idx.by_id : keys[j].kind == SK_CLASS ? &e->idx.by_class :
                    keys[j].kind == SK_ATTR ? &e->idx.by_attr : &e->idx.by_tag, keys[j].name, r);
            else if (local.kind == 2) vec_push(e->idx.root_rules, r);
            else if (local.kind == 3) idx_add(&e->idx.by_attr, local.name, r);
            else if (local.kind == 4) idx_add(&e->idx.by_parent_tag, local.name, r);
            else if (local.kind == 5) idx_add(&e->idx.by_parent_class, local.name, r);
            else if (local.kind == 6) idx_add(&e->idx.by_parent_attr, local.name, r);
            else if (r->ancestor_class_filter < 1024) {
                unsigned bit = r->ancestor_class_filter;
                RuleVec *bucket = e->idx.ancestor_buckets[bit];
                if (!bucket) e->idx.ancestor_buckets[bit] = bucket = xcalloc(1, sizeof *bucket);
                vec_push(*bucket, r);
                e->idx.ancestor_bucket_bits[bit >> 6] |= UINT64_C(1) << (bit & 63);
            } else vec_push(e->idx.universal, r);
            e->idx.count++;
            hov_skey = cmp_key(&r->sel.c[r->sel.n - 1]); hov_scan(&e->idx, &r->sel, 0, NULL);
            sib_scan(&e->idx, &r->sel, false);
        }
    }
}
static void idx_build(StyleEngine *e) {
    idx_clear(&e->idx);
    for (int i = 0; i < e->dead_sheets.n; i++) css_sheet_free(e->dead_sheets.v[i]);
    e->dead_sheets.n = 0;
    uint32_t gap = (uint32_t)(UINT32_MAX / ((uint32_t)e->sheets.n + 1));
    for (int si = 0; si < e->sheets.n; si++) { e->sheets.v[si]->rank = gap * (uint32_t)(si + 1); idx_add_sheet(e, e->sheets.v[si]); }
    e->idx_dirty = false; e->idx_changed = true;
}

static void sheet_delta_clear(StyleEngine *e);
StyleEngine *style_engine_new(Document *d) {
    StyleEngine *e = xcalloc(1, sizeof *e);
    e->doc = d; d->stylesheets = e; e->media.vw = 1280; e->media.vh = 800; e->media.dpr = 2;
    StyleSheet *ua = css_parse_sheet(css_ua_sheet, strlen(css_ua_sheet), NULL, 0, &e->media);
    vec_push(e->sheets, ua);
    e->idx_dirty = true; e->delta_all = true;
    return e;
}
void style_engine_free(StyleEngine *e) { if (e->doc->stylesheets == e) e->doc->stylesheets = NULL; for (int i = 0; i < e->sheets.n; i++) css_sheet_free(e->sheets.v[i]); vec_free(e->sheets); for (int i = 0; i < e->dead_sheets.n; i++) css_sheet_free(e->dead_sheets.v[i]); vec_free(e->dead_sheets); idx_clear(&e->idx); sheet_delta_clear(e); css_match_cache_free(e->match_cache); free(e->moves); free(e); }
static bool compound_has_root(const Compound *c) {
    for (int k = 0; k < c->n; k++) if (is_pseudo(&c->s[k], "root")) return true;
    return false;
}
/* `:is(.a, .b) > *` or `:is(.a, .b) *`: the subject's parent (or an ancestor) carries
 * one of the keys of the compound left of it; `.a + *` or `.a ~ *`: a previous sibling does. */
static bool sheet_delta_parent_keys(StyleEngine *e, const Selector *sel) {
    if (sel->n < 2) return false;
    uint8_t comb = sel->c[sel->n - 1].comb;
    Selector up = {.c = (Compound *)&sel->c[sel->n - 2], .n = 1};
    SubjectKey keys[8], one = subject_key(&up); int n = one.name ? 1 : subject_keys(&up, keys);
    if (one.name) keys[0] = one;
    for (int j = 0; j < n; j++) if (keys[j].kind == SK_ID || (comb != CB_CHILD && keys[j].kind != SK_CLASS)) return false;
    for (int j = 0; j < n; j++) {
        if (comb == CB_ADJ || comb == CB_SIB) hm_put(comb == CB_ADJ ? &e->delta_adj_classes : &e->delta_sib_classes, keys[j].name, (void *)1);
        else if (comb == CB_DESC) { unsigned bit = class_filter_bit(keys[j].name); e->delta_ancestors[bit >> 6] |= UINT64_C(1) << (bit & 63); }
        else hm_put(keys[j].kind == SK_CLASS ? &e->delta_parent_classes : keys[j].kind == SK_ATTR ? &e->delta_parent_attrs :
            &e->delta_parent_tags, keys[j].name, (void *)1);
    }
    return n > 0;
}
/* `:root[data-reduced-motion] *` matches nothing while the root lacks the
 * attribute; a later root change reselects through the index as usual. */
static bool sheet_delta_root_fails(StyleEngine *e, const Rule *r) {
    if (r->sel.n < 2 || r->scope || !e->doc->html || !compound_has_root(&r->sel.c[0])) return false;
    Selector prefix = r->sel; prefix.n = 1; prefix.pseudo_el = 0;
    return !css_match_selector(&prefix, e->doc->html, NULL);
}
/* Record the index keys of a sheet's rules, mirroring matched_decls' buckets. */
static void sheet_delta(StyleEngine *e, const StyleSheet *s) {
    e->delta_any = true;
    if (s->disabled || e->delta_all) return;
    for (int i = 0; i < s->rules.n && !e->delta_all; i++) {
        const Rule *r = &s->rules.v[i];
        if (!r->sel.n || r->sel.pseudo_el == 3) continue;   /* pseudo-elements never styled (::view-transition-*, ::selection) */
        SubjectKey key = subject_key(&r->sel), keys[8];
        LocalKey local; int nkeys;
        if (!key.name && (nkeys = subject_keys(&r->sel, keys))) {
            for (int j = 0; j < nkeys; j++)
                hm_put(keys[j].kind == SK_ID ? &e->delta_ids : keys[j].kind == SK_CLASS ? &e->delta_classes :
                    keys[j].kind == SK_ATTR ? &e->delta_attrs : &e->delta_tags, keys[j].name, (void *)1);
        }
        else if (key.name && key.kind == SK_ID) hm_put(&e->delta_ids, key.name, (void *)1);
        else if (key.name && key.kind == SK_CLASS) hm_put(&e->delta_classes, key.name, (void *)1);
        else if (key.name && key.kind == SK_TYPE) hm_put(&e->delta_tags, key.name, (void *)1);
        else if (compound_has_root(&r->sel.c[r->sel.n - 1])) e->delta_root = true;
        else if ((local = local_key(&r->sel)).kind == 1) continue;
        else if (local.kind == 3) hm_put(&e->delta_attrs, local.name, (void *)1);
        else if (local.kind == 4) hm_put(&e->delta_parent_tags, local.name, (void *)1);
        else if (local.kind == 5) hm_put(&e->delta_parent_classes, local.name, (void *)1);
        else if (local.kind == 6) hm_put(&e->delta_parent_attrs, local.name, (void *)1);
        else {
            const char *required = required_class(&r->sel, true);
            if (!required && (sheet_delta_parent_keys(e, &r->sel) || sheet_delta_root_fails(e, r))) continue;
            if (!required) { e->delta_all = true; e->dbg_delta_all_rule = r; break; }
            unsigned bit = class_filter_bit(required);
            e->delta_ancestors[bit >> 6] |= UINT64_C(1) << (bit & 63);
        }
    }
}
static void sheet_delta_clear(StyleEngine *e) {
    hm_free(&e->delta_ids, NULL); hm_free(&e->delta_classes, NULL); hm_free(&e->delta_tags, NULL);
    hm_free(&e->delta_attrs, NULL); hm_free(&e->delta_parent_tags, NULL); hm_free(&e->delta_parent_classes, NULL); hm_free(&e->delta_parent_attrs, NULL);
    hm_free(&e->delta_adj_classes, NULL); hm_free(&e->delta_sib_classes, NULL);
    memset(e->delta_ancestors, 0, sizeof e->delta_ancestors);
    e->delta_any = e->delta_all = e->delta_root = false; e->dbg_delta_all_rule = NULL;
}
/* Which kind of key marks n (debug statistics), 0 for none. */
static int sheet_delta_hit(StyleEngine *e, Node *n, const uint64_t classes[16]) {
    if (n->id && hm_get(&e->delta_ids, n->id)) return 1;
    for (int i = 0; i < n->nclasses; i++) if (hm_get(&e->delta_classes, n->classes[i])) return 2;
    if (e->delta_tags.n && hm_get(&e->delta_tags, n->ns == NS_HTML ? n->tag : atom_lower(n->tag, strlen(n->tag)))) return 3;
    if (e->delta_root && n->parent && n->parent->type == NODE_DOCUMENT) return 4;
    for (int i = 0; e->delta_attrs.n && i < n->nattrs; i++)
        if (hm_get(&e->delta_attrs, atom_lower(n->attrs[i].name, strlen(n->attrs[i].name)))) return 5;
    Node *p = n->parent;
    if (e->delta_parent_tags.n && p && p->type == NODE_ELEMENT &&
            hm_get(&e->delta_parent_tags, p->ns == NS_HTML ? p->tag : atom_lower(p->tag, strlen(p->tag)))) return 6;
    if (p && p->type == NODE_ELEMENT) {
        for (int i = 0; e->delta_parent_classes.n && i < p->nclasses; i++) if (hm_get(&e->delta_parent_classes, p->classes[i])) return 7;
        for (int i = 0; e->delta_parent_attrs.n && i < p->nattrs; i++)
            if (hm_get(&e->delta_parent_attrs, atom_lower(p->attrs[i].name, strlen(p->attrs[i].name)))) return 8;
    }
    for (int k = 0; k < 16; k++) if (classes[k] & e->delta_ancestors[k]) return 9;
    return 0;
}
static bool sheet_delta_class(HMap *keys, const Node *n) {
    for (int i = 0; keys->n && i < n->nclasses; i++) if (hm_get(keys, n->classes[i])) return true;
    return false;
}
#if defined(__ANDROID__) && !defined(NDEBUG)
static int g_delta_kinds[11];   /* debug: elements per marking key kind, [0] unmarked */
#endif
/* Marks matching elements; returns whether any descendant of n was marked.
 * sibling: a previous sibling carries a class a new sibling rule follows. */
static bool sheet_delta_mark(StyleEngine *e, Node *n, const uint64_t ancestors[16], bool sibling) {
    uint64_t classes[16]; memcpy(classes, ancestors, sizeof classes);
    bool any = false;
    if (n->type == NODE_ELEMENT) {
        add_class_filter(classes, n);
        int kind = sibling ? 10 : sheet_delta_hit(e, n, classes);
        if (kind) { n->flags |= NF_STYLE_DIRTY; any = true; DBG_HTML(n, "delta"); }
#if defined(__ANDROID__) && !defined(NDEBUG)
        g_delta_kinds[kind]++;
#endif
    }
    bool below = false, after = false;
    const Node *prev = NULL;
    for (Node *c = n->first; c; c = c->next) {
        bool adj = prev && sheet_delta_class(&e->delta_adj_classes, prev);
        below |= sheet_delta_mark(e, c, classes, after || adj);
        if (c->type != NODE_ELEMENT) continue;
        after |= sheet_delta_class(&e->delta_sib_classes, c);
        prev = c;
    }
    if (n->shadow_root) below |= sheet_delta_mark(e, n->shadow_root, classes, false);
    if (below) n->flags |= NF_CHILD_STYLE_DIRTY;
    return any || below;
}

void (*css_sheet_added_hook)(StyleSheet *s);
void style_engine_add_sheet(StyleEngine *e, StyleSheet *s) {
    /* keep document order of owner nodes */
    int pos = e->sheets.n;
    if (s->owner) {
        for (int i = 1; i < e->sheets.n; i++) {
            Node *o = e->sheets.v[i]->owner; if (!o || o == s->owner) continue;   /* a sheet's appended tail goes after it */
            /* is s->owner before o in tree order? */
            bool before = false;
            for (Node *n = s->owner; n; n = node_next_deep(n, NULL)) { if (n == o) { before = true; break; } }
            if (before) { pos = i; break; }
        }
    }
    vec_push(e->sheets, s);
    memmove(&e->sheets.v[pos + 1], &e->sheets.v[pos], sizeof(StyleSheet *) * (size_t)(e->sheets.n - 1 - pos));
    if (css_sheet_added_hook) css_sheet_added_hook(s);
    e->sheets.v[pos] = s;
    sheet_delta(e, s);
    /* rank the sheet between its neighbours and extend the index; renumber everything once no rank is left between them */
    uint32_t lo = pos ? e->sheets.v[pos - 1]->rank : 0, hi = pos + 1 < e->sheets.n ? e->sheets.v[pos + 1]->rank : UINT32_MAX;
    if (!e->idx_dirty && hi > lo && hi - lo >= 2) {
        s->rank = lo + (pos == e->sheets.n - 1 ? LMIN((hi - lo) / 2, 1u << 20) : (hi - lo) / 2);   /* appends keep room for more */
        idx_add_sheet(e, s); e->idx_changed = true;
    }
    else e->idx_dirty = true;
    e->generation++;
}
void style_engine_remove_owner(StyleEngine *e, Node *owner) {
    bool removed = false;
    for (int i = 0; i < e->sheets.n; i++) if (e->sheets.v[i]->owner == owner) {
        StyleSheet *sh = e->sheets.v[i];
        sheet_delta(e, sh);
        /* small sheets (scroll locks, toggled overlays) leave dead rules in the index instead of rebuilding it */
        if (!e->idx_dirty && sh->rules.n <= 256 && e->dead_sheets.n < 64) { for (int k = 0; k < sh->rules.n; k++) sh->rules.v[k].dead = true; vec_push(e->dead_sheets, sh); }
        else { css_sheet_free(sh); e->idx_dirty = true; }
        memmove(&e->sheets.v[i], &e->sheets.v[i + 1], sizeof(StyleSheet *) * (size_t)(e->sheets.n - i - 1)); e->sheets.n--; i--;
        removed = true;
    }
    if (!removed) return;
    if (!e->idx_dirty) e->idx_changed = true;
    e->generation++;
}
void style_engine_invalidate(StyleEngine *e) { e->idx_dirty = true; e->delta_all = true; e->generation++; }

/* ---------------- cascade ---------------- */
typedef struct { Decl *d; uint64_t key, order; } MDecl;   /* key: importance and origin | specificity; then cascade order */
static int mdecl_cmp(const void *a, const void *b) {
    const MDecl *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

static bool name_in(const char *list, const char *name) {
    size_t n = strlen(name);
    for (const char *p = list; p && *p; ) {
        while (isspace((unsigned char)*p)) p++;
        const char *e = p; while (*e && !isspace((unsigned char)*e)) e++;
        if ((size_t)(e - p) == n && !strncmp(p, name, n)) return true;
        p = e;
    }
    return false;
}
static const char *cq_var(const void *ud, const char *name) { const ComputedStyle *s = ud; return s ? custom_get(s->custom, name) : NULL; }
static bool container_matches(const ContainerCond *cc, Node *el) {
    for (; cc; cc = cc->outer) {
        bool style_q = strstr(cc->query, "style(") != NULL;
        Node *a = el->parent;
        for (; a; a = a->parent) {
            if (a->type != NODE_ELEMENT || !a->style) continue;
            if (cc->name && !name_in(a->style->container_name, cc->name)) continue;
            if (style_q || a->style->container_type) break;
        }
        if (!a) return false;
        CQEnv env = { a->cq_w, a->cq_h, a->style->container_type != 0, a->style->container_type == 2, cq_var, a->style };
        if (!css_container_eval(cc->query, &env)) return false;
    }
    return true;
}

static bool sel_last_has_host(const Selector *sel) {
    const char *h = atom("host"); const Compound *c = &sel->c[sel->n - 1];
    for (int k = 0; k < c->n; k++) if (c->s[k].kind == SK_PSEUDO && c->s[k].name == h) return true;
    return false;
}
static uint64_t g_collect_stamp; /* one per matched_decls call */
static void collect(StyleEngine *e, Node *el, RuleVec *rv, int pseudo, VEC(MDecl) *out) {
    if (!rv) return;
    e->stats_rule_iter += (uint64_t)rv->n;
    for (int i = 0; i < rv->n; i++) {
        Rule *r = rv->v[i];
        if (r->sel.pseudo_el != pseudo || r->dead) continue;
        if (r->multi_key) { if (r->collect_stamp == g_collect_stamp) continue; r->collect_stamp = g_collect_stamp; }
        if (r->origin && r->scope != e->el_scope && !(r->scope && el->shadow_root == r->scope && sel_last_has_host(&r->sel))) continue;
        if (r->mq && !r->mq->matches) continue;
        unsigned bit = r->ancestor_class_filter;
        if (bit < 1024 && e->ancestor_classes && !(e->ancestor_classes[bit >> 6] & (UINT64_C(1) << (bit & 63)))) continue;
        bit = r->ancestor_class_filter2;
        if (bit < 1024 && e->ancestor_classes && !(e->ancestor_classes[bit >> 6] & (UINT64_C(1) << (bit & 63)))) continue;
        if (r->sel.reads_style_attribute) e->collect_style_value = true;
        e->stats_rule_tests++;
#if defined(__ANDROID__) && !defined(NDEBUG)
        r->dbg_tests++;
#endif
        if (!css_match_selector(&r->sel, el, NULL)) continue;
        e->stats_rule_hits++;
#if defined(__ANDROID__) && !defined(NDEBUG)
        r->dbg_hits++;
#endif
        if (r->cq) e->collect_dynamic = true;
        if (r->cq && !container_matches(r->cq, el)) continue;
        for (int k = 0; k < r->decls->n; k++) {
            Decl *d = &r->decls->v[k];
            /* key: important(1) | origin(1) | spec(30); order: sheet rank | rule | declaration */
            uint64_t imp = d->important ? 1 : 0;
            uint64_t level = imp ? (r->origin == 0 ? 3 : 2) : (r->origin == 0 ? 0 : 1);
            MDecl m = { d, (level << 62) | ((uint64_t)r->sel.spec << 32), r->order | (uint64_t)(k & 0xff) };
            vec_push(*out, m);
        }
    }
}

/* Skip entire groups of unkeyed rules whose required class cannot occur on
   this ancestry. Hash collisions only add full selector checks; never skip one. */
static void collect_universal(StyleEngine *e, Node *el, int pseudo, VEC(MDecl) *out) {
    RuleVec u = { e->idx.universal.v, e->idx.universal.n, 0 };
    collect(e, el, &u, pseudo, (void *)out);
    for (unsigned word = 0; word < 16; word++) {
        uint64_t bits = e->idx.ancestor_bucket_bits[word];
        if (e->ancestor_classes) bits &= e->ancestor_classes[word];
        while (bits) {
            unsigned bit = (unsigned)__builtin_ctzll(bits);
            e->stats_anc_iter += (uint64_t)((RuleVec *)e->idx.ancestor_buckets[word * 64 + bit])->n;
            collect(e, el, e->idx.ancestor_buckets[word * 64 + bit], pseudo, (void *)out);
            bits &= bits - 1;
        }
    }
}

/* Sorted matched declarations per element and pseudo-element. Matching only
 * changes with selector_version, the rule index or media (g_match_epoch),
 * text emptiness or custom-element definition, so inline-style restyles reuse
 * them. Results that consulted container queries or [style] are not kept.
 * Only restyled elements with a style attribute keep a memo, bounding memory. */
typedef struct MatchMemo {
    const Document *doc; uint64_t selector_version, epoch, emptiness; bool ext;
    uint8_t have; int n[4]; MDecl *v[4];
} MatchMemo;
static uint64_t g_match_epoch = 1;
static void match_memo_free(void *memo) {
    MatchMemo *m = memo; if (!m) return;
    for (int i = 0; i < 4; i++) free(m->v[i]);
    free(m);
}
static MatchMemo *match_memo_valid(Node *el) {
    MatchMemo *m = el->match_memo; const Document *d = el->doc;
    if (!m) return NULL;
    if (d && m->doc == d && m->selector_version == d->selector_version && m->epoch == g_match_epoch &&
            m->emptiness == d->text_emptiness_version && m->ext == (el->ext != NULL)) return m;
    match_memo_free(m); el->match_memo = NULL;
    return NULL;
}
/* Returns sorted matches for pseudo (0 element, 1 before, 2 after, 4 marker);
 * *owned tells the caller to free the array. */
static MDecl *matched_decls(StyleEngine *e, Node *el, int pseudo, int *count, bool *owned) {
    int slot = pseudo == 4 ? 3 : pseudo;
    MatchMemo *m = match_memo_valid(el);
    if (m && (m->have >> slot & 1)) { e->stats_memo_hits++; *count = m->n[slot]; *owned = false; return m->v[slot]; }
    VEC(MDecl) md = {0};
    RuleVec *rv;
    bool dynamic = e->collect_dynamic; e->collect_dynamic = false; e->collect_style_value = false;
    g_collect_stamp++;
    if (el->id && (rv = hm_get(&e->idx.by_id, el->id))) collect(e, el, rv, pseudo, (void *)&md);
    for (int i = 0; i < el->nclasses; i++) if ((rv = hm_get(&e->idx.by_class, el->classes[i]))) collect(e, el, rv, pseudo, (void *)&md);
    const char *tagkey = el->ns == NS_HTML ? el->tag : atom_lower(el->tag, strlen(el->tag));
    if ((rv = hm_get(&e->idx.by_tag, tagkey))) collect(e, el, rv, pseudo, (void *)&md);
    if (e->idx.by_attr.n) for (int i = 0; i < el->nattrs; i++) {
        const char *name = el->attrs[i].name;
        if (el->ns != NS_HTML || (el->flags & NF_ATTR_MIXED_CASE)) name = atom_lower(name, strlen(name));
        if ((rv = hm_get(&e->idx.by_attr, name))) collect(e, el, rv, pseudo, (void *)&md);
    }
    Node *parent = el->parent;
    if (e->idx.by_parent_tag.n && parent && parent->type == NODE_ELEMENT &&
            (rv = hm_get(&e->idx.by_parent_tag, parent->ns == NS_HTML ? parent->tag : atom_lower(parent->tag, strlen(parent->tag)))))
        collect(e, el, rv, pseudo, (void *)&md);
    if (parent && parent->type == NODE_ELEMENT) {
        for (int i = 0; e->idx.by_parent_class.n && i < parent->nclasses; i++)
            if ((rv = hm_get(&e->idx.by_parent_class, parent->classes[i]))) collect(e, el, rv, pseudo, (void *)&md);
        for (int i = 0; e->idx.by_parent_attr.n && i < parent->nattrs; i++) {
            const char *name = parent->attrs[i].name;
            if (parent->ns != NS_HTML || (parent->flags & NF_ATTR_MIXED_CASE)) name = atom_lower(name, strlen(name));
            if ((rv = hm_get(&e->idx.by_parent_attr, name))) collect(e, el, rv, pseudo, (void *)&md);
        }
    }
    if (e->idx.root_rules.n && parent && parent->type == NODE_DOCUMENT) {
        RuleVec root = { e->idx.root_rules.v, e->idx.root_rules.n, 0 };
        collect(e, el, &root, pseudo, (void *)&md);
    }
    collect_universal(e, el, pseudo, (void *)&md);
    if (md.n > 1) qsort(md.v, (size_t)md.n, sizeof(MDecl), mdecl_cmp);
    bool keep = !e->collect_dynamic && !e->collect_style_value && el->doc && el->style && node_attr_atom(el, A_style);
    e->collect_dynamic |= dynamic;
    *count = md.n; *owned = !keep;
    if (keep) {
        if (!m) {
            m = xcalloc(1, sizeof *m); el->match_memo = m;
            m->doc = el->doc; m->selector_version = el->doc->selector_version; m->epoch = g_match_epoch;
            m->emptiness = el->doc->text_emptiness_version; m->ext = el->ext != NULL;
        }
        m->v[slot] = md.v; m->n[slot] = md.n; m->have |= (uint8_t)(1u << slot);
    }
    return md.v;
}

__attribute__((weak)) Node *js_doc_frame_el(Document *d) { (void)d; return NULL; }   /* js/frames.cc */
/* HTML "maps to the pixel length property": the rules for parsing non-negative integers, as "Npx" */
static bool html_pixel_length(const char *v, char *out, size_t n) {
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\f' || *v == '\r') v++;
    bool neg = *v == '-';
    if (*v == '-' || *v == '+') v++;
    if (!isdigit((unsigned char)*v)) return false;
    long x = 0;
    for (; isdigit((unsigned char)*v); v++) x = x < 100000000 ? x * 10 + (*v - '0') : x;
    if (neg && x) return false;
    snprintf(out, n, "%ldpx", x);
    return true;
}
/* HTML's rules for parsing dimension values: leading whitespace, digits, an optional fraction, then '%' right after
   for a percentage; anything else ends the number. Without zero_ok a zero value is ignored (td/th, table width). */
static bool html_dimension(const char *v, bool zero_ok, char *out, size_t n) {
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\f' || *v == '\r') v++;
    if (*v < '0' || *v > '9') return false;
    double x = 0;
    for (; *v >= '0' && *v <= '9'; v++) x = x * 10 + (*v - '0');
    if (*v == '.') { v++; for (double f = 0.1; *v >= '0' && *v <= '9'; v++, f /= 10) x += (*v - '0') * f; }
    if (!zero_ok && x == 0) return false;
    snprintf(out, n, "%.10g%s", x, *v == '%' ? "%" : "px");
    return true;
}
static void presentational_hints(ComputedStyle *st, const ComputedStyle *par, Node *el, StyleEngine *e) {
    if (el->ns != NS_HTML) {
        if (el->ns == NS_SVG) {
            const char *w = node_attr_atom(el, A_width), *h = node_attr_atom(el, A_height), *f = node_attr_atom(el, A_fill);
            if (w && el->tag == A_svg) css_apply_decl(st, par, "width", w, e, el);
            if (h && el->tag == A_svg) css_apply_decl(st, par, "height", h, e, el);
            if (f) css_apply_decl(st, par, "fill", f, e, el);
            if (el->tag != A_svg) st->display = D_NONE + 0 == st->display ? D_NONE : st->display;
        }
        return;
    }
    const char *v;
    char dim[48];
    bool marquee = el->tag && !strcmp(el->tag, "marquee"), imgin = el->tag == A_input && (v = node_attr(el, "type")) && !strcasecmp(v, "image");
    /* Canvas attributes define its bitmap and intrinsic dimensions, not CSS sizing hints. */
    bool replaced = el->tag == A_img || el->tag == A_video || el->tag == A_iframe || el->tag == A_embed || el->tag == A_object || marquee || imgin;
    bool cell = el->tag == A_td || el->tag == A_th;
    if ((v = node_attr_atom(el, A_width)) && (replaced || cell || el->tag == A_table || el->tag == A_hr || el->tag == A_col) && html_dimension(v, !cell && el->tag != A_table, dim, sizeof dim))
        css_apply_decl(st, par, "width", dim, e, el);
    if ((v = node_attr_atom(el, A_height)) && (replaced || cell || el->tag == A_table || el->tag == A_tr) && html_dimension(v, !cell, dim, sizeof dim))
        css_apply_decl(st, par, "height", dim, e, el);
    if (el->tag == A_img || el->tag == A_embed || el->tag == A_object || marquee || imgin) {
        if ((v = node_attr(el, "hspace")) && html_dimension(v, true, dim, sizeof dim)) { css_apply_decl(st, par, "margin-left", dim, e, el); css_apply_decl(st, par, "margin-right", dim, e, el); }
        if ((v = node_attr(el, "vspace")) && html_dimension(v, true, dim, sizeof dim)) { css_apply_decl(st, par, "margin-top", dim, e, el); css_apply_decl(st, par, "margin-bottom", dim, e, el); }
    }
    if ((v = node_attr_atom(el, A_bgcolor))) css_apply_decl(st, par, "background-color", v, e, el);
    if (el->tag == A_font) { if ((v = node_attr_atom(el, A_color))) css_apply_decl(st, par, "color", v, e, el); if ((v = node_attr_atom(el, A_face))) css_apply_decl(st, par, "font-family", v, e, el); if ((v = node_attr_atom(el, A_size))) { static const char *sz[] = { "x-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large" }; int s = atoi(v); if (v[0] == '+' || v[0] == '-') s += 3; css_apply_decl(st, par, "font-size", sz[LCLAMP(s, 1, 7)], e, el); } }
    if (el->tag == A_body && (v = node_attr_atom(el, A_text_attr))) css_apply_decl(st, par, "color", v, e, el);
    if ((v = node_attr_atom(el, A_align)) && (el->tag == A_div || el->tag == A_p || el->tag == A_td || el->tag == A_th || el->tag == A_tr || el->tag == A_h1 || el->tag == A_h2 || el->tag == A_h3)) css_apply_decl(st, par, "text-align", v, e, el);
    if (el->tag == A_table && (v = node_attr_atom(el, A_align)) && str_ieq(v, "center")) { css_apply_decl(st, par, "margin-left", "auto", e, el); css_apply_decl(st, par, "margin-right", "auto", e, el); }
    if (el->tag == A_table && (v = node_attr_atom(el, A_border))) { char buf[64]; snprintf(buf, sizeof buf, "%spx solid", v); css_apply_decl(st, par, "border", buf, e, el); }
    if ((el->tag == A_img || el->tag == A_object || imgin) && (v = node_attr_atom(el, A_border)) && html_pixel_length(v, dim, sizeof dim)) {
        static const char *const side[] = { "top", "right", "bottom", "left" };
        char p[32];
        for (int i = 0; i < 4; i++) {
            snprintf(p, sizeof p, "border-%s-width", side[i]); css_apply_decl(st, par, p, dim, e, el);
            snprintf(p, sizeof p, "border-%s-style", side[i]); css_apply_decl(st, par, p, "solid", e, el);
        }
    }
    if (el->tag == A_body && el == el->doc->body) {   /* a frame's body takes its iframe/frame element's margins */
        Node *f = js_doc_frame_el(el->doc);
        if (f && f->ns == NS_HTML && (f->tag == A_iframe || f->tag == A_frame)) {
            if ((v = node_attr(f, "marginheight")) && html_pixel_length(v, dim, sizeof dim)) { css_apply_decl(st, par, "margin-top", dim, e, el); css_apply_decl(st, par, "margin-bottom", dim, e, el); }
            if ((v = node_attr(f, "marginwidth")) && html_pixel_length(v, dim, sizeof dim)) { css_apply_decl(st, par, "margin-left", dim, e, el); css_apply_decl(st, par, "margin-right", dim, e, el); }
        }
    }
    if (el->tag == A_body) {   /* marginheight/marginwidth, then the per-side legacy names win */
        static const char *const attr[] = { "marginheight", "margin-top", "marginheight", "margin-bottom", "marginwidth", "margin-left", "marginwidth", "margin-right",
                                            "topmargin", "margin-top", "leftmargin", "margin-left" };   /* rightmargin/bottommargin are ignored, as in browsers */
        for (int i = 0; i < 12; i += 2) if ((v = node_attr(el, attr[i])) && html_pixel_length(v, dim, sizeof dim)) css_apply_decl(st, par, attr[i + 1], dim, e, el);
    }
    if ((el->tag == A_td || el->tag == A_th) && (v = node_attr_atom(el, A_valign))) css_apply_decl(st, par, "vertical-align", v, e, el);
    if (el->tag == A_td || el->tag == A_th) {
        /* cellpadding from table */
        Node *t = el->parent; while (t && t->tag != A_table) t = t->parent;
        if (t && (v = node_attr_atom(t, A_cellpadding))) { char buf[32]; snprintf(buf, sizeof buf, "%spx", v); css_apply_decl(st, par, "padding", buf, e, el); }
    }
    if (el->tag == A_textarea) {   /* rows and cols: lines of text and monospace characters (a scrollbar's room beside them) */
        char buf[64]; int k;
        if ((v = node_attr(el, "rows")) && (k = atoi(v)) > 0) { snprintf(buf, sizeof buf, "%.2fem", k * 1.15); css_apply_decl(st, par, "height", buf, e, el); }
        if ((v = node_attr(el, "cols")) && (k = atoi(v)) > 0) { snprintf(buf, sizeof buf, "calc(%.2fem + 14px)", k * 0.6); css_apply_decl(st, par, "width", buf, e, el); }
    }
    if (el->tag == A_img || el->tag == A_video) { /* aspect from attributes */ const char *w = node_attr_atom(el, A_width), *h = node_attr_atom(el, A_height); if (w && h && atof(h) > 0) st->aspect_ratio = (float)(atof(w) / atof(h)); }
}

static void apply_font_declarations(ComputedStyle *st, const ComputedStyle *par, MDecl *v, int n,
                                    StyleEngine *e, Node *el, bool author);
static ComputedStyle *compute_pseudo(StyleEngine *e, Node *el, ComputedStyle *base, int which) {
    int n; bool owned;
    MDecl *md = matched_decls(e, el, which, &n, &owned);
    /* without a content declaration ::before/::after stay content:none (it is not inherited) */
    bool content = which == 4;
    for (int i = 0; i < n && !content; i++) content = !strcmp(md[i].d->prop, "content");
    if (!content) { if (owned) free(md); return NULL; }
    ComputedStyle *st = style_inherit(base);
    for (int i = 0; i < n; i++) if (md[i].d->prop[0] == '-' && md[i].d->prop[1] == '-') css_apply_decl(st, base, md[i].d->prop, md[i].d->value, e, el);
    /* Logical dimensions and edges use the pseudo's final mapping basis, regardless of
       where its declaration appears relative to the dimension declarations. */
    for (int i = 0; i < n; i++) if (flow_basis_prop(md[i].d->prop)) css_apply_decl(st, base, md[i].d->prop, md[i].d->value, e, el);
    apply_font_declarations(st, base, md, n, e, el, false);
    for (int i = 0; i < n; i++) if (!(md[i].d->prop[0] == '-' && md[i].d->prop[1] == '-') && !flow_basis_prop(md[i].d->prop) && !font_basis_prop(md[i].d->prop)) css_apply_decl(st, base, md[i].d->prop, md[i].d->value, e, el);
    if (owned) free(md);
    /* An absent pseudo can become visible when a consulted variable changes. */
    cm_or(&base->custom_dependencies, &st->custom_dependencies);
    base->attribute_dependencies |= st->attribute_dependencies;
    base->parent_sensitive |= st->parent_sensitive;
    base->inherited_dynamic |= st->inherited_dynamic;
    /* Keep computed values for explicitly styled, ungenerated pseudos;
       box construction still requires content. */
    bg_position_resolve_logical(st);
    return st;
}

/* the properties whose lengths may be unitless numbers in quirks mode (quirks spec "unitless length quirk") */
static bool quirky_length_prop(const char *p) {
    static const char *const L[] = { "background-position", "border-spacing", "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width", "border-width", "bottom", "clip", "font-size", "height", "left", "letter-spacing", "margin", "margin-top",
        "margin-right", "margin-bottom", "margin-left", "max-height", "max-width", "min-height", "min-width", "padding", "padding-top",
        "padding-right", "padding-bottom", "padding-left", "right", "text-indent", "top", "vertical-align", "width", "word-spacing", NULL };
    for (int i = 0; L[i]; i++) if (!strcmp(p, L[i])) return true;
    return false;
}
/* a top-level non-zero number without a unit ("1", "+1.5"; "0" is a valid length) */
static bool has_unitless_number(const char *v) {
    int depth = 0;
    for (const char *s = v; *s;) {
        if (*s == '(') { depth++; s++; continue; }
        if (*s == ')') { depth--; s++; continue; }
        if (is_ws((unsigned char)*s) || *s == ',' || *s == '/') { s++; continue; }
        const char *t = s; while (*s && !is_ws((unsigned char)*s) && *s != '(' && *s != ')' && *s != ',' && *s != '/') s++;
        if (depth) continue;
        const char *q = t; if (*q == '+' || *q == '-') q++;
        bool dig = false, nz = false;
        for (; q < s && ((*q >= '0' && *q <= '9') || *q == '.'); q++) if (*q != '.') { dig = true; nz |= *q != '0'; }
        if (dig && nz && q == s) return true;
    }
    return false;
}
/* quirks mode: the value with "px" after each top-level unitless number, for parsers that only take lengths */
static char *quirk_px(const char *v) {
    SB b = {0}; int depth = 0;
    for (const char *s = v; *s;) {
        if (*s == '(' || *s == ')' || is_ws((unsigned char)*s) || *s == ',' || *s == '/') { depth += *s == '(' ? 1 : *s == ')' ? -1 : 0; sb_putc(&b, *s++); continue; }
        const char *t = s; while (*s && !is_ws((unsigned char)*s) && *s != '(' && *s != ')' && *s != ',' && *s != '/') s++;
        const char *q = t; if (*q == '+' || *q == '-') q++;
        bool dig = false; for (; q < s && ((*q >= '0' && *q <= '9') || *q == '.'); q++) dig |= *q != '.';
        for (const char *c = t; c < s; c++) sb_putc(&b, *c);
        if (!depth && dig && q == s) sb_puts(&b, "px");
    }
    if (!b.s) sb_puts(&b, "");
    return b.s;
}
/* author declarations: a unitless length is invalid outside quirks mode, and an escaped one always is */
static bool unitless_invalid(const char *p, const char *v, Node *el) {
    if (!quirky_length_prop(p)) return false;
    if (el && el->doc && el->doc->quirks) return strchr(v, '\\') != NULL;
    return has_unitless_number(v);
}
static void apply_font_declarations(ComputedStyle *st, const ComputedStyle *par, MDecl *v, int n,
                                    StyleEngine *e, Node *el, bool author) {
    FontPhase pending = { .style = st }, *previous = font_phase;
    font_phase = &pending;
    for (int i = 0; i < n; i++) {
        const char *p = v[i].d->prop;
        if (font_basis_prop(p) && !(author && unitless_invalid(p, v[i].d->value, el)))
            css_apply_decl(st, par, p, v[i].d->value, e, el);
    }
    font_phase = previous;
    if (pending.value) css_apply_decl(st, par, "line-height", pending.value, e, el);
    free(pending.owned);
}
/* This new logical/physical family observes the priority already carried by
 * MDecl and Decl. It does not reorder unrelated properties or introduce a
 * cascade/layer model. Inline important declarations sit above author rules
 * and below UA important rules. Project all only onto these two slots. */
static bool contain_intrinsic_family(const char *prop) {
    return contain_intrinsic_property(prop) != -2 || !strcmp(prop, "all");
}
static void apply_contain_intrinsic_declarations(ComputedStyle *st, const ComputedStyle *par,
        MDecl *v, int n, StyleEngine *e, Node *el) {
    for (int phase = 0; phase < 3; phase++) for (int i = 0; i < n; i++) {
        const Decl *d = v[i].d;
        if (!contain_intrinsic_family(d->prop)) continue;
        int priority = (v[i].key >> 62) == 3 ? 2 : !v[i].key && d->important ? 1 : 0;
        if (priority != phase) continue;
        /* all follows its ordinary substitution/CSS-wide validation path;
           that path projects only the new intrinsic-size slots. */
        css_apply_decl(st, par, d->prop, d->value, e, el);
    }
}
static void apply_decl_ordered(ComputedStyle *st, const ComputedStyle *par, MDecl *v, int n, StyleEngine *e, Node *el, bool author) {
    /* Dependencies precede their consumers: custom values, writing-mode/direction/text-orientation,
       font-size (em basis), color, then the remaining cascade in its order. */
    CustomProps *origin_base = custom_ref(st->custom);
    const CustomProps *previous_origin = custom_origin_base;
    custom_origin_base = origin_base;
    custom_defer = true;
    for (int i = 0; i < n; i++) if (v[i].d->prop[0] == '-' && v[i].d->prop[1] == '-') css_apply_decl(st, par, v[i].d->prop, v[i].d->value, e, el);
    custom_defer = false;
    /* Resolve every winning local against one immutable map, then commit.
     * Inherited values are already computed; revert uses the earlier origin. */
    custom_resolve_values(st, par, origin_base);
    custom_origin_base = previous_origin;
    custom_unref(origin_base);
    /* CSS Logical Properties requires the element's computed writing mode
       before corresponding logical/physical dimensions cascade together. */
    for (int i = 0; i < n; i++) if (flow_basis_prop(v[i].d->prop)) css_apply_decl(st, par, v[i].d->prop, v[i].d->value, e, el);
    apply_font_declarations(st, par, v, n, e, el, author);
    for (int i = 0; i < n; i++) { const char *p = v[i].d->prop; if (!strcmp(p, "color")) css_apply_decl(st, par, p, v[i].d->value, e, el); }
    bool intrinsic_family = false;
    for (int i = 0; i < n; i++) {
        const char *p = v[i].d->prop;
        if ((p[0] == '-' && p[1] == '-') || flow_basis_prop(p) || font_basis_prop(p) || !strcmp(p, "color")) continue;
        if (contain_intrinsic_family(p)) {
            intrinsic_family = true;
            if (strcmp(p, "all")) continue;
        }
        if (author && unitless_invalid(p, v[i].d->value, el)) continue;
        if (author && el && el->doc && el->doc->quirks && quirky_length_prop(p) && has_unitless_number(v[i].d->value)) {
            char *q = quirk_px(v[i].d->value); css_apply_decl(st, par, p, q, e, el); free(q); continue;
        }
        css_apply_decl(st, par, p, v[i].d->value, e, el);
    }
    if (intrinsic_family) apply_contain_intrinsic_declarations(st, par, v, n, e, el);
    bg_position_resolve_logical(st);
}

#if defined(__ANDROID__) && !defined(NDEBUG)
/* Debug Android: the slowest restyled elements of a pass and where their time went (tags only). */
typedef struct { double ms, compute, pseudo, hook, rest, decls; const char *tag; int ncls; uint64_t has_tests, has_nodes; } DbgSlowEl;
static double g_decls_ms;   /* matched_decls time of the current element */
static DbgSlowEl g_slow_el[3];
static double g_el_total;   /* all restyled elements' own time */
static double dbg_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void dbg_slow_el(const Node *n, double ms, double compute, double pseudo, double hook, const uint64_t has0[2]) {
    g_el_total += ms;
    int k = 3; while (k > 0 && g_slow_el[k - 1].ms < ms) k--;
    if (k >= 3) return;
    memmove(&g_slow_el[k + 1], &g_slow_el[k], sizeof *g_slow_el * (size_t)(2 - k));
    g_slow_el[k] = (DbgSlowEl){ms, compute, pseudo, hook, ms - compute - pseudo - hook, g_decls_ms, n->tag, n->nclasses, css_has_stats[0] - has0[0], css_has_stats[1] - has0[1]};
}
#endif
static ComputedStyle *compute(StyleEngine *e, Node *el, const ComputedStyle *par) {
    int mn; bool owned;
#if defined(__ANDROID__) && !defined(NDEBUG)
    double dbg_d0 = dbg_now();
#endif
    MDecl *md = matched_decls(e, el, 0, &mn, &owned);
#if defined(__ANDROID__) && !defined(NDEBUG)
    g_decls_ms = dbg_now() - dbg_d0;
#endif
    ComputedStyle *st = style_inherit(par);
    /* split normal / important so presentational hints & inline style go between */
    int split = 0; while (split < mn && (md[split].key >> 62) < 2) split++;
    /* hints sit above the UA sheet's normal declarations and below the author's (css-cascade "presentational hints") */
    int ua = 0; while (ua < split && (md[ua].key >> 62) == 0) ua++;
    if (ua) apply_decl_ordered(st, par, md, ua, e, el, false);
    presentational_hints(st, par, el, e);
    /* one pass so var() in sheet custom properties sees inline custom properties */
    VEC(MDecl) all = {0};
    for (int i = ua; i < split; i++) vec_push(all, md[i]);
    const char *inl = node_attr_atom(el, A_style);
    if (inl) {
        if (!el->inline_style_src || strcmp(el->inline_style_src, inl)) { free(el->inline_style_src); el->inline_style_src = xstrdup(inl); css_decls_free(el->inline_decls); el->inline_decls = css_parse_decls(inl, strlen(inl)); }
        DeclList *dl = el->inline_decls;
        for (int i = 0; i < dl->n; i++) { MDecl m = { &dl->v[i], 0, 0 }; vec_push(all, m); }
    }
    if (el->anim_decls) { DeclList *dl = el->anim_decls; for (int i = 0; i < dl->n; i++) { MDecl m = { &dl->v[i], 0, 0 }; vec_push(all, m); } }
    for (int i = split; i < mn; i++) vec_push(all, md[i]);
    apply_decl_ordered(st, par, all.v, all.n, e, el, true);
    vec_free(all);
    if (owned) free(md);
    /* fixups */
    if (st->line_height_factor > 0) st->line_height = st->line_height_factor * st->font_size;
    if (st->position == P_ABSOLUTE || st->position == P_FIXED || st->float_ != F_NONE) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK || st->display == D_TABLE_CELL || st->display == D_TABLE_ROW) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_GRID) st->display = D_GRID;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    if (st->position == P_ABSOLUTE || st->position == P_FIXED) st->float_ = F_NONE;
    /* css-overflow-3: visible/clip compute to auto/hidden beside a scrollable axis */
    bool ox = st->overflow_x == OV_VISIBLE || st->overflow_x == OV_CLIP, oy = st->overflow_y == OV_VISIBLE || st->overflow_y == OV_CLIP;
    if (ox != oy) {
        uint8_t *v = ox ? &st->overflow_x : &st->overflow_y;
        *v = *v == OV_CLIP ? OV_HIDDEN : OV_AUTO;
    }
    if (el->parent && el->parent->type == NODE_DOCUMENT && (st->display == D_INLINE || st->display == D_CONTENTS)) st->display = D_BLOCK;
    if (par && (par->display == D_FLEX || par->display == D_INLINE_FLEX || par->display == D_GRID || par->display == D_INLINE_GRID)) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_GRID) st->display = D_GRID;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    for (int i = 0; i < 4; i++) if (st->border_style[i] == BS_NONE || st->border_style[i] == BS_HIDDEN) st->border_width[i] = 0;
    if (st->outline_style == BS_NONE) st->outline_width = 0;
    if (node_has_attr(el, "inert")) st->inert = 1;   /* the UA [inert] rule makes the attribute restyle the element */
    if (st->inert) { st->pointer_events = 0; st->user_select = 1; }
    if (node_has_attr(el, "hidden") && el->ns == NS_HTML && st->display != D_NONE) {
        const char *hv = node_attr(el, "hidden"); if (!hv || !str_ieq(hv, "until-found")) { /* author CSS can override display; UA rule already sets none */ }
    }
    return st;
}

static bool str_same(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }
/* true when a and b differ only in properties that are resolved at paint time */
static bool paint_only(const ComputedStyle *a, const ComputedStyle *b) {
    if (!a || !b) return a == b;
    if (a->has_transform != b->has_transform || !str_same(a->content, b->content) || !str_same(a->grid_areas, b->grid_areas) || !str_same(a->grid_area, b->grid_area)) return false;
    if (!str_same(a->anchor_name, b->anchor_name) || !str_same(a->pos_anchor, b->pos_anchor) || !str_same(a->pos_area, b->pos_area) || !str_same(a->pos_try, b->pos_try)) return false;
    for (int i = 0; i < 4; i++) if (!str_same(a->anchor_ins[i], b->anchor_ins[i]) || (i < 2 && !str_same(a->anchor_sz[i], b->anchor_sz[i]))) return false;
    if (a->grid_ncols != b->grid_ncols || a->grid_nrows != b->grid_nrows) return false;
    if (a->grid_ncols && memcmp(a->grid_cols, b->grid_cols, sizeof *a->grid_cols * (size_t)a->grid_ncols)) return false;
    if (a->grid_nrows && memcmp(a->grid_rows, b->grid_rows, sizeof *a->grid_rows * (size_t)a->grid_nrows)) return false;
    static const char *const abs_align_props[] = { "align-self", "align-items", "align-content",
        "justify-self", "justify-items", "justify-content", "grid-auto-columns", "grid-auto-rows" };
    for (size_t i = 0; i < sizeof abs_align_props / sizeof *abs_align_props; i++)
        if (!str_same(custom_get(a->gen_n, abs_align_props[i]), custom_get(b->gen_n, abs_align_props[i]))) return false;
    if (!paint_only(a->before, b->before) || !paint_only(a->after, b->after) || !paint_only(a->marker, b->marker)) return false;
    ComputedStyle x = *a, y = *b;
#define PO_Z(f) (memset(&x.f, 0, sizeof x.f), memset(&y.f, 0, sizeof y.f))
    PO_Z(anchor_name); PO_Z(pos_anchor); PO_Z(pos_area); PO_Z(pos_try); PO_Z(anchor_ins); PO_Z(anchor_sz); PO_Z(refs); PO_Z(grid_cols); PO_Z(grid_rows); PO_Z(content); PO_Z(grid_areas); PO_Z(grid_area); PO_Z(before); PO_Z(after); PO_Z(custom); PO_Z(gen_i); PO_Z(gen_n);
    PO_Z(custom_dependencies); PO_Z(attribute_dependencies); PO_Z(parent_sensitive); PO_Z(inherited_dynamic); PO_Z(indep_set);
    PO_Z(color); PO_Z(bg_color); PO_Z(bg_image); PO_Z(bg_gradient); PO_Z(bg_repeat); PO_Z(bg_origin); PO_Z(bg_clip); PO_Z(bg_color_clip); PO_Z(bg_layers); PO_Z(bg_size_kind); PO_Z(bg_size); PO_Z(bg_pos);
    PO_Z(mask_image); PO_Z(mask_fit); PO_Z(border_color); PO_Z(outline_style); PO_Z(outline_width); PO_Z(outline_offset); PO_Z(outline_color);
    PO_Z(box_shadow); PO_Z(has_shadow); PO_Z(text_shadow); PO_Z(has_text_shadow); PO_Z(text_decoration);
    PO_Z(opacity); PO_Z(transform); PO_Z(transform_origin); PO_Z(translate_pending); PO_Z(visibility); PO_Z(z_index); PO_Z(z_auto); PO_Z(isolation);
    PO_Z(cursor); PO_Z(pointer_events); PO_Z(inert); PO_Z(user_select); PO_Z(touch_block); PO_Z(caret_color); PO_Z(fill); PO_Z(stroke); PO_Z(stroke_width); PO_Z(object_fit);
    PO_Z(filter_blur); PO_Z(filter_brightness); PO_Z(backdrop_blur);
    PO_Z(anim_name); PO_Z(anim_dur); PO_Z(anim_delay); PO_Z(anim_iter); PO_Z(tr_prop); PO_Z(tr_dur); PO_Z(tr_delay);
#undef PO_Z
    return !memcmp(&x, &y, sizeof x);
}
/* Differs at most in paint-only properties and inset (geometry: a translation). */
static bool inset_only(const ComputedStyle *a, const ComputedStyle *b) {
    ComputedStyle y = *b;
    memcpy(y.inset, a->inset, sizeof y.inset);
    return paint_only(a, &y);
}
#if defined(__ANDROID__) && !defined(NDEBUG)
/* Property names only: no values. Explains why a restyle forced layout. */
static void debug_layout_change(const ComputedStyle *a, const ComputedStyle *b, const Node *node) {
    char out[512]; size_t n = 0; out[0] = 0;
#define F(f) if (n < sizeof out - 40 && memcmp(&a->f, &b->f, sizeof a->f)) n += (size_t)snprintf(out + n, sizeof out - n, " %s", #f);
    F(display) F(position) F(float_) F(clear) F(overflow_x) F(overflow_y) F(box_sizing) F(text_align) F(white_space) F(text_transform) F(font_style) F(list_style) F(flex_direction) F(flex_wrap) F(justify_content) F(align_items) F(align_self) F(align_content) F(justify_items) F(justify_self) F(vertical_align) F(text_overflow) F(word_break) F(overflow_wrap) F(direction) F(border_style) F(appearance) F(table_layout) F(border_collapse) F(container_type) F(resize) F(writing_mode) F(container_name) F(font_weight) F(order) F(font_size) F(line_height) F(line_height_normal) F(line_height_factor) F(letter_spacing) F(word_spacing) F(text_indent) F(flex_grow) F(flex_shrink) F(flex_basis) F(row_gap) F(column_gap) F(row_gap_l) F(column_gap_l) F(vertical_align_len) F(width) F(height) F(min_width) F(min_height) F(max_width) F(max_height) F(margin) F(padding) F(inset) F(border_width) F(border_radius) F(font_family) F(aspect_ratio) F(grid_auto_rows) F(grid_column) F(grid_row) F(line_clamp)
#undef F
    if (a->has_transform != b->has_transform) n += (size_t)snprintf(out + n, sizeof out - n, " has_transform");
    if ((a->before != NULL) != (b->before != NULL) || (a->after != NULL) != (b->after != NULL)) n += (size_t)snprintf(out + n, sizeof out - n, " pseudo");
    __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style layout tag=%s parent=%.120s fields=%s\n", node->tag,
        node->parent && node->parent->class_name ? node->parent->class_name : "", n ? out : " other");
}
#endif
/* Inputs of the page's image fetch scan: <img> elements and image URLs of rendered boxes. */
static bool image_inputs_changed(const Node *n, const ComputedStyle *a, const ComputedStyle *b) {
    bool img = n->ns == NS_HTML && n->tag == A_img;
    if (!a) return img || b->bg_image || b->mask_image;
    if (!str_same(a->bg_image, b->bg_image) || !str_same(a->mask_image, b->mask_image)) return true;
    return (b->bg_image || b->mask_image) && (a->display == D_NONE) != (b->display == D_NONE);
}
/* moves nw's values into old (keeping old's address, which boxes point at); nw is left holding old's values */
static void adopt(ComputedStyle *old, ComputedStyle *nw) {
    ComputedStyle *ob = old->before, *oa = old->after, *om = old->marker, *nb = nw->before, *na = nw->after, *nm = nw->marker;
    if (ob) adopt(ob, nb);
    if (oa) adopt(oa, na);
    ComputedStyle t = *old; int refs = old->refs;
    *old = *nw; old->refs = refs; old->before = ob; old->after = oa; old->marker = nm;
    *nw = t; nw->refs = 1; nw->before = nb; nw->after = na; nw->marker = om;
}

/* Inheritance-only updates must still expose the new custom-property values to
 * descendants and getComputedStyle. Rebase immutable maps; never mutate maps
 * retained by another computed style. Flattened maps take the normal path. */
/* The parent values an explicit `inherit` of a non-inherited property copies (see apply's inherit). */
static bool uncompared_differ(const ComputedStyle *a, const ComputedStyle *b) {
    return a->column_count != b->column_count || memcmp(&a->column_width, &b->column_width, sizeof a->column_width) || a->size_containment != b->size_containment || a->content_visibility != b->content_visibility || memcmp(a->contain_intrinsic, b->contain_intrinsic, sizeof a->contain_intrinsic) || a->bg_color != b->bg_color || memcmp(&a->width, &b->width, sizeof a->width) || memcmp(&a->height, &b->height, sizeof a->height) ||
        memcmp(&a->min_width, &b->min_width, sizeof a->min_width) || memcmp(&a->min_height, &b->min_height, sizeof a->min_height) ||
        memcmp(&a->max_width, &b->max_width, sizeof a->max_width) || memcmp(&a->max_height, &b->max_height, sizeof a->max_height) ||
        memcmp(a->margin, b->margin, sizeof a->margin) || memcmp(a->padding, b->padding, sizeof a->padding) ||
        memcmp(a->inset, b->inset, sizeof a->inset) ||
        a->flex_grow != b->flex_grow || a->flex_shrink != b->flex_shrink ||
        memcmp(&a->flex_basis, &b->flex_basis, sizeof a->flex_basis) ||
        memcmp(a->border_style, b->border_style, sizeof a->border_style) ||
        memcmp(a->border_width, b->border_width, sizeof a->border_width) ||
        memcmp(a->border_color, b->border_color, sizeof a->border_color) || a->position != b->position ||
        memcmp(a->border_radius, b->border_radius, sizeof a->border_radius);
}
static bool can_rebase(const ComputedStyle *s, const CustomProps *old_parent,
        const CustomProps *new_parent, const CustomMask *changed, unsigned diff) {
    if (!s) return true;
    if ((s->parent_sensitive && (diff & INH_UNCOMPARED)) || s->inherited_dynamic || cm_any(&s->custom_dependencies, changed)) return false;
    if (s->custom != old_parent && (!s->custom || s->custom->parent != old_parent ||
            (new_parent && new_parent->depth >= CUSTOM_MAX_DEPTH))) return false;
    return can_rebase(s->before, s->custom, s->custom, changed, diff) &&
        can_rebase(s->after, s->custom, s->custom, changed, diff) &&
        can_rebase(s->marker, s->custom, s->custom, changed, diff);
}
static int rebase_block_reason(const ComputedStyle *s, const CustomProps *old_parent, const CustomProps *new_parent, const CustomMask *changed, unsigned diff) {
    if (s->parent_sensitive && (diff & INH_UNCOMPARED)) return 0;
    if (s->inherited_dynamic) return 1;
    if (cm_any(&s->custom_dependencies, changed)) return 2;
    if (s->custom != old_parent && (!s->custom || s->custom->parent != old_parent ||
            (new_parent && new_parent->depth >= CUSTOM_MAX_DEPTH))) return 3;
    return 4;
}
static void rebase_custom(ComputedStyle *s, CustomProps *old_parent, CustomProps *new_parent) {
    if (!s) return;
    CustomProps *old = s->custom, *next;
    if (old == old_parent) next = custom_ref(new_parent);
    else {
        next = xcalloc(1, sizeof *next); next->refs = 1;
        next->parent = custom_ref(new_parent); next->depth = new_parent ? new_parent->depth + 1 : 0;
        hm_foreach(&old->map, ent) hm_put(&next->map, ent->key, xstrdup(ent->val));
    }
    s->custom = next;
    rebase_custom(s->before, old, next);
    rebase_custom(s->after, old, next);
    rebase_custom(s->marker, old, next);
    custom_unref(old);
}
static bool custom_reaches(const CustomProps *c, const CustomProps *stop) {
    for (; c; c = c->parent) if (c == stop) return true;
    return !stop;
}
/* Entries at and above the parents' maps differ only as inherited says.
 * Otherwise entries above the chains' first shared map are identical unless
 * shadowed below it, and the walk visits every shadowing entry. */
static CustomMask custom_changes(const CustomProps *a, const CustomProps *b,
        const CustomProps *old_parent, const CustomProps *new_parent, const CustomMask *inherited) {
    CustomMask changed = {{0}};
    if (a == b) return changed;
    bool local = custom_reaches(a, old_parent) && custom_reaches(b, new_parent);
    g_cc[0]++; g_cc[1] += !local;
    if (local) changed = *inherited;
    const CustomProps *stops[] = {old_parent, new_parent};
    if (!local) {
        const CustomProps *shared = NULL;
        for (const CustomProps *x = a; x && !shared; x = x->parent) if (custom_reaches(b, x)) shared = x;
        stops[0] = stops[1] = shared;
        g_cc[2] += !shared;
    }
    for (int i = 0; i < 2; i++) for (const CustomProps *c = i ? b : a; c && c != stops[i]; c = c->parent) {
        g_cc[3] += (uint64_t)c->map.n; if ((uint64_t)c->map.n > g_cc[4]) g_cc[4] = (uint64_t)c->map.n;
        hm_foreach((HMap *)&c->map, ent) {
            if (cm_has(&changed, ent->hash)) continue;
            size_t n = strlen(ent->key);
            if (!str_same(custom_geth(a, ent->key, n, ent->hash), custom_geth(b, ent->key, n, ent->hash))) cm_add(&changed, ent->hash);
        }
    }
    return changed;
}
/* 0 when children inherit the same values; INDEP_* bits when only
 * independent properties differ; INH_OTHER otherwise. */
static const char *g_inh_field; /* debug: first non-independent inherited difference */
#if defined(__ANDROID__) && !defined(NDEBUG)
/* Debug Android: the largest subtree matched under an element that was not a dirty root, and why. */
static struct { int n; const char *tag, *field; unsigned diff; bool resel, inherit_only, cleared, reuse; int depth; } g_cascade;
#endif
static unsigned inherited_diff(const ComputedStyle *a, const ComputedStyle *b) {
    if (!a || !b) return INH_OTHER;
#define INHERITED_EQ(f) if (memcmp(&a->f, &b->f, sizeof a->f)) { g_inh_field = #f; return INH_OTHER; }
    INHERITED_EQ(display); /* flex/grid blockification depends on the parent */
    INHERITED_EQ(color); INHERITED_EQ(font_size); INHERITED_EQ(font_weight); INHERITED_EQ(font_style); INHERITED_EQ(font_family);
    INHERITED_EQ(line_height); INHERITED_EQ(line_height_normal); INHERITED_EQ(line_height_factor);
    INHERITED_EQ(text_align);
    INHERITED_EQ(letter_spacing); INHERITED_EQ(word_spacing); INHERITED_EQ(text_indent); INHERITED_EQ(list_style);
    INHERITED_EQ(direction);
    INHERITED_EQ(has_text_shadow); INHERITED_EQ(text_shadow); INHERITED_EQ(caret_color); INHERITED_EQ(fill); INHERITED_EQ(stroke); INHERITED_EQ(stroke_width);
    INHERITED_EQ(border_collapse); INHERITED_EQ(border_spacing); INHERITED_EQ(writing_mode); INHERITED_EQ(inert);
    /* The generic inherited orientation is a dependency of logical edge
       mapping. Keep the ordinary identical-map case free of hash lookups. */
    if (a->gen_i != b->gen_i) {
        const GenProp *orientation = gen_prop("text-orientation");
        if (!str_same(gen_value(a, orientation), gen_value(b, orientation))) {
            g_inh_field = "text-orientation"; return INH_OTHER;
        }
    }
#undef INHERITED_EQ
    return (a->pointer_events != b->pointer_events ? INDEP_POINTER : 0) | (a->visibility != b->visibility ? INDEP_VISIBILITY : 0) |
        (a->cursor != b->cursor ? INDEP_CURSOR : 0) | (a->user_select != b->user_select ? INDEP_USER_SELECT : 0) |
        (a->white_space != b->white_space ? INDEP_WHITE_SPACE : 0) | (a->text_transform != b->text_transform ? INDEP_TEXT_TRANSFORM : 0) |
        (a->word_break != b->word_break ? INDEP_WORD_BREAK : 0) | (a->overflow_wrap != b->overflow_wrap ? INDEP_OVERFLOW_WRAP : 0);
}
static bool indep_free(const ComputedStyle *s, unsigned diff) {
    return !s || (!(s->indep_set & diff) && indep_free(s->before, diff) && indep_free(s->after, diff) && indep_free(s->marker, diff));
}
/* Copies the changed independent values from par into s and its pseudo-elements. */
static void indep_inherit(ComputedStyle *s, const ComputedStyle *par, unsigned diff) {
    if (!s) return;
    if (diff & INDEP_POINTER) s->pointer_events = par->pointer_events;
    if (diff & INDEP_VISIBILITY) s->visibility = par->visibility;
    if (diff & INDEP_CURSOR) s->cursor = par->cursor;
    if (diff & INDEP_USER_SELECT) s->user_select = par->user_select;
    if (diff & INDEP_WHITE_SPACE) s->white_space = par->white_space;
    if (diff & INDEP_TEXT_TRANSFORM) s->text_transform = par->text_transform;
    if (diff & INDEP_WORD_BREAK) s->word_break = par->word_break;
    if (diff & INDEP_OVERFLOW_WRAP) s->overflow_wrap = par->overflow_wrap;
    indep_inherit(s->before, s, diff); indep_inherit(s->after, s, diff); indep_inherit(s->marker, s, diff);
}
#ifdef __ANDROID__
static int inl_has_color(Node *n) { const char *v = node_attr_atom(n, A_style); return v && strstr(v, "color") != NULL; }
#endif
static _Thread_local bool g_style_hidden;   /* style dirty elements under display:none too (css_get_computed_value) */
static void recalc(StyleEngine *e, Node *n, const ComputedStyle *par, bool force, bool reselect, bool keyed,
        const uint64_t ancestors[16], CustomProps *old_parent, const CustomMask *changed, unsigned parent_diff) {
    uint64_t classes[16]; memcpy(classes, ancestors, sizeof classes);
    add_class_filter(classes, n);
    unsigned own = (n->flags & NF_RESELECT_SUBTREE ? 1 : 0) | (n->flags & NF_RESELECT_KEYED ? 2 : 0) | (reselect ? 4 : 0) | (keyed ? 8 : 0);
    reselect |= (n->flags & NF_RESELECT_SUBTREE) != 0;
    keyed |= (n->flags & NF_RESELECT_KEYED) && e->keyed_reselect;
    n->flags &= ~(uint32_t)(NF_RESELECT_SUBTREE | NF_RESELECT_KEYED | NF_CHILD_LIST_DIRTY);
    if (n->type == NODE_ELEMENT) {
        bool sub_root = !force && ((n->flags & NF_STYLE_DIRTY) || !n->style); int m0 = e->stats_matched;
        CustomProps *old_custom = n->style ? custom_ref(n->style->custom) : NULL;
        unsigned child_diff = 0;
        CustomMask child_changed = {{0}};
        bool restore_reuse = false;   /* reuse_inherited was cleared for this subtree only */
        bool need = force || (n->flags & NF_STYLE_DIRTY) || !n->style;
        bool resel = reselect || (keyed && need && keyed_hit(e, n));
        bool inherit_only = need && force && !resel && !(n->flags & NF_STYLE_DIRTY) && n->style && par &&
            !(parent_diff & INH_OTHER) && e->reuse_inherited;
        bool indep = inherit_only && indep_free(n->style, parent_diff);
        if (inherit_only && !indep) e->stats_indep_block++;
        else if (indep && !can_rebase(n->style, old_parent, par->custom, changed, parent_diff)) {
            e->stats_rebase_block++; int why = rebase_block_reason(n->style, old_parent, par->custom, changed, parent_diff);
            e->stats_rebase_why[why]++; indep = false;
            if (why == 2) { uint64_t bits = 0; for (int i = 0; i < 4; i++) bits += (uint64_t)__builtin_popcountll(changed->w[i]); if (bits > g_cc[6]) g_cc[6] = bits; }
        }
        if (indep) {
            rebase_custom(n->style, old_parent, par->custom);
            if (parent_diff & ~(unsigned)INH_UNCOMPARED) {
                ComputedStyle old = *n->style;
                indep_inherit(n->style, par, parent_diff);
                if (css_style_change_hook) css_style_change_hook(n, &old, n->style);
                if (parent_diff & (INDEP_WHITE_SPACE | INDEP_TEXT_TRANSFORM | INDEP_WORD_BREAK | INDEP_OVERFLOW_WRAP)) e->layout_dirty = true;
            }
            e->stats_reused++;
            child_diff = parent_diff & ~(unsigned)INH_UNCOMPARED;   /* n kept its own uncompared values */
            child_changed = *changed; /* over-invalidation is safe when a local variable shadows it */
        } else if (need) {
#if defined(__ANDROID__) && !defined(NDEBUG)
            if (e->debug_style_passes && !force) __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style root tag=%s classes=%.180s inline=%d\n", n->tag, n->class_name ? n->class_name : "", node_has_attr(n, "style"));
#endif
            e->el_scope = tree_scope(n);
            e->ancestor_classes = classes;
            e->collect_dynamic = false;
            css_match_cache_subject(n, classes);
            g_vh_used = false;
#if defined(__ANDROID__) && !defined(NDEBUG)
            double dbg_e0 = dbg_now(); uint64_t dbg_has0[2] = {css_has_stats[0], css_has_stats[1]};
#endif
            ComputedStyle *st = compute(e, n, par);
#if defined(__ANDROID__) && !defined(NDEBUG)
            double dbg_e1 = dbg_now();
#endif
            if (st->display != D_NONE) {
                if (e->idx.count) { st->before = compute_pseudo(e, n, st, 1); st->after = compute_pseudo(e, n, st, 2); if (st->display == D_LIST_ITEM) st->marker = compute_pseudo(e, n, st, 4); }
            }
#if defined(__ANDROID__) && !defined(NDEBUG)
            double dbg_e2 = dbg_now(), dbg_h = 0;
#endif
            st->uses_vh = g_vh_used;
            st->inherited_dynamic |= e->collect_dynamic;
            /* Root-font units and animation visibility can depend on distant
             * ancestors even after intermediate inherited values stabilize:
             * the subtree matches afresh, and later elements reuse again. */
            if (e->reuse_inherited && n->style && (((n->style->display == D_NONE) != (st->display == D_NONE)) ||
                    (n == e->doc->html && n->style->font_size != st->font_size))) { e->reuse_inherited = false; restore_reuse = true; }
            child_diff = e->reuse_inherited ? inherited_diff(n->style, st) : INH_OTHER;
            if (n->style && uncompared_differ(n->style, st)) child_diff |= INH_UNCOMPARED;
#ifdef __ANDROID__
            /* Why a page-wide inherited value moved: values and selector shapes only. */
            if ((n == e->doc->body || n == e->doc->html) && n->style && n->style->color != st->color) {
                int mn; bool owned; MDecl *md = matched_decls(e, n, 0, &mn, &owned);
                const char *val = "", *var = NULL; char vname[64] = "";
                for (int i = 0; i < mn; i++) if (!strcmp(md[i].d->prop, "color")) val = md[i].d->value;
                if ((var = strstr(val, "var(--"))) { int j = 0; for (var += 4; *var && *var != ')' && *var != ',' && j < 63; ) vname[j++] = *var++; vname[j] = 0; }
                const char *ov = *vname ? custom_get(n->style->custom, vname) : NULL, *nv = *vname ? custom_get(st->custom, vname) : NULL;
                SB attrs; sb_init(&attrs);
                for (int i = 0; i < n->nattrs; i++) { sb_puts(&attrs, n->attrs[i].name); sb_puts(&attrs, " "); }
                __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "inh_color tag=%s old=%08x new=%08x decl=%.80s old_var=%.60s new_var=%.60s inline_color=%d par_old=%08x attrs=%.200s",
                    n->tag, n->style->color, st->color, val, ov ? ov : "-", nv ? nv : "-", inl_has_color(n), par ? par->color : 0, attrs.s ? attrs.s : "");
                sb_free(&attrs); if (owned) free(md);
            }
#endif
            if (e->reuse_inherited) child_changed = custom_changes(old_custom, st->custom,
                old_parent, par ? par->custom : NULL, changed);
#if defined(__ANDROID__) && !defined(NDEBUG)
            double dbg_h0 = dbg_now();
#endif
            if (css_style_change_hook) css_style_change_hook(n, n->style, st);
#if defined(__ANDROID__) && !defined(NDEBUG)
            dbg_h = dbg_now() - dbg_h0;
#endif
            if (n->doc && image_inputs_changed(n, n->style, st)) n->doc->resource_version++;
            if (n->style && paint_only(n->style, st)) { adopt(n->style, st); style_free(st); e->stats_paint++; }
            else if (n->style && e->allow_moves && inset_only(n->style, st)) {
                if (e->nmoves == e->cmoves) { e->cmoves = e->cmoves ? e->cmoves * 2 : 16; e->moves = xrealloc(e->moves, sizeof *e->moves * (size_t)e->cmoves); }
                struct StyleMove *m = &e->moves[e->nmoves++];
                m->node = n; memcpy(m->old, n->style->inset, sizeof m->old);
                adopt(n->style, st); style_free(st); e->stats_moved++;
            } else {
#if defined(__ANDROID__) && !defined(NDEBUG)
                if (e->debug_style_passes && n->style) debug_layout_change(n->style, st, n);
#endif
                if (n->style) style_free(n->style); n->style = st; e->layout_dirty = true;
            }
            force = true; /* children inherit */
            e->stats_matched++;
#if defined(__ANDROID__) && !defined(NDEBUG)
            dbg_slow_el(n, dbg_now() - dbg_e0, dbg_e1 - dbg_e0, dbg_e2 - dbg_e1, dbg_h, dbg_has0);
#endif
        }
        n->flags &= ~(uint32_t)(NF_STYLE_DIRTY);
        if (!force && !(n->flags & NF_CHILD_STYLE_DIRTY)) { custom_unref(old_custom); return; }
        n->flags &= ~(uint32_t)NF_CHILD_STYLE_DIRTY;
        if (n->style->display == D_NONE && !force && !g_style_hidden) { custom_unref(old_custom); return; }
        Node *kids = n->template_content ? NULL : n;
        if (kids) for (Node *c = n->first; c; c = c->next) recalc(e, c, n->style, force, reselect, keyed, classes, old_custom, &child_changed, child_diff);
        if (n->shadow_root) for (Node *c = n->shadow_root->first; c; c = c->next) recalc(e, c, n->style, force, reselect, keyed, classes, old_custom, &child_changed, child_diff);
        if (restore_reuse) e->reuse_inherited = true;
#if defined(__ANDROID__) && !defined(NDEBUG)
        if (!sub_root && need && e->stats_matched - m0 > g_cascade.n && (child_diff & INH_OTHER)) {
            int depth = 0; for (Node *a = n->parent; a; a = a->parent) depth++;
            g_cascade.n = e->stats_matched - m0; g_cascade.tag = n->tag; g_cascade.field = e->reuse_inherited && !restore_reuse ? g_inh_field : "noreuse";
            g_cascade.diff = child_diff; g_cascade.resel = resel; g_cascade.inherit_only = inherit_only; g_cascade.cleared = restore_reuse; g_cascade.reuse = e->reuse_inherited; g_cascade.depth = depth;
        }
        if (e->debug_style_passes && e->stats_matched - m0 >= 1000 && (own & 3) && !(own & 4))   /* where a large reselect starts: own flags 1 subtree, 2 keyed; 8 keyed from above */
            __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style big_reselect tag=%s matched=%d own=%u whole=%d", n->tag ? n->tag : "-", e->stats_matched - m0, own, e->keyed_whole);
#else
        (void)own;
#endif
        if (sub_root) { e->stats_roots++; if (e->stats_matched - m0 > e->stats_big_root) { e->stats_big_root = e->stats_matched - m0; e->stats_big_tag = n->tag; e->stats_big_reselect = resel; e->stats_big_inh = (int)child_diff; e->stats_inh_field = child_diff & INH_OTHER ? g_inh_field : ""; } }
        custom_unref(old_custom);
    } else if (n->type == NODE_DOCUMENT || n->type == NODE_FRAGMENT) {
        force = force || (n->flags & NF_STYLE_DIRTY);
        n->flags &= ~(uint32_t)(NF_STYLE_DIRTY | NF_CHILD_STYLE_DIRTY);
        for (Node *c = n->first; c; c = c->next) recalc(e, c, par, force, reselect, keyed, classes, old_parent, changed, parent_diff);
    } else n->flags &= ~(uint32_t)NF_STYLE_DIRTY;
}

static bool annotate_tree_changes(Node *n) {
    n->flags &= ~(uint32_t)(NF_TREE_ANCESTOR | NF_TREE_RELATED | NF_TREE_CHILD);
    bool direct = (n->flags & NF_CHILD_LIST_DIRTY) != 0;
    bool changed = direct, following = changed;
    for (Node *c = n->last; c; c = c->prev) {
        bool child = annotate_tree_changes(c);
        if (direct) c->flags |= NF_TREE_CHILD;
        if (child || following) c->flags |= NF_TREE_RELATED;
        changed |= child; following |= child;
    }
    if (n->shadow_root) changed |= annotate_tree_changes(n->shadow_root);
    if (changed) n->flags |= NF_TREE_ANCESTOR | NF_TREE_RELATED;
    return changed;
}
#if defined(__ANDROID__) && !defined(NDEBUG)
/* Selector structure only: no attribute values, declaration values, text or URLs. */
static void debug_selector_shape(const Selector *s, SB *out) {
    for (int i = 0; i < s->n && out->n < 6000; i++) {
        sb_printf(out, " [%d", s->c[i].comb);
        for (int j = 0; j < s->c[i].n && out->n < 6000; j++) {
            SimpleSel *p = &s->c[i].s[j];
            sb_printf(out, " %d:%s", p->kind, p->name ? p->name : "*");
            if (p->sub) for (int k = 0; k < p->sub->n; k++) {
                sb_puts(out, "("); debug_selector_shape(&p->sub->v[k], out); sb_puts(out, ")");
            }
        }
        sb_puts(out, "]");
    }
}
#endif
/* One compact index per mutation batch avoids scanning every active rule for
 * every element in a long conversation. Collisions only add exact-key checks.
 * Coalesced changes that can remove the subject key stay in the universal list. */
typedef struct { TreeDependency *dep; const char *key; unsigned kind; uint32_t need; int next; bool parent; } ActiveTreeEntry;
/* Lists [global * 2 + flagged]: outside changed subtrees only global entries
 * can hit; flagged entries need a tree flag most elements of a big inserted
 * subtree lack, so those elements skip them without walking the chains. */
typedef struct {
    ActiveTreeEntry *entries;
    int heads[4][1024], universal[4], count, unkeyed;
    bool any_global;
} ActiveTreeIndex;
static unsigned tree_subject_hash(unsigned kind, const char *key) {
    uint32_t h = 2166136261u ^ kind;
    bool fold = kind == SK_TYPE || kind == SK_ATTR;
    for (; *key; key++) h = (h ^ (fold ? lc((unsigned char)*key) : (unsigned char)*key)) * 16777619u;
    return h & 1023;
}
/* A widened class/ID key (SK_CHANGED_KEY) is still required unless that key
 * itself changed in this batch; widening drops class/id attribute tests. */
static bool tree_key_kept(const Document *d, const TreeDependency *dep, SubjectKey key) {
    if (!dep->selector_changed || key.kind == SK_TYPE) return true;
    if (key.kind == SK_ATTR) return !str_ieq(key.name, "class") && !str_ieq(key.name, "id");
    unsigned bit = dom_tree_key_bit(key.name);
    return !d->selector_changes_unknown && !(d->selector_change_keys[bit >> 6] >> (bit & 63) & 1);
}
/* Every element the dependency can match carries one of these keys. */
static int tree_dep_keys(const Document *d, const TreeDependency *dep, SubjectKey out[8]) {
    int n = 0;
    if (dep->subject) out[n++] = (SubjectKey){dep->subject, dep->subject_kind};
    else if (dep->source && !(n = subject_keys(dep->source, out))) {
        LocalKey local = local_key(dep->source);
        if (local.kind == 3) out[n++] = (SubjectKey){local.name, SK_ATTR};
    }
    for (int i = 0; i < n; i++) if (!tree_key_kept(d, dep, out[i])) return 0;
    return n;
}
/* Tree flags the candidate's subject compound requires: most positional and
 * sibling dependencies anchor on the subject, so other elements fail a bit test. */
static uint32_t tree_subject_flags(const TreeDependency *dep) {
    if (dep->selector_changed || !dep->candidate.n) return 0;   /* the widened candidate drops anchors */
    const Compound *last = &dep->candidate.c[dep->candidate.n - 1];
    uint32_t need = 0;
    for (int k = 0; k < last->n; k++) if (last->s[k].kind == SK_TREE_RELATED)
        need |= last->s[k].op == 2 ? NF_TREE_CHILD : last->s[k].op ? NF_TREE_RELATED : NF_TREE_ANCESTOR;
    return need;
}
static void active_tree_push(ActiveTreeIndex *index, int *head, TreeDependency *dep, SubjectKey key, bool parent) {
    index->entries[index->count] = (ActiveTreeEntry){dep, key.name, key.kind, tree_subject_flags(dep), *head, parent};
    *head = index->count++;
}
static bool tree_parent_hit(const TreeDependency *dep, const Node *n) {
    const Node *p = n->parent;
    if (!p || p->type != NODE_ELEMENT) return false;
    for (int i = 0; i < dep->nparent; i++) {
        const char *key = dep->parent_names[i];
        switch (dep->parent_kinds[i]) {
        case SK_ID: if (p->id && !strcmp(p->id, key)) return true; break;
        case SK_TYPE: if (p->tag && str_ieq(p->tag, key)) return true; break;
        case SK_CLASS: for (int k = 0; k < p->nclasses; k++) if (!strcmp(p->classes[k], key)) return true; break;
        default: for (int k = 0; k < p->nattrs; k++) if (str_ieq(p->attrs[k].name, key)) return true;
        }
    }
    return false;
}
static void active_tree_add(ActiveTreeIndex *index, TreeDependency *dep, const Document *d) {
    SubjectKey keys[8]; int n = tree_dep_keys(d, dep, keys);
    /* the parent's keys did not change in this batch, so it carried them before as well */
    bool parent = dep->nparent > 0 && !(dep->selector_changed && d->selector_changes_unknown);
    for (int i = 0; parent && dep->selector_changed && i < dep->nparent; i++) if (dep->parent_kinds[i] != SK_TYPE) {
        unsigned bit = dom_tree_key_bit(dep->parent_names[i]);
        parent = !(d->selector_change_keys[bit >> 6] >> (bit & 63) & 1);
    }
    index->unkeyed += !n && !parent;
    bool global = dep->global || dep->selector_changed;
    index->any_global |= global;
    int flagged = tree_subject_flags(dep) != 0;
    if (!n) {
        active_tree_push(index, &index->universal[flagged], dep, (SubjectKey){0}, parent);
        if (global) active_tree_push(index, &index->universal[2 + flagged], dep, (SubjectKey){0}, parent);
    }
    for (int i = 0; i < n; i++) {
        unsigned hash = tree_subject_hash(keys[i].kind, keys[i].name);
        active_tree_push(index, &index->heads[flagged][hash], dep, keys[i], parent);
        if (global) active_tree_push(index, &index->heads[2 + flagged][hash], dep, keys[i], parent);
    }
}
#if defined(__ANDROID__) && !defined(NDEBUG)
static int *g_dep_hits;   /* debug: tree-dependency hits per candidate during one pass */
static int *g_dep_tests;  /* debug: selector tests per candidate */
static uint64_t g_tt[3];  /* debug: key mismatches, selector tests, tests inside changed subtrees */
#endif
static bool invalidate_tree_bucket(StyleEngine *e, Node *n, bool local, int *logged,
        const ActiveTreeIndex *index, int head, unsigned kind, const char *key) {
    for (int i = head; i >= 0; i = index->entries[i].next) {
        const ActiveTreeEntry *entry = &index->entries[i];
        TreeDependency *dep = entry->dep;
        e->stats_tree_tests++;
        if (!local && !dep->global && !dep->selector_changed) continue;
        if ((n->flags & entry->need) != entry->need) continue;
        if (key && (entry->kind != kind ||
                (kind == SK_TYPE || kind == SK_ATTR ? !str_ieq(entry->key, key) : strcmp(entry->key, key)))) {
#if defined(__ANDROID__) && !defined(NDEBUG)
            g_tt[0]++;
#endif
            continue;
        }
        if (entry->parent && !tree_parent_hit(dep, n)) continue;
        e->stats_tree_selector_tests++;
#if defined(__ANDROID__) && !defined(NDEBUG)
        g_tt[1]++; g_tt[2] += local;
        if (g_dep_tests) g_dep_tests[dep - e->idx.tree_candidates.v]++;
#endif
        if (!css_match_selector(dep->selector_changed ? &dep->changed_candidate : &dep->candidate, n, dep->scope)) continue;
#if defined(__ANDROID__) && !defined(NDEBUG)
        if (e->debug_style_passes && *logged < 3) {
            (*logged)++;
            SB shape; sb_init(&shape); debug_selector_shape(dep->source, &shape);
            __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style tree hit=%d global=%d tag=%s source=%.6000s", (int)(dep - e->idx.tree_candidates.v), dep->global, n->tag, shape.s ? shape.s : "");
            sb_free(&shape);
        }
        if (g_dep_hits) g_dep_hits[dep - e->idx.tree_candidates.v]++;
#endif
#if defined(__ANDROID__) && !defined(NDEBUG)
        if (n->doc && n == n->doc->html && dep->selector_changed && g_html_shapes < 12) {
            extern char dom_debug_key_names[1024][24];
            char names[200]; int len = 0; names[0] = 0;
            for (int k = 0; k < 16; k++) for (uint64_t bits = dep->selector_keys[k] & n->doc->selector_change_keys[k]; bits; bits &= bits - 1) {
                const char *nm = dom_debug_key_names[k * 64 + __builtin_ctzll(bits)];
                if (len < 180) len += snprintf(names + len, sizeof names - len, "%s,", *nm ? nm : "?");
            }
            __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "html_sel_keys %s", names);
        }
        if (n->doc && n == n->doc->html) debug_html_shape(dep->global ? (dep->selector_changed ? "tree_global_sel" : dep->witness_filter ? "tree_global_wit" : "tree_global_nowit") : local ? "tree_local" : "tree_changed", key, dep->source, dep->selector_changed ? &dep->changed_candidate : &dep->candidate);
#endif
        mark_style_subject(n, "tree");
        return true;
    }
    return false;
}
static int invalidate_tree_dependents(StyleEngine *e, Node *n, bool local, int *logged, const ActiveTreeIndex *index) {
    int affected = 0;
    /* annotate_tree_changes flagged every ancestor of an edited child list */
    if (!local && !(n->flags & NF_TREE_ANCESTOR) && !index->any_global) return 0;
    /* an element without a style yet (just inserted), or dirty and reselected itself, is matched in full anyway */
    bool fresh = !n->style || (n->flags & (NF_STYLE_DIRTY | NF_RESELECT_SUBTREE)) == (NF_STYLE_DIRTY | NF_RESELECT_SUBTREE);
    if (n->type == NODE_ELEMENT && !fresh) {
        bool hit = false;
        int sets = n->flags & (NF_TREE_ANCESTOR | NF_TREE_RELATED | NF_TREE_CHILD) ? 2 : 1;
        for (int set = local ? 0 : 2, end = set + sets; !hit && set < end; set++) {
            const int *heads = index->heads[set];
            hit = invalidate_tree_bucket(e, n, local, logged, index, index->universal[set], 0, NULL);
            if (!hit && n->id) hit = invalidate_tree_bucket(e, n, local, logged, index,
                heads[tree_subject_hash(SK_ID, n->id)], SK_ID, n->id);
            if (!hit && n->tag) hit = invalidate_tree_bucket(e, n, local, logged, index,
                heads[tree_subject_hash(SK_TYPE, n->tag)], SK_TYPE, n->tag);
            for (int i = 0; !hit && i < n->nclasses; i++)
                hit = invalidate_tree_bucket(e, n, local, logged, index,
                    heads[tree_subject_hash(SK_CLASS, n->classes[i])], SK_CLASS, n->classes[i]);
            for (int i = 0; !hit && i < n->nattrs; i++)
                hit = invalidate_tree_bucket(e, n, local, logged, index,
                    heads[tree_subject_hash(SK_ATTR, n->attrs[i].name)], SK_ATTR, n->attrs[i].name);
        }
        affected += hit;
    }
    local |= (n->flags & NF_CHILD_LIST_DIRTY) != 0;
    n->flags &= ~(uint32_t)NF_CHILD_LIST_DIRTY;
    for (Node *c = n->first; c; c = c->next) affected += invalidate_tree_dependents(e, c, local, logged, index);
    if (n->shadow_root) affected += invalidate_tree_dependents(e, n->shadow_root, local, logged, index);
    return affected;
}

static void vh_mark(Node *n, int *count) {
    if (n->type == NODE_ELEMENT && n->style && n->style->uses_vh && !(n->flags & NF_STYLE_DIRTY)) {
        (*count)++;
        n->flags |= NF_STYLE_DIRTY; DBG_HTML(n, "vh");
        for (Node *p = n->parent ? n->parent : n->host; p; p = p->parent ? p->parent : p->host) p->flags |= NF_CHILD_STYLE_DIRTY;
    }
    for (Node *c = n->first; c; c = c->next) vh_mark(c, count);
    if (n->shadow_root) vh_mark(n->shadow_root, count);
}
static void empty_mark(Node *n, int *count) {
    if (n->flags & NF_STYLE_DIRTY) return;
    (*count)++;
    n->flags |= NF_STYLE_DIRTY; DBG_HTML(n, "empty");
    for (Node *p = n->parent ? n->parent : n->host; p; p = p->parent ? p->parent : p->host) p->flags |= NF_CHILD_STYLE_DIRTY;
}
static Node *empty_next_el(Node *n) { for (n = n->next; n && n->type != NODE_ELEMENT; n = n->next) {} return n; }
static Node *empty_prev_el(Node *n) { for (n = n->prev; n && n->type != NODE_ELEMENT; n = n->prev) {} return n; }
typedef VEC(Node *) EmptyNodes;
/* From the elements compound dep can be, walk the combinators to the subject,
 * keeping only elements that match the selector up to each compound. Returns
 * false when the walk grows past the budget; the caller then tests everything. */
static bool empty_precise(const EmptyDependency *d, const EmptyNodes *changed, EmptyNodes *cur, EmptyNodes *next, int *marked) {
    enum { BUDGET = 4096 };
    const Selector *full = &d->candidate;
    cur->n = 0;
    for (int i = 0; i < changed->n; i++) {
        Node *x = changed->v[i];
        if (d->mode == 1) { vec_push(*cur, x); continue; }
        for (Node *a = x; a && a->type == NODE_ELEMENT; a = a->parent) {
            vec_push(*cur, a);
            if (d->sib) for (Node *s = empty_prev_el(a); s; s = empty_prev_el(s)) { vec_push(*cur, s); if (cur->n > BUDGET) return false; }
            if (cur->n > BUDGET) return false;
        }
    }
    for (int k = d->dep; ; k++) {
        Selector prefix = *full; prefix.n = k + 1;
        if (k != full->n - 1) prefix.pseudo_el = 0;
        int kept = 0;
        for (int i = 0; i < cur->n; i++) if (css_match_selector(&prefix, cur->v[i], d->scope)) cur->v[kept++] = cur->v[i];
        cur->n = kept;
        if (k == full->n - 1) break;
        uint8_t comb = full->c[k + 1].comb;
        next->n = 0;
        for (int i = 0; i < cur->n; i++) {
            Node *x = cur->v[i];
            if (comb == CB_CHILD) { for (Node *c = x->first; c; c = c->next) if (c->type == NODE_ELEMENT) vec_push(*next, c); }
            else if (comb == CB_ADJ) { Node *s = empty_next_el(x); if (s) vec_push(*next, s); }
            else if (comb == CB_SIB) { for (Node *s = empty_next_el(x); s; s = empty_next_el(s)) vec_push(*next, s); }
            else for (Node *c = x->first; c; c = node_next_deep(c, x)) if (c->type == NODE_ELEMENT) { vec_push(*next, c); if (next->n > BUDGET) return false; }
            if (next->n > BUDGET) return false;
        }
        EmptyNodes t = *cur; *cur = *next; *next = t;
    }
    for (int i = 0; i < cur->n; i++) empty_mark(cur->v[i], marked);
    return true;
}
static void invalidate_empty_dependents(StyleEngine *e) {
    RuleIndex *ix = &e->idx;
    int affected_count = 0;
#if defined(__ANDROID__) && !defined(NDEBUG)
    int dbg_hits[256] = {0};
#endif
    EmptyNodes changed = {0}, cur = {0}, next = {0};
    bool overflow = false, any_full = false;
    for (int i = 0; i < ix->empty_candidates.n; i++) any_full |= !ix->empty_candidates.v[i].mode;
    /* the changed elements; removed ones are gone with their flag, their parent flipped instead */
    for (Node *n = e->doc->node.first; n; n = node_next_deep(n, &e->doc->node)) {
        if (n->type != NODE_ELEMENT || !(n->flags & NF_EMPTY_CHANGED)) continue;
        n->flags &= ~(uint32_t)NF_EMPTY_CHANGED;
        if (changed.n < 512) vec_push(changed, n); else overflow = true;
    }
    uint8_t *full = xcalloc((size_t)ix->empty_candidates.n + 1, 1);
    for (int i = 0; i < ix->empty_candidates.n; i++) {
        EmptyDependency *d = &ix->empty_candidates.v[i];
        if (!d->mode || overflow) { full[i] = 1; continue; }
        int before = affected_count;
        if (!empty_precise(d, &changed, &cur, &next, &affected_count)) { full[i] = 1; any_full = true; }
#if defined(__ANDROID__) && !defined(NDEBUG)
        if (i < 256) dbg_hits[i] += affected_count - before;
#else
        (void)before;
#endif
    }
    if (overflow) any_full = true;
    for (Node *n = any_full ? e->doc->node.first : NULL; n; n = node_next_deep(n, &e->doc->node)) {
        if (n->type != NODE_ELEMENT) continue;
        bool affected = false;
        for (int i = 0; !affected && i < ix->empty_candidates.n; i++) {
            if (!full[i]) continue;
            EmptyDependency *d = &ix->empty_candidates.v[i];
            /* precise candidates that overflowed are not in the key maps */
            if (!d->mode) {
                const char *tag = n->ns == NS_HTML ? n->tag : atom_lower(n->tag, strlen(n->tag));
                bool keyed = ix->empty_universal || (n->id && hm_get(&ix->empty_ids, n->id)) || hm_get(&ix->empty_tags, tag);
                for (int c = 0; !keyed && c < n->nclasses; c++) keyed = hm_get(&ix->empty_classes, n->classes[c]) != NULL;
                if (!keyed) break;
            }
            affected = css_match_selector(&d->candidate, n, d->scope);
#if defined(__ANDROID__) && !defined(NDEBUG)
            if (affected && i < 256) dbg_hits[i]++;
#endif
        }
        if (affected) empty_mark(n, &affected_count);
    }
    int changed_n = changed.n;
    free(full); vec_free(changed); vec_free(cur); vec_free(next);
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (e->debug_style_passes) {
        __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style empty candidates=%d affected=%d universal=%d changed=%d full=%d\n",
            ix->empty_candidates.n, affected_count, ix->empty_universal, changed_n, any_full);
        for (int i = 0; i < ix->empty_candidates.n && i < 256; i++) if (dbg_hits[i] > 20) {
            SB shape; sb_init(&shape); debug_selector_shape(&ix->empty_candidates.v[i].candidate, &shape);
            __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style empty hit=%d n=%d shape=%.1500s", i, dbg_hits[i], shape.s ? shape.s : "");
            sb_free(&shape);
        }
    }
#else
    (void)affected_count; (void)changed_n;
#endif
}
bool style_font_metrics_dirty(const StyleEngine *e) {
    return e && e->uses_font_units && e->styled_font_generation != font_generation();
}
void style_recalc(StyleEngine *e, Node *root, bool force) {
    if (style_font_metrics_dirty(e)) { force = true; root = &e->doc->node; }
    e->styled_font_generation = font_generation();
    e->stats_tree_tests = e->stats_tree_selector_tests = 0;
#if defined(__ANDROID__) && !defined(NDEBUG)
    struct timespec dbg_t0, dbg_c0; clock_gettime(CLOCK_MONOTONIC, &dbg_t0); clock_gettime(CLOCK_THREAD_CPUTIME_ID, &dbg_c0);
    int dbg_cpu0 = sched_getcpu();
    memset(g_slow_el, 0, sizeof g_slow_el); g_el_total = 0;
    bool dbg_force = force, dbg_idx = e->idx_dirty || e->idx_changed, dbg_tree = e->seen_tree_version != e->doc->tree_version,
         dbg_empty = e->seen_text_emptiness != e->doc->text_emptiness_version, dbg_inh = e->seen_inheritance_version != e->doc->inheritance_version;
    int dbg_affected = -1, dbg_unkeyed = -1;
    const char *dbg_unknown = e->doc->selector_changes_unknown ? (e->doc->unknown_reason ? e->doc->unknown_reason : "?") : "";
#endif
    bool media_changed = !e->media_valid || e->media.vw != e->styled_media.vw ||
        e->media.vh != e->styled_media.vh || e->media.dpr != e->styled_media.dpr ||
        e->media.dark != e->styled_media.dark;
    /* only the height moved (the on-screen keyboard): unless a media query flips, restyle just
       the elements that resolved a viewport-height unit, not the whole document */
    bool height_only = media_changed && e->media_valid && e->media.vw == e->styled_media.vw &&
        e->media.dpr == e->styled_media.dpr && e->media.dark == e->styled_media.dark && !e->idx_dirty && !e->idx_changed;
    for (int i = 0; i < e->sheets.n; i++) {   /* the owner's media attribute: set, changed, or the viewport moved */
        StyleSheet *sh = e->sheets.v[i]; const char *m = sh->owner ? node_attr(sh->owner, "media") : NULL;
        bool off = m && *m && !css_media_matches(m, &e->media);
        if (off != sh->media_off) { sh->media_off = off; e->idx_dirty = true; e->delta_all = true; force = true; }
    }
    if (e->idx_dirty) idx_build(e);
    if (force) e->npending_attrs = 0;
    else attribute_subjects_flush(e);
    if (media_changed || e->idx_changed) {
        g_match_epoch++;
        for (int sheet_index = 0; sheet_index < e->sheets.n; sheet_index++) {
            StyleSheet *sheet = e->sheets.v[sheet_index];
            for (int index = 0; index < sheet->media_conds.n; index++) {
                MediaCond *condition = sheet->media_conds.v[index];
                bool matches = (!condition->outer || condition->outer->matches) &&
                    css_media_matches(condition->query, &e->media);
                if (matches != condition->matches) height_only = false;
                condition->matches = matches;
            }
        }
    }
    if (media_changed && height_only && !force) { int marked = 0; vh_mark(&e->doc->node, &marked); root = &e->doc->node; }
    else if (media_changed) { force = true; root = &e->doc->node; }
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (e->idx_changed && !force && (!e->delta_any || e->delta_all || e->seen_regprop_version != g_regprop_version)) {
        SB shape; sb_init(&shape); if (e->dbg_delta_all_rule) debug_selector_shape(&e->dbg_delta_all_rule->sel, &shape);
        __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "idx_force any=%d all=%d regprop=%d sheets=%d rule=%.200s",
            e->delta_any, e->delta_all, e->seen_regprop_version != g_regprop_version, e->sheets.n, shape.s ? shape.s : "");
        sb_free(&shape);
    }
#endif
    if (e->idx_changed) {
        e->idx_changed = false;
        /* Unchanged rules keep their matches: restyle only what the added or
         * removed rules can match. Unkeyed rules fall back to every element. */
        if (!force && e->delta_any && !e->delta_all && e->seen_regprop_version == g_regprop_version) {
            uint64_t none[16] = {0};
#if defined(__ANDROID__) && !defined(NDEBUG)
            memset(g_delta_kinds, 0, sizeof g_delta_kinds);
#endif
            sheet_delta_mark(e, &e->doc->node, none, false);
#if defined(__ANDROID__) && !defined(NDEBUG)
            int *k = g_delta_kinds;
            if (k[1] + k[2] + k[3] + k[5] + k[6] + k[7] + k[8] + k[9] + k[10] >= 200)
                __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "sheet_delta unmarked=%d id=%d class=%d tag=%d root=%d attr=%d ptag=%d pclass=%d pattr=%d anc=%d sib=%d",
                    k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7], k[8], k[9], k[10]);
#endif
            root = &e->doc->node;
        } else force = true;
        sheet_delta_clear(e);
        e->seen_regprop_version = g_regprop_version;
    }
    /* Structural/empty invalidation repeatedly matches the same ancestor
     * compounds too. DOM selector versions invalidate old results before this
     * pass; annotate_tree_changes establishes the flags before any matching. */
    CssMatchCache *previous_cache = css_match_cache_begin_style(&e->match_cache,
        e->doc, e->generation, force);
    /* A class/ID change inside a :has() argument, without a tree edit, needs
     * only the :has dependencies; its own subtree already reselects. */
    bool has_changed = false;
    for (int k = 0; k < 16; k++) has_changed |= (e->idx.has_keys[k] & e->doc->selector_change_keys[k]) != 0;
    has_changed &= !force && e->seen_tree_version == e->doc->tree_version &&
        e->seen_inheritance_version != e->doc->inheritance_version && !e->doc->selector_changes_unknown;
    if (e->seen_tree_version != e->doc->tree_version || has_changed) {
        if (!force) {
            int logged = 0, affected = 0;
            if (annotate_tree_changes(&e->doc->node) || has_changed) {
                ActiveTreeIndex active = {.universal = {-1, -1, -1, -1}};
                memset(active.heads, -1, sizeof active.heads);
                active.entries = xmalloc((size_t)e->idx.tree_candidates.n * 16 * sizeof *active.entries + 1);
                for (int i = 0; i < e->idx.tree_candidates.n; i++) {
                    TreeDependency *dep = &e->idx.tree_candidates.v[i];
                    bool selectors_changed = e->seen_inheritance_version != e->doc->inheritance_version;
                    dep->selector_changed = false;
                    if (selectors_changed && !e->doc->selector_changes_unknown) {
                        selectors_changed = false;
                        for (int k = 0; !selectors_changed && k < 16; k++)
                            selectors_changed = (dep->selector_keys[k] & e->doc->selector_change_keys[k]) != 0;
                        dep->selector_changed = selectors_changed;
                    }
                    if (has_changed && (!dep->global || !dep->selector_changed)) continue;
                    if (dep->selector_changed && !dep->changed_candidate.c)
                        dep->changed_candidate = attribute_candidate(&e->idx, &dep->candidate, NULL);
                    if (dep->focus_tree_filter && !selectors_changed && !e->doc->tree_focus_changed) continue;
                    bool hit = !dep->witness_filter || selectors_changed;
                    for (int k = 0; !hit && k < 16; k++) hit = (dep->witness_keys[k] & e->doc->tree_change_keys[k]) != 0;
                    if (hit) active_tree_add(&active, dep, e->doc);
                }
#if defined(__ANDROID__) && !defined(NDEBUG)
                g_dep_hits = xcalloc((size_t)e->idx.tree_candidates.n + 1, sizeof *g_dep_hits);
                g_dep_tests = xcalloc((size_t)e->idx.tree_candidates.n + 1, sizeof *g_dep_tests);
                memset(g_tt, 0, sizeof g_tt);
#endif
#if defined(__ANDROID__) && !defined(NDEBUG)
                dbg_unkeyed = active.unkeyed;
                static int unkeyed_dumps;   /* debug: the unkeyed dependencies' selector shapes, twice per process */
                if (active.unkeyed >= 40 && unkeyed_dumps < 2) {
                    unkeyed_dumps++;
                    int shown = 0;
                    for (int set = 0; set < 2; set++)
                    for (int i = active.universal[set]; i >= 0 && shown < 160; i = active.entries[i].next, shown++) {
                        TreeDependency *dep = active.entries[i].dep;
                        SB shape; sb_init(&shape); debug_selector_shape(dep->source, &shape);
                        __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "tree_unkeyed %d global=%d changed=%d need=%x witness=%d subject=%s shape=%.260s", shown, dep->global, dep->selector_changed,
                            active.entries[i].need, dep->witness_filter, dep->subject ? dep->subject : "*", shape.s ? shape.s : "");
                        sb_free(&shape);
                    }
                }
#endif
                affected = invalidate_tree_dependents(e, &e->doc->node, false, &logged, &active);
                free(active.entries);
#if defined(__ANDROID__) && !defined(NDEBUG)
                dbg_affected = affected;
                if (affected >= 300) for (int round = 0; round < 3; round++) {   /* the widest deps, as selector shapes */
                    int best = -1; for (int i = 0; i < e->idx.tree_candidates.n; i++) if (g_dep_hits[i] && (best < 0 || g_dep_hits[i] > g_dep_hits[best])) best = i;
                    if (best < 0) break;
                    TreeDependency *dep = &e->idx.tree_candidates.v[best];
                    SB shape; sb_init(&shape); debug_selector_shape(dep->source, &shape);
                    __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "tree_dep hits=%d global=%d changed=%d kind=%d subject=%s shape=%.400s", g_dep_hits[best], dep->global, dep->selector_changed, dep->subject_kind, dep->subject ? dep->subject : "*", shape.s ? shape.s : "");
                    sb_free(&shape); g_dep_hits[best] = 0;
                }
                free(g_dep_hits); g_dep_hits = NULL;
                if (g_tt[1] >= 5000) {
                    __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "tree_tests mismatch=%llu tests=%llu local=%llu active=%d unkeyed=%d",
                        (unsigned long long)g_tt[0], (unsigned long long)g_tt[1], (unsigned long long)g_tt[2], active.count, active.unkeyed);
                    for (int round = 0; round < 12; round++) {   /* the most-tested deps, as selector shapes */
                        int best = -1; for (int i = 0; i < e->idx.tree_candidates.n; i++) if (g_dep_tests[i] && (best < 0 || g_dep_tests[i] > g_dep_tests[best])) best = i;
                        if (best < 0) break;
                        TreeDependency *dep = &e->idx.tree_candidates.v[best];
                        SubjectKey keys[8]; int nk = tree_dep_keys(e->doc, dep, keys);
                        SB shape; sb_init(&shape); debug_selector_shape(dep->source, &shape);
                        __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "tree_tested tests=%d global=%d changed=%d nkeys=%d key0=%d:%s shape=%.300s", g_dep_tests[best], dep->global, dep->selector_changed, nk, nk ? keys[0].kind : 0, nk ? keys[0].name : "*", shape.s ? shape.s : "");
                        sb_free(&shape); g_dep_tests[best] = 0;
                    }
                }
                free(g_dep_tests); g_dep_tests = NULL;
#endif
            }
#if defined(__ANDROID__) && !defined(NDEBUG)
            if (e->debug_style_passes) __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style tree candidates=%d affected=%d\n", e->idx.tree_candidates.n, affected);
#else
            (void)affected;
#endif
        }
        e->seen_tree_version = e->doc->tree_version;
        memset(e->doc->tree_change_keys, 0, sizeof e->doc->tree_change_keys);
        e->doc->tree_focus_changed = false;
        root = &e->doc->node;
    }
    if (e->seen_text_emptiness != e->doc->text_emptiness_version) {
        if (!force) invalidate_empty_dependents(e);
        e->seen_text_emptiness = e->doc->text_emptiness_version;
        root = &e->doc->node;
    }
    e->stats_matched = e->stats_paint = e->stats_reused = e->stats_memo_hits = e->stats_moved = 0;
    e->stats_roots = e->stats_big_root = e->stats_big_reselect = e->stats_big_inh = 0; e->stats_big_tag = "";
    memset(e->stats_rebase_why, 0, sizeof e->stats_rebase_why);
    e->stats_indep_block = e->stats_rebase_block = 0; e->stats_inh_field = ""; e->stats_anc_iter = 0; memset(g_cc, 0, sizeof g_cc);
    e->stats_rule_iter = e->stats_rule_tests = e->stats_rule_hits = 0;
    /* Class/ID changes invalidate their own subjects: subtree, siblings and
     * :has. Unkeyed state and attribute changes may reach unforced descendants. */
    e->reuse_inherited = !force && (e->seen_inheritance_version == e->doc->inheritance_version ||
        !e->doc->selector_changes_unknown);
    /* The batch's keys bound each changed element's own keys. */
    e->keyed_reselect = e->doc->selector_changes_unknown;
    for (int k = 0; k < 16; k++) e->keyed_reselect |= (e->idx.reselect_keys[k] & e->doc->reselect_change_keys[k]) != 0;
    e->keyed_whole = e->doc->selector_changes_unknown;
    memset(e->keyed_subjects, 0, sizeof e->keyed_subjects);
    for (int k = 0; k < 16 && e->keyed_reselect && !e->keyed_whole; k++)
        for (uint64_t bits = e->idx.reselect_keys[k] & e->doc->reselect_change_keys[k]; bits; bits &= bits - 1) {
            const uint16_t *v = e->idx.reselect_subjects[k * 64 + __builtin_ctzll(bits)].v;
            for (int i = 0, n = e->idx.reselect_subjects[k * 64 + __builtin_ctzll(bits)].n; i < n; i++) {
                if (v[i] == 1024) {
                    e->keyed_whole = true;
#if defined(__ANDROID__) && !defined(NDEBUG)
                    if (e->debug_style_passes) __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style keyed_whole bit=%d", k * 64 + __builtin_ctzll(bits));
#endif
                }
                else e->keyed_subjects[v[i] >> 6] |= UINT64_C(1) << (v[i] & 63);
            }
        }
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (e->debug_style_passes && e->keyed_whole) __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style keyed_whole unknown=%d", e->doc->selector_changes_unknown);
#endif
    uint64_t ancestors[16] = {0};
    for (Node *n = root->parent ? root->parent : root->host; n; n = n->parent ? n->parent : n->host) add_class_filter(ancestors, n);
#if defined(__ANDROID__) && !defined(NDEBUG)
    struct timespec dbg_tw; clock_gettime(CLOCK_MONOTONIC, &dbg_tw);
#endif
    recalc(e, root, NULL, force, force, false, ancestors, NULL, &(CustomMask){{0}}, INH_OTHER);
#if defined(__ANDROID__) && !defined(NDEBUG)
    struct timespec dbg_tr; clock_gettime(CLOCK_MONOTONIC, &dbg_tr);
#endif
    e->seen_inheritance_version = e->doc->inheritance_version;
    memset(e->doc->selector_change_keys, 0, sizeof e->doc->selector_change_keys);
    memset(e->doc->reselect_change_keys, 0, sizeof e->doc->reselect_change_keys);
    e->doc->selector_changes_unknown = false; e->doc->unknown_reason = NULL;
    e->seen_sheet_version = e->doc->sheet_version;
    e->ancestor_classes = NULL;
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (e->debug_style_passes) { __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "lumen-style pass matched=%d reused=%d memo=%d moved=%d eligible=%d force=%d\n", e->stats_matched, e->stats_reused, e->stats_memo_hits, e->stats_moved, e->reuse_inherited, force); e->debug_style_passes--; }
#endif
    css_match_cache_end(previous_cache);
#if defined(__ANDROID__) && !defined(NDEBUG)
    {   /* Debug Android: why a slow style pass ran and how much it matched. Counts and tag names only. */
        struct timespec t1, c1; clock_gettime(CLOCK_MONOTONIC, &t1); clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c1);
        double ms = (t1.tv_sec - dbg_t0.tv_sec) * 1e3 + (t1.tv_nsec - dbg_t0.tv_nsec) / 1e6;
        double cpu_ms = (c1.tv_sec - dbg_c0.tv_sec) * 1e3 + (c1.tv_nsec - dbg_c0.tv_nsec) / 1e6;   /* below ms: the thread waited (preempted, faults) */
        if (ms >= 15) __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "pass_ms=%.0f matched=%d reused=%d memo=%d paint=%d roots=%d big=%d big_tag=%s big_reselect=%d big_inh=%d force=%d/%d media=%d idx=%d tree=%d affected=%d empty=%d inh=%d tree_tests=%llu rules_iter=%llu tested=%llu hit=%llu idx_rules=%d universal=%d indep_block=%d rebase_block=%d inh_field=%s anc_iter=%llu why=%d/%d/%d/%d/%d unknown=%s cc=%llu/%llu/%llu/%llu/%llu/%llu chg=%llu tu=%d pre=%.0f walk=%.0f thread_ms=%.0f core=%d/%d html_why=%d/%s",
            ms, e->stats_matched, e->stats_reused, e->stats_memo_hits, e->stats_paint, e->stats_roots, e->stats_big_root, e->stats_big_tag ? e->stats_big_tag : "", e->stats_big_reselect, e->stats_big_inh,
            dbg_force, force, media_changed, dbg_idx, dbg_tree, dbg_affected, dbg_empty, dbg_inh, (unsigned long long)e->stats_tree_tests,
            (unsigned long long)e->stats_rule_iter, (unsigned long long)e->stats_rule_tests, (unsigned long long)e->stats_rule_hits, (int)e->idx.count, e->idx.universal.n,
            e->stats_indep_block, e->stats_rebase_block, e->stats_inh_field ? e->stats_inh_field : "", (unsigned long long)e->stats_anc_iter,
            e->stats_rebase_why[0], e->stats_rebase_why[1], e->stats_rebase_why[2], e->stats_rebase_why[3], e->stats_rebase_why[4], dbg_unknown,
            (unsigned long long)g_cc[0], (unsigned long long)g_cc[1], (unsigned long long)g_cc[2], (unsigned long long)g_cc[3], (unsigned long long)g_cc[4], (unsigned long long)g_cc[5], (unsigned long long)g_cc[6], dbg_unkeyed,
            (dbg_tw.tv_sec - dbg_t0.tv_sec) * 1e3 + (dbg_tw.tv_nsec - dbg_t0.tv_nsec) / 1e6, (dbg_tr.tv_sec - dbg_tw.tv_sec) * 1e3 + (dbg_tr.tv_nsec - dbg_tw.tv_nsec) / 1e6, cpu_ms, dbg_cpu0, sched_getcpu(), g_html_line, g_html_key ? g_html_key : "-");
        g_html_line = 0; g_html_key = NULL;
        if (ms >= 15) for (int k = 0; k < 3 && g_slow_el[k].tag; k++)
            __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "slow_el %d of_total=%.1f tag=%s classes=%d ms=%.2f compute=%.2f decls=%.2f pseudo=%.2f hook=%.2f rest=%.2f has=%llu/%llu", k, g_el_total, g_slow_el[k].tag,
                g_slow_el[k].ncls, g_slow_el[k].ms, g_slow_el[k].compute, g_slow_el[k].decls, g_slow_el[k].pseudo, g_slow_el[k].hook, g_slow_el[k].rest,
                (unsigned long long)g_slow_el[k].has_tests, (unsigned long long)g_slow_el[k].has_nodes);
        if (ms >= 60 && g_cascade.n) __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "cascade n=%d tag=%s depth=%d field=%s diff=%u resel=%d inherit_only=%d cleared=%d reuse=%d",
            g_cascade.n, g_cascade.tag, g_cascade.depth, g_cascade.field ? g_cascade.field : "", g_cascade.diff, g_cascade.resel, g_cascade.inherit_only, g_cascade.cleared, g_cascade.reuse);
        memset(&g_cascade, 0, sizeof g_cascade);
        css_has_debug_dump(ms >= 60);
        /* The rules that cost the most failed selector tests, as selector shapes. */
        for (int round = 0; ms >= 90 && round < 10; round++) {
            Rule *best = NULL;
            for (int si = 0; si < e->sheets.n; si++) for (int i = 0; i < e->sheets.v[si]->rules.n; i++) {
                Rule *r = &e->sheets.v[si]->rules.v[i];
                if (r->dbg_tests > r->dbg_hits && (!best || r->dbg_tests - r->dbg_hits > best->dbg_tests - best->dbg_hits)) best = r;
            }
            if (!best) break;
            SubjectKey key = subject_key(&best->sel);
            SB shape; sb_init(&shape); debug_selector_shape(&best->sel, &shape);
            __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "rule_cost tests=%u hits=%u key=%d:%s anc=%d/%d pseudo=%d mq=%d shape=%.300s",
                best->dbg_tests, best->dbg_hits, key.kind, key.name ? key.name : "*", best->ancestor_class_filter < 1024, best->ancestor_class_filter2 < 1024,
                best->sel.pseudo_el, best->mq != NULL, shape.s ? shape.s : "");
            sb_free(&shape); best->dbg_tests = best->dbg_hits = 0;
        }
        for (int si = 0; si < e->sheets.n; si++) for (int i = 0; i < e->sheets.v[si]->rules.n; i++)
            e->sheets.v[si]->rules.v[i].dbg_tests = e->sheets.v[si]->rules.v[i].dbg_hits = 0;
    }
#endif
    // Paint-only transforms can change intersections without running layout.
    if (e->stats_matched) doc_boxes_changed();
    e->styled_media = e->media;
    e->media_valid = true;
}
ComputedStyle *style_for_text(Node *t) { Node *p = t->parent; while (p && p->type != NODE_ELEMENT) p = p->parent ? p->parent : p->host; return p ? p->style : NULL; }

/* alpha as Chrome prints it: the 2-decimal value that maps back to the same byte, else 3 decimals */
static double alpha_round(unsigned a8) { double r = round(a8 / 2.55) / 100; return (unsigned)lround(r * 255) == a8 ? r : round(a8 / 0.255) / 1000; }
static void color_str(SB *b, Color c) { if (COLOR_A(c) == 255) sb_printf(b, "rgb(%u, %u, %u)", COLOR_R(c), COLOR_G(c), COLOR_B(c)); else sb_printf(b, "rgba(%u, %u, %u, %g)", COLOR_R(c), COLOR_G(c), COLOR_B(c), alpha_round(COLOR_A(c))); }
static void len_str(SB *b, Length l, const char *au) {
    if (l.kind == LK_AUTO) sb_puts(b, au);
    else if (l.kind == LK_NONE) sb_puts(b, "none");
    else if (l.kind == LK_MIN_CONTENT) sb_puts(b, "min-content");
    else if (l.kind == LK_MAX_CONTENT) sb_puts(b, "max-content");
    else if (l.kind == LK_FIT_CONTENT) sb_puts(b, "fit-content");
    else if (l.kind == LK_CONTENT) sb_puts(b, "content");
    else if (l.kind == LK_STRETCH) sb_puts(b, "stretch");
    else if (l.mm) bg_length_str(b, l);
    else if (l.pct && l.px) sb_printf(b, "calc(%g%% %c %gpx)", l.pct, l.px < 0 ? '-' : '+', fabsf(l.px));
    else if (l.pct || (l.pctu && !l.px)) sb_printf(b, "%g%%", l.pct);
    else sb_printf(b, "%gpx", l.px);
}
static void tracks_str(SB *b, const GridTrack *t, int n) {
    if (!n) { sb_puts(b, "none"); return; }
    for (int i = 0; i < n; i++, t++) {
        if (i) sb_putc(b, ' ');
        if (t->fr > 0 && t->min.kind == LK_LEN && (t->min.px || t->min.pct)) { sb_puts(b, "minmax("); len_str(b, t->min, "auto"); sb_printf(b, ", %gfr)", t->fr); }
        else if (t->fr > 0) sb_printf(b, "%gfr", t->fr);
        else if (t->size.kind == LK_LEN && t->min.kind == LK_LEN) { sb_puts(b, "minmax("); len_str(b, t->min, "auto"); sb_puts(b, ", "); len_str(b, t->size, "auto"); sb_putc(b, ')'); }
        else len_str(b, t->size, "auto");
    }
}

/* CSS Grid 7.2.6: expose the layout's actual track arrays. Line-name expansion
   follows the computed token structure; it never estimates track sizes. */
static const Box *grid_style_box(Node *el, const ComputedStyle *s) {
    if (!el || !(el->flags & NF_CONNECTED) || !el->style) return NULL;
    if (el->style == s) return el->box && el->box->st == s ? el->box : NULL;
    if (s != el->style->before && s != el->style->after) return NULL;
    Node *ancestor = el;
    while (ancestor && !ancestor->box) ancestor = ancestor->parent;
    const Box *root = ancestor ? ancestor->box : NULL;
    for (const Box *box = root; box;) {
        if (box->node == el && box->st == s) return box;
        if (box->first) { box = box->first; continue; }
        while (box != root && !box->next) box = box->parent;
        box = box == root ? NULL : box->next;
    }
    return NULL;
}
static const char *grid_name_end(const char *p) {
    while (*p) {
        if (*p == ']') return p;
        if (*p == '\\' && p[1]) {
            p++;
            if (isxdigit((unsigned char)*p)) {
                int n = 0; while (n++ < 6 && isxdigit((unsigned char)*p)) p++;
                if (is_ws((unsigned char)*p)) p++;
            } else p++;
        } else p++;
    }
    return NULL;
}
static void grid_add_names(SB *out, const char *p, const char *end) {
    bool space = out->n != 0;
    while (p < end) {
        if (is_ws((unsigned char)*p)) { space = out->n != 0; p++; continue; }
        if (space) sb_putc(out, ' '); space = false;
        if (*p == '\\' && p + 1 < end) {
            sb_putc(out, *p++);
            if (isxdigit((unsigned char)*p)) {
                int n = 0; while (p < end && n++ < 6 && isxdigit((unsigned char)*p)) sb_putc(out, *p++);
                if (p < end && is_ws((unsigned char)*p)) { sb_putc(out, ' '); p++; }
            } else sb_putc(out, *p++);
        } else sb_putc(out, *p++);
    }
}
/* Count ordinary track tokens and expand only their associated names. An
   automatic repeat uses the count already materialized by layout, including
   a zero-count first pass to identify the surrounding fixed sections. */
static int grid_expand_names(ACtx *c, const char *value, SB *names, int limit,
                             int offset, int automatic, int *auto_width, int depth) {
    if (depth > 16) return -1;
    const char *p = value; int count = 0;
    for (;;) {
        while (is_ws((unsigned char)*p)) p++;
        if (*p == '[') {
            const char *end = grid_name_end(p + 1); if (!end) return -1;
            if (names && offset + count <= limit) grid_add_names(names + offset + count, p + 1, end);
            p = end + 1; continue;
        }
        char *token = next_token(&p); if (!token) break;
        if (str_ieq(token, "none")) { free(token); continue; }
        if (str_ieq(token, "subgrid")) { free(token); return -1; }
        if (str_istarts(token, "repeat(")) {
            size_t length = strlen(token);
            if (length < 9 || token[length - 1] != ')') { free(token); return -1; }
            char *inner = xstrndup(token + 7, length - 8), *parts[3];
            int n = split_top(inner, ',', parts, 3);
            if (n != 2) { free(inner); free(token); return -1; }
            int nested_auto = 0;
            int width = grid_expand_names(c, parts[1], NULL, limit, 0, 0, &nested_auto, depth + 1);
            long repeat = 1; const char *number = str_trim(parts[0]);
            bool is_auto = str_ieq(number, "auto-fill") || str_ieq(number, "auto-fit");
            if (width <= 0 || nested_auto || (!is_auto && !grid_repeat_count(c, number, &repeat))) {
                free(inner); free(token); return -1;
            }
            if (is_auto) {
                if (*auto_width) { free(inner); free(token); return -1; }
                *auto_width = width; repeat = automatic;
            }
            if (names) for (long k = 0; k < repeat && offset + count + k * width <= limit; k++) {
                int ignored = 0;
                if (grid_expand_names(c, parts[1], names, limit, offset + count + (int)k * width,
                                      automatic, &ignored, depth + 1) < 0) {
                    free(inner); free(token); return -1;
                }
            }
            count = LMIN(1000, count + (int)repeat * width);
            free(inner);
        } else count = LMIN(1000, count + 1);
        free(token);
    }
    return count;
}
static void grid_resolved_tracks(SB *out, Node *el, const ComputedStyle *s, const char *property, int axis) {
    const GenProp *g = gen_prop(property);
    const char *value = g ? gen_value(s, g) : NULL;
    const Box *box = grid_style_box(el, s);
    const GridUsedTracks *used = box && box->fmt == FMT_GRID ? box->grid_used : NULL;
    if (!used || (value && str_istarts(value, "subgrid"))) {
        if (value) sb_puts(out, value);
        else tracks_str(out, axis ? s->grid_rows : s->grid_cols, axis ? s->grid_nrows : s->grid_ncols);
        return;
    }
    int count = used->count[axis];
    if (!count) { sb_puts(out, "none"); return; }
    /* Layout already resolved every track size. Only declarations with a
       line-name delimiter need name expansion; ordinary/repeated/calculated
       unnamed tracks can serialize these arrays without reparsing or a second
       allocation. Escaped identifiers still occur inside literal brackets. */
    SB *names = value && strchr(value, '[') ? xcalloc((size_t)count + 1, sizeof *names) : NULL;
    if (names) {
        ACtx c = {0}; c.em = s->font_size; c.rem = 16;
        if (el && el->doc) {
            if (el->doc->html && el->doc->html->style) c.rem = el->doc->html->style->font_size;
            if (el->doc->stylesheets) c.mc = &((const StyleEngine *)el->doc->stylesheets)->media;
        }
        int automatic_width = 0;
        int fixed = grid_expand_names(&c, value, NULL, count, 0, 0, &automatic_width, 0);
        int automatic = automatic_width ? LMAX(1, (used->template_count[axis] - fixed) / automatic_width) : 1;
        int ignored = 0;
        if (fixed < 0 || grid_expand_names(&c, value, names, count, used->explicit_start[axis],
                                         automatic, &ignored, 0) < 0)
            for (int i = 0; i <= count; i++) { free(names[i].s); sb_init(names + i); }
    }
    for (int i = 0; i <= count; i++) {
        if (names && names[i].n) {
            if (out->n) sb_putc(out, ' ');
            sb_putc(out, '['); sb_puts(out, names[i].s); sb_putc(out, ']');
        }
        if (i < count) {
            if (out->n) sb_putc(out, ' ');
            float size = used->size[axis][i]; sb_printf(out, "%gpx", size == 0 ? 0.0 : (double)size);
        }
        if (names) free(names[i].s);
    }
    free(names);
}

static void gap_str(SB *b, float px, Length l) { if (l.kind == LK_LEN && l.pct) len_str(b, l, "normal"); else if (px) sb_printf(b, "%gpx", px); else sb_puts(b, "normal"); }
static char *computed_of(Node *el, const ComputedStyle *s, const char *prop);
static void computed_prepare(Node *el) {
    /* restyle skips display:none subtrees; an unstyled element there is styled when script asks for its style */
    if (el && el->type == NODE_ELEMENT && (!el->style || (el->flags & NF_STYLE_DIRTY) || (el->doc && style_font_metrics_dirty(el->doc->stylesheets))) && (el->flags & NF_CONNECTED) && el->doc && el->doc->stylesheets) {
        bool flat = true;   /* not for children outside the flat tree: of a shadow host or of an element that doesn't render its children */
        for (Node *a = el->parent; a && flat; a = a->parent)
            if (a->shadow_root || (a->ns == NS_HTML && (a->tag == A_video || a->tag == A_audio || a->tag == A_object || a->tag == A_iframe || a->tag == A_canvas || a->tag == A_textarea))) flat = false;
        if (flat) {
        el->flags |= NF_STYLE_DIRTY;
        for (Node *a = el->parent; a; a = a->parent) a->flags |= NF_CHILD_STYLE_DIRTY;
        g_style_hidden = true; style_recalc(el->doc->stylesheets, &el->doc->node, false); g_style_hidden = false;
        }
    }
}
char *css_get_computed_value(Node *el, const char *prop) {
    computed_prepare(el);
    return computed_of(el, el && el->type == NODE_ELEMENT ? el->style : NULL, prop);
}
/* a ::before (1) or ::after (2) value: the pseudo-element's own style, or content: none when it generates no box */
char *css_get_computed_pseudo(Node *el, int which, const char *prop) {
    free(css_get_computed_value(el, "display"));
    const ComputedStyle *b = el && el->type == NODE_ELEMENT ? el->style : NULL;
    const ComputedStyle *ps = b ? (which == 1 ? b->before : b->after) : NULL;
    if (!ps && (!strcmp(prop, "content") || !strcmp(prop, "grid-template-columns") || !strcmp(prop, "grid-template-rows"))) return xstrdup("none");
    return computed_of(el, ps ? ps : b, prop);
}
/* Resolve track dimensions while css_apply_decl has the correct element,
   font, viewport and container-query context. Store this computed structure,
   rather than reparsing old declarations against a later document state. */
static char *grid_computed_tokens(const char *raw, float em, float rem, const MediaCtx *mc, const ComputedStyle *basis, bool *used) {
    SB out; sb_init(&out);
    for (const char *p = raw; *p;) {
        if (*p == '[') {
            do { char c = *p++; sb_putc(&out, c); if (c == '\\' && *p) sb_putc(&out, *p++); else if (c == ']') break; } while (*p);
            continue;
        }
        bool start = p == raw || !(isalnum((unsigned char)p[-1]) || p[-1] == '_' || p[-1] == '-');
        if (start && (isdigit((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.')) {
            char *end; (void)strtof(p, &end);
            if (end != p) {
                const char *u = end; while (isalpha((unsigned char)*u)) u++;
                size_t length = (size_t)(u - p);
                if (u > end && length < 128 && !str_ieqn(end, "fr", (size_t)(u - end))) {
                    char token[128]; memcpy(token, p, length); token[length] = 0;
                    Length l;
                    if (css_parse_length_context(token, &l, em, rem, mc, basis, true, used, NULL) && l.kind == LK_LEN) {
                        len_str(&out, l, "auto"); p = u; continue;
                    }
                }
            }
        }
        sb_putc(&out, *p++);
    }
    return sb_take(&out);
}
static char *animation_value_of(Node *n, const ComputedStyle *s, const char *prop) {
    /* Animation combines computed values, before CSSOM resolves an automatic
       minimum to zero. Keep the keyword for discrete interpolation and for
       underlying/neutral keyframes, independent of box or aspect-ratio state. */
    if (s && !strncmp(prop, "min-", 4)) {
        const char *physical = logical_size_prop(prop, s->writing_mode != 0);
        if (!strcmp(physical, "min-width") || !strcmp(physical, "min-height")) {
            SB out; sb_init(&out);
            len_str(&out, !strcmp(physical, "min-width") ? s->min_width : s->min_height, "auto");
            return sb_take(&out);
        }
    }
    if (!s || (strcmp(prop, "grid-template-columns") && strcmp(prop, "grid-template-rows"))) return computed_of(n, s, prop);
    const GenProp *g = gen_prop(prop);
    const char *value = g ? gen_value(s, g) : NULL;
    if (value) return xstrdup(value);
    SB out; sb_init(&out);
    bool rows = !strcmp(prop, "grid-template-rows");
    tracks_str(&out, rows ? s->grid_rows : s->grid_cols, rows ? s->grid_nrows : s->grid_ncols);
    return sb_take(&out);
}
/* Animation keeps computed track structure and minimum-size keywords. CSSOM
   intentionally resolves those values differently and keeps its public behavior. */
char *css_get_animation_value(Node *el, const char *prop) {
    computed_prepare(el);
    const ComputedStyle *s = el && el->type == NODE_ELEMENT ? el->style : NULL;
    return animation_value_of(el, s, prop);
}
/* Properties whose computed value differs between o and s, for CSS transitions: the names in the
 * transition-property list, or for "all" every natively computed property plus the generic ones. */
static void tr_scan_one(Node *n, const ComputedStyle *o, const ComputedStyle *s, const char *prop, void (*cb)(Node *, const char *, const char *, const char *)) {
    if (!strncmp(prop, "transition", 10) || !strncmp(prop, "animation", 9)) return;
    char *a = animation_value_of(n, o, prop), *b = animation_value_of(n, s, prop);
    if (strcmp(a, b)) cb(n, prop, a, b);
    free(a); free(b);
}
static void tr_scan_gen(Node *n, const ComputedStyle *o, const ComputedStyle *s, const CustomProps *c, HMap *seen, void (*cb)(Node *, const char *, const char *, const char *)) {
    for (; c; c = c->parent) hm_foreach((HMap *)&c->map, ent) {
        if (hm_get(seen, ent->key)) continue;
        hm_put(seen, ent->key, (void *)1);
        const GenProp *g = gen_prop(ent->key);
        if (g && !g->native) tr_scan_one(n, o, s, g->name, cb);
    }
}
void css_transition_scan(Node *n, const ComputedStyle *o, const ComputedStyle *s, void (*cb)(Node *, const char *, const char *, const char *)) {
    const char *list = s->tr_prop;
    bool all = !list;
    if (list) for (const char *p = list;;) {
        const char *e = strchr(p, ','); size_t len = e ? (size_t)(e - p) : strlen(p);
        char name[64];
        while (len && *p == ' ') { p++; len--; }
        while (len && p[len - 1] == ' ') len--;
        if (len == 3 && !strncmp(p, "all", 3)) all = true;
        else if (len && len < sizeof name) { memcpy(name, p, len); name[len] = 0; if (gen_prop(name)) tr_scan_one(n, o, s, name, cb); }
        if (!e) break;
        p = e + 1;
    }
    if (!all) return;
    for (size_t i = 0; i < sizeof gen_props / sizeof *gen_props; i++) if (gen_props[i].native && strcmp(gen_props[i].name, "display")) tr_scan_one(n, o, s, gen_props[i].name, cb);
    HMap seen; memset(&seen, 0, sizeof seen);
    tr_scan_gen(n, o, s, o->gen_n, &seen, cb); tr_scan_gen(n, o, s, s->gen_n, &seen, cb);
    tr_scan_gen(n, o, s, o->gen_i, &seen, cb); tr_scan_gen(n, o, s, s->gen_i, &seen, cb);
    hm_free(&seen, NULL);
}
/* Canonical form of a multi-keyword (or rare single-keyword) display value; NULL if invalid or not handled. */
static const char *display_canon(const char *val) {
    char buf[96]; size_t n = strlen(val); if (n >= sizeof buf) return NULL;
    for (size_t i = 0; i <= n; i++) buf[i] = (char)lc(val[i]);
    static const char *const single[] = { "flow", "run-in", "ruby", "ruby-base", "ruby-text", "math", "grid-lanes", "inline-grid-lanes", NULL };
    if (!strchr(buf, ' ')) { for (const char *const *q = single; *q; q++) if (!strcmp(*q, buf)) return !strcmp(buf, "flow") ? atom("block") : atom(buf); return NULL; }
    const char *outer = NULL, *inner = NULL; bool li = false;
    for (char *t = strtok(buf, " \t\n"); t; t = strtok(NULL, " \t\n")) {
        if (!strcmp(t, "block") || !strcmp(t, "inline") || !strcmp(t, "run-in")) { if (outer) return NULL; outer = t; }
        else if (!strcmp(t, "flow") || !strcmp(t, "flow-root") || !strcmp(t, "table") || !strcmp(t, "flex") || !strcmp(t, "grid") || !strcmp(t, "ruby") || !strcmp(t, "math") || !strcmp(t, "grid-lanes")) { if (inner) return NULL; inner = t; }
        else if (!strcmp(t, "list-item")) { if (li) return NULL; li = true; }
        else return NULL;
    }
    char out[64];
    if (li) {
        if (inner && strcmp(inner, "flow") && strcmp(inner, "flow-root")) return NULL;
        snprintf(out, sizeof out, "%s%s%slist-item", outer && strcmp(outer, "block") ? outer : "", outer && strcmp(outer, "block") ? " " : "", inner && !strcmp(inner, "flow-root") ? "flow-root " : "");
        return atom(out);
    }
    if (!inner) inner = "flow";
    if (!outer) outer = !strcmp(inner, "ruby") ? "inline" : "block";
    if (!strcmp(outer, "block")) snprintf(out, sizeof out, "%s%s", !strcmp(inner, "ruby") || !strcmp(inner, "math") ? "block " : "", !strcmp(inner, "flow") ? "block" : inner);
    else if (!strcmp(outer, "inline") && (!strcmp(inner, "flow") || !strcmp(inner, "flow-root") || !strcmp(inner, "ruby") || !strcmp(inner, "math"))) snprintf(out, sizeof out, "%s", !strcmp(inner, "flow") ? "inline" : !strcmp(inner, "flow-root") ? "inline-block" : inner);
    else if (!strcmp(outer, "inline")) snprintf(out, sizeof out, "inline-%s", inner);
    else snprintf(out, sizeof out, "run-in%s%s", strcmp(inner, "flow") ? " " : "", strcmp(inner, "flow") ? inner : "");
    return atom(out);
}
/* Blockified display (CSS Display 3 §2.7) for absolutely positioned and floated boxes. */
static const char *display_blockify(const char *d) {
    static const char *const to_block[] = { "inline", "inline-block", "run-in", "ruby-base", "ruby-text", "table-row-group", "table-column", "table-column-group",
        "table-header-group", "table-footer-group", "table-row", "table-cell", "table-caption", NULL };
    for (const char *const *q = to_block; *q; q++) if (!strcmp(*q, d)) return "block";
    if (!strncmp(d, "inline-", 7)) return d + 7;
    if (!strncmp(d, "inline ", 7)) return d + 7;
    if (!strncmp(d, "run-in ", 7)) return d + 7;
    if (!strcmp(d, "ruby")) return "block ruby";
    if (!strcmp(d, "math")) return "block math";
    return d;
}
static void contain_intrinsic_str(SB *out, ContainIntrinsicSize value) {
    if (value.flags & CIS_AUTO) sb_puts(out, "auto ");
    if (value.flags & CIS_LENGTH) sb_printf(out, "%gpx", (double)value.length);
    else sb_puts(out, "none");
}
static char *computed_of(Node *el, const ComputedStyle *s, const char *prop) {
    SB b; sb_init(&b);
    if (s && (!strncmp(prop, "transition-", 11) || !strncmp(prop, "animation-", 10))) {
        const GenProp *g = gen_prop(prop); const char *v = g ? gen_value(s, g) : NULL;
        if (v) {
            if (strstr(prop, "-duration") || strstr(prop, "-delay")) {
                char *copy = xstrdup(v), *parts[32]; int count = split_top(copy, ',', parts, 32);
                bool valid = count > 0;
                for (int i = 0; i < count; i++) {
                    float value;
                    if (!css_time(parts[i], &value)) { valid = false; break; }
                    if (i) sb_puts(&b, ", ");
                    sb_printf(&b, "%gs", isnan(value) || value == 0 ? 0.0 : isinf(value) ? copysign(3.4028234663852886e38, value) : (double)value);
                }
                free(copy);
                if (valid) return sb_take(&b);
                sb_free(&b); sb_init(&b);
            }
            sb_puts(&b, v); return sb_take(&b);
        }
    }
    if (!s) return sb_take(&b);
    /* Resolve logical queries through the same cascaded physical slots.
       Pair shorthands serialize their computed longhands, not the raw tokens. */
    int group; bool block;
    if (logical_edge_pair(prop, &group, &block)) {
        char *a = computed_of(el, s, physical_edge_props[group][logical_edge_side(s, block, false)]);
        char *z = computed_of(el, s, physical_edge_props[group][logical_edge_side(s, block, true)]);
        sb_puts(&b, a);
        if (strcmp(a, z)) { sb_putc(&b, ' '); sb_puts(&b, z); }
        free(a); free(z); return sb_take(&b);
    }
    prop = logical_box_prop(prop, s);
    if (column_property(prop)) {
        bool shorthand = !strcmp(prop, "columns"), width = s->column_width.kind != LK_AUTO;
        if (!strcmp(prop, "column-width") || (shorthand && width)) {
            if (!width) sb_puts(&b, "auto"); else sb_printf(&b, "%gpx", s->column_width.px);
        }
        if (!strcmp(prop, "column-count") || (shorthand && s->column_count)) {
            if (shorthand && width) sb_putc(&b, ' ');
            if (!s->column_count) sb_puts(&b, "auto"); else sb_printf(&b, "%u", s->column_count);
        }
        if (shorthand && !width && !s->column_count) sb_puts(&b, "auto");
        return sb_take(&b);
    }
    int intrinsic_axis = contain_intrinsic_property(prop);
    if (intrinsic_axis != -2) {
        contain_intrinsic_str(&b, s->contain_intrinsic[intrinsic_axis < 0 ? 0 : intrinsic_axis]);
        if (intrinsic_axis < 0 && (s->contain_intrinsic[0].flags != s->contain_intrinsic[1].flags ||
            s->contain_intrinsic[0].length != s->contain_intrinsic[1].length)) {
            sb_putc(&b, ' '); contain_intrinsic_str(&b, s->contain_intrinsic[1]);
        }
        return sb_take(&b);
    }
    if (!strcmp(prop, "background-position") || !strcmp(prop, "background-position-x") || !strcmp(prop, "background-position-y")) return bg_position_computed(s, !strcmp(prop, "background-position-x") ? 0 : !strcmp(prop, "background-position-y") ? 1 : -1);
    static const char *const ds[] = { "none", "inline", "block", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "list-item", "table", "inline-table", "table-row", "table-cell", "table-row-group", "table-header-group", "table-footer-group", "table-column", "table-column-group", "table-caption", "contents", "flow-root" };
    static const char *const ps[] = { "static", "relative", "absolute", "fixed", "sticky" };
    static const char *const bs[] = { "none", "hidden", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset" };
    if (prop[0] == '-' && prop[1] == '-') { const char *v = custom_get(s->custom, prop); if (!v) v = css_property_initial(prop); if (v) sb_puts(&b, v); }
    else if (!strcmp(prop, "text-decoration-line") && s->text_decoration && !(gen_prop(prop) && gen_value(s, gen_prop(prop)) && strcmp(gen_value(s, gen_prop(prop)), "none"))) {   /* set by the shorthand too */
        int n = 0; static const char *const td[] = { "underline", "overline", "line-through" };
        for (int i = 0; i < 3; i++) if (s->text_decoration & (1 << i)) sb_printf(&b, "%s%s", n++ ? " " : "", td[i]);
    }
    else if (!strcmp(prop, "display")) { const char *d = s->display_text ? s->display_text : ds[s->display];
        if (s->display_text && (!el || el->ns != NS_MATHML)) { if (!strcmp(d, "math")) d = "inline"; else if (!strcmp(d, "block math")) d = "block"; }   /* math is only for MathML elements */ sb_puts(&b, (s->position == 2 || s->position == 3 || s->float_) && s->display != D_NONE ? display_blockify(d) : d); }
    else if (!strncmp(prop, "animation-", 10) || !strncmp(prop, "transition-", 11) || !strncmp(prop, "-webkit-animation-", 18) || !strncmp(prop, "-webkit-transition-", 19)) {
        const char *q = strncmp(prop, "-webkit-", 8) ? prop : prop + 8;
        if (!strcmp(q, "animation-name")) sb_puts(&b, s->anim_name ? s->anim_name : "none");
        else if (!strcmp(q, "animation-duration")) sb_printf(&b, "%gs", s->anim_dur);
        else if (!strcmp(q, "animation-delay")) sb_printf(&b, "%gs", s->anim_delay);
        else if (!strcmp(q, "animation-iteration-count")) { if (isinf(s->anim_iter)) sb_puts(&b, "infinite"); else sb_printf(&b, "%g", s->anim_iter); }
        else if (!strcmp(q, "transition-property")) sb_puts(&b, s->tr_prop ? s->tr_prop : "all");
        else if (!strcmp(q, "transition-duration")) sb_printf(&b, "%gs", s->tr_dur);
        else if (!strcmp(q, "transition-delay")) sb_printf(&b, "%gs", s->tr_delay);
    }
    else if (!strcmp(prop, "position")) sb_puts(&b, ps[s->position]);
    else if (!strcmp(prop, "visibility")) sb_puts(&b, s->visibility ? "hidden" : "visible");
    else if (!strcmp(prop, "color")) { if (s->color_ser) sb_puts(&b, s->color_ser); else color_str(&b, s->color); }
    else if (!strcmp(prop, "background-color")) { if (s->bg_ser) sb_puts(&b, s->bg_ser); else color_str(&b, s->bg_color); }
    else if (!strcmp(prop, "background-image")) {
        const Gradient *g = s->bg_gradient;
        if (s->bg_image) sb_printf(&b, "url(\"%s\")", s->bg_image);
        else if (!g) sb_puts(&b, "none");
        else if (g->ser) sb_puts(&b, g->ser);
        else {
            sb_puts(&b, g->repeating ? "repeating-" : ""); sb_puts(&b, g->type ? "radial-gradient(" : "linear-gradient(");
            if (!g->type) sb_printf(&b, "%gdeg", g->angle);
            for (int i = 0; i < g->nstops; i++) { if (i || !g->type) sb_puts(&b, ", "); color_str(&b, g->stops[i].color); sb_printf(&b, " %g%%", g->stops[i].pos * 100); }
            sb_putc(&b, ')');
        }
    }
    else if (!strcmp(prop, "font-size")) sb_printf(&b, "%gpx", s->font_size);
    else if (!strcmp(prop, "font-weight")) sb_printf(&b, "%d", s->font_weight);
    else if (!strcmp(prop, "font-family")) sb_puts(&b, s->font_family);
    else if (!strcmp(prop, "opacity")) sb_printf(&b, "%g", s->opacity);
    else if (!strcmp(prop, "line-height")) { if (s->line_height_normal) sb_puts(&b, "normal"); else if (s->line_height_factor > 0) sb_printf(&b, "%g", s->line_height_factor); else sb_printf(&b, "%gpx", s->line_height); }
    else if (!strcmp(prop, "overflow") || !strcmp(prop, "overflow-y") || !strcmp(prop, "overflow-x")) { static const char *ov[] = { "visible", "hidden", "scroll", "auto", "clip" }; sb_puts(&b, ov[prop[9] == 'x' ? s->overflow_x : s->overflow_y]); }
    else if (!strcmp(prop, "z-index")) { if (s->z_auto) sb_puts(&b, "auto"); else sb_printf(&b, "%d", s->z_index); }
    else if (!strcmp(prop, "direction")) sb_puts(&b, s->direction ? "rtl" : "ltr");
    else if (!strcmp(prop, "pointer-events")) sb_puts(&b, s->pointer_events ? "auto" : "none");
    else if (!strcmp(prop, "box-sizing")) sb_puts(&b, s->box_sizing ? "border-box" : "content-box");
    else if (!strcmp(prop, "border-spacing")) {
        sb_printf(&b, "%gpx", s->border_spacing[0]);
        if (s->border_spacing[0] != s->border_spacing[1]) sb_printf(&b, " %gpx", s->border_spacing[1]);
    }
    else if (!strcmp(prop, "transform")) { if (s->has_transform) sb_printf(&b, "matrix(%g, %g, %g, %g, %g, %g)", s->transform[0], s->transform[1], s->transform[2], s->transform[3], s->transform[4], s->transform[5]); else sb_puts(&b, "none"); }
    else if (!strncmp(prop, "margin-", 7) || !strncmp(prop, "padding-", 8)) {
        bool m = prop[0] == 'm'; const char *side = prop + (m ? 7 : 8);
        int i = !strcmp(side, "top") ? 0 : !strcmp(side, "right") ? 1 : !strcmp(side, "bottom") ? 2 : 3;
        Length l = m ? s->margin[i] : s->padding[i];
        /* rendered: the used value (resolves %); not rendered: the computed value */
        if (el->box || !(l.pct || l.pctu)) sb_printf(&b, "%gpx", el->box && (l.pct || l.pctu) ? (m ? el->box->m[i] : el->box->p[i]) : l.px);
        else if (l.kind == LK_AUTO) sb_printf(&b, "%gpx", l.px); else len_str(&b, l, "auto");
    }
    else if (!strcmp(prop, "width") || !strcmp(prop, "height")) {
        bool W = prop[0] == 'w'; const Box *bx = el->box;
        if (bx && s->display != D_INLINE) {
            float v = W ? bx->w : bx->h;
            if (!s->box_sizing) v -= W ? bx->p[1] + bx->p[3] + bx->b[1] + bx->b[3] : bx->p[0] + bx->p[2] + bx->b[0] + bx->b[2];
            sb_printf(&b, "%gpx", v > 0 ? v : 0);
        } else len_str(&b, W ? s->width : s->height, "auto");
    }
    else if (!strcmp(prop, "min-width") || !strcmp(prop, "min-height")) {
        /* The resolved automatic minimum is zero for ordinary boxes; flex and
           grid items retain auto. Logical queries arrive through the same
           mapping above, so their range and automatic-value rules agree. */
        Length minimum = !strcmp(prop, "min-width") ? s->min_width : s->min_height;
        const char *automatic = "auto";
        if (minimum.kind == LK_AUTO) {
            const Node *parent = el ? (s == el->style ? el->parent : el) : NULL;
            while (parent && parent->style && parent->style->display == D_CONTENTS) parent = parent->parent;
            const ComputedStyle *ps = parent ? parent->style : NULL;
            /* A computed-style query need not materialize layout boxes. Infer
               box generation from the computed tree rather than a lazy pointer. */
            bool box = el && (el->flags & NF_CONNECTED) && s->display != D_NONE && s->display != D_CONTENTS;
            for (const Node *a = parent; a && box; a = a->parent)
                if (a->style && a->style->display == D_NONE) box = false;
            bool item = box && s->position != P_ABSOLUTE && s->position != P_FIXED && ps &&
                (ps->display == D_FLEX || ps->display == D_INLINE_FLEX || ps->display == D_GRID || ps->display == D_INLINE_GRID);
            const GenProp *aspect = gen_prop("aspect-ratio");
            const char *ratio = aspect ? gen_value(s, aspect) : NULL;
            /* CSS Sizing's resolved-value exception requires initial auto
               aspect-ratio. Inspect the computed property, not a float that
               also represents degenerate ratios or intrinsic image ratios. */
            bool noninitial_ratio = ratio && !str_ieq(ratio, "auto");
            automatic = box && (item || noninitial_ratio) ? "auto" : "0px";
        }
        len_str(&b, minimum, automatic);
    }
    else if (!strcmp(prop, "max-width")) len_str(&b, s->max_width, "none");
    else if (!strcmp(prop, "max-height")) len_str(&b, s->max_height, "none");
    else if (!strcmp(prop, "top") || !strcmp(prop, "right") || !strcmp(prop, "bottom") || !strcmp(prop, "left")) {
        int i = prop[0] == 't' ? 0 : prop[0] == 'r' ? 1 : prop[0] == 'b' ? 2 : 3;
        Length l = s->inset[i];
        const Box *cb = NULL;
        /* positioned boxes resolve percentages against the containing block */
        if (el && el->box && s->position != P_STATIC && l.kind == LK_LEN && l.pct) {
            bool abs = s->position == P_ABSOLUTE || s->position == P_FIXED;
            for (const Node *a = el->parent; a && !cb; a = a->parent)
                if (a->type == NODE_ELEMENT && a->box && (!abs || (a->style && a->style->position != P_STATIC) || !a->parent || a->parent->type != NODE_ELEMENT)) cb = a->box;
        }
        const Box *ab = el && el->box && s->anchor_ins[i] ? el->box : NULL;
        if (ab && ab->cb && (ab->anchor_ok >> i & 1)) {   /* a resolved anchor() inset: the used value */
            const Box *c = ab->cb;
            float v = i == 3 ? ab->x - ab->m[3] - (c->x + c->b[3]) : i == 0 ? ab->y - ab->m[0] - (c->y + c->b[0])
                    : i == 1 ? c->x + c->w - c->b[1] - (ab->x + ab->w + ab->m[1]) : c->y + c->h - c->b[2] - (ab->y + ab->h + ab->m[2]);
            sb_printf(&b, "%gpx", v);
        } else if (cb) {
            bool vert = i == 0 || i == 2;
            float size = vert ? cb->h - cb->b[0] - cb->b[2] : cb->w - cb->b[1] - cb->b[3];
            if (!(s->position == P_ABSOLUTE || s->position == P_FIXED)) size -= vert ? cb->p[0] + cb->p[2] : cb->p[1] + cb->p[3];
            sb_printf(&b, "%gpx", l.px + l.pct * size / 100);
        } else len_str(&b, l, "auto");
    }
    else if (!strcmp(prop, "flex-grow")) sb_printf(&b, "%g", s->flex_grow);
    else if (!strcmp(prop, "flex-shrink")) sb_printf(&b, "%g", s->flex_shrink);
    else if (!strcmp(prop, "flex-basis")) len_str(&b, s->flex_basis, "auto");
    else if (!strcmp(prop, "flex")) { sb_printf(&b, "%g %g ", s->flex_grow, s->flex_shrink); len_str(&b, s->flex_basis, "auto"); }
    else if (!strcmp(prop, "column-gap")) gap_str(&b, s->column_gap, s->column_gap_l);
    else if (!strcmp(prop, "row-gap")) gap_str(&b, s->row_gap, s->row_gap_l);
    else if (!strcmp(prop, "gap")) { gap_str(&b, s->row_gap, s->row_gap_l); sb_putc(&b, ' '); gap_str(&b, s->column_gap, s->column_gap_l); }
    else if (!strcmp(prop, "grid-template-columns")) grid_resolved_tracks(&b, el, s, prop, 0);
    else if (!strcmp(prop, "grid-template-rows")) grid_resolved_tracks(&b, el, s, prop, 1);
    else if (!strcmp(prop, "text-align")) { static const char *ta[] = { "start", "left", "right", "center", "justify", "end" }; sb_puts(&b, ta[s->text_align % 6]); }
    else if (!strcmp(prop, "white-space")) { static const char *ws[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line", "break-spaces" }; sb_puts(&b, ws[s->white_space % 6]); }
    else if (!strcmp(prop, "flex-direction")) { static const char *fd[] = { "row", "row-reverse", "column", "column-reverse" }; sb_puts(&b, fd[s->flex_direction & 3]); }
    else if (!strcmp(prop, "flex-wrap")) { static const char *fw[] = { "nowrap", "wrap", "wrap-reverse" }; sb_puts(&b, fw[s->flex_wrap % 3]); }
    else if (!strcmp(prop, "justify-content") || !strcmp(prop, "align-items") || !strcmp(prop, "align-self") || !strcmp(prop, "align-content") || !strcmp(prop, "justify-items") || !strcmp(prop, "justify-self")) {
        static const char *al[] = { "auto", "normal", "stretch", "flex-start", "flex-end", "center", "baseline", "space-between", "space-around", "space-evenly", "start", "end" };
        uint8_t v = prop[0] == 'j' ? (prop[8] == 'c' ? s->justify_content : prop[8] == 'i' ? s->justify_items : s->justify_self)
                                   : (prop[6] == 'i' ? s->align_items : prop[6] == 's' ? s->align_self : s->align_content);
        sb_puts(&b, al[v % 12]);
    }
    else if (!strncmp(prop, "border-", 7) && (strstr(prop, "-width") || strstr(prop, "-style") || strstr(prop, "-color") || strstr(prop, "-radius"))) {
        static const char *const sides[] = { "top-", "right-", "bottom-", "left-" }, *const corners[] = { "top-left-", "top-right-", "bottom-right-", "bottom-left-" };
        const char *q = prop + 7; int i = -1;
        if (strstr(q, "-radius")) { for (int k = 0; k < 4; k++) if (!strncmp(q, corners[k], strlen(corners[k]))) i = k; if (i >= 0) len_str(&b, s->border_radius[i], "0px"); }
        else {
            for (int k = 0; k < 4; k++) if (!strncmp(q, sides[k], strlen(sides[k]))) { i = k; q += strlen(sides[k]); }
            if (i >= 0 && !strcmp(q, "width")) sb_printf(&b, "%gpx", s->border_style[i] > BS_HIDDEN ? s->border_width[i] : 0);
            else if (i >= 0 && !strcmp(q, "style")) sb_puts(&b, bs[s->border_style[i] % 10]);
            else if (i >= 0 && !strcmp(q, "color")) color_str(&b, s->border_color[i]);
        }
    }
    else if (!strcmp(prop, "outline-style")) sb_puts(&b, bs[s->outline_style % 10]);
    else if (!strcmp(prop, "outline-width")) sb_printf(&b, "%gpx", s->outline_style ? s->outline_width : 0);
    else if (!strcmp(prop, "outline-color")) color_str(&b, s->outline_color);
    else if (!strcmp(prop, "box-shadow")) {
        const Shadow *h = &s->box_shadow;
        if (!s->has_shadow) sb_puts(&b, "none");
        else { color_str(&b, h->color); sb_printf(&b, " %gpx %gpx %gpx %gpx%s", h->x, h->y, h->blur, h->spread, h->inset ? " inset" : ""); }
    }
    else if (!strcmp(prop, "order")) sb_printf(&b, "%d", s->order);
    else if (!strcmp(prop, "float")) sb_puts(&b, s->float_ == F_LEFT ? "left" : s->float_ == F_RIGHT ? "right" : "none");
    else if (!strcmp(prop, "clear")) { static const char *cl[] = { "none", "left", "right", "both" }; sb_puts(&b, cl[s->clear & 3]); }
    else if (!strcmp(prop, "text-transform")) { static const char *tt[] = { "none", "uppercase", "lowercase", "capitalize" }; sb_puts(&b, tt[s->text_transform & 3]); }
    else if (!strcmp(prop, "font-style")) sb_puts(&b, s->font_style ? "italic" : "normal");
    else if (!strcmp(prop, "vertical-align")) {
        static const char *va[] = { "baseline", "top", "middle", "bottom", "text-top", "text-bottom", "sub", "super" };
        if (s->vertical_align == VA_LENGTH) sb_printf(&b, "%gpx", s->vertical_align_len); else sb_puts(&b, va[s->vertical_align % 8]);
    }
    else if (!strcmp(prop, "object-fit")) { static const char *of[] = { "fill", "contain", "cover", "none", "scale-down" }; sb_puts(&b, of[s->object_fit % 5]); }
    else if (!strcmp(prop, "text-overflow")) sb_puts(&b, s->text_overflow == TO_ELLIPSIS ? "ellipsis" : "clip");
    else if (!strcmp(prop, "word-break")) { static const char *wb[] = { "normal", "break-all", "break-word" }; sb_puts(&b, wb[s->word_break % 3]); }
    else if (!strcmp(prop, "overflow-wrap") || !strcmp(prop, "word-wrap")) sb_puts(&b, s->overflow_wrap ? "break-word" : "normal");
    else if (!strcmp(prop, "user-select") || !strcmp(prop, "-webkit-user-select")) sb_puts(&b, s->user_select ? "none" : "auto");
    else if (!strcmp(prop, "touch-action")) { static const char *ta[] = { "auto", "pan-y", "pan-x", "none" }; sb_puts(&b, ta[s->touch_block & 3]); }
    else if (!strcmp(prop, "cursor")) { static const char *cu[] = { "auto", "pointer", "text", "move", "not-allowed", "grab" }; sb_puts(&b, cu[s->cursor % 6]); }
    else if (!strcmp(prop, "list-style-type")) {
        static const char *ls[] = { "none", "disc", "circle", "square", "decimal", "lower-alpha", "upper-alpha", "lower-roman", "upper-roman" };
        sb_puts(&b, ls[s->list_style % 9]);
    }
    else {
        const GenProp *g = gen_prop(prop);
        const char *v = g ? gen_value(s, g) : NULL;
        if (!v && g) v = g->initial;
        /* Canonical numeric/angle values include both plain dimensions and math. */
        if (v && (!strcmp(prop, "scale") || !strcmp(prop, "rotate"))) {
            float value; int unit = prop[0] == 'r' ? MATH_ANGLE : MATH_NUMBER;
            const ComputedStyle *root = el && el->doc && el->doc->html ? el->doc->html->style : NULL;
            if (el && el->doc && el == el->doc->html) root = s;
            StyleEngine *engine = el && el->doc ? el->doc->stylesheets : NULL;
            LCtx context = { .em = s->font_size, .rem = root ? root->font_size : 16,
                .font_basis = s, .actual_font = true, .root_font_basis = root,
                .resolve_font_metrics = true,
                .used_font_units = engine ? &engine->uses_font_units : NULL };
            if (css_scalar_math_context(v, unit, context, &value)) {
                const char *u = unit == MATH_ANGLE ? "deg" : "";
                if (isnan(value)) sb_printf(&b, "0%s", u);
                else if (isinf(value)) sb_printf(&b, value > 0 ? "calc(infinity%s)" : "calc(-infinity%s)", *u ? " * 1deg" : "");
                else sb_printf(&b, "%g%s", value == 0 ? 0.0 : (double)value, u);
                return sb_take(&b);
            }
        }
        if (v && !strcmp(v, "currentcolor") && g && !g->native) color_str(&b, s->color);
        else if (v) sb_puts(&b, v);
    }
    return sb_take(&b);
}

/* the width in px that a sizes attribute gives (default 100vw) */
static float sizes_px(const char *sz, const MediaCtx *mc) {
    float vw = mc ? mc->vw : 1280;
    if (!sz) return vw;
    char *nc = xstrdup(sz);   /* comments removed */
    for (char *r = nc, *w = nc;; ) {
        if (r[0] == '/' && r[1] == '*') { char *x = strstr(r + 2, "*/"); r = x ? x + 2 : r + strlen(r); *w++ = ' '; continue; }   /* a comment separates tokens */
        if (r[0] == '\\' && isalpha((unsigned char)r[1]) && !isxdigit((unsigned char)r[1])) { r++; continue; }   /* 1\p\x is 1px */
        if (r[0] == '\\' && r[1]) { *w++ = *r++; *w++ = *r++; continue; }
        if (!(*w++ = *r++)) break;
    }
    float res = vw;
    const char *p = nc;
    while (*p) {
        const char *e = p; int dep = 0;   /* a comma inside an open (, [ or { block doesn't end the entry */
        while (*e && (dep || *e != ',')) { if (*e == '\\' && e[1]) e++; else if (*e == '(' || *e == '[' || *e == '{') dep++; else if ((*e == ')' || *e == ']' || *e == '}') && dep) dep--; e++; }
        char item[512]; size_t l = (size_t)(e - p); if (l >= sizeof item - 8) l = sizeof item - 9;
        memcpy(item, p, l); item[l] = 0;
        if (!*e && dep > 0 && dep < 8) { for (int i = 0; i < dep; i++) item[l + i] = ')'; item[l + dep] = 0; }   /* blocks close at the end of input */
        char *a = item; while (isspace((unsigned char)*a)) a++;
        char *z = a + strlen(a); while (z > a && isspace((unsigned char)z[-1])) *--z = 0;
        p = *e ? e + 1 : e;
        if (!*a) continue;
        /* the last component value: a function call/parenthesized block or a token */
        char *c = z;
        if (z[-1] == ')') { int d = 0; while (c > a) { c--; if (*c == ')') d++; else if (*c == '(' && !--d) break; } while (c > a && (isalnum((unsigned char)c[-1]) || c[-1] == '-')) c--; }
        else while (c > a && !isspace((unsigned char)c[-1])) c--;
        if (*c == '(') continue;
        char *cond = a; char last[256]; snprintf(last, sizeof last, "%s", c); *c = 0;
        char *ce = cond + strlen(cond); while (ce > cond && isspace((unsigned char)ce[-1])) *--ce = 0;
        if (*cond) {   /* a <media-condition>: no media type, balanced blocks, parseable */
            const char *q = cond; if (!strncasecmp(q, "not ", 4)) { q += 4; while (*q == ' ') q++; }
            const char *w = q; while (isalnum((unsigned char)*w) || *w == '-' || *w == '_') w++;
            int bal = 0; bool ok = *q == '(' || (w > q && *w == '(');
            for (const char *x = cond; *x && ok; x++) { if (*x == '(' || *x == '[' || *x == '{') bal++; else if (*x == ')' || *x == ']' || *x == '}') { if (--bal < 0) ok = false; } else if (*x == '\\') ok = false; }
            if (!ok || bal || !css_media_valid(cond)) continue;
        }
        if (!strcasecmp(last, "auto")) { if (!*cond) break; continue; }
        if (strchr(last, '\\')) continue;
        Length L;
        bool math = !strncasecmp(last, "min(", 4) || !strncasecmp(last, "max(", 4) || !strncasecmp(last, "clamp(", 6) || !strncasecmp(last, "calc(", 5);
        if (!css_parse_length(last, &L, 16, 16, mc) || L.pct || L.kind != LK_LEN || (L.px < 0 && !math) || !isfinite(L.px)) continue;
        if (L.px < 0) L.px = 0;   /* a math function's result is clamped to the allowed range */
        if (math) { bool z = false; for (const char *d = strchr(last, '/'); d && !z; d = strchr(d + 1, '/')) { const char *q = d + 1; while (*q == ' ') q++; if (*q == '0' || *q == '-' || *q == '+') { if (*q != '0') q++; while (*q == '0') q++; if (*q == '.') { q++; while (*q == '0') q++; } z = !isdigit((unsigned char)*q) && q[-1] == '0'; } } if (z) continue; }   /* x / 0 is infinite or NaN: invalid */
        const char *u = last; if (*u == '+' || *u == '-') u++;
        while (isdigit((unsigned char)*u) || *u == '.') u++;
        if ((*u == 'e' || *u == 'E') && (isdigit((unsigned char)u[1]) || ((u[1] == '+' || u[1] == '-') && isdigit((unsigned char)u[2])))) { u += 2; while (isdigit((unsigned char)*u)) u++; if (*u == '.') continue; }   /* 1e1.5px */
        static const char *const bad[] = { "deg", "grad", "rad", "turn", "s", "ms", "hz", "khz", "dpi", "dpcm", "dppx", "x" };
        bool angle = false; for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) if (!strcasecmp(u, bad[i])) angle = true;
        if (angle) continue;
        if (!strcmp(last, "0") || !strcmp(last, "-0") || !strcmp(last, "+0")) L.px = 0;
        else if (!*u && isdigit((unsigned char)last[strlen(last) - 1])) continue;
        if (*cond && !css_media_matches(cond, mc)) continue;
        res = L.px; break;
    }
    free(nc);
    return res;
}
static bool digits_only(const char *t) { if (!*t) return false; for (; *t; t++) if (!isdigit((unsigned char)*t)) return false; return true; }
/* an HTML valid floating-point number: -?digits(.digits)?(e[+-]?digits)? or -?.digits... */
static bool valid_float(const char *t) {
    if (*t == '-') t++;
    bool dig = false; while (isdigit((unsigned char)*t)) { t++; dig = true; }
    if (*t == '.') { t++; if (!isdigit((unsigned char)*t)) return false; while (isdigit((unsigned char)*t)) t++; dig = true; }
    if (!dig) return false;
    if (*t == 'e' || *t == 'E') { t++; if (*t == '+' || *t == '-') t++; if (!isdigit((unsigned char)*t)) return false; while (isdigit((unsigned char)*t)) t++; }
    return !*t;
}
/* srcset candidates (HTML "parse a srcset attribute"); picks the one with the smallest density >= dpr, else the
   densest. Returns false when none parse. */
static bool srcset_pick(const char *ss, const char *sz, const char *src, const MediaCtx *mc, char *buf, size_t bn, float *dens) {
    float dpr = mc && mc->dpr > 0 ? mc->dpr : 1, width = -1, best = -1; bool have1x = false, anyw = false; size_t bl = 0; const char *bu = NULL;
    for (int pass = 0; pass < 2; pass++) {
        const char *p = ss;
        while (p && *p) {
            while (isspace((unsigned char)*p) || *p == ',') p++;
            if (!*p) break;
            const char *u = p; while (*p && !isspace((unsigned char)*p)) p++;
            size_t ul = (size_t)(p - u); bool trailing = false;
            while (ul && u[ul - 1] == ',') { ul--; trailing = true; }
            float d = 1; bool ok = true, gotd = false, gotw = false, goth = false;
            if (!trailing) {
                for (;;) {
                    while (isspace((unsigned char)*p)) p++;
                    if (!*p || *p == ',') { if (*p) p++; break; }
                    const char *t = p; int dep = 0;
                    while (*p && (dep || (!isspace((unsigned char)*p) && *p != ','))) { if (*p == '(') dep++; else if (*p == ')' && dep) dep--; p++; }
                    size_t tl = (size_t)(p - t); char tok[64]; if (tl >= sizeof tok) { ok = false; continue; }
                    memcpy(tok, t, tl); tok[tl] = 0; char last = tok[tl - 1]; tok[tl - 1] = 0; char *end;
                    if (last == 'w' && tl > 1) { long w = strtol(tok, &end, 10); if (*end || w <= 0 || !digits_only(tok) || gotd || gotw) ok = false; else { gotw = true; d = (float)w; } }
                    else if (last == 'x' && tl > 1) { double x = strtod(tok, &end); if (*end || x < 0 || !valid_float(tok) || gotd || gotw || goth) ok = false; else { gotd = true; d = (float)x; } }
                    else if (last == 'h' && tl > 1) { long h = strtol(tok, &end, 10); if (*end || h <= 0 || !digits_only(tok) || goth || gotd) ok = false; else goth = true; }
                    else ok = false;
                }
                if (goth && !gotw) ok = false;
            }
            if (!ok || !ul) continue;
            if (pass == 0) { if (gotw) anyw = true; else if (d == 1) have1x = true; continue; }
            if (gotw) { if (width < 0) width = sizes_px(sz, mc); d = width > 0 ? d / width : INFINITY; }
            bool better = !bu || (best < dpr ? d > best : d >= dpr && d < best);
            if (better) { bu = u; bl = ul; best = d; }
        }
    }
    if (src && *src && !have1x && !anyw) { float d = 1; bool better = !bu || (best < dpr ? d > best : d >= dpr && d < best); if (better) { bu = src; bl = strlen(src); best = 1; } }
    if (!bu || bn < 2) return false;
    if (bl >= bn) bl = bn - 1;
    memcpy(buf, bu, bl); buf[bl] = 0; *dens = best > 0 ? best : 1;
    return true;
}
static bool img_type_ok(const char *t) {
    if (!t || !*t) return true;
    static const char *const ok[] = { "image/png", "image/jpeg", "image/jpg", "image/gif", "image/webp", "image/bmp", "image/svg+xml", "image/x-icon", "image/vnd.microsoft.icon", "image/apng" };
    char m[64]; size_t i = 0; while (t[i] && t[i] != ';' && i < sizeof m - 1) { m[i] = (char)tolower((unsigned char)t[i]); i++; } m[i] = 0;
    while (i && isspace((unsigned char)m[i - 1])) m[--i] = 0;
    const char *b = m; while (isspace((unsigned char)*b)) b++;
    for (size_t k = 0; k < sizeof ok / sizeof *ok; k++) if (!strcmp(b, ok[k])) return true;
    return false;
}
/* sizes="auto" (first entry) on a lazy-loading img is its used width: the laid-out content width, else 300px */
static bool sizes_is_auto(const char *a) {
    while (a && isspace((unsigned char)*a)) a++;
    return a && !strncasecmp(a, "auto", 4) && (!a[4] || a[4] == ',' || isspace((unsigned char)a[4]));
}
/* a lazy img with sizes="auto" is size-contained at 300x150 (the UA sheet's contain-intrinsic-size), so its
   width never depends on its own load */
bool css_img_sizes_auto(const Node *img) {
    const char *lz = node_attr(img, "loading");
    return lz && !strcasecmp(lz, "lazy") && sizes_is_auto(node_attr(img, "sizes"));
}
static const char *sizes_auto(const Node *img, const char *sz, char *out, size_t n) {
    if (!sizes_is_auto(sz) || !css_img_sizes_auto(img)) return sz;
    const Box *b = img->box;
    Node *m = (Node *)img;   /* unrendered (no box): its last rendered width, else 300px */
    float w = b ? fmaxf(0, b->w - b->b[1] - b->b[3] - b->p[1] - b->p[3]) : m->img_auto_w > 0 ? m->img_auto_w - 1 : 300;
    if (m->img_auto_w != w + 1) {   /* a new width picks again: the loader refetches on a resource change */ if (m->img_auto_w && m->doc) { m->doc->resource_version++; m->doc->dom_version++; }   /* sync_images runs on a DOM change */ m->img_auto_w = w + 1; }
    snprintf(out, n, "%gpx", w);
    return out;
}
bool css_img_source(const Node *img, char *buf, size_t bn, float *dens) {
    char autosz[32];
    const Document *d = img->doc;
    const StyleEngine *e = d ? d->stylesheets : NULL;
    const MediaCtx *mc = e ? &e->media : NULL;
    *dens = 1;
    const Node *pic = img->parent;
    if (pic && node_is(pic, atom("picture")))
        for (const Node *s = pic->first; s && s != img; s = s->next) {
            if (!node_is(s, atom("source"))) continue;
            const char *ss = node_attr(s, "srcset"), *m = node_attr(s, "media");
            if (!ss || !*ss || !img_type_ok(node_attr(s, "type")) || (m && *m && (m[strspn(m, " \t\n\r\f,")] ? !css_media_matches(m, mc) : !!strchr(m, ',')))) continue;
            const char *ssz = node_attr(s, "sizes");   /* a source without sizes follows the img's sizes="auto" */
            if (!ssz) { const char *isz = sizes_auto(img, node_attr(img, "sizes"), autosz, sizeof autosz); if (isz == autosz) ssz = isz; }
            else ssz = sizes_auto(img, ssz, autosz, sizeof autosz);
            if (srcset_pick(ss, ssz, NULL, mc, buf, bn, dens)) return true;
        }
    const char *ss = node_attr(img, "srcset"), *src = node_attr(img, "src");
    if (ss && *ss) return srcset_pick(ss, sizes_auto(img, node_attr(img, "sizes"), autosz, sizeof autosz), src, mc, buf, bn, dens);
    if (!src) return false;
    snprintf(buf, bn, "%s", src);
    return true;
}
