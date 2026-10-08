/* Display list construction: CSS painting order over the box tree */
#include "paint.h"
#include <math.h>

Image *(*paint_image_hook)(Node *n);
Image *(*paint_url_image_hook)(const char *url);

typedef struct { Box *b; float dx, dy; bool clip; float cx, cy, cw, ch; int order; } Deferred;
typedef VEC(Deferred) DefVec;
typedef struct {
    DisplayList *dl; Layout *L;
    float dx, dy;               /* doc -> viewport translation */
    float sx, sy;               /* page scroll */
    bool clip; float cx, cy, cw, ch;
    int order;
} PB;

static DItem *emit(PB *p, uint8_t op) { DItem it; memset(&it, 0, sizeof it); it.op = op; it.alpha = 1; vec_push(p->dl->items, it); return &p->dl->items.v[p->dl->items.n - 1]; }
static bool visible(PB *p, float x, float y, float w, float h) {
    if (x > p->dl->vw || y > p->dl->vh || x + w < 0 || y + h < 0) return false;
    if (p->clip && (x > p->cx + p->cw || y > p->cy + p->ch || x + w < p->cx || y + h < p->cy)) return false;
    return true;
}
static void radii(const Box *b, float w, float h, float r[4]) {
    for (int i = 0; i < 4; i++) r[i] = res(b->st->border_radius[i], i % 2 ? w : w);
    float f = 1;
    if (r[0] + r[1] > w) f = LMIN(f, w / (r[0] + r[1]));
    if (r[3] + r[2] > w) f = LMIN(f, w / (r[3] + r[2]));
    if (r[0] + r[3] > h) f = LMIN(f, h / (r[0] + r[3]));
    if (r[1] + r[2] > h) f = LMIN(f, h / (r[1] + r[2]));
    if (f < 1) for (int i = 0; i < 4; i++) r[i] *= f;
}

