/* Cascade, inheritance and computed values */
#include "css.h"
#include "../layout/layout.h"
#include "../base/url.h"
#include <math.h>
#include <ctype.h>

typedef VEC(Rule *) RuleVec;

/* ---------------- style objects ---------------- */
ComputedStyle *style_new_default(void) {
    ComputedStyle *s = xcalloc(1, sizeof *s);
    s->refs = 1; s->anim_iter = 1;
    s->display = D_INLINE; s->font_size = 16; s->font_weight = 400; s->line_height_normal = true;
    s->color = RGBA(0, 0, 0, 255); s->opacity = 1; s->flex_shrink = 1; s->flex_basis = L_auto();
    s->width = s->height = L_auto(); s->min_width = s->min_height = L_auto();
    s->max_width.kind = s->max_height.kind = LK_NONE;
    for (int i = 0; i < 4; i++) { s->inset[i] = L_auto(); s->border_color[i] = s->color; s->border_width[i] = 3; }
    s->z_auto = true; s->font_family = atom("sans-serif");
    s->list_style = LST_DISC; s->align_items = AL_NORMAL; s->align_self = AL_AUTO; s->justify_content = AL_NORMAL; s->align_content = AL_NORMAL;
    s->bg_size[0] = s->bg_size[1] = L_auto();
    s->transform_origin[0].pct = 50; s->transform_origin[1].pct = 50;
    s->filter_brightness = 1; s->caret_color = 0; s->fill = RGBA(0, 0, 0, 255); s->stroke_width = 1;
    s->grid_col_span = s->grid_row_span = 1; s->grid_auto_rows = L_auto();
    s->pointer_events = 1;
    return s;
}
static CustomProps *custom_ref(CustomProps *c) { if (c) c->refs++; return c; }
static void custom_unref(CustomProps *c) { if (c && --c->refs == 0) { hm_free(&c->map, free); free(c); } }

ComputedStyle *style_inherit(const ComputedStyle *p) {
    ComputedStyle *s = style_new_default();
    if (!p) return s;
    s->color = p->color; s->font_size = p->font_size; s->font_weight = p->font_weight; s->font_style = p->font_style; s->font_family = p->font_family;
    s->line_height = p->line_height; s->line_height_normal = p->line_height_normal; s->line_height_factor = p->line_height_factor;
    s->text_align = p->text_align; s->white_space = p->white_space; s->text_transform = p->text_transform; s->visibility = p->visibility;
    s->letter_spacing = p->letter_spacing; s->word_spacing = p->word_spacing; s->text_indent = p->text_indent; s->list_style = p->list_style;
    s->cursor = p->cursor; s->pointer_events = p->pointer_events; s->word_break = p->word_break; s->overflow_wrap = p->overflow_wrap; s->direction = p->direction;
    s->has_text_shadow = p->has_text_shadow; s->text_shadow = p->text_shadow; s->caret_color = p->caret_color; s->fill = p->fill; s->stroke = p->stroke; s->stroke_width = p->stroke_width;
    s->user_select = p->user_select; s->border_collapse = p->border_collapse; s->writing_mode = p->writing_mode;
    s->custom = custom_ref(p->custom);
    for (int i = 0; i < 4; i++) s->border_color[i] = s->color;
    s->outline_color = s->color;
    return s;
}
void style_ref(ComputedStyle *s) { if (s) s->refs++; }
void style_free(ComputedStyle *s) {
    if (!s || --s->refs > 0) return;
    free(s->bg_image); free(s->bg_gradient); free(s->content); free(s->grid_cols); free(s->grid_rows); free(s->grid_areas); free(s->grid_area);
    custom_unref(s->custom);
    if (s->before) style_free(s->before);
    if (s->after) style_free(s->after);
    free(s);
}

/* ---------------- colors ---------------- */
static const struct { const char *n; uint32_t c; } named_colors[] = {
{"aliceblue",0xf0f8ff},{"antiquewhite",0xfaebd7},{"aqua",0x00ffff},{"aquamarine",0x7fffd4},{"azure",0xf0ffff},{"beige",0xf5f5dc},{"bisque",0xffe4c4},{"black",0x000000},{"blanchedalmond",0xffebcd},{"blue",0x0000ff},{"blueviolet",0x8a2be2},{"brown",0xa52a2a},{"burlywood",0xdeb887},{"cadetblue",0x5f9ea0},{"chartreuse",0x7fff00},{"chocolate",0xd2691e},{"coral",0xff7f50},{"cornflowerblue",0x6495ed},{"cornsilk",0xfff8dc},{"crimson",0xdc143c},{"cyan",0x00ffff},{"darkblue",0x00008b},{"darkcyan",0x008b8b},{"darkgoldenrod",0xb8860b},{"darkgray",0xa9a9a9},{"darkgreen",0x006400},{"darkgrey",0xa9a9a9},{"darkkhaki",0xbdb76b},{"darkmagenta",0x8b008b},{"darkolivegreen",0x556b2f},{"darkorange",0xff8c00},{"darkorchid",0x9932cc},{"darkred",0x8b0000},{"darksalmon",0xe9967a},{"darkseagreen",0x8fbc8f},{"darkslateblue",0x483d8b},{"darkslategray",0x2f4f4f},{"darkslategrey",0x2f4f4f},{"darkturquoise",0x00ced1},{"darkviolet",0x9400d3},{"deeppink",0xff1493},{"deepskyblue",0x00bfff},{"dimgray",0x696969},{"dimgrey",0x696969},{"dodgerblue",0x1e90ff},{"firebrick",0xb22222},{"floralwhite",0xfffaf0},{"forestgreen",0x228b22},{"fuchsia",0xff00ff},{"gainsboro",0xdcdcdc},{"ghostwhite",0xf8f8ff},{"gold",0xffd700},{"goldenrod",0xdaa520},{"gray",0x808080},{"green",0x008000},{"greenyellow",0xadff2f},{"grey",0x808080},{"honeydew",0xf0fff0},{"hotpink",0xff69b4},{"indianred",0xcd5c5c},{"indigo",0x4b0082},{"ivory",0xfffff0},{"khaki",0xf0e68c},{"lavender",0xe6e6fa},{"lavenderblush",0xfff0f5},{"lawngreen",0x7cfc00},{"lemonchiffon",0xfffacd},{"lightblue",0xadd8e6},{"lightcoral",0xf08080},{"lightcyan",0xe0ffff},{"lightgoldenrodyellow",0xfafad2},{"lightgray",0xd3d3d3},{"lightgreen",0x90ee90},{"lightgrey",0xd3d3d3},{"lightpink",0xffb6c1},{"lightsalmon",0xffa07a},{"lightseagreen",0x20b2aa},{"lightskyblue",0x87cefa},{"lightslategray",0x778899},{"lightslategrey",0x778899},{"lightsteelblue",0xb0c4de},{"lightyellow",0xffffe0},{"lime",0x00ff00},{"limegreen",0x32cd32},{"linen",0xfaf0e6},{"magenta",0xff00ff},{"maroon",0x800000},{"mediumaquamarine",0x66cdaa},{"mediumblue",0x0000cd},{"mediumorchid",0xba55d3},{"mediumpurple",0x9370db},{"mediumseagreen",0x3cb371},{"mediumslateblue",0x7b68ee},{"mediumspringgreen",0x00fa9a},{"mediumturquoise",0x48d1cc},{"mediumvioletred",0xc71585},{"midnightblue",0x191970},{"mintcream",0xf5fffa},{"mistyrose",0xffe4e1},{"moccasin",0xffe4b5},{"navajowhite",0xffdead},{"navy",0x000080},{"oldlace",0xfdf5e6},{"olive",0x808000},{"olivedrab",0x6b8e23},{"orange",0xffa500},{"orangered",0xff4500},{"orchid",0xda70d6},{"palegoldenrod",0xeee8aa},{"palegreen",0x98fb98},{"paleturquoise",0xafeeee},{"palevioletred",0xdb7093},{"papayawhip",0xffefd5},{"peachpuff",0xffdab9},{"peru",0xcd853f},{"pink",0xffc0cb},{"plum",0xdda0dd},{"powderblue",0xb0e0e6},{"purple",0x800080},{"rebeccapurple",0x663399},{"red",0xff0000},{"rosybrown",0xbc8f8f},{"royalblue",0x4169e1},{"saddlebrown",0x8b4513},{"salmon",0xfa8072},{"sandybrown",0xf4a460},{"seagreen",0x2e8b57},{"seashell",0xfff5ee},{"sienna",0xa0522d},{"silver",0xc0c0c0},{"skyblue",0x87ceeb},{"slateblue",0x6a5acd},{"slategray",0x708090},{"slategrey",0x708090},{"snow",0xfffafa},{"springgreen",0x00ff7f},{"steelblue",0x4682b4},{"tan",0xd2b48c},{"teal",0x008080},{"thistle",0xd8bfd8},{"tomato",0xff6347},{"turquoise",0x40e0d0},{"violet",0xee82ee},{"wheat",0xf5deb3},{"white",0xffffff},{"whitesmoke",0xf5f5f5},{"yellow",0xffff00},{"yellowgreen",0x9acd32},
{"canvas",0xffffff},{"canvastext",0x000000},{"linktext",0x0000ee},{"buttonface",0xefefef},{"buttontext",0x000000},{"field",0xffffff},{"fieldtext",0x000000},{"graytext",0x808080},{"highlight",0x3390ff},{"highlighttext",0xffffff},{"buttonborder",0x767676},{"windowtext",0x000000},{"window",0xffffff},{"menu",0xffffff}};

static float hue2rgb(float p, float q, float t) { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1.f / 6) return p + (q - p) * 6 * t; if (t < .5f) return q; if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6; return p; }

static int parse_fn_args(const char *s, float *v, bool *pct, int max) {
    int n = 0; const char *p = s;
    while (*p && n < max) {
        while (*p && (is_ws((unsigned char)*p) || *p == ',' || *p == '/')) p++;
        if (!*p || *p == ')') break;
        char *e; float x = strtof(p, &e);
        if (e == p) { if (str_istarts(p, "none")) { x = 0; e = (char *)p + 4; } else break; }
        pct[n] = *e == '%';
        if (str_istarts(e, "deg")) e += 3; else if (str_istarts(e, "turn")) { x *= 360; e += 4; } else if (str_istarts(e, "rad")) { x *= 57.2958f; e += 3; }
        if (*e == '%') e++;
        v[n++] = x; p = e;
    }
    return n;
}

