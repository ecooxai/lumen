/* Nested browsing contexts (<iframe>): child V8 contexts sharing the parent's isolate */
#include "js_int.h"
#include "jsglue.h"
#include "../base/url.h"

using FCI = v8::FunctionCallbackInfo<v8::Value>;

struct FrameLoad { JsCtx *p; Node *f; uint32_t kid; std::string url; };
static std::vector<FrameLoad *> g_loads;

static std::string origin_of(const char *u) {
    URL x{};
    if (!u || !url_parse(u, &x)) return "null";
    char *o = url_origin(&x);
    std::string r = o && *o ? o : "null";
    free(o); url_free(&x);
    return r;
}
std::string ctx_origin(JsCtx *c) {
    if (c->sb_opaque) return "null";
    if (c->parent && (!c->doc->url || !strncmp(c->doc->url, "about:", 6))) return ctx_origin(c->parent);
    return origin_of(c->doc->url);
}
static bool same_origin(JsCtx *a, JsCtx *b) { return a == b || (!a->sb_opaque && !b->sb_opaque && ctx_origin(a) == ctx_origin(b)); }

static v8::Local<v8::Value> win_for(JsCtx *v, JsCtx *t) {
    v8::Isolate *iso = v->iso;
    if (!t || t->dead || t->ctx.IsEmpty()) return v8::Null(iso);
    if (same_origin(v, t)) return t->ctx.Get(iso)->Global();
    return v8::Number::New(iso, t->id);
}
static JsCtx *root_of(JsCtx *c) { while (c->parent) c = c->parent; return c; }
static JsCtx *by_id(JsCtx *any, uint32_t id) {
    std::vector<JsCtx *> st{ root_of(any) };
    while (!st.empty()) {
        JsCtx *c = st.back(); st.pop_back();
        if (c->id == id) return c->dead ? nullptr : c;
        for (JsCtx *k : c->kids) st.push_back(k);
    }
    return nullptr;
}

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
    k->timers.clear(); k->rafs.clear(); k->anims.clear();
    if (k->host.frame_close && k->host_ud) k->host.frame_close(k->host_ud);
    k->host_ud = nullptr;
}
void frames_forget(JsCtx *c) { for (FrameLoad *l : g_loads) if (l->p == c) l->p = nullptr; }

static std::string src_key(JsCtx *p, Node *f) {
    if (const char *sd = node_attr(f, "srcdoc")) return std::string("srcdoc:") + sd;
    const char *src = node_attr(f, "src");
    char *u = src && *src ? url_join(p->doc->url, src) : nullptr;
    std::string r = u ? u : "";
    free(u);
    return r;
}

