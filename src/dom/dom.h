#ifndef LUMEN_DOM_H
#define LUMEN_DOM_H
#include "../base/util.h"

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
    NF_INERT = 1 << 11,           /* template content fragment */
};

typedef struct { const char *name; char *value; } Attr; /* name is an atom */

struct Document;
struct ComputedStyle;
struct Box;

typedef struct Node {
    uint8_t type, ns;
    uint32_t flags;
    struct Node *parent, *first, *last, *prev, *next;
    struct Document *doc;
    const char *tag;               /* atom; lower-case for HTML */
    const char *id;                /* cached id attr (points into attrs) */
    Attr *attrs; int nattrs, attrcap;
    char *text; size_t text_len;   /* text / comment / doctype name */
    struct ComputedStyle *style;
    struct Box *box;
    float scroll_x, scroll_y;      /* element scroll offsets (kept across relayouts) */
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
    uint32_t refcount;             /* JS keeps nodes alive */
} Node;

typedef struct Document {
    Node node;
    char *url;
    char *base_url;
    char *title;
    Node *html, *head, *body;
    bool quirks;
    int ready_state;              /* 0 loading, 1 interactive, 2 complete */
    uint64_t dom_version;         /* bumped on any mutation */
    uint64_t layout_version;      /* bumped on mutations that can change layout beyond computed style */
    HMap id_cache; uint64_t id_cache_ver; bool id_cache_ok; /* id -> first element, rebuilt per dom_version */
    void *stylesheets;            /* css engine data */
    struct Node *focus;
    void *browser;                /* owning page */
    void (*on_mutation)(struct Document *, Node *);
} Document;

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

const char *node_attr(const Node *n, const char *name);  /* name: atom or plain string */
bool node_has_attr(const Node *n, const char *name);
void node_set_attr(Node *n, const char *name, const char *value);
void node_remove_attr(Node *n, const char *name);
bool node_has_class(const Node *n, const char *cls);
static inline bool node_is(const Node *n, const char *tag_atom) { return n && n->type == NODE_ELEMENT && n->tag == tag_atom && n->ns == NS_HTML; }

char *node_text_content(const Node *n);
void node_set_text_content(Node *n, const char *s);
void node_text_append(Node *n, const char *s, size_t len);
char *node_serialize(const Node *n, bool outer);  /* innerHTML/outerHTML */
Node *node_next_in_tree(Node *n, Node *root);     /* pre-order traversal */
Node *doc_get_element_by_id(Document *d, const char *id);
void doc_mark_dirty(Document *d, Node *n);         /* style + layout dirty */
void doc_mark_style_dirty(Document *d, Node *n);   /* style only: layout follows from the computed style diff */
void dom_dump(const Node *n, int depth, FILE *f);
bool node_is_inclusive_ancestor(const Node *a, const Node *b);

/* HTML parser */
void html_parse(Document *d, const char *src, size_t len);
/* Fragment parsing for innerHTML; children appended to 'context' replacement fragment */
Node *html_parse_fragment(Document *d, Node *context, const char *src, size_t len);
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
