#include "dom.h"

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

static Node *alloc_node(Document *d, int type) { Node *n = xcalloc(1, sizeof *n); n->type = (uint8_t)type; n->doc = d; n->flags = NF_STYLE_DIRTY | NF_LAYOUT_DIRTY; return n; }
Node *node_new_element(Document *d, const char *tag, int ns) { Node *n = alloc_node(d, NODE_ELEMENT); n->tag = atom(tag); n->ns = (uint8_t)ns; return n; }
Node *node_new_text(Document *d, const char *s, size_t len) { Node *n = alloc_node(d, NODE_TEXT); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom("#text"); return n; }
Node *node_new_comment(Document *d, const char *s, size_t len) { Node *n = alloc_node(d, NODE_COMMENT); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom("#comment"); return n; }
Node *node_new_fragment(Document *d) { Node *n = alloc_node(d, NODE_FRAGMENT); n->tag = atom("#document-fragment"); return n; }
Node *node_new_doctype(Document *d, const char *name) { Node *n = alloc_node(d, NODE_DOCTYPE); n->text = xstrdup(name ? name : "html"); n->text_len = strlen(n->text); n->tag = atom("#doctype"); return n; }

Node *node_new_pi(Document *d, const char *target, const char *s, size_t len) { Node *n = alloc_node(d, NODE_PI); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom(target); return n; }
Node *node_new_cdata(Document *d, const char *s, size_t len) { Node *n = alloc_node(d, NODE_CDATA); n->text = xstrndup(s, len); n->text_len = len; n->tag = atom("#cdata-section"); return n; }

static void mark_connected(Node *n, bool on) {
    for (Node *c = n; c; c = node_next_in_tree(c, n)) { if (on) c->flags |= NF_CONNECTED; else c->flags &= ~(uint32_t)NF_CONNECTED; }
}
static bool is_connected(Node *p) { return p->type == NODE_DOCUMENT || (p->flags & NF_CONNECTED); }

static void mark(Document *d, Node *n, bool layout) {
    if (!d) return;
    d->dom_version++;
    if (layout) d->layout_version++;
    if (n) {
        n->flags |= NF_STYLE_DIRTY | NF_LAYOUT_DIRTY;
        for (Node *p = n->parent; p; p = p->parent ? p->parent : p->host) {
            if ((p->flags & (NF_CHILD_STYLE_DIRTY | NF_LAYOUT_DIRTY)) == (NF_CHILD_STYLE_DIRTY | NF_LAYOUT_DIRTY)) break;
            p->flags |= NF_CHILD_STYLE_DIRTY | NF_LAYOUT_DIRTY;
            if (!p->parent && !p->host) break;
        }
    }
    if (d->on_mutation) d->on_mutation(d, n);
}
void doc_mark_dirty(Document *d, Node *n) { mark(d, n, true); }
void doc_mark_style_dirty(Document *d, Node *n) { mark(d, n, false); }
static bool style_only_attr(const char *name) { return !strcmp(name, "style") || !strcmp(name, "class"); }

void node_insert_before(Node *parent, Node *child, Node *ref) {
    if (child->type == NODE_FRAGMENT) {
        while (child->first) node_insert_before(parent, child->first, ref);
        return;
    }
    if (child->parent) node_remove(child);
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
    if (is_connected(parent)) mark_connected(child, true);
    child->flags |= NF_STYLE_DIRTY;
    doc_mark_dirty(parent->doc, child);
    doc_mark_dirty(parent->doc, parent);
}
void node_append(Node *parent, Node *child) { node_insert_before(parent, child, NULL); }

void node_remove(Node *c) {
    Node *p = c->parent; if (!p) return;
    if (c->prev) c->prev->next = c->next; else p->first = c->next;
    if (c->next) c->next->prev = c->prev; else p->last = c->prev;
    c->parent = c->prev = c->next = NULL;
    mark_connected(c, false);
    if (c->doc && c->doc->focus && node_is_inclusive_ancestor(c, c->doc->focus)) c->doc->focus = NULL;
    doc_mark_dirty(p->doc, p);
    if (c->refcount) c->refcount--;
}

