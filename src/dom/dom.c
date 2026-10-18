#include "dom.h"
#include <stdatomic.h>
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#endif

static void trace_mutation(Document *d, Node *n, const char *kind, const char *attribute) {
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (d && d->debug_mutations) {
        d->debug_mutations--;
        __android_log_print(ANDROID_LOG_INFO, "LumenMutation", "kind=%s tag=%s attr=%s selector=%llu",
            kind, n && n->tag ? n->tag : "-", attribute ? attribute : "-", (unsigned long long)d->selector_version);
    }
#else
    (void)d; (void)n; (void)kind; (void)attribute;
#endif
}

static atomic_uint_fast64_t visual_version;
uint64_t doc_visual_generation(void) { return atomic_load_explicit(&visual_version, memory_order_relaxed); }
void doc_visual_changed(void) { atomic_fetch_add_explicit(&visual_version, 1, memory_order_relaxed); }

#define X(n) const char *A_##n;
X(html) X(head) X(body) X(title) X(meta) X(link) X(style) X(script) X(noscript) X(base) X(div) X(span) X(p) X(a) X(img) X(br) X(hr)
X(ul) X(ol) X(li) X(dl) X(dt) X(dd) X(table) X(tbody) X(thead) X(tfoot) X(tr) X(td) X(th) X(caption) X(colgroup) X(col) X(form) X(input)
X(button) X(select) X(option) X(optgroup) X(textarea) X(label) X(iframe) X(video) X(audio) X(source) X(track) X(canvas) X(svg) X(math)
X(template) X(pre) X(code) X(h1) X(h2) X(h3) X(h4) X(h5) X(h6) X(b) X(i) X(u) X(s) X(em) X(strong) X(small) X(big) X(font) X(nobr) X(center)
X(section) X(article) X(nav) X(header) X(footer) X(aside) X(main) X(figure) X(figcaption) X(blockquote) X(address) X(details) X(summary)
X(dialog) X(menu) X(fieldset) X(legend) X(object) X(embed) X(param) X(picture) X(frameset) X(frame) X(xmp) X(plaintext) X(listing) X(area)
X(wbr) X(keygen) X(textpath) X(slot) X(sup) X(sub) X(abbr) X(cite) X(q) X(mark) X(time)
X(id) X(class) X(style_attr) X(href) X(src) X(type) X(rel) X(name) X(value) X(hidden) X(width) X(height) X(alt) X(content) X(charset)
X(http_equiv) X(media) X(async) X(defer) X(disabled) X(checked) X(selected) X(placeholder) X(colspan) X(rowspan) X(dir) X(lang) X(tabindex)
X(srcset) X(sizes) X(poster) X(autoplay) X(controls) X(loop) X(muted) X(open) X(nomodule) X(crossorigin) X(integrity) X(bgcolor) X(color)
X(face) X(size) X(align) X(valign) X(border) X(cellpadding) X(cellspacing) X(text_attr) X(background) X(viewbox) X(fill) X(stroke) X(d)
X(for_attr) X(role) X(multiple)
#undef X

void dom_init(void) {
#define X(n) A_##n = atom(#n);
X(html) X(head) X(body) X(title) X(meta) X(link) X(style) X(script) X(noscript) X(base) X(div) X(span) X(p) X(a) X(img) X(br) X(hr)
X(ul) X(ol) X(li) X(dl) X(dt) X(dd) X(table) X(tbody) X(thead) X(tfoot) X(tr) X(td) X(th) X(caption) X(colgroup) X(col) X(form) X(input)
X(button) X(select) X(option) X(optgroup) X(textarea) X(label) X(iframe) X(video) X(audio) X(source) X(track) X(canvas) X(svg) X(math)
X(template) X(pre) X(code) X(h1) X(h2) X(h3) X(h4) X(h5) X(h6) X(b) X(i) X(u) X(s) X(em) X(strong) X(small) X(big) X(font) X(nobr) X(center)
X(section) X(article) X(nav) X(header) X(footer) X(aside) X(main) X(figure) X(figcaption) X(blockquote) X(address) X(details) X(summary)
X(dialog) X(menu) X(fieldset) X(legend) X(object) X(embed) X(param) X(picture) X(frameset) X(frame) X(xmp) X(plaintext) X(listing) X(area)
X(wbr) X(keygen) X(slot) X(sup) X(sub) X(abbr) X(cite) X(q) X(mark) X(time)
X(id) X(class) X(href) X(src) X(type) X(rel) X(name) X(value) X(hidden) X(width) X(height) X(alt) X(content) X(charset)
X(media) X(async) X(defer) X(disabled) X(checked) X(selected) X(placeholder) X(colspan) X(rowspan) X(dir) X(lang) X(tabindex)
X(srcset) X(sizes) X(poster) X(autoplay) X(controls) X(loop) X(muted) X(open) X(nomodule) X(crossorigin) X(integrity) X(bgcolor) X(color)
X(face) X(size) X(align) X(valign) X(border) X(cellpadding) X(cellspacing) X(background) X(fill) X(stroke) X(d) X(role) X(multiple)
#undef X
    A_style_attr = A_style; A_http_equiv = atom("http-equiv"); A_text_attr = atom("text"); A_viewbox = atom("viewBox");
    A_textpath = atom("textPath"); A_for_attr = atom("for");
}

