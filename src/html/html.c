/* The WHATWG HTML parser: the tokenizer state machine and tree construction with every insertion
 * mode, the adoption agency, foster parenting, templates, foreign content and the customizable
 * <select> rules. Input is UTF-8; it is parsed when the parser is finished. */
#include "../dom/dom.h"
#include <ctype.h>
#include <pthread.h>
#include <strings.h>

void (*html_script_hook)(Document *d, Node *script, void *ud) = NULL;
void *html_script_hook_ud = NULL;

enum { T_CHARS, T_START, T_END, T_COMMENT, T_PI, T_DOCTYPE, T_EOF };
enum { C_OTHER, C_WS, C_NUL };   /* a character token run: anything, only whitespace, only NULs */

enum {
    S_DATA, S_RCDATA, S_RAWTEXT, S_SCRIPT, S_PLAINTEXT, S_TAG_OPEN, S_END_TAG_OPEN, S_TAG_NAME,
    S_RCDATA_LT, S_RCDATA_END_OPEN, S_RCDATA_END_NAME, S_RAWTEXT_LT, S_RAWTEXT_END_OPEN, S_RAWTEXT_END_NAME,
    S_SCRIPT_LT, S_SCRIPT_END_OPEN, S_SCRIPT_END_NAME, S_SCRIPT_ESC_START, S_SCRIPT_ESC_START_DASH,
    S_SCRIPT_ESC, S_SCRIPT_ESC_DASH, S_SCRIPT_ESC_DASH_DASH, S_SCRIPT_ESC_LT, S_SCRIPT_ESC_END_OPEN, S_SCRIPT_ESC_END_NAME,
    S_SCRIPT_DESC_START, S_SCRIPT_DESC, S_SCRIPT_DESC_DASH, S_SCRIPT_DESC_DASH_DASH, S_SCRIPT_DESC_LT, S_SCRIPT_DESC_END,
    S_BEFORE_ATTR_NAME, S_ATTR_NAME, S_AFTER_ATTR_NAME, S_BEFORE_ATTR_VALUE, S_ATTR_VALUE_DQ, S_ATTR_VALUE_SQ,
    S_ATTR_VALUE_UQ, S_AFTER_ATTR_VALUE_Q, S_SELF_CLOSING, S_BOGUS_COMMENT, S_MARKUP_DECL, S_COMMENT_START,
    S_COMMENT_START_DASH, S_COMMENT, S_COMMENT_LT, S_COMMENT_LT_BANG, S_COMMENT_LT_BANG_DASH, S_COMMENT_LT_BANG_DASH_DASH,
    S_COMMENT_END_DASH, S_COMMENT_END, S_COMMENT_END_BANG, S_DOCTYPE, S_BEFORE_DT_NAME, S_DT_NAME, S_AFTER_DT_NAME,
    S_AFTER_DT_PUBLIC_KW, S_BEFORE_DT_PUBLIC, S_DT_PUBLIC_DQ, S_DT_PUBLIC_SQ, S_AFTER_DT_PUBLIC, S_BETWEEN_DT_IDS,
    S_AFTER_DT_SYSTEM_KW, S_BEFORE_DT_SYSTEM, S_DT_SYSTEM_DQ, S_DT_SYSTEM_SQ, S_AFTER_DT_SYSTEM, S_BOGUS_DOCTYPE,
    S_CDATA, S_CDATA_BRACKET, S_CDATA_END
};
enum {
    M_INITIAL, M_BEFORE_HTML, M_BEFORE_HEAD, M_IN_HEAD, M_IN_HEAD_NOSCRIPT, M_AFTER_HEAD, M_IN_BODY, M_TEXT,
    M_IN_TABLE, M_IN_TABLE_TEXT, M_IN_CAPTION, M_IN_COLUMN_GROUP, M_IN_TABLE_BODY, M_IN_ROW, M_IN_CELL, M_IN_TEMPLATE,
    M_AFTER_BODY, M_IN_FRAMESET, M_AFTER_FRAMESET, M_AFTER_AFTER_BODY, M_AFTER_AFTER_FRAMESET
};
enum { SC_DEFAULT, SC_LIST, SC_BUTTON, SC_TABLE };

typedef struct { const char *name; char *value; } TAttr;

typedef struct Redir { Node *el, *parent, *before; } Redir;
typedef struct {
    int type, cls;
    const char *name;             /* tag name atom, ASCII lowercase */
    TAttr *attrs; int nattrs;
    bool self_closing;
    const char *data; size_t len; /* characters, comment or processing instruction data */
    const char *target;           /* processing instruction target */
} Tok;

struct HtmlParser {
    Document *doc;
    SB in;
    bool scripting, quirks, stopped;
    /* tokenizer */
    int state;
    SB tag; bool end_tag, self_closing; VEC(TAttr) attrs; SB an, av; bool attr_open;
    SB text, tmp, chars;
    SB dt_name, dt_pub, dt_sys; bool dt_has_name, dt_has_pub, dt_has_sys, dt_force_quirks;
    const char *last_start;
    /* tree construction */
    VEC(Node *) open;
    VEC(Node *) fmt;              /* the list of active formatting elements; NULL is a marker */
    VEC(int) tmpl_modes;
    int mode, orig_mode;
    Node *head_el, *form_el;
    Node *ctx, *root;             /* fragment case: the context element and the root html element */
    bool frameset_ok, foster, skip_lf, saw_selectedcontent;
    SB table_text; bool table_text_other;
    /* the parser sanitizer configuration, the insertion target redirection map, the elements made while
       sanitizing (the ones never inserted are freed with the parser), and declarative shadow root templates */
    const SanCfg *san; bool dsd;
    VEC(Redir) redir;
    VEC(Node *) made, dsd_tmpls;
};
#define P HtmlParser *p

/* ---------------- names ---------------- */
typedef struct { const char *v[96]; int n; } ASet;
static bool aset_has(const ASet *s, const char *a) { for (int i = 0; i < s->n; i++) if (s->v[i] == a) return true; return false; }
static void aset_init(ASet *s, const char *names) {
    s->n = 0;
    for (const char *q = names; *q;) {
        while (*q == ' ') q++;
        const char *e = q; while (*e && *e != ' ') e++;
        if (e > q && s->n < 96) s->v[s->n++] = atomn(q, (size_t)(e - q));
        q = e;
    }
}
static ASet S_special, S_scope, S_implied, S_implied_all, S_block, S_block_end, S_fmt, S_head_start, S_breakout,
    S_table_ignore, S_mathml_text, S_svg_ip;
static const char *K_applet, *K_marquee, *K_basefont, *K_bgsound, *K_noframes, *K_noembed, *K_image, *K_rb, *K_rp, *K_rt,
    *K_rtc, *K_ruby, *K_mglyph, *K_malignmark, *K_annotation_xml, *K_foreignObject, *K_foreignobject, *K_desc,
    *K_selectedcontent, *K_datalist, *K_encoding, *K_multiple, *K_selected, *K_color, *K_publicId, *K_systemId,
    *K_shadowrootmode;
static pthread_once_t names_once = PTHREAD_ONCE_INIT;
static void names_init(void) {
    if (!A_html) dom_init();   /* the tag atoms, for a parse before dom_init() */
    aset_init(&S_special, "address applet area article aside base basefont bgsound blockquote body br button caption center "
        "col colgroup dd details dir div dl dt embed fieldset figcaption figure footer form frame frameset h1 h2 h3 h4 h5 h6 "
        "head header hgroup hr html iframe img input keygen li link listing main marquee menu meta nav noembed noframes "
        "noscript object ol p param plaintext pre script search section source style summary table tbody td "
        "template textarea tfoot th thead title tr track ul wbr xmp");
    aset_init(&S_scope, "applet caption html table td th marquee object template");
    aset_init(&S_implied, "dd dt li optgroup option p rb rp rt rtc");
    aset_init(&S_implied_all, "caption colgroup dd dt li optgroup option p rb rp rt rtc tbody td tfoot th thead tr");
    aset_init(&S_block, "address article aside blockquote center details dialog dir div dl fieldset figcaption figure "
        "footer header hgroup main menu nav ol p search section summary ul");
    aset_init(&S_block_end, "address article aside blockquote button center details dialog dir div dl fieldset figcaption "
        "figure footer header hgroup listing main menu nav ol pre search section summary ul");
    aset_init(&S_fmt, "b big code em font i s small strike strong tt u");
    aset_init(&S_head_start, "base basefont bgsound link meta noframes script style template title");
    aset_init(&S_breakout, "b big blockquote body br center code dd div dl dt em embed h1 h2 h3 h4 h5 h6 head hr i img "
        "li listing menu meta nobr ol p pre ruby s small span strong strike sub sup table tt u ul var");
    aset_init(&S_table_ignore, "caption col colgroup frame head tbody td tfoot th thead tr");
    aset_init(&S_mathml_text, "mi mo mn ms mtext");
    aset_init(&S_svg_ip, "foreignObject foreignobject desc title");
    K_applet = atom("applet"); K_marquee = atom("marquee"); K_basefont = atom("basefont"); K_bgsound = atom("bgsound");
    K_noframes = atom("noframes"); K_noembed = atom("noembed"); K_image = atom("image"); K_rb = atom("rb"); K_rp = atom("rp");
    K_rt = atom("rt"); K_rtc = atom("rtc"); K_ruby = atom("ruby"); K_mglyph = atom("mglyph"); K_malignmark = atom("malignmark");
    K_annotation_xml = atom("annotation-xml"); K_foreignObject = atom("foreignObject"); K_foreignobject = atom("foreignobject");
    K_desc = atom("desc"); K_selectedcontent = atom("selectedcontent"); K_datalist = atom("datalist");
    K_encoding = atom("encoding"); K_multiple = atom("multiple"); K_selected = atom("selected"); K_color = atom("color");
    K_publicId = atom("publicId"); K_systemId = atom("systemId"); K_shadowrootmode = atom("shadowrootmode");
}

static bool is_html(const Node *n, const char *t) { return n && n->type == NODE_ELEMENT && n->ns == NS_HTML && n->tag == t; }
static bool is_heading(const char *t) { return t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]; }
static bool is_special(const Node *n) {
    if (n->ns == NS_HTML) return aset_has(&S_special, n->tag);
    if (n->ns == NS_MATHML) return aset_has(&S_mathml_text, n->tag) || n->tag == K_annotation_xml;
    if (n->ns == NS_SVG) return aset_has(&S_svg_ip, n->tag);
    return false;
}
static bool mathml_text_ip(const Node *n) { return n->ns == NS_MATHML && aset_has(&S_mathml_text, n->tag); }
static bool html_ip(const Node *n) {
    if (n->ns == NS_MATHML && n->tag == K_annotation_xml) {
        const char *e = node_attr(n, K_encoding);
        return e && (str_ieq(e, "text/html") || str_ieq(e, "application/xhtml+xml"));
    }
    return n->ns == NS_SVG && aset_has(&S_svg_ip, n->tag);
}
static const char *lname(const Node *n) { return n->xln ? n->xln : n->tag; }

/* the adjusting of SVG tag and attribute names, MathML and foreign attributes */
static const char *svg_tag_name(const char *t) {
    static const char *const m[] = { "altglyph","altGlyph","altglyphdef","altGlyphDef","altglyphitem","altGlyphItem","animatecolor","animateColor","animatemotion","animateMotion","animatetransform","animateTransform","clippath","clipPath","feblend","feBlend","fecolormatrix","feColorMatrix","fecomponenttransfer","feComponentTransfer","fecomposite","feComposite","feconvolvematrix","feConvolveMatrix","fediffuselighting","feDiffuseLighting","fedisplacementmap","feDisplacementMap","fedistantlight","feDistantLight","fedropshadow","feDropShadow","feflood","feFlood","fefunca","feFuncA","fefuncb","feFuncB","fefuncg","feFuncG","fefuncr","feFuncR","fegaussianblur","feGaussianBlur","feimage","feImage","femerge","feMerge","femergenode","feMergeNode","femorphology","feMorphology","feoffset","feOffset","fepointlight","fePointLight","fespecularlighting","feSpecularLighting","fespotlight","feSpotLight","fetile","feTile","feturbulence","feTurbulence","foreignobject","foreignObject","glyphref","glyphRef","lineargradient","linearGradient","radialgradient","radialGradient","textpath","textPath", NULL };
    for (int i = 0; m[i]; i += 2) if (!strcmp(t, m[i])) return atom(m[i + 1]);
    return t;
}
static const char *svg_attr_name(const char *a) {
    static const char *const m[] = { "attributename","attributeName","attributetype","attributeType","basefrequency","baseFrequency","baseprofile","baseProfile","calcmode","calcMode","clippathunits","clipPathUnits","diffuseconstant","diffuseConstant","edgemode","edgeMode","filterunits","filterUnits","glyphref","glyphRef","gradienttransform","gradientTransform","gradientunits","gradientUnits","kernelmatrix","kernelMatrix","kernelunitlength","kernelUnitLength","keypoints","keyPoints","keysplines","keySplines","keytimes","keyTimes","lengthadjust","lengthAdjust","limitingconeangle","limitingConeAngle","markerheight","markerHeight","markerunits","markerUnits","markerwidth","markerWidth","maskcontentunits","maskContentUnits","maskunits","maskUnits","numoctaves","numOctaves","pathlength","pathLength","patterncontentunits","patternContentUnits","patterntransform","patternTransform","patternunits","patternUnits","pointsatx","pointsAtX","pointsaty","pointsAtY","pointsatz","pointsAtZ","preservealpha","preserveAlpha","preserveaspectratio","preserveAspectRatio","primitiveunits","primitiveUnits","refx","refX","refy","refY","repeatcount","repeatCount","repeatdur","repeatDur","requiredextensions","requiredExtensions","requiredfeatures","requiredFeatures","specularconstant","specularConstant","specularexponent","specularExponent","spreadmethod","spreadMethod","startoffset","startOffset","stddeviation","stdDeviation","stitchtiles","stitchTiles","surfacescale","surfaceScale","systemlanguage","systemLanguage","tablevalues","tableValues","targetx","targetX","targety","targetY","textlength","textLength","viewbox","viewBox","viewtarget","viewTarget","xchannelselector","xChannelSelector","ychannelselector","yChannelSelector","zoomandpan","zoomAndPan", NULL };
    for (int i = 0; m[i]; i += 2) if (!strcmp(a, m[i])) return atom(m[i + 1]);
    return a;
}
static void foreign_attr_ns(const char *name, const char **ns, const char **pfx) {
    *ns = *pfx = NULL;
    if (!strncmp(name, "xlink:", 6)) {
        static const char *const xl[] = { "actuate", "arcrole", "href", "role", "show", "title", "type", NULL };
        for (int i = 0; xl[i]; i++) if (!strcmp(name + 6, xl[i])) { *ns = atom("http://www.w3.org/1999/xlink"); *pfx = atom("xlink"); }
    } else if (!strcmp(name, "xml:lang") || !strcmp(name, "xml:space")) { *ns = atom("http://www.w3.org/XML/1998/namespace"); *pfx = atom("xml"); }
    else if (!strcmp(name, "xmlns")) *ns = atom("http://www.w3.org/2000/xmlns/");
    else if (!strcmp(name, "xmlns:xlink")) { *ns = atom("http://www.w3.org/2000/xmlns/"); *pfx = atom("xmlns"); }
}

