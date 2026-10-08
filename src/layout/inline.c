/* Inline formatting context: line breaking and line box construction */
#include "layout.h"
#include <math.h>

bool text_break_after(const Box *t, int gi);

typedef struct { Box *box; float start; int line; } OpenInline;
typedef struct {
    Layout *L; Box *blk; FloatCtx *fc;
    float x0, W, y, left, right, pen;
    float A, D;           /* ascent/descent of current line relative to baseline */
    int line_f0;          /* first frag index of current line */
    bool line_has_content;
    OpenInline open[64]; int nopen;
    int ir0;              /* unused */
    float strutA, strutD;
    bool first_line;
} IC;

static TextFrag *push_frag(Box *b) {
    if (b->nfrags == b->fcap) { b->fcap = b->fcap ? b->fcap * 2 : 16; TextFrag *n = xrealloc(b->frags, sizeof *n * (size_t)b->fcap); b->frags = n; }
    TextFrag *f = &b->frags[b->nfrags++]; memset(f, 0, sizeof *f); return f;
}
static void push_ir(Box *ib, IRect r) {
    if (ib->nir == ib->ircap) { ib->ircap = ib->ircap ? ib->ircap * 2 : 4; ib->ir = xrealloc(ib->ir, sizeof(IRect) * (size_t)ib->ircap); }
    ib->ir[ib->nir++] = r;
}

static void metrics_for(const ComputedStyle *st, Font *f, float *A, float *D) {
    float asc = f ? f->ascent : st->font_size * 0.8f, desc = f ? f->descent : st->font_size * 0.2f;
    float lh = style_line_height(st, f);
    float half = (lh - (asc + desc)) / 2;
    *A = asc + half; *D = desc + half;
}

static void line_avail(IC *c) {
    float l = c->x0, r = c->x0 + c->W;
    if (c->fc) fc_avail(c->fc, c->y, LMAX(1, c->strutA + c->strutD), c->x0, c->x0 + c->W, &l, &r);
    c->left = l; c->right = r; c->pen = l;
    if (c->first_line) c->pen += c->blk->st->text_indent;
}

static void new_line_state(IC *c) {
    c->A = c->strutA; c->D = c->strutD; c->line_f0 = c->blk->nfrags; c->line_has_content = false;
    line_avail(c);
    for (int i = 0; i < c->nopen; i++) { c->open[i].start = c->pen; c->open[i].line = c->blk->nlines; }
}

static float valign_shift(const ComputedStyle *st, const ComputedStyle *pst) {
    switch (st->vertical_align) {
    case VA_SUB: return pst->font_size * 0.2f;
    case VA_SUPER: return -pst->font_size * 0.35f;
    case VA_LENGTH: return -st->vertical_align_len;
    default: return 0;
    }
}

