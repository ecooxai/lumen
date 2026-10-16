/* Selector matching and querySelector */
#include "css.h"

enum { MATCH_CACHE_SIZE = 8192 };
typedef struct {
    const Compound *compound;
    Node *element, *scope;
    uint64_t version;
    uint32_t epoch;
    bool result;
} MatchEntry;
struct CssMatchCache {
    uint32_t epoch;
    Document *document;
    uint64_t generation;
    Node *subject;
    const uint64_t *ancestor_classes;
    MatchEntry entries[MATCH_CACHE_SIZE];
};
static _Thread_local CssMatchCache *active_match_cache;
CssMatchCache *css_match_cache_begin(CssMatchCache **cache) {
    if (!*cache) *cache = xcalloc(1, sizeof **cache);
    if (!++(*cache)->epoch) { memset((*cache)->entries, 0, sizeof (*cache)->entries); (*cache)->epoch = 1; }
    (*cache)->document = NULL;
    (*cache)->subject = NULL;
    CssMatchCache *previous = active_match_cache;
    active_match_cache = *cache;
    return previous;
}
CssMatchCache *css_match_cache_begin_style(CssMatchCache **cache, Document *doc,
        uint64_t generation, bool force) {
    if (!*cache) *cache = xcalloc(1, sizeof **cache);
    CssMatchCache *c = *cache;
    if (force || c->document != doc || c->generation != generation) {
        if (!++c->epoch) { memset(c->entries, 0, sizeof c->entries); c->epoch = 1; }
    }
    c->document = doc; c->generation = generation;
    c->subject = NULL;
    CssMatchCache *previous = active_match_cache;
    active_match_cache = c;
    return previous;
}
void css_match_cache_subject(Node *subject, const uint64_t ancestor_classes[16]) {
    if (!active_match_cache) return;
    active_match_cache->subject = subject;
    active_match_cache->ancestor_classes = ancestor_classes;
}
void css_match_cache_end(CssMatchCache *previous) { active_match_cache = previous; }
void css_match_cache_free(CssMatchCache *cache) { free(cache); }

static Node *parent_el(Node *n) { Node *p = n->parent; return p && p->type == NODE_ELEMENT ? p : NULL; }
static Node *prev_el(Node *n) { for (n = n->prev; n; n = n->prev) if (n->type == NODE_ELEMENT) return n; return NULL; }
static Node *next_el(Node *n) { for (n = n->next; n; n = n->next) if (n->type == NODE_ELEMENT) return n; return NULL; }

static bool nth_match(int a, int b, int idx) {
    if (a == 0) return idx == b;
    int d = idx - b;
    return d % a == 0 && d / a >= 0;
}

static bool html_ci_attr(const char *n) {
    static const char *const names[] = { "accept", "accept-charset", "align", "alink", "axis", "bgcolor", "charset", "checked", "clear", "codetype", "color", "compact", "declare", "defer", "dir", "direction", "disabled", "enctype", "face", "frame", "hreflang", "http-equiv", "lang", "language", "link", "media", "method", "multiple", "nohref", "noresize", "noshade", "nowrap", "readonly", "rel", "rev", "rules", "scope", "scrolling", "selected", "shape", "target", "text", "type", "valign", "valuetype", "vlink" };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (!strcmp(n, names[i])) return true;
    return false;
}
static bool attr_match(const SimpleSel *s, Node *el) {
    const char *v = node_attr_atom(el, s->name);
    if (!v) return false;
    if (!s->op) return true;
    const char *w = s->value; size_t wl = strlen(w), vl = strlen(v);
    bool ci = s->ci == 1 || (!s->ci && el->ns == NS_HTML && html_ci_attr(s->name));
    int (*cmp)(const char *, const char *, size_t) = ci ? (int (*)(const char *, const char *, size_t))strncasecmp : strncmp;
    switch (s->op) {
    case '=': return vl == wl && !cmp(v, w, wl);
    case '^': return wl && vl >= wl && !cmp(v, w, wl);
    case '$': return wl && vl >= wl && !cmp(v + vl - wl, w, wl);
    case '*': if (!wl) return false; for (size_t i = 0; i + wl <= vl; i++) if (!cmp(v + i, w, wl)) return true; return false;
    case '|': return (vl == wl && !cmp(v, w, wl)) || (vl > wl && !cmp(v, w, wl) && v[wl] == '-');
    case '~': {
        if (!wl) return false;
        const char *p = v;
        while (*p) { while (is_ws((unsigned char)*p)) p++; const char *st = p; while (*p && !is_ws((unsigned char)*p)) p++; if ((size_t)(p - st) == wl && !cmp(st, w, wl)) return true; }
        return false; }
    }
    return false;
}

