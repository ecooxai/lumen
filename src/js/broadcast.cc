/* BroadcastChannel: process-local transport; V8 handles never cross threads.
   Every destination gets its own deserialize task on its owning event loop. */
#include "broadcast.h"
#include "js_int.h"
#include "clone.h"
#include "v8_tasks.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <vector>
#ifdef LUMEN_HAS_PSL
#include <libpsl.h>
#endif

struct BroadcastHandle;
struct JsBroadcastContext : std::enable_shared_from_this<JsBroadcastContext> {
    v8::Isolate *isolate = nullptr;
    v8::Global<v8::Context> realm;
    v8::Global<v8::Function> dispatch;
    JsBroadcastIdentity identity;
    std::shared_ptr<JsForegroundTasks> tasks;
    std::function<bool()> eligible;
    std::atomic<bool> alive{true};
    /* Accessed only on the realm's owning thread. */
    std::map<uint64_t, std::unique_ptr<BroadcastHandle>> handles;
};
struct BroadcastHandle {
    uint64_t id = 0;
    std::weak_ptr<JsBroadcastContext> owner;
    v8::Global<v8::Object> receiver;
    std::u16string name;
    bool retained = false;
};
namespace {
#ifdef LUMEN_HAS_PSL
const char kPublicSuffixList[] =
#include "public_suffix_list.inc"
;
struct PublicSuffixList {
    psl_ctx_t *context = nullptr;
    PublicSuffixList() {
        /* Read the pinned resource embedded in the binary. No runtime network
           request or build-directory-relative file can alter storage sites. */
#if defined(__linux__)
        FILE *input = fmemopen(const_cast<char *>(kPublicSuffixList), sizeof(kPublicSuffixList) - 1, "r");
#else
        FILE *input = std::tmpfile();
        if (input && std::fwrite(kPublicSuffixList, 1, sizeof(kPublicSuffixList) - 1, input) != sizeof(kPublicSuffixList) - 1) {
            std::fclose(input); input = nullptr;
        }
        if (input) std::rewind(input);
#endif
        if (input) { context = psl_load_fp(input); std::fclose(input); }
    }
    ~PublicSuffixList() { if (context) psl_free(context); }
};
const psl_ctx_t *suffix_list() { static const PublicSuffixList list; return list.context; }
#endif
struct Registration {
    std::weak_ptr<JsBroadcastContext> owner;
    std::string key;
    std::u16string name;
};
std::mutex registry_mutex;
std::map<uint64_t, Registration> registry; // IDs also impose creation order.
std::atomic<uint64_t> next_id{1}, next_origin{1};
using FCI = v8::FunctionCallbackInfo<v8::Value>;

std::u16string channel_name(v8::Isolate *iso, v8::Local<v8::Value> value) {
    v8::String::Value text(iso, value);
    std::u16string result;
    if (*text) { result.reserve(text.length()); for (int i = 0; i < text.length(); ++i) result.push_back(static_cast<char16_t>((*text)[i])); }
    return result;
}
v8::Local<v8::Private> state_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:broadcast-state"));
}
JsBroadcastContext *context(const FCI &a) {
    return static_cast<JsBroadcastContext *>(a.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
}
uint64_t channel_id(const FCI &a) {
    return (uint64_t)a[0]->IntegerValue(a.GetIsolate()->GetCurrentContext()).FromMaybe(0);
}
/* Registry identities remain native. A borrowed method can address a receiver
   in another realm only when both realms belong to this V8 isolate/thread. */
std::shared_ptr<JsBroadcastContext> channel_owner(uint64_t id, v8::Isolate *iso) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    auto entry = registry.find(id);
    if (entry == registry.end()) return {};
    auto owner = entry->second.owner.lock();
    return owner && owner->isolate == iso ? owner : nullptr;
}
bool eligible(JsBroadcastContext *c) {
    return c && c->alive.load() && c->eligible && c->eligible();
}
void close_channel(JsBroadcastContext *c, uint64_t id) {
    auto it = c->handles.find(id);
    if (it == c->handles.end()) return;
    { std::lock_guard<std::mutex> lock(registry_mutex); registry.erase(id); }
    c->handles.erase(it);
}
void collected(const v8::WeakCallbackInfo<BroadcastHandle> &info) {
    BroadcastHandle *handle = info.GetParameter();
    auto owner = handle->owner.lock();
    const uint64_t id = handle->id;
    handle->receiver.Reset();
    if (owner) close_channel(owner.get(), id);
}
void make_weak(BroadcastHandle *handle) {
    handle->receiver.SetWeak(handle, collected, v8::WeakCallbackType::kParameter);
    handle->retained = false;
}

