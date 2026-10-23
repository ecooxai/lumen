/* Nested browsing contexts (<iframe>): child V8 contexts sharing the parent's isolate */
#include "js_int.h"
#include "jsglue.h"
#include "../base/url.h"
#include <algorithm>

using FCI = v8::FunctionCallbackInfo<v8::Value>;

struct FrameLoad { JsCtx *p; Node *f; uint32_t kid; std::string url; };
/* postMessage to another tab's context: serialized by the sender, read by the target's next tick */
/* A message for another tab: a window message, or with port set a message for the bridged port with that key.
   keys are the transferred ports' bridge keys (see exportPort in 07b-net.js). */
struct XMsg { uint32_t to; std::vector<uint8_t> data; std::string origin; double src; double port; std::vector<double> keys; };
static std::vector<XMsg> g_xmsgs;
static std::vector<FrameLoad *> g_loads;

static std::string origin_of(const char *u) {
    URL x{};
    if (u && str_ieqn(u, "blob:", 5) && (str_ieqn(u + 5, "http:", 5) || str_ieqn(u + 5, "https:", 6))) u += 5;   /* a blob URL's origin is its inner URL's */
    if (!u || !url_parse(u, &x)) return "null";
    char *o = url_origin(&x);
    std::string r = o && *o ? o : "null";
    free(o); url_free(&x);
    return r;
}
std::string ctx_origin(JsCtx *c) {
    if (c->parent && (!c->doc->url || !strncmp(c->doc->url, "about:", 6))) return ctx_origin(c->parent);
    return origin_of(c->doc->url);
}
std::string ctx_origin_key(JsCtx *c) {
    if (c->parent && (!c->doc->url || !strncmp(c->doc->url, "about:", 6))) return ctx_origin_key(c->parent);
    const char *u = c->doc->url;
    if (u && str_ieqn(u, "file:", 5)) return std::string(u, strcspn(u, "#"));
    return origin_of(u);
}
static bool same_origin(JsCtx *a, JsCtx *b) { return a == b || ctx_origin_key(a) == ctx_origin_key(b); }

/* t as v sees it: its global when they share an isolate and origin, else a number for asWin, a tab's
   top context being -its window id (tabs have isolates of their own) */
static double remote_id(JsCtx *t) { return !t->parent && t->wid ? -(double)t->wid : (double)t->id; }
static v8::Local<v8::Value> win_for(JsCtx *v, JsCtx *t) {
    v8::Isolate *iso = v->iso;
    if (!t || t->dead || t->ctx.IsEmpty()) return v8::Null(iso);
    if (t->iso == v->iso && same_origin(v, t)) return t->ctx.Get(iso)->Global();
    return v8::Number::New(iso, t->iso == v->iso ? t->id : remote_id(t));
}
static JsCtx *root_of(JsCtx *c) { while (c->parent) c = c->parent; return c; }
static JsCtx *by_id(JsCtx *any, uint32_t id) { (void)any; return js_ctx_by_id(id); }

JsCtx *js_frame_ctx(Node *f) {
    JsCtx *p = f && f->doc ? owner_ctx(f->doc) : nullptr;
    if (!p) return nullptr;
    for (JsCtx *k : p->kids) if (!k->dead && k->frame_el == f) return k;
    return nullptr;
}
void frame_nav(JsCtx *k, const char *u);
void js_frame_navigate(Node *f, const char *u) { if (JsCtx *k = js_frame_ctx(f)) frame_nav(k, u); }
Document *js_frame_doc(Node *f) { JsCtx *k = js_frame_ctx(f); return k ? k->doc : nullptr; }

void frame_kill(JsCtx *k) {
    if (k->dead) return;
    k->dead = true;
    for (JsCtx *g : k->kids) frame_kill(g);
    for (auto &kv : k->fetches) { net_cancel(kv.first); kv.second->c = nullptr; kv.second->cb.Reset(); kv.second->head.Reset(); kv.second->chunk.Reset(); }
    k->fetches.clear(); for (auto &kv : k->sockets) { net_ws_release(kv.second->ws); kv.second->cb.Reset(); delete kv.second; } k->sockets.clear();
    k->timer_clear(); k->rafs.clear(); k->anims.clear();
    if (k->host.frame_close && k->host_ud) k->host.frame_close(k->host_ud);
    k->host_ud = nullptr;
}
void frames_forget(JsCtx *c) { for (FrameLoad *l : g_loads) if (l->p == c) l->p = nullptr; }

