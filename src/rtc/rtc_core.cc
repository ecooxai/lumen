/* libdatachannel peer connections, data channels and Opus audio tracks behind rtc_core.h.
   Network threads post events into a queue that the owner's thread drains with rtcc_take. */
#include "rtc_core.h"
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif
#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <openssl/rand.h>
#include <opus/opus.h>
#include <rtc/rtc.hpp>
#include <string>
#include <strings.h>
#include <vector>

extern "C" void (*net_wakeup)(void);

namespace {

struct Event {
    void *c;
    int pc, h;
    std::string type, a, b;
    bool bin = false;
    double n = 0;
};

#ifdef __ANDROID__
/* Received call audio plays on its own AAudio stream with the voice communication usage, as
   Chrome's WebRTC plays it: with the microphone's VOICE_COMMUNICATION input and communication
   mode (src/media/capture.c), that is the playback the platform echo canceller removes. SDL
   would share one output among all its streams, whatever usage the first one opened with. */
struct Audio;
struct VoiceOut {
    AAudioStream *s = nullptr;
    std::vector<float> buf = std::vector<float>(48000 / 2);   /* ring of mono 48 kHz samples */
    size_t r = 0, n = 0;
    float gain = 1;
    std::atomic<bool> lost{false};   /* the stream was disconnected (a headset came or went) */
    size_t queued() const { return n; }
    void clear() { r = n = 0; }
    void put(const float *p, size_t k) {
        for (size_t i = 0; i < k; i++) {
            if (n == buf.size()) { r = (r + 1) % buf.size(); n--; }
            buf[(r + n++) % buf.size()] = p[i];
        }
    }
    size_t take(float *o, size_t k) {
        size_t m = std::min(k, n);
        for (size_t i = 0; i < m; i++) o[i] = buf[(r + i) % buf.size()] * gain;
        r = (r + m) % buf.size(); n -= m;
        return m;
    }
};
#endif
/* the receiving side of an audio track; the network thread decodes into it */
struct Audio {
    std::mutex mu;
    OpusDecoder *dec = nullptr;
#ifdef __ANDROID__
    VoiceOut *out = nullptr;          /* playing through an <audio> element */
#else
    SDL_AudioStream *out = nullptr;   /* playing through an <audio> element */
#endif
    bool started = false;
    bool tap = false;                 /* Web Audio reads the PCM */
    std::vector<float> ring;
    std::atomic<uint64_t> rx{0}, tx{0};   /* RTP bytes (payload + header) for getStats */
    ~Audio();
};

#ifdef __ANDROID__
aaudio_data_callback_result_t voice_data(AAudioStream *, void *ud, void *data, int32_t frames) {
    Audio *au = (Audio *)ud;
    float *o = (float *)data;
    size_t got = 0;
    {
        /* never wait for the network thread on the audio thread: a missed lock plays silence */
        std::unique_lock<std::mutex> l(au->mu, std::try_to_lock);
        if (l.owns_lock() && au->out && au->started) {
            got = au->out->take(o, (size_t)frames);
            if (got < (size_t)frames) au->started = false;   /* ran dry: buffer again before resuming */
        }
    }
    memset(o + got, 0, ((size_t)frames - got) * sizeof(float));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void voice_error(AAudioStream *, void *ud, aaudio_result_t) {
    Audio *au = (Audio *)ud;
    std::unique_lock<std::mutex> l(au->mu, std::try_to_lock);
    if (l.owns_lock() && au->out) au->out->lost = true;
}
/* with au->mu held */
bool voice_open(Audio *au) {
    AAudioStreamBuilder *b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 1);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_VOICE_COMMUNICATION);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_SPEECH);
    AAudioStreamBuilder_setDataCallback(b, voice_data, au);
    AAudioStreamBuilder_setErrorCallback(b, voice_error, au);
    AAudioStream *s = nullptr;
    if (AAudioStreamBuilder_openStream(b, &s) != AAUDIO_OK) s = nullptr;
    AAudioStreamBuilder_delete(b);
    if (s && AAudioStream_requestStart(s) != AAUDIO_OK) { AAudioStream_close(s); s = nullptr; }
    au->out->s = s;
    au->out->lost = false;
    return s != nullptr;
}
/* with au->mu held; the callbacks only try the lock, so closing here cannot deadlock */
void voice_close(Audio *au) {
    if (!au->out) return;
    if (au->out->s) { AAudioStream_requestStop(au->out->s); AAudioStream_close(au->out->s); }
    delete au->out;
    au->out = nullptr;
}
Audio::~Audio() { if (dec) opus_decoder_destroy(dec); std::lock_guard<std::mutex> l(mu); voice_close(this); }
#else
Audio::~Audio() { if (dec) opus_decoder_destroy(dec); if (out) SDL_DestroyAudioStream(out); }
#endif

