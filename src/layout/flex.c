/* Flexbox layout */
#include "layout.h"
#include <math.h>

typedef struct { Box *b; int ord; float base, hypo, mn, mx, main, cross, mm0, mm1; bool frozen, amA0, amA1; } FItem;
typedef struct { int first; float cross; } FLine;
static int ord_cmp(const void *a, const void *b) { const FItem *x = a, *y = b; int d = x->b->st->order - y->b->st->order; return d ? d : (x->ord - y->ord); }

/* Retain alignment qualifiers and logical start/end: the compact Align enum
 * intentionally does not encode safe/unsafe or self-start/self-end. */
static const char *flex_alignment(const ComputedStyle *s, const char *property, int fallback, char out[64]) {
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
static bool flex_axis_reverse(const ComputedStyle *s, bool horizontal) {
    return horizontal ? (s->writing_mode ? s->writing_mode == 2 : s->direction != 0) : (s->writing_mode && s->direction);
}
static float flex_cross_offset(const char *value, bool reverse, bool normal_reverse, bool self_reverse, float free_space) {
    bool safe = false;
    if (!strncmp(value, "safe ", 5)) { safe = true; value += 5; }
    else if (!strncmp(value, "unsafe ", 7)) value += 7;
    float fraction = reverse ? 1 : 0;
    if (!strcmp(value, "center")) fraction = .5f;
    else if (!strcmp(value, "flex-end")) fraction = reverse ? 0 : 1;
    else if (!strcmp(value, "start") || !strcmp(value, "baseline") || !strcmp(value, "first baseline")) fraction = normal_reverse ? 1 : 0;
    else if (!strcmp(value, "end") || !strcmp(value, "last baseline")) fraction = normal_reverse ? 0 : 1;
    else if (!strcmp(value, "self-start")) fraction = self_reverse ? 1 : 0;
    else if (!strcmp(value, "self-end")) fraction = self_reverse ? 0 : 1;
    if (safe && free_space < 0) fraction = reverse ? 1 : 0;
    return free_space * fraction;
}
static float flex_cross_stretch_size(Layout *L, Box *c, const Box *cb, bool row, float available, float reference, float stretch_reference) {
    const ComputedStyle *s = c->st;
    float bp = row ? vbp(c) : hbp(c), extra = s->box_sizing == BOX_BORDER ? 0 : bp;
    Length lo = row ? s->min_height : s->min_width, hi = row ? s->max_height : s->max_width;
    float size = LMAX(bp, available), mn = 0, mx = 0;
    float stretch = lo.kind == LK_STRETCH || hi.kind == LK_STRETCH ?
        layout_stretch_border_size(c, cb, !row, stretch_reference, stretch_reference >= 0) : -1;
    bool ilo = !row && !s->writing_mode && intrinsic_width_keyword(lo);
    bool ihi = !row && !s->writing_mode && intrinsic_width_keyword(hi);
    if (ilo || ihi) intrinsic_content_width(L, c, reference, &mn, &mx);
    if (hi.kind == LK_STRETCH && stretch >= 0) size = LMIN(size, stretch);
    else if (len_def(hi, reference)) size = LMIN(size, LMAX(bp, res(hi, reference) + extra));
    else if (ihi) size = LMIN(size, intrinsic_width_size(hi, mn, mx, available));
    if (lo.kind == LK_STRETCH && stretch >= 0) size = LMAX(size, stretch);
    else if (len_def(lo, reference)) size = LMAX(size, res(lo, reference) + extra);
    else if (ilo) size = LMAX(size, intrinsic_width_size(lo, mn, mx, available));
    return size;
}
/* Resolve percentage padding against the original containing block while
 * finding a column item's natural cross width. Ordinary intrinsic() caches
 * zero-basis edges, which cannot supply the used border-box width here. */
static bool flex_column_stretch_intrinsic_width(Layout *L, Box *c, const Box *cb,
                                                float cw, float available, float *width) {
    const ComputedStyle *s = c->st;
    if ((s->min_width.kind != LK_STRETCH && s->max_width.kind != LK_STRETCH) ||
        (s->width.kind != LK_AUTO && !intrinsic_width_keyword(s->width)) ||
        s->writing_mode || s->aspect_ratio > 0 || c->fmt == FMT_REPLACED) return false;
    compute_mbp(c, cw);
    float mn, mx;
    intrinsic_content_width(L, c, cw, &mn, &mx);
    float natural = intrinsic_width_size(s->width, mn, mx, available - c->m[1] - c->m[3]);
    *width = flex_cross_stretch_size(L, c, cb, false, natural, cw, available);
    return true;
}
/* A larger final line can relax a stretch maximum. Remeasure the preferred
 * size instead of clamping an already capped hypothetical size. Express the
 * resolved border-box limit in property units for the normal layout path;
 * percentage lengths still use the original containing-block basis. */
static void flex_cross_remeasure_limits(Layout *L, Box *c, const Box *cb, bool row,
                                        float cw, float chdef, float linecross) {
    float width;
    if (!row && flex_column_stretch_intrinsic_width(L, c, cb, cw, linecross, &width)) {
        c->memo = false;
        layout_box(L, c, c->x, c->y, cw, chdef, NULL, SZ_FORCED, width, c->h);
        return;
    }
    ComputedStyle *original = c->st, resolved = *original;
    float size = layout_stretch_border_size(c, cb, !row, linecross, true);
    float bp = row ? vbp(c) : hbp(c);
    Length limit = L_px(LMAX(0, size - (resolved.box_sizing == BOX_BORDER ? 0 : bp)));
    Length *lo = row ? &resolved.min_height : &resolved.min_width;
    Length *hi = row ? &resolved.max_height : &resolved.max_width;
    if (lo->kind == LK_STRETCH) *lo = limit;
    if (hi->kind == LK_STRETCH) *hi = limit;
    /* Column items already have their ordinary intrinsic cache from the
     * main-size pass. Preserve it here: a fresh zero-basis traversal would
     * overwrite percentage edges. The resolved limits are applied by clampw;
     * row items use forced width and do not need root intrinsic sizing. */
    c->st = &resolved; c->memo = false;
    layout_box(L, c, c->x, c->y, cw, chdef, NULL, row ? SZ_FORCED : SZ_SHRINK,
               c->w, row ? -1 : c->h);
    c->st = original; c->memo = false; c->intr_ok = false;
}
float layout_flex(Layout *L, Box *b, float cx, float cy, float cw, float chdef) {
    const ComputedStyle *s = b->st;
    bool row = s->flex_direction <= FD_ROW_REVERSE, rev = s->flex_direction == FD_ROW_REVERSE || s->flex_direction == FD_COLUMN_REVERSE;
    if (row && !s->writing_mode && s->direction) rev = !rev;
    float mgap = row ? s->column_gap : s->row_gap, cgap = row ? s->row_gap : s->column_gap;
    if (row && s->column_gap_l.pct) mgap = res(s->column_gap_l, cw);
    VEC(FItem) it = {0}; int idx = 0;
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs) { c->sx = cx; c->sy = cy; add_abs(L, c); continue; }
        FItem f; memset(&f, 0, sizeof f); f.b = c; f.ord = idx++; c->bfc = true;
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
        Length mnl = row ? cs->min_width : cs->min_height, mxl = row ? cs->max_width : cs->max_height;
        float stretch = fb.kind == LK_STRETCH || sz.kind == LK_STRETCH ||
                        mnl.kind == LK_STRETCH || mxl.kind == LK_STRETCH ?
            layout_stretch_border_size(c, b, row, mainsz, mainsz >= 0) : -1;
        bool content_basis = fb.kind == LK_CONTENT || (fb.kind == LK_STRETCH && stretch < 0);
        bool specified_main = (sz.kind == LK_LEN && len_def(sz, mainsz)) ||
                              (sz.kind == LK_STRETCH && stretch >= 0);
        float suggested_main = sz.kind == LK_STRETCH ? stretch : res(sz, mainsz) + (bs ? 0 : bp);
        float mn, mx;
        if (row && content_basis) intrinsic_content_width(L, c, -1, &mn, &mx);
        else intrinsic(L, c, &mn, &mx);
        if (row && c->fmt == FMT_REPLACED && cs->height.kind == LK_STRETCH && chdef >= 0) {
            /* A definite cross size participates in replaced intrinsic sizing.
             * Keep main-size constraints out of this content suggestion; the
             * normal specified-size and min/max branches apply them below. */
            ComputedStyle measured = *cs;
            measured.width = L_auto(); measured.min_width = L_auto();
            measured.max_width = (Length){ .kind = LK_NONE };
            compute_mbp(c, cw);
            float measured_w, measured_h;
            c->st = &measured;
            replaced_size(L, c, cw, chdef, -1, -1, &measured_w, &measured_h);
            c->st = (ComputedStyle *)cs;
            mn = mx = measured_w + hbp(c);
        }
        float content_h = -1;
        if (fb.kind == LK_STRETCH && stretch >= 0) f->base = stretch;
        else if (len_def(fb, mainsz) && !(fb.pctu && mainsz < 0)) f->base = res(fb, mainsz) + (bs ? 0 : bp);
        else if (!content_basis && specified_main) f->base = suggested_main;
        else if (row) f->base = mx;
        else {
            float w = (cs->width.kind == LK_LEN || cs->width.kind == LK_STRETCH) ? 0 : cw - c->m[1] - c->m[3];
            ComputedStyle measured;
            if (content_basis) {
                measured = *cs; measured.height = L_auto(); measured.min_height = L_auto();
                measured.max_height = (Length){ .kind = LK_NONE }; c->st = &measured; c->memo = false;
            }
            layout_box(L, c, cx + c->m[3], cy, cw, -1, NULL, (cs->width.kind == LK_LEN || cs->width.kind == LK_STRETCH) ? SZ_FILL : SZ_FORCED, w, -1);
            c->st = (ComputedStyle *)cs;
            if (content_basis) c->memo = false;
            f->base = content_h = c->h;
        }
        bool ilo = row && !cs->writing_mode && intrinsic_width_keyword(mnl);
        bool ihi = row && !cs->writing_mode && intrinsic_width_keyword(mxl);
        float imn = mn, imx = mx, available = mainsz - f->mm0 - f->mm1;
        if (ilo || ihi) intrinsic_content_width(L, c, cw, &imn, &imx);
        if (mnl.kind == LK_STRETCH) f->mn = stretch >= 0 ? stretch : bp;
        else if (mnl.kind == LK_LEN) f->mn = res(mnl, mainsz) + (bs ? 0 : bp);
        else if (ilo) f->mn = intrinsic_width_size(mnl, imn, imx, available);
        else if (row && cs->overflow_x == OV_VISIBLE) f->mn = LMIN(imn, specified_main ? suggested_main : imn);
        else if (!row && cs->overflow_y == OV_VISIBLE) {
            if (content_h < 0) { float w = (cs->width.kind == LK_LEN || cs->width.kind == LK_STRETCH) ? 0 : cw - c->m[1] - c->m[3]; layout_box(L, c, cx + c->m[3], cy, cw, -1, NULL, (cs->width.kind == LK_LEN || cs->width.kind == LK_STRETCH) ? SZ_FILL : SZ_FORCED, w, -1); content_h = c->h; }
            f->mn = specified_main ? LMIN(content_h, suggested_main) : content_h;
        }
        else f->mn = bp;
        f->mx = mxl.kind == LK_STRETCH ? (stretch >= 0 ? stretch : 1e30f) :
                mxl.kind == LK_LEN && len_def(mxl, mainsz) ? res(mxl, mainsz) + (bs ? 0 : bp) :
                ihi ? intrinsic_width_size(mxl, imn, imx, available) : 1e30f;
        /* A definite maximum caps the content-based automatic minimum. An
         * explicit minimum still wins when the two author limits conflict. */
        if (mnl.kind == LK_AUTO && mxl.kind == LK_STRETCH && stretch >= 0)
            f->mn = LMIN(f->mn, f->mx);
        if (f->mx < f->mn) f->mx = f->mn;
        f->hypo = LCLAMP(f->base, f->mn, f->mx);
    }
    if (mainsz < 0) { float t = 0; for (int i = 0; i < it.n; i++) t += it.v[i].hypo + it.v[i].mm0 + it.v[i].mm1; mainsz = t + mgap * LMAX(0, it.n - 1); float mxh = s->max_height.kind == LK_LEN && !s->max_height.pct ? s->max_height.px : 1e30f; if (mainsz > mxh) mainsz = mxh; if (s->min_height.kind == LK_LEN && !s->min_height.pct && mainsz < s->min_height.px) mainsz = s->min_height.px; }
    /* lines */
    VEC(FLine) lines = {0}; vec_push(lines, ((FLine){ .first = 0 }));
    if (s->flex_wrap) { float acc = 0; int cnt = 0; for (int i = 0; i < it.n; i++) { float o = it.v[i].hypo + it.v[i].mm0 + it.v[i].mm1; if (cnt && acc + mgap + o > mainsz + 0.01f) { vec_push(lines, ((FLine){ .first = i })); acc = o; cnt = 1; } else { acc += (cnt ? mgap : 0) + o; cnt++; } } }
    vec_push(lines, ((FLine){ .first = it.n }));
    float crossdef = row ? chdef : cw;
    float total_cross = 0;
    int nlines = it.n ? lines.n - 1 : 0;
    for (int ln = 0; ln < nlines; ln++) {
        int a = lines.v[ln].first, z = lines.v[ln + 1].first, n = z - a;
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
                if ((al == AL_STRETCH || al == AL_NORMAL) && cs->height.kind == LK_AUTO && chdef >= 0 && !s->flex_wrap && cs->margin[0].kind != LK_AUTO && cs->margin[2].kind != LK_AUTO) fh = flex_cross_stretch_size(L, c, b, true, chdef - c->m[0] - c->m[2], chdef, chdef);
                layout_box(L, c, cx + mpos, cy + c->m[0], cw, chdef, NULL, SZ_FORCED, f->main, fh);
                f->cross = c->h + c->m[0] + c->m[2];
            } else {
                int al = cs->align_self == AL_AUTO ? s->align_items : cs->align_self;
                bool stretch = !s->flex_wrap && (al == AL_STRETCH || al == AL_NORMAL) && cs->width.kind == LK_AUTO && cs->margin[1].kind != LK_AUTO && cs->margin[3].kind != LK_AUTO;
                float w = cw - c->m[1] - c->m[3];
                if (stretch) w = flex_cross_stretch_size(L, c, b, false, w, cw, cw);
                bool intrinsic_stretch = !stretch && flex_column_stretch_intrinsic_width(L, c, b, cw, cw, &w);
                layout_box(L, c, cx + c->m[3], cy + mpos, cw, chdef >= 0 ? chdef : -1, NULL, stretch || intrinsic_stretch ? SZ_FORCED : SZ_SHRINK, w, f->main);
                f->cross = c->w + c->m[1] + c->m[3];
            }
            linecross = LMAX(linecross, f->cross);
            pos += f->mm0 + f->main + f->mm1 + between;
        }
        /* Only nowrap has a line constrained directly to the container's
         * cross size. A wrapping container with one line still aligns it. */
        if (!s->flex_wrap && crossdef >= 0) linecross = crossdef;
        lines.v[ln].cross = linecross;
        total_cross += linecross + (ln ? cgap : 0);
    }
    /* CSS Flexbox 9.4/9.6: first measure every line, distribute the remaining
     * cross space, then determine the items' stretched sizes and positions. */
    bool normal_reverse = flex_axis_reverse(s, !row);
    bool cross_reverse = normal_reverse ^ (s->flex_wrap == 2);
    float free_cross = crossdef >= 0 ? crossdef - total_cross : 0;
    float line_offset = 0, extra_gap = 0;
    char content_buf[64];
    const char *content = flex_alignment(s, "align-content", s->align_content, content_buf);
    if (s->flex_wrap && nlines) {
        if ((!strcmp(content, "normal") || !strcmp(content, "stretch")) && free_cross > 0) {
            for (int ln = 0; ln < nlines; ln++) lines.v[ln].cross += free_cross / nlines;
            total_cross += free_cross; free_cross = 0;
        }
        if (!strcmp(content, "space-between")) {
            if (nlines > 1 && free_cross > 0) extra_gap = free_cross / (nlines - 1);
            else line_offset = cross_reverse ? free_cross : 0;
        } else if (!strcmp(content, "space-around") || !strcmp(content, "space-evenly")) {
            if (free_cross > 0) {
                bool around = !strcmp(content, "space-around");
                extra_gap = free_cross / (around ? nlines : nlines + 1);
                line_offset = extra_gap / (around ? 2 : 1);
            } else line_offset = cross_reverse ? free_cross : 0;
        } else line_offset = flex_cross_offset(content, cross_reverse, normal_reverse, normal_reverse, free_cross);
    }
    float occupied_cross = total_cross + extra_gap * LMAX(0, nlines - 1), progress_cross = 0;
    char parent_align_buf[64];
    const char *parent_align = flex_alignment(s, "align-items", s->align_items, parent_align_buf);
    for (int ln = 0; ln < nlines; ln++) {
        float linecross = lines.v[ln].cross;
        float linepos = line_offset + (cross_reverse ? occupied_cross - progress_cross - linecross : progress_cross);
        for (int i = lines.v[ln].first; i < lines.v[ln + 1].first; i++) {
            FItem *f = &it.v[i]; Box *c = f->b; const ComputedStyle *cs = c->st;
            char self_buf[64];
            const char *align = cs->align_self == AL_AUTO ? parent_align : flex_alignment(cs, "align-self", cs->align_self, self_buf);
            int low = row ? 0 : 3, high = row ? 2 : 1;
            bool auto_low = cs->margin[low].kind == LK_AUTO, auto_high = cs->margin[high].kind == LK_AUTO;
            bool stretch = !strcmp(align, "normal") || !strcmp(align, "stretch");
            Length cross_size = row ? cs->height : cs->width;
            bool sizing_stretch = cross_size.kind == LK_STRETCH;
            bool alignment_stretch = stretch && !auto_low && !auto_high && cross_size.kind == LK_AUTO;
            bool stretch_limits = (row ? cs->min_height : cs->min_width).kind == LK_STRETCH ||
                                  (row ? cs->max_height : cs->max_width).kind == LK_STRETCH;
            if (sizing_stretch || alignment_stretch) {
                float available = sizing_stretch ?
                    layout_stretch_border_size(c, b, !row, linecross, true) :
                    linecross - c->m[low] - c->m[high];
                float size = flex_cross_stretch_size(L, c, b, row, available, crossdef, linecross);
                /* Sizing stretch contributes against a definite container in
                 * the measure pass, then resolves against the final line.
                 * Auto margins suppress alignment stretch, not sizing stretch. */
                bool established_cross = !s->flex_wrap && (!row || chdef >= 0);
                if (!established_cross || fabsf(size - (row ? c->h : c->w)) > .01f)
                    layout_box(L, c, c->x, c->y, cw, chdef, NULL, SZ_FORCED, row ? c->w : size, row ? size : c->h);
            } else if (stretch_limits && (crossdef < 0 || fabsf(linecross - crossdef) > .01f)) {
                float before = row ? c->h : c->w;
                flex_cross_remeasure_limits(L, c, b, row, cw, chdef, linecross);
                float size = row ? c->h : c->w;
                /* A changed used cross size is definite for descendants;
                 * preserve the already flexed main size. */
                if (fabsf(size - before) > .01f)
                    layout_box(L, c, c->x, c->y, cw, chdef, NULL, SZ_FORCED, c->w, c->h);
            }
            float free_item = linecross - (row ? c->h : c->w) - c->m[low] - c->m[high];
            float off;
            if (auto_low || auto_high)
                off = free_item > 0 ? (auto_low ? free_item / (auto_high ? 2 : 1) : 0) : (normal_reverse ? free_item : 0);
            else if (stretch) off = cross_reverse ? free_item : 0;
            /* Last baseline needs a baseline-sharing group; end placement is
             * not equivalent. Preserve existing in-flow start placement until
             * that separate group calculation is implemented. */
            else if (!strcmp(align, "last baseline")) off = 0;
            else off = flex_cross_offset(align, cross_reverse, normal_reverse, flex_axis_reverse(cs, !row), free_item);
            float target = (row ? cy : cx) + linepos + c->m[low] + off;
            float delta = target - (row ? c->y : c->x);
            if (delta) box_translate(c, row ? 0 : delta, row ? delta : 0);
        }
        progress_cross += linecross + cgap + extra_gap;
    }
    b->has_baseline = false;
    for (int i = 0; i < it.n; i++) if (it.v[i].b->has_baseline) { b->has_baseline = true; b->baseline = it.v[i].b->baseline; break; }
    vec_free(it); vec_free(lines);
    return row ? total_cross : mainsz;
}
