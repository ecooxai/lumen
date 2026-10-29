/* Auxiliary top-level contexts retain native identity, not foreign V8 handles.
 * All mutable state here belongs to the browser main thread. The native host
 * reserves/coalesces actions and commits them only after its JS/event batch.
 */
#include "js_int.h"
#include "clone_brand.h"
#include <deque>
#include <limits>
#include <memory>

namespace {
using FCI = v8::FunctionCallbackInfo<v8::Value>;
enum Sandbox : uint32_t {
    SB_NAV = 1u << 0, SB_AUX = 1u << 1, SB_PROPAGATE = 1u << 2,
    SB_ORIGIN = 1u << 3, SB_SCRIPTS = 1u << 4,
    SB_TOP_WITH_ACTIVATION = 1u << 5, SB_TOP_WITHOUT_ACTIVATION = 1u << 6,
    SB_FORMS = 1u << 7
};
struct Principal {
    std::string tuple;
    uint64_t opaque = 0;
    bool operator==(const Principal &other) const {
        return opaque || other.opaque ? opaque && opaque == other.opaque : tuple == other.tuple;
    }
    std::string serialized() const { return opaque ? "null" : tuple; }
};
struct Window {
    JsWindowId id = 0, parent = 0, opener = 0, group = 0;
    JsWindowHost host{};
    JsCtx *current = nullptr;
    uint64_t epoch = 0;
    Principal principal;
    std::string url = "about:blank", name, base, csp, referrer, origin_url, referrer_policy, permissions_policy;
    std::map<std::string, std::string> sessions;
    uint32_t forced_sandbox = 0, sandbox = 0;
    JsWindowCoop coop = JS_COOP_UNSAFE_NONE;
    bool external = false, script_created = false, closing = false;
    bool pending_blank = false, first_navigation = false, activated = false, secure = false;
    bool coep = false, isolated = false;
    double activation_until = 0;
    size_t pending_count = 0, javascript_count = 0;
    size_t form_source_count = 0, form_target_count = 0;
};
struct Message {
    JsWindowId source = 0, target = 0;
    uint64_t epoch = 0, sequence = 0;
    Principal wanted;
    bool wildcard = false;
    std::string origin;
    struct FreeBytes { void operator()(uint8_t *p) const { free(p); } };
    std::unique_ptr<uint8_t, FreeBytes> data;
    size_t data_size = 0;
    std::vector<std::shared_ptr<v8::BackingStore>> buffers;
    size_t bytes = 0;
};
/* javascript: navigation is an actual later task, not an eval during click.
 * Queue entries own only bytes and immutable principal/window identities. */
struct JavascriptNavigation {
    JsWindowId target = 0;
    uint64_t epoch = 0, sequence = 0;
    Principal initiator;
    std::string url, csp;
    size_t bytes = 0;
};
constexpr size_t javascript_max_count = 256, javascript_target_max_count = 64;
constexpr size_t javascript_max_bytes = 8u * 1024u * 1024u;
std::deque<JavascriptNavigation> javascript_navigations;
uint64_t next_javascript_navigation = 1;
size_t javascript_pending_bytes = 0;

/* One planned navigation per form, with bounded owned bytes. The retained DOM
 * node prevents address reuse while a task is pending; source/target epochs
 * cancel tasks when either browsing-context document is retired. */
struct FormNavigation {
    JsWindowId source = 0, target = 0;
    uint64_t source_epoch = 0, target_epoch = 0, sequence = 0;
    struct ReleaseForm { void operator()(Node *node) const { if (node) node_release(node); } };
    std::unique_ptr<Node, ReleaseForm> form;
    std::string url;
    FrameSubmission submission;
    size_t bytes = 0;
};
constexpr size_t form_max_count = 256, form_max_bytes = 64u * 1024u * 1024u;
std::deque<FormNavigation> form_navigations;
uint64_t next_form_navigation = 1;
size_t form_pending_bytes = 0;

std::unordered_map<JsWindowId, std::unique_ptr<Window>> windows;
std::deque<Message> messages;
uint64_t next_window = 1, next_group = 1, next_opaque = 1, next_message = 1;
size_t pending_bytes = 0, serializing_bytes = 0;
bool trustworthy(const URL &url);
bool secure_url(const char *text) {
    URL url{}; if (!text || !url_parse(text, &url)) return false;
    bool secure = trustworthy(url); url_free(&url); return secure;
}

Window *find_window(JsWindowId id) {
    auto it = windows.find(id);
    return it == windows.end() ? nullptr : it->second.get();
}
Window *live_window(JsWindowId id) {
    Window *w = find_window(id);
    return w && !w->closing ? w : nullptr;
}
JsWindowId top_id(JsWindowId id) {
    Window *w = find_window(id);
    for (size_t i = 0; w && w->parent && i < windows.size(); ++i) w = find_window(w->parent);
    return w ? w->id : 0;
}
bool descendant(JsWindowId child, JsWindowId ancestor) {
    for (size_t i = 0; child && i <= windows.size(); ++i) {
        if (child == ancestor) return true;
        Window *w = find_window(child); child = w ? w->parent : 0;
    }
    return false;
}
Window *create_window(const JsWindowHost *host, bool external) {
    if (!next_window || !next_group) return nullptr;
    auto w = std::make_unique<Window>();
    w->id = next_window++; w->group = next_group++; w->external = external;
    if (host) w->host = *host;
    Window *out = w.get(); windows.emplace(w->id, std::move(w)); return out;
}
Principal opaque_principal() { Principal p; p.opaque = next_opaque++; return p; }
Principal url_principal(const char *text, unsigned depth = 0) {
    URL url{};
    if (!text || !url_parse(text, &url)) return opaque_principal();
    Principal p;
    if (!strcmp(url.scheme, "blob") && url.path && !depth) {
        URL nested{}; bool inherits = url_parse(url.path, &nested) &&
            (!strcmp(nested.scheme, "http") || !strcmp(nested.scheme, "https") || !strcmp(nested.scheme, "file"));
        p = inherits ? url_principal(url.path, 1) : opaque_principal();
        url_free(&nested);
    } else if (!url.opaque && url.host &&
               (!strcmp(url.scheme, "http") || !strcmp(url.scheme, "https") ||
                !strcmp(url.scheme, "ws") || !strcmp(url.scheme, "wss") || !strcmp(url.scheme, "ftp"))) {
        char *origin = url_origin(&url); p.tuple = origin ? origin : ""; free(origin);
    } else p = opaque_principal();
    url_free(&url);
    return p;
}
Principal context_principal(const JsCtx *c) { return {c->opaque_origin ? std::string() : c->origin, c->opaque_origin}; }
bool blank_url(const char *url) {
    return !url || !strcmp(url, "about:blank") || !strncmp(url, "about:blank#", 12) ||
           !strcmp(url, "about:srcdoc") || !strncmp(url, "about:srcdoc#", 13);
}
bool token_n(const char *list, size_t length, const char *wanted) {
    if (!list) return false;
    const size_t n = strlen(wanted);
    for (size_t p = 0; p < length;) {
        while (p < length && isspace((unsigned char)list[p])) ++p;
        size_t end = p; while (end < length && !isspace((unsigned char)list[end])) ++end;
        if (end - p == n && !strncasecmp(list + p, wanted, n)) return true;
        p = end;
    }
    return false;
}
uint32_t sandbox_list(const char *list, size_t length) {
    if (!list) return 0;
    uint32_t flags = SB_NAV | SB_AUX | SB_PROPAGATE | SB_ORIGIN | SB_SCRIPTS |
                     SB_TOP_WITH_ACTIVATION | SB_TOP_WITHOUT_ACTIVATION | SB_FORMS;
    if (token_n(list, length, "allow-popups")) flags &= ~SB_AUX;
    if (token_n(list, length, "allow-popups-to-escape-sandbox")) flags &= ~SB_PROPAGATE;
    if (token_n(list, length, "allow-same-origin")) flags &= ~SB_ORIGIN;
    if (token_n(list, length, "allow-scripts")) flags &= ~SB_SCRIPTS;
    if (token_n(list, length, "allow-forms")) flags &= ~SB_FORMS;
    if (token_n(list, length, "allow-top-navigation")) flags &= ~(SB_TOP_WITH_ACTIVATION | SB_TOP_WITHOUT_ACTIVATION);
    if (token_n(list, length, "allow-top-navigation-by-user-activation")) flags &= ~SB_TOP_WITH_ACTIVATION;
    return flags;
}
uint32_t sandbox_list(const char *list) { return sandbox_list(list, list ? strlen(list) : 0); }
uint32_t csp_sandbox(const char *csp) {
    uint32_t flags = 0;
    for (const char *p = csp; p && *p;) {
        while (*p && (isspace((unsigned char)*p) || *p == ';')) ++p;
        const char *end = p; while (*end && *end != ';' && *end != '\n') ++end;
        const char *word = p; while (word < end && !isspace((unsigned char)*word)) ++word;
        if (word - p == 7 && !strncasecmp(p, "sandbox", 7)) {
            std::string list(word, end); flags |= sandbox_list(list.c_str());
        }
        p = *end ? end + 1 : end;
    }
    return flags;
}
uint32_t document_sandbox_flags(const JsCtx *parent, Node *frame,
                                const Document *document, const Window *window) {
    size_t length = 0;
    const char *value = frame ? node_attr_n(frame, "sandbox", &length) : nullptr;
    return (window ? window->forced_sandbox : 0) | (parent ? parent->sandbox_flags : 0) |
           sandbox_list(value, length) | csp_sandbox(document ? document->csp : nullptr);
}
void release_form_accounting(const FormNavigation &request) {
    form_pending_bytes -= request.bytes;
    if (Window *source = find_window(request.source))
        if (source->form_source_count) --source->form_source_count;
    if (Window *target = find_window(request.target))
        if (target->form_target_count) --target->form_target_count;
}
void purge_form_navigations(JsWindowId id, uint64_t epoch) {
    for (auto it = form_navigations.begin(); it != form_navigations.end();) {
        if ((it->source == id && (!epoch || it->source_epoch == epoch)) ||
            (it->target == id && (!epoch || it->target_epoch == epoch))) {
            release_form_accounting(*it);
            it = form_navigations.erase(it);
        } else ++it;
    }
}

void purge_destination(JsWindowId id, uint64_t epoch = 0) {
    purge_form_navigations(id, epoch);
    for (auto it = messages.begin(); it != messages.end();) {
        if (it->target == id && (!epoch || it->epoch == epoch)) {
            pending_bytes -= it->bytes;
            if (Window *w = find_window(id)) if (w->pending_count) --w->pending_count;
            it = messages.erase(it);
        } else ++it;
    }
    for (auto it = javascript_navigations.begin(); it != javascript_navigations.end();) {
        if (it->target == id && (!epoch || it->epoch == epoch)) {
            javascript_pending_bytes -= it->bytes;
            if (Window *w = find_window(id)) if (w->javascript_count) --w->javascript_count;
            it = javascript_navigations.erase(it);
        } else ++it;
    }
}
bool coop_matches(JsWindowCoop a, JsWindowCoop b, bool same, bool popup) {
    if (popup && b == JS_COOP_NOOPENER_ALLOW_POPUPS) return false;
    if (popup && b == JS_COOP_UNSAFE_NONE &&
        (a == JS_COOP_UNSAFE_NONE || a == JS_COOP_SAME_ORIGIN_ALLOW_POPUPS || a == JS_COOP_NOOPENER_ALLOW_POPUPS)) return true;
    if (a != b) return false;
    return a == JS_COOP_UNSAFE_NONE || same;
}
JsCtx *caller(v8::Isolate *iso) {
    v8::Local<v8::Context> entered = iso->GetEnteredOrMicrotaskContext();
    JsCtx *c = entered.IsEmpty() ? nullptr : static_cast<JsCtx *>(entered->GetAlignedPointerFromEmbedderData(1, kTag));
    return c ? c : jctx(iso);
}
bool current_context(const JsCtx *c) {
    Window *w = c ? live_window(c->window_id) : nullptr;
    return w && w->current == c && w->epoch == c->window_epoch && !c->dead;
}
void dom_error(v8::Isolate *iso, const char *name, const char *message) {
    JsCtx *c = jctx(iso); v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> ctor; v8::Local<v8::Object> error;
    if (c && !c->api.IsEmpty() && c->api.Get(iso)->Get(ctx, jstr(iso, "DOMException")).ToLocal(&ctor) && ctor->IsFunction()) {
        v8::Local<v8::Value> args[] = {jstr(iso, message), jstr(iso, name)};
        if (ctor.As<v8::Function>()->NewInstance(ctx, 2, args).ToLocal(&error)) { iso->ThrowException(error); return; }
    }
    error = v8::Exception::Error(jstr(iso, message)).As<v8::Object>();
    (void)error->Set(ctx, jstr(iso, "name"), jstr(iso, name)); iso->ThrowException(error);
}
bool same_view(const JsCtx *source, const Window *target) {
    return source && target && context_principal(source) == target->principal;
}
Window *accessible(JsCtx *source, JsWindowId target) {
    Window *s = source && current_context(source) ? live_window(source->window_id) : nullptr;
    Window *t = live_window(target);
    return s && t && s->group == t->group ? t : nullptr;
}
JsWindowId value_id(v8::Local<v8::Value> value) {
    if (!value->IsBigInt()) return 0;
    bool exact = false; uint64_t id = value.As<v8::BigInt>()->Uint64Value(&exact);
    return exact ? id : 0;
}
bool transient_activation(JsWindowId id) {
    Window *w = live_window(id); return w && w->activation_until > now_ms();
}
void consume_activation(JsWindowId id) {
    JsWindowId top = top_id(id);
    for (auto &entry : windows) if (top_id(entry.first) == top) entry.second->activation_until = 0;
}
} // namespace