struct Track {
    int pc;
    std::shared_ptr<rtc::Track> t;
    std::shared_ptr<Audio> audio = std::make_shared<Audio>();
    std::shared_ptr<rtc::RtpPacketizationConfig> cfg;
    OpusEncoder *enc = nullptr;
    std::vector<float> pending;
    uint32_t ts = 0;
    ~Track() { if (enc) opus_encoder_destroy(enc); }
};

struct Pc {
    void *c;
    std::shared_ptr<rtc::PeerConnection> pc;
};

std::mutex g_mu;   /* guards the queue and the handle maps */
std::deque<Event> g_events;
std::map<int, Pc> g_pcs;
std::map<int, std::pair<int, std::shared_ptr<rtc::DataChannel>>> g_dcs;   /* handle -> (pc, channel) */
std::map<int, std::unique_ptr<Track>> g_tracks;
int g_next = 1;
std::string g_err;      /* rtcc_local/rtcc_remote result */
Event g_taken;          /* rtcc_take result */

void post(Event e) {
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (!g_pcs.count(e.pc)) return;
        g_events.push_back(std::move(e));
    }
    if (net_wakeup) net_wakeup();
}

const char *state_name(rtc::PeerConnection::State s) {
    switch (s) {
    case rtc::PeerConnection::State::New: return "new";
    case rtc::PeerConnection::State::Connecting: return "connecting";
    case rtc::PeerConnection::State::Connected: return "connected";
    case rtc::PeerConnection::State::Disconnected: return "disconnected";
    case rtc::PeerConnection::State::Failed: return "failed";
    default: return "closed";
    }
}
const char *ice_name(rtc::PeerConnection::IceState s) {
    switch (s) {
    case rtc::PeerConnection::IceState::New: return "new";
    case rtc::PeerConnection::IceState::Checking: return "checking";
    case rtc::PeerConnection::IceState::Connected: return "connected";
    case rtc::PeerConnection::IceState::Completed: return "completed";
    case rtc::PeerConnection::IceState::Failed: return "failed";
    case rtc::PeerConnection::IceState::Disconnected: return "disconnected";
    default: return "closed";
    }
}

/* data channel callbacks (registered without g_mu: a callback may run at once) */
void wire_dc(void *c, int pc, int h, const std::shared_ptr<rtc::DataChannel> &dc) {
    std::weak_ptr<rtc::DataChannel> w = dc;
    dc->onOpen([c, pc, h, w] { auto d = w.lock(); post({ c, pc, h, "open", "", "", false, d && d->stream() ? (double)*d->stream() : -1 }); });
    dc->onClosed([c, pc, h] { post({ c, pc, h, "close" }); });
    dc->onError([c, pc, h](std::string e) { post({ c, pc, h, "dcerror", e }); });
    dc->onBufferedAmountLow([c, pc, h] { post({ c, pc, h, "low" }); });
    dc->onMessage([c, pc, h](rtc::binary b) { post({ c, pc, h, "msg", std::string((const char *)b.data(), b.size()), "", true }); },
                  [c, pc, h](std::string s) { post({ c, pc, h, "msg", std::move(s) }); });
}