Document *doc_new(const char *url) {
    Document *d = xcalloc(1, sizeof *d);
    d->node.type = NODE_DOCUMENT; d->node.doc = d; d->node.tag = atom("#document");
    d->url = xstrdup(url ? url : "about:blank");
    d->node.refcount = 1;
    return d;
}

long g_nodes_live;
static Node *alloc_node(Document *d, int type) { Node *n = xcalloc(1, sizeof *n); g_nodes_live++; if (d) d->selector_version++; /* invalidate cached addresses reused by detached nodes; existing inheritance is unchanged */ n->type = (uint8_t)type; n->doc = d; n->flags = NF_STYLE_DIRTY | NF_LAYOUT_DIRTY; return n; }
Node *node_new_element(Document *d, const char *tag, int ns) { Node *n = alloc_node(d, NODE_ELEMENT); n->tag = atom(tag); n->ns = (uint8_t)ns; return n; }
Node *node_new_text(Document *d, const char *s, size_t len) { Node *n = alloc_node(d, NODE_TEXT); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom("#text"); return n; }
Node *node_new_comment(Document *d, const char *s, size_t len) { Node *n = alloc_node(d, NODE_COMMENT); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom("#comment"); return n; }
Node *node_new_fragment(Document *d) { Node *n = alloc_node(d, NODE_FRAGMENT); n->tag = atom("#document-fragment"); return n; }
Node *node_new_doctype(Document *d, const char *name) { Node *n = alloc_node(d, NODE_DOCTYPE); n->text = xstrdup(name ? name : "html"); n->text_len = strlen(n->text); n->tag = atom("#doctype"); return n; }

Node *node_new_pi(Document *d, const char *target, const char *s, size_t len) { Node *n = alloc_node(d, NODE_PI); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom(target); return n; }
Node *node_new_cdata(Document *d, const char *s, size_t len) { Node *n = alloc_node(d, NODE_CDATA); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom("#cdata-section"); return n; }

void node_set_modal(Node *n, bool modal) {
    if (!n->doc || modal == !!(n->flags & NF_MODAL)) return;
    Document *d = n->doc;
    if (modal) {
        if (!(n->flags & NF_CONNECTED) || !node_is(n, A_dialog) || !node_has_attr(n, "open")) return;
        vec_push(d->modals, n);
        n->flags |= NF_MODAL;
    } else {
        for (int i = 0; i < d->modals.n; i++) if (d->modals.v[i] == n) {
            memmove(d->modals.v + i, d->modals.v + i + 1, sizeof(Node *) * (size_t)(d->modals.n - i - 1));
            d->modals.n--;
            break;
        }
        n->flags &= ~(uint32_t)NF_MODAL;
    }
    doc_mark_dirty(d, n);
}

static void mark_connected(Node *n, bool on) {
    for (Node *c = n; c; c = node_next_in_tree(c, n)) {
        if (c->doc && (node_is(c, A_style) || node_is(c, A_link))) c->doc->sheet_version++;
        if (!on && (c->flags & NF_MODAL)) node_set_modal(c, false);
        if (on) c->flags |= NF_CONNECTED; else c->flags &= ~(uint32_t)(NF_CONNECTED | NF_MODAL);
        if (c->type == NODE_ELEMENT && c->shadow_root) mark_connected(c->shadow_root, on);
    }
}
static bool is_connected(Node *p) { return p->type == NODE_DOCUMENT || (p->flags & NF_CONNECTED); }

/* Ordinary text/input changes must not rehash every stylesheet in a long page.
 * Include ancestors because a style element's textContent can contain elements. */
static void mark_sheet_content(Document *d, Node *n) {
    for (; n; n = n->parent) {
        if (node_is(n, A_style) || node_is(n, A_link)) { d->sheet_version++; return; }
    }
}