/* ---------------- the sanitizer configuration ---------------- */
typedef struct { bool has_attrs, has_rm; HMap attrs, rm; } SanElem;
struct SanCfg { HMap l[SAN_NLISTS]; bool has[SAN_NLISTS]; bool comments, data_attrs, js_urls; };
static const char *const NS_URI[] = { "http://www.w3.org/1999/xhtml", "http://www.w3.org/2000/svg", "http://www.w3.org/1998/Math/MathML" };
#define XLINK_NS "http://www.w3.org/1999/xlink"
static const char *san_key(char *buf, size_t cap, const char *ns, const char *name) {
    snprintf(buf, cap, "%s\x1f%s", ns ? ns : "", name ? name : "");
    return buf;
}
SanCfg *san_new(void) { return xcalloc(1, sizeof(SanCfg)); }
static void san_elem_free(void *v) { SanElem *e = v; hm_free(&e->attrs, NULL); hm_free(&e->rm, NULL); free(e); }
void san_free(SanCfg *c) {
    if (!c) return;
    for (int i = 0; i < SAN_NLISTS; i++) hm_free(&c->l[i], i == SAN_ELEMENTS ? san_elem_free : NULL);
    free(c);
}
void san_add(SanCfg *c, int list, const char *ns, const char *name) {
    c->has[list] = true;
    if (!name) return;
    char k[512]; san_key(k, sizeof k, ns, name);
    if (hm_get(&c->l[list], k)) return;
    hm_put(&c->l[list], k, list == SAN_ELEMENTS ? (void *)xcalloc(1, sizeof(SanElem)) : (void *)1);
}
void san_elem_attr(SanCfg *c, const char *el_ns, const char *el_name, bool remove, const char *ns, const char *name) {
    char k[512]; san_key(k, sizeof k, el_ns, el_name);
    SanElem *e = hm_get(&c->l[SAN_ELEMENTS], k);
    if (!e) return;
    if (remove) e->has_rm = true; else e->has_attrs = true;
    if (!name) return;
    san_key(k, sizeof k, ns, name);
    hm_put(remove ? &e->rm : &e->attrs, k, (void *)1);
}
void san_flags(SanCfg *c, bool comments, bool data_attributes, bool javascript_urls) { c->comments = comments; c->data_attrs = data_attributes; c->js_urls = javascript_urls; }
static bool san_has(const SanCfg *c, int list, const char *ns, const char *name) {
    char k[512]; return hm_get((HMap *)&c->l[list], san_key(k, sizeof k, ns, name)) != NULL;
}
enum { SA_KEEP, SA_REMOVE, SA_REPLACE };
static int san_action(const SanCfg *c, const char *ns, const char *name) {
    if (!c) return SA_KEEP;
    if (c->has[SAN_REPLACE_ELEMENTS] && san_has(c, SAN_REPLACE_ELEMENTS, ns, name)) return SA_REPLACE;
    if (c->has[SAN_ELEMENTS]) return san_has(c, SAN_ELEMENTS, ns, name) ? SA_KEEP : SA_REMOVE;
    return c->has[SAN_REMOVE_ELEMENTS] && san_has(c, SAN_REMOVE_ELEMENTS, ns, name) ? SA_REMOVE : SA_KEEP;
}
static const char *el_ns_uri(const Node *n) { return n->ns <= NS_MATHML ? NS_URI[n->ns] : n->xns; }
static int el_action(const SanCfg *c, const Node *n) { return san_action(c, el_ns_uri(n), lname(n)); }
/* the basic URL parser yields a "javascript" scheme */
static bool has_js_url(const char *v) {
    char buf[16]; int n = 0;
    const unsigned char *q = (const unsigned char *)v;
    while (*q && *q <= ' ') q++;
    for (; *q; q++) {
        if (*q == '\t' || *q == '\n' || *q == '\r') continue;
        if (*q == ':') break;
        bool ok = isalpha(*q) || (n && (isdigit(*q) || *q == '+' || *q == '-' || *q == '.'));
        if (!ok || n >= 10) return false;
        buf[n++] = (char)tolower(*q);
    }
    return *q == ':' && n == 10 && !memcmp(buf, "javascript", 10);
}
static bool san_allows_attr(const SanCfg *c, const char *ln, const char *ns, const char *value, const char *eln, const char *ens) {
    char k[512]; san_key(k, sizeof k, ns, ln);
    SanElem *e = NULL;
    if (c->has[SAN_ELEMENTS]) { char ek[512]; e = hm_get((HMap *)&c->l[SAN_ELEMENTS], san_key(ek, sizeof ek, ens, eln)); }
    if (e && e->has_rm && hm_get(&e->rm, k)) return false;
    if (c->has[SAN_ATTRIBUTES]) {
        if (!hm_get((HMap *)&c->l[SAN_ATTRIBUTES], k) && !(e && e->has_attrs && hm_get(&e->attrs, k))
            && (strncmp(ln, "data-", 5) || ns || !c->data_attrs)) return false;
    } else {
        if (e && e->has_attrs && !hm_get(&e->attrs, k)) return false;
        if (c->has[SAN_REMOVE_ATTRIBUTES] && hm_get((HMap *)&c->l[SAN_REMOVE_ATTRIBUTES], k)) return false;
    }
    if (c->js_urls) return true;
    bool html = ens && !strcmp(ens, NS_URI[NS_HTML]), svg = ens && !strcmp(ens, NS_URI[NS_SVG]), mml = ens && !strcmp(ens, NS_URI[NS_MATHML]);
    if (html && !ns && (((!strcmp(eln, "a") || !strcmp(eln, "area")) && !strcmp(ln, "href")) || (!strcmp(eln, "form") && !strcmp(ln, "action"))
                        || ((!strcmp(eln, "button") || !strcmp(eln, "input")) && !strcmp(ln, "formaction"))) && has_js_url(value)) return false;
    if (svg && !strcmp(eln, "a") && !strcmp(ln, "href") && (!ns || !strcmp(ns, XLINK_NS)) && has_js_url(value)) return false;
    if (mml && !strcmp(ln, "href") && (!ns || !strcmp(ns, XLINK_NS)) && has_js_url(value)) return false;
    if (svg && !ns && !strcmp(ln, "attributeName") && (!strcmp(eln, "animate") || !strcmp(eln, "animateTransform") || !strcmp(eln, "set"))
        && (!strcmp(value, "href") || !strcmp(value, "xlink:href"))) return false;
    return true;
}
static bool san_allows_pi(const SanCfg *c, const char *target) {
    if (c->has[SAN_PIS]) return san_has(c, SAN_PIS, NULL, target);
    return !(c->has[SAN_REMOVE_PIS] && san_has(c, SAN_REMOVE_PIS, NULL, target));
}

/* ---------------- parser objects ---------------- */
HtmlParser *html_parser_new(Document *d, Node *ctx) {
    pthread_once(&names_once, names_init);
    HtmlParser *p = xcalloc(1, sizeof *p);
    p->doc = d; p->mode = M_INITIAL; p->frameset_ok = true; p->scripting = true; p->state = S_DATA;
    if (!ctx) return p;
    /* the fragment parsing algorithm */
    p->ctx = ctx; p->quirks = d->quirks;
    if (ctx->ns == NS_HTML) {
        const char *t = ctx->tag;
        if (t == A_title || t == A_textarea) p->state = S_RCDATA;
        else if (t == A_style || t == A_xmp || t == A_iframe || t == K_noembed || t == K_noframes) p->state = S_RAWTEXT;
        else if (t == A_script) p->state = S_SCRIPT;
        else if (t == A_noscript) p->state = p->scripting ? S_RAWTEXT : S_DATA;
        else if (t == A_plaintext) p->state = S_PLAINTEXT;
    }
    p->root = node_new_element(d, A_html, NS_HTML);
    vec_push(p->open, p->root);
    if (is_html(ctx, A_template)) vec_push(p->tmpl_modes, M_IN_TEMPLATE);
    for (Node *a = ctx; a; a = a->parent) if (is_html(a, A_form)) { p->form_el = a; break; }
    return p;
}
void html_parser_free(HtmlParser *p) {
    sb_free(&p->in); sb_free(&p->tag); sb_free(&p->an); sb_free(&p->av); sb_free(&p->text); sb_free(&p->tmp);
    sb_free(&p->chars); sb_free(&p->dt_name); sb_free(&p->dt_pub); sb_free(&p->dt_sys); sb_free(&p->table_text);
    for (int i = 0; i < p->attrs.n; i++) free(p->attrs.v[i].value);
    vec_free(p->attrs); vec_free(p->open); vec_free(p->fmt); vec_free(p->tmpl_modes);
    /* a declarative shadow root template's contents are the shadow root, kept by its host */
    for (int i = 0; i < p->dsd_tmpls.n; i++) p->dsd_tmpls.v[i]->template_content = NULL;
    for (int i = 0; i < p->made.n; i++) { Node *n = p->made.v[i]; if (n->template_content && n->template_content->host) n->template_content = NULL; }
    /* the elements the sanitizer removed or replaced: free the topmost ones, once */
    int no = 0;
    for (int i = 0; i < p->made.n; i++) { Node *n = p->made.v[i]; if (!n->parent && n != p->root) p->made.v[no++] = n; }
    for (int i = 0; i < p->dsd_tmpls.n; i++) if (!p->dsd_tmpls.v[i]->parent) node_free_tree(p->dsd_tmpls.v[i]);
    for (int i = 0; i < no; i++) {
        bool dup = false;
        for (int j = 0; j < p->dsd_tmpls.n && !dup; j++) dup = p->dsd_tmpls.v[j] == p->made.v[i];
        if (!dup) node_free_tree(p->made.v[i]);
    }
    vec_free(p->redir); vec_free(p->made); vec_free(p->dsd_tmpls);
    free(p);
}

/* ---------------- the stack of open elements ---------------- */
static Node *cur(P) { return p->open.n ? p->open.v[p->open.n - 1] : NULL; }
static Node *adj_cur(P) { return p->ctx && p->open.n == 1 ? p->ctx : cur(p); }
static int stack_index(P, const Node *n) { for (int i = p->open.n - 1; i >= 0; i--) if (p->open.v[i] == n) return i; return -1; }
static void stack_remove_at(P, int i) { memmove(&p->open.v[i], &p->open.v[i + 1], sizeof(Node *) * (size_t)(p->open.n - i - 1)); p->open.n--; }
static void stack_insert_at(P, int i, Node *n) {
    vec_push(p->open, n);
    memmove(&p->open.v[i + 1], &p->open.v[i], sizeof(Node *) * (size_t)(p->open.n - 1 - i));
    p->open.v[i] = n;
}
static bool tmpl_on_stack(P) { for (int i = p->open.n - 1; i >= 0; i--) if (is_html(p->open.v[i], A_template)) return true; return false; }
/* for the form element pointer, a template context element counts as a template on the stack */
static bool in_template(P) { return tmpl_on_stack(p) || (p->ctx && is_html(p->ctx, A_template)); }

static void maybe_clone_selected(P, Node *opt);
static void pop(P) {
    if (!p->open.n) return;
    Node *n = vec_pop(p->open);
    if (p->saw_selectedcontent && is_html(n, A_option)) maybe_clone_selected(p, n);
}
static void pop_until(P, const char *t) { while (p->open.n) { Node *n = cur(p); pop(p); if (is_html(n, t)) break; } }

static bool scope_boundary(const Node *n, int kind) {
    if (kind == SC_TABLE) return n->ns == NS_HTML && (n->tag == A_html || n->tag == A_table || n->tag == A_template);
    if (n->ns == NS_HTML) {
        if (aset_has(&S_scope, n->tag)) return true;
        if (kind == SC_LIST) return n->tag == A_ol || n->tag == A_ul;
        if (kind == SC_BUTTON) return n->tag == A_button;
        return false;
    }
    if (n->ns == NS_MATHML) return aset_has(&S_mathml_text, n->tag) || n->tag == K_annotation_xml;
    if (n->ns == NS_SVG) return aset_has(&S_svg_ip, n->tag);
    return false;
}
static bool in_scope(P, const char *t, int kind) {
    for (int i = p->open.n - 1; i >= 0; i--) { Node *n = p->open.v[i]; if (is_html(n, t)) return true; if (scope_boundary(n, kind)) return false; }
    return false;
}
static bool node_in_scope(P, const Node *x) {
    for (int i = p->open.n - 1; i >= 0; i--) { Node *n = p->open.v[i]; if (n == x) return true; if (scope_boundary(n, SC_DEFAULT)) return false; }
    return false;
}
static bool heading_in_scope(P) {
    for (int i = p->open.n - 1; i >= 0; i--) { Node *n = p->open.v[i]; if (n->ns == NS_HTML && is_heading(n->tag)) return true; if (scope_boundary(n, SC_DEFAULT)) return false; }
    return false;
}
static void gen_implied(P, const char *except) {
    for (Node *c; (c = cur(p)) && c->ns == NS_HTML && aset_has(&S_implied, c->tag) && c->tag != except;) pop(p);
}
static void gen_implied_all(P) { for (Node *c; (c = cur(p)) && c->ns == NS_HTML && aset_has(&S_implied_all, c->tag);) pop(p); }
static void close_p(P) { gen_implied(p, A_p); pop_until(p, A_p); }
static void close_p_in_button_scope(P) { if (in_scope(p, A_p, SC_BUTTON)) close_p(p); }

