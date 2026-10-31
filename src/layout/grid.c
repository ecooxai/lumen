#include <math.h>
/* Grid and table layout */
#include "layout.h"

static float track_fixed(const GridTrack *t, float reference) {
    if (t->fr || t->size.kind != LK_LEN || !len_def(t->size, reference)) return -1;
    float maximum = res(t->size, reference);
    /* A minmax() maximum below its minimum is floored at the minimum. */
    return t->min.kind == LK_LEN && len_def(t->min, reference) ? LMAX(maximum, res(t->min, reference)) : maximum;
}
static int implicit_track_pattern(Layout *L, const ComputedStyle *s, const char *property, GridTrack **out) {
    const char *value = custom_get(s->gen_n, property);
    *out = NULL;
    if (!value || !*value || str_ieq(value, "auto")) return 0;
    float rem = L->doc && L->doc->html && L->doc->html->style ? L->doc->html->style->font_size : 16;
    MediaCtx media = { L->vw, L->vh, L->dpr, false };
    return css_parse_grid_tracks(value, s->font_size, rem, &media, out);
}

typedef VEC(GridTrack) GridTracks;
static float auto_repeat_measure(const GridTrack *track, float reference) {
    float maximum = track_fixed(track, reference);
    if (maximum < 0 && track->min.kind == LK_LEN && len_def(track->min, reference)) maximum = res(track->min, reference);
    return LMAX(0, maximum);
}
static void expand_grid_tracks(const GridTrack *source, int count, float available, float gap, GridTracks *out) {
    for (int i = 0; i < count && out->n < 1000; ) {
        int span = LMIN(source[i].repeat_span, count - i);
        if (!span) { vec_push(*out, source[i++]); continue; }
        double pattern = 0, other = 0;
        /* Only repeated track sizes need the 1px UA floor. Fixed zero
           tracks outside the pattern must remain zero in the equation. */
        for (int k = 0; k < span; k++) pattern += LMAX(1, auto_repeat_measure(&source[i + k], available));
        for (int k = 0; k < count; k++) if (k < i || k >= i + span)
            other += auto_repeat_measure(&source[k], available);
        int repetitions = 1, limit = LMAX(1, (1000 - (count - span)) / span);
        if (available >= 0 && isfinite(available)) {
            double fits = ((double)available - other - gap * (count - span - 1)) / (pattern + gap * span);
            if (fits >= limit) repetitions = limit;
            else if (fits > 1) repetitions = (int)floor(fits + 1e-7);
        }
        for (int repeat = 0; repeat < repetitions && out->n < 1000; repeat++)
            for (int k = 0; k < span && out->n < 1000; k++) {
                GridTrack track = source[i + k]; track.repeat_span = 0; vec_push(*out, track);
            }
        i += span;
    }
}

static bool area_lookup(const char *areas, const char *name, int *r, int *c, int *rs, int *cs) {
    int r0 = 1 << 20, c0 = 1 << 20, r1 = -1, c1 = -1, row = 0, col = 0;
    size_t nl = strlen(name);
    for (const char *q = areas; ; ) {
        const char *e = q; while (*e && *e != ' ' && *e != '/') e++;
        if ((size_t)(e - q) == nl && !memcmp(q, name, nl)) { r0 = LMIN(r0, row); c0 = LMIN(c0, col); r1 = LMAX(r1, row); c1 = LMAX(c1, col); }
        if (!*e) break;
        if (*e == '/') { row++; col = 0; } else col++;
        q = e + 1;
    }
    if (r1 < 0) return false;
    *r = r0; *c = c0; *rs = r1 - r0 + 1; *cs = c1 - c0 + 1; return true;
}
static int area_cols(const char *a) { int n = 1; for (; *a && *a != '/'; a++) if (*a == ' ') n++; return n; }
static int area_rows(const char *a) { int n = 1; for (; *a; a++) if (*a == '/') n++; return n; }
static GridTrack grid_auto_track(const GridTrack *pattern, int count, int offset) {
    if (!count) return (GridTrack){ L_auto(), 0, L_auto() };
    int index = offset % count;
    return pattern[index < 0 ? index + count : index];
}

typedef struct { uint8_t *v; int rows, nc; } Occ;
static bool occ_free(const Occ *o, int r, int c, int rs, int cs) {
    if (c + cs > o->nc) return false;
    for (int y = r; y < r + rs && y < o->rows; y++) for (int x = c; x < c + cs; x++) if (o->v[(size_t)y * o->nc + x]) return false;
    return true;
}
static void occ_mark(Occ *o, int r, int c, int rs, int cs) {
    if (r + rs > o->rows) { int nr = r + rs; o->v = xrealloc(o->v, (size_t)nr * o->nc); memset(o->v + (size_t)o->rows * o->nc, 0, (size_t)(nr - o->rows) * o->nc); o->rows = nr; }
    for (int y = r; y < r + rs; y++) for (int x = c; x < c + cs && x < o->nc; x++) o->v[(size_t)y * o->nc + x] = 1;
}

/* A preferred width that behaves as auto (or depends on the grid area) with no min-width contributes the automatic
   minimum, which a fixed maximum clamps; a definite width or min-width contributes the min-content contribution. */
float grid_item_auto_min(Layout *L, Box *c, float maxlen) {
    float a, z; intrinsic_outer(L, c, &a, &z);
    const ComputedStyle *s = c->st;
    bool dep = s->width.kind == LK_AUTO || s->width.kind == LK_STRETCH || len_has_pct(s->width);
    bool minw = s->min_width.kind == LK_LEN && (s->min_width.px > 0 || s->min_width.pct);
    float m = (s->margin[1].pct ? 0 : s->margin[1].px) + (s->margin[3].pct ? 0 : s->margin[3].px);   /* the clamp is on the box, margins come on top */
    return dep && !minw ? LMIN(a - m, LMAX(0, maxlen - m)) + m : a;
}

