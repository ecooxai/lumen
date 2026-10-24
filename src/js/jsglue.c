/* C-side helpers for the V8 bindings (keeps C-only headers out of C++) */
#include "jsglue.h"
#include <math.h>
#include <string.h>
#include "../css/css.h"
#include "../layout/layout.h"
#include "../paint/paint.h"
#include "../base/util.h"

#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
/* Debug Android: the selectors pages query most, by time, every 2 s (selector text is page code). */
static struct { char *sel; bool all; int calls, found; double ms; } g_qstat[64];
static double g_qstat_t0;
static void query_stat(const char *sel, bool all, int found, double ms) {
    double now = now_ms();
    if (!g_qstat_t0) g_qstat_t0 = now;
    uint32_t h = 2166136261u;
    for (const char *p = sel; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    for (int i = 0; i < 64; i++) {
        int k = (h + i) % 64;
        if (g_qstat[k].sel && (g_qstat[k].all != all || strcmp(g_qstat[k].sel, sel))) continue;
        if (!g_qstat[k].sel) { g_qstat[k].sel = xstrdup(sel); g_qstat[k].all = all; }
        g_qstat[k].calls++; g_qstat[k].found += found; g_qstat[k].ms += ms;
        break;
    }
    if (now - g_qstat_t0 < 2000) return;
    double total = 0; int calls = 0;
    for (int i = 0; i < 64; i++) { total += g_qstat[i].ms; calls += g_qstat[i].calls; }
    if (total >= 5) {
        char line[900]; int n = snprintf(line, sizeof line, "window_ms=%.0f calls=%d ms=%.1f top", now - g_qstat_t0, calls, total);
        for (int round = 0; round < 6 && n < (int)sizeof line - 1; round++) {
            int best = -1;
            for (int i = 0; i < 64; i++) if (g_qstat[i].sel && g_qstat[i].calls > 0 && (best < 0 || g_qstat[i].ms > g_qstat[best].ms)) best = i;
            if (best < 0) break;
            n += snprintf(line + n, sizeof line - n, " %dx%.1fms/%d%s{%.60s}", g_qstat[best].calls, g_qstat[best].ms, g_qstat[best].found, g_qstat[best].all ? "a" : "1", g_qstat[best].sel);
            g_qstat[best].calls = -g_qstat[best].calls;   /* reported */
        }
        __android_log_print(ANDROID_LOG_INFO, "LumenQuery", "%s", line);
    }
    for (int i = 0; i < 64; i++) { free(g_qstat[i].sel); g_qstat[i].sel = NULL; g_qstat[i].calls = g_qstat[i].found = 0; g_qstat[i].ms = 0; }
    g_qstat_t0 = now;
}
#endif

/* -1: the selector is invalid (its parse is cached with the document's). */
int jsg_query(Node *root, const char *sel, bool all, Node ***out) {
    NodeVec v = {0};
#if defined(__ANDROID__) && !defined(NDEBUG)
    double t0 = now_ms();
#endif
    Node *first = css_query(root, sel, all, all ? &v : NULL);
    *out = NULL;
    if (first == (Node *)-1) { free(v.v); return -1; }
#if defined(__ANDROID__) && !defined(NDEBUG)
    query_stat(sel, all, all ? v.n : first != NULL, now_ms() - t0);
#endif
    if (!all) { if (!first) return 0; *out = xmalloc(sizeof(Node *)); (*out)[0] = first; return 1; }
    *out = v.v; return v.n;
}

bool jsg_matches(Node *el, const char *sel, Node *scope, bool *ok) {
    SelList scratch = {0};
    const SelList *parsed = css_query_syntax(el->doc, sel, &scratch);
    *ok = parsed != NULL;
    bool m = parsed && css_match_selector_list_scoped(parsed, el, scope);
    css_sellist_free(&scratch);
    return m;
}

char *jsg_computed(Node *el, const char *prop) { return css_get_computed_value(el, prop); }
void jsg_set_anim_style(Node *el, const char *css) { if (el->type == NODE_ELEMENT) css_set_anim_style(el, css); }

uint64_t jsg_layout_generation(void) { return layout_generation(); }
uint64_t jsg_visual_generation(void) { return doc_visual_generation(); }
void jsg_visual_changed(void) { doc_visual_changed(); }
uint64_t jsg_activity_generation(void) { return doc_activity_generation(); }

bool jsg_rect(Node *n, float r[4]) {
    struct Box *b = n->box;
    if (!b) return false;
    r[0] = b->x; r[1] = b->y; r[2] = b->w; r[3] = b->h;
    return true;
}

/* Viewport border box: a fixed ancestor stops document scrolling, while
   element scrollers and translations still affect its descendants. */
bool jsg_vrect(Node *n, float r[4], float scroll_x, float scroll_y) {
    struct Box *b = n->box;
    if (!b) return false;
    r[0] = b->x; r[1] = b->y; r[2] = b->w; r[3] = b->h;
    for (struct Box *a = b; a; ) {
        const ComputedStyle *s = a->st;
        float sticky_x, sticky_y;
        box_sticky_offset(a, scroll_x, scroll_y, &sticky_x, &sticky_y);
        r[0] += sticky_x; r[1] += sticky_y;
        if (s && s->has_transform) {
            r[0] += s->transform[4] + s->translate_pending[0].pct * a->w / 100;
            r[1] += s->transform[5] + s->translate_pending[1].pct * a->h / 100;
        }
        if (a->fixed) return true;
        struct Box *up = a->abs && a->cb ? a->cb : a->parent;
        if (up && up->scroller && up->node && up->st && (up->st->overflow_x != OV_VISIBLE || up->st->overflow_y != OV_VISIBLE)) {
            r[0] -= up->node->scroll_x; r[1] -= box_scroll_y(up);
        }
        a = up;
    }
    r[0] -= scroll_x; r[1] -= scroll_y;
    return true;
}

/* The viewport rectangle that clipping ancestors (overflow other than visible) leave for a box, up to
   but not including stop: its descendants are cut to their padding boxes, as IntersectionObserver sees
   them. False without a box; an unclipped axis spans everything. */
bool jsg_vclip(Node *n, Node *stop, float c[4], float scroll_x, float scroll_y) {
    struct Box *b = n->box;
    if (!b) return false;
    c[0] = c[1] = -INFINITY; c[2] = c[3] = INFINITY;
    for (struct Box *a = b; a && !a->fixed; ) {
        struct Box *up = a->abs && a->cb ? a->cb : a->parent;
        if (!up || (stop && up->node == stop)) break;
        const ComputedStyle *s = up->st;
        bool viewport = up->node && (up->node->tag == A_html || up->node->tag == A_body);   /* their overflow is the viewport's */
        if (s && up->node && !viewport && (s->overflow_x != OV_VISIBLE || s->overflow_y != OV_VISIBLE)) {
            float r[4];
            if (jsg_vrect(up->node, r, scroll_x, scroll_y) && up->node->box == up) {
                if (s->overflow_x != OV_VISIBLE) { c[0] = fmaxf(c[0], r[0] + up->b[3]); c[2] = fminf(c[2], r[0] + r[2] - up->b[1]); }
                if (s->overflow_y != OV_VISIBLE) { c[1] = fmaxf(c[1], r[1] + up->b[0]); c[3] = fminf(c[3], r[1] + r[3] - up->b[2]); }
            }
        }
        a = up;
    }
    return true;
}

/* A text node's data byte offset -> its box text's, whose white space the box tree collapsed (prep_text): a dropped
   character maps to where the next kept one is */
static int box_byte(const Node *n, const Box *t, int o) {
    bool pre = t->st && !(t->st->white_space == WS_NORMAL || t->st->white_space == WS_NOWRAP || t->st->white_space == WS_PRE_LINE);
    int k = 0;
    for (int i = 0; i < o && i < (int)n->text_len && k < t->text_len; i++) {
        unsigned char c = (unsigned char)n->text[i];
        if (c == '\r') continue;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
            if (t->text[k] == ' ' || t->text[k] == '\n') { k++; if (c == '\t' && pre) for (int j = 0; j < 3 && k < t->text_len && t->text[k] == ' '; j++) k++; }
        } else k++;
    }
    return k;
}
/* Rects (x y w h, document coordinates) over box text bytes [s, e) of t, one per fragment owner holds; s == e gives
   the caret's zero-width rect */