static bool is_link(Node *el) { return (el->tag == A_a || el->tag == A_area || el->tag == A_link) && el->ns == NS_HTML && (node_attr_atom(el, A_href) != NULL); }
static bool is_disabled(Node *el) {
    if (el->ns != NS_HTML) return false;
    if ((el->tag == A_button || el->tag == A_input || el->tag == A_select || el->tag == A_textarea || el->tag == A_option || el->tag == A_optgroup || el->tag == A_fieldset) && (node_attr_atom(el, A_disabled) != NULL)) return true;
    return false;
}

static bool match_compound(const Compound *c, Node *el, Node *scope);
static bool match_pseudo(const SimpleSel *s, Node *el, Node *scope) {
    const char *n = s->name;
    static const char *P_hover, *P_active, *P_focus, *P_first_child, *P_last_child, *P_only_child, *P_nth_child, *P_nth_last_child,
        *P_nth_of_type, *P_nth_last_of_type, *P_first_of_type, *P_last_of_type, *P_only_of_type, *P_not, *P_is, *P_where, *P_has, *P_root, *P_empty,
        *P_checked, *P_disabled, *P_enabled, *P_link, *P_any_link, *P_visited, *P_focus_within, *P_focus_visible, *P_scope, *P_defined,
        *P_host, *P_placeholder_shown, *P_matches, *P_webkit_any, *P_required, *P_optional, *P_lang, *P_target, *P_read_only, *P_read_write, *P_default, *P_moz_any, *P_dir, *P_popover_open, *P_modal, *P_fullscreen, *P_playing, *P_paused;
    if (!P_hover) {
        P_hover = atom("hover"); P_active = atom("active"); P_focus = atom("focus"); P_first_child = atom("first-child"); P_last_child = atom("last-child");
        P_only_child = atom("only-child"); P_nth_child = atom("nth-child"); P_nth_last_child = atom("nth-last-child"); P_nth_of_type = atom("nth-of-type");
        P_nth_last_of_type = atom("nth-last-of-type"); P_first_of_type = atom("first-of-type"); P_last_of_type = atom("last-of-type"); P_only_of_type = atom("only-of-type");
        P_not = atom("not"); P_is = atom("is"); P_where = atom("where"); P_has = atom("has"); P_root = atom("root"); P_empty = atom("empty"); P_checked = atom("checked");
        P_disabled = atom("disabled"); P_enabled = atom("enabled"); P_link = atom("link"); P_any_link = atom("any-link"); P_visited = atom("visited");
        P_focus_within = atom("focus-within"); P_focus_visible = atom("focus-visible"); P_scope = atom("scope"); P_defined = atom("defined"); P_host = atom("host");
        P_placeholder_shown = atom("placeholder-shown"); P_matches = atom("matches"); P_webkit_any = atom("-webkit-any"); P_required = atom("required");
        P_optional = atom("optional"); P_lang = atom("lang"); P_target = atom("target"); P_read_only = atom("read-only"); P_read_write = atom("read-write");
        P_default = atom("default"); P_moz_any = atom("-moz-any"); P_dir = atom("dir"); P_popover_open = atom("popover-open"); P_modal = atom("modal"); P_fullscreen = atom("fullscreen");
        P_playing = atom("playing"); P_paused = atom("paused");
    }
    if (n == P_hover) return el->flags & NF_HOVER;
    if (n == P_active) return el->flags & NF_ACTIVE;
    if (n == P_focus) return el->doc && el->doc->focus == el;
    if (n == P_focus_visible) return el->doc && el->doc->focus == el && (el->tag == A_input || el->tag == A_textarea);
    if (n == P_focus_within) { Node *f = el->doc ? el->doc->focus : NULL; return f && node_is_inclusive_ancestor(el, f); }
    if (n == P_first_child) return !prev_el(el) && el->parent;
    if (n == P_last_child) return !next_el(el) && el->parent;
    if (n == P_only_child) return !prev_el(el) && !next_el(el) && el->parent;
    if (n == P_nth_child || n == P_nth_last_child) {
        if (!el->parent) return false;
        if (s->sub && !css_match_selector_list(s->sub, el)) return false;
        int idx = 1;
        for (Node *x = n == P_nth_child ? prev_el(el) : next_el(el); x; x = n == P_nth_child ? prev_el(x) : next_el(x)) if (!s->sub || css_match_selector_list(s->sub, x)) idx++;
        return nth_match(s->a, s->b, idx);
    }
    if (n == P_nth_of_type || n == P_nth_last_of_type || n == P_first_of_type || n == P_last_of_type || n == P_only_of_type) {
        if (!el->parent) return false;
        int before = 0, after = 0;
        for (Node *x = prev_el(el); x; x = prev_el(x)) if (x->tag == el->tag) before++;
        for (Node *x = next_el(el); x; x = next_el(x)) if (x->tag == el->tag) after++;
        if (n == P_first_of_type) return !before;
        if (n == P_last_of_type) return !after;
        if (n == P_only_of_type) return !before && !after;
        return nth_match(s->a, s->b, (n == P_nth_of_type ? before : after) + 1);
    }
    if (n == P_not) return s->sub && !css_match_selector_list(s->sub, el);
    if (n == P_is || n == P_where || n == P_matches || n == P_webkit_any || n == P_moz_any) return s->sub && css_match_selector_list(s->sub, el);
    if (n == P_has) {
        if (!s->sub) return false;
        for (int i = 0; i < s->sub->n; i++) {
            const Selector *sel = &s->sub->v[i];
            uint8_t comb = sel->n > 1 ? sel->c[1].comb : CB_DESC;
            if (comb == CB_ADJ || comb == CB_SIB) {
                for (Node *x = next_el(el); x; x = next_el(x)) { if (css_match_selector(sel, x, el)) return true; if (comb == CB_ADJ) break; }
            } else {
                for (Node *x = el->first; x; x = node_next_in_tree(x, el)) if (x->type == NODE_ELEMENT && css_match_selector(sel, x, el)) return true;
            }
        }
        return false;
    }
    if (n == P_root) return el->parent && el->parent->type == NODE_DOCUMENT;
    if (n == P_scope) return scope ? el == scope : (el->parent && el->parent->type == NODE_DOCUMENT);
    if (n == P_empty) { for (Node *c = el->first; c; c = c->next) if (c->type == NODE_ELEMENT || ((c->type == NODE_TEXT || c->type == NODE_CDATA) && c->text_len)) return false; return true; }
    if (n == P_checked) return (el->tag == A_input && ((el->flags & NF_CHECKED) || (!(el->flags & NF_PARSER_INSERTED) && false))) || (el->tag == A_option && (node_attr_atom(el, A_selected) != NULL)) || (el->tag == A_input && (node_attr_atom(el, A_checked) != NULL) && !(el->flags & NF_CHECKED) && false) || ((el->flags & NF_CHECKED) != 0);
    if (n == P_disabled) return is_disabled(el);
    if (n == P_enabled) return (el->tag == A_button || el->tag == A_input || el->tag == A_select || el->tag == A_textarea) && !is_disabled(el);
    if (n == P_link || n == P_any_link) return is_link(el);
    if (n == P_visited) return false;
    if (n == P_defined) return !strchr(el->tag, '-') || el->ext != NULL || (el->flags & (1u << 20));
    if (n == P_host) { if (!el->shadow_root) return false; if (!s->sub) return true; return css_match_selector_list(s->sub, el); }
    if (n == P_placeholder_shown) { if (el->tag != A_input && el->tag != A_textarea) return false; const char *v = node_attr_atom(el, A_value); return (node_attr_atom(el, A_placeholder) != NULL) && (!v || !v[0]); }
    if (n == P_required) return node_has_attr(el, "required");
    if (n == P_optional) return (el->tag == A_input || el->tag == A_select || el->tag == A_textarea) && !node_has_attr(el, "required");
    if (n == P_read_only) return !(el->tag == A_input || el->tag == A_textarea) || node_has_attr(el, "readonly");
    if (n == P_read_write) return (el->tag == A_input || el->tag == A_textarea) && !node_has_attr(el, "readonly");
    if (n == P_lang) { for (Node *x = el; x && x->type == NODE_ELEMENT; x = x->parent) { const char *l = node_attr_atom(x, A_lang); if (l) return s->value && str_istarts(l, s->value); } return false; }
    if (n == P_dir) return s->value && !strcmp(s->value, "ltr");
    if (n == P_modal) return (el->flags & NF_MODAL) != 0;
    if (n == P_target || n == P_default || n == P_popover_open || n == P_fullscreen) return false;
    if (n == P_playing || n == P_paused) return n == P_paused;
    /* vendor / unknown pseudo-classes never match */
    return false;
}