/* CSS Grid 10: align tracks in the content box, then items in their areas.
   Keep qualifier/physical-edge text: the compact Align enum loses left/right,
   self-start/self-end and safe/unsafe. */
static const char *grid_alignment(const ComputedStyle *s, const char *property, int fallback, char out[64]) {
    static const char *const names[] = { "auto", "normal", "stretch", "flex-start", "flex-end", "center", "baseline", "space-between", "space-around", "space-evenly", "start", "end" };
    const char *value = custom_get(s->gen_n, property);
    if (!value) return fallback >= 0 && (size_t)fallback < sizeof names / sizeof *names ? names[fallback] : "normal";
    if (!*value) return !strcmp(property, "align-self") || !strcmp(property, "justify-self") ? "auto" : "normal";
    size_t n = 0; bool space = false;
    for (const unsigned char *q = (const unsigned char *)value; *q; q++) {
        if (is_ws(*q)) { if (n) space = true; continue; }
        if (n + (space ? 1 : 0) + 1 >= 64) return "normal";
        if (space) out[n++] = ' '; space = false;
        out[n++] = *q >= 'A' && *q <= 'Z' ? (char)(*q + ('a' - 'A')) : (char)*q;
    }
    out[n] = 0; return out;
}
static const char *grid_self_alignment(const ComputedStyle *s, const ComputedStyle *parent, bool horizontal, char out[64]) {
    const char *v = grid_alignment(s, horizontal ? "justify-self" : "align-self", horizontal ? s->justify_self : s->align_self, out);
    if (!strcmp(v, "auto")) v = grid_alignment(parent, horizontal ? "justify-items" : "align-items", horizontal ? parent->justify_items : parent->align_items, out);
    return v;
}
static bool grid_axis_reverse(const ComputedStyle *s, bool horizontal) {
    return horizontal ? (s->writing_mode ? s->writing_mode == 2 : s->direction != 0) : (s->writing_mode && s->direction);
}
static float grid_align_offset(const char *value, bool reverse, bool self_reverse, float free_space) {
    bool safe = false;
    if (!strncmp(value, "safe ", 5)) { safe = true; value += 5; }
    else if (!strncmp(value, "unsafe ", 7)) value += 7;
    float fraction = reverse ? 1 : 0;
    if (!strcmp(value, "center")) fraction = .5f;
    else if (!strcmp(value, "end") || !strcmp(value, "flex-end") || !strcmp(value, "last baseline")) fraction = reverse ? 0 : 1;
    else if (!strcmp(value, "left")) fraction = 0;
    else if (!strcmp(value, "right")) fraction = 1;
    else if (!strcmp(value, "self-start")) fraction = self_reverse ? 1 : 0;
    else if (!strcmp(value, "self-end")) fraction = self_reverse ? 0 : 1;
    if (safe && free_space < 0) fraction = reverse ? 1 : 0;
    return free_space * fraction;
}
static void grid_content_alignment(const char *value, bool reverse, int tracks, float free_space, float *offset, float *gap) {
    *gap = 0;
    if (!strcmp(value, "space-between")) {
        if (tracks > 1 && free_space > 0) { *offset = 0; *gap = free_space / (tracks - 1); }
        else *offset = reverse ? free_space : 0;
    } else if (!strcmp(value, "space-around") || !strcmp(value, "space-evenly")) {
        if (tracks && free_space > 0) {
            bool around = !strcmp(value, "space-around");
            *gap = free_space / (around ? tracks : tracks + 1);
            *offset = *gap / (around ? 2 : 1);
        } else *offset = reverse ? free_space : 0;
    } else *offset = grid_align_offset(value, reverse, reverse, free_space);
}
static bool grid_stretches(const Box *c, bool horizontal, const char *alignment) {
    const ComputedStyle *s = c->st;
    if ((horizontal ? s->width : s->height).kind != LK_AUTO) return false;
    if (s->margin[horizontal ? 3 : 0].kind == LK_AUTO || s->margin[horizontal ? 1 : 2].kind == LK_AUTO) return false;
    if (!strcmp(alignment, "stretch")) return true;
    return (!strcmp(alignment, "normal") || !strcmp(alignment, "auto")) && c->fmt != FMT_REPLACED && s->aspect_ratio <= 0;
}
static float grid_stretch_size(const Box *c, const Box *cb, bool horizontal, float available, float reference) {
    const ComputedStyle *s = c->st;
    float bp = horizontal ? hbp(c) : vbp(c), extra = s->box_sizing == BOX_BORDER ? 0 : bp;
    Length lo = horizontal ? s->min_width : s->min_height, hi = horizontal ? s->max_width : s->max_height;
    float size = LMAX(bp, available);
    float stretch = lo.kind == LK_STRETCH || hi.kind == LK_STRETCH ?
        layout_stretch_border_size(c, cb, horizontal, reference, reference >= 0) : -1;
    if (hi.kind == LK_STRETCH && stretch >= 0) size = LMIN(size, stretch);
    else if (len_def(hi, reference)) size = LMIN(size, LMAX(bp, res(hi, reference) + extra));
    if (lo.kind == LK_STRETCH && stretch >= 0) size = LMAX(size, stretch);
    else if (len_def(lo, reference)) size = LMAX(size, res(lo, reference) + extra);
    return size;
}
static float grid_item_offset(Box *c, bool horizontal, const char *alignment, bool reverse, float area_size) {
    int low = horizontal ? 3 : 0, high = horizontal ? 1 : 2;
    bool a = c->st->margin[low].kind == LK_AUTO, z = c->st->margin[high].kind == LK_AUTO;
    float free_space = area_size - (horizontal ? c->w : c->h) - c->m[low] - c->m[high];
    if (a || z) {
        if (free_space > 0) {
            float share = free_space / ((int)a + (int)z);
            if (a) c->m[low] = share;
            if (z) c->m[high] = share;
        }
        /* Chromium150 compatibility: overflowing auto margins still suppress
           self-alignment. Current Grid1 10.2 / Grid2 11.2 instead say overflow
           follows box alignment; this is an explicit compatibility divergence.
           Zero margins keep the item at the container's start (also in RTL). */
        return c->m[low] + (reverse && free_space < 0 ? free_space : 0);
    }
    return c->m[low] + grid_align_offset(alignment, reverse, grid_axis_reverse(c->st, horizontal), free_space);
}

