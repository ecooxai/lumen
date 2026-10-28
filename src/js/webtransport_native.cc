/* Real HTTP/3 WebTransport binding. Rust owns bounded I/O; only the document's
 * V8 thread reads events or calls JavaScript. No worker holds a JsCtx pointer. */
#include "js_int.h"
#include "webtransport-config.h"
#include <cmath>
#include <cstring>
#include <limits>

#if LUMEN_WEBTRANSPORT_NATIVE
#include <lumen_wt.h>

namespace {
constexpr uint64_t kMaxSafeId = 9007199254740991ULL;
constexpr size_t kConfigBytes = 8192, kURLBytes = 4096, kChunkBytes = 65536, kEventJsonBytes = 16384;
constexpr size_t kSessionsPerDocument = 4, kEventsPerTick = 64;
struct State {
    uint64_t owner = 0;
    /* Only the owning V8 thread accesses these handles and callbacks. */
    std::map<uint64_t, v8::Global<v8::Function>> callbacks;
};
std::mutex states_mutex;
std::unordered_map<JsCtx *, std::unique_ptr<State>> states;
std::once_flag initialize_once;
std::atomic<int32_t> initialize_status{LUME_WT_STATE};

void wake_owner() { if (js_wakeup) js_wakeup(); }
void fail(v8::Isolate *iso, const char *name, const char *message) {
    auto context = iso->GetCurrentContext();
    auto error = v8::Exception::Error(jstr(iso, message)).As<v8::Object>();
    (void)error->CreateDataProperty(context, jstr(iso, "name"), jstr(iso, name));
    iso->ThrowException(error);
}
bool native_ok(v8::Isolate *iso, int32_t status) {
    if (status == LUME_WT_OK) return true;
    const char *name = status == LUME_WT_INVALID ? "TypeError" :
        status == LUME_WT_CLOSED || status == LUME_WT_STATE ? "InvalidStateError" :
        status == LUME_WT_QUOTA ? "QuotaExceededError" :
        status == LUME_WT_UNSUPPORTED ? "NotSupportedError" :
        status == LUME_WT_TIMEOUT ? "TimeoutError" : "OperationError";
    fail(iso, name, "The native WebTransport operation could not be accepted");
    return false;
}
State *state(JsCtx *c, bool create = false) {
    std::lock_guard<std::mutex> lock(states_mutex);
    auto it = states.find(c);
    if (it != states.end()) return it->second.get();
    if (!create) return nullptr;
    std::call_once(initialize_once, [] { initialize_status = lume_wt_initialize(wake_owner); });
    if (initialize_status.load() != LUME_WT_OK) return nullptr;
    uint64_t owner = lume_wt_owner_create();
    if (!owner) return nullptr;
    auto owned = std::make_unique<State>(); owned->owner = owner;
    State *result = owned.get(); states.emplace(c, std::move(owned)); return result;
}
std::unique_ptr<State> take_state(JsCtx *c) {
    std::lock_guard<std::mutex> lock(states_mutex);
    auto it = states.find(c); if (it == states.end()) return nullptr;
    auto result = std::move(it->second); states.erase(it); return result;
}
bool integer(v8::Isolate *iso, v8::Local<v8::Value> value, uint64_t max, bool zero, uint64_t &out) {
    if (!value->IsNumber()) return false;
    double n = value.As<v8::Number>()->Value();
    if (!std::isfinite(n) || std::floor(n) != n || n < (zero ? 0 : 1) || n > static_cast<double>(max)) return false;
    out = static_cast<uint64_t>(n); return true;
}
v8::Local<v8::Value> own(v8::Isolate *iso, v8::Local<v8::Object> object, const char *name) {
    auto context = iso->GetCurrentContext(); auto key = jstr(iso, name);
    if (!object->HasOwnProperty(context, key).FromMaybe(false)) return v8::Undefined(iso);
    v8::Local<v8::Value> value;
    return object->Get(context, key).ToLocal(&value) ? value : v8::Local<v8::Value>(v8::Undefined(iso));
}
std::string json_quote(const std::string &text) {
    static const char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch < 0x20) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += static_cast<char>(ch);
    }
    result += '"'; return result;
}
/* Serialize native-owned fields without JSON.stringify/toJSON/prototype hooks.
 * A page must never be able to substitute the HTTP/3 Origin header. */