static void paint_rect_deco(PB *p, Box *b, float x, float y, float w, float h, bool l_edge, bool r_edge) {
    const ComputedStyle *s = b->st;
    if (!visible(p, x - 64, y - 64, w + 128, h + 128)) return;
    float r[4]; radii(b, w, h, r);
    if (s->has_shadow && !s->box_shadow.inset && COLOR_A(s->box_shadow.color)) {
        DItem *it = emit(p, DO_SHADOW); it->x = x + s->box_shadow.x; it->y = y + s->box_shadow.y; it->w = w; it->h = h;
        memcpy(it->r, r, sizeof r); it->color = s->box_shadow.color; it->blur = s->box_shadow.blur; it->spread = s->box_shadow.spread;
    }
    if (COLOR_A(s->bg_color)) { DItem *it = emit(p, DO_RECT); it->x = x; it->y = y; it->w = w; it->h = h; memcpy(it->r, r, sizeof r); it->color = s->bg_color; }
    if (s->bg_gradient) { DItem *it = emit(p, DO_GRADIENT); it->x = x; it->y = y; it->w = w; it->h = h; memcpy(it->r, r, sizeof r); it->grad = s->bg_gradient; }
    if (s->bg_image && paint_url_image_hook) {
        Image *im = paint_url_image_hook(s->bg_image);
        if (im && im->w && im->h) {
            float iw = (float)im->w, ih = (float)im->h;
            if (s->bg_size_kind == BGS_COVER || s->bg_size_kind == BGS_CONTAIN) { float k = s->bg_size_kind == BGS_COVER ? LMAX(w / iw, h / ih) : LMIN(w / iw, h / ih); iw *= k; ih *= k; }
            else if (s->bg_size_kind == BGS_LEN) { float a = s->bg_size[0].kind == LK_LEN ? res(s->bg_size[0], w) : -1, c = s->bg_size[1].kind == LK_LEN ? res(s->bg_size[1], h) : -1; if (a >= 0 && c >= 0) { iw = a; ih = c; } else if (a >= 0) { ih = ih * a / iw; iw = a; } else if (c >= 0) { iw = iw * c / ih; ih = c; } }
            float ox = x + res(s->bg_pos[0], w - iw), oy = y + res(s->bg_pos[1], h - ih);
            DItem *c = emit(p, DO_PUSH_CLIP); c->x = x; c->y = y; c->w = w; c->h = h; memcpy(c->r, r, sizeof r);
            bool rx = s->bg_repeat == BG_REPEAT || s->bg_repeat == BG_REPEAT_X, ry = s->bg_repeat == BG_REPEAT || s->bg_repeat == BG_REPEAT_Y;
            float x0 = ox, y0 = oy;
            if (rx && iw > 1) while (x0 > x) x0 -= iw;
            if (ry && ih > 1) while (y0 > y) y0 -= ih;
            int n = 0;
            for (float yy = y0; yy < y + h && n < 4096; yy += ih) { for (float xx = x0; xx < x + w && n < 4096; xx += iw) { if (visible(p, xx, yy, iw, ih)) { DItem *it = emit(p, DO_IMAGE); it->x = xx; it->y = yy; it->w = iw; it->h = ih; it->img = im; } n++; if (!rx || iw <= 1) break; } if (!ry || ih <= 1) break; }
            emit(p, DO_POP_CLIP);
        }
    }
    float bw[4]; bool any = false;
    for (int i = 0; i < 4; i++) { bw[i] = b->b[i]; if ((i == 3 && !l_edge) || (i == 1 && !r_edge)) bw[i] = 0; if (bw[i] > 0 && s->border_style[i] > BS_HIDDEN && COLOR_A(s->border_color[i])) any = true; else bw[i] = 0; }
    if (any) { DItem *it = emit(p, DO_BORDER); it->x = x; it->y = y; it->w = w; it->h = h; memcpy(it->r, r, sizeof r); memcpy(it->bw, bw, sizeof bw); memcpy(it->bc, s->border_color, sizeof it->bc); memcpy(it->bs, s->border_style, 4); }
    if (s->outline_style > BS_HIDDEN && s->outline_width > 0 && COLOR_A(s->outline_color)) {
        float o = s->outline_offset + s->outline_width;
        DItem *it = emit(p, DO_BORDER); it->x = x - o; it->y = y - o; it->w = w + 2 * o; it->h = h + 2 * o;
        for (int i = 0; i < 4; i++) { it->bw[i] = s->outline_width; it->bc[i] = s->outline_color; it->bs[i] = s->outline_style; }
    }
}

static void emit_glyphs(PB *p, Box *t, TextFrag *f, float ox, float oy, Color col) {
    DItem *it = emit(p, DO_TEXT);
    int n = f->g1 - f->g0;
    it->g = arena_alloc(&p->dl->arena, sizeof(DGlyph) * (size_t)n); it->ng = 0;
    float x0 = t->sh.g[f->g0].x;
    for (int i = f->g0; i < f->g1; i++) {
        GlyphPos *g = &t->sh.g[i];
        char ch = t->text[g->cluster];
        if (ch == ' ' || ch == '\n') continue;
        DGlyph *d = &it->g[it->ng++]; d->gid = g->gid; d->font = g->font; d->x = f->x + (g->x - x0) + p->dx + ox; d->y = f->base + g->y + p->dy + oy;
    }
    it->color = col; it->x = f->x + p->dx + ox; it->y = f->y + p->dy + oy; it->w = f->w; it->h = f->h;
}