/* Opus payloads from the depacketizer: decode, then play and/or keep for Web Audio */
void wire_audio(Track *tr) {
    std::shared_ptr<Audio> au = tr->audio;
    tr->t->onFrame([au](rtc::binary data, rtc::FrameInfo) {
        static thread_local float pcm[5760];
        au->rx += data.size() + 12;
        std::lock_guard<std::mutex> l(au->mu);
        if (!au->out && !au->tap) return;
        if (!au->dec) { int err; au->dec = opus_decoder_create(48000, 1, &err); if (!au->dec) return; }
        int n = opus_decode_float(au->dec, (const unsigned char *)data.data(), (opus_int32)data.size(), pcm, 5760, 0);
        if (n <= 0) return;
#ifdef __ANDROID__
        if (au->out) {
            if (au->out->lost) { if (au->out->s) AAudioStream_close(au->out->s); au->out->s = nullptr; voice_open(au.get()); }
            if (au->out->queued() > 48000 / 4) au->out->clear();   /* > 250 ms behind: drop */
            au->out->put(pcm, (size_t)n);
            if (!au->started && au->out->queued() >= 48000 * 6 / 100) au->started = true;
        }
#else
        if (au->out) {
            const int bytes = n * (int)sizeof(float);
            if (SDL_GetAudioStreamQueued(au->out) > 48000 * (int)sizeof(float) / 4) SDL_ClearAudioStream(au->out);   /* > 250 ms behind: drop */
            SDL_PutAudioStreamData(au->out, pcm, bytes);
            if (!au->started && SDL_GetAudioStreamQueued(au->out) >= 48000 * (int)sizeof(float) * 6 / 100) { au->started = true; SDL_ResumeAudioStreamDevice(au->out); }
        }
#endif
        if (au->tap) {
            if (au->ring.size() > 48000) au->ring.erase(au->ring.begin(), au->ring.begin() + (long)(au->ring.size() - 24000));
            au->ring.insert(au->ring.end(), pcm, pcm + n);
        }
    });
}

std::string sdp_reorder(const std::string &sdp) {
    /* the native offer lists tracks before the data section; give it the answer in that order */
    std::vector<std::string> secs;
    size_t at = 0, m;
    std::string head;
    while ((m = sdp.find("\nm=", at)) != std::string::npos) {
        if (secs.empty() && head.empty()) head = sdp.substr(0, m + 1);
        else secs.back() += sdp.substr(at, m + 1 - at);
        secs.push_back("");
        at = m + 1;
    }
    if (secs.empty()) return sdp;
    secs.back() = sdp.substr(at);
    std::string out = head, app;
    for (auto &s : secs) (s.compare(0, 14, "m=application ") ? out : app) += s;
    return out + app;
}

std::shared_ptr<rtc::PeerConnection> pc_of(int id) {
    std::lock_guard<std::mutex> l(g_mu);
    auto it = g_pcs.find(id);
    return it == g_pcs.end() ? nullptr : it->second.pc;
}
Track *track_of(int h) {
    std::lock_guard<std::mutex> l(g_mu);
    auto it = g_tracks.find(h);
    return it == g_tracks.end() ? nullptr : it->second.get();
}
std::shared_ptr<rtc::DataChannel> dc_of(int h) {
    std::lock_guard<std::mutex> l(g_mu);
    auto it = g_dcs.find(h);
    return it == g_dcs.end() ? nullptr : it->second.second;
}
/* the Opus payload type of a negotiated audio section, else the first one offered */
int opus_pt(const rtc::Description::Media &m, int fallback) {
    for (int pt : m.payloadTypes()) if (const auto *r = m.rtpMap(pt)) if (!strcasecmp(r->format.c_str(), "opus")) return pt;
    return fallback;
}
const char *str(const char *s) { return s ? s : ""; }

