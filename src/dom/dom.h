#ifndef LUMEN_DOM_H
#define LUMEN_DOM_H
#include "../base/util.h"

static inline unsigned dom_tree_key_bit(const char *key) { return hash_str(key, strlen(key)) & 1023u; }

/* Style, layout and scroll updates, shared across documents for observers. */
uint64_t doc_visual_generation(void);
void doc_visual_changed(void);
/* box geometry or computed styles changed (layout, restyle), not just scroll offsets; also a visual change */
uint64_t doc_box_generation(void);
void doc_boxes_changed(void);
/* the shell repainted page pixels or got user input; a rAF loop with neither is throttled (see js.cc) */
uint64_t doc_activity_generation(void);
void doc_activity(void);

enum { NODE_ELEMENT = 1, NODE_TEXT = 3, NODE_CDATA = 4, NODE_PI = 7, NODE_COMMENT = 8, NODE_DOCUMENT = 9, NODE_DOCTYPE = 10, NODE_FRAGMENT = 11 };
enum { NS_HTML = 0, NS_SVG = 1, NS_MATHML = 2, NS_NONE = 3 };

/* node flags */
enum {
    NF_STYLE_DIRTY = 1 << 0,     /* this node's style needs recompute */
    NF_CHILD_STYLE_DIRTY = 1 << 1,/* some descendant needs style */
    NF_LAYOUT_DIRTY = 1 << 2,
    NF_HOVER = 1 << 3, NF_ACTIVE = 1 << 4, NF_FOCUS = 1 << 5,
    NF_CHECKED = 1 << 6, NF_PARSER_INSERTED = 1 << 7, NF_SCRIPT_STARTED = 1 << 8,
    NF_CONNECTED = 1 << 9, NF_DISABLED = 1 << 10,
    NF_ATTR_MIXED_CASE = 1 << 12, /* conservative: an HTML attribute name contained ASCII uppercase */
    NF_MODAL = 1 << 14,          /* dialog opened with showModal */
    NF_FULLSCREEN = 1 << 13,     /* requestFullscreen: :fullscreen (on an <iframe>, for an element inside it) */
    NF_RESELECT_SUBTREE = 1 << 15, /* ancestry/selector changes cannot reuse descendant matches */
    NF_CHILD_LIST_DIRTY = 1 << 16, /* structural selector invalidation pending */
    NF_TREE_ANCESTOR = 1 << 17, /* scratch: ancestor of a pending child-list change */
    NF_TREE_RELATED = 1 << 18,  /* scratch: also preceding siblings / changed parent's children */
    NF_TREE_CHILD = 1 << 19,    /* scratch: direct child of a pending child-list change */
    NF_RESELECT_KEYED = 1 << 20, /* class/ID change: descendants reselect only if a rule tests those keys above its subject */
    NF_EMPTY_CHANGED = 1 << 21,  /* this connected element's :empty state flipped since the last style pass */
    NF_INERT = 1 << 11,           /* template content fragment */
    /* a shadow root's (a fragment with a host) metadata */
    NF_SR_CLOSED = 1 << 22, NF_SR_DELEGATES = 1 << 23, NF_SR_SERIALIZABLE = 1 << 24, NF_SR_CLONABLE = 1 << 25,
    NF_SR_MANUAL = 1 << 26, NF_SR_DECLARATIVE = 1 << 27,
    NF_SAN_MADE = 1 << 28,        /* made by a sanitizing parser, which frees it unless inserted */
};

/* name is an atom: the qualified name. ns and pfx are atoms of a namespaced attribute (setAttributeNS, XML), else NULL. */
typedef struct { const char *name; char *value; const char *ns, *pfx; } Attr;

struct Document;
struct ComputedStyle;
struct Box;

