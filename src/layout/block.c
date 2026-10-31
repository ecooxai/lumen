/* Block layout, floats, positioning, driver, hit testing */
#include "layout.h"
#include <math.h>
#include <stdatomic.h>
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#endif

/* Shared across documents: a parent observer can watch an iframe element. */
static atomic_uint_fast64_t geometry_version;
uint64_t layout_generation(void) { return atomic_load_explicit(&geometry_version, memory_order_relaxed); }

void compute_mbp(Box *b, float cbw) {
    const ComputedStyle *s = b->st;
    for (int i = 0; i < 4; i++) {
        b->m[i] = s->margin[i].kind == LK_LEN ? res(s->margin[i], cbw) : 0;
        b->p[i] = LMAX(0, res(s->padding[i], cbw));
        b->b[i] = b->anon ? 0 : s->border_width[i];
    }
    if (b->anon && b->kind == BX_BLOCK && !b->node) for (int i = 0; i < 4; i++) { b->m[i] = b->p[i] = 0; }
}

/* ---------------- floats ---------------- */
void fc_avail(FloatCtx *fc, float y, float h, float x0, float x1, float *l, float *r) {
    *l = x0; *r = x1;
    if (!fc) return;
    for (int i = 0; i < fc->n; i++) {
        FRect *f = &fc->v[i];
        if (f->y >= y + h || f->y + f->h <= y) continue;
        if (f->side == F_LEFT) *l = LMAX(*l, f->x + f->w); else *r = LMIN(*r, f->x);
    }
}
void fc_place(FloatCtx *fc, Box *b, float y, float x0, float x1) {
    float ow = b->w + b->m[1] + b->m[3], oh = b->h + b->m[0] + b->m[2];
    int side = b->st->float_ == F_RIGHT ? F_RIGHT : F_LEFT;
    if (b->st->clear) y = LMAX(y, fc_clear(fc, b->st->clear));
    for (int iter = 0; iter < 200; iter++) {
        float l, r; fc_avail(fc, y, LMAX(oh, 0.01f), x0, x1, &l, &r);
        if (r - l >= ow - 0.01f || (l == x0 && r == x1)) {
            float fx = side == F_LEFT ? l : r - ow;
            box_translate(b, fx + b->m[3] - b->x, y + b->m[0] - b->y);
            if (fc->n == fc->cap) { fc->cap = fc->cap ? fc->cap * 2 : 8; fc->v = xrealloc(fc->v, sizeof(FRect) * (size_t)fc->cap); }
            fc->v[fc->n++] = (FRect){ fx, y, ow, oh, (uint8_t)side };
            return;
        }
        float ny = 1e30f;
        for (int i = 0; i < fc->n; i++) { FRect *f = &fc->v[i]; if (f->y <= y + 0.01f && f->y + f->h > y) ny = LMIN(ny, f->y + f->h); }
        if (ny >= 1e29f) break;
        y = ny;
    }
    box_translate(b, x0 + b->m[3] - b->x, y + b->m[0] - b->y);
}
float fc_clear(FloatCtx *fc, int side) {
    float y = -1e30f; if (!fc) return y;
    for (int i = 0; i < fc->n; i++) { FRect *f = &fc->v[i]; if (side == CLEAR_BOTH || (side == CLEAR_LEFT && f->side == F_LEFT) || (side == CLEAR_RIGHT && f->side == F_RIGHT)) y = LMAX(y, f->y + f->h); }
    return y;
}
float fc_bottom(FloatCtx *fc) { return fc ? fc_clear(fc, CLEAR_BOTH) : -1e30f; }

/* ---------------- translate ---------------- */
static bool within(Box *x, Box *root) { for (; x; x = x->parent) if (x == root) return true; return false; }
static void tr(Box *root, Box *b, float dx, float dy) {
    b->x += dx; b->y += dy;
    if (b->has_baseline) b->baseline += dy;
    for (int i = 0; i < b->nfrags; i++) { b->frags[i].x += dx; b->frags[i].y += dy; if (b->frags[i].box->kind == BX_TEXT) b->frags[i].base += dy; }
    for (int i = 0; i < b->nlines; i++) { b->lines[i].y += dy; b->lines[i].base += dy; }
    for (int i = 0; i < b->nir; i++) { b->ir[i].x += dx; b->ir[i].y += dy; }
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs && (c->fixed || (c->cb && !within(c->cb, root)))) { c->sx += dx; c->sy += dy; continue; }   /* placed later from its static position */
        tr(root, c, dx, dy);
    }
}
void box_translate(Box *b, float dx, float dy) { if (dx || dy) tr(b, b, dx, dy); }

/* ---------------- absolute positioning ---------------- */
void add_abs(Layout *L, Box *c) {
    if (L->measure_only) return;
    Box *cb = L->root;
    if (!c->fixed) for (Box *p = c->parent; p; p = p->parent) if (p->st && p->kind != BX_INLINE && (p->st->position != P_STATIC || p->st->has_transform || layout_contents_hidden(p)) && p != c) { cb = p; break; }
    if (c->fixed) for (Box *p = c->parent; p; p = p->parent) if (p->st && (p->st->has_transform || layout_contents_hidden(p)) && p->kind != BX_INLINE) { cb = p; break; }
    c->cb = cb; cb->has_abs = true;
    if (L->nalog == L->calog) { L->calog = L->calog ? L->calog * 2 : 64; L->alog = xrealloc(L->alog, sizeof *L->alog * (size_t)L->calog); }
    L->alog[L->nalog++] = c;
    for (Box *x = cb->abs_head; x; x = x->abs_next) if (x == c) return;
    c->abs_next = cb->abs_head; cb->abs_head = c;
}

/* anchor(<name>? <side>[, <fallback>]) as a whole inset (0 top, 1 right, 2 bottom, 3 left): the named element's edge, as an
   inset from the containing block's padding box (origin px,py, size pw,ph). false: unresolvable (the fallback, else auto). */
static bool name_in_list(const char *list, const char *nm, size_t nl) {
    for (const char *p = list; (p = strstr(p, nm)); p += nl) {
        bool st = p == list || p[-1] == ',' || p[-1] == ' ', en = !p[nl] || p[nl] == ',' || p[nl] == ' ';
        if (st && en) return true;
    }
    return false;
}
static const Box *find_anchor(const Box *a, const char *name) {   /* the last laid-out element with that anchor-name */
    const Box *ab = NULL; size_t nl = strlen(name); Node *root = &a->node->doc->node;
    /* acceptable: inside the positioned box's containing block, or before the positioned box in tree order */
    const Node *cbn = a->cb && a->cb->node ? a->cb->node : NULL; bool before = true;
    for (Node *m = root->first; m; m = node_next_in_tree(m, root)) {
        if (m == a->node) { before = false; continue; }
        if (m->type != NODE_ELEMENT || !m->style || !m->style->anchor_name || !m->box || !name_in_list(m->style->anchor_name, name, nl)) continue;
        bool inside = false, incb = !cbn; for (Node *p = m->parent; p; p = p->parent) { if (p == a->node) inside = true; if (p == cbn) incb = true; }
        if (!inside && (before || incb)) ab = m->box;
    }
    return ab;
}
/* anchor-size(<name>? <size>[, <fallback>]) as a whole width (h=0) or height (h=1), border-box excluded */
static bool anchor_size(const Box *a, int h, float cbs, float *out) {
    const char *v = a->st->anchor_sz[h];
    while (*v == ' ') v++;
    if (strncmp(v, "anchor-size(", 12)) return false;
    const char *q = v + 12, *end = strrchr(q, ')'); if (!end) return false;
    for (const char *z = end + 1; *z; z++) if (*z != ' ') return false;
    char buf[256]; size_t n = (size_t)(end - q); if (n >= sizeof buf) return false;
    memcpy(buf, q, n); buf[n] = 0;
    char *fb = strchr(buf, ','); if (fb) *fb++ = 0;
    char *tok[3]; int nt = 0;
    for (char *s = strtok(buf, " \t\n"); s && nt < 3; s = strtok(NULL, " \t\n")) tok[nt++] = s;
    const char *name = nt && !strncmp(tok[0], "--", 2) ? tok[0] : NULL, *dim = nt > (name ? 1 : 0) ? tok[name ? 1 : 0] : NULL;
    if (!name) name = a->st->pos_anchor;
    const Box *ab = name ? find_anchor(a, name) : NULL;
    if (ab) {
        bool vm = a->st->writing_mode != 0, wide;
        if (!dim) wide = !h;
        else if (!strcmp(dim, "width")) wide = true;
        else if (!strcmp(dim, "height")) wide = false;
        else if (!strcmp(dim, "inline") || !strcmp(dim, "self-inline")) wide = !vm;
        else if (!strcmp(dim, "block") || !strcmp(dim, "self-block")) wide = vm;
        else return false;
        *out = wide ? ab->w : ab->h; return true;
    }
    if (!fb) return false;
    Length l; if (!css_parse_length(fb, &l, a->st->font_size, a->st->font_size, NULL) || l.kind != LK_LEN) return false;
    *out = res(l, cbs); return true;
}
static bool anchor_inset(const Box *a, const Box *cb, int i, int src, const int kwm[4], float px, float py, float pw, float ph, float *out) {
    const char *v = a->st->anchor_ins[src];   /* src != i: a try tactic moved it from side src; kwm maps its anchor side */
    while (*v == ' ') v++;
    if (strncmp(v, "anchor(", 7)) return false;
    const char *q = v + 7, *end = strrchr(q, ')'); if (!end) return false;
    for (const char *z = end + 1; *z; z++) if (*z != ' ') return false;
    char buf[256]; size_t n = (size_t)(end - q); if (n >= sizeof buf) return false;
    memcpy(buf, q, n); buf[n] = 0;
    char *fb = strchr(buf, ','); if (fb) *fb++ = 0;
    char *tok[3]; int nt = 0;
    for (char *s = strtok(buf, " \t\n"); s && nt < 3; s = strtok(NULL, " \t\n")) tok[nt++] = s;
    const char *name = nt && !strncmp(tok[0], "--", 2) ? tok[0] : NULL, *side = nt > (name ? 1 : 0) ? tok[name ? 1 : 0] : NULL;
    if (!name) name = a->st->pos_anchor;   /* the default anchor */
    const Box *ab = name && side ? find_anchor(a, name) : NULL;
    float scx = 0, scy = 0;   /* scrollers between the anchor and the containing block shift it */
    if (ab) for (const Node *p = ab->node ? ab->node->parent : NULL; p && p != cb->node; p = p->parent) if (p->box && p->box->scroller) { scx += box_scroll_left(p->box); scy += box_scroll_top(p->box); }
    if (ab) {
        /* the side as a physical side q (0 top 1 right 2 bottom 3 left) and a fraction from q toward the opposite side,
           read on the declaring side's axis, then moved by the try tactics */
        bool hzs = src & 1, low = i == 0 || i == 3, fixed = false;
        int q = hzs ? 3 : 0; float fr = -1;
        const ComputedStyle *cs = cb->st ? cb->st : a->st;
        bool cflip = hzs ? (cs->writing_mode ? cs->writing_mode == 2 : cs->direction) : (cs->writing_mode ? cs->direction : false);
        bool sflip = hzs ? (a->st->writing_mode ? a->st->writing_mode == 2 : a->st->direction) : (a->st->writing_mode ? a->st->direction : false);
        char *e;
        float pct = strtof(side, &e);
        if (e != side && *e == '%' && !e[1]) fr = pct / 100;
        else if (!strcmp(side, "center")) fr = 0.5f;
        else if (hzs && !strcmp(side, "left")) fr = 0;
        else if (hzs && !strcmp(side, "right")) fr = 1;
        else if (!hzs && !strcmp(side, "top")) fr = 0;
        else if (!hzs && !strcmp(side, "bottom")) fr = 1;
        else if (!strcmp(side, "start") || !strcmp(side, "end")) fr = (side[0] == 's') != cflip ? 0 : 1;
        else if (!strcmp(side, "self-start") || !strcmp(side, "self-end")) fr = (side[5] == 's') != sflip ? 0 : 1;
        else if (!strcmp(side, "inside") || !strcmp(side, "outside")) { q = i; fr = side[0] == 'i' ? 0 : 1; fixed = true; }
        if (fr >= 0 && !fixed) q = kwm[q];
        if (fr >= 0 && (q & 1) == (i & 1)) {
            bool hz = i & 1;
            float lo = hz ? ab->x - px - scx : ab->y - py - scy, sz = hz ? ab->w : ab->h;
            float pos = q == 0 || q == 3 ? lo + fr * sz : lo + sz - fr * sz;
            *out = low ? pos : (hz ? pw : ph) - pos; return true;
        }
    }
    if (!fb) return false;
    Length l; if (!css_parse_length(fb, &l, a->st->font_size, a->st->font_size, NULL) || l.kind != LK_LEN) return false;
    *out = res(l, i & 1 ? pw : ph); return true;
}