static void paint_text_frag(PB *p, TextFrag *f) {
    Box *t = f->box; const ComputedStyle *s = t->st;
    if (s->visibility != VIS_VISIBLE || f->g1 <= f->g0) return;
    if (!visible(p, f->x + p->dx, f->y + p->dy, f->w, f->h)) return;
    Color col = s->color;
    Box *blk = t->parent; while (blk && blk->kind == BX_INLINE) blk = blk->parent;
    if (blk && blk->placeholder) col = RGBA(117, 117, 117, COLOR_A(col));
    if (s->has_text_shadow && COLOR_A(s->text_shadow.color)) emit_glyphs(p, t, f, s->text_shadow.x, s->text_shadow.y, s->text_shadow.color);
    emit_glyphs(p, t, f, 0, 0, col);
    /* text-decoration may come from an ancestor inline */
    uint8_t deco = s->text_decoration; Color dc = s->color;
    for (Box *a = t->parent; a && !deco; a = a->parent) { if (a->st && a->st->text_decoration) { deco = a->st->text_decoration; dc = a->st->color; } if (a->kind != BX_INLINE) break; }
    if (deco && t->font) {
        float th = LMAX(1, t->font->underline_thick);
        float x = f->x + p->dx, w = f->w;
        if (deco & TD_UNDERLINE) { DItem *it = emit(p, DO_LINE); it->x = x; it->y = f->base + p->dy - t->font->underline_pos; it->w = w; it->h = th; it->color = dc; }
        if (deco & TD_LINE_THROUGH) { DItem *it = emit(p, DO_LINE); it->x = x; it->y = f->base + p->dy - t->font->x_height / 2; it->w = w; it->h = th; it->color = dc; }
        if (deco & TD_OVERLINE) { DItem *it = emit(p, DO_LINE); it->x = x; it->y = f->y + p->dy; it->w = w; it->h = th; it->color = dc; }
    }
}

static void paint_marker(PB *p, Box *b) {
    const ComputedStyle *s = b->st;
    if (s->list_style == LST_NONE || !b->nlines) return;
    Font *f = style_font(s);
    float base = b->lines[0].base + p->dy, x = b->x + p->dx;
    if (s->list_style == LST_DISC || s->list_style == LST_CIRCLE || s->list_style == LST_SQUARE) {
        float d = LMAX(4, s->font_size * 0.33f), cy = base - (f ? f->x_height / 2 : d / 2);
        DItem *it = emit(p, s->list_style == LST_CIRCLE ? DO_BORDER : DO_RECT);
        it->x = x - d - s->font_size * 0.5f; it->y = cy - d / 2; it->w = d; it->h = d; it->color = s->color;
        if (s->list_style != LST_SQUARE) for (int i = 0; i < 4; i++) it->r[i] = d / 2;
        if (it->op == DO_BORDER) for (int i = 0; i < 4; i++) { it->bw[i] = 1; it->bc[i] = s->color; it->bs[i] = BS_SOLID; }
        return;
    }
    char buf[32]; int n = b->list_index;
    if (s->list_style == LST_LOWER_ALPHA || s->list_style == LST_UPPER_ALPHA) snprintf(buf, sizeof buf, "%c.", (s->list_style == LST_LOWER_ALPHA ? 'a' : 'A') + (n - 1) % 26);
    else if (s->list_style == LST_LOWER_ROMAN || s->list_style == LST_UPPER_ROMAN) {
        static const char *R[] = { "m","cm","d","cd","c","xc","l","xl","x","ix","v","iv","i" }; static const int V[] = { 1000,900,500,400,100,90,50,40,10,9,5,4,1 };
        int k = 0; for (int i = 0; i < 13 && k < 24; i++) while (n >= V[i] && k < 24) { for (const char *c = R[i]; *c; c++) buf[k++] = s->list_style == LST_UPPER_ROMAN ? (char)(*c - 32) : *c; n -= V[i]; }
        buf[k++] = '.'; buf[k] = 0;
    } else snprintf(buf, sizeof buf, "%d.", n);
    ShapedRun r; text_shape(f, buf, strlen(buf), 0, &r);
    DItem *it = emit(p, DO_TEXT); it->g = arena_alloc(&p->dl->arena, sizeof(DGlyph) * (size_t)LMAX(1, r.n)); it->color = s->color;
    float x0 = x - r.width - s->font_size * 0.4f;
    for (int i = 0; i < r.n; i++) { DGlyph *d = &it->g[it->ng++]; d->gid = r.g[i].gid; d->font = r.g[i].font; d->x = x0 + r.g[i].x; d->y = base + r.g[i].y; }
    shaped_free(&r);
}

