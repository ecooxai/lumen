#include "dom.h"
#include "../base/url.h"
#include <stdatomic.h>
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#include <dlfcn.h>
/* Debug Android: who restyles the root element, as caller offsets in libmain.so (rate-limited). */
static void html_dirty_log(const char *reason, bool subtree, bool selectors, bool layout, void *ret0, void *ret1) {
    static unsigned count;
    if (++count > 40 && count % 25) return;
    Dl_info info; uintptr_t base = dladdr(ret0, &info) && info.dli_fbase ? (uintptr_t)info.dli_fbase : 0;
    __android_log_print(ANDROID_LOG_INFO, "LumenStyle", "html_dirty n=%u reason=%s subtree=%d selectors=%d layout=%d caller=0x%lx,0x%lx", count, reason ? reason : "-",
        subtree, selectors, layout, (unsigned long)((uintptr_t)ret0 - base), (unsigned long)((uintptr_t)ret1 - base));
}
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
static atomic_uint_fast64_t box_paint_version;
uint64_t doc_box_generation(void) { return atomic_load_explicit(&box_paint_version, memory_order_relaxed); }
void doc_boxes_changed(void) { atomic_fetch_add_explicit(&box_paint_version, 1, memory_order_relaxed); doc_visual_changed(); }
static atomic_uint_fast64_t activity_version;
uint64_t doc_activity_generation(void) { return atomic_load_explicit(&activity_version, memory_order_relaxed); }
void doc_activity(void) { atomic_fetch_add_explicit(&activity_version, 1, memory_order_relaxed); }

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

/* Template contents have a real inert node document shared by every template
 * in the same owner document, including nested templates. */
Document *doc_template_contents_owner(Document *d) {
    if (!d) return NULL;
    if (!d->template_contents_owner) {
        Document *owner = doc_new("about:blank");
        owner->node.ns = d->node.ns;
        owner->interface_kind = 2;
        owner->ready_state = 2;
        owner->template_contents_owner = owner;
        d->template_contents_owner = owner;
    }
    return d->template_contents_owner;
}