/* one position-area keyword: its axis (0 x, 1 y, 2 either) and the grid tracks lo..hi it covers (0 before the anchor,
   1 the anchor, 2 after); horizontal writing modes only, rtl flips the inline axis */
static int area_kw(const char *k, bool rtl, bool srtl, int *lo, int *hi) {
    static const struct { const char *k; int8_t ax, lo, hi, inl; } t[] = {
        { "left", 0, 0, 0, 0 }, { "right", 0, 2, 2, 0 }, { "span-left", 0, 0, 1, 0 }, { "span-right", 0, 1, 2, 0 },
        { "top", 1, 0, 0, 0 }, { "bottom", 1, 2, 2, 0 }, { "span-top", 1, 0, 1, 0 }, { "span-bottom", 1, 1, 2, 0 },
        { "x-start", 0, 0, 0, 1 }, { "x-end", 0, 2, 2, 1 }, { "span-x-start", 0, 0, 1, 1 }, { "span-x-end", 0, 1, 2, 1 },
        { "inline-start", 0, 0, 0, 1 }, { "inline-end", 0, 2, 2, 1 }, { "span-inline-start", 0, 0, 1, 1 }, { "span-inline-end", 0, 1, 2, 1 },
        { "self-x-start", 0, 0, 0, 2 }, { "self-x-end", 0, 2, 2, 2 }, { "span-self-x-start", 0, 0, 1, 2 }, { "span-self-x-end", 0, 1, 2, 2 },
        { "self-inline-start", 0, 0, 0, 2 }, { "self-inline-end", 0, 2, 2, 2 }, { "span-self-inline-start", 0, 0, 1, 2 }, { "span-self-inline-end", 0, 1, 2, 2 },
        { "y-start", 1, 0, 0, 0 }, { "y-end", 1, 2, 2, 0 }, { "span-y-start", 1, 0, 1, 0 }, { "span-y-end", 1, 1, 2, 0 },
        { "block-start", 1, 0, 0, 0 }, { "block-end", 1, 2, 2, 0 }, { "span-block-start", 1, 0, 1, 0 }, { "span-block-end", 1, 1, 2, 0 },
        { "self-y-start", 1, 0, 0, 0 }, { "self-y-end", 1, 2, 2, 0 }, { "span-self-y-start", 1, 0, 1, 0 }, { "span-self-y-end", 1, 1, 2, 0 },
        { "self-block-start", 1, 0, 0, 0 }, { "self-block-end", 1, 2, 2, 0 }, { "span-self-block-start", 1, 0, 1, 0 }, { "span-self-block-end", 1, 1, 2, 0 },
        { "center", 2, 1, 1, 0 }, { "span-all", 2, 0, 2, 0 }, { "start", 2, 0, 0, 3 }, { "end", 2, 2, 2, 3 },
        { "span-start", 2, 0, 1, 3 }, { "span-end", 2, 1, 2, 3 }, { "self-start", 2, 0, 0, 4 }, { "self-end", 2, 2, 2, 4 },
        { "span-self-start", 2, 0, 1, 4 }, { "span-self-end", 2, 1, 2, 4 } };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (!strcmp(k, t[i].k)) {
        *lo = t[i].lo; *hi = t[i].hi;
        if ((t[i].inl == 1 && rtl) || (t[i].inl == 2 && srtl)) { int x = *lo; *lo = 2 - *hi; *hi = 2 - x; }
        return t[i].ax | (t[i].inl == 3 ? 8 : t[i].inl == 4 ? 16 : 0);   /* 8/16: generic start/end, flipped by direction if it lands inline */
    }
    return -1;
}
/* position-area: shrink the containing block to the chosen region around the default anchor. f[0]/f[1]: where a box
   that isn't stretched sits in it (0 start, 1 end), -2 when stretched. false: no area applies. */
static bool area_apply(const Box *a, const Box *cb, const char *area, const int kwm[4], float *px, float *py, float *pw, float *ph, float f[2]) {
    const ComputedStyle *s = a->st, *cs = cb->st ? cb->st : s;
    const Box *ab = find_anchor(a, s->pos_anchor);
    if (!ab) return false;
    char buf[128]; snprintf(buf, sizeof buf, "%s", area);
    char *tk[3]; int nt = 0;
    for (char *p = strtok(buf, " \t\n"); p && nt < 3; p = strtok(NULL, " \t\n")) tk[nt++] = p;
    if (nt < 1 || nt > 2) return false;
    int lo[2], hi[2], ax[2];
    for (int i = 0; i < nt; i++) { ax[i] = area_kw(tk[i], cs->direction, s->direction, &lo[i], &hi[i]); if (ax[i] < 0) return false; }
    if (nt == 1) { ax[1] = ax[0]; lo[1] = lo[0]; hi[1] = hi[0]; if ((ax[0] & 3) != 2) { ax[1] = 2; lo[1] = 0; hi[1] = 2; } }
    /* two generics: block (y) then inline (x); one generic takes the other axis */
    int a0 = ax[0] & 3, a1 = ax[1] & 3;
    if (a0 == 2 && a1 == 2) { a0 = 1; a1 = 0; } else if (a0 == 2) a0 = !a1; else if (a1 == 2) a1 = !a0;
    if (a0 == a1) return false;
    int rx = a0 == 0 ? 0 : 1, ry = !rx;
    int xl = lo[rx], xh = hi[rx], yl = lo[ry], yh = hi[ry];
    if (((ax[rx] & 8) && cs->direction) || ((ax[rx] & 16) && s->direction)) { int x = xl; xl = 2 - xh; xh = 2 - x; }
    {   /* try tactics: the y tracks (from side 0) and x tracks (from side 3) move with their start sides */
        int yl0 = yl, yh0 = yh, xl0 = xl, xh0 = xh, sy0 = kwm[0], sx0 = kwm[3];
        int *dl = sy0 & 1 ? &xl : &yl, *dh = sy0 & 1 ? &xh : &yh;
        if (sy0 == 0 || sy0 == 3) { *dl = yl0; *dh = yh0; } else { *dl = 2 - yh0; *dh = 2 - yl0; }
        dl = sx0 & 1 ? &xl : &yl; dh = sx0 & 1 ? &xh : &yh;
        if (sx0 == 0 || sx0 == 3) { *dl = xl0; *dh = xh0; } else { *dl = 2 - xh0; *dh = 2 - xl0; }
    }
    float scx = 0, scy = 0;
    for (const Node *p = ab->node ? ab->node->parent : NULL; p && p != cb->node; p = p->parent) if (p->box && p->box->scroller) { scx += box_scroll_left(p->box); scy += box_scroll_top(p->box); }
    float X[4] = { 0, ab->x - *px - scx, ab->x - *px - scx + ab->w, *pw }, Y[4] = { 0, ab->y - *py - scy, ab->y - *py - scy + ab->h, *ph };
    for (int i = 1; i < 3; i++) { X[i] = LMIN(LMAX(X[i], X[0]), X[3]); Y[i] = LMIN(LMAX(Y[i], Y[0]), Y[3]); }
    *px += X[xl]; *pw = X[xh + 1] - X[xl]; *py += Y[yl]; *ph = Y[yh + 1] - Y[yl];
    /* normal alignment points toward the anchor; span-all centres */
    uint8_t al[2] = { s->justify_self, s->align_self }; int l2[2] = { xl, yl }, h2[2] = { xh, yh };
    for (int i = 0; i < 2; i++) {
        uint8_t v = al[i];
        if (v == AL_STRETCH) f[i] = -2;
        else if (v == AL_START || v == AL_FLEX_START) f[i] = 0;
        else if (v == AL_END || v == AL_FLEX_END) f[i] = 1;
        else if (v == AL_CENTER) f[i] = 0.5f;
        else f[i] = l2[i] == 0 && h2[i] < 2 ? 1 : h2[i] == 2 && l2[i] > 0 ? 0 : 0.5f;
    }
    return true;
}

/* Flexbox 4.1: out-of-flow children align as a sole fixed-used-size item.
 * Their real absolute containing block can be a different ancestor. */
static bool static_axis_reverse(const ComputedStyle *s, bool horizontal) {
    return horizontal ? (s->writing_mode ? s->writing_mode == 2 : s->direction != 0) : (s->writing_mode && s->direction);
}
static const char *static_align_text(const ComputedStyle *s, const char *property, int fallback, char out[64]) {
    static const char *const names[] = { "auto", "normal", "stretch", "flex-start", "flex-end", "center", "baseline", "space-between", "space-around", "space-evenly", "start", "end" };
    const char *value = custom_get(s->gen_n, property);
    if (!value) return fallback >= 0 && (size_t)fallback < sizeof names / sizeof *names ? names[fallback] : "normal";
    if (!*value) return !strcmp(property, "align-self") ? "auto" : "normal";
    size_t n = 0; bool space = false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (is_ws(*p)) { if (n) space = true; continue; }
        if (n + (space ? 1 : 0) + 1 >= 64) return "normal";
        if (space) out[n++] = ' '; space = false;
        out[n++] = *p >= 'A' && *p <= 'Z' ? (char)(*p + ('a' - 'A')) : (char)*p;
    }
    out[n] = 0; return out;
}
static float static_align_offset(const char *value, const Box *a, bool horizontal, bool flex_reverse, bool normal_reverse, bool main_axis, bool row, float free_space) {
    bool safe = false;
    if (!strncmp(value, "safe ", 5)) { safe = true; value += 5; }
    else if (!strncmp(value, "unsafe ", 7)) value += 7;
    float fraction = flex_reverse ? 1 : 0;
    if (!strcmp(value, "center") || (main_axis && (!strcmp(value, "space-around") || !strcmp(value, "space-evenly")))) fraction = .5f;
    else if (!strcmp(value, "flex-end")) fraction = flex_reverse ? 0 : 1;
    else if (!strcmp(value, "start")) fraction = normal_reverse ? 1 : 0;
    else if (!strcmp(value, "end")) fraction = normal_reverse ? 0 : 1;
    /* Match the interoperable flex static-position baseline fallback in the
       container's logical axes, independently of child axes and wrap reversal. */
    else if (!strcmp(value, "baseline") || !strcmp(value, "first baseline")) fraction = normal_reverse ? 1 : 0;
    else if (!strcmp(value, "last baseline")) fraction = normal_reverse ? 0 : 1;
    else if (!strcmp(value, "self-start")) fraction = static_axis_reverse(a->st, horizontal) ? 1 : 0;
    else if (!strcmp(value, "self-end")) fraction = static_axis_reverse(a->st, horizontal) ? 0 : 1;
    else if (!strcmp(value, "left")) fraction = horizontal || row ? 0 : (normal_reverse ? 1 : 0);
    else if (!strcmp(value, "right")) fraction = horizontal || row ? 1 : (normal_reverse ? 1 : 0);
    if (safe && free_space < 0) fraction = flex_reverse ? 1 : 0;
    return free_space * fraction;
}
static void flex_static_position(const Box *a, bool auto_x, bool auto_y, float *sx, float *sy) {
    const Box *p = a->parent;
    if (!p || p->fmt != FMT_FLEX || !p->st || (!auto_x && !auto_y)) return;
    const ComputedStyle *s = p->st;
    bool row = s->flex_direction <= FD_ROW_REVERSE;
    bool horizontal = row != (s->writing_mode != 0);
    bool normal_main_reverse = static_axis_reverse(s, horizontal);
    bool normal_cross_reverse = static_axis_reverse(s, !horizontal);
    bool main_reverse = normal_main_reverse ^ (s->flex_direction == FD_ROW_REVERSE || s->flex_direction == FD_COLUMN_REVERSE);
    bool cross_reverse = normal_cross_reverse ^ (s->flex_wrap == 2);
    char main_buf[64], cross_buf[64], parent_buf[64];
    const char *main_align = static_align_text(s, "justify-content", s->justify_content, main_buf);
    const char *cross_align = static_align_text(a->st, "align-self", a->st->align_self, cross_buf);
    if (!strcmp(cross_align, "auto")) cross_align = static_align_text(s, "align-items", s->align_items, parent_buf);
    /* compute_mbp resolves auto margins to zero for these out-of-flow boxes. */
    float free_x = LMAX(0, p->w - hbp(p)) - a->w - a->m[1] - a->m[3];
    float free_y = LMAX(0, p->h - vbp(p)) - a->h - a->m[0] - a->m[2];
    float main_off = static_align_offset(main_align, a, horizontal, main_reverse, normal_main_reverse, true, row, horizontal ? free_x : free_y);
    float cross_off = static_align_offset(cross_align, a, !horizontal, cross_reverse, normal_cross_reverse, false, row, horizontal ? free_y : free_x);
    if (auto_x) *sx = p->x + p->b[3] + p->p[3] + (horizontal ? main_off : cross_off);
    if (auto_y) *sy = p->y + p->b[0] + p->p[0] + (horizontal ? cross_off : main_off);
}

