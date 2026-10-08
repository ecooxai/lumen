/* C-side helpers for the V8 bindings (keeps C-only headers out of C++) */
#include "jsglue.h"
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