typedef struct Node {
    uint8_t type, ns;
    uint32_t flags;
    struct Node *parent, *first, *last, *prev, *next;
    struct Document *doc;
    const char *tag;               /* atom; lower-case for HTML */
    const char *xns, *xpfx, *xln;  /* atoms of elements from XML or createElementNS: a namespace URI outside NS_*, the prefix, the case-kept local name */
    const char *id;                /* cached id attr (points into attrs) */
    const char *class_name;        /* cached class attr (points into attrs) */
    const char **classes; int nclasses;
    uint64_t class_bits;           /* negative filter; positive hits compare atoms */
    Attr *attrs; int nattrs, attrcap;
    char *text; size_t text_len;   /* text / comment / doctype name */
    struct ComputedStyle *style;
    struct Box *box;
    float scroll_x, scroll_y;      /* element scroll offsets (kept across relayouts) */
    float cq_w, cq_h;              /* query-container content size from the last layout */
    void *js;                      /* JS wrapper (v8::Global*) */
    void *ext;                     /* element specific data (img, video, canvas, input) */
    void (*ext_free)(void *);
    struct Node *shadow_root;      /* attached shadow root (FRAGMENT with host) */
    struct Node *host;             /* for shadow roots */
    char *value_override;          /* form control value set by user/script */
    int8_t checked_override;       /* 0 unset, 1 checked, -1 unchecked */
    struct Node *template_content; /* for <template> */
    char *inline_style_src;        /* cached style="" for change detection */
    void *inline_decls;            /* parsed CSS declarations for style attr */
    void *anim_decls;              /* Web Animations effect values (above inline style, below !important) */
    void *match_memo;              /* style engine: matched rules, valid per selector_version */
    uint32_t refcount;             /* JS keeps nodes alive */
} Node;

typedef struct Document {
    Node node;
    char *url;
    char *base_url;               /* an about:blank or about:srcdoc frame's fallback base URL (its parent's), else NULL */
    char *title;
    char *content_type;           /* the response's MIME type of a loaded XML document, else NULL */
    char *csp, *csp_ro;           /* the response's Content-Security-Policy(-Report-Only) headers, joined; else NULL */
    int charset;   /* the document's encoding (base/encoding.h); 0 is UTF-8 */
    Node *html, *head, *body;
    bool quirks;
    int ready_state;              /* 0 loading, 1 interactive, 2 complete */
    uint64_t dom_version;         /* bumped on any mutation */
    uint64_t selector_version;    /* excludes inline CSS and nonempty text edits */
    uint64_t inheritance_version; /* unindexed connected selector changes; excludes precise attributes/tree edits */
    uint64_t text_emptiness_version;
    uint64_t tree_version;        /* child-list mutations, coalesced by the style engine */
    uint64_t frame_version;       /* connected iframes inserted, removed or given a new src/srcdoc */
    uint64_t tree_change_keys[16]; /* conservative keys in inserted/removed connected subtrees */
    uint64_t selector_change_keys[16]; /* changed class tokens/IDs and their attribute names */
    uint64_t reselect_change_keys[16]; /* the same, less data-/aria- attributes the style engine invalidates by candidate */
    bool selector_changes_unknown; /* non-keyed connected selector/state changes */
    const char *unknown_reason; /* debug: first source of this batch's unknown changes */
    bool tree_focus_changed; /* a child-list change moved/removed the focused node */
    unsigned debug_mutations;     /* bounded debug trace, explicitly armed on device */
    uint64_t sheet_version;       /* stylesheet content, attributes, tree membership/order */
    uint64_t layout_version;      /* bumped on mutations that can change layout beyond computed style */
    uint64_t paint_version;       /* visual changes without DOM/style/layout invalidation */
    uint64_t resource_version;    /* img src and computed background/mask image changes */
    float layout_vw, layout_vh;   /* viewport of the current box tree */
    HMap id_cache, id_dups; bool id_cache_ok; /* id -> first element, kept current by tree and id edits; ids seen twice */
    void *selector_cache; void (*selector_cache_free)(void *); /* bounded parsed query syntax */
    void *stylesheets;            /* css engine data */
    struct Node *focus;
    VEC(Node *) modals;
    struct Node *sel[2]; int selo[2]; bool sel_back; uint64_t sel_ver;   /* JS selection start/end (text offsets in bytes) */
    struct Node *csel_n; int csel[2];   /* <input>/<textarea> selection: the control it belongs to, anchor/focus in codepoints of its value */
    void *browser;                /* owning page */
    void (*on_mutation)(struct Document *, Node *);
} Document;
/* the URL a document's relative URLs resolve against (without <base>) */
static inline const char *doc_base(const Document *d) { return d->base_url ? d->base_url : d->url; }

Document *doc_new(const char *url);
void doc_free(Document *d);
Node *node_new_element(Document *d, const char *tag, int ns);
Node *node_new_text(Document *d, const char *s, size_t n);
Node *node_new_comment(Document *d, const char *s, size_t n);
Node *node_new_fragment(Document *d);
Node *node_new_doctype(Document *d, const char *name);
Node *node_new_pi(Document *d, const char *target, const char *s, size_t n);   /* target in tag, data in text */
Node *node_new_cdata(Document *d, const char *s, size_t n);
void node_append(Node *parent, Node *child);
void node_insert_before(Node *parent, Node *child, Node *ref);
void node_remove(Node *child);
void node_free_tree(Node *n);   /* free subtree if not referenced by JS */
void node_retain(Node *n);
void node_release(Node *n);
Node *node_clone(Node *n, bool deep, Document *into);

