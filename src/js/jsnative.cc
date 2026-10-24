/* Native functions (the `N` object) backing the JS DOM/Web API prelude */
#include "js_int.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>
#ifndef __APPLE__
#include <sys/random.h>
#endif
#include <vector>
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#endif

using FCI = v8::FunctionCallbackInfo<v8::Value>;
#define FN(nm) static void n_##nm(const FCI &a)
#define CTX                                              \
    JsCtx *c = jctx(a.GetIsolate());                     \
    v8::Isolate *iso = c->iso;                           \
    v8::Local<v8::Context> ctx = iso->GetCurrentContext(); \
    (void)ctx
#define RET(v) a.GetReturnValue().Set(v)
#define ARGN(var, i)                                                                          \
    Node *var = junwrap(a[i]);                                                                \
    if (!var) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, "parameter is not of type 'Node'"))); return; }
#define S(i) jcstr(iso, a[i])
#define NUM(i) a[i]->NumberValue(ctx).FromMaybe(0)
#define BOOL(i) a[i]->BooleanValue(iso)

static v8::Local<v8::Value> nstr(v8::Isolate *iso, const char *s) { return s ? v8::Local<v8::Value>(jstr(iso, s)) : v8::Local<v8::Value>(v8::Null(iso)); }
static v8::Local<v8::ArrayBuffer> mkab(v8::Isolate *iso, const void *p, size_t n) {
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, n);
    if (n) memcpy(ab->Data(), p, n);
    return ab;
}
static bool bytes_of(v8::Local<v8::Value> v, const char **p, size_t *n) {
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); *p = (const char *)ab->Data(); *n = ab->ByteLength(); return true; }
    if (v->IsArrayBufferView()) { auto vw = v.As<v8::ArrayBufferView>(); *p = (const char *)vw->Buffer()->Data() + vw->ByteOffset(); *n = vw->ByteLength(); return true; }
    return false;
}
static bool is_textish(Node *n) { return n->type == NODE_TEXT || n->type == NODE_COMMENT || n->type == NODE_CDATA || n->type == NODE_PI; }
static void adopt_tree(Node *n, Document *d) { for (Node *x = n; x; x = node_next_in_tree(x, n)) x->doc = d; }
#define ADOC(i) Document *od = c->doc; if (a.Length() > (i)) if (Node *dn_ = junwrap(a[i])) if (dn_->type == NODE_DOCUMENT) od = (Document *)dn_
static void clear_children(Node *n) {
    while (n->first) { Node *ch = n->first; node_remove(ch); node_free_tree(ch); }
}
static void mark_started(Node *root) {
    for (Node *x = root; x; x = node_next_in_tree(x, root)) {
        if (x->type != NODE_ELEMENT) continue;
        if (x->tag == A_script) x->flags |= NF_SCRIPT_STARTED;
        if (x->shadow_root) mark_started(x->shadow_root);
    }
}

void (*js_clip_set)(const char *text);
char *(*js_clip_get)(void);
FN(clipSet) { CTX; v8::String::Utf8Value u(iso, a[0]); if (js_clip_set) js_clip_set(*u ? *u : ""); }
FN(clipGet) { CTX; char *t = js_clip_get ? js_clip_get() : nullptr; RET(jstr(iso, t ? t : "")); free(t); }
unsigned char *(*js_clip_image)(char **mime, size_t *len);
/* clipImage(): the system clipboard's image as [mime, ArrayBuffer], else null */
FN(clipImage) {
    CTX; (void)ctx;
    char *mime = nullptr; size_t n = 0;
    unsigned char *p = js_clip_image ? js_clip_image(&mime, &n) : nullptr;
    if (!p || !mime) { free(p); free(mime); RET(v8::Null(iso)); return; }
    v8::Local<v8::Value> v[2] = { jstr(iso, mime), mkab(iso, p, n) };
    free(p); free(mime);
    RET(v8::Array::New(iso, v, 2));
}
FN(isNode) { CTX; RET(junwrap(a[0]) != nullptr); }
FN(type) { CTX; ARGN(n, 0); RET((int)n->type); }
FN(name) { CTX; ARGN(n, 0); RET(jstr(iso, n->type == NODE_ELEMENT || n->type == NODE_PI ? n->tag : n->type == NODE_DOCTYPE ? n->text : "")); }
FN(ns) { CTX; ARGN(n, 0); RET((int)n->ns); }
FN(parent) { CTX; ARGN(n, 0); RET(jwrap(c, n->parent)); }
FN(first) { CTX; ARGN(n, 0); RET(jwrap(c, n->first)); }
FN(last) { CTX; ARGN(n, 0); RET(jwrap(c, n->last)); }
FN(next) { CTX; ARGN(n, 0); RET(jwrap(c, n->next)); }
FN(prev) { CTX; ARGN(n, 0); RET(jwrap(c, n->prev)); }
FN(index) { CTX; ARGN(n, 0); int i = 0; for (Node *s = n->prev; s; s = s->prev) i++; RET(i); }
/* Character data is WTF-8: UTF-8 that also carries lone surrogates (ED A0..BF xx), so a script gets back the exact
   UTF-16 code units it set (CharacterData splits and joins surrogate pairs) */
static std::string wtf8(v8::Isolate *iso, v8::Local<v8::Value> v) {
    v8::Local<v8::String> s;
    if (v.IsEmpty() || !v->ToString(iso->GetCurrentContext()).ToLocal(&s)) return {};
    if (s->IsOneByte()) return jcstr(iso, s);
    uint32_t n = (uint32_t)s->Length();
    std::vector<uint16_t> u(n);
    s->Write(iso, 0, n, u.data());
    bool lone = false;
    for (uint32_t i = 0; i < n && !lone; i++) {
        if (u[i] >= 0xD800 && u[i] < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000) i++;
        else if (u[i] >= 0xD800 && u[i] < 0xE000) lone = true;
    }
    if (!lone) return jcstr(iso, s);
    std::string o; o.reserve(n * 3);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = u[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (u[++i] - 0xDC00);
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | c >> 6); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | c >> 12); o += (char)(0x80 | (c >> 6 & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | c >> 18); o += (char)(0x80 | (c >> 12 & 63)); o += (char)(0x80 | (c >> 6 & 63)); o += (char)(0x80 | (c & 63)); }
    }
    return o;
}
static v8::Local<v8::String> jstr_wtf8(v8::Isolate *iso, const char *s, int n) {
    bool sur = false;
    for (int i = 0; i + 1 < n && !sur; i++) sur = (unsigned char)s[i] == 0xED && (unsigned char)s[i + 1] >= 0xA0;
    if (!sur) return jstr(iso, s, n);
    std::vector<uint16_t> u; u.reserve((size_t)n);
    const unsigned char *p = (const unsigned char *)s, *e = p + n;
    while (p < e) {
        uint32_t c = *p; int k = c < 0x80 ? 0 : c >= 0xF0 && c < 0xF5 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : -1;
        if (k < 0 || e - p <= k) { u.push_back(0xFFFD); p++; continue; }
        c &= k == 0 ? 0x7F : k == 1 ? 0x1F : k == 2 ? 0x0F : 0x07;
        bool ok = true;
        for (int j = 1; j <= k; j++) { if ((p[j] & 0xC0) != 0x80) { ok = false; break; } c = c << 6 | (p[j] & 63); }
        if (!ok) { u.push_back(0xFFFD); p++; continue; }
        p += k + 1;
        if (c >= 0x10000) { c -= 0x10000; u.push_back((uint16_t)(0xD800 + (c >> 10))); u.push_back((uint16_t)(0xDC00 + (c & 0x3FF))); }
        else u.push_back((uint16_t)c);
    }
    v8::Local<v8::String> r;
    if (!v8::String::NewFromTwoByte(iso, u.data(), v8::NewStringType::kNormal, (int)u.size()).ToLocal(&r)) return v8::String::Empty(iso);
    return r;
}
FN(text) {
    CTX; ARGN(n, 0);
    if (is_textish(n)) { RET(jstr_wtf8(iso, n->text ? n->text : "", n->text ? (int)n->text_len : 0)); return; }
    size_t len = 0; char *t = node_text_content_n(n, &len); RET(jstr_wtf8(iso, t ? t : "", (int)len)); free(t);
}
FN(setText) {
    CTX; ARGN(n, 0); std::string s = wtf8(iso, a[1]);
    if (is_textish(n)) {
        node_set_character_data(n, s.data(), s.size());
        return;
    }
    clear_children(n);
    if (!s.empty()) node_append(n, node_new_text(c->doc, s.data(), s.size()));
}
FN(attr) { CTX; ARGN(n, 0); std::string k = S(1); RET(nstr(iso, n->type == NODE_ELEMENT || n->type == NODE_DOCTYPE ? node_attr(n, k.c_str()) : nullptr)); }
FN(setAttr) { CTX; ARGN(n, 0); std::string k = S(1), v = S(2); node_set_attr(n, atom(k.c_str()), v.c_str()); }
FN(rmAttr) { CTX; ARGN(n, 0); std::string k = S(1); if (node_has_attr(n, k.c_str())) node_remove_attr(n, k.c_str()); }
/* the DOM's exact attribute lookups (qualified name without case folding, or namespace + local name); an ns of null
   or "" is no namespace */