static bool match_simple(const SimpleSel *s, Node *el, Node *scope) {
    switch (s->kind) {
    case SK_UNIVERSAL: return true;
    case SK_TYPE: return el->ns == NS_HTML ? el->tag == (const char *)s->value : (el->tag == s->name || str_ieq(el->tag, s->name));
    case SK_ID: return el->id && el->id == s->name ? true : (el->id && !strcmp(el->id, s->name));
    case SK_CLASS: return node_has_class_atom(el, s->name);
    case SK_ATTR: return attr_match(s, el);
    case SK_PSEUDO: return match_pseudo(s, el, scope);
    case SK_PSEUDO_EL: return true; /* handled by caller */
    case SK_TREE_RELATED: return (el->flags & (s->op == 2 ? NF_TREE_CHILD : s->op ? NF_TREE_RELATED : NF_TREE_ANCESTOR)) != 0;
    case SK_CHANGED_KEY: {
        /* Widen a coalesced class/ID constraint only when that key changed.
         * Unrelated ancestor classes must still restrict invalidation. */
        unsigned bit = dom_tree_key_bit(s->name);
        if (!el->doc || el->doc->selector_changes_unknown ||
                (el->doc->selector_change_keys[bit >> 6] & (UINT64_C(1) << (bit & 63)))) return true;
        return s->op == SK_CLASS ? node_has_class_atom(el, s->name) :
            el->id && !strcmp(el->id, s->name);
    }
    }
    return false;
}
static bool match_compound(const Compound *c, Node *el, Node *scope) {
    for (int i = 0; i < c->n; i++) if (!match_simple(&c->s[i], el, scope)) return false;
    return true;
}