static std::string g_next_csp;   /* CSP of the response frame_create is about to build a document from */
static JsCtx *frame_create(JsCtx *p, Node *f, const char *url, const char *html, size_t n, bool fire_load) {
    if (JsCtx *old = js_frame_ctx(f)) frame_kill(old);
    Document *d = doc_new(url);
    if (!g_next_csp.empty()) { d->csp = csp_append(nullptr, g_next_csp.c_str()); g_next_csp.clear(); }
    else if (!strncmp(url, "about:", 6) && p->doc && p->doc->csp) d->csp = strdup(p->doc->csp);
    html_parse(d, html ? html : "", html ? n : 0);
    JsHost ch{};
    ch.viewport = p->host.viewport;
    if (p->host.frame_open) p->host.frame_open(p->host.ud, f, d, &ch);
    if (!ch.frame_open) ch.frame_open = p->host.frame_open;
    ch.frame_close = p->host.frame_close;
    JsCtx *k = js_new_ex(d, &ch, p, f);
    k->host_ud = ch.ud;
    if (const char *fp = getenv("LUMEN_FRAME_PRE")) if (strncmp(url, "about:", 6)) js_eval(k, fp, "lumen:frame-pre");
    for (Node *s = d->node.first; s && !k->dead && !k->sb_noscript; s = node_next_in_tree(s, &d->node)) {
        if (s->type != NODE_ELEMENT || s->tag != A_script || s->ns != NS_HTML || (s->flags & NF_SCRIPT_STARTED)) continue;
        bool mod = jsg_module_script(s);
        if (!mod && !jsg_classic_script(s)) continue;
        const char *src = node_attr(s, "src");
        if (mod) {
            char *t = src ? nullptr : node_text_content(s), *u = src ? url_join(d->url, src) : nullptr;
            js_run_module(k, s, t ? t : (src ? nullptr : ""), t ? strlen(t) : 0, u ? u : d->url);
            free(t); free(u);
        } else if (src) {
            char *u = url_join(d->url, src);
            NetResponse *sr = u ? net_fetch_sync(net_request_ctx("GET", u, js_top_url(k))) : nullptr;
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
    if (k->dead) return k;
    js_set_ready_state(k, 1);
    js_dispatch(k, &d->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
    js_set_ready_state(k, 2);
    js_dispatch_window(k, "load");
    if (fire_load && !k->dead && !p->dead) js_dispatch(p, f, "load", "Event", false, false, 0, 0, 0, NULL);
    return k;
}

/* ---- X-Frame-Options / CSP frame-ancestors ---- */
static bool fa_source_match(std::string src, const char *anc, const std::string &self) {
    for (auto &ch : src) ch = (char)tolower((unsigned char)ch);
    URL a; if (!anc || !url_parse(anc, &a)) return false;
    bool web = a.scheme && (!strcmp(a.scheme, "http") || !strcmp(a.scheme, "https")), ok = false;
    if (src == "*") ok = web;
    else if (src == "'self'") ok = origin_of(anc) == self;
    else if (src.size() > 1 && src.back() == ':' && src.find('/') == std::string::npos) {
        std::string sch = src.substr(0, src.size() - 1);
        ok = a.scheme && (sch == a.scheme || (sch == "http" && !strcmp(a.scheme, "https")));
    } else if (a.host && src[0] != '\'') {
        std::string sch, rest = src; size_t ss = rest.find("://");
        if (ss != std::string::npos) { sch = rest.substr(0, ss); rest = rest.substr(ss + 3); }
        size_t sl = rest.find('/'); if (sl != std::string::npos) rest = rest.substr(0, sl);
        std::string host = rest, port; size_t co = rest.rfind(':');
        if (co != std::string::npos && rest.find(']') == std::string::npos) { host = rest.substr(0, co); port = rest.substr(co + 1); }
        bool sch_ok = sch.empty() ? web : (sch == a.scheme || (sch == "http" && !strcmp(a.scheme, "https")));
        std::string ah = a.host; bool host_ok;
        if (host.size() > 2 && host[0] == '*' && host[1] == '.') { std::string suf = host.substr(1); host_ok = ah.size() > suf.size() && !ah.compare(ah.size() - suf.size(), suf.size(), suf); }
        else host_ok = host == ah;
        bool port_ok = port == "*" || (port.empty() ? a.port == url_default_port(a.scheme) : atoi(port.c_str()) == a.port);
        ok = sch_ok && host_ok && port_ok;
    }
    url_free(&a);
    return ok;
}
/* true if the response forbids being shown in this frame (checked against every ancestor document) */
static bool frame_embed_blocked(JsCtx *p, const NetResponse *r, const char *url) {
    std::string self = origin_of(url);
    for (int i = 0; i < r->headers.n; i++) {
        if (!str_ieq(r->headers.v[i].name, "content-security-policy")) continue;
        std::string pol = r->headers.v[i].value;
        size_t st = 0;
        while (st <= pol.size()) {
            size_t en = pol.find(';', st); if (en == std::string::npos) en = pol.size();
            std::string d = pol.substr(st, en - st); st = en + 1;
            std::vector<std::string> tok; size_t q = 0;
            while (q < d.size()) { while (q < d.size() && isspace((unsigned char)d[q])) q++; size_t e = q; while (e < d.size() && !isspace((unsigned char)d[e])) e++; if (e > q) tok.push_back(d.substr(q, e - q)); q = e; }
            if (tok.empty() || !str_ieq(tok[0].c_str(), "frame-ancestors")) continue;
            for (JsCtx *a = p; a; a = a->parent) {
                const char *au = a->doc ? a->doc->url : nullptr; bool any = false;
                for (size_t k = 1; k < tok.size() && !any; k++) any = !str_ieq(tok[k].c_str(), "'none'") && fa_source_match(tok[k], au, self);
                if (!any) return true;
            }
            return false;   /* frame-ancestors present and satisfied: X-Frame-Options is ignored */
        }
    }
    const char *xfo = headers_get(&r->headers, "x-frame-options");
    if (!xfo) return false;
    while (*xfo == ' ') xfo++;
    if (!strncasecmp(xfo, "deny", 4)) return true;
    if (!strncasecmp(xfo, "sameorigin", 10)) { for (JsCtx *a = p; a; a = a->parent) if (origin_of(a->doc ? a->doc->url : nullptr) != self) return true; }
    return false;
}

static void frame_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq;
    FrameLoad *l = static_cast<FrameLoad *>(ud);
    g_loads.erase(std::remove(g_loads.begin(), g_loads.end(), l), g_loads.end());
    JsCtx *p = l->p, *blank = nullptr;
    if (p && !p->dead) for (JsCtx *k : p->kids) if (!k->dead && k->id == l->kid && k->frame_el == l->f) blank = k;
    if (blank) {
        std::string key = blank->frame_src;
        bool ok = r && r->status > 0;
        if (ok && frame_embed_blocked(p, r, r->url ? r->url : l->url.c_str())) {
            fprintf(stderr, "lumen: refused to display '%s' in a frame (X-Frame-Options / frame-ancestors)\n", r->url ? r->url : l->url.c_str());
            JsCtx *k = frame_create(p, l->f, "about:blank", "", 0, true);
            k->frame_src = key;
            delete l;
            return;
        }
        const char *body = ok && r->body ? r->body : "";
        if (ok) for (int i = 0; i < r->headers.n; i++)
            if (str_ieq(r->headers.v[i].name, "content-security-policy")) { if (!g_next_csp.empty()) g_next_csp += '\n'; g_next_csp += r->headers.v[i].value; }
        JsCtx *k = frame_create(p, l->f, ok && r->url ? r->url : l->url.c_str(), body, ok && r->body ? r->body_len : 0, true);
        k->frame_src = key;
    }
    delete l;
}

static void frame_load(JsCtx *p, Node *f, const char *u, const std::string &key) {
    bool remote = u && *u && strncmp(u, "about:", 6) && strncmp(u, "javascript:", 11);
    JsCtx *k = frame_create(p, f, "about:blank", "", 0, !remote);
    k->frame_src = key;
    if (!remote) return;
    NetRequest *rq = net_request_ctx("GET", u, js_top_url(p));
    if (char *ref = net_referrer(p->doc->url, u, p->doc->referrer_policy)) { headers_add(&rq->headers, "Referer", ref); free(ref); }
    FrameLoad *l = new FrameLoad{ p, f, k->id, u };
    g_loads.push_back(l);
    rq->done = frame_done; rq->ud = l;
    net_fetch(rq);
}

static void frame_start(JsCtx *p, Node *f) {
    if (p->dead) return;
    std::string key = src_key(p, f);
    if (const char *sd = node_attr(f, "srcdoc")) { JsCtx *k = frame_create(p, f, "about:srcdoc", sd, strlen(sd), true); k->frame_src = key; return; }
    frame_load(p, f, key.c_str(), key);
}

void frame_nav(JsCtx *k, const char *u) {
    if (!k->parent || k->dead) return;
    char *abs = url_join(k->doc->url, u);
    if (abs) frame_load(k->parent, k->frame_el, abs, k->frame_src);
    free(abs);
}

static bool is_iframe(Node *n) { return n->type == NODE_ELEMENT && n->ns == NS_HTML && n->tag == A_iframe; }
static void collect_iframes(Node *root, std::vector<Node *> &out) {
    for (Node *n = root; n; n = node_next_in_tree(n, root)) {
        if (is_iframe(n) && !js_frame_ctx(n)) out.push_back(n);
        if (n->type == NODE_ELEMENT && n->shadow_root) collect_iframes(n->shadow_root, out);
    }
}

void frames_scan(JsCtx *c) {
    if (c->dead || c->doc->dom_version == c->frame_scan_ver) return;
    c->frame_scan_ver = c->doc->dom_version;
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
static JsCtx *ctx_of_value(v8::Isolate *iso, JsCtx *c, v8::Local<v8::Value> v) {
    if (v->IsNumber()) return by_id(c, (uint32_t)v.As<v8::Number>()->Value());
    if (!v->IsObject()) return nullptr;
    v8::Local<v8::Context> cx;
    if (!v.As<v8::Object>()->GetCreationContext(iso).ToLocal(&cx)) return nullptr;
    return static_cast<JsCtx *>(cx->GetAlignedPointerFromEmbedderData(1, kTag));
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

void js_install_frames(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(frameWin); REG(frameDoc); REG(parentWin); REG(topWin); REG(frameEl); REG(frameCount); REG(frameAt); REG(ctxRel); REG(caller); REG(postTo);
#undef REG
}