static std::string src_key(JsCtx *p, Node *f) {
    if (const char *sd = node_attr(f, "srcdoc")) return std::string("srcdoc:") + sd;
    const char *src = node_attr(f, "src");
    char *u = src && *src ? url_join(doc_base(p->doc), src) : nullptr;
    std::string r = u ? u : "";
    free(u);
    return r;
}

static void frame_run_scripts(JsCtx *k, Document *d);
/* key: the frame's src_key, set before its load event fires (a handler that rescans the frames must not see it
   as stale and restart it) */
static JsCtx *frame_create(JsCtx *p, Node *f, const std::string &key, const char *url, const char *html, size_t n, bool fire_load,
                           const char *ctype = nullptr, const Headers *hdrs = nullptr) {
    if (JsCtx *old = js_frame_ctx(f)) frame_kill(old);
    Document *d = doc_new(url);
    if (!strncmp(url, "about:", 6)) d->base_url = xstrdup(doc_base(p->doc));   /* about:blank and about:srcdoc inherit the parent's */
    if (hdrs) { d->csp = headers_join(hdrs, "content-security-policy"); d->csp_ro = headers_join(hdrs, "content-security-policy-report-only"); }
    bool xml = xml_mime(ctype);
    char *decoded = nullptr;
    if (ctype && html) {   /* a response (not about:blank or srcdoc): decoded by its encoding */
        int enc; size_t dn = 0;
        if ((decoded = enc_document((const uint8_t *)html, n, ctype, xml, &enc, &dn))) { html = decoded; n = dn; }
        d->charset = enc;
    }
    if (xml) { xml_parse(d, html ? html : "", html ? n : 0); d->content_type = mime_essence(ctype); }
    else html_parse(d, html ? html : "", html ? n : 0);
    free(decoded);
    JsHost ch{};
    ch.viewport = p->host.viewport;
    if (p->host.frame_open) p->host.frame_open(p->host.ud, f, d, &ch);
    if (!ch.frame_open) ch.frame_open = p->host.frame_open;
    ch.frame_close = p->host.frame_close;
    JsCtx *k = js_new_ex(d, &ch, p, f);
    k->host_ud = ch.ud;
    k->frame_src = key;
    if (const char *fp = getenv("LUMEN_FRAME_PRE")) if (strncmp(url, "about:", 6)) js_eval(k, fp, "lumen:frame-pre");
    frame_run_scripts(k, d);
    if (k->dead) return k;
    js_set_ready_state(k, 1);
    js_dispatch(k, &d->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
    k->el_load = fire_load;
    if (!k->dead) js_fire_load(k);
    return k;
}
/* document.close() of a document script opened: its written markup is parsed as a whole, its scripts run in
   order, then it loads as a frame's document does (its <iframe> gets the load event) */
void js_doc_reparse(JsCtx *k, const char *html, size_t n) {
    Document *d = k->doc;
    if (d->node.first) return;   /* open() emptied it */
    d->html = d->head = d->body = NULL; d->quirks = false;
    js_set_ready_state(k, 0);
    html_parse(d, html, n);
    frame_run_scripts(k, d);
    if (k->dead) return;
    js_set_ready_state(k, 1);
    js_dispatch(k, &d->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
    k->el_load = k->frame_el != nullptr;
    if (!k->dead) js_fire_load(k);
}
static void frame_run_scripts(JsCtx *k, Document *d) {
    for (Node *s = d->node.first; s && !k->dead; s = node_next_in_tree(s, &d->node)) {
        if (s->type != NODE_ELEMENT || s->tag != A_script || s->ns != NS_HTML || (s->flags & NF_SCRIPT_STARTED)) continue;
        bool mod = jsg_module_script(s);
        if (!mod && !jsg_classic_script(s)) continue;
        const char *src = node_attr(s, "src");
        if (mod) {
            char *t = src ? nullptr : node_text_content(s), *u = src ? url_join(doc_base(d), src) : nullptr;
            js_run_module(k, s, t ? t : (src ? nullptr : ""), t ? strlen(t) : 0, u ? u : d->url);
            free(t); free(u);
        } else if (src) {
            char *u = url_join(doc_base(d), src);
            NetResponse *sr = u ? net_fetch_sync(net_request_new("GET", u)) : nullptr;
            if (sr && sr->status >= 200 && sr->status < 300) js_run_script(k, s, sr->body ? sr->body : "", sr->body_len, u);
            else { s->flags |= NF_SCRIPT_STARTED; js_dispatch(k, s, "error", "Event", false, false, 0, 0, 0, NULL); }
            if (sr) net_response_free(sr);
            free(u);
        } else {
            char *t = node_text_content(s);
            js_run_script(k, s, t ? t : "", t ? strlen(t) : 0, d->url);
            free(t);
        }
    }
}

/* An iframe still fetching its document, or whose document still waits for its own frames, delays its
   parent's load event. A frame removed meanwhile no longer does. */
static bool frames_loading(JsCtx *c) {
    for (FrameLoad *l : g_loads) if (l->p == c) for (JsCtx *k : c->kids) if (!k->dead && k->id == l->kid && k->frame_el == l->f) return true;
    for (JsCtx *k : c->kids) if (!k->dead && k->load_deferred) return true;
    return false;
}
void js_fire_load(JsCtx *c) {
    if (!c || c->dead) return;
    c->load_deferred = false;   /* a frame that finishes during the scan must not fire this load from within it */
    frames_scan(c);   /* the frames its parser inserted start loading now */
    if (c->dead) return;
    if (frames_loading(c)) { c->load_deferred = true; return; }
    js_set_ready_state(c, 2);
    js_dispatch_window(c, "load");
    JsCtx *p = c->parent;
    if (!p || c->dead || p->dead) return;
    if (c->el_load) { c->el_load = false; js_dispatch(p, c->frame_el, "load", "Event", false, false, 0, 0, 0, NULL); }
    frames_load_check(p);
}
void frames_load_check(JsCtx *c) { if (c && !c->dead && c->load_deferred && !frames_loading(c)) js_fire_load(c); }

/* a response's Content-Type, "" when it has none (frame_create then sniffs its encoding) */
static const char *frame_ctype(NetResponse *r) { const char *t = headers_get(&r->headers, "content-type"); return t ? t : ""; }

static void frame_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq;
    FrameLoad *l = static_cast<FrameLoad *>(ud);
    g_loads.erase(std::remove(g_loads.begin(), g_loads.end(), l), g_loads.end());
    JsCtx *p = l->p, *blank = nullptr;
    if (p && !p->dead) for (JsCtx *k : p->kids) if (!k->dead && k->id == l->kid && k->frame_el == l->f) blank = k;
    if (blank) {
        std::string key = blank->frame_src;
        bool ok = r && r->status > 0;
        const char *body = ok && r->body ? r->body : "";
        bool blob = !strncmp(l->url.c_str(), "blob:", 5);   /* read through a data: URL, but a document at its blob URL */
        frame_create(p, l->f, key, ok && r->url && !blob ? r->url : l->url.c_str(), body, ok && r->body ? r->body_len : 0, true,
                     ok ? frame_ctype(r) : nullptr, ok ? &r->headers : nullptr);
    }
    delete l;
    frames_load_check(p);
}

/* ctype: a POST of body (a form submitted into the frame) */
/* a blob: URL's bytes as a data: URL the loader reads, from the realm that made it (its realm hook's blob) */
static std::string blob_as_data(JsCtx *p, const char *u) {
    JS_ENTER(p);
    v8::Local<v8::Value> realm, fn, res;
    if (!ctx->Global()->GetPrivate(ctx, v8::Private::ForApi(iso, jstr(iso, "lumen:realm"))).ToLocal(&realm) || !realm->IsObject()) return "";
    if (!realm.As<v8::Object>()->Get(ctx, jstr(iso, "blob")).ToLocal(&fn) || !fn->IsFunction()) return "";
    v8::Local<v8::Value> arg = jstr(iso, u);
    v8::TryCatch tc(iso);
    if (!fn.As<v8::Function>()->Call(ctx, realm, 1, &arg).ToLocal(&res) || !res->IsArray()) return "";
    v8::Local<v8::Array> r = res.As<v8::Array>();
    v8::Local<v8::Value> bytes, type;
    if (!r->Get(ctx, 0).ToLocal(&bytes) || !bytes->IsUint8Array() || !r->Get(ctx, 1).ToLocal(&type)) return "";
    v8::Local<v8::Uint8Array> b = bytes.As<v8::Uint8Array>();
    std::vector<uint8_t> raw(b->ByteLength());
    if (!raw.empty()) b->CopyContents(raw.data(), raw.size());
    v8::String::Utf8Value ts(iso, type);
    std::string t = *ts && **ts ? *ts : "text/plain";
    for (char &ch : t) if (ch == ',') ch = ';';
    static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "data:" + t + ";base64,";
    out.reserve(out.size() + raw.size() / 3 * 4 + 4);
    for (size_t i = 0; i < raw.size(); i += 3) {
        uint32_t v = raw[i] << 16 | (i + 1 < raw.size() ? raw[i + 1] << 8 : 0) | (i + 2 < raw.size() ? raw[i + 2] : 0);
        out += B64[v >> 18 & 63]; out += B64[v >> 12 & 63];
        out += i + 1 < raw.size() ? B64[v >> 6 & 63] : '=';
        out += i + 2 < raw.size() ? B64[v & 63] : '=';
    }
    return out;
}
static void frame_load(JsCtx *p, Node *f, const char *u, const std::string &key,
                       const char *body = nullptr, size_t blen = 0, const char *ctype = nullptr) {
    bool remote = u && *u && strncmp(u, "about:", 6) && strncmp(u, "javascript:", 11);
    JsCtx *k = frame_create(p, f, key, "about:blank", "", 0, !remote);
    if (!remote) return;
    std::string blob = !strncmp(u, "blob:", 5) ? blob_as_data(p, u) : "";
    NetRequest *rq = net_request_new(ctype ? "POST" : "GET", blob.empty() ? u : blob.c_str());
    if (ctype) {
        rq->body = (char *)xmalloc(blen + 1); if (blen) memcpy(rq->body, body, blen); rq->body[blen] = 0; rq->body_len = blen;
        headers_add(&rq->headers, "Content-Type", ctype);
    }
    if (p->doc->url && !strncmp(p->doc->url, "http", 4)) headers_add(&rq->headers, "Referer", p->doc->url);
    FrameLoad *l = new FrameLoad{ p, f, k->id, u };
    g_loads.push_back(l);
    rq->done = frame_done; rq->ud = l;
    net_fetch(rq);
}

static void frame_start(JsCtx *p, Node *f) {
    if (p->dead) return;
    std::string key = src_key(p, f);
    if (const char *sd = node_attr(f, "srcdoc")) { frame_create(p, f, key, "about:srcdoc", sd, strlen(sd), true); return; }
    frame_load(p, f, key.c_str(), key);
}

void frame_nav(JsCtx *k, const char *u) {
    if (!k->parent || k->dead) return;
    char *abs = url_join(doc_base(k->doc), u);
    if (abs) frame_load(k->parent, k->frame_el, abs, k->frame_src);
    free(abs);
}

/* The browsing context a link or form names in its target, seen from c: c itself for "", _self and
   _blank (Lumen opens no windows), and for names no frame carries. */
static JsCtx *named_ctx(JsCtx *c, const char *name) {
    std::vector<JsCtx *> st{ c };
    while (!st.empty()) {
        JsCtx *k = st.back(); st.pop_back();
        if (k != c && !k->dead) if (const char *n = node_attr(k->frame_el, "name")) if (!strcmp(n, name)) return k;
        for (auto it = k->kids.rbegin(); it != k->kids.rend(); ++it) st.push_back(*it);
    }
    return nullptr;
}
static JsCtx *target_ctx(JsCtx *c, const char *t) {
    if (!t || !*t || str_ieq(t, "_self") || str_ieq(t, "_blank")) return c;
    if (str_ieq(t, "_top")) return root_of(c);
    if (str_ieq(t, "_parent")) return c->parent ? c->parent : c;
    frames_scan(root_of(c));
    JsCtx *k = named_ctx(c, t);   /* its own frames first, then the whole page */
    if (!k) k = named_ctx(root_of(c), t);
    return k ? k : c;
}
bool js_navigate_target(JsCtx *c, const char *target, const char *url, const char *body, size_t n, const char *ctype) {
    JsCtx *t = c ? target_ctx(c, target) : nullptr;
    if (!t || t == c || t->dead || !url) return false;
    if (t->parent) {
        char *abs = url_join(doc_base(c->doc), url);
        if (abs) frame_load(t->parent, t->frame_el, abs, t->frame_src, body, n, ctype);
        free(abs);
    } else if (ctype && t->host.navigate_post) t->host.navigate_post(t->host.ud, url, body, n, ctype);
    else if (t->host.navigate) t->host.navigate(t->host.ud, url);
    return true;
}

static bool is_iframe(Node *n) { return n->type == NODE_ELEMENT && n->ns == NS_HTML && n->tag == A_iframe; }
static void collect_iframes(Node *root, std::vector<Node *> &out) {
    for (Node *n = root; n; n = node_next_in_tree(n, root)) {
        if (is_iframe(n) && !js_frame_ctx(n)) out.push_back(n);
        if (n->type == NODE_ELEMENT && n->shadow_root) collect_iframes(n->shadow_root, out);
    }
}

void frames_scan(JsCtx *c) {
    /* Not per DOM version: a page animating thousands of nodes would walk the whole tree every tick. */
    if (c->dead || c->doc->frame_version == c->frame_scan_ver) return;
    c->frame_scan_ver = c->doc->frame_version;
    for (JsCtx *k : std::vector<JsCtx *>(c->kids))
        if (!k->dead && (!(k->frame_el->flags & NF_CONNECTED) || k->frame_el->doc != c->doc || src_key(c, k->frame_el) != k->frame_src)) frame_kill(k);
    std::vector<Node *> todo;
    collect_iframes(&c->doc->node, todo);
    for (Node *n : todo) if (!js_frame_ctx(n)) frame_start(c, n);
}

void frames_inserted(JsCtx *c, Node *root) {
    if (!(root->flags & NF_CONNECTED)) return;
    JsCtx *p = owner_ctx(root->doc);
    if (!p) p = c;
    std::vector<Node *> todo;
    collect_iframes(root, todo);
    for (Node *x : todo) if (!js_frame_ctx(x)) frame_start(p, x);
}

#define FCTX JsCtx *c = jctx(a.GetIsolate()); v8::Isolate *iso = c->iso; (void)iso
static JsCtx *ensure_frame(Node *f) {
    JsCtx *k = js_frame_ctx(f);
    if (!k && (f->flags & NF_CONNECTED)) if (JsCtx *p = owner_ctx(f->doc)) { frame_start(p, f); k = js_frame_ctx(f); }
    return k;
}
static void n_frameWin(const FCI &a) { FCTX; Node *f = junwrap(a[0]); if (f) a.GetReturnValue().Set(win_for(c, ensure_frame(f))); else a.GetReturnValue().SetNull(); }
static void n_frameDoc(const FCI &a) {
    FCTX; Node *f = junwrap(a[0]); JsCtx *k = f ? ensure_frame(f) : nullptr;
    if (k && same_origin(c, k)) a.GetReturnValue().Set(jwrap(k, &k->doc->node)); else a.GetReturnValue().SetNull();
}
static void n_parentWin(const FCI &a) { FCTX; if (c->parent) a.GetReturnValue().Set(win_for(c, c->parent)); else a.GetReturnValue().SetNull(); }
static void n_topWin(const FCI &a) { FCTX; if (c->parent) a.GetReturnValue().Set(win_for(c, root_of(c))); else a.GetReturnValue().SetNull(); }
static void n_frameEl(const FCI &a) { FCTX; if (c->parent && same_origin(c, c->parent)) a.GetReturnValue().Set(jwrap(c->parent, c->frame_el)); else a.GetReturnValue().SetNull(); }
static int live_kids(JsCtx *c) { int n = 0; for (JsCtx *k : c->kids) n += !k->dead; return n; }
static void n_frameCount(const FCI &a) { FCTX; frames_scan(c); a.GetReturnValue().Set(live_kids(c)); }
static void n_frameAt(const FCI &a) {
    FCTX; int i = a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(-1);
    frames_scan(c);   /* a frame the parser just inserted gets its about:blank window now */
    for (JsCtx *k : c->kids) if (!k->dead && i-- == 0) { a.GetReturnValue().Set(win_for(c, k)); return; }
    a.GetReturnValue().SetUndefined();
}
static void n_ctxRel(const FCI &a) {
    FCTX; v8::Local<v8::Context> cx = iso->GetCurrentContext();
    JsCtx *t = by_id(c, (uint32_t)a[0]->Uint32Value(cx).FromMaybe(0));
    int op = a[1]->Int32Value(cx).FromMaybe(-1);
    switch (op) {
    case 0: a.GetReturnValue().Set(t && t->parent ? win_for(c, t->parent) : v8::Local<v8::Value>(v8::Null(iso))); break;
    case 1: a.GetReturnValue().Set(t && t->parent ? win_for(c, root_of(t)) : v8::Local<v8::Value>(v8::Null(iso))); break;
    case 2: a.GetReturnValue().Set(t ? live_kids(t) : 0); break;
    case 3: a.GetReturnValue().Set(!t); break;
    case 4: if (t) { std::string u = jcstr(iso, a[2]); frame_nav(t, u.c_str()); } break;
    }
}
/* navTarget(target, url[, body, ctype]): navigates the context target names, if not this one */
static void n_navTarget(const FCI &a) {
    FCTX;
    std::string t = jcstr(iso, a[0]), u = jcstr(iso, a[1]), b, ct;
    bool post = a.Length() > 3 && a[3]->IsString();
    if (post) { b = js_body_bytes(iso, a[2]); ct = jcstr(iso, a[3]); }
    a.GetReturnValue().Set(js_navigate_target(c, t.c_str(), u.c_str(), b.data(), b.size(), post ? ct.c_str() : nullptr));
}
static JsCtx *ctx_of_value(v8::Isolate *iso, JsCtx *c, v8::Local<v8::Value> v) {
    if (v->IsNumber()) { double d = v.As<v8::Number>()->Value(); return d < 0 ? js_ctx_of_window((uint32_t)-d) : by_id(c, (uint32_t)d); }
    if (!v->IsObject()) return nullptr;
    v8::Local<v8::Context> cx;
    if (!v.As<v8::Object>()->GetCreationContext(iso).ToLocal(&cx)) return nullptr;
    return static_cast<JsCtx *>(cx->GetAlignedPointerFromEmbedderData(1, kTag));
}
/* HTML "fully active": the context still shows its frame's document, its <iframe> is connected, and so on up */
static bool fully_active(JsCtx *c) {
    for (; c; c = c->parent) {
        if (c->dead) return false;
        if (!c->parent) return true;
        if (!c->frame_el || !(c->frame_el->flags & NF_CONNECTED) || js_frame_ctx(c->frame_el) != c) return false;
    }
    return true;
}
/* fullyActive(x): a node's document's realm, else an object's creation realm (DOM "inner invoke" skips listeners
   when either the target's or the callback's global is not fully active) */
static void n_fullyActive(const FCI &a) {
    FCTX;
    JsCtx *k = nullptr;
    if (Node *n = junwrap(a[0])) k = n->doc ? owner_ctx(n->doc) : nullptr;
    else k = ctx_of_value(iso, c, a[0]);
    if (!k && !a[0]->IsNumber()) k = c;
    a.GetReturnValue().Set(k ? fully_active(k) : true);
}
static void n_caller(const FCI &a) {
    FCTX;
    v8::Local<v8::Context> ec = iso->GetEnteredOrMicrotaskContext();
    JsCtx *s = ec.IsEmpty() ? c : static_cast<JsCtx *>(ec->GetAlignedPointerFromEmbedderData(1, kTag));
    if (!s) s = c;
    v8::Local<v8::Value> v[2] = { win_for(c, s), jstr(iso, ctx_origin(s).c_str()) };
    a.GetReturnValue().Set(v8::Array::New(iso, v, 2));
}
static void n_postTo(const FCI &a) {
    FCTX; v8::Local<v8::Context> cx = iso->GetCurrentContext();
    JsCtx *t = ctx_of_value(iso, c, a[0]);
    if (!t || t->dead || t->api.IsEmpty()) return;
    std::string want = jcstr(iso, a[2]);
    if (want == "/") want = ctx_origin(c);
    if (want != "*" && origin_of(want.c_str()) != ctx_origin(t)) return;
    v8::ValueSerializer ser(iso);
    ser.WriteHeader();
    if (!ser.WriteValue(cx, a[1]).FromMaybe(false)) return;
    std::pair<uint8_t *, size_t> buf = ser.Release();
    if (t->iso != iso) {   /* another tab: its next tick delivers it, transferred ports bridged by key */
        std::vector<double> keys;
        v8::Local<v8::Value> fn;
        if (a[3]->IsArray() && a[3].As<v8::Array>()->Length() && c->api.Get(iso)->Get(cx, jstr(iso, "exportPort")).ToLocal(&fn) && fn->IsFunction()) {
            v8::Local<v8::Array> ps = a[3].As<v8::Array>();
            for (uint32_t i = 0; i < ps->Length(); i++) {
                v8::Local<v8::Value> argv[2], k;
                if (!ps->Get(cx, i).ToLocal(&argv[0])) continue;
                argv[1] = v8::Number::New(iso, remote_id(t));
                if (fn.As<v8::Function>()->Call(cx, v8::Undefined(iso), 2, argv).ToLocal(&k) && k->IsNumber()) keys.push_back(k.As<v8::Number>()->Value());
            }
        }
        g_xmsgs.push_back({ t->id, std::vector<uint8_t>(buf.first, buf.first + buf.second), ctx_origin(c), remote_id(c), 0, std::move(keys) });
        free(buf.first);
        if (js_wakeup) js_wakeup();
        return;
    }
    {
        v8::Local<v8::Context> tc = t->ctx.Get(iso);
        v8::Context::Scope cs(tc);
        v8::ValueDeserializer des(iso, buf.first, buf.second);
        v8::Local<v8::Value> data, fn;
        if (des.ReadHeader(tc).FromMaybe(false) && des.ReadValue(tc).ToLocal(&data) &&
            t->api.Get(iso)->Get(tc, jstr(iso, "queueMessage")).ToLocal(&fn) && fn->IsFunction()) {
            v8::Local<v8::Value> argv[4] = { data, jstr(iso, ctx_origin(c).c_str()), win_for(t, c), a[3]->IsArray() ? a[3] : v8::Array::New(iso, 0).As<v8::Value>() };
            (void)jcall(t, fn.As<v8::Function>(), v8::Undefined(iso), 4, argv);
        }
    }
    free(buf.first);
}
/* portTo(dest, key, data, keys): a message for the port bridged to another tab by key */
static void n_portTo(const FCI &a) {
    FCTX; v8::Local<v8::Context> cx = iso->GetCurrentContext();
    JsCtx *t = ctx_of_value(iso, c, a[0]);
    double k = a[1]->NumberValue(cx).FromMaybe(0);
    if (!t || t->dead || t->api.IsEmpty() || t->iso == iso || !k) return;
    std::vector<double> keys;
    if (a[3]->IsArray()) {
        v8::Local<v8::Array> ks = a[3].As<v8::Array>();
        for (uint32_t i = 0; i < ks->Length(); i++) { v8::Local<v8::Value> v; if (ks->Get(cx, i).ToLocal(&v) && v->IsNumber()) keys.push_back(v.As<v8::Number>()->Value()); }
    }
    v8::ValueSerializer ser(iso);
    ser.WriteHeader();
    if (!ser.WriteValue(cx, a[2]).FromMaybe(false)) return;
    std::pair<uint8_t *, size_t> buf = ser.Release();
    g_xmsgs.push_back({ t->id, std::vector<uint8_t>(buf.first, buf.first + buf.second), ctx_origin(c), remote_id(c), k, std::move(keys) });
    free(buf.first);
    if (js_wakeup) js_wakeup();
}

bool xmsg_waiting(JsCtx *c) {
    for (const XMsg &m : g_xmsgs) if (m.to == c->id) return true;
    return false;
}
void xmsg_deliver(JsCtx *c) {   /* in c's isolate and context */
    if (g_xmsgs.empty()) return;
    std::vector<XMsg> mine;
    for (auto it = g_xmsgs.begin(); it != g_xmsgs.end();) {
        if (it->to == c->id) { mine.push_back(std::move(*it)); it = g_xmsgs.erase(it); }
        else if (!js_ctx_by_id(it->to)) it = g_xmsgs.erase(it);   /* its page is gone */
        else ++it;
    }
    v8::Isolate *iso = c->iso;
    for (XMsg &m : mine) {
        if (c->dead || c->api.IsEmpty()) break;
        v8::HandleScope hs(iso);
        v8::Local<v8::Context> tc = c->ctx.Get(iso);
        v8::ValueDeserializer des(iso, m.data.data(), m.data.size());
        v8::Local<v8::Value> data, fn;
        v8::Local<v8::Array> keys = v8::Array::New(iso, (int)m.keys.size());
        for (size_t i = 0; i < m.keys.size(); i++) (void)keys->Set(tc, (uint32_t)i, v8::Number::New(iso, m.keys[i]));
        if (m.port) {
            if (des.ReadHeader(tc).FromMaybe(false) && des.ReadValue(tc).ToLocal(&data) &&
                c->api.Get(iso)->Get(tc, jstr(iso, "portMessage")).ToLocal(&fn) && fn->IsFunction()) {
                v8::Local<v8::Value> argv[4] = { v8::Number::New(iso, m.port), data, v8::Number::New(iso, m.src), keys };
                (void)jcall(c, fn.As<v8::Function>(), v8::Undefined(iso), 4, argv);
            }
        } else if (des.ReadHeader(tc).FromMaybe(false) && des.ReadValue(tc).ToLocal(&data) &&
            c->api.Get(iso)->Get(tc, jstr(iso, "queueMessage")).ToLocal(&fn) && fn->IsFunction()) {
            v8::Local<v8::Value> argv[4] = { data, jstr(iso, m.origin.c_str()), v8::Number::New(iso, m.src), keys };
            (void)jcall(c, fn.As<v8::Function>(), v8::Undefined(iso), 4, argv);
        }
    }
}
/* openWin(url, name, withOpener): the new tab's window id, 0 if none */
static void n_openWin(const FCI &a) {
    FCTX; JsCtx *r = root_of(c);
    if (!r->host.open_window || a.Length() < 3) { a.GetReturnValue().Set(0); return; }
    std::string u = jcstr(iso, a[0]), nm = jcstr(iso, a[1]);
    a.GetReturnValue().Set(r->host.open_window(r->host.ud, u.c_str(), nm.c_str(), a[2]->BooleanValue(iso) ? c->id : 0));
}
/* winOp(wid, op[, arg]): JsHost.window_op */
static void n_winOp(const FCI &a) {
    FCTX; JsCtx *r = root_of(c); v8::Local<v8::Context> cx = iso->GetCurrentContext();
    uint32_t w = a[0]->Uint32Value(cx).FromMaybe(0); int op = a[1]->Int32Value(cx).FromMaybe(-1);
    std::string arg = a.Length() > 2 ? jcstr(iso, a[2]) : std::string();
    a.GetReturnValue().Set(r->host.window_op ? r->host.window_op(r->host.ud, w, op, arg.c_str()) : op == 0);
}
static void n_openerWin(const FCI &a) {
    FCTX; JsCtx *r = root_of(c), *o = c == r && r->opener ? js_ctx_by_id(r->opener) : nullptr;
    a.GetReturnValue().Set(o ? win_for(c, o) : v8::Local<v8::Value>(v8::Null(iso)));
}

void js_install_frames(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(frameWin); REG(frameDoc); REG(parentWin); REG(topWin); REG(frameEl); REG(frameCount); REG(frameAt); REG(ctxRel); REG(navTarget); REG(caller); REG(fullyActive); REG(postTo); REG(portTo); REG(openWin); REG(winOp); REG(openerWin);
#undef REG
}