/* ---------------- inserting nodes ---------------- */
typedef struct { Node *parent, *before; } Loc;
static Loc insertion_loc(P, Node *override) {
    Node *target = override ? override : cur(p);
    Loc l = { target, NULL };
    if (p->foster && target->ns == NS_HTML && (target->tag == A_table || target->tag == A_tbody || target->tag == A_tfoot || target->tag == A_thead || target->tag == A_tr)) {
        int lt = -1, ltab = -1;
        for (int i = p->open.n - 1; i >= 0 && (lt < 0 || ltab < 0); i--) {
            if (lt < 0 && is_html(p->open.v[i], A_template)) lt = i;
            if (ltab < 0 && is_html(p->open.v[i], A_table)) ltab = i;
        }
        if (lt >= 0 && (ltab < 0 || lt > ltab)) l.parent = p->open.v[lt];
        else if (ltab < 0) l.parent = p->open.v[0];
        else if (p->open.v[ltab]->parent) { l.parent = p->open.v[ltab]->parent; l.before = p->open.v[ltab]; }
        else l.parent = p->open.v[ltab - 1];
    }
    if (is_html(l.parent, A_template) && l.parent->template_content) { l.parent = l.parent->template_content; l.before = NULL; }
    return l;
}
static struct Redir *redir_get(P, const Node *el) {
    for (int i = p->redir.n - 1; i >= 0; i--) if (p->redir.v[i].el == el) return &p->redir.v[i];
    return NULL;
}
static void redir_set(P, Node *el, Loc l) {
    struct Redir *r = redir_get(p, el);
    if (r) { r->parent = l.parent; r->before = l.before; return; }
    struct Redir nr = { el, l.parent, l.before };
    vec_push(p->redir, nr);
}
/* the adjusted insertion location */
static Loc adjust_loc(P, Loc l) {
    if (!p->redir.n) return l;
    struct Redir *r = redir_get(p, l.parent);
    if (r) { l.parent = r->parent; l.before = r->before; }
    return l;
}
static Loc adj_loc(P, Node *override) { return adjust_loc(p, insertion_loc(p, override)); }
static bool san_skip_el(P, Node *n, Loc l, bool done) {
    if (!p->san || done || n->type != NODE_ELEMENT) return false;
    int a = el_action(p->san, n);
    if (a == SA_REPLACE) { redir_set(p, n, l); return true; }
    return a == SA_REMOVE;
}
static void insert_at(Loc l, Node *n) {
    if (l.parent->type == NODE_DOCUMENT && n->type == NODE_ELEMENT)
        for (Node *k = l.parent->first; k; k = k->next) if (k->type == NODE_ELEMENT) { if (!(n->flags & NF_SAN_MADE)) node_free_tree(n); return; }
    for (Node *a = l.parent; a; a = a->parent ? a->parent : a->host) if (a == n) return;
    node_insert_before(l.parent, n, l.before);
}
/* insert an element at the adjusted insertion location */
static void insert_el_at(P, Loc l, Node *n, bool done) {
    l = adjust_loc(p, l);
    if (n->parent) return;
    if (san_skip_el(p, n, l, done)) return;
    insert_at(l, n);
}
static void insert_chars(P, const char *s, size_t n) {
    if (!n) return;
    Loc l = adj_loc(p, NULL);
    if (l.parent->type == NODE_DOCUMENT) return;
    Node *prev = l.before ? l.before->prev : l.parent->last;
    if (prev && prev->type == NODE_TEXT) node_text_append(prev, s, n);
    else node_insert_before(l.parent, node_new_text(p->doc, s, n), l.before);
}
static Node *new_comment(P, Tok *t) {
    return t->type == T_PI ? node_new_pi(p->doc, t->target, t->data, t->len) : node_new_comment(p->doc, t->data, t->len);
}
static bool san_drops(P, Tok *t) {
    if (!p->san) return false;
    return t->type == T_PI ? !san_allows_pi(p->san, t->target) : !p->san->comments;
}
static void insert_comment(P, Tok *t) { if (!san_drops(p, t)) insert_at(adj_loc(p, NULL), new_comment(p, t)); }
static void append_comment_to(P, Tok *t, Node *parent) { if (!san_drops(p, t)) node_append(parent, new_comment(p, t)); }

static Node *create_el(P, Tok *t, int ns, const char *name) {
    Node *e = node_new_element(p->doc, name, ns);
    for (int i = 0; t && i < t->nattrs; i++) {
        const char *an = t->attrs[i].name, *ans = NULL, *apfx = NULL;
        if (ns == NS_SVG) an = svg_attr_name(an);
        else if (ns == NS_MATHML && !strcmp(an, "definitionurl")) an = atom("definitionURL");
        if (ns != NS_HTML) foreign_attr_ns(an, &ans, &apfx);
        if (node_attr_qindex(e, an) >= 0) continue;
        if (p->san) {
            const char *ln = apfx ? strchr(an, ':') + 1 : an;
            if (!san_allows_attr(p->san, ln, ans, t->attrs[i].value, lname(e), el_ns_uri(e))) continue;
        }
        node_set_attr_at(e, -1, an, t->attrs[i].value, ans, apfx);
    }
    e->flags |= NF_PARSER_INSERTED;
    if (ns == NS_HTML && name == A_script) e->flags |= NF_SCRIPT_NO_FORCE_ASYNC;
    if (p->san) { e->flags |= NF_SAN_MADE; vec_push(p->made, e); }
    if (ns == NS_HTML && name == A_template) {
        e->template_content = node_new_fragment(p->doc); e->template_content->refcount = 1; e->template_content->flags |= NF_INERT;
    }
    if (ns == NS_HTML && name == K_selectedcontent) p->saw_selectedcontent = true;
    return e;
}
static Node *insert_el(P, Tok *t, int ns, const char *name) {
    Loc l = insertion_loc(p, NULL);
    Node *e = create_el(p, t, ns, name);
    insert_el_at(p, l, e, false);
    vec_push(p->open, e);
    return e;
}
static Node *insert_html(P, Tok *t) { return insert_el(p, t, NS_HTML, t->name); }
static Node *insert_fake(P, const char *name) { return insert_el(p, NULL, NS_HTML, name); }
static Node *clone_el(P, Node *n) {
    Node *e = node_new_element(p->doc, n->tag, n->ns);
    for (int i = 0; i < n->nattrs; i++) node_set_attr_at(e, -1, n->attrs[i].name, n->attrs[i].value, n->attrs[i].ns, n->attrs[i].pfx);
    e->flags |= NF_PARSER_INSERTED;
    if (p->san) { e->flags |= NF_SAN_MADE; vec_push(p->made, e); }
    return e;
}
static void add_missing_attrs(P, Node *e, Tok *t) {
    for (int i = 0; i < t->nattrs; i++) {
        if (node_attr_qindex(e, t->attrs[i].name) >= 0) continue;
        if (p->san && !san_allows_attr(p->san, t->attrs[i].name, NULL, t->attrs[i].value, e->tag, NS_URI[NS_HTML])) continue;
        node_set_attr_at(e, -1, t->attrs[i].name, t->attrs[i].value, NULL, NULL);
    }
}
static const char *tok_attr(Tok *t, const char *name) { for (int i = 0; i < t->nattrs; i++) if (!strcmp(t->attrs[i].name, name)) return t->attrs[i].value; return NULL; }

/* "maybe clone an option into selectedcontent": a parsed option that is its select's selected option
   gives the select's selectedcontent element a copy of its content */
static void maybe_clone_selected(P, Node *opt) {
    Node *sel = NULL;
    for (Node *a = opt->parent; a && a->type == NODE_ELEMENT; a = a->parent) {
        if (is_html(a, A_select)) { sel = a; break; }
        if (is_html(a, K_datalist) || is_html(a, A_hr) || is_html(a, A_option)) return;
    }
    if (!sel || node_has_attr(sel, K_multiple)) return;
    Node *sc = NULL, *first = NULL; bool any_selected = false;
    for (Node *x = sel->first; x; x = node_next_in_tree(x, sel)) {
        if (!sc && is_html(x, K_selectedcontent)) sc = x;
        if (is_html(x, A_option)) { if (!first) first = x; if (node_has_attr(x, K_selected)) any_selected = true; }
    }
    if (!sc || !(node_has_attr(opt, K_selected) || (!any_selected && first == opt))) return;
    while (sc->first) { Node *k = sc->first; node_remove(k); node_free_tree(k); }
    for (Node *k = opt->first; k; k = k->next) node_append(sc, node_clone(k, true, p->doc));
}

/* ---------------- the list of active formatting elements ---------------- */
static int fmt_index(P, const Node *n) { for (int i = p->fmt.n - 1; i >= 0; i--) if (p->fmt.v[i] == n) return i; return -1; }
static void fmt_remove_at(P, int i) { memmove(&p->fmt.v[i], &p->fmt.v[i + 1], sizeof(Node *) * (size_t)(p->fmt.n - i - 1)); p->fmt.n--; }
static void fmt_insert_at(P, int i, Node *n) {
    vec_push(p->fmt, n);
    memmove(&p->fmt.v[i + 1], &p->fmt.v[i], sizeof(Node *) * (size_t)(p->fmt.n - 1 - i));
    p->fmt.v[i] = n;
}
static void clear_to_marker(P) { while (p->fmt.n) if (!vec_pop(p->fmt)) break; }
static bool same_attrs(const Node *a, const Node *b) {
    if (a->nattrs != b->nattrs) return false;
    for (int i = 0; i < a->nattrs; i++) {
        int j = node_attr_qindex(b, a->attrs[i].name);
        if (j < 0 || b->attrs[j].ns != a->attrs[i].ns || strcmp(a->attrs[i].value, b->attrs[j].value)) return false;
    }
    return true;
}
static void push_fmt(P, Node *e) {
    int count = 0, earliest = -1;
    for (int i = p->fmt.n - 1; i >= 0 && p->fmt.v[i]; i--) {
        Node *x = p->fmt.v[i];
        if (x->tag == e->tag && x->ns == e->ns && same_attrs(x, e)) { count++; earliest = i; }
    }
    if (count >= 3) fmt_remove_at(p, earliest);
    vec_push(p->fmt, e);
}
static void reconstruct(P) {
    if (!p->fmt.n) return;
    Node *last = p->fmt.v[p->fmt.n - 1];
    if (!last || stack_index(p, last) >= 0) return;
    int i = p->fmt.n - 1;
    while (i > 0) { Node *x = p->fmt.v[i - 1]; if (!x || stack_index(p, x) >= 0) break; i--; }
    for (; i < p->fmt.n; i++) {
        Node *e = clone_el(p, p->fmt.v[i]);
        insert_el_at(p, insertion_loc(p, NULL), e, false);
        vec_push(p->open, e);
        p->fmt.v[i] = e;
    }
}

/* the adoption agency algorithm; false: act as for any other end tag */
static bool adoption(P, const char *subject) {
    Node *c = cur(p);
    if (is_html(c, subject) && fmt_index(p, c) < 0) { pop(p); return true; }
    for (int outer = 0; outer < 8; outer++) {
        int fi = -1;
        for (int i = p->fmt.n - 1; i >= 0 && p->fmt.v[i]; i--) if (is_html(p->fmt.v[i], subject)) { fi = i; break; }
        if (fi < 0) return false;
        Node *fe = p->fmt.v[fi];
        int si = stack_index(p, fe);
        if (si < 0) { fmt_remove_at(p, fi); return true; }
        if (!node_in_scope(p, fe)) return true;
        int fb = -1;
        for (int i = si + 1; i < p->open.n; i++) if (is_special(p->open.v[i])) { fb = i; break; }
        if (fb < 0) { while (p->open.n > si) pop(p); fmt_remove_at(p, fi); return true; }
        Node *furthest = p->open.v[fb], *common = p->open.v[si - 1], *last = furthest;
        int bookmark = fi, ni = fb;
        bool moved = false;
        VEC(Node *) pending = { 0 };
        if (p->san) {
            int a = el_action(p->san, last);
            if (a != SA_KEEP) last = NULL;
            if (a == SA_REPLACE) vec_push(pending, furthest);
        }
        for (int inner = 1;; inner++) {
            Node *node = p->open.v[--ni];
            if (node == fe) break;
            int nfi = fmt_index(p, node);
            if (inner > 3 && nfi >= 0) { fmt_remove_at(p, nfi); if (nfi < bookmark) bookmark--; nfi = -1; }
            if (nfi < 0) { stack_remove_at(p, ni); continue; }
            Node *ne = clone_el(p, node);
            p->fmt.v[nfi] = ne; p->open.v[ni] = ne; node = ne;
            if (!moved) { bookmark = nfi + 1; moved = true; }
            int a = p->san ? el_action(p->san, node) : SA_KEEP;
            if (a != SA_REPLACE) {
                for (int k = 0; k < pending.n; k++) redir_set(p, pending.v[k], (Loc){ node, NULL });
                pending.n = 0;
            }
            if (a == SA_KEEP) { if (last) node_append(node, last); last = node; }
            else if (a == SA_REMOVE) { if (last && last->parent) node_remove(last); last = NULL; }
            else vec_push(pending, node);
        }
        Loc cl = adj_loc(p, common);
        for (int k = 0; k < pending.n; k++) redir_set(p, pending.v[k], cl);
        vec_free(pending);
        if (last && last->parent) node_remove(last);
        if (last && !last->parent && (!cl.before || cl.before->parent == cl.parent)) insert_at(cl, last);
        Node *ne = clone_el(p, fe);
        int na = p->san ? el_action(p->san, ne) : SA_KEEP;
        if (na == SA_REPLACE) redir_set(p, ne, adjust_loc(p, (Loc){ furthest, NULL }));
        else {
            while (furthest->first) node_append(ne, furthest->first);
            if (na == SA_KEEP) insert_at(adjust_loc(p, (Loc){ furthest, NULL }), ne);
        }
        int ofi = fmt_index(p, fe);
        fmt_remove_at(p, ofi); if (ofi < bookmark) bookmark--;
        if (bookmark > p->fmt.n) bookmark = p->fmt.n;
        fmt_insert_at(p, bookmark, ne);
        stack_remove_at(p, stack_index(p, fe));
        int nsi = stack_index(p, furthest) + 1;
        stack_insert_at(p, nsi, ne);
        if (p->redir.n)
            for (int k = nsi + 1; k < p->open.n && redir_get(p, p->open.v[k]); k++) redir_set(p, p->open.v[k], adjust_loc(p, (Loc){ ne, NULL }));
    }
    return true;
}
static void any_other_end(P, const char *t) {
    for (int i = p->open.n - 1; i >= 0; i--) {
        Node *n = p->open.v[i];
        if (is_html(n, t)) { gen_implied(p, t); while (p->open.n > i) pop(p); return; }
        if (is_special(n)) return;
    }
}

static void reset_mode(P) {
    for (int i = p->open.n - 1; i >= 0; i--) {
        Node *n = p->open.v[i]; bool last = i == 0;
        if (last && p->ctx) n = p->ctx;
        if (n->ns == NS_HTML) {
            const char *t = n->tag;
            if ((t == A_td || t == A_th) && !last) { p->mode = M_IN_CELL; return; }
            if (t == A_tr) { p->mode = M_IN_ROW; return; }
            if (t == A_tbody || t == A_thead || t == A_tfoot) { p->mode = M_IN_TABLE_BODY; return; }
            if (t == A_caption) { p->mode = M_IN_CAPTION; return; }
            if (t == A_colgroup) { p->mode = M_IN_COLUMN_GROUP; return; }
            if (t == A_table) { p->mode = M_IN_TABLE; return; }
            if (t == A_template) { p->mode = p->tmpl_modes.n ? vec_last(p->tmpl_modes) : M_IN_BODY; return; }
            if (t == A_head && !last) { p->mode = M_IN_HEAD; return; }
            if (t == A_body) { p->mode = M_IN_BODY; return; }
            if (t == A_frameset) { p->mode = M_IN_FRAMESET; return; }
            if (t == A_html) { p->mode = p->head_el ? M_AFTER_HEAD : M_BEFORE_HEAD; return; }
        }
        if (last) { p->mode = M_IN_BODY; return; }
    }
    p->mode = M_IN_BODY;
}

/* ---------------- tree construction ---------------- */
static void process(P, Tok *t, int mode);
static void reprocess(P, Tok *t) { process(p, t, p->mode); }
static void stop_parsing(P) { while (p->open.n) pop(p); p->stopped = true; }
static void generic_text(P, Tok *t, int state) { insert_html(p, t); p->state = state; p->orig_mode = p->mode; p->mode = M_TEXT; }