static const char *ns_atom(v8::Isolate *iso, v8::Local<v8::Value> v) {
    if (v->IsNullOrUndefined()) return nullptr;
    std::string s = jcstr(iso, v);
    return s.empty() ? nullptr : atom(s.c_str());
}
static bool attr_host(Node *n) { return n->type == NODE_ELEMENT || n->type == NODE_DOCTYPE; }
FN(attrQ) { CTX; ARGN(n, 0); std::string k = S(1); int i = attr_host(n) ? node_attr_qindex(n, k.c_str()) : -1; RET(nstr(iso, i >= 0 ? n->attrs[i].value : nullptr)); }
FN(attrNS) { CTX; ARGN(n, 0); std::string l = S(2); int i = attr_host(n) ? node_attr_ns_index(n, ns_atom(iso, a[1]), l.c_str()) : -1; RET(nstr(iso, i >= 0 ? n->attrs[i].value : nullptr)); }
FN(setAttrQ) { CTX; ARGN(n, 0); std::string k = S(1), v = S(2); if (attr_host(n)) node_set_attr_at(n, node_attr_qindex(n, k.c_str()), atom(k.c_str()), v.c_str(), nullptr, nullptr); }
FN(setAttrNS) {   /* (el, ns, prefix, local, value): an existing attribute keeps its prefix */
    CTX; ARGN(n, 0);
    if (!attr_host(n)) return;
    const char *ns = ns_atom(iso, a[1]);
    std::string l = S(3), v = S(4), q = l;
    const char *pfx = a[2]->IsNullOrUndefined() ? nullptr : atom(S(2).c_str());
    if (pfx) q = std::string(pfx) + ":" + l;
    node_set_attr_at(n, node_attr_ns_index(n, ns, l.c_str()), atom(q.c_str()), v.c_str(), ns, pfx);
}
FN(rmAttrQ) { CTX; ARGN(n, 0); std::string k = S(1); int i = attr_host(n) ? node_attr_qindex(n, k.c_str()) : -1; if (i >= 0) node_remove_attr_at(n, i); RET(i >= 0); }
FN(rmAttrNS) {   /* returns the removed attribute's qualified name, or null */
    CTX; ARGN(n, 0); std::string l = S(2);
    int i = attr_host(n) ? node_attr_ns_index(n, ns_atom(iso, a[1]), l.c_str()) : -1;
    if (i < 0) { a.GetReturnValue().SetNull(); return; }
    const char *q = n->attrs[i].name;
    node_remove_attr_at(n, i);
    RET(jstr(iso, q));
}
FN(attrsNS) {   /* [qualified name, value, namespace, prefix, ...] */
    CTX; ARGN(n, 0);
    v8::Local<v8::Array> r = v8::Array::New(iso, n->nattrs * 4);
    for (int i = 0; i < n->nattrs; i++) {
        const Attr *t = &n->attrs[i];
        (void)r->Set(ctx, 4 * i, jstr(iso, t->name));
        (void)r->Set(ctx, 4 * i + 1, jstr(iso, t->value ? t->value : ""));
        (void)r->Set(ctx, 4 * i + 2, nstr(iso, t->ns));
        (void)r->Set(ctx, 4 * i + 3, nstr(iso, t->pfx));
    }
    RET(r);
}
FN(attrs) {
    CTX; ARGN(n, 0);
    v8::Local<v8::Array> r = v8::Array::New(iso, n->nattrs * 2);
    for (int i = 0; i < n->nattrs; i++) {
        (void)r->Set(ctx, 2 * i, jstr(iso, n->attrs[i].name));
        (void)r->Set(ctx, 2 * i + 1, jstr(iso, n->attrs[i].value ? n->attrs[i].value : ""));
    }
    RET(r);
}
FN(insert) {
    CTX; ARGN(p, 0); ARGN(ch, 1);
    Node *ref = junwrap(a[2]);
    if (ch->type == NODE_DOCUMENT || p->type == NODE_TEXT || p->type == NODE_COMMENT) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, "HierarchyRequestError"))); return; }
    std::vector<Node *> added;
    if (ch->type == NODE_FRAGMENT) for (Node *x = ch->first; x; x = x->next) added.push_back(x);
    else added.push_back(ch);
    /* the inserted nodes are adopted; a fragment itself keeps its node document */
    for (Node *x : added) if (x->doc != p->doc) adopt_tree(x, p->doc);
    node_insert_before(p, ch, ref && ref->parent == p ? ref : nullptr);
    for (Node *x : added) js_run_inserted(c, x);
}
FN(adopt) { CTX; ARGN(n, 0); ARGN(d, 1); adopt_tree(n, d->doc); }
FN(newDoc) {
    CTX;
    Document *nd = doc_new("about:blank");
    std::string h = "<!doctype html><html><head>";
    if (!a[0]->IsNullOrUndefined()) {
        h += "<title>";
        for (char ch : S(0)) h += ch == '<' ? std::string("&lt;") : ch == '&' ? std::string("&amp;") : std::string(1, ch);
        h += "</title>";
    }
    h += "</head><body></body></html>";
    html_parse(nd, h.data(), h.size());
    c->docs.push_back(nd);
    RET(jwrap(c, &nd->node));
}
FN(remove) { CTX; ARGN(ch, 0); node_remove(ch); }
FN(create) { CTX; ADOC(2); std::string t = S(0); RET(jwrap(c, node_new_element(od, atom(t.c_str()), a[1]->Int32Value(ctx).FromMaybe(0)))); }
FN(textNode) { CTX; ADOC(1); std::string s = wtf8(iso, a[0]); RET(jwrap(c, node_new_text(od, s.data(), s.size()))); }
FN(comment) { CTX; ADOC(1); std::string s = wtf8(iso, a[0]); RET(jwrap(c, node_new_comment(od, s.data(), s.size()))); }
FN(frag) { CTX; ADOC(0); RET(jwrap(c, node_new_fragment(od))); }
FN(pi) { CTX; ADOC(2); std::string t = S(0), s = wtf8(iso, a[1]); RET(jwrap(c, node_new_pi(od, atom(t.c_str()), s.data(), s.size()))); }
FN(cdata) { CTX; ADOC(1); std::string s = S(0); RET(jwrap(c, node_new_cdata(od, s.data(), s.size()))); }
FN(doctype) {
    CTX; ADOC(3); std::string n = S(0), p = S(1), s = S(2);
    Node *d = node_new_doctype(od, n.c_str());
    node_set_attr(d, atom("publicId"), p.c_str()); node_set_attr(d, atom("systemId"), s.c_str());
    RET(jwrap(c, d));
}
FN(newXmlDoc) {
    CTX; Document *nd = doc_new("about:blank");
    nd->node.ns = NS_NONE;
    c->docs.push_back(nd);
    RET(jwrap(c, &nd->node));
}
/* xname(el, namespaceURI outside NS_* | null, prefix | null, case-kept local name | null): kept by clones */
FN(xname) {
    CTX; ARGN(n, 0); (void)iso;
    auto at = [&](int i) -> const char * { if (a.Length() <= i || !a[i]->IsString()) return nullptr; std::string v = S(i); return atom(v.c_str()); };
    if (n->type == NODE_ELEMENT) { n->xns = at(1); n->xpfx = at(2); n->xln = at(3); }
}
FN(charset) { CTX; ARGN(n, 0); RET(jstr(iso, enc_name(n->type == NODE_DOCUMENT ? ((Document *)n)->charset : n->doc ? n->doc->charset : 0))); }
/* the document's Content-Security-Policy and -Report-Only response headers */
FN(csp) {
    CTX; ARGN(n, 0);
    Document *d = n->type == NODE_DOCUMENT ? (Document *)n : n->doc;
    v8::Local<v8::Value> v[2] = { nstr(iso, d ? d->csp : nullptr), nstr(iso, d ? d->csp_ro : nullptr) };
    RET(v8::Array::New(iso, v, 2));
}
/* Trusted Types and CSP: string compilation goes through the ttEval hook (evalGuard), inline scripts through
   ttScript (ttOn); codeLike makes an object V8 treats as code (a TrustedScript) */
FN(evalGuard) { CTX; ctx->AllowCodeGenerationFromStrings(!BOOL(0)); }
FN(ttOn) { CTX; c->tt_on = BOOL(0); }
FN(codeLike) {
    CTX;
    if (c->code_like.IsEmpty()) { v8::Local<v8::ObjectTemplate> t = v8::ObjectTemplate::New(iso); t->SetCodeLike(); c->code_like.Reset(iso, t); }
    v8::Local<v8::Object> o;
    if (c->code_like.Get(iso)->NewInstance(ctx).ToLocal(&o)) RET(o);
}
/* lazySrc(name): a src/js/lazy module's source (the editing commands), compiled by the prelude on first use */
static const char kLazyEditing[] =
#include "editing_lazy.inc"
;
FN(lazySrc) {
    CTX;
    if (jcstr(iso, a[0]) == "editing") RET(v8::String::NewFromUtf8(iso, kLazyEditing, v8::NewStringType::kNormal, (int)sizeof kLazyEditing - 1).ToLocalChecked());
}
FN(ctype) { CTX; ARGN(n, 0); if (n->type == NODE_DOCUMENT && ((Document *)n)->content_type) RET(jstr(iso, ((Document *)n)->content_type)); else a.GetReturnValue().SetNull(); }
/* parseXml(xmlDoc, source): the document's content; its scripts never run */
FN(parseXml) {
    CTX; ARGN(n, 0); (void)iso;
    if (n->type != NODE_DOCUMENT || n->first) return;
    std::string src = S(1);
    xml_parse((Document *)n, src.data(), src.size());
    mark_started(n);
}
FN(ownerDoc) { CTX; ARGN(n, 0); RET(n->type == NODE_DOCUMENT || !n->doc ? v8::Local<v8::Value>(v8::Null(iso)) : jwrap(c, &n->doc->node)); }
FN(html) { CTX; ARGN(n, 0); char *s = node_serialize(n, BOOL(1)); RET(jstr(iso, s ? s : "")); free(s); }
FN(setHTML) {
    CTX; ARGN(n, 0); std::string s = S(1);
    Node *target = n;
    if (n->type == NODE_ELEMENT && n->tag == A_template) {
        if (!n->template_content) { n->template_content = node_new_fragment(c->doc); n->template_content->refcount = 1; n->template_content->flags |= NF_INERT; }
        target = n->template_content;
    }
    clear_children(target);
    Node *ctxn = n->type == NODE_ELEMENT ? n : n->host ? n->host : nullptr;
    Node *f = html_parse_fragment(c->doc, ctxn ? ctxn : c->doc->body, s.data(), s.size());
    if (!f) return;
    mark_started(f);
    node_append(target, f);
    node_free_tree(f);
}
/* (context, html, written): written, from document.write() inside a script, queues the
   fragment's classic scripts to run after that script (js_run_script) */
FN(parseFrag) {
    CTX; Node *cx = junwrap(a[0]); std::string s = S(1);
    if (!cx || cx->type != NODE_ELEMENT) cx = c->doc->body;
    Node *f = html_parse_fragment(c->doc, cx, s.data(), s.size());
    if (!f) { RET(jwrap(c, f)); return; }
    if (a.Length() > 2 && a[2]->IsTrue() && c->current_script)
        for (Node *x = f; x; x = node_next_in_tree(x, f))
            if (x->type == NODE_ELEMENT && x->tag == A_script && x->ns == NS_HTML && jsg_classic_script(x)) { jwrap(c, x); c->written.push_back(x); }
    mark_started(f);
    RET(jwrap(c, f));
}
FN(docReparse) { CTX; std::string s = S(0); js_doc_reparse(c, s.data(), s.size()); }
FN(parseDoc) {
    CTX; std::string s = S(0);
    Document *nd = doc_new(c->doc->url);
    HtmlOpts o = { nullptr, false };   /* DOMParser and XHR documents get no declarative shadow roots */
    html_parse_ex(nd, s.data(), s.size(), &o);
    mark_started(&nd->node);
    c->docs.push_back(nd);
    RET(jwrap(c, &nd->node));
}
static void throw_dom(v8::Isolate *iso, v8::Local<v8::Context> ctx, const std::string &msg, const char *name) {
    v8::Local<v8::Value> ctor;
    if (ctx->Global()->Get(ctx, jstr(iso, "DOMException")).ToLocal(&ctor) && ctor->IsFunction()) {
        v8::Local<v8::Value> args[2] = { jstr(iso, msg.c_str()), jstr(iso, name) };
        v8::Local<v8::Object> ex;
        if (ctor.As<v8::Function>()->NewInstance(ctx, 2, args).ToLocal(&ex)) { iso->ThrowException(ex); return; }
    }
    iso->ThrowException(v8::Exception::SyntaxError(jstr(iso, msg.c_str())));
}
FN(query) {
    CTX; ARGN(root, 0); std::string sel = S(1); bool all = BOOL(2);
    Node **out = nullptr;
    int k = jsg_query(root, sel.c_str(), all, &out);
    if (k < 0) { throw_dom(iso, ctx, "'" + sel + "' is not a valid selector.", "SyntaxError"); return; }
    if (!all) { RET(jwrap(c, k ? out[0] : nullptr)); free(out); return; }
    v8::Local<v8::Array> r = v8::Array::New(iso, k);
    for (int i = 0; i < k; i++) (void)r->Set(ctx, i, jwrap(c, out[i]));
    free(out);
    RET(r);
}
FN(matches) {
    CTX; ARGN(n, 0); std::string sel = S(1); bool ok = false;
    Node *scope = a.Length() > 2 ? junwrap(a[2]) : n;   /* :scope is the element matches() or closest() was called on */
    bool m = n->type == NODE_ELEMENT && jsg_matches(n, sel.c_str(), scope ? scope : n, &ok);
    if (n->type == NODE_ELEMENT && !ok) { throw_dom(iso, ctx, "'" + sel + "' is not a valid selector.", "SyntaxError"); return; }
    RET(m);
}
FN(byId) { CTX; std::string id = S(0); RET(jwrap(c, doc_get_element_by_id(c->doc, id.c_str()))); }
FN(clone) { CTX; ARGN(n, 0); RET(jwrap(c, node_clone(n, BOOL(1), c->doc))); }
FN(doc) { CTX; RET(jwrap(c, &c->doc->node)); }
FN(contains) { CTX; ARGN(x, 0); ARGN(y, 1); RET(node_is_inclusive_ancestor(x, y)); }
FN(version) { CTX; ARGN(n, 0); RET(v8::Number::New(iso, n->doc ? (double)n->doc->dom_version : 0)); }
FN(inert) { CTX; ARGN(n, 0); while (n->parent) n = n->parent; RET((n->flags & NF_INERT) != 0); }
FN(connected) { CTX; ARGN(n, 0); RET(n->type == NODE_DOCUMENT || (n->flags & NF_CONNECTED) != 0); }
FN(host) { CTX; ARGN(n, 0); RET(jwrap(c, n->host)); }
/* realm hooks for cross-realm events: each prelude stores its own on its global; realmOf finds an object's through its
   creation context, for same-origin realms only */