/* The style engine marks following siblings that sibling combinators relate. */
__attribute__((weak)) void node_sibling_selectors_changed(Node *n, int mode) { (void)n; (void)mode; }
/* reason names an unknown change; NULL marks known keys. subtree reselects
 * n's descendants even for known keys. */
static void mark_impl(Document *d, Node *n, bool layout, bool selectors, const char *reason, bool subtree, bool style) {
    if (!d) return;
    d->dom_version++;
    if (selectors) {
        d->selector_version++;
        if (!n || is_connected(n)) {
            d->inheritance_version++;
            if (reason) { d->selector_changes_unknown = true; if (!d->unknown_reason) d->unknown_reason = reason; }
        }
        if (n) n->flags |= reason || subtree ? NF_RESELECT_SUBTREE : NF_RESELECT_KEYED;
    }
    if (selectors) trace_mutation(d, n, "selector", reason ? reason : subtree ? "subtree" : "keyed");
    mark_sheet_content(d, n);
    if (layout) d->layout_version++;
    if (n) {
        n->flags |= (style ? NF_STYLE_DIRTY : NF_CHILD_STYLE_DIRTY) | NF_LAYOUT_DIRTY;
        for (Node *p = n->parent; p; p = p->parent ? p->parent : p->host) {
            if ((p->flags & (NF_CHILD_STYLE_DIRTY | NF_LAYOUT_DIRTY)) == (NF_CHILD_STYLE_DIRTY | NF_LAYOUT_DIRTY)) break;
            p->flags |= NF_CHILD_STYLE_DIRTY | NF_LAYOUT_DIRTY;
            if (!p->parent && !p->host) break;
        }
    }
    if (d->on_mutation) d->on_mutation(d, n);
}
static void mark(Document *d, Node *n, bool layout, bool selectors, const char *reason) { mark_impl(d, n, layout, selectors, reason, true, true); }
void doc_mark_dirty(Document *d, Node *n) { mark(d, n, true, true, "dirty"); if (n) node_sibling_selectors_changed(n, 0); }
void doc_mark_style_dirty(Document *d, Node *n) { mark(d, n, false, true, "style"); if (n) node_sibling_selectors_changed(n, 0); }
void doc_mark_state_dirty(Document *d, Node *n, bool subtree) {
    mark_impl(d, n, true, true, NULL, subtree, true);
    if (n) node_sibling_selectors_changed(n, 0);
}
/* The style engine reports whether any rule tests :focus-within. */
__attribute__((weak)) bool doc_uses_focus_within(Document *d) { (void)d; return true; }
/* ... and may restyle exactly the elements whose :focus-within-dependent matches change. */
__attribute__((weak)) bool doc_focus_within_moved(Document *d, Node *old, Node *n) { (void)d; (void)old; (void)n; return false; }
static void selector_change_key(Document *d, const char *key);
void doc_set_focus(Document *d, Node *n) {
    Node *old = d->focus;
    if (old == n) return;
    if (doc_uses_focus_within(d)) {
        /* Only ancestors on one side of the common ancestor change state. */
        if (!doc_focus_within_moved(d, old, n)) selector_change_key(d, ":focus-within");
        for (int side = 0; side < 2; side++)
            for (Node *a = side ? n : old; a; a = a->parent ? a->parent : a->host) {
                if (a->type != NODE_ELEMENT) continue;
                Node *other = side ? old : n;
                if (other && node_is_inclusive_ancestor(a, other)) break;
                mark_impl(d, a, true, true, NULL, false, true);
                node_sibling_selectors_changed(a, 0);
            }
    }
    if (old) { old->flags &= ~(uint32_t)NF_FOCUS; doc_mark_state_dirty(d, old, true); }
    d->focus = n;
    if (n) { n->flags |= NF_FOCUS; doc_mark_state_dirty(d, n, true); }
}
static bool style_only_attr(const char *name) { return !strcmp(name, "style") || !strcmp(name, "class"); }
/* CSS may precisely invalidate attribute-selector subjects. DOM-only users
 * retain the conservative behavior without linking the style engine. */