/* places one absolutely positioned box, with a position-area and flips (1 block, 2 inline) from its style or a
   position-try fallback; true when its margin box fits the containing block */
static float clampw_basis(Layout *L, Box *b, float w, float cbw, float bp, float available, bool definite);
static float clamph_basis(Box *b, float h, float cbh, float bp, float available, bool definite);
static float clamph(Box *b, float h, float cbh, float bp);
/* Private absolute shrink mode: cbw remains the percentage containing-block
   width; fw is the available inset-constrained width, before margins. Existing
   memo keys include mode, cbw and fw, so the two inputs remain independent. */
enum { SZ_ABS_SHRINK = SZ_FORCED + 1 };

static float g_imcb[2];   /* the last abs_place's inset-modified containing block size */
static bool abs_place(Layout *L, Box *cb, Box *a, const char *area_v, const int src[4], const int kwm[4]) {
    float px, py, pw, ph;
    if (cb == L->root || (a->fixed && cb == L->root)) { px = 0; py = 0; pw = L->vw; ph = L->vh; }
    else { px = cb->x + cb->b[3]; py = cb->y + cb->b[0]; pw = cb->w - cb->b[1] - cb->b[3]; ph = cb->h - cb->b[0] - cb->b[2]; }
    grid_absolute_containing_block(cb, a, &px, &py, &pw, &ph);
    const ComputedStyle *s = a->st;
    float af[2] = { -1, -1 };
    float x0 = px, y0 = py, w0 = pw, h0 = ph;
    bool area = area_v && s->pos_anchor && area_apply(a, cb, area_v, kwm, &px, &py, &pw, &ph, af);
    compute_mbp(a, pw);
    { float m[4]; memcpy(m, a->m, sizeof m); for (int k = 0; k < 4; k++) a->m[k] = m[src[k]]; }
#define FI(k) (src[k])   /* the declared side that side k takes under the try tactics */
    bool lA = s->inset[FI(3)].kind != LK_LEN, rA = s->inset[FI(1)].kind != LK_LEN, tA = s->inset[FI(0)].kind != LK_LEN, bA = s->inset[FI(2)].kind != LK_LEN;
    float l = res(s->inset[FI(3)], pw), r = res(s->inset[FI(1)], pw), t = res(s->inset[FI(0)], ph), bt = res(s->inset[FI(2)], ph);
    if (s->anchor_ins[0] || s->anchor_ins[1] || s->anchor_ins[2] || s->anchor_ins[3]) {
        float *iv[4] = { &t, &r, &bt, &l }; bool *ia[4] = { &tA, &rA, &bA, &lA };
        a->anchor_ok = 0;
        for (int k = 0; k < 4; k++) if (s->anchor_ins[FI(k)] && anchor_inset(a, cb, k, FI(k), kwm, px, py, pw, ph, iv[k])) { *ia[k] = false; a->anchor_ok |= (uint8_t)(1 << k); }
    }
    if (area) {   /* auto insets are 0 in the area; a box that isn't stretched keeps its size and is aligned after layout */
        if (lA) { lA = false; l = 0; }
        if (rA) { rA = false; r = 0; }
        if (tA) { tA = false; t = 0; }
        if (bA) { bA = false; bt = 0; }
        if (af[0] >= 0) rA = true;
        if (af[1] >= 0) bA = true;
    }
    /* CSS Position3 3.5.2: a stretch size uses a nonnegative inset-modified
       rectangle. Reduce its weaker inset, retaining the same adjusted edges
       for both sizing and final placement. An auto inset is always weaker;
       otherwise the containing block's logical end edge is weaker. */
    bool sizing_x = s->width.kind == LK_STRETCH || s->min_width.kind == LK_STRETCH || s->max_width.kind == LK_STRETCH;
    bool sizing_y = s->height.kind == LK_STRETCH || s->min_height.kind == LK_STRETCH || s->max_height.kind == LK_STRETCH;
    /* Both auto insets use the static-position edge. In normal flow this
       starts at the parent's content edge, inside the abs CB padding box;
       grid and flex already provide their context-specific static rectangles. */
    const Box *static_parent = a->parent;
    while (static_parent && layout_percentage_wrapper(static_parent)) static_parent = static_parent->parent;
    if (!area && sizing_x && lA && rA && (!static_parent || (static_parent->fmt != FMT_FLEX && static_parent->fmt != FMT_GRID))) {
        if (cb->st && static_axis_reverse(cb->st, true)) {
            float edge = static_parent && static_parent->fmt != FMT_GRID ?
                static_parent->x + static_parent->w - static_parent->b[1] - static_parent->p[1] : px + pw;
            r = px + pw - edge; rA = false;
        } else { l = a->sx - px; lA = false; }
    }
    if (!area && sizing_y && tA && bA && (!static_parent || (static_parent->fmt != FMT_FLEX && static_parent->fmt != FMT_GRID))) {
        if (cb->st && static_axis_reverse(cb->st, false)) {
            float edge = static_parent && static_parent->fmt != FMT_GRID ?
                static_parent->y + static_parent->h - static_parent->b[2] - static_parent->p[2] : py + ph;
            bt = py + ph - edge; bA = false;
        } else { t = a->sy - py; tA = false; }
    }
    float remainder_x = pw - (lA && !sizing_x ? 0 : l) - (rA && !sizing_x ? 0 : r);
    float remainder_y = ph - (tA && !sizing_y ? 0 : t) - (bA && !sizing_y ? 0 : bt);
    bool weak_left = lA != rA ? lA : cb->st && static_axis_reverse(cb->st, true);
    bool weak_top = tA != bA ? tA : cb->st && static_axis_reverse(cb->st, false);
    if (sizing_x && remainder_x < 0) {
        if (weak_left) l = (lA ? 0 : l) + remainder_x;
        else r = (rA ? 0 : r) + remainder_x;
    }
    if (sizing_y && remainder_y < 0) {
        if (weak_top) t = (tA ? 0 : t) + remainder_y;
        else bt = (bA ? 0 : bt) + remainder_y;
    }
    g_imcb[0] = pw - (lA ? (sizing_x ? l : 0) : l) - (rA ? (sizing_x ? r : 0) : r);
    g_imcb[1] = ph - (tA ? (sizing_y ? t : 0) : t) - (bA ? (sizing_y ? bt : 0) : bt);
    bool mlA = s->margin[FI(3)].kind == LK_AUTO, mrA = s->margin[FI(1)].kind == LK_AUTO, mtA = s->margin[FI(0)].kind == LK_AUTO, mbA = s->margin[FI(2)].kind == LK_AUTO;
#undef FI
    int mode = SZ_SHRINK; float fw = 0, fh = -1;
    bool wdef = len_def(s->width, pw) || s->width.kind == LK_STRETCH;
    if (!wdef && !lA && !rA && s->width.kind == LK_AUTO && a->fmt != FMT_REPLACED) { mode = SZ_FORCED;   /* replaced: CSS2 10.3.8, insets don't size it */ fw = pw - l - r - a->m[1] - a->m[3]; }
    float avail = sizing_x ? g_imcb[0] : pw - (lA ? 0 : l) - (rA ? 0 : r);
    if (!len_def(s->height, ph) && !tA && !bA) {
        fh = ph - t - bt - a->m[0] - a->m[2];
        /* Preserve the old sizing path for non-stretch alignment and for
           vertical containing blocks, whose physical axis mapping is separate. */
        bool stretch_height = (!cb->st || !cb->st->writing_mode) &&
            (s->align_self == AL_AUTO || s->align_self == AL_NORMAL ||
             s->align_self == AL_STRETCH);
        if (stretch_height && s->height.kind == LK_AUTO && s->aspect_ratio <= 0 &&
            a->fmt != FMT_REPLACED && a->fmt != FMT_TABLE) {
            /* An inset-resolved auto height stays definite at its border-box
               floor. Apply min/max before positioning and preserve the
               existing intrinsic, ratio and table sizing paths. */
            float bp = vbp(a);
            fh = LMAX(bp, clamph(a, LMAX(bp, fh), ph, bp));
        }
    }
    if (a->fmt == FMT_REPLACED) fh = -1;
    float stretch_avail_h = sizing_y ? g_imcb[1] : ph - (tA ? 0 : t) - (bA ? 0 : bt);
    if (s->width.kind == LK_STRETCH) {
        mode = SZ_FORCED;
        fw = layout_stretch_border_size(a, cb, true, avail, true);
        fw = clampw_basis(L, a, fw, pw, hbp(a), avail, true);
    }
    if (s->height.kind == LK_STRETCH) {
        fh = layout_stretch_border_size(a, cb, false, stretch_avail_h, true);
        fh = clamph_basis(a, fh, ph, vbp(a), stretch_avail_h, true);
    }
    if (s->anchor_sz[0] || s->anchor_sz[1]) {   /* anchor-size() widths and heights are border-box sizes here */
        float v; bool bs = s->box_sizing == BOX_BORDER;
        if (s->anchor_sz[0] && anchor_size(a, 0, pw, &v)) { mode = SZ_FORCED; fw = bs ? v : v + a->b[1] + a->b[3] + a->p[1] + a->p[3]; }
        if (s->anchor_sz[1] && anchor_size(a, 1, ph, &v)) fh = bs ? v : v + a->b[0] + a->b[2] + a->p[0] + a->p[2];
    }
    /* Resolve own intrinsic edges against the percentage containing block.
       Ratio, table, replaced/orthogonal and anchor/try sizing retain their
       separate sizing paths. */
    bool ordinary_try = src[0] == 0 && src[1] == 1 && src[2] == 2 && src[3] == 3;
    if (mode == SZ_SHRINK && !wdef && s->width.kind == LK_AUTO &&
        (!cb->st || !cb->st->writing_mode) && !s->writing_mode && s->aspect_ratio <= 0 &&
        a->fmt != FMT_REPLACED && a->fmt != FMT_TABLE && !area && ordinary_try &&
        !s->pos_try && !s->pos_anchor && !s->pos_area &&
        !s->anchor_ins[0] && !s->anchor_ins[1] && !s->anchor_ins[2] && !s->anchor_ins[3] &&
        !s->anchor_sz[0] && !s->anchor_sz[1]) {
        mode = SZ_ABS_SHRINK;
        fw = avail;
    }
    /* Insets constrain shrink-to-fit space, not the percentage basis of the
       box's padding, margins and width limits. Other shrink callers retain
       their original sizing contract. */
    layout_box(L, a, 0, 0, mode == SZ_FORCED || mode == SZ_ABS_SHRINK || wdef || a->fmt == FMT_REPLACED ? pw : avail, ph, NULL, mode, fw, fh);
    /* Limits-only stretch needs the natural size first. Bound width before
       reading height: changing an automatic ratio-dependent width can change
       that height. A later height limit can in turn change automatic width. */
    bool stretch_width_limit = s->min_width.kind == LK_STRETCH || s->max_width.kind == LK_STRETCH;
    bool stretch_height_limit = s->min_height.kind == LK_STRETCH || s->max_height.kind == LK_STRETCH;
    if (stretch_width_limit) {
        float bounded = clampw_basis(L, a, a->w, pw, hbp(a), avail, true);
        if (fabsf(bounded - a->w) > .001f) {
            mode = SZ_FORCED; fw = bounded;
            layout_box(L, a, 0, 0, pw, ph, NULL, mode, fw, fh);
        }
    }
    if (stretch_height_limit) {
        float bounded = clamph_basis(a, a->h, ph, vbp(a), stretch_avail_h, true);
        if (fabsf(bounded - a->h) > .001f) {
            fh = bounded;
            layout_box(L, a, 0, 0, pw, ph, NULL, mode, fw, fh);
            if (stretch_width_limit) {
                bounded = clampw_basis(L, a, a->w, pw, hbp(a), avail, true);
                if (fabsf(bounded - a->w) > .001f)
                    layout_box(L, a, 0, 0, pw, ph, NULL, SZ_FORCED, bounded, fh);
            }
        }
    }
    float ow = a->w, oh = a->h;
    if (!lA && !rA && mlA && mrA) { float m = (pw - l - r - ow) / 2; a->m[1] = a->m[3] = LMAX(0, m); }
    if (!tA && !bA && mtA && mbA) {
        float inset_height = ph - t - bt;
        float m = (inset_height - oh) / 2;
        if (!cb->st || !cb->st->writing_mode) {
            /* Share remaining block-axis space after inset-area flooring.
               Unlike inline-axis auto margins, this split may be negative. */
            m = (LMAX(0, inset_height) - oh) / 2;
        } else m = LMAX(0, m); /* preserve the existing vertical-writing path */
        a->m[0] = a->m[2] = m;
    } else if (!tA && !bA && (mtA != mbA) &&
        (!cb->st || !cb->st->writing_mode) && !s->writing_mode &&
        (mode == SZ_FORCED || mode == SZ_ABS_SHRINK || wdef) &&
        a->fmt != FMT_REPLACED && a->fmt != FMT_TABLE && !area && ordinary_try &&
        !s->pos_try && !s->pos_anchor && !s->pos_area &&
        !s->anchor_ins[0] && !s->anchor_ins[1] && !s->anchor_ins[2] && !s->anchor_ins[3] &&
        !s->anchor_sz[0] && !s->anchor_sz[1]) {
        /* The admitted sizing paths preserve the original containing-block
           basis of the opposite used margin. Give the single auto margin
           all remaining block-axis space, including a negative remainder. */
        float remaining = LMAX(0, ph - t - bt) - oh;
        if (mtA) a->m[0] = remaining - a->m[2];
        else a->m[2] = remaining - a->m[0];
    }
    float x, y;
    float sx = a->sx, sy = a->sy;
    if (!area) flex_static_position(a, lA && rA, tA && bA, &sx, &sy);
    if (!lA) x = px + l + a->m[3]; else if (!rA) x = px + pw - r - a->m[1] - ow; else x = sx + a->m[3];
    if (!tA) y = py + t + a->m[0]; else if (!bA) y = py + ph - bt - a->m[2] - oh; else y = sy + a->m[0];
    /* Normal/stretch alignment keeps the stronger logical-start edge even
       when a floor overflows, or a maximum leaves room in, the rectangle. */
    bool vertical_cb = cb->st && cb->st->writing_mode;
    int ax = vertical_cb ? s->align_self : s->justify_self;
    int ay = vertical_cb ? s->justify_self : s->align_self;
    if (sizing_x && weak_left && !rA &&
        (ax == AL_AUTO || ax == AL_NORMAL || ax == AL_STRETCH)) x = px + pw - r - a->m[1] - ow;
    if (sizing_y && weak_top && !bA &&
        (ay == AL_AUTO || ay == AL_NORMAL || ay == AL_STRETCH)) y = py + ph - bt - a->m[2] - oh;
    if (area && af[0] >= 0) x += af[0] * (pw - l - r - ow - a->m[1] - a->m[3]);
    if (area && af[1] >= 0) y += af[1] * (ph - t - bt - oh - a->m[0] - a->m[2]);
    box_translate(a, x - a->x, y - a->y);
    return x - a->m[3] >= x0 - 0.5f && y - a->m[0] >= y0 - 0.5f && x + ow + a->m[1] <= x0 + w0 + 0.5f && y + oh + a->m[2] <= y0 + h0 + 0.5f;
}
/* position-try-fallbacks: when the box overflows, the first option that fits (a <position-area>, or flip-block /
   flip-inline); none fits: the base position */
