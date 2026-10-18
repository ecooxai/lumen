/* Native text selection: mouse drag / double / triple click / select-all, highlight and copy */
#include "paint.h"
#include <stdlib.h>
#include <string.h>

TextSel g_tsel;
TselCaret g_tsel_caret[2];
bool g_tsel_fwd = true;

typedef struct SelR { const Box *b; int s, e; } SelR;
static SelR *g_r; static int g_nr, g_cr;

typedef void (*WalkFn)(Box *b, float dx, float dy, void *ud);
static void walk(Box *b, float dx, float dy, float psy, WalkFn fn, void *ud) {
    if (b->fixed) dy += psy;
    if (b->scroller && b->node) { dx -= b->node->scroll_x; dy -= box_scroll_y(b); }
    fn(b, dx, dy, ud);
    for (Box *c = b->first; c; c = c->next) walk(c, dx, dy, psy, fn, ud);
}

static bool editable(const Node *n) {
    for (; n; n = n->parent) {
        const char *ce = n->type == NODE_ELEMENT ? node_attr((Node *)n, "contenteditable") : NULL;
        if (ce) return strcmp(ce, "false") != 0;
    }
    return false;
}

static bool selectable(const Box *t) {
    return t->kind == BX_TEXT && t->st && t->st->visibility == VIS_VISIBLE && t->text_len > 0 &&
           (!t->st->user_select || editable(t->node));
}

typedef struct Hit { float x, y; Box *scope; int depth_in; float best; Box *bb; int off; } Hit;
static bool inside(const Box *b, const Box *scope) { for (; b; b = b->parent) if (b == scope) return true; return false; }
static void hit_fn(Box *b, float dx, float dy, void *ud) {
    Hit *h = ud;
    if (!b->nfrags || (h->scope && !inside(b, h->scope))) return;
    for (int i = 0; i < b->nfrags; i++) {
        TextFrag *f = &b->frags[i]; Box *t = f->box;
        if (!selectable(t) || !t->node || f->g1 <= f->g0) continue;
        float fx = f->x + dx, fy = f->y + dy, d;
        float hx = h->x < fx ? fx - h->x : h->x > fx + f->w ? h->x - fx - f->w : 0;
        if (h->y >= fy && h->y < fy + f->h) d = hx;
        else d = 1e5f + (h->y < fy ? fy - h->y : h->y - fy - f->h) * 100 + hx;
        if (d >= h->best) continue;
        h->best = d; h->bb = t;
        int off = t->sh.g[f->g0].cluster;
        float x0 = t->sh.g[f->g0].x;
        if (h->x >= fx + f->w) off = f->g1 < t->sh.n ? (int)t->sh.g[f->g1].cluster : t->text_len;
        else for (int g = f->g0; g < f->g1; g++) {
            float gx = fx + t->sh.g[g].x - x0, mid = gx + t->sh.g[g].adv / 2;
            if (h->x < mid) { off = t->sh.g[g].cluster; break; }
            off = g + 1 < t->sh.n ? (int)t->sh.g[g + 1].cluster : t->text_len;
        }
        h->off = off;
    }
}

bool tsel_point(Layout *L, float x, float y, float psy, Box *scope, Node **n, int *off) {
    return tsel_point_near(L, x, y, psy, scope, 1e30f, n, off);
}

bool tsel_point_near(Layout *L, float x, float y, float psy, Box *scope, float maxd, Node **n, int *off) {
    if (!L || !L->root) return false;
    Hit h = { x, y, scope, 0, 1e30f, NULL, 0 };
    walk(L->root, 0, 0, psy, hit_fn, &h);
    if (!h.bb && scope) { h.scope = NULL; walk(L->root, 0, 0, psy, hit_fn, &h); }
    if (!h.bb || h.best > maxd) return false;
    *n = h.bb->node; *off = h.off;
    return true;
}

typedef struct Ord { const Box *t[2]; Node *n[2]; int o[2]; int phase; } Ord;
static void push_r(const Box *b, int s, int e) {
    if (e <= s) return;
    if (g_nr == g_cr) { g_cr = g_cr ? g_cr * 2 : 64; g_r = realloc(g_r, sizeof *g_r * (size_t)g_cr); }
    g_r[g_nr++] = (SelR){ b, s, e };
}
static void ord_fn(Box *b, float dx, float dy, void *ud) {
    (void)dx; (void)dy; Ord *o = ud;
    if (b->kind != BX_TEXT || o->phase > 1) return;
    bool isa = b->node && b->node == g_tsel.an, isf = b->node && b->node == g_tsel.fn;
    if (o->phase == 0 && (isa || isf)) {
        if (isa && isf) { int s = LMIN(g_tsel.ao, g_tsel.fo), e = LMAX(g_tsel.ao, g_tsel.fo); if (selectable(b)) push_r(b, s, LMIN(e, b->text_len)); o->phase = 2; g_tsel_fwd = g_tsel.ao <= g_tsel.fo; return; }
        o->phase = 1; g_tsel_fwd = isa; o->n[1] = isa ? g_tsel.fn : g_tsel.an; o->o[1] = isa ? g_tsel.fo : g_tsel.ao;
        if (selectable(b)) push_r(b, LMIN(isa ? g_tsel.ao : g_tsel.fo, b->text_len), b->text_len);
        return;
    }
    if (o->phase == 1) {
        if (b->node && b->node == o->n[1]) { if (selectable(b)) push_r(b, 0, LMIN(o->o[1], b->text_len)); o->phase = 2; }
        else if (selectable(b)) push_r(b, 0, b->text_len);
    }
}

