/* Box tree construction, text preparation, intrinsic sizes */
#include "layout.h"
#include <math.h>

struct ArenaChunk { ArenaChunk *next; size_t cap, used; char data[]; };
void *arena_alloc(Arena *a, size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = LMAX(n, (size_t)256 * 1024);
        ArenaChunk *c = xmalloc(sizeof *c + cap); c->cap = cap; c->used = 0; c->next = a->head; a->head = c; a->total += cap;
    }
    void *p = a->head->data + a->head->used; a->head->used += n;
    memset(p, 0, n); return p;
}
void arena_reset(Arena *a) { ArenaChunk *c = a->head; while (c) { ArenaChunk *n = c->next; free(c); c = n; } a->head = NULL; a->total = 0; }

bool (*layout_image_size_hook)(Node *n, float *w, float *h);

Font *style_font(const ComputedStyle *s) { return font_get(s->font_family, s->font_weight, s->font_style, s->font_size); }
float style_line_height(const ComputedStyle *s, Font *f) {
    if (!s->line_height_normal) return s->line_height;
    return f ? ceilf(f->ascent + f->descent + f->line_gap) : s->font_size * 1.2f;
}

typedef struct { Layout *L; bool last_space; } BState;

static Box *new_box(Layout *L, uint8_t kind, Node *n, ComputedStyle *st) {
    Box *b = arena_alloc(&L->arena, sizeof *b);
    b->kind = kind; b->node = n; b->st = st; L->nboxes++;
    return b;
}
static void append(Box *p, Box *c) { c->parent = p; c->prev = p->last; if (p->last) p->last->next = c; else p->first = c; p->last = c; }

