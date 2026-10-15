#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <chrono>
/* Native functions (the `N` object) backing the JS DOM/Web API prelude */
#include "js_int.h"
#include <thread>
#ifndef __APPLE__
#include <sys/random.h>
#endif
#include <vector>
#include <map>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/param_build.h>
#include <openssl/x509.h>

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
    for (Node *x = root; x; x = node_next_in_tree(x, root))
        if (x->type == NODE_ELEMENT && x->tag == A_script) x->flags |= NF_SCRIPT_STARTED;
}

void (*js_clip_set)(const char *text);
char *(*js_clip_get)(void);
FN(clipSet) { CTX; v8::String::Utf8Value u(iso, a[0]); if (js_clip_set) js_clip_set(*u ? *u : ""); }
FN(clipGet) { CTX; char *t = js_clip_get ? js_clip_get() : nullptr; RET(jstr(iso, t ? t : "")); free(t); }
FN(isNode) { CTX; RET(junwrap(a[0]) != nullptr); }
FN(type) { CTX; ARGN(n, 0); RET((int)n->type); }
FN(name) { CTX; ARGN(n, 0); RET(jstr(iso, n->type == NODE_ELEMENT || n->type == NODE_PI ? n->tag : n->type == NODE_DOCTYPE ? n->text : "")); }
FN(ns) { CTX; ARGN(n, 0); RET((int)n->ns); }
FN(parent) { CTX; ARGN(n, 0); RET(jwrap(c, n->parent)); }
FN(first) { CTX; ARGN(n, 0); RET(jwrap(c, n->first)); }
FN(last) { CTX; ARGN(n, 0); RET(jwrap(c, n->last)); }
FN(next) { CTX; ARGN(n, 0); RET(jwrap(c, n->next)); }
FN(prev) { CTX; ARGN(n, 0); RET(jwrap(c, n->prev)); }
FN(text) {
    CTX; ARGN(n, 0);
    if (is_textish(n)) { RET(jstr(iso, n->text ? n->text : "", n->text ? (int)n->text_len : 0)); return; }
    char *t = node_text_content(n); RET(jstr(iso, t ? t : "")); free(t);
}
FN(setText) {
    CTX; ARGN(n, 0); std::string s = S(1);
    if (is_textish(n)) {
        free(n->text); n->text = xstrndup(s.data(), s.size()); n->text_len = s.size();
        doc_mark_dirty(n->doc, n->parent ? n->parent : n);
        return;
    }
    clear_children(n);
    if (!s.empty()) node_append(n, node_new_text(c->doc, s.data(), s.size()));
}
FN(attr) { CTX; ARGN(n, 0); std::string k = S(1); RET(nstr(iso, n->type == NODE_ELEMENT || n->type == NODE_DOCTYPE ? node_attr(n, k.c_str()) : nullptr)); }
FN(setAttr) { CTX; ARGN(n, 0); std::string k = S(1), v = S(2); node_set_attr(n, atom(k.c_str()), v.c_str()); }
FN(rmAttr) { CTX; ARGN(n, 0); std::string k = S(1); if (node_has_attr(n, k.c_str())) node_remove_attr(n, k.c_str()); }
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
    if (ch->doc != p->doc) for (Node *x = ch; x; x = node_next_in_tree(x, ch)) x->doc = p->doc;
    if (ch->doc != p->doc) adopt_tree(ch, p->doc);
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
FN(textNode) { CTX; ADOC(1); std::string s = S(0); RET(jwrap(c, node_new_text(od, s.data(), s.size()))); }
FN(comment) { CTX; ADOC(1); std::string s = S(0); RET(jwrap(c, node_new_comment(od, s.data(), s.size()))); }
FN(frag) { CTX; ADOC(0); RET(jwrap(c, node_new_fragment(od))); }
FN(pi) { CTX; ADOC(2); std::string t = S(0), s = S(1); RET(jwrap(c, node_new_pi(od, atom(t.c_str()), s.data(), s.size()))); }
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
FN(parseFrag) {
    CTX; Node *cx = junwrap(a[0]); std::string s = S(1);
    if (!cx || cx->type != NODE_ELEMENT) cx = c->doc->body;
    Node *f = html_parse_fragment(c->doc, cx, s.data(), s.size());
    if (f) mark_started(f);
    RET(jwrap(c, f));
}
FN(parseDoc) {
    CTX; std::string s = S(0);
    Document *nd = doc_new(c->doc->url);
    html_parse(nd, s.data(), s.size());
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
    if (!jsg_valid_selector(sel.c_str())) { throw_dom(iso, ctx, "'" + sel + "' is not a valid selector.", "SyntaxError"); return; }
    Node **out = nullptr;
    int k = jsg_query(root, sel.c_str(), all, &out);
    if (!all) { RET(jwrap(c, k ? out[0] : nullptr)); free(out); return; }
    v8::Local<v8::Array> r = v8::Array::New(iso, k);
    for (int i = 0; i < k; i++) (void)r->Set(ctx, i, jwrap(c, out[i]));
    free(out);
    RET(r);
}
FN(matches) {
    CTX; ARGN(n, 0); std::string sel = S(1); bool ok = false;
    bool m = n->type == NODE_ELEMENT && jsg_matches(n, sel.c_str(), &ok);
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
FN(attachShadow) {
    CTX; ARGN(n, 0);
    if (!n->shadow_root) { Node *f = node_new_fragment(c->doc); f->host = n; f->refcount = 1; n->shadow_root = f; if (n->flags & NF_CONNECTED) f->flags |= NF_CONNECTED; doc_mark_dirty(c->doc, n); }
    RET(jwrap(c, n->shadow_root));
}
FN(templateContent) {
    CTX; ARGN(n, 0);
    if (!n->template_content) { n->template_content = node_new_fragment(c->doc); n->template_content->refcount = 1; n->template_content->flags |= NF_INERT; }
    RET(jwrap(c, n->template_content));
}
FN(rect) {
    CTX; ARGN(n, 0); float r[4];
    if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
    if (!jsg_rect(n, r)) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Array> o = v8::Array::New(iso, 4);
    for (int i = 0; i < 4; i++) (void)o->Set(ctx, i, v8::Number::New(iso, r[i]));
    RET(o);
}
FN(vrect) {
    CTX; ARGN(n, 0); float r[4];
    if (c->host.sync) c->host.sync(c->host.ud, c->doc, true);
    if (!jsg_vrect(n, r)) { RET(v8::Null(iso)); return; }
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
FN(computed) { CTX; ARGN(n, 0); if (c->host.sync) c->host.sync(c->host.ud, c->doc, false); std::string p = S(1); char *v = jsg_computed(n, p.c_str()); RET(nstr(iso, v)); free(v); }
FN(value) { CTX; ARGN(n, 0); RET(nstr(iso, n->value_override)); }
FN(setValue) { CTX; ARGN(n, 0); free(n->value_override); n->value_override = a[1]->IsNullOrUndefined() ? nullptr : xstrdup(S(1).c_str()); doc_mark_dirty(n->doc, n); }
FN(checked) {
    CTX; ARGN(n, 0);
    RET(n->checked_override ? n->checked_override > 0 : node_has_attr(n, n->tag && !strcmp(n->tag, "option") ? "selected" : "checked"));
}
FN(setPseudo) {   /* 0 :modal, 1 :popover-open */
    CTX; ARGN(n, 0); uint32_t f = NUM(1) == 0 ? NF_MODAL : NF_POPOVER_OPEN;
    if (BOOL(2)) n->flags |= f; else n->flags &= ~f;
    doc_mark_dirty(n->doc, n);
}
FN(setChecked) {
    CTX; ARGN(n, 0); bool b = BOOL(1);
    n->checked_override = b ? 1 : -1;
    if (b) n->flags |= NF_CHECKED; else n->flags &= ~(uint32_t)NF_CHECKED;
    doc_mark_dirty(n->doc, n);
}
FN(pickFiles) {
    CTX;
    if (!a[0]->IsFunction()) return;
    c->pick_cb.Reset(iso, a[0].As<v8::Function>());
    std::string acc = S(2);
    js_pick_begin(c, BOOL(1), acc.c_str());
}
FN(focus) {
    CTX; Node *n = junwrap(a[0]); Node *old = c->doc->focus;
    if (old == n) return;
    if (old) { old->flags &= ~(uint32_t)NF_FOCUS; doc_mark_dirty(c->doc, old); }
    c->doc->focus = n;
    if (n) { n->flags |= NF_FOCUS; doc_mark_dirty(c->doc, n); }
    if (old) jfire(c, old, "blur");
    if (n) jfire(c, n, "focus");
}
FN(active) { CTX; RET(jwrap(c, c->doc->focus)); }
FN(cookie) { CTX; char *s = cookies_get_document(c->doc->url); RET(jstr(iso, s ? s : "")); free(s); }
FN(setCookie) { CTX; std::string s = S(0); cookies_set_document(c->doc->url, s.c_str()); }
FN(url) { CTX; RET(jstr(iso, c->doc->url ? c->doc->url : "about:blank")); }
FN(setUrl) {
    CTX; std::string u = S(0);
    free(c->doc->url); c->doc->url = xstrdup(u.c_str());
    if (c->host.set_url) c->host.set_url(c->host.ud, u.c_str(), BOOL(1));
}
FN(navigate) { CTX; std::string u = S(0); if (c->parent) { frame_nav(c, u.c_str()); return; } if (c->host.navigate) c->host.navigate(c->host.ud, u.c_str()); }
FN(navigatePost) {
    CTX; std::string u = S(0), t = S(2); const char *bp; size_t bn; std::string b;
    if (bytes_of(a[1], &bp, &bn)) b.assign(bp, bn); else b = S(1);
    if (c->host.navigate_post) c->host.navigate_post(c->host.ud, u.c_str(), b.data(), b.size(), t.c_str());
    else if (c->host.navigate) c->host.navigate(c->host.ud, u.c_str());
}
void (*js_save_file)(const char *, const void *, size_t);
FN(saveFile) { CTX; std::string nm = S(0); const char *p = nullptr; size_t n = 0; if (!bytes_of(a[1], &p, &n) || !js_save_file) RET(false); js_save_file(nm.c_str(), p, n); RET(true); }
FN(histGo) { CTX; if (c->host.history_go) c->host.history_go(c->host.ud, a[0]->Int32Value(ctx).FromMaybe(0)); }
/* sessionStorage outlives page loads within a tab: keyed by (tab, origin) */
static std::map<std::pair<void *, std::string>, std::string> g_session;
FN(ssGet) { CTX; auto it = g_session.find({c->host.ud, S(0)}); RET(nstr(iso, it == g_session.end() ? nullptr : it->second.c_str())); }
FN(ssSet) { CTX; g_session[{c->host.ud, S(0)}] = S(1); }

/* localStorage: one process-wide map per origin (shared by all tabs), loaded lazily from
 * <profile>/localstorage/ and written back at most every 2 s, on backgrounding and at exit */
struct LsOrigin { std::map<std::string, std::string> m; bool loaded = false, dirty = false; };
static std::map<std::string, LsOrigin> g_ls;
static std::mutex g_ls_mu;
static std::string g_ls_dir;
static double g_ls_last;
static std::string ls_file(const std::string &o) {
    uint64_t h = 1469598103934665603ull; for (unsigned char ch : o) { h ^= ch; h *= 1099511628211ull; }
    char b[32]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h); return g_ls_dir + b;
}
static LsOrigin &ls_get(const std::string &o) {
    LsOrigin &L = g_ls[o]; if (L.loaded) return L;
    L.loaded = true; if (g_ls_dir.empty()) return L;
    FILE *f = fopen(ls_file(o).c_str(), "rb"); if (!f) return L;
    auto rd = [&](std::string &s) { uint32_t n; if (fread(&n, 4, 1, f) != 1 || n > (64u << 20)) return false; s.resize(n); return !n || fread(&s[0], 1, n, f) == n; };
    std::string org, k, v;
    if (rd(org) && org == o) while (rd(k) && rd(v)) L.m[k] = v;
    fclose(f); return L;
}
static void ls_write(const std::string &o, const LsOrigin &L) {
    std::string p = ls_file(o), t = p + ".tmp"; int fd = open(t.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600); FILE *f = fd < 0 ? NULL : fdopen(fd, "wb"); if (!f) { if (fd >= 0) close(fd); return; }
    auto wr = [&](const std::string &s) { uint32_t n = (uint32_t)s.size(); fwrite(&n, 4, 1, f); fwrite(s.data(), 1, n, f); };
    wr(o); for (auto &kv : L.m) { wr(kv.first); wr(kv.second); }
    if (fclose(f) == 0) rename(t.c_str(), p.c_str()); else unlink(t.c_str());
}

/* ---- in-process message bus: BroadcastChannel (pages, iframes, workers) and cross-context localStorage `storage` events ---- */
static std::vector<BusMsg> g_bus;
static std::mutex g_bus_mu;
static std::atomic<uint64_t> g_bus_seqn{0};
static std::map<const void *, std::function<void()>> g_bus_wake;
uint64_t js_bus_seq() { return g_bus_seqn.load(); }
void js_bus_listen(const void *key, std::function<void()> wake) {
    std::lock_guard<std::mutex> g(g_bus_mu);
    if (wake) g_bus_wake[key] = std::move(wake); else g_bus_wake.erase(key);
}
bool js_bus_post(v8::Isolate *iso, v8::Local<v8::Context> cx, v8::Local<v8::Value> v, const std::string &origin,
                 const std::string &kind, const std::string &name, const void *sender, double id) {
    v8::ValueSerializer ser(iso);
    ser.WriteHeader();
    if (!ser.WriteValue(cx, v).FromMaybe(false)) return false;
    std::pair<uint8_t *, size_t> r = ser.Release();
    BusMsg m;
    m.data.assign(r.first, r.first + r.second);
    free(r.first);
    m.origin = origin; m.kind = kind; m.name = name; m.sender = sender; m.id = id;
    {
        std::lock_guard<std::mutex> g(g_bus_mu);
        double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        m.t = now; m.seq = g_bus_seqn.load() + 1;
        g_bus.push_back(std::move(m));
        g_bus_seqn.store(g_bus.back().seq);
        size_t i = 0;   /* every live context drains the bus within a tick; keep 5 s / 256 messages */
        while (i < g_bus.size() && (now - g_bus[i].t > 5000 || g_bus.size() - i > 256)) i++;
        g_bus.erase(g_bus.begin(), g_bus.begin() + (long)i);
        for (auto &kv : g_bus_wake) kv.second();   /* lock order: g_bus_mu, then the worker's mutex */
    }
    if (js_wakeup) js_wakeup();
    return true;
}
bool js_bus_take(const std::string &origin, const void *self, uint64_t &seen, std::vector<BusMsg> &out) {
    if (origin.empty() || seen == g_bus_seqn.load()) return false;
    std::lock_guard<std::mutex> g(g_bus_mu);
    for (auto &m : g_bus)
        if (m.seq > seen && m.origin == origin && (origin != "null" || m.sender == self)) out.push_back(m);
    seen = g_bus_seqn.load();
    return !out.empty();
}
bool js_bus_args(v8::Isolate *iso, v8::Local<v8::Context> ctx, const BusMsg &m, const void *self, v8::Local<v8::Value> argv[5]) {
    v8::ValueDeserializer d(iso, m.data.data(), m.data.size());
    v8::Local<v8::Value> v;
    if (!d.ReadHeader(ctx).FromMaybe(false) || !d.ReadValue(ctx).ToLocal(&v)) return false;
    argv[0] = jstr(iso, m.kind.c_str()); argv[1] = jstr(iso, m.name.c_str()); argv[2] = v;
    argv[3] = v8::Number::New(iso, m.id); argv[4] = v8::Boolean::New(iso, m.sender == self);
    return true;
}
FN(busOrigin) { CTX; c->bus_origin = S(0); c->bus_seen = js_bus_seq(); }
FN(busPost) {
    CTX;
    v8::Local<v8::Context> cx = iso->GetCurrentContext();
    (void)js_bus_post(iso, cx, a[3], S(0), S(1), S(2), c, a[4]->NumberValue(cx).FromMaybe(0));
}
void js_bus_deliver(JsCtx *c) {
    std::vector<BusMsg> mine;
    if (!js_bus_take(c->bus_origin, c, c->bus_seen, mine) || c->api.IsEmpty()) return;
    v8::Isolate *iso = c->iso;
    v8::HandleScope hs(iso);
    v8::Local<v8::Context> ctx = c->ctx.Get(iso);
    v8::Context::Scope cs(ctx);
    v8::Local<v8::Value> fv;
    if (!c->api.Get(iso)->Get(ctx, jstr(iso, "busDeliver")).ToLocal(&fv) || !fv->IsFunction()) return;
    for (auto &m : mine) {
        v8::Local<v8::Value> argv[5];
        if (js_bus_args(iso, ctx, m, c, argv)) (void)jcall(c, fv.As<v8::Function>(), v8::Undefined(iso), 5, argv);
    }
}

extern "C" void js_ls_flush(bool force) {
    std::lock_guard<std::mutex> g(g_ls_mu);
    double t = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!force && t - g_ls_last < 2000) return;
    g_ls_last = t;
    if (g_ls_dir.empty()) return;
    for (auto &e : g_ls) if (e.second.dirty) { ls_write(e.first, e.second); e.second.dirty = false; }
}
static void ls_atexit(void) { js_ls_flush(true); }
extern "C" void js_set_profile_dir(const char *dir) {
    if (!dir || !*dir || getenv("LUMEN_NO_PERSIST_STORAGE")) return;
    g_ls_dir = std::string(dir) + "localstorage/"; mkdir(g_ls_dir.c_str(), 0700); atexit(ls_atexit);
}
FN(lsGet) {
    CTX; std::lock_guard<std::mutex> g(g_ls_mu); LsOrigin &L = ls_get(S(0));
    v8::Local<v8::Context> cx = iso->GetCurrentContext();
    v8::Local<v8::Array> r = v8::Array::New(iso, (int)L.m.size() * 2); uint32_t i = 0;
    for (auto &kv : L.m) { r->Set(cx, i++, nstr(iso, kv.first.c_str())).Check(); r->Set(cx, i++, nstr(iso, kv.second.c_str())).Check(); }
    a.GetReturnValue().Set(r);
}
FN(lsSet) { CTX; std::lock_guard<std::mutex> g(g_ls_mu); LsOrigin &L = ls_get(S(0)); L.m[S(1)] = S(2); L.dirty = true; }
FN(lsDel) { CTX; std::lock_guard<std::mutex> g(g_ls_mu); LsOrigin &L = ls_get(S(0)); if (a.Length() < 2) L.m.clear(); else L.m.erase(S(1)); L.dirty = true; }
extern "C" void js_session_forget(void *ud) {
    for (auto it = g_session.begin(); it != g_session.end();) it = it->first.first == ud ? g_session.erase(it) : std::next(it);
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
    t.due = now_ms() + t.interval;
    RET(id);
}
FN(clearTimer) { CTX; c->timers.erase((uint32_t)NUM(0)); }
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
            if (str_ieq(ks.c_str(), "referer")) referer = true;
            headers_add(&rq->headers, ks.c_str(), vs.c_str());
        }
    }
    if (!referer && c->doc->url && !strncmp(c->doc->url, "http", 4)) headers_add(&rq->headers, "Referer", c->doc->url);
    const char *p; size_t n;
    if (a.Length() > 3 && bytes_of(a[3], &p, &n) && n) { rq->body = (char *)xmalloc(n); memcpy(rq->body, p, n); rq->body_len = n; }
    return rq;
}
static void resp_args(v8::Isolate *iso, NetResponse *r, v8::Local<v8::Value> out[6]) {
    bool err = !r || r->status == 0;
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
    if (f->script) {
        if (r && r->status >= 200 && r->status < 300 && !js_script_mime_blocked(headers_get(&r->headers, "content-type"))) js_run_script(c, f->script, r->body ? r->body : "", r->body ? r->body_len : 0, r->url ? r->url : "");
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
FN(audioDecode) {
    CTX; const char *p; size_t n; int frames = 0;
    if (!bytes_of(a[0], &p, &n) || !n) { RET(v8::Null(iso)); return; }
    float *s = media_decode_pcm((const uint8_t *)p, n, &frames);
    if (!s) { RET(v8::Null(iso)); return; }
    RET(mkab(iso, s, (size_t)frames * 8)); free(s);
}
FN(mediaNew) { CTX; ARGN(n, 0); uint32_t id = c->next_player++; c->players[id] = mp_new(n); RET((double)id); }
FN(mediaFree) { CTX; MP; mp_free(mp); c->players.erase(mpit); }
FN(mediaOpen) { CTX; MP; mp_open_url(mp, S(1).c_str()); }
/* Blob -> temp file so <video>/<audio> play blob: URLs through the ordinary file path */
FN(blobFile) {
    CTX; const char *p; size_t n;
    if (!bytes_of(a[0], &p, &n)) return;
    const char *td = getenv("TMPDIR"); std::string path = td && *td ? td : "/tmp";
    if (path.back() != '/') path += '/';
    path += "lumen-blob-XXXXXX";
    int fd = mkstemp(&path[0]); if (fd < 0) return;
    size_t off = 0; ssize_t w;
    while (off < n && (w = write(fd, p + off, n - off)) > 0) off += (size_t)w;
    close(fd);
    if (off < n) { unlink(path.c_str()); return; }
    a.GetReturnValue().Set(v8::String::NewFromUtf8(iso, ("file://" + path).c_str()).ToLocalChecked());
}
/* blob: media temp files whose object URL was never revoked; skip recent ones another
 * running Lumen may still be playing */
extern "C" void js_blob_cleanup(void) {
    const char *td = getenv("TMPDIR");
    std::string d = td && *td ? td : "/tmp";
    if (d.back() != '/') d += '/';
    DIR *dir = opendir(d.c_str()); if (!dir) return;
    time_t now = time(NULL);
    for (struct dirent *e; (e = readdir(dir));) {
        if (strncmp(e->d_name, "lumen-blob-", 11)) continue;
        std::string p = d + e->d_name; struct stat st;
        if (!stat(p.c_str(), &st) && S_ISREG(st.st_mode) && now - st.st_mtime > 24 * 3600) unlink(p.c_str());
    }
    closedir(dir);
}

FN(blobFileFree) {
    CTX; std::string u = jcstr(iso, a[0]);
    if (!u.compare(0, 7, "file://") && u.find("/lumen-blob-") != std::string::npos) unlink(u.c_str() + 7);
}
FN(mediaAddBuffer) { CTX; MP; RET((double)mp_add_buffer(mp, S(1).c_str())); }
FN(mediaAppend) {
    CTX; MP; const char *p; size_t n;
    if (!bytes_of(a[2], &p, &n)) return;
    RET(v8::Boolean::New(iso, mp_append(mp, (int)NUM(1), (const uint8_t *)p, n)));
}
FN(mediaRemove) { CTX; MP; mp_remove(mp, (int)NUM(1), NUM(2), NUM(3)); }
FN(mediaAbort) { CTX; MP; mp_abort(mp, (int)NUM(1)); }
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
FN(abort) {
    CTX; uint64_t id = (uint64_t)NUM(0);
    auto it = c->fetches.find(id);
    if (it == c->fetches.end()) return;
    net_cancel(id);
    it->second->c = nullptr; it->second->cb.Reset(); it->second->head.Reset(); it->second->chunk.Reset();
    c->fetches.erase(it);
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
FN(coi) { CTX; RET(c->doc->cross_origin_isolated); }
FN(currentScript) { CTX; RET(jwrap(c, c->current_script)); }
FN(media) { CTX; std::string q = S(0); RET(jsg_media(c->host.media, q.c_str())); }
FN(cssSelText) { CTX; std::string q = S(0); char *r = css_selector_text(q.c_str()); RET(nstr(iso, r)); free(r); }
FN(cssSupports) { CTX; std::string q = S(0); RET(jsg_supports(q.c_str())); }
FN(urlParse) {
    CTX; std::string s = S(0);
    URL u = {}, b = {};
    bool ok;
    if (a[1]->IsNullOrUndefined()) ok = url_parse(s.c_str(), &u);
    else {
        std::string bs = S(1);
        ok = url_parse(bs.c_str(), &b) && url_resolve(&b, s.c_str(), &u);
        url_free(&b);
    }
    if (!ok || !u.scheme) { url_free(&u); RET(v8::Null(iso)); return; }
    char *href = url_to_string(&u);
    std::string user, pass;
    if (u.userinfo) { const char *col = strchr(u.userinfo, ':'); user = col ? std::string(u.userinfo, (size_t)(col - u.userinfo)) : u.userinfo; if (col) pass = col + 1; }
    std::string hostname = u.host ? u.host : "";
    std::string port = u.has_port ? std::to_string(u.port) : "";
    std::string host = hostname + (port.empty() ? "" : ":" + port);
    std::string origin = "null";
    if (!u.opaque && u.host) { char *o = url_origin(&u); if (o) { origin = o; free(o); } }
    std::string f[11] = { href ? href : s, std::string(u.scheme) + ":", user, pass, host, hostname, port, u.path ? u.path : "",
                          u.query && *u.query ? std::string("?") + u.query : "", u.fragment && *u.fragment ? std::string("#") + u.fragment : "", origin };
    free(href);
    url_free(&u);
    v8::Local<v8::Array> r = v8::Array::New(iso, 11);
    for (int i = 0; i < 11; i++) (void)r->Set(ctx, i, jstr(iso, f[i].c_str(), (int)f[i].size()));
    RET(r);
}
FN(encode) { CTX; v8::String::Utf8Value u(iso, a[0]); RET(mkab(iso, *u ? *u : "", *u ? (size_t)u.length() : 0)); }
FN(decode) {
    CTX; const char *p = ""; size_t n = 0;
    bytes_of(a[0], &p, &n);
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { p += 3; n -= 3; }
    RET(jstr(iso, p, (int)n));
}

/* SubtleCrypto primitives (OpenSSL). Keys cross the bridge as bytes: EC private = PKCS#8 DER, EC public = uncompressed point. */
static v8::Local<v8::Value> cbuf(v8::Isolate *iso, const void *p, size_t n) {
    auto ab = v8::ArrayBuffer::New(iso, n);
    if (n) memcpy(ab->Data(), p, n);
    return ab;
}
static void cb_in(v8::Local<v8::Value> v, const uint8_t **p, size_t *n) {
    *p = (const uint8_t *)""; *n = 0;
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); *n = ab->ByteLength(); if (*n) *p = (const uint8_t *)ab->Data(); }
    else if (v->IsArrayBufferView()) { auto vw = v.As<v8::ArrayBufferView>(); *n = vw->ByteLength(); if (*n) *p = (const uint8_t *)vw->Buffer()->Data() + vw->ByteOffset(); }
}
#define IN(i, p, n) const uint8_t *p; size_t n; cb_in(a[i], &p, &n)
static const EVP_MD *md_of(const std::string &h) {
    if (h == "SHA-1") return EVP_sha1();
    if (h == "SHA-256") return EVP_sha256();
    if (h == "SHA-384") return EVP_sha384();
    if (h == "SHA-512") return EVP_sha512();
    return nullptr;
}
static const char *curve_of(const std::string &c) { return c == "P-256" ? "prime256v1" : c == "P-384" ? "secp384r1" : c == "P-521" ? "secp521r1" : nullptr; }
static size_t curve_bytes(const std::string &c) { return c == "P-256" ? 32 : c == "P-384" ? 48 : 66; }
static const char *curve_name(EVP_PKEY *k) {
    char g[64] = ""; size_t gl = 0;
    if (!EVP_PKEY_get_utf8_string_param(k, OSSL_PKEY_PARAM_GROUP_NAME, g, sizeof g, &gl)) return nullptr;
    return !strcmp(g, "prime256v1") ? "P-256" : !strcmp(g, "secp384r1") ? "P-384" : !strcmp(g, "secp521r1") ? "P-521" : nullptr;
}
static EVP_PKEY *ec_fromdata(const std::string &curve, const uint8_t *d, size_t dn, const uint8_t *pub, size_t pn) {
    const char *g = curve_of(curve); if (!g) return nullptr;
    OSSL_PARAM_BLD *b = OSSL_PARAM_BLD_new(); BIGNUM *bn = d ? BN_bin2bn(d, (int)dn, nullptr) : nullptr;
    OSSL_PARAM_BLD_push_utf8_string(b, OSSL_PKEY_PARAM_GROUP_NAME, g, 0);
    OSSL_PARAM_BLD_push_octet_string(b, OSSL_PKEY_PARAM_PUB_KEY, pub, pn);
    if (bn) OSSL_PARAM_BLD_push_BN(b, OSSL_PKEY_PARAM_PRIV_KEY, bn);
    OSSL_PARAM *ps = OSSL_PARAM_BLD_to_param(b);
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr); EVP_PKEY *k = nullptr;
    if (c && ps && EVP_PKEY_fromdata_init(c) > 0) EVP_PKEY_fromdata(c, &k, bn ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, ps);
    EVP_PKEY_CTX_free(c); OSSL_PARAM_free(ps); OSSL_PARAM_BLD_free(b); BN_free(bn);
    return k;
}
static EVP_PKEY *ec_priv(const uint8_t *p, size_t n) {
    const unsigned char *q = p; PKCS8_PRIV_KEY_INFO *i = d2i_PKCS8_PRIV_KEY_INFO(nullptr, &q, (long)n);
    if (!i) return nullptr;
    EVP_PKEY *k = EVP_PKCS82PKEY(i); PKCS8_PRIV_KEY_INFO_free(i); return k;
}
static v8::Local<v8::Value> pkcs8_of(v8::Isolate *iso, EVP_PKEY *k) {
    PKCS8_PRIV_KEY_INFO *i = EVP_PKEY2PKCS8(k); if (!i) return v8::Undefined(iso);
    unsigned char *o = nullptr; int l = i2d_PKCS8_PRIV_KEY_INFO(i, &o); PKCS8_PRIV_KEY_INFO_free(i);
    if (l <= 0) return v8::Undefined(iso);
    auto r = cbuf(iso, o, (size_t)l); OPENSSL_free(o); return r;
}
static v8::Local<v8::Value> pubraw_of(v8::Isolate *iso, EVP_PKEY *k) {
    EVP_PKEY_set_utf8_string_param(k, OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT, "uncompressed");
    size_t l = 0; if (!EVP_PKEY_get_octet_string_param(k, OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &l)) return v8::Undefined(iso);
    std::vector<uint8_t> o(l); if (!EVP_PKEY_get_octet_string_param(k, OSSL_PKEY_PARAM_PUB_KEY, o.data(), l, &l)) return v8::Undefined(iso);
    return cbuf(iso, o.data(), l);
}
FN(cDigest) {
    CTX; const EVP_MD *md = md_of(S(0)); IN(1, p, n); if (!md) return;
    unsigned char o[EVP_MAX_MD_SIZE]; unsigned ol = 0;
    if (EVP_Digest(p, n, o, &ol, md, nullptr)) RET(cbuf(iso, o, ol));
}
FN(cHmac) {
    CTX; const EVP_MD *md = md_of(S(0)); IN(1, k, kn); IN(2, p, n); if (!md) return;
    unsigned char o[EVP_MAX_MD_SIZE]; unsigned ol = 0;
    if (HMAC(md, k, (int)kn, p, n, o, &ol)) RET(cbuf(iso, o, ol));
}
FN(cAes) {
    CTX; std::string mode = S(0); bool enc = BOOL(1); IN(2, k, kn); IN(3, iv, ivn); IN(4, d, dn); IN(5, ad, adn); int tag = (int)NUM(6) / 8;
    bool gcm = mode == "AES-GCM", cbc = mode == "AES-CBC", ctr = mode == "AES-CTR";
    const EVP_CIPHER *ciph = nullptr;
    if (kn == 16) ciph = gcm ? EVP_aes_128_gcm() : cbc ? EVP_aes_128_cbc() : ctr ? EVP_aes_128_ctr() : nullptr;
    else if (kn == 24) ciph = gcm ? EVP_aes_192_gcm() : cbc ? EVP_aes_192_cbc() : ctr ? EVP_aes_192_ctr() : nullptr;
    else if (kn == 32) ciph = gcm ? EVP_aes_256_gcm() : cbc ? EVP_aes_256_cbc() : ctr ? EVP_aes_256_ctr() : nullptr;
    if (!ciph) return;
    EVP_CIPHER_CTX *x = EVP_CIPHER_CTX_new(); std::vector<uint8_t> out(dn + 48); int l1 = 0, l2 = 0; bool ok = false;
    do {
        if (!x || !EVP_CipherInit_ex(x, ciph, nullptr, nullptr, nullptr, enc)) break;
        if (gcm && !EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_SET_IVLEN, (int)ivn, nullptr)) break;
        if (!EVP_CipherInit_ex(x, nullptr, nullptr, k, iv, enc)) break;
        size_t body = dn;
        if (gcm) {
            if (tag <= 0 || tag > 16) break;
            if (!enc) { if (dn < (size_t)tag) break; body = dn - (size_t)tag; if (!EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_SET_TAG, tag, (void *)(d + body))) break; }
            if (adn && !EVP_CipherUpdate(x, nullptr, &l1, ad, (int)adn)) break;
        }
        if (!EVP_CipherUpdate(x, out.data(), &l1, d, (int)body)) break;
        if (!EVP_CipherFinal_ex(x, out.data() + l1, &l2)) break;
        size_t total = (size_t)(l1 + l2);
        if (gcm && enc) { if (!EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_GET_TAG, tag, out.data() + total)) break; total += (size_t)tag; }
        out.resize(total); ok = true;
    } while (0);
    EVP_CIPHER_CTX_free(x);
    if (ok) RET(cbuf(iso, out.data(), out.size()));
}
FN(cEcGen) {
    CTX; const char *g = curve_of(S(0)); if (!g) return;
    EVP_PKEY *k = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", g); if (!k) return;
    auto arr = v8::Array::New(iso, 2);
    (void)arr->Set(ctx, 0, pkcs8_of(iso, k)); (void)arr->Set(ctx, 1, pubraw_of(iso, k));
    EVP_PKEY_free(k); RET(arr);
}
FN(cEcDerive) {
    CTX; std::string curve = S(0); IN(1, pv, pvn); IN(2, pb, pbn);
    EVP_PKEY *pk = ec_priv(pv, pvn), *peer = ec_fromdata(curve, nullptr, 0, pb, pbn);
    EVP_PKEY_CTX *pc = pk ? EVP_PKEY_CTX_new(pk, nullptr) : nullptr; size_t l = 0; std::vector<uint8_t> o; bool ok = false;
    if (pc && peer && EVP_PKEY_derive_init(pc) > 0 && EVP_PKEY_derive_set_peer(pc, peer) > 0 && EVP_PKEY_derive(pc, nullptr, &l) > 0) { o.resize(l); ok = EVP_PKEY_derive(pc, o.data(), &l) > 0; }
    EVP_PKEY_CTX_free(pc); EVP_PKEY_free(pk); EVP_PKEY_free(peer);
    if (ok) RET(cbuf(iso, o.data(), l));
}
FN(cEcSign) {
    CTX; std::string curve = S(0); const EVP_MD *md = md_of(S(1)); IN(2, pv, pvn); IN(3, d, dn); size_t cb = curve_bytes(curve);
    EVP_PKEY *pk = ec_priv(pv, pvn); EVP_MD_CTX *m = EVP_MD_CTX_new(); size_t l = 0; std::vector<uint8_t> der, o; bool ok = false;
    if (pk && md && m && EVP_DigestSignInit(m, nullptr, md, nullptr, pk) > 0 && EVP_DigestSign(m, nullptr, &l, d, dn) > 0) {
        der.resize(l);
        if (EVP_DigestSign(m, der.data(), &l, d, dn) > 0) {
            const unsigned char *q = der.data(); ECDSA_SIG *sg = d2i_ECDSA_SIG(nullptr, &q, (long)l);
            if (sg) { const BIGNUM *r, *s2; ECDSA_SIG_get0(sg, &r, &s2); o.resize(2 * cb); ok = BN_bn2binpad(r, o.data(), (int)cb) > 0 && BN_bn2binpad(s2, o.data() + cb, (int)cb) > 0; ECDSA_SIG_free(sg); }
        }
    }
    EVP_MD_CTX_free(m); EVP_PKEY_free(pk);
    if (ok) RET(cbuf(iso, o.data(), o.size()));
}
FN(cEcVerify) {
    CTX; std::string curve = S(0); const EVP_MD *md = md_of(S(1)); IN(2, pb, pbn); IN(3, d, dn); IN(4, sig, sn); size_t cb = curve_bytes(curve);
    bool ok = false;
    if (md && sn == 2 * cb) {
        EVP_PKEY *pk = ec_fromdata(curve, nullptr, 0, pb, pbn); ECDSA_SIG *sg = ECDSA_SIG_new();
        ECDSA_SIG_set0(sg, BN_bin2bn(sig, (int)cb, nullptr), BN_bin2bn(sig + cb, (int)cb, nullptr));
        unsigned char *der = nullptr; int dl = i2d_ECDSA_SIG(sg, &der); EVP_MD_CTX *m = EVP_MD_CTX_new();
        ok = pk && dl > 0 && m && EVP_DigestVerifyInit(m, nullptr, md, nullptr, pk) > 0 && EVP_DigestVerify(m, der, (size_t)dl, d, dn) == 1;
        EVP_MD_CTX_free(m); OPENSSL_free(der); ECDSA_SIG_free(sg); EVP_PKEY_free(pk);
    }
    RET(v8::Boolean::New(iso, ok));
}
FN(cSpki) {
    CTX; IN(1, pb, pbn); EVP_PKEY *k = ec_fromdata(S(0), nullptr, 0, pb, pbn); if (!k) return;
    unsigned char *o = nullptr; int l = i2d_PUBKEY(k, &o); EVP_PKEY_free(k);
    if (l > 0) RET(cbuf(iso, o, (size_t)l));
    OPENSSL_free(o);
}
FN(cSpkiParse) {
    CTX; IN(0, p, n); const unsigned char *q = p; EVP_PKEY *k = d2i_PUBKEY(nullptr, &q, (long)n); if (!k) return;
    const char *cn = curve_name(k);
    if (cn) { auto arr = v8::Array::New(iso, 2); (void)arr->Set(ctx, 0, jstr(iso, cn)); (void)arr->Set(ctx, 1, pubraw_of(iso, k)); RET(arr); }
    EVP_PKEY_free(k);
}
FN(cPkcs8Parse) {
    CTX; IN(0, p, n); EVP_PKEY *k = ec_priv(p, n); if (!k) return;
    const char *cn = curve_name(k); BIGNUM *bn = nullptr;
    if (cn && EVP_PKEY_get_bn_param(k, OSSL_PKEY_PARAM_PRIV_KEY, &bn)) {
        size_t cb = curve_bytes(cn); std::vector<uint8_t> d(cb); BN_bn2binpad(bn, d.data(), (int)cb);
        auto arr = v8::Array::New(iso, 3);
        (void)arr->Set(ctx, 0, jstr(iso, cn)); (void)arr->Set(ctx, 1, cbuf(iso, d.data(), cb)); (void)arr->Set(ctx, 2, pubraw_of(iso, k));
        RET(arr);
    }
    BN_free(bn); EVP_PKEY_free(k);
}
FN(cEcFromD) {
    CTX; IN(1, d, dn); IN(2, pb, pbn); EVP_PKEY *k = ec_fromdata(S(0), d, dn, pb, pbn); if (!k) return;
    RET(pkcs8_of(iso, k)); EVP_PKEY_free(k);
}
FN(cHkdf) {
    CTX; const EVP_MD *md = md_of(S(0)); IN(1, k, kn); IN(2, salt, sn); IN(3, info, in); size_t len = (size_t)NUM(4);
    if (!md || !len) return;
    EVP_PKEY_CTX *hc = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr); std::vector<uint8_t> o(len); size_t ol = len;
    bool ok = hc && EVP_PKEY_derive_init(hc) > 0 && EVP_PKEY_CTX_set_hkdf_md(hc, md) > 0 && EVP_PKEY_CTX_set1_hkdf_salt(hc, salt, (int)sn) > 0 &&
              EVP_PKEY_CTX_set1_hkdf_key(hc, k, (int)kn) > 0 && EVP_PKEY_CTX_add1_hkdf_info(hc, info, (int)in) > 0 && EVP_PKEY_derive(hc, o.data(), &ol) > 0;
    EVP_PKEY_CTX_free(hc);
    if (ok) RET(cbuf(iso, o.data(), ol));
}
FN(cPbkdf2) {
    CTX; const EVP_MD *md = md_of(S(0)); IN(1, pw, pn); IN(2, salt, sn); int iter = (int)NUM(3); size_t len = (size_t)NUM(4);
    if (!md || !len || iter < 1) return;
    std::vector<uint8_t> o(len);
    if (PKCS5_PBKDF2_HMAC((const char *)pw, (int)pn, salt, (int)sn, iter, md, (int)len, o.data())) RET(cbuf(iso, o.data(), len));
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
FN(imgSize) {
    CTX; ARGN(n, 0); float w, h;
    if (!jsg_img_size(n, &w, &h)) { RET(v8::Null(iso)); return; }
    v8::Local<v8::Value> v[2] = { v8::Number::New(iso, w), v8::Number::New(iso, h) };
    RET(v8::Array::New(iso, v, 2));
}
FN(userAgent) { CTX; RET(jstr(iso, g_user_agent)); }
FN(platform) {
    CTX;
#ifdef __APPLE__
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
            char *t = src ? nullptr : node_text_content(s), *u = src && *src ? url_join(c->doc->url, src) : nullptr;
            if (src && !u) jfire(c, s, "error");
            else js_run_module(c, s, t ? t : (src ? nullptr : ""), t ? strlen(t) : 0, u ? u : c->doc->url);
            free(t); free(u);
            continue;
        }
        if (!src) {
            char *t = node_text_content(s);
            js_run_script(c, s, t ? t : "", t ? strlen(t) : 0, c->doc->url);
            free(t);
            continue;
        }
        char *u = *src ? url_join(c->doc->url, src) : nullptr;
        if (!u) { jfire(c, s, "error"); continue; }
        jwrap(c, s);
        NetRequest *rq = net_request_new("GET", u);
        if (c->doc->url && !strncmp(c->doc->url, "http", 4)) headers_add(&rq->headers, "Referer", c->doc->url);
        start_fetch(c, rq, new Fetch{ c, 0, {}, s });
        free(u);
    }
    frames_inserted(c, root);
}