void tsel_prepare(Layout *L) {
    g_nr = 0;
    if (!g_tsel.on || !L || !L->root || g_tsel.L != L) return;
    Ord o; memset(&o, 0, sizeof o);
    walk(L->root, 0, 0, 0, ord_fn, &o);
    if (o.phase != 2) g_nr = 0;   /* an end vanished from the tree: draw nothing */
}


int tsel_edge(const Box *t) {
    return !g_nr ? 0 : (g_r[0].b == t) | (g_r[g_nr - 1].b == t) << 1;
}

bool tsel_range(const Box *t, int *s, int *e) {
    for (int i = 0; i < g_nr; i++) if (g_r[i].b == t) { *s = g_r[i].s; *e = g_r[i].e; return true; }
    return false;
}

static const Box *blk_of(const Box *b) { while (b && (b->kind == BX_INLINE || b->kind == BX_TEXT)) b = b->parent; return b; }

char *tsel_text(Layout *L) {
    tsel_prepare(L);
    SB o; sb_init(&o);
    const Box *pb = NULL;
    for (int i = 0; i < g_nr; i++) {
        const SelR *r = &g_r[i]; const Box *bk = blk_of(r->b);
        if (o.n && bk != pb) sb_putc(&o, '\n');
        pb = bk;
        sb_put(&o, r->b->text + r->s, (size_t)(r->e - r->s));
    }
    if (!o.s) sb_puts(&o, "");
    return o.s;
}

typedef struct Find { Node *n; Box *b; const Box *blk; Box *first, *last; } Find;
static void find_fn(Box *b, float dx, float dy, void *ud) {
    (void)dx; (void)dy; Find *f = ud;
    if (b->kind != BX_TEXT) return;
    if (f->n && b->node == f->n && !f->b) f->b = b;
    if ((!f->blk || inside(b, f->blk)) && selectable(b) && b->node) { if (!f->first) f->first = b; f->last = b; }
}
static bool wordch(unsigned char c) { return c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '\''; }

void tsel_word(Layout *L, Node *n, int off) {
    Find f = { n, NULL, NULL, NULL, NULL };
    walk(L->root, 0, 0, 0, find_fn, &f);
    if (!f.b) return;
    const char *s = f.b->text; int len = f.b->text_len, a = LMIN(off, len), e = a;
    if (a < len && wordch((unsigned char)s[a])) { while (a > 0 && wordch((unsigned char)s[a - 1])) a--; while (e < len && wordch((unsigned char)s[e])) e++; }
    else if (a > 0 && wordch((unsigned char)s[a - 1])) { while (a > 0 && wordch((unsigned char)s[a - 1])) a--; }
    else if (a < len) { e = a + 1; while (e < len && (s[e] & 0xC0) == 0x80) e++; }
    g_tsel = (TextSel){ true, L, n, n, a, e };
}

void tsel_block(Layout *L, Node *n) {
    Find f = { n, NULL, NULL, NULL, NULL };
    walk(L->root, 0, 0, 0, find_fn, &f);
    if (!f.b) return;
    Find g = { NULL, NULL, blk_of(f.b), NULL, NULL };
    walk(L->root, 0, 0, 0, find_fn, &g);
    if (!g.first) return;
    g_tsel = (TextSel){ true, L, g.first->node, g.last->node, 0, g.last->text_len };
}

void tsel_all(Layout *L) {
    if (!L || !L->root) return;
    Find g = { NULL, NULL, NULL, NULL, NULL };
    walk(L->root, 0, 0, 0, find_fn, &g);
    if (!g.first) return;
    g_tsel = (TextSel){ true, L, g.first->node, g.last->node, 0, g.last->text_len };
}

bool node_within(const Node *n, const Node *anc) { for (; n; n = n->parent) if (n == anc) return true; return false; }

bool tsel_dom_point(Node *n, int o, bool end, Node **tn, int *to) {
    if (!n) return false;
    if (n->type == NODE_TEXT) { *tn = n; *to = o; return true; }
    Node *c = n->first, *before = NULL, *after = NULL;
    for (int i = 0; c && i < o; i++) c = c->next;
    for (Node *x = n->first; x && x != c; x = node_next_in_tree(x, n)) if (x->type == NODE_TEXT) before = x;
    for (Node *x = c; x; x = node_next_in_tree(x, n)) if (x->type == NODE_TEXT) { after = x; break; }
    if (after && (!end || !before)) { *tn = after; *to = 0; return true; }
    if (before) { *tn = before; *to = (int)before->text_len; return true; }
    return false;
}