class Delivery final : public v8::Task {
    std::weak_ptr<JsBroadcastContext> destination_;
    uint64_t channel_;
    std::shared_ptr<const JsSerializedValue> message_;
    std::string origin_, cluster_;
public:
    Delivery(std::weak_ptr<JsBroadcastContext> destination, uint64_t channel,
             std::shared_ptr<const JsSerializedValue> message,
             std::string origin, std::string cluster)
        : destination_(std::move(destination)), channel_(channel), message_(std::move(message)),
          origin_(std::move(origin)), cluster_(std::move(cluster)) {}
    void Run() override {
        auto c = destination_.lock();
        if (!c || !eligible(c.get()) || c->realm.IsEmpty() || c->dispatch.IsEmpty()) return;
        auto it = c->handles.find(channel_);
        if (it == c->handles.end() || it->second->receiver.IsEmpty()) return;
        v8::Isolate *iso = c->isolate;
        v8::HandleScope handles(iso);
        auto realm = c->realm.Get(iso);
        v8::Context::Scope scope(realm);
        auto receiver = it->second->receiver.Get(iso);
        v8::Local<v8::Value> data = v8::Null(iso);
        bool error = !message_->modules.empty() && cluster_ != c->identity.cluster;
        if (!error) {
            v8::TryCatch caught(iso);
            if (!js_deserialize(iso, realm, *message_).ToLocal(&data)) { error = true; data = v8::Null(iso); }
        }
        /* Deserialization may invoke native host reconstruction. Recheck close
           before dispatching; never retain an iterator across script execution. */
        if (!eligible(c.get()) || !c->handles.count(channel_)) return;
        v8::Local<v8::Value> args[] = { receiver, data, jstr(iso, origin_.c_str()), v8::Boolean::New(iso, error) };
        (void)c->dispatch.Get(iso)->Call(realm, v8::Undefined(iso), 4, args);
    }
};
void init(const FCI &a) {
    auto c = context(a);
    if (a[0]->IsFunction() && c->alive.load()) c->dispatch.Reset(a.GetIsolate(), a[0].As<v8::Function>());
}
void is_eligible(const FCI &a) { a.GetReturnValue().Set(eligible(context(a))); }
void state(const FCI &a) {
    auto iso = a.GetIsolate(); if (!a[0]->IsObject()) return;
    v8::Local<v8::Value> value;
    if (a[0].As<v8::Object>()->GetPrivate(iso->GetCurrentContext(), state_key(iso)).ToLocal(&value))
        a.GetReturnValue().Set(value);
}
void open(const FCI &a) {
    auto c = context(a); auto iso = a.GetIsolate();
    if (!a[1]->IsObject() || !a[2]->IsObject()) return;
    /* Private state supplies an unforgeable brand across same-isolate realms,
       including cross-realm newTarget constructors and inactive documents. */
    if (!a[1].As<v8::Object>()->SetPrivate(iso->GetCurrentContext(), state_key(iso), a[2]).FromMaybe(false)) return;
    /* Detached globals can still construct inert channels through a retained
       constructor, but they cannot send or receive messages. */
    if (!eligible(c)) { a.GetReturnValue().Set(0); return; }
    auto handle = std::make_unique<BroadcastHandle>();
    handle->id = next_id.fetch_add(1);
    if (handle->id >= (uint64_t{1} << 53)) { iso->ThrowException(v8::Exception::RangeError(jstr(iso, "BroadcastChannel identifier limit reached"))); return; }
    handle->owner = c->shared_from_this();
    handle->name = channel_name(iso, a[0]);
    handle->receiver.Reset(iso, a[1].As<v8::Object>());
    const uint64_t id = handle->id;
    { std::lock_guard<std::mutex> lock(registry_mutex); registry.emplace(id, Registration{c->shared_from_this(), c->identity.storage_key(), handle->name}); }
    make_weak(handle.get());
    c->handles.emplace(id, std::move(handle));
    a.GetReturnValue().Set((double)id);
}
void retain(const FCI &a) {
    auto c = channel_owner(channel_id(a), a.GetIsolate()); if (!c) return;
    auto it = c->handles.find(channel_id(a));
    if (it == c->handles.end()) return;
    auto handle = it->second.get(); const bool strong = a[1]->IsTrue();
    if (strong && !handle->retained) { handle->receiver.ClearWeak(); handle->retained = true; }
    else if (!strong && handle->retained) make_weak(handle);
}
void close(const FCI &a) {
    const uint64_t id = channel_id(a);
    if (auto c = channel_owner(id, a.GetIsolate())) close_channel(c.get(), id);
}
void post(const FCI &a) {
    const uint64_t source_id = channel_id(a);
    auto c = channel_owner(source_id, a.GetIsolate()); if (!c || !eligible(c.get())) return;
    auto found = c->handles.find(source_id); if (found == c->handles.end()) return;
    /* Structured serialization can call arbitrary getters, including close().
       Copy all routing metadata before it and never reuse `found` afterward. */
    const std::u16string name = found->second->name;
    const std::string key = c->identity.storage_key();
    const std::string origin = c->identity.origin, cluster = c->identity.cluster;
    auto message = std::make_shared<JsSerializedValue>();
    if (!js_serialize(a.GetIsolate(), a.GetIsolate()->GetCurrentContext(), a[1], *message)) return;
    std::vector<std::pair<uint64_t, std::shared_ptr<JsBroadcastContext>>> recipients;
    {
        std::lock_guard<std::mutex> lock(registry_mutex);
        for (const auto &entry : registry) {
            if (entry.first == source_id || entry.second.key != key || entry.second.name != name) continue;
            if (auto owner = entry.second.owner.lock(); owner && owner->alive.load()) recipients.emplace_back(entry.first, std::move(owner));
        }
    }
    for (const auto &recipient : recipients) {
        if (recipient.second->tasks)
            recipient.second->tasks->PostTask(std::make_unique<Delivery>(recipient.second, recipient.first, message, origin, cluster));
    }
}
std::string origin_for(const std::string &url) {
    const char *raw = url.c_str();
    if (!url.compare(0, 5, "blob:")) raw += 5;
    URL parsed{}; if (!url_parse(raw, &parsed)) return "null";
    char *value = url_origin_web(&parsed);
    std::string origin = value && *value ? value : "null";
    free(value); url_free(&parsed); return origin;
}
bool sandbox_allows_origin(std::string tokens) {
    std::transform(tokens.begin(), tokens.end(), tokens.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    size_t p = 0;
    while (p < tokens.size()) {
        while (p < tokens.size() && std::isspace((unsigned char)tokens[p])) ++p;
        size_t end = p; while (end < tokens.size() && !std::isspace((unsigned char)tokens[end])) ++end;
        if (tokens.substr(p, end - p) == "allow-same-origin") return true;
        p = end;
    }
    return false;
}
bool frame_origin_sandboxed(Node *frame) {
    return frame && node_has_attr(frame, "sandbox") && !sandbox_allows_origin(node_attr(frame, "sandbox") ? node_attr(frame, "sandbox") : "");
}
bool policy_origin_sandboxed(const char *header) {
    if (!header) return false;
    std::string policy(header);
    std::transform(policy.begin(), policy.end(), policy.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    size_t start = 0;
    while (start < policy.size()) {
        size_t end = policy.find_first_of(";,", start); if (end == std::string::npos) end = policy.size();
        size_t at = start; while (at < end && std::isspace((unsigned char)policy[at])) ++at;
        if (end - at >= 7 && !policy.compare(at, 7, "sandbox") &&
            (at + 7 == end || std::isspace((unsigned char)policy[at + 7])) &&
            !sandbox_allows_origin(policy.substr(at + 7, end - at - 7))) return true;
        start = end + 1;
    }
    return false;
}
std::string opaque_key() { return "opaque-broadcast-origin:" + std::to_string(next_origin.fetch_add(1)); }
} // namespace

bool js_broadcast_has_psl() {
#ifdef LUMEN_HAS_PSL
    return suffix_list() != nullptr;
#else
    return false;
#endif
}
std::string js_broadcast_site_key(const std::string &origin, const std::string &opaque_key) {
    if (origin == "null") return opaque_key;
    URL parsed{}; if (!url_parse(origin.c_str(), &parsed)) return opaque_key;
    /* URL parsing supplies canonical lower-case/IDNA hosts and canonical IPs.
       PSL extraction includes PRIVATE rules as well as ICANN rules. IPv4/IPv6
       addresses are hosts, never suffix-list domains. Preserve trailing dots. */
    const std::string host = parsed.host ? parsed.host : "";
    std::string site = host;
#ifdef LUMEN_HAS_PSL
    const bool ip = host.find(':') != std::string::npos ||
        (!host.empty() && std::all_of(host.begin(), host.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || c == '.'; }));
    if (!ip) if (const psl_ctx_t *psl = suffix_list())
        if (const char *registrable = psl_registrable_domain(psl, host.c_str())) site = registrable;
#endif
    /* Without a PSL facility, partition by the entire host. This deliberately
       over-isolates subdomains instead of merging unrelated registry tenants. */
    std::string result = std::string(parsed.scheme ? parsed.scheme : "") + "://" + site;
    url_free(&parsed); return result;
}