static v8::Local<v8::Private> realm_key(v8::Isolate *iso) { return v8::Private::ForApi(iso, jstr(iso, "lumen:realm")); }
FN(setRealm) { CTX; (void)ctx->Global()->SetPrivate(ctx, realm_key(iso), a[0]); }
FN(realmOf) {
    CTX;
    if (!a[0]->IsObject()) return;
    v8::Local<v8::Context> cx;
    if (!a[0].As<v8::Object>()->GetCreationContext(iso).ToLocal(&cx)) return;
    if (cx != ctx && !cx->GetSecurityToken()->StrictEquals(ctx->GetSecurityToken())) return;
    v8::Local<v8::Value> v;
    if (cx->Global()->GetPrivate(cx, realm_key(iso)).ToLocal(&v) && !v->IsUndefined()) RET(v);
}
/* attachShadow(host, flags): flags are NF_SR_* >> 22 (closed, delegates, serializable, clonable, manual, declarative) */
FN(attachShadow) {
    CTX; ARGN(n, 0);
    uint32_t f = (uint32_t)(a.Length() > 1 ? a[1]->Int32Value(ctx).FromMaybe(0) : 0) << 22;
    Node *sr = node_attach_shadow(n, f);
    if (sr) doc_mark_dirty(c->doc, n);
    RET(jwrap(c, sr));
}
FN(shadowOf) { CTX; ARGN(n, 0); RET(jwrap(c, n->type == NODE_ELEMENT ? n->shadow_root : nullptr)); }
FN(srFlags) { CTX; ARGN(n, 0); (void)ctx; RET(v8::Integer::New(iso, (int)((n->flags >> 22) & 0x3f))); }
FN(srSetFlags) {
    CTX; ARGN(n, 0);
    uint32_t set = (uint32_t)a[1]->Int32Value(ctx).FromMaybe(0) << 22, clr = (uint32_t)a[2]->Int32Value(ctx).FromMaybe(0) << 22;
    if (n->type == NODE_FRAGMENT && n->host) n->flags = (n->flags & ~clr) | set;
}
/* srRef(root[, value|null]): a shadow root's referenceTarget, kept in its (otherwise unused) text */
FN(srRef) {
    CTX; ARGN(n, 0); (void)ctx;
    if (n->type != NODE_FRAGMENT || !n->host) return;
    if (a.Length() > 1) {
        free(n->text); n->text = nullptr; n->text_len = 0;
        if (!a[1]->IsNull() && !a[1]->IsUndefined()) { v8::String::Utf8Value s(iso, a[1]); n->text = xstrdup(*s ? *s : ""); n->text_len = strlen(n->text); }
        return;
    }
    if (n->text) RET(jstr(iso, n->text)); else RET(v8::Null(iso));
}
/* getHTML(node, serializableShadowRoots, [shadowRoots]) */
FN(getHTML) {
    CTX; ARGN(n, 0);
    std::vector<Node *> roots;
    if (a.Length() > 2 && a[2]->IsArray()) {
        v8::Local<v8::Array> r = a[2].As<v8::Array>();
        for (uint32_t i = 0; i < r->Length(); i++) { v8::Local<v8::Value> v; if (r->Get(ctx, i).ToLocal(&v)) if (Node *x = junwrap(v)) roots.push_back(x); }
    }
    char *s = node_serialize_ex(n, false, BOOL(1), roots.data(), (int)roots.size());
    RET(jstr(iso, s ? s : "")); free(s);
}
/* a canonical config from the Sanitizer prelude: lists of names are flat [namespace|null, name, ...] arrays, elements
   [namespace|null, name, attributes|null, removeAttributes|null, ...], processing instructions [target, ...] */
static SanCfg *san_from_js(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> v) {
    if (!v->IsObject()) return nullptr;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    SanCfg *c = san_new();
    auto get = [&](v8::Local<v8::Object> x, const char *k) { v8::Local<v8::Value> r; if (!x->Get(ctx, jstr(iso, k)).ToLocal(&r)) r = v8::Undefined(iso); return r; };
    auto at = [&](v8::Local<v8::Array> x, uint32_t i) { v8::Local<v8::Value> r; if (!x->Get(ctx, i).ToLocal(&r)) r = v8::Undefined(iso); return r; };
    auto sv = [&](v8::Local<v8::Value> x, std::string &out) { if (!x->IsString()) return false; out = jcstr(iso, x); return true; };
    auto names = [&](v8::Local<v8::Value> arr, int step, const std::function<void(const char *, const char *, v8::Local<v8::Array>, uint32_t)> &fn) {
        if (!arr->IsArray()) return false;
        v8::Local<v8::Array> x = arr.As<v8::Array>();
        for (uint32_t i = 0; i + step <= x->Length(); i += step) {
            std::string ns, name; bool has_ns = sv(at(x, i), ns);
            if (step == 1) { name = ns; has_ns = false; } else sv(at(x, i + 1), name);
            fn(has_ns ? ns.c_str() : nullptr, name.c_str(), x, i);
        }
        return true;
    };
    static const char *const keys[] = { "elements", "removeElements", "replaceWithChildrenElements", "attributes", "removeAttributes", "processingInstructions", "removeProcessingInstructions" };
    for (int l = 0; l < SAN_NLISTS; l++) {
        int step = l == SAN_ELEMENTS ? 4 : l >= SAN_PIS ? 1 : 2;
        bool had = names(get(o, keys[l]), step, [&](const char *ns, const char *name, v8::Local<v8::Array> x, uint32_t i) {
            san_add(c, l, ns, name);
            if (l != SAN_ELEMENTS) return;
            for (int r = 0; r < 2; r++) {
                v8::Local<v8::Value> al = at(x, i + 2 + r);
                if (!al->IsArray()) continue;
                san_elem_attr(c, ns, name, r == 1, nullptr, nullptr);
                names(al, 2, [&](const char *ans, const char *an, v8::Local<v8::Array>, uint32_t) { san_elem_attr(c, ns, name, r == 1, ans, an); });
            }
        });
        if (had) san_add(c, l, nullptr, nullptr);
    }
    san_flags(c, get(o, "comments")->IsTrue(), get(o, "dataAttributes")->IsTrue(), get(o, "javascriptURLs")->IsTrue());
    return c;
}
/* parseFragEx(context, html, config|null, allowDeclarativeShadowRoots): the fragment for setHTML() and its kin */
FN(parseFragEx) {
    CTX; Node *cx = junwrap(a[0]); std::string s = S(1);
    if (!cx || cx->type != NODE_ELEMENT) cx = c->doc->body;
    SanCfg *san = san_from_js(iso, ctx, a[2]);
    HtmlOpts o = { san, BOOL(3) };
    Node *f = html_parse_fragment_ex(c->doc, cx, s.data(), s.size(), &o);
    san_free(san);
    if (f && !BOOL(4)) mark_started(f);
    RET(jwrap(c, f));
}
/* parseDocEx(html, config|null): Document.parseHTML(Unsafe)'s document, declarative shadow roots allowed */
FN(parseDocEx) {
    CTX; std::string s = S(0);
    SanCfg *san = san_from_js(iso, ctx, a[1]);
    HtmlOpts o = { san, true };
    Document *nd = doc_new(c->doc->url);
    html_parse_ex(nd, s.data(), s.size(), &o);
    san_free(san);
    mark_started(&nd->node);
    c->docs.push_back(nd);
    RET(jwrap(c, &nd->node));
}
FN(templateContent) {
    CTX; ARGN(n, 0);
    if (!n->template_content) { n->template_content = node_new_fragment(c->doc); n->template_content->refcount = 1; n->template_content->flags |= NF_INERT; }
    RET(jwrap(c, n->template_content));
}
FN(rect) {
    CTX; ARGN(n, 0); float r[4];
    // An observer may belong to the parent realm while its target is in a frame.
    JsCtx *owner = n->doc != c->doc ? owner_ctx(n->doc) : c;
    if (owner && owner->host.sync) owner->host.sync(owner->host.ud, n->doc, true);
    if (!jsg_rect(n, r)) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Array> o = v8::Array::New(iso, 4);
    for (int i = 0; i < 4; i++) (void)o->Set(ctx, i, v8::Number::New(iso, r[i]));
    RET(o);
}
/* textRects(node, start, end): a text node's UTF-16 range, or a text control's value codepoints; [x, y, w, h, ...] */
FN(textRects) {
    CTX; ARGN(n, 0);
    JsCtx *owner = n->doc != c->doc ? owner_ctx(n->doc) : c;
    if (owner && owner->host.sync) owner->host.sync(owner->host.ud, n->doc, true);
    int s = (int)NUM(1), e = (int)NUM(2);
    float r[256 * 4];
    int k = n->type == NODE_TEXT ? jsg_text_rects(n, s, e, r, 256) : n->type == NODE_ELEMENT ? jsg_ctl_rects(n, s, e, r, 256) : 0;
    v8::Local<v8::Array> o = v8::Array::New(iso, k * 4);
    for (int i = 0; i < k * 4; i++) (void)o->Set(ctx, i, v8::Number::New(iso, r[i]));
    RET(o);
}
FN(vrect) {
    CTX; ARGN(n, 0); float r[4];
    JsCtx *owner = n->doc != c->doc ? owner_ctx(n->doc) : c;
    if (owner && owner->host.sync) owner->host.sync(owner->host.ud, n->doc, true);
    float w = 0, h = 0, sx = 0, sy = 0, dpr = 1;
    if (owner && owner->host.viewport) owner->host.viewport(owner->host.ud, &w, &h, &sx, &sy, &dpr);
    if (!jsg_vrect(n, r, sx, sy)) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Array> o = v8::Array::New(iso, 4);
    for (int i = 0; i < 4; i++) (void)o->Set(ctx, i, v8::Number::New(iso, r[i]));
    RET(o);
}
FN(vclip) {
    CTX; ARGN(n, 0); float r[4];
    JsCtx *owner = n->doc != c->doc ? owner_ctx(n->doc) : c;
    if (owner && owner->host.sync) owner->host.sync(owner->host.ud, n->doc, true);
    float w = 0, h = 0, sx = 0, sy = 0, dpr = 1;
    if (owner && owner->host.viewport) owner->host.viewport(owner->host.ud, &w, &h, &sx, &sy, &dpr);
    if (!jsg_vclip(n, a.Length() > 1 ? junwrap(a[1]) : nullptr, r, sx, sy)) { RET(v8::Null(iso)); return; }
    if (std::isinf(r[0]) && std::isinf(r[1]) && std::isinf(r[2]) && std::isinf(r[3])) { RET(v8::Undefined(iso)); return; }
    v8::Local<v8::Array> o = v8::Array::New(iso, 4);
    for (int i = 0; i < 4; i++) (void)o->Set(ctx, i, v8::Number::New(iso, r[i]));
    RET(o);
}
FN(scrollPos) {
    CTX; ARGN(n, 0); float r[4];
    if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
    if (!jsg_scroll(n, r)) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Array> o = v8::Array::New(iso, 4);
    for (int i = 0; i < 4; i++) (void)o->Set(ctx, i, v8::Number::New(iso, r[i]));
    RET(o);
}
FN(setScroll) { CTX; ARGN(n, 0); if (c->host.sync) c->host.sync(c->host.ud, c->doc, true); RET(jsg_set_scroll(n, (float)NUM(1), (float)NUM(2))); }
/* resolved values that read the box (used sizes, absolutized insets) need a current layout */
static bool computed_needs_layout(const std::string &p) {
    static const char *const l[] = { "width", "height", "top", "right", "bottom", "left", "inset", "margin", "padding", "block-size", "inline-size", "grid-template", "transform-origin", "perspective-origin" };
    for (const char *x : l) if (!p.compare(0, strlen(x), x)) return true;
    return false;
}
FN(computed) { CTX; ARGN(n, 0); std::string p = S(1); if (c->host.sync) c->host.sync(c->host.ud, c->doc, computed_needs_layout(p)); char *v = jsg_computed(n, p.c_str()); RET(nstr(iso, v)); free(v); }
FN(trTake) {
    CTX; v8::Local<v8::Array> out = v8::Array::New(iso); uint32_t k = 0;
    for (size_t i = 0; i < c->anims.size();)
        if (c->anims[i].type == "lumen-transition" || c->anims[i].type == "lumen-animation") {
            v8::Local<v8::Value> job[3] = { c->anims[i].target.Get(iso), jstr(iso, c->anims[i].name.c_str()), jstr(iso, c->anims[i].type.c_str()) };
            (void)out->Set(ctx, k++, v8::Array::New(iso, job, 3));
            c->anims.erase(c->anims.begin() + (long)i);
        } else i++;
    RET(out);
}
FN(setAnimStyle) { CTX; ARGN(n, 0); (void)c; std::string s = S(1); jsg_set_anim_style(n, s.c_str()); }
FN(value) { CTX; ARGN(n, 0); RET(nstr(iso, n->value_override)); }
static int cp_count(const char *s) { int k = 0; for (; s && *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) k++; return k; }
FN(setValue) {
    CTX; ARGN(n, 0);
    char *v = a[1]->IsNullOrUndefined() ? nullptr : xstrdup(S(1).c_str());
    Document *d = n->doc;
    if (d && d->csel_n == n) {   /* a changed value puts the caret at its end, as in Chrome */
        const char *old = n->value_override ? n->value_override : node_attr(n, "value");
        int len = cp_count(v);
        if (strcmp(old ? old : "", v ? v : "")) d->csel[0] = d->csel[1] = len;
        else { d->csel[0] = d->csel[0] < len ? d->csel[0] : len; d->csel[1] = d->csel[1] < len ? d->csel[1] : len; }
        d->paint_version++;
    }
    free(n->value_override); n->value_override = v; doc_mark_state_dirty(n->doc, n, true);
}
/* <input>/<textarea> selection as [anchor, focus] codepoints of the value; null when never set */
FN(ctlSel) {
    CTX; ARGN(n, 0);
    Document *d = n->doc;
    if (!d || d->csel_n != n) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Array> o = v8::Array::New(iso, 2);
    for (int i = 0; i < 2; i++) (void)o->Set(ctx, i, v8::Integer::New(iso, d->csel[i]));
    RET(o);
}
FN(setCtlSel) {
    CTX; ARGN(n, 0);
    Document *d = n->doc;
    if (!d) return;
    d->csel_n = n; d->csel[0] = (int)NUM(1); d->csel[1] = (int)NUM(2); d->paint_version++;
}
FN(checked) {
    CTX; ARGN(n, 0);
    RET(n->checked_override ? n->checked_override > 0 : node_has_attr(n, n->tag && !strcmp(n->tag, "option") ? "selected" : "checked"));
}
FN(setChecked) {
    CTX; ARGN(n, 0); bool b = BOOL(1);
    n->checked_override = b ? 1 : -1;
    if (b) n->flags |= NF_CHECKED; else n->flags &= ~(uint32_t)NF_CHECKED;
    doc_mark_state_dirty(n->doc, n, true);
}
FN(pickFiles) {
    CTX;
    if (!a[0]->IsFunction()) return;
    c->pick_cb.Reset(iso, a[0].As<v8::Function>());
    std::string acc = S(2);
    js_pick_begin(c, BOOL(1), acc.c_str());
}
FN(modal) {
    CTX; ARGN(n, 0);
    if (a.Length() > 1) {
        node_set_modal(n, BOOL(1));
    }
    RET(!!(n->flags & NF_MODAL));
}
/* fullscreen(el, on, window): marks el :fullscreen or not, and the <iframe>s holding this document with it;
   window asks the browser to fill the window with the page or put it back */