static const int ID4[4] = { 0, 1, 2, 3 };
/* one position-try option: 1 fits, 0 doesn't, -1 invalid; g_imcb has its space */
static int try_option(Layout *L, Box *cb, Box *a, char *opt) {
        ComputedStyle *s = a->st;
        int lo, hi; bool ok = true;
        char first[48]; size_t fl = strcspn(opt, " "); if (fl >= sizeof first) return -1;
        memcpy(first, opt, fl); first[fl] = 0;
        if (area_kw(first, false, false, &lo, &hi) >= 0) return abs_place(L, cb, a, opt, ID4, ID4);
        /* tactics: flip-block swaps top/bottom, flip-inline left/right, flip-start top<->left and bottom<->right.
           Declared side src[k] lands on side k; an anchor side q becomes kwm[q]. */
        static const int TB[4] = { 2, 1, 0, 3 }, TI[4] = { 0, 3, 2, 1 }, TS[4] = { 3, 2, 1, 0 };
        const int *tac[8]; int nt = 0; char tmp[128], *rule = NULL; snprintf(tmp, sizeof tmp, "%s", opt);
        for (char *t = strtok(tmp, " "); t; t = strtok(NULL, " ")) {
            if ((!strcmp(t, "flip-block") || !strcmp(t, "flip-y")) && nt < 8) tac[nt++] = TB; else if ((!strcmp(t, "flip-inline") || !strcmp(t, "flip-x")) && nt < 8) tac[nt++] = TI;
            else if (!strcmp(t, "flip-start") && nt < 8) tac[nt++] = TS;
            else if (!strncmp(t, "--", 2) && !rule) rule = t; else ok = false;
        }
        if (!ok || (!nt && !rule)) return -1;
        int src[4], kwm[4]; bool tr = false;
        for (int k = 0; k < 4; k++) { int x = k; for (int j = 0; j < nt; j++) x = tac[j][x]; kwm[k] = x; x = k; for (int j = nt - 1; j >= 0; j--) x = tac[j][x]; src[k] = x; }
        for (int j = 0; j < nt; j++) if (tac[j] == TS) tr = !tr;
        ComputedStyle *ts = css_position_try_style(s, rule, a->node);
        if (!ts) return -1;
        if (tr) {   /* flip-start swaps the axes of sizes and self-alignment */
#define SWP(T, x, y) { T t_ = ts->x; ts->x = ts->y; ts->y = t_; }
            SWP(Length, width, height) SWP(Length, min_width, min_height) SWP(Length, max_width, max_height) SWP(uint8_t, justify_self, align_self) SWP(char *, anchor_sz[0], anchor_sz[1])
#undef SWP
        }
        /* an axis whose start side lands on an end side reverses its self-alignment */
        uint8_t *al[2] = { &ts->justify_self, &ts->align_self }; bool rev[2] = { kwm[tr ? 0 : 3] == 1 || kwm[tr ? 0 : 3] == 2, kwm[tr ? 3 : 0] == 1 || kwm[tr ? 3 : 0] == 2 };
        for (int k = 0; k < 2; k++) if (rev[k]) { uint8_t *v = al[k]; *v = *v == AL_START ? AL_END : *v == AL_END ? AL_START : *v == AL_FLEX_START ? AL_FLEX_END : *v == AL_FLEX_END ? AL_FLEX_START : *v; }
        a->st = ts;
        bool fit = abs_place(L, cb, a, ts->pos_area, src, kwm);
        a->st = s; css_position_try_free(ts);
        return fit;
}
/* position-try-fallbacks: when the base position overflows, the first option that fits, in list order or (with
   position-try-order) by most space; none fits: the base position */
static void abs_place_try(Layout *L, Box *cb, Box *a) {
    ComputedStyle *s = a->st;
    if (abs_place(L, cb, a, s->pos_area, ID4, ID4) || !s->pos_try) return;
    char list[512]; snprintf(list, sizeof list, "%s", s->pos_try);
    for (char *c0; (c0 = strstr(list, "/*")); ) { char *c1 = strstr(c0 + 2, "*/"); if (!c1) { *c0 = 0; break; } memmove(c0, c1 + 2, strlen(c1 + 2) + 1); }
    for (char *c = list; *c; c++) if (*c == '\n' || *c == '\t') *c = ' ';
    char *opts[16]; float key[16]; int n = 0;
    for (char *opt = list, *next; opt && n < 16; opt = next) {
        next = strchr(opt, ','); if (next) *next++ = 0;
        while (*opt == ' ') opt++;
        char *e = opt + strlen(opt); while (e > opt && e[-1] == ' ') *--e = 0;
        if (*opt) opts[n++] = opt;
    }
    if (s->pos_try_order) {   /* stable sort by the space each option leaves */
        for (int i = 0; i < n; i++) { char tmp[128]; snprintf(tmp, sizeof tmp, "%s", opts[i]); key[i] = try_option(L, cb, a, tmp) < 0 ? -1e30f : g_imcb[s->pos_try_order - 1]; }
        for (int i = 1; i < n; i++) for (int j = i; j > 0 && key[j] > key[j - 1]; j--) { float k = key[j]; key[j] = key[j - 1]; key[j - 1] = k; char *o = opts[j]; opts[j] = opts[j - 1]; opts[j - 1] = o; }
    }
    for (int i = 0; i < n; i++) { char tmp[128]; snprintf(tmp, sizeof tmp, "%s", opts[i]); if (try_option(L, cb, a, tmp) == 1) return; }
    abs_place(L, cb, a, s->pos_area, ID4, ID4);
}

static void layout_abs(Layout *L, Box *cb) {
    /* Laying out one positioned box can register more (e.g. a fixed box inside an absolute one), so repeat until no new ones appear. */
    Box *stop = NULL;
    while (cb->abs_head != stop) {
    Box *list = stop;
    for (Box *a = cb->abs_head; a != stop; ) { Box *n = a->abs_next; a->abs_next = list; list = a; a = n; } /* restore document order */
    cb->abs_head = list;
    for (Box *a = list; a != stop; a = a->abs_next) abs_place_try(L, cb, a);
    stop = list;
    }
}

/* ---------------- block flow ---------------- */
static Box *first_inflow_block(Box *b) { for (Box *c = b->first; c; c = c->next) { if (!in_flow(c)) continue; return c->kind == BX_BLOCK ? c : NULL; } return NULL; }
static Box *next_inflow_block(Box *b) { for (b = b->next; b; b = b->next) { if (!in_flow(b)) continue; return b->kind == BX_BLOCK ? b : NULL; } return NULL; }
static bool empty_margin_through(const Box *b, float cbh) {
    if (b->bfc || b->fmt == FMT_REPLACED || b->first || b->st->clear || vbp(b) != 0) return false;
    Length h = b->st->height, mn = b->st->min_height;
    bool zero_h = h.kind == LK_AUTO || (h.kind == LK_LEN && len_def(h, cbh) && res(h, cbh) == 0);
    bool zero_min = mn.kind == LK_AUTO || (mn.kind == LK_LEN && len_def(mn, cbh) && res(mn, cbh) <= 0);
    return zero_h && zero_min;
}
/* Resolve the preferred width again when float exclusion changes the
   stretch basis, while retaining the original percentage containing block. */
static void flow_stretch_width(Layout *L, Box *c, float left, float y,
                               float cw, float cbh, float available, bool changed_basis) {
    if (c->st->width.kind != LK_STRETCH && (changed_basis || fabsf(available - cw) > .001f)) {
        ComputedStyle *original = c->st, resolved = *original;
        float border_size = layout_stretch_border_size(c, layout_percentage_cb(c), true, available, true);
        Length limit = L_px(LMAX(0, border_size - (resolved.box_sizing == BOX_BORDER ? 0 : hbp(c))));
        if (resolved.min_width.kind == LK_STRETCH) resolved.min_width = limit;
        if (resolved.max_width.kind == LK_STRETCH) resolved.max_width = limit;
        if (resolved.width.kind == LK_AUTO && c->fmt != FMT_REPLACED &&
            !(resolved.aspect_ratio > 0 && resolved.height.kind == LK_STRETCH && cbh >= 0)) resolved.width = limit;
        c->st = &resolved; c->memo = false;
        layout_box(L, c, left, y, cw, cbh, NULL, SZ_FILL, 0, -1);
        c->st = original; c->memo = false; c->intr_ok = false;
    } else {
        float width = c->st->width.kind == LK_STRETCH ?
            layout_stretch_border_size(c, layout_percentage_cb(c), true, available, true) : c->w;
        width = clampw_basis(L, c, width, cw, hbp(c), available, true);
        if (fabsf(width - c->w) > .001f)
            layout_box(L, c, left, y, cw, cbh, NULL, SZ_FORCED, width, -1);
    }
}
/* A first child's margin cannot escape through an actual clearance or a
   BFC that must move below a preceding float. Measure only this uncommon
   boundary; ordinary zero-margin/block chains retain the single-pass path. */
