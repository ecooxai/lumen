/* Grid and table layout */
#include "layout.h"

static float track_fixed(const GridTrack *t, float W) { if (t->size.kind == LK_LEN && !t->fr) return res(t->size, W); if (t->min.kind == LK_LEN && t->min.px > 0 && !t->fr) return res(t->min, W); return -1; }

float layout_grid(Layout *L, Box *b, float cx, float cy, float cw, float chdef) {
    const ComputedStyle *s = b->st;
    float cg = s->column_gap_l.pct ? res(s->column_gap_l, cw) : s->column_gap, rg = s->row_gap;
    int nc = s->grid_ncols; GridTrack *tracks = s->grid_cols;
    GridTrack one = { L_auto(), 1, L_auto() };
    VEC(GridTrack) cols = {0};
    for (int i = 0; i < nc; i++) {
        GridTrack t = tracks[i];
        if (t.size.kind == LK_FIT_CONTENT && t.size.px > 0) { /* repeat(auto-fill, minmax(px, 1fr)) marker */
            float mn = t.size.px; int cnt = LMAX(1, (int)((cw + cg) / (mn + cg)));
            GridTrack g = { L_auto(), t.fr ? t.fr : 1, L_px(mn) };
            for (int k = 0; k < cnt; k++) vec_push(cols, g);
            continue;
        }
        vec_push(cols, t);
    }
    if (!cols.n) vec_push(cols, one);
    nc = cols.n;
    /* items + placement */
    typedef struct { Box *b; int r, c, rs, cs; } GI;
    VEC(GI) items = {0};
    int ar = 0, ac = 0;
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs) { c->sx = cx; c->sy = cy; add_abs(L, c); continue; }
        c->bfc = true;
        const ComputedStyle *is = c->st;
        int cs = is->grid_col_span == -1 ? nc : LMAX(1, LMIN(is->grid_col_span, nc));
        int rs = LMAX(1, is->grid_row_span);
        GI g = { c, 0, 0, rs, cs };
        if (is->grid_col_start > 0) { g.c = LMIN(is->grid_col_start - 1, nc - 1); if (is->grid_col_span == -1) g.cs = nc - g.c; g.r = is->grid_row_start > 0 ? is->grid_row_start - 1 : ar; if (is->grid_row_start <= 0 && g.c < ac) g.r = ++ar; ac = g.c + g.cs; }
        else if (is->grid_row_start > 0) { g.r = is->grid_row_start - 1; g.c = 0; }
        else { if (ac + cs > nc) { ar++; ac = 0; } g.r = ar; g.c = ac; ac += cs; if (ac >= nc) { ar++; ac = 0; } }
        if (g.c + g.cs > nc) g.cs = nc - g.c;
        vec_push(items, g);
    }
    /* column sizes */
    float *cwid = xcalloc((size_t)nc, sizeof(float));
    float fixed = cg * (nc - 1), frsum = 0; int nauto = 0;
    for (int i = 0; i < nc; i++) {
        float f = track_fixed(&cols.v[i], cw);
        if (cols.v[i].fr > 0) { frsum += cols.v[i].fr; cwid[i] = cols.v[i].min.kind == LK_LEN ? res(cols.v[i].min, cw) : 0; fixed += cwid[i]; continue; }
        if (f >= 0) { cwid[i] = f; fixed += f; continue; }
        float mx = 0;
        for (int k = 0; k < items.n; k++) if (items.v[k].c == i && items.v[k].cs == 1) { float a, z; intrinsic_outer(L, items.v[k].b, &a, &z); mx = LMAX(mx, cols.v[i].size.kind == LK_MIN_CONTENT ? a : z); }
        cwid[i] = mx; fixed += mx; nauto++;
    }
    float left = cw - fixed;
    if (frsum > 0 && left > 0) { for (int i = 0; i < nc; i++) if (cols.v[i].fr > 0) cwid[i] += left * cols.v[i].fr / LMAX(1, frsum); }
    else if (nauto && left > 0 && (s->justify_content == AL_NORMAL || s->justify_content == AL_STRETCH)) { for (int i = 0; i < nc; i++) if (cols.v[i].fr == 0 && track_fixed(&cols.v[i], cw) < 0) cwid[i] += left / nauto; }
    else if (left < 0 && nauto) { /* shrink autos towards min-content */ float need = -left; for (int i = 0; i < nc && need > 0; i++) if (cols.v[i].fr == 0 && track_fixed(&cols.v[i], cw) < 0) { float take = LMIN(need / nauto, cwid[i]); cwid[i] -= take; } }
    float *cx0 = xcalloc((size_t)nc + 1, sizeof(float));
    for (int i = 0; i < nc; i++) cx0[i + 1] = cx0[i] + cwid[i] + (i < nc - 1 ? cg : 0);
    int nr = 0; for (int k = 0; k < items.n; k++) nr = LMAX(nr, items.v[k].r + items.v[k].rs);
    nr = LMAX(nr, s->grid_nrows);
    float *rh = xcalloc((size_t)nr + 1, sizeof(float));
    for (int i = 0; i < s->grid_nrows && i < nr; i++) { float f = track_fixed(&s->grid_rows[i], chdef); if (f >= 0) rh[i] = f; }
    float auto_row = s->grid_auto_rows.kind == LK_LEN ? res(s->grid_auto_rows, chdef) : -1;
    /* layout items to find row heights */
    for (int k = 0; k < items.n; k++) {
        GI *g = &items.v[k]; Box *c = g->b;
        float w = cx0[g->c + g->cs] - cx0[g->c] - (g->c + g->cs < nc ? cg : 0);
        if (g->c + g->cs == nc) w = cx0[nc] - cx0[g->c];
        compute_mbp(c, w);
        int ja = c->st->justify_self ? c->st->justify_self : s->justify_items;
        bool stretch = (ja == AL_NORMAL || ja == AL_STRETCH || ja == AL_AUTO) && c->st->width.kind == LK_AUTO;
        layout_box(L, c, cx + cx0[g->c] + c->m[3], cy, w, -1, NULL, stretch ? SZ_FORCED : SZ_SHRINK, w - c->m[1] - c->m[3], -1);
        if (!stretch && (ja == AL_CENTER || ja == AL_END || ja == AL_FLEX_END)) { float fr = w - c->w - c->m[1] - c->m[3]; box_translate(c, ja == AL_CENTER ? fr / 2 : fr, 0); }
        if (g->rs == 1) { float need = c->h + c->m[0] + c->m[2]; bool fixedrow = g->r < s->grid_nrows && track_fixed(&s->grid_rows[g->r], chdef) >= 0; if (!fixedrow) rh[g->r] = LMAX(rh[g->r], LMAX(need, auto_row)); }
    }
    float *ry = xcalloc((size_t)nr + 1, sizeof(float));
    for (int i = 0; i < nr; i++) ry[i + 1] = ry[i] + rh[i] + (i < nr - 1 ? rg : 0);
    for (int k = 0; k < items.n; k++) {
        GI *g = &items.v[k]; Box *c = g->b;
        float top = ry[g->r], hh = ry[g->r + g->rs] - top - (g->r + g->rs < nr ? rg : 0);
        int al = c->st->align_self == AL_AUTO ? s->align_items : c->st->align_self;
        float off = 0, fr = hh - c->h - c->m[0] - c->m[2];
        if (al == AL_CENTER) off = fr / 2; else if (al == AL_END || al == AL_FLEX_END) off = fr;
        else if ((al == AL_NORMAL || al == AL_STRETCH) && c->st->height.kind == LK_AUTO && fr > 0.01f) { if (c->fmt == FMT_FLEX || c->fmt == FMT_GRID || c->has_abs) layout_box(L, c, c->x, c->y, c->w, hh, NULL, SZ_FORCED, c->w, hh - c->m[0] - c->m[2]); else c->h = hh - c->m[0] - c->m[2]; }
        box_translate(c, 0, cy + top + c->m[0] + off - c->y);
    }
    float total = ry[nr];
    free(cwid); free(cx0); free(rh); free(ry); vec_free(items); vec_free(cols);
    return total;
}