static int frag_rects(const Box *owner, const Box *t, int s, int e, float dx, float dy, float *out, int max) {
    int n = 0;
    for (int i = 0; i < owner->nfrags && n < max; i++) {
        const TextFrag *f = &owner->frags[i];
        if (f->box != t || f->g1 <= f->g0) continue;
        int lo = (int)t->sh.g[f->g0].cluster, hi = f->g1 < t->sh.n ? (int)t->sh.g[f->g1].cluster : t->text_len;
        if (s == e ? s < lo || s > hi : e <= lo || s >= hi) continue;
        float x0 = t->sh.g[f->g0].x, a = -1, z = -1;
        for (int g = f->g0; g < f->g1; g++) {
            int cl = (int)t->sh.g[g].cluster; float gx = t->sh.g[g].x - x0;
            if (a < 0 && cl >= s) a = gx;
            if (z < 0 && cl >= e) z = gx;
        }
        if (a < 0) a = f->w;
        if (z < 0) z = f->w;
        out[n * 4] = f->x + LMIN(a, z) + dx; out[n * 4 + 1] = f->y + dy; out[n * 4 + 2] = fabsf(z - a); out[n * 4 + 3] = f->h;
        n++;
        if (s == e) break;
    }
    return n;
}
static int u16_byte(const char *s, int len, int u) {
    int i = 0;
    while (i < len && u > 0) {
        unsigned char c = (unsigned char)s[i];
        int w = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        u -= w == 4 ? 2 : 1; i += w;
    }
    return i < len ? i : len;
}
/* The rects of UTF-16 offsets [s, e) of a text node's data, document coordinates; its parent element's own scroll
   applies, as the parent's viewport rect does not include it */