static bool top_float_barrier(Layout *L, Box *c, Box *f, float cw, float ch,
                              float margin, FloatCtx *inherited, float x, float y) {
    bool independent = f->bfc || f->fmt == FMT_REPLACED;
    if (L->measure_only || margin == 0 || (!f->st->clear && !independent)) return false;
    bool local = false;
    for (Box *v = c->first; v && v != f; v = v->next) if (v->floated) { local = true; break; }
    if (!local && (!inherited || !inherited->n)) return false;
    FloatCtx probe = {0};
    if (inherited && inherited->n) {
        probe.n = probe.cap = inherited->n;
        probe.v = xmalloc(sizeof *probe.v * (size_t)probe.n);
        memcpy(probe.v, inherited->v, sizeof *probe.v * (size_t)probe.n);
    }
    float hypothetic = y + collapse2(c->m[0], margin);
    bool old_measure = L->measure_only; L->measure_only = true;
    for (Box *v = c->first; v && v != f; v = v->next) if (v->floated) {
        layout_box(L, v, 0, 0, cw, ch, NULL, SZ_SHRINK, 0, -1);
        fc_place(&probe, v, hypothetic, x, x + cw);
    }
    bool barrier = f->st->clear && fc_clear(&probe, f->st->clear) > hypothetic + .001f;
    if (!barrier && independent) {
        /* The preferred width is measured before stretch limits: the actual
           float-excluded minimum can be smaller than the containing block. */
        ComputedStyle *original = f->st, measured = *original;
        if (measured.min_width.kind == LK_STRETCH) measured.min_width = L_auto();
        if (measured.max_width.kind == LK_STRETCH) measured.max_width = (Length){.kind=LK_NONE};
        f->st = &measured; f->memo = false;
        layout_box(L, f, 0, 0, cw, ch, NULL, SZ_FILL, 0, -1);
        f->st = original; f->memo = false; f->intr_ok = false;
        float ml = original->margin[3].kind == LK_AUTO ? 0 : f->m[3];
        float mr = original->margin[1].kind == LK_AUTO ? 0 : f->m[1], left, right;
        fc_avail(&probe, hypothetic, LMAX(f->h, 1), x + ml, x + cw - mr, &left, &right);
        float available = right - left + ml + mr, width = f->w;
        if (original->width.kind == LK_STRETCH || (original->width.kind == LK_AUTO && f->fmt != FMT_REPLACED &&
            !(original->aspect_ratio > 0 && original->height.kind == LK_STRETCH && ch >= 0)))
            width = layout_stretch_border_size(f, layout_percentage_cb(f), true, available, true);
        width = clampw_basis(L, f, width, cw, hbp(f), available, true);
        barrier = width > right - left + .001f && (left != x + ml || right != x + cw - mr);
    }
    L->measure_only = old_measure;
    free(probe.v);
    return barrier;
}
static float top_chain(Layout *L, Box *c, float cbw, float cbh, FloatCtx *fc, float x, float y) {
    compute_mbp(c, cbw);
    float m = c->m[0];
    c->collapse_top = false;
    if ((c->fmt == FMT_FLOW) && !c->bfc && c->b[0] == 0 && c->p[0] == 0 && c->st->display != D_TABLE_CELL) {
        bool bs = c->st->box_sizing == BOX_BORDER;
        float cw = cbw - c->m[1] - c->m[3] - hbp(c), ch = -1;
        if (len_def(c->st->width, cbw)) cw = res(c->st->width, cbw) - (bs ? hbp(c) : 0);
        if (len_def(c->st->height, cbh)) ch = LMAX(0, res(c->st->height, cbh) - (bs ? vbp(c) : 0));
        else if (c->st->height.kind == LK_STRETCH && cbh >= 0) ch = LMAX(0, layout_stretch_border_size(c, layout_percentage_cb(c), false, cbh, true) - vbp(c));
        cw = LMAX(0, cw);
        for (Box *f = first_inflow_block(c); f && f->fmt != FMT_TABLE; f = next_inflow_block(f)) {
            float child_margin = top_chain(L, f, cw, ch, fc, x + c->m[3] + c->b[3] + c->p[3], y);
            if (top_float_barrier(L, c, f, cw, ch, child_margin, fc, x + c->m[3] + c->b[3] + c->p[3], y)) break;
            c->collapse_top = true; m = collapse2(m, child_margin);
            if (!empty_margin_through(f, ch)) break;
            m = collapse2(m, f->m[2]);
        }
    }
    return m;
}

float layout_flow(Layout *L, Box *b, float cx, float cy, float cw, float cbh, FloatCtx *fc) {
    float cursor = cy, pending = 0; bool first = true;
    b->has_baseline = false;
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs) { c->sx = cx; c->sy = cursor + (first && b->collapse_top ? 0 : pending); add_abs(L, c); continue; }
        if (c->floated) {
            layout_box(L, c, 0, 0, cw, cbh, NULL, SZ_SHRINK, 0, -1);
            if (fc) fc_place(fc, c, cursor + pending, cx, cx + cw); else box_translate(c, cx + c->m[3] - c->x, cursor + c->m[0] - c->y);
            continue;
        }
        float mt = top_chain(L, c, cw, cbh, fc, cx, cursor);
        float y;
        if (first && b->collapse_top) y = cursor; else y = cursor + collapse2(pending, mt);
        if (c->st->clear && fc) {
            float cy2 = fc_clear(fc, c->st->clear);
            /* Clearance is decided at the hypothetical position with clear:
               none, where a first top margin could have escaped its parent. */
            bool first_collapse = first && !b->bfc && b->b[0] == 0 && b->p[0] == 0 && b->st->display != D_TABLE_CELL;
            float hypothetical = first_collapse ? cursor : y;
            if (cy2 > hypothetical) y = cy2;
        }
        /* place so that the child's own margin is inside the collapsed amount */
        float own = c->collapse_top ? 0 : 0; (void)own;
        layout_box(L, c, cx + c->m[3], y, cw, cbh, c->bfc ? NULL : fc, SZ_FILL, 0, -1);
        bool sizing_width = c->st->width.kind == LK_STRETCH || c->st->min_width.kind == LK_STRETCH || c->st->max_width.kind == LK_STRETCH;
        bool sizing_height = c->st->height.kind == LK_STRETCH || c->st->min_height.kind == LK_STRETCH || c->st->max_height.kind == LK_STRETCH;
        if (fc && fc->n && (c->bfc || c->fmt == FMT_REPLACED) && c->kind == BX_BLOCK && !b->st->writing_mode &&
            (sizing_width || sizing_height)) {
            float ml = c->st->margin[3].kind == LK_AUTO ? 0 : c->m[3];
            float mr = c->st->margin[1].kind == LK_AUTO ? 0 : c->m[1];
            /* A first independent box is tested beside floats at its
               hypothetical collapsed border position. If it cannot fit,
               clearance inhibits the escaped margin and starts at the float
               bottom, not at the already-added margin. Explicit clear has
               resolved that position above and must retain it. */
            bool first_collapse = first && !c->st->clear && !b->bfc &&
                b->b[0] == 0 && b->p[0] == 0 && b->st->display != D_TABLE_CELL;
            float fy = first_collapse ? cursor : y;
            float left = cx + ml, right = cx + cw - mr, fitted = NAN;
            /* Each change is either another excluded float edge or a move to
               a later float bottom. Bound the work and never repeat a layout
               when the width basis has not changed. */
            for (int step = 0; step < 2 * fc->n + 4; step++) {
                fc_avail(fc, fy, LMAX(c->h, 1), cx + ml, cx + cw - mr, &left, &right);
                float available = right - left + ml + mr;
                if (sizing_width && (isnan(fitted) || fabsf(available - fitted) > .001f)) {
                    flow_stretch_width(L, c, left, fy, cw, cbh, available, !isnan(fitted));
                    fitted = available;
                    float next_left, next_right;
                    fc_avail(fc, fy, LMAX(c->h, 1), cx + ml, cx + cw - mr, &next_left, &next_right);
                    if (fabsf(next_left - left) > .001f || fabsf(next_right - right) > .001f) continue;
                }
                if (c->w <= right - left + .001f || (left == cx + ml && right == cx + cw - mr)) break;
                float next = INFINITY;
                for (int i = 0; i < fc->n; i++) {
                    const FRect *v = &fc->v[i];
                    if (v->y < fy + LMAX(c->h, 1) && v->y + v->h > fy + .001f) next = LMIN(next, v->y + v->h);
                }
                if (!isfinite(next)) break;
                fy = next;
            }
            if (fabsf(left - c->x) > .001f || fabsf(fy - c->y) > .001f) box_translate(c, left - c->x, fy - c->y);
        }
        if (c->collapse_top) { /* child's margins absorbed: nothing */ }
        bool empty = c->h == 0 && c->nlines == 0 && !c->first && !c->bfc && c->fmt != FMT_REPLACED;
        if (empty) { pending = collapse2(collapse2(first && b->collapse_top ? 0 : pending, mt), c->m[2]); cursor = y - (first && b->collapse_top ? 0 : collapse2(0, 0)); if (!(first && b->collapse_top)) cursor = y - collapse2(pending, 0) + collapse2(pending, 0) - 0; cursor = LMIN(cursor, y); continue; }
        cursor = c->y + c->h; pending = c->eff_mb;
        if (c->has_baseline) { b->has_baseline = true; b->baseline = c->baseline; }
        first = false;
    }
    bool cb_bottom = !b->bfc && b->b[2] == 0 && b->p[2] == 0 && b->st->height.kind == LK_AUTO && b->fmt == FMT_FLOW && b->st->display != D_TABLE_CELL;
    float end;
    if (cb_bottom && !first) { b->eff_mb = collapse2(b->m[2], pending); end = cursor; }
    else { b->eff_mb = b->m[2]; end = cursor + (first ? 0 : pending); }
    return end - cy;
}

/* ---------------- generic box layout ---------------- */
static float clampw_basis(Layout *L, Box *b, float w, float cbw, float bp, float stretch_available, bool definite) {
    const ComputedStyle *s = b->st;
    bool bs = s->box_sizing == BOX_BORDER;
    bool ilo = !s->writing_mode && intrinsic_width_keyword(s->min_width);
    bool ihi = !s->writing_mode && intrinsic_width_keyword(s->max_width);
    float mn = 0, mx = 0, available = cbw - b->m[1] - b->m[3];
    if (ilo || ihi) intrinsic_content_width(L, b, cbw, &mn, &mx);
    float stretch = (s->min_width.kind == LK_STRETCH || s->max_width.kind == LK_STRETCH) ?
        layout_stretch_border_size(b, layout_percentage_cb(b), true, stretch_available, definite) : -1;
    if (s->max_width.kind == LK_LEN && len_def(s->max_width, cbw)) { float v = res(s->max_width, cbw) + (bs ? 0 : bp); if (w > v) w = v; }
    else if (s->max_width.kind == LK_STRETCH && stretch >= 0) w = LMIN(w, stretch);
    else if (ihi) w = LMIN(w, intrinsic_width_size(s->max_width, mn, mx, available));
    if (s->min_width.kind == LK_LEN && len_def(s->min_width, cbw)) { float v = res(s->min_width, cbw) + (bs ? 0 : bp); if (w < v) w = v; }
    else if (s->min_width.kind == LK_STRETCH && stretch >= 0) w = LMAX(w, stretch);
    else if (ilo) w = LMAX(w, intrinsic_width_size(s->min_width, mn, mx, available));
    return s->width.kind == LK_STRETCH || s->min_width.kind == LK_STRETCH || s->max_width.kind == LK_STRETCH ? LMAX(bp, w) : w;
}
static float clampw(Layout *L, Box *b, float w, float cbw, float bp) {
    /* abs_place owns the distinct inset-modified stretch basis. */
    return clampw_basis(L, b, w, cbw, bp, cbw, !b->abs && cbw >= 0);
}
static float clamph_basis(Box *b, float h, float cbh, float bp, float available, bool definite) {
    const ComputedStyle *s = b->st;
    bool bs = s->box_sizing == BOX_BORDER;
    float stretch = (s->min_height.kind == LK_STRETCH || s->max_height.kind == LK_STRETCH) ?
        layout_stretch_border_size(b, layout_percentage_cb(b), false, available, definite) : -1;
    if (s->max_height.kind == LK_LEN && len_def(s->max_height, cbh)) { float v = res(s->max_height, cbh) + (bs ? 0 : bp); if (h > v) h = v; }
    else if (s->max_height.kind == LK_STRETCH && stretch >= 0) h = LMIN(h, stretch);
    if (s->min_height.kind == LK_LEN && len_def(s->min_height, cbh)) { float v = res(s->min_height, cbh) + (bs ? 0 : bp); if (h < v) h = v; }
    else if (s->min_height.kind == LK_STRETCH && stretch >= 0) h = LMAX(h, stretch);
    return s->height.kind == LK_STRETCH || s->min_height.kind == LK_STRETCH || s->max_height.kind == LK_STRETCH ? LMAX(bp, h) : h;
}
static float clamph(Box *b, float h, float cbh, float bp) {
    return clamph_basis(b, h, cbh, bp, cbh, !b->abs && cbh >= 0);
}