/* ---- <input>/<textarea>: the focused control's selection lives in its Document as codepoints of the value;
   the shown text has one codepoint per value codepoint (a bullet each for passwords) ---- */
int u8_cp_byte(const char *s, int len, int cp) {
    int i = 0;
    for (; i < len && cp > 0; cp--) { i++; while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80) i++; }
    return i;
}
int u8_byte_cp(const char *s, int byte) {
    int n = 0;
    for (int i = 0; i < byte && s[i]; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
    return n;
}

const Box *ctl_text_box(const Box *cb) {
    if (!cb || cb->placeholder || (cb->ctl != CTL_TEXT && cb->ctl != CTL_TEXTAREA)) return NULL;
    for (const Box *c = cb->first; c; c = c->next) if (c->kind == BX_TEXT && !c->node) return c;
    return NULL;
}

static void ctl_off(const Box *cb, float psy, float *dx, float *dy) {
    *dx = *dy = 0;
    for (const Box *a = cb; a; a = a->parent) {
        if (a->fixed) *dy += psy;
        if (a->scroller && a->node) { *dx -= a->node->scroll_x; *dy -= box_scroll_y(a); }
    }
}

static int ctl_hit_at(const Box *cb, float x, float y, float dx, float dy) {
    const Box *t = ctl_text_box(cb);
    if (!t) return 0;
    float best = 1e30f; int off = t->text_len;
    for (int i = 0; i < cb->nfrags; i++) {
        const TextFrag *f = &cb->frags[i];
        if (f->box != t || f->g1 <= f->g0) continue;
        float fx = f->x + dx, fy = f->y + dy;
        float d = (y < fy ? fy - y : y >= fy + f->h ? y - fy - f->h : 0) * 1000 + (x < fx ? fx - x : x > fx + f->w ? x - fx - f->w : 0);
        if (d >= best) continue;
        best = d;
        float x0 = t->sh.g[f->g0].x;
        off = f->g1 < t->sh.n ? (int)t->sh.g[f->g1].cluster : t->text_len;
        for (int g = f->g0; g < f->g1; g++) {
            float gx = fx + t->sh.g[g].x - x0;
            if (x < gx + t->sh.g[g].adv / 2) { off = t->sh.g[g].cluster; break; }
        }
        if (off > 0 && off == (f->g1 < t->sh.n ? (int)t->sh.g[f->g1].cluster : -1) && t->text[off - 1] == '\n') off--;   /* stay on this line */
    }
    return u8_byte_cp(t->text, off);
}
int ctl_hit(const Box *cb, float x, float y, float psy) {
    float dx, dy; ctl_off(cb, psy, &dx, &dy);
    return ctl_hit_at(cb, x, y, dx, dy);
}

bool ctl_caret(const Box *cb, int cp, float *x, float *y, float *h) {
    const Box *t = ctl_text_box(cb);
    if (!t) return false;
    int off = u8_cp_byte(t->text, t->text_len, cp);
    const TextFrag *last = NULL;
    for (int i = 0; i < cb->nfrags; i++) {
        const TextFrag *f = &cb->frags[i];
        if (f->box != t || f->g1 <= f->g0) continue;
        int lo = (int)t->sh.g[f->g0].cluster, hi = f->g1 < t->sh.n ? (int)t->sh.g[f->g1].cluster : t->text_len;
        if (off < lo) break;
        last = f;
        if (off >= hi) continue;
        float x0 = t->sh.g[f->g0].x;
        *x = f->x + f->w;
        for (int g = f->g0; g < f->g1; g++) if ((int)t->sh.g[g].cluster >= off) { *x = f->x + t->sh.g[g].x - x0; break; }
        *y = f->y; *h = f->h;
        return true;
    }
    if (!last) return false;
    *x = last->x + last->w; *y = last->y; *h = last->h;
    return true;
}

bool ctl_range(const Box *t, int *s, int *e) {
    const Box *cb = t->node ? NULL : t->parent;
    const Node *n = cb ? cb->node : NULL;
    if (!n || !n->doc || n != n->doc->csel_n || !(n->flags & NF_FOCUS) || ctl_text_box(cb) != t) return false;
    int a = n->doc->csel[0], b = n->doc->csel[1];
    if (a == b) return false;
    *s = u8_cp_byte(t->text, t->text_len, LMIN(a, b)); *e = u8_cp_byte(t->text, t->text_len, LMAX(a, b));
    return *e > *s;
}
/* Up/Down from codepoint cp: the nearest caret on the line above or below, else the start or end of the text */
int ctl_vmove(const Box *cb, int cp, int dir) {
    const Box *t = ctl_text_box(cb);
    float x, y, h;
    if (!t || !ctl_caret(cb, cp, &x, &y, &h)) return cp;
    int to = ctl_hit_at(cb, x, dir < 0 ? y - h / 2 : y + h * 1.5f, 0, 0);
    float x2, y2, h2;
    if (ctl_caret(cb, to, &x2, &y2, &h2) && fabsf(y2 - y) < 0.5f) return dir < 0 ? 0 : u8_byte_cp(t->text, t->text_len);
    return to;
}