static void finish_line(IC *c, bool forced) {
    Box *b = c->blk;
    /* close open inline rects for this line */
    for (int i = 0; i < c->nopen; i++) {
        OpenInline *o = &c->open[i];
        IRect r = { o->start, 0, c->pen - o->start, 0, false, false, b->nlines };
        r.first = o->box->nir == 0;
        push_ir(o->box, r);
    }
    if (!c->line_has_content && !forced) { new_line_state(c); return; }
    /* trim trailing collapsible space */
    if (b->nfrags > c->line_f0) {
        TextFrag *f = &b->frags[b->nfrags - 1];
        if (f->box->kind == BX_TEXT && f->g1 > f->g0) {
            Box *t = f->box;
            int ws = t->st->white_space;
            if ((ws == WS_NORMAL || ws == WS_NOWRAP || ws == WS_PRE_LINE) && t->text[t->sh.g[f->g1 - 1].cluster] == ' ') { f->w -= t->sh.g[f->g1 - 1].adv; c->pen -= t->sh.g[f->g1 - 1].adv; f->g1--; }
        }
    }
    float lh = c->A + c->D;
    float base = c->y + c->A;
    float used = c->pen - c->left, avail = c->right - c->left;
    float shift = 0; int ta = b->st->text_align;
    if (b->st->direction && ta == TA_START) ta = TA_RIGHT;
    if (ta == TA_END) ta = b->st->direction ? TA_LEFT : TA_RIGHT;
    if (avail > used) { if (ta == TA_CENTER) shift = (avail - used) / 2; else if (ta == TA_RIGHT) shift = avail - used; }
    for (int i = c->line_f0; i < b->nfrags; i++) {
        TextFrag *f = &b->frags[i];
        f->x += shift;
        if (f->box->kind == BX_TEXT) {
            Font *fo = f->box->font;
            f->base = base + f->base; /* f->base held the vertical-align shift */
            f->y = f->base - (fo ? fo->ascent : 0); f->h = fo ? fo->ascent + fo->descent : f->box->st->font_size;
        } else {
            Box *a = f->box;
            float itemA = f->base; /* stored ascent part */
            float ty = base - itemA + a->m[0];
            if (a->st->vertical_align == VA_TOP) ty = c->y + a->m[0];
            else if (a->st->vertical_align == VA_BOTTOM) ty = c->y + lh - (a->h + a->m[2]);
            box_translate(a, f->x + a->m[3] - a->x, ty - a->y);
            f->y = a->y; f->h = a->h;
        }
    }
    /* inline box rects for this line */
    for (Box *ib = b->first; ib; ib = NULL) (void)ib;
    Line ln = { c->y, lh, base, c->line_f0, b->nfrags - c->line_f0 };
    if (b->nlines == b->lcap) { b->lcap = b->lcap ? b->lcap * 2 : 8; b->lines = xrealloc(b->lines, sizeof(Line) * (size_t)b->lcap); }
    b->lines[b->nlines++] = ln;
    c->y += lh; c->first_line = false;
    new_line_state(c);
}

static void fix_irects(Box *blk, Box *ib) {
    /* fill vertical geometry of inline box rects using the owning lines */
    for (Box *c = ib->first; c; c = c->next) if (c->kind == BX_INLINE) fix_irects(blk, c);
    if (ib->kind != BX_INLINE) return;
    Font *f = style_font(ib->st);
    float asc = f ? f->ascent : ib->st->font_size * 0.8f, desc = f ? f->descent : ib->st->font_size * 0.2f;
    float shift = valign_shift(ib->st, ib->parent && ib->parent->st ? ib->parent->st : ib->st);
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    for (int i = 0; i < ib->nir; i++) {
        IRect *r = &ib->ir[i];
        if (r->line >= blk->nlines) { r->w = 0; continue; }
        Line *ln = &blk->lines[r->line];
        /* apply the same horizontal alignment shift as frags on that line */
        if (ln->nf) { TextFrag *ff = &blk->frags[ln->f0]; (void)ff; }
        r->y = ln->base + shift - asc - ib->p[0] - ib->b[0];
        r->h = asc + desc + vbp(ib);
        if (r->first) { r->x += ib->m[3]; r->w -= ib->m[3]; }
        if (r->last) r->w -= ib->m[1];
        mnx = LMIN(mnx, r->x); mny = LMIN(mny, r->y); mxx = LMAX(mxx, r->x + r->w); mxy = LMAX(mxy, r->y + r->h);
    }
    if (ib->nir) { ib->x = mnx; ib->y = mny; ib->w = mxx - mnx; ib->h = mxy - mny; }
}