static std::string document_base_url(Document *document, JsCtx *owner, bool include_element) {
    if (!document) return "about:blank";
    std::string fallback = owner && owner->doc == document && blank_url(document->url) &&
        !owner->inherited_base_url.empty() ? owner->inherited_base_url :
        document->url ? document->url : "about:blank";
    if (include_element) for (Node *node = document->node.first; node;
                              node = node_next_in_tree(node, &document->node)) {
        if (!node_is(node, A_base)) continue;
        size_t length = 0;
        const char *href = node_attr_n(node, "href", &length);
        if (!href) continue;
        char *resolved = url_join_encoding_n(fallback.c_str(), href, length, document->encoding);
        if (resolved) { fallback = resolved; free(resolved); }
        break;
    }
    return fallback;
}

std::string js_document_base_url(Document *document, bool include_element) {
    /* owner_ctx also knows script-created documents. They keep their own URL:
     * the owner's inherited base applies only to its actual browsing document. */
    return document_base_url(document, owner_ctx(document), include_element);
}

std::string js_windows_base_url(JsCtx *context, bool include_element) {
    return document_base_url(context ? context->doc : nullptr, context, include_element);
}

JsWindowId js_window_new(const JsWindowHost *host) {
    Window *w = create_window(host, true); return w ? w->id : 0;
}
bool js_window_is_closed(JsWindowId id) { return !live_window(id); }
JsWindowId js_window_id(const JsCtx *c) { return c ? c->window_id : 0; }
void js_windows_pending(size_t *count, size_t *bytes) {
    if (count) *count = messages.size(); if (bytes) *bytes = pending_bytes;
}
void js_form_navigations_pending(size_t *count, size_t *bytes) {
    if (count) *count = form_navigations.size(); if (bytes) *bytes = form_pending_bytes;
}
void js_window_destroy(JsWindowId id) {
    Window *w = find_window(id); if (!w) return;
    std::vector<JsWindowId> children;
    for (auto &entry : windows) if (entry.second->parent == id) children.push_back(entry.first);
    for (JsWindowId child : children) js_window_destroy(child);
    purge_destination(id);
    /* No JsCtx or V8 handle is dereferenced here: crash recovery can retire a
     * faulted realm's ID without attempting its unsafe native destruction. */
    windows.erase(id);
}
JsWindowId js_window_commit(JsWindowId id, Document *d, JsWindowCoop coop) {
    Window *w = live_window(id); if (!w || !d) return 0;
    bool initial_blank = w->pending_blank && blank_url(d->url);
    uint32_t flags = w->forced_sandbox | csp_sandbox(d->csp);
    Principal principal = initial_blank ? w->principal : flags & SB_ORIGIN ? opaque_principal() : url_principal(d->url);
    if (!w->parent && w->epoch && !initial_blank &&
        !coop_matches(w->coop, coop, w->principal == principal, w->first_navigation)) {
        JsWindowHost host = w->host; bool external = w->external, scripted = w->script_created;
        const uint32_t forced = w->forced_sandbox;
        auto sessions = std::move(w->sessions);
        js_window_destroy(id); w = create_window(&host, external); if (!w) return 0;
        w->script_created = scripted; w->forced_sandbox = forced; w->sessions = std::move(sessions); id = w->id;
    }
    if (initial_blank) {
        free(d->base_url); d->base_url = w->base.empty() ? nullptr : xstrdup(w->base.c_str());
        free(d->csp); d->csp = w->csp.empty() ? nullptr : xstrdup(w->csp.c_str());
        free(d->permissions_policy); d->permissions_policy = w->permissions_policy.empty() ? nullptr : xstrdup(w->permissions_policy.c_str());
        free(d->referrer); d->referrer = w->referrer.empty() ? nullptr : xstrdup(w->referrer.c_str());
        snprintf(d->referrer_policy, sizeof d->referrer_policy, "%s", w->referrer_policy.c_str());
        d->coep_require_corp = w->coep; d->cross_origin_isolated = w->isolated;
        flags = w->forced_sandbox | csp_sandbox(d->csp);
        w->pending_blank = false; /* reserved epoch already represents this document */
    } else {
        purge_destination(id); ++w->epoch; w->first_navigation = false;
        if (!(w->principal == principal)) w->name.clear();
        w->principal = principal; w->origin_url = d->url ? d->url : "about:blank"; w->coop = coop;
        w->secure = secure_url(d->url); w->coep = d->coep_require_corp; w->isolated = d->cross_origin_isolated;
    }
    if (!w->epoch) w->epoch = 1;
    w->current = nullptr; w->url = d->url ? d->url : "about:blank"; w->sandbox = flags;
    return id;
}
void js_windows_attach(JsCtx *c) {
    Window *w = live_window(c->host.window_id);
    if (!w) {
        Window *parent = c->parent ? find_window(c->parent->window_id) : nullptr;
        w = create_window(parent ? &parent->host : nullptr, false);
        if (!w) return;
        if (parent) { w->parent = parent->id; w->group = parent->group; w->closing = parent->closing; }
        c->host.window_id = w->id;
    }
    if (!w->epoch) js_window_commit(w->id, c->doc, JS_COOP_UNSAFE_NONE);
    uint32_t flags = document_sandbox_flags(c->parent, c->frame_el, c->doc, w);
    if (c->parent) {
        w->parent = c->parent->window_id;
        if (Window *parent = find_window(w->parent)) {
            w->group = parent->group;
            w->secure = parent->secure && (blank_url(c->doc->url) || secure_url(c->doc->url));
            if (blank_url(c->doc->url)) {
                free(c->doc->base_url);
                c->doc->base_url = xstrdup(js_windows_base_url(c->parent, true).c_str());
            }
        }
        else w->closing = true;
        if (flags & SB_ORIGIN) w->principal = opaque_principal();
        else if (blank_url(c->doc->url)) w->principal = context_principal(c->parent);
        else w->principal = url_principal(c->doc->url);
        w->origin_url = blank_url(c->doc->url) ? (c->parent->inherited_origin_url.empty() ? c->parent->doc->url : c->parent->inherited_origin_url) : c->doc->url;
        if (const char *name = node_attr(c->frame_el, "name")) if (w->name.empty()) w->name = name;
    } else if ((flags & SB_ORIGIN) && !w->principal.opaque) w->principal = opaque_principal();
    w->sandbox = flags; w->current = c;
    c->window_id = w->id; c->window_epoch = w->epoch;
    c->origin = w->principal.serialized(); c->opaque_origin = w->principal.opaque;
    c->inherited_origin_url = w->origin_url;
    c->inherited_base_url = blank_url(c->doc->url) && c->doc->base_url ? c->doc->base_url : "";
    c->sandbox_flags = flags; c->sb_opaque = flags & SB_ORIGIN; c->sb_noscript = flags & SB_SCRIPTS;
}
void js_windows_detach(JsCtx *c, bool close_frame) {
    Window *w = c ? find_window(c->window_id) : nullptr;
    if (!w || w->current != c || w->epoch != c->window_epoch) return;
    if ((close_frame && c->parent) || (!w->external && !c->parent)) { js_window_destroy(w->id); return; }
    w->current = nullptr; purge_destination(w->id, c->window_epoch);
}
bool js_windows_is_secure(const JsCtx *c) {
    Window *w = c ? live_window(c->window_id) : nullptr;
    return w && w->current == c && w->epoch == c->window_epoch && w->secure;
}
bool js_windows_has_activation(const JsCtx *c) {
    return c && current_context(c) && transient_activation(c->window_id);
}
bool js_windows_same_origin(const JsCtx *a, const JsCtx *b) {
    return a && b && (a == b || context_principal(a) == context_principal(b));
}
WebEncoding js_windows_frame_encoding_fallback(const JsCtx *parent, Node *frame,
                                               const Document *incoming) {
    /* UNKNOWN selects the locale default; UTF-8 is an actual eligible parent. */
    if (!parent || !current_context(parent) || !parent->doc || !frame ||
        frame->doc != parent->doc || !incoming || !incoming->url || parent->opaque_origin)
        return WEB_ENC_UNKNOWN;
    const WebEncoding encoding = parent->doc->encoding;
    if (encoding < WEB_ENC_UTF8 || encoding >= WEB_ENC_COUNT ||
        encoding == WEB_ENC_UTF16LE || encoding == WEB_ENC_UTF16BE)
        return WEB_ENC_UNKNOWN;
    const JsCtx *previous = js_frame_ctx(frame);
    const Window *window = previous ? find_window(previous->window_id) : nullptr;
    if (document_sandbox_flags(parent, frame, incoming, window) & SB_ORIGIN)
        return WEB_ENC_UNKNOWN;
    /* Compare only canonical tuple origins. Opaque "null" serializations do
     * not establish shared identity, and no synthetic principal is allocated. */
    URL url{};
    if (!url_parse(incoming->url, &url)) return WEB_ENC_UNKNOWN;
    char *origin = url_origin(&url);
    const bool same = origin && strcmp(origin, "null") && parent->origin == origin;
    free(origin); url_free(&url);
    return same ? encoding : WEB_ENC_UNKNOWN;
}