int jsg_text_rects(Node *n, int s, int e, float *out, int max) {
    const Box *t = n->box;
    if (!t || t->kind != BX_TEXT || !t->text) return 0;
    const Box *owner = NULL;
    for (const Box *a = t->parent; a && !owner; a = a->parent)
        for (int i = 0; i < a->nfrags; i++) if (a->frags[i].box == t) { owner = a; break; }
    if (!owner) return 0;
    float dx = 0, dy = 0;
    for (const Box *a = t->parent; a; a = a->parent) {
        if (a->scroller && a->node) { dx -= a->node->scroll_x; dy -= box_scroll_y(a); }
        if (a->node && a->node == n->parent) break;
    }
    int bs = box_byte(n, t, u16_byte(n->text, (int)n->text_len, s)), be = box_byte(n, t, u16_byte(n->text, (int)n->text_len, e));
    return frag_rects(owner, t, bs, be, dx, dy, out, max);
}
/* The rects of an <input>/<textarea> value's codepoints [s, e), document coordinates with the control's own scroll;
   an empty value gives the caret at the content box's start */
int jsg_ctl_rects(Node *el, int s, int e, float *out, int max) {
    const Box *cb = el->box;
    if (!cb || (cb->ctl != CTL_TEXT && cb->ctl != CTL_TEXTAREA) || max < 1) return 0;
    float dx = 0, dy = 0;
    if (cb->scroller && cb->node) { dx -= cb->node->scroll_x; dy -= box_scroll_y(cb); }
    const Box *t = ctl_text_box(cb);
    if (!t) {
        if (s != e) return 0;
        Font *f = cb->st ? style_font(cb->st) : NULL;
        out[0] = cb->x + cb->b[3] + cb->p[3]; out[1] = cb->y + cb->b[0] + cb->p[0]; out[2] = 0;
        out[3] = f ? style_line_height(cb->st, f) : cb->h - cb->b[0] - cb->b[2] - cb->p[0] - cb->p[2];
        return 1;
    }
    return frag_rects(cb, t, u8_cp_byte(t->text, t->text_len, s), u8_cp_byte(t->text, t->text_len, e), dx, dy, out, max);
}