void js_install_native(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(isNode); REG(version);
    REG(inert); REG(type); REG(name); REG(ns); REG(parent); REG(first); REG(last); REG(next); REG(prev);
    REG(text); REG(setText); REG(attr); REG(setAttr); REG(rmAttr); REG(attrs); REG(insert); REG(remove); REG(newDoc); REG(newXmlDoc); REG(parseDoc); REG(adopt); REG(pi); REG(cdata); REG(doctype); REG(ownerDoc);
    REG(create); REG(textNode); REG(comment); REG(frag); REG(html); REG(setHTML); REG(parseFrag); REG(query);
    REG(matches); REG(byId); REG(clone); REG(doc); REG(contains); REG(connected); REG(host); REG(attachShadow);
    REG(templateContent); REG(rect); REG(vrect); REG(clipSet); REG(clipGet); REG(scrollPos); REG(setScroll); REG(computed); REG(value); REG(setValue); REG(checked); REG(setChecked); REG(setPseudo); REG(pickFiles);
    REG(focus); REG(active); REG(cookie); REG(setCookie); REG(url); REG(setUrl); REG(navigate); REG(navigatePost); REG(saveFile); REG(histGo);
    REG(histLen); REG(ssGet); REG(ssSet); REG(lsGet); REG(lsSet); REG(lsDel);
    REG(busPost);
    REG(busOrigin); REG(timer); REG(clearTimer); REG(raf); REG(cancelRaf); REG(now); REG(fetch); REG(fetchSync);
    REG(abort); REG(mediaNew); REG(mediaFree); REG(mediaOpen); REG(blobFile); REG(blobFileFree); REG(mediaAddBuffer); REG(mediaAppend); REG(mediaRemove); REG(mediaAbort);
    REG(mediaBuffered); REG(mediaEos); REG(mediaSetDuration); REG(mediaPlay); REG(mediaPause); REG(mediaSeek);
    REG(mediaVolume); REG(mediaState); REG(mediaCanPlay); REG(log); REG(logLevel); REG(wsOpen); REG(wsSend); REG(wsClose); REG(viewport); REG(scrollTo); REG(hit); REG(ceScan); REG(selSet); REG(waOpen); REG(waPush); REG(waQueued); REG(waPause); REG(waClose); REG(audioDecode); REG(readyState); REG(quirks); REG(coi);
    REG(currentScript); REG(media); REG(cssSupports); REG(cssSelText); REG(urlParse); REG(encode); REG(decode); REG(random); REG(cDigest); REG(cHmac); REG(cAes); REG(cEcGen); REG(cEcDerive); REG(cEcSign); REG(cEcVerify); REG(cSpki); REG(cSpkiParse); REG(cPkcs8Parse); REG(cEcFromD); REG(cHkdf); REG(cPbkdf2);
    REG(heap); REG(imgSize); REG(userAgent); REG(platform); REG(cpus); REG(makeAll);
#undef REG
    js_install_frames(c, N); js_install_workers(c, N); js_install_canvas(c, N); js_install_webgpu(c, N); js_install_capture(c, N);
}