/* Caller supplies an interned name, ASCII lowercase for HTML. */
const char *node_attr_atom(const Node *n, const char *name);
const char *node_attr(const Node *n, const char *name);  /* name: atom or plain string */
bool node_has_attr(const Node *n, const char *name);
void node_set_attr(Node *n, const char *name, const char *value);
void node_remove_attr(Node *n, const char *name);
/* the DOM's exact lookups: the first attribute with qualified name qname (no case folding), or by namespace and local name;
   -1 if there is none */
int node_attr_qindex(const Node *n, const char *qname);
int node_attr_ns_index(const Node *n, const char *ns, const char *local);
/* sets attribute i's value, or with i < 0 appends one (qname, ns, pfx atoms) */
void node_set_attr_at(Node *n, int i, const char *qname, const char *value, const char *ns, const char *pfx);
void node_remove_attr_at(Node *n, int i);
void node_set_modal(Node *n, bool modal);
bool node_has_class(const Node *n, const char *cls);
static inline uint64_t node_class_bit(const char *cls) {
    uintptr_t p = (uintptr_t)cls;
    return UINT64_C(1) << (((p >> 4) ^ (p >> 10)) & 63);
}
/* Selector names and cached class tokens are interned. No string scan/lock. */
static inline bool node_has_class_atom(const Node *n, const char *cls) {
    if (!n || !(n->class_bits & node_class_bit(cls))) return false;
    for (int i = 0; i < n->nclasses; i++) if (n->classes[i] == cls) return true;
    return false;
}
static inline bool node_is(const Node *n, const char *tag_atom) { return n && n->type == NODE_ELEMENT && n->tag == tag_atom && n->ns == NS_HTML; }

char *node_text_content(const Node *n);
char *node_text_content_n(const Node *n, size_t *len);   /* with its length: the text may hold NULs */
void node_set_text_content(Node *n, const char *s);
void node_set_character_data(Node *n, const char *s, size_t len);
void node_text_append(Node *n, const char *s, size_t len);
char *node_serialize(const Node *n, bool outer);  /* innerHTML/outerHTML */
Node *node_next_in_tree(Node *n, Node *root);     /* pre-order traversal */
Node *node_next_deep(Node *n, Node *root);        /* pre-order, entering shadow trees */
Node *doc_get_element_by_id(Document *d, const char *id);
void doc_mark_dirty(Document *d, Node *n);         /* style + layout dirty */
void doc_mark_style_dirty(Document *d, Node *n);   /* style only: layout follows from the computed style diff */
/* n's own selector state (hover, focus, form value, container size) changed
 * and the caller invalidated everything else that state reaches: restyle n,
 * and with subtree its descendants, without an unknown document-wide change. */
void doc_mark_state_dirty(Document *d, Node *n, bool subtree);
void doc_set_focus(Document *d, Node *n);         /* moves focus, invalidating :focus and :focus-within */
void dom_dump(const Node *n, int depth, FILE *f);
bool node_is_inclusive_ancestor(const Node *a, const Node *b);

/* HTML parser */
void html_parse(Document *d, const char *src, size_t len);
/* XML documents (also XHTML, SVG): d becomes an XML document; a well-formedness error leaves a parsererror element */
void xml_parse(Document *d, const char *src, size_t len);
bool xml_mime(const char *content_type);   /* text/xml, application/xml or any +xml type */
char *mime_essence(const char *content_type);   /* "type/subtype" in lower case, without parameters */
/* Fragment parsing for innerHTML; children appended to 'context' replacement fragment */
Node *html_parse_fragment(Document *d, Node *context, const char *src, size_t len);
/* The HTML Sanitizer: a canonical SanitizerConfig built by the bindings. Names are (namespace URI or NULL, local
   name); processing instructions use a NULL namespace and their target. */
