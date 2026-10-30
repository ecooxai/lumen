#include "broadcast.h"
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
/* a and b now hold each other's objects, so a removed frame among them (or above them) can't be freed early */
static void xref_mark(JsCtx *a, JsCtx *b, const char *why) {
    if (a == b) return;
    if (getenv("LUMEN_DEBUG_FRAMES") && !(a->xref && b->xref)) fprintf(stderr, "lumen: frames %u and %u share objects (%s)\n", a->id, b->id, why);
    for (JsCtx *x = a; x && !x->xref; x = x->parent) x->xref = true;
    for (JsCtx *x = b; x && !x->xref; x = x->parent) x->xref = true;
}
static v8::Local<v8::Value> win_for(JsCtx *v, JsCtx *t) {
    v8::Isolate *iso = v->iso;
    if (!t || t->dead || t->ctx.IsEmpty()) return v8::Null(iso);
    if (t->iso == v->iso && same_origin(v, t)) {
        if (getenv("LUMEN_DEBUG_FRAMES") && !(v->xref && t->xref)) {
            v8::Local<v8::StackTrace> st = v8::StackTrace::CurrentStackTrace(iso, 4);
            for (int i = 0; i < st->GetFrameCount(); i++) {
                v8::Local<v8::StackFrame> f = st->GetFrame(iso, i); v8::String::Utf8Value fn(iso, f->GetFunctionName());
                fprintf(stderr, "lumen:   at %s line %d\n", *fn ? *fn : "?", f->GetLineNumber());
            }
        }
        xref_mark(v, t, "window"); return t->ctx.Get(iso)->Global(); }
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
/* the iframe/frame element showing d (the context holds a reference to it), else NULL */
extern "C" Node *js_doc_frame_el(Document *d) { JsCtx *k = owner_ctx(d); return k && k->parent && !k->dead && k->doc == d ? k->frame_el : nullptr; }

void frame_kill(JsCtx *k) {
    if (k->dead) return;
    k->dead = true;
    js_broadcast_deactivate(k->broadcast);
    k->dead_at = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (JsCtx *g : k->kids) frame_kill(g);
    frames_parser_cancel(k);
    js_cancel_fetches(k);
    k->ordered_scripts.clear(); k->pending_scripts = 0;
    for (auto &kv : k->sockets) { net_ws_release(kv.second->ws); kv.second->cb.Reset(); delete kv.second; } k->sockets.clear();
    k->timer_clear(); k->rafs.clear(); k->anims.clear();
    if (k->host.frame_close && k->host_ud) k->host.frame_close(k->host_ud);
    k->host_ud = nullptr;
}
void frames_forget(JsCtx *c) { frames_parser_cancel(c); for (FrameLoad *l : g_loads) if (l->p == c) l->p = nullptr; }

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
    if (hdrs) { d->csp = headers_join(hdrs, "content-security-policy"); d->csp_ro = headers_join(hdrs, "content-security-policy-report-only"); d->referrer_policy = headers_join(hdrs, "referrer-policy"); }
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
    k->el_load = fire_load;
    frame_run_scripts(k, d);
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
    k->el_load = k->frame_el != nullptr;
    frame_run_scripts(k, d);
}
/* Parser scripts retain encounter order while pending fetches yield to the
   event loop. The network callback only publishes bytes; it never evaluates JS. */