bool jsg_scroll(Node *n, float r[4]) {
    struct Box *b = n->box;
    if (!b || !b->scroller) return false;
    r[0] = n->scroll_x; r[1] = n->scroll_y; r[2] = LMAX(b->scroll_w, b->w); r[3] = LMAX(b->scroll_h, b->h);
    return true;
}

bool jsg_set_scroll(Node *n, float x, float y) {
    struct Box *b = n->box;
    if (!b || !b->scroller) return false;
    float maxx = LMAX(0, b->scroll_w - b->w), maxy = LMAX(0, b->scroll_h - b->h);
    float nx = isnan(x) ? n->scroll_x : LCLAMP(x, 0, maxx);
    float ny = isnan(y) ? n->scroll_y : box_scroll_from_end(b) ? LCLAMP(y, -maxy, 0) : LCLAMP(y, 0, maxy);
    if (nx == n->scroll_x && ny == n->scroll_y) return false;
    /* Scrolling changes geometry in viewport coordinates, not computed styles
       or layout. Autoscrolling a streamed reply must not restyle its subtree. */
    n->scroll_x = nx; n->scroll_y = ny;
    if (n->doc) n->doc->paint_version++;
    doc_visual_changed();
    return true;
}

bool jsg_media(void *media, const char *q) { return media && css_media_matches(q, (const MediaCtx *)media); }

bool jsg_supports(const char *cond) { return css_supports(cond); }

bool (*jsg_img_done_hook)(Node *n);
bool jsg_img_done(Node *n) { return jsg_img_done_hook && jsg_img_done_hook(n); }
bool jsg_img_size(Node *n, float *w, float *h) {
    return layout_image_size_hook && layout_image_size_hook(n, w, h);
}

uint8_t *jsg_decode_rgba(const uint8_t *d, size_t n, int *w, int *h) {
    Image *im = image_decode_bitmap(d, n);
    if (!im || !im->px) { image_unref(im); return NULL; }
    size_t np = (size_t)im->w * (size_t)im->h;
    uint8_t *o = xmalloc(np * 4);
    for (size_t i = 0; i < np; i++) {
        uint32_t v = im->px[i], al = v >> 24;
        uint32_t r = v >> 16 & 0xff, g = v >> 8 & 0xff, b = v & 0xff;
        if (al && al < 255) { r = (r * 255 + al / 2) / al; g = (g * 255 + al / 2) / al; b = (b * 255 + al / 2) / al; }
        o[i * 4] = (uint8_t)(r > 255 ? 255 : r); o[i * 4 + 1] = (uint8_t)(g > 255 ? 255 : g); o[i * 4 + 2] = (uint8_t)(b > 255 ? 255 : b); o[i * 4 + 3] = (uint8_t)al;
    }
    *w = im->w; *h = im->h;
    image_unref(im);
    return o;
}

bool jsg_module_script(Node *s) {
    const char *t = node_attr(s, "type");
    if (!t) return false;
    while (*t == ' ' || *t == '\t' || *t == '\n') t++;
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\n')) n--;
    return n == 6 && str_istarts(t, "module");
}

bool jsg_classic_script(Node *s) {
    if (node_has_attr(s, "nomodule")) return false;
    const char *t = node_attr(s, "type");
    if (!t) return true;
    while (*t == ' ' || *t == '\t' || *t == '\n') t++;
    if (!*t) return true;
    static const char *ok[] = { "text/javascript", "application/javascript", "text/ecmascript", "application/ecmascript",
                                "application/x-javascript", "text/x-javascript", "text/jscript", "text/livescript" };
    for (size_t i = 0; i < sizeof ok / sizeof *ok; i++)
        if (str_istarts(t, ok[i]) && (!t[strlen(ok[i])] || t[strlen(ok[i])] == ';' || t[strlen(ok[i])] == ' ')) return true;
    return false;
}