typedef struct SanCfg SanCfg;
enum { SAN_ELEMENTS, SAN_REMOVE_ELEMENTS, SAN_REPLACE_ELEMENTS, SAN_ATTRIBUTES, SAN_REMOVE_ATTRIBUTES, SAN_PIS, SAN_REMOVE_PIS, SAN_NLISTS };
SanCfg *san_new(void);
void san_free(SanCfg *c);
void san_add(SanCfg *c, int list, const char *ns, const char *name);   /* name NULL: the list exists, empty */
/* an element's attributes (remove false) or removeAttributes list; the element must be in SAN_ELEMENTS */
void san_elem_attr(SanCfg *c, const char *el_ns, const char *el_name, bool remove, const char *ns, const char *name);
void san_flags(SanCfg *c, bool comments, bool data_attributes, bool javascript_urls);
/* the parser's sanitizer configuration (NULL: none) and its allow declarative shadow roots flag */
typedef struct { const SanCfg *san; bool dsd; } HtmlOpts;
void html_parse_ex(Document *d, const char *src, size_t len, const HtmlOpts *o);   /* html_parse allows DSD */
Node *html_parse_fragment_ex(Document *d, Node *context, const char *src, size_t len, const HtmlOpts *o);
/* attach a shadow root: flags are NF_SR_*; NULL if host already is one or cannot host one */
Node *node_attach_shadow(Node *host, uint32_t flags);
bool node_valid_shadow_host(const Node *n);
/* the HTML fragment serialization with shadow roots: serializable ones, and those in roots[] */
char *node_serialize_ex(const Node *n, bool outer, bool serializable, Node *const *roots, int nroots);
/* Incremental parser for document.write & streaming */
typedef struct HtmlParser HtmlParser;
HtmlParser *html_parser_new(Document *d, Node *fragment_context);
void html_parser_feed(HtmlParser *p, const char *src, size_t len);
void html_parser_finish(HtmlParser *p);
void html_parser_free(HtmlParser *p);
/* hook invoked when a </script> end tag is seen (parser-inserted scripts) */
extern void (*html_script_hook)(Document *d, Node *script, void *ud);
extern void *html_script_hook_ud;

uint32_t html_entity_lookup(const char *name, size_t n, uint32_t *second, size_t *consumed);
bool html_is_void(const char *tag);

/* Commonly used tag atoms, initialised by dom_init() */
extern const char *A_html, *A_head, *A_body, *A_title, *A_meta, *A_link, *A_style, *A_script, *A_noscript, *A_base,
    *A_div, *A_span, *A_p, *A_a, *A_img, *A_br, *A_hr, *A_ul, *A_ol, *A_li, *A_dl, *A_dt, *A_dd, *A_table, *A_tbody, *A_thead, *A_tfoot,
    *A_tr, *A_td, *A_th, *A_caption, *A_colgroup, *A_col, *A_form, *A_input, *A_button, *A_select, *A_option, *A_optgroup, *A_textarea,
    *A_label, *A_iframe, *A_video, *A_audio, *A_source, *A_track, *A_canvas, *A_svg, *A_math, *A_template, *A_pre, *A_code, *A_h1, *A_h2,
    *A_h3, *A_h4, *A_h5, *A_h6, *A_b, *A_i, *A_u, *A_s, *A_em, *A_strong, *A_small, *A_big, *A_font, *A_nobr, *A_center, *A_section,
    *A_article, *A_nav, *A_header, *A_footer, *A_aside, *A_main, *A_figure, *A_figcaption, *A_blockquote, *A_address, *A_details,
    *A_summary, *A_dialog, *A_menu, *A_fieldset, *A_legend, *A_object, *A_embed, *A_param, *A_picture, *A_frameset, *A_frame,
    *A_xmp, *A_plaintext, *A_listing, *A_area, *A_wbr, *A_keygen, *A_textpath, *A_slot, *A_sup, *A_sub, *A_abbr, *A_cite, *A_q, *A_mark, *A_time,
    *A_id, *A_class, *A_style_attr, *A_href, *A_src, *A_type, *A_rel, *A_name, *A_value, *A_hidden, *A_width, *A_height, *A_alt,
    *A_content, *A_charset, *A_http_equiv, *A_media, *A_async, *A_defer, *A_disabled, *A_checked, *A_selected, *A_placeholder,
    *A_colspan, *A_rowspan, *A_dir, *A_lang, *A_tabindex, *A_srcset, *A_sizes, *A_poster, *A_autoplay, *A_controls, *A_loop, *A_muted,
    *A_open, *A_nomodule, *A_crossorigin, *A_integrity, *A_bgcolor, *A_color, *A_face, *A_size, *A_align, *A_valign, *A_border,
    *A_cellpadding, *A_cellspacing, *A_text_attr, *A_background, *A_viewbox, *A_fill, *A_stroke, *A_d, *A_for_attr, *A_role, *A_multiple;
void dom_init(void);
#endif