FN(fullscreen) {
    CTX; ARGN(n, 0);
    bool on = BOOL(1), win = BOOL(2);
    if (on) n->flags |= NF_FULLSCREEN; else n->flags &= ~(uint32_t)NF_FULLSCREEN;
    if (n->doc) doc_mark_dirty(n->doc, n);
    c->fullscreen = win;
    JsCtx *r = c;
    for (; r->parent; r = r->parent) if (Node *f = r->frame_el) {
        if (win) f->flags |= NF_FULLSCREEN; else f->flags &= ~(uint32_t)NF_FULLSCREEN;
        if (f->doc) doc_mark_dirty(f->doc, f);
    }
    if (r->host.fullscreen) r->host.fullscreen(r->host.ud, win);
}
FN(focus) {
    CTX; Node *n = junwrap(a[0]); Node *old = c->doc->focus;
    if (old == n) return;
    for (Node *p = n; p; p = p->parent ? p->parent : p->host) if (p->type == NODE_ELEMENT && node_has_attr(p, "inert")) return;   /* inert content can't be focused */
    doc_set_focus(c->doc, n);
    if (old) jfire(c, old, "blur");
    if (n) jfire(c, n, "focus");
}
FN(active) { CTX; RET(jwrap(c, c->doc->focus)); }
FN(cookie) { CTX; char *s = cookies_get_document(c->doc->url); RET(jstr(iso, s ? s : "")); free(s); }
FN(setCookie) { CTX; std::string s = S(0); cookies_set_document(c->doc->url, s.c_str()); }
FN(url) { CTX; RET(jstr(iso, c->doc->url ? c->doc->url : "about:blank")); }
FN(docBase) { CTX; RET(jstr(iso, c->doc->url ? doc_base(c->doc) : "about:blank")); }
FN(setUrl) {
    CTX; std::string u = S(0);
    free(c->doc->url); c->doc->url = xstrdup(u.c_str());
    if (c->host.set_url) c->host.set_url(c->host.ud, u.c_str(), BOOL(1));
}
FN(navigate) { CTX; std::string u = S(0); if (c->parent) { frame_nav(c, u.c_str()); return; } if (c->host.navigate) c->host.navigate(c->host.ud, u.c_str()); }
FN(navigatePost) {
    CTX; std::string u = S(0), b = js_body_bytes(iso, a[1]), t = S(2);
    if (c->host.navigate_post) c->host.navigate_post(c->host.ud, u.c_str(), b.data(), b.size(), t.c_str());
    else if (c->host.navigate) c->host.navigate(c->host.ud, u.c_str());
}
FN(histGo) { CTX; if (c->host.history_go) c->host.history_go(c->host.ud, a[0]->Int32Value(ctx).FromMaybe(0)); }
/* frames reach the host only for testdriver ("td?"), which the top context's host answers (LUMEN_TESTDRIVER runs) */
FN(internal) {
    CTX; std::string s = S(0); JsCtx *h = c;
    if (c->parent) { if (s.compare(0, 3, "td?")) return; while (h->parent) h = h->parent; }
    if (!h->host.internal) return;
    char *r = h->host.internal(h->host.ud, s.c_str()); if (r) { RET(jstr(iso, r)); free(r); }
}
FN(histLen) { CTX; RET(c->host.history_len ? c->host.history_len(c->host.ud) : 1); }
FN(timer) {
    CTX;
    if (!a[0]->IsFunction()) return;
    uint32_t id = c->next_timer++;
    Timer &t = c->timers[id];
    t.fn.Reset(iso, a[0].As<v8::Function>());
    t.repeat = BOOL(2);
    t.interval = std::max(NUM(1), t.repeat ? 4.0 : 0.0);
    // a[3]: a posted task (MessagePort), run as soon as possible like Chrome's.
    // Timers nested five deep are clamped to 4 ms, as HTML specifies.
    t.nest = a.Length() > 3 && BOOL(3) ? 0 : c->timer_nest + 1;
    if (t.nest > 5) t.interval = std::max(t.interval, 4.0);
    t.due = now_ms() + t.interval;
    c->timer_due.insert({ t.due, id });
    RET(id);
}
FN(clearTimer) { CTX; auto it = c->timers.find((uint32_t)NUM(0)); if (it != c->timers.end()) c->timer_erase(it); }
FN(raf) {
    CTX;
    if (!a[0]->IsFunction()) return;
    uint32_t id = c->next_raf++;
    c->rafs[id].Reset(iso, a[0].As<v8::Function>());
    RET(id);
}
FN(cancelRaf) { CTX; c->rafs.erase((uint32_t)NUM(0)); }
FN(now) { CTX; RET(now_ms() - c->t0); }

static NetRequest *mkreq(JsCtx *c, const FCI &a) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    std::string m = S(0), u = S(1);
    NetRequest *rq = net_request_new(m.c_str(), u.c_str());
    bool referer = false;
    if (a[2]->IsArray()) {
        v8::Local<v8::Array> h = a[2].As<v8::Array>();
        for (uint32_t i = 0; i + 1 < h->Length(); i += 2) {
            v8::Local<v8::Value> k, v;
            if (!h->Get(ctx, i).ToLocal(&k) || !h->Get(ctx, i + 1).ToLocal(&v)) continue;
            std::string ks = jcstr(iso, k), vs = jcstr(iso, v);
            if (net_request_control(rq, ks.c_str(), vs.c_str())) continue;
            if (str_ieq(ks.c_str(), "referer")) { referer = true; if (vs.empty()) continue; }   /* an empty Referer: none sent */
            headers_add(&rq->headers, ks.c_str(), vs.c_str());
        }
    }
    if (!referer && c->doc->url && !strncmp(c->doc->url, "http", 4)) headers_add(&rq->headers, "Referer", c->doc->url);
    const char *p; size_t n;
    if (a.Length() > 3 && bytes_of(a[3], &p, &n) && n) { rq->body = (char *)xmalloc(n); memcpy(rq->body, p, n); rq->body_len = n; }
    return rq;
}
static void resp_args(v8::Isolate *iso, NetResponse *r, v8::Local<v8::Value> out[6]) {
    bool err = !r || r->status == 0 || r->error;
    out[0] = v8::Integer::New(iso, r ? r->status : 0);
    out[1] = jstr(iso, r && r->status_text ? r->status_text : "");
    out[2] = jstr(iso, r && r->url ? r->url : "");
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Array> h = v8::Array::New(iso, r ? (int)r->headers.n * 2 : 0);
    if (r) for (size_t i = 0; i < r->headers.n; i++) {
        (void)h->Set(ctx, (uint32_t)(2 * i), jstr(iso, r->headers.v[i].name));
        (void)h->Set(ctx, (uint32_t)(2 * i + 1), jstr(iso, r->headers.v[i].value));
    }
    out[3] = h;
    out[4] = mkab(iso, r && r->body ? r->body : "", r && r->body ? r->body_len : 0);
    out[5] = err ? v8::Local<v8::Value>(jstr(iso, r && r->error ? r->error : "network error")) : v8::Local<v8::Value>(v8::False(iso));
}
static void fetch_done(NetRequest *req, NetResponse *r, void *ud) {
    (void)req;
    Fetch *f = static_cast<Fetch *>(ud);
    JsCtx *c = f->c;
    if (!c) { delete f; return; }
    c->fetches.erase(f->id);
    JS_ENTER(c);
#if defined(__ANDROID__) && !defined(NDEBUG)
    struct Slow { double t = now_ms(); bool script; size_t kb; ~Slow() { if (now_ms() - t >= 200) __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "fetch_done script=%d kb=%zu ms=%.0f", script, kb, now_ms() - t); } }
        slow{ now_ms(), f->script != nullptr, r && r->body ? r->body_len >> 10 : 0 };
#endif
    if (f->script) {
        if (r && !r->error && r->status >= 200 && r->status < 300) js_run_script(c, f->script, r->body ? r->body : "", r->body ? r->body_len : 0, r->url ? r->url : "");
        else jfire(c, f->script, "error");
    } else {
        v8::Local<v8::Value> argv[6];
        resp_args(iso, r, argv);
        (void)jcall(c, f->cb.Get(iso), v8::Undefined(iso), 6, argv);
    }
    f->cb.Reset(); f->head.Reset(); f->chunk.Reset();
    delete f;
}
static void fetch_head(NetRequest *req, NetResponse *r, void *ud) {
    (void)req;
    Fetch *f = static_cast<Fetch *>(ud);
    JsCtx *c = f->c;
    if (!c || f->head.IsEmpty()) return;
    JS_ENTER(c);
    v8::Local<v8::Value> argv[6];
    resp_args(iso, r, argv);
    (void)jcall(c, f->head.Get(iso), v8::Undefined(iso), 4, argv);
}
static void fetch_chunk(NetRequest *req, const char *d, size_t n, void *ud) {
    (void)req;
    Fetch *f = static_cast<Fetch *>(ud);
    JsCtx *c = f->c;
    if (!c || f->chunk.IsEmpty()) return;
    JS_ENTER(c);
    v8::Local<v8::Value> argv[1] = { mkab(iso, d, n) };
    (void)jcall(c, f->chunk.Get(iso), v8::Undefined(iso), 1, argv);
}
static uint64_t start_fetch(JsCtx *c, NetRequest *rq, Fetch *f) {
    rq->done = fetch_done;
    rq->ud = f;
    f->id = net_fetch(rq);
    c->fetches[f->id] = f;
    return f->id;
}
#define MP                                                        \
    auto mpit = c->players.find((uint32_t)NUM(0));                \
    if (mpit == c->players.end()) return;                         \
    MediaPlayer *mp = mpit->second