v8::Local<v8::Value> js_window_value(JsCtx *viewer, JsWindowId id) {
    v8::Isolate *iso = viewer->iso;
    if (!id) return v8::Null(iso);
    Window *w = accessible(viewer, id);
    if (w && w->current && w->current->iso == iso && same_view(viewer, w) && !w->current->ctx.IsEmpty())
        return w->current->ctx.Get(iso)->Global();
    if (viewer->window_refs.size() > 256) {
        for (auto it = viewer->window_refs.begin(); it != viewer->window_refs.end();) {
            if (!find_window(*it)) it = viewer->window_refs.erase(it);
            else ++it;
        }
    }
    viewer->window_refs.insert(id);
    return v8::BigInt::NewFromUnsigned(iso, id);
}
JsCtx *js_window_context(JsCtx *viewer, v8::Local<v8::Value> value) {
    JsWindowId id = value_id(value);
    if (!id && value->IsObject()) {
        v8::Local<v8::Context> creation;
        if (value.As<v8::Object>()->GetCreationContext(viewer->iso).ToLocal(&creation)) {
            JsCtx *c = static_cast<JsCtx *>(creation->GetAlignedPointerFromEmbedderData(1, kTag));
            if (c && value->StrictEquals(creation->Global())) id = c->window_id;
        }
    }
    Window *w = accessible(viewer, id);
    return w && w->current && w->current->iso == viewer->iso ? w->current : nullptr;
}
void js_window_activate(JsCtx *c) {
    if (!current_context(c)) return;
    const double deadline = now_ms() + 5000;
    const JsWindowId top = top_id(c->window_id);
    for (auto &entry : windows) {
        Window *w = entry.second.get();
        if (top_id(w->id) == top && (descendant(c->window_id, w->id) || w->principal == context_principal(c))) {
            w->activated = true; w->activation_until = deadline;
        }
    }
}
bool js_window_can_sleep(JsWindowId id) {
    Window *w = live_window(id); if (!w) return true;
    for (auto &entry : windows) {
        Window *other = entry.second.get(); if (other->closing) continue;
        if (top_id(other->id) == id && (other->pending_count ||
            other->form_source_count || other->form_target_count)) return false;
        if (live_window(other->opener) && (top_id(other->opener) == id || top_id(other->id) == id)) return false;
        if (other->current && top_id(other->id) != id)
            for (JsWindowId ref : other->current->window_refs)
                if (live_window(ref) && top_id(ref) == id) return false;
    }
    return true;
}
const char *js_window_session_get(JsCtx *c, const std::string &origin) {
    Window *w = find_window(top_id(c->window_id)); if (!w) return nullptr;
    auto it = w->sessions.find(origin); return it == w->sessions.end() ? nullptr : it->second.c_str();
}
void js_window_session_set(JsCtx *c, const std::string &origin, const std::string &value) {
    Window *w = find_window(top_id(c->window_id)); if (w) w->sessions[origin] = value;
}
void js_window_session_forget(void *ud) {
    for (auto &entry : windows) {
        Window *w = entry.second.get(); if (!w->parent && w->current && w->current->host.ud == ud) w->sessions.clear();
    }
}

