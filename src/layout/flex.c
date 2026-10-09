/* Flexbox layout */
#include "layout.h"

typedef struct { Box *b; float base, hypo, mn, mx, main, cross, mm0, mm1; bool frozen, amA0, amA1; } FItem;
static int ord_cmp(const void *a, const void *b) { const FItem *x = a, *y = b; int d = x->b->st->order - y->b->st->order; return d ? d : (x->b->list_index - y->b->list_index); }

float layout_flex(Layout *L, Box *b, float cx, float cy, float cw, float chdef) {
    const ComputedStyle *s = b->st;
    bool row = s->flex_direction <= FD_ROW_REVERSE, rev = s->flex_direction == FD_ROW_REVERSE || s->flex_direction == FD_COLUMN_REVERSE;
    float mgap = row ? s->column_gap : s->row_gap, cgap = row ? s->row_gap : s->column_gap;
    if (row && s->column_gap_l.pct) mgap = res(s->column_gap_l, cw);
    VEC(FItem) it = {0}; int idx = 0;
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs) { c->sx = cx; c->sy = cy; add_abs(L, c); continue; }
        FItem f; memset(&f, 0, sizeof f); f.b = c; c->list_index = idx++; c->bfc = true;
        vec_push(it, f);
    }
    if (it.n > 1) qsort(it.v, (size_t)it.n, sizeof(FItem), ord_cmp);
    float mainsz = row ? cw : chdef;
    for (int i = 0; i < it.n; i++) {
        FItem *f = &it.v[i]; Box *c = f->b; const ComputedStyle *cs = c->st;
        compute_mbp(c, cw);
        f->amA0 = (row ? cs->margin[3] : cs->margin[0]).kind == LK_AUTO; f->amA1 = (row ? cs->margin[1] : cs->margin[2]).kind == LK_AUTO;
        f->mm0 = row ? c->m[3] : c->m[0]; f->mm1 = row ? c->m[1] : c->m[2];
        float bp = row ? hbp(c) : vbp(c); bool bs = cs->box_sizing == BOX_BORDER;
        Length fb = cs->flex_basis, sz = row ? cs->width : cs->height;
        float mn, mx; intrinsic(L, c, &mn, &mx);
        float content_h = -1;
        if (len_def(fb, mainsz) && !(fb.pctu && mainsz < 0)) f->base = res(fb, mainsz) + (bs ? 0 : bp);
        else if (len_def(sz, mainsz) && sz.kind == LK_LEN) f->base = res(sz, mainsz) + (bs ? 0 : bp);
        else if (row) f->base = mx;
        else {
            float w = (cs->width.kind == LK_LEN) ? 0 : cw - c->m[1] - c->m[3];
            layout_box(L, c, cx + c->m[3], cy, cw, -1, NULL, cs->width.kind == LK_LEN ? SZ_FILL : SZ_FORCED, w, -1);
            f->base = content_h = c->h;
        }
        Length mnl = row ? cs->min_width : cs->min_height, mxl = row ? cs->max_width : cs->max_height;
        if (mnl.kind == LK_LEN) f->mn = res(mnl, mainsz) + (bs ? 0 : bp);
        else if (row && cs->overflow_x == OV_VISIBLE) f->mn = LMIN(mn, len_def(sz, mainsz) && sz.kind == LK_LEN ? res(sz, mainsz) + (bs ? 0 : bp) : mn);
        else if (!row && cs->overflow_y == OV_VISIBLE) {
            if (content_h < 0) { float w = (cs->width.kind == LK_LEN) ? 0 : cw - c->m[1] - c->m[3]; layout_box(L, c, cx + c->m[3], cy, cw, -1, NULL, cs->width.kind == LK_LEN ? SZ_FILL : SZ_FORCED, w, -1); content_h = c->h; }
            f->mn = len_def(sz, mainsz) && sz.kind == LK_LEN ? LMIN(content_h, res(sz, mainsz) + (bs ? 0 : bp)) : content_h;
        }
        else f->mn = bp;
        f->mx = mxl.kind == LK_LEN && len_def(mxl, mainsz) ? res(mxl, mainsz) + (bs ? 0 : bp) : 1e30f;
        if (f->mx < f->mn) f->mx = f->mn;
        f->hypo = LCLAMP(f->base, f->mn, f->mx);
    }
    if (mainsz < 0) { float t = 0; for (int i = 0; i < it.n; i++) t += it.v[i].hypo + it.v[i].mm0 + it.v[i].mm1; mainsz = t + mgap * LMAX(0, it.n - 1); float mxh = s->max_height.kind == LK_LEN && !s->max_height.pct ? s->max_height.px : 1e30f; if (mainsz > mxh) mainsz = mxh; if (s->min_height.kind == LK_LEN && !s->min_height.pct && mainsz < s->min_height.px) mainsz = s->min_height.px; }
    /* lines */
    VEC(int) starts = {0}; vec_push(starts, 0);
    if (s->flex_wrap) { float acc = 0; int cnt = 0; for (int i = 0; i < it.n; i++) { float o = it.v[i].hypo + it.v[i].mm0 + it.v[i].mm1; if (cnt && acc + mgap + o > mainsz + 0.01f) { vec_push(starts, i); acc = o; cnt = 1; } else { acc += (cnt ? mgap : 0) + o; cnt++; } } }
    vec_push(starts, it.n);
    float crossdef = row ? chdef : cw;
    float cursor_cross = 0; float total_cross = 0;
    int nlines = starts.n - 1;
    for (int ln = 0; ln < nlines; ln++) {
        int a = starts.v[ln], z = starts.v[ln + 1], n = z - a;
        /* resolve flexible lengths */
        for (int i = a; i < z; i++) { it.v[i].frozen = false; it.v[i].main = it.v[i].hypo; }
        float used0 = 0; for (int i = a; i < z; i++) used0 += it.v[i].hypo + it.v[i].mm0 + it.v[i].mm1;
        used0 += mgap * LMAX(0, n - 1);
        bool grow = used0 < mainsz;
        for (int iter = 0; iter < 8; iter++) {
            float used = mgap * LMAX(0, n - 1), wsum = 0;
            for (int i = a; i < z; i++) { FItem *f = &it.v[i]; used += f->mm0 + f->mm1 + (f->frozen ? f->main : f->base); if (!f->frozen) wsum += grow ? f->b->st->flex_grow : f->b->st->flex_shrink * f->base; }
            float free = mainsz - used;
            if (wsum <= 0) { for (int i = a; i < z; i++) if (!it.v[i].frozen) it.v[i].main = LCLAMP(it.v[i].base, it.v[i].mn, it.v[i].mx); break; }
            if (grow && wsum < 1) free *= wsum;
            float viol = 0;
            for (int i = a; i < z; i++) { FItem *f = &it.v[i]; if (f->frozen) continue; float w = grow ? f->b->st->flex_grow : f->b->st->flex_shrink * f->base; float t = f->base + free * w / wsum; float cl = LCLAMP(t, f->mn, f->mx); viol += cl - t; f->main = cl; }
            if (viol == 0) break;
            for (int i = a; i < z; i++) { FItem *f = &it.v[i]; if (f->frozen) continue; float w = grow ? f->b->st->flex_grow : f->b->st->flex_shrink * f->base; float t = f->base + free * w / wsum; if ((viol > 0 && f->main > t) || (viol < 0 && f->main < t)) f->frozen = true; }
        }
        /* main positions */
        float used = mgap * LMAX(0, n - 1); int autos = 0;
        for (int i = a; i < z; i++) { used += it.v[i].main + it.v[i].mm0 + it.v[i].mm1; autos += it.v[i].amA0 + it.v[i].amA1; }
        float free = mainsz - used, start = 0, between = mgap;
        if (autos && free > 0) { for (int i = a; i < z; i++) { if (it.v[i].amA0) it.v[i].mm0 += free / autos; if (it.v[i].amA1) it.v[i].mm1 += free / autos; } free = 0; }
        int jc = s->justify_content;
        if (free > 0 || jc == AL_CENTER || jc == AL_FLEX_END || jc == AL_END) switch (jc) {
            case AL_FLEX_END: case AL_END: start = free; break;
            case AL_CENTER: start = free / 2; break;
            case AL_SPACE_BETWEEN: if (n > 1) between += free / (n - 1); break;
            case AL_SPACE_AROUND: start = free / n / 2; between += free / n; break;
            case AL_SPACE_EVENLY: start = free / (n + 1); between += free / (n + 1); break;
        }
        if (rev) { if (jc == AL_FLEX_END || jc == AL_END) start = 0; else if (jc == AL_NORMAL || jc == AL_FLEX_START || jc == AL_START) start = LMAX(0, free); }
        /* lay out items */
        float pos = start, linecross = 0;
        for (int k = 0; k < n; k++) {
            int i = rev ? z - 1 - k : a + k; FItem *f = &it.v[i]; Box *c = f->b; const ComputedStyle *cs = c->st;
            float mpos = pos + f->mm0;
            if (row) {
                int al = cs->align_self == AL_AUTO ? s->align_items : cs->align_self;
                float fh = -1;
                if ((al == AL_STRETCH || al == AL_NORMAL) && cs->height.kind == LK_AUTO && chdef >= 0 && nlines == 1 && cs->margin[0].kind != LK_AUTO && cs->margin[2].kind != LK_AUTO) fh = LCLAMP(chdef - c->m[0] - c->m[2], 0, 1e30f);
                layout_box(L, c, cx + mpos, cy + cursor_cross + c->m[0], cw, chdef, NULL, SZ_FORCED, f->main, fh);
                f->cross = c->h + c->m[0] + c->m[2];
            } else {
                int al = cs->align_self == AL_AUTO ? s->align_items : cs->align_self;
                bool stretch = (al == AL_STRETCH || al == AL_NORMAL) && cs->width.kind == LK_AUTO && cs->margin[1].kind != LK_AUTO && cs->margin[3].kind != LK_AUTO;
                float w = cw - c->m[1] - c->m[3];
                layout_box(L, c, cx + cursor_cross + c->m[3], cy + mpos, cw, chdef >= 0 ? chdef : -1, NULL, stretch ? SZ_FORCED : SZ_SHRINK, w, f->main);
                f->cross = c->w + c->m[1] + c->m[3];
            }
            linecross = LMAX(linecross, f->cross);
            pos += f->mm0 + f->main + f->mm1 + between;
        }
        if (nlines == 1 && crossdef >= 0) linecross = crossdef;
        /* cross alignment + stretch */
        for (int i = a; i < z; i++) {
            FItem *f = &it.v[i]; Box *c = f->b; const ComputedStyle *cs = c->st;
            int al = cs->align_self == AL_AUTO ? s->align_items : cs->align_self;
            float freec = linecross - f->cross, off = 0;
            bool mA0 = (row ? cs->margin[0] : cs->margin[3]).kind == LK_AUTO, mA1 = (row ? cs->margin[2] : cs->margin[1]).kind == LK_AUTO;
            if (mA0 && mA1) off = freec / 2; else if (mA0) off = freec; else if (mA1) off = 0;
            else if (al == AL_CENTER) off = freec / 2;
            else if (al == AL_FLEX_END || al == AL_END) off = freec;
            else if ((al == AL_STRETCH || al == AL_NORMAL) && freec > 0.01f) {
                if (row && cs->height.kind == LK_AUTO) {
                    float nh = linecross - c->m[0] - c->m[2];
                    if (c->fmt == FMT_FLEX || c->fmt == FMT_GRID || c->has_abs || c->fmt == FMT_TABLE) layout_box(L, c, c->x, c->y, cw, chdef, NULL, SZ_FORCED, c->w, nh);
                    else c->h = nh;
                }
            }
            if (off) box_translate(c, row ? 0 : off, row ? off : 0);
        }
        cursor_cross += linecross + cgap; total_cross += linecross + (ln ? cgap : 0);
    }
    b->has_baseline = false;
    for (int i = 0; i < it.n; i++) if (it.v[i].b->has_baseline) { b->has_baseline = true; b->baseline = it.v[i].b->baseline; break; }
    vec_free(it); vec_free(starts);
    return row ? total_cross : mainsz;
}