FN(waOpen) { CTX; RET((double)wa_open(c)); }
FN(waPush) {
    CTX; const char *p; size_t n;
    if (bytes_of(a[1], &p, &n)) wa_push(c, (int)NUM(0), (const float *)p, (int)(n / 8));
}
FN(waQueued) { CTX; RET((double)wa_queued(c, (int)NUM(0))); }
FN(waPause) { CTX; wa_pause(c, (int)NUM(0), BOOL(1)); }
FN(waClose) { CTX; wa_close(c, (int)NUM(0)); }
FN(camCount) { RET((double)cam_count()); }
FN(camOpen) { CTX; RET((double)cam_open(c, (int)NUM(0), (int)NUM(1), (int)NUM(2), (int)NUM(3))); }
FN(camState) { CTX; RET((double)cam_state(c, (int)NUM(0))); }
FN(camSource) { CTX; RET((double)cam_source(c, (int)NUM(0))); }
FN(camPermission) { RET((double)cam_permission()); }
FN(camInfo) {
    CTX; int w, h, fps, facing; cam_info(c, (int)NUM(0), &w, &h, &fps, &facing);
    v8::Local<v8::Value> v[] = { v8::Number::New(iso, w), v8::Number::New(iso, h), v8::Number::New(iso, fps), v8::Number::New(iso, facing) };
    RET(v8::Array::New(iso, v, 4));
}
FN(camClose) { CTX; cam_close(c, (int)NUM(0)); }
FN(screenOpen) { CTX; RET((double)(screen_supported() ? screen_open(c, (int)NUM(0), (int)NUM(1), (int)NUM(2), a.Length() > 3 && a[3]->BooleanValue(iso)) : -1)); }
FN(screenState) { CTX; RET((double)screen_state(c, (int)NUM(0))); }
FN(screenSource) { CTX; RET((double)screen_source(c, (int)NUM(0))); }
FN(screenInfo) {
    CTX; int w, h; bool audio; screen_info(c, (int)NUM(0), &w, &h, &audio);
    v8::Local<v8::Value> v[] = { v8::Number::New(iso, w), v8::Number::New(iso, h), v8::Boolean::New(iso, audio) };
    RET(v8::Array::New(iso, v, 3));
}
FN(screenAudio) {   /* the system audio (mono float32 PCM) captured since the last read, or null */
    CTX; int id = (int)NUM(0), n = screen_audio_available(c, id);
    if (n <= 0) { RET(v8::Null(iso)); return; }
    if (n > 48000 * 4) n = 48000 * 4;
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, (size_t)n * sizeof(float));
    int got = screen_audio_read(c, id, (float *)ab->Data(), n);
    if (got <= 0) { RET(v8::Null(iso)); return; }
    RET(v8::Float32Array::New(ab, 0, (size_t)got));
}
FN(screenClose) { CTX; screen_close(c, (int)NUM(0)); }
FN(micOpen) { CTX; RET((double)mic_open(c, a.Length() < 1 || BOOL(0))); }
FN(micPermission) { RET((double)mic_permission()); }
FN(micState) { CTX; RET((double)mic_state(c, (int)NUM(0))); }
FN(micRead) {   /* mono float32 PCM captured since the last read, or null */
    CTX; int id = (int)NUM(0), n = mic_available(c, id);
    if (n <= 0) { RET(v8::Null(iso)); return; }
    if (n > 48000 * 4) n = 48000 * 4;
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, (size_t)n * sizeof(float));
    int got = mic_read(c, id, (float *)ab->Data(), n);
    if (got <= 0) { RET(v8::Null(iso)); return; }
    RET(v8::Float32Array::New(ab, 0, (size_t)got));
}
FN(micClose) { CTX; mic_close(c, (int)NUM(0)); }
FN(recKind) { CTX; (void)c; RET((double)rec_kind(S(0).c_str())); }
FN(recOpen) { CTX; RET((double)rec_open(c, S(0).c_str(), (int)NUM(1), (int)NUM(2), (int)NUM(3), (int)NUM(4), (int)NUM(5), (int)NUM(6), a.Length() < 8 || a[7]->BooleanValue(iso))); }
FN(recPause) { CTX; rec_pause(c, (int)NUM(0), a[1]->BooleanValue(iso)); }
FN(recPush) {
    CTX; const char *p; size_t n;
    if (bytes_of(a[1], &p, &n)) rec_push(c, (int)NUM(0), (const float *)p, (int)(n / sizeof(float)));
}
FN(recFinish) { CTX; rec_finish(c, (int)NUM(0)); }
FN(recTake) {
    CTX; size_t n; uint8_t *p = rec_take(c, (int)NUM(0), &n);
    if (!p) { RET(v8::Null(iso)); return; }
    RET(mkab(iso, p, n)); free(p);
}
FN(recMime) { CTX; const char *m = rec_mime(c, (int)NUM(0)); RET(v8::String::NewFromUtf8(iso, m ? m : "").ToLocalChecked()); }
FN(recClose) { CTX; rec_close(c, (int)NUM(0)); }
FN(audioDecode) {
    CTX; const char *p; size_t n; int frames = 0;
    if (!bytes_of(a[0], &p, &n) || !n) { RET(v8::Null(iso)); return; }
    float *s = media_decode_pcm((const uint8_t *)p, n, &frames);
    if (!s) { RET(v8::Null(iso)); return; }
    RET(mkab(iso, s, (size_t)frames * 8)); free(s);
}
FN(mediaNew) { CTX; ARGN(n, 0); uint32_t id = c->next_player++; c->players[id] = mp_new(n); RET((double)id); }
FN(mediaFree) { CTX; MP; mp_free(mp); c->players.erase(mpit); }
FN(mediaLive) { CTX; MP; mp_live(mp, (int)NUM(1)); }
FN(mediaOpen) { CTX; MP; mp_open_url(mp, S(1).c_str()); }
FN(mediaOpenBytes) { CTX; MP; const char *p; size_t n; if (bytes_of(a[1], &p, &n)) mp_open_bytes(mp, (const uint8_t *)p, n); }
FN(mediaAddBuffer) { CTX; MP; RET((double)mp_add_buffer(mp, S(1).c_str())); }
FN(mediaAppend) {
    CTX; MP; const char *p; size_t n;
    if (!bytes_of(a[2], &p, &n)) return;
    RET(v8::Boolean::New(iso, mp_append(mp, (int)NUM(1), (const uint8_t *)p, n)));
}
FN(mediaRemove) { CTX; MP; mp_remove(mp, (int)NUM(1), NUM(2), NUM(3)); }
FN(mediaBuffered) {
    CTX; MP; double r[64]; int n = mp_buffered(mp, (int)NUM(1), r, 32);
    v8::Local<v8::Array> arr = v8::Array::New(iso, n * 2);
    for (int i = 0; i < n * 2; i++) (void)arr->Set(ctx, (uint32_t)i, v8::Number::New(iso, r[i]));
    RET(arr);
}
FN(mediaEos) { CTX; MP; mp_end_of_stream(mp); }
FN(mediaSetDuration) { CTX; MP; mp_set_duration(mp, NUM(1)); }
FN(mediaPlay) { CTX; MP; mp_play(mp); }
FN(mediaPause) { CTX; MP; mp_pause(mp); }
FN(mediaSeek) { CTX; MP; mp_seek(mp, NUM(1)); }
FN(mediaVolume) { CTX; MP; mp_set_volume(mp, (float)NUM(1), BOOL(2)); }
FN(mediaState) {
    CTX; MP; MpState s; mp_state(mp, &s);
    v8::Local<v8::Value> v[] = {
        v8::Number::New(iso, s.ready), v8::Boolean::New(iso, s.paused), v8::Boolean::New(iso, s.ended),
        v8::Boolean::New(iso, s.seeking), v8::Boolean::New(iso, s.waiting), v8::Boolean::New(iso, s.error),
        v8::Number::New(iso, s.time), v8::Number::New(iso, s.duration), v8::Number::New(iso, s.w),
        v8::Number::New(iso, s.h), nstr(iso, s.errmsg),
    };
    RET(v8::Array::New(iso, v, 11));
}
// Idle players wait for native state changes; playing players retain time updates.
FN(mediaPump) {
    CTX;
    if (!a[0]->IsFunction()) { c->media_pump.Reset(); c->media_due = INFINITY; return; }
    c->media_pump.Reset(iso, a[0].As<v8::Function>());
    c->media_due = 0;
}
// A live size observer waits for layout completion or a new observation.
FN(resizePump) {
    CTX;
    if (!a[0]->IsFunction()) { c->resize_pump.Reset(); c->resize_pending = false; return; }
    c->resize_pump.Reset(iso, a[0].As<v8::Function>());
    c->resize_pending = true;
}
// New observations receive an asynchronous check even without a layout change.
FN(intersectionPump) {
    CTX;
    if (!a[0]->IsFunction()) { c->intersection_pump.Reset(); c->intersection_pending = false; return; }
    c->intersection_pump.Reset(iso, a[0].As<v8::Function>());
    c->intersection_pending = true;
}
// Frame callbacks wake only when media_tick selects a frame for composition.
FN(videoFramePump) {
    CTX;
    if (!a[0]->IsFunction()) { c->video_frame_pump.Reset(); return; }
    c->video_frame_version = UINT64_MAX;
    c->video_frame_pump.Reset(iso, a[0].As<v8::Function>());
}
FN(videoFrameInfo) {
    CTX; ARGN(n, 0);
    if (n->tag != A_video) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Not a video element"))); return; }
    MpState s{};
    if (!media_state_for(n, &s) || !s.frame_serial || (double)s.frame_serial == NUM(1)) { RET(v8::Null(iso)); return; }
    double data[] = { (double)s.frame_serial, s.frame_pts, s.frame_wall - c->t0,
        (double)s.frame_w, (double)s.frame_h, (double)s.presented_frames };
    auto out = v8::Array::New(iso, 6);
    for (uint32_t k = 0; k < 6; k++) (void)out->Set(ctx, k, v8::Number::New(iso, data[k]));
    RET(out);
}
FN(videoQuality) {
    CTX; ARGN(n, 0);
    if (n->tag != A_video) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Not a video element"))); return; }
    MpState s{}; media_state_for(n, &s);
    v8::Local<v8::Value> values[] = { v8::Number::New(iso, (double)(s.presented_frames + s.dropped_frames)), v8::Number::New(iso, (double)s.dropped_frames) };
    RET(v8::Array::New(iso, values, 2));
}
FN(mediaCanPlay) { CTX; RET((double)media_can_play(S(0).c_str(), BOOL(1))); }
FN(fetch) {
    CTX;
    if (!a[4]->IsFunction()) return;
    Fetch *f = new Fetch{ c, 0, {}, nullptr };
    f->cb.Reset(iso, a[4].As<v8::Function>());
    NetRequest *rq = mkreq(c, a);
    if (a.Length() > 6 && a[5]->IsFunction() && a[6]->IsFunction()) {
        f->head.Reset(iso, a[5].As<v8::Function>()); f->chunk.Reset(iso, a[6].As<v8::Function>());
        rq->head = fetch_head; rq->chunk = fetch_chunk;
    }
    RET((double)start_fetch(c, rq, f));
}
static void ws_cb(NetWs *ws, int type, const char *d, size_t n, int code, void *ud) {
    (void)ws;
    WsSock *s = static_cast<WsSock *>(ud);
    JsCtx *c = s->c;
    JS_ENTER(c);
    v8::Local<v8::Value> argv[3] = {
        v8::Integer::New(iso, type),
        type == NET_WS_BINARY ? v8::Local<v8::Value>(mkab(iso, d, n)) : v8::Local<v8::Value>(v8::String::NewFromUtf8(iso, d, v8::NewStringType::kNormal, (int)n).ToLocalChecked()),
        v8::Integer::New(iso, code),
    };
    v8::Local<v8::Function> f = s->cb.Get(iso);
    if (type == NET_WS_CLOSE) { c->sockets.erase(s->id); net_ws_release(s->ws); s->cb.Reset(); delete s; }
    (void)jcall(c, f, v8::Undefined(iso), 3, argv);
}
FN(wsOpen) {
    CTX;
    if (!a[3]->IsFunction()) return;
    WsSock *s = new WsSock{ c, c->next_ws++, nullptr, {} };
    s->cb.Reset(iso, a[3].As<v8::Function>());
    c->sockets[s->id] = s;
    s->ws = net_ws_open(S(0).c_str(), S(1).c_str(), S(2).c_str(), ws_cb, s);
    RET((double)s->id);
}
FN(wsSend) {
    CTX;
    auto it = c->sockets.find((uint32_t)NUM(0));
    if (it == c->sockets.end()) return;
    if (BOOL(2)) { const char *p; size_t n; if (bytes_of(a[1], &p, &n)) net_ws_send(it->second->ws, 2, p, n); }
    else { std::string t = S(1); net_ws_send(it->second->ws, 1, t.data(), t.size()); }
}
FN(wsClose) {
    CTX;
    auto it = c->sockets.find((uint32_t)NUM(0));
    if (it == c->sockets.end()) return;
    std::string r = S(2);
    net_ws_close(it->second->ws, (int)NUM(1), r.c_str());
}
FN(fetchSync) {
    CTX;
    NetResponse *r = net_fetch_sync(mkreq(c, a));
    v8::Local<v8::Value> out[6];
    resp_args(iso, r, out);
    if (r) net_response_free(r);
    RET(v8::Array::New(iso, out, 6));
}
FN(fetchPause) { CTX; (void)c; net_pause((uint64_t)NUM(0), BOOL(1)); }
FN(abort) {
    CTX; uint64_t id = (uint64_t)NUM(0);
    auto it = c->fetches.find(id);
    if (it == c->fetches.end()) return;
    net_cancel(id);
    it->second->c = nullptr; it->second->cb.Reset(); it->second->head.Reset(); it->second->chunk.Reset();
    c->fetches.erase(it);
}
/* errPos(error): [script URL, line, column] for reportError's event: where an Error was made, else the innermost
   page script frame calling (the prelude's own frames skipped) */