void close_pc(int id, std::vector<std::shared_ptr<rtc::PeerConnection>> &pcs, std::vector<std::unique_ptr<Track>> &trs, std::vector<std::shared_ptr<rtc::DataChannel>> &dcs) {
    auto it = g_pcs.find(id);
    if (it == g_pcs.end()) return;
    pcs.push_back(it->second.pc);
    g_pcs.erase(it);
    for (auto t = g_tracks.begin(); t != g_tracks.end();) if (t->second->pc == id) { trs.push_back(std::move(t->second)); t = g_tracks.erase(t); } else ++t;
    for (auto d = g_dcs.begin(); d != g_dcs.end();) if (d->second.first == id) { dcs.push_back(d->second.second); d = g_dcs.erase(d); } else ++d;
}
void finish_close(std::vector<std::shared_ptr<rtc::PeerConnection>> &pcs) {
    for (auto &p : pcs) try { p->close(); } catch (const std::exception &) {}
}

}  // namespace

extern "C" {

int rtcc_new(void *c, const char *const *urls, const char *const *users, const char *const *creds, int n, bool relay_only, const char *cert_pem, const char *key_pem) {
    rtc::Configuration cfg;
    cfg.disableAutoNegotiation = true;
    for (int i = 0; i < n; i++) {
        try {
            rtc::IceServer srv(str(urls[i]));
            if (users[i]) srv.username = users[i];
            if (creds[i]) srv.password = creds[i];
            cfg.iceServers.push_back(srv);
        } catch (const std::exception &) {}
    }
    if (relay_only) cfg.iceTransportPolicy = rtc::TransportPolicy::Relay;
    if (cert_pem && *cert_pem && key_pem && *key_pem) { cfg.certificatePemFile = cert_pem; cfg.keyPemFile = key_pem; }
    std::shared_ptr<rtc::PeerConnection> pc;
    try { pc = std::make_shared<rtc::PeerConnection>(cfg); } catch (const std::exception &) { return 0; }
    int id;
    {
        std::lock_guard<std::mutex> l(g_mu);
        id = g_next++;
        g_pcs[id] = { c, pc };
    }
    pc->onLocalCandidate([c, id](rtc::Candidate cand) { post({ c, id, 0, "cand", std::string(cand), cand.mid() }); });
    pc->onStateChange([c, id](rtc::PeerConnection::State s) { post({ c, id, 0, "state", state_name(s) }); });
    pc->onIceStateChange([c, id](rtc::PeerConnection::IceState s) { post({ c, id, 0, "ice", ice_name(s) }); });
    pc->onGatheringStateChange([c, id](rtc::PeerConnection::GatheringState s) {
        post({ c, id, 0, "gather", s == rtc::PeerConnection::GatheringState::Complete ? "complete" : s == rtc::PeerConnection::GatheringState::InProgress ? "gathering" : "new" });
    });
    pc->onDataChannel([c, id](std::shared_ptr<rtc::DataChannel> dc) {
        int h;
        {
            std::lock_guard<std::mutex> l(g_mu);
            if (!g_pcs.count(id)) return;
            h = g_next++;
            g_dcs[h] = { id, dc };
        }
        Event e{ c, id, h, "dc", dc->label(), dc->protocol(), false, dc->stream() ? (double)*dc->stream() : -1 };
        e.bin = dc->reliability().unordered;
        post(e);
        wire_dc(c, id, h, dc);
        if (dc->isOpen()) post({ c, id, h, "open", "", "", false, e.n });
    });
    pc->onTrack([c, id](std::shared_ptr<rtc::Track> t) {
        int h;
        Track *tr;
        {
            std::lock_guard<std::mutex> l(g_mu);
            if (!g_pcs.count(id)) return;
            h = g_next++;
            auto own = std::make_unique<Track>();
            own->pc = id; own->t = t; tr = own.get();
            g_tracks[h] = std::move(own);
        }
        wire_audio(tr);
        post({ c, id, h, "track", t->mid(), t->description().type() });
    });
    return id;
}

int rtcc_add_track(int id, const char *mid, const char *kind, const char *dir, uint32_t ssrc, const char *cname, const char *msid, const char *track_id) {
    auto pc = pc_of(id);
    if (!pc) return 0;
    std::string d = str(dir);
    bool audio = !strcmp(str(kind), "audio");
    rtc::Description::Direction dd = d == "sendonly" ? rtc::Description::Direction::SendOnly : d == "recvonly" ? rtc::Description::Direction::RecvOnly
                                     : d == "inactive" ? rtc::Description::Direction::Inactive : rtc::Description::Direction::SendRecv;
    std::shared_ptr<rtc::Track> t;
    try {
        if (audio) {
            rtc::Description::Audio m(str(mid), dd);
            m.addOpusCodec(111);
            if (dd == rtc::Description::Direction::SendOnly || dd == rtc::Description::Direction::SendRecv) m.addSSRC(ssrc, std::string(str(cname)), std::string(str(msid)), std::string(str(track_id)));
            t = pc->addTrack(m);
        } else {
            rtc::Description::Video m(str(mid), dd);
            m.addVP8Codec(96);
            t = pc->addTrack(m);
        }
    } catch (const std::exception &) { return 0; }
    int h;
    Track *tr;
    {
        std::lock_guard<std::mutex> l(g_mu);
        h = g_next++;
        auto own = std::make_unique<Track>();
        own->pc = id; own->t = t; tr = own.get();
        g_tracks[h] = std::move(own);
    }
    if (audio) wire_audio(tr);
    return h;
}

void rtcc_track_setup(int h, uint32_t ssrc, const char *cname) {
    Track *tr = track_of(h);
    if (!tr || tr->cfg) return;
    try {
        int pt = opus_pt(tr->t->description(), 111);
        tr->cfg = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, str(cname), (uint8_t)pt, 48000);
        auto pk = std::make_shared<rtc::OpusRtpPacketizer>(tr->cfg);
        pk->addToChain(std::make_shared<rtc::RtcpSrReporter>(tr->cfg));
        pk->addToChain(std::make_shared<rtc::RtcpNackResponder>());
        pk->addToChain(std::make_shared<rtc::OpusRtpDepacketizer>());
        pk->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
        tr->t->setMediaHandler(pk);
    } catch (const std::exception &) {}
}