/* For none, the element still retains its own grid tracks while being
 * measured as empty. A shallow measurement box borrows immutable style and
 * has no descendants, registrations or memo state. Actual children are laid
 * out normally in the original box after its contained size is fixed. */
static unsigned select_display_rows(const Box *b) {
    const char *value = node_attr(b->node, "size");
    if (value) {
        while (is_ws((unsigned char)*value)) value++;
        if (*value != '-') {
            char *end; unsigned long rows = strtoul(value, &end, 10);
            if (end != value && rows && rows <= UINT32_MAX) return (unsigned)rows;
        }
    }
    return node_has_attr(b->node, "multiple") ? 4 : 1;
}
static float select_empty_height(const Box *b) {
    return select_display_rows(b) * style_line_height(b->st, style_font(b->st));
}
static float contained_auto_height(Layout *L, Box *b, float content_width) {
    if (b->st->contain_intrinsic[1].flags & CIS_LENGTH) return b->st->contain_intrinsic[1].length;
    /* A native empty select retains its own UI rows; option content is not
       consulted. Explicit intrinsic height, including zero, replaces it. */
    if (b->ctl == CTL_SELECT) return select_empty_height(b);
    if (b->fmt != FMT_GRID) return 0;
    Box empty = *b; empty.first = empty.last = empty.abs_head = NULL;
    empty.grid_used = NULL; empty.memo = empty.intr_ok = false;
    float height = layout_grid(L, &empty, 0, 0, content_width, -1);
    if (empty.grid_used) { free(empty.grid_used->size[0]); free(empty.grid_used->size[1]); free(empty.grid_used); }
    return LMAX(0, height);
}
static void layout_box_in(Layout *L, Box *b, float x, float y, float cbw, float cbh, FloatCtx *fc, int mode, float fw, float fh);
/* flex/grid measure a child and then lay it out again, often with identical inputs; nested containers made that exponential */
int g_lb_calls, g_lb_hits;
static bool pct_h(const ComputedStyle *s) { return s->height.kind == LK_STRETCH || s->min_height.kind == LK_STRETCH || s->max_height.kind == LK_STRETCH || s->height.pctu || s->height.pct || s->min_height.pctu || s->min_height.pct || s->max_height.pctu || s->max_height.pct; }
static bool hdep(const Box *b) {
    if (b->has_abs) return true;
    for (const Box *c = b->first; c; c = c->next) {
        if (c->st && pct_h(c->st)) return true;
        if (layout_percentage_wrapper(c) && hdep(c)) return true;
    }
    return false;
}
void layout_box(Layout *L, Box *b, float x, float y, float cbw, float cbh, FloatCtx *fc, int mode, float fw, float fh) {
    g_lb_calls++; b->dcalls++;
    /* a forced height equal to the natural one only matters to children that resolve percentages or insets against it */
    bool fh_same = b->mk[5] == fh || (b->mk[5] < 0 && fh >= 0 && fabsf(fh - b->mk[7]) < 0.01f && !hdep(b));
    if (!L->measure_only && !fc && b->memo && b->mmode == mode && b->mk[2] == cbw && b->mk[3] == cbh && b->mk[4] == fw && fh_same && b->w == b->mk[6] && b->h == b->mk[7]) {
        float dx = x + b->mk[0] - b->x, dy = y + b->mk[1] - b->y;
        box_translate(b, dx, dy); g_lb_hits++;
        static unsigned stamp; stamp++;
        for (int i = b->alo; i < b->ahi; i++) {
            Box *c = L->alog[i];
            if (c->astamp == stamp) continue;
            c->astamp = stamp;
            if (c->fixed || (c->cb && !within(c->cb, b))) { c->sx += dx; c->sy += dy; }   /* positioned by an outer box: tr left it alone */
            add_abs(L, c);
        }
        return;
    }
    int alo = L->nalog;
    layout_box_in(L, b, x, y, cbw, cbh, fc, mode, fw, fh);
    /* table parts get adjusted by their table after layout (row heights, vertical-align), so never reuse them */
    b->alo = alo; b->ahi = L->nalog;
    b->memo = !L->measure_only && !fc && b->fmt != FMT_TABLE && b->st->display != D_TABLE_CELL && (!b->parent || b->parent->fmt != FMT_TABLE);
    b->mmode = (int8_t)mode; b->mk[0] = b->x - x; b->mk[1] = b->y - y; b->mk[2] = cbw; b->mk[3] = cbh; b->mk[4] = fw; b->mk[5] = fh; b->mk[6] = b->w; b->mk[7] = b->h;
}
static void layout_box_in(Layout *L, Box *b, float x, float y, float cbw, float cbh, FloatCtx *fc, int mode, float fw, float fh) {
    const ComputedStyle *s = b->st;
    compute_mbp(b, cbw);
    b->abs_head = NULL;
    b->x = x; b->y = y;
    float hb = hbp(b), vb = vbp(b);
    bool bs = s->box_sizing == BOX_BORDER;
    bool is_cell = s->display == D_TABLE_CELL;
    if (mode == SZ_FORCED) b->w = fw;
    else if (b->fmt == FMT_REPLACED) { float w, h; replaced_size(L, b, cbw, cbh, -1, fh, &w, &h); b->w = w + hb; }
    else if (s->width.kind == LK_AUTO && s->height.kind == LK_STRETCH &&
             s->aspect_ratio > 0 && (fh >= 0 || cbh >= 0) && !is_cell) {
        /* A definite stretch height supplies the automatic width through the
           preferred aspect ratio, including a shrink-to-fit floated box. */
        float rh = fh >= 0 ? fh : layout_stretch_border_size(b, layout_percentage_cb(b), false, cbh, true);
        if (fh < 0) rh = clamph(b, rh, cbh, vb);
        float rw = (bs ? rh : LMAX(0, rh - vb)) * s->aspect_ratio + (bs ? 0 : hb);
        b->w = LMAX(hb, clampw(L, b, rw, cbw, hb));
    }
    else if (s->width.kind == LK_STRETCH && cbw >= 0 && !is_cell) {
        b->w = clampw(L, b, layout_stretch_border_size(b, layout_percentage_cb(b), true, cbw, true), cbw, hb);
    }
    else if (len_def(s->width, cbw) && !is_cell) { b->w = clampw(L, b, res(s->width, cbw) + (bs ? 0 : hb), cbw, hb); if (b->w < hb) b->w = hb; }
    else if (s->writing_mode && s->width.kind == LK_AUTO && layout_size_contained(b, true)) {
        /* In a vertical flow physical width is the content-sized block axis.
           Its contained intrinsic size does not fill the parent's width. */
        b->w = LMAX(hb, clampw(L, b, layout_contained_inner_size(b, true) + hb, cbw, hb));
    }
    else if (mode == SZ_FILL && b->fmt != FMT_TABLE && s->width.kind != LK_FIT_CONTENT && s->width.kind != LK_MAX_CONTENT && s->width.kind != LK_MIN_CONTENT) b->w = clampw(L, b, cbw - b->m[1] - b->m[3], cbw, hb);
    else {
        float mn, mx;
        if (mode == SZ_ABS_SHRINK) intrinsic_with_basis(L, b, cbw, &mn, &mx);
        else intrinsic(L, b, &mn, &mx);
        float avail = (mode == SZ_ABS_SHRINK ? fw : cbw) - b->m[1] - b->m[3];
        float w = s->width.kind == LK_MIN_CONTENT ? mn : s->width.kind == LK_MAX_CONTENT ? mx : LMIN(LMAX(mn, avail), mx);
        b->w = clampw(L, b, w, cbw, hb);
        /* A border-box maximum cannot make the content box negative. The
           resolved padding and border remain present even when they alone
           exceed max-width. */
        if (mode == SZ_ABS_SHRINK) b->w = LMAX(hb, b->w);
    }
    if (b->w < 0) b->w = 0;
    /* auto margins for in-flow blocks */
    if (mode == SZ_FILL && !b->abs && !b->floated && !(b->parent && b->parent->fmt == FMT_FLEX)) {   /* flex items: auto margins are resolved by flex alignment */
        bool mlA = s->margin[3].kind == LK_AUTO, mrA = s->margin[1].kind == LK_AUTO;
        float free = cbw - b->w - (mlA ? 0 : b->m[3]) - (mrA ? 0 : b->m[1]);
        if (free > 0 && (mlA || mrA)) {
            float old = b->m[3];
            if (mlA && mrA) b->m[3] = b->m[1] = free / 2; else if (mlA) b->m[3] = free; else b->m[1] = free;
            b->x += b->m[3] - old;
        }
    }
    float cx = b->x + b->b[3] + b->p[3], cy = b->y + b->b[0] + b->p[0], cw = LMAX(0, b->w - hb);
    float hdef = -1;
    if (fh >= 0) hdef = LMAX(0, fh - vb);
    else if (s->height.kind == LK_STRETCH && cbh >= 0)
        hdef = LMAX(0, layout_stretch_border_size(b, layout_percentage_cb(b), false, cbh, true) - vb);
    else if (len_def(s->height, cbh) && s->height.kind == LK_LEN) hdef = LMAX(0, res(s->height, cbh) - (bs ? vb : 0));
    else if (s->aspect_ratio > 0 && b->fmt != FMT_REPLACED && s->height.kind != LK_LEN) hdef = LMAX(0, (bs ? b->w : cw) / s->aspect_ratio - (bs ? vb : 0));
    else if (b->fmt != FMT_REPLACED && layout_size_contained(b, false))
        hdef = contained_auto_height(L, b, cw);
    /* children size against the used height: YouTube's theater player stretches in a flex row whose
       height:56.25vw is capped by max-height and then raised by min-height:480px */
    if (hdef >= 0 && fh < 0) hdef = LMAX(0, clamph(b, hdef + vb, cbh, vb) - vb);
    /* Quirks3.6/3.7 make the automatic html/body block size fill its
       available viewport/parent space. It remains a minimum, not a content
       clip, and supplies the definite stretch basis to its direct children. */
    float quirks_height = -1;
    bool quirks_candidate = L->doc->quirks && s->height.kind == LK_AUTO && fh < 0 &&
        b->node && b->node->ns == NS_HTML && (b->node == L->doc->html || b->node == L->doc->body);
    const char *quirks_wm = quirks_candidate && !s->writing_mode ? custom_get(s->gen_i, "writing-mode") : NULL;
    bool horizontal_quirk = !s->writing_mode && !(quirks_wm &&
        (str_ieq(quirks_wm, "sideways-lr") || str_ieq(quirks_wm, "sideways-rl")));
    if (quirks_candidate && horizontal_quirk) {
        if (b->node == L->doc->html)
            quirks_height = LMAX(vb, L->vh - b->m[0] - b->m[2]);
        else if (b->node == L->doc->body && b->node->parent == L->doc->html &&
                 !b->abs && !b->floated && b->kind == BX_BLOCK && cbh >= 0)
            quirks_height = LMAX(vb, cbh - b->m[0] - b->m[2]);
    }
    /* Anonymous and converted-inline wrappers retain the ancestor's basis. */
    float ch_for_kids = layout_percentage_wrapper(b) ? cbh : hdef >= 0 ? hdef :
        quirks_height >= 0 ? LMAX(0, quirks_height - vb) : -1;
    FloatCtx own = {0};
    FloatCtx *f = b->bfc || !fc ? &own : fc;
    float ch = 0;
    b->nlines = 0; b->nfrags = 0;
    switch (b->fmt) {
    case FMT_INLINE: L->inl_cbh = ch_for_kids; ch = layout_inline(L, b, cx, cy, cw, f); break;
    case FMT_FLOW: ch = layout_flow(L, b, cx, cy, cw, ch_for_kids, f); break;
    case FMT_FLEX: ch = layout_flex(L, b, cx, cy, cw, ch_for_kids); break;
    case FMT_GRID: ch = layout_grid(L, b, cx, cy, cw, ch_for_kids); break;
    case FMT_TABLE: ch = layout_table(L, b, cx, cy, cw, ch_for_kids); break;
    case FMT_REPLACED: { float w, h; replaced_size(L, b, cbw, cbh, mode == SZ_FORCED ? b->w : -1, fh, &w, &h); ch = h; break; }
    }
    if (b->ctl == CTL_SELECT) ch = LMAX(ch, select_empty_height(b));
    if (b->fmt != FMT_FLOW) b->eff_mb = b->m[2];
    if (b->bfc && f == &own) { float fb = fc_bottom(&own); if (fb > cy + ch) ch = fb - cy; }
    free(own.v);
    float content_h = ch;
    float h = (hdef >= 0 && b->fmt != FMT_REPLACED ? hdef : ch) + vb;
    if (fh < 0) h = clamph(b, h, cbh, vb);
    if (quirks_height >= 0) h = LMAX(h, quirks_height);
    b->h = LMAX(0, h);
    if (b->fmt == FMT_REPLACED || (b->ctl && b->nlines)) b->has_baseline = b->ctl && b->nlines, b->baseline = b->nlines ? b->lines[0].base : b->y + b->h;
    /* a button's content is centred vertically in a taller box, as in Chrome (google.com's
       icon buttons: an inline-block svg in a 48px display:block <button>); so is single-line
       control text */
    bool button = b->node && b->node->type == NODE_ELEMENT && b->node->tag == A_button && b->node->ns == NS_HTML && (b->fmt == FMT_FLOW || b->fmt == FMT_INLINE);
    bool listbox = b->ctl == CTL_SELECT && (node_has_attr(b->node, "multiple") || select_display_rows(b) > 1);
    if (button || (b->ctl && b->ctl != CTL_TEXTAREA && !listbox && b->nlines <= 1)) {
        float d = (b->h - vb - content_h) / 2;
        if (d > 0) { box_translate(b, 0, d); b->y -= d; if (b->ctl && !b->nlines) b->baseline += d; }   /* everything inside moves, the box stays */
    }
    b->scroll_w = cw; b->scroll_h = content_h;
    if (b->abs_head) layout_abs(L, b);
}