namespace {
std::string policy_token(const char *header) {
    if (!header) return {};
    while (*header == ' ' || *header == '\t') ++header;
    const char *end = header; while (*end && *end != ';' && *end != ' ' && *end != '\t') ++end;
    std::string value(header, end);
    while (*end == ' ' || *end == '\t') ++end;
    if (*end && *end != ';') return {};
    /* A comma denotes a combined/invalid singleton field, not another policy. */
    if (strchr(end, ',')) return {};
    return value;
}
bool trustworthy(const URL &u) {
    if (!strcmp(u.scheme, "https")) return true;
    if (strcmp(u.scheme, "http") || !u.host) return false;
    std::string host = u.host;
    if (!host.empty() && host.back() == '.') host.pop_back();
    if (host == "localhost" || host == "[::1]" ||
        (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0)) return true;
    /* URL parsing has already canonicalized IPv4, including unusual spellings. */
    return host.rfind("127.", 0) == 0 && host.find_first_not_of("0123456789.") == std::string::npos;
}
bool can_navigate(JsCtx *source, Window *target) {
    if (!source || !target || !current_context(source)) return false;
    const JsWindowId sid = source->window_id, tid = target->id;
    if (sid == tid || descendant(tid, sid)) return true;
    const JsWindowId source_top = top_id(sid);
    if (source->sandbox_flags & SB_NAV) {
        if (target->parent || tid != source_top) {
            /* A sandbox may navigate the auxiliary it itself was allowed to create. */
            if (target->opener != sid) return false;
        } else if (transient_activation(sid) ? source->sandbox_flags & SB_TOP_WITH_ACTIVATION :
                                             source->sandbox_flags & SB_TOP_WITHOUT_ACTIVATION) return false;
    }
    if (same_view(source, target)) return true;
    if (target->opener == sid) return true;
    Window *s = find_window(sid);
    if (s && s->opener && (tid == s->opener || descendant(s->opener, tid))) return true;
    /* Cross-origin frames need activation to navigate their own top context. */
    if (tid == source_top && !target->parent) return transient_activation(sid);
    return false;
}
bool window_string(v8::Isolate *iso, v8::Local<v8::Value> value, std::string &out) {
    /* Public Web IDL conversion already produced a string. Bound the temporary
     * UTF-8 copy before constructing it as well as the final native request. */
    if (value->IsString() && value.As<v8::String>()->Length() > 65536) {
        dom_error(iso, "QuotaExceededError", "Window request text exceeds the implementation limit."); return false;
    }
    out = jcstr(iso, value);
    if (out.size() > 65536) { dom_error(iso, "QuotaExceededError", "Window request text exceeds the implementation limit."); return false; }
    return true;
}
bool absolute_navigation(JsCtx *source, const std::string &input, std::string &out) {
    if (input.size() > 65536) { dom_error(source->iso, "QuotaExceededError", "Window navigation URL exceeds the implementation limit."); return false; }
    const std::string base = js_windows_base_url(source, true);
    if (base.size() > 65536) { dom_error(source->iso, "QuotaExceededError", "Window navigation base URL exceeds the implementation limit."); return false; }
    char *joined = url_join_n(base.c_str(), input.data(), input.size());
    if (!joined) { dom_error(source->iso, "SyntaxError", "The window navigation URL is invalid."); return false; }
    if (strlen(joined) > 65536) { free(joined); dom_error(source->iso, "QuotaExceededError", "Resolved window navigation URL exceeds the implementation limit."); return false; }
    URL url{}; bool parsed = url_parse(joined, &url);
    bool allowed = parsed && (!strcmp(url.scheme, "http") || !strcmp(url.scheme, "https") ||
        !strcmp(joined, "about:blank") || !strncmp(joined, "about:blank#", 12));
    url_free(&url);
    if (allowed) out = joined;
    free(joined);
    if (!allowed) dom_error(source->iso, "SecurityError", "Auxiliary navigation supports HTTP, HTTPS and about:blank URLs.");
    return allowed;
}
std::string navigation_referrer(JsCtx *source, const std::string &url) {
    const char *document_url = blank_url(source->doc->url) && !source->inherited_origin_url.empty() ?
                               source->inherited_origin_url.c_str() : source->doc->url;
    char *text = net_referrer(document_url, url.c_str(), source->doc->referrer_policy);
    std::string ref = text ? text : ""; free(text); return ref;
}
bool navigate_window(JsCtx *source, Window *target, const std::string &input, bool replace) {
    if (!can_navigate(source, target)) { dom_error(source->iso, "SecurityError", "This window is not allowed to navigate the target."); return false; }
    std::string url; if (!absolute_navigation(source, input, url)) return false;
    if (!current_context(source)) return false;
    if (target->parent) {
        if (target->current && target->current->iso == source->iso) { frame_nav(target->current, url); return true; }
        dom_error(source->iso, "NotSupportedError", "Cross-tab iframe navigation is not supported."); return false;
    }
    std::string ref = navigation_referrer(source, url);
    JsWindowNavigate request{target->id, url.c_str(), ref.empty() ? nullptr : ref.c_str(), replace};
    if (target->host.navigate) return target->host.navigate(target->host.ud, &request);
    if (target->current == source && source->host.navigate) { source->host.navigate(source->host.ud, url.c_str()); return true; }
    return false;
}
struct Features { bool noopener = false, noreferrer = false; };
Features parse_features(const std::string &input) {
    Features out; size_t p = 0;
    while (p < input.size()) {
        while (p < input.size() && (isspace((unsigned char)input[p]) || input[p] == ',')) ++p;
        size_t start = p;
        while (p < input.size() && !isspace((unsigned char)input[p]) && input[p] != ',' && input[p] != '=') ++p;
        std::string name = input.substr(start, p - start);
        for (char &ch : name) ch = (char)tolower((unsigned char)ch);
        while (p < input.size() && isspace((unsigned char)input[p])) ++p;
        std::string value;
        if (p < input.size() && input[p] == '=') {
            ++p; while (p < input.size() && isspace((unsigned char)input[p])) ++p;
            start = p; while (p < input.size() && !isspace((unsigned char)input[p]) && input[p] != ',') ++p;
            value = input.substr(start, p - start);
            for (char &ch : value) ch = (char)tolower((unsigned char)ch);
        }
        bool yes = value.empty() || value == "yes" || value == "true" || (value != "no" && value != "false" && strtol(value.c_str(), nullptr, 10) != 0);
        if (name == "noopener") out.noopener = yes;
        if (name == "noreferrer") out.noreferrer = yes;
        if (p == start && p < input.size() && input[p] == '=') ++p;
    }
    if (out.noreferrer) out.noopener = true;
    return out;
}
#include "form_navigation.h"
#include "form_encoding.h"

void n_winId(const FCI &a) { JsCtx *c = jctx(a.GetIsolate()); a.GetReturnValue().Set(v8::BigInt::NewFromUnsigned(a.GetIsolate(), c->window_id)); }
void n_winOpen(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); JsCtx *source = caller(iso);
    a.GetReturnValue().SetNull();
    if (!current_context(source)) return;
    std::string input, name, features;
    if (!window_string(iso, a[0], input) || !window_string(iso, a[1], name) || !window_string(iso, a[2], features)) return;
    std::string url = "about:blank";
    if (!input.empty() && !absolute_navigation(source, input, url)) return;
    if (name.empty()) name = "_blank";
    std::string reserved = name; for (char &ch : reserved) ch = (char)tolower((unsigned char)ch);
    Features f = parse_features(features);
    Window *s = live_window(source->window_id), *target = nullptr;
    if (!s) return;
    if (reserved == "_self") target = s;
    else if (reserved == "_parent") target = live_window(s->parent ? s->parent : s->id);
    else if (reserved == "_top") target = live_window(top_id(s->id));
    else if (!f.noopener && reserved != "_blank") {
        for (auto &entry : windows) {
            Window *w = entry.second.get();
            if (!w->closing && w->group == s->group && w->name == name) { target = w; break; }
        }
    }
    if (target) {
        if (!can_navigate(source, target)) { dom_error(iso, "SecurityError", "This window is not allowed to navigate the named target."); return; }
        if (!input.empty()) (void)navigate_window(source, target, url, false);
        if (!f.noopener) a.GetReturnValue().Set(js_window_value(source, target->id));
        return;
    }
    if ((source->sandbox_flags & SB_AUX) || !transient_activation(source->window_id) || !s->host.open) return;
    const JsWindowId source_id = s->id, source_top = top_id(source_id);
    Window *top = live_window(source_top);
    const bool same_top = top && context_principal(source) == top->principal;
    const JsWindowCoop creator_coop = same_top ? top->coop : JS_COOP_UNSAFE_NONE;
    /* COOP belongs to the top active document. A same-origin creator frame
     * inherits that policy; a cross-origin creator under a strict policy must
     * open independently. This rule runs only after existing-target lookup. */
    if (top && !same_top && (top->coop == JS_COOP_SAME_ORIGIN || top->coop == JS_COOP_SAME_ORIGIN_PLUS_COEP))
        f.noopener = true;
    const JsWindowHost host = s->host;
    Window *w = create_window(&host, true); if (!w) return;
    const JsWindowId id = w->id;
    w->script_created = true; w->pending_blank = true; w->first_navigation = true; w->epoch = 1;
    w->opener = f.noopener ? 0 : source_id; w->group = f.noopener ? w->group : s->group;
    w->forced_sandbox = source->sandbox_flags & SB_PROPAGATE ? source->sandbox_flags : 0;
    w->sandbox = w->forced_sandbox;
    w->principal = w->sandbox & SB_ORIGIN ? opaque_principal() : context_principal(source);
    w->coop = creator_coop;
    w->secure = s->secure; w->coep = source->doc->coep_require_corp; w->isolated = source->doc->cross_origin_isolated;
    w->name = reserved == "_blank" || f.noopener ? "" : name;
    w->base = js_windows_base_url(source, true);
    w->origin_url = blank_url(source->doc->url) && !source->inherited_origin_url.empty() ? source->inherited_origin_url : source->doc->url ? source->doc->url : "about:blank";
    w->csp = source->doc->csp ? source->doc->csp : ""; w->permissions_policy = source->doc->permissions_policy ? source->doc->permissions_policy : "";
    w->referrer_policy = source->doc->referrer_policy;
    w->referrer = f.noreferrer ? "" : navigation_referrer(source, w->origin_url);
    if (!f.noopener) if (Window *top = find_window(source_top)) w->sessions = top->sessions;
    const std::string ref = f.noreferrer ? "" : navigation_referrer(source, url);
    JsWindowOpen request{id, source_id, source_top, w->opener, w->name.c_str(), url.c_str(), ref.empty() ? nullptr : ref.c_str(), f.noreferrer};
    if (!host.open(host.ud, &request)) { js_window_destroy(id); return; }
    consume_activation(source_id);
    if (js_wakeup) js_wakeup();
    if (!f.noopener) a.GetReturnValue().Set(js_window_value(source, id));
}
void n_winOp(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); JsCtx *source = caller(iso);
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    const JsWindowId id = value_id(a[0]);
    int op = a[1]->Int32Value(ctx).FromMaybe(-1);
    if (op == 18) { if (source) source->window_refs.erase(id); return; }
    Window *w = accessible(source, id);
    if (op == 3) { a.GetReturnValue().Set(!w); return; }
    if (op == 4) { a.GetReturnValue().Set(same_view(source, w)); return; }
    if (op == 14) { a.GetReturnValue().Set(w && w->current && w->current->iso == iso); return; }
    if (!w) { if (op == 2) a.GetReturnValue().Set(0); else a.GetReturnValue().SetNull(); return; }
    switch (op) {
    case 0: a.GetReturnValue().Set(js_window_value(source, w->parent ? w->parent : id)); break;
    case 1: a.GetReturnValue().Set(js_window_value(source, top_id(id))); break;
    case 2: { int n = 0; for (auto &entry : windows) if (entry.second->parent == id && !entry.second->closing) ++n; a.GetReturnValue().Set(n); break; }
    case 5:
    case 7:
    case 8:
        if (!same_view(source, w)) { dom_error(iso, "SecurityError", "Cross-origin window information is not accessible."); return; }
        if (op == 5) a.GetReturnValue().Set(jstr(iso, w->current && w->current->doc->url ? w->current->doc->url : w->url.c_str()));
        if (op == 7) a.GetReturnValue().Set(jstr(iso, w->name.c_str()));
        if (op == 8) { std::string name; if (!window_string(iso, a[2], name)) return; w->name = std::move(name); }
        break;
    case 6: a.GetReturnValue().Set(js_window_value(source, w->opener)); break;
    case 9:
        if (!same_view(source, w)) { dom_error(iso, "SecurityError", "Cross-origin opener assignment is not allowed."); return; }
        w->opener = 0; break;
    case 10:
        if (w->parent || !w->script_created || !w->host.close) break;
        if (w->host.close(w->host.ud, id)) {
            w->closing = true;
            std::vector<JsWindowId> affected;
            for (auto &entry : windows) if (descendant(entry.first, id)) affected.push_back(entry.first);
            for (JsWindowId child : affected) { if (Window *k = find_window(child)) k->closing = true; purge_destination(child); }
            if (js_wakeup) js_wakeup();
        }
        break;
    case 11:
        if (Window *top = live_window(top_id(id))) if (top->host.focus) (void)top->host.focus(top->host.ud, top->id);
        break;
    case 12: { std::string url; if (!window_string(iso, a[2], url)) return; a.GetReturnValue().Set(navigate_window(source, w, url, a[3]->BooleanValue(iso))); break; }
    case 15: {
        v8::Local<v8::Value> values[] = {v8::Boolean::New(iso, w->activated), v8::Boolean::New(iso, transient_activation(id))};
        a.GetReturnValue().Set(v8::Array::New(iso, values, 2)); break;
    }
    case 16: a.GetReturnValue().Set(w->secure); break;
    case 17:
        if (!same_view(source, w)) { dom_error(iso, "SecurityError", "Cross-origin window information is not accessible."); return; }
        a.GetReturnValue().Set(jstr(iso, w->principal.serialized().c_str())); break;
    }
}
} // namespace

