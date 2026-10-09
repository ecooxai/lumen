/* Native text selection: mouse drag / double / triple click / select-all, highlight and copy */
#include "paint.h"
#include <stdlib.h>
#include <string.h>

TextSel g_tsel;

typedef struct SelR { const Box *b; int s, e; } SelR;
static SelR *g_r; static int g_nr, g_cr;
static const Layout *g_rl;

typedef void (*WalkFn)(Box *b, float dx, float dy, void *ud);
static void walk(Box *b, float dx, float dy, float psy, WalkFn fn, void *ud) {
    if (b->fixed) dy += psy;
    if (b->scroller && b->node) { dx -= b->node->scroll_x; dy -= box_scroll_y(b); }
    fn(b, dx, dy, ud);
    for (Box *c = b->first; c; c = c->next) walk(c, dx, dy, psy, fn, ud);
}

static bool selectable(const Box *t) {
    return t->kind == BX_TEXT && t->st && !t->st->user_select && t->st->visibility == VIS_VISIBLE && t->text_len > 0;
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
    if (!L || !L->root) return false;
    Hit h = { x, y, scope, 0, 1e30f, NULL, 0 };
    walk(L->root, 0, 0, psy, hit_fn, &h);
    if (!h.bb && scope) { h.scope = NULL; walk(L->root, 0, 0, psy, hit_fn, &h); }
    if (!h.bb) return false;
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
        if (isa && isf) { int s = LMIN(g_tsel.ao, g_tsel.fo), e = LMAX(g_tsel.ao, g_tsel.fo); if (selectable(b)) push_r(b, s, LMIN(e, b->text_len)); o->phase = 2; return; }
        o->phase = 1; o->n[1] = isa ? g_tsel.fn : g_tsel.an; o->o[1] = isa ? g_tsel.fo : g_tsel.ao;
        if (selectable(b)) push_r(b, LMIN(isa ? g_tsel.ao : g_tsel.fo, b->text_len), b->text_len);
        return;
    }
    if (o->phase == 1) {
        if (b->node && b->node == o->n[1]) { if (selectable(b)) push_r(b, 0, LMIN(o->o[1], b->text_len)); o->phase = 2; }
        else if (selectable(b)) push_r(b, 0, b->text_len);
    }
}

void tsel_prepare(Layout *L) {
    g_nr = 0; g_rl = L;
    if (!g_tsel.on || !L || !L->root || g_tsel.L != L) return;
    Ord o; memset(&o, 0, sizeof o);
    walk(L->root, 0, 0, 0, ord_fn, &o);
    if (o.phase != 2) g_nr = 0;   /* an end vanished from the tree: draw nothing */
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