static void doctype_quirks(P) {
    static const char *const pub_prefix[] = { "+//silmaril//dtd html pro v0r11 19970101//", "-//as//dtd html 3.0 aswedit + extensions//", "-//advasoft ltd//dtd html 3.0 aswedit + extensions//", "-//ietf//dtd html 2.0 level 1//", "-//ietf//dtd html 2.0 level 2//", "-//ietf//dtd html 2.0 strict level 1//", "-//ietf//dtd html 2.0 strict level 2//", "-//ietf//dtd html 2.0 strict//", "-//ietf//dtd html 2.0//", "-//ietf//dtd html 2.1e//", "-//ietf//dtd html 3.0//", "-//ietf//dtd html 3.2 final//", "-//ietf//dtd html 3.2//", "-//ietf//dtd html 3//", "-//ietf//dtd html level 0//", "-//ietf//dtd html level 1//", "-//ietf//dtd html level 2//", "-//ietf//dtd html level 3//", "-//ietf//dtd html strict level 0//", "-//ietf//dtd html strict level 1//", "-//ietf//dtd html strict level 2//", "-//ietf//dtd html strict level 3//", "-//ietf//dtd html strict//", "-//ietf//dtd html//", "-//metrius//dtd metrius presentational//", "-//microsoft//dtd internet explorer 2.0 html strict//", "-//microsoft//dtd internet explorer 2.0 html//", "-//microsoft//dtd internet explorer 2.0 tables//", "-//microsoft//dtd internet explorer 3.0 html strict//", "-//microsoft//dtd internet explorer 3.0 html//", "-//microsoft//dtd internet explorer 3.0 tables//", "-//netscape comm. corp.//dtd html//", "-//netscape comm. corp.//dtd strict html//", "-//o'reilly and associates//dtd html 2.0//", "-//o'reilly and associates//dtd html extended 1.0//", "-//o'reilly and associates//dtd html extended relaxed 1.0//", "-//sq//dtd html 2.0 hotmetal + extensions//", "-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//", "-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//", "-//spyglass//dtd html 2.0 extended//", "-//sun microsystems corp.//dtd hotjava html//", "-//sun microsystems corp.//dtd hotjava strict html//", "-//w3c//dtd html 3 1995-03-24//", "-//w3c//dtd html 3.2 draft//", "-//w3c//dtd html 3.2 final//", "-//w3c//dtd html 3.2//", "-//w3c//dtd html 3.2s draft//", "-//w3c//dtd html 4.0 frameset//", "-//w3c//dtd html 4.0 transitional//", "-//w3c//dtd html experimental 19960712//", "-//w3c//dtd html experimental 970421//", "-//w3c//dtd w3 html//", "-//w3o//dtd w3 html 3.0//", "-//webtechs//dtd mozilla html 2.0//", "-//webtechs//dtd mozilla html//", NULL };
    const char *name = p->dt_has_name && p->dt_name.s ? p->dt_name.s : "";
    const char *pub = p->dt_has_pub ? (p->dt_pub.s ? p->dt_pub.s : "") : NULL, *sys = p->dt_has_sys ? (p->dt_sys.s ? p->dt_sys.s : "") : NULL;
    bool q = p->dt_force_quirks || strcmp(name, "html");
    if (!q && pub) {
        if (str_ieq(pub, "-//w3o//dtd w3 html strict 3.0//en//") || str_ieq(pub, "-/w3c/dtd html 4.0 transitional/en") || str_ieq(pub, "html")) q = true;
        for (int i = 0; !q && pub_prefix[i]; i++) if (str_istarts(pub, pub_prefix[i])) q = true;
        if (!sys && (str_istarts(pub, "-//w3c//dtd html 4.01 frameset//") || str_istarts(pub, "-//w3c//dtd html 4.01 transitional//"))) q = true;
    }
    if (!q && sys && str_ieq(sys, "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd")) q = true;
    p->quirks = q;
}

static void m_initial(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) return; break;
    case T_COMMENT: case T_PI: append_comment_to(p, t, &p->doc->node); return;
    case T_DOCTYPE: {
        Node *dt = node_new_doctype(p->doc, p->dt_has_name && p->dt_name.s ? p->dt_name.s : "");
        if (p->dt_has_pub) node_set_attr(dt, K_publicId, p->dt_pub.s ? p->dt_pub.s : "");
        if (p->dt_has_sys) node_set_attr(dt, K_systemId, p->dt_sys.s ? p->dt_sys.s : "");
        node_append(&p->doc->node, dt);
        doctype_quirks(p);
        p->mode = M_BEFORE_HTML;
        return;
    }
    }
    p->quirks = true; p->mode = M_BEFORE_HTML; reprocess(p, t);
}
static void m_before_html(P, Tok *t) {
    switch (t->type) {
    case T_DOCTYPE: return;
    case T_COMMENT: case T_PI: append_comment_to(p, t, &p->doc->node); return;
    case T_CHARS: if (t->cls == C_WS) return; break;
    case T_START:
        if (t->name == A_html) { Node *e = create_el(p, t, NS_HTML, A_html); node_append(&p->doc->node, e); vec_push(p->open, e); p->mode = M_BEFORE_HEAD; return; }
        break;
    case T_END: if (t->name != A_head && t->name != A_body && t->name != A_html && t->name != A_br) return; break;
    }
    Node *e = create_el(p, NULL, NS_HTML, A_html); node_append(&p->doc->node, e); vec_push(p->open, e);
    p->mode = M_BEFORE_HEAD; reprocess(p, t);
}
static void m_before_head(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) return; break;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_START:
        if (t->name == A_html) { process(p, t, M_IN_BODY); return; }
        if (t->name == A_head) { p->head_el = insert_html(p, t); p->mode = M_IN_HEAD; return; }
        break;
    case T_END: if (t->name != A_head && t->name != A_body && t->name != A_html && t->name != A_br) return; break;
    }
    p->head_el = insert_fake(p, A_head); p->mode = M_IN_HEAD; reprocess(p, t);
}
/* the name="..." (or name=...) value in a processing instruction's data, into out */
static bool pi_name(const char *d, char *out, size_t cap) {
    const char *q = d ? strstr(d, "name=") : NULL;
    if (!q || (q > d && !isspace((unsigned char)q[-1]))) return false;
    q += 5;
    char qu = *q == '"' || *q == '\'' ? *q++ : 0;
    size_t k = 0;
    while (*q && (qu ? *q != qu : !isspace((unsigned char)*q)) && k + 1 < cap) out[k++] = *q++;
    out[k] = 0;
    return true;
}
/* declarative partial updates: a parsed <template for=NAME> patches the first <?marker name=NAME?> (inserting its
   content there) or <?start name=NAME?> (replacing everything up to the <?end?> closing it, nested starts counted,
   or to the end of the parent); the markers and the template go away. An empty name or no marker: nothing happens. */
static void template_for(P, Node *t) {
    const char *nm = node_attr(t, "for");
    if (!nm || !*nm || !t->template_content) return;
    Node *root = t; while (root->parent) root = root->parent;
    Node *mk = NULL; char nb[256];
    for (Node *n = root->first; n && !mk; n = node_next_in_tree(n, root))
        if (n->type == NODE_PI && (n->tag == atom("start") || n->tag == atom("marker")) && pi_name(n->text, nb, sizeof nb) && !strcmp(nb, nm)) mk = n;
    if (!mk || !mk->parent) return;
    Node *par = mk->parent;
    if (mk->tag == atom("start")) {
        int depth = 0;
        for (Node *e = mk->next; e; ) {
            Node *nx = e->next;
            if (e == t) break;
            bool stop = false;
            if (e->type == NODE_PI && e->tag == atom("start")) depth++;
            else if (e->type == NODE_PI && e->tag == atom("end")) { if (!depth) stop = true; else depth--; }
            node_remove(e); node_free_tree(e);
            if (stop) break;
            e = nx;
        }
    }
    Node *ref = mk->next;
    for (Node *c = t->template_content->first; c; c = t->template_content->first) { node_remove(c); node_insert_before(par, c, ref); }
    node_remove(mk); node_free_tree(mk);
    if (t->parent) node_remove(t);
}
static void end_template(P) {
    if (!tmpl_on_stack(p)) return;
    Node *t = NULL; for (int i = p->open.n - 1; i >= 0; i--) if (p->open.v[i]->tag == A_template) { t = p->open.v[i]; break; }
    gen_implied_all(p); pop_until(p, A_template); clear_to_marker(p);
    if (p->tmpl_modes.n) p->tmpl_modes.n--;
    reset_mode(p);
    if (t) template_for(p, t);
}
/* a template start tag: a declarative shadow root, or a template element */
static void template_start(P, Tok *t) {
    vec_push(p->fmt, (Node *)NULL); p->frameset_ok = false;
    p->mode = M_IN_TEMPLATE; vec_push(p->tmpl_modes, M_IN_TEMPLATE);
    Node *host = cur(p);
    bool host_root = host == p->open.v[0];
    Node *tp = create_el(p, t, NS_HTML, A_template);
    Loc il = insertion_loc(p, NULL);
    vec_push(p->open, tp);
    if (p->san && el_action(p->san, tp) != SA_KEEP) return;
    const char *mode = node_attr(tp, K_shadowrootmode);
    bool closed = mode && str_ieq(mode, "closed");
    if (!mode || (!closed && !str_ieq(mode, "open"))) { insert_el_at(p, il, tp, true); return; }
    if (!p->dsd || host_root || redir_get(p, host)) { insert_el_at(p, il, tp, true); return; }
    const char *slot = node_attr(tp, "shadowrootslotassignment");
    uint32_t f = NF_SR_DECLARATIVE | (closed ? NF_SR_CLOSED : 0) | (node_attr(tp, "shadowrootdelegatesfocus") ? NF_SR_DELEGATES : 0)
        | (node_attr(tp, "shadowrootserializable") ? NF_SR_SERIALIZABLE : 0) | (node_attr(tp, "shadowrootclonable") ? NF_SR_CLONABLE : 0)
        | (slot && str_ieq(slot, "manual") ? NF_SR_MANUAL : 0);
    Node *sr = node_attach_shadow(host, f);
    if (!sr) { insert_el_at(p, il, tp, true); return; }
    const char *rt = node_attr(tp, "shadowrootreferencetarget");
    if (rt) { free(sr->text); sr->text = xstrdup(rt); sr->text_len = strlen(rt); }
    Node *old = tp->template_content;
    old->refcount = 0; node_free_tree(old);
    tp->template_content = sr;
    vec_push(p->dsd_tmpls, tp);
}
static void m_in_head(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) { insert_chars(p, t->data, t->len); return; } break;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_START: {
        const char *n = t->name;
        if (n == A_html) { process(p, t, M_IN_BODY); return; }
        if (n == A_base || n == K_basefont || n == K_bgsound || n == A_link || n == A_meta) { insert_html(p, t); pop(p); return; }
        if (n == A_title) { generic_text(p, t, S_RCDATA); return; }
        if ((n == A_noscript && p->scripting) || n == K_noframes || n == A_style) { generic_text(p, t, S_RAWTEXT); return; }
        if (n == A_noscript) { insert_html(p, t); p->mode = M_IN_HEAD_NOSCRIPT; return; }
        if (n == A_script) { generic_text(p, t, S_SCRIPT); return; }
        if (n == A_template) { template_start(p, t); return; }
        if (n == A_head) return;
        break;
    }
    case T_END: {
        const char *n = t->name;
        if (n == A_head) { pop(p); p->mode = M_AFTER_HEAD; return; }
        if (n == A_template) { end_template(p); return; }
        if (n != A_body && n != A_html && n != A_br) return;
        break;
    }
    }
    pop(p); p->mode = M_AFTER_HEAD; reprocess(p, t);
}
static void m_in_head_noscript(P, Tok *t) {
    switch (t->type) {
    case T_DOCTYPE: return;
    case T_CHARS: if (t->cls == C_WS) { process(p, t, M_IN_HEAD); return; } break;
    case T_COMMENT: case T_PI: process(p, t, M_IN_HEAD); return;
    case T_START: {
        const char *n = t->name;
        if (n == A_html) { process(p, t, M_IN_BODY); return; }
        if (n == K_basefont || n == K_bgsound || n == A_link || n == A_meta || n == K_noframes || n == A_style) { process(p, t, M_IN_HEAD); return; }
        if (n == A_head || n == A_noscript) return;
        break;
    }
    case T_END:
        if (t->name == A_noscript) { pop(p); p->mode = M_IN_HEAD; return; }
        if (t->name != A_br) return;
        break;
    }
    pop(p); p->mode = M_IN_HEAD; reprocess(p, t);
}
static void m_after_head(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) { insert_chars(p, t->data, t->len); return; } break;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_START: {
        const char *n = t->name;
        if (n == A_html) { process(p, t, M_IN_BODY); return; }
        if (n == A_body) { insert_html(p, t); p->frameset_ok = false; p->mode = M_IN_BODY; return; }
        if (n == A_frameset) { insert_html(p, t); p->mode = M_IN_FRAMESET; return; }
        if (aset_has(&S_head_start, n)) {
            Node *h = p->head_el;
            vec_push(p->open, h);
            process(p, t, M_IN_HEAD);
            int i = stack_index(p, h); if (i >= 0) stack_remove_at(p, i);
            return;
        }
        if (n == A_head) return;
        break;
    }
    case T_END:
        if (t->name == A_template) { process(p, t, M_IN_HEAD); return; }
        if (t->name != A_body && t->name != A_html && t->name != A_br) return;
        break;
    }
    insert_fake(p, A_body); p->frameset_ok = true; p->mode = M_IN_BODY; reprocess(p, t);
}