static void paint_replaced(PB *p, Box *b) {
    float x = b->x + p->dx + b->b[3] + b->p[3], y = b->y + p->dy + b->b[0] + b->p[0], w = b->w - hbp(b), h = b->h - vbp(b);
    if (!visible(p, x, y, w, h)) return;
    if (b->ctl == CTL_CHECK || b->ctl == CTL_RADIO) {
        bool on = b->node && (b->node->checked_override ? b->node->checked_override > 0 : node_has_attr(b->node, "checked"));
        DItem *it = emit(p, on ? DO_RECT : DO_BORDER); it->x = x; it->y = y; it->w = w; it->h = h;
        float rr = b->ctl == CTL_RADIO ? w / 2 : 2; for (int i = 0; i < 4; i++) { it->r[i] = rr; it->bw[i] = 1; it->bc[i] = RGBA(118, 118, 118, 255); it->bs[i] = BS_SOLID; }
        it->color = RGBA(0, 117, 255, 255);
        if (on) { DItem *d = emit(p, DO_RECT); float k = b->ctl == CTL_RADIO ? w * 0.3f : w * 0.25f; d->x = x + k; d->y = y + k; d->w = w - 2 * k; d->h = h - 2 * k; d->color = RGBA(255, 255, 255, 255); for (int i = 0; i < 4; i++) d->r[i] = b->ctl == CTL_RADIO ? d->w / 2 : 1; }
        return;
    }
    if (b->ctl == CTL_RANGE) { DItem *t = emit(p, DO_RECT); t->x = x; t->y = y + h / 2 - 2; t->w = w; t->h = 4; t->color = RGBA(0, 117, 255, 255); for (int i = 0; i < 4; i++) t->r[i] = 2; DItem *k = emit(p, DO_RECT); k->x = x + w / 2 - 8; k->y = y; k->w = 16; k->h = 16; k->color = RGBA(0, 117, 255, 255); for (int i = 0; i < 4; i++) k->r[i] = 8; return; }
    Image *im = paint_image_hook && b->node ? paint_image_hook(b->node) : NULL;
    if (!im || !im->w || !im->h) return;
    float dw = w, dh = h, dx = x, dy = y;
    int of = b->st->object_fit;
    if (of == OF_CONTAIN || of == OF_COVER || of == OF_SCALE_DOWN || of == OF_NONE) {
        float k = of == OF_NONE ? 1 : of == OF_COVER ? LMAX(w / im->w, h / im->h) : LMIN(w / im->w, h / im->h);
        if (of == OF_SCALE_DOWN) k = LMIN(k, 1);
        dw = im->w * k; dh = im->h * k; dx = x + (w - dw) / 2; dy = y + (h - dh) / 2;
    }
    bool needclip = dw > w + 0.5f || dh > h + 0.5f || b->st->border_radius[0].px > 0;
    if (needclip) { DItem *c = emit(p, DO_PUSH_CLIP); c->x = x; c->y = y; c->w = w; c->h = h; radii(b, w, h, c->r); }
    DItem *it = emit(p, DO_IMAGE); it->x = dx; it->y = dy; it->w = dw; it->h = dh; it->img = im;
    if (needclip) emit(p, DO_POP_CLIP);
}

static bool positioned(const Box *b) { return b->st && b->node && (b->st->position != P_STATIC || (!b->st->z_auto && b->parent && b->parent->st && (b->parent->fmt == FMT_FLEX || b->parent->fmt == FMT_GRID))); }
static bool makes_layer(const Box *b) { return b->st && b->node && (b->st->opacity < 1 || b->st->has_transform || b->st->position != P_STATIC); }

static void paint_stacking(PB *p, Box *b);
static void paint_flow(PB *p, Box *b, DefVec *defs);