static void place_text(IC *c, Box *t) {
    text_ensure_shaped(t);
    const ComputedStyle *st = t->st;
    int ws = st->white_space;
    bool wrap = ws != WS_NOWRAP && ws != WS_PRE;
    bool collapsible = ws == WS_NORMAL || ws == WS_NOWRAP || ws == WS_PRE_LINE;
    float A, D; metrics_for(st, t->font, &A, &D);
    float vshift = valign_shift(st, t->parent && t->parent->st ? t->parent->st : st);
    int n = t->sh.n, i = 0;
    TextFrag *cur = NULL;
    while (i < n) {
        /* forced newline */
        if (t->text[t->sh.g[i].cluster] == '\n' && !(ws == WS_NORMAL || ws == WS_NOWRAP)) { c->A = LMAX(c->A, A - vshift); c->D = LMAX(c->D, D + vshift); finish_line(c, true); cur = NULL; i++; continue; }
        /* collect a word: glyphs up to and including a break opportunity */
        int j = i; float w = 0, trail = 0;
        while (j < n) {
            char ch = t->text[t->sh.g[j].cluster];
            if (ch == '\n' && !(ws == WS_NORMAL || ws == WS_NOWRAP)) break;
            w += t->sh.g[j].adv;
            if (!wrap) { j++; continue; }
            if (ch == ' ') { trail = t->sh.g[j].adv; j++; break; }
            if (st->word_break == 1 || text_break_after(t, j)) { j++; break; }
            j++;
        }
        bool only_space = (j - i == 1) && trail > 0;
        if (only_space && collapsible && !c->line_has_content && c->pen <= c->left + 0.01f + (c->first_line ? st->text_indent : 0)) { i = j; continue; }
        if (wrap && c->line_has_content && c->pen + w - trail > c->right + 0.01f) { finish_line(c, false); cur = NULL; if (only_space && collapsible) { i = j; continue; } }
        /* overlong word on an empty line: break inside when allowed */
        if (wrap && !c->line_has_content && c->pen + w - trail > c->right + 0.01f && (st->overflow_wrap || st->word_break)) {
            float x = c->pen; int k = i;
            while (k < j && (k == i || x + t->sh.g[k].adv <= c->right)) { x += t->sh.g[k].adv; k++; }
            if (k < j) { j = k; w = x - c->pen; trail = 0; }
        }
        if (!cur || cur->g1 != i) {
            cur = push_frag(c->blk); cur->box = t; cur->x = c->pen; cur->g0 = i; cur->g1 = i; cur->base = vshift;
        }
        cur->g1 = j; cur->w += w; c->pen += w;
        c->line_has_content = true;
        c->A = LMAX(c->A, A - vshift); c->D = LMAX(c->D, D + vshift);
        i = j;
    }
}

static void place_atomic(IC *c, Box *a) {
    layout_box(c->L, a, 0, 0, c->W, -1, NULL, SZ_SHRINK, 0, -1);
    float ow = a->w + a->m[1] + a->m[3], oh = a->h + a->m[0] + a->m[2];
    if (c->line_has_content && c->pen + ow > c->right + 0.01f) finish_line(c, false);
    float itemA;
    bool use_bl = a->has_baseline && a->fmt != FMT_REPLACED && !a->scroller;
    if (use_bl) itemA = a->baseline - a->y + a->m[0]; else itemA = oh;
    if (a->st->vertical_align == VA_MIDDLE) { Font *pf = style_font(c->blk->st); float xh = pf ? pf->x_height : 4; itemA = oh / 2 + xh / 2; }
    else if (a->st->vertical_align == VA_TEXT_TOP) itemA = c->strutA;
    itemA -= valign_shift(a->st, c->blk->st);
    TextFrag *f = push_frag(c->blk); f->box = a; f->x = c->pen; f->w = ow; f->base = itemA; f->g0 = f->g1 = -1;
    c->pen += ow; c->line_has_content = true;
    if (a->st->vertical_align != VA_TOP && a->st->vertical_align != VA_BOTTOM) { c->A = LMAX(c->A, itemA); c->D = LMAX(c->D, oh - itemA); }
    else { float need = oh - (c->A + c->D); if (need > 0) c->D += need; }
}