static void body_start(P, Tok *t) {
    const char *n = t->name;
    if (n == A_html) { if (!tmpl_on_stack(p)) add_missing_attrs(p, p->open.v[0], t); return; }
    if (aset_has(&S_head_start, n)) { process(p, t, M_IN_HEAD); return; }
    if (n == A_body) {
        if (p->open.n < 2 || !is_html(p->open.v[1], A_body) || tmpl_on_stack(p)) return;
        p->frameset_ok = false; add_missing_attrs(p, p->open.v[1], t); return;
    }
    if (n == A_frameset) {
        if (p->open.n < 2 || !is_html(p->open.v[1], A_body) || !p->frameset_ok) return;
        Node *b = p->open.v[1];
        if (b->parent) node_remove(b);
        while (p->open.n > 1) pop(p);
        insert_html(p, t); p->mode = M_IN_FRAMESET;
        return;
    }
    if (aset_has(&S_block, n)) { close_p_in_button_scope(p); insert_html(p, t); return; }
    if (is_heading(n)) {
        close_p_in_button_scope(p);
        Node *c = cur(p); if (c->ns == NS_HTML && is_heading(c->tag)) pop(p);
        insert_html(p, t); return;
    }
    if (n == A_pre || n == A_listing) { close_p_in_button_scope(p); insert_html(p, t); p->skip_lf = true; p->frameset_ok = false; return; }
    if (n == A_form) {
        bool tm = in_template(p);
        if (p->form_el && !tm) return;
        close_p_in_button_scope(p);
        Node *e = insert_html(p, t); if (!tm) p->form_el = e;
        return;
    }
    if (n == A_li || n == A_dd || n == A_dt) {
        p->frameset_ok = false;
        for (int i = p->open.n - 1; i >= 0; i--) {
            Node *node = p->open.v[i];
            const char *m = node->ns == NS_HTML ? node->tag : NULL;
            if (n == A_li ? m == A_li : (m == A_dd || m == A_dt)) { gen_implied(p, m); pop_until(p, m); break; }
            if (is_special(node) && m != A_address && m != A_div && m != A_p) break;
        }
        close_p_in_button_scope(p); insert_html(p, t); return;
    }
    if (n == A_plaintext) { close_p_in_button_scope(p); insert_html(p, t); p->state = S_PLAINTEXT; return; }
    if (n == A_button) {
        if (in_scope(p, A_button, SC_DEFAULT)) { gen_implied(p, NULL); pop_until(p, A_button); }
        reconstruct(p); insert_html(p, t); p->frameset_ok = false; return;
    }
    if (n == A_a) {
        for (int i = p->fmt.n - 1; i >= 0 && p->fmt.v[i]; i--) if (is_html(p->fmt.v[i], A_a)) {
            Node *a = p->fmt.v[i];
            if (!adoption(p, A_a)) any_other_end(p, A_a);
            int k = fmt_index(p, a); if (k >= 0) fmt_remove_at(p, k);
            k = stack_index(p, a); if (k >= 0) stack_remove_at(p, k);
            break;
        }
        reconstruct(p); push_fmt(p, insert_html(p, t)); return;
    }
    if (aset_has(&S_fmt, n)) { reconstruct(p); push_fmt(p, insert_html(p, t)); return; }
    if (n == A_nobr) {
        reconstruct(p);
        if (in_scope(p, A_nobr, SC_DEFAULT)) { if (!adoption(p, A_nobr)) any_other_end(p, A_nobr); reconstruct(p); }
        push_fmt(p, insert_html(p, t)); return;
    }
    if (n == K_applet || n == K_marquee || n == A_object) { reconstruct(p); insert_html(p, t); vec_push(p->fmt, (Node *)NULL); p->frameset_ok = false; return; }
    if (n == A_table) {
        if (!p->quirks) close_p_in_button_scope(p);
        insert_html(p, t); p->frameset_ok = false; p->mode = M_IN_TABLE; return;
    }
    if (n == A_area || n == A_br || n == A_embed || n == A_img || n == A_keygen || n == A_wbr) {
        reconstruct(p); insert_html(p, t); pop(p); p->frameset_ok = false; return;
    }
    if (n == A_input) {
        if (p->ctx && is_html(p->ctx, A_select)) return;
        if (in_scope(p, A_select, SC_DEFAULT)) pop_until(p, A_select);
        reconstruct(p); insert_html(p, t); pop(p);
        const char *ty = tok_attr(t, "type"); if (!ty || !str_ieq(ty, "hidden")) p->frameset_ok = false;
        return;
    }
    if (n == A_param || n == A_source || n == A_track) { insert_html(p, t); pop(p); return; }
    if (n == A_hr) {
        close_p_in_button_scope(p);
        if (in_scope(p, A_select, SC_DEFAULT)) gen_implied(p, NULL);
        insert_html(p, t); pop(p); p->frameset_ok = false; return;
    }
    if (n == K_image) { Tok t2 = *t; t2.name = A_img; reprocess(p, &t2); return; }
    if (n == A_textarea) { insert_html(p, t); p->skip_lf = true; p->state = S_RCDATA; p->orig_mode = p->mode; p->frameset_ok = false; p->mode = M_TEXT; return; }
    if (n == A_xmp) { close_p_in_button_scope(p); reconstruct(p); p->frameset_ok = false; generic_text(p, t, S_RAWTEXT); return; }
    if (n == A_iframe) { p->frameset_ok = false; generic_text(p, t, S_RAWTEXT); return; }
    if (n == K_noembed || (n == A_noscript && p->scripting)) { generic_text(p, t, S_RAWTEXT); return; }
    if (n == A_select) {
        if (p->ctx && is_html(p->ctx, A_select)) return;
        if (in_scope(p, A_select, SC_DEFAULT)) { pop_until(p, A_select); return; }
        reconstruct(p); insert_html(p, t); p->frameset_ok = false; return;
    }
    if (n == A_option) {
        if (in_scope(p, A_select, SC_DEFAULT)) gen_implied(p, A_optgroup);
        else if (is_html(cur(p), A_option)) pop(p);
        reconstruct(p); insert_html(p, t); return;
    }
    if (n == A_optgroup) {
        if (in_scope(p, A_select, SC_DEFAULT)) gen_implied(p, NULL);
        else if (is_html(cur(p), A_option)) pop(p);
        reconstruct(p); insert_html(p, t); return;
    }
    if (n == K_rb || n == K_rtc) { if (in_scope(p, K_ruby, SC_DEFAULT)) gen_implied(p, NULL); insert_html(p, t); return; }
    if (n == K_rp || n == K_rt) { if (in_scope(p, K_ruby, SC_DEFAULT)) gen_implied(p, K_rtc); insert_html(p, t); return; }
    if (n == A_math || n == A_svg) {
        reconstruct(p); insert_el(p, t, n == A_math ? NS_MATHML : NS_SVG, n);
        if (t->self_closing) pop(p);
        return;
    }
    if (aset_has(&S_table_ignore, n)) return;
    reconstruct(p); insert_html(p, t);
}
static void body_end(P, Tok *t) {
    const char *n = t->name;
    if (n == A_template) { process(p, t, M_IN_HEAD); return; }
    if (n == A_body) { if (in_scope(p, A_body, SC_DEFAULT)) p->mode = M_AFTER_BODY; return; }
    if (n == A_html) { if (!in_scope(p, A_body, SC_DEFAULT)) return; p->mode = M_AFTER_BODY; reprocess(p, t); return; }
    if (aset_has(&S_block_end, n)) { if (!in_scope(p, n, SC_DEFAULT)) return; gen_implied(p, NULL); pop_until(p, n); return; }
    if (n == A_form) {
        if (!in_template(p)) {
            Node *f = p->form_el; p->form_el = NULL;
            if (!f || !node_in_scope(p, f)) return;
            gen_implied(p, NULL);
            int i = stack_index(p, f); if (i >= 0) stack_remove_at(p, i);
        } else {
            if (!in_scope(p, A_form, SC_DEFAULT)) return;
            gen_implied(p, NULL); pop_until(p, A_form);
        }
        return;
    }
    if (n == A_p) {
        if (!in_scope(p, A_p, SC_BUTTON)) insert_fake(p, A_p);
        close_p(p); return;
    }
    if (n == A_li) { if (!in_scope(p, A_li, SC_LIST)) return; gen_implied(p, A_li); pop_until(p, A_li); return; }
    if (n == A_dd || n == A_dt) { if (!in_scope(p, n, SC_DEFAULT)) return; gen_implied(p, n); pop_until(p, n); return; }
    if (is_heading(n)) {
        if (!heading_in_scope(p)) return;
        gen_implied(p, NULL);
        while (p->open.n) { Node *c = cur(p); pop(p); if (c->ns == NS_HTML && is_heading(c->tag)) break; }
        return;
    }
    if (n == A_a || n == A_nobr || aset_has(&S_fmt, n)) { if (!adoption(p, n)) any_other_end(p, n); return; }
    if (n == K_applet || n == K_marquee || n == A_object) {
        if (!in_scope(p, n, SC_DEFAULT)) return;
        gen_implied(p, NULL); pop_until(p, n); clear_to_marker(p); return;
    }
    if (n == A_br) { Tok f = { .type = T_START, .name = A_br }; body_start(p, &f); return; }
    if (n == A_select) { if (in_scope(p, A_select, SC_DEFAULT)) pop_until(p, A_select); return; }
    any_other_end(p, n);
}
static void m_in_body(P, Tok *t) {
    switch (t->type) {
    case T_CHARS:
        if (t->cls == C_NUL) return;
        reconstruct(p); insert_chars(p, t->data, t->len);
        if (t->cls == C_OTHER) p->frameset_ok = false;
        return;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_EOF: if (p->tmpl_modes.n) process(p, t, M_IN_TEMPLATE); else stop_parsing(p); return;
    case T_START: body_start(p, t); return;
    case T_END: body_end(p, t); return;
    }
}
static void m_text(P, Tok *t) {
    if (t->type == T_CHARS) { insert_chars(p, t->data, t->len); return; }
    if (t->type == T_EOF) { Node *c = cur(p); if (is_html(c, A_script)) c->flags |= NF_SCRIPT_STARTED; pop(p); p->mode = p->orig_mode; reprocess(p, t); return; }
    if (t->type == T_END) {
        Node *c = cur(p); pop(p); p->mode = p->orig_mode;
        if (!p->ctx && is_html(c, A_script) && html_script_hook) html_script_hook(p->doc, c, html_script_hook_ud);
        if (!p->ctx && is_html(c, A_title) && !p->doc->title) p->doc->title = node_text_content(c);
    }
}

static void clear_to_table(P) { for (Node *c; (c = cur(p)) && !(is_html(c, A_table) || is_html(c, A_template) || is_html(c, A_html));) pop(p); }
static void clear_to_table_body(P) { for (Node *c; (c = cur(p)) && !(is_html(c, A_tbody) || is_html(c, A_tfoot) || is_html(c, A_thead) || is_html(c, A_template) || is_html(c, A_html));) pop(p); }
static void clear_to_row(P) { for (Node *c; (c = cur(p)) && !(is_html(c, A_tr) || is_html(c, A_template) || is_html(c, A_html));) pop(p); }

