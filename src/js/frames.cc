/* Nested browsing contexts (<iframe>): child V8 contexts sharing the parent's isolate */
#include "js_int.h"
#include "clone_brand.h"
#include "jsglue.h"
#include "../base/url.h"

using FCI = v8::FunctionCallbackInfo<v8::Value>;

struct FrameLoad { JsCtx *p; Node *f; uint32_t kid; std::string url; std::string ref; };
static std::vector<FrameLoad *> g_loads;

static std::string origin_of(const char *u) {
    URL x{};
    if (!u || !url_parse(u, &x)) return "null";
    char *o = url_origin(&x);
    std::string r = o && *o ? o : "null";
    free(o); url_free(&x);
    return r;
}
std::string ctx_origin(JsCtx *c) { return c ? c->origin : "null"; }
static bool same_origin(JsCtx *a, JsCtx *b) { return js_windows_same_origin(a, b); }

static v8::Local<v8::Value> win_for(JsCtx *v, JsCtx *t) {
    v8::Isolate *iso = v->iso;
    if (!t || t->dead || t->ctx.IsEmpty()) return v8::Null(iso);
    return js_window_value(v, t->window_id);
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
void frame_nav(JsCtx *k, const std::string &u);
void js_frame_navigate(Node *f, const char *u) { if (u) if (JsCtx *k = js_frame_ctx(f)) frame_nav(k, u); }
Document *js_frame_doc(Node *f) { JsCtx *k = js_frame_ctx(f); return k ? k->doc : nullptr; }

void frame_kill(JsCtx *k) {
    if (k->dead) return;
    js_windows_detach(k, true); /* removal closes the frame; replacement already advanced its epoch */
    k->dead = true;
    for (JsCtx *g : k->kids) frame_kill(g);
    for (auto &kv : k->fetches) { net_cancel(kv.first); kv.second->c = nullptr; kv.second->cb.Reset(); kv.second->head.Reset(); kv.second->chunk.Reset(); }
    k->fetches.clear(); for (auto &kv : k->sockets) { net_ws_release(kv.second->ws); kv.second->cb.Reset(); delete kv.second; } k->sockets.clear();
    k->timers.clear(); k->rafs.clear(); k->anims.clear(); k->rejections.clear();
    if (k->host.frame_close && k->host_ud) k->host.frame_close(k->host_ud);
    k->host_ud = nullptr;
}
/* DOM removing steps must sever the public association in the same call.
 * A later host scan, or a getter-only guard, can otherwise revive a removed
 * child when the same iframe is reinserted before the next event-loop turn.
 * frame_kill deactivates queues/handles but retains the native JsCtx and its
 * Document for already-held wrappers; it never destroys a V8-active owner. */
extern "C" void dom_connected_subtree_removed(Document *document) {
    JsCtx *parent = owner_ctx(document);
    if (!parent || parent->dead) return;
    for (JsCtx *child : std::vector<JsCtx *>(parent->kids)) {
        if (!child->dead && child->frame_el &&
            (!(child->frame_el->flags & NF_CONNECTED) || child->frame_el->doc != document))
            frame_kill(child);
    }
}

void frames_forget(JsCtx *c) { for (FrameLoad *l : g_loads) if (l->p == c) l->p = nullptr; }

static std::string src_key(JsCtx *p, Node *f) {
    size_t length = 0;
    if (const char *sd = node_attr_n(f, "srcdoc", &length)) return std::string("srcdoc:") + std::string(sd, length);
    const char *src = node_attr_n(f, "src", &length);
    char *u = src && length ? url_join_n(p->doc->url, src, length) : nullptr;
    std::string r = u ? u : "";
    free(u);
    return r;
}

static std::string g_next_csp;   /* CSP of the response frame_create is about to build a document from */
static std::string g_next_permissions; /* Permissions-Policy on the incoming frame response */
static std::string g_next_ref;   /* Referer sent for the frame navigation frame_create is about to build */
static JsCtx *frame_create(JsCtx *p, Node *f, const char *url, const char *html, size_t n, bool fire_load,
                           const char *content_type = nullptr) {
    JsCtx *old = js_frame_ctx(f);
    Document *d = doc_new(url);
    if (!g_next_permissions.empty()) { d->permissions_policy = strdup(g_next_permissions.c_str()); g_next_permissions.clear(); }
    if (!g_next_csp.empty()) { d->csp = csp_append(nullptr, g_next_csp.c_str()); g_next_csp.clear(); }
    if (!g_next_ref.empty()) { d->referrer = strdup(g_next_ref.c_str()); g_next_ref.clear(); }
    else if (!strncmp(url, "about:", 6) && p->doc && p->doc->csp) d->csp = strdup(p->doc->csp);
    if (content_type) {
        if (!html_parse_bytes(d, html ? html : "", html ? n : 0, content_type)) {
            fprintf(stderr, "lumen: frame HTML decoding exceeded its byte contract\n");
            html_parse(d, "", 0);
        }
    } else html_parse(d, html ? html : "", html ? n : 0);
    /* A document replacement retains the iframe browsing context. Commit before
     * retiring the old realm so its detach cannot close the successor's ID. */
    JsWindowId window_id = old ? js_window_commit(old->window_id, d, JS_COOP_UNSAFE_NONE) : 0;
    if (old) frame_kill(old);
    JsHost ch{};
    ch.viewport = p->host.viewport;
    if (p->host.frame_open) p->host.frame_open(p->host.ud, f, d, &ch);
    if (!ch.frame_open) ch.frame_open = p->host.frame_open;
    ch.frame_close = p->host.frame_close;
    ch.window_id = window_id;
    JsCtx *k = js_new_ex(d, &ch, p, f);
    k->host_ud = ch.ud;
    if (const char *fp = getenv("LUMEN_FRAME_PRE")) if (strncmp(url, "about:", 6)) js_eval(k, fp, "lumen:frame-pre");
    for (Node *s = d->node.first; s && !k->dead && !k->sb_noscript; s = node_next_in_tree(s, &d->node)) {
        if (s->type != NODE_ELEMENT || s->tag != A_script || s->ns != NS_HTML || (s->flags & NF_SCRIPT_STARTED)) continue;
        bool mod = jsg_module_script(s);
        if (!mod && !jsg_classic_script(s)) continue;
        size_t src_length = 0; const char *src = node_attr_n(s, "src", &src_length);
        if (mod) {
            char *t = src ? nullptr : node_text_content(s), *u = src && src_length ? url_join_n(d->url, src, src_length) : nullptr;
            if (src && !u) { s->flags |= NF_SCRIPT_STARTED; js_dispatch(k, s, "error", "Event", false, false, 0, 0, 0, NULL); }
            else js_run_module(k, s, t ? t : (src ? nullptr : ""), t ? strlen(t) : 0, u ? u : d->url);
            free(t); free(u);
        } else if (src) {
            char *u = src_length ? url_join_n(d->url, src, src_length) : nullptr;
            NetRequest *srq = u ? net_request_ctx("GET", u, js_top_url(k)) : nullptr;
            if (srq && d->url) { srq->no_cors_origin = strdup(d->url); srq->coep_require_corp = d->coep_require_corp; }
            NetResponse *sr = srq ? net_fetch_sync(srq) : nullptr;
            net_request_free(srq);
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
    else if (src == "'self'") ok = self != "null" && origin_of(anc) == self;
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
    /* An opaque origin's "null" serialization is not an origin identity. */
    if (!strncasecmp(xfo, "sameorigin", 10)) { if (self == "null") return true; for (JsCtx *a = p; a; a = a->parent) if (origin_of(a->doc ? a->doc->url : nullptr) != self) return true; }
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
        if (ok) { const char *policy = headers_get(&r->headers, "permissions-policy"); g_next_permissions = policy ? policy : ""; g_next_ref = l->ref; }
        const char *content_type = ok ? headers_get(&r->headers, "content-type") : nullptr;
        JsCtx *k = frame_create(p, l->f, ok && r->url ? r->url : l->url.c_str(),
            body, ok && r->body ? r->body_len : 0, true, ok ? (content_type ? content_type : "") : nullptr);
        k->frame_src = key;
    }
    delete l;
}

static void frame_load(JsCtx *p, Node *f, const char *u, const std::string &key,
                       const FrameSubmission *submission = nullptr) {
    bool remote = u && *u && strncmp(u, "about:", 6) && strncmp(u, "javascript:", 11);
    if (submission && (!p || p->dead || !(f->flags & NF_CONNECTED) || f->doc != p->doc)) return;
    if (submission && p->doc && p->doc->csp &&
        !csp_allows_url(p->doc->csp, p->doc->url, u, "frame-src")) {
        fprintf(stderr, "lumen: CSP frame-src blocked form target %.120s\n", u); return;
    }
    JsCtx *k = frame_create(p, f, "about:blank", "", 0, !remote);
    k->frame_src = key;
    if (!remote) return;
    if (p->doc && p->doc->csp && !csp_allows_url(p->doc->csp, p->doc->url, u, "frame-src")) { fprintf(stderr, "lumen: CSP frame-src blocked %.120s\n", u); return; }
    NetRequest *rq = net_request_ctx(submission && submission->post ? "POST" : "GET", u,
        submission ? submission->top_url.c_str() : js_top_url(p));
    std::string refs;
    if (submission) {
        refs = submission->referrer;
        if (!refs.empty()) headers_add(&rq->headers, "Referer", refs.c_str());
        if (!submission->csp.empty()) {
            rq->csp = xstrdup(submission->csp.c_str());
            rq->csp_doc = xstrdup(submission->policy_url.c_str());
            rq->csp_dir = "form-action"; /* Retained through HTTP redirects. */
        }
        if (submission->post) {
            rq->body_len = submission->body.size();
            rq->body = (char *)xmalloc(rq->body_len ? rq->body_len : 1);
            if (rq->body_len) memcpy(rq->body, submission->body.data(), rq->body_len);
            headers_add(&rq->headers, "Content-Type", submission->content_type.c_str());
            headers_add(&rq->headers, "Origin", submission->origin.c_str());
        }
    } else if (char *ref = net_referrer(p->doc->url, u, p->doc->referrer_policy)) {
        headers_add(&rq->headers, "Referer", ref); refs = ref; free(ref);
    }
    FrameLoad *l = new FrameLoad{ p, f, k->id, u, refs };
    g_loads.push_back(l);
    rq->done = frame_done; rq->ud = l;
    net_fetch(rq);
}

static void frame_start(JsCtx *p, Node *f) {
    if (p->dead) return;
    std::string key = src_key(p, f);
    size_t length = 0;
    if (const char *sd = node_attr_n(f, "srcdoc", &length)) { JsCtx *k = frame_create(p, f, "about:srcdoc", sd, length, true); k->frame_src = key; return; }
    frame_load(p, f, key.c_str(), key);
}

void frame_nav(JsCtx *k, const std::string &u) {
    if (!k->parent || k->dead) return;
    char *abs = url_join_n(k->doc->url, u.data(), u.size());
    if (abs) frame_load(k->parent, k->frame_el, abs, k->frame_src);
    free(abs);
}
void frame_submit(JsCtx *k, const std::string &url, const FrameSubmission &submission) {
    if (!k || !k->parent || k->dead || !k->frame_el ||
        !(k->frame_el->flags & NF_CONNECTED) || k->frame_el->doc != k->parent->doc) return;
    /* Preserve the iframe src/srcdoc attribute key: a form navigation changes
     * the active document, not those attributes. Copy before frame_load retires
     * the old realm and creates its replacement. */
    JsCtx *parent = k->parent;
    Node *frame = k->frame_el;
    const std::string key = k->frame_src;
    frame_load(parent, frame, url.c_str(), key, &submission);
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
    for (JsCtx *k : std::vector<JsCtx *>(c->kids)) {
        if (k->dead) continue;
        if (!(k->frame_el->flags & NF_CONNECTED) || k->frame_el->doc != c->doc) frame_kill(k);
        else if (src_key(c, k->frame_el) != k->frame_src) frame_start(c, k->frame_el);
    }
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
    case 4: if (t) { std::string u = jcstr(iso, a[2]); frame_nav(t, u); } break;
    }
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
    /* Existing MessagePort delivery is confined to frames in this same isolate.
     * All ordinary Window messages use the bounded directed queue instead. */
    v8::Isolate *iso = a.GetIsolate(); JsCtx *source = js_window_caller(iso);
    v8::Local<v8::Context> cx = iso->GetCurrentContext();
    JsCtx *target = js_window_context(source, a[0]);
    std::string wanted = jcstr(iso, a[2]); bool valid = false;
    (void)js_windows_target_origin(source, target, wanted, &valid);
    if (!valid || !target || target->dead || target->api.IsEmpty()) return;
    const JsWindowId source_id = source->window_id;
    const std::string origin = ctx_origin(source);
    JsgCloneDelegate delegate(iso);
    v8::ValueSerializer serializer(iso, &delegate);
    serializer.WriteHeader();
    if (!serializer.WriteValue(cx, a[1]).FromMaybe(false)) return;
    auto bytes = serializer.Release();
    /* Payload getters can navigate/remove the destination or close the sender.
     * The target must still be this exact installed realm; a closed sender's
     * already serialized message keeps its send-time origin and stable ID. */
    if (!target->dead && !target->api.IsEmpty() && js_windows_target_origin(source, target, wanted, &valid)) {
        v8::Local<v8::Context> tc = target->ctx.Get(iso);
        v8::Context::Scope cs(tc);
        v8::ValueDeserializer deserializer(iso, bytes.first, bytes.second);
        v8::Local<v8::Value> data, fn;
        if (deserializer.ReadHeader(tc).FromMaybe(false) && deserializer.ReadValue(tc).ToLocal(&data) &&
            target->api.Get(iso)->Get(tc, jstr(iso, "queueMessage")).ToLocal(&fn) && fn->IsFunction()) {
            v8::Local<v8::Value> args[] = {data, jstr(iso, origin.c_str()), js_window_value(target, source_id),
                a[3]->IsArray() ? a[3] : v8::Array::New(iso, 0).As<v8::Value>()};
            (void)jcall(target, fn.As<v8::Function>(), v8::Undefined(iso), 4, args);
        }
    }
    free(bytes.first);
}

void js_install_frames(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(frameWin); REG(frameDoc); REG(parentWin); REG(topWin); REG(frameEl); REG(frameCount); REG(frameAt); REG(ctxRel); REG(caller); REG(postTo);
#undef REG
}