static char *prep_text(BState *bs, const char *s, size_t n, const ComputedStyle *st, int *outlen) {
    char *o = arena_alloc(&bs->L->arena, n * 3 + 8); int k = 0;
    int ws = st->white_space;
    bool collapse_sp = ws == WS_NORMAL || ws == WS_NOWRAP || ws == WS_PRE_LINE;
    bool keep_nl = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_PRE_LINE || ws == WS_BREAK_SPACES;
    bool cap_next = true;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r') continue;
        if (c == '\n' && keep_nl) {
            if (ws == WS_PRE_LINE) while (k && o[k - 1] == ' ') k--;
            o[k++] = '\n'; bs->last_space = true; continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
            if (collapse_sp) { if (!bs->last_space) o[k++] = ' '; bs->last_space = true; }
            else if (c == '\t') { for (int t = 0; t < 4; t++) o[k++] = ' '; }
            else o[k++] = ' ';
            cap_next = true;
            continue;
        }
        bs->last_space = false;
        if (st->text_transform == TT_UPPER && c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        else if (st->text_transform == TT_LOWER && c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        else if (st->text_transform == TT_CAPITALIZE && cap_next && c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        cap_next = false;
        o[k++] = (char)c;
    }
    if (!collapse_sp) bs->last_space = false;
    o[k] = 0; *outlen = k; return o;
}

static bool is_ws_text(const Box *b) { if (b->kind != BX_TEXT) return false; for (int i = 0; i < b->text_len; i++) if (b->text[i] != ' ' && b->text[i] != '\n') return false; return b->st->white_space == WS_NORMAL || b->st->white_space == WS_NOWRAP || b->st->white_space == WS_PRE_LINE; }
static bool block_level(const Box *b) { return b->kind == BX_BLOCK && in_flow(b); }
static bool inline_level(const Box *b) { return b->kind == BX_INLINE || b->kind == BX_TEXT || b->kind == BX_ATOMIC || b->kind == BX_BR; }

static void fixup_block(Layout *L, Box *b) {
    bool has_block = false;
    for (Box *c = b->first; c; c = c->next) { if (block_level(c)) has_block = true; }
    if (!has_block) { b->fmt = b->fmt == FMT_FLOW ? FMT_INLINE : b->fmt; return; }
    /* wrap runs of inline-level content in anonymous blocks */
    Box *c = b->first; b->first = b->last = NULL;
    while (c) {
        Box *next = c->next;
        if (block_level(c)) { c->next = c->prev = NULL; append(b, c); c = next; continue; }
        Box *run = c, *end = c; bool real = false;
        while (end && !block_level(end)) { if (inline_level(end) && !is_ws_text(end)) real = true; end = end->next; }
        if (real) {
            Box *a = new_box(L, BX_BLOCK, NULL, b->st); a->anon = true; a->fmt = FMT_INLINE;
            for (Box *x = run; x != end;) { Box *nx = x->next; x->next = x->prev = NULL; append(a, x); x = nx; }
            append(b, a);
        } else {
            for (Box *x = run; x != end;) { Box *nx = x->next; x->next = x->prev = NULL; if (!inline_level(x)) append(b, x); x = nx; }
        }
        c = end;
    }
    b->fmt = FMT_FLOW;
}

static void fixup_container_items(Layout *L, Box *b) {
    /* flex/grid: every in-flow child is a block-level item; wrap text runs */
    Box *c = b->first; b->first = b->last = NULL;
    while (c) {
        Box *next = c->next; c->next = c->prev = NULL;
        if (c->kind == BX_TEXT || c->kind == BX_BR) {
            if (c->kind == BX_TEXT && !is_ws_text(c)) { Box *a = new_box(L, BX_BLOCK, NULL, b->st); a->anon = true; a->fmt = FMT_INLINE; a->bfc = true; append(a, c); append(b, a); }
        } else {
            if (c->kind == BX_INLINE) { c->kind = BX_BLOCK; fixup_block(L, c); }
            else if (c->kind == BX_ATOMIC) c->kind = BX_BLOCK;
            c->bfc = true; append(b, c);
        }
        c = next;
    }
}

static const char *ctl_text(Node *n, int *ctl, bool *ph) {
    *ph = false;
    if (n->tag == A_input) {
        const char *t = node_attr(n, "type");
        if (t && (str_ieq(t, "checkbox"))) { *ctl = CTL_CHECK; return NULL; }
        if (t && str_ieq(t, "radio")) { *ctl = CTL_RADIO; return NULL; }
        if (t && str_ieq(t, "range")) { *ctl = CTL_RANGE; return NULL; }
        if (t && (str_ieq(t, "submit") || str_ieq(t, "button") || str_ieq(t, "reset"))) { *ctl = CTL_BUTTON; const char *v = node_attr(n, "value"); return v ? v : str_ieq(t, "submit") ? "Submit" : str_ieq(t, "reset") ? "Reset" : ""; }
        if (t && (str_ieq(t, "image") || str_ieq(t, "file") || str_ieq(t, "color"))) { *ctl = CTL_BUTTON; return str_ieq(t, "file") ? "Choose File" : ""; }
        *ctl = CTL_TEXT;
        const char *v = n->value_override ? n->value_override : node_attr(n, "value");
        if (v && *v) return v;
        *ph = true; v = node_attr(n, "placeholder"); return v ? v : "";
    }
    if (n->tag == A_textarea) { *ctl = CTL_TEXTAREA; if (n->value_override) return n->value_override; char *t = node_text_content(n); if (*t) return t; free(t); *ph = true; const char *p = node_attr(n, "placeholder"); return p ? p : ""; }
    if (n->tag == A_select) {
        *ctl = CTL_SELECT; Node *first = NULL;
        for (Node *o = n->first; o; o = node_next_in_tree(o, n)) if (o->type == NODE_ELEMENT && o->tag == A_option) { if (!first) first = o; if (node_has_attr(o, "selected")) { first = o; break; } }
        return first ? node_text_content(first) : "";
    }
    return NULL;
}

static bool is_replaced(Node *n) {
    if (n->ns == NS_SVG) return n->tag == A_svg;
    return n->tag == A_img || n->tag == A_video || n->tag == A_canvas || n->tag == A_iframe || n->tag == A_embed || n->tag == A_object || n->tag == A_audio;
}

static void build(BState *bs, Box *parent, Node *n, int *li_counter);

static void build_pseudo(BState *bs, Box *parent, Node *el, ComputedStyle *ps) {
    if (!ps || !ps->content || ps->display == D_NONE) return;
    bool blockify = ps->position == P_ABSOLUTE || ps->position == P_FIXED || ps->float_ != F_NONE;
    uint8_t kind = blockify ? BX_BLOCK : ps->display == D_INLINE ? BX_INLINE : (ps->display == D_INLINE_BLOCK || ps->display == D_INLINE_FLEX) ? BX_ATOMIC : BX_BLOCK;
    Box *b = new_box(bs->L, kind, el, ps); b->anon = true;
    b->fmt = ps->display == D_FLEX || ps->display == D_INLINE_FLEX ? FMT_FLEX : ps->display == D_GRID ? FMT_GRID : FMT_FLOW;
    b->abs = ps->position == P_ABSOLUTE || ps->position == P_FIXED; b->fixed = ps->position == P_FIXED; b->floated = ps->float_ != F_NONE;
    if (b->abs || b->floated || kind == BX_ATOMIC) b->bfc = true;
    if (*ps->content) { Box *t = new_box(bs->L, BX_TEXT, NULL, ps); t->text = prep_text(bs, ps->content, strlen(ps->content), ps, &t->text_len); append(b, t); }
    if (kind != BX_INLINE) fixup_block(bs->L, b);
    append(parent, b);
}

static void build_children(BState *bs, Box *b, Node *n) {
    int li = 0;
    if (n->tag == A_ol) { const char *s = node_attr(n, "start"); li = s ? atoi(s) - 1 : 0; }
    if (n->style) build_pseudo(bs, b, n, n->style->before);
    Node *kids = n->template_content ? NULL : n;
    if (kids) for (Node *c = n->first; c; c = c->next) build(bs, b, c, &li);
    if (n->shadow_root) for (Node *c = n->shadow_root->first; c; c = c->next) build(bs, b, c, &li);
    if (n->style) build_pseudo(bs, b, n, n->style->after);
}

static void build(BState *bs, Box *parent, Node *n, int *li) {
    Layout *L = bs->L;
    if (n->type == NODE_TEXT) {
        if (!n->text_len) return;
        ComputedStyle *st = style_for_text(n);
        if (!st || st->display == D_NONE) return;
        Box *t = new_box(L, BX_TEXT, n, st);
        t->text = prep_text(bs, n->text, n->text_len, st, &t->text_len);
        if (!t->text_len) return;
        append(parent, t); n->box = t;
        return;
    }
    if (n->type != NODE_ELEMENT) return;
    ComputedStyle *st = n->style;
    n->box = NULL;
    if (!st || st->display == D_NONE) return;
    if (n->tag == A_slot && n->ns == NS_HTML) { for (Node *c = n->first; c; c = c->next) build(bs, parent, c, li); return; }
    if (st->display == D_CONTENTS) { build_children(bs, parent, n); return; }
    if (n->ns == NS_SVG && n->tag != A_svg) return;
    if (n->tag == A_br && n->ns == NS_HTML) { Box *b = new_box(L, BX_BR, n, st); append(parent, b); n->box = b; bs->last_space = true; return; }
    if (n->tag == A_input) { const char *t = node_attr(n, "type"); if (t && str_ieq(t, "hidden")) return; }
    bool inl = st->display == D_INLINE;
    bool atomic = st->display == D_INLINE_BLOCK || st->display == D_INLINE_FLEX || st->display == D_INLINE_GRID || st->display == D_INLINE_TABLE;
    int ctl = CTL_NONE; bool ph = false;
    const char *ct = (n->tag == A_input || n->tag == A_select || n->tag == A_textarea) && n->ns == NS_HTML ? ctl_text(n, &ctl, &ph) : NULL;
    bool repl = (is_replaced(n) && n->ns != NS_MATHML) || ctl == CTL_CHECK || ctl == CTL_RADIO || ctl == CTL_RANGE;
    if ((repl || ctl) && inl) { inl = false; atomic = true; }
    Box *b = new_box(L, inl ? BX_INLINE : atomic ? BX_ATOMIC : BX_BLOCK, n, st);
    n->box = b;
    b->ctl = (uint8_t)ctl;
    b->abs = st->position == P_ABSOLUTE || st->position == P_FIXED; b->fixed = st->position == P_FIXED;
    b->floated = !b->abs && st->float_ != F_NONE;
    /* The root's overflow (or body's, when the root's is visible) belongs to the viewport, not the box */
    const ComputedStyle *hs = L->doc->html ? L->doc->html->style : NULL;
    bool to_viewport = n == L->doc->html || (n == L->doc->body && (!hs || (hs->overflow_x == OV_VISIBLE && hs->overflow_y == OV_VISIBLE)));
    b->scroller = !to_viewport && (st->overflow_x != OV_VISIBLE || st->overflow_y != OV_VISIBLE);
    b->bfc = b->abs || b->floated || atomic || b->scroller || st->display == D_FLOW_ROOT || st->display == D_TABLE_CELL || st->display == D_TABLE_CAPTION || n == L->doc->html;
    switch (st->display) {
    case D_FLEX: case D_INLINE_FLEX: b->fmt = FMT_FLEX; break;
    case D_GRID: case D_INLINE_GRID: b->fmt = FMT_GRID; break;
    case D_TABLE: case D_INLINE_TABLE: b->fmt = FMT_TABLE; break;
    default: b->fmt = FMT_FLOW;
    }
    if (st->display == D_LIST_ITEM) { b->list_index = ++*li; const char *v = node_attr(n, "value"); if (v && n->tag == A_li) { b->list_index = atoi(v); *li = b->list_index; } }
    if (repl) { b->fmt = FMT_REPLACED; append(parent, b); bs->last_space = false; return; }
    if (ctl) {
        b->fmt = FMT_INLINE; b->bfc = true; b->placeholder = ph;
        if (ct && *ct) {
            Box *t = new_box(L, BX_TEXT, NULL, st); BState s2 = { L, false };
            const char *tt = ct;
            char *masked = NULL;
            if (ctl == CTL_TEXT && !ph) { const char *ty = node_attr(n, "type"); if (ty && str_ieq(ty, "password")) { size_t k = strlen(ct); masked = xmalloc(k * 3 + 1); size_t o = 0; for (size_t i = 0; i < k; i++) if (((unsigned char)ct[i] & 0xC0) != 0x80) { memcpy(masked + o, "\xe2\x80\xa2", 3); o += 3; } masked[o] = 0; tt = masked; } }
            t->text = prep_text(&s2, tt, strlen(tt), st, &t->text_len);
            free(masked);
            append(b, t);
        }
        append(parent, b); bs->last_space = false; return;
    }
    bool saved = bs->last_space;
    if (!inl) bs->last_space = true;
    build_children(bs, b, n);
    if (b->fmt == FMT_FLEX || b->fmt == FMT_GRID) fixup_container_items(L, b);
    else if (b->fmt == FMT_TABLE || st->display == D_TABLE_ROW || st->display == D_TABLE_ROW_GROUP || st->display == D_TABLE_HEADER_GROUP || st->display == D_TABLE_FOOTER_GROUP) {
        /* drop whitespace text; keep blocks */
        Box *c = b->first; b->first = b->last = NULL;
        while (c) { Box *nx = c->next; c->next = c->prev = NULL; if (!is_ws_text(c)) { if (c->kind != BX_BLOCK) { Box *a = new_box(L, BX_BLOCK, NULL, st); a->anon = true; append(a, c); fixup_block(L, a); c = a; } append(b, c); } c = nx; }
    }
    else if (inl) {
        bool blk = false; for (Box *c = b->first; c; c = c->next) if (block_level(c)) blk = true;
        if (blk) { b->kind = BX_BLOCK; fixup_block(L, b); }
    } else fixup_block(L, b);
    if (!inl) bs->last_space = saved || true;
    append(parent, b);
}

Box *build_box_tree(Layout *L, Document *d) {
    Box *root = new_box(L, BX_BLOCK, &d->node, NULL);
    static ComputedStyle *rs; if (!rs) { rs = style_new_default(); rs->display = D_BLOCK; }
    root->st = rs; root->bfc = true; root->fmt = FMT_FLOW;
    BState bs = { L, true };
    int li = 0;
    if (d->html) build(&bs, root, d->html, &li);
    return root;
}

/* ---------------- text shaping ---------------- */
void text_ensure_shaped(Box *t) {
    if (t->shaped) return;
    t->font = style_font(t->st);
    text_shape(t->font, t->text, (size_t)t->text_len, t->st->letter_spacing, &t->sh);
    if (t->st->word_spacing) for (int i = 0; i < t->sh.n; i++) if (t->text[t->sh.g[i].cluster] == ' ') t->sh.g[i].adv += t->st->word_spacing;
    t->shaped = true;
}

/* ---------------- intrinsic sizes ---------------- */
static bool cjk(uint32_t cp) { return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x3000 && cp <= 0x303F); }
bool text_break_after(const Box *t, int gi) {
    uint32_t c = (unsigned char)t->text[t->sh.g[gi].cluster];
    if (c == ' ' || c == '-' || c == '\n' || c == '?' ) return true;
    if (c >= 0x80) { uint32_t cp; utf8_decode(t->text + t->sh.g[gi].cluster, (size_t)(t->text_len - (int)t->sh.g[gi].cluster), &cp); if (cjk(cp) || cp == 0x200B) return true; }
    if (gi + 1 < t->sh.n) { uint32_t cn = (unsigned char)t->text[t->sh.g[gi + 1].cluster]; if (cn >= 0x80) { uint32_t cp; utf8_decode(t->text + t->sh.g[gi + 1].cluster, (size_t)(t->text_len - (int)t->sh.g[gi + 1].cluster), &cp); if (cjk(cp)) return true; } }
    return false;
}

typedef struct { float line, mx, word, mn; } IState;
static void inline_intrinsic(Layout *L, Box *b, IState *s) {
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs) continue;
        if (c->kind == BX_TEXT) {
            text_ensure_shaped(c);
            bool wrap = c->st->white_space != WS_NOWRAP && c->st->white_space != WS_PRE;
            for (int i = 0; i < c->sh.n; i++) {
                char ch = c->text[c->sh.g[i].cluster];
                if (ch == '\n' && c->st->white_space != WS_NORMAL && c->st->white_space != WS_NOWRAP) { s->mx = LMAX(s->mx, s->line); s->line = 0; s->mn = LMAX(s->mn, s->word); s->word = 0; continue; }
                float a = c->sh.g[i].adv;
                s->line += a;
                if (ch == ' ' && wrap) { s->mn = LMAX(s->mn, s->word); s->word = 0; }
                else { s->word += a; if (wrap && (c->st->word_break == 1 || text_break_after(c, i))) { s->mn = LMAX(s->mn, s->word); s->word = 0; } }
            }
        } else if (c->kind == BX_BR) { s->mx = LMAX(s->mx, s->line); s->line = 0; s->mn = LMAX(s->mn, s->word); s->word = 0; }
        else if (c->kind == BX_INLINE) {
            compute_mbp(c, 0);
            float l = c->m[3] + c->b[3] + c->p[3], r = c->m[1] + c->b[1] + c->p[1];
            s->line += l; s->word += l; inline_intrinsic(L, c, s); s->line += r; s->word += r;
        } else {
            float mn, mx; intrinsic_outer(L, c, &mn, &mx);
            if (c->floated) { s->mn = LMAX(s->mn, mn); s->line += mx; continue; }
            s->mn = LMAX(s->mn, LMAX(s->word, 0)); s->word = 0;
            s->mn = LMAX(s->mn, mn); s->line += mx;
        }
    }
}