__attribute__((weak)) bool node_attribute_style_changed(Node *n, const char *name) { (void)n; (void)name; return false; }
static void mark_attribute(Node *n, const char *name) {
    Document *d = n->doc;
    if (d && str_ieq(name, "src")) d->resource_version++;
    if (d && node_attribute_style_changed(n, name)) {
        d->dom_version++; d->selector_version++;
        mark_sheet_content(d, n);
        if (d->on_mutation) d->on_mutation(d, n);
    } else {
        bool inline_style = str_ieq(name, "style");
        mark_impl(d, n, !style_only_attr(name), !inline_style, str_ieq(name, "class") || str_ieq(name, "id") ? NULL : atom_lower(name, strlen(name)), false, true);
        if (d) node_sibling_selectors_changed(n, inline_style ? 2 : 1);
    }
}
static bool prevents_empty(const Node *n) {
    return n->type == NODE_ELEMENT || ((n->type == NODE_TEXT || n->type == NODE_CDATA) && n->text_len);
}
static bool has_empty_content(const Node *n) {
    for (const Node *c = n->first; c; c = c->next) if (prevents_empty(c)) return false;
    return true;
}
static void tree_change_key(Document *d, const char *key) {
    if (!key) return;
    unsigned bit = dom_tree_key_bit(key);
    d->tree_change_keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
}
/* Capture removed keys before nodes can be freed, and moved descendants as
 * well as the subtree root. Bloom collisions only cause extra invalidation. */
static void record_tree_change(Document *d, Node *n) {
    if (!d) return;
    if (n == d->focus) d->tree_focus_changed = true;
    if (n->type == NODE_ELEMENT) {
        tree_change_key(d, atom_lower(n->tag, strlen(n->tag)));
        tree_change_key(d, n->id);
        for (int i = 0; i < n->nclasses; i++) tree_change_key(d, n->classes[i]);
        for (int i = 0; i < n->nattrs; i++) tree_change_key(d, atom_lower(n->attrs[i].name, strlen(n->attrs[i].name)));
    }
    for (Node *c = n->first; c; c = c->next) record_tree_change(d, c);
    if (n->shadow_root) record_tree_change(d, n->shadow_root);
}
static void mark_child_list(Node *parent) {
    Document *d = parent->doc;
    bool connected = is_connected(parent);
    if (d) { if (connected) d->tree_version++; d->selector_version++; }
    if (!connected) parent->flags |= NF_RESELECT_SUBTREE;
    parent->flags |= NF_CHILD_LIST_DIRTY;
    /* Structural selector invalidation selects affected parents separately.
     * A child-list edit alone changes layout, not inherited computed style.
     * DOM-only clients without a style engine retain conservative dirtiness. */
    mark_impl(d, parent, true, false, "children", true, !d || !d->stylesheets);
}

void node_insert_before(Node *parent, Node *child, Node *ref) {
    if (child->type == NODE_FRAGMENT) {
        while (child->first) node_insert_before(parent, child->first, ref);
        return;
    }
    if (child->parent) node_remove(child);
    if (prevents_empty(child) && has_empty_content(parent)) {
        parent->flags |= NF_STYLE_DIRTY; /* the parent's own :empty may change */
        if (parent->doc && is_connected(parent)) parent->doc->text_emptiness_version++;
    }
    child->parent = parent;
    if (ref && ref->parent == parent) {
        child->next = ref; child->prev = ref->prev;
        if (ref->prev) ref->prev->next = child; else parent->first = child;
        ref->prev = child;
    } else {
        child->prev = parent->last; child->next = NULL;
        if (parent->last) parent->last->next = child; else parent->first = child;
        parent->last = child;
    }
    child->refcount++;
    if (is_connected(parent)) { mark_connected(child, true); record_tree_change(parent->doc, child); }
    /* A moved subtree has new ancestry. Existing siblings only need the
     * structural selector dependencies, not a fresh cascade for every leaf. */
    child->flags |= NF_STYLE_DIRTY | NF_RESELECT_SUBTREE;
    mark(parent->doc, child, true, false, NULL);
    mark_child_list(parent);
}
void node_append(Node *parent, Node *child) { node_insert_before(parent, child, NULL); }

void node_remove(Node *c) {
    Node *p = c->parent; if (!p) return;
    if (is_connected(p)) record_tree_change(p->doc, c);
    if (c->prev) c->prev->next = c->next; else p->first = c->next;
    if (c->next) c->next->prev = c->prev; else p->last = c->prev;
    c->parent = c->prev = c->next = NULL;
    if (prevents_empty(c) && has_empty_content(p)) {
        p->flags |= NF_STYLE_DIRTY;
        if (p->doc && is_connected(p)) p->doc->text_emptiness_version++;
    }
    mark_connected(c, false);
    if (c->doc && c->doc->focus && node_is_inclusive_ancestor(c, c->doc->focus)) c->doc->focus = NULL;
    mark_child_list(p);
    if (c->refcount) c->refcount--;
}