static void table_anything_else(P, Tok *t) {
    bool f = p->foster; p->foster = true;
    process(p, t, M_IN_BODY);
    p->foster = f;
}
static void m_in_table(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: {
        Node *c = cur(p);
        if (c->ns == NS_HTML && (c->tag == A_table || c->tag == A_tbody || c->tag == A_template || c->tag == A_tfoot || c->tag == A_thead || c->tag == A_tr)) {
            sb_clear(&p->table_text); p->table_text_other = false;
            p->orig_mode = p->mode; p->mode = M_IN_TABLE_TEXT; reprocess(p, t); return;
        }
        break;
    }
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_START: {
        const char *n = t->name;
        if (n == A_caption) { clear_to_table(p); vec_push(p->fmt, (Node *)NULL); insert_html(p, t); p->mode = M_IN_CAPTION; return; }
        if (n == A_colgroup) { clear_to_table(p); insert_html(p, t); p->mode = M_IN_COLUMN_GROUP; return; }
        if (n == A_col) { clear_to_table(p); insert_fake(p, A_colgroup); p->mode = M_IN_COLUMN_GROUP; reprocess(p, t); return; }
        if (n == A_tbody || n == A_tfoot || n == A_thead) { clear_to_table(p); insert_html(p, t); p->mode = M_IN_TABLE_BODY; return; }
        if (n == A_td || n == A_th || n == A_tr) { clear_to_table(p); insert_fake(p, A_tbody); p->mode = M_IN_TABLE_BODY; reprocess(p, t); return; }
        if (n == A_table) {
            if (!in_scope(p, A_table, SC_TABLE)) return;
            pop_until(p, A_table); reset_mode(p); reprocess(p, t); return;
        }
        if (n == A_style || n == A_script || n == A_template) { process(p, t, M_IN_HEAD); return; }
        if (n == A_input) {
            const char *ty = tok_attr(t, "type");
            if (!ty || !str_ieq(ty, "hidden")) break;
            insert_html(p, t); pop(p); return;
        }
        if (n == A_form) {
            bool tm = in_template(p);
            if (p->form_el && !tm) return;
            Node *e = insert_html(p, t); if (!tm) p->form_el = e;
            pop(p); return;
        }
        break;
    }
    case T_END: {
        const char *n = t->name;
        if (n == A_table) { if (!in_scope(p, A_table, SC_TABLE)) return; pop_until(p, A_table); reset_mode(p); return; }
        if (n == A_body || n == A_caption || n == A_col || n == A_colgroup || n == A_html || n == A_tbody || n == A_td || n == A_tfoot || n == A_th || n == A_thead || n == A_tr) return;
        if (n == A_template) { process(p, t, M_IN_HEAD); return; }
        break;
    }
    case T_EOF: process(p, t, M_IN_BODY); return;
    }
    table_anything_else(p, t);
}
static void m_in_table_text(P, Tok *t) {
    if (t->type == T_CHARS) {
        if (t->cls == C_NUL) return;
        sb_put(&p->table_text, t->data, t->len);
        if (t->cls == C_OTHER) p->table_text_other = true;
        return;
    }
    if (p->table_text.n) {
        Tok c = { .type = T_CHARS, .cls = p->table_text_other ? C_OTHER : C_WS, .data = p->table_text.s, .len = p->table_text.n };
        SB keep = p->table_text; sb_init(&p->table_text);
        if (p->table_text_other) table_anything_else(p, &c); else insert_chars(p, c.data, c.len);
        sb_free(&p->table_text); p->table_text = keep; sb_clear(&p->table_text);
    }
    p->mode = p->orig_mode; reprocess(p, t);
}
static void m_in_caption(P, Tok *t) {
    if (t->type == T_END && t->name == A_caption) {
        if (!in_scope(p, A_caption, SC_TABLE)) return;
        gen_implied(p, NULL); pop_until(p, A_caption); clear_to_marker(p); p->mode = M_IN_TABLE; return;
    }
    if ((t->type == T_START && (t->name == A_caption || t->name == A_col || t->name == A_colgroup || t->name == A_tbody || t->name == A_td || t->name == A_tfoot || t->name == A_th || t->name == A_thead || t->name == A_tr)) || (t->type == T_END && t->name == A_table)) {
        if (!in_scope(p, A_caption, SC_TABLE)) return;
        gen_implied(p, NULL); pop_until(p, A_caption); clear_to_marker(p); p->mode = M_IN_TABLE; reprocess(p, t); return;
    }
    if (t->type == T_END && (t->name == A_body || t->name == A_col || t->name == A_colgroup || t->name == A_html || t->name == A_tbody || t->name == A_td || t->name == A_tfoot || t->name == A_th || t->name == A_thead || t->name == A_tr)) return;
    process(p, t, M_IN_BODY);
}
static void m_in_column_group(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) { insert_chars(p, t->data, t->len); return; } break;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_START:
        if (t->name == A_html) { process(p, t, M_IN_BODY); return; }
        if (t->name == A_col) { insert_html(p, t); pop(p); return; }
        if (t->name == A_template) { process(p, t, M_IN_HEAD); return; }
        break;
    case T_END:
        if (t->name == A_colgroup) { if (!is_html(cur(p), A_colgroup)) return; pop(p); p->mode = M_IN_TABLE; return; }
        if (t->name == A_col) return;
        if (t->name == A_template) { process(p, t, M_IN_HEAD); return; }
        break;
    case T_EOF: process(p, t, M_IN_BODY); return;
    }
    if (!is_html(cur(p), A_colgroup)) return;
    pop(p); p->mode = M_IN_TABLE; reprocess(p, t);
}
static bool table_section_in_scope(P) { return in_scope(p, A_tbody, SC_TABLE) || in_scope(p, A_thead, SC_TABLE) || in_scope(p, A_tfoot, SC_TABLE); }
static void m_in_table_body(P, Tok *t) {
    const char *n = t->name;
    if (t->type == T_START) {
        if (n == A_tr) { clear_to_table_body(p); insert_html(p, t); p->mode = M_IN_ROW; return; }
        if (n == A_th || n == A_td) { clear_to_table_body(p); insert_fake(p, A_tr); p->mode = M_IN_ROW; reprocess(p, t); return; }
        if (n == A_caption || n == A_col || n == A_colgroup || n == A_tbody || n == A_tfoot || n == A_thead) {
            if (!table_section_in_scope(p)) return;
            clear_to_table_body(p); pop(p); p->mode = M_IN_TABLE; reprocess(p, t); return;
        }
    } else if (t->type == T_END) {
        if (n == A_tbody || n == A_tfoot || n == A_thead) {
            if (!in_scope(p, n, SC_TABLE)) return;
            clear_to_table_body(p); pop(p); p->mode = M_IN_TABLE; return;
        }
        if (n == A_table) {
            if (!table_section_in_scope(p)) return;
            clear_to_table_body(p); pop(p); p->mode = M_IN_TABLE; reprocess(p, t); return;
        }
        if (n == A_body || n == A_caption || n == A_col || n == A_colgroup || n == A_html || n == A_td || n == A_th || n == A_tr) return;
    }
    process(p, t, M_IN_TABLE);
}
static void m_in_row(P, Tok *t) {
    const char *n = t->name;
    if (t->type == T_START) {
        if (n == A_th || n == A_td) { clear_to_row(p); insert_html(p, t); p->mode = M_IN_CELL; vec_push(p->fmt, (Node *)NULL); return; }
        if (n == A_caption || n == A_col || n == A_colgroup || n == A_tbody || n == A_tfoot || n == A_thead || n == A_tr) {
            if (!in_scope(p, A_tr, SC_TABLE)) return;
            clear_to_row(p); pop(p); p->mode = M_IN_TABLE_BODY; reprocess(p, t); return;
        }
    } else if (t->type == T_END) {
        if (n == A_tr) { if (!in_scope(p, A_tr, SC_TABLE)) return; clear_to_row(p); pop(p); p->mode = M_IN_TABLE_BODY; return; }
        if (n == A_table) {
            if (!in_scope(p, A_tr, SC_TABLE)) return;
            clear_to_row(p); pop(p); p->mode = M_IN_TABLE_BODY; reprocess(p, t); return;
        }
        if (n == A_tbody || n == A_tfoot || n == A_thead) {
            if (!in_scope(p, n, SC_TABLE) || !in_scope(p, A_tr, SC_TABLE)) return;
            clear_to_row(p); pop(p); p->mode = M_IN_TABLE_BODY; reprocess(p, t); return;
        }
        if (n == A_body || n == A_caption || n == A_col || n == A_colgroup || n == A_html || n == A_td || n == A_th) return;
    }
    process(p, t, M_IN_TABLE);
}
static void close_cell(P) {
    gen_implied(p, NULL);
    while (p->open.n) { Node *c = cur(p); pop(p); if (is_html(c, A_td) || is_html(c, A_th)) break; }
    clear_to_marker(p); p->mode = M_IN_ROW;
}
static void m_in_cell(P, Tok *t) {
    const char *n = t->name;
    if (t->type == T_END) {
        if (n == A_td || n == A_th) {
            if (!in_scope(p, n, SC_TABLE)) return;
            gen_implied(p, NULL); pop_until(p, n); clear_to_marker(p); p->mode = M_IN_ROW; return;
        }
        if (n == A_body || n == A_caption || n == A_col || n == A_colgroup || n == A_html) return;
        if (n == A_table || n == A_tbody || n == A_tfoot || n == A_thead || n == A_tr) {
            if (!in_scope(p, n, SC_TABLE)) return;
            close_cell(p); reprocess(p, t); return;
        }
    } else if (t->type == T_START && (n == A_caption || n == A_col || n == A_colgroup || n == A_tbody || n == A_td || n == A_tfoot || n == A_th || n == A_thead || n == A_tr)) {
        if (!in_scope(p, A_td, SC_TABLE) && !in_scope(p, A_th, SC_TABLE)) return;
        close_cell(p); reprocess(p, t); return;
    }
    process(p, t, M_IN_BODY);
}
static void m_in_template(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: case T_COMMENT: case T_PI: case T_DOCTYPE: process(p, t, M_IN_BODY); return;
    case T_START: {
        const char *n = t->name;
        if (aset_has(&S_head_start, n)) { process(p, t, M_IN_HEAD); return; }
        int m = M_IN_BODY;
        if (n == A_caption || n == A_colgroup || n == A_tbody || n == A_tfoot || n == A_thead) m = M_IN_TABLE;
        else if (n == A_col) m = M_IN_COLUMN_GROUP;
        else if (n == A_tr) m = M_IN_TABLE_BODY;
        else if (n == A_td || n == A_th) m = M_IN_ROW;
        if (p->tmpl_modes.n) p->tmpl_modes.n--;
        vec_push(p->tmpl_modes, m); p->mode = m; reprocess(p, t);
        return;
    }
    case T_END: if (t->name == A_template) process(p, t, M_IN_HEAD); return;
    case T_EOF:
        if (!tmpl_on_stack(p)) { stop_parsing(p); return; }
        pop_until(p, A_template); clear_to_marker(p);
        if (p->tmpl_modes.n) p->tmpl_modes.n--;
        reset_mode(p); reprocess(p, t);
        return;
    }
}
static void m_after_body(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) { process(p, t, M_IN_BODY); return; } break;
    case T_COMMENT: case T_PI: append_comment_to(p, t, p->open.v[0]); return;
    case T_DOCTYPE: return;
    case T_START: if (t->name == A_html) { process(p, t, M_IN_BODY); return; } break;
    case T_END: if (t->name == A_html) { if (!p->ctx) p->mode = M_AFTER_AFTER_BODY; return; } break;
    case T_EOF: stop_parsing(p); return;
    }
    p->mode = M_IN_BODY; reprocess(p, t);
}
static void m_in_frameset(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) insert_chars(p, t->data, t->len); return;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_START:
        if (t->name == A_html) { process(p, t, M_IN_BODY); return; }
        if (t->name == A_frameset) { insert_html(p, t); return; }
        if (t->name == A_frame) { insert_html(p, t); pop(p); return; }
        if (t->name == K_noframes) { process(p, t, M_IN_HEAD); return; }
        return;
    case T_END:
        if (t->name == A_frameset) {
            if (p->open.n == 1 && is_html(cur(p), A_html)) return;
            pop(p);
            if (!p->ctx && !is_html(cur(p), A_frameset)) p->mode = M_AFTER_FRAMESET;
        }
        return;
    case T_EOF: stop_parsing(p); return;
    }
}
static void m_after_frameset(P, Tok *t) {
    switch (t->type) {
    case T_CHARS: if (t->cls == C_WS) insert_chars(p, t->data, t->len); return;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_START:
        if (t->name == A_html) { process(p, t, M_IN_BODY); return; }
        if (t->name == K_noframes) { process(p, t, M_IN_HEAD); return; }
        return;
    case T_END: if (t->name == A_html) p->mode = M_AFTER_AFTER_FRAMESET; return;
    case T_EOF: stop_parsing(p); return;
    }
}
static void m_after_after_body(P, Tok *t) {
    switch (t->type) {
    case T_COMMENT: case T_PI: append_comment_to(p, t, &p->doc->node); return;
    case T_DOCTYPE: process(p, t, M_IN_BODY); return;
    case T_CHARS: if (t->cls == C_WS) { process(p, t, M_IN_BODY); return; } break;
    case T_START: if (t->name == A_html) { process(p, t, M_IN_BODY); return; } break;
    case T_EOF: stop_parsing(p); return;
    }
    p->mode = M_IN_BODY; reprocess(p, t);
}
static void m_after_after_frameset(P, Tok *t) {
    switch (t->type) {
    case T_COMMENT: case T_PI: append_comment_to(p, t, &p->doc->node); return;
    case T_DOCTYPE: process(p, t, M_IN_BODY); return;
    case T_CHARS: if (t->cls == C_WS) process(p, t, M_IN_BODY); return;
    case T_START:
        if (t->name == A_html) { process(p, t, M_IN_BODY); return; }
        if (t->name == K_noframes) { process(p, t, M_IN_HEAD); return; }
        return;
    case T_EOF: stop_parsing(p); return;
    }
}

static void process(P, Tok *t, int mode) {
    switch (mode) {
    case M_INITIAL: m_initial(p, t); break;
    case M_BEFORE_HTML: m_before_html(p, t); break;
    case M_BEFORE_HEAD: m_before_head(p, t); break;
    case M_IN_HEAD: m_in_head(p, t); break;
    case M_IN_HEAD_NOSCRIPT: m_in_head_noscript(p, t); break;
    case M_AFTER_HEAD: m_after_head(p, t); break;
    case M_IN_BODY: m_in_body(p, t); break;
    case M_TEXT: m_text(p, t); break;
    case M_IN_TABLE: m_in_table(p, t); break;
    case M_IN_TABLE_TEXT: m_in_table_text(p, t); break;
    case M_IN_CAPTION: m_in_caption(p, t); break;
    case M_IN_COLUMN_GROUP: m_in_column_group(p, t); break;
    case M_IN_TABLE_BODY: m_in_table_body(p, t); break;
    case M_IN_ROW: m_in_row(p, t); break;
    case M_IN_CELL: m_in_cell(p, t); break;
    case M_IN_TEMPLATE: m_in_template(p, t); break;
    case M_AFTER_BODY: m_after_body(p, t); break;
    case M_IN_FRAMESET: m_in_frameset(p, t); break;
    case M_AFTER_FRAMESET: m_after_frameset(p, t); break;
    case M_AFTER_AFTER_BODY: m_after_after_body(p, t); break;
    case M_AFTER_AFTER_FRAMESET: m_after_after_frameset(p, t); break;
    }
}

/* the rules for parsing tokens in foreign content */
static void foreign_breakout(P, Tok *t) {
    for (Node *c; (c = cur(p)) && !(mathml_text_ip(c) || html_ip(c) || c->ns == NS_HTML);) pop(p);
    reprocess(p, t);
}
static void foreign(P, Tok *t) {
    switch (t->type) {
    case T_CHARS:
        if (t->cls == C_NUL) { for (size_t i = 0; i < t->len; i++) insert_chars(p, "\xEF\xBF\xBD", 3); return; }
        insert_chars(p, t->data, t->len);
        if (t->cls == C_OTHER) p->frameset_ok = false;
        return;
    case T_COMMENT: case T_PI: insert_comment(p, t); return;
    case T_DOCTYPE: return;
    case T_START: {
        const char *n = t->name;
        if (aset_has(&S_breakout, n) || (n == A_font && (tok_attr(t, "color") || tok_attr(t, "face") || tok_attr(t, "size")))) { foreign_breakout(p, t); return; }
        int ns = adj_cur(p)->ns;
        insert_el(p, t, ns, ns == NS_SVG ? svg_tag_name(n) : n);
        /* an SVG script runs only once its end tag (or a self-closing tag) is seen, and never from a fragment */
        if (ns == NS_SVG && n == A_script && (p->ctx || !t->self_closing)) cur(p)->flags |= NF_SCRIPT_STARTED;
        if (t->self_closing) pop(p);
        return;
    }
    case T_END: {
        const char *n = t->name;
        if (n == A_br || n == A_p) { foreign_breakout(p, t); return; }
        if (n == A_script && cur(p)->ns == NS_SVG && cur(p)->tag == A_script) { if (!p->ctx) cur(p)->flags &= ~NF_SCRIPT_STARTED; pop(p); return; }
        int i = p->open.n - 1;
        for (;;) {
            if (i <= 0) return;
            Node *node = p->open.v[i];
            if (str_ieq(lname(node), n)) { while (p->open.n > i) pop(p); return; }
            node = p->open.v[--i];
            if (node->ns == NS_HTML) { reprocess(p, t); return; }
        }
    }
    }
}