namespace {
void queue_javascript_navigation(JsCtx *source, Window *target, const std::string &url) {
    if (!source || !target || !target->current || !current_context(source) ||
        !current_context(target->current)) return;
    /* Opaque principals compare by their native identity, never by "null". */
    if (!(context_principal(source) == target->principal) || target->current->sb_noscript) return;
    const char *policy = source->doc && source->doc->csp ? source->doc->csp : "";
    const size_t policy_length = strlen(policy);
    if (url.size() > 65536 || policy_length > javascript_max_bytes ||
        url.size() > javascript_max_bytes - policy_length ||
        javascript_navigations.size() >= javascript_max_count ||
        target->javascript_count >= javascript_target_max_count) {
        dom_error(source->iso, "QuotaExceededError", "The javascript navigation queue is full."); return;
    }
    JavascriptNavigation request;
    request.target = target->id; request.epoch = target->epoch;
    request.initiator = context_principal(source); request.url = url; request.csp = policy;
    request.bytes = sizeof(request) + request.url.capacity() + request.csp.capacity() + request.initiator.tuple.capacity();
    if (request.bytes > javascript_max_bytes - javascript_pending_bytes) {
        dom_error(source->iso, "QuotaExceededError", "The javascript navigation queue is full."); return;
    }
    request.sequence = next_javascript_navigation++;
    javascript_pending_bytes += request.bytes; ++target->javascript_count;
    javascript_navigations.push_back(std::move(request));
    if (js_wakeup) js_wakeup();
}
} // namespace