static void paint_inline_decos(PB *p, Box *ib) {
    for (Box *c = ib->first; c; c = c->next) {
        if (c->kind != BX_INLINE || positioned(c)) continue;
        if (c->st->visibility == VIS_VISIBLE) for (int i = 0; i < c->nir; i++) { IRect *r = &c->ir[i]; if (r->w > 0) paint_rect_deco(p, c, r->x + p->dx, r->y + p->dy, r->w, r->h, r->first, r->last); }
        paint_inline_decos(p, c);
    }
}
static void collect_inline_positioned(PB *p, Box *ib, DefVec *defs) {
    for (Box *c = ib->first; c; c = c->next) {
        if (positioned(c) || c->abs) { Deferred d = { c, p->dx, p->dy, p->clip, p->cx, p->cy, p->cw, p->ch, p->order++ }; vec_push(*defs, d); continue; }
        if (c->kind == BX_INLINE) collect_inline_positioned(p, c, defs);
    }
}

/* paints a non-positioned box's own background and its normal-flow content */
static void paint_block_content(PB *p, Box *b, DefVec *defs) {
    const ComputedStyle *s = b->st;
    bool vis = s->visibility == VIS_VISIBLE;
    if (vis && b->node && b->kind != BX_INLINE && b->node != p->L->doc->html) paint_rect_deco(p, b, b->x + p->dx, b->y + p->dy, b->w, b->h, true, true);
    if (b->fmt == FMT_REPLACED) { if (vis) paint_replaced(p, b); return; }
    bool clips = b->scroller && b->node && (s->overflow_x != OV_VISIBLE || s->overflow_y != OV_VISIBLE);
    PB saved = *p;
    if (clips) {
        float x = b->x + p->dx + b->b[3], y = b->y + p->dy + b->b[0], w = b->w - b->b[1] - b->b[3], h = b->h - b->b[0] - b->b[2];
        DItem *c = emit(p, DO_PUSH_CLIP); c->x = x; c->y = y; c->w = w; c->h = h;
        if (p->clip) { float x1 = LMIN(x + w, p->cx + p->cw), y1 = LMIN(y + h, p->cy + p->ch); x = LMAX(x, p->cx); y = LMAX(y, p->cy); w = LMAX(0, x1 - x); h = LMAX(0, y1 - y); }
        p->clip = true; p->cx = x; p->cy = y; p->cw = w; p->ch = h;
        p->dx -= b->node->scroll_x; p->dy -= b->node->scroll_y;
    }
    if (s->display == D_LIST_ITEM && vis) paint_marker(p, b);
    paint_flow(p, b, defs);
    if (clips) { int order = p->order; *p = saved; p->order = order; emit(p, DO_POP_CLIP); }
}

static void paint_flow(PB *p, Box *b, DefVec *defs) {
    /* block-level descendants */
    for (Box *c = b->first; c; c = c->next) {
        if (c->kind == BX_TEXT || c->kind == BX_BR || c->kind == BX_INLINE) continue;
        if (positioned(c) || makes_layer(c)) { Deferred d = { c, p->dx, p->dy, p->clip, p->cx, p->cy, p->cw, p->ch, p->order++ }; vec_push(*defs, d); continue; }
        if (c->floated || c->kind == BX_ATOMIC) continue;
        paint_block_content(p, c, defs);
    }
    /* floats */
    for (Box *c = b->first; c; c = c->next) if (c->floated && !positioned(c) && !makes_layer(c)) { DefVec local = {0}; paint_block_content(p, c, &local); for (int i = 0; i < local.n; i++) vec_push(*defs, local.v[i]); vec_free(local); }
    /* inline content */
    if (b->nfrags) {
        paint_inline_decos(p, b);
        collect_inline_positioned(p, b, defs);
        for (int i = 0; i < b->nfrags; i++) {
            TextFrag *f = &b->frags[i];
            if (f->box->kind == BX_TEXT) paint_text_frag(p, f);
            else if (!positioned(f->box) && !makes_layer(f->box) && !f->box->floated) { DefVec local = {0}; paint_block_content(p, f->box, &local); for (int k = 0; k < local.n; k++) vec_push(*defs, local.v[k]); vec_free(local); }
        }
    }
}

static int def_cmp(const void *a, const void *b) {
    const Deferred *x = a, *y = b;
    int zx = x->b->st->z_auto ? 0 : x->b->st->z_index, zy = y->b->st->z_auto ? 0 : y->b->st->z_index;
    if (zx != zy) return zx < zy ? -1 : 1;
    return x->order - y->order;
}