/* `:host ... x` steps from a shadow tree's top level up to its host */
static Node *host_up(Node *el, const Compound *c) {
    Node *p = el->parent;
    if (!p || p->type != NODE_FRAGMENT || !p->host) return NULL;
    const char *h = atom("host");
    for (int k = 0; k < c->n; k++) if (c->s[k].kind == SK_PSEUDO && c->s[k].name == h) return p->host;
    return NULL;
}
static bool match_from(const Selector *s, int ci, Node *el, Node *scope);
static bool match_from_uncached(const Selector *s, int ci, Node *el, Node *scope) {
    if (!match_compound(&s->c[ci], el, scope)) return false;
    if (ci == 0) return true;
    uint8_t comb = s->c[ci].comb;
    switch (comb) {
    case CB_CHILD: { Node *p = parent_el(el); if (!p) p = host_up(el, &s->c[ci - 1]); return p && match_from(s, ci - 1, p, scope); }
    case CB_DESC: {
        Node *p = parent_el(el), *last = el;
        for (; p; last = p, p = parent_el(p)) if (match_from(s, ci - 1, p, scope)) return true;
        p = host_up(last, &s->c[ci - 1]);
        return p && match_from(s, ci - 1, p, scope); }
    case CB_ADJ: { Node *p = prev_el(el); return p && match_from(s, ci - 1, p, scope); }
    case CB_SIB: { for (Node *p = prev_el(el); p; p = prev_el(p)) if (match_from(s, ci - 1, p, scope)) return true; return false; }
    }
    return false;
}

static bool match_from(const Selector *s, int ci, Node *el, Node *scope) {
    CssMatchCache *cache = active_match_cache;
    if (!cache) return match_from_uncached(s, ci, el, scope);
    const Compound *compound = &s->c[ci];
    uintptr_t hash = ((uintptr_t)compound >> 4) * 0x9e3779b1u;
    hash ^= ((uintptr_t)el >> 4) * 0x85ebca6bu;
    hash ^= (uintptr_t)scope >> 4;
    MatchEntry *entry = &cache->entries[(hash ^ (hash >> 16)) & (MATCH_CACHE_SIZE - 1)];
    uint64_t version = el->doc ? el->doc->dom_version : 0;
    if (el->doc && el->doc == cache->document && !s->reads_style_attribute)
        version = el->doc->selector_version;
    if (entry->epoch == cache->epoch && entry->compound == compound &&
            entry->element == el && entry->scope == scope && entry->version == version) return entry->result;
    bool result = match_from_uncached(s, ci, el, scope);
    *entry = (MatchEntry){compound, el, scope, version, cache->epoch, result};
    return result;
}