void node_retain(Node *n) { n->refcount++; }
void node_release(Node *n) { if (n->refcount) n->refcount--; if (!n->refcount && !n->parent) node_free_tree(n); }

__attribute__((weak)) void style_free(struct ComputedStyle *s) { free(s); }
__attribute__((weak)) void node_free_inline_style(Node *n) { (void)n; }
__attribute__((weak)) void box_detach_node(Node *n) { n->box = NULL; }
static void node_free_one(Node *n) {
    for (int i = 0; i < n->nattrs; i++) free(n->attrs[i].value);
    free(n->attrs); free(n->text); free(n->inline_style_src); free(n->classes);
    node_free_inline_style(n);
    if (n->ext && n->ext_free) n->ext_free(n->ext);
    if (n->style) style_free(n->style);
    if (n->box) box_detach_node(n);
    g_nodes_live--; free(n);
}
void node_free_tree(Node *n) {
    /* frees n and children not referenced elsewhere (JS wrappers hold a ref) */
    if (n->refcount || n->js) return;
    Node *c = n->first;
    while (c) { Node *nx = c->next; c->parent = NULL; c->prev = c->next = NULL; if (c->refcount) c->refcount--; node_free_tree(c); c = nx; }
    n->first = n->last = NULL;
    if (n->template_content) { n->template_content->refcount = 0; node_free_tree(n->template_content); }
    if (n->shadow_root) { n->shadow_root->host = NULL; n->shadow_root->refcount = 0; node_free_tree(n->shadow_root); }
    node_free_one(n);
}
void doc_free(Document *d) {
    if (d->selector_cache_free) d->selector_cache_free(d->selector_cache);
    free(d->modals.v); d->modals.v = NULL; d->modals.n = d->modals.cap = 0;
    for (int i = 0; i < 2; i++) if (d->sel[i]) { node_release(d->sel[i]); d->sel[i] = NULL; }
    hm_free(&d->id_cache, NULL);
    Node *c = d->node.first;
    while (c) { Node *nx = c->next; c->parent = NULL; c->refcount = 0; c->js = NULL; node_free_tree(c); c = nx; }
    free(d->url); free(d->base_url); free(d->title); free(d);
}

static int attr_index(const Node *n, const char *name) {
    for (int i = 0; i < n->nattrs; i++) if (n->attrs[i].name == name) return i;
    const char *a = atom(name);
    for (int i = 0; i < n->nattrs; i++) if (n->attrs[i].name == a) return i;
    if (n->ns == NS_HTML) { for (int i = 0; i < n->nattrs; i++) if (str_ieq(n->attrs[i].name, name)) return i; }
    return -1;
}
const char *node_attr(const Node *n, const char *name) { if (!n || (n->type != NODE_ELEMENT && n->type != NODE_DOCTYPE)) return NULL; int i = attr_index(n, name); return i >= 0 ? n->attrs[i].value : NULL; }
const char *node_attr_atom(const Node *n, const char *name) {
    if (!n || (n->type != NODE_ELEMENT && n->type != NODE_DOCTYPE)) return NULL;
    for (int i = 0; i < n->nattrs; i++) if (n->attrs[i].name == name) return n->attrs[i].value;
    if (n->ns == NS_HTML && (n->flags & NF_ATTR_MIXED_CASE))
        for (int i = 0; i < n->nattrs; i++) if (str_ieq(n->attrs[i].name, name)) return n->attrs[i].value;
    return NULL;
}
bool node_has_attr(const Node *n, const char *name) { return node_attr(n, name) != NULL; }
static void cache_classes(Node *n, const char *value) {
    n->class_name = value;
    n->nclasses = 0; n->class_bits = 0;
    free(n->classes); n->classes = NULL;
    int cap = 0;
    for (const char *p = value; p && *p;) {
        while (is_ws((unsigned char)*p)) p++;
        const char *start = p;
        while (*p && !is_ws((unsigned char)*p)) p++;
        if (p == start) break;
        const char *cls = atomn(start, (size_t)(p - start));
        if (node_has_class_atom(n, cls)) continue;
        if (n->nclasses == cap) { cap = cap ? cap * 2 : 4; n->classes = xrealloc(n->classes, (size_t)cap * sizeof *n->classes); }
        n->classes[n->nclasses++] = cls;
        n->class_bits |= node_class_bit(cls);
    }
}
static void selector_change_key_in(Document *d, const char *key, bool reselect) {
    if (!key) return;
    unsigned bit = dom_tree_key_bit(key);
    d->selector_change_keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
    if (reselect) d->reselect_change_keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (reselect && d->debug_mutations) { char b[24]; snprintf(b, sizeof b, "rkey bit=%u", bit); trace_mutation(d, NULL, b, key); }
#endif
}
static void selector_change_key(Document *d, const char *key) { selector_change_key_in(d, key, true); }
static bool class_value_contains(const char *value, const char *key) {
    size_t len = strlen(key);
    for (const char *p = value; p && *p;) {
        while (is_ws((unsigned char)*p)) p++;
        const char *start = p;
        while (*p && !is_ws((unsigned char)*p)) p++;
        if ((size_t)(p - start) == len && !memcmp(start, key, len)) return true;
    }
    return false;
}
/* Keep removed keys before freeing attribute storage. Unchanged class tokens
 * need not disable an unrelated :has witness filter during a coalesced edit. */