bool js_windows_navigate(JsCtx *source, JsCtx *target, const std::string &url, bool replace) {
    if (!current_context(source) || !current_context(target)) return true;
    Window *w = accessible(source, target->window_id);
    if (!w || !can_navigate(source, w)) { dom_error(source->iso, "SecurityError", "This window is not allowed to navigate the target."); return true; }
    URL parsed{};
    const bool valid = url_parse_n(url.data(), url.size(), &parsed);
    const bool javascript = valid && !strcmp(parsed.scheme, "javascript");
    const bool web = valid && (!strcmp(parsed.scheme, "http") || !strcmp(parsed.scheme, "https") || blank_url(url.c_str()));
    if (javascript) {
        char *canonical = url_to_string(&parsed); url_free(&parsed);
        if (canonical) { queue_javascript_navigation(source, w, canonical); free(canonical); }
        return true;
    }
    url_free(&parsed);
    /* Keep pre-existing local-document/frame navigation with its original host.
     * Native web tabs and every script-created auxiliary use deferred commands. */
    if (w->parent || !w->host.navigate) return false;
    if (!web && !w->script_created) return false;
    (void)navigate_window(source, w, url, replace); return true;
}

JsWindowCoop js_window_coop(const char *url, const char *coop, const char *coep) {
    URL parsed{};
    if (!url || !url_parse(url, &parsed)) return JS_COOP_UNSAFE_NONE;
    bool secure = trustworthy(parsed); url_free(&parsed);
    if (!secure) return JS_COOP_UNSAFE_NONE;
    const std::string policy = policy_token(coop), embedder = policy_token(coep);
    if (policy == "same-origin-allow-popups") return JS_COOP_SAME_ORIGIN_ALLOW_POPUPS;
    if (policy == "noopener-allow-popups") return JS_COOP_NOOPENER_ALLOW_POPUPS;
    if (policy == "same-origin") return embedder == "require-corp" || embedder == "credentialless" ? JS_COOP_SAME_ORIGIN_PLUS_COEP : JS_COOP_SAME_ORIGIN;
    return JS_COOP_UNSAFE_NONE;
}