bool css_match_selector(const Selector *s, Node *el, Node *scope) {
    if (!s->n || el->type != NODE_ELEMENT) return false;
    /* Also filter nested :not/:where alternatives. The ancestry belongs only
     * to this subject: a :has descendant or sibling must use full matching. */
    CssMatchCache *cache = active_match_cache;
    if (s->required_class_filter && cache && cache->subject == el && cache->ancestor_classes) {
        unsigned bit = s->required_class_filter - 1;
        if (!(cache->ancestor_classes[bit >> 6] & (UINT64_C(1) << (bit & 63)))) return false;
    }
    return match_from(s, s->n - 1, el, scope);
}
bool css_match_selector_list(const SelList *l, Node *el) {
    for (int i = 0; i < l->n; i++) if (!l->v[i].pseudo_el && css_match_selector(&l->v[i], el, NULL)) return true;
    return false;
}

enum { QUERY_CACHE_SIZE = 64, QUERY_CACHE_MAX_TEXT = 4096 };
typedef struct { char *text; SelList selectors; bool valid; } QueryEntry;
typedef struct { QueryEntry entries[QUERY_CACHE_SIZE]; } QueryCache;
static void query_cache_free(void *ptr) {
    QueryCache *cache = ptr;
    if (!cache) return;
    for (int i = 0; i < QUERY_CACHE_SIZE; i++) {
        free(cache->entries[i].text); css_sellist_free(&cache->entries[i].selectors);
    }
    free(cache);
}
const SelList *css_query_syntax(Document *d, const char *text, SelList *scratch) {
    size_t len = strlen(text);
    if (!d || len > QUERY_CACHE_MAX_TEXT)
        return css_parse_selector_list(text, scratch) ? scratch : NULL;
    if (!d->selector_cache) {
        d->selector_cache = xcalloc(1, sizeof(QueryCache));
        d->selector_cache_free = query_cache_free;
    }
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) hash = (hash ^ (unsigned char)text[i]) * 16777619u;
    QueryCache *cache = d->selector_cache;
    QueryEntry *entry = &cache->entries[hash % QUERY_CACHE_SIZE];
    if (!entry->text || strcmp(entry->text, text)) {
        free(entry->text); css_sellist_free(&entry->selectors);
        memset(&entry->selectors, 0, sizeof entry->selectors);
        entry->text = xstrdup(text);
        entry->valid = css_parse_selector_list(text, &entry->selectors);
    }
    return entry->valid ? &entry->selectors : NULL;
}

Node *css_query(Node *root, const char *sel, bool all, NodeVec *outp) {
    SelList scratch = {0};
    const SelList *parsed = css_query_syntax(root->doc, sel, &scratch);
    if (!parsed) { css_sellist_free(&scratch); return (Node *)-1; } /* syntax error */
    const SelList l = *parsed;
    Node *found = NULL;
    NodeVec *out = outp;
    Node *start = root->template_content && false ? root->template_content : root;
    /* fast path: single #id */
    if (l.n == 1 && l.v[0].n == 1 && l.v[0].c[0].n == 1 && l.v[0].c[0].s[0].kind == SK_ID && root->type == NODE_DOCUMENT && !all) {
        found = doc_get_element_by_id(root->doc, l.v[0].c[0].s[0].name);
        css_sellist_free(&scratch); return found;
    }
    for (Node *n = start->first; n; n = node_next_in_tree(n, start)) {
        if (n->type != NODE_ELEMENT) continue;
        bool m = false;
        for (int i = 0; i < l.n && !m; i++) m = css_match_selector(&l.v[i], n, root->type == NODE_DOCUMENT ? NULL : root);
        if (m) { if (!all) { found = n; break; } vec_push(*out, n); }
    }
    css_sellist_free(&scratch);
    return found;
}