/* ---------------- relative offsets + extents ---------------- */
static void rel_offset(const Length in[4], float cbw, float cbh, float *dx, float *dy) {
    *dx = in[3].kind == LK_LEN ? res(in[3], cbw) : in[1].kind == LK_LEN ? -res(in[1], cbw) : 0;
    *dy = in[0].kind == LK_LEN ? res(in[0], cbh) : in[2].kind == LK_LEN ? -res(in[2], cbh) : 0;
}
/* Visible overflow propagates through constrained flex/block wrappers.
 * Looking only at immediate border boxes makes long conversations appear
 * to have no scroll range. Compute bottom-up once, stopping at clips. */
static void overflow(Box *b) {
    if (layout_contents_hidden(b)) { b->scroll_w = b->w; b->scroll_h = b->h; return; }
    float r = b->x + b->w, btm = b->y + b->h - b->p[2], bb = btm;
    for (Box *c = b->first; c; c = c->next) {
        if (c->fixed || (c->abs && c->cb && !box_within(c->cb, b))) continue;
        float w = c->w, h = c->h;
        if (!c->st || c->st->overflow_x == OV_VISIBLE) w = LMAX(w, c->scroll_w);
        if (!c->st || c->st->overflow_y == OV_VISIBLE) h = LMAX(h, c->scroll_h);
        /* the border box counts even when negative margins pull the margin box in; end padding follows margin boxes */
        r = LMAX(r, LMAX(c->x + w + c->m[1], c->x + w)); btm = LMAX(btm, c->y + h + c->m[2]); bb = LMAX(bb, c->y + h);
    }
    for (int i = 0; i < b->nfrags; i++) { r = LMAX(r, b->frags[i].x + b->frags[i].w); btm = LMAX(btm, b->frags[i].y + b->frags[i].h); }
    b->scroll_w = r - b->x; b->scroll_h = LMAX(btm - b->y + b->p[2], bb - b->y);
}
static void clamp_scroll(Box *b, float maxx, float maxy) {
    /* A skipped owner has no used scroll range; retain its backing offsets. */
    if (layout_contents_hidden(b)) return;
    b->node->scroll_x = LCLAMP(b->node->scroll_x, 0, maxx); b->node->scroll_y = box_scroll_from_end(b) ? LCLAMP(b->node->scroll_y, -maxy, 0) : LCLAMP(b->node->scroll_y, 0, maxy);
}
static void post(Layout *L, Box *b, float cbw, float cbh, float clipx, float clipy) {
    const ComputedStyle *s = b->st;
    /* text boxes share their element's style but are not positioned themselves */
    if (s && s->position == P_RELATIVE && b->node && b->kind != BX_TEXT) {
        float dx, dy; rel_offset(s->inset, cbw, cbh, &dx, &dy);
        box_translate(b, dx, dy);
    }
    float cw = b->w - hbp(b);
    for (Box *c = b->first; c; c = c->next) post(L, c, cw, b->h, clipx, clipy);
    overflow(b);
    if (b->scroller && b->node) {
        float maxx = LMAX(0, b->scroll_w - b->w), maxy = LMAX(0, b->scroll_h - b->h);
        clamp_scroll(b, maxx, maxy);
        /* abs children of a column-reverse scroller are placed against its scroll origin, which is the end */
        if (box_scroll_from_end(b) && maxy > 0) for (Box *x = b->abs_head; x; x = x->abs_next) box_translate(x, 0, maxy);
    }
}
static void extents(Layout *L, Box *b) {
    if (b->fixed) return;
    if (b->kind != BX_TEXT) { L->doc_w = LMAX(L->doc_w, b->x + b->w); L->doc_h = LMAX(L->doc_h, b->y + b->h); }
    if (b->scroller && b->node && b != L->root) return;
    if (layout_contents_hidden(b)) return;
    for (int i = 0; i < b->nfrags; i++) { L->doc_w = LMAX(L->doc_w, b->frags[i].x + b->frags[i].w); L->doc_h = LMAX(L->doc_h, b->frags[i].y + b->frags[i].h); }
    for (Box *c = b->first; c; c = c->next) extents(L, c);
}

static void finish_extents(Layout *L) {
    L->doc_w = L->vw; L->doc_h = 0;
    extents(L, L->root);
    L->doc_h = LMAX(L->doc_h, L->vh);
}

/* An inset-only style change moves a positioned box without resizing anything,
 * so translating the laid-out box gives the same geometry as layout_run. `old`
 * holds the insets the box was laid out with. Returns false when that cannot
 * be shown cheaply; the caller must then run layout. */
bool layout_move(Layout *L, Node *n, const Length old[4]) {
    Box *b = n->box;
    const ComputedStyle *s = b ? b->st : NULL;
    if (!L->root || !b || b->anon || b->kind == BX_TEXT || !s || s != n->style || !box_within(b, L->root)) return false;
    float dx = 0, dy = 0;
    if (s->position == P_RELATIVE) {
        Box *p = b->parent;
        float cbw = p ? p->w - hbp(p) : L->vw, cbh = p ? p->h : L->vh, ox, oy;
        rel_offset(old, cbw, cbh, &ox, &oy); rel_offset(s->inset, cbw, cbh, &dx, &dy);
        dx -= ox; dy -= oy;
    } else if (b->abs) {
        Box *cb = b->cb;
        if (!cb) return false;
        float pw = cb == L->root ? L->vw : cb->w - cb->b[1] - cb->b[3], ph = cb == L->root ? L->vh : cb->h - cb->b[0] - cb->b[2];
        const Length *in = s->inset;
        for (int axis = 0; axis < 2; axis++) {
            int lo = axis ? 0 : 3, hi = axis ? 2 : 1;   /* top/bottom, left/right */
            if (!memcmp(&old[lo], &in[lo], sizeof *in) && !memcmp(&old[hi], &in[hi], sizeof *in)) continue;
            bool loA = in[lo].kind != LK_LEN, hiA = in[hi].kind != LK_LEN;
            /* both sides set: insets size the box; a side switching to or from auto changes the anchor */
            if ((old[lo].kind != LK_LEN) != loA || (old[hi].kind != LK_LEN) != hiA || (!loA && !hiA)) return false;
            float ref = axis ? ph : pw;
            /* one horizontal inset narrows the shrink-to-fit width unless the width is definite */
            if (!axis && !len_def(s->width, pw)) return false;
            float d = !loA ? res(in[lo], ref) - res(old[lo], ref) : -(res(in[hi], ref) - res(old[hi], ref));
            if (axis) dy = d; else dx = d;
        }
    } else if (s->position != P_STATIC && s->position != P_STICKY) return false;
    /* static boxes ignore insets; sticky offsets are applied at paint from the current style */
    if (dx || dy) {
        box_translate(b, dx, dy);
        for (Box *a = b->parent; a; a = a->parent) {
            float ow = a->scroll_w, oh = a->scroll_h;
            overflow(a);
            if (a->scroll_w == ow && a->scroll_h == oh) break;
            if (a->scroller && a->node) {
                float maxy = LMAX(0, a->scroll_h - a->h);
                /* positioned children of a column-reverse scroller are placed against its end */
                if (box_scroll_from_end(a) && a->abs_head && maxy != LMAX(0, oh - a->h)) return false;
                clamp_scroll(a, LMAX(0, a->scroll_w - a->w), maxy);
            }
        }
        finish_extents(L);
    }
    atomic_fetch_add_explicit(&geometry_version, 1, memory_order_relaxed);
    doc_boxes_changed();
    return true;
}

static void free_box_data(Box *b) {
    for (Box *c = b->first; c; c = c->next) free_box_data(c);
    free(b->frags); free(b->lines); free(b->ir); text_release_shaped(b);
    if (b->grid_used) { free(b->grid_used->size[0]); free(b->grid_used->size[1]); free(b->grid_used); b->grid_used = NULL; }
    if (b->node && b->node->box == b) b->node->box = NULL;
}

Layout *layout_new(void) { Layout *L = xcalloc(1, sizeof *L); L->dpr = 2; return L; }
void layout_free(Layout *L) { if (L->root) free_box_data(L->root); arena_reset(&L->arena); free(L->alog); free(L); }

static void track_containers(Document *d, Box *b) {
    for (; b; b = b->next) {
        Node *n = b->node;
        if (n && !b->anon && b->st && b->st->container_type && n->style == b->st) {
            float w = b->w - b->b[1] - b->b[3] - b->p[1] - b->p[3], h = b->h - b->b[0] - b->b[2] - b->p[0] - b->p[2];
            if (fabsf(w - n->cq_w) > 0.5f || (b->st->container_type == 2 && fabsf(h - n->cq_h) > 0.5f)) { n->cq_w = w; n->cq_h = h; doc_mark_state_dirty(d, n, true); }
        }
        track_containers(d, b->first);
    }
}