namespace {
struct WindowSerializer : v8::ValueSerializer::Delegate {
    v8::Isolate *iso;
    size_t limit, capacity = 0;
    bool exceeded = false, queue_exceeded = false;
    std::unordered_multimap<int, v8::Local<v8::Object>> blocked;
    WindowSerializer(JsCtx *c, size_t bytes) : iso(c->iso), limit(bytes) {
        auto ctx = iso->GetCurrentContext(); v8::Local<v8::Value> value;
        if (!c->api.IsEmpty() && c->api.Get(iso)->Get(ctx, jstr(iso, "windowCloneBlocked")).ToLocal(&value) && value->IsArray()) {
            auto list = value.As<v8::Array>();
            for (uint32_t i = 0; i < list->Length(); ++i) if (list->Get(ctx, i).ToLocal(&value) && value->IsObject()) {
                auto object = value.As<v8::Object>(); blocked.emplace(object->GetIdentityHash(), object);
            }
        }
    }
    ~WindowSerializer() override { serializing_bytes -= capacity; }
    size_t release_capacity() { size_t bytes = capacity; serializing_bytes -= bytes; capacity = 0; return bytes; }
    void ThrowDataCloneError(v8::Local<v8::String>) override {
        jsg_throw_clone_exception(iso, jstr(iso, queue_exceeded ?
            "The message queue and active serializations exceed the 32 MiB limit." : exceeded ?
            "The serialized message exceeds the 8 MiB limit." : "This value cannot be cloned between windows."),
            queue_exceeded ? "QuotaExceededError" : "DataCloneError");
    }
    bool HasCustomHostObject(v8::Isolate *) override { return true; }
    v8::Maybe<bool> IsHostObject(v8::Isolate *, v8::Local<v8::Object> object) override {
        if (object->InternalFieldCount()) return v8::Just(true);
        auto platform = jsg_is_uncloneable(iso, iso->GetCurrentContext(), object);
        if (platform.IsNothing() || platform.FromJust()) return platform;
        auto prototype = object->GetPrototype();
        for (unsigned depth = 0; !prototype.IsEmpty() && prototype->IsObject(); ++depth) {
            if (depth == 128) { ThrowDataCloneError({}); return v8::Nothing<bool>(); }
            auto p = prototype.As<v8::Object>();
            auto range = blocked.equal_range(p->GetIdentityHash());
            for (auto it = range.first; it != range.second; ++it) if (p->StrictEquals(it->second)) return v8::Just(true);
            prototype = p->GetPrototype();
        }
        return prototype.IsEmpty() ? v8::Nothing<bool>() : v8::Just(false);
    }
    v8::Maybe<bool> WriteHostObject(v8::Isolate *, v8::Local<v8::Object>) override {
        ThrowDataCloneError({}); return v8::Nothing<bool>();
    }
    v8::Maybe<uint32_t> GetSharedArrayBufferId(v8::Isolate *, v8::Local<v8::SharedArrayBuffer>) override {
        ThrowDataCloneError({}); return v8::Nothing<uint32_t>();
    }
    v8::Maybe<uint32_t> GetWasmModuleTransferId(v8::Isolate *, v8::Local<v8::WasmModuleObject>) override {
        ThrowDataCloneError({}); return v8::Nothing<uint32_t>();
    }
    bool AdoptSharedValueConveyor(v8::Isolate *, v8::SharedValueConveyor &&) override { ThrowDataCloneError({}); return false; }
    void *ReallocateBufferMemory(void *old, size_t requested, size_t *actual) override {
        if (requested > limit) { exceeded = true; return nullptr; }
        if (requested > JS_WINDOW_PENDING_MAX_BYTES - pending_bytes - (serializing_bytes - capacity)) {
            queue_exceeded = true; return nullptr;
        }
        void *memory = realloc(old, requested);
        if (memory) { serializing_bytes -= capacity; *actual = requested; capacity = requested; serializing_bytes += capacity; }
        return memory;
    }
    void FreeBufferMemory(void *memory) override {
        free(memory); serializing_bytes -= capacity; capacity = 0;
    }
};
bool wanted_origin(JsCtx *source, const std::string &text, Message &message) {
    if (text == "*") { message.wildcard = true; return true; }
    if (text == "/") { message.wanted = context_principal(source); return true; }
    URL url{};
    if (!url_parse_n(text.data(), text.size(), &url)) { dom_error(source->iso, "SyntaxError", "The target origin is not a valid absolute URL."); return false; }
    char *canonical = url_to_string(&url); url_free(&url);
    message.wanted = url_principal(canonical); free(canonical); return true;
}
bool queue_capacity(Window *target, size_t bytes, size_t active_capacity = 0) {
    return messages.size() < JS_WINDOW_PENDING_MAX_COUNT &&
           target && target->pending_count < JS_WINDOW_TARGET_MAX_COUNT &&
           bytes <= JS_WINDOW_PENDING_MAX_BYTES - pending_bytes - (serializing_bytes - active_capacity);
}
void n_winPost(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate(); JsCtx *source = caller(iso);
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    Message message;
    if (!current_context(source)) return;
    const JsWindowId target_id = value_id(a[0]);
    std::string wanted;
    if (!window_string(iso, a[2], wanted) || !wanted_origin(source, wanted, message)) return;
    Window *target = accessible(source, target_id);
    if (!target) return;
    message.source = source->window_id; message.origin = source->origin;
    message.target = target_id; message.epoch = target->epoch;
    const uint64_t group = target->group;
    if (!queue_capacity(target, 0)) { dom_error(iso, "QuotaExceededError", "The destination message queue is full."); return; }
    std::vector<v8::Local<v8::ArrayBuffer>> transfer;
    size_t transfer_bytes = 0;
    if (a[3]->IsArray()) {
        auto list = a[3].As<v8::Array>();
        if (list->Length() > 1024) { dom_error(iso, "DataCloneError", "The message transfer list is too large."); return; }
        for (uint32_t i = 0; i < list->Length(); ++i) {
            v8::Local<v8::Value> value; if (!list->Get(ctx, i).ToLocal(&value)) return;
            if (!value->IsArrayBuffer()) { dom_error(iso, "DataCloneError", "Only ArrayBuffer transfer is supported between independent windows."); return; }
            auto buffer = value.As<v8::ArrayBuffer>();
            if (buffer->WasDetached() || !buffer->IsDetachable() ||
                std::any_of(transfer.begin(), transfer.end(), [&](auto other) { return buffer->StrictEquals(other); })) {
                dom_error(iso, "DataCloneError", "The transfer list contains a detached, non-transferable or duplicate buffer."); return;
            }
            size_t bytes = buffer->GetBackingStore()->MaxByteLength();
            if (bytes > JS_WINDOW_MESSAGE_MAX_BYTES - transfer_bytes) { dom_error(iso, "DataCloneError", "Transferred buffers exceed the 8 MiB message limit."); return; }
            transfer_bytes += bytes; transfer.push_back(buffer);
        }
    }
    WindowSerializer delegate(source, JS_WINDOW_MESSAGE_MAX_BYTES - transfer_bytes);
    v8::ValueSerializer serializer(iso, &delegate);
    for (uint32_t i = 0; i < transfer.size(); ++i) serializer.TransferArrayBuffer(i, transfer[i]);
    serializer.WriteHeader();
    if (!serializer.WriteValue(ctx, a[1]).FromMaybe(false)) return;
    auto bytes = serializer.Release(); message.data.reset(bytes.first); message.data_size = bytes.second;
    /* Getters may close/navigate a context, detach/resize transferables, or fill
     * this same queue. Revalidate using IDs/epochs, never an old Window pointer. */
    target = live_window(target_id);
    if (!target || target->epoch != message.epoch || target->group != group) return;
    message.bytes = delegate.capacity; /* Release() reports written length, not retained allocation capacity. */
    for (auto buffer : transfer) {
        if (buffer->WasDetached() || !buffer->IsDetachable()) { dom_error(iso, "DataCloneError", "A transferred buffer changed during serialization."); return; }
        auto store = buffer->GetBackingStore(); size_t retained = store->MaxByteLength();
        if (retained > JS_WINDOW_MESSAGE_MAX_BYTES - message.bytes) { dom_error(iso, "DataCloneError", "The serialized message exceeds the 8 MiB limit."); return; }
        message.bytes += retained; message.buffers.push_back(std::move(store));
    }
    if (!queue_capacity(target, message.bytes, delegate.capacity)) { dom_error(iso, "QuotaExceededError", "The destination message queue is full."); return; }
    for (auto buffer : transfer) if (!buffer->Detach(v8::Local<v8::Value>()).FromMaybe(false)) return;
    (void)delegate.release_capacity(); /* move wire allocation accounting into the accepted queue */
    pending_bytes += message.bytes; ++target->pending_count; message.sequence = next_message++;
    messages.push_back(std::move(message));
    if (js_wakeup) js_wakeup();
}
} // namespace