static void record_selector_attribute(Node *n, const char *name, const char *value) {
    if (!n->doc || !is_connected(n)) return;
    Document *d = n->doc;
    /* node_attribute_style_changed marks these subjects by candidate; their
     * keys must not widen the descendant reselect of unrelated changes. */
    bool precise = d->stylesheets && n->ns == NS_HTML && (str_istarts(name, "data-") || str_istarts(name, "aria-"));
    selector_change_key_in(d, atom_lower(name, strlen(name)), !precise);
    if (str_ieq(name, "id")) {
        selector_change_key(d, "id"); selector_change_key(d, n->id); selector_change_key(d, value);
    } else if (str_ieq(name, "class")) {
        selector_change_key(d, "class");
        for (int i = 0; i < n->nclasses; i++)
            if (!class_value_contains(value, n->classes[i])) selector_change_key(d, n->classes[i]);
        for (const char *p = value; p && *p;) {
            while (is_ws((unsigned char)*p)) p++;
            const char *start = p;
            while (*p && !is_ws((unsigned char)*p)) p++;
            if (p == start) break;
            const char *key = atomn(start, (size_t)(p - start));
            if (!node_has_class_atom(n, key)) selector_change_key(d, key);
        }
    }
}
void node_set_attr(Node *n, const char *name, const char *value) {
    if (n->ns == NS_HTML && !(n->flags & NF_ATTR_MIXED_CASE))
        for (const unsigned char *p = (const unsigned char *)name; *p; p++)
            if (*p >= 'A' && *p <= 'Z') { n->flags |= NF_ATTR_MIXED_CASE; break; }
    int i = attr_index(n, name);
    if (i >= 0 && !strcmp(n->attrs[i].value, value)) return;
    record_selector_attribute(n, name, value);
    if (i >= 0) { if (!strcmp(n->attrs[i].value, value)) return; free(n->attrs[i].value); n->attrs[i].value = xstrdup(value); }
    else {
        if (n->nattrs >= n->attrcap) { n->attrcap = n->attrcap ? n->attrcap * 2 : 4; n->attrs = xrealloc(n->attrs, sizeof(Attr) * (size_t)n->attrcap); }
        n->attrs[n->nattrs].name = atom(name); n->attrs[n->nattrs].value = xstrdup(value); i = n->nattrs++;
    }
    if (n->attrs[i].name == A_id) n->id = n->attrs[i].value;
    if (n->attrs[i].name == A_class || (n->ns == NS_HTML && str_ieq(name, "class"))) cache_classes(n, n->attrs[i].value);
    /* re-point cached id after realloc */
    for (int k = 0; k < n->nattrs; k++) if (n->attrs[k].name == A_id) n->id = n->attrs[k].value;
    trace_mutation(n->doc, n, "set", name);
    mark_attribute(n, name);
}
void node_remove_attr(Node *n, const char *name) {
    if (!strcmp(name, "open")) node_set_modal(n, false);
    int i = attr_index(n, name); if (i < 0) return;
    record_selector_attribute(n, name, NULL);
    if (n->attrs[i].name == A_id) n->id = NULL;
    if (n->attrs[i].name == A_class || (n->ns == NS_HTML && str_ieq(name, "class"))) cache_classes(n, NULL);
    free(n->attrs[i].value);
    memmove(&n->attrs[i], &n->attrs[i + 1], sizeof(Attr) * (size_t)(n->nattrs - i - 1)); n->nattrs--;
    for (int k = 0; k < n->nattrs; k++) if (n->attrs[k].name == A_id) n->id = n->attrs[k].value;
    trace_mutation(n->doc, n, "remove", name);
    mark_attribute(n, name);
}
bool node_has_class(const Node *n, const char *cls) {
    return n && n->nclasses && node_has_class_atom(n, atom(cls));
}