/* the tree construction dispatcher */
static void dispatch(P, Tok *t) {
    Node *ac = adj_cur(p);
    bool html = !ac || ac->ns == NS_HTML || t->type == T_EOF;
    if (!html) {
        if (mathml_text_ip(ac) && ((t->type == T_START && t->name != K_mglyph && t->name != K_malignmark) || t->type == T_CHARS)) html = true;
        else if (ac->ns == NS_MATHML && ac->tag == K_annotation_xml && t->type == T_START && t->name == A_svg) html = true;
        else if (html_ip(ac) && (t->type == T_START || t->type == T_CHARS)) html = true;
    }
    if (html) process(p, t, p->mode); else foreign(p, t);
}
static int char_class(unsigned char c) { return c == 0 ? C_NUL : (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') ? C_WS : C_OTHER; }
static void tree_chars(P, const char *s, size_t n) {
    bool skip = p->skip_lf; p->skip_lf = false;
    if (skip && n && s[0] == '\n') { s++; n--; }
    while (n && !p->stopped) {
        int cls = char_class((unsigned char)s[0]); size_t k = 1;
        if (cls != C_NUL && (p->mode == M_IN_BODY || p->mode == M_TEXT)) {
            /* whitespace and other characters get the same treatment here; NULs do not */
            for (; k < n && s[k]; k++) if (cls == C_WS && char_class((unsigned char)s[k]) == C_OTHER) cls = C_OTHER;
        } else while (k < n && char_class((unsigned char)s[k]) == cls) k++;
        Tok t = { .type = T_CHARS, .cls = cls, .data = s, .len = k };
        dispatch(p, &t);
        s += k; n -= k;
    }
}
static void tree_token(P, Tok *t) {
    p->skip_lf = false;
    if (!p->stopped) dispatch(p, t);
}

/* ---------------- tokenizer ---------------- */
static void flush_chars(P) { if (p->chars.n) { tree_chars(p, p->chars.s, p->chars.n); sb_clear(&p->chars); } }
static void put_fffd(SB *b) { sb_put(b, "\xEF\xBF\xBD", 3); }

static void new_tag(P, bool end) {
    sb_clear(&p->tag); p->end_tag = end; p->self_closing = false; p->attr_open = false;
    for (int i = 0; i < p->attrs.n; i++) free(p->attrs.v[i].value);
    p->attrs.n = 0;
}
static void attr_finish(P) {
    if (!p->attr_open) return;
    p->attr_open = false;
    const char *name = atomn(p->an.s ? p->an.s : "", p->an.n);
    for (int i = 0; i < p->attrs.n; i++) if (p->attrs.v[i].name == name) return;
    TAttr a = { name, xstrndup(p->av.s ? p->av.s : "", p->av.n) };
    vec_push(p->attrs, a);
}
static void attr_start(P) { attr_finish(p); p->attr_open = true; sb_clear(&p->an); sb_clear(&p->av); }
static void emit_tag(P) {
    attr_finish(p);
    flush_chars(p);
    Tok t = { .type = p->end_tag ? T_END : T_START, .name = atomn(p->tag.s ? p->tag.s : "", p->tag.n), .attrs = p->attrs.v,
              .nattrs = p->end_tag ? 0 : p->attrs.n, .self_closing = p->self_closing };
    if (!p->end_tag) p->last_start = t.name;
    tree_token(p, &t);
    new_tag(p, false);
}
static void emit_comment(P) {
    flush_chars(p);
    Tok t = { .type = T_COMMENT, .data = p->text.s ? p->text.s : "", .len = p->text.n };
    tree_token(p, &t);
    sb_clear(&p->text);
}
static void emit_doctype(P) {
    flush_chars(p);
    Tok t = { .type = T_DOCTYPE };
    tree_token(p, &t);
}
static void emit_eof(P) {
    flush_chars(p);
    Tok t = { .type = T_EOF };
    tree_token(p, &t);
    stop_parsing(p);
}
static void new_doctype(P) {
    sb_clear(&p->dt_name); sb_clear(&p->dt_pub); sb_clear(&p->dt_sys);
    p->dt_has_name = p->dt_has_pub = p->dt_has_sys = p->dt_force_quirks = false;
}
static bool appropriate_end(P) { return p->last_start && strlen(p->last_start) == p->tag.n && !memcmp(p->last_start, p->tag.s, p->tag.n); }
static bool is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_alnum(int c) { return is_alpha(c) || (c >= '0' && c <= '9'); }
static bool tok_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f'; }

/* a character reference after '&' (s[*pi] is the next character): its characters go to out */
static void charref(const unsigned char *s, size_t n, size_t *pi, SB *out, bool attr) {
    size_t i = *pi;
    if (i < n && is_alnum(s[i])) {
        size_t used = 0; uint32_t second = 0;
        uint32_t cp = html_entity_lookup((const char *)s + i, n - i, &second, &used);
        if (cp && used) {
            bool semi = s[i + used - 1] == ';';
            if (attr && !semi && i + used < n && (s[i + used] == '=' || is_alnum(s[i + used]))) { sb_putc(out, '&'); return; }
            sb_utf8(out, cp); if (second) sb_utf8(out, second);
            *pi = i + used;
            return;
        }
        sb_putc(out, '&');
        return;
    }
    if (i < n && s[i] == '#') {
        size_t j = i + 1; bool hex = false;
        if (j < n && (s[j] == 'x' || s[j] == 'X')) { hex = true; j++; }
        size_t st = j; uint32_t cp = 0;
        for (; j < n; j++) {
            int c = s[j], d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            if (cp <= 0x10FFFF) cp = cp * (hex ? 16 : 10) + (uint32_t)d;
        }
        if (j == st) { sb_putc(out, '&'); return; }
        if (j < n && s[j] == ';') j++;
        static const uint16_t win1252[32] = { 0x20AC,0x81,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x8D,0x017D,0x8F,0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x9D,0x017E,0x0178 };
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        else if (cp >= 0x80 && cp <= 0x9F) cp = win1252[cp - 0x80];
        sb_utf8(out, cp);
        *pi = j;
        return;
    }
    sb_putc(out, '&');
}

/* "<?": a processing instruction when an ASCII name (a letter or _, then letters, digits, - and _) not starting
   with "xml" follows and ends at whitespace, ? or >; its data runs to the next > less one ? before it. One left
   open at the end of the input is dropped, as is a lone "<?". Anything else is a bogus comment from the ?. */
static void pi_or_bogus(P, const unsigned char *s, size_t n, size_t *pi) {
    size_t q = *pi;
    p->state = S_DATA;
    if (q >= n) { *pi = n; return; }
    if (is_alpha(s[q]) || s[q] == '_') {
        size_t t = q;
        while (t < n && (is_alnum(s[t]) || s[t] == '-' || s[t] == '_')) t++;
        if (t >= n) { *pi = n; return; }
        if ((tok_ws(s[t]) || s[t] == '?' || s[t] == '>') && !(t - q >= 3 && !strncasecmp((const char *)s + q, "xml", 3))) {
            size_t ds = t; while (ds < n && tok_ws(s[ds])) ds++;
            const unsigned char *e = ds < n ? memchr(s + ds, '>', n - ds) : NULL;
            if (!e) { *pi = n; return; }
            size_t de = (size_t)(e - s); if (de > ds && s[de - 1] == '?') de--;
            flush_chars(p);
            SB data; sb_init(&data);
            for (size_t k = ds; k < de; k++) { if (s[k]) sb_putc(&data, (char)s[k]); else put_fffd(&data); }
            Tok tk = { .type = T_PI, .target = atomn((const char *)s + q, t - q), .data = data.s ? data.s : "", .len = data.n };
            tree_token(p, &tk);
            sb_free(&data);
            *pi = (size_t)(e - s) + 1;
            return;
        }
    }
    sb_clear(&p->text); *pi = q - 1; p->state = S_BOGUS_COMMENT;
}

#define EOF_C (-1)
static void tokenize(P) {
    const unsigned char *s = (const unsigned char *)(p->in.s ? p->in.s : "");
    size_t n = p->in.n, i = 0;
    int c;
#define NEXT() (i < n ? (int)s[i++] : (i++, EOF_C))
#define RECONSUME() (i--)
#define TO(st) (p->state = (st))
    while (!p->stopped) {
        switch (p->state) {
        case S_DATA: {
            size_t k = i; while (k < n && s[k] != '<' && s[k] != '&' && s[k]) k++;
            if (k > i) { sb_put(&p->chars, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == '&') charref(s, n, &i, &p->chars, false);
            else if (c == '<') TO(S_TAG_OPEN);
            else if (c == 0) sb_putc(&p->chars, 0);
            else if (c == EOF_C) { emit_eof(p); return; }
            break;
        }
        case S_RCDATA: case S_RAWTEXT: case S_SCRIPT: case S_PLAINTEXT: {
            int st = p->state;
            size_t k = i; while (k < n && s[k] != '<' && s[k] != '&' && s[k]) k++;
            if (k > i) { sb_put(&p->chars, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == EOF_C) { emit_eof(p); return; }
            if (c == 0) put_fffd(&p->chars);
            else if (c == '&' && st == S_RCDATA) charref(s, n, &i, &p->chars, false);
            else if (c == '<' && st != S_PLAINTEXT) TO(st == S_RCDATA ? S_RCDATA_LT : st == S_RAWTEXT ? S_RAWTEXT_LT : S_SCRIPT_LT);
            else sb_putc(&p->chars, (char)c);
            break;
        }
        case S_TAG_OPEN:
            c = NEXT();
            if (c == '!') TO(S_MARKUP_DECL);
            else if (c == '/') TO(S_END_TAG_OPEN);
            else if (is_alpha(c)) { new_tag(p, false); RECONSUME(); TO(S_TAG_NAME); }
            else if (c == '?') pi_or_bogus(p, s, n, &i);
            else { sb_putc(&p->chars, '<'); RECONSUME(); TO(S_DATA); }
            break;
        case S_END_TAG_OPEN:
            c = NEXT();
            if (is_alpha(c)) { new_tag(p, true); RECONSUME(); TO(S_TAG_NAME); }
            else if (c == '>') TO(S_DATA);
            else if (c == EOF_C) { sb_put(&p->chars, "</", 2); RECONSUME(); TO(S_DATA); }
            else { sb_clear(&p->text); RECONSUME(); TO(S_BOGUS_COMMENT); }
            break;
        case S_TAG_NAME:
            c = NEXT();
            if (tok_ws(c)) TO(S_BEFORE_ATTR_NAME);
            else if (c == '/') TO(S_SELF_CLOSING);
            else if (c == '>') { TO(S_DATA); emit_tag(p); }
            else if (c == 0) put_fffd(&p->tag);
            else if (c == EOF_C) { RECONSUME(); TO(S_DATA); }
            else sb_putc(&p->tag, (char)lc(c));
            break;

        case S_RCDATA_LT: case S_RAWTEXT_LT: {
            int back = p->state == S_RCDATA_LT ? S_RCDATA : S_RAWTEXT;
            c = NEXT();
            if (c == '/') { sb_clear(&p->tmp); TO(back == S_RCDATA ? S_RCDATA_END_OPEN : S_RAWTEXT_END_OPEN); }
            else { sb_putc(&p->chars, '<'); RECONSUME(); TO(back); }
            break;
        }
        case S_RCDATA_END_OPEN: case S_RAWTEXT_END_OPEN: case S_SCRIPT_END_OPEN: case S_SCRIPT_ESC_END_OPEN: {
            int st = p->state;
            int back = st == S_RCDATA_END_OPEN ? S_RCDATA : st == S_RAWTEXT_END_OPEN ? S_RAWTEXT : st == S_SCRIPT_END_OPEN ? S_SCRIPT : S_SCRIPT_ESC;
            c = NEXT();
            if (is_alpha(c)) { new_tag(p, true); RECONSUME(); TO(st + 1); }
            else { sb_put(&p->chars, "</", 2); RECONSUME(); TO(back); }
            break;
        }
        case S_RCDATA_END_NAME: case S_RAWTEXT_END_NAME: case S_SCRIPT_END_NAME: case S_SCRIPT_ESC_END_NAME: {
            int st = p->state;
            int back = st == S_RCDATA_END_NAME ? S_RCDATA : st == S_RAWTEXT_END_NAME ? S_RAWTEXT : st == S_SCRIPT_END_NAME ? S_SCRIPT : S_SCRIPT_ESC;
            c = NEXT();
            if ((tok_ws(c) || c == '/' || c == '>') && appropriate_end(p)) {
                if (c == '>') { TO(S_DATA); emit_tag(p); }
                else TO(c == '/' ? S_SELF_CLOSING : S_BEFORE_ATTR_NAME);
            } else if (is_alpha(c)) { sb_putc(&p->tag, (char)lc(c)); sb_putc(&p->tmp, (char)c); }
            else { sb_put(&p->chars, "</", 2); sb_put(&p->chars, p->tmp.s ? p->tmp.s : "", p->tmp.n); new_tag(p, false); RECONSUME(); TO(back); }
            break;
        }

        case S_SCRIPT_LT:
            c = NEXT();
            if (c == '/') { sb_clear(&p->tmp); TO(S_SCRIPT_END_OPEN); }
            else if (c == '!') { TO(S_SCRIPT_ESC_START); sb_put(&p->chars, "<!", 2); }
            else { sb_putc(&p->chars, '<'); RECONSUME(); TO(S_SCRIPT); }
            break;
        case S_SCRIPT_ESC_START: case S_SCRIPT_ESC_START_DASH:
            c = NEXT();
            if (c == '-') { sb_putc(&p->chars, '-'); TO(p->state == S_SCRIPT_ESC_START ? S_SCRIPT_ESC_START_DASH : S_SCRIPT_ESC_DASH_DASH); }
            else { RECONSUME(); TO(S_SCRIPT); }
            break;
        case S_SCRIPT_ESC: {
            size_t k = i; while (k < n && s[k] != '-' && s[k] != '<' && s[k]) k++;
            if (k > i) { sb_put(&p->chars, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == '-') { TO(S_SCRIPT_ESC_DASH); sb_putc(&p->chars, '-'); }
            else if (c == '<') TO(S_SCRIPT_ESC_LT);
            else if (c == 0) put_fffd(&p->chars);
            else if (c == EOF_C) { emit_eof(p); return; }
            break;
        }
        case S_SCRIPT_ESC_DASH: case S_SCRIPT_ESC_DASH_DASH: {
            bool dd = p->state == S_SCRIPT_ESC_DASH_DASH;
            c = NEXT();
            if (c == '-') { TO(S_SCRIPT_ESC_DASH_DASH); sb_putc(&p->chars, '-'); }
            else if (c == '<') TO(S_SCRIPT_ESC_LT);
            else if (c == '>' && dd) { TO(S_SCRIPT); sb_putc(&p->chars, '>'); }
            else if (c == 0) { TO(S_SCRIPT_ESC); put_fffd(&p->chars); }
            else if (c == EOF_C) { emit_eof(p); return; }
            else { TO(S_SCRIPT_ESC); sb_putc(&p->chars, (char)c); }
            break;
        }
        case S_SCRIPT_ESC_LT:
            c = NEXT();
            if (c == '/') { sb_clear(&p->tmp); TO(S_SCRIPT_ESC_END_OPEN); }
            else if (is_alpha(c)) { sb_clear(&p->tmp); sb_putc(&p->chars, '<'); RECONSUME(); TO(S_SCRIPT_DESC_START); }
            else { sb_putc(&p->chars, '<'); RECONSUME(); TO(S_SCRIPT_ESC); }
            break;
        case S_SCRIPT_DESC_START: case S_SCRIPT_DESC_END: {
            bool start = p->state == S_SCRIPT_DESC_START;
            c = NEXT();
            if (tok_ws(c) || c == '/' || c == '>') {
                bool script = p->tmp.n == 6 && !memcmp(p->tmp.s, "script", 6);
                TO(start ? (script ? S_SCRIPT_DESC : S_SCRIPT_ESC) : (script ? S_SCRIPT_ESC : S_SCRIPT_DESC));
                sb_putc(&p->chars, (char)c);
            } else if (is_alpha(c)) { sb_putc(&p->tmp, (char)lc(c)); sb_putc(&p->chars, (char)c); }
            else { RECONSUME(); TO(start ? S_SCRIPT_ESC : S_SCRIPT_DESC); }
            break;
        }
        case S_SCRIPT_DESC: {
            size_t k = i; while (k < n && s[k] != '-' && s[k] != '<' && s[k]) k++;
            if (k > i) { sb_put(&p->chars, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == '-') { TO(S_SCRIPT_DESC_DASH); sb_putc(&p->chars, '-'); }
            else if (c == '<') { TO(S_SCRIPT_DESC_LT); sb_putc(&p->chars, '<'); }
            else if (c == 0) put_fffd(&p->chars);
            else if (c == EOF_C) { emit_eof(p); return; }
            break;
        }
        case S_SCRIPT_DESC_DASH: case S_SCRIPT_DESC_DASH_DASH: {
            bool dd = p->state == S_SCRIPT_DESC_DASH_DASH;
            c = NEXT();
            if (c == '-') { TO(S_SCRIPT_DESC_DASH_DASH); sb_putc(&p->chars, '-'); }
            else if (c == '<') { TO(S_SCRIPT_DESC_LT); sb_putc(&p->chars, '<'); }
            else if (c == '>' && dd) { TO(S_SCRIPT); sb_putc(&p->chars, '>'); }
            else if (c == 0) { TO(S_SCRIPT_DESC); put_fffd(&p->chars); }
            else if (c == EOF_C) { emit_eof(p); return; }
            else { TO(S_SCRIPT_DESC); sb_putc(&p->chars, (char)c); }
            break;
        }
        case S_SCRIPT_DESC_LT:
            c = NEXT();
            if (c == '/') { sb_clear(&p->tmp); TO(S_SCRIPT_DESC_END); sb_putc(&p->chars, '/'); }
            else { RECONSUME(); TO(S_SCRIPT_DESC); }
            break;

        case S_BEFORE_ATTR_NAME:
            c = NEXT();
            if (tok_ws(c)) break;
            if (c == '/' || c == '>' || c == EOF_C) { RECONSUME(); TO(S_AFTER_ATTR_NAME); }
            else if (c == '=') { attr_start(p); sb_putc(&p->an, '='); TO(S_ATTR_NAME); }
            else { attr_start(p); RECONSUME(); TO(S_ATTR_NAME); }
            break;
        case S_ATTR_NAME:
            c = NEXT();
            if (tok_ws(c) || c == '/' || c == '>' || c == EOF_C) { RECONSUME(); TO(S_AFTER_ATTR_NAME); }
            else if (c == '=') TO(S_BEFORE_ATTR_VALUE);
            else if (c == 0) put_fffd(&p->an);
            else sb_putc(&p->an, (char)lc(c));
            break;
        case S_AFTER_ATTR_NAME:
            c = NEXT();
            if (tok_ws(c)) break;
            if (c == '/') TO(S_SELF_CLOSING);
            else if (c == '=') TO(S_BEFORE_ATTR_VALUE);
            else if (c == '>') { TO(S_DATA); emit_tag(p); }
            else if (c == EOF_C) { RECONSUME(); TO(S_DATA); }
            else { attr_start(p); RECONSUME(); TO(S_ATTR_NAME); }
            break;
        case S_BEFORE_ATTR_VALUE:
            c = NEXT();
            if (tok_ws(c)) break;
            if (c == '"') TO(S_ATTR_VALUE_DQ);
            else if (c == '\'') TO(S_ATTR_VALUE_SQ);
            else if (c == '>') { TO(S_DATA); emit_tag(p); }
            else { RECONSUME(); TO(S_ATTR_VALUE_UQ); }
            break;
        case S_ATTR_VALUE_DQ: case S_ATTR_VALUE_SQ: {
            int q = p->state == S_ATTR_VALUE_DQ ? '"' : '\'';
            size_t k = i; while (k < n && s[k] != q && s[k] != '&' && s[k]) k++;
            if (k > i) { sb_put(&p->av, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == q) TO(S_AFTER_ATTR_VALUE_Q);
            else if (c == '&') charref(s, n, &i, &p->av, true);
            else if (c == 0) put_fffd(&p->av);
            else if (c == EOF_C) { RECONSUME(); TO(S_DATA); }
            break;
        }
        case S_ATTR_VALUE_UQ:
            c = NEXT();
            if (tok_ws(c)) TO(S_BEFORE_ATTR_NAME);
            else if (c == '&') charref(s, n, &i, &p->av, true);
            else if (c == '>') { TO(S_DATA); emit_tag(p); }
            else if (c == 0) put_fffd(&p->av);
            else if (c == EOF_C) { RECONSUME(); TO(S_DATA); }
            else sb_putc(&p->av, (char)c);
            break;
        case S_AFTER_ATTR_VALUE_Q:
            c = NEXT();
            if (tok_ws(c)) TO(S_BEFORE_ATTR_NAME);
            else if (c == '/') TO(S_SELF_CLOSING);
            else if (c == '>') { TO(S_DATA); emit_tag(p); }
            else if (c == EOF_C) { RECONSUME(); TO(S_DATA); }
            else { RECONSUME(); TO(S_BEFORE_ATTR_NAME); }
            break;
        case S_SELF_CLOSING:
            c = NEXT();
            if (c == '>') { p->self_closing = true; TO(S_DATA); emit_tag(p); }
            else if (c == EOF_C) { RECONSUME(); TO(S_DATA); }
            else { RECONSUME(); TO(S_BEFORE_ATTR_NAME); }
            break;

        case S_BOGUS_COMMENT: {
            size_t k = i; while (k < n && s[k] != '>' && s[k]) k++;
            if (k > i) { sb_put(&p->text, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == '>') { TO(S_DATA); emit_comment(p); }
            else if (c == EOF_C) { emit_comment(p); RECONSUME(); TO(S_DATA); }
            else if (c == 0) put_fffd(&p->text);
            break;
        }
        case S_MARKUP_DECL:
            if (i + 2 <= n && s[i] == '-' && s[i + 1] == '-') { i += 2; sb_clear(&p->text); TO(S_COMMENT_START); }
            else if (i + 7 <= n && !strncasecmp((const char *)s + i, "doctype", 7)) { i += 7; TO(S_DOCTYPE); }
            else if (i + 7 <= n && !memcmp(s + i, "[CDATA[", 7)) {
                i += 7;
                Node *ac = adj_cur(p);
                if (ac && ac->ns != NS_HTML) TO(S_CDATA);
                else { sb_clear(&p->text); sb_put(&p->text, "[CDATA[", 7); TO(S_BOGUS_COMMENT); }
            } else { sb_clear(&p->text); TO(S_BOGUS_COMMENT); }
            break;
        case S_COMMENT_START:
            c = NEXT();
            if (c == '-') TO(S_COMMENT_START_DASH);
            else if (c == '>') { TO(S_DATA); emit_comment(p); }
            else { RECONSUME(); TO(S_COMMENT); }
            break;
        case S_COMMENT_START_DASH:
            c = NEXT();
            if (c == '-') TO(S_COMMENT_END);
            else if (c == '>') { TO(S_DATA); emit_comment(p); }
            else if (c == EOF_C) { emit_comment(p); RECONSUME(); TO(S_DATA); }
            else { sb_putc(&p->text, '-'); RECONSUME(); TO(S_COMMENT); }
            break;
        case S_COMMENT: {
            size_t k = i; while (k < n && s[k] != '<' && s[k] != '-' && s[k]) k++;
            if (k > i) { sb_put(&p->text, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == '<') { sb_putc(&p->text, '<'); TO(S_COMMENT_LT); }
            else if (c == '-') TO(S_COMMENT_END_DASH);
            else if (c == 0) put_fffd(&p->text);
            else if (c == EOF_C) { emit_comment(p); RECONSUME(); TO(S_DATA); }
            break;
        }
        case S_COMMENT_LT:
            c = NEXT();
            if (c == '!') { sb_putc(&p->text, '!'); TO(S_COMMENT_LT_BANG); }
            else if (c == '<') sb_putc(&p->text, '<');
            else { RECONSUME(); TO(S_COMMENT); }
            break;
        case S_COMMENT_LT_BANG:
            c = NEXT();
            if (c == '-') TO(S_COMMENT_LT_BANG_DASH); else { RECONSUME(); TO(S_COMMENT); }
            break;
        case S_COMMENT_LT_BANG_DASH:
            c = NEXT();
            if (c == '-') TO(S_COMMENT_LT_BANG_DASH_DASH); else { RECONSUME(); TO(S_COMMENT_END_DASH); }
            break;
        case S_COMMENT_LT_BANG_DASH_DASH:
            NEXT(); RECONSUME(); TO(S_COMMENT_END);
            break;
        case S_COMMENT_END_DASH:
            c = NEXT();
            if (c == '-') TO(S_COMMENT_END);
            else if (c == EOF_C) { emit_comment(p); RECONSUME(); TO(S_DATA); }
            else { sb_putc(&p->text, '-'); RECONSUME(); TO(S_COMMENT); }
            break;
        case S_COMMENT_END:
            c = NEXT();
            if (c == '>') { TO(S_DATA); emit_comment(p); }
            else if (c == '!') TO(S_COMMENT_END_BANG);
            else if (c == '-') sb_putc(&p->text, '-');
            else if (c == EOF_C) { emit_comment(p); RECONSUME(); TO(S_DATA); }
            else { sb_put(&p->text, "--", 2); RECONSUME(); TO(S_COMMENT); }
            break;
        case S_COMMENT_END_BANG:
            c = NEXT();
            if (c == '-') { sb_put(&p->text, "--!", 3); TO(S_COMMENT_END_DASH); }
            else if (c == '>') { TO(S_DATA); emit_comment(p); }
            else if (c == EOF_C) { emit_comment(p); RECONSUME(); TO(S_DATA); }
            else { sb_put(&p->text, "--!", 3); RECONSUME(); TO(S_COMMENT); }
            break;

        case S_DOCTYPE:
            c = NEXT();
            if (tok_ws(c)) TO(S_BEFORE_DT_NAME);
            else if (c == EOF_C) { new_doctype(p); p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else { RECONSUME(); TO(S_BEFORE_DT_NAME); }
            break;
        case S_BEFORE_DT_NAME:
            c = NEXT();
            if (tok_ws(c)) break;
            new_doctype(p);
            if (c == '>') { p->dt_force_quirks = true; TO(S_DATA); emit_doctype(p); }
            else if (c == EOF_C) { p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else { p->dt_has_name = true; if (c == 0) put_fffd(&p->dt_name); else sb_putc(&p->dt_name, (char)lc(c)); TO(S_DT_NAME); }
            break;
        case S_DT_NAME:
            c = NEXT();
            if (tok_ws(c)) TO(S_AFTER_DT_NAME);
            else if (c == '>') { TO(S_DATA); emit_doctype(p); }
            else if (c == 0) put_fffd(&p->dt_name);
            else if (c == EOF_C) { p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else sb_putc(&p->dt_name, (char)lc(c));
            break;
        case S_AFTER_DT_NAME:
            c = NEXT();
            if (tok_ws(c)) break;
            if (c == '>') { TO(S_DATA); emit_doctype(p); }
            else if (c == EOF_C) { p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else if (i + 5 <= n && !strncasecmp((const char *)s + i - 1, "public", 6)) { i += 5; TO(S_AFTER_DT_PUBLIC_KW); }
            else if (i + 5 <= n && !strncasecmp((const char *)s + i - 1, "system", 6)) { i += 5; TO(S_AFTER_DT_SYSTEM_KW); }
            else { p->dt_force_quirks = true; RECONSUME(); TO(S_BOGUS_DOCTYPE); }
            break;
        case S_AFTER_DT_PUBLIC_KW: case S_BEFORE_DT_PUBLIC: case S_AFTER_DT_SYSTEM_KW: case S_BEFORE_DT_SYSTEM: {
            int st = p->state; bool pub = st == S_AFTER_DT_PUBLIC_KW || st == S_BEFORE_DT_PUBLIC;
            c = NEXT();
            if (tok_ws(c)) { if (st == S_AFTER_DT_PUBLIC_KW) TO(S_BEFORE_DT_PUBLIC); else if (st == S_AFTER_DT_SYSTEM_KW) TO(S_BEFORE_DT_SYSTEM); }
            else if (c == '"' || c == '\'') {
                if (pub) { p->dt_has_pub = true; sb_clear(&p->dt_pub); TO(c == '"' ? S_DT_PUBLIC_DQ : S_DT_PUBLIC_SQ); }
                else { p->dt_has_sys = true; sb_clear(&p->dt_sys); TO(c == '"' ? S_DT_SYSTEM_DQ : S_DT_SYSTEM_SQ); }
            }
            else if (c == '>') { p->dt_force_quirks = true; TO(S_DATA); emit_doctype(p); }
            else if (c == EOF_C) { p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else { p->dt_force_quirks = true; RECONSUME(); TO(S_BOGUS_DOCTYPE); }
            break;
        }
        case S_DT_PUBLIC_DQ: case S_DT_PUBLIC_SQ: case S_DT_SYSTEM_DQ: case S_DT_SYSTEM_SQ: {
            int st = p->state; bool pub = st == S_DT_PUBLIC_DQ || st == S_DT_PUBLIC_SQ;
            int q = st == S_DT_PUBLIC_DQ || st == S_DT_SYSTEM_DQ ? '"' : '\'';
            SB *b = pub ? &p->dt_pub : &p->dt_sys;
            c = NEXT();
            if (c == q) TO(pub ? S_AFTER_DT_PUBLIC : S_AFTER_DT_SYSTEM);
            else if (c == 0) put_fffd(b);
            else if (c == '>') { p->dt_force_quirks = true; TO(S_DATA); emit_doctype(p); }
            else if (c == EOF_C) { p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else sb_putc(b, (char)c);
            break;
        }
        case S_AFTER_DT_PUBLIC: case S_BETWEEN_DT_IDS: {
            int st = p->state;
            c = NEXT();
            if (tok_ws(c)) { if (st == S_AFTER_DT_PUBLIC) TO(S_BETWEEN_DT_IDS); }
            else if (c == '>') { TO(S_DATA); emit_doctype(p); }
            else if (c == '"' || c == '\'') { p->dt_has_sys = true; sb_clear(&p->dt_sys); TO(c == '"' ? S_DT_SYSTEM_DQ : S_DT_SYSTEM_SQ); }
            else if (c == EOF_C) { p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else { p->dt_force_quirks = true; RECONSUME(); TO(S_BOGUS_DOCTYPE); }
            break;
        }
        case S_AFTER_DT_SYSTEM:
            c = NEXT();
            if (tok_ws(c)) break;
            if (c == '>') { TO(S_DATA); emit_doctype(p); }
            else if (c == EOF_C) { p->dt_force_quirks = true; emit_doctype(p); RECONSUME(); TO(S_DATA); }
            else { RECONSUME(); TO(S_BOGUS_DOCTYPE); }
            break;
        case S_BOGUS_DOCTYPE:
            c = NEXT();
            if (c == '>') { TO(S_DATA); emit_doctype(p); }
            else if (c == EOF_C) { emit_doctype(p); RECONSUME(); TO(S_DATA); }
            break;

        case S_CDATA: {
            size_t k = i; while (k < n && s[k] != ']') k++;
            if (k > i) { sb_put(&p->chars, (const char *)s + i, k - i); i = k; }
            c = NEXT();
            if (c == ']') TO(S_CDATA_BRACKET);
            else if (c == EOF_C) { emit_eof(p); return; }
            break;
        }
        case S_CDATA_BRACKET:
            c = NEXT();
            if (c == ']') TO(S_CDATA_END); else { sb_putc(&p->chars, ']'); RECONSUME(); TO(S_CDATA); }
            break;
        case S_CDATA_END:
            c = NEXT();
            if (c == ']') sb_putc(&p->chars, ']');
            else if (c == '>') TO(S_DATA);
            else { sb_put(&p->chars, "]]", 2); RECONSUME(); TO(S_CDATA); }
            break;
        }
    }
#undef NEXT
#undef RECONSUME
#undef TO
}

/* ---------------- entry points ---------------- */
void html_parser_feed(HtmlParser *p, const char *src, size_t len) { sb_put(&p->in, src, len); }

void html_parser_finish(HtmlParser *p) {
    /* input stream preprocessing: CR LF and lone CR become LF */
    if (p->in.n) {
        char *s = p->in.s; size_t o = 0;
        for (size_t i = 0; i < p->in.n; i++) {
            if (s[i] == '\r') { s[o++] = '\n'; if (i + 1 < p->in.n && s[i + 1] == '\n') i++; }
            else s[o++] = s[i];
        }
        p->in.n = o; s[o] = 0;
    }
    tokenize(p);
    if (p->ctx) return;
    Document *d = p->doc;
    d->quirks = p->quirks;
    d->html = d->head = d->body = NULL;
    for (Node *k = d->node.first; k; k = k->next) if (k->type == NODE_ELEMENT) { d->html = k; break; }
    if (d->html && is_html(d->html, A_html))
        for (Node *k = d->html->first; k; k = k->next) {
            if (!d->head && is_html(k, A_head)) d->head = k;
            if (!d->body && is_html(k, A_body)) d->body = k;
        }
}

void html_parse_ex(Document *d, const char *src, size_t len, const HtmlOpts *o) {
    HtmlParser *p = html_parser_new(d, NULL);
    p->san = o ? o->san : NULL; p->dsd = o ? o->dsd : true;
    if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) { src += 3; len -= 3; }
    html_parser_feed(p, src, len);
    html_parser_finish(p);
    html_parser_free(p);
}
void html_parse(Document *d, const char *src, size_t len) { html_parse_ex(d, src, len, NULL); }

Node *html_parse_fragment_ex(Document *d, Node *ctx, const char *src, size_t len, const HtmlOpts *o) {
    Node *tmp = NULL;
    if (!ctx || ctx->type != NODE_ELEMENT) ctx = tmp = node_new_element(d, A_body, NS_HTML);
    HtmlParser *p = html_parser_new(d, ctx);
    if (o) { p->san = o->san; p->dsd = o->dsd; }
    reset_mode(p);
    html_parser_feed(p, src, len);
    html_parser_finish(p);
    Node *root = p->root, *frag = node_new_fragment(d);
    while (root->first) node_append(frag, root->first);
    html_parser_free(p);
    node_free_tree(root);
    if (tmp) node_free_tree(tmp);
    return frag;
}
Node *html_parse_fragment(Document *d, Node *ctx, const char *src, size_t len) { return html_parse_fragment_ex(d, ctx, src, len, NULL); }