/* CSS Grid 8.3/8.3.1: resolve numeric edges after the explicit grid is known.
   Reversed definite lines are swapped; equal lines imply one track; a pair of
   spans discards its end span. Negative positions precede the explicit grid. */
typedef struct GridPosition { int start, span; bool definite; } GridPosition;
static int grid_line_index(int number, int explicit_tracks) {
    number = LMAX(-1000, LMIN(number, 1000));
    return number > 0 ? number - 1 : explicit_tracks + 1 + number;
}
static GridPosition grid_axis_position(GridAxis axis, int explicit_tracks) {
    int span = axis.start.span ? axis.start.span : axis.end.span;
    span = LMAX(1, LMIN(span, 1000));
    bool first = axis.start.number != 0, last = axis.end.number != 0;
    int start = first ? grid_line_index(axis.start.number, explicit_tracks) : 0;
    int end = last ? grid_line_index(axis.end.number, explicit_tracks) : 0;
    if (first && last) {
        if (end < start) { int tmp = start; start = end; end = tmp; }
        span = LMAX(1, LMIN(end - start, 1000));
    } else if (last) start = end - span;
    return (GridPosition){ LMAX(-1000, LMIN(start, 999)), span, first || last };
}

/* Cyclic percentage tracks are auto during intrinsic sizing. For explicit
 * single-column items, their contributions belong to that column only; a
 * neighboring fixed track contributes its own fixed size, not the widest
 * child's size. Leave spans and implicit/flexible/minmax tracks to the
 * existing path until their full distribution algorithm is available. */
bool grid_intrinsic_explicit(Layout *L, Box *b, float *mn, float *mx) {
    const ComputedStyle *s = b->st;
    int nc = s->grid_ncols;
    if (nc <= 0 || nc > 1000 || !s->grid_cols || s->grid_areas) return false;
    bool percentage = false;
    for (int i = 0; i < nc; i++) {
        const GridTrack *t = &s->grid_cols[i];
        if (t->size.kind != LK_LEN || t->size.mm || t->fr || t->min.kind != LK_AUTO) return false;
        percentage |= t->size.pct != 0 || t->size.pctu;
    }
    if (!percentage) return false;
    for (Box *c = b->first; c; c = c->next) {
        if (!in_flow(c)) continue;
        GridPosition p = grid_axis_position(c->st->grid_column, nc);
        if (!p.definite || p.span != 1 || p.start < 0 || p.start >= nc) return false;
    }
    float *sizes = xcalloc((size_t)nc * 2, sizeof(float));
    float *minimum = sizes, *maximum = sizes + nc;
    for (int i = 0; i < nc; i++) {
        Length size = s->grid_cols[i].size;
        if (size.pct == 0 && !size.pctu) minimum[i] = maximum[i] = LMAX(0, size.px);
    }
    for (Box *c = b->first; c; c = c->next) {
        if (!in_flow(c)) continue;
        int col = grid_axis_position(c->st->grid_column, nc).start;
        Length size = s->grid_cols[col].size;
        if (size.pct == 0 && !size.pctu) continue;
        float a, z;
        intrinsic_outer(L, c, &a, &z);
        minimum[col] = LMAX(minimum[col], a);
        maximum[col] = LMAX(maximum[col], z);
    }
    float lo = s->column_gap * (nc - 1), hi = lo;
    for (int i = 0; i < nc; i++) { lo += minimum[i]; hi += maximum[i]; }
    free(sizes);
    *mn = lo; *mx = LMAX(lo, hi);
    return true;
}


/* CSS Grid 9.1: an absolutely positioned descendant uses the grid area of
   its containing block. Auto and nonexistent lines use the padding edge;
   out-of-flow placement must never create implicit tracks. */