Node *node_next_in_tree(Node *n, Node *root) {
    if (n->first) return n->first;
    while (n && n != root) { if (n->next) return n->next; n = n->parent; }
    return NULL;
}
/* pre-order traversal that also enters shadow trees (shadow tree before the host's light children) */
Node *node_next_deep(Node *n, Node *root) {
    if (n->type == NODE_ELEMENT && n->shadow_root && n->shadow_root->first) return n->shadow_root->first;
    if (n->first) return n->first;
    for (;;) {
        if (n == root) return NULL;
        if (n->next) return n->next;
        Node *p = n->parent;
        if (!p) return NULL;
        if (p->type == NODE_FRAGMENT && p->host) { Node *h = p->host; if (h->first) return h->first; n = h; continue; }
        n = p;
    }
}
bool node_is_inclusive_ancestor(const Node *a, const Node *b) { for (; b; b = b->parent) if (a == b) return true; return false; }

static void text_rec(const Node *n, SB *b) {
    for (const Node *c = n->first; c; c = c->next) {
        if (c->type == NODE_TEXT || c->type == NODE_CDATA) sb_put(b, c->text, c->text_len);
        else if (c->type == NODE_ELEMENT || c->type == NODE_FRAGMENT) text_rec(c, b);
    }
}
char *node_text_content(const Node *n) {
    if (n->type == NODE_TEXT || n->type == NODE_COMMENT || n->type == NODE_CDATA || n->type == NODE_PI) return xstrndup(n->text, n->text_len);
    SB b; sb_init(&b); text_rec(n, &b); return sb_take(&b);
}
void node_set_character_data(Node *n, const char *s, size_t len) {
    bool emptiness_changed = !n->text_len != !len;
    char *replacement = xstrndup(s, len);
    free(n->text); n->text = replacement; n->text_len = len;
    /* Ordinary edits only affect layout. The style engine tracks selectors
     * that depend on another element's emptiness (:has and combinators). */
    Node *dirty = n;
    if (emptiness_changed && (n->type == NODE_TEXT || n->type == NODE_CDATA) && n->parent) {
        dirty = n->parent;
        if (n->doc && is_connected(n->parent)) n->doc->text_emptiness_version++;
    }
    mark(n->doc, dirty, true, dirty != n, "text");
}
void node_set_text_content(Node *n, const char *s) {
    if (n->type == NODE_TEXT || n->type == NODE_COMMENT || n->type == NODE_CDATA || n->type == NODE_PI) { node_set_character_data(n, s ? s : "", s ? strlen(s) : 0); return; }
    while (n->first) { Node *c = n->first; node_remove(c); node_free_tree(c); }
    if (s && *s) node_append(n, node_new_text(n->doc, s, strlen(s)));
}
void node_text_append(Node *n, const char *s, size_t len) {
    n->text = xrealloc(n->text, n->text_len + len + 1); memcpy(n->text + n->text_len, s, len); n->text_len += len; n->text[n->text_len] = 0;
    if (len && n->doc) mark_sheet_content(n->doc, n);
}

Node *node_clone(Node *n, bool deep, Document *d) {
    Node *c;
    switch (n->type) {
    case NODE_ELEMENT: c = node_new_element(d, n->tag, n->ns); for (int i = 0; i < n->nattrs; i++) node_set_attr(c, n->attrs[i].name, n->attrs[i].value); break;
    case NODE_TEXT: c = node_new_text(d, n->text, n->text_len); break;
    case NODE_COMMENT: c = node_new_comment(d, n->text, n->text_len); break;
    case NODE_DOCTYPE: c = node_new_doctype(d, n->text); for (int i = 0; i < n->nattrs; i++) node_set_attr(c, n->attrs[i].name, n->attrs[i].value); break;
    case NODE_PI: c = node_new_pi(d, n->tag, n->text, n->text_len); break;
    case NODE_CDATA: c = node_new_cdata(d, n->text, n->text_len); break;
    default: c = node_new_fragment(d); break;
    }
    if (n->template_content) { c->template_content = node_clone(n->template_content, true, d); c->template_content->refcount = 1; c->template_content->flags |= NF_INERT; }
    if (deep) for (Node *k = n->first; k; k = k->next) node_append(c, node_clone(k, true, d));
    return c;
}