const char *rtcc_local(int id, const char *type, const char *ufrag, const char *pwd) {
    auto pc = pc_of(id);
    if (!pc) return "closed";
    std::string t = str(type);
    try {
        pc->setLocalDescription(t == "answer" ? rtc::Description::Type::Answer : t == "pranswer" ? rtc::Description::Type::Pranswer : rtc::Description::Type::Offer,
                                rtc::LocalDescriptionInit{ std::string(str(ufrag)), std::string(str(pwd)) });
    } catch (const std::exception &e) { g_err = e.what(); if (g_err.empty()) g_err = "error"; return g_err.c_str(); }
    return nullptr;
}

const char *rtcc_remote(int id, const char *sdp, const char *type) {
    auto pc = pc_of(id);
    if (!pc) return "closed";
    std::string t = str(type), s = str(sdp);
    try {
        if (t != "offer") s = sdp_reorder(s);
        pc->setRemoteDescription(rtc::Description(s, t));
    } catch (const std::exception &e) { g_err = e.what(); if (g_err.empty()) g_err = "error"; return g_err.c_str(); }
    return nullptr;
}

void rtcc_cand(int id, const char *cand, const char *mid) {
    auto pc = pc_of(id);
    if (!pc) return;
    try { pc->addRemoteCandidate(rtc::Candidate(str(cand), str(mid))); } catch (const std::exception &) {}
}

