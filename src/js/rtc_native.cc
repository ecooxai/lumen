/* Real data-channel transport. Network callbacks only copy bounded events;
 * V8 and document ownership stay on the context's thread. */
#include "js_int.h"
#include "rtc-config.h"
#include <deque>
#include <cmath>
#include <cstring>
#include <limits>

#if LUMEN_RTC_NATIVE
#include <rtc/rtc.h>
#include <atomic>

namespace {
constexpr size_t kQueueBytes = 8 * 1024 * 1024, kQueueEvents = 1024;
constexpr size_t kMessageBytes = 1024 * 1024, kSdpBytes = 1024 * 1024;
constexpr size_t kMaxPeers = 32, kMaxChannels = 128;
enum class Kind { Description, LocalDescription, Operation, Error, State, Candidate,
                  Incoming, Open, Closed, ChannelError, Message, BufferedLow };
struct Event {
    Kind kind; int peer = 0, channel = 0; uint32_t request = 0;
    std::string a, b; std::vector<uint8_t> data; bool binary = false;
    size_t bytes() const { return sizeof(Event) + a.size() + b.size() + data.size(); }
};
struct Channel {
    int id = 0; bool negotiated = false;
    bool reliability_known = true;
    int lifetime = -1, retransmits = -1;
    std::atomic<uint64_t> sent{0}, sent_bytes{0}, received{0}, received_bytes{0};
};
struct State;
struct Peer {
    State *owner = nullptr; int id = 0;
    v8::Global<v8::Function> callback;
    std::atomic<bool> closing{false}, overflow{false};
    std::atomic<int> connection{RTC_NEW}, ice{RTC_ICE_NEW}, gathering{RTC_GATHERING_NEW}, signaling{RTC_SIGNALING_STABLE};
    std::mutex channels_mutex;
    std::map<int, std::unique_ptr<Channel>> channels;
    std::string generated_offer, generated_answer;
};
struct State {
    std::mutex mutex; bool alive = true; size_t bytes = 0;
    std::deque<Event> events;
    /* Only the owning V8 thread accesses this map. */
    std::map<int, std::unique_ptr<Peer>> peers;
};
static std::mutex library_mutex, states_mutex;
static size_t library_peers;
static std::unordered_map<JsCtx *, std::unique_ptr<State>> states;
static const char *connection_name(int s) {
    static const char *v[] = {"new", "connecting", "connected", "disconnected", "failed", "closed"};
    return s >= 0 && s < 6 ? v[s] : "failed";
}
static const char *ice_name(int s) {
    static const char *v[] = {"new", "checking", "connected", "completed", "failed", "disconnected", "closed"};
    return s >= 0 && s < 7 ? v[s] : "failed";
}
static const char *gathering_name(int s) {
    static const char *v[] = {"new", "gathering", "complete"};
    return s >= 0 && s < 3 ? v[s] : "new";
}
static const char *signaling_name(int s) {
    static const char *v[] = {"stable", "have-local-offer", "have-remote-offer", "have-local-pranswer", "have-remote-pranswer"};
    return s >= 0 && s < 5 ? v[s] : "closed";
}
static void wake_owner() { if (js_wakeup) js_wakeup(); }
static void push(Peer *p, Event e) {
    if (!p || p->closing.load()) return;
    State *s = p->owner; e.peer = p->id;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (!s->alive || p->closing.load()) return;
        size_t bytes = e.bytes();
        if (s->events.size() >= kQueueEvents || bytes > kQueueBytes - s->bytes) p->overflow = true;
        else { s->bytes += bytes; s->events.push_back(std::move(e)); }
    }
    wake_owner();
}
static std::string string_result(int id, int (*fn)(int, char *, int)) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        int n = fn(id, nullptr, 0);
        if (n < 0 || static_cast<size_t>(n) > kSdpBytes) return {};
        std::vector<char> b(static_cast<size_t>(n) + 1);
        int r = fn(id, b.data(), static_cast<int>(b.size()));
        if (r >= 0) return std::string(b.data());
        if (r != RTC_ERR_TOO_SMALL) return {};
    }
    return {};
}
static void description_cb(int, const char *sdp, const char *type, void *ptr) {
    Peer *p = static_cast<Peer *>(ptr);
    if (!sdp || !type) return;
    if (strlen(sdp) > kSdpBytes) { p->overflow = true; wake_owner(); return; }
    Event e{Kind::LocalDescription}; e.a = sdp; e.b = type; push(p, std::move(e));
}
static void candidate_cb(int, const char *candidate, const char *mid, void *ptr) {
    if (!candidate || strlen(candidate) > 8192 || (mid && strlen(mid) > 1024)) return;
    Event e{Kind::Candidate}; e.a = candidate; if (mid) e.b = mid;
    push(static_cast<Peer *>(ptr), std::move(e));
}
static void connection_cb(int, rtcState value, void *ptr) {
    Peer *p = static_cast<Peer *>(ptr); p->connection = value;
    Event e{Kind::State}; e.a = "connectionState"; e.b = connection_name(value); push(p, std::move(e));
}
static void ice_cb(int, rtcIceState value, void *ptr) {
    Peer *p = static_cast<Peer *>(ptr); p->ice = value;
    Event e{Kind::State}; e.a = "iceConnectionState"; e.b = ice_name(value); push(p, std::move(e));
}
static void gathering_cb(int, rtcGatheringState value, void *ptr) {
    Peer *p = static_cast<Peer *>(ptr); p->gathering = value;
    Event e{Kind::State}; e.a = "iceGatheringState"; e.b = gathering_name(value); push(p, std::move(e));
    if (value == RTC_GATHERING_COMPLETE) { Event end{Kind::Candidate}; push(p, std::move(end)); }
}
static void signaling_cb(int, rtcSignalingState value, void *ptr) {
    Peer *p = static_cast<Peer *>(ptr); p->signaling = value;
    Event e{Kind::State}; e.a = "signalingState"; e.b = signaling_name(value); push(p, std::move(e));
}
static void channel_open(int id, void *ptr) { Event e{Kind::Open}; e.channel = id; push(static_cast<Peer *>(ptr), std::move(e)); }
static void channel_closed(int id, void *ptr) { Event e{Kind::Closed}; e.channel = id; push(static_cast<Peer *>(ptr), std::move(e)); }
static void channel_low(int id, void *ptr) { Event e{Kind::BufferedLow}; e.channel = id; push(static_cast<Peer *>(ptr), std::move(e)); }
static void channel_error(int id, const char *, void *ptr) {
    Event e{Kind::ChannelError}; e.channel = id; e.a = "Native data channel transport failed"; push(static_cast<Peer *>(ptr), std::move(e));
}
static void channel_message(int id, const char *data, int size, void *ptr) {
    Peer *p = static_cast<Peer *>(ptr); if (!p || p->closing.load()) return;
    /* libdatachannel's C callback encodes UTF-8 byte length as -(n + 1).
       Preserve embedded NULs received from independent browsers. */
    size_t n = size < 0 ? static_cast<size_t>(-static_cast<int64_t>(size) - 1) : static_cast<size_t>(size);
    if (n > kMessageBytes || (n && !data)) { p->overflow = true; wake_owner(); return; }
    {
        std::lock_guard<std::mutex> lock(p->channels_mutex);
        auto it = p->channels.find(id); if (it == p->channels.end()) return;
        ++it->second->received; it->second->received_bytes += n;
    }
    Event e{Kind::Message}; e.channel = id; e.binary = size >= 0;
    if (n) e.data.assign(reinterpret_cast<const uint8_t *>(data), reinterpret_cast<const uint8_t *>(data) + n);
    push(p, std::move(e));
}
static void register_channel(Peer *p, int id) {
    rtcSetUserPointer(id, p);
    rtcSetOpenCallback(id, channel_open); rtcSetClosedCallback(id, channel_closed);
    rtcSetErrorCallback(id, channel_error); rtcSetMessageCallback(id, channel_message);
    rtcSetBufferedAmountLowCallback(id, channel_low);
}
static void incoming_cb(int, int id, void *ptr) {
    Peer *p = static_cast<Peer *>(ptr);
    if (!p || p->closing.load()) { rtcDeleteDataChannel(id); return; }
    bool accepted;
    {
        std::lock_guard<std::mutex> lock(p->channels_mutex);
        accepted = p->channels.size() < kMaxChannels && !p->closing.load();
        if (accepted) {
            auto c = std::make_unique<Channel>(); c->id = id;
            rtcReliability reliability{};
            if (rtcGetDataChannelReliability(id, &reliability) < 0) c->reliability_known = false;
            else if (reliability.unreliable) {
                if (reliability.maxPacketLifeTime) c->lifetime = static_cast<int>(reliability.maxPacketLifeTime);
                else if (reliability.maxRetransmits) c->retransmits = static_cast<int>(reliability.maxRetransmits);
                else c->reliability_known = false; /* C API cannot distinguish the two zero modes. */
            }
            p->channels.emplace(id, std::move(c));
        }
    }
    if (!accepted) { rtcDeleteDataChannel(id); p->overflow = true; wake_owner(); return; }
    /* Publish the wrapper event before any channel callbacks can be delivered. */
    Event e{Kind::Incoming}; e.channel = id; push(p, std::move(e));
    register_channel(p, id);
}
static State *state(JsCtx *c, bool create = false) {
    std::lock_guard<std::mutex> lock(states_mutex);
    auto it = states.find(c); if (it != states.end()) return it->second.get();
    if (!create) return nullptr;
    auto p = std::make_unique<State>(); State *out = p.get(); states.emplace(c, std::move(p)); return out;
}
static void close_peer(State *s, int id) {
    auto it = s->peers.find(id); if (it == s->peers.end()) return;
    Peer *p = it->second.get(); p->closing = true;
    std::lock_guard<std::mutex> library_lock(library_mutex);
    rtcDeletePeerConnection(id); /* Joins peer callbacks before examining incoming channels. */
    std::vector<int> channels;
    { std::lock_guard<std::mutex> lock(p->channels_mutex); for (auto &entry : p->channels) channels.push_back(entry.first); }
    for (int channel : channels) rtcDeleteDataChannel(channel); /* Joins each channel callback. */
    p->callback.Reset(); s->peers.erase(it);
    if (library_peers && --library_peers == 0) rtcCleanup();
}
static void fail(v8::Isolate *iso, const char *name, const char *message) {
    auto ctx = iso->GetCurrentContext(); auto error = v8::Exception::Error(jstr(iso, message)).As<v8::Object>();
    (void)error->Set(ctx, jstr(iso, "name"), jstr(iso, name)); iso->ThrowException(error);
}
static bool native_ok(v8::Isolate *iso, int code, const char *operation) {
    if (code >= 0) return true;
    fail(iso, code == RTC_ERR_INVALID ? "OperationError" : code == RTC_ERR_NOT_AVAIL ? "InvalidStateError" : "OperationError", operation); return false;
}
static Peer *peer(v8::Isolate *iso, int id) {
    State *s = state(jctx(iso));
    if (s) { auto it = s->peers.find(id); if (it != s->peers.end() && !it->second->closing.load()) return it->second.get(); }
    fail(iso, "InvalidStateError", "RTC peer is closed or belongs to another document"); return nullptr;
}
static Peer *channel_peer(v8::Isolate *iso, int id) {
    State *s = state(jctx(iso));
    if (s) for (auto &entry : s->peers) {
        Peer *p = entry.second.get(); std::lock_guard<std::mutex> lock(p->channels_mutex);
        if (!p->closing.load() && p->channels.count(id)) return p;
    }
    fail(iso, "InvalidStateError", "RTC channel is closed or belongs to another document"); return nullptr;
}
static v8::Local<v8::Value> field(v8::Isolate *iso, v8::Local<v8::Object> o, const char *key) {
    v8::Local<v8::Value> v; return o->Get(iso->GetCurrentContext(), jstr(iso, key)).ToLocal(&v) ? v : v8::Local<v8::Value>(v8::Undefined(iso));
}
static void set(v8::Isolate *iso, v8::Local<v8::Object> o, const char *k, v8::Local<v8::Value> value) { (void)o->Set(iso->GetCurrentContext(), jstr(iso, k), value); }
static void str(v8::Isolate *iso, v8::Local<v8::Object> o, const char *k, const std::string &v) { set(iso, o, k, jstr(iso, v.c_str(), static_cast<int>(v.size()))); }
static void num(v8::Isolate *iso, v8::Local<v8::Object> o, const char *k, double v) { set(iso, o, k, v8::Number::New(iso, v)); }
static v8::Local<v8::Value> description(v8::Isolate *iso, int id) {
    std::string sdp = string_result(id, rtcGetLocalDescription), type = string_result(id, rtcGetLocalDescriptionType);
    if (sdp.empty() || type.empty()) return v8::Null(iso);
    auto o = v8::Object::New(iso); str(iso, o, "type", type); str(iso, o, "sdp", sdp); return o;
}
static bool json_object(v8::Isolate *iso, v8::Local<v8::Value> value, v8::Local<v8::Object> &out) {
    v8::Local<v8::Value> parsed;
    if (value->IsString() && value.As<v8::String>()->Length() > 262144) { fail(iso, "TypeError", "RTC configuration exceeds the supported limit"); return false; }
    if (!value->IsString() || !v8::JSON::Parse(iso->GetCurrentContext(), value.As<v8::String>()).ToLocal(&parsed) || !parsed->IsObject()) {
        fail(iso, "TypeError", "RTC configuration must be a JSON object"); return false;
    }
    out = parsed.As<v8::Object>(); return true;
}
static std::string encode_userinfo(const std::string &value) {
    const char *hex = "0123456789ABCDEF"; std::string out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
static void create(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext(); State *s = state(jctx(iso), true);
    if (s->peers.size() >= kMaxPeers) { fail(iso, "OperationError", "This document has too many RTC peers"); return; }
    if (a.Length() < 2 || !a[1]->IsFunction()) { fail(iso, "TypeError", "RTC event callback is required"); return; }
    v8::Local<v8::Object> cfg; if (!json_object(iso, a[0], cfg)) return;
    std::vector<std::string> servers;
    auto ice_servers = field(iso, cfg, "iceServers");
    if (ice_servers->IsArray()) {
        auto list = ice_servers.As<v8::Array>(); if (list->Length() > 16) { fail(iso, "NotSupportedError", "At most 16 ICE servers are supported"); return; }
        for (uint32_t i = 0; i < list->Length(); ++i) {
            v8::Local<v8::Value> v; if (!list->Get(ctx, i).ToLocal(&v) || !v->IsObject()) continue;
            auto o = v.As<v8::Object>(); auto urls = field(iso, o, "urls"); std::vector<std::string> entries;
            if (urls->IsString()) entries.push_back(jcstr(iso, urls));
            else if (urls->IsArray()) { auto ar = urls.As<v8::Array>(); if (ar->Length() > 16) { fail(iso, "NotSupportedError", "Too many ICE URLs"); return; } for (uint32_t n = 0; n < ar->Length(); ++n) { v8::Local<v8::Value> u; if (ar->Get(ctx, n).ToLocal(&u) && u->IsString()) entries.push_back(jcstr(iso, u)); } }
            auto user = field(iso, o, "username"), password = field(iso, o, "credential");
            for (auto url : entries) {
                auto colon = url.find(':');
                if (url.compare(0, 6, "stuns:") == 0) { fail(iso, "NotSupportedError", "TLS STUN is not supported by this ICE backend"); return; }
                if (url.size() > 8192 || url.find('\0') != std::string::npos || colon == std::string::npos || (url.substr(0, colon) != "stun" && url.substr(0, colon) != "turn" && url.substr(0, colon) != "turns")) { fail(iso, "SyntaxError", "Invalid ICE server URL"); return; }
                if (url.compare(0, 4, "turn") == 0 && user->IsString() && password->IsString()) {
                    size_t at = colon + 1; if (url.compare(at, 2, "//") == 0) at += 2;
                    url.insert(at, encode_userinfo(jcstr(iso, user)) + ':' + encode_userinfo(jcstr(iso, password)) + '@');
                }
                if (url.size() > 8192) { fail(iso, "NotSupportedError", "ICE server URL including credentials is too long"); return; }
                servers.push_back(std::move(url));
                if (servers.size() > 32) { fail(iso, "NotSupportedError", "Too many ICE URLs"); return; }
            }
        }
    }
    std::vector<const char *> server_ptrs; for (const auto &v : servers) server_ptrs.push_back(v.c_str());
    rtcConfiguration conf{}; conf.iceServers = server_ptrs.data(); conf.iceServersCount = static_cast<int>(server_ptrs.size());
    conf.disableAutoNegotiation = true; conf.maxMessageSize = static_cast<int>(kMessageBytes);
    if (jcstr(iso, field(iso, cfg, "iceTransportPolicy")) == "relay") conf.iceTransportPolicy = RTC_TRANSPORT_POLICY_RELAY;
    std::lock_guard<std::mutex> library_lock(library_mutex);
    if (!library_peers) { rtcInitLogger(RTC_LOG_NONE, nullptr); rtcSetThreadPoolSize(2); }
    int id = rtcCreatePeerConnection(&conf);
    if (!native_ok(iso, id, "Could not create native RTC transport")) { if (!library_peers) rtcCleanup(); return; }
    auto owned = std::make_unique<Peer>(); Peer *p = owned.get(); p->owner = s; p->id = id; p->callback.Reset(iso, a[1].As<v8::Function>());
    s->peers.emplace(id, std::move(owned)); ++library_peers;
    rtcSetUserPointer(id, p); rtcSetLocalDescriptionCallback(id, description_cb); rtcSetLocalCandidateCallback(id, candidate_cb);
    rtcSetStateChangeCallback(id, connection_cb); rtcSetIceStateChangeCallback(id, ice_cb); rtcSetGatheringStateChangeCallback(id, gathering_cb);
    rtcSetSignalingStateChangeCallback(id, signaling_cb); rtcSetDataChannelCallback(id, incoming_cb);
    a.GetReturnValue().Set(id);
}
static uint32_t request_id(const v8::FunctionCallbackInfo<v8::Value> &a, int index) { return a.Length() > index ? a[index]->Uint32Value(a.GetIsolate()->GetCurrentContext()).FromMaybe(0) : 0; }
static void operation(Peer *p, uint32_t id) { Event e{Kind::Operation}; e.request = id; push(p, std::move(e)); }
static void generate(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); Peer *p = peer(iso, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); if (!p) return;
    std::string type = jcstr(iso, a[1]); if (type != "offer" && type != "answer") { fail(iso, "TypeError", "RTC description must be offer or answer"); return; }
    std::string sdp = string_result(p->id, type == "offer" ? rtcCreateOffer : rtcCreateAnswer);
    if (sdp.empty()) { fail(iso, "OperationError", "Native RTC could not generate a description"); return; }
    (type == "offer" ? p->generated_offer : p->generated_answer) = sdp;
    Event e{Kind::Description}; e.request = request_id(a, 2); e.a = std::move(sdp); e.b = type; push(p, std::move(e));
}
static void set_local(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); Peer *p = peer(iso, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); if (!p) return;
    std::string type = jcstr(iso, a[1]), sdp = a[2]->IsNullOrUndefined() ? "" : jcstr(iso, a[2]);
    if (type != "offer" && type != "answer") { fail(iso, "NotSupportedError", "This RTC backend supports offer and answer descriptions"); return; }
    if (!sdp.empty() && sdp != (type == "offer" ? p->generated_offer : p->generated_answer)) { fail(iso, "NotSupportedError", "SDP munging is not supported by this native RTC backend"); return; }
    if (native_ok(iso, rtcSetLocalDescription(p->id, type.c_str()), "Native RTC rejected the local description")) operation(p, request_id(a, 3));
}
static void set_remote(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); Peer *p = peer(iso, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); if (!p) return;
    std::string type = jcstr(iso, a[1]), sdp = jcstr(iso, a[2]);
    if ((type != "offer" && type != "answer") || sdp.size() > kSdpBytes || sdp.find('\0') != std::string::npos) { fail(iso, "NotSupportedError", "Unsupported or oversized remote RTC description"); return; }
    if (sdp.find("m=audio ") != std::string::npos || sdp.find("m=video ") != std::string::npos) { fail(iso, "NotSupportedError", "Native RTC media RTP transport is not implemented"); return; }
    if (native_ok(iso, rtcSetRemoteDescription(p->id, sdp.c_str(), type.c_str()), "Native RTC rejected the remote description")) operation(p, request_id(a, 3));
}
static void add_candidate(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); Peer *p = peer(iso, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); if (!p) return;
    std::string candidate = a[1]->IsNullOrUndefined() ? "" : jcstr(iso, a[1]);
    std::string mid = a[2]->IsNullOrUndefined() ? "" : jcstr(iso, a[2]);
    if (candidate.size() > 8192 || mid.size() > 1024 || candidate.find('\0') != std::string::npos || mid.find('\0') != std::string::npos) { fail(iso, "OperationError", "Invalid RTC candidate"); return; }
    if (candidate.empty() || native_ok(iso, rtcAddRemoteCandidate(p->id, candidate.c_str(), mid.empty() ? nullptr : mid.c_str()), "Native RTC rejected the remote candidate")) operation(p, request_id(a, 3));
}
static void get_local(const v8::FunctionCallbackInfo<v8::Value> &a) { auto iso = a.GetIsolate(); Peer *p = peer(iso, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); if (p) a.GetReturnValue().Set(description(iso, p->id)); }
static void create_channel(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); Peer *p = peer(iso, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); if (!p) return;
    v8::Local<v8::Object> options; if (!json_object(iso, a[1], options)) return;
    std::string label = jcstr(iso, field(iso, options, "label")); auto protocol_value = field(iso, options, "protocol"); std::string protocol = protocol_value->IsString() ? jcstr(iso, protocol_value) : "";
    if (label.size() > 65535 || protocol.size() > 65535 || label.find('\0') != std::string::npos || protocol.find('\0') != std::string::npos) { fail(iso, "NotSupportedError", "RTC label or protocol is too long or contains NUL"); return; }
    rtcDataChannelInit init{}; init.protocol = protocol.c_str(); init.negotiated = field(iso, options, "negotiated")->BooleanValue(iso);
    auto ordered = field(iso, options, "ordered"); init.reliability.unordered = !ordered->IsUndefined() && !ordered->BooleanValue(iso);
    auto lifetime = field(iso, options, "maxPacketLifeTime"), retransmits = field(iso, options, "maxRetransmits"), stream = field(iso, options, "id");
    int life = lifetime->IsNumber() ? lifetime->Int32Value(iso->GetCurrentContext()).FromMaybe(-1) : -1;
    int retry = retransmits->IsNumber() ? retransmits->Int32Value(iso->GetCurrentContext()).FromMaybe(-1) : -1;
    if (life > 65535 || retry > 65535 || (life >= 0 && retry >= 0)) { fail(iso, "TypeError", "Invalid RTC channel reliability"); return; }
    if (life == 0) { fail(iso, "NotSupportedError", "Zero packet lifetime is not representable by the native RTC C API"); return; }
    init.reliability.unreliable = life >= 0 || retry >= 0;
    init.reliability.maxPacketLifeTime = life >= 0 ? static_cast<unsigned>(life) : 0;
    init.reliability.maxRetransmits = retry >= 0 ? static_cast<unsigned>(retry) : 0;
    if (stream->IsNumber()) { int n = stream->Int32Value(iso->GetCurrentContext()).FromMaybe(-1); if (n < 0 || n > 65534) { fail(iso, "TypeError", "Invalid RTC stream id"); return; } init.manualStream = true; init.stream = static_cast<uint16_t>(n); }
    if (init.negotiated && !init.manualStream) { fail(iso, "TypeError", "Negotiated RTC channels require a stream id"); return; }
    { std::lock_guard<std::mutex> lock(p->channels_mutex); if (p->channels.size() >= kMaxChannels) { fail(iso, "OperationError", "Too many RTC channels"); return; } }
    int id = rtcCreateDataChannelEx(p->id, label.c_str(), &init); if (!native_ok(iso, id, "Native RTC channel creation failed")) return;
    auto c = std::make_unique<Channel>(); c->id = id; c->negotiated = init.negotiated; c->lifetime = life; c->retransmits = retry;
    bool accepted;
    {
        std::lock_guard<std::mutex> lock(p->channels_mutex);
        /* An incoming channel can consume the last slot during native allocation. */
        accepted = p->channels.size() < kMaxChannels && !p->closing.load();
        if (accepted) p->channels.emplace(id, std::move(c));
    }
    if (!accepted) { rtcDeleteDataChannel(id); fail(iso, "OperationError", "This peer has too many RTC channels"); return; }
    register_channel(p, id); a.GetReturnValue().Set(id);
}
static v8::Local<v8::Object> channel_info(v8::Isolate *iso, Peer *p, int id, bool counters) {
    auto o = v8::Object::New(iso); num(iso, o, "channelId", id);
    int stream = rtcGetDataChannelStream(id); if (stream >= 0) num(iso, o, "id", stream); else set(iso, o, "id", v8::Null(iso));
    str(iso, o, "label", string_result(id, rtcGetDataChannelLabel)); str(iso, o, "protocol", string_result(id, rtcGetDataChannelProtocol));
    const char *status = rtcIsOpen(id) ? "open" : rtcIsClosed(id) ? "closed" : "connecting";
    str(iso, o, "state", status); str(iso, o, "readyState", status);
    num(iso, o, "bufferedAmount", std::max(0, rtcGetBufferedAmount(id))); num(iso, o, "maxMessageSize", std::max(0, rtcMaxMessageSize(id)));
    rtcReliability reliability{}; int rc = rtcGetDataChannelReliability(id, &reliability);
    set(iso, o, "ordered", v8::Boolean::New(iso, rc >= 0 && !reliability.unordered));
    std::lock_guard<std::mutex> lock(p->channels_mutex); auto it = p->channels.find(id);
    if (it != p->channels.end()) {
        Channel *c = it->second.get(); set(iso, o, "negotiated", v8::Boolean::New(iso, c->negotiated));
        set(iso, o, "reliabilityKnown", v8::Boolean::New(iso, c->reliability_known));
        if (c->lifetime >= 0) num(iso, o, "maxPacketLifeTime", c->lifetime); else set(iso, o, "maxPacketLifeTime", v8::Null(iso));
        if (c->retransmits >= 0) num(iso, o, "maxRetransmits", c->retransmits); else set(iso, o, "maxRetransmits", v8::Null(iso));
        if (counters) { num(iso, o, "messagesSent", static_cast<double>(c->sent.load())); num(iso, o, "bytesSent", static_cast<double>(c->sent_bytes.load())); num(iso, o, "messagesReceived", static_cast<double>(c->received.load())); num(iso, o, "bytesReceived", static_cast<double>(c->received_bytes.load())); }
    }
    return o;
}
static void info(const v8::FunctionCallbackInfo<v8::Value> &a) { auto iso = a.GetIsolate(); int id = a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0); Peer *p = channel_peer(iso, id); if (p) a.GetReturnValue().Set(channel_info(iso, p, id, false)); }
static bool document_send_fits(State *s, size_t bytes) {
    if (bytes > kQueueBytes) return false;
    /* JS sends are serialized on this document's owner thread. Callback threads
       can drain these buffers or add empty channels, but cannot enqueue sends. */
    for (auto &entry : s->peers) {
        std::vector<int> ids;
        { std::lock_guard<std::mutex> lock(entry.second->channels_mutex); for (auto &item : entry.second->channels) ids.push_back(item.first); }
        for (int id : ids) {
            int queued = rtcGetBufferedAmount(id);
            if (queued < 0 || static_cast<size_t>(queued) > kQueueBytes - bytes) return false;
            bytes += static_cast<size_t>(queued);
        }
    }
    return true;
}
static void send(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); int id = a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0); Peer *p = channel_peer(iso, id); if (!p) return;
    if (!rtcIsOpen(id)) { fail(iso, "InvalidStateError", "RTC data channel is not open"); return; }
    std::string text; std::shared_ptr<v8::BackingStore> backing; const char *data = nullptr; size_t size = 0; bool binary = false;
    if (a[1]->IsString() && a[1].As<v8::String>()->Length() > kMessageBytes) { fail(iso, "TypeError", "RTC message exceeds the supported maximum"); return; }
    if (a[1]->IsString()) { text = jcstr(iso, a[1]); data = text.data(); size = text.size(); if (text.find('\0') != std::string::npos) { fail(iso, "NotSupportedError", "Embedded NUL text requires binary data in this RTC backend"); return; } }
    else if (a[1]->IsArrayBuffer()) { backing = a[1].As<v8::ArrayBuffer>()->GetBackingStore(); data = static_cast<const char *>(backing->Data()); size = backing->ByteLength(); binary = true; }
    else if (a[1]->IsArrayBufferView()) { auto view = a[1].As<v8::ArrayBufferView>(); backing = view->Buffer()->GetBackingStore(); data = backing->Data() ? static_cast<const char *>(backing->Data()) + view->ByteOffset() : nullptr; size = view->ByteLength(); binary = true; }
    else { fail(iso, "TypeError", "RTC send requires string, ArrayBuffer or an ArrayBuffer view"); return; }
    int max = rtcMaxMessageSize(id);
    if (size > kMessageBytes || (max > 0 && size > static_cast<size_t>(max))) { fail(iso, "TypeError", "RTC message exceeds the negotiated maximum"); return; }
    if (!document_send_fits(p->owner, size)) { fail(iso, "OperationError", "This document's RTC pending payload budget is full"); return; }
    if (!native_ok(iso, rtcSendMessage(id, data ? data : "", binary ? static_cast<int>(size) : -1), "Native RTC send failed")) return;
    std::lock_guard<std::mutex> lock(p->channels_mutex); auto it = p->channels.find(id);
    if (it != p->channels.end()) { ++it->second->sent; it->second->sent_bytes += size; }
}
static void threshold(const v8::FunctionCallbackInfo<v8::Value> &a) { auto iso = a.GetIsolate(); int id = a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0); if (!channel_peer(iso, id)) return; double n = a[1]->NumberValue(iso->GetCurrentContext()).FromMaybe(-1); if (!std::isfinite(n) || n < 0 || n > std::numeric_limits<int>::max()) { fail(iso, "TypeError", "Invalid RTC buffered amount threshold"); return; } native_ok(iso, rtcSetBufferedAmountLowThreshold(id, static_cast<int>(n)), "Native RTC threshold failed"); }
static void close_channel(const v8::FunctionCallbackInfo<v8::Value> &a) { auto iso = a.GetIsolate(); int id = a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0); if (channel_peer(iso, id)) native_ok(iso, rtcClose(id), "Native RTC channel close failed"); }
static void close(const v8::FunctionCallbackInfo<v8::Value> &a) { auto iso = a.GetIsolate(); State *s = state(jctx(iso)); if (s) close_peer(s, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); }
static void stats(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); Peer *p = peer(iso, a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(0)); if (!p) return;
    auto o = v8::Object::New(iso); str(iso, o, "connectionState", connection_name(p->connection.load())); str(iso, o, "iceConnectionState", ice_name(p->ice.load())); str(iso, o, "iceGatheringState", gathering_name(p->gathering.load()));
    std::vector<int> ids; { std::lock_guard<std::mutex> lock(p->channels_mutex); for (auto &item : p->channels) ids.push_back(item.first); }
    auto ar = v8::Array::New(iso, static_cast<int>(ids.size())); for (uint32_t i = 0; i < ids.size(); ++i) (void)ar->Set(iso->GetCurrentContext(), i, channel_info(iso, p, ids[i], true)); set(iso, o, "channels", ar);
    char local[8192] = {}, remote[8192] = {};
    if (rtcGetSelectedCandidatePair(p->id, local, sizeof local, remote, sizeof remote) >= 0) { auto pair = v8::Object::New(iso); str(iso, pair, "localCandidate", local); str(iso, pair, "remoteCandidate", remote); set(iso, o, "selectedCandidatePair", pair); }
    else set(iso, o, "selectedCandidatePair", v8::Null(iso));
    a.GetReturnValue().Set(o);
}
static v8::Local<v8::Object> event_object(v8::Isolate *iso, const Event &e) {
    auto o = v8::Object::New(iso); const char *type = "error";
    switch (e.kind) {
    case Kind::Description: type = "description"; num(iso, o, "requestId", e.request); str(iso, o, "sdp", e.a); str(iso, o, "descriptionType", e.b); break;
    case Kind::LocalDescription: { type = "state"; auto d = v8::Object::New(iso); str(iso, d, "sdp", e.a); str(iso, d, "type", e.b); set(iso, o, "localDescription", d); break; }
    case Kind::Operation: type = "operation"; num(iso, o, "requestId", e.request); break;
    case Kind::Error: type = "error"; str(iso, o, "name", "OperationError"); str(iso, o, "message", e.a); set(iso, o, "fatal", v8::True(iso)); break;
    case Kind::State: type = "state"; str(iso, o, e.a.c_str(), e.b); if (e.a == "iceGatheringState" && e.b == "complete") set(iso, o, "localDescription", description(iso, e.peer)); break;
    case Kind::Candidate: type = "candidate"; if (e.a.empty()) set(iso, o, "candidate", v8::Null(iso)); else str(iso, o, "candidate", e.a); if (!e.b.empty()) str(iso, o, "sdpMid", e.b); break;
    case Kind::Incoming: type = "datachannel"; break;
    case Kind::Open: type = "channel-open"; break;
    case Kind::Closed: type = "channel-close"; break;
    case Kind::ChannelError: type = "channel-error"; str(iso, o, "message", e.a); break;
    case Kind::BufferedLow: type = "channel-bufferedamountlow"; break;
    case Kind::Message:
        type = "channel-message";
        if (e.binary) { auto b = v8::ArrayBuffer::New(iso, e.data.size()); if (!e.data.empty()) memcpy(b->Data(), e.data.data(), e.data.size()); set(iso, o, "data", b); }
        else set(iso, o, "data", jstr(iso, e.data.empty() ? "" : reinterpret_cast<const char *>(e.data.data()), static_cast<int>(e.data.size())));
        break;
    }
    str(iso, o, "type", type); if (e.channel) num(iso, o, "channelId", e.channel); return o;
}
} // namespace
#endif

