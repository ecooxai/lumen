/* Block layout, floats, positioning, driver, hit testing */
#include "layout.h"
#include <math.h>

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
        if (c->abs && (c->fixed || (c->cb && !within(c->cb, root)))) continue;
        tr(root, c, dx, dy);
    }
}
void box_translate(Box *b, float dx, float dy) { if (dx || dy) tr(b, b, dx, dy); }

/* ---------------- absolute positioning ---------------- */
void add_abs(Layout *L, Box *c) {
    Box *cb = L->root;
    if (!c->fixed) for (Box *p = c->parent; p; p = p->parent) if (p->st && p->kind != BX_INLINE && (p->st->position != P_STATIC || p->st->has_transform) && p != c) { cb = p; break; }
    if (c->fixed) for (Box *p = c->parent; p; p = p->parent) if (p->st && p->st->has_transform && p->kind != BX_INLINE) { cb = p; break; }
    c->cb = cb; cb->has_abs = true;
    for (Box *x = cb->abs_head; x; x = x->abs_next) if (x == c) return;
    c->abs_next = cb->abs_head; cb->abs_head = c;
}

static void layout_abs(Layout *L, Box *cb) {
    /* Laying out one positioned box can register more (e.g. a fixed box inside an absolute one), so repeat until no new ones appear. */
    Box *stop = NULL;
    while (cb->abs_head != stop) {
    Box *list = stop;
    for (Box *a = cb->abs_head; a != stop; ) { Box *n = a->abs_next; a->abs_next = list; list = a; a = n; } /* restore document order */
    cb->abs_head = list;
    for (Box *a = list; a != stop; a = a->abs_next) {
        float px, py, pw, ph;
        if (cb == L->root || (a->fixed && cb == L->root)) { px = 0; py = 0; pw = L->vw; ph = L->vh; }
        else { px = cb->x + cb->b[3]; py = cb->y + cb->b[0]; pw = cb->w - cb->b[1] - cb->b[3]; ph = cb->h - cb->b[0] - cb->b[2]; }
        const ComputedStyle *s = a->st;
        compute_mbp(a, pw);
        bool lA = s->inset[3].kind != LK_LEN, rA = s->inset[1].kind != LK_LEN, tA = s->inset[0].kind != LK_LEN, bA = s->inset[2].kind != LK_LEN;
        float l = res(s->inset[3], pw), r = res(s->inset[1], pw), t = res(s->inset[0], ph), bt = res(s->inset[2], ph);
        bool mlA = s->margin[3].kind == LK_AUTO, mrA = s->margin[1].kind == LK_AUTO, mtA = s->margin[0].kind == LK_AUTO, mbA = s->margin[2].kind == LK_AUTO;
        int mode = SZ_SHRINK; float fw = 0, fh = -1;
        bool wdef = len_def(s->width, pw);
        if (!wdef && !lA && !rA && s->width.kind == LK_AUTO) { mode = SZ_FORCED; fw = pw - l - r - a->m[1] - a->m[3]; }
        float avail = pw - (lA ? 0 : l) - (rA ? 0 : r);
        if (!len_def(s->height, ph) && !tA && !bA) fh = ph - t - bt - a->m[0] - a->m[2];
        if (a->fmt == FMT_REPLACED) fh = -1;
        layout_box(L, a, 0, 0, mode == SZ_FORCED ? pw : avail, ph, NULL, mode, fw, fh);
        float ow = a->w, oh = a->h;
        if (!lA && !rA && mlA && mrA) { float m = (pw - l - r - ow) / 2; a->m[1] = a->m[3] = LMAX(0, m); }
        if (!tA && !bA && mtA && mbA) { float m = (ph - t - bt - oh) / 2; a->m[0] = a->m[2] = LMAX(0, m); }
        float x, y;
        float sx = a->sx, sy = a->sy;
        if (!lA) x = px + l + a->m[3]; else if (!rA) x = px + pw - r - a->m[1] - ow; else x = sx + a->m[3];
        if (!tA) y = py + t + a->m[0]; else if (!bA) y = py + ph - bt - a->m[2] - oh; else y = sy + a->m[0];
        box_translate(a, x - a->x, y - a->y);
    }
    stop = list;
    }
}