void intrinsic_outer(Layout *L, Box *b, float *mn, float *mx) {
    intrinsic(L, b, mn, mx);
    float m = (b->st->margin[1].kind == LK_LEN ? b->st->margin[1].px : 0) + (b->st->margin[3].kind == LK_LEN ? b->st->margin[3].px : 0);
    *mn += m; *mx += m;
}

void intrinsic(Layout *L, Box *b, float *omn, float *omx) {
    if (b->intr_ok) { *omn = b->imin; *omx = b->imax; return; }
    compute_mbp(b, 0);
    const ComputedStyle *st = b->st;
    float bp = hbp(b), mn = 0, mx = 0;
    if (st->width.kind == LK_LEN && st->width.pct == 0 && b->fmt != FMT_TABLE) {
        mn = mx = st->width.px - (st->box_sizing == BOX_BORDER ? bp : 0);
    } else if (b->fmt == FMT_REPLACED) {
        float w, h; replaced_size(L, b, -1, &w, &h); mn = mx = w;
        if (st->width.kind == LK_LEN && st->width.pct) mn = 0;
    } else if (b->fmt == FMT_INLINE) {
        IState s = {0}; inline_intrinsic(L, b, &s);
        mx = LMAX(s.mx, s.line); mn = LMAX(s.mn, s.word);
        if (st->white_space == WS_NOWRAP || st->white_space == WS_PRE) mn = mx;
        if (b->ctl == CTL_SELECT) { mn += 20; mx += 20; }
    } else if (b->fmt == FMT_FLEX) {
        bool row = st->flex_direction <= FD_ROW_REVERSE; int n = 0;
        for (Box *c = b->first; c; c = c->next) {
            if (!in_flow(c)) continue;
            float a, z; intrinsic_outer(L, c, &a, &z);
            if (row) { mx += z; mn = st->flex_wrap ? LMAX(mn, a) : mn + a; n++; }
            else { mx = LMAX(mx, z); mn = LMAX(mn, a); }
        }
        if (row && n > 1) { mx += st->column_gap * (n - 1); if (!st->flex_wrap) mn += st->column_gap * (n - 1); }
    } else if (b->fmt == FMT_GRID) {
        int nc = LMAX(1, st->grid_ncols); bool fixed = st->grid_ncols > 0;
        float fsum = 0;
        for (int i = 0; i < st->grid_ncols; i++) { if (st->grid_cols[i].size.kind == LK_LEN && !st->grid_cols[i].size.pct && !st->grid_cols[i].fr) fsum += st->grid_cols[i].size.px; else fixed = false; }
        if (fixed) mn = mx = fsum + st->column_gap * (nc - 1);
        else {
            float cmx = 0, cmn = 0;
            for (Box *c = b->first; c; c = c->next) { if (!in_flow(c)) continue; float a, z; intrinsic_outer(L, c, &a, &z); cmx = LMAX(cmx, z); cmn = LMAX(cmn, a); }
            mx = cmx * nc + st->column_gap * (nc - 1); mn = cmn * nc + st->column_gap * (nc - 1);
        }
    } else if (b->fmt == FMT_TABLE) {
        table_intrinsic(L, b, &mn, &mx);
        mn -= bp; mx -= bp;
        if (st->width.kind == LK_LEN && !st->width.pct) { float w = st->width.px - (st->box_sizing == BOX_BORDER ? bp : 0); mn = LMAX(mn, w); mx = LMAX(mx, w); }
    } else {
        for (Box *c = b->first; c; c = c->next) {
            if (c->abs) continue;
            float a, z; intrinsic_outer(L, c, &a, &z);
            mn = LMAX(mn, a); mx = LMAX(mx, z);
        }
    }
    if (st->max_width.kind == LK_LEN && !st->max_width.pct) { float v = st->max_width.px - (st->box_sizing == BOX_BORDER ? bp : 0); mx = LMIN(mx, v); mn = LMIN(mn, v); }
    if (st->min_width.kind == LK_LEN && !st->min_width.pct) { float v = st->min_width.px - (st->box_sizing == BOX_BORDER ? bp : 0); mx = LMAX(mx, v); mn = LMAX(mn, v); }
    if (mn > mx) mx = mn;
    b->imin = mn + bp; b->imax = mx + bp; b->intr_ok = true;
    *omn = b->imin; *omx = b->imax;
}