FN(errPos) {
    CTX; v8::Local<v8::Value> out[3] = { jstr(iso, ""), v8::Integer::New(iso, 0), v8::Integer::New(iso, 0) };
    if (a[0]->IsNativeError()) {
        v8::Local<v8::Message> m = v8::Exception::CreateMessage(iso, a[0]);
        if (m->GetScriptResourceName()->IsString()) out[0] = m->GetScriptResourceName();
        out[1] = v8::Integer::New(iso, m->GetLineNumber(ctx).FromMaybe(0));
        out[2] = v8::Integer::New(iso, m->GetStartColumn(ctx).FromMaybe(-1) + 1);
    } else {
        v8::Local<v8::StackTrace> st = v8::StackTrace::CurrentStackTrace(iso, 16);
        for (int i = 0; i < st->GetFrameCount(); i++) {
            v8::Local<v8::StackFrame> f = st->GetFrame(iso, i);
            v8::Local<v8::String> nm = f->GetScriptName();
            if (nm.IsEmpty() || nm->Length() == 0 || jcstr(iso, nm) == "lumen:prelude") continue;
            out[0] = nm; out[1] = v8::Integer::New(iso, f->GetLineNumber()); out[2] = v8::Integer::New(iso, f->GetColumn());
            break;
        }
    }
    RET(v8::Array::New(iso, out, 3));
}
FN(log) {
    CTX; int lv = a[0]->Int32Value(ctx).FromMaybe(1);
    if (lv < g_log_level) return;
    static const char *names[] = { "debug", "log", "warn", "error" };
    std::string m = S(1);
    fprintf(stderr, "[js %s] %.2000s\n", names[std::clamp(lv, 0, 3)], m.c_str());
}
FN(logLevel) { a.GetReturnValue().Set(g_log_level); }
FN(viewport) {
    CTX; float w = 0, h = 0, sx = 0, sy = 0, dpr = 1;
    if (c->host.viewport) c->host.viewport(c->host.ud, &w, &h, &sx, &sy, &dpr);
    double v[7] = { w, h, sx, sy, dpr, std::max(w, 1440.f), std::max(h, 900.f) };
    v8::Local<v8::Array> r = v8::Array::New(iso, 7);
    for (int i = 0; i < 7; i++) (void)r->Set(ctx, i, v8::Number::New(iso, v[i]));
    RET(r);
}
FN(scrollTo) { CTX; if (c->host.scroll_to) c->host.scroll_to(c->host.ud, (float)NUM(0), (float)NUM(1)); }
FN(hit) {
    CTX; Node *n = c->host.hit ? c->host.hit(c->host.ud, (float)NUM(0), (float)NUM(1)) : nullptr;
    while (n && n->type != NODE_ELEMENT) n = n->parent;
    RET(jwrap(c, n));
}
FN(caret) {
    CTX; Node *n = nullptr; int off = 0;
    if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
    if (!c->host.caret || !c->host.caret(c->host.ud, (float)NUM(0), (float)NUM(1), &n, &off) || !n) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Value> v[2] = { jwrap(c, n), v8::Integer::New(iso, off) };
    RET(v8::Array::New(iso, v, 2));
}
FN(selSet) {
    CTX; Document *d = c->doc;
    if (!d) return;
    for (int i = 0; i < 2; i++) {
        Node *n = junwrap(a[2 * i]);
        if (n) node_retain(n);
        if (d->sel[i]) node_release(d->sel[i]);
        d->sel[i] = n; d->selo[i] = n ? (int)NUM(2 * i + 1) : 0;
    }
    d->sel_back = BOOL(4); d->sel_ver++;
}
FN(ceScan) {
    CTX; ARGN(root, 0);
    v8::Local<v8::Array> r = v8::Array::New(iso);
    uint32_t k = 0;
    for (Node *x = root; x; x = node_next_in_tree(x, root))
        if (x->type == NODE_ELEMENT && x->ns == NS_HTML && x->tag && strchr(x->tag, '-')) (void)r->Set(ctx, k++, jwrap(c, x));
    RET(r);
}
FN(readyState) { CTX; RET(c->doc->ready_state); }
FN(quirks) { CTX; RET(c->doc->quirks); }
FN(currentScript) { CTX; RET(jwrap(c, c->current_script)); }
FN(hidden) { CTX; RET(v8::Boolean::New(iso, c->bg)); }
FN(media) { CTX; std::string q = S(0); RET(jsg_media(c->host.media, q.c_str())); }
FN(cssSelText) { CTX; std::string q = S(0); char *r = css_selector_text(q.c_str()); RET(nstr(iso, r)); free(r); }
FN(cssSupports) { CTX; std::string q = S(0); RET(jsg_supports(q.c_str())); }
extern "C" void css_register_property(const char *name, const char *initial);
FN(registerProp) { CTX; std::string n = S(0), v = S(1); css_register_property(n.c_str(), v.c_str()); if (c->doc) doc_mark_dirty(c->doc, &c->doc->node); }
FN(encode) { CTX; v8::String::Utf8Value u(iso, a[0]); RET(mkab(iso, *u ? *u : "", *u ? (size_t)u.length() : 0)); }
FN(decode) {
    CTX; const char *p = ""; size_t n = 0;
    bytes_of(a[0], &p, &n);
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { p += 3; n -= 3; }
    RET(jstr(iso, p, (int)n));
}

FN(random) {
    CTX; (void)iso;
    if (!a[0]->IsArrayBuffer()) return;
    uint8_t *p = (uint8_t *)a[0].As<v8::ArrayBuffer>()->Data() + (size_t)NUM(1);
    size_t n = (size_t)NUM(2);
#ifdef __APPLE__
    arc4random_buf(p, n);
#else
    while (n) { ssize_t k = getrandom(p, n, 0); if (k <= 0) break; p += k; n -= (size_t)k; }
#endif
}
FN(heap) {
    CTX; v8::HeapStatistics hs;
    iso->GetHeapStatistics(&hs);
    v8::Local<v8::Value> v[3] = { v8::Number::New(iso, (double)hs.used_heap_size()), v8::Number::New(iso, (double)hs.total_heap_size()), v8::Number::New(iso, (double)hs.heap_size_limit()) };
    RET(v8::Array::New(iso, v, 3));
}
/* a detached <img> got a src: the page's image loop loads it and fires load/error like a connected one */
FN(imgWatch) {
    CTX; ARGN(n, 0); Document *d = n->doc;
    if (!d) return;
    for (int i = 0; i < d->ndimg; i++) if (d->dimg[i] == n) { d->resource_version++; d->dom_version++; return; }   /* dom_version: restyle runs sync_images */
    if (d->ndimg == d->cdimg) { d->cdimg = d->cdimg ? d->cdimg * 2 : 8; d->dimg = (Node **)xrealloc(d->dimg, (size_t)d->cdimg * sizeof *d->dimg); }
    n->refcount++; d->dimg[d->ndimg++] = n; d->resource_version++; d->dom_version++;
}
FN(imgDone) { CTX; ARGN(n, 0); RET(v8::Boolean::New(iso, jsg_img_done(n))); }
FN(imgSize) {
    CTX; ARGN(n, 0); float w, h;
    if (!jsg_img_size(n, &w, &h)) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Value> v[2] = { v8::Number::New(iso, w), v8::Number::New(iso, h) };
    RET(v8::Array::New(iso, v, 2));
}
/* createImageBitmap(blob): the bytes decode on a thread of their own; imgTake gives undefined while that runs,
   then [width, height, straight-alpha RGBA ArrayBuffer], or null when they were no image */
struct ImgJob { std::atomic<int> state{ 0 }; int w = 0, h = 0; uint8_t *rgba = nullptr; ~ImgJob() { free(rgba); } };
static std::mutex g_img_mu;
static std::map<uint32_t, std::shared_ptr<ImgJob>> g_img_jobs;
static uint32_t g_img_next = 1;
FN(imgDecode) {
    CTX; const char *p; size_t n;
    if (a.Length() < 1 || !bytes_of(a[0], &p, &n) || !n) { RET(0); return; }
    auto j = std::make_shared<ImgJob>();
    uint32_t id;
    { std::lock_guard<std::mutex> lk(g_img_mu); id = g_img_next++; g_img_jobs[id] = j; }
    std::thread([j, bytes = std::vector<uint8_t>((const uint8_t *)p, (const uint8_t *)p + n)] {
        j->rgba = jsg_decode_rgba(bytes.data(), bytes.size(), &j->w, &j->h);
        j->state = j->rgba ? 1 : -1;
        if (js_wakeup) js_wakeup();
    }).detach();
    RET(id);
}
FN(imgTake) {
    CTX; uint32_t id = (uint32_t)NUM(0);
    std::shared_ptr<ImgJob> j;
    {
        std::lock_guard<std::mutex> lk(g_img_mu);
        auto it = g_img_jobs.find(id);
        if (it == g_img_jobs.end()) { RET(v8::Null(iso)); return; }
        if (!it->second->state) return;
        j = it->second; g_img_jobs.erase(it);
    }
    if (j->state < 0) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Value> v[3] = { v8::Number::New(iso, j->w), v8::Number::New(iso, j->h), mkab(iso, j->rgba, (size_t)j->w * (size_t)j->h * 4) };
    RET(v8::Array::New(iso, v, 3));
}
/* A page's browser identity, frames included, follows its top-level document, as one browser has one
   identity per tab: a sign-in host's button frame inside another site must agree with the script in the
   embedding page about which browser this is. */