JsCtx *js_window_caller(v8::Isolate *iso) { return caller(iso); }
bool js_windows_target_origin(JsCtx *source, JsCtx *target, const std::string &text, bool *valid) {
    Message check;
    bool parsed = source && wanted_origin(source, text, check);
    if (valid) *valid = parsed;
    return parsed && current_context(target) && (check.wildcard || check.wanted == context_principal(target));
}

namespace {
int javascript_hex(unsigned char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
std::string javascript_source(const std::string &url) {
    std::string source; source.reserve(url.size() - 11);
    for (size_t at = 11; at < url.size(); ++at) {
        if (url[at] == '%' && at + 2 < url.size()) {
            const int high = javascript_hex((unsigned char)url[at + 1]);
            const int low = javascript_hex((unsigned char)url[at + 2]);
            if (high >= 0 && low >= 0) {
                source.push_back((char)((high << 4) | low)); at += 2; continue;
            }
        }
        source.push_back(url[at]);
    }
    /* run_source's explicit-length V8 UTF-8 conversion replaces malformed byte
     * sequences. A decoded NUL remains part of the script, never a terminator. */
    return source;
}
void deliver_javascript_navigations(JsCtx *c) {
    if (!current_context(c)) return;
    const uint64_t cutoff = next_javascript_navigation - 1;
    for (unsigned delivered = 0; delivered < 64; ++delivered) {
        auto next = std::find_if(javascript_navigations.begin(), javascript_navigations.end(),
            [&](const JavascriptNavigation &request) {
                return request.target == c->window_id && request.sequence <= cutoff;
            });
        if (next == javascript_navigations.end()) break;
        JavascriptNavigation request = std::move(*next); javascript_navigations.erase(next);
        javascript_pending_bytes -= request.bytes;
        Window *target = live_window(request.target);
        if (target && target->javascript_count) --target->javascript_count;
        if (!current_context(c) || !target || request.epoch != c->window_epoch ||
            !(request.initiator == target->principal) || c->sb_noscript) continue;
        /* Navigation checks the request's cloned source policy container.
         * unsafe-eval cannot authorize it, and an anchor nonce is not a script
         * nonce. A destination or later policy cannot replace this snapshot. */
        if (!csp_allows_navigation(request.csp.c_str(), request.url.data(), request.url.size())) continue;
        std::string source = javascript_source(request.url);
        std::string().swap(request.url); std::string().swap(request.csp);
        const std::string base = js_windows_base_url(c, true);
        js_run_script(c, nullptr, source.data(), source.size(), base.c_str());
        /* Callback-driven close/navigation invalidates the current epoch. New
         * requests are beyond cutoff and cannot recurse in this delivery pass. */
        if (!current_context(c)) break;
    }
}
} // namespace

double js_windows_deadline(JsCtx *c) {
    Window *w = c ? live_window(c->window_id) : nullptr;
    return w && w->current == c && (w->pending_count || w->javascript_count || w->form_source_count) ? now_ms() : INFINITY;
}
void js_windows_deliver(JsCtx *c) {
    if (!current_context(c)) return;
    deliver_javascript_navigations(c);
    if (!current_context(c)) return;
    deliver_form_navigations(c);
    if (!current_context(c)) return;
    Window *w = live_window(c->window_id); if (!w || !w->pending_count) return;
    /* The sequence cutoff prevents a handler's new messages from recursively
     * draining in this pump. Unprocessed messages remain globally owned and
     * counted, so callback-driven close/commit can purge them immediately. */
    const uint64_t cutoff = next_message - 1;
    for (unsigned delivered = 0; delivered < 64; ++delivered) {
        auto next = std::find_if(messages.begin(), messages.end(), [&](const Message &m) {
            return m.target == c->window_id && m.sequence <= cutoff;
        });
        if (next == messages.end()) break;
        Message message = std::move(*next); messages.erase(next);
        pending_bytes -= message.bytes;
        if (Window *target = find_window(message.target)) if (target->pending_count) --target->pending_count;
        w = live_window(c->window_id);
        if (!current_context(c) || !w || message.epoch != c->window_epoch ||
            (!message.wildcard && !(message.wanted == w->principal))) continue;
        v8::Isolate *iso = c->iso; auto ctx = c->ctx.Get(iso);
        v8::TryCatch caught(iso);
        v8::Local<v8::Value> data, function;
        bool decoded;
        {
            v8::ValueDeserializer deserializer(iso, message.data.get(), message.data_size);
            for (uint32_t i = 0; i < message.buffers.size(); ++i)
                deserializer.TransferArrayBuffer(i, v8::ArrayBuffer::New(iso, message.buffers[i]));
            decoded = deserializer.ReadHeader(ctx).FromMaybe(false) && deserializer.ReadValue(ctx).ToLocal(&data);
        }
        if (!decoded) { caught.Reset(); data = v8::Null(iso); }
        /* Deserialization has finished reading the wire bytes. The receiver
         * now owns its ArrayBuffers; no uncounted native payload spans JS. */
        message.data.reset(); message.buffers.clear();
        if (!c->api.IsEmpty() && c->api.Get(iso)->Get(ctx, jstr(iso, "receiveWindowMessage")).ToLocal(&function) && function->IsFunction()) {
            v8::Local<v8::Value> args[] = {data, jstr(iso, message.origin.c_str()), js_window_value(c, message.source), v8::Boolean::New(iso, !decoded)};
            (void)jcall(c, function.As<v8::Function>(), v8::Undefined(iso), 4, args);
        }
    }
}
void js_install_windows(JsCtx *c, v8::Local<v8::Object> native) {
    v8::Isolate *iso = c->iso; auto ctx = iso->GetCurrentContext();
#define REG(name) (void)native->Set(ctx, jstr(iso, #name), v8::Function::New(ctx, n_##name).ToLocalChecked())
    REG(winId); REG(winOpen); REG(winOp); REG(winPost);
    REG(formCanNavigate); REG(formAction); REG(formNavigate); REG(formEncoding); REG(formEncode); REG(formUtf8Length);
#undef REG
}