void node_adopt_document(Node *n, Document *d) {
    if (!n || !d) return;
    for (Node *x = n; x; x = node_next_in_tree(x, n)) {
        if (x->doc != d) {
            if (x->doc) x->doc->selector_version++;
            d->selector_version++;
            x->doc = d;
            x->flags |= NF_STYLE_DIRTY | NF_LAYOUT_DIRTY;
        }
        if (x->shadow_root) node_adopt_document(x->shadow_root, d);
        if (x->template_content && x->template_content != x->shadow_root)
            node_adopt_document(x->template_content, doc_template_contents_owner(d));
    }
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

/* The document whose own tree holds n (none for detached nodes and shadow trees). */
static Document *tree_doc(Node *n) { while (n->parent) n = n->parent; return n->type == NODE_DOCUMENT ? n->doc : NULL; }
/* getElementById's map (id -> first element with it) follows elements as they connect, disconnect
   or change id. A duplicate id leaves the order to a full tree-order rebuild on the next lookup. */
static void id_map_add(Document *d, Node *n) {
    if (!d || !d->id_cache_ok || !n->id || !n->id[0]) return;
    Node *o = hm_get(&d->id_cache, n->id);
    if (o && o != n) d->id_cache_ok = false;
    else hm_put(&d->id_cache, n->id, n);
}
static void id_map_del(Document *d, Node *n, const char *id) {
    if (!d || !d->id_cache_ok || !id || !id[0] || hm_get(&d->id_cache, id) != n) return;
    if (hm_get(&d->id_dups, id) || d->id_cache.n > 16384) d->id_cache_ok = false;   /* another element may hold it; or compact */
    else hm_put(&d->id_cache, id, NULL);
}
/* d is the document whose tree gains or loses n (NULL inside shadow trees, which getElementById skips). */
static void mark_connected(Node *n, bool on, Document *d) {
    for (Node *c = n; c; c = node_next_in_tree(c, n)) {
        if (c->doc && (node_is(c, A_style) || node_is(c, A_link))) c->doc->sheet_version++;
        if (!on && (c->flags & NF_MODAL)) node_set_modal(c, false);
        if (on) c->flags |= NF_CONNECTED; else c->flags &= ~(uint32_t)(NF_CONNECTED | NF_MODAL | NF_FULLSCREEN | NF_POPOVER_OPEN);
        if (c->type == NODE_ELEMENT && c->id) { if (on) id_map_add(d, c); else id_map_del(d, c, c->id); }
        if (c->type == NODE_ELEMENT && c->shadow_root) mark_connected(c->shadow_root, on, NULL);
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
__attribute__((noinline)) static void mark_impl(Document *d, Node *n, bool layout, bool selectors, const char *reason, bool subtree, bool style) {
    if (!d) return;
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (n && n == d->html && style) html_dirty_log(reason, subtree, selectors, layout, __builtin_return_address(0), __builtin_return_address(1));
#endif
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
/* Allocated only for controls whose selection is touched. Other Node.ext users
   have distinct free-function discriminants; cloning starts with no selection. */
typedef struct { int anchor, focus; } ControlSelection;
static void control_selection_free(void *p) { free(p); }
bool node_control_selection_get(Node *n, int *anchor, int *focus) {
    if (!n || !n->ext || n->ext_free != control_selection_free) return false;
    const ControlSelection *s = n->ext;
    *anchor = s->anchor; *focus = s->focus;
    return true;
}
void node_control_selection_set(Node *n, int anchor, int focus) {
    if (!n || n->type != NODE_ELEMENT || n->ns != NS_HTML || (n->tag != A_input && n->tag != A_textarea)) return;
    if (n->ext && n->ext_free != control_selection_free) return;
    if (!n->ext) { n->ext = xcalloc(1, sizeof(ControlSelection)); n->ext_free = control_selection_free; }
    ControlSelection *s = n->ext;
    s->anchor = LMAX(0, anchor); s->focus = LMAX(0, focus);
    Document *d = n->doc;
    if (d && d->focus == n) {
        d->csel_n = n; d->csel[0] = s->anchor; d->csel[1] = s->focus; d->paint_version++;
    }
}
void doc_set_focus(Document *d, Node *n) {
    Node *old = d->focus;
    if (old == n) return;
    if (doc_uses_focus_within(d)) {
        /* Only ancestors on one side of the common ancestor change state. A precise
         * move has also restyled the subjects of sibling, descendant and :has
         * dependents, so those ancestors rematch only themselves: a keyed mark
         * would let another key changed in this batch reselect their subtrees. */
        bool precise = doc_focus_within_moved(d, old, n);
        if (!precise) selector_change_key(d, ":focus-within");
        for (int side = 0; side < 2; side++)
            for (Node *a = side ? n : old; a; a = a->parent ? a->parent : a->host) {
                if (a->type != NODE_ELEMENT) continue;
                Node *other = side ? old : n;
                if (other && node_is_inclusive_ancestor(a, other)) break;
                mark_impl(d, a, true, !precise, NULL, false, true);
                if (!precise) node_sibling_selectors_changed(a, 0);
            }
    }
    if (old) { old->flags &= ~(uint32_t)NF_FOCUS; doc_mark_state_dirty(d, old, true); }
    d->focus = n;
    d->csel_n = NULL;
    if (n) {
        int anchor = 0, focus = 0;
        (void)node_control_selection_get(n, &anchor, &focus);
        node_control_selection_set(n, anchor, focus);
        n->flags |= NF_FOCUS; doc_mark_state_dirty(d, n, true);
    }
}
static bool style_only_attr(const char *name) { return !strcmp(name, "style") || !strcmp(name, "class"); }
/* CSS may precisely invalidate attribute-selector subjects. DOM-only users
 * retain the conservative behavior without linking the style engine. */
__attribute__((weak)) bool node_attribute_style_changed(Node *n, const char *name, bool value_only) { (void)n; (void)name; (void)value_only; return false; }
/* A link's new href keeps :link and :any-link (sites rewrite them on touch for click tracking);
   only [href] value tests can flip. <base> moves URL resolution, so it keeps the ordinary path. */
static bool href_value_edit(const Node *n, const char *name, bool presence) {
    return !presence && n->ns == NS_HTML && str_ieq(name, "href") && !node_is(n, A_base);
}
__attribute__((weak)) bool css_attribute_observed(Node *n, const char *name) { (void)n; (void)name; return true; }
__attribute__((weak)) bool css_selector_only_attribute(const char *name) { return str_istarts(name, "data-") || str_istarts(name, "aria-"); }
/* ... and the [style] tests when an element's style attribute changes: presence tests (`:not([style])`)
 * when it comes or goes, value tests (`[style*=x]`) on any edit. */
__attribute__((weak)) bool node_style_attribute_changed(Node *n, bool presence) { (void)n; (void)presence; return true; }
static void mark_attribute(Node *n, const char *name, bool presence) {
    Document *d = n->doc;
    if (node_is(n, A_img) && (str_ieq(name, "src") || str_ieq(name, "srcset") || str_ieq(name, "sizes"))) n->flags |= NF_IMG_REQUEST;
    if (d && (str_ieq(name, "src") || str_ieq(name, "srcset") || str_ieq(name, "sizes") || str_ieq(name, "media"))) d->resource_version++;
    if (d && node_attribute_style_changed(n, name, href_value_edit(n, name, presence))) {
        d->dom_version++; d->selector_version++;
        if (str_ieq(name, "disabled") && node_is(n, A_fieldset)) mark_impl(d, n, false, true, NULL, true, true);   /* its descendants' :disabled */
        if (str_ieq(name, "src") || str_ieq(name, "srcset") || str_ieq(name, "sizes") || str_ieq(name, "poster")) mark_impl(d, n, true, false, NULL, false, true);
        mark_sheet_content(d, n);
        if (d->on_mutation) d->on_mutation(d, n);
    } else {
        bool inline_style = str_ieq(name, "style");
        /* cached presence matches key on selector_version, value tests on dom_version */
        if (inline_style && d && node_style_attribute_changed(n, presence) && presence) d->selector_version++;
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
        if ((n->tag == A_iframe || n->tag == A_frame) && n->ns == NS_HTML) d->frame_version++;
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
        if (parent->doc && is_connected(parent)) { parent->flags |= NF_EMPTY_CHANGED; parent->doc->text_emptiness_version++; }
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
    if (is_connected(parent)) { mark_connected(child, true, tree_doc(parent)); record_tree_change(parent->doc, child); }
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
        if (p->doc && is_connected(p)) { p->flags |= NF_EMPTY_CHANGED; p->doc->text_emptiness_version++; }
    }
    mark_connected(c, false, tree_doc(p));
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
    if (n->doc && n->doc->csel_n == n) n->doc->csel_n = NULL;
    for (int i = 0; i < n->nattrs; i++) free(n->attrs[i].value);
    free(n->attrs); free(n->text); free(n->inline_style_src); free(n->classes); free(n->is_value);
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
/* Capture only actual direct head-child metadata from an unexposed native parse.
   The live prefix DOM and its script/stylesheet owners are always separate. */
void doc_capture_parser_meta(Document *target, const Document *parsed) {
    for (int i = 0; i < target->nparser_meta * 2; i++) free(target->parser_meta[i]);
    free(target->parser_meta); target->parser_meta = NULL; target->nparser_meta = 0;
    target->parser_meta_captured = true;
    for (Node *n = parsed && parsed->head ? parsed->head->first : NULL; n; n = n->next) {
        if (n->type != NODE_ELEMENT || n->tag != A_meta) continue;
        const char *equiv = node_attr(n, "http-equiv"), *content = node_attr(n, "content");
        size_t index = (size_t)target->nparser_meta * 2;
        target->parser_meta = xrealloc(target->parser_meta, (index + 2) * sizeof *target->parser_meta);
        target->parser_meta[index] = equiv ? xstrdup(equiv) : NULL;
        target->parser_meta[index + 1] = content ? xstrdup(content) : NULL;
        target->nparser_meta++;
    }
}
void doc_retire_owned(Document *owner, Document *retired) {
    if (!owner || !retired || owner == retired) return;
    for (int i = 0; i < owner->retired_documents.n; i++)
        if (owner->retired_documents.v[i] == retired) return;
    vec_push(owner->retired_documents, retired);
}

typedef VEC(Document *) DocumentList;
static void doc_list_add(DocumentList *list, Document *d) {
    if (!d) return;
    for (int i = 0; i < list->n; i++) if (list->v[i] == d) return;
    vec_push(*list, d);
}
static DocumentList doc_owned_documents(Document *root) {
    DocumentList documents = {0};
    doc_list_add(&documents, root);
    for (int i = 0; i < documents.n; i++) {
        Document *d = documents.v[i];
        doc_list_add(&documents, d->template_contents_owner);
        for (int j = 0; j < d->retired_documents.n; j++) doc_list_add(&documents, d->retired_documents.v[j]);
    }
    return documents;
}
static void doc_release_resources(Document *d) {
    for (int j = 0; j < d->ndimg; j++) node_release(d->dimg[j]);
    free(d->dimg); d->dimg = NULL; d->ndimg = d->cdimg = 0;
    if (d->selector_cache && d->selector_cache_free) d->selector_cache_free(d->selector_cache);
    d->selector_cache = NULL;
    free(d->modals.v); d->modals.v = NULL; d->modals.n = d->modals.cap = 0;
    for (int j = 0; j < 2; j++) if (d->sel[j]) { node_release(d->sel[j]); d->sel[j] = NULL; }
    hm_free(&d->id_cache, NULL); hm_free(&d->id_dups, NULL);
}
static void doc_release_tree(Document *d, bool final) {
    Node *c = d->node.first;
    d->node.first = d->node.last = d->html = d->head = d->body = NULL;
    while (c) {
        Node *next = c->next;
        c->parent = c->prev = c->next = NULL; c->js = NULL;
        if (final) { c->refcount = 0; node_free_tree(c); }
        else node_release(c); /* release only the old document-parent reference */
        c = next;
    }
}
void doc_clear_retired_trees(Document *root) {
    /* Context retirement releases child/auxiliary trees promptly, after all
     * wrappers are gone. The Page may be sleeping, so its own tree/resources
     * stay alive. Retain every document's metadata for nodes adopted into it. */
    DocumentList documents = doc_owned_documents(root);
    for (int i = 0; i < documents.n; i++)
        if (documents.v[i] != root) doc_release_resources(documents.v[i]);
    for (int i = 0; i < documents.n; i++)
        if (documents.v[i] != root) doc_release_tree(documents.v[i], false);
    vec_free(documents);
}
void doc_free(Document *root) {
    /* Adopted template contents can still be held by another document's tree.
     * Keep every owner alive until all trees and retained node references in
     * this retirement group have been released. Self/duplicate associations
     * are harmless; the release phases also tolerate an already-cleared tree. */
    DocumentList documents = doc_owned_documents(root);
    for (int i = 0; i < documents.n; i++) doc_release_resources(documents.v[i]);
    for (int i = 0; i < documents.n; i++) doc_release_tree(documents.v[i], true);
    for (int i = 0; i < documents.n; i++) {
        Document *d = documents.v[i];
        for (int j = 0; j < d->nparser_meta * 2; j++) free(d->parser_meta[j]);
        free(d->parser_meta); free(d->retired_documents.v);
        free(d->url); free(d->base_url); free(d->title); free(d->content_type);
        free(d->csp); free(d->csp_ro); free(d->referrer_policy); free(d);
    }
    vec_free(documents);
}

/* attributes the engine reads by name (style, on*, href...): null-namespace ones and prefixed ones (xlink:href);
   setAttributeNS(ns, "style", ...) makes one that must not stand in for the element's own */
static inline bool plain_attr(const Attr *a) {
    static const char *xmlns;
    if (!a->ns || a->pfx) return true;
    if (!xmlns) xmlns = atom("xmlns");
    return a->name == xmlns;
}
static int attr_index(const Node *n, const char *name) {
    for (int i = 0; i < n->nattrs; i++) if (n->attrs[i].name == name && plain_attr(&n->attrs[i])) return i;
    const char *a = atom(name);
    for (int i = 0; i < n->nattrs; i++) if (n->attrs[i].name == a && plain_attr(&n->attrs[i])) return i;
    if (n->ns == NS_HTML) { for (int i = 0; i < n->nattrs; i++) if (str_ieq(n->attrs[i].name, name) && plain_attr(&n->attrs[i])) return i; }
    return -1;
}
const char *node_attr(const Node *n, const char *name) { if (!n || (n->type != NODE_ELEMENT && n->type != NODE_DOCTYPE)) return NULL; int i = attr_index(n, name); return i >= 0 ? n->attrs[i].value : NULL; }
const char *node_attr_atom(const Node *n, const char *name) {
    if (!n || (n->type != NODE_ELEMENT && n->type != NODE_DOCTYPE)) return NULL;
    for (int i = 0; i < n->nattrs; i++) if (n->attrs[i].name == name && plain_attr(&n->attrs[i])) return n->attrs[i].value;
    if (n->ns == NS_HTML && (n->flags & NF_ATTR_MIXED_CASE))
        for (int i = 0; i < n->nattrs; i++) if (str_ieq(n->attrs[i].name, name) && plain_attr(&n->attrs[i])) return n->attrs[i].value;
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
#if defined(__ANDROID__) && !defined(NDEBUG)
char dom_debug_key_names[1024][24];   /* debug: the last key set per selector-change bit (copied) */
#endif
static void selector_change_key_in(Document *d, const char *key, bool reselect) {
    if (!key) return;
    unsigned bit = dom_tree_key_bit(key);
    d->selector_change_keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
    if (reselect) d->reselect_change_keys[bit >> 6] |= UINT64_C(1) << (bit & 63);
#if defined(__ANDROID__) && !defined(NDEBUG)
    snprintf(dom_debug_key_names[bit], sizeof dom_debug_key_names[bit], "%s", key);
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
    if ((n->tag == A_iframe || n->tag == A_frame) && n->ns == NS_HTML && (str_ieq(name, "src") || str_ieq(name, "srcdoc"))) d->frame_version++;
    /* node_attribute_style_changed marks these subjects by candidate; their
     * keys must not widen the descendant reselect of unrelated changes. */
    bool precise = d->stylesheets && n->ns == NS_HTML && (css_selector_only_attribute(name) || href_value_edit(n, name, !value || attr_index(n, name) < 0));
    /* Attributes without built-in selector state are observed only through attribute
     * selectors: when none can test it on n (a map moving tiles by inline style),
     * no :has() or other dependency needs to see the edit. */
    bool unobserved = d->stylesheets && n->ns == NS_HTML && (precise || str_ieq(name, "style")) && !css_attribute_observed(n, name);
    if (!unobserved) selector_change_key_in(d, atom_lower(name, strlen(name)), !precise);
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
int node_attr_qindex(const Node *n, const char *qname) {
    const char *a = atom(qname);
    for (int i = 0; i < n->nattrs; i++) if (n->attrs[i].name == a) return i;
    return -1;
}
int node_attr_ns_index(const Node *n, const char *ns, const char *local) {
    for (int i = 0; i < n->nattrs; i++) {
        const Attr *a = &n->attrs[i];
        if (a->ns != ns) continue;
        if (!strcmp(a->pfx ? a->name + strlen(a->pfx) + 1 : a->name, local)) return i;
    }
    return -1;
}
void node_set_attr_at(Node *n, int i, const char *name, const char *value, const char *ns, const char *pfx) {
    /* Adding an async content attribute permanently clears force async. Its
       later removal does not restore the dynamic-script default. */
    if (n->type == NODE_ELEMENT && n->ns == NS_HTML && n->tag == A_script && !ns && !strcmp(name, "async"))
        n->flags |= NF_SCRIPT_NO_FORCE_ASYNC;
    if (n->ns == NS_HTML && !(n->flags & NF_ATTR_MIXED_CASE))
        for (const unsigned char *p = (const unsigned char *)name; *p; p++)
            if (*p >= 'A' && *p <= 'Z') { n->flags |= NF_ATTR_MIXED_CASE; break; }
    if (i >= 0 && !strcmp(n->attrs[i].value, value)) return;
    bool added = i < 0;
    /* only null-namespace id and class attributes are the element's ID and classes */
    bool plain = !(i >= 0 ? n->attrs[i].ns : ns);
    record_selector_attribute(n, name, value);
    Document *id_doc = plain && (n->flags & NF_CONNECTED) && atom(name) == A_id ? tree_doc(n) : NULL;
    id_map_del(id_doc, n, n->id);
    if (i >= 0) { free(n->attrs[i].value); n->attrs[i].value = xstrdup(value); }
    else {
        if (n->nattrs >= n->attrcap) { n->attrcap = n->attrcap ? n->attrcap * 2 : 4; n->attrs = xrealloc(n->attrs, sizeof(Attr) * (size_t)n->attrcap); }
        n->attrs[n->nattrs] = (Attr){ atom(name), xstrdup(value), ns, pfx }; i = n->nattrs++;
    }
    if (plain && (n->attrs[i].name == A_class || (n->ns == NS_HTML && str_ieq(name, "class")))) cache_classes(n, n->attrs[i].value);
    /* re-point cached id after realloc */
    if (plain || added) { n->id = NULL; for (int k = 0; k < n->nattrs; k++) if (n->attrs[k].name == A_id && !n->attrs[k].ns) { n->id = n->attrs[k].value; break; } }
    id_map_add(id_doc, n);
    trace_mutation(n->doc, n, "set", name);
    mark_attribute(n, name, added);
}
void node_set_attr(Node *n, const char *name, const char *value) {
    int i = attr_index(n, name);
    if (i >= 0 && !strcmp(n->attrs[i].value, value)) return;
    node_set_attr_at(n, i, i >= 0 ? n->attrs[i].name : name, value, NULL, NULL);
}
void node_remove_attr_at(Node *n, int i) {
    if (i < 0 || i >= n->nattrs) return;
    const char *name = n->attrs[i].name;
    if (!strcmp(name, "open")) node_set_modal(n, false);
    record_selector_attribute(n, name, NULL);
    bool plain = !n->attrs[i].ns;
    if (plain && n->attrs[i].name == A_id) { if (n->flags & NF_CONNECTED) id_map_del(tree_doc(n), n, n->id); n->id = NULL; }
    if (plain && (n->attrs[i].name == A_class || (n->ns == NS_HTML && str_ieq(name, "class")))) cache_classes(n, NULL);
    free(n->attrs[i].value);
    memmove(&n->attrs[i], &n->attrs[i + 1], sizeof(Attr) * (size_t)(n->nattrs - i - 1)); n->nattrs--;
    n->id = NULL; for (int k = 0; k < n->nattrs; k++) if (n->attrs[k].name == A_id && !n->attrs[k].ns) { n->id = n->attrs[k].value; break; }
    trace_mutation(n->doc, n, "remove", name);
    mark_attribute(n, name, true);
}
void node_remove_attr(Node *n, const char *name) { node_remove_attr_at(n, attr_index(n, name)); }
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
static const Node *node_host_parent(const Node *n) { return n ? (n->parent ? n->parent : n->host) : NULL; }
bool node_host_including_ancestor(const Node *ancestor, const Node *node) {
    if (!ancestor || !node) return false;
    const Node *root = node, *slow = node, *fast = node;
    for (const Node *p = node; p; p = node_host_parent(p)) {
        if (p == ancestor) return true;
        root = p;
        slow = node_host_parent(slow);
        fast = node_host_parent(node_host_parent(fast));
        if (slow && slow == fast) return true; /* reject an already malformed cycle */
    }
    /* Ordinary light/shadow ancestry stays allocation-free. Template contents
     * retain their separate host representation, even after direct adoption. */
    if (root->type != NODE_FRAGMENT || !(root->flags & NF_INERT) || root->host) return false;
    VEC(const Node *) pending = {0}; HMap seen = {0}; bool found = false;
    vec_push(pending, ancestor);
    while (pending.n) {
        const Node *n = pending.v[--pending.n];
        if (n == node) { found = true; break; }
        char key[2 * sizeof(void *) + 3]; snprintf(key, sizeof key, "%p", (const void *)n);
        if (hm_get(&seen, key)) continue;
        hm_put(&seen, key, (void *)n);
        if (n->first) vec_push(pending, n->first);
        if (n != ancestor && n->next) vec_push(pending, n->next);
        if (n->shadow_root) vec_push(pending, n->shadow_root);
        if (n->template_content) vec_push(pending, n->template_content);
    }
    hm_free(&seen, NULL); vec_free(pending);
    return found;
}

bool node_is_inclusive_ancestor(const Node *a, const Node *b) { for (; b; b = b->parent) if (a == b) return true; return false; }

static void text_rec(const Node *n, SB *b) {
    for (const Node *c = n->first; c; c = c->next) {
        if (c->type == NODE_TEXT || c->type == NODE_CDATA) sb_put(b, c->text, c->text_len);
        else if (c->type == NODE_ELEMENT || c->type == NODE_FRAGMENT) text_rec(c, b);
    }
}
char *node_text_content(const Node *n) { size_t len; return node_text_content_n(n, &len); }
char *node_text_content_n(const Node *n, size_t *len) {
    if (n->type == NODE_TEXT || n->type == NODE_COMMENT || n->type == NODE_CDATA || n->type == NODE_PI) { *len = n->text_len; return xstrndup(n->text, n->text_len); }
    SB b; sb_init(&b); text_rec(n, &b); *len = b.n; return sb_take(&b);
}
void node_set_character_data(Node *n, const char *s, size_t len) {
    /* Only a flip of the parent's own :empty matters, not a text node that
     * empties beside other content (frameworks clear and refill text often). */
    bool text_node = n->type == NODE_TEXT || n->type == NODE_CDATA;
    bool emptiness_changed = !n->text_len != !len, was_empty = emptiness_changed && text_node && n->parent && has_empty_content(n->parent);
    char *replacement = xstrndup(s, len);
    free(n->text); n->text = replacement; n->text_len = len;
    /* Ordinary edits only affect layout. The style engine tracks selectors
     * that depend on another element's emptiness (:has and combinators). */
    Node *dirty = n;
    if (emptiness_changed && text_node && n->parent && was_empty != has_empty_content(n->parent)) {
        dirty = n->parent;
        if (n->doc && is_connected(n->parent)) { n->parent->flags |= NF_EMPTY_CHANGED; n->doc->text_emptiness_version++; }
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

void node_set_is_value(Node *n, const char *value, size_t length) {
    char *copy = value ? xstrndup(value, length) : NULL;
    free(n->is_value); n->is_value = copy; n->is_value_len = value ? length : 0;
}

Node *node_clone(Node *n, bool deep, Document *d) {
    Node *c;
    switch (n->type) {
    case NODE_DOCUMENT: {
        const Document *source = (const Document *)n;
        Document *copy = doc_new(source->url);
        copy->node.ns = source->node.ns;
        copy->interface_kind = source->interface_kind;
        copy->charset = source->charset;
        copy->quirks = source->quirks;
        copy->ready_state = 2;
        if (source->content_type) copy->content_type = xstrdup(source->content_type);
        copy->base_url = xstrdup(doc_base(source));
        /* Deep clones retain their base element and its original resolution
         * context. A shallow clone has no such node, so preserve its value. */
        if (!deep) for (Node *x = source->node.first; x; x = node_next_in_tree(x, (Node *)&source->node)) {
            if (x->type != NODE_ELEMENT || x->ns != NS_HTML || x->tag != A_base) continue;
            const char *href = node_attr(x, "href");
            if (!href) continue;
            char *base = url_join(doc_base(source), href);
            if (base) { free(copy->base_url); copy->base_url = base; }
            break;
        }
        d = copy; c = &copy->node;
        break;
    }
    case NODE_ELEMENT: c = node_new_element(d, n->tag, n->ns); c->xns = n->xns; c->xpfx = n->xpfx; c->xln = n->xln; for (int i = 0; i < n->nattrs; i++) node_set_attr_at(c, -1, n->attrs[i].name, n->attrs[i].value, n->attrs[i].ns, n->attrs[i].pfx); break;
    case NODE_TEXT: c = node_new_text(d, n->text, n->text_len); break;
    case NODE_COMMENT: c = node_new_comment(d, n->text, n->text_len); break;
    case NODE_DOCTYPE: c = node_new_doctype(d, n->text); for (int i = 0; i < n->nattrs; i++) node_set_attr(c, n->attrs[i].name, n->attrs[i].value); break;
    case NODE_PI: c = node_new_pi(d, n->tag, n->text, n->text_len); break;
    case NODE_CDATA: c = node_new_cdata(d, n->text, n->text_len); break;
    default: c = node_new_fragment(d); break;
    }
    if (n->type == NODE_ELEMENT && n->is_value) node_set_is_value(c, n->is_value, n->is_value_len);
    if (n->type == NODE_ELEMENT && n->tag == A_script) c->flags |= n->flags & NF_SCRIPT_STARTED;   /* "already started" is cloned */
    if (n->template_content) {
        c->template_content = node_clone(n->template_content, deep, doc_template_contents_owner(d));
        c->template_content->refcount = 1; c->template_content->flags |= NF_INERT;
    }
    if (deep) for (Node *k = n->first; k; k = k->next) node_append(c, node_clone(k, true, d));
    if (n->type == NODE_DOCUMENT) {
        for (Node *x = c->first; x; x = x->next) if (x->type == NODE_ELEMENT) { d->html = x; break; }
        if (d->html) for (Node *x = d->html->first; x; x = x->next) {
            if (x->type != NODE_ELEMENT || x->ns != NS_HTML) continue;
            if (!d->head && x->tag == A_head) d->head = x;
            if (!d->body && (x->tag == A_body || x->tag == A_frameset)) d->body = x;
        }
    }
    /* a clonable shadow root is cloned with its host, its children always */
    if (n->type == NODE_ELEMENT && n->shadow_root && (n->shadow_root->flags & NF_SR_CLONABLE)) {
        Node *sr = node_attach_shadow(c, n->shadow_root->flags & (NF_SR_CLOSED | NF_SR_DELEGATES | NF_SR_SERIALIZABLE | NF_SR_CLONABLE | NF_SR_MANUAL | NF_SR_DECLARATIVE));
        if (sr) for (Node *k = n->shadow_root->first; k; k = k->next) node_append(sr, node_clone(k, true, d));
    }
    return c;
}

bool node_valid_shadow_host(const Node *n) {
    static const char *const ok[] = { "article", "aside", "blockquote", "body", "div", "footer", "h1", "h2", "h3", "h4", "h5", "h6", "header", "main", "nav", "p", "section", "span", NULL };
    if (!n || n->type != NODE_ELEMENT || n->ns != NS_HTML) return false;
    for (int i = 0; ok[i]; i++) if (!strcmp(n->tag, ok[i])) return true;
    /* a valid custom element name */
    const char *t = n->tag;
    if (!(t[0] >= 'a' && t[0] <= 'z') || !strchr(t, '-')) return false;
    for (const unsigned char *q = (const unsigned char *)t; *q; q++) if (*q >= 'A' && *q <= 'Z') return false;
    static const char *const bad[] = { "annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri", "font-face-format", "font-face-name", "missing-glyph", NULL };
    for (int i = 0; bad[i]; i++) if (!strcmp(t, bad[i])) return false;
    return true;
}
Node *node_attach_shadow(Node *host, uint32_t flags) {
    if (!host || host->shadow_root || !node_valid_shadow_host(host)) return NULL;
    Node *f = node_new_fragment(host->doc);
    f->host = host; f->refcount = 1; f->flags |= flags;
    host->shadow_root = f;
    if (host->flags & NF_CONNECTED) { f->flags |= NF_CONNECTED; if (host->doc) doc_mark_dirty(host->doc, host); }
    return f;
}

bool html_is_void(const char *t) {
    static const char *v[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr", "keygen", "basefont", "bgsound", "frame" };
    for (size_t i = 0; i < ARRLEN(v); i++) if (!strcmp(t, v[i])) return true;
    return false;
}
static void esc(SB *b, const char *s, size_t n, bool attr) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') sb_puts(b, "&amp;"); else if (c == '<') sb_puts(b, "&lt;"); else if (c == '>') sb_puts(b, "&gt;");
        else if (c == '"' && attr) sb_puts(b, "&quot;"); else if ((unsigned char)c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) { sb_puts(b, "&nbsp;"); i++; }
        else sb_putc(b, c);
    }
}
static void ser(const Node *n, SB *b);
/* getHTML(): the shadow roots to serialize */
static __thread struct { bool on, serializable; Node *const *roots; int nroots; } g_ser;
static void ser_shadow(const Node *host, SB *b) {
    const Node *sr = host->shadow_root;
    bool want = g_ser.serializable && (sr->flags & NF_SR_SERIALIZABLE);
    for (int i = 0; !want && i < g_ser.nroots; i++) want = g_ser.roots[i] == sr;
    if (!want) return;
    sb_puts(b, (sr->flags & NF_SR_CLOSED) ? "<template shadowrootmode=\"closed\"" : "<template shadowrootmode=\"open\"");
    if (sr->flags & NF_SR_DELEGATES) sb_puts(b, " shadowrootdelegatesfocus=\"\"");
    if (sr->flags & NF_SR_SERIALIZABLE) sb_puts(b, " shadowrootserializable=\"\"");
    if (sr->flags & NF_SR_MANUAL) sb_puts(b, " shadowrootslotassignment=\"manual\"");
    if (sr->flags & NF_SR_CLONABLE) sb_puts(b, " shadowrootclonable=\"\"");
    sb_putc(b, '>');
    for (const Node *c = sr->first; c; c = c->next) ser(c, b);
    sb_puts(b, "</template>");
}
static void ser_children(const Node *n, const Node *src, SB *b) {
    if (g_ser.on && n->type == NODE_ELEMENT && n->shadow_root) ser_shadow(n, b);
    bool raw = n->type == NODE_ELEMENT && n->ns == NS_HTML && (n->tag == A_script || n->tag == A_style || n->tag == A_xmp || n->tag == A_iframe || n->tag == A_noscript || n->tag == A_plaintext || n->tag == atom("noembed") || n->tag == atom("noframes"));
    for (const Node *c = src->first; c; c = c->next) { if (raw && c->type == NODE_TEXT) sb_put(b, c->text, c->text_len); else ser(c, b); }
}
static void ser(const Node *n, SB *b) {
    switch (n->type) {
    case NODE_ELEMENT: {
        sb_putc(b, '<'); sb_puts(b, n->tag);
        if (n->is_value) {
            bool has_is = false;
            for (int i = 0; i < n->nattrs; i++)
                if (!n->attrs[i].ns && !strcmp(n->attrs[i].name, "is")) { has_is = true; break; }
            if (!has_is) { sb_puts(b, " is=\""); esc(b, n->is_value, n->is_value_len, true); sb_putc(b, '"'); }
        }
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
char *node_serialize_n(const Node *n, bool outer, size_t *length) {
    SB b; sb_init(&b);
    if (outer) ser(n, &b);
    else ser_children(n, n->template_content ? n->template_content : n, &b);
    if (length) *length = b.n;
    return sb_take(&b);
}
char *node_serialize(const Node *n, bool outer) { return node_serialize_n(n, outer, NULL); }
char *node_serialize_ex_n(const Node *n, bool outer, bool serializable, Node *const *roots, int nroots, size_t *length) {
    g_ser.on = true; g_ser.serializable = serializable; g_ser.roots = roots; g_ser.nroots = nroots;
    char *s = node_serialize_n(n, outer, length);
    memset(&g_ser, 0, sizeof g_ser);
    return s;
}
char *node_serialize_ex(const Node *n, bool outer, bool serializable, Node *const *roots, int nroots) {
    return node_serialize_ex_n(n, outer, serializable, roots, nroots, NULL);
}

Node *doc_get_element_by_id(Document *d, const char *id) {
    if (!id[0]) return NULL;
    if (!d->id_cache_ok) {
        hm_free(&d->id_cache, NULL); memset(&d->id_cache, 0, sizeof d->id_cache);
        hm_free(&d->id_dups, NULL); memset(&d->id_dups, 0, sizeof d->id_dups);
        for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node))
            if (n->type == NODE_ELEMENT && n->id && n->id[0]) {
                if (!hm_get(&d->id_cache, n->id)) hm_put(&d->id_cache, n->id, n);
                else hm_put(&d->id_dups, n->id, n);
            }
        d->id_cache_ok = true;
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

static const char *const k_ref_policies[] = { "no-referrer", "no-referrer-when-downgrade", "same-origin", "origin", "strict-origin", "origin-when-cross-origin", "strict-origin-when-cross-origin", "unsafe-url" };
/* the last policy token of a comma list Lumen knows, else -1 */
static int ref_policy_parse(const char *v) {
    int r = -1;
    while (v && *v) {
        while (*v == ' ' || *v == '\t' || *v == ',') v++;
        const char *e = v; while (*e && *e != ',') e++;
        const char *t = e; while (t > v && (t[-1] == ' ' || t[-1] == '\t')) t--;
        for (int i = 0; i < 8; i++) if (strlen(k_ref_policies[i]) == (size_t)(t - v) && !strncasecmp(v, k_ref_policies[i], (size_t)(t - v))) r = i;
        v = e;
    }
    return r;
}
static size_t url_origin_len(const char *u) { const char *p = strstr(u, "://"); if (!p) return 0; p += 3; while (*p && *p != '/' && *p != '?' && *p != '#') p++; return (size_t)(p - u); }
static bool url_trustworthy(const char *u) {
    if (!strncasecmp(u, "https:", 6) || !strncasecmp(u, "wss:", 4) || !strncasecmp(u, "file:", 5)) return true;
    const char *h = strstr(u, "://"); if (!h) return false; h += 3;
    return !strncmp(h, "localhost", 9) || !strncmp(h, "127.", 4) || !strncmp(h, "[::1]", 5);
}
static bool ref_apply(const char *ref, const char *url, int p, char *out, size_t n);
bool url_referrer(const char *ref, const char *url, const char *policy, char *out, size_t n) {
    if (!ref || strncasecmp(ref, "http", 4) || !url) return false;
    int p = policy && *policy ? ref_policy_parse(policy) : -1;
    return ref_apply(ref, url, p < 0 ? 6 : p, out, n);
}
bool doc_referrer(const Document *d, const char *url, const char *policy, char *out, size_t n) {
    const char *ref = d ? d->url : NULL;
    if (!ref || strncasecmp(ref, "http", 4) || !url) return false;
    int p = policy && *policy ? ref_policy_parse(policy) : -1;
    /* a <meta name=referrer> in the head (the last one wins) replaces the header's policy */
    if (p < 0 && d->head) for (const Node *m = d->head->first; m; m = m->next)
        if (node_is(m, atom("meta"))) { const char *nm = node_attr(m, "name"), *ct = node_attr(m, "content"); if (nm && ct && !strcasecmp(nm, "referrer")) { int q = ref_policy_parse(ct); if (q >= 0) p = q; } }
    if (p < 0 && d->referrer_policy) p = ref_policy_parse(d->referrer_policy);
    if (p < 0) p = 6;
    return ref_apply(ref, url, p, out, n);
}
static bool ref_apply(const char *ref, const char *url, int p, char *out, size_t n) {
    size_t ol = url_origin_len(ref), fl = strcspn(ref, "#");
    if (!ol || n < 2) return false;
    size_t tl = url_origin_len(url);
    bool same = tl == ol && !strncasecmp(ref, url, ol), down = url_trustworthy(ref) && !url_trustworthy(url);
    bool full;
    switch (p) {
    case 0: return false;
    case 1: if (down) return false; full = true; break;
    case 2: if (!same) return false; full = true; break;
    case 3: full = false; break;
    case 4: if (down) return false; full = false; break;
    case 5: full = same; break;
    case 7: full = true; break;
    default: if (!same && down) return false; full = same; break;
    }
    if (fl > 4096) full = false;
    size_t len = full ? fl : ol;
    if (len + 2 > n) len = n - 2;
    memcpy(out, ref, len); out[len] = 0;
    if (!full || len == ol) { out[len] = '/'; out[len + 1] = 0; }
    return true;
}