struct FrameParserScript {
    Node *node = nullptr;
    std::string source, url, fetched_url;
    uint64_t request = 0, serial = 0;
    bool entered = false, external = false, module = false, deferred = false;
    bool done = false, ready = false, success = false;
};
struct FrameScriptParser {
    Document *document = nullptr;
    uint64_t generation = 0;
    std::vector<FrameParserScript> scripts;
    size_t index = 0;
    bool deferred_phase = false, pumping = false, complete = false, aborted = false;
};
void frames_parser_cancel(JsCtx *c) {
    auto state = std::move(c->frame_parser);
    if (!state) return;
    state->aborted = true;
    for (auto &script : state->scripts) {
        if (script.request) js_cancel_fetch(c, script.request);
        node_release(script.node);
    }
    state->scripts.clear();
}
static void frame_parser_fetch(JsCtx *c, const std::shared_ptr<FrameScriptParser> &state,
                               size_t index, const std::string &url) {
    FrameParserScript &script = state->scripts[index];
    if (script.request) js_cancel_fetch(c, script.request);
    script.url = url; script.fetched_url.clear(); script.source.clear();
    script.ready = script.success = false;
    const uint64_t serial = ++script.serial;
    std::weak_ptr<FrameScriptParser> weak = state;
    script.request = js_fetch_parser_source(c, script.node, url.empty() ? nullptr : url.c_str(),
        [weak, index, serial](JsCtx *context, NetResponse *response) {
            auto parser = weak.lock();
            if (!parser || parser->aborted || parser->generation != context->parser_generation || index >= parser->scripts.size()) return;
            auto &item = parser->scripts[index];
            if (item.serial != serial) return;
            item.request = 0; item.ready = true;
            item.success = response && !response->error && response->status >= 200 && response->status < 300;
            if (item.success) {
                item.source.assign(response->body ? response->body : "", response->body ? response->body_len : 0);
                item.fetched_url = response->url ? response->url : item.url;
            }
        });
}
void frames_parser_tick(JsCtx *c) {
    auto state = c->frame_parser;
    if (!state || state->pumping || state->complete || state->aborted || c->dead) return;
    if (state->generation != c->parser_generation) { frames_parser_cancel(c); return; }
    state->pumping = true;
    while (!c->dead && !state->aborted && state->generation == c->parser_generation) {
        if (state->index == state->scripts.size()) {
            if (!state->deferred_phase) {
                state->deferred_phase = true; state->index = 0;
                js_set_ready_state(c, 1);
                continue;
            }
            state->complete = true;
            js_dispatch(c, &state->document->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
            if (!c->dead && !state->aborted && state->generation == c->parser_generation) js_fire_load(c);
            break;
        }
        const size_t index = state->index;
        auto &item = state->scripts[index];
        Node *script = item.node;
        if (item.done || script->doc != state->document) { item.done = true; ++state->index; continue; }
        if (!item.entered) {
            item.entered = true;
            if (!js_prepare_script(script)) { item.done = true; ++state->index; continue; }
            item.module = jsg_module_script(script);
            const char *src = jsg_script_src(script);
            item.external = src != nullptr;
            char *joined = src && *src ? url_join(doc_base(state->document), src) : nullptr;
            const std::string url = joined ? joined : ""; free(joined);
            const bool async = node_has_attr(script, "async");
            if (item.external && url.empty()) {
                if (item.request) js_cancel_fetch(c, item.request);
                item.request = 0; item.done = true; ++state->index;
                js_queue_classic_script(c, script, nullptr, false);
                continue;
            }
            item.deferred = !async && (item.module || (item.external && node_has_attr(script, "defer")));
            if (item.module) {
                if (item.request) js_cancel_fetch(c, item.request);
                item.request = 0; item.url = url;
                if (!item.external) {
                    size_t length = 0; char *text = jsg_script_text(script, &length);
                    item.source.assign(text ? text : "", length); free(text);
                }
                if (async) {
                    item.done = true; ++state->index;
                    const bool external = item.external;
                    const std::string source = item.source, name = item.url;
                    if (external && name.empty()) js_queue_classic_script(c, script, nullptr, false);
                    else js_run_module_inserted(c, script, external ? nullptr : source.data(), source.size(),
                                                external ? name.c_str() : state->document->url);
                    continue;
                }
            } else if (item.external && async) {
                if (item.request) js_cancel_fetch(c, item.request);
                item.request = 0; item.done = true; ++state->index;
                js_queue_classic_script(c, script, url.empty() ? nullptr : url.c_str(), false);
                continue;
            } else if (item.external) {
                if (item.url != url || (!item.ready && !item.request)) frame_parser_fetch(c, state, index, url);
            } else {
                size_t length = 0; char *text = jsg_script_text(script, &length);
                item.source.assign(text ? text : "", length); free(text);
            }
        }
        if (item.deferred && !state->deferred_phase) { ++state->index; continue; }
        if (!item.module && item.external && !item.ready) break;
        item.done = true; ++state->index;
        // Evaluation may open the document, which releases all parser records.
        // Keep source and URL values alive independently across that callback.
        const std::string source = std::move(item.source);
        const std::string url = item.fetched_url.empty() ? item.url : item.fetched_url;
        const bool external = item.external, module = item.module, deferred = item.deferred, success = item.success;
        if (module) {
            if (external && url.empty()) js_queue_classic_script(c, script, nullptr, false);
            else js_run_module(c, script, external ? nullptr : source.data(), source.size(), external ? url.c_str() : state->document->url);
        } else if (external && !success) {
            js_dispatch(c, script, "error", "Event", false, false, 0, 0, 0, NULL);
        } else {
            js_run_prepared_classic(c, script, source.data(), source.size(), external ? url.c_str() : state->document->url,
                                    external, !deferred);
        }
    }
    state->pumping = false;
    if (state->complete && c->frame_parser == state) frames_parser_cancel(c);
}
static void frame_run_scripts(JsCtx *c, Document *document) {
    frames_parser_cancel(c);
    auto state = std::make_shared<FrameScriptParser>();
    state->document = document; state->generation = c->parser_generation;
    for (Node *node = document->node.first; node; node = node_next_in_tree(node, &document->node)) {
        if (!jsg_is_script(node) || (node->flags & NF_SCRIPT_STARTED)) continue;
        ++node->refcount;
        FrameParserScript script; script.node = node;
        state->scripts.push_back(std::move(script));
    }
    c->frame_parser = state; c->load_deferred = true;
    for (size_t i = 0; i < state->scripts.size(); ++i) {
        Node *node = state->scripts[i].node;
        const char *src = jsg_script_src(node);
        if (!src || !*src || !jsg_classic_script(node) || node_has_attr(node, "async")) continue;
        char *url = url_join(doc_base(document), src);
        if (url) frame_parser_fetch(c, state, i, url);
        free(url);
    }
    frames_parser_tick(c);
}

/* An iframe still fetching its document, or whose document still waits for its own frames, delays its
   parent's load event. A frame removed meanwhile no longer does. */
bool (*js_load_blocked)(JsCtx *c) = nullptr;
static bool frames_loading(JsCtx *c) {
    if (c->frame_parser && !c->frame_parser->complete && !c->frame_parser->aborted) return true;
    if (c->pending_scripts) return true;
    for (const auto &job : c->mod_jobs) if (job->script && job->generation == c->parser_generation) return true;
    if (js_load_blocked && js_load_blocked(c)) return true;
    for (FrameLoad *l : g_loads) if (l->p == c) for (JsCtx *k : c->kids) if (!k->dead && k->id == l->kid && k->frame_el == l->f) return true;
    for (JsCtx *k : c->kids) if (!k->dead && k->load_deferred) return true;
    return false;
}
void js_fire_load(JsCtx *c) {
    if (!c || c->dead) return;
    c->load_deferred = false;   /* a frame that finishes during the scan must not fire this load from within it */
    frames_scan(c);   /* the frames its parser inserted start loading now */
    if (c->dead) return;
    if (!c->load_synced && c->host.sync) { c->load_synced = true; c->host.sync(c->host.ud, c->doc, true); }   /* layout requests the web fonts load waits for */
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
void js_load_recheck(JsCtx *c) { frames_load_check(c); }

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
    { char ref[4200]; if (doc_referrer(p->doc, u, f ? node_attr((Node *)f, "referrerpolicy") : NULL, ref, sizeof ref)) headers_add(&rq->headers, "Referer", ref); }
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

static bool is_iframe(Node *n) { return n->type == NODE_ELEMENT && n->ns == NS_HTML && (n->tag == A_iframe || n->tag == A_frame); }
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
    if (k && same_origin(c, k)) { xref_mark(c, k, "contentDocument"); a.GetReturnValue().Set(jwrap(k, &k->doc->node)); } else a.GetReturnValue().SetNull();
}
static void n_parentWin(const FCI &a) { FCTX; if (c->parent) a.GetReturnValue().Set(win_for(c, c->parent)); else a.GetReturnValue().SetNull(); }
static void n_topWin(const FCI &a) { FCTX; if (c->parent) a.GetReturnValue().Set(win_for(c, root_of(c))); else a.GetReturnValue().SetNull(); }
static void n_xrefd(const FCI &a) { FCTX; a.GetReturnValue().Set(c->xref); }
/* parentSecure(): the same-origin parent's isSecureContext, read without handing either frame the other's objects */
static void n_parentSecure(const FCI &a) {
    FCTX; JsCtx *p = c->parent; bool s = true;
    if (p && !p->dead && !p->ctx.IsEmpty() && p->iso == iso && same_origin(c, p)) {
        v8::Local<v8::Context> pc = p->ctx.Get(iso); v8::Local<v8::Value> v;
        if (pc->Global()->Get(pc, jstr(iso, "isSecureContext")).ToLocal(&v)) s = !v->IsFalse();
    }
    a.GetReturnValue().Set(s);
}
/* parentText(key): calls the same-origin parent's global function `key` (a string or Symbol.for key) in its realm and returns its result only if a
   string, so the frames don't share objects (an about: frame inheriting the parent's CSP) */
static void n_parentText(const FCI &a) {
    FCTX; JsCtx *p = c->parent;
    if (!p || p->dead || p->ctx.IsEmpty() || p->iso != iso || !same_origin(c, p) || !a[0]->IsName()) return;
    v8::Local<v8::Context> pc = p->ctx.Get(iso); v8::Context::Scope cs(pc); v8::TryCatch tc(iso);
    v8::Local<v8::Value> f, r;
    if (pc->Global()->Get(pc, a[0]).ToLocal(&f) && f->IsFunction() && f.As<v8::Function>()->Call(pc, pc->Global(), 0, nullptr).ToLocal(&r) && r->IsString())
        a.GetReturnValue().Set(r);
}
static void n_frameEl(const FCI &a) { FCTX; if (c->parent && same_origin(c, c->parent)) { xref_mark(c, c->parent, "frameElement"); a.GetReturnValue().Set(jwrap(c->parent, c->frame_el)); } else a.GetReturnValue().SetNull(); }
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
bool js_context_fully_active(JsCtx *c) {
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
    a.GetReturnValue().Set(k ? js_context_fully_active(k) : true);
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
        if (a[3]->IsArray() && a[3].As<v8::Array>()->Length()) xref_mark(c, t, "ports");   /* transferred ports are shared objects */
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
    REG(frameWin); REG(frameDoc); REG(parentWin); REG(topWin); REG(xrefd); REG(parentSecure); REG(parentText); REG(frameEl); REG(frameCount); REG(frameAt); REG(ctxRel); REG(navTarget); REG(caller); REG(fullyActive); REG(postTo); REG(portTo); REG(openWin); REG(winOp); REG(openerWin);
#undef REG
}