bool html_is_void(const char *t) {
    static const char *v[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr", "keygen", "basefont", "bgsound", "frame" };
    for (size_t i = 0; i < ARRLEN(v); i++) if (!strcmp(t, v[i])) return true;
    return false;
}
static void esc(SB *b, const char *s, size_t n, bool attr) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') sb_puts(b, "&amp;"); else if (c == '<' && !attr) sb_puts(b, "&lt;"); else if (c == '>' && !attr) sb_puts(b, "&gt;");
        else if (c == '"' && attr) sb_puts(b, "&quot;"); else if ((unsigned char)c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) { sb_puts(b, "&nbsp;"); i++; }
        else sb_putc(b, c);
    }
}
static void ser(const Node *n, SB *b);
static void ser_children(const Node *n, const Node *src, SB *b) {
    bool raw = n->type == NODE_ELEMENT && n->ns == NS_HTML && (n->tag == A_script || n->tag == A_style || n->tag == A_xmp || n->tag == A_iframe || n->tag == A_noscript || n->tag == A_plaintext || n->tag == atom("noembed") || n->tag == atom("noframes"));
    for (const Node *c = src->first; c; c = c->next) { if (raw && c->type == NODE_TEXT) sb_put(b, c->text, c->text_len); else ser(c, b); }
}
static void ser(const Node *n, SB *b) {
    switch (n->type) {
    case NODE_ELEMENT: {
        sb_putc(b, '<'); sb_puts(b, n->tag);
        for (int i = 0; i < n->nattrs; i++) { sb_putc(b, ' '); sb_puts(b, n->attrs[i].name); sb_puts(b, "=\""); esc(b, n->attrs[i].value, strlen(n->attrs[i].value), true); sb_putc(b, '"'); }
        sb_putc(b, '>');
        if (n->ns == NS_HTML && html_is_void(n->tag)) return;
        const Node *src = n->template_content ? n->template_content : n;
        ser_children(n, src, b);
        sb_puts(b, "</"); sb_puts(b, n->tag); sb_putc(b, '>');
        break; }
    case NODE_TEXT: esc(b, n->text, n->text_len, false); break;
    case NODE_COMMENT: sb_puts(b, "<!--"); sb_put(b, n->text, n->text_len); sb_puts(b, "-->"); break;
    case NODE_DOCTYPE: sb_printf(b, "<!DOCTYPE %s>", n->text); break;
    case NODE_PI: sb_printf(b, "<?%s ", n->tag); sb_put(b, n->text, n->text_len); sb_puts(b, "?>"); break;
    case NODE_CDATA: sb_puts(b, "<![CDATA["); sb_put(b, n->text, n->text_len); sb_puts(b, "]]>"); break;
    default: for (const Node *c = n->first; c; c = c->next) ser(c, b);
    }
}
char *node_serialize(const Node *n, bool outer) {
    SB b; sb_init(&b);
    if (outer) ser(n, &b);
    else ser_children(n, n->template_content ? n->template_content : n, &b);
    return sb_take(&b);
}

Node *doc_get_element_by_id(Document *d, const char *id) {
    if (!id[0]) return NULL;
    if (!d->id_cache_ok || d->id_cache_ver != d->dom_version) {
        hm_free(&d->id_cache, NULL); memset(&d->id_cache, 0, sizeof d->id_cache);
        for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node))
            if (n->type == NODE_ELEMENT && n->id && n->id[0] && !hm_get(&d->id_cache, n->id)) hm_put(&d->id_cache, n->id, n);
        d->id_cache_ver = d->dom_version; d->id_cache_ok = true;
    }
    return hm_get(&d->id_cache, id);
}

void dom_dump(const Node *n, int depth, FILE *f) {
    for (int i = 0; i < depth; i++) fputs("  ", f);
    if (n->type == NODE_ELEMENT) {
        fprintf(f, "<%s%s", n->ns == NS_SVG ? "svg " : n->ns == NS_MATHML ? "math " : "", n->tag);
        for (int i = 0; i < n->nattrs; i++) fprintf(f, " %s=\"%.40s\"", n->attrs[i].name, n->attrs[i].value);
        fputs(">\n", f);
    } else if (n->type == NODE_TEXT) fprintf(f, "\"%.*s\"\n", (int)LMIN(n->text_len, 60), n->text);
    else if (n->type == NODE_COMMENT) fprintf(f, "<!-- %.30s -->\n", n->text);
    else if (n->type == NODE_DOCTYPE) fprintf(f, "<!DOCTYPE %s>\n", n->text);
    else fprintf(f, "%s\n", n->tag);
    const Node *src = n->template_content ? n->template_content : n;
    for (const Node *c = src->first; c; c = c->next) dom_dump(c, depth + 1, f);
}