std::string JsBroadcastIdentity::storage_key() const {
    return top_site + '\0' + origin_key + '\0' + (cross_site_ancestor ? "cross-site" : "same-site");
}
JsBroadcastIdentity js_broadcast_identity(JsCtx *c) {
    if (c->broadcast) return c->broadcast->identity;
    JsBroadcastIdentity parent, result;
    if (c->parent) parent = js_broadcast_identity(c->parent);
    const std::string url = c->doc && c->doc->url ? c->doc->url : "about:blank";
    const bool inherit = c->parent && (!url.compare(0, 6, "about:"));
    /* Capture the active sandbox flags at realm creation. Removing an iframe
       attribute later does not relax this document or any descendant. */
    const bool sandboxed = (c->parent && parent.origin_sandboxed) || frame_origin_sandboxed(c->frame_el) ||
                           policy_origin_sandboxed(c->doc ? c->doc->csp : nullptr);
    if (!sandboxed && inherit) result = parent;
    else {
        result.origin = sandboxed ? "null" : origin_for(url);
        result.origin_key = result.origin == "null" ? opaque_key() : result.origin;
        result.site = js_broadcast_site_key(result.origin, result.origin_key);
        result.top_site = c->parent ? parent.top_site : result.site;
        result.cross_site_ancestor = c->parent && (parent.cross_site_ancestor || parent.site != result.site);
    }
    result.origin_sandboxed = sandboxed;
    JsCtx *root = c; while (root->parent) root = root->parent;
    result.cluster = "window-agent:" + std::to_string(root->id) + ':' + result.origin_key;
    return result;
}
JsBroadcastIdentity js_broadcast_worker_identity(const JsBroadcastIdentity &owner, const std::string &url) {
    JsBroadcastIdentity result = owner;
    /* Blob workers inherit their creator's opaque nonce as well as their origin.
       A data URL worker gets its own opaque origin, even with identical bytes. */
    if (!url.compare(0, 5, "blob:")) return result;
    const std::string parsed_origin = origin_for(url);
    const std::string origin = owner.origin == "null" && parsed_origin != "null" ? "null" : parsed_origin;
    if (origin != owner.origin || origin == "null") {
        result.origin = origin;
        result.origin_key = origin == "null" ? opaque_key() : origin;
        result.site = js_broadcast_site_key(origin, result.origin_key);
        result.cross_site_ancestor = owner.cross_site_ancestor || owner.site != result.site;
        result.cluster = owner.cluster + ':' + result.origin_key;
    }
    return result;
}
std::shared_ptr<JsBroadcastContext> js_broadcast_install(
    v8::Local<v8::Context> realm, v8::Local<v8::Object> bridge, const JsBroadcastIdentity &identity,
    std::shared_ptr<JsForegroundTasks> tasks, std::function<bool()> predicate) {
    auto c = std::make_shared<JsBroadcastContext>();
    auto iso = v8::Isolate::GetCurrent(); c->isolate = iso; c->realm.Reset(iso, realm);
    c->identity = identity; c->tasks = std::move(tasks); c->eligible = std::move(predicate);
    struct Binding { const char *name; v8::FunctionCallback callback; } bindings[] = {
        {"bcInit", init}, {"bcEligible", is_eligible}, {"bcState", state}, {"bcOpen", open},
        {"bcRetain", retain}, {"bcClose", close}, {"bcPost", post}
    };
    auto data = v8::External::New(iso, c.get(), v8::kExternalPointerTypeTagDefault);
    for (const auto &binding : bindings)
        (void)bridge->Set(realm, jstr(iso, binding.name), v8::Function::New(realm, binding.callback, data).ToLocalChecked());
    return c;
}
void js_broadcast_deactivate(const std::shared_ptr<JsBroadcastContext> &c) {
    if (!c) return;
    c->alive = false;
    { std::lock_guard<std::mutex> lock(registry_mutex); for (const auto &entry : c->handles) registry.erase(entry.first); }
    c->handles.clear(); c->dispatch.Reset(); c->eligible = {}; c->realm.Reset();
}
