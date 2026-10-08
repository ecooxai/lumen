/* Selector matching and querySelector */
#include "css.h"

static Node *parent_el(Node *n) { Node *p = n->parent; return p && p->type == NODE_ELEMENT ? p : NULL; }
static Node *prev_el(Node *n) { for (n = n->prev; n; n = n->prev) if (n->type == NODE_ELEMENT) return n; return NULL; }
static Node *next_el(Node *n) { for (n = n->next; n; n = n->next) if (n->type == NODE_ELEMENT) return n; return NULL; }

static bool nth_match(int a, int b, int idx) {
    if (a == 0) return idx == b;
    int d = idx - b;
    return d % a == 0 && d / a >= 0;
}

static bool attr_match(const SimpleSel *s, Node *el) {
    const char *v = node_attr(el, s->name);
    if (!v) return false;
    if (!s->op) return true;
    const char *w = s->value; size_t wl = strlen(w), vl = strlen(v);
    int (*cmp)(const char *, const char *, size_t) = s->ci ? (int (*)(const char *, const char *, size_t))strncasecmp : strncmp;
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

static bool is_link(Node *el) { return (el->tag == A_a || el->tag == A_area || el->tag == A_link) && el->ns == NS_HTML && node_has_attr(el, "href"); }
static bool is_disabled(Node *el) {
    if (el->ns != NS_HTML) return false;
    if ((el->tag == A_button || el->tag == A_input || el->tag == A_select || el->tag == A_textarea || el->tag == A_option || el->tag == A_optgroup || el->tag == A_fieldset) && node_has_attr(el, "disabled")) return true;
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
    if (n == P_empty) { for (Node *c = el->first; c; c = c->next) if (c->type == NODE_ELEMENT || (c->type == NODE_TEXT && c->text_len)) return false; return true; }
    if (n == P_checked) return (el->tag == A_input && ((el->flags & NF_CHECKED) || (!(el->flags & NF_PARSER_INSERTED) && false))) || (el->tag == A_option && node_has_attr(el, "selected")) || (el->tag == A_input && node_has_attr(el, "checked") && !(el->flags & NF_CHECKED) && false) || ((el->flags & NF_CHECKED) != 0);
    if (n == P_disabled) return is_disabled(el);
    if (n == P_enabled) return (el->tag == A_button || el->tag == A_input || el->tag == A_select || el->tag == A_textarea) && !is_disabled(el);
    if (n == P_link || n == P_any_link) return is_link(el);
    if (n == P_visited) return false;
    if (n == P_defined) return !strchr(el->tag, '-') || el->ext != NULL || (el->flags & (1u << 20));
    if (n == P_host) { if (!el->shadow_root) return false; if (!s->sub) return true; return css_match_selector_list(s->sub, el); }
    if (n == P_placeholder_shown) { if (el->tag != A_input && el->tag != A_textarea) return false; const char *v = node_attr(el, "value"); return node_has_attr(el, "placeholder") && (!v || !v[0]); }
    if (n == P_required) return node_has_attr(el, "required");
    if (n == P_optional) return (el->tag == A_input || el->tag == A_select || el->tag == A_textarea) && !node_has_attr(el, "required");
    if (n == P_read_only) return !(el->tag == A_input || el->tag == A_textarea) || node_has_attr(el, "readonly");
    if (n == P_read_write) return (el->tag == A_input || el->tag == A_textarea) && !node_has_attr(el, "readonly");
    if (n == P_lang) { for (Node *x = el; x && x->type == NODE_ELEMENT; x = x->parent) { const char *l = node_attr(x, "lang"); if (l) return s->value && str_istarts(l, s->value); } return false; }
    if (n == P_dir) return s->value && !strcmp(s->value, "ltr");
    if (n == P_target || n == P_default || n == P_popover_open || n == P_modal || n == P_fullscreen) return false;
    if (n == P_playing || n == P_paused) return n == P_paused;
    /* vendor / unknown pseudo-classes never match */
    return false;
}

static bool match_simple(const SimpleSel *s, Node *el, Node *scope) {
    switch (s->kind) {
    case SK_UNIVERSAL: return true;
    case SK_TYPE: return el->ns == NS_HTML ? el->tag == (const char *)s->value : (el->tag == s->name || str_ieq(el->tag, s->name));
    case SK_ID: return el->id && el->id == s->name ? true : (el->id && !strcmp(el->id, s->name));
    case SK_CLASS: return node_has_class(el, s->name);
    case SK_ATTR: return attr_match(s, el);
    case SK_PSEUDO: return match_pseudo(s, el, scope);
    case SK_PSEUDO_EL: return true; /* handled by caller */
    }
    return false;
}
static bool match_compound(const Compound *c, Node *el, Node *scope) {
    for (int i = 0; i < c->n; i++) if (!match_simple(&c->s[i], el, scope)) return false;
    return true;
}

static bool match_from(const Selector *s, int ci, Node *el, Node *scope) {
    if (!match_compound(&s->c[ci], el, scope)) return false;
    if (ci == 0) return true;
    uint8_t comb = s->c[ci].comb;
    switch (comb) {
    case CB_CHILD: { Node *p = parent_el(el); return p && match_from(s, ci - 1, p, scope); }
    case CB_DESC: {
        for (Node *p = parent_el(el); p; p = parent_el(p)) if (match_from(s, ci - 1, p, scope)) return true;
        return false; }
    case CB_ADJ: { Node *p = prev_el(el); return p && match_from(s, ci - 1, p, scope); }
    case CB_SIB: { for (Node *p = prev_el(el); p; p = prev_el(p)) if (match_from(s, ci - 1, p, scope)) return true; return false; }
    }
    return false;
}

bool css_match_selector(const Selector *s, Node *el, Node *scope) {
    if (!s->n || el->type != NODE_ELEMENT) return false;
    return match_from(s, s->n - 1, el, scope);
}
bool css_match_selector_list(const SelList *l, Node *el) {
    for (int i = 0; i < l->n; i++) if (!l->v[i].pseudo_el && css_match_selector(&l->v[i], el, NULL)) return true;
    return false;
}

Node *css_query(Node *root, const char *sel, bool all, NodeVec *outp) {
    SelList l = {0};
    if (!css_parse_selector_list(sel, &l)) return (Node *)-1; /* syntax error */
    Node *found = NULL;
    NodeVec *out = outp;
    Node *start = root->template_content && false ? root->template_content : root;
    /* fast path: single #id */
    if (l.n == 1 && l.v[0].n == 1 && l.v[0].c[0].n == 1 && l.v[0].c[0].s[0].kind == SK_ID && root->type == NODE_DOCUMENT && !all) {
        found = doc_get_element_by_id(root->doc, l.v[0].c[0].s[0].name);
        css_sellist_free(&l); return found;
    }
    for (Node *n = start->first; n; n = node_next_in_tree(n, start)) {
        if (n->type != NODE_ELEMENT) continue;
        bool m = false;
        for (int i = 0; i < l.n && !m; i++) m = css_match_selector(&l.v[i], n, root->type == NODE_DOCUMENT ? NULL : root);
        if (m) { if (!all) { found = n; break; } vec_push(*out, n); }
    }
    css_sellist_free(&l);
    return found;
}