void js_install_rtc(JsCtx *c, v8::Local<v8::Object> N) {
    auto iso = c->iso; auto ctx = c->ctx.Get(iso);
    auto available = [](const v8::FunctionCallbackInfo<v8::Value> &a) {
#if LUMEN_RTC_NATIVE
        a.GetReturnValue().Set(true);
#else
        a.GetReturnValue().Set(false);
#endif
    };
    (void)N->Set(ctx, jstr(iso, "rtcNativeAvailable"), v8::Function::New(ctx, available).ToLocalChecked());
#if LUMEN_RTC_NATIVE
#define RTC_BIND(name, fn) (void)N->Set(ctx, jstr(iso, name), v8::Function::New(ctx, fn).ToLocalChecked())
    RTC_BIND("rtcCreate", create); RTC_BIND("rtcGenerateDescription", generate); RTC_BIND("rtcSetLocalDescription", set_local); RTC_BIND("rtcSetRemoteDescription", set_remote); RTC_BIND("rtcAddCandidate", add_candidate); RTC_BIND("rtcGetLocalDescription", get_local); RTC_BIND("rtcCreateChannel", create_channel); RTC_BIND("rtcChannelInfo", info); RTC_BIND("rtcSend", send); RTC_BIND("rtcSetBufferedAmountLowThreshold", threshold); RTC_BIND("rtcCloseChannel", close_channel); RTC_BIND("rtcClosePeer", close); RTC_BIND("rtcStats", stats);
#undef RTC_BIND
#endif
}
void js_rtc_close(JsCtx *c) {
#if LUMEN_RTC_NATIVE
    std::unique_ptr<State> owned;
    { std::lock_guard<std::mutex> lock(states_mutex); auto it = states.find(c); if (it == states.end()) return; owned = std::move(it->second); states.erase(it); }
    State *s = owned.get();
    { std::lock_guard<std::mutex> lock(s->mutex); s->alive = false; s->events.clear(); s->bytes = 0; }
    while (!s->peers.empty()) close_peer(s, s->peers.begin()->first);
#else
    (void)c;
#endif
}
double js_rtc_deadline(JsCtx *c) {
#if LUMEN_RTC_NATIVE
    State *s = state(c); if (s) {
        { std::lock_guard<std::mutex> lock(s->mutex); if (!s->events.empty()) return 0; }
        for (auto &entry : s->peers) if (entry.second->overflow.load()) return 0;
    }
#else
    (void)c;
#endif
    return INFINITY;
}
void js_rtc_deliver(JsCtx *c) {
#if LUMEN_RTC_NATIVE
    State *s = state(c); if (!s) return;
    auto iso = c->iso; auto ctx = c->ctx.Get(iso); std::deque<Event> ready;
    { std::lock_guard<std::mutex> lock(s->mutex); ready.swap(s->events); s->bytes = 0; }
    std::vector<int> overflow;
    for (auto &entry : s->peers) if (entry.second->overflow.exchange(false)) overflow.push_back(entry.first);
    for (int id : overflow) {
        auto it = s->peers.find(id); if (it == s->peers.end()) continue;
        auto callback = it->second->callback.Get(iso); close_peer(s, id);
        Event e{Kind::Error}; e.a = "Native RTC event queue limit exceeded; transport closed";
        v8::Local<v8::Value> argument = event_object(iso, e); (void)jcall(c, callback, ctx->Global(), 1, &argument);
    }
    for (const auto &e : ready) {
        auto it = s->peers.find(e.peer); if (it == s->peers.end() || it->second->closing.load()) continue;
        auto callback = it->second->callback.Get(iso); v8::Local<v8::Value> argument = event_object(iso, e);
        (void)jcall(c, callback, ctx->Global(), 1, &argument);
    }
#else
    (void)c;
#endif
}

size_t js_rtc_live_peers() {
#if LUMEN_RTC_NATIVE
    std::lock_guard<std::mutex> lock(library_mutex); return library_peers;
#else
    return 0;
#endif
}
