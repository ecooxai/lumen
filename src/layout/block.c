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
    if (L->nalog == L->calog) { L->calog = L->calog ? L->calog * 2 : 64; L->alog = xrealloc(L->alog, sizeof *L->alog * (size_t)L->calog); }
    L->alog[L->nalog++] = c;
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
        /* Insets constrain shrink-to-fit space, not the percentage basis of a
           specified width (or its percentage padding, margins and limits). */
        layout_box(L, a, 0, 0, mode == SZ_FORCED || wdef ? pw : avail, ph, NULL, mode, fw, fh);
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

static void layout_box_in(Layout *L, Box *b, float x, float y, float cbw, float cbh, FloatCtx *fc, int mode, float fw, float fh);
/* flex/grid measure a child and then lay it out again, often with identical inputs; nested containers made that exponential */
int g_lb_calls, g_lb_hits;
static bool pct_h(const ComputedStyle *s) { return s->height.pctu || s->height.pct || s->min_height.pctu || s->min_height.pct || s->max_height.pctu || s->max_height.pct; }
static bool hdep(const Box *b) {
    if (b->has_abs) return true;
    for (const Box *c = b->first; c; c = c->next) if (c->st && pct_h(c->st)) return true;
    return false;
}
void layout_box(Layout *L, Box *b, float x, float y, float cbw, float cbh, FloatCtx *fc, int mode, float fw, float fh) {
    g_lb_calls++; b->dcalls++;
    /* a forced height equal to the natural one only matters to children that resolve percentages or insets against it */
    bool fh_same = b->mk[5] == fh || (b->mk[5] < 0 && fh >= 0 && fabsf(fh - b->mk[7]) < 0.01f && !hdep(b));
    if (!fc && b->memo && b->mmode == mode && b->mk[2] == cbw && b->mk[3] == cbh && b->mk[4] == fw && fh_same && b->w == b->mk[6] && b->h == b->mk[7]) {
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
    b->memo = !fc && b->fmt != FMT_TABLE && b->st->display != D_TABLE_CELL && (!b->parent || b->parent->fmt != FMT_TABLE);
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
static void rel_offset(const Length in[4], float cbw, float cbh, float *dx, float *dy) {
    *dx = in[3].kind == LK_LEN ? res(in[3], cbw) : in[1].kind == LK_LEN ? -res(in[1], cbw) : 0;
    *dy = in[0].kind == LK_LEN ? res(in[0], cbh) : in[2].kind == LK_LEN ? -res(in[2], cbh) : 0;
}
/* Visible overflow propagates through constrained flex/block wrappers.
 * Looking only at immediate border boxes makes long conversations appear
 * to have no scroll range. Compute bottom-up once, stopping at clips. */
static void overflow(Box *b) {
    float r = b->x + b->w, btm = b->y + b->h - b->p[2];
    for (Box *c = b->first; c; c = c->next) {
        if (c->fixed || (c->abs && c->cb && !box_within(c->cb, b))) continue;
        float w = c->w, h = c->h;
        if (!c->st || c->st->overflow_x == OV_VISIBLE) w = LMAX(w, c->scroll_w);
        if (!c->st || c->st->overflow_y == OV_VISIBLE) h = LMAX(h, c->scroll_h);
        r = LMAX(r, c->x + w + c->m[1]); btm = LMAX(btm, c->y + h + c->m[2]);
    }
    for (int i = 0; i < b->nfrags; i++) { r = LMAX(r, b->frags[i].x + b->frags[i].w); btm = LMAX(btm, b->frags[i].y + b->frags[i].h); }
    b->scroll_w = r - b->x; b->scroll_h = btm - b->y + b->p[2];
}
static void clamp_scroll(Box *b, float maxx, float maxy) {
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
    doc_visual_changed();
    return true;
}

static void free_box_data(Box *b) {
    for (Box *c = b->first; c; c = c->next) free_box_data(c);
    free(b->frags); free(b->lines); free(b->ir); text_release_shaped(b);
    if (b->node && b->node->box == b) b->node->box = NULL;
}

Layout *layout_new(void) { Layout *L = xcalloc(1, sizeof *L); L->dpr = 2; return L; }
void layout_free(Layout *L) { if (L->root) free_box_data(L->root); arena_reset(&L->arena); free(L->alog); free(L); }

static void track_containers(Document *d, Box *b) {
    for (; b; b = b->next) {
        Node *n = b->node;
        if (n && !b->anon && b->st && b->st->container_type && n->style == b->st) {
            float w = b->w - b->b[1] - b->b[3] - b->p[1] - b->p[3], h = b->h - b->b[0] - b->b[2] - b->p[0] - b->p[2];
            if (fabsf(w - n->cq_w) > 0.5f || (b->st->container_type == 2 && fabsf(h - n->cq_h) > 0.5f)) { n->cq_w = w; n->cq_h = h; doc_mark_style_dirty(d, n); }
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
    L->doc = d; L->vw = vw; L->vh = vh; L->nboxes = 0; L->nalog = 0;
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
    doc_visual_changed();
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
    float px = port ? port->x + port->b[3] + port->node->scroll_x : scroll_x;
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
    float lx = x + ox, ly = y + oy;
    if (b->scroller && b->node && !(lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h)) return;
    float cox = ox, coy = oy;
    if (b->scroller && b->node) { cox += b->node->scroll_x; coy += box_scroll_y(b); }
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
    if (layers) {
        HitLayer best = {0};
        hit_layers(b, x, y, ox, oy, vx, vy, sx, sy, &best);
        if (best.box) return best.box;
    }
    float cox = ox, coy = oy;
    if (b->scroller && b->node) { cox += b->node->scroll_x; coy += box_scroll_y(b); }
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