bool css_parse_color(const char *in, Color *out, Color current) {
    while (is_ws((unsigned char)*in)) in++;
    char buf[128]; size_t L = strlen(in); if (L >= sizeof buf) L = sizeof buf - 1;
    memcpy(buf, in, L); buf[L] = 0; while (L && is_ws((unsigned char)buf[L - 1])) buf[--L] = 0;
    for (size_t i = 0; i < L; i++) buf[i] = (char)lc((unsigned char)buf[i]);
    const char *s = buf;
    if (s[0] == '#') {
        unsigned v = 0; size_t n = strlen(s + 1);
        for (size_t i = 1; s[i]; i++) if (!isxdigit((unsigned char)s[i])) return false;
        v = (unsigned)strtoul(s + 1, NULL, 16);
        if (n == 3) { *out = RGBA(((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, 255); return true; }
        if (n == 4) { *out = RGBA(((v >> 12) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17); return true; }
        if (n == 6) { *out = 0xff000000u | v; return true; }
        if (n == 8) { *out = ((v & 0xff) << 24) | (v >> 8); return true; }
        return false;
    }
    if (!strcmp(s, "transparent")) { *out = 0; return true; }
    if (!strcmp(s, "currentcolor")) { *out = current; return true; }
    if (str_starts(s, "rgb(") || str_starts(s, "rgba(")) {
        float v[4] = {0, 0, 0, 1}; bool pc[4] = {0}; int n = parse_fn_args(strchr(s, '(') + 1, v, pc, 4);
        if (n < 3) return false;
        int c[3]; for (int i = 0; i < 3; i++) c[i] = (int)LCLAMP(pc[i] ? v[i] * 2.55f : v[i], 0, 255);
        float a = n > 3 ? (pc[3] ? v[3] / 100 : v[3]) : 1;
        *out = RGBA(c[0], c[1], c[2], (int)LCLAMP(a * 255 + .5f, 0, 255)); return true;
    }
    if (str_starts(s, "hsl(") || str_starts(s, "hsla(")) {
        float v[4] = {0, 0, 0, 1}; bool pc[4] = {0}; int n = parse_fn_args(strchr(s, '(') + 1, v, pc, 4);
        if (n < 3) return false;
        float h = fmodf(v[0], 360) / 360; if (h < 0) h += 1; float sat = v[1] / 100, l = v[2] / 100;
        float q = l < .5f ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
        float a = n > 3 ? (pc[3] ? v[3] / 100 : v[3]) : 1;
        *out = RGBA((int)(hue2rgb(p, q, h + 1.f / 3) * 255 + .5f), (int)(hue2rgb(p, q, h) * 255 + .5f), (int)(hue2rgb(p, q, h - 1.f / 3) * 255 + .5f), (int)LCLAMP(a * 255 + .5f, 0, 255));
        return true;
    }
    if (str_starts(s, "color-mix(")) {
        /* approximate: take first color */
        const char *c = strchr(s, ','); if (!c) return false;
        char t[96]; snprintf(t, sizeof t, "%s", c + 1); char *e = strpbrk(t, " ,"); if (e && *e == ' ' && strchr(t, ',')) { } char *comma = strchr(t, ','); if (comma) *comma = 0;
        char *pct = strrchr(t, ' '); if (pct && strchr(pct, '%')) *pct = 0;
        return css_parse_color(t, out, current);
    }
    if (str_starts(s, "light-dark(")) { char t[96]; snprintf(t, sizeof t, "%s", s + 11); char *c = strchr(t, ','); if (c) *c = 0; return css_parse_color(t, out, current); }
    if (str_starts(s, "oklch(") || str_starts(s, "lab(") || str_starts(s, "lch(") || str_starts(s, "oklab(")) { float v[4] = {0, 0, 0, 1}; bool pc[4] = {0}; int n = parse_fn_args(strchr(s, '(') + 1, v, pc, 4); if (n < 3) return false; int g = (int)LCLAMP((pc[0] ? v[0] / 100 : (s[0] == 'o' ? v[0] : v[0] / 100)) * 255, 0, 255); *out = RGBA(g, g, g, (int)(LCLAMP(n > 3 ? v[3] : 1, 0, 1) * 255)); return true; }
    int lo = 0, hi = (int)ARRLEN(named_colors) - 1;
    for (int i = 0; i <= hi; i++) if (!strcmp(named_colors[i].n, s)) { *out = 0xff000000u | named_colors[i].c; return true; }
    (void)lo;
    return false;
}

/* ---------------- lengths ---------------- */
typedef struct { float em, rem; const MediaCtx *mc; } LCtx;
static bool calc_expr(const char **pp, Length *out, LCtx *c);
static bool parse_dim(const char **pp, Length *out, LCtx *c) {
    const char *p = *pp; while (is_ws((unsigned char)*p)) p++;
    if (*p == '(') { p++; if (!calc_expr(&p, out, c)) return false; while (is_ws((unsigned char)*p)) p++; if (*p == ')') p++; *pp = p; return true; }
    if (str_istarts(p, "calc(") || str_istarts(p, "-webkit-calc(")) { p = strchr(p, '(') + 1; if (!calc_expr(&p, out, c)) return false; while (is_ws((unsigned char)*p)) p++; if (*p == ')') p++; *pp = p; return true; }
    if (str_istarts(p, "min(") || str_istarts(p, "max(") || str_istarts(p, "clamp(")) {
        int kind = lc(p[1]) == 'i' ? 0 : lc(p[1]) == 'a' ? 1 : 2;
        p = strchr(p, '(') + 1; Length v[3]; int n = 0;
        while (n < 3) { if (!calc_expr(&p, &v[n], c)) return false; n++; while (is_ws((unsigned char)*p)) p++; if (*p == ',') { p++; continue; } break; }
        if (*p == ')') p++;
        *pp = p;
        /* when percentages are involved we can't resolve; pick a reasonable operand */
        if (kind == 2 && n == 3) { Length r = v[1]; if (!r.pct && !v[0].pct && r.px < v[0].px) r = v[0]; if (!r.pct && !v[2].pct && r.px > v[2].px) r = v[2]; *out = r; return true; }
        Length best = v[0];
        for (int i = 1; i < n; i++) { if (v[i].pct || best.pct) { if (kind == 0 && v[i].pct && !best.pct) {} else if (!v[i].pct) best = kind == 0 ? best : v[i]; continue; } if (kind == 0 ? v[i].px < best.px : v[i].px > best.px) best = v[i]; }
        *out = best; return true;
    }
    if (str_istarts(p, "var(")) return false;
    char *e; float v = strtof(p, &e);
    if (e == p) return false;
    p = e;
    Length l = { 0, 0, LK_LEN };
    char u[8] = {0}; int k = 0; while (isalpha((unsigned char)*p) && k < 7) u[k++] = (char)lc(*p++);
    if (*p == '%') { l.pct = v; p++; }
    else if (!k || !strcmp(u, "px")) l.px = v;
    else if (!strcmp(u, "em")) l.px = v * c->em;
    else if (!strcmp(u, "rem")) l.px = v * c->rem;
    else if (!strcmp(u, "ex")) l.px = v * c->em * 0.5f;
    else if (!strcmp(u, "ch")) l.px = v * c->em * 0.55f;
    else if (!strcmp(u, "cap") || !strcmp(u, "ic") || !strcmp(u, "lh")) l.px = v * c->em * (u[0] == 'l' ? 1.2f : 0.7f);
    else if (!strcmp(u, "pt")) l.px = v * 4.f / 3.f;
    else if (!strcmp(u, "pc")) l.px = v * 16;
    else if (!strcmp(u, "in")) l.px = v * 96;
    else if (!strcmp(u, "cm")) l.px = v * 96 / 2.54f;
    else if (!strcmp(u, "mm")) l.px = v * 96 / 25.4f;
    else if (!strcmp(u, "q")) l.px = v * 96 / 101.6f;
    else if (!strcmp(u, "vw") || !strcmp(u, "svw") || !strcmp(u, "lvw") || !strcmp(u, "dvw")) l.px = v * (c->mc ? c->mc->vw : 1024) / 100;
    else if (!strcmp(u, "vh") || !strcmp(u, "svh") || !strcmp(u, "lvh") || !strcmp(u, "dvh")) l.px = v * (c->mc ? c->mc->vh : 768) / 100;
    else if (!strcmp(u, "vmin")) l.px = v * LMIN(c->mc ? c->mc->vw : 1024, c->mc ? c->mc->vh : 768) / 100;
    else if (!strcmp(u, "vmax")) l.px = v * LMAX(c->mc ? c->mc->vw : 1024, c->mc ? c->mc->vh : 768) / 100;
    else if (!strcmp(u, "fr")) { l.px = v; }
    else if (!strcmp(u, "deg") || !strcmp(u, "s") || !strcmp(u, "ms")) l.px = v;
    else return false;
    *out = l; *pp = p; return true;
}
static bool calc_term(const char **pp, Length *out, LCtx *c) {
    if (!parse_dim(pp, out, c)) return false;
    for (;;) {
        const char *p = *pp; while (is_ws((unsigned char)*p)) p++;
        if (*p == '*' || *p == '/') {
            char op = *p++; Length r; if (!parse_dim(&p, &r, c)) return false;
            if (op == '*') { float f = r.pct ? 1 : r.px; if (r.pct == 0 && out->pct == 0 && false) {} if (out->pct == 0 && out->px != 0 && r.pct) { float k = out->px; *out = r; out->px *= k; out->pct *= k; } else { out->px *= f; out->pct *= f; } }
            else { if (r.px) { out->px /= r.px; out->pct /= r.px; } }
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
            char op = *p++; Length r; if (!calc_term(&p, &r, c)) return false;
            if (op == '+') { out->px += r.px; out->pct += r.pct; } else { out->px -= r.px; out->pct -= r.pct; }
            *pp = p;
        } else break;
    }
    return true;
}
bool css_parse_length(const char *s, Length *out, float em, float rem, const MediaCtx *mc) {
    while (is_ws((unsigned char)*s)) s++;
    if (str_istarts(s, "auto")) { *out = L_auto(); return true; }
    if (str_istarts(s, "none")) { out->kind = LK_NONE; out->px = out->pct = 0; return true; }
    if (str_istarts(s, "min-content")) { out->kind = LK_MIN_CONTENT; out->px = out->pct = 0; return true; }
    if (str_istarts(s, "max-content")) { out->kind = LK_MAX_CONTENT; out->px = out->pct = 0; return true; }
    if (str_istarts(s, "fit-content") || str_istarts(s, "-webkit-fit-content") || str_istarts(s, "-moz-fit-content")) { out->kind = LK_FIT_CONTENT; out->px = out->pct = 0; return true; }
    LCtx c = { em, rem, mc };
    const char *p = s;
    if (!parse_dim(&p, out, &c)) return false;
    return true;
}

/* ---------------- var() substitution ---------------- */
static char *subst_vars(const char *v, const ComputedStyle *st, int depth) {
    if (!strstr(v, "var(") || depth > 16) return xstrdup(v);
    SB b; sb_init(&b);
    const char *p = v;
    while (*p) {
        const char *q = strstr(p, "var(");
        if (!q) { sb_puts(&b, p); break; }
        sb_put(&b, p, (size_t)(q - p));
        const char *a = q + 4; int d = 1; const char *e = a;
        while (*e && d) { if (*e == '(') d++; else if (*e == ')') d--; if (d) e++; }
        char *inner = xstrndup(a, (size_t)(e - a));
        char *comma = NULL; int dd = 0; for (char *c = inner; *c; c++) { if (*c == '(') dd++; else if (*c == ')') dd--; else if (*c == ',' && !dd) { comma = c; break; } }
        if (comma) *comma = 0;
        char *name = str_trim(inner);
        const char *val = st->custom ? hm_get((HMap *)&st->custom->map, name) : NULL;
        char *r = val && *val ? subst_vars(val, st, depth + 1) : comma ? subst_vars(str_trim(comma + 1), st, depth + 1) : NULL;
        free(inner);
        /* unresolvable or runaway expansion: declaration is invalid at computed-value time */
        if (!r || b.n + strlen(r) > (1u << 16)) { free(r); sb_free(&b); return NULL; }
        sb_puts(&b, r); free(r);
        p = *e ? e + 1 : e;
    }
    return sb_take(&b);
}

/* ---------------- property application ---------------- */
static int kw(const char *v, const char *const *list) { for (int i = 0; list[i]; i++) if (str_ieq(v, list[i])) return i; return -1; }
static char *next_token(const char **pp) {
    const char *p = *pp; while (is_ws((unsigned char)*p)) p++;
    if (!*p) return NULL;
    const char *s = p; int d = 0;
    while (*p && (d || !is_ws((unsigned char)*p))) { if (*p == '(') d++; else if (*p == ')') d--; else if (*p == '"' || *p == '\'') { char q = *p++; while (*p && *p != q) p++; if (!*p) break; } p++; }
    *pp = p; return xstrndup(s, (size_t)(p - s));
}
static int split_ws(const char *v, char **out, int max) { int n = 0; const char *p = v; char *t; while (n < max && (t = next_token(&p))) out[n++] = t; return n; }
static void free_toks(char **t, int n) { for (int i = 0; i < n; i++) free(t[i]); }

typedef struct { ComputedStyle *st; const ComputedStyle *par; StyleEngine *e; Node *el; float em, rem; const MediaCtx *mc; } ACtx;
static bool alen(ACtx *c, const char *v, Length *out) { return css_parse_length(v, out, c->em, c->rem, c->mc); }
static float alen_px(ACtx *c, const char *v, float def) { Length l; if (alen(c, v, &l) && l.kind == LK_LEN) return l.px; return def; }

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
    if (g->nstops < 2) { if (g->nstops == 1) { g->stops[1] = g->stops[0]; g->nstops = 2; } else { free(g); return NULL; } }
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
static void apply_background_image(ACtx *c, const char *v) {
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
        char *abs = url_join(base, u); free(u); c->st->bg_image = abs;
    }
}
static void apply_bg_pos(ACtx *c, const char *v) {
    char *t[4]; int n = split_ws(v, t, 4); Length l[2] = { {0, 0, LK_LEN}, {0, 50, LK_LEN} };
    int k = 0; bool xset = false, yset = false;
    for (int i = 0; i < n; i++) {
        if (str_ieq(t[i], "left")) { l[0] = (Length){0, 0, LK_LEN}; xset = true; }
        else if (str_ieq(t[i], "right")) { l[0] = (Length){0, 100, LK_LEN}; xset = true; }
        else if (str_ieq(t[i], "top")) { l[1] = (Length){0, 0, LK_LEN}; yset = true; }
        else if (str_ieq(t[i], "bottom")) { l[1] = (Length){0, 100, LK_LEN}; yset = true; }
        else if (str_ieq(t[i], "center")) { if (!xset && k == 0) { l[0] = (Length){0, 50, LK_LEN}; xset = true; } else { l[1] = (Length){0, 50, LK_LEN}; yset = true; } }
        else { Length x; if (alen(c, t[i], &x)) { if (!xset) { l[0] = x; xset = true; } else { l[1] = x; yset = true; } } }
        k++;
    }
    if (n == 1 && !yset) l[1] = (Length){0, 50, LK_LEN};
    c->st->bg_pos[0] = l[0]; c->st->bg_pos[1] = l[1];
    free_toks(t, n);
}
static void apply_bg_size(ACtx *c, const char *v) {
    if (str_ieq(v, "cover")) { c->st->bg_size_kind = BGS_COVER; return; }
    if (str_ieq(v, "contain")) { c->st->bg_size_kind = BGS_CONTAIN; return; }
    char *t[2]; int n = split_ws(v, t, 2);
    c->st->bg_size_kind = BGS_LEN; c->st->bg_size[0] = c->st->bg_size[1] = L_auto();
    for (int i = 0; i < n; i++) alen(c, t[i], &c->st->bg_size[i]);
    if (n == 1) c->st->bg_size[1] = L_auto();
    free_toks(t, n);
}
static void apply_background(ACtx *c, const char *v) {
    /* use last layer for color, first for image */
    c->st->bg_color = 0; free(c->st->bg_image); c->st->bg_image = NULL; free(c->st->bg_gradient); c->st->bg_gradient = NULL;
    c->st->bg_repeat = BG_REPEAT; c->st->bg_size_kind = BGS_AUTO; c->st->bg_pos[0] = c->st->bg_pos[1] = (Length){0, 0, LK_LEN};
    if (str_ieq(v, "none")) return;
    if (strstr(v, "url(") || strstr(v, "gradient(")) apply_background_image(c, v);
    const char *p = v; char *t; int pos_n = 0; char posbuf[128] = {0};
    while ((t = next_token(&p))) {
        size_t tl = strlen(t);
        char *tt = t; if (tl && tt[tl - 1] == ',') tt[tl - 1] = 0;
        Color col;
        if (str_istarts(tt, "url(") || strstr(tt, "gradient(")) {}
        else if (str_ieq(tt, "no-repeat")) c->st->bg_repeat = BG_NO_REPEAT;
        else if (str_ieq(tt, "repeat-x")) c->st->bg_repeat = BG_REPEAT_X;
        else if (str_ieq(tt, "repeat-y")) c->st->bg_repeat = BG_REPEAT_Y;
        else if (str_ieq(tt, "repeat")) c->st->bg_repeat = BG_REPEAT;
        else if (str_ieq(tt, "cover") || str_ieq(tt, "contain")) apply_bg_size(c, tt);
        else if (!strcmp(tt, "/")) {}
        else if (css_parse_color(tt, &col, c->st->color)) c->st->bg_color = col;
        else if (pos_n < 2 && (str_ieq(tt, "center") || str_ieq(tt, "left") || str_ieq(tt, "right") || str_ieq(tt, "top") || str_ieq(tt, "bottom") || isdigit((unsigned char)tt[0]) || tt[0] == '-' || tt[0] == '.')) { strncat(posbuf, " ", sizeof posbuf - strlen(posbuf) - 1); strncat(posbuf, tt, sizeof posbuf - strlen(posbuf) - 1); pos_n++; }
        free(t);
    }
    if (pos_n) apply_bg_pos(c, posbuf);
}
static void apply_font(ACtx *c, const char *v) {
    /* [style] [variant] [weight] size[/line-height] family */
    if (str_ieq(v, "caption") || str_ieq(v, "menu") || str_ieq(v, "message-box") || str_ieq(v, "status-bar") || str_ieq(v, "icon") || str_ieq(v, "small-caption")) { c->st->font_family = atom("system-ui"); return; }
    const char *p = v; char *t;
    c->st->font_style = 0; c->st->font_weight = 400; c->st->line_height_normal = true; c->st->line_height_factor = 0;
    while ((t = next_token(&p))) {
        if (str_ieq(t, "italic") || str_ieq(t, "oblique")) c->st->font_style = 1;
        else if (str_ieq(t, "bold")) c->st->font_weight = 700;
        else if (str_ieq(t, "bolder")) c->st->font_weight = 700;
        else if (str_ieq(t, "lighter")) c->st->font_weight = 300;
        else if (str_ieq(t, "normal") || str_ieq(t, "small-caps")) {}
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
static void parse_transform(ACtx *c, const char *v) {
    float m[6] = { 1, 0, 0, 1, 0, 0 };
    c->st->translate_pending[0] = c->st->translate_pending[1] = (Length){0, 0, LK_LEN};
    if (str_ieq(v, "none")) { c->st->has_transform = false; return; }
    const char *p = v;
    while (*p) {
        while (is_ws((unsigned char)*p)) p++;
        const char *lp = strchr(p, '('); if (!lp) break;
        char fn[32]; snprintf(fn, sizeof fn, "%.*s", (int)LMIN(lp - p, 31), p);
        const char *rp = strchr(lp, ')'); if (!rp) break;
        char *args = xstrndup(lp + 1, (size_t)(rp - lp - 1));
        for (char *q = args; *q; q++) if (*q == ',') *q = ' ';
        char *t[6]; int n = split_ws(args, t, 6);
        float a = 1, b = 0, cc = 0, d = 1, e = 0, f = 0;
        for (char *q = fn; *q; q++) *q = (char)lc(*q);
        if (!strcmp(fn, "translate") || !strcmp(fn, "translatex") || !strcmp(fn, "translatey") || !strcmp(fn, "translate3d")) {
            Length lx = {0, 0, LK_LEN}, ly = {0, 0, LK_LEN};
            if (!strcmp(fn, "translatey")) { if (n) alen(c, t[0], &ly); } else { if (n) alen(c, t[0], &lx); if (n > 1 && strcmp(fn, "translatex")) alen(c, t[1], &ly); }
            e = lx.px; f = ly.px;
            c->st->translate_pending[0].pct += lx.pct; c->st->translate_pending[1].pct += ly.pct;
        } else if (!strcmp(fn, "scale") || !strcmp(fn, "scale3d")) { a = n ? strtof(t[0], NULL) : 1; d = n > 1 ? strtof(t[1], NULL) : a; }
        else if (!strcmp(fn, "scalex")) a = n ? strtof(t[0], NULL) : 1;
        else if (!strcmp(fn, "scaley")) d = n ? strtof(t[0], NULL) : 1;
        else if (!strcmp(fn, "rotate") || !strcmp(fn, "rotatez")) {
            float ang = n ? strtof(t[0], NULL) : 0; if (n && strstr(t[0], "turn")) ang *= 360; else if (n && strstr(t[0], "rad")) ang *= 57.2958f;
            float r = ang * (float)M_PI / 180; a = cosf(r); b = sinf(r); cc = -sinf(r); d = cosf(r);
        } else if (!strcmp(fn, "matrix") && n == 6) { a = strtof(t[0], NULL); b = strtof(t[1], NULL); cc = strtof(t[2], NULL); d = strtof(t[3], NULL); e = strtof(t[4], NULL); f = strtof(t[5], NULL); }
        else if (!strcmp(fn, "skewx")) { cc = tanf(strtof(t[0], NULL) * (float)M_PI / 180); }
        else if (!strcmp(fn, "skewy")) { b = tanf(strtof(t[0], NULL) * (float)M_PI / 180); }
        /* m = m * [a b c d e f] */
        float r0 = m[0] * a + m[2] * b, r1 = m[1] * a + m[3] * b, r2 = m[0] * cc + m[2] * d, r3 = m[1] * cc + m[3] * d, r4 = m[0] * e + m[2] * f + m[4], r5 = m[1] * e + m[3] * f + m[5];
        m[0] = r0; m[1] = r1; m[2] = r2; m[3] = r3; m[4] = r4; m[5] = r5;
        free_toks(t, n); free(args);
        p = rp + 1;
    }
    memcpy(c->st->transform, m, sizeof m);
    c->st->has_transform = !(m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1 && m[4] == 0 && m[5] == 0) || c->st->translate_pending[0].pct || c->st->translate_pending[1].pct;
}
static void parse_shadow(ACtx *c, const char *v, Shadow *sh, bool *has) {
    if (str_ieq(v, "none")) { *has = false; return; }
    /* first shadow only */
    char *first = xstrdup(v); int d = 0; for (char *q = first; *q; q++) { if (*q == '(') d++; else if (*q == ')') d--; else if (*q == ',' && !d) { *q = 0; break; } }
    char *t[7]; int n = split_ws(first, t, 7); float nums[4] = {0}; int nn = 0;
    memset(sh, 0, sizeof *sh); sh->color = c->st->color;
    for (int i = 0; i < n; i++) {
        Color col;
        if (str_ieq(t[i], "inset")) sh->inset = true;
        else if (nn < 4 && (isdigit((unsigned char)t[i][0]) || t[i][0] == '-' || t[i][0] == '.' || str_istarts(t[i], "calc"))) nums[nn++] = alen_px(c, t[i], 0);
        else if (css_parse_color(t[i], &col, c->st->color)) sh->color = col;
    }
    sh->x = nums[0]; sh->y = nums[1]; sh->blur = nums[2]; sh->spread = nums[3];
    *has = nn >= 2 && COLOR_A(sh->color) > 0;
    free_toks(t, n); free(first);
}
static int parse_grid_tracks(ACtx *c, const char *v, GridTrack **out) {
    VEC(GridTrack) tr = {0};
    const char *p = v; char *t;
    while ((t = next_token(&p))) {
        if (t[0] == '[') { free(t); continue; }
        if (str_istarts(t, "repeat(")) {
            char *inner = xstrndup(t + 7, strlen(t) - 8); char *comma = strchr(inner, ',');
            if (comma) {
                *comma = 0; int cnt = atoi(inner); if (cnt <= 0) cnt = str_istarts(str_trim(inner), "auto") ? -1 : 1;
                GridTrack *sub = NULL; int ns = parse_grid_tracks(c, comma + 1, &sub);
                if (cnt == -1) { /* auto-fill/auto-fit: estimate from min size */ cnt = 1; if (ns && sub[0].min.kind == LK_LEN && sub[0].min.px > 0) cnt = -(int)sub[0].min.px; }
                if (cnt < 0) { /* store negative marker for layout to compute */ GridTrack g = sub[0]; g.fr = g.fr ? g.fr : 1; g.size.kind = LK_FIT_CONTENT; g.size.px = (float)-cnt; vec_push(tr, g); }
                else for (int k = 0; k < cnt; k++) for (int j = 0; j < ns; j++) vec_push(tr, sub[j]);
                free(sub);
            }
            free(inner); free(t); continue;
        }
        GridTrack g; memset(&g, 0, sizeof g); g.size = L_auto(); g.min = L_auto();
        size_t tl = strlen(t);
        if (tl > 2 && !strcmp(t + tl - 2, "fr")) { g.fr = strtof(t, NULL); g.min.kind = LK_LEN; }
        else if (str_istarts(t, "minmax(")) {
            char *inner = xstrndup(t + 7, tl - 8); char *comma = strchr(inner, ',');
            if (comma) { *comma = 0; alen(c, str_trim(inner), &g.min); char *mx = str_trim(comma + 1); size_t ml = strlen(mx); if (ml > 2 && !strcmp(mx + ml - 2, "fr")) g.fr = strtof(mx, NULL); else alen(c, mx, &g.size); }
            free(inner);
        } else alen(c, t, &g.size);
        vec_push(tr, g); free(t);
    }
    *out = tr.v; return tr.n;
}
static void parse_grid_line(const char *v, int *start, int *span) {
    *start = 0; *span = 1;
    char *s = xstrdup(v); char *slash = strchr(s, '/');
    if (slash) *slash = 0;
    char *a = str_trim(s);
    if (str_istarts(a, "span")) *span = LMAX(1, atoi(a + 4)); else *start = atoi(a);
    if (slash) { char *b = str_trim(slash + 1); if (str_istarts(b, "span")) *span = LMAX(1, atoi(b + 4)); else { int e = atoi(b); if (e > 0 && *start > 0 && e > *start) *span = e - *start; else if (e < 0 && *start > 0) *span = e; } }
    free(s);
}
static uint8_t parse_align(const char *v) {
    static const char *const al[] = { "auto", "normal", "stretch", "flex-start", "flex-end", "center", "baseline", "space-between", "space-around", "space-evenly", "start", "end", NULL };
    const char *x = v; if (str_istarts(x, "safe ")) x += 5; else if (str_istarts(x, "unsafe ")) x += 7;
    if (str_istarts(x, "first baseline") || str_istarts(x, "last baseline")) return AL_BASELINE;
    int k = kw(x, al);
    if (k < 0) { if (str_ieq(x, "left") || str_ieq(x, "self-start")) return AL_START; if (str_ieq(x, "right") || str_ieq(x, "self-end")) return AL_END; return AL_NORMAL; }
    return (uint8_t)k;
}

static const char *const known_props[] = { "animation","animation-name","animation-duration","animation-delay","animation-iteration-count","animation-timing-function","animation-fill-mode","animation-direction","animation-play-state","transition","transition-property","transition-duration","transition-delay","transition-timing-function","display","position","float","clear","width","height","min-width","min-height","max-width","max-height","margin","margin-top","margin-right","margin-bottom","margin-left","padding","padding-top","padding-right","padding-bottom","padding-left","border","border-width","border-style","border-color","border-top","border-right","border-bottom","border-left","border-radius","color","background","background-color","background-image","font","font-size","font-weight","font-family","font-style","line-height","text-align","text-decoration","white-space","overflow","overflow-x","overflow-y","visibility","opacity","z-index","top","right","bottom","left","inset","flex","flex-direction","flex-wrap","flex-grow","flex-shrink","flex-basis","justify-content","align-items","align-self","align-content","gap","row-gap","column-gap","order","grid","grid-template-columns","grid-template-rows","grid-column","grid-row","grid-area","transform","transition","animation","box-shadow","text-shadow","box-sizing","cursor","pointer-events","content","list-style","list-style-type","vertical-align","text-transform","letter-spacing","word-spacing","text-indent","text-overflow","word-break","overflow-wrap","word-wrap","object-fit","aspect-ratio","filter","outline","user-select","appearance","will-change","contain","isolation","mix-blend-mode","place-items","place-content","place-self","justify-items","justify-self","table-layout","border-collapse","border-spacing","clip-path","mask","resize","scroll-behavior","overscroll-behavior","touch-action","font-variant","text-rendering","-webkit-font-smoothing","fill","stroke","caret-color","accent-color","color-scheme","translate","scale","rotate","container-type","backdrop-filter","line-clamp","-webkit-line-clamp","text-wrap","hyphens","tab-size","direction","unicode-bidi","writing-mode","inset-inline","inset-block","margin-inline","margin-block","padding-inline","padding-block", NULL };
bool css_property_known(const char *p) { return kw(p, known_props) >= 0; }

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
    char *e; float v = strtof(t, &e);
    if (e == t) return false;
    if (str_ieq(e, "ms")) v /= 1000; else if (!str_ieq(e, "s")) return false;
    *out = v; return true;
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

void css_apply_decl(ComputedStyle *st, const ComputedStyle *par, const char *prop, const char *value_in, StyleEngine *e, Node *el) {
    if (prop[0] == '-' && prop[1] == '-') {
        /* custom property: copy-on-write map */
        if (!st->custom || st->custom->refs > 1 || (par && st->custom == par->custom)) {
            CustomProps *n = xcalloc(1, sizeof *n); n->refs = 1;
            if (st->custom) hm_foreach(&st->custom->map, ent) hm_put(&n->map, ent->key, xstrdup((char *)ent->val));
            custom_unref(st->custom); st->custom = n;
        }
        char *old = hm_get(&st->custom->map, prop);
        char *v = subst_vars(value_in, st, 0);
        hm_put(&st->custom->map, prop, v ? v : xstrdup(""));
        free(old);
        return;
    }
    char *subst = NULL;
    const char *v = value_in;
    if (strstr(v, "var(")) { subst = subst_vars(v, st, 0); if (!subst) return; v = subst; }
    char *vbuf = xstrdup(v); char *val = str_trim(vbuf);
    ACtx c = { st, par, e, el, st->font_size, 16, e ? &e->media : NULL };
    if (e && e->doc && e->doc->html && e->doc->html->style && el != e->doc->html) c.rem = e->doc->html->style->font_size;
    const char *P = prop;
    Length l; Color col; int k;
    bool inherit = str_ieq(val, "inherit"), initial = str_ieq(val, "initial") || str_ieq(val, "revert") || str_ieq(val, "revert-layer"), unset = str_ieq(val, "unset");
    {
        const char *Q = strncmp(P, "-webkit-", 8) ? P : P + 8;
        if (!strncmp(Q, "animation", 9) || !strncmp(Q, "transition", 10)) { anim_decl(st, Q, inherit || initial || unset ? "" : val); free(vbuf); free(subst); return; }
    }
    if (inherit || initial || unset) {
        static ComputedStyle *def; if (!def) def = style_new_default();
        const ComputedStyle *src = inherit ? (par ? par : def) : def;
        /* unset: inherited props inherit, others initial -- approximate: inherit color/font/text props */
        if (unset && par && (strstr(P, "color") == P || strstr(P, "font") || strstr(P, "text") || strstr(P, "line-height") || strstr(P, "white-space") || strstr(P, "visibility") || strstr(P, "cursor") || strstr(P, "letter"))) src = par;
        if (!strcmp(P, "color")) st->color = src->color;
        else if (!strcmp(P, "font-size")) st->font_size = src->font_size;
        else if (!strcmp(P, "font-weight")) st->font_weight = src->font_weight;
        else if (!strcmp(P, "font-family")) st->font_family = src->font_family;
        else if (!strcmp(P, "line-height")) { st->line_height = src->line_height; st->line_height_normal = src->line_height_normal; st->line_height_factor = src->line_height_factor; }
        else if (!strcmp(P, "background-color") || !strcmp(P, "background")) { st->bg_color = src->bg_color; if (!strcmp(P, "background")) { free(st->bg_image); st->bg_image = NULL; free(st->bg_gradient); st->bg_gradient = NULL; } }
        else if (!strcmp(P, "display")) st->display = src->display;
        else if (!strcmp(P, "text-align")) st->text_align = src->text_align;
        else if (!strcmp(P, "width")) st->width = src->width;
        else if (!strcmp(P, "height")) st->height = src->height;
        else if (!strcmp(P, "margin")) memcpy(st->margin, src->margin, sizeof st->margin);
        else if (!strcmp(P, "padding")) memcpy(st->padding, src->padding, sizeof st->padding);
        else if (!strcmp(P, "border")) { memcpy(st->border_style, src->border_style, 4); }
        else if (!strcmp(P, "white-space")) st->white_space = src->white_space;
        else if (!strcmp(P, "visibility")) st->visibility = src->visibility;
        else if (!strcmp(P, "position")) st->position = src->position;
        else if (!strcmp(P, "cursor")) st->cursor = src->cursor;
        else if (!strcmp(P, "fill")) st->fill = src->fill;
        else if (!strcmp(P, "all")) {}
        goto out;
    }
    switch (P[0]) {
    case 'a':
        if (!strcmp(P, "align-items")) st->align_items = parse_align(val);
        else if (!strcmp(P, "align-self")) st->align_self = parse_align(val);
        else if (!strcmp(P, "align-content")) st->align_content = parse_align(val);
        else if (!strcmp(P, "aspect-ratio")) { float a = 0, b = 1; const char *q = val; if (str_istarts(q, "auto")) q += 4; while (is_ws((unsigned char)*q)) q++; if (sscanf(q, "%f / %f", &a, &b) >= 1 && b > 0) st->aspect_ratio = a / b; else st->aspect_ratio = 0; }
        else if (!strcmp(P, "appearance") || !strcmp(P, "-webkit-appearance")) st->appearance = str_ieq(val, "none") ? 1 : 0;
        break;
    case 'b':
        if (!strcmp(P, "background-color")) { if (css_parse_color(val, &col, st->color)) st->bg_color = col; }
        else if (!strcmp(P, "background")) apply_background(&c, val);
        else if (!strcmp(P, "background-image")) apply_background_image(&c, val);
        else if (!strcmp(P, "background-repeat")) { st->bg_repeat = str_ieq(val, "no-repeat") ? BG_NO_REPEAT : str_ieq(val, "repeat-x") ? BG_REPEAT_X : str_ieq(val, "repeat-y") ? BG_REPEAT_Y : BG_REPEAT; }
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
            if (!strcmp(P, "border-inline-start-width") || !strcmp(P, "border-left-width")) { float w = border_w(&c, val); if (w >= 0) st->border_width[3] = w; }
            if (!strcmp(P, "border-start-start-radius")) alen(&c, val, &st->border_radius[0]);
            if (!strcmp(P, "border-start-end-radius")) alen(&c, val, &st->border_radius[1]);
            if (!strcmp(P, "border-end-end-radius")) alen(&c, val, &st->border_radius[2]);
            if (!strcmp(P, "border-end-start-radius")) alen(&c, val, &st->border_radius[3]);
        }
        else if (!strcmp(P, "backdrop-filter") || !strcmp(P, "-webkit-backdrop-filter")) { const char *b = strstr(val, "blur("); st->backdrop_blur = b ? alen_px(&c, b + 5, 0) : 0; }
        break;
    case 'c':
        if (!strcmp(P, "color")) { if (css_parse_color(val, &col, par ? par->color : st->color)) { st->color = col; } }
        else if (!strcmp(P, "clear")) { static const char *const cl[] = { "none", "left", "right", "both", NULL }; k = kw(val, cl); if (k >= 0) st->clear = (uint8_t)k; else if (str_ieq(val, "inline-start")) st->clear = CLEAR_LEFT; }
        else if (!strcmp(P, "cursor")) { st->cursor = str_ieq(val, "pointer") ? CUR_POINTER : str_ieq(val, "text") ? CUR_TEXT : str_ieq(val, "move") ? CUR_MOVE : str_ieq(val, "not-allowed") ? CUR_NOT_ALLOWED : str_istarts(val, "grab") ? CUR_GRAB : CUR_DEFAULT; }
        else if (!strcmp(P, "content")) { free(st->content); st->content = NULL; if (!str_ieq(val, "none") && !str_ieq(val, "normal")) { SB b; sb_init(&b); const char *q = val; while (*q) { if (*q == '"' || *q == '\'') { char qc = *q++; while (*q && *q != qc) { if (*q == '\\' && q[1]) { q++; if (isxdigit((unsigned char)*q)) { uint32_t cp = (uint32_t)strtoul(q, (char **)&q, 16); sb_utf8(&b, cp); if (*q == ' ') q++; continue; } } sb_putc(&b, *q++); } if (*q) q++; } else if (str_istarts(q, "attr(")) { const char *e2 = strchr(q, ')'); char an[64]; snprintf(an, sizeof an, "%.*s", (int)(e2 ? e2 - q - 5 : 0), q + 5); const char *av = el ? node_attr(el, str_trim(an)) : NULL; if (av) sb_puts(&b, av); q = e2 ? e2 + 1 : q + strlen(q); } else if (str_istarts(q, "open-quote")) { sb_puts(&b, "\xe2\x80\x9c"); q += 10; } else if (str_istarts(q, "close-quote")) { sb_puts(&b, "\xe2\x80\x9d"); q += 11; } else q++; } st->content = sb_take(&b); } }
        else if (!strcmp(P, "column-gap")) { if (alen(&c, val, &l)) { st->column_gap = l.px; st->column_gap_l = l; } }
        else if (!strcmp(P, "caret-color")) { css_parse_color(val, &st->caret_color, st->color); }
        break;
    case 'd':
        if (!strcmp(P, "display")) {
            static const char *const ds[] = { "none", "inline", "block", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "list-item", "table", "inline-table", "table-row", "table-cell", "table-row-group", "table-header-group", "table-footer-group", "table-column", "table-column-group", "table-caption", "contents", "flow-root", NULL };
            k = kw(val, ds);
            if (k >= 0) st->display = (uint8_t)k;
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
            float pf = par ? par->font_size : 16;
            static const char *const sz[] = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large", NULL };
            static const float szv[] = { 9, 10, 13, 16, 18, 24, 32, 48 };
            k = kw(val, sz);
            if (k >= 0) st->font_size = szv[k];
            else if (str_ieq(val, "smaller")) st->font_size = pf / 1.2f;
            else if (str_ieq(val, "larger")) st->font_size = pf * 1.2f;
            else if (css_parse_length(val, &l, pf, c.rem, c.mc) && l.kind == LK_LEN) st->font_size = LMAX(0, l.px + l.pct * pf / 100);
            c.em = st->font_size;
        }
        else if (!strcmp(P, "font-weight")) { if (str_ieq(val, "bold")) st->font_weight = 700; else if (str_ieq(val, "normal")) st->font_weight = 400; else if (str_ieq(val, "bolder")) st->font_weight = (int16_t)(par && par->font_weight >= 600 ? 900 : 700); else if (str_ieq(val, "lighter")) st->font_weight = 300; else { int w = atoi(val); if (w > 0) st->font_weight = (int16_t)w; } }
        else if (!strcmp(P, "font-family")) st->font_family = atom(val);
        else if (!strcmp(P, "font-style")) st->font_style = str_ieq(val, "italic") || str_istarts(val, "oblique");
        else if (!strcmp(P, "font")) apply_font(&c, val);
        else if (!strcmp(P, "float")) { st->float_ = str_ieq(val, "left") || str_ieq(val, "inline-start") ? F_LEFT : str_ieq(val, "right") || str_ieq(val, "inline-end") ? F_RIGHT : F_NONE; }
        else if (!strcmp(P, "flex")) {
            if (str_ieq(val, "none")) { st->flex_grow = 0; st->flex_shrink = 0; st->flex_basis = L_auto(); }
            else if (str_ieq(val, "auto")) { st->flex_grow = 1; st->flex_shrink = 1; st->flex_basis = L_auto(); }
            else {
                char *t[3]; int n = split_ws(val, t, 3); int nums = 0;
                st->flex_grow = 1; st->flex_shrink = 1; st->flex_basis = L_px(0);
                for (int i = 0; i < n; i++) {
                    char *end; float x = strtof(t[i], &end);
                    if (end != t[i] && !*end && nums < 2) { if (nums == 0) st->flex_grow = x; else st->flex_shrink = x; nums++; }
                    else if (alen(&c, t[i], &l)) st->flex_basis = l;
                    else if (str_ieq(t[i], "content")) st->flex_basis = L_auto();
                }
                free_toks(t, n);
            }
        }
        else if (!strcmp(P, "flex-grow")) st->flex_grow = strtof(val, NULL);
        else if (!strcmp(P, "flex-shrink")) st->flex_shrink = strtof(val, NULL);
        else if (!strcmp(P, "flex-basis")) { if (str_ieq(val, "content")) st->flex_basis = L_auto(); else alen(&c, val, &st->flex_basis); }
        else if (!strcmp(P, "flex-direction")) { static const char *const fd[] = { "row", "row-reverse", "column", "column-reverse", NULL }; k = kw(val, fd); if (k >= 0) st->flex_direction = (uint8_t)k; }
        else if (!strcmp(P, "flex-wrap")) st->flex_wrap = str_ieq(val, "wrap") ? 1 : str_ieq(val, "wrap-reverse") ? 2 : 0;
        else if (!strcmp(P, "flex-flow")) { char *t[2]; int n = split_ws(val, t, 2); for (int i = 0; i < n; i++) { if (strstr(t[i], "wrap")) css_apply_decl(st, par, "flex-wrap", t[i], e, el); else css_apply_decl(st, par, "flex-direction", t[i], e, el); } free_toks(t, n); }
        else if (!strcmp(P, "fill")) { if (css_parse_color(val, &col, st->color)) st->fill = col; else if (str_ieq(val, "none")) st->fill = 0; }
        else if (!strcmp(P, "filter")) { const char *b = strstr(val, "blur("); st->filter_blur = b ? alen_px(&c, b + 5, 0) : 0; const char *br = strstr(val, "brightness("); st->filter_brightness = br ? strtof(br + 11, NULL) : 1; }
        break;
    case 'g':
        if (!strcmp(P, "gap") || !strcmp(P, "grid-gap")) { char *t[2]; int n = split_ws(val, t, 2); if (n && alen(&c, t[0], &l)) { st->row_gap = l.px; st->row_gap_l = l; st->column_gap = l.px; st->column_gap_l = l; } if (n > 1 && alen(&c, t[1], &l)) { st->column_gap = l.px; st->column_gap_l = l; } free_toks(t, n); }
        else if (!strcmp(P, "grid-template-columns")) { free(st->grid_cols); st->grid_cols = NULL; st->grid_ncols = str_ieq(val, "none") ? 0 : parse_grid_tracks(&c, val, &st->grid_cols); }
        else if (!strcmp(P, "grid-template-rows")) { free(st->grid_rows); st->grid_rows = NULL; st->grid_nrows = str_ieq(val, "none") ? 0 : parse_grid_tracks(&c, val, &st->grid_rows); }
        else if (!strcmp(P, "grid-template")) { if (strchr(val, '"') || strchr(val, '\'')) { free(st->grid_areas); st->grid_areas = parse_grid_areas(val); } char *sl = strchr(val, '/'); if (sl) { *sl = 0; css_apply_decl(st, par, "grid-template-rows", val, e, el); css_apply_decl(st, par, "grid-template-columns", sl + 1, e, el); } }
        else if (!strcmp(P, "grid-column")) parse_grid_line(val, &st->grid_col_start, &st->grid_col_span);
        else if (!strcmp(P, "grid-row")) parse_grid_line(val, &st->grid_row_start, &st->grid_row_span);
        else if (!strcmp(P, "grid-column-start")) { if (str_istarts(val, "span")) st->grid_col_span = atoi(val + 4); else st->grid_col_start = atoi(val); }
        else if (!strcmp(P, "grid-row-start")) { if (str_istarts(val, "span")) st->grid_row_span = atoi(val + 4); else st->grid_row_start = atoi(val); }
        else if (!strcmp(P, "grid-auto-rows")) alen(&c, val, &st->grid_auto_rows);
        else if (!strcmp(P, "grid-area")) { free(st->grid_area); st->grid_area = NULL; int a = atoi(val); if (a > 0) st->grid_row_start = a; else if (isalpha((unsigned char)*val) || *val == '_' || *val == '-') { size_t k = strcspn(val, " /"); st->grid_area = xstrndup(val, k); } }
        else if (!strcmp(P, "grid-template-areas")) { free(st->grid_areas); st->grid_areas = parse_grid_areas(val); }
        break;
    case 'h':
        if (!strcmp(P, "height")) alen(&c, val, &st->height);
        break;
    case 'i':
        if (!strcmp(P, "inset")) set4(st->inset, &c, val);
        else if (!strcmp(P, "inset-inline-start")) alen(&c, val, &st->inset[3]);
        else if (!strcmp(P, "inset-inline-end")) alen(&c, val, &st->inset[1]);
        else if (!strcmp(P, "inset-block-start")) alen(&c, val, &st->inset[0]);
        else if (!strcmp(P, "inset-block-end")) alen(&c, val, &st->inset[2]);
        else if (!strcmp(P, "inset-inline")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { alen(&c, t[0], &st->inset[3]); alen(&c, t[n - 1], &st->inset[1]); } free_toks(t, n); }
        else if (!strcmp(P, "inset-block")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { alen(&c, t[0], &st->inset[0]); alen(&c, t[n - 1], &st->inset[2]); } free_toks(t, n); }
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
            else if (css_parse_length(val, &l, st->font_size, c.rem, c.mc) && l.kind == LK_LEN) { st->line_height_normal = false; st->line_height_factor = 0; st->line_height = l.px + l.pct * st->font_size / 100; }
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
        else if (!strcmp(P, "margin-inline")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { alen(&c, t[0], &st->margin[3]); alen(&c, t[n - 1], &st->margin[1]); } free_toks(t, n); }
        else if (!strcmp(P, "margin-block")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { alen(&c, t[0], &st->margin[0]); alen(&c, t[n - 1], &st->margin[2]); } free_toks(t, n); }
        else if (!strcmp(P, "min-width")) { alen(&c, val, &st->min_width); }
        else if (!strcmp(P, "min-height")) alen(&c, val, &st->min_height);
        else if (!strcmp(P, "max-width")) alen(&c, val, &st->max_width);
        else if (!strcmp(P, "max-height")) alen(&c, val, &st->max_height);
        else if (!strcmp(P, "min-inline-size")) alen(&c, val, &st->min_width);
        else if (!strcmp(P, "max-inline-size")) alen(&c, val, &st->max_width);
        break;
    case 'o':
        if (!strcmp(P, "opacity")) { float x = strtof(val, NULL); if (strchr(val, '%')) x /= 100; st->opacity = LCLAMP(x, 0, 1); }
        else if (!strcmp(P, "overflow") || !strcmp(P, "overflow-x") || !strcmp(P, "overflow-y")) {
            static const char *const ov[] = { "visible", "hidden", "scroll", "auto", "clip", NULL };
            char *t[2]; int n = split_ws(val, t, 2);
            int a = n ? kw(t[0], ov) : -1, b = n > 1 ? kw(t[1], ov) : a;
            if (n && str_ieq(t[0], "overlay")) a = b = OV_AUTO;
            if (a >= 0) { if (P[8] != 'y') st->overflow_x = (uint8_t)a; if (P[8] != 'x') st->overflow_y = (uint8_t)(P[8] == 'y' ? a : b >= 0 ? b : a); }
            free_toks(t, n);
        }
        else if (!strcmp(P, "order")) st->order = atoi(val);
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
        else if (!strcmp(P, "padding-inline")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { alen(&c, t[0], &st->padding[3]); alen(&c, t[n - 1], &st->padding[1]); } free_toks(t, n); }
        else if (!strcmp(P, "padding-block")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { alen(&c, t[0], &st->padding[0]); alen(&c, t[n - 1], &st->padding[2]); } free_toks(t, n); }
        else if (!strcmp(P, "position")) { static const char *const ps[] = { "static", "relative", "absolute", "fixed", "sticky", NULL }; k = kw(val, ps); if (k >= 0) st->position = (uint8_t)k; else if (str_ieq(val, "-webkit-sticky")) st->position = P_STICKY; }
        else if (!strcmp(P, "pointer-events")) st->pointer_events = !str_ieq(val, "none");
        else if (!strcmp(P, "place-items")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { st->align_items = parse_align(t[0]); st->justify_items = parse_align(t[n - 1]); } free_toks(t, n); }
        else if (!strcmp(P, "place-content")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { st->align_content = parse_align(t[0]); st->justify_content = parse_align(t[n - 1]); } free_toks(t, n); }
        else if (!strcmp(P, "place-self")) { char *t[2]; int n = split_ws(val, t, 2); if (n) { st->align_self = parse_align(t[0]); st->justify_self = parse_align(t[n - 1]); } free_toks(t, n); }
        break;
    case 'r':
        if (!strcmp(P, "right")) alen(&c, val, &st->inset[1]);
        else if (!strcmp(P, "row-gap")) { if (alen(&c, val, &l)) { st->row_gap = l.px; st->row_gap_l = l; } }
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
        else if (!strcmp(P, "text-shadow")) parse_shadow(&c, val, &st->text_shadow, &st->has_text_shadow);
        else if (!strcmp(P, "transform") || !strcmp(P, "-webkit-transform")) parse_transform(&c, val);
        else if (!strcmp(P, "translate")) { char buf[96]; snprintf(buf, sizeof buf, "translate(%s)", val); if (!str_ieq(val, "none")) { for (char *q = buf + 10; *q; q++) if (*q == ' ') { *q = ','; break; } parse_transform(&c, buf); } }
        else if (!strcmp(P, "transform-origin")) { char *t[2]; int n = split_ws(val, t, 2); for (int i = 0; i < n; i++) { if (str_ieq(t[i], "left") || str_ieq(t[i], "top")) st->transform_origin[i] = (Length){0, 0, LK_LEN}; else if (str_ieq(t[i], "right") || str_ieq(t[i], "bottom")) st->transform_origin[i] = (Length){0, 100, LK_LEN}; else if (str_ieq(t[i], "center")) st->transform_origin[i] = (Length){0, 50, LK_LEN}; else alen(&c, t[i], &st->transform_origin[i]); } free_toks(t, n); }
        else if (!strcmp(P, "table-layout")) st->table_layout = str_ieq(val, "fixed");
        else if (!strcmp(P, "text-wrap") || !strcmp(P, "text-wrap-mode")) { if (str_ieq(val, "nowrap")) st->white_space = WS_NOWRAP; }
        break;
    case 'u':
        if (!strcmp(P, "user-select") || !strcmp(P, "-webkit-user-select")) st->user_select = str_ieq(val, "none");
        break;
    case 'v':
        if (!strcmp(P, "visibility")) st->visibility = str_ieq(val, "hidden") ? VIS_HIDDEN : str_ieq(val, "collapse") ? VIS_COLLAPSE : VIS_VISIBLE;
        else if (!strcmp(P, "vertical-align")) { static const char *const va[] = { "baseline", "top", "middle", "bottom", "text-top", "text-bottom", "sub", "super", NULL }; k = kw(val, va); if (k >= 0) st->vertical_align = (uint8_t)k; else if (alen(&c, val, &l)) { st->vertical_align = VA_LENGTH; st->vertical_align_len = l.px + l.pct * st->font_size / 100; } }
        break;
    case 'w':
        if (!strcmp(P, "width")) alen(&c, val, &st->width);
        else if (!strcmp(P, "white-space") || !strcmp(P, "white-space-collapse")) { static const char *const ws[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line", "break-spaces", NULL }; k = kw(val, ws); if (k >= 0) st->white_space = (uint8_t)k; else if (str_ieq(val, "preserve")) st->white_space = WS_PRE_WRAP; }
        else if (!strcmp(P, "word-break")) st->word_break = str_ieq(val, "break-all") ? 1 : str_ieq(val, "break-word") ? 2 : 0;
        else if (!strcmp(P, "word-spacing")) st->word_spacing = str_ieq(val, "normal") ? 0 : alen_px(&c, val, 0);
        else if (!strcmp(P, "writing-mode")) st->writing_mode = str_istarts(val, "vertical");
        break;
    case 'z':
        if (!strcmp(P, "z-index")) { if (str_ieq(val, "auto")) st->z_auto = true; else { st->z_auto = false; st->z_index = atoi(val); } }
        break;
    }
out:
    free(vbuf); free(subst);
}

/* ---------------- rule index ---------------- */
static void idx_add(HMap *m, const char *key, Rule *r) {
    RuleVec *v = hm_get(m, key);
    if (!v) { v = xcalloc(1, sizeof *v); hm_put(m, key, v); }
    vec_push(*v, r);
}
static void rv_free(void *p) { RuleVec *v = p; vec_free(*v); free(v); }
static void idx_clear(RuleIndex *ix) { hm_free(&ix->by_id, rv_free); hm_free(&ix->by_class, rv_free); hm_free(&ix->by_tag, rv_free); vec_free(ix->universal); ix->count = 0; }
static void idx_build(StyleEngine *e) {
    idx_clear(&e->idx);
    for (int si = 0; si < e->sheets.n; si++) {
        StyleSheet *sh = e->sheets.v[si];
        if (sh->disabled) continue;
        for (int i = 0; i < sh->rules.n; i++) {
            Rule *r = &sh->rules.v[i];
            r->order = (uint32_t)(si << 20) + (uint32_t)i;
            Compound *last = &r->sel.c[r->sel.n - 1];
            const char *id = NULL, *cls = NULL, *tag = NULL;
            for (int k = 0; k < last->n; k++) {
                SimpleSel *s = &last->s[k];
                if (s->kind == SK_ID) id = s->name; else if (s->kind == SK_CLASS && !cls) cls = s->name; else if (s->kind == SK_TYPE) tag = s->value;
            }
            if (id) idx_add(&e->idx.by_id, id, r);
            else if (cls) idx_add(&e->idx.by_class, cls, r);
            else if (tag) idx_add(&e->idx.by_tag, tag, r);
            else vec_push(e->idx.universal, r);
            e->idx.count++;
        }
    }
    e->idx_dirty = false;
}

StyleEngine *style_engine_new(Document *d) {
    StyleEngine *e = xcalloc(1, sizeof *e);
    e->doc = d; e->media.vw = 1280; e->media.vh = 800; e->media.dpr = 2;
    StyleSheet *ua = css_parse_sheet(css_ua_sheet, strlen(css_ua_sheet), NULL, 0, &e->media);
    vec_push(e->sheets, ua);
    e->idx_dirty = true;
    return e;
}
void style_engine_free(StyleEngine *e) { for (int i = 0; i < e->sheets.n; i++) css_sheet_free(e->sheets.v[i]); vec_free(e->sheets); idx_clear(&e->idx); free(e); }
void style_engine_add_sheet(StyleEngine *e, StyleSheet *s) {
    /* keep document order of owner nodes */
    int pos = e->sheets.n;
    if (s->owner) {
        for (int i = 1; i < e->sheets.n; i++) {
            Node *o = e->sheets.v[i]->owner; if (!o) continue;
            /* is s->owner before o in tree order? */
            bool before = false;
            for (Node *n = s->owner; n; n = node_next_in_tree(n, NULL)) { if (n == o) { before = true; break; } }
            if (before) { pos = i; break; }
        }
    }
    vec_push(e->sheets, s);
    memmove(&e->sheets.v[pos + 1], &e->sheets.v[pos], sizeof(StyleSheet *) * (size_t)(e->sheets.n - 1 - pos));
    e->sheets.v[pos] = s;
    e->idx_dirty = true; e->generation++;
}
void style_engine_remove_owner(StyleEngine *e, Node *owner) {
    for (int i = 0; i < e->sheets.n; i++) if (e->sheets.v[i]->owner == owner) { css_sheet_free(e->sheets.v[i]); memmove(&e->sheets.v[i], &e->sheets.v[i + 1], sizeof(StyleSheet *) * (size_t)(e->sheets.n - i - 1)); e->sheets.n--; i--; }
    e->idx_dirty = true; e->generation++;
}
void style_engine_invalidate(StyleEngine *e) { e->idx_dirty = true; e->generation++; }

/* ---------------- cascade ---------------- */
typedef struct { Decl *d; uint64_t key; } MDecl;
static int mdecl_cmp(const void *a, const void *b) { uint64_t x = ((const MDecl *)a)->key, y = ((const MDecl *)b)->key; return x < y ? -1 : x > y; }

static void collect(StyleEngine *e, Node *el, RuleVec *rv, int pseudo, VEC(MDecl) *out) {
    if (!rv) return;
    for (int i = 0; i < rv->n; i++) {
        Rule *r = rv->v[i];
        if (r->sel.pseudo_el != pseudo) continue;
        if (!css_match_selector(&r->sel, el, NULL)) continue;
        for (int k = 0; k < r->decls->n; k++) {
            Decl *d = &r->decls->v[k];
            /* key: important(1) | origin(1) | spec(30) | order(32) */
            uint64_t imp = d->important ? 1 : 0;
            uint64_t level = imp ? (r->origin == 0 ? 3 : 2) : (r->origin == 0 ? 0 : 1);
            MDecl m = { d, (level << 62) | ((uint64_t)r->sel.spec << 32) | ((uint64_t)r->order << 8) | (uint64_t)(k & 0xff) };
            vec_push(*out, m);
        }
    }
}

static void presentational_hints(ComputedStyle *st, const ComputedStyle *par, Node *el, StyleEngine *e) {
    if (el->ns != NS_HTML) {
        if (el->ns == NS_SVG) {
            const char *w = node_attr(el, "width"), *h = node_attr(el, "height"), *f = node_attr(el, "fill");
            if (w && el->tag == A_svg) css_apply_decl(st, par, "width", w, e, el);
            if (h && el->tag == A_svg) css_apply_decl(st, par, "height", h, e, el);
            if (f) css_apply_decl(st, par, "fill", f, e, el);
            if (el->tag != A_svg) st->display = D_NONE + 0 == st->display ? D_NONE : st->display;
        }
        return;
    }
    const char *v;
    if ((v = node_attr(el, "width")) && (el->tag == A_img || el->tag == A_video || el->tag == A_canvas || el->tag == A_iframe || el->tag == A_table || el->tag == A_td || el->tag == A_th || el->tag == A_embed || el->tag == A_object || el->tag == A_hr || el->tag == A_col)) {
        char buf[64]; snprintf(buf, sizeof buf, "%s%s", v, strpbrk(v, "%px") ? "" : "px"); css_apply_decl(st, par, "width", buf, e, el);
    }
    if ((v = node_attr(el, "height")) && (el->tag == A_img || el->tag == A_video || el->tag == A_canvas || el->tag == A_iframe || el->tag == A_table || el->tag == A_td || el->tag == A_tr || el->tag == A_embed || el->tag == A_object)) {
        char buf[64]; snprintf(buf, sizeof buf, "%s%s", v, strpbrk(v, "%px") ? "" : "px"); css_apply_decl(st, par, "height", buf, e, el);
    }
    if ((v = node_attr(el, "bgcolor"))) css_apply_decl(st, par, "background-color", v, e, el);
    if (el->tag == A_font) { if ((v = node_attr(el, "color"))) css_apply_decl(st, par, "color", v, e, el); if ((v = node_attr(el, "face"))) css_apply_decl(st, par, "font-family", v, e, el); if ((v = node_attr(el, "size"))) { static const char *sz[] = { "x-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large" }; int s = atoi(v); if (v[0] == '+' || v[0] == '-') s += 3; css_apply_decl(st, par, "font-size", sz[LCLAMP(s, 1, 7)], e, el); } }
    if (el->tag == A_body && (v = node_attr(el, "text"))) css_apply_decl(st, par, "color", v, e, el);
    if ((v = node_attr(el, "align")) && (el->tag == A_div || el->tag == A_p || el->tag == A_td || el->tag == A_th || el->tag == A_tr || el->tag == A_h1 || el->tag == A_h2 || el->tag == A_h3)) css_apply_decl(st, par, "text-align", v, e, el);
    if (el->tag == A_table && (v = node_attr(el, "align")) && str_ieq(v, "center")) { css_apply_decl(st, par, "margin-left", "auto", e, el); css_apply_decl(st, par, "margin-right", "auto", e, el); }
    if ((el->tag == A_img || el->tag == A_table) && (v = node_attr(el, "border"))) { char buf[64]; snprintf(buf, sizeof buf, "%spx solid", v); css_apply_decl(st, par, "border", buf, e, el); }
    if ((el->tag == A_td || el->tag == A_th) && (v = node_attr(el, "valign"))) css_apply_decl(st, par, "vertical-align", v, e, el);
    if (el->tag == A_td || el->tag == A_th) {
        /* cellpadding from table */
        Node *t = el->parent; while (t && t->tag != A_table) t = t->parent;
        if (t && (v = node_attr(t, "cellpadding"))) { char buf[32]; snprintf(buf, sizeof buf, "%spx", v); css_apply_decl(st, par, "padding", buf, e, el); }
    }
    if (el->tag == A_img || el->tag == A_video) { /* aspect from attributes */ const char *w = node_attr(el, "width"), *h = node_attr(el, "height"); if (w && h && atof(h) > 0) st->aspect_ratio = (float)(atof(w) / atof(h)); }
}

static ComputedStyle *compute_pseudo(StyleEngine *e, Node *el, ComputedStyle *base, int which) {
    VEC(MDecl) md = {0};
    RuleVec *rv;
    if (el->id && (rv = hm_get(&e->idx.by_id, el->id))) collect(e, el, rv, which, (void *)&md);
    const char *cls = node_attr(el, "class");
    if (cls) { const char *p = cls; while (*p) { while (is_ws((unsigned char)*p)) p++; const char *s = p; while (*p && !is_ws((unsigned char)*p)) p++; if (p > s && (rv = hm_getn(&e->idx.by_class, s, (size_t)(p - s)))) collect(e, el, rv, which, (void *)&md); } }
    if ((rv = hm_get(&e->idx.by_tag, el->tag))) collect(e, el, rv, which, (void *)&md);
    { RuleVec u = { e->idx.universal.v, e->idx.universal.n, 0 }; collect(e, el, &u, which, (void *)&md); }
    if (!md.n) { vec_free(md); return NULL; }
    qsort(md.v, (size_t)md.n, sizeof(MDecl), mdecl_cmp);
    ComputedStyle *st = style_inherit(base);
    for (int i = 0; i < md.n; i++) if (md.v[i].d->prop[0] == '-' && md.v[i].d->prop[1] == '-') css_apply_decl(st, base, md.v[i].d->prop, md.v[i].d->value, e, el);
    for (int i = 0; i < md.n; i++) if (!(md.v[i].d->prop[0] == '-' && md.v[i].d->prop[1] == '-')) css_apply_decl(st, base, md.v[i].d->prop, md.v[i].d->value, e, el);
    vec_free(md);
    if (!st->content) { style_free(st); return NULL; }
    return st;
}

static void apply_decl_ordered(ComputedStyle *st, const ComputedStyle *par, MDecl *v, int n, StyleEngine *e, Node *el) {
    /* custom properties first, then font-size (em basis), then rest */
    for (int i = 0; i < n; i++) if (v[i].d->prop[0] == '-' && v[i].d->prop[1] == '-') css_apply_decl(st, par, v[i].d->prop, v[i].d->value, e, el);
    for (int i = 0; i < n; i++) { const char *p = v[i].d->prop; if (!strcmp(p, "font-size") || !strcmp(p, "font")) css_apply_decl(st, par, p, v[i].d->value, e, el); }
    for (int i = 0; i < n; i++) { const char *p = v[i].d->prop; if (!strcmp(p, "color")) css_apply_decl(st, par, p, v[i].d->value, e, el); }
    for (int i = 0; i < n; i++) { const char *p = v[i].d->prop; if ((p[0] == '-' && p[1] == '-') || !strcmp(p, "font-size") || !strcmp(p, "font") || !strcmp(p, "color")) continue; css_apply_decl(st, par, p, v[i].d->value, e, el); }
}

static ComputedStyle *compute(StyleEngine *e, Node *el, const ComputedStyle *par) {
    VEC(MDecl) md = {0};
    RuleVec *rv;
    if (el->id && (rv = hm_get(&e->idx.by_id, el->id))) collect(e, el, rv, 0, (void *)&md);
    const char *cls = node_attr(el, "class");
    if (cls) {
        const char *seen[32]; int ns = 0;
        const char *p = cls;
        while (*p) {
            while (is_ws((unsigned char)*p)) p++;
            const char *s = p; while (*p && !is_ws((unsigned char)*p)) p++;
            if (p > s) {
                const char *a = atomn(s, (size_t)(p - s)); bool dup = false;
                for (int i = 0; i < ns; i++) if (seen[i] == a) dup = true;
                if (!dup) { if (ns < 32) seen[ns++] = a; if ((rv = hm_get(&e->idx.by_class, a))) collect(e, el, rv, 0, (void *)&md); }
            }
        }
    }
    const char *tagkey = el->ns == NS_HTML ? el->tag : atom_lower(el->tag, strlen(el->tag));
    if ((rv = hm_get(&e->idx.by_tag, tagkey))) collect(e, el, rv, 0, (void *)&md);
    { RuleVec u = { e->idx.universal.v, e->idx.universal.n, 0 }; collect(e, el, &u, 0, (void *)&md); }
    qsort(md.v, (size_t)md.n, sizeof(MDecl), mdecl_cmp);
    ComputedStyle *st = style_inherit(par);
    if (el->ns == NS_SVG && el->tag != A_svg) st->display = D_NONE;
    /* split normal / important so presentational hints & inline style go between */
    int split = 0; while (split < md.n && (md.v[split].key >> 62) < 2) split++;
    presentational_hints(st, par, el, e);
    apply_decl_ordered(st, par, md.v, split, e, el);
    const char *inl = node_attr(el, "style");
    if (inl) {
        if (!el->inline_style_src || strcmp(el->inline_style_src, inl)) { free(el->inline_style_src); el->inline_style_src = xstrdup(inl); css_decls_free(el->inline_decls); el->inline_decls = css_parse_decls(inl, strlen(inl)); }
        DeclList *dl = el->inline_decls;
        VEC(MDecl) im = {0};
        for (int i = 0; i < dl->n; i++) { MDecl m = { &dl->v[i], 0 }; vec_push(im, m); }
        apply_decl_ordered(st, par, im.v, im.n, e, el);
        vec_free(im);
    }
    apply_decl_ordered(st, par, md.v + split, md.n - split, e, el);
    vec_free(md);
    /* fixups */
    if (st->line_height_factor > 0) st->line_height = st->line_height_factor * st->font_size;
    if (st->position == P_ABSOLUTE || st->position == P_FIXED || st->float_ != F_NONE) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK || st->display == D_TABLE_CELL || st->display == D_TABLE_ROW) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_GRID) st->display = D_GRID;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    if (st->position == P_ABSOLUTE || st->position == P_FIXED) st->float_ = F_NONE;
    if (el->parent && el->parent->type == NODE_DOCUMENT && (st->display == D_INLINE || st->display == D_CONTENTS)) st->display = D_BLOCK;
    if (par && (par->display == D_FLEX || par->display == D_INLINE_FLEX || par->display == D_GRID || par->display == D_INLINE_GRID)) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_GRID) st->display = D_GRID;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    for (int i = 0; i < 4; i++) if (st->border_style[i] == BS_NONE || st->border_style[i] == BS_HIDDEN) st->border_width[i] = 0;
    if (st->outline_style == BS_NONE) st->outline_width = 0;
    if (node_has_attr(el, "hidden") && el->ns == NS_HTML && st->display != D_NONE) {
        const char *hv = node_attr(el, "hidden"); if (!hv || !str_ieq(hv, "until-found")) { /* author CSS can override display; UA rule already sets none */ }
    }
    return st;
}

static void recalc(StyleEngine *e, Node *n, const ComputedStyle *par, bool force) {
    if (n->type == NODE_ELEMENT) {
        bool need = force || (n->flags & NF_STYLE_DIRTY) || !n->style;
        if (need) {
            ComputedStyle *st = compute(e, n, par);
            if (st->display != D_NONE) {
                if (e->idx.count) { st->before = compute_pseudo(e, n, st, 1); st->after = compute_pseudo(e, n, st, 2); }
            }
            if (css_style_change_hook) css_style_change_hook(n, n->style, st);
            if (n->style) style_free(n->style);
            n->style = st;
            force = true; /* children inherit */
            e->stats_matched++;
        }
        n->flags &= ~(uint32_t)(NF_STYLE_DIRTY);
        if (!force && !(n->flags & NF_CHILD_STYLE_DIRTY)) return;
        n->flags &= ~(uint32_t)NF_CHILD_STYLE_DIRTY;
        if (n->style->display == D_NONE && !force) return;
        Node *kids = n->template_content ? NULL : n;
        if (kids) for (Node *c = n->first; c; c = c->next) recalc(e, c, n->style, force);
        if (n->shadow_root) for (Node *c = n->shadow_root->first; c; c = c->next) recalc(e, c, n->style, force);
    } else if (n->type == NODE_DOCUMENT || n->type == NODE_FRAGMENT) {
        n->flags &= ~(uint32_t)(NF_STYLE_DIRTY | NF_CHILD_STYLE_DIRTY);
        for (Node *c = n->first; c; c = c->next) recalc(e, c, par, force);
    } else n->flags &= ~(uint32_t)NF_STYLE_DIRTY;
}

void style_recalc(StyleEngine *e, Node *root, bool force) {
    if (e->idx_dirty) { idx_build(e); force = true; }
    e->stats_matched = 0;
    recalc(e, root, NULL, force);
}
ComputedStyle *style_for_text(Node *t) { Node *p = t->parent; while (p && p->type != NODE_ELEMENT) p = p->parent ? p->parent : p->host; return p ? p->style : NULL; }

static void color_str(SB *b, Color c) { if (COLOR_A(c) == 255) sb_printf(b, "rgb(%u, %u, %u)", COLOR_R(c), COLOR_G(c), COLOR_B(c)); else sb_printf(b, "rgba(%u, %u, %u, %g)", COLOR_R(c), COLOR_G(c), COLOR_B(c), COLOR_A(c) / 255.0); }
static void len_str(SB *b, Length l, const char *au) {
    if (l.kind == LK_AUTO) sb_puts(b, au);
    else if (l.kind == LK_NONE) sb_puts(b, "none");
    else if (l.kind == LK_MIN_CONTENT) sb_puts(b, "min-content");
    else if (l.kind == LK_MAX_CONTENT) sb_puts(b, "max-content");
    else if (l.kind == LK_FIT_CONTENT) sb_puts(b, "fit-content");
    else if (l.pct && l.px) sb_printf(b, "calc(%g%% + %gpx)", l.pct, l.px);
    else if (l.pct) sb_printf(b, "%g%%", l.pct);
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
static void gap_str(SB *b, float px, Length l) { if (l.kind == LK_LEN && l.pct) len_str(b, l, "normal"); else if (px) sb_printf(b, "%gpx", px); else sb_puts(b, "normal"); }
char *css_get_computed_value(Node *el, const char *prop) {
    ComputedStyle *s = el && el->type == NODE_ELEMENT ? el->style : NULL;
    SB b; sb_init(&b);
    if (!s) return sb_take(&b);
    static const char *const ds[] = { "none", "inline", "block", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "list-item", "table", "inline-table", "table-row", "table-cell", "table-row-group", "table-header-group", "table-footer-group", "table-column", "table-column-group", "table-caption", "contents", "flow-root" };
    static const char *const ps[] = { "static", "relative", "absolute", "fixed", "sticky" };
    if (prop[0] == '-' && prop[1] == '-') { const char *v = s->custom ? hm_get(&s->custom->map, prop) : NULL; if (v) sb_puts(&b, v); }
    else if (!strcmp(prop, "display")) sb_puts(&b, ds[s->display]);
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
    else if (!strcmp(prop, "color")) color_str(&b, s->color);
    else if (!strcmp(prop, "background-color")) color_str(&b, s->bg_color);
    else if (!strcmp(prop, "background-image")) {
        const Gradient *g = s->bg_gradient;
        if (s->bg_image) sb_printf(&b, "url(\"%s\")", s->bg_image);
        else if (!g) sb_puts(&b, "none");
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
    else if (!strcmp(prop, "line-height")) { if (s->line_height_normal) sb_puts(&b, "normal"); else sb_printf(&b, "%gpx", s->line_height); }
    else if (!strcmp(prop, "overflow") || !strcmp(prop, "overflow-y") || !strcmp(prop, "overflow-x")) { static const char *ov[] = { "visible", "hidden", "scroll", "auto", "clip" }; sb_puts(&b, ov[prop[9] == 'x' ? s->overflow_x : s->overflow_y]); }
    else if (!strcmp(prop, "z-index")) { if (s->z_auto) sb_puts(&b, "auto"); else sb_printf(&b, "%d", s->z_index); }
    else if (!strcmp(prop, "direction")) sb_puts(&b, s->direction ? "rtl" : "ltr");
    else if (!strcmp(prop, "pointer-events")) sb_puts(&b, s->pointer_events ? "auto" : "none");
    else if (!strcmp(prop, "box-sizing")) sb_puts(&b, s->box_sizing ? "border-box" : "content-box");
    else if (!strcmp(prop, "transform")) { if (s->has_transform) sb_printf(&b, "matrix(%g, %g, %g, %g, %g, %g)", s->transform[0], s->transform[1], s->transform[2], s->transform[3], s->transform[4], s->transform[5]); else sb_puts(&b, "none"); }
    else if (!strncmp(prop, "margin-", 7) || !strncmp(prop, "padding-", 8)) {
        bool m = prop[0] == 'm'; const char *side = prop + (m ? 7 : 8);
        int i = !strcmp(side, "top") ? 0 : !strcmp(side, "right") ? 1 : !strcmp(side, "bottom") ? 2 : 3;
        Length l = m ? s->margin[i] : s->padding[i]; sb_printf(&b, "%gpx", l.px);
    }
    else if (!strcmp(prop, "width") || !strcmp(prop, "height")) {
        bool W = prop[0] == 'w'; const Box *bx = el->box;
        if (bx && s->display != D_INLINE) {
            float v = W ? bx->w : bx->h;
            if (!s->box_sizing) v -= W ? bx->p[1] + bx->p[3] + bx->b[1] + bx->b[3] : bx->p[0] + bx->p[2] + bx->b[0] + bx->b[2];
            sb_printf(&b, "%gpx", v > 0 ? v : 0);
        } else len_str(&b, W ? s->width : s->height, "auto");
    }
    else if (!strcmp(prop, "min-width")) len_str(&b, s->min_width, "auto");
    else if (!strcmp(prop, "min-height")) len_str(&b, s->min_height, "auto");
    else if (!strcmp(prop, "max-width")) len_str(&b, s->max_width, "none");
    else if (!strcmp(prop, "max-height")) len_str(&b, s->max_height, "none");
    else if (!strcmp(prop, "top")) len_str(&b, s->inset[0], "auto");
    else if (!strcmp(prop, "right")) len_str(&b, s->inset[1], "auto");
    else if (!strcmp(prop, "bottom")) len_str(&b, s->inset[2], "auto");
    else if (!strcmp(prop, "left")) len_str(&b, s->inset[3], "auto");
    else if (!strcmp(prop, "flex-grow")) sb_printf(&b, "%g", s->flex_grow);
    else if (!strcmp(prop, "flex-shrink")) sb_printf(&b, "%g", s->flex_shrink);
    else if (!strcmp(prop, "flex-basis")) len_str(&b, s->flex_basis, "auto");
    else if (!strcmp(prop, "flex")) { sb_printf(&b, "%g %g ", s->flex_grow, s->flex_shrink); len_str(&b, s->flex_basis, "auto"); }
    else if (!strcmp(prop, "column-gap")) gap_str(&b, s->column_gap, s->column_gap_l);
    else if (!strcmp(prop, "row-gap")) gap_str(&b, s->row_gap, s->row_gap_l);
    else if (!strcmp(prop, "gap")) { gap_str(&b, s->row_gap, s->row_gap_l); sb_putc(&b, ' '); gap_str(&b, s->column_gap, s->column_gap_l); }
    else if (!strcmp(prop, "grid-template-columns")) tracks_str(&b, s->grid_cols, s->grid_ncols);
    else if (!strcmp(prop, "grid-template-rows")) tracks_str(&b, s->grid_rows, s->grid_nrows);
    else sb_puts(&b, "");
    return sb_take(&b);
}
