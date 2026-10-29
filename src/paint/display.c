/* Display list construction: CSS painting order over the box tree */
#include "paint.h"
#include <math.h>

Image *(*paint_image_hook)(Node *n);
Image *(*paint_url_image_hook)(const char *url);

typedef struct { Box *b; float dx, dy; bool clip; float cx, cy, cw, ch; int order; const Box *mv, *mv_out; } Deferred;
typedef VEC(Deferred) DefVec;
typedef struct {
    DisplayList *dl; Layout *L;
    float dx, dy;               /* doc -> viewport translation */
    float sx, sy;               /* page scroll */
    bool clip; float cx, cy, cw, ch;
    int order;
    bool fixed, sticky;         /* sticky: fixed only as a repaint region, inside a sticky box that moves with the page */
    Box *sb; const void *outer;   /* innermost scroller and the state outside it */
    const Box *mv, *mv_out;       /* scroller painted items move with, and the one outside it (for sticky boxes) */
    bool cull; uint64_t gen;      /* skip normal-flow subtrees outside the viewport (box_ink extents of visual generation gen) */
} PB;

static DItem *emit(PB *p, uint8_t op) { DItem it; memset(&it, 0, sizeof it); it.op = op; it.alpha = 1; it.fixed = p->fixed; it.sticky = p->sticky; it.mv = p->mv; vec_push(p->dl->items, it); return &p->dl->items.v[p->dl->items.n - 1]; }
static bool visible(PB *p, float x, float y, float w, float h) {
    if (x > p->dl->vw || y > p->dl->vh || x + w < 0 || y + h < 0) return false;
    if (p->clip && (x > p->cx + p->cw || y > p->cy + p->ch || x + w < p->cx || y + h < p->cy)) return false;
    return true;
}
static bool rounded(const ComputedStyle *s) {
    for (int i = 0; i < 4; i++) if (s->border_radius[i].px > 0 || s->border_radius[i].pct > 0) return true;
    return false;
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

/* mask-image is approximated as the background colour shown through the mask's alpha */
typedef struct { Image *src; Color c; Image *out; } Tint;
static Tint tints[128]; static int tint_i; static Image *tint_grave[128]; static int tint_grave_i;
static Image *tinted(Image *src, Color c) {
    for (int i = 0; i < 128; i++) if (tints[i].src == src && tints[i].c == c) return tints[i].out;
    Tint *t = &tints[tint_i]; tint_i = (tint_i + 1) % 128;
    if (t->out) {
        if (tint_grave[tint_grave_i]) image_unref(tint_grave[tint_grave_i]);
        tint_grave[tint_grave_i] = t->out; tint_grave_i = (tint_grave_i + 1) % 128; image_unref(t->src);
    }
    size_t n = (size_t)src->w * (size_t)src->h;
    Image *o = xcalloc(1, sizeof *o); o->w = src->w; o->h = src->h; o->refs = 1; o->scale = src->scale; o->px = xmalloc(n * 4);
    uint32_t ca = COLOR_A(c), cr = COLOR_R(c), cg = COLOR_G(c), cb = COLOR_B(c);
    for (size_t i = 0; i < n; i++) { uint32_t a = (src->px[i] >> 24) * ca / 255; o->px[i] = a << 24 | (cr * a / 255) << 16 | (cg * a / 255) << 8 | cb * a / 255; }
    src->refs++; t->src = src; t->c = c; t->out = o;
    return o;
}

typedef struct BgArea { float x, y, w, h, r[4]; } BgArea;
static BgArea background_area(const Box *b, float x, float y, float w, float h,
                              int kind, const float outer_r[4], bool l_edge, bool r_edge) {
    float in[4] = {0};
    if (kind != BG_BORDER_BOX) for (int i = 0; i < 4; i++) {
        if ((i == 3 && !l_edge) || (i == 1 && !r_edge)) continue;
        in[i] = b->b[i] + (kind == BG_CONTENT_BOX ? b->p[i] : 0);
    }
    BgArea a = {x + in[3], y + in[0], LMAX(0, w - in[1] - in[3]), LMAX(0, h - in[0] - in[2]), {0}};
    a.r[0] = LMAX(0, outer_r[0] - LMAX(in[0], in[3]));
    a.r[1] = LMAX(0, outer_r[1] - LMAX(in[0], in[1]));
    a.r[2] = LMAX(0, outer_r[2] - LMAX(in[2], in[1]));
    a.r[3] = LMAX(0, outer_r[3] - LMAX(in[2], in[3]));
    return a;
}
/* Background position percentages use (positioning area - image size), which
 * may be negative. Layout's indefinite-size resolver must not be used here. */
static float background_length(Length l, float reference) {
    if (l.kind != LK_LEN) return 0;
    return len_resolve(l, reference);
}
static void paint_background_source(PB *p, const ComputedStyle *s, const BgArea *area,
                                    const BgArea *clip_area, Image *im, Gradient *grad) {
    if (clip_area->w <= 0 || clip_area->h <= 0 || !isfinite(area->w) || !isfinite(area->h)) return;
    float iw = im ? image_css_w(im) : area->w, ih = im ? image_css_h(im) : area->h;
    if ((im && (iw <= 0 || ih <= 0)) || !isfinite(iw) || !isfinite(ih)) return;
    bool auto_x = s->bg_size_kind == BGS_AUTO || (s->bg_size_kind == BGS_LEN && s->bg_size[0].kind != LK_LEN);
    bool auto_y = s->bg_size_kind == BGS_AUTO || (s->bg_size_kind == BGS_LEN && s->bg_size[1].kind != LK_LEN);
    if (s->bg_size_kind == BGS_COVER || s->bg_size_kind == BGS_CONTAIN) {
        if (im) { float scale = s->bg_size_kind == BGS_COVER ? LMAX(area->w / iw, area->h / ih) : LMIN(area->w / iw, area->h / ih); iw *= scale; ih *= scale; }
        else { iw = area->w; ih = area->h; }
    } else if (s->bg_size_kind == BGS_LEN) {
        float w = auto_x ? -1 : LMAX(0, background_length(s->bg_size[0], area->w));
        float h = auto_y ? -1 : LMAX(0, background_length(s->bg_size[1], area->h));
        if (w >= 0 && h >= 0) { iw = w; ih = h; }
        else if (w >= 0) { ih = im ? ih * w / iw : area->h; iw = w; }
        else if (h >= 0) { iw = im ? iw * h / ih : area->w; ih = h; }
    }
    if (iw <= 0 || ih <= 0 || !isfinite(iw) || !isfinite(ih)) return;
    int rx = s->bg_repeat & 3, ry = (s->bg_repeat >> 2) & 3;
    float original_w = iw, original_h = ih;
    if (rx == BGR_ROUND) iw = (float)(area->w / fmax(1, floor((double)area->w / iw + .5)));
    if (ry == BGR_ROUND) ih = (float)(area->h / fmax(1, floor((double)area->h / ih + .5)));
    if (rx == BGR_ROUND && ry != BGR_ROUND && auto_y) ih = original_h * iw / original_w;
    if (ry == BGR_ROUND && rx != BGR_ROUND && auto_x) iw = original_w * ih / original_h;
    if (iw <= 0 || ih <= 0 || !isfinite(iw) || !isfinite(ih)) return;
    double ox = (double)area->x + background_length(s->bg_pos[0], area->w - iw);
    double oy = (double)area->y + background_length(s->bg_pos[1], area->h - ih);
    float step_x = rx == BGR_NO_REPEAT ? 0 : iw, step_y = ry == BGR_NO_REPEAT ? 0 : ih;
    if (rx == BGR_SPACE) {
        double count = floor((double)area->w / iw);
        if (count < 2) step_x = 0;
        else { ox = area->x; step_x = (float)((area->w - iw) / (count - 1)); }
    }
    if (ry == BGR_SPACE) {
        double count = floor((double)area->h / ih);
        if (count < 2) step_y = 0;
        else { oy = area->y; step_y = (float)((area->h - ih) / (count - 1)); }
    }
    if (!isfinite(ox) || !isfinite(oy) || !isfinite(step_x) || !isfinite(step_y)) return;
    /* Reduce the phase once, without a subtraction loop or integer tile count. */
    if (step_x > 0) ox = clip_area->x + fmod(ox - clip_area->x, step_x);
    if (step_y > 0) oy = clip_area->y + fmod(oy - clip_area->y, step_y);
    float x = clip_area->x, y = clip_area->y, right = x + clip_area->w, bottom = y + clip_area->h;
    if (step_x <= 0) { x = LMAX(x, (float)ox); right = LMIN(right, (float)(ox + iw)); }
    if (step_y <= 0) { y = LMAX(y, (float)oy); bottom = LMIN(bottom, (float)(oy + ih)); }
    if (right <= x || bottom <= y || !visible(p, x, y, right - x, bottom - y)) return;
    DItem *it = emit(p, im ? DO_IMAGE : DO_GRADIENT); it->x = x; it->y = y; it->w = right - x; it->h = bottom - y; it->img = im; it->grad = grad;
    memcpy(it->r, clip_area->r, sizeof it->r);
    it->tile = (BackgroundTile){(float)(ox - x), (float)(oy - y), iw, ih, step_x, step_y,
                               clip_area->x - x, clip_area->y - y, clip_area->w, clip_area->h};
}

static void paint_rect_deco(PB *p, Box *b, float x, float y, float w, float h, bool l_edge, bool r_edge) {
    const ComputedStyle *s = b->st;
    if (!visible(p, x - 64, y - 64, w + 128, h + 128)) return;
    float r[4]; radii(b, w, h, r);
    if (s->mask_image) {
        Image *mk = paint_url_image_hook ? paint_url_image_hook(s->mask_image) : NULL;
        if (!mk || !mk->w || !mk->h || !COLOR_A(s->bg_color)) return;
        float iw = image_css_w(mk), ih = image_css_h(mk), ox = x, oy = y;
        if (s->mask_fit) { float k = s->mask_fit == 1 ? LMIN(w / iw, h / ih) : LMAX(w / iw, h / ih); iw *= k; ih *= k; ox += (w - iw) / 2; oy += (h - ih) / 2; }
        DItem *c = emit(p, DO_PUSH_CLIP); c->x = x; c->y = y; c->w = w; c->h = h; memcpy(c->r, r, sizeof r);
        DItem *it = emit(p, DO_IMAGE); it->x = ox; it->y = oy; it->w = iw; it->h = ih; it->img = tinted(mk, s->bg_color);
        emit(p, DO_POP_CLIP);
        return;
    }
    if (s->has_shadow && !s->box_shadow.inset && COLOR_A(s->box_shadow.color)) {
        DItem *it = emit(p, DO_SHADOW); it->x = x + s->box_shadow.x; it->y = y + s->box_shadow.y; it->w = w; it->h = h;
        memcpy(it->r, r, sizeof r); it->color = s->box_shadow.color; it->blur = s->box_shadow.blur; it->spread = s->box_shadow.spread;
        it->ox = s->box_shadow.x; it->oy = s->box_shadow.y;
    }
    BgArea bg_clip = background_area(b, x, y, w, h, s->bg_clip, r, l_edge, r_edge);
    BgArea bg_origin = background_area(b, x, y, w, h, s->bg_origin, r, l_edge, r_edge);
    BgArea color_clip = background_area(b, x, y, w, h, s->bg_color_clip, r, l_edge, r_edge);
    if (COLOR_A(s->bg_color) && color_clip.w > 0 && color_clip.h > 0) {
        Color c = s->bg_color;
        /* backdrop-filter: blur() isn't rendered; blurred content behind a translucent surface reads as
           nearly flat, so approximate it by cutting the remaining transparency to a quarter. A strong blur
           (YouTube's frosted masthead, 48 px) leaves no shapes at all: a mostly opaque surface is drawn
           opaque, since faint sharp text behind it reads as stale pixels */
        if (s->backdrop_blur > 0 && COLOR_A(c) < 255)
            c = (c & 0xffffff) | (Color)(s->backdrop_blur >= 16 && COLOR_A(c) >= 128 ? 255 : 255 - (255 - COLOR_A(c)) / 4) << 24;
        DItem *it = emit(p, DO_RECT); it->x = color_clip.x; it->y = color_clip.y; it->w = color_clip.w; it->h = color_clip.h; memcpy(it->r, color_clip.r, sizeof it->r); it->color = c;
    }
    if (s->bg_gradient) paint_background_source(p, s, &bg_origin, &bg_clip, NULL, s->bg_gradient);
    if (s->bg_image && paint_url_image_hook) {
        Image *im = paint_url_image_hook(s->bg_image);
        if (im && im->w && im->h) paint_background_source(p, s, &bg_origin, &bg_clip, im, NULL);
    }
    float bw[4]; bool any = false;
    for (int i = 0; i < 4; i++) { bw[i] = b->b[i]; if ((i == 3 && !l_edge) || (i == 1 && !r_edge)) bw[i] = 0; if (bw[i] > 0 && s->border_style[i] > BS_HIDDEN && COLOR_A(s->border_color[i])) any = true; else bw[i] = 0; }
    if (any) { DItem *it = emit(p, DO_BORDER); it->x = x; it->y = y; it->w = w; it->h = h; memcpy(it->r, r, sizeof r); memcpy(it->bw, bw, sizeof bw); memcpy(it->bc, s->border_color, sizeof it->bc); memcpy(it->bs, s->border_style, 4); }
    if (s->outline_style > BS_HIDDEN && s->outline_width > 0 && COLOR_A(s->outline_color)) {
        float o = s->outline_offset + s->outline_width;
        DItem *it = emit(p, DO_BORDER); it->x = x - o; it->y = y - o; it->w = w + 2 * o; it->h = h + 2 * o;
        /* the outline follows the box's rounded corners, grown by its offset and width */
        for (int i = 0; i < 4; i++) { it->bw[i] = s->outline_width; it->bc[i] = s->outline_color; it->bs[i] = s->outline_style; it->r[i] = r[i] > 0 ? LMAX(0, r[i] + o) : 0; }
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
    int ss, se, edge = 0;
    bool sel = tsel_range(t, &ss, &se) ? (edge = tsel_edge(t), true) : ctl_range(t, &ss, &se) ? (edge = 3, true) : false;
    if (sel) {
        float x0 = t->sh.g[f->g0].x, a = -1, e = -1;
        for (int i = f->g0; i < f->g1; i++) {
            int c = (int)t->sh.g[i].cluster;
            if (c >= ss && c < se) { float gx = t->sh.g[i].x - x0; if (a < 0) a = gx; e = gx + t->sh.g[i].adv; }
        }
        if (a >= 0 && e > a) { DItem *it = emit(p, DO_RECT); it->x = f->x + a + p->dx; it->y = f->y + p->dy; it->w = e - a; it->h = f->h; it->color = RGBA(179, 215, 255, 255); }
        if (edge) {   /* touch handles sit under the first and last selected glyphs */
            for (int i = f->g0; i < f->g1; i++) {
                int c = (int)t->sh.g[i].cluster; float gx = f->x + t->sh.g[i].x - x0 + p->dx;
                if (c < ss || c >= se) continue;
                TselCaret *k = &g_tsel_caret[0];
                if ((edge & 1) && (!k->ok || c < k->cluster)) *k = (TselCaret){ true, gx, f->y + p->dy, f->h, c };
                k = &g_tsel_caret[1];
                if ((edge & 2) && (!k->ok || c >= k->cluster)) *k = (TselCaret){ true, gx + t->sh.g[i].adv, f->y + p->dy, f->h, c };
            }
        }
    }
    if (s->has_text_shadow && COLOR_A(s->text_shadow.color)) emit_glyphs(p, t, f, s->text_shadow.x, s->text_shadow.y, s->text_shadow.color);
    emit_glyphs(p, t, f, 0, 0, col);
    /* text-decoration may come from an ancestor inline */
    uint8_t deco = s->text_decoration; Color dc = s->color;
    for (Box *a = t->parent; a && !deco; a = a->parent) { if (a->st && a->st->text_decoration) { deco = a->st->text_decoration; dc = a->st->color; } if (a->kind != BX_INLINE) break; }
    if (deco && t->font) {
        float th = LMAX(1, t->font->underline_thick);
        float x = f->x + p->dx, w = f->w;
        if (deco & TD_UNDERLINE) { DItem *it = emit(p, DO_LINE); it->x = x; it->y = f->base + p->dy + t->font->underline_pos;   /* underline_pos: distance below the baseline */ it->w = w; it->h = th; it->color = dc; }
        if (deco & TD_LINE_THROUGH) { DItem *it = emit(p, DO_LINE); it->x = x; it->y = f->base + p->dy - t->font->x_height / 2; it->w = w; it->h = th; it->color = dc; }
        if (deco & TD_OVERLINE) { DItem *it = emit(p, DO_LINE); it->x = x; it->y = f->y + p->dy; it->w = w; it->h = th; it->color = dc; }
    }
}

static void paint_marker(PB *p, Box *b) {
    const ComputedStyle *s = b->st, *ms = s->marker ? s->marker : s;
    if (s->list_style == LST_NONE) return;
    const Box *lb = b;
    while (lb && !lb->nlines) {
        const Box *c = lb->first;
        while (c && (c->abs || c->floated || c->kind == BX_TEXT || c->kind == BX_BR || c->kind == BX_INLINE)) c = c->next;
        lb = c;
    }
    if (!lb) return;
    Font *f = style_font(ms);
    float base = lb->lines[0].base + p->dy, x = b->x + p->dx;
    if (s->list_style == LST_DISC || s->list_style == LST_CIRCLE || s->list_style == LST_SQUARE) {
        float d = LMAX(4, ms->font_size * 0.33f), cy = base - (f ? f->x_height / 2 : d / 2);
        DItem *it = emit(p, s->list_style == LST_CIRCLE ? DO_BORDER : DO_RECT);
        it->x = x - d - ms->font_size * 0.5f; it->y = cy - d / 2; it->w = d; it->h = d; it->color = ms->color;
        if (s->list_style != LST_SQUARE) for (int i = 0; i < 4; i++) it->r[i] = d / 2;
        if (it->op == DO_BORDER) for (int i = 0; i < 4; i++) { it->bw[i] = 1; it->bc[i] = ms->color; it->bs[i] = BS_SOLID; }
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
    DItem *it = emit(p, DO_TEXT); it->g = arena_alloc(&p->dl->arena, sizeof(DGlyph) * (size_t)LMAX(1, r.n)); it->color = ms->color;
    float x0 = x - r.width - ms->font_size * 0.4f;
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
    Image *im = b->node && b->node->ns == NS_SVG ? svg_image(b->node, w, h, 2) : paint_image_hook && b->node ? paint_image_hook(b->node) : NULL;
    if (!im || !im->w || !im->h) return;
    float dw = w, dh = h, dx = x, dy = y;
    int of = b->st->object_fit;
    if (of == OF_CONTAIN || of == OF_COVER || of == OF_SCALE_DOWN || of == OF_NONE) {
        float k = of == OF_NONE ? 1 : of == OF_COVER ? LMAX(w / im->w, h / im->h) : LMIN(w / im->w, h / im->h);
        if (of == OF_SCALE_DOWN) k = LMIN(k, 1);
        dw = im->w * k; dh = im->h * k; dx = x + (w - dw) / 2; dy = y + (h - dh) / 2;
    }
    bool needclip = dw > w + 0.5f || dh > h + 0.5f || rounded(b->st);
    if (needclip) { DItem *c = emit(p, DO_PUSH_CLIP); c->x = x; c->y = y; c->w = w; c->h = h; radii(b, w, h, c->r); }
    DItem *it = emit(p, DO_IMAGE); it->x = dx; it->y = dy; it->w = dw; it->h = dh; it->img = im;
    if (needclip) emit(p, DO_POP_CLIP);
}

static bool positioned(const Box *b) { return b->st && b->node && (b->st->position != P_STATIC || (!b->st->z_auto && b->parent && b->parent->st && (b->parent->fmt == FMT_FLEX || b->parent->fmt == FMT_GRID))); }
static bool makes_layer(const Box *b) { return b->st && b->node && (b->st->opacity < 1 || b->st->has_transform || b->st->position != P_STATIC); }

static void paint_stacking(PB *p, Box *b);
static void paint_flow(PB *p, Box *b, DefVec *defs);

/* Off-screen culling. box_ink() keeps, per box, the vertical extent of what paint_block_content
   paints for it (its box, normal-flow children, floats, line content; a clipping scroller only its
   box) and whether that walk would collect boxes painted later in stacking order (positioned or
   layered ones), which must still be found. A subtree without those whose extent, widened by the
   64 px decoration margin paint_rect_deco allows, misses the viewport or clip is skipped whole.
   The extents hold until the next layout or restyle (doc_box_generation; scrolling keeps them). */
static void box_ink(Box *b, uint64_t gen);
static void ink_inline(Box *ib, uint64_t gen, float *y0, float *y1, bool *defer) {
    for (Box *c = ib->first; c; c = c->next) {
        if (positioned(c) || c->abs) { *defer = true; continue; }
        if (c->kind != BX_INLINE) continue;
        for (int i = 0; i < c->nir; i++) { *y0 = LMIN(*y0, c->ir[i].y); *y1 = LMAX(*y1, c->ir[i].y + c->ir[i].h); }
        ink_inline(c, gen, y0, y1, defer);
    }
}
static void ink_add(Box *c, uint64_t gen, float *y0, float *y1, bool *defer) {
    box_ink(c, gen); *y0 = LMIN(*y0, c->ink0); *y1 = LMAX(*y1, c->ink1); *defer |= c->defer_in;
}
static void box_ink(Box *b, uint64_t gen) {
    if (b->ink_gen == gen) return;
    float y0 = b->y, y1 = b->y + b->h, c0 = y0, c1 = y1; bool defer = false;
    if (b->fmt != FMT_REPLACED) {
        const ComputedStyle *s = b->st;
        bool clips = b->scroller && b->node && (s->overflow_x != OV_VISIBLE || s->overflow_y != OV_VISIBLE);
        for (Box *c = b->first; c; c = c->next) {   /* as paint_flow: block children and floats */
            if (c->kind == BX_TEXT || c->kind == BX_BR || c->kind == BX_INLINE) continue;
            if (positioned(c) || makes_layer(c)) { defer = true; continue; }
            if (c->kind == BX_ATOMIC && !c->floated) continue;
            ink_add(c, gen, &c0, &c1, &defer);
        }
        if (b->nfrags) {
            ink_inline(b, gen, &c0, &c1, &defer);
            for (int i = 0; i < b->nfrags; i++) {
                TextFrag *f = &b->frags[i]; Box *fb = f->box;
                if (fb->kind == BX_TEXT) { c0 = LMIN(c0, f->y); c1 = LMAX(c1, f->y + f->h); }
                else if (positioned(fb) || makes_layer(fb)) defer = true;
                else if (!fb->floated) ink_add(fb, gen, &c0, &c1, &defer);
            }
        }
        if (!clips) { y0 = c0; y1 = c1; }
    }
    b->ink0 = y0; b->ink1 = y1; b->defer_in = defer; b->ink_gen = gen;
}
static bool culled(PB *p, Box *c) {
    if (!p->cull) return false;
    box_ink(c, p->gen);
    if (c->defer_in) return false;
    float y0 = c->ink0 + p->dy - 64, y1 = c->ink1 + p->dy + 64;
    if (y1 < 0 || y0 > p->dl->vh) return true;
    return p->clip && (y1 < p->cy || y0 > p->cy + p->ch);
}

static void paint_inline_decos(PB *p, Box *ib) {
    for (Box *c = ib->first; c; c = c->next) {
        if (c->kind != BX_INLINE || positioned(c)) continue;
        if (c->st->visibility == VIS_VISIBLE) for (int i = 0; i < c->nir; i++) { IRect *r = &c->ir[i]; if (r->w > 0) paint_rect_deco(p, c, r->x + p->dx, r->y + p->dy, r->w, r->h, r->first, r->last); }
        paint_inline_decos(p, c);
    }
}
static void collect_inline_positioned(PB *p, Box *ib, DefVec *defs) {
    for (Box *c = ib->first; c; c = c->next) {
        if (positioned(c) || c->abs) { Deferred d = { c, p->dx, p->dy, p->clip, p->cx, p->cy, p->cw, p->ch, p->order++, p->mv, p->mv_out }; vec_push(*defs, d); continue; }
        if (c->kind == BX_INLINE) collect_inline_positioned(p, c, defs);
    }
}

bool dl_caret_on;
bool dl_cull = true;

static void paint_caret(PB *p, Box *b) {
    const ComputedStyle *s = b->st;
    float x = b->x + b->b[3] + b->p[3], y = b->y + b->b[0] + b->p[0], h = s->font_size * 1.2f;
    float ch = b->h - b->b[0] - b->b[2] - b->p[0] - b->p[2];
    TextFrag *last = b->nfrags ? &b->frags[b->nfrags - 1] : NULL;
    for (Box *c = b->first; c; c = c->next) if (c->nfrags) last = &c->frags[c->nfrags - 1];
    const Document *d = b->node->doc;
    if (d && b->node == d->csel_n) {   /* at the selection focus; a range shows as a highlight instead */
        float cx, cy, cht;
        if (d->csel[0] != d->csel[1]) return;
        if (ctl_caret(b, d->csel[1], &cx, &cy, &cht)) { DItem *it = emit(p, DO_RECT); it->x = cx + p->dx; it->y = cy + p->dy; it->w = 1; it->h = cht; it->color = s->color; return; }
    }
    if (last) { y = last->y; h = last->h; if (!b->placeholder) x = last->x + last->w; }
    else if (b->ctl == CTL_TEXT && ch > h) y += (ch - h) / 2;
    DItem *it = emit(p, DO_RECT); it->x = x + p->dx; it->y = y + p->dy; it->w = 1; it->h = h; it->color = s->color;
}

static void last_frag_in(Box *b, TextFrag **out, const Box **blk) {
    for (int i = b->nfrags - 1; i >= 0; i--) if (b->frags[i].box->node) { *out = &b->frags[i]; *blk = b; break; }
    for (Box *c = b->first; c; c = c->next) if (!c->abs && c->kind != BX_TEXT && c->kind != BX_INLINE) last_frag_in(c, out, blk);
}

/* contenteditable host: caret after the last text, else at the start of its first (empty) block */
typedef struct { const Node *n; int o; TextFrag *f; float x; bool exact; } CaretAt;
static void caret_find(Box *b, CaretAt *k) {
    for (int i = 0; i < b->nfrags && !k->exact; i++) {
        TextFrag *f = &b->frags[i]; const Box *t = f->box;
        if (t->node != k->n || f->g0 >= f->g1) continue;
        int lo = (int)t->sh.g[f->g0].cluster, hi = f->g1 < t->sh.n ? (int)t->sh.g[f->g1].cluster : t->text_len;
        if (k->o < lo || k->o > hi) continue;
        float x = f->x + f->w;
        for (int g = f->g0; g < f->g1; g++) if ((int)t->sh.g[g].cluster >= k->o) { x = f->x + t->sh.g[g].x - t->sh.g[f->g0].x; break; }
        k->f = f; k->x = x; k->exact = k->o < hi;
    }
    for (Box *c = b->first; c && !k->exact; c = c->next) if (!c->abs) caret_find(c, k);
}

/* contenteditable host: caret at the selection focus, else after the last text, else at the start of its first (empty) block */
static void paint_edit_caret(PB *p, Box *b) {
    TextFrag *f = NULL; const Box *fb = NULL;
    const ComputedStyle *s = b->st;
    float x, y, h;
    const Document *d = b->node->doc; const Box *eb = NULL;
    if (d && d->sel[0] && node_within(d->sel[0], b->node)) {
        if (d->sel[0] != d->sel[1] || d->selo[0] != d->selo[1]) return;   /* a range is shown as a highlight */
        Node *tn; int to;
        if (tsel_dom_point(d->sel[0], d->selo[0], false, &tn, &to)) {
            CaretAt k = { tn, to, NULL, 0, false }; caret_find(b, &k);
            if (k.f) { DItem *it = emit(p, DO_RECT); it->x = k.x + p->dx; it->y = k.f->y + p->dy; it->w = 1; it->h = k.f->h; s = k.f->box->st;
                it->color = COLOR_A(s->caret_color) ? s->caret_color : s->color; return; }
        } else if (d->sel[0]->type == NODE_ELEMENT && d->sel[0]->box) eb = d->sel[0]->box;
    }
    if (!eb) last_frag_in(b, &f, &fb);
    if (f) { x = f->x + f->w; y = f->y; h = f->h; s = f->box->st; }
    else {
        const Box *c = eb ? eb : b; while (c->first && !c->first->abs && c->first->kind == BX_BLOCK) c = c->first;
        s = c->st; h = s->font_size * 1.2f;
        x = c->x + c->b[3] + c->p[3]; y = c->y + c->b[0] + c->p[0];
        float ch = c->h - c->b[0] - c->b[2] - c->p[0] - c->p[2]; if (ch > h) y += (ch - h) / 2;
    }
    DItem *it = emit(p, DO_RECT); it->x = x + p->dx; it->y = y + p->dy; it->w = 1; it->h = h; it->color = COLOR_A(s->caret_color) ? s->caret_color : s->color;
}
static bool is_edit_host(const Node *n) {
    const char *ce = n && n->type == NODE_ELEMENT ? node_attr((Node *)n, "contenteditable") : NULL;
    return ce && strcmp(ce, "false");
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
        DItem *c = emit(p, DO_PUSH_CLIP); c->x = x; c->y = y; c->w = w; c->h = h; c->sc = b;
        if (rounded(s)) {   /* overflow clips to the padding box's rounded corners */
            float o[4]; radii(b, b->w, b->h, o);
            const float side[4][2] = { { b->b[3], b->b[0] }, { b->b[1], b->b[0] }, { b->b[1], b->b[2] }, { b->b[3], b->b[2] } };
            for (int i = 0; i < 4; i++) c->r[i] = LMAX(0, o[i] - LMAX(side[i][0], side[i][1]));
        }
        if (p->clip) { float x1 = LMIN(x + w, p->cx + p->cw), y1 = LMIN(y + h, p->cy + p->ch); x = LMAX(x, p->cx); y = LMAX(y, p->cy); w = LMAX(0, x1 - x); h = LMAX(0, y1 - y); }
        p->clip = true; p->cx = x; p->cy = y; p->cw = w; p->ch = h;
        p->dx -= b->node->scroll_x; p->dy -= box_scroll_y(b);
        p->sb = b; p->outer = &saved; p->mv_out = p->mv; p->mv = b;
    }
    if (s->display == D_LIST_ITEM && vis) paint_marker(p, b);
    paint_flow(p, b, defs);
    if (vis && dl_caret_on && (b->ctl == CTL_TEXT || b->ctl == CTL_TEXTAREA) && b->node && (b->node->flags & NF_FOCUS)) paint_caret(p, b);
    else if (vis && dl_caret_on && b->node && (b->node->flags & NF_FOCUS) && is_edit_host(b->node)) paint_edit_caret(p, b);
    if (clips) { int order = p->order; *p = saved; p->order = order; emit(p, DO_POP_CLIP); }
}

static void paint_flow(PB *p, Box *b, DefVec *defs) {
    /* block-level descendants */
    for (Box *c = b->first; c; c = c->next) {
        if (c->kind == BX_TEXT || c->kind == BX_BR || c->kind == BX_INLINE) continue;
        if (positioned(c) || makes_layer(c)) {
            /* an abs box whose containing block is outside a scroller neither scrolls nor clips with it */
            const PB *q = p; if (c->abs) while (q->sb && !box_within(c->cb, q->sb)) q = q->outer;
            Deferred d = { c, q->dx, q->dy, q->clip, q->cx, q->cy, q->cw, q->ch, p->order++, q->mv, q->mv_out }; vec_push(*defs, d); continue; }
        if (c->floated || c->kind == BX_ATOMIC || culled(p, c)) continue;
        paint_block_content(p, c, defs);
    }
    /* floats */
    for (Box *c = b->first; c; c = c->next) if (c->floated && !positioned(c) && !makes_layer(c) && !culled(p, c)) { DefVec local = {0}; paint_block_content(p, c, &local); for (int i = 0; i < local.n; i++) vec_push(*defs, local.v[i]); vec_free(local); }
    /* inline content */
    if (b->nfrags) {
        paint_inline_decos(p, b);
        collect_inline_positioned(p, b, defs);
        for (int i = 0; i < b->nfrags; i++) {
            TextFrag *f = &b->frags[i];
            if (f->box->kind == BX_TEXT) paint_text_frag(p, f);
            else if (!positioned(f->box) && !makes_layer(f->box) && !f->box->floated && !culled(p, f->box)) { DefVec local = {0}; paint_block_content(p, f->box, &local); for (int k = 0; k < local.n; k++) vec_push(*defs, local.v[k]); vec_free(local); }
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
    if (d->b->fixed) { p->dx = -0.0f; p->dy = 0; p->clip = false; p->fixed = true; p->sticky = false; p->mv = p->mv_out = NULL; }
    else {
        p->dx = d->dx; p->dy = d->dy; p->mv = d->mv; p->mv_out = d->mv_out;
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
    float sticky_x, sticky_y; box_sticky_offset(b, p->sx, p->sy, &sticky_x, &sticky_y);
    tx += sticky_x; ty += sticky_y;
    bool was_fixed = p->fixed, was_sticky = p->sticky; const Box *was_mv = p->mv;
    if (s->position == P_STICKY) {   /* include in scroll damage; it stays put with the page only while stuck to the viewport */
        bool still = (was_fixed && !was_sticky) || ((sticky_x || sticky_y) && !p->mv);
        p->fixed = true; p->sticky = !still; p->mv = p->mv_out;
    }
    p->dx += tx; p->dy += ty;
    /* axis-aligned scale (e.g. progress bars using scaleX): scale the subtree's items about transform-origin;
       text only under a uniform scale, as the same face at the scaled size (x.com's scale(1.15) Google
       button drew its scaled icon over unscaled text) */
    float ksx = s->has_transform && !s->transform[1] && !s->transform[2] ? s->transform[0] : 1, ksy = s->has_transform && !s->transform[1] && !s->transform[2] ? s->transform[3] : 1;
    int i0 = p->dl->items.n;
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
    if (fabsf(ksx - 1) > 1e-4f || fabsf(ksy - 1) > 1e-4f) {
        float ox = b->x + p->dx + s->transform_origin[0].px + s->transform_origin[0].pct * b->w / 100;
        float oy = b->y + p->dy + s->transform_origin[1].px + s->transform_origin[1].pct * b->h / 100;
        for (int i = i0; i < p->dl->items.n; i++) {
            DItem *it = &p->dl->items.v[i];
            if (it->g) {
                if (fabsf(ksx - ksy) > 1e-4f || ksx <= 0) continue;
                Font *from = NULL, *to = NULL;   /* sizes in quarter pixels: a scale animation reuses a few fonts */
                for (int k = 0; k < it->ng; k++) {
                    DGlyph *g = &it->g[k];
                    g->x = ox + (g->x - ox) * ksx; g->y = oy + (g->y - oy) * ksy;
                    if (g->font && g->font != from) { from = g->font; to = font_resized(from, LMAX(0.25f, roundf(from->size * ksx * 4) / 4)); }
                    if (g->font) g->font = to;
                }
            }
            it->x = ox + (it->x - ox) * ksx; it->y = oy + (it->y - oy) * ksy; it->w *= ksx; it->h *= ksy;
            if (it->w < 0) { it->x += it->w; it->w = -it->w; }
            if (it->h < 0) { it->y += it->h; it->h = -it->h; }
        }
    }
    p->dx -= tx; p->dy -= ty;
    p->fixed = was_fixed; p->sticky = was_sticky; p->mv = was_mv;
}

void dl_clear(DisplayList *dl) { dl->items.n = 0; dl->fixed.count = 0; arena_reset(&dl->arena); }

void dl_build(DisplayList *dl, Layout *L, float scroll_x, float scroll_y, float vw, float vh) {
    dl_clear(dl); dl->vw = vw; dl->vh = vh;
    if (L == g_tsel.L || (L->doc && L->doc->csel_n)) memset(g_tsel_caret, 0, sizeof g_tsel_caret);
    if (!L->root) return;
    tsel_prepare(L);
    PB p; memset(&p, 0, sizeof p); p.dl = dl; p.L = L; p.dx = -scroll_x; p.dy = -scroll_y; p.sx = scroll_x; p.sy = scroll_y;
    p.cull = dl_cull; p.gen = doc_box_generation() + 1;
    /* canvas background propagates from html, else body */
    Node *html = L->doc->html, *body = L->doc->body;
    Color bg = 0;
    if (html && html->style && COLOR_A(html->style->bg_color)) bg = html->style->bg_color;
    else if (body && body->style && COLOR_A(body->style->bg_color)) bg = body->style->bg_color;
    /* the canvas fills the viewport wherever the page is scrolled: fixed for scroll signatures, not a fixed region */
    if (bg) { DItem *it = emit(&p, DO_RECT); it->x = 0; it->y = 0; it->w = vw; it->h = vh; it->color = bg; it->fixed = true; }
    for (Box *c = L->root->first; c; c = c->next) paint_stacking(&p, c);
    /* Collect final coordinates after ancestor transforms have been applied. */
    for (int i = bg ? 1 : 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (!it->fixed || it->w <= 0 || it->h <= 0 || it->op == DO_PUSH_CLIP) continue;
        float pad = it->op == DO_SHADOW ? LMAX(0, it->spread) + LMAX(0.5f, it->blur) : 0;
        float bounds[4] = {it->x - pad, it->y - pad, it->x + it->w + pad, it->y + it->h + pad};
        paint_fixed_add(&dl->fixed, bounds);
    }
}