static int64_t grid_absolute_line(int number, int explicit_tracks, int leading) {
    return (number > 0 ? (int64_t)number - 1 : (int64_t)explicit_tracks + 1 + number) + leading;
}
static void grid_absolute_edges(GridAxis axis, const GridUsedTracks *tracks, int dim, int edge[2]) {
    int64_t start = 0, end = 0;
    bool first = axis.start.number != 0, last = axis.end.number != 0;
    if (first) start = grid_absolute_line(axis.start.number, tracks->explicit_count[dim], tracks->explicit_start[dim]);
    if (last) end = grid_absolute_line(axis.end.number, tracks->explicit_count[dim], tracks->explicit_start[dim]);
    if (first && last) {
        if (end < start) { int64_t tmp = start; start = end; end = tmp; }
        if (end == start) end = start + 1;
    }
    int count = tracks->count[dim];
    if (first && (start < 0 || start > count)) first = false;
    if (last && (end < 0 || end > count)) last = false;
    /* A span can resolve only against a surviving definite opposite edge. */
    if (last && axis.start.span > 0) { start = end - (int64_t)axis.start.span; first = start >= 0 && start <= count; }
    if (first && axis.end.span > 0) { end = start + (int64_t)axis.end.span; last = end >= 0 && end <= count; }
    edge[0] = first ? (int)start : -1;
    edge[1] = last ? (int)end : -1;
}
static float grid_absolute_coordinate(const GridUsedTracks *tracks, int dim, int line, bool end, bool reverse) {
    int count = tracks->count[dim];
    float position = 0, total = 0;
    for (int i = 0; i < count; ++i) {
        float gap = i < count - 1 ? (tracks->gap_after[dim] ? tracks->gap_after[dim][i] : tracks->gap[dim]) : 0;
        total += tracks->size[dim][i] + gap;
        if (i < line) position += tracks->size[dim][i] + gap;
    }
    /* A gutter belongs to neither adjacent grid area. */
    if (end && line > 0 && line < count)
        position -= tracks->gap_after[dim] ? tracks->gap_after[dim][line - 1] : tracks->gap[dim];
    return tracks->offset[dim] + (reverse ? total - position : position);
}
void grid_absolute_containing_block(const Box *cb, const Box *a, float *x, float *y, float *w, float *h) {
    /* Vertical writing-mode grid geometry is not implemented by this path. */
    if (!cb || cb->fmt != FMT_GRID || !cb->grid_used || !cb->st || cb->st->writing_mode || !a->st) return;
    const GridUsedTracks *tracks = cb->grid_used;
    for (int dim = 0; dim < 2; ++dim) {
        int edge[2];
        grid_absolute_edges(dim ? a->st->grid_row : a->st->grid_column, tracks, dim, edge);
        bool reverse = !dim && cb->st->direction;
        float *origin = dim ? y : x, *extent = dim ? h : w;
        float content = *origin + cb->p[dim ? 0 : 3];
        float start = *origin + (reverse ? *extent : 0);
        float end = *origin + (reverse ? 0 : *extent);
        if (edge[0] >= 0) start = content + grid_absolute_coordinate(tracks, dim, edge[0], false, reverse);
        if (edge[1] >= 0) end = content + grid_absolute_coordinate(tracks, dim, edge[1], true, reverse);
        *origin = reverse ? end : start;
        *extent = LMAX(0, reverse ? start - end : end - start);
    }
}