bool canonical_config(JsCtx *c, v8::Local<v8::Value> value, std::string &result) {
    auto iso = c->iso; auto context = iso->GetCurrentContext();
    if (!js_windows_is_secure(c) || c->dead) {
        fail(iso, "SecurityError", "WebTransport requires a secure document and secure ancestors"); return false;
    }
    if (!value->IsString() || value.As<v8::String>()->Length() > static_cast<int>(kConfigBytes)) {
        fail(iso, "TypeError", "WebTransport configuration must be bounded JSON"); return false;
    }
    std::string raw = jcstr(iso, value);
    if (raw.size() > kConfigBytes) { fail(iso, "TypeError", "WebTransport configuration is too large"); return false; }
    v8::Local<v8::Value> parsed; bool parsed_json;
    {
        v8::TryCatch caught(iso);
        parsed_json = v8::JSON::Parse(context, value.As<v8::String>()).ToLocal(&parsed);
        if (!parsed_json) caught.Reset();
    }
    if (!parsed_json) { fail(iso, "TypeError", "WebTransport configuration is invalid JSON"); return false; }
    if (!parsed->IsObject() || parsed->IsArray() || parsed->IsNull()) {
        fail(iso, "TypeError", "WebTransport configuration must be an object"); return false;
    }
    auto config = parsed.As<v8::Object>(); auto url_value = own(iso, config, "url");
    if (!url_value->IsString()) { fail(iso, "TypeError", "WebTransport requires an HTTPS URL"); return false; }
    std::string input = jcstr(iso, url_value);
    if (input.empty() || input.size() > kURLBytes || input.find('\0') != std::string::npos) {
        fail(iso, "TypeError", "WebTransport URL is invalid or too large"); return false;
    }
    URL url{}; bool parsed_url = url_parse(input.c_str(), &url);
    bool valid = parsed_url && url.scheme && !strcmp(url.scheme, "https") &&
        url.host && *url.host && !url.opaque && !url.userinfo && !url.fragment;
    if (!valid) { url_free(&url); fail(iso, "TypeError", "WebTransport requires HTTPS without credentials or a fragment"); return false; }
    char *serialized = url_to_string(&url); std::string target = serialized ? serialized : "";
    free(serialized); url_free(&url);
    if (target.empty() || target.size() > kURLBytes) { fail(iso, "TypeError", "WebTransport URL could not be serialized within its size bound"); return false; }
    if (c->doc && c->doc->csp && !csp_allows_url(c->doc->csp, c->doc->url, target.c_str(), "connect-src")) {
        fail(iso, "SecurityError", "WebTransport is blocked by the document's connect-src policy"); return false;
    }
    const std::string origin = c->opaque_origin || c->origin.empty() ? "null" : c->origin;
    result = "{\"url\":" + json_quote(target) + ",\"origin\":" + json_quote(origin);
    auto unreliable = own(iso, config, "requireUnreliable");
    if (!unreliable->IsUndefined()) {
        if (!unreliable->IsBoolean()) { fail(iso, "TypeError", "requireUnreliable must be Boolean"); return false; }
        result += unreliable->BooleanValue(iso) ? ",\"requireUnreliable\":true" : ",\"requireUnreliable\":false";
    }
    auto pins = own(iso, config, "certificateHashes");
    if (!pins->IsUndefined()) {
        if (!pins->IsArray() || pins.As<v8::Array>()->Length() == 0 || pins.As<v8::Array>()->Length() > 8) {
            fail(iso, "TypeError", "One to eight SHA-256 certificate hashes are supported"); return false;
        }
        auto list = pins.As<v8::Array>(); result += ",\"certificateHashes\":[";
        for (uint32_t i = 0; i < list->Length(); ++i) {
            v8::Local<v8::Value> pin;
            if (!list->Get(context, i).ToLocal(&pin) || !pin->IsArray() || pin.As<v8::Array>()->Length() != 32) {
                fail(iso, "TypeError", "A certificate hash must contain exactly 32 bytes"); return false;
            }
            if (i) result += ','; result += '[';
            for (uint32_t j = 0; j < 32; ++j) {
                v8::Local<v8::Value> byte; uint64_t n;
                if (!pin.As<v8::Array>()->Get(context, j).ToLocal(&byte) || !integer(iso, byte, 255, true, n)) {
                    fail(iso, "TypeError", "Certificate hash entries must be integer bytes"); return false;
                }
                if (j) result += ','; result += std::to_string(n);
            }
            result += ']';
        }
        result += ']';
    }
    result += '}';
    if (result.size() > kConfigBytes) { fail(iso, "TypeError", "Native WebTransport configuration is too large"); return false; }
    return true;
}
void create(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); JsCtx *c = jctx(iso);
    if (a.Length() != 2 || !a[1]->IsFunction()) { fail(iso, "TypeError", "WebTransport requires an event callback"); return; }
    std::string config; if (!canonical_config(c, a[0], config)) return;
    State *s = state(c, true);
    if (!s) { fail(iso, "OperationError", "The native WebTransport runtime is unavailable"); return; }
    if (s->callbacks.size() >= kSessionsPerDocument) { fail(iso, "QuotaExceededError", "Too many WebTransport sessions in this document"); return; }
    uint64_t session = 0;
    if (!native_ok(iso, lume_wt_connect(s->owner, reinterpret_cast<const uint8_t *>(config.data()), config.size(), &session))) return;
    if (!session || session > kMaxSafeId || s->callbacks.count(session)) {
        /* Invalid native handles are not exposed to page code. */
        (void)lume_wt_command(s->owner, session, 0, LUME_WT_CLOSE, 0, 0, nullptr, 0);
        fail(iso, "OperationError", "The native WebTransport handle was invalid"); return;
    }
    s->callbacks.emplace(session, v8::Global<v8::Function>(iso, a[1].As<v8::Function>()));
    a.GetReturnValue().Set(static_cast<double>(session));
}
void command(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); JsCtx *c = jctx(iso); uint64_t session, stream, op, request, code;
    if (a.Length() < 5 || !integer(iso, a[0], kMaxSafeId, false, session) ||
        !integer(iso, a[1], kMaxSafeId, true, stream) ||
        !integer(iso, a[2], LUME_WT_CANCEL_ACCEPT_UNI, false, op) ||
        !integer(iso, a[3], kMaxSafeId, true, request) ||
        !integer(iso, a[4], UINT32_MAX, true, code) || (op != LUME_WT_CLOSE && !request)) {
        fail(iso, "TypeError", "Invalid native WebTransport command arguments"); return;
    }
    State *s = state(c);
    if (!s || !s->callbacks.count(session)) { fail(iso, "InvalidStateError", "WebTransport is closed or belongs to another document"); return; }
    const uint8_t *data = nullptr; size_t length = 0; std::shared_ptr<v8::BackingStore> backing;
    if (a.Length() > 5 && !a[5]->IsNullOrUndefined()) {
        if (!a[5]->IsArrayBuffer()) { fail(iso, "TypeError", "The native transport payload must be an ArrayBuffer"); return; }
        auto buffer = a[5].As<v8::ArrayBuffer>();
        if (buffer->WasDetached()) { fail(iso, "TypeError", "A detached transport buffer cannot be sent"); return; }
        length = buffer->ByteLength();
        if (length > kChunkBytes || (op == LUME_WT_CLOSE && length > 1024)) {
            fail(iso, "QuotaExceededError", "The native transport payload exceeds its bounded chunk size"); return;
        }
        backing = buffer->GetBackingStore(); data = static_cast<const uint8_t *>(backing->Data());
    }
    if (!native_ok(iso, lume_wt_command(s->owner, session, stream, static_cast<uint32_t>(op), request,
                                      static_cast<uint32_t>(code), data, length))) return;
    a.GetReturnValue().Set(true);
}
void number(v8::Isolate *iso, v8::Local<v8::Object> object, const char *name, uint64_t value) {
    (void)object->CreateDataProperty(iso->GetCurrentContext(), jstr(iso, name), v8::Number::New(iso, static_cast<double>(value)));
}
void resource_counts(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); State *s = state(jctx(iso)); LumeWtCounts counts{};
    if (s && !native_ok(iso, lume_wt_counts(s->owner, &counts))) return;
    auto result = v8::Object::New(iso);
    number(iso, result, "owners", counts.owners); number(iso, result, "sessions", counts.sessions);
    number(iso, result, "streams", counts.streams); number(iso, result, "tasks", counts.tasks);
    number(iso, result, "queuedEventBytes", counts.queued_event_bytes); number(iso, result, "pendingSendBytes", counts.pending_send_bytes);
    a.GetReturnValue().Set(result);
}
void invalid_event(JsCtx *c) {
    auto owned = take_state(c); if (!owned) return;
    auto iso = c->iso; auto context = iso->GetCurrentContext();
    std::vector<std::pair<uint64_t, v8::Local<v8::Function>>> callbacks;
    for (auto &entry : owned->callbacks) callbacks.emplace_back(entry.first, entry.second.Get(iso));
    owned->callbacks.clear(); (void)lume_wt_owner_drop(owned->owner);
    for (auto &entry : callbacks) {
        if (c->dead) break;
        auto event = v8::Object::New(iso); number(iso, event, "sessionId", entry.first);
        (void)event->CreateDataProperty(context, jstr(iso, "type"), jstr(iso, "closed"));
        (void)event->CreateDataProperty(context, jstr(iso, "clean"), v8::False(iso));
        (void)event->CreateDataProperty(context, jstr(iso, "localCloseRequested"), v8::False(iso));
        (void)event->CreateDataProperty(context, jstr(iso, "errorName"), jstr(iso, "OperationError"));
        (void)event->CreateDataProperty(context, jstr(iso, "errorMessage"), jstr(iso, "The native WebTransport event was invalid; this document's transports were closed"));
        v8::Local<v8::Value> value = event;
        (void)jcall(c, entry.second, context->Global(), 1, &value);
    }
}
} // namespace
#endif