int rtcc_dc(int id, const char *label, const char *protocol, bool unordered, int max_retransmits, int max_lifetime_ms, bool negotiated, int stream) {
    auto pc = pc_of(id);
    if (!pc) return 0;
    std::shared_ptr<rtc::DataChannel> dc;
    try {
        rtc::DataChannelInit init;
        init.protocol = str(protocol);
        init.reliability.unordered = unordered;
        if (max_retransmits >= 0) init.reliability.maxRetransmits = (unsigned)max_retransmits;
        else if (max_lifetime_ms >= 0) init.reliability.maxPacketLifeTime = std::chrono::milliseconds(max_lifetime_ms);
        init.negotiated = negotiated;
        if (stream >= 0) init.id = (uint16_t)stream;
        dc = pc->createDataChannel(str(label), init);
    } catch (const std::exception &) { return 0; }
    int h;
    void *c;
    {
        std::lock_guard<std::mutex> l(g_mu);
        auto it = g_pcs.find(id);
        if (it == g_pcs.end()) return 0;
        c = it->second.c;
        h = g_next++;
        g_dcs[h] = { id, dc };
    }
    wire_dc(c, id, h, dc);
    return h;
}

double rtcc_dc_send(int h, const void *data, size_t len, bool binary) {
    auto dc = dc_of(h);
    if (!dc) return -1;
    try {
        if (binary) dc->send((const rtc::byte *)data, len);
        else dc->send(std::string((const char *)data, len));
    } catch (const std::exception &) {}
    return (double)dc->bufferedAmount();
}

bool rtcc_dc_info(int h, double low_threshold, double *buffered, int *stream) {
    auto dc = dc_of(h);
    if (!dc) return false;
    if (low_threshold >= 0) dc->setBufferedAmountLowThreshold((size_t)low_threshold);
    *buffered = (double)dc->bufferedAmount();
    *stream = dc->stream() ? (int)*dc->stream() : -1;
    return true;
}

void rtcc_dc_close(int h) {
    auto dc = dc_of(h);
    if (dc) try { dc->close(); } catch (const std::exception &) {}
}

void rtcc_send_pcm(int h, const float *p, size_t n, bool on) {
    Track *tr = track_of(h);
    if (!tr || !tr->cfg) return;
    if (!tr->enc) {
        int err;
        tr->enc = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
        if (!tr->enc) return;
        opus_encoder_ctl(tr->enc, OPUS_SET_BITRATE(32000));
        opus_encoder_ctl(tr->enc, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(tr->enc, OPUS_SET_PACKET_LOSS_PERC(5));
        opus_encoder_ctl(tr->enc, OPUS_SET_COMPLEXITY(5));
        RAND_bytes((unsigned char *)&tr->ts, sizeof tr->ts);
    }
    size_t at = tr->pending.size();
    tr->pending.resize(at + n);
    if (on) memcpy(tr->pending.data() + at, p, n * sizeof(float)); else std::fill(tr->pending.begin() + (long)at, tr->pending.end(), 0.f);
    if (tr->pending.size() > 48000) tr->pending.erase(tr->pending.begin(), tr->pending.end() - 48000);   /* stalled: keep the last second */
    size_t used = 0;
    unsigned char pkt[1500];
    bool open = tr->t->isOpen();
    while (tr->pending.size() - used >= 960) {
        if (open) {
            int len = opus_encode_float(tr->enc, tr->pending.data() + used, 960, pkt, sizeof pkt);
            if (len > 0) try { tr->t->sendFrame((const rtc::byte *)pkt, (size_t)len, rtc::FrameInfo(tr->ts)); tr->audio->tx += (uint64_t)len + 12; } catch (const std::exception &) { open = false; }
        }
        tr->ts += 960; used += 960;
    }
    tr->pending.erase(tr->pending.begin(), tr->pending.begin() + (long)used);
}

void rtcc_play(int h, bool on, float vol) {
    Track *tr = track_of(h);
    if (!tr) return;
    std::lock_guard<std::mutex> l(tr->audio->mu);
    Audio *au = tr->audio.get();
#ifdef __ANDROID__
    if (on && !au->out) {
        au->out = new VoiceOut;
        au->started = false;
        if (!voice_open(au)) voice_close(au);
    } else if (!on) voice_close(au);
    if (au->out) au->out->gain = vol;
#else
    if (on && !au->out) {
        if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
        SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, 48000 };
        au->out = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        au->started = false;
    } else if (!on && au->out) { SDL_DestroyAudioStream(au->out); au->out = nullptr; }
    if (au->out) SDL_SetAudioStreamGain(au->out, vol);
#endif
}