float layout_grid(Layout *L, Box *b, float cx, float cy, float cw, float chdef) {
    const ComputedStyle *s = b->st;
    float cg = s->column_gap_l.pct ? res(s->column_gap_l, cw) : s->column_gap, rg = s->row_gap;
    int nc;
    GridTrack *auto_cols = NULL, *auto_rows = NULL;
    int n_auto_cols = implicit_track_pattern(L, s, "grid-auto-columns", &auto_cols);
    int n_auto_rows = implicit_track_pattern(L, s, "grid-auto-rows", &auto_rows);
    GridTracks cols = {0}, explicit_row_tracks = {0};
    expand_grid_tracks(s->grid_cols, s->grid_ncols, cw, cg, &cols);
    const GridTrack *row_source = s->grid_rows; int sized_rows = s->grid_nrows;
    bool automatic_rows = false;
    for (int i = 0; i < s->grid_nrows; i++) if (s->grid_rows[i].repeat_span) { automatic_rows = true; break; }
    if (automatic_rows) {
        expand_grid_tracks(s->grid_rows, s->grid_nrows, chdef, rg, &explicit_row_tracks);
        row_source = explicit_row_tracks.v; sized_rows = explicit_row_tracks.n;
    }
    int sized_columns = cols.n;
    if (s->grid_areas) { int an = area_cols(s->grid_areas); while (cols.n < an) { GridTrack t = grid_auto_track(auto_cols, n_auto_cols, cols.n - sized_columns); vec_push(cols, t); } }
    int explicit_columns = cols.n;
    int explicit_rows = LMAX(sized_rows, s->grid_areas ? area_rows(s->grid_areas) : 0);
    int first_column = 0, last_column = explicit_columns, first_row = 0, last_row = explicit_rows, auto_span = 1;
    /* Grid 8.5: establish both ends of the implicit grid before auto-placement.
       Negative lines remain relative to the explicit grid, then every item is
       shifted by the number of leading implicit tracks. */
    for (Box *child = b->first; child; child = child->next) if (!child->abs) {
        GridPosition col = grid_axis_position(child->st->grid_column, explicit_columns);
        GridPosition row = grid_axis_position(child->st->grid_row, explicit_rows);
        if (col.definite) {
            first_column = LMIN(first_column, col.start);
            last_column = LMAX(last_column, LMIN(1000, col.start + col.span));
        } else auto_span = LMAX(auto_span, col.span);
        if (row.definite) {
            first_row = LMIN(first_row, row.start);
            last_row = LMAX(last_row, row.start + row.span);
        }
    }
    int leading_columns = -first_column, leading_rows = -first_row;
    int needed_columns = LMAX(last_column - first_column, auto_span);
    int existing_columns = cols.n;
    vec_reserve(cols, needed_columns);
    if (leading_columns) memmove(cols.v + leading_columns, cols.v, (size_t)existing_columns * sizeof *cols.v);
    cols.n = needed_columns;
    /* Grid 7.6: repeat forwards after the last explicitly sized track and
       backwards before the explicit grid; no extra end track is introduced
       solely because all definite items occupy negative positions. */
    for (int i = 0; i < leading_columns; i++) cols.v[i] = grid_auto_track(auto_cols, n_auto_cols, i - leading_columns);
    for (int i = leading_columns + existing_columns; i < cols.n; i++) cols.v[i] = grid_auto_track(auto_cols, n_auto_cols, i - leading_columns - sized_columns);
    nc = cols.n;
    /* items + placement */
    typedef struct { Box *b; int r, c, rs, cs; } GI;
    VEC(GI) items = {0};
    Occ oc = { NULL, 0, nc };
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs) { c->sx = cx; c->sy = cy; add_abs(L, c); continue; }
        c->bfc = true;
        const ComputedStyle *is = c->st;
        GridPosition column = grid_axis_position(is->grid_column, explicit_columns);
        GridPosition row = grid_axis_position(is->grid_row, explicit_rows);
        GI g = { c, -1, -1, row.span, LMIN(column.span, nc) };
        if (s->grid_areas && is->grid_area && area_lookup(s->grid_areas, is->grid_area, &g.r, &g.c, &g.rs, &g.cs)) {
            g.c += leading_columns; g.r += leading_rows;
        } else {
            if (column.definite) g.c = LMIN(column.start + leading_columns, nc - 1);
            if (row.definite) g.r = row.start + leading_rows;
        }
        if (g.c >= 0 && g.c + g.cs > nc) g.cs = nc - g.c;
        if (g.r >= 0 && g.c >= 0) occ_mark(&oc, g.r, g.c, g.rs, g.cs);
        vec_push(items, g);
    }
    for (int k = 0; k < items.n; k++) {
        GI *g = &items.v[k];
        if (g->r < 0 || g->c >= 0) continue;
        int x = 0; while (x + g->cs <= nc && !occ_free(&oc, g->r, x, g->rs, g->cs)) x++;
        g->c = x + g->cs <= nc ? x : 0; occ_mark(&oc, g->r, g->c, g->rs, g->cs);
    }
    int ar = 0, ac = 0;
    for (int k = 0; k < items.n; k++) {
        GI *g = &items.v[k];
        if (g->r >= 0) continue;
        if (g->c >= 0) { if (g->c < ac) ar++; while (!occ_free(&oc, ar, g->c, g->rs, g->cs)) ar++; }
        else { for (;;) { if (ac + g->cs > nc) { ar++; ac = 0; } if (occ_free(&oc, ar, ac, g->rs, g->cs)) break; ac++; } g->c = ac; }
        g->r = ar; occ_mark(&oc, g->r, g->c, g->rs, g->cs); ac = g->c + g->cs;
    }
    free(oc.v);
    /* column sizes: kind 0=auto 1=fixed 2=fr 3=minmax(len,len) */
    float *cwid = xcalloc((size_t)nc * 2, sizeof(float)), *cap = xcalloc((size_t)nc, sizeof(float));
    float *cgaps = cwid + nc, *minimum = xcalloc((size_t)nc, sizeof(float));
    uint8_t *kind = xcalloc((size_t)nc, 1);
    for (int i = 0; i < nc; i++) if (cols.v[i].repeat_kind == GRID_REPEAT_FIT) kind[i] = 4;
    for (int k = 0; k < items.n; k++) for (int i = items.v[k].c; i < items.v[k].c + items.v[k].cs; i++) kind[i] = 0;
    float fixed = 0, frsum = 0; int nauto = 0, ngrow = 0, active_columns = 0;
    for (int i = 0; i < nc; i++) {
        if (kind[i] != 4) active_columns++;
        if (i < nc - 1 && kind[i] != 4 && kind[i + 1] != 4) fixed += cgaps[i] = cg;
    }
    for (int i = 0; i < nc; i++) {
        GridTrack *t = &cols.v[i];
        if (kind[i] == 4) continue;
        if (t->fr > 0) { kind[i] = 2; frsum += t->fr; cwid[i] = t->min.kind == LK_LEN ? res(t->min, cw) : 0; fixed += cwid[i]; continue; }
        if (t->size.kind == LK_LEN && t->min.kind == LK_LEN && res(t->min, cw) < res(t->size, cw)) { kind[i] = 3; cwid[i] = res(t->min, cw); cap[i] = res(t->size, cw); fixed += cwid[i]; ngrow++; continue; }
        if (t->min.kind == LK_MIN_CONTENT && t->size.kind == LK_LEN) {   /* minmax(auto, len): at least the items' automatic minimum */
            float mn = 0;
            for (int k = 0; k < items.n; k++) if (items.v[k].c == i && items.v[k].cs == 1) mn = LMAX(mn, grid_item_auto_min(L, items.v[k].b, res(t->size, cw)));
            kind[i] = 1; cwid[i] = LMAX(mn, res(t->size, cw)); fixed += cwid[i]; continue;
        }
        float f = track_fixed(t, cw);
        if (f >= 0) { kind[i] = 1; cwid[i] = f; fixed += f; continue; }
        float mn = t->min.kind == LK_LEN && len_def(t->min, cw) ? res(t->min, cw) : 0, mx = mn;
        for (int k = 0; k < items.n; k++) if (items.v[k].c == i && items.v[k].cs == 1) {
            float a, z; intrinsic_outer(L, items.v[k].b, &a, &z);
            if (t->min.kind != LK_LEN) mn = LMAX(mn, a);
            mx = LMAX(mx, t->size.kind == LK_MIN_CONTENT ? a : z);
        }
        cwid[i] = LMAX(mn, mx); minimum[i] = mn; fixed += cwid[i];
        if (t->size.kind == LK_AUTO) nauto++;
        else kind[i] = 1; /* intrinsic maxima do not stretch with alignment */
    }
    float left = cw - fixed;
    for (int pass = 0; pass < 4 && left > 0.01f && ngrow; pass++) {
        int n = 0; for (int i = 0; i < nc; i++) if (kind[i] == 3 && cwid[i] < cap[i]) n++;
        if (!n) break;
        float share = left / n;
        for (int i = 0; i < nc; i++) if (kind[i] == 3 && cwid[i] < cap[i]) { float add = LMIN(share, cap[i] - cwid[i]); cwid[i] += add; left -= add; }
    }
    if (frsum > 0 && left > 0) { for (int i = 0; i < nc; i++) if (kind[i] == 2) cwid[i] += left * cols.v[i].fr / LMAX(1, frsum); }
    else if (nauto && left > 0 && (s->justify_content == AL_NORMAL || s->justify_content == AL_STRETCH)) { for (int i = 0; i < nc; i++) if (kind[i] == 0) cwid[i] += left / nauto; }
    else if (left < 0 && nauto) {
        float need = -left;
        for (int pass = 0; pass < nc && need > .01f; pass++) {
            int shrinkable = 0;
            for (int i = 0; i < nc; i++) if (kind[i] == 0 && cwid[i] > minimum[i] + .01f) shrinkable++;
            if (!shrinkable) break;
            float share = need / shrinkable;
            for (int i = 0; i < nc; i++) if (kind[i] == 0) {
                float take = LMIN(share, LMAX(0, cwid[i] - minimum[i])); cwid[i] -= take; need -= take;
            }
        }
    }
    free(cap); free(minimum);
    float grid_width = 0;
    for (int i = 0; i < nc; i++) grid_width += cwid[i] + cgaps[i];
    char justify_buf[64]; float xoff = 0, column_extra = 0;
    const char *justify = grid_alignment(s, "justify-content", s->justify_content, justify_buf);
    bool rtl = !s->writing_mode && s->direction;
    grid_content_alignment(justify, rtl, active_columns, cw - grid_width, &xoff, &column_extra);
    int remaining_columns = active_columns;
    for (int i = 0; i < nc; i++) if (kind[i] != 4 && --remaining_columns > 0) cgaps[i] += column_extra;
    cg += column_extra; grid_width += column_extra * LMAX(0, active_columns - 1);
    free(kind);
    float *cx0 = xcalloc((size_t)nc + 1, sizeof(float));
    for (int i = 0; i < nc; i++) cx0[i + 1] = cx0[i] + cwid[i] + cgaps[i];
    int nr = 0; for (int k = 0; k < items.n; k++) nr = LMAX(nr, items.v[k].r + items.v[k].rs);
    nr = LMAX(nr, last_row + leading_rows);
    float *rh = xcalloc(((size_t)nr + 1) * 2, sizeof(float)), *rgaps = rh + nr + 1;
    GridTrack *rows = xcalloc((size_t)nr + 1, sizeof(GridTrack));
    enum { REPEAT_COLLAPSED = 3 };
    for (int i = 0; i < nr; i++) {
        int logical_row = i - leading_rows;
        rows[i] = logical_row >= 0 && logical_row < sized_rows ? row_source[logical_row] : grid_auto_track(auto_rows, n_auto_rows, logical_row < 0 ? logical_row : logical_row - sized_rows);
        if (rows[i].repeat_kind == GRID_REPEAT_FIT) rows[i].repeat_kind = REPEAT_COLLAPSED;
    }
    for (int k = 0; k < items.n; k++) for (int i = items.v[k].r; i < items.v[k].r + items.v[k].rs; i++)
        if (rows[i].repeat_kind == REPEAT_COLLAPSED) rows[i].repeat_kind = GRID_REPEAT_FIT;
    float row_gutters = 0; int active_rows = 0;
    for (int i = 0; i < nr; i++) {
        if (rows[i].repeat_kind == REPEAT_COLLAPSED) { rows[i].size = L_px(0); rows[i].min = L_auto(); rows[i].fr = 0; }
        else active_rows++;
        if (i < nr - 1 && rows[i].repeat_kind != REPEAT_COLLAPSED && rows[i + 1].repeat_kind != REPEAT_COLLAPSED) row_gutters += rgaps[i] = rg;
        float fixed_height = track_fixed(&rows[i], chdef);
        if (fixed_height >= 0) rh[i] = fixed_height;
        else if (rows[i].min.kind == LK_LEN && len_def(rows[i].min, chdef)) rh[i] = res(rows[i].min, chdef);
    }
    /* layout items to find row heights */
    for (int k = 0; k < items.n; k++) {
        GI *g = &items.v[k]; Box *c = g->b;
        float w = cx0[g->c + g->cs] - cx0[g->c] - (g->c + g->cs < nc ? cgaps[g->c + g->cs - 1] : 0);
        if (g->c + g->cs == nc) w = cx0[nc] - cx0[g->c];
        compute_mbp(c, w);
        char align_buf[64]; const char *ja = grid_self_alignment(c->st, s, true, align_buf);
        bool stretch = grid_stretches(c, true, ja);
        float forced_w = grid_stretch_size(c, b, true, w - c->m[1] - c->m[3], w);
        float measure_height = -1;
        bool sizing_height = c->st->height.kind == LK_STRETCH || c->st->min_height.kind == LK_STRETCH ||
                             c->st->max_height.kind == LK_STRETCH;
        if (sizing_height) {
            float fixed_height = 0; bool definite = true;
            for (int r = g->r; r < g->r + g->rs; r++) {
                float h = track_fixed(&rows[r], chdef);
                if (h < 0) { definite = false; break; }
                fixed_height += h + (r < g->r + g->rs - 1 ? rgaps[r] : 0);
            }
            if (definite) measure_height = fixed_height;
        }
        /* An intrinsic row remains indefinite while contributions are
         * measured. Fixed spanned tracks already provide an area basis. */
        layout_box(L, c, cx + cx0[g->c] + c->m[3], cy, w, measure_height, NULL, stretch ? SZ_FORCED : SZ_SHRINK, forced_w, -1);
        if (g->rs == 1 && track_fixed(&rows[g->r], chdef) < 0)
            rh[g->r] = LMAX(rh[g->r], c->h + c->m[0] + c->m[2]);
    }
    /* Flexible tracks divide the remaining definite height. A row whose
       intrinsic minimum exceeds its share freezes before redistribution.
       A total flex factor below one intentionally leaves unused space. */
    float row_fractions = 0, available_height = chdef - row_gutters;
    for (int i = 0; i < nr; i++) {
        if (rows[i].fr > 0) row_fractions += rows[i].fr;
        else available_height -= rh[i];
    }
    if (row_fractions > 0 && chdef >= 0) {
        uint8_t *frozen = xcalloc((size_t)nr, 1);
        for (int pass = 0; pass <= nr; pass++) {
            float fraction = LMAX(0, available_height) / LMAX(1, row_fractions);
            bool changed = false;
            for (int i = 0; i < nr; i++) if (rows[i].fr > 0 && !frozen[i] && rh[i] > fraction * rows[i].fr + .01f) {
                frozen[i] = 1; available_height -= rh[i]; row_fractions -= rows[i].fr; changed = true;
            }
            if (!changed) {
                for (int i = 0; i < nr; i++) if (rows[i].fr > 0 && !frozen[i]) rh[i] = LMAX(rh[i], fraction * rows[i].fr);
                break;
            }
        }
        free(frozen);
    } else if (row_fractions > 0) {
        float fraction = 0;
        for (int i = 0; i < nr; i++) if (rows[i].fr > 0) fraction = LMAX(fraction, rh[i] / LMAX(1, rows[i].fr));
        for (int i = 0; i < nr; i++) if (rows[i].fr > 0) rh[i] = LMAX(rh[i], fraction * rows[i].fr);
    }
    /* align-content: normal/stretch grows auto rows to fill a definite height */
    if (chdef >= 0 && nr && (s->align_content == AL_NORMAL || s->align_content == AL_STRETCH)) {
        float used = row_gutters; int na = 0;
        for (int i = 0; i < nr; i++) { used += rh[i]; if (!rows[i].fr && rows[i].size.kind == LK_AUTO) na++; }
        if (na && chdef - used > 0.01f) for (int i = 0; i < nr; i++)
            if (!rows[i].fr && rows[i].size.kind == LK_AUTO) rh[i] += (chdef - used) / na;
    }
    float grid_height = row_gutters;
    for (int i = 0; i < nr; i++) grid_height += rh[i];
    char align_content_buf[64]; float yoff = 0, row_extra = 0;
    const char *align_content = grid_alignment(s, "align-content", s->align_content, align_content_buf);
    grid_content_alignment(align_content, false, active_rows, chdef >= 0 ? chdef - grid_height : 0, &yoff, &row_extra);
    int remaining_rows = active_rows;
    for (int i = 0; i < nr; i++) if (rows[i].repeat_kind != REPEAT_COLLAPSED && --remaining_rows > 0) rgaps[i] += row_extra;
    rg += row_extra;
    float *ry = xcalloc((size_t)nr + 1, sizeof(float));
    for (int i = 0; i < nr; i++) ry[i + 1] = ry[i] + rh[i] + rgaps[i];
    for (int k = 0; k < items.n; k++) {
        GI *g = &items.v[k]; Box *c = g->b;
        float top = ry[g->r], hh = ry[g->r + g->rs] - top - (g->r + g->rs < nr ? rgaps[g->r + g->rs - 1] : 0);
        float w = cx0[g->c + g->cs] - cx0[g->c] - (g->c + g->cs < nc ? cgaps[g->c + g->cs - 1] : 0);
        char justify_self_buf[64], align_self_buf[64];
        const char *ja = grid_self_alignment(c->st, s, true, justify_self_buf);
        const char *al = grid_self_alignment(c->st, s, false, align_self_buf);
        bool stretch_x = grid_stretches(c, true, ja), stretch_y = grid_stretches(c, false, al);
        bool sizing_height = c->st->height.kind == LK_STRETCH || c->st->min_height.kind == LK_STRETCH ||
                             c->st->max_height.kind == LK_STRETCH;
        if (stretch_y || sizing_height || len_has_pct(c->st->height) || len_has_pct(c->st->min_height) || len_has_pct(c->st->max_height)) {
            float fw = grid_stretch_size(c, b, true, w - c->m[1] - c->m[3], w);
            float fh = stretch_y ? grid_stretch_size(c, b, false, hh - c->m[0] - c->m[2], hh) : -1;
            layout_box(L, c, cx + cx0[g->c] + c->m[3], cy, w, hh, NULL, stretch_x ? SZ_FORCED : SZ_SHRINK, fw, fh);
        }
        float x = cx + xoff + (rtl ? grid_width - cx0[g->c] - w : cx0[g->c]);
        x += grid_item_offset(c, true, ja, rtl, w);
        float y = cy + yoff + top + grid_item_offset(c, false, al, false, hh);
        box_translate(c, x - c->x, y - c->y);
    }
    float total = ry[nr];
    if (!b->grid_used) b->grid_used = xcalloc(1, sizeof *b->grid_used);
    else { free(b->grid_used->size[0]); free(b->grid_used->size[1]); }
    *b->grid_used = (GridUsedTracks){
        .size = {cwid, rh}, .gap_after = {cgaps, rgaps}, .count = {items.n || explicit_columns ? nc : 0, nr},
        .explicit_start = {leading_columns, leading_rows},
        .explicit_count = {explicit_columns, explicit_rows},
        .template_count = {sized_columns, sized_rows},
        .offset = {xoff, yoff}, .gap = {cg, rg}
    };
    free(cx0); free(ry); free(rows); free(auto_cols); free(auto_rows); vec_free(items); vec_free(cols); vec_free(explicit_row_tracks);
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
static float spacing(const Box *t, int axis) { return t->st->border_collapse ? 0 : t->st->border_spacing[axis]; }
static void col_widths(Layout *L, TGrid *g, float *mn, float *mx) {
    for (int i = 0; i < g->cells.n; i++) { TCell *c = &g->cells.v[i]; if (c->cs != 1) continue; float a, z; intrinsic(L, c->cell, &a, &z); mn[c->c] = LMAX(mn[c->c], a); mx[c->c] = LMAX(mx[c->c], z); }
    for (int i = 0; i < g->cells.n; i++) { TCell *c = &g->cells.v[i]; if (c->cs == 1) continue; float a, z; intrinsic(L, c->cell, &a, &z); float sa = 0, sz = 0; for (int k = c->c; k < c->c + c->cs && k < g->ncols; k++) { sa += mn[k]; sz += mx[k]; } if (a > sa) for (int k = c->c; k < c->c + c->cs && k < g->ncols; k++) mn[k] += (a - sa) / c->cs; if (z > sz) for (int k = c->c; k < c->c + c->cs && k < g->ncols; k++) mx[k] += (z - sz) / c->cs; }
    for (int k = 0; k < g->ncols; k++) if (mx[k] < mn[k]) mx[k] = mn[k];
}
void table_intrinsic(Layout *L, Box *b, float *omn, float *omx) {
    TGrid g; build_grid(b, &g);
    float *mn = xcalloc((size_t)g.ncols + 1, sizeof(float)), *mx = xcalloc((size_t)g.ncols + 1, sizeof(float));
    col_widths(L, &g, mn, mx);
    float sp = spacing(b, 0), a = g.ncols ? sp * (g.ncols + 1) : 0, z = a;
    for (int k = 0; k < g.ncols; k++) { a += mn[k]; z += mx[k]; }
    *omn = a + hbp(b); *omx = z + hbp(b);
    free(mn); free(mx); vec_free(g.cells); vec_free(g.rows); vec_free(g.captions);
}
float layout_table(Layout *L, Box *b, float cx, float cy, float cw, float chdef) {
    TGrid g; build_grid(b, &g);
    int nc = g.ncols, nr = g.rows.n;
    float spx = nc ? spacing(b, 0) : 0, spy = nr ? spacing(b, 1) : 0;
    float *mn = xcalloc((size_t)nc + 1, sizeof(float)), *mx = xcalloc((size_t)nc + 1, sizeof(float));
    col_widths(L, &g, mn, mx);
    float smn = 0, smx = 0; for (int k = 0; k < nc; k++) { smn += mn[k]; smx += mx[k]; }
    float avail = cw - spx * (nc + 1);
    bool specified = b->st->width.kind == LK_LEN || b->st->width.kind == LK_STRETCH;
    /* The outer sizing path already resolved stretch and minimum widths.
     * Preserve that used width instead of shrinking columns to max-content. */
    bool has_min = b->st->min_width.kind == LK_STRETCH ||
                   (b->st->min_width.kind == LK_LEN && (b->st->min_width.px > 0 || b->st->min_width.pct > 0));
    float target = specified || has_min ? avail : LMIN(avail, smx);
    if (target < smn) target = smn;
    float *w = xcalloc((size_t)nc + 1, sizeof(float));
    for (int k = 0; k < nc; k++) {
        if (target >= smx) w[k] = mx[k] + (smx > 0 ? (target - smx) * mx[k] / smx : (target - smx) / nc);
        else w[k] = mn[k] + (smx > smn ? (target - smn) * (mx[k] - mn[k]) / (smx - smn) : 0);
    }
    float *x0 = xcalloc((size_t)nc + 2, sizeof(float));
    x0[0] = spx; for (int k = 0; k < nc; k++) x0[k + 1] = x0[k] + w[k] + spx;
    float tw = x0[nc];
    /* captions */
    float y = 0;
    for (int i = 0; i < g.captions.n; i++) { Box *c = g.captions.v[i]; layout_box(L, c, cx, cy + y, tw, -1, NULL, SZ_FORCED, tw, -1); y += c->h + c->m[0] + c->m[2]; }
    float *rh = xcalloc((size_t)nr + 1, sizeof(float));
    for (int i = 0; i < g.cells.n; i++) {
        TCell *c = &g.cells.v[i];
        float cwid = x0[LMIN(c->c + c->cs, nc)] - x0[c->c] - spx;
        layout_box(L, c->cell, cx + x0[c->c], cy, cwid, -1, NULL, SZ_FORCED, cwid, -1);
        float need = c->cell->h;
        if (c->rs == 1 && c->r < nr) rh[c->r] = LMAX(rh[c->r], need);
    }
    for (int r = 0; r < nr; r++) { Box *row = g.rows.v[r]; if (row->st->height.kind == LK_LEN && !row->st->height.pct) rh[r] = LMAX(rh[r], row->st->height.px); }
    float *ry = xcalloc((size_t)nr + 2, sizeof(float));
    ry[0] = y + spy; for (int r = 0; r < nr; r++) ry[r + 1] = ry[r] + rh[r] + spy;
    if (chdef > ry[nr] && nr) { float extra = (chdef - ry[nr]) / nr; for (int r = 0; r < nr; r++) rh[r] += extra; for (int r = 0; r < nr; r++) ry[r + 1] = ry[r] + rh[r] + spy; }
    for (int i = 0; i < g.cells.n; i++) {
        TCell *c = &g.cells.v[i]; Box *cell = c->cell;
        int re = LMIN(c->r + c->rs, nr);
        float h = ry[re] - ry[c->r] - spy;
        float content = cell->st->height.kind == LK_LEN ? LMIN(cell->h, cell->scroll_h + vbp(cell)) : cell->h;   /* specified height only sets the row minimum; align the real content */
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
    for (int r = 0; r < nr; r++) { Box *row = g.rows.v[r]; if (row->st->display != D_TABLE_ROW) continue; row->x = cx; row->y = cy + ry[r]; row->w = tw; row->h = rh[r];
        for (int i = 0; i < 4; i++) row->b[i] = row->st->border_style[i] > BS_HIDDEN ? row->st->border_width[i] : 0; }
    for (Box *c = b->first; c; c = c->next) if (is_group(c) && c->first) { c->x = cx; c->y = c->first->y; Box *l = c->last; c->w = tw; c->h = l->y + l->h - c->y; }
    float total = ry[nr];
    if (b->st->width.kind != LK_LEN) { float adj = tw - cw; (void)adj; }
    b->w = tw + hbp(b);
    free(mn); free(mx); free(w); free(x0); free(rh); free(ry);
    vec_free(g.cells); vec_free(g.rows); vec_free(g.captions);
    return total;
}