void replaced_size(Layout *L, Box *b, float cbw, float *ow, float *oh) {
    Node *n = b->node; const ComputedStyle *st = b->st;
    float nw = 300, nh = 150, ratio = 0; bool have = false;
    if (b->ctl == CTL_CHECK || b->ctl == CTL_RADIO) { nw = nh = 13; have = true; }
    else if (b->ctl == CTL_RANGE) { nw = 129; nh = 16; have = true; }
    else if (n && n->tag == A_img) {
        float w, h;
        if (layout_image_size_hook && layout_image_size_hook(n, &w, &h) && w > 0) { nw = w; nh = h; have = true; }
        else { nw = nh = 0; const char *aw = node_attr(n, "width"), *ah = node_attr(n, "height"); if (aw) nw = (float)atof(aw); if (ah) nh = (float)atof(ah); have = aw || ah; }
    } else if (n && n->ns == NS_SVG) {
        const char *vb = node_attr(n, "viewBox");
        float a, c2, w, h; if (vb && sscanf(vb, "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &a, &c2, &w, &h) == 4 && h > 0) { ratio = w / h; nw = w; nh = h; }
        /* An outer <svg> without width is 100% wide; the viewBox ratio gives its height */
        bool outer = !n->parent || n->parent->type != NODE_ELEMENT || n->parent->ns != NS_SVG;
        if (outer && ratio > 0 && cbw > 0 && !node_attr(n, "width") && !node_attr(n, "height")) { nw = cbw; nh = cbw / ratio; }
    } else if (n && n->tag == A_video) {
        float w, h; if (layout_image_size_hook && layout_image_size_hook(n, &w, &h) && w > 0) { nw = w; nh = h; }
    } else if (n && n->tag == A_canvas) {
        const char *aw = node_attr(n, "width"), *ah = node_attr(n, "height"); if (aw) nw = (float)atof(aw); if (ah) nh = (float)atof(ah);
    } else if (n && n->tag == A_audio) { nw = 300; nh = 54; }
    if (!ratio && nh > 0) ratio = nw / nh;
    if (st->aspect_ratio > 0) ratio = st->aspect_ratio;
    bool bs = st->box_sizing == BOX_BORDER; float hb = hbp(b), vb2 = vbp(b);
    float w = -1, h = -1;
    if (len_def(st->width, cbw)) w = res(st->width, cbw) - (bs ? hb : 0);
    if (st->height.kind == LK_LEN && st->height.pct == 0) h = st->height.px - (bs ? vb2 : 0);
    if (w >= 0 && h < 0) h = ratio > 0 ? w / ratio : nh;
    else if (h >= 0 && w < 0) w = ratio > 0 ? h * ratio : nw;
    else if (w < 0 && h < 0) { w = nw; h = nh; (void)have; }
    if (st->max_width.kind == LK_LEN && cbw >= 0) { float mxw = res(st->max_width, cbw) - (bs ? hb : 0); if (w > mxw) { w = mxw; if (ratio > 0 && st->height.kind != LK_LEN) h = w / ratio; } }
    if (st->min_width.kind == LK_LEN) { float mnw = res(st->min_width, cbw) - (bs ? hb : 0); if (w < mnw) w = mnw; }
    if (st->max_height.kind == LK_LEN && !st->max_height.pct) { float mxh = st->max_height.px - (bs ? vb2 : 0); if (h > mxh) { h = mxh; if (ratio > 0 && st->width.kind != LK_LEN) w = h * ratio; } }
    *ow = LMAX(0, w); *oh = LMAX(0, h);
}