/* ---------------- tables ---------------- */
typedef struct { Box *cell; int r, c, cs, rs; } TCell;
typedef struct { VEC(TCell) cells; VEC(Box *) rows; VEC(Box *) captions; int ncols; } TGrid;
static bool is_group(const Box *b) { int d = b->st->display; return d == D_TABLE_ROW_GROUP || d == D_TABLE_HEADER_GROUP || d == D_TABLE_FOOTER_GROUP; }
static void add_row(TGrid *g, Box *row, int *occ_cols, int nrow) {
    int col = 0;
    for (Box *c = row->first; c; c = c->next) {
        if (!in_flow(c)) continue;
        int cs = 1, rs = 1;
        if (c->node) { const char *a = node_attr(c->node, "colspan"); if (a) cs = LCLAMP(atoi(a), 1, 1000); a = node_attr(c->node, "rowspan"); if (a) rs = LCLAMP(atoi(a), 1, 65534); }
        while (col < 1024 && occ_cols[col] > nrow) col++;
        TCell t = { c, nrow, col, cs, rs };
        for (int k = col; k < col + cs && k < 1024; k++) occ_cols[k] = nrow + rs;
        vec_push(g->cells, t);
        col += cs; if (col > g->ncols) g->ncols = col;
    }
}
static void build_grid(Box *t, TGrid *g) {
    memset(g, 0, sizeof *g);
    static int occ[1024]; memset(occ, 0, sizeof occ);
    Box *groups[3][64]; int ng[3] = {0};
    for (Box *c = t->first; c; c = c->next) {
        if (!in_flow(c)) continue;
        int d = c->st->display;
        if (d == D_TABLE_CAPTION) { vec_push(g->captions, c); continue; }
        int k = d == D_TABLE_HEADER_GROUP ? 0 : d == D_TABLE_FOOTER_GROUP ? 2 : 1;
        if (ng[k] < 64) groups[k][ng[k]++] = c;
    }
    for (int k = 0; k < 3; k++) for (int i = 0; i < ng[k]; i++) {
        Box *c = groups[k][i];
        if (is_group(c)) { for (Box *r = c->first; r; r = r->next) if (in_flow(r)) { add_row(g, r, occ, g->rows.n); vec_push(g->rows, r); } }
        else if (c->st->display == D_TABLE_ROW) { add_row(g, c, occ, g->rows.n); vec_push(g->rows, c); }
        else { /* stray cell: implicit row = the cell's parent table */ TCell tc = { c, g->rows.n, 0, 1, 1 }; vec_push(g->cells, tc); if (!g->ncols) g->ncols = 1; vec_push(g->rows, c); }
    }
}
static float spacing(const Box *t) { return t->st->border_collapse ? 0 : 2; }
static void col_widths(Layout *L, TGrid *g, float *mn, float *mx) {
    for (int i = 0; i < g->cells.n; i++) { TCell *c = &g->cells.v[i]; if (c->cs != 1) continue; float a, z; intrinsic(L, c->cell, &a, &z); mn[c->c] = LMAX(mn[c->c], a); mx[c->c] = LMAX(mx[c->c], z); }
    for (int i = 0; i < g->cells.n; i++) { TCell *c = &g->cells.v[i]; if (c->cs == 1) continue; float a, z; intrinsic(L, c->cell, &a, &z); float sa = 0, sz = 0; for (int k = c->c; k < c->c + c->cs && k < g->ncols; k++) { sa += mn[k]; sz += mx[k]; } if (a > sa) for (int k = c->c; k < c->c + c->cs && k < g->ncols; k++) mn[k] += (a - sa) / c->cs; if (z > sz) for (int k = c->c; k < c->c + c->cs && k < g->ncols; k++) mx[k] += (z - sz) / c->cs; }
    for (int k = 0; k < g->ncols; k++) if (mx[k] < mn[k]) mx[k] = mn[k];
}
void table_intrinsic(Layout *L, Box *b, float *omn, float *omx) {
    TGrid g; build_grid(b, &g);
    float *mn = xcalloc((size_t)g.ncols + 1, sizeof(float)), *mx = xcalloc((size_t)g.ncols + 1, sizeof(float));
    col_widths(L, &g, mn, mx);
    float sp = spacing(b), a = sp * (g.ncols + 1), z = a;
    for (int k = 0; k < g.ncols; k++) { a += mn[k]; z += mx[k]; }
    *omn = a + hbp(b); *omx = z + hbp(b);
    free(mn); free(mx); vec_free(g.cells); vec_free(g.rows); vec_free(g.captions);
}
float layout_table(Layout *L, Box *b, float cx, float cy, float cw, float chdef) {
    TGrid g; build_grid(b, &g);
    int nc = g.ncols, nr = g.rows.n;
    float sp = spacing(b);
    float *mn = xcalloc((size_t)nc + 1, sizeof(float)), *mx = xcalloc((size_t)nc + 1, sizeof(float));
    col_widths(L, &g, mn, mx);
    float smn = 0, smx = 0; for (int k = 0; k < nc; k++) { smn += mn[k]; smx += mx[k]; }
    float avail = cw - sp * (nc + 1);
    bool specified = b->st->width.kind == LK_LEN;
    float target = specified ? avail : LMIN(avail, smx);
    if (target < smn) target = smn;
    float *w = xcalloc((size_t)nc + 1, sizeof(float));
    for (int k = 0; k < nc; k++) {
        if (target >= smx) w[k] = mx[k] + (smx > 0 ? (target - smx) * mx[k] / smx : (target - smx) / nc);
        else w[k] = mn[k] + (smx > smn ? (target - smn) * (mx[k] - mn[k]) / (smx - smn) : 0);
    }
    float *x0 = xcalloc((size_t)nc + 2, sizeof(float));
    x0[0] = sp; for (int k = 0; k < nc; k++) x0[k + 1] = x0[k] + w[k] + sp;
    float tw = x0[nc];
    /* captions */
    float y = 0;
    for (int i = 0; i < g.captions.n; i++) { Box *c = g.captions.v[i]; layout_box(L, c, cx, cy + y, tw, -1, NULL, SZ_FORCED, tw, -1); y += c->h + c->m[0] + c->m[2]; }
    float *rh = xcalloc((size_t)nr + 1, sizeof(float));
    for (int i = 0; i < g.cells.n; i++) {
        TCell *c = &g.cells.v[i];
        float cwid = x0[LMIN(c->c + c->cs, nc)] - x0[c->c] - sp;
        layout_box(L, c->cell, cx + x0[c->c], cy, cwid, -1, NULL, SZ_FORCED, cwid, -1);
        float need = c->cell->h;
        if (c->rs == 1 && c->r < nr) rh[c->r] = LMAX(rh[c->r], need);
    }
    for (int r = 0; r < nr; r++) { Box *row = g.rows.v[r]; if (row->st->height.kind == LK_LEN && !row->st->height.pct) rh[r] = LMAX(rh[r], row->st->height.px); }
    float *ry = xcalloc((size_t)nr + 2, sizeof(float));
    ry[0] = y + sp; for (int r = 0; r < nr; r++) ry[r + 1] = ry[r] + rh[r] + sp;
    if (chdef > ry[nr] && nr) { float extra = (chdef - ry[nr]) / nr; for (int r = 0; r < nr; r++) rh[r] += extra; for (int r = 0; r < nr; r++) ry[r + 1] = ry[r] + rh[r] + sp; }
    for (int i = 0; i < g.cells.n; i++) {
        TCell *c = &g.cells.v[i]; Box *cell = c->cell;
        int re = LMIN(c->r + c->rs, nr);
        float h = ry[re] - ry[c->r] - sp;
        float content = cell->h;
        cell->h = LMAX(cell->h, h);
        float off = 0; int va = cell->st->vertical_align;
        if (va == VA_MIDDLE || va == VA_BASELINE) off = (cell->h - content) / 2; else if (va == VA_BOTTOM) off = cell->h - content;
        if (va == VA_BASELINE) off = 0;
        if (cell->node && cell->node->tag == A_td && va == VA_BASELINE) off = (cell->h - content) / 2;
        box_translate(cell, 0, cy + ry[c->r] - cell->y);
        if (off > 0.5f) for (Box *k = cell->first; k; k = k->next) box_translate(k, 0, off);
        if (off > 0.5f) for (int f = 0; f < cell->nfrags; f++) { cell->frags[f].y += off; if (cell->frags[f].box->kind == BX_TEXT) cell->frags[f].base += off; }
        if (off > 0.5f) for (int l = 0; l < cell->nlines; l++) { cell->lines[l].y += off; cell->lines[l].base += off; }
    }
    /* rows/groups geometry for backgrounds */
    for (int r = 0; r < nr; r++) { Box *row = g.rows.v[r]; if (row->st->display != D_TABLE_ROW) continue; row->x = cx; row->y = cy + ry[r]; row->w = tw; row->h = rh[r]; }
    for (Box *c = b->first; c; c = c->next) if (is_group(c) && c->first) { c->x = cx; c->y = c->first->y; Box *l = c->last; c->w = tw; c->h = l->y + l->h - c->y; }
    float total = ry[nr];
    if (b->st->width.kind != LK_LEN) { float adj = tw - cw; (void)adj; }
    b->w = tw + hbp(b);
    free(mn); free(mx); free(w); free(x0); free(rh); free(ry);
    vec_free(g.cells); vec_free(g.rows); vec_free(g.captions);
    return total;
}