static bool hidden_chain(Node *n) {
    for (; n && n->type == NODE_ELEMENT; n = n->parent) if (n->style && n->style->display == D_NONE) return true;
    return false;
}
static bool tr_allows(const char *list, const char *p) {
    if (!list) return true;
    size_t pl = strlen(p);
    for (const char *s = list;;) {
        const char *e = strchr(s, ','); size_t n = e ? (size_t)(e - s) : strlen(s);
        if ((n == 3 && !strncmp(s, "all", 3)) || (n == pl && !strncmp(s, p, n))) return true;
        if (!e) return false;
        s = e + 1;
    }
}
static bool len_ne(Length a, Length b) { return a.kind != b.kind || a.px != b.px || a.pct != b.pct; }

/* Lumen does not interpolate CSS animations/transitions, but pages (Google sign-in, YouTube) wait for their events. */
static void style_change(Node *n, const ComputedStyle *o, const ComputedStyle *s) {
    bool vis = s->display != D_NONE && !hidden_chain(n->parent);
    const char *on = o && o->display != D_NONE ? o->anim_name : NULL, *nn = vis ? s->anim_name : NULL;
    if (on && on != nn) js_anim_cancel(n, on);
    if ((on || nn) && (!on || !nn || strcmp(on, nn))) js_animation_names(n, nn ? nn : "");
    if (nn && nn != on) {
        js_anim_event(n, "animationstart", nn, s->anim_delay * 1000, s->anim_delay < 0 ? -s->anim_delay : 0, true);
        if (isfinite(s->anim_iter)) {
            double d = s->anim_dur * s->anim_iter;
            /* animationiteration at each iteration boundary still ahead (a negative delay starts part way through) */
            if (s->anim_dur > 0)
                for (int k = 1, sent = 0; k < s->anim_iter && sent < 32; k++)
                    if (s->anim_delay + k * s->anim_dur >= 0) { js_anim_event(n, "animationiteration", nn, (s->anim_delay + k * s->anim_dur) * 1000, k * s->anim_dur, true); sent++; }
            js_anim_event(n, "animationend", nn, (s->anim_delay + d) * 1000, d, true);
        }
    }
    if (!o || !vis || o->display == D_NONE || s->tr_dur + s->tr_delay <= 0) return;
    css_transition_scan(n, o, s, js_transition_start);
    return;   /* transition events come from the JS CSSTransitions */
    struct { const char *p; bool ch; } c[] = {
        { "opacity", o->opacity != s->opacity },
        { "transform", o->has_transform != s->has_transform || memcmp(o->transform, s->transform, sizeof s->transform) != 0 },
        { "color", o->color != s->color }, { "background-color", o->bg_color != s->bg_color },
        { "visibility", o->visibility != s->visibility },
        { "width", len_ne(o->width, s->width) }, { "height", len_ne(o->height, s->height) },
        { "max-width", len_ne(o->max_width, s->max_width) }, { "max-height", len_ne(o->max_height, s->max_height) },
        { "top", len_ne(o->inset[0], s->inset[0]) }, { "right", len_ne(o->inset[1], s->inset[1]) },
        { "bottom", len_ne(o->inset[2], s->inset[2]) }, { "left", len_ne(o->inset[3], s->inset[3]) },
        { "margin-top", len_ne(o->margin[0], s->margin[0]) }, { "margin-left", len_ne(o->margin[3], s->margin[3]) },
        { "filter", o->filter_blur != s->filter_blur || o->filter_brightness != s->filter_brightness },
    };
    for (size_t i = 0; i < sizeof c / sizeof *c; i++)
        if (c[i].ch && tr_allows(s->tr_prop, c[i].p)) {
            js_anim_event(n, "transitionrun", c[i].p, 0, 0, false);
            js_anim_event(n, "transitionstart", c[i].p, s->tr_delay * 1000, 0, false);
            js_anim_event(n, "transitionend", c[i].p, (s->tr_delay + s->tr_dur) * 1000, s->tr_dur, false);
        }
}
void jsg_install_hooks(void) { css_style_change_hook = style_change; }