/* LUMEN_DEBUG_LAYOUT=2: boxes whose layout_box count multiplies most relative to their parent */
typedef struct Hot { Box *b; float r; } Hot;
static void hot_walk(Box *b, Hot *h, int n) {
    for (Box *c = b->first; c; c = c->next) {
        float r = b->dcalls ? (float)c->dcalls / b->dcalls : 0;
        if (c->dcalls > 50 && r > 1.5f) { int k = n - 1; if (r * c->dcalls > h[k].r) { while (k > 0 && r * c->dcalls > h[k - 1].r) { h[k] = h[k - 1]; k--; } h[k] = (Hot){ c, r * c->dcalls }; } }
        hot_walk(c, h, n);
    }
}
static void hot_report(Layout *L) {
    Hot h[8] = {0}; hot_walk(L->root, h, 8);
    for (Box *b = h[0].b; b && b->parent; b = b->parent) {
        const Node *n = b->node; const char *cl = n && n->type == NODE_ELEMENT ? node_attr(n, "class") : NULL;
        if (b->dcalls > b->parent->dcalls) fprintf(stderr, "  chain x%.1f %d fmt=%d pfmt=%d disp=%d <%s class=\"%.70s\">\n", (float)b->dcalls / LMAX(1, b->parent->dcalls), b->dcalls, b->fmt, b->parent->fmt, b->st ? b->st->display : -1, n && n->type == NODE_ELEMENT ? n->tag : "#anon", cl ? cl : "");
    }
    for (int i = 0; i < 8 && h[i].b; i++) {
        Box *b = h[i].b; const Node *n = b->node; const char *cl = n && n->type == NODE_ELEMENT ? node_attr(n, "class") : NULL;
        fprintf(stderr, "  hot %d calls (parent %d) fmt=%d disp=%d <%s class=\"%.60s\">\n", b->dcalls, b->parent->dcalls, b->fmt, b->st ? b->st->display : -1, n && n->type == NODE_ELEMENT ? n->tag : "#anon", cl ? cl : "");
    }
}
void layout_run(Layout *L, Document *d, float vw, float vh) {
    double t0 = now_ms();
    if (L->root) free_box_data(L->root);
    arena_reset(&L->arena);
    L->doc = d; L->vw = vw; L->vh = vh; L->nboxes = 0; L->nalog = 0; L->measure_only = false;
    d->layout_vw = vw; d->layout_vh = vh;
    L->root = build_box_tree(L, d);
    Box *r = L->root;
    r->x = r->y = 0; r->w = vw;
    FloatCtx fc = {0};
    float h = layout_flow(L, r, 0, 0, vw, vh, &fc);
    free(fc.v);
    r->h = LMAX(vh, h);
    if (r->abs_head) layout_abs(L, r);
    post(L, r, vw, vh, 0, 0);
    finish_extents(L);
    track_containers(d, r);
    text_shape_cache_sweep();
    atomic_fetch_add_explicit(&geometry_version, 1, memory_order_relaxed);
    doc_boxes_changed();
    L->ms = now_ms() - t0;
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (d->debug_mutations) {
        unsigned hits, misses; size_t entries; text_shape_cache_stats(&hits, &misses, &entries);
        __android_log_print(ANDROID_LOG_INFO, "LumenLayout", "layout ms=%.2f boxes=%d calls=%d hits=%d shape_hits=%u shape_misses=%u shape_entries=%zu",
            L->ms, L->nboxes, g_lb_calls, g_lb_hits, hits, misses, entries);
        g_lb_calls = g_lb_hits = 0;
    }
#endif
    static int dbg = -1, nrun; static double tsum, tlast;
    if (dbg < 0) { const char *e = getenv("LUMEN_DEBUG_LAYOUT"); dbg = e ? atoi(e) > 0 ? atoi(e) : 1 : 0; }
    if (dbg > 1 && L->ms > 50) { fprintf(stderr, "lumen: slow layout %.0fms\n", L->ms); hot_report(L); }
    if (dbg) { nrun++; tsum += L->ms; double t = now_ms(); if (t - tlast > 1000) { fprintf(stderr, "lumen: layout %d runs/s %.1fms avg %d boxes %d calls %d hits\n", nrun, nrun ? tsum / nrun : 0, L->nboxes, nrun ? g_lb_calls / nrun : 0, nrun ? g_lb_hits / nrun : 0); g_lb_calls = g_lb_hits = 0; nrun = 0; tsum = 0; tlast = t; } }
}

/* Sticky translation is computed from current scroll offsets, without layout.
   Paint, hit testing and DOM rectangles must use the same translation.
   https://www.w3.org/TR/css-position-3/#stickypos-insets */
static float sticky_axis(float pos, float size, float margin0, float margin1,
                         float cb0, float cb1, float port0, float port_size,
                         Length start, Length end, bool reverse) {
    float lo = port0 + res(start, port_size), hi = port0 + port_size - res(end, port_size);
    if (hi - lo < size) { if (reverse) lo = hi - size; else hi = lo + size; }
    float shift = 0;
    if (end.kind == LK_LEN && pos + size > hi) shift = hi - pos - size;
    if (start.kind == LK_LEN && pos + shift < lo) shift = lo - pos;
    if (shift > 0) shift = LMIN(shift, LMAX(0, cb1 - pos - size - LMIN(margin1, cb1 - pos - size)));
    if (shift < 0) shift = LMAX(shift, LMIN(0, cb0 - pos + LMIN(margin0, pos - cb0)));
    return shift;
}
void box_sticky_offset(const Box *b, float scroll_x, float scroll_y, float *dx, float *dy) {
    *dx = *dy = 0;
    if (!b->st || b->st->position != P_STICKY || !b->node || !b->parent) return;
    const Box *port = NULL;
    float parent_dx = 0, parent_dy = 0;
    for (const Box *a = b->parent; a; a = a->parent) {
        if (a->scroller && a->node && a->st &&
            ((a->st->overflow_x != OV_VISIBLE && a->st->overflow_x != OV_CLIP) ||
             (a->st->overflow_y != OV_VISIBLE && a->st->overflow_y != OV_CLIP))) { port = a; break; }
        float ax, ay; box_sticky_offset(a, scroll_x, scroll_y, &ax, &ay);
        parent_dx += ax; parent_dy += ay;
        if (a->fixed) { scroll_x = scroll_y = 0; break; }
    }
    Document *d = b->node->doc;
    float px = port ? port->x + port->b[3] + box_scroll_left(port) : scroll_x;
    float py = port ? port->y + port->b[0] + box_scroll_y(port) : scroll_y;
    float pw = port ? port->w - port->b[3] - port->b[1] : d->layout_vw;
    float ph = port ? port->h - port->b[0] - port->b[2] : d->layout_vh;
    const Box *cb = b->parent;
    while (cb->kind == BX_INLINE && cb->parent) cb = cb->parent;
    float cx = cb->x + cb->b[3] + cb->p[3], cy = cb->y + cb->b[0] + cb->p[0];
    float cr = cb->x + cb->w - cb->b[1] - cb->p[1], cbottom = cb->y + cb->h - cb->b[2] - cb->p[2];
    if (cb == port) {
        cr += LMAX(0, cb->scroll_w - cb->w);
        cbottom += LMAX(0, cb->scroll_h - cb->h);
    }
    *dx = sticky_axis(b->x, b->w, b->m[3], b->m[1], cx, cr, px - parent_dx, pw, b->st->inset[3], b->st->inset[1], b->st->direction != 0);
    *dy = sticky_axis(b->y, b->h, b->m[0], b->m[2], cy, cbottom, py - parent_dy, ph, b->st->inset[0], b->st->inset[2], false);
}

/* ---------------- hit testing ---------------- */
/* Match the renderer's deferred layers: positioned descendants of ordinary
   wrappers participate in the same ordering as their positioned uncles. A
   wrapper alone must not put a painted overlay behind the content it covers. */
static int hit_layer(const Box *c) {
    if (!c->st || !c->node) return 2;
    bool item = c->parent && (c->parent->fmt == FMT_FLEX || c->parent->fmt == FMT_GRID);
    if (c->st->position == P_STATIC && !c->st->has_transform && c->st->opacity >= 1 &&
            !(item && !c->st->z_auto)) return 2;
    int z = c->st->z_auto ? 0 : c->st->z_index;
    return z > 0 ? 0 : z == 0 ? 1 : 3;
}
static Box *hit(Box *b, float x, float y, float ox, float oy, float vx, float vy, float sx, float sy, bool layers);
typedef struct HitLayer { Box *box; int z; bool negative; } HitLayer;
static void hit_layers(Box *b, float x, float y, float ox, float oy, float vx, float vy, float sx, float sy, HitLayer *best) {
    if (layout_contents_hidden(b)) return;
    float lx = x + ox, ly = y + oy;
    if (b->scroller && b->node && !(lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h)) return;
    float cox = ox, coy = oy;
    if (b->scroller && b->node) { cox += box_scroll_left(b); coy += box_scroll_y(b); }
    /* Reverse tree order resolves equal z-index without sorting or allocating.
       Each layer is tested at most once, and a lower layer cannot replace a hit. */
    for (Box *c = b->last; c; c = c->prev) {
        bool outside = c->abs && b->scroller && !box_within(c->cb, b);
        float cx = c->fixed ? vx : outside ? ox : cox, cy = c->fixed ? vy : outside ? oy : coy;
        if (hit_layer(c) == 2) { hit_layers(c, x, y, cx, cy, vx, vy, sx, sy, best); continue; }
        int z = c->st->z_auto ? 0 : c->st->z_index;
        if ((z < 0) != best->negative || (best->box && z <= best->z)) continue;
        Box *h = hit(c, x, y, cx, cy, vx, vy, sx, sy, true);
        if (h) { best->box = h; best->z = z; }
    }
}
static Box *hit(Box *b, float x, float y, float ox, float oy, float vx, float vy, float sx, float sy, bool layers) {
    bool hidden = b->st && b->st->visibility != VIS_VISIBLE;   /* not a target itself, but visible descendants are */
    float sticky_x, sticky_y; box_sticky_offset(b, sx, sy, &sticky_x, &sticky_y);
    ox -= sticky_x; oy -= sticky_y;
    if (b->st && b->st->has_transform) {   /* painted shifted by its translation (paint_stacking) */
        float tx = b->st->transform[4] + b->st->translate_pending[0].pct * b->w / 100;
        float ty = b->st->transform[5] + b->st->translate_pending[1].pct * b->h / 100;
        ox -= tx; oy -= ty; vx -= tx; vy -= ty;
    }
    float lx = x + ox, ly = y + oy;
    bool inside = lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h;
    if (b->scroller && b->node && !inside) return NULL;
    if (layout_contents_hidden(b))
        return inside && !hidden && b->st->pointer_events ? b : NULL;
    if (layers) {
        HitLayer best = {0};
        hit_layers(b, x, y, ox, oy, vx, vy, sx, sy, &best);
        if (best.box) return best.box;
    }
    float cox = ox, coy = oy;
    if (b->scroller && b->node) { cox += box_scroll_left(b); coy += box_scroll_y(b); }
    for (Box *c = b->last; c; c = c->prev) {
        if (hit_layer(c) != 2) continue;
        Box *h = hit(c, x, y, cox, coy, vx, vy, sx, sy, false);
        if (h) return h;
    }
    if (layers) {
        HitLayer best = {.negative = true};
        hit_layers(b, x, y, ox, oy, vx, vy, sx, sy, &best);
        if (best.box) return best.box;
    }
    if (b->kind == BX_INLINE) {
        for (int i = 0; i < b->nir; i++) { IRect *r = &b->ir[i]; if (lx >= r->x && lx < r->x + r->w && ly >= r->y && ly < r->y + r->h) return hidden || (b->st && !b->st->pointer_events) ? NULL : b; }
        return NULL;
    }
    for (int i = 0; i < b->nfrags; i++) { TextFrag *f = &b->frags[i]; if (f->box->kind == BX_TEXT && !(f->box->st && (f->box->st->visibility != VIS_VISIBLE || !f->box->st->pointer_events)) && lx >= f->x && lx < f->x + f->w && ly >= f->y && ly < f->y + f->h) { Box *t = f->box; return t->parent && t->parent->kind == BX_INLINE ? t->parent : b; } }
    if (inside && !hidden && b->node && b->st && b->st->pointer_events) return b;
    return NULL;
}
Box *layout_hit(Layout *layout, float x, float y, float scroll_x, float scroll_y) {
    if (!layout->root) return NULL;
    Box *box = hit(layout->root, x, y, 0, 0, -scroll_x, -scroll_y, scroll_x, scroll_y, true);
    while (box && !box->node) box = box->parent;
    return box;
}

void layout_dump(Box *b, int depth, int maxdepth) {
    if (depth > maxdepth) return;
    static const char *kinds[] = { "block", "inline", "text", "atomic", "br" };
    static const char *fmts[] = { "flow", "ifc", "flex", "grid", "table", "repl" };
    printf("%*s%s/%s %s%s%s [%.1f,%.1f %.1fx%.1f]", depth * 2, "", kinds[b->kind], fmts[b->fmt], b->node && b->node->type == NODE_ELEMENT ? b->node->tag : b->anon ? "(anon)" : "", b->abs ? " abs" : "", b->floated ? " float" : "", b->x, b->y, b->w, b->h);
    if (b->kind == BX_TEXT) printf(" \"%.40s\"", b->text);
    if (b->nlines) printf(" lines=%d", b->nlines);
    printf("\n");
    for (Box *c = b->first; c; c = c->next) layout_dump(c, depth + 1, maxdepth);
}