/* ---------------- block flow ---------------- */
static Box *first_inflow_block(Box *b) { for (Box *c = b->first; c; c = c->next) { if (!in_flow(c)) continue; return c->kind == BX_BLOCK ? c : NULL; } return NULL; }
static float top_chain(Box *c, float cbw) {
    compute_mbp(c, cbw);
    float m = c->m[0];
    c->collapse_top = false;
    if ((c->fmt == FMT_FLOW) && !c->bfc && c->b[0] == 0 && c->p[0] == 0 && c->st->display != D_TABLE_CELL) {
        Box *f = first_inflow_block(c);
        if (f && f->fmt != FMT_TABLE && !f->st->clear) { c->collapse_top = true; m = collapse2(m, top_chain(f, cbw)); }
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
        float mt = top_chain(c, cw);
        float y;
        if (first && b->collapse_top) y = cursor; else y = cursor + collapse2(pending, mt);
        if (c->st->clear && fc) { float cy2 = fc_clear(fc, c->st->clear); if (cy2 > y) y = cy2; }
        /* place so that the child's own margin is inside the collapsed amount */
        float own = c->collapse_top ? 0 : 0; (void)own;
        layout_box(L, c, cx + c->m[3], y, cw, cbh, c->bfc ? NULL : fc, SZ_FILL, 0, -1);
        if (c->collapse_top) { /* child's margins absorbed: nothing */ }
        bool empty = c->h == 0 && c->nlines == 0 && !c->first && c->fmt != FMT_REPLACED;
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
static float clampw(const ComputedStyle *s, float w, float cbw, float bp) {
    bool bs = s->box_sizing == BOX_BORDER;
    if (s->max_width.kind == LK_LEN && len_def(s->max_width, cbw)) { float v = res(s->max_width, cbw) + (bs ? 0 : bp); if (w > v) w = v; }
    if (s->min_width.kind == LK_LEN && len_def(s->min_width, cbw)) { float v = res(s->min_width, cbw) + (bs ? 0 : bp); if (w < v) w = v; }
    return w;
}
static float clamph(const ComputedStyle *s, float h, float cbh, float bp) {
    bool bs = s->box_sizing == BOX_BORDER;
    if (s->max_height.kind == LK_LEN && len_def(s->max_height, cbh)) { float v = res(s->max_height, cbh) + (bs ? 0 : bp); if (h > v) h = v; }
    if (s->min_height.kind == LK_LEN && len_def(s->min_height, cbh)) { float v = res(s->min_height, cbh) + (bs ? 0 : bp); if (h < v) h = v; }
    return h;
}

void layout_box(Layout *L, Box *b, float x, float y, float cbw, float cbh, FloatCtx *fc, int mode, float fw, float fh) {
    const ComputedStyle *s = b->st;
    compute_mbp(b, cbw);
    b->abs_head = NULL;
    b->x = x; b->y = y;
    float hb = hbp(b), vb = vbp(b);
    bool bs = s->box_sizing == BOX_BORDER;
    bool is_cell = s->display == D_TABLE_CELL;
    if (mode == SZ_FORCED) b->w = fw;
    else if (b->fmt == FMT_REPLACED) { float w, h; replaced_size(L, b, cbw, &w, &h); b->w = w + hb; }
    else if (len_def(s->width, cbw) && !is_cell) { b->w = clampw(s, res(s->width, cbw) + (bs ? 0 : hb), cbw, hb); if (b->w < hb) b->w = hb; }
    else if (mode == SZ_FILL && b->fmt != FMT_TABLE && s->width.kind != LK_FIT_CONTENT && s->width.kind != LK_MAX_CONTENT && s->width.kind != LK_MIN_CONTENT) b->w = clampw(s, cbw - b->m[1] - b->m[3], cbw, hb);
    else {
        float mn, mx; intrinsic(L, b, &mn, &mx);
        float avail = cbw - b->m[1] - b->m[3];
        float w = s->width.kind == LK_MIN_CONTENT ? mn : s->width.kind == LK_MAX_CONTENT ? mx : LMIN(LMAX(mn, avail), mx);
        b->w = clampw(s, w, cbw, hb);
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
    else if (len_def(s->height, cbh) && s->height.kind == LK_LEN) hdef = LMAX(0, res(s->height, cbh) - (bs ? vb : 0));
    else if (s->aspect_ratio > 0 && b->fmt != FMT_REPLACED && s->height.kind != LK_LEN) hdef = LMAX(0, (bs ? b->w : cw) / s->aspect_ratio - (bs ? vb : 0));
    float ch_for_kids = hdef >= 0 ? hdef : -1;
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
    case FMT_REPLACED: { float w, h; replaced_size(L, b, cbw, &w, &h); ch = h; if (len_def(s->height, cbh) && s->height.pct) ch = res(s->height, cbh) - (bs ? vb : 0); break; }
    }
    if (b->fmt != FMT_FLOW) b->eff_mb = b->m[2];
    if (b->bfc && f == &own) { float fb = fc_bottom(&own); if (fb > cy + ch) ch = fb - cy; }
    free(own.v);
    float content_h = ch;
    float h = (hdef >= 0 && b->fmt != FMT_REPLACED ? hdef : ch) + vb;
    if (fh < 0) h = clamph(s, h, cbh, vb);
    b->h = LMAX(0, h);
    if (b->fmt == FMT_REPLACED || b->ctl) b->has_baseline = b->ctl && b->nlines, b->baseline = b->nlines ? b->lines[0].base : b->y + b->h;
    /* vertical centering of single-line control text */
    if (b->ctl && b->ctl != CTL_TEXTAREA && b->nlines == 1) { float d = (b->h - vb - content_h) / 2; if (d > 0) { for (Box *c = b->first; c; c = c->next) box_translate(c, 0, 0); for (int i = 0; i < b->nfrags; i++) { b->frags[i].y += d; b->frags[i].base += d; } b->lines[0].y += d; b->lines[0].base += d; b->baseline += d; } }
    b->scroll_w = cw; b->scroll_h = content_h;
    if (b->abs_head) layout_abs(L, b);
}

/* ---------------- relative offsets + extents ---------------- */
static void post(Layout *L, Box *b, float cbw, float cbh, float clipx, float clipy) {
    const ComputedStyle *s = b->st;
    if (s && (s->position == P_RELATIVE || s->position == P_STICKY) && b->node) {
        float dx = 0, dy = 0;
        if (s->inset[3].kind == LK_LEN) dx = res(s->inset[3], cbw); else if (s->inset[1].kind == LK_LEN) dx = -res(s->inset[1], cbw);
        if (s->position == P_RELATIVE) { if (s->inset[0].kind == LK_LEN) dy = res(s->inset[0], cbh); else if (s->inset[2].kind == LK_LEN) dy = -res(s->inset[2], cbh); }
        box_translate(b, dx, dy);
    }
    float cw = b->w - hbp(b);
    for (Box *c = b->first; c; c = c->next) post(L, c, cw, b->h, clipx, clipy);
    if (b->scroller && b->node) {
        float r = b->x + b->w, btm = b->y + b->h;
        for (Box *c = b->first; c; c = c->next) { r = LMAX(r, c->x + c->w + c->m[1]); btm = LMAX(btm, c->y + c->h + c->m[2]); }
        for (int i = 0; i < b->nfrags; i++) { r = LMAX(r, b->frags[i].x + b->frags[i].w); btm = LMAX(btm, b->frags[i].y + b->frags[i].h); }
        b->scroll_w = r - b->x; b->scroll_h = btm - b->y + b->p[2];
        float maxx = LMAX(0, b->scroll_w - b->w), maxy = LMAX(0, b->scroll_h - b->h);
        b->node->scroll_x = LCLAMP(b->node->scroll_x, 0, maxx); b->node->scroll_y = box_scroll_from_end(b) ? LCLAMP(b->node->scroll_y, -maxy, 0) : LCLAMP(b->node->scroll_y, 0, maxy);
        /* abs children of a column-reverse scroller are placed against its scroll origin, which is the end */
        if (box_scroll_from_end(b) && maxy > 0) for (Box *x = b->abs_head; x; x = x->abs_next) box_translate(x, 0, maxy);
    }
}
static void extents(Layout *L, Box *b) {
    if (b->fixed) return;
    if (b->kind != BX_TEXT) { L->doc_w = LMAX(L->doc_w, b->x + b->w); L->doc_h = LMAX(L->doc_h, b->y + b->h); }
    if (b->scroller && b->node && b != L->root) return;
    for (int i = 0; i < b->nfrags; i++) { L->doc_w = LMAX(L->doc_w, b->frags[i].x + b->frags[i].w); L->doc_h = LMAX(L->doc_h, b->frags[i].y + b->frags[i].h); }
    for (Box *c = b->first; c; c = c->next) extents(L, c);
}

static void free_box_data(Box *b) {
    for (Box *c = b->first; c; c = c->next) free_box_data(c);
    free(b->frags); free(b->lines); free(b->ir); if (b->shaped) shaped_free(&b->sh);
    if (b->node && b->node->box == b) b->node->box = NULL;
}

Layout *layout_new(void) { Layout *L = xcalloc(1, sizeof *L); L->dpr = 2; return L; }
void layout_free(Layout *L) { if (L->root) free_box_data(L->root); arena_reset(&L->arena); free(L); }

void layout_run(Layout *L, Document *d, float vw, float vh) {
    double t0 = now_ms();
    if (L->root) free_box_data(L->root);
    arena_reset(&L->arena);
    L->doc = d; L->vw = vw; L->vh = vh; L->nboxes = 0;
    L->root = build_box_tree(L, d);
    Box *r = L->root;
    r->x = r->y = 0; r->w = vw;
    FloatCtx fc = {0};
    float h = layout_flow(L, r, 0, 0, vw, vh, &fc);
    free(fc.v);
    r->h = LMAX(vh, h);
    if (r->abs_head) layout_abs(L, r);
    post(L, r, vw, vh, 0, 0);
    L->doc_w = vw; L->doc_h = 0;
    extents(L, r);
    L->doc_h = LMAX(L->doc_h, vh);
    L->ms = now_ms() - t0;
}

/* ---------------- hit testing ---------------- */
/* Children are tested front to back: positioned z>0 (highest first), positioned z auto/0,
   in-flow, then positioned z<0; ties go to the later sibling. vx/vy map fixed boxes into viewport space. */
static int hit_layer(const Box *c) {
    if (!c->st || c->st->position == P_STATIC) return 2;
    int z = c->st->z_auto ? 0 : c->st->z_index;
    return z > 0 ? 0 : z == 0 ? 1 : 3;
}
static Box *hit(Box *b, float x, float y, float ox, float oy, float vx, float vy) {
    bool hidden = b->st && b->st->visibility != VIS_VISIBLE;   /* not a target itself, but visible descendants are */
    float lx = x + ox, ly = y + oy;
    bool inside = lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h;
    if (b->scroller && b->node && !inside) return NULL;
    float cox = ox, coy = oy;
    if (b->scroller && b->node) { cox += b->node->scroll_x; coy += box_scroll_y(b); }
    int done = INT32_MAX;   /* z>0 layer: repeatedly take the highest z below the last one taken */
    for (;;) {
        int best = INT32_MIN;
        for (Box *c = b->last; c; c = c->prev) if (hit_layer(c) == 0 && c->st->z_index < done && c->st->z_index > best) best = c->st->z_index;
        if (best == INT32_MIN) break;
        for (Box *c = b->last; c; c = c->prev) if (hit_layer(c) == 0 && c->st->z_index == best) {
            Box *h = hit(c, x, y, c->fixed ? vx : c->abs && b->scroller && !box_within(c->cb, b) ? ox : cox, c->fixed ? vy : c->abs && b->scroller && !box_within(c->cb, b) ? oy : coy, vx, vy);
            if (h) return h;
        }
        done = best;
    }
    for (int layer = 1; layer <= 3; layer++)
        for (Box *c = b->last; c; c = c->prev) {
            if (hit_layer(c) != layer) continue;
            Box *h = hit(c, x, y, c->fixed ? vx : c->abs && b->scroller && !box_within(c->cb, b) ? ox : cox, c->fixed ? vy : c->abs && b->scroller && !box_within(c->cb, b) ? oy : coy, vx, vy);
            if (h) return h;
        }
    if (b->kind == BX_INLINE) {
        for (int i = 0; i < b->nir; i++) { IRect *r = &b->ir[i]; if (lx >= r->x && lx < r->x + r->w && ly >= r->y && ly < r->y + r->h) return b; }
        return NULL;
    }
    for (int i = 0; i < b->nfrags; i++) { TextFrag *f = &b->frags[i]; if (f->box->kind == BX_TEXT && !(f->box->st && f->box->st->visibility != VIS_VISIBLE) && lx >= f->x && lx < f->x + f->w && ly >= f->y && ly < f->y + f->h) { Box *t = f->box; return t->parent && t->parent->kind == BX_INLINE ? t->parent : b; } }
    if (inside && !hidden && b->node && b->st && b->st->pointer_events) return b;
    return NULL;
}
Box *layout_hit(Layout *L, float x, float y) {
    if (!L->root) return NULL;
    Box *b = hit(L->root, x, y, 0, 0, -L->scroll_x, -L->scroll_y);
    while (b && !b->node) b = b->parent;   /* anonymous boxes (e.g. a flex item's text run) belong to their parent's node */
    return b;
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