static void walk(IC *c, Box *p) {
    for (Box *ch = p->first; ch; ch = ch->next) {
        switch (ch->kind) {
        case BX_TEXT: place_text(c, ch); break;
        case BX_BR: { float A, D; metrics_for(ch->st, style_font(ch->st), &A, &D); c->A = LMAX(c->A, A); c->D = LMAX(c->D, D); finish_line(c, true); break; }
        case BX_INLINE: {
            compute_mbp(ch, c->W);
            ch->nir = 0;
            if (ch->abs) { ch->sx = c->pen; ch->sy = c->y; add_abs(c->L, ch); break; }
            float l = ch->m[3] + ch->b[3] + ch->p[3], r = ch->m[1] + ch->b[1] + ch->p[1];
            if (c->nopen < 64) { c->open[c->nopen].box = ch; c->open[c->nopen].start = c->pen; c->open[c->nopen].line = c->blk->nlines; c->nopen++; }
            c->pen += l;
            if (l > 0 || r > 0 || ch->first == NULL) c->line_has_content = c->line_has_content || l > 0;
            float A, D; Font *f = style_font(ch->st); metrics_for(ch->st, f, &A, &D);
            float vs = valign_shift(ch->st, p->st ? p->st : ch->st);
            c->A = LMAX(c->A, A - vs); c->D = LMAX(c->D, D + vs);
            walk(c, ch);
            c->pen += r;
            if (r > 0) c->line_has_content = true;
            /* close */
            for (int i = c->nopen - 1; i >= 0; i--) if (c->open[i].box == ch) {
                IRect rr = { c->open[i].start, 0, c->pen - c->open[i].start, 0, ch->nir == 0, true, c->blk->nlines };
                push_ir(ch, rr);
                memmove(&c->open[i], &c->open[i + 1], sizeof(OpenInline) * (size_t)(c->nopen - i - 1)); c->nopen--; break;
            }
            break;
        }
        default:
            if (ch->abs) { ch->sx = c->pen; ch->sy = c->y; add_abs(c->L, ch); break; }
            if (ch->floated) {
                layout_box(c->L, ch, 0, 0, c->W, -1, NULL, SZ_SHRINK, 0, -1);
                float fy = c->line_has_content ? c->y + c->A + c->D : c->y;
                if (c->fc) { fc_place(c->fc, ch, fy, c->x0, c->x0 + c->W); if (!c->line_has_content) { float l, r; fc_avail(c->fc, c->y, LMAX(1, c->strutA + c->strutD), c->x0, c->x0 + c->W, &l, &r); c->left = l; c->right = r; c->pen = LMAX(c->pen, l); } }
                else box_translate(ch, c->x0 + ch->m[3] - ch->x, fy + ch->m[0] - ch->y);
                break;
            }
            place_atomic(c, ch);
        }
    }
}

static void clear_frags(Box *b) {
    for (Box *c = b->first; c; c = c->next) if (c->kind == BX_INLINE) { c->nir = 0; clear_frags(c); }
}

float layout_inline(Layout *L, Box *b, float x0, float y0, float W, FloatCtx *fc) {
    b->nfrags = 0; b->nlines = 0;
    clear_frags(b);
    IC c; memset(&c, 0, sizeof c);
    c.L = L; c.blk = b; c.fc = fc; c.x0 = x0; c.W = W; c.y = y0; c.first_line = true;
    Font *f = style_font(b->st);
    metrics_for(b->st, f, &c.strutA, &c.strutD);
    new_line_state(&c);
    walk(&c, b);
    if (c.line_has_content || c.nopen) finish_line(&c, false);
    /* propagate horizontal alignment shifts into inline rects: recompute from frags is complex; rects use pen positions */
    for (Box *ch = b->first; ch; ch = ch->next) if (ch->kind == BX_INLINE) fix_irects(b, ch);
    /* nested inline boxes inside inline boxes */
    if (b->nlines) { b->has_baseline = true; b->baseline = b->lines[b->nlines - 1].base; }
    /* apply text-align shift to inline rects */
    for (int li = 0; li < b->nlines; li++) {
        Line *ln = &b->lines[li];
        if (!ln->nf) continue;
        (void)ln;
    }
    return c.y - y0;
}