void node_retain(Node *n) { n->refcount++; }
void node_release(Node *n) { if (n->refcount) n->refcount--; if (!n->refcount && !n->parent) node_free_tree(n); }

__attribute__((weak)) void style_free(struct ComputedStyle *s) { free(s); }
__attribute__((weak)) void box_detach_node(Node *n) { n->box = NULL; }
static void node_free_one(Node *n) {
    for (int i = 0; i < n->nattrs; i++) free(n->attrs[i].value);
    free(n->attrs); free(n->text); free(n->inline_style_src);
    if (n->ext && n->ext_free) n->ext_free(n->ext);
    if (n->style) style_free(n->style);
    if (n->box) box_detach_node(n);
    free(n);
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
bool node_has_attr(const Node *n, const char *name) { return node_attr(n, name) != NULL; }
void node_set_attr(Node *n, const char *name, const char *value) {
    int i = attr_index(n, name);
    if (i >= 0) { if (!strcmp(n->attrs[i].value, value)) return; free(n->attrs[i].value); n->attrs[i].value = xstrdup(value); }
    else {
        if (n->nattrs >= n->attrcap) { n->attrcap = n->attrcap ? n->attrcap * 2 : 4; n->attrs = xrealloc(n->attrs, sizeof(Attr) * (size_t)n->attrcap); }
        n->attrs[n->nattrs].name = atom(name); n->attrs[n->nattrs].value = xstrdup(value); i = n->nattrs++;
    }
    if (n->attrs[i].name == A_id) n->id = n->attrs[i].value;
    /* re-point cached id after realloc */
    for (int k = 0; k < n->nattrs; k++) if (n->attrs[k].name == A_id) n->id = n->attrs[k].value;
    mark(n->doc, n, !style_only_attr(name));
}
void node_remove_attr(Node *n, const char *name) {
    int i = attr_index(n, name); if (i < 0) return;
    if (n->attrs[i].name == A_id) n->id = NULL;
    free(n->attrs[i].value);
    memmove(&n->attrs[i], &n->attrs[i + 1], sizeof(Attr) * (size_t)(n->nattrs - i - 1)); n->nattrs--;
    for (int k = 0; k < n->nattrs; k++) if (n->attrs[k].name == A_id) n->id = n->attrs[k].value;
    mark(n->doc, n, !style_only_attr(name));
}
bool node_has_class(const Node *n, const char *cls) {
    const char *c = node_attr(n, "class"); if (!c) return false;
    size_t L = strlen(cls);
    while (*c) {
        while (is_ws((unsigned char)*c)) c++;
        const char *s = c; while (*c && !is_ws((unsigned char)*c)) c++;
        if ((size_t)(c - s) == L && !memcmp(s, cls, L)) return true;
    }
    return false;
}

Node *node_next_in_tree(Node *n, Node *root) {
    if (n->first) return n->first;
    while (n && n != root) { if (n->next) return n->next; n = n->parent; }
    return NULL;
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
void node_set_text_content(Node *n, const char *s) {
    if (n->type == NODE_TEXT || n->type == NODE_COMMENT || n->type == NODE_CDATA || n->type == NODE_PI) { free(n->text); n->text = xstrdup(s); n->text_len = strlen(s); doc_mark_dirty(n->doc, n); return; }
    while (n->first) { Node *c = n->first; node_remove(c); node_free_tree(c); }
    if (s && *s) node_append(n, node_new_text(n->doc, s, strlen(s)));
}
void node_text_append(Node *n, const char *s, size_t len) {
    n->text = xrealloc(n->text, n->text_len + len + 1); memcpy(n->text + n->text_len, s, len); n->text_len += len; n->text[n->text_len] = 0;
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
