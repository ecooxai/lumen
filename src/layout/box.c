/* Box tree construction, text preparation, intrinsic sizes */
#include "layout.h"
#include <math.h>
#include <pthread.h>

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

static char *prep_text(BState *bs, const char *s, size_t n, const ComputedStyle *st, int *outlen, int **srcmap) {
    char *o = arena_alloc(&bs->L->arena, n * 3 + 8); int k = 0;
    int *map = srcmap ? arena_alloc(&bs->L->arena, (n * 3 + 9) * sizeof(int)) : NULL;   /* output byte -> source byte */
    int ws = st->white_space;
    bool collapse_sp = ws == WS_NORMAL || ws == WS_NOWRAP || ws == WS_PRE_LINE;
    bool keep_nl = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_PRE_LINE || ws == WS_BREAK_SPACES;
    bool cap_next = true;
    for (size_t i = 0; i < n; i++) {
#define PUT(ch) (map ? (void)(map[k] = (int)i) : (void)0, o[k++] = (ch))
        unsigned char c = (unsigned char)s[i];
        if (c == '\r') continue;
        if (c == '\n' && keep_nl) {
            if (ws == WS_PRE_LINE) while (k && o[k - 1] == ' ') k--;
            PUT('\n'); bs->last_space = true; continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
            if (collapse_sp) { if (!bs->last_space) PUT(' '); bs->last_space = true; }
            else if (c == '\t') { for (int t = 0; t < 4; t++) PUT(' '); }
            else PUT(' ');
            cap_next = true;
            continue;
        }
        bs->last_space = false;
        if (st->text_transform == TT_UPPER && c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        else if (st->text_transform == TT_LOWER && c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        else if (st->text_transform == TT_CAPITALIZE && cap_next && c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        cap_next = false;
        PUT((char)c);
    }
    if (!collapse_sp) bs->last_space = false;
#undef PUT
    if (map) { map[k] = (int)n; *srcmap = map; }
    o[k] = 0; *outlen = k; return o;
}

static bool is_ws_text(const Box *b) { if (b->kind != BX_TEXT) return false; for (int i = 0; i < b->text_len; i++) if (b->text[i] != ' ' && b->text[i] != '\n') return false; return b->st->white_space == WS_NORMAL || b->st->white_space == WS_NOWRAP || b->st->white_space == WS_PRE_LINE; }
static bool block_level(const Box *b) { return b->kind == BX_BLOCK && in_flow(b); }
static bool inline_level(const Box *b) { return b->kind == BX_INLINE || b->kind == BX_TEXT || b->kind == BX_ATOMIC || b->kind == BX_BR; }

/* an anonymous block wrapping an inline run shares its parent's style, but not its size: a
   height: 100% parent would otherwise give each wrapper the parent's whole height */
static ComputedStyle *anon_block_style(Layout *L, ComputedStyle *p) {
    if (p->width.kind == LK_AUTO && p->height.kind == LK_AUTO && p->min_width.kind == LK_AUTO && p->min_height.kind == LK_AUTO &&
        p->max_width.kind == LK_NONE && p->max_height.kind == LK_NONE && p->aspect_ratio <= 0) return p;
    ComputedStyle *s = arena_alloc(&L->arena, sizeof *s);
    *s = *p; s->refs = 1 << 20;
    s->width = s->height = s->min_width = s->min_height = L_auto();
    s->max_width.kind = s->max_height.kind = LK_NONE; s->aspect_ratio = 0;
    return s;
}
static void fixup_block(Layout *L, Box *b) {
    bool has_block = false;
    for (Box *c = b->first; c; c = c->next) { if (block_level(c)) has_block = true; }
    if (!has_block) { b->fmt = b->fmt == FMT_FLOW ? FMT_INLINE : b->fmt; return; }
    /* wrap runs of inline-level content in anonymous blocks */
    Box *c = b->first; b->first = b->last = NULL; ComputedStyle *ast = NULL;
    while (c) {
        Box *next = c->next;
        if (block_level(c)) { c->next = c->prev = NULL; append(b, c); c = next; continue; }
        Box *run = c, *end = c; bool real = false;
        while (end && !block_level(end)) { if (inline_level(end) && !is_ws_text(end)) real = true; end = end->next; }
        if (real) {
            if (!ast) ast = anon_block_style(L, b->st);
            Box *a = new_box(L, BX_BLOCK, NULL, ast); a->anon = true; a->fmt = FMT_INLINE;
            for (Box *x = run; x != end;) { Box *nx = x->next; x->next = x->prev = NULL; append(a, x); x = nx; }
            append(b, a);
        } else {
            for (Box *x = run; x != end;) { Box *nx = x->next; x->next = x->prev = NULL; if (!inline_level(x)) append(b, x); x = nx; }
        }
        c = end;
    }
    b->fmt = FMT_FLOW;
}

/* a flex/grid item wrapping a text run: inherited properties only, everything else initial */
static ComputedStyle *anon_item_style(Layout *L, const ComputedStyle *p) {
    ComputedStyle *t = style_inherit(p), *s = arena_alloc(&L->arena, sizeof *s);
    *s = *t; s->text_decoration = p->text_decoration; s->refs = 1 << 20;
    style_free(t);
    return s;
}
static void fixup_container_items(Layout *L, Box *b) {
    /* flex/grid: every in-flow child is a block-level item; wrap text runs */
    Box *c = b->first; b->first = b->last = NULL;
    while (c) {
        Box *next = c->next; c->next = c->prev = NULL;
        if (c->kind == BX_TEXT || c->kind == BX_BR) {
            if (c->kind == BX_TEXT && !is_ws_text(c)) { Box *a = new_box(L, BX_BLOCK, NULL, anon_item_style(L, b->st)); a->anon = true; a->fmt = FMT_INLINE; a->bfc = true; append(a, c); append(b, a); }
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
    uint8_t kind = blockify ? BX_BLOCK : ps->display == D_INLINE ? BX_INLINE : (ps->display == D_INLINE_BLOCK || ps->display == D_INLINE_FLEX || ps->display == D_INLINE_GRID) ? BX_ATOMIC : BX_BLOCK;
    Box *b = new_box(bs->L, kind, el, ps); b->anon = true;
    b->fmt = ps->display == D_FLEX || ps->display == D_INLINE_FLEX ? FMT_FLEX : ps->display == D_GRID || ps->display == D_INLINE_GRID ? FMT_GRID : FMT_FLOW;
    b->abs = ps->position == P_ABSOLUTE || ps->position == P_FIXED; b->fixed = ps->position == P_FIXED; b->floated = ps->float_ != F_NONE;
    if (b->abs || b->floated || kind == BX_ATOMIC) b->bfc = true;
    if (*ps->content) { Box *t = new_box(bs->L, BX_TEXT, NULL, ps); t->text = prep_text(bs, ps->content, strlen(ps->content), ps, &t->text_len, NULL); append(b, t); }
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
        t->text = prep_text(bs, n->text, n->text_len, st, &t->text_len, &t->tsrc);
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
            t->text = prep_text(&s2, tt, strlen(tt), st, &t->text_len, NULL);
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

/* ---------------- text shaping ----------------
   Every layout pass rebuilds the box tree, so without reuse all text of the document is shaped
   by HarfBuzz again whenever a script reads geometry after a mutation. Shaped runs are immutable
   once built and shared by refcount, keyed by font, spacing, font generation and the exact text.
   Runs no box references are kept for a few layout passes so the next pass finds them. */
typedef struct ShapeEntry {
    struct ShapeEntry *next;
    uint64_t hash; Font *font; float ls, ws; unsigned gen, stamp; int len, refs;
    ShapedRun run; char text[];
} ShapeEntry;
static struct {
    ShapeEntry **slot; size_t nslot, count, idle_bytes; unsigned stamp, hits, misses;
    pthread_mutex_t mu;
} shape_cache = { .mu = PTHREAD_MUTEX_INITIALIZER };
enum { SHAPE_KEEP_PASSES = 4 };
static const size_t SHAPE_IDLE_LIMIT = (size_t)4 << 20;   /* unreferenced bytes kept for reuse */

static size_t shape_bytes(const ShapeEntry *e) { return sizeof *e + (size_t)e->len + 1 + (size_t)e->run.n * sizeof *e->run.g; }
static uint64_t shape_hash(const Font *f, float ls, float ws, unsigned gen, const char *s, int len) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)(uintptr_t)f;
    uint32_t bits[3]; memcpy(&bits[0], &ls, 4); memcpy(&bits[1], &ws, 4); bits[2] = gen;
    for (int i = 0; i < 3; i++) h = (h ^ bits[i]) * 1099511628211ull;
    for (int i = 0; i < len; i++) h = (h ^ (uint8_t)s[i]) * 1099511628211ull;
    return h ^ h >> 29;
}
static void shape_cache_grow(void) {
    size_t n = shape_cache.nslot ? shape_cache.nslot * 2 : 1024;
    ShapeEntry **slot = xcalloc(n, sizeof *slot);
    for (size_t i = 0; i < shape_cache.nslot; i++)
        for (ShapeEntry *e = shape_cache.slot[i], *next; e; e = next) { next = e->next; ShapeEntry **b = &slot[e->hash & (n - 1)]; e->next = *b; *b = e; }
    free(shape_cache.slot); shape_cache.slot = slot; shape_cache.nslot = n;
}

void text_ensure_shaped(Box *t) {
    if (t->shaped) return;
    t->font = style_font(t->st);
    float ls = t->st->letter_spacing, ws = t->st->word_spacing;
    unsigned gen = font_generation();
    uint64_t h = shape_hash(t->font, ls, ws, gen, t->text, t->text_len);
    pthread_mutex_lock(&shape_cache.mu);
    ShapeEntry *e = NULL;
    if (shape_cache.nslot)
        for (e = shape_cache.slot[h & (shape_cache.nslot - 1)]; e; e = e->next)
            if (e->hash == h && e->font == t->font && e->len == t->text_len && e->ls == ls && e->ws == ws &&
                    e->gen == gen && !memcmp(e->text, t->text, (size_t)t->text_len)) break;
    if (!e) {
        shape_cache.misses++;
        pthread_mutex_unlock(&shape_cache.mu);
        ShapedRun run; text_shape(t->font, t->text, (size_t)t->text_len, ls, &run);
        if (ws) for (int i = 0; i < run.n; i++) if (t->text[run.g[i].cluster] == ' ') run.g[i].adv += ws;
        e = xmalloc(sizeof *e + (size_t)t->text_len + 1);
        *e = (ShapeEntry){ .hash = h, .font = t->font, .ls = ls, .ws = ws, .gen = gen, .len = t->text_len, .run = run };
        memcpy(e->text, t->text, (size_t)t->text_len); e->text[t->text_len] = 0;
        pthread_mutex_lock(&shape_cache.mu);
        if (shape_cache.count >= shape_cache.nslot) shape_cache_grow();
        ShapeEntry **b = &shape_cache.slot[h & (shape_cache.nslot - 1)]; e->next = *b; *b = e; shape_cache.count++;
    } else { shape_cache.hits++; if (!e->refs) shape_cache.idle_bytes -= shape_bytes(e); }
    e->refs++; e->stamp = shape_cache.stamp;
    pthread_mutex_unlock(&shape_cache.mu);
    t->sh = e->run; t->shape_ref = e; t->shaped = true;
}

void text_release_shaped(Box *t) {
    if (!t->shaped) return;
    ShapeEntry *e = t->shape_ref;
    if (e) {
        pthread_mutex_lock(&shape_cache.mu);
        if (!--e->refs) { shape_cache.idle_bytes += shape_bytes(e); e->stamp = shape_cache.stamp; }
        pthread_mutex_unlock(&shape_cache.mu);
    } else shaped_free(&t->sh);
    t->sh = (ShapedRun){0}; t->shape_ref = NULL; t->shaped = false;
}

void text_shape_cache_stats(unsigned *hits, unsigned *misses, size_t *entries) {
    pthread_mutex_lock(&shape_cache.mu);
    *hits = shape_cache.hits; *misses = shape_cache.misses; *entries = shape_cache.count;
    shape_cache.hits = shape_cache.misses = 0;
    pthread_mutex_unlock(&shape_cache.mu);
}

void text_shape_cache_sweep(void) {
    pthread_mutex_lock(&shape_cache.mu);
    unsigned now = ++shape_cache.stamp;
    /* over the idle budget, also drop runs released during the pass that just finished */
    unsigned keep = shape_cache.idle_bytes > SHAPE_IDLE_LIMIT ? 0 : SHAPE_KEEP_PASSES;
    for (size_t i = 0; i < shape_cache.nslot; i++)
        for (ShapeEntry **p = &shape_cache.slot[i], *e; (e = *p);) {
            if (e->refs || now - e->stamp <= keep) { p = &e->next; continue; }
            *p = e->next; shape_cache.count--; shape_cache.idle_bytes -= shape_bytes(e);
            shaped_free(&e->run); free(e);
        }
    pthread_mutex_unlock(&shape_cache.mu);
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

typedef struct { float line, mx, word, mn, edge_prefix; unsigned contents; bool after_atomic; } IState;
static void inline_intrinsic(Layout *L, Box *b, IState *s) {
    for (Box *c = b->first; c; c = c->next) {
        if (c->abs) continue;
        if (c->kind == BX_TEXT) {
            text_ensure_shaped(c);
            if (c->sh.n) {
                if (s->after_atomic) {
                    s->mn = LMAX(s->mn, LMAX(0, s->word - s->edge_prefix));
                    s->word = s->edge_prefix; s->after_atomic = false;
                }
                s->edge_prefix = 0; s->contents++;
            }
            bool wrap = c->st->white_space != WS_NOWRAP && c->st->white_space != WS_PRE;
            for (int i = 0; i < c->sh.n; i++) {
                char ch = c->text[c->sh.g[i].cluster];
                if (ch == '\n' && c->st->white_space != WS_NORMAL && c->st->white_space != WS_NOWRAP) { s->mx = LMAX(s->mx, s->line); s->line = 0; s->mn = LMAX(s->mn, s->word); s->word = 0; continue; }
                float a = c->sh.g[i].adv;
                s->line += a;
                if (ch == ' ' && wrap) { s->mn = LMAX(s->mn, s->word); s->word = 0; }
                else { s->word += a; if (wrap && (c->st->word_break == 1 || text_break_after(c, i))) { s->mn = LMAX(s->mn, s->word); s->word = 0; } }
            }
        } else if (c->kind == BX_BR) { s->mx = LMAX(s->mx, s->line); s->line = 0; s->mn = LMAX(s->mn, s->word); s->word = 0; s->edge_prefix = 0; s->after_atomic = false; s->contents++; }
        else if (c->kind == BX_INLINE) {
            compute_mbp(c, 0);
            float l = c->m[3] + c->b[3] + c->p[3], r = c->m[1] + c->b[1] + c->p[1];
            unsigned before = s->contents;
            s->line += l; s->word += l; s->edge_prefix += l;
            inline_intrinsic(L, c, s);
            s->line += r; s->word += r;
            if (s->contents == before) s->edge_prefix += r;
        } else {
            float mn, mx; intrinsic_outer(L, c, &mn, &mx);
            if (c->floated) { s->mn = LMAX(s->mn, mn); s->line += mx; continue; }
            /* An atomic inline permits a break on either side. Opening
               inline edges stay with this item; closing edges subsequently
               extend its contribution instead of disappearing at the break. */
            s->mn = LMAX(s->mn, LMAX(s->word - s->edge_prefix, 0));
            s->word = s->edge_prefix + mn; s->edge_prefix = 0;
            s->after_atomic = true; s->contents++; s->line += mx;
        }
    }
}

void intrinsic_outer(Layout *L, Box *b, float *mn, float *mx) {
    intrinsic(L, b, mn, mx);
    float m = (b->st->margin[1].kind == LK_LEN ? b->st->margin[1].px : 0) + (b->st->margin[3].kind == LK_LEN ? b->st->margin[3].px : 0);
    *mn += m; *mx += m;
}

static void intrinsic_measure(Layout *L, Box *b, float cbw, bool resolve_own, float *omn, float *omx) {
    if (!resolve_own && b->intr_ok) { *omn = b->imin; *omx = b->imax; return; }
    compute_mbp(b, resolve_own ? cbw : 0);
    const ComputedStyle *st = b->st;
    float bp = hbp(b), mn = 0, mx = 0;
    if (st->width.kind == LK_LEN && (resolve_own ? len_def(st->width, cbw) : st->width.pct == 0) && b->fmt != FMT_TABLE) {
        mn = mx = (resolve_own ? res(st->width, cbw) : st->width.px) - (st->box_sizing == BOX_BORDER ? bp : 0);
    } else if (b->fmt == FMT_REPLACED) {
        float w, h; replaced_size(L, b, -1, -1, -1, -1, &w, &h); mn = mx = w;
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
        for (int i = 0; i < st->grid_ncols; i++) {
            const GridTrack *t = &st->grid_cols[i];
            if (t->size.kind != LK_LEN || t->size.pct || t->fr) { fixed = false; continue; }
            float w = t->size.px;
            if (t->min.kind == LK_MIN_CONTENT && st->grid_ncols == 1) for (Box *c = b->first; c; c = c->next) if (in_flow(c)) w = LMAX(w, grid_item_auto_min(L, c, t->size.px));   /* minmax(auto, len); which items land in which of several columns is not known here */
            fsum += w;
        }
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
    if (st->max_width.kind == LK_LEN && (resolve_own ? len_def(st->max_width, cbw) : !st->max_width.pct)) { float v = (resolve_own ? res(st->max_width, cbw) : st->max_width.px) - (st->box_sizing == BOX_BORDER ? bp : 0); mx = LMIN(mx, v); mn = LMIN(mn, v); }
    if (st->min_width.kind == LK_LEN && (resolve_own ? len_def(st->min_width, cbw) : !st->min_width.pct)) { float v = (resolve_own ? res(st->min_width, cbw) : st->min_width.px) - (st->box_sizing == BOX_BORDER ? bp : 0); mx = LMAX(mx, v); mn = LMAX(mn, v); }
    if (resolve_own) { mn = LMAX(0, mn); mx = LMAX(0, mx); }
    if (mn > mx) mx = mn;
    *omn = mn + bp; *omx = mx + bp;
    if (!resolve_own) { b->imin = *omn; b->imax = *omx; b->intr_ok = true; }
}

void intrinsic(Layout *L, Box *b, float *mn, float *mx) {
    intrinsic_measure(L, b, 0, false, mn, mx);
}

/* Content-based width limits ignore the preferred width and other width
 * limits. Restore the style, edges and ordinary intrinsic cache afterwards;
 * the temporary definite padding basis must not leak into parent sizing. */
void intrinsic_content_width(Layout *L, Box *b, float cbw, float *mn, float *mx) {
    ComputedStyle measured = *b->st, *original = b->st;
    measured.width = L_auto(); measured.min_width = L_auto();
    measured.max_width = (Length){ .kind = LK_NONE };
    /* Intrinsic traversal also resolves descendant inline edges. This helper
       may run after layout, so retain every affected box's edges and cache;
       a size-preserving flex pass is allowed to reuse its existing geometry. */
    typedef struct {
        Box *box;
        float margin[4], border[4], padding[4], min, max;
        bool cached;
    } IntrinsicState;
    VEC(IntrinsicState) saved = {0};
    for (Box *node = b; node;) {
        IntrinsicState state = { .box = node, .min = node->imin,
                                 .max = node->imax, .cached = node->intr_ok };
        memcpy(state.margin, node->m, sizeof state.margin);
        memcpy(state.border, node->b, sizeof state.border);
        memcpy(state.padding, node->p, sizeof state.padding);
        vec_push(saved, state);
        if (node->first) { node = node->first; continue; }
        while (node != b && !node->next) node = node->parent;
        if (node == b) break;
        node = node->next;
    }
    b->st = &measured; b->intr_ok = false;
    intrinsic_measure(L, b, cbw >= 0 ? cbw : 0, cbw >= 0, mn, mx);
    b->st = original;
    for (int i = 0; i < saved.n; i++) {
        IntrinsicState *state = &saved.v[i]; Box *node = state->box;
        memcpy(node->m, state->margin, sizeof state->margin);
        memcpy(node->b, state->border, sizeof state->border);
        memcpy(node->p, state->padding, sizeof state->padding);
        node->imin = state->min; node->imax = state->max; node->intr_ok = state->cached;
    }
    vec_free(saved);
}

/* A positioned box has a definite percentage containing block even when its
   width is shrink-to-fit. Resolve its own edges and constraints there; child
   percentages still participate in the ordinary cyclic intrinsic algorithm.
   Keep this result out of the zero-basis intrinsic cache used by its parent. */
void intrinsic_with_basis(Layout *L, Box *b, float cbw, float *mn, float *mx) {
    intrinsic_measure(L, b, cbw, true, mn, mx);
}

/* CSS Sizing 4: transfer only definite constraints, then clamp each axis
 * independently. A destination's own preferred/min/max size always wins over
 * a transferred constraint. Coordinates below use the ratio's sizing box. */
static float replaced_bound(Length value, float reference, float subtract, float add, float fallback) {
    return len_def(value, reference) ? LMAX(0, res(value, reference) - subtract) + add : fallback;
}
static bool replaced_auto_ratio(const char *value) {
    if (!value || !*value) return true;
    for (const char *p = value; *p;) {
        while (is_ws((unsigned char)*p)) p++;
        if (str_istarts(p, "auto") && (!p[4] || is_ws((unsigned char)p[4]))) return true;
        while (*p && !is_ws((unsigned char)*p)) p++;
    }
    return false;
}
void replaced_size(Layout *L, Box *b, float cbw, float cbh, float fw, float fh, float *ow, float *oh) {
    Node *n = b->node; const ComputedStyle *st = b->st;
    float nw = 300, nh = 150, ratio = 0, natural_ratio = 0;
    bool contained = false, have_width = true;
    if (b->ctl == CTL_CHECK || b->ctl == CTL_RADIO) nw = nh = 13;
    else if (b->ctl == CTL_RANGE) { nw = 129; nh = 16; }
    else if (n && n->tag == A_img) {
        float w, h;
        if (css_img_sizes_auto(n)) contained = true; /* UA size containment has no natural ratio. */
        else if (layout_image_size_hook && layout_image_size_hook(n, &w, &h) && w > 0) {
            nw = w; nh = h; if (h > 0) natural_ratio = w / h;
        } else {
            nw = nh = 0; const char *aw = node_attr(n, "width"), *ah = node_attr(n, "height");
            if (aw) nw = (float)atof(aw); if (ah) nh = (float)atof(ah); have_width = aw != NULL;
        }
    } else if (n && n->ns == NS_SVG) {
        const char *viewbox = node_attr(n, "viewBox"); float v[4]; int nv = 0;
        for (const char *q = viewbox; q && *q && nv < 4;) { char *e; float f = strtof(q, &e); if (e == q) break; v[nv++] = f; q = e; while (*q == ' ' || *q == ',' || *q == '\t' || *q == '\n') q++; }
        if (nv == 4 && v[3] > 0) { ratio = natural_ratio = v[2] / v[3]; nw = v[2]; nh = v[3]; }
        /* Preserve the existing SVG intrinsic-size path; viewBox-only flex
           sizing is a separate issue from preferred ratio/constraint transfer. */
        bool outer = !n->parent || n->parent->type != NODE_ELEMENT || n->parent->ns != NS_SVG;
        if (outer && ratio > 0 && cbw > 0 && !node_attr(n, "width") && !node_attr(n, "height")) { nw = cbw; nh = cbw / ratio; }
    } else if (n && n->tag == A_video) {
        float w, h;
        if (layout_image_size_hook && layout_image_size_hook(n, &w, &h) && w > 0) { nw = w; nh = h; if (h > 0) natural_ratio = w / h; }
    } else if (n && n->tag == A_canvas) {
        const char *aw = node_attr(n, "width"), *ah = node_attr(n, "height");
        if (aw) nw = (float)atof(aw); if (ah) nh = (float)atof(ah);
        if (nh > 0) natural_ratio = nw / nh;
    } else if (n && n->tag == A_audio) { nw = 300; nh = 54; }
    /* A default object size (for example iframe300x150) is not a natural ratio. */
    if (!contained && natural_ratio > 0) ratio = natural_ratio;
    bool auto_ratio = replaced_auto_ratio(custom_get(st->gen_n, "aspect-ratio"));
    bool authored_ratio = st->aspect_ratio > 0 && isfinite(st->aspect_ratio);
    if (authored_ratio && (!auto_ratio || natural_ratio <= 0)) ratio = st->aspect_ratio;
    else if (auto_ratio && natural_ratio > 0) ratio = natural_ratio;
    if (!isfinite(ratio) || ratio <= 0) ratio = 0;
    bool bs = st->box_sizing == BOX_BORDER;
    float hb = hbp(b), vb = vbp(b), subtract_w = bs ? hb : 0, subtract_h = bs ? vb : 0;
    /* auto && <ratio> always uses content dimensions; a bare CSS ratio uses
       the box selected by box-sizing. Natural sizes themselves are content sizes. */
    bool border_ratio = authored_ratio && !auto_ratio && bs;
    float rw = border_ratio ? hb : 0, rh = border_ratio ? vb : 0;
    /* Flex/grid may already have chosen a border-box size. It determines the
       dependent automatic axis, and must not be clamped a second time here. */
    bool wdef = fw >= 0 || len_def(st->width, cbw), hdef = fh >= 0 || len_def(st->height, cbh);
    float w = fw >= 0 ? LMAX(0, fw - hb) + rw : wdef ? LMAX(0, res(st->width, cbw) - subtract_w) + rw : -1;
    float h = fh >= 0 ? LMAX(0, fh - vb) + rh : hdef ? LMAX(0, res(st->height, cbh) - subtract_h) + rh : -1;
    if (wdef && !hdef) h = ratio > 0 ? w / ratio : nh + rh;
    else if (hdef && !wdef) w = ratio > 0 ? h * ratio : nw + rw;
    else if (!wdef && !hdef) {
        if (ratio > 0 && !have_width && nh > 0) { h = nh + rh; w = h * ratio; }
        else { w = nw + rw; h = ratio > 0 ? w / ratio : nh + rh; }
    }
    float minw = replaced_bound(st->min_width, cbw, subtract_w, rw, rw);
    float minh = replaced_bound(st->min_height, cbh, subtract_h, rh, rh);
    float maxw = LMAX(minw, replaced_bound(st->max_width, cbw, subtract_w, rw, INFINITY));
    float maxh = LMAX(minh, replaced_bound(st->max_height, cbh, subtract_h, rh, INFINITY));
    if (ratio > 0) {
        float transferred_minw = LMIN(minh * ratio, LMIN(maxw, wdef ? w : INFINITY));
        float transferred_minh = LMIN(minw / ratio, LMIN(maxh, hdef ? h : INFINITY));
        float transferred_maxw = LMAX(maxh * ratio, LMAX(minw, LMAX(wdef ? w : rw, transferred_minw)));
        float transferred_maxh = LMAX(maxw / ratio, LMAX(minh, LMAX(hdef ? h : rh, transferred_minh)));
        minw = LMAX(minw, transferred_minw); minh = LMAX(minh, transferred_minh);
        maxw = LMIN(maxw, transferred_maxw); maxh = LMIN(maxh, transferred_maxh);
    }
    w = LMAX(minw, LMIN(maxw, w)); h = LMAX(minh, LMIN(maxh, h));
    *ow = fw >= 0 ? LMAX(0, fw - hb) : LMAX(0, w - rw);
    *oh = fh >= 0 ? LMAX(0, fh - vb) : LMAX(0, h - rh);
}