long rtcc_read(int h, bool tap, float *out, size_t cap) {
    Track *tr = track_of(h);
    if (!tr) return -1;
    std::lock_guard<std::mutex> l(tr->audio->mu);
    Audio *au = tr->audio.get();
    au->tap = tap;
    if (!tap) { au->ring.clear(); return 0; }
    size_t n = std::min(cap, au->ring.size());
    if (n) memcpy(out, au->ring.data(), n * sizeof(float));
    au->ring.erase(au->ring.begin(), au->ring.begin() + (long)n);
    return (long)n;
}

bool rtcc_stats(int id, double *sent, double *received, double *rtt_ms, char *local, size_t local_size, char *remote, size_t remote_size) {
    auto pc = pc_of(id);
    if (!pc) return false;
    auto rtt = pc->rtt();
    *sent = (double)pc->bytesSent();
    *received = (double)pc->bytesReceived();
    *rtt_ms = rtt ? (double)rtt->count() : -1;
    {
        std::lock_guard<std::mutex> l(g_mu);
        for (auto &t : g_tracks) if (t.second->pc == id) { *sent += (double)t.second->audio->tx; *received += (double)t.second->audio->rx; }
    }
    rtc::Candidate lc, rc;
    bool pair = pc->getSelectedCandidatePair(&lc, &rc);
    snprintf(local, local_size, "%s", pair ? std::string(lc).c_str() : "");
    snprintf(remote, remote_size, "%s", pair ? std::string(rc).c_str() : "");
    return true;
}

void rtcc_close(int id) {
    std::vector<std::shared_ptr<rtc::PeerConnection>> pcs;
    std::vector<std::unique_ptr<Track>> trs;
    std::vector<std::shared_ptr<rtc::DataChannel>> dcs;
    {
        std::lock_guard<std::mutex> l(g_mu);
        close_pc(id, pcs, trs, dcs);
    }
    finish_close(pcs);
}

void rtcc_owner_free(void *c) {
    std::vector<std::shared_ptr<rtc::PeerConnection>> pcs;
    std::vector<std::unique_ptr<Track>> trs;
    std::vector<std::shared_ptr<rtc::DataChannel>> dcs;
    {
        std::lock_guard<std::mutex> l(g_mu);
        std::vector<int> ids;
        for (auto &kv : g_pcs) if (kv.second.c == c) ids.push_back(kv.first);
        for (int id : ids) close_pc(id, pcs, trs, dcs);
        for (auto it = g_events.begin(); it != g_events.end();) if (it->c == c) it = g_events.erase(it); else ++it;
    }
    finish_close(pcs);
}

size_t rtcc_pending(void *c) {
    std::lock_guard<std::mutex> l(g_mu);
    size_t n = 0;
    for (const Event &e : g_events) if (e.c == c) n++;
    return n;
}

bool rtcc_take(void *c, RtccEvent *out) {
    std::lock_guard<std::mutex> l(g_mu);
    for (auto it = g_events.begin(); it != g_events.end();) {
        if (it->c != c) { ++it; continue; }
        g_taken = std::move(*it);
        it = g_events.erase(it);
        auto p = g_pcs.find(g_taken.pc);
        if (p == g_pcs.end() || p->second.c != c) continue;   /* closed meanwhile */
        *out = { g_taken.pc, g_taken.h, g_taken.type.c_str(), g_taken.a.data(), g_taken.a.size(), g_taken.b.c_str(), g_taken.bin, g_taken.n };
        return true;
    }
    return false;
}

}  // extern "C"