static const char *ident_url(JsCtx *c) { while (c->parent) c = c->parent; return c->doc->url; }
FN(userAgent) { CTX; RET(jstr(iso, net_user_agent_for_url(ident_url(c)))); }
FN(touchPoints) {
    CTX;
#ifdef __ANDROID__
    RET(v8::Integer::New(iso, 16));
#else
    RET(v8::Integer::New(iso, 0));
#endif
}
FN(download) {   /* url, file name hint */
    CTX;
    std::string url = S(0), name = a.Length() > 1 ? S(1) : std::string();
    if (!url.compare(0, 5, "http:") || !url.compare(0, 6, "https:")) dl_start(url.c_str(), c->doc->url, name.empty() ? NULL : name.c_str());
}
FN(downloadBytes) {   /* name, MIME type, bytes */
    CTX;
    const char *p = NULL; size_t n = 0;
    if (a.Length() < 3 || !bytes_of(a[2], &p, &n)) return;
    std::string name = S(0), mime = S(1);
    dl_save_bytes(name.c_str(), mime.c_str(), p, n);
}
FN(platform) {
    CTX;
#ifdef __ANDROID__
    RET(jstr(iso, strstr(net_user_agent_for_url(ident_url(c)), "Android") ? "Linux armv8l" : "Linux x86_64"));
#elif defined(__APPLE__)
    RET(jstr(iso, "MacIntel"));
#else
    RET(jstr(iso, "Linux x86_64"));
#endif
}
static v8::Intercepted all_named(v8::Local<v8::Name> k, const v8::PropertyCallbackInfo<v8::Value> &i) {
    if (!k->IsString()) return v8::Intercepted::kNo;
    v8::Isolate *iso = i.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> argv[1] = { k }, r;
    if (!i.Data().As<v8::Function>()->Call(ctx, i.Holder(), 1, argv).ToLocal(&r) || r->IsUndefined()) return v8::Intercepted::kNo;
    i.GetReturnValue().Set(r);
    return v8::Intercepted::kYes;
}
static v8::Intercepted all_index(uint32_t idx, const v8::PropertyCallbackInfo<v8::Value> &i) {
    v8::Isolate *iso = i.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> argv[1] = { v8::Integer::NewFromUnsigned(iso, idx) }, r;
    if (!i.Data().As<v8::Function>()->Call(ctx, i.Holder(), 1, argv).ToLocal(&r) || r->IsUndefined()) return v8::Intercepted::kNo;
    i.GetReturnValue().Set(r);
    return v8::Intercepted::kYes;
}
static void all_call(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> argv[1] = { a.Length() ? a[0] : v8::Undefined(iso).As<v8::Value>() }, r;
    if (a.Data().As<v8::Function>()->Call(ctx, a.This(), 1, argv).ToLocal(&r)) a.GetReturnValue().Set(r->IsUndefined() ? v8::Null(iso).As<v8::Value>() : r);
}
/* document.all: an undetectable object (typeof "undefined", falsy, == null) whose lookups go to get(key) */
FN(makeAll) {
    CTX;
    if (!a[0]->IsFunction()) return;
    v8::Local<v8::ObjectTemplate> t = v8::ObjectTemplate::New(iso);
    t->MarkAsUndetectable();
    t->SetCallAsFunctionHandler(all_call, a[0]);
    t->SetHandler(v8::NamedPropertyHandlerConfiguration(all_named, nullptr, nullptr, nullptr, nullptr, a[0], v8::PropertyHandlerFlags::kNonMasking));
    t->SetHandler(v8::IndexedPropertyHandlerConfiguration(all_index, nullptr, nullptr, nullptr, nullptr, a[0]));
    v8::Local<v8::Object> o;
    if (t->NewInstance(ctx).ToLocal(&o)) RET(o);
}
FN(cpus) { CTX; (void)iso; RET((int)std::max(1u, std::thread::hardware_concurrency())); }

void js_run_inserted(JsCtx *c, Node *root) {
    if (!(root->flags & NF_CONNECTED)) return;
    std::vector<Node *> list;
    for (Node *x = root; x; x = node_next_in_tree(x, root))
        if (x->type == NODE_ELEMENT && x->tag == A_script && x->ns == NS_HTML && !(x->flags & NF_SCRIPT_STARTED)) list.push_back(x);
    for (Node *s : list) {
        s->flags |= NF_SCRIPT_STARTED;
        bool mod = jsg_module_script(s);
        if (!mod && !jsg_classic_script(s)) continue;
        const char *src = node_attr(s, "src");
        if (mod) {
            char *t = src ? nullptr : node_text_content(s), *u = src && *src ? url_join(doc_base(c->doc), src) : nullptr;
            if (src && !u) jfire(c, s, "error");
            else js_run_module_inserted(c, s, t ? t : (src ? nullptr : ""), t ? strlen(t) : 0, u ? u : c->doc->url);
            free(t); free(u);
            continue;
        }
        if (!src) {
            char *t = node_text_content(s);
            js_run_script(c, s, t ? t : "", t ? strlen(t) : 0, c->doc->url);
            free(t);
            continue;
        }
        char *u = *src ? url_join(doc_base(c->doc), src) : nullptr;
        if (!u) { jfire(c, s, "error"); continue; }
        jwrap(c, s);
        NetRequest *rq = net_request_new("GET", u);
        if (c->doc->url && !strncmp(c->doc->url, "http", 4)) headers_add(&rq->headers, "Referer", c->doc->url);
        start_fetch(c, rq, new Fetch{ c, 0, {}, s });
        free(u);
    }
    frames_inserted(c, root);
}

/* ---- canvas 2D (paint/canvas2d.c) ---- */
extern "C" {
#include "../paint/canvas2d.h"
#include "../text/font.h"
}
template <class T> static T *c2d_arr(v8::Local<v8::Value> v, size_t *n) {
    const char *p; size_t b;
    if (!v->IsArrayBufferView() || !bytes_of(v, &p, &b)) { *n = 0; return nullptr; }
    *n = b / sizeof(T); return (T *)p;
}
static thread_local float *g_c2d_cov; static thread_local size_t g_c2d_ncov;
#define C2DCTX v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext(); (void)ctx
static float *c2d_scratch(size_t n) {
    if (n > g_c2d_ncov) { free(g_c2d_cov); g_c2d_cov = (float *)xmalloc(n * sizeof(float)); g_c2d_ncov = n; }
    memset(g_c2d_cov, 0, n * sizeof(float));
    return g_c2d_cov;
}
/* text shape [str, family, weight, italic, size, x, y, sx, sy]: glyph alpha into cov; returns rows touched */
static bool c2d_text_cov(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Array> sh, float *cov, int w, int h, int *y0, int *y1) {
    auto G = [&](int i) { return sh->Get(ctx, i).ToLocalChecked(); };
    std::string s = jcstr(iso, G(0)), fam = jcstr(iso, G(1));
    double wt = G(2)->NumberValue(ctx).FromMaybe(400), size = G(4)->NumberValue(ctx).FromMaybe(10), x = G(5)->NumberValue(ctx).FromMaybe(0), y = G(6)->NumberValue(ctx).FromMaybe(0);
    double sx = G(7)->NumberValue(ctx).FromMaybe(1), sy = G(8)->NumberValue(ctx).FromMaybe(1);
    bool it = G(3)->BooleanValue(iso);
    if (!(size > 0) || s.empty()) return false;
    Font *f = font_get(fam.c_str(), (int)wt, it, (float)size);
    if (!f) return false;
    ShapedRun r = { 0 };
    text_shape(f, s.c_str(), s.size(), 0, &r);
    float sc = (float)LMAX(1e-3, sy);
    double hs = sx / LMAX(1e-3, sy);   /* horizontal squeeze (maxWidth) relative to the rasterised size */
    int ymin = h, ymax = 0;
    for (int i = 0; i < r.n; i++) {
        const Glyph *gl = font_glyph(r.g[i].font ? r.g[i].font : f, r.g[i].gid, sc);
        if (!gl || (!gl->alpha && !gl->rgba)) continue;
        auto A = [&](int yy, int xx) { return gl->alpha ? gl->alpha[yy * gl->w + xx] / 255.f : (gl->rgba[yy * gl->w + xx] >> 24) / 255.f; };
        int dw = fabs(hs - 1) < 1e-6 ? gl->w : (int)ceil(gl->w * hs);
        int gx = (int)lround(x + (r.g[i].x) * sx + gl->left * hs), gy = (int)lround(y + r.g[i].y * sy) - gl->top;
        for (int yy = 0; yy < gl->h; yy++) {
            int py = gy + yy; if (py < 0 || py >= h) continue;
            for (int xx = 0; xx < dw; xx++) {
                int px = gx + xx; if (px < 0 || px >= w) continue;
                float a;
                if (dw == gl->w) a = A(yy, xx);
                else {   /* box-filter the source columns this output column covers */
                    double s0 = xx / hs, s1 = LMIN((double)gl->w, (xx + 1) / hs), sum = 0;
                    for (int k = (int)s0; k < s1; k++) sum += A(yy, k) * (LMIN(s1, k + 1.0) - LMAX(s0, (double)k));
                    a = (float)(sum / LMAX(1e-6, s1 - s0));
                }
                float &o = cov[(size_t)py * w + px]; o = LMIN(1.f, o + a);
            }
            ymin = LMIN(ymin, py); ymax = LMAX(ymax, py + 1);
        }
    }
    shaped_free(&r);
    if (ymax <= ymin) return false;
    *y0 = ymin; *y1 = ymax; return true;
}
static bool c2d_shape_cov(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> v, int w, int h, float **cov, int *y0, int *y1) {
    if (v->IsFloat32Array()) {   /* a ready coverage mask (shadows) */
        size_t n; float *m = c2d_arr<float>(v, &n);
        if (n < (size_t)w * h) return false;
        *cov = c2d_scratch((size_t)w * h); memcpy(*cov, m, (size_t)w * h * sizeof(float)); *y0 = 0; *y1 = h; return true;
    }
    if (!v->IsArray()) { *cov = nullptr; *y0 = 0; *y1 = h; return true; }
    auto sh = v.As<v8::Array>();
    float *c = c2d_scratch((size_t)w * h);
    *cov = c;
    auto first = sh->Get(ctx, 0).ToLocalChecked();
    if (first->IsString()) return c2d_text_cov(iso, ctx, sh, c, w, h, y0, y1);
    size_t nxy, nsub;
    float *xy = c2d_arr<float>(first, &nxy);
    int *sub = c2d_arr<int>(sh->Get(ctx, 1).ToLocalChecked(), &nsub);
    if (!xy || !sub || !nsub) return false;
    return c2d_cover(c, w, h, xy, sub, (int)nsub, (int)(nxy / 2), sh->Get(ctx, 2).ToLocalChecked()->BooleanValue(iso), y0, y1);
}
/* c2dFill(px, w, h, shape | null, alpha, op, clip | null, paint [kind, col, params(12), lut, img, iw, ih, flags]) */
FN(c2dFill) {
    C2DCTX; size_t npx, nclip;
    uint32_t *px = c2d_arr<uint32_t>(a[0], &npx);
    int w = (int)NUM(1), h = (int)NUM(2);
    if (!px || w <= 0 || h <= 0 || npx < (size_t)w * h || !a[7]->IsArray()) return;
    float *clip = c2d_arr<float>(a[6], &nclip);
    if (clip && nclip < (size_t)w * h) clip = nullptr;
    float *cov; int y0 = 0, y1 = h;
    int op = (int)NUM(5);
    if (!c2d_shape_cov(iso, ctx, a[3], w, h, &cov, &y0, &y1)) { if (op != C2D_SRC_IN && op != C2D_SRC_OUT && op != C2D_DST_IN && op != C2D_DST_ATOP && op != C2D_COPY) return; y0 = y1 = 0; }
    auto pa = a[7].As<v8::Array>();
    auto P = [&](int i) { return pa->Get(ctx, i).ToLocalChecked(); };
    C2DPaint p = { 0 }; size_t n;
    p.kind = (int)P(0)->NumberValue(ctx).FromMaybe(0);
    p.col = (uint32_t)P(1)->NumberValue(ctx).FromMaybe(0);
    if (float *q = c2d_arr<float>(P(2), &n)) if (n >= 12) { memcpy(p.inv, q, 6 * sizeof(float)); memcpy(p.g, q + 6, 6 * sizeof(float)); }
    p.lut = c2d_arr<uint32_t>(P(3), &n); if (p.lut && n < 1024) p.lut = nullptr;
    p.img = c2d_arr<uint32_t>(P(4), &n);
    p.iw = (int)P(5)->NumberValue(ctx).FromMaybe(0); p.ih = (int)P(6)->NumberValue(ctx).FromMaybe(0);
    if (p.img && n < (size_t)p.iw * p.ih) p.img = nullptr;
    int fl = (int)P(7)->NumberValue(ctx).FromMaybe(0);
    p.rx = fl & 1; p.ry = fl & 2; p.smooth = fl & 4;
    if ((p.kind == 1 || p.kind == 2 || p.kind == 3) && !p.lut) return;
    if (cov && y1 <= y0) cov = c2d_scratch((size_t)w * h);
    c2d_paint(px, w, h, cov, &p, (float)NUM(4), op, clip, y0, y1);
}
/* c2dClip(mask, w, h, shape): mask *= the shape's coverage */
FN(c2dClip) {
    C2DCTX; size_t nm;
    float *m = c2d_arr<float>(a[0], &nm);
    int w = (int)NUM(1), h = (int)NUM(2);
    if (!m || w <= 0 || h <= 0 || nm < (size_t)w * h) return;
    float *cov; int y0 = 0, y1 = 0;
    if (!c2d_shape_cov(iso, ctx, a[3], w, h, &cov, &y0, &y1)) { memset(m, 0, (size_t)w * h * sizeof(float)); return; }
    for (size_t i = 0, e = (size_t)w * h; i < e; i++) m[i] *= cov[i];
}
/* c2dMeasure(str, family, weight, italic, size) -> [width, ascent, descent] */
FN(c2dMeasure) {
    C2DCTX;
    std::string s = jcstr(iso, a[0]), fam = jcstr(iso, a[1]);
    Font *f = font_get(fam.c_str(), (int)NUM(2), BOOL(3), (float)NUM(4));
    auto o = v8::Array::New(iso, 3);
    double wd = f ? text_width(f, s.c_str(), s.size(), 0) : 0;
    o->Set(ctx, 0, v8::Number::New(iso, wd)).Check();
    o->Set(ctx, 1, v8::Number::New(iso, f ? f->ascent : NUM(4) * 0.8)).Check();
    o->Set(ctx, 2, v8::Number::New(iso, f ? f->descent : NUM(4) * 0.2)).Check();
    RET(o);
}
/* c2dSource(node) -> [w, h, ArrayBuffer of premultiplied ARGB] for an <img>, <video> or shown canvas */
/* FontFace: fontFace(family, weight, italic, key, bytes) declares the face under key and loads its bytes; false when no font */
static void n_fontFace(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); const char *p; size_t n;
    if (a.Length() < 5 || !bytes_of(a[4], &p, &n) || !n) { RET(false); return; }
    v8::String::Utf8Value fam(iso, a[0]), key(iso, a[3]);
    if (!*fam || !*key) { RET(false); return; }
    font_declare(*fam, a[1]->Int32Value(iso->GetCurrentContext()).FromMaybe(400), a[2]->BooleanValue(iso), *key);
    RET(font_loaded(*key, p, n));
}
/* worker createImageBitmap(blob): decode on the worker's own thread -> [w, h, straight RGBA ArrayBuffer] or null */
static void n_imgDecodeSync(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); const char *p; size_t n; int w = 0, h = 0;
    if (a.Length() < 1 || !bytes_of(a[0], &p, &n) || !n) { RET(v8::Null(iso)); return; }
    uint8_t *rgba = jsg_decode_rgba((const uint8_t *)p, n, &w, &h);
    if (!rgba) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Value> v[3] = { v8::Number::New(iso, w), v8::Number::New(iso, h), mkab(iso, rgba, (size_t)w * (size_t)h * 4) };
    free(rgba);
    RET(v8::Array::New(iso, v, 3));
}
/* the pure canvas natives (no document) for worker globals: OffscreenCanvas */
void js_c2d_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> W) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    (void)W->Set(ctx, jstr(iso, "c2dFill"), v8::Function::New(ctx, n_c2dFill).ToLocalChecked());
    (void)W->Set(ctx, jstr(iso, "c2dClip"), v8::Function::New(ctx, n_c2dClip).ToLocalChecked());
    (void)W->Set(ctx, jstr(iso, "c2dMeasure"), v8::Function::New(ctx, n_c2dMeasure).ToLocalChecked());
    (void)W->Set(ctx, jstr(iso, "imgDecodeSync"), v8::Function::New(ctx, n_imgDecodeSync).ToLocalChecked());
    (void)W->Set(ctx, jstr(iso, "fontFace"), v8::Function::New(ctx, n_fontFace).ToLocalChecked());
}
FN(c2dSource) {
    CTX; Node *n = junwrap(a[0]); int w = 0, h = 0;
    uint32_t *px = n ? c2d_node_pixels(n, &w, &h) : nullptr;
    if (!px) { RET(v8::Null(iso)); return; }
    auto o = v8::Array::New(iso, 3);
    o->Set(ctx, 0, v8::Number::New(iso, w)).Check();
    o->Set(ctx, 1, v8::Number::New(iso, h)).Check();
    o->Set(ctx, 2, mkab(iso, px, (size_t)w * h * 4)).Check();
    free(px);
    RET(o);
}
/* c2dPresent(node, px, w, h): show the context's pixels for the canvas element */
FN(c2dPresent) {
    CTX; Node *n = junwrap(a[0]); size_t np;
    uint32_t *px = c2d_arr<uint32_t>(a[1], &np);
    int w = (int)NUM(2), h = (int)NUM(3);
    if (!n) return;
    if (!px || w <= 0 || h <= 0 || np < (size_t)w * h) c2d_forget(n); else c2d_present(n, px, w, h);
    if (n->doc) n->doc->paint_version++;
}