static void available(const v8::FunctionCallbackInfo<v8::Value> &a) {
#if LUMEN_WEBTRANSPORT_NATIVE
    JsCtx *c = jctx(a.GetIsolate()); a.GetReturnValue().Set(c && !c->dead && js_windows_is_secure(c));
#else
    a.GetReturnValue().Set(false);
#endif
}
void js_install_webtransport(JsCtx *c, v8::Local<v8::Object> N) {
    auto iso = c->iso; auto context = iso->GetCurrentContext();
    (void)N->CreateDataProperty(context, jstr(iso, "wtNativeAvailable"), v8::Function::New(context, available).ToLocalChecked());
#if LUMEN_WEBTRANSPORT_NATIVE
#define WT_BIND(name, fn) (void)N->CreateDataProperty(context, jstr(iso, name), v8::Function::New(context, fn).ToLocalChecked())
    WT_BIND("wtCreate", create); WT_BIND("wtCommand", command); WT_BIND("wtResourceCounts", resource_counts);
#undef WT_BIND
#endif
}
void js_webtransport_close(JsCtx *c) {
#if LUMEN_WEBTRANSPORT_NATIVE
    auto owned = take_state(c); if (!owned) return;
    owned->callbacks.clear();
    int32_t status = lume_wt_owner_drop(owned->owner);
    if (status == LUME_WT_TIMEOUT) fprintf(stderr, "lumen: WebTransport document cancellation reached its two-second bound; remaining jobs aborted\n");
#else
    (void)c;
#endif
}
double js_webtransport_deadline(JsCtx *c) {
#if LUMEN_WEBTRANSPORT_NATIVE
    State *s = state(c); LumeWtCounts counts{};
    if (s && lume_wt_counts(s->owner, &counts) == LUME_WT_OK && counts.queued_event_bytes) return 0;
#else
    (void)c;
#endif
    return INFINITY;
}
void js_webtransport_deliver(JsCtx *c) {
#if LUMEN_WEBTRANSPORT_NATIVE
    if (!c || c->dead) return;
    auto iso = c->iso; auto context = iso->GetCurrentContext();
    for (size_t delivered = 0; delivered < kEventsPerTick; ++delivered) {
        if (c->dead) return;
        State *s = state(c); if (!s) return;
        std::unique_ptr<LumeWtEvent, decltype(&lume_wt_event_free)> event(lume_wt_poll(s->owner), lume_wt_event_free);
        if (!event) return;
        size_t json_length = 0, data_length = 0;
        const uint8_t *json = lume_wt_event_json(event.get(), &json_length);
        const uint8_t *data = lume_wt_event_bytes(event.get(), &data_length);
        if (!json || !json_length || json_length > kEventJsonBytes || data_length > kChunkBytes || (data_length && !data)) {
            event.reset(); invalid_event(c); return;
        }
        v8::Local<v8::Value> parsed; bool valid;
        {
            v8::TryCatch caught(iso);
            valid = v8::JSON::Parse(context, jstr(iso, reinterpret_cast<const char *>(json), static_cast<int>(json_length))).ToLocal(&parsed);
            if (!valid) caught.Reset();
        }
        if (!valid || !parsed->IsObject() || parsed->IsArray() || parsed->IsNull()) {
            event.reset(); invalid_event(c); return;
        }
        auto object = parsed.As<v8::Object>(); uint64_t session = 0;
        auto type_value = own(iso, object, "type");
        if (!integer(iso, own(iso, object, "sessionId"), kMaxSafeId, false, session) || !type_value->IsString()) {
            event.reset(); invalid_event(c); return;
        }
        auto it = s->callbacks.find(session); if (it == s->callbacks.end()) continue;
        std::string type = jcstr(iso, type_value);
        if (type != "ready" && type != "result" && type != "error" && type != "stats" && type != "closed") {
            event.reset(); invalid_event(c); return;
        }
        uint64_t operation = 0;
        (void)integer(iso, own(iso, object, "operation"), LUME_WT_CANCEL_ACCEPT_UNI, false, operation);
        if (data_length || (type == "result" && (operation == LUME_WT_DATAGRAM_READ || operation == LUME_WT_STREAM_READ))) {
            auto backing = v8::ArrayBuffer::NewBackingStore(iso, data_length);
            if (data_length) memcpy(backing->Data(), data, data_length);
            auto array = v8::ArrayBuffer::New(iso, std::move(backing));
            (void)object->CreateDataProperty(context, jstr(iso, "data"), array);
        }
        auto callback = it->second.Get(iso);
        if (type == "closed") s->callbacks.erase(it);
        event.reset(); /* JS owns copied metadata/payload; release Rust before any reentrant code. */
        v8::Local<v8::Value> argument = object;
        (void)jcall(c, callback, context->Global(), 1, &argument);
    }
#else
    (void)c;
#endif
}
void js_webtransport_resource_counts(uint64_t out[6]) {
    for (int i = 0; i < 6; ++i) out[i] = 0;
#if LUMEN_WEBTRANSPORT_NATIVE
    if (initialize_status.load() != LUME_WT_OK) return;
    LumeWtCounts counts{}; if (lume_wt_counts(0, &counts) != LUME_WT_OK) return;
    out[0] = counts.owners; out[1] = counts.sessions; out[2] = counts.streams;
    out[3] = counts.tasks; out[4] = counts.queued_event_bytes; out[5] = counts.pending_send_bytes;
#endif
}
