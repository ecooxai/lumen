/* C-side helpers for the V8 bindings (keeps C-only headers out of C++) */
#include "jsglue.h"
#include <math.h>
#include <string.h>
#include "../css/css.h"
#include "../layout/layout.h"

int jsg_query(Node *root, const char *sel, bool all, Node ***out) {
    NodeVec v = {0};
    Node *first = css_query(root, sel, all, all ? &v : NULL);
    if (!all) { *out = NULL; if (!first) return 0; *out = xmalloc(sizeof(Node *)); (*out)[0] = first; return 1; }
    *out = v.v; return v.n;
}

bool jsg_matches(Node *el, const char *sel, bool *ok) {
    SelList l = {0};
    *ok = css_parse_selector_list(sel, &l);
    if (!*ok) return false;
    bool m = css_match_selector_list(&l, el);
    css_sellist_free(&l);
    return m;
}

char *jsg_computed(Node *el, const char *prop) { return css_get_computed_value(el, prop); }

bool jsg_rect(Node *n, float r[4]) {
    struct Box *b = n->box;
    if (!b) return false;
    r[0] = b->x; r[1] = b->y; r[2] = b->w; r[3] = b->h;
    return true;
}

bool jsg_media(void *media, const char *q) { return media && css_media_matches(q, (const MediaCtx *)media); }

bool jsg_valid_selector(const char *sel) {
    SelList l = {0};
    bool ok = css_parse_selector_list(sel, &l);
    css_sellist_free(&l);
    return ok;
}

bool jsg_supports(const char *cond) { return css_supports(cond); }

bool jsg_img_size(Node *n, float *w, float *h) {
    return layout_image_size_hook && layout_image_size_hook(n, w, h);
}

bool jsg_classic_script(Node *s) {
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
    if (nn && nn != on) {
        js_anim_event(n, "animationstart", nn, s->anim_delay * 1000, 0, true);
        if (isfinite(s->anim_iter)) { double d = s->anim_dur * s->anim_iter; js_anim_event(n, "animationend", nn, (s->anim_delay + d) * 1000, d, true); }
    }
    if (!o || !vis || o->display == D_NONE || s->tr_dur + s->tr_delay <= 0) return;
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