#if defined(__ANDROID__) && !defined(NDEBUG)
#include <mutex>
/* Debug Android: while the jsprof diagnostic is armed, each native counts its calls and time
   (inclusive of any JS it calls back into); jsprof logs the totals. Native names only. */
struct NatStat { const char *name; v8::FunctionCallback fn; uint64_t n; double ms; };
static NatStat g_nat[512]; static size_t g_nnat; static std::mutex g_nat_mu;
static void n_timed(const FCI &a) {
    NatStat &s = g_nat[(size_t)a.Data().As<v8::Integer>()->Value()];
    double t = now_ms(); s.fn(a); s.ms += now_ms() - t; s.n++;
}
static v8::Local<v8::Function> nat_fn(v8::Local<v8::Context> ctx, const char *name, v8::FunctionCallback fn) {
    if (!jsprof_active()) return v8::Function::New(ctx, fn).ToLocalChecked();
    size_t i;
    { std::lock_guard<std::mutex> l(g_nat_mu);
      for (i = 0; i < g_nnat && g_nat[i].fn != fn; i++) {}
      if (i == g_nnat) { if (g_nnat == sizeof g_nat / sizeof *g_nat) return v8::Function::New(ctx, fn).ToLocalChecked(); g_nat[g_nnat++] = { name, fn, 0, 0 }; } }
    return v8::Function::New(ctx, n_timed, v8::Integer::New(v8::Isolate::GetCurrent(), (int)i)).ToLocalChecked();
}
void js_native_report(void) {
    std::lock_guard<std::mutex> l(g_nat_mu);
    std::vector<NatStat *> v; for (size_t i = 0; i < g_nnat; i++) if (g_nat[i].n) v.push_back(&g_nat[i]);
    std::sort(v.begin(), v.end(), [](NatStat *x, NatStat *y) { return x->ms > y->ms; });
    for (size_t i = 0; i < v.size() && i < 30; i++)
        __android_log_print(ANDROID_LOG_INFO, "LumenProf", "native %-14s calls=%llu ms=%.0f", v[i]->name, (unsigned long long)v[i]->n, v[i]->ms);
    for (size_t i = 0; i < g_nnat; i++) g_nat[i].n = 0, g_nat[i].ms = 0;
}
#define NAT_FN(nm) nat_fn(ctx, #nm, n_##nm)
#else
#define NAT_FN(nm) v8::Function::New(ctx, n_##nm).ToLocalChecked()
#endif
void js_install_native(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), NAT_FN(nm))
    REG(isNode); REG(version);
    REG(inert); REG(type); REG(name); REG(ns); REG(parent); REG(first); REG(last); REG(next); REG(prev); REG(index);
    (void)N->Set(ctx, jstr(iso, "fontFace"), v8::Function::New(ctx, n_fontFace).ToLocalChecked()); REG(c2dFill); REG(c2dClip); REG(c2dMeasure); REG(c2dSource); REG(c2dPresent);
    REG(text); REG(setText); REG(attr); REG(setAttr); REG(rmAttr); REG(attrs); REG(attrQ); REG(attrNS); REG(setAttrQ); REG(setAttrNS); REG(rmAttrQ); REG(rmAttrNS); REG(attrsNS); REG(insert); REG(remove); REG(newDoc); REG(newXmlDoc); REG(xname); REG(charset); REG(ctype); REG(csp); REG(evalGuard); REG(ttOn); REG(codeLike); REG(lazySrc); REG(parseXml); REG(parseDoc); REG(docReparse); REG(adopt); REG(pi); REG(cdata); REG(doctype); REG(ownerDoc);
    REG(create); REG(textNode); REG(comment); REG(frag); REG(html); REG(setHTML); REG(parseFrag); REG(query);
    REG(matches); REG(byId); REG(clone); REG(doc); REG(contains); REG(connected); REG(host); REG(setRealm); REG(realmOf); REG(attachShadow);
    REG(shadowOf); REG(srFlags); REG(srSetFlags); REG(srRef); REG(getHTML); REG(parseFragEx); REG(parseDocEx);
    REG(templateContent); REG(rect); REG(textRects); REG(vrect); REG(clipSet); REG(clipGet); REG(clipImage); REG(scrollPos); REG(setScroll); REG(computed); REG(trTake); REG(setAnimStyle); REG(value); REG(setValue); REG(ctlSel); REG(setCtlSel); REG(checked); REG(setChecked); REG(pickFiles);
    REG(modal); REG(fullscreen); REG(focus); REG(active); REG(cookie); REG(setCookie); REG(url); REG(docBase); REG(setUrl); REG(navigate); REG(navigatePost); REG(histGo);
    REG(histLen); REG(internal); REG(timer); REG(clearTimer); REG(raf); REG(cancelRaf); REG(now); REG(fetch); REG(fetchSync);
    REG(abort); REG(fetchPause); REG(mediaNew); REG(mediaFree); REG(mediaOpen); REG(mediaOpenBytes); REG(mediaAddBuffer); REG(mediaAppend); REG(mediaRemove);
    REG(mediaBuffered); REG(mediaEos); REG(mediaSetDuration); REG(mediaPlay); REG(mediaPause); REG(mediaSeek);
    REG(mediaPump); REG(resizePump); REG(intersectionPump); REG(vclip); REG(videoFramePump); REG(videoFrameInfo); REG(videoQuality);
    REG(mediaVolume); REG(mediaState); REG(mediaCanPlay); REG(log); REG(errPos); REG(logLevel); REG(wsOpen); REG(wsSend); REG(wsClose); REG(viewport); REG(scrollTo); REG(hit); REG(caret); REG(ceScan); REG(selSet); REG(waOpen); REG(waPush); REG(waQueued); REG(waPause); REG(waClose); REG(audioDecode); REG(camCount); REG(camOpen); REG(camState); REG(camSource); REG(camPermission); REG(camInfo); REG(camClose); REG(mediaLive); REG(screenOpen); REG(screenState); REG(screenSource); REG(screenInfo); REG(screenClose); REG(screenAudio); REG(micOpen); REG(micPermission); REG(micState); REG(micRead); REG(micClose); REG(recKind); REG(recOpen); REG(recPause); REG(recPush); REG(recFinish); REG(recTake); REG(recClose); REG(recMime); REG(readyState); REG(quirks);
    REG(currentScript); REG(hidden); REG(media); REG(cssSupports); REG(registerProp); REG(cssSelText); REG(encode); REG(decode); REG(random);
    REG(heap); REG(imgWatch); REG(imgSize); REG(imgDone); REG(imgDecode); REG(imgTake); REG(userAgent); REG(platform); REG(download); REG(downloadBytes); REG(touchPoints); REG(cpus); REG(makeAll);
#undef REG
    js_install_frames(c, N); js_install_workers(c, N); js_install_rtc(c, N); js_install_webgl(c, N); js_install_webgpu(c, N);
}