static void paint_deferred(PB *p, Deferred *d) {
    PB saved = *p;
    if (d->b->fixed) { p->dx = -0.0f; p->dy = 0; p->clip = false; }
    else {
        p->dx = d->dx; p->dy = d->dy;
        /* clips of scrollers apply only when the box's containing block is inside them */
        p->clip = d->clip && !(d->b->abs && d->b->cb == p->L->root); p->cx = d->cx; p->cy = d->cy; p->cw = d->cw; p->ch = d->ch;
    }
    if (p->clip) { DItem *c = emit(p, DO_PUSH_CLIP); c->x = p->cx; c->y = p->cy; c->w = p->cw; c->h = p->ch; }
    paint_stacking(p, d->b);
    if (p->clip) emit(p, DO_POP_CLIP);
    int order = p->order; *p = saved; p->order = order;
}

static void paint_stacking(PB *p, Box *b) {
    const ComputedStyle *s = b->st;
    if (s->opacity <= 0.001f) return;
    float tx = 0, ty = 0;
    if (s->has_transform) { tx = s->transform[4]; ty = s->transform[5]; if (s->translate_pending[0].pct) tx += s->translate_pending[0].pct * b->w / 100; if (s->translate_pending[1].pct) ty += s->translate_pending[1].pct * b->h / 100; }
    p->dx += tx; p->dy += ty;
    bool layer = s->opacity < 0.999f;
    if (layer) { DItem *l = emit(p, DO_PUSH_LAYER); l->alpha = s->opacity; }
    DefVec defs = {0};
    if (b->kind == BX_INLINE) {
        if (s->visibility == VIS_VISIBLE) for (int i = 0; i < b->nir; i++) { IRect *r = &b->ir[i]; if (r->w > 0) paint_rect_deco(p, b, r->x + p->dx, r->y + p->dy, r->w, r->h, r->first, r->last); }
        /* text of an inline positioned box lives in its block's frags: paint those belonging to it */
        Box *blk = b->parent; while (blk && blk->kind == BX_INLINE) blk = blk->parent;
        if (blk) for (int i = 0; i < blk->nfrags; i++) { TextFrag *f = &blk->frags[i]; Box *o = f->box; while (o && o != b && o != blk) o = o->parent; if (o == b) { if (f->box->kind == BX_TEXT) paint_text_frag(p, f); else paint_block_content(p, f->box, &defs); } }
    } else paint_block_content(p, b, &defs);
    if (defs.n > 1) qsort(defs.v, (size_t)defs.n, sizeof(Deferred), def_cmp);
    for (int i = 0; i < defs.n; i++) paint_deferred(p, &defs.v[i]);
    vec_free(defs);
    if (layer) emit(p, DO_POP_LAYER);
    p->dx -= tx; p->dy -= ty;
}

void dl_clear(DisplayList *dl) { dl->items.n = 0; arena_reset(&dl->arena); }

void dl_build(DisplayList *dl, Layout *L, float scroll_x, float scroll_y, float vw, float vh) {
    dl_clear(dl); dl->vw = vw; dl->vh = vh;
    if (!L->root) return;
    PB p; memset(&p, 0, sizeof p); p.dl = dl; p.L = L; p.dx = -scroll_x; p.dy = -scroll_y; p.sx = scroll_x; p.sy = scroll_y;
    /* canvas background propagates from html, else body */
    Node *html = L->doc->html, *body = L->doc->body;
    Color bg = 0;
    if (html && html->style && COLOR_A(html->style->bg_color)) bg = html->style->bg_color;
    else if (body && body->style && COLOR_A(body->style->bg_color)) bg = body->style->bg_color;
    if (bg) { DItem *it = emit(&p, DO_RECT); it->x = 0; it->y = 0; it->w = vw; it->h = vh; it->color = bg; }
    for (Box *c = L->root->first; c; c = c->next) paint_stacking(&p, c);
}
