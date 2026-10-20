/* JS side of the native WebRTC transport (src/rtc/rtc_core.h) under the prelude's RTCPeerConnection.
   The prelude keeps the spec state machine and writes the SDP; a peer that is not another
   RTCPeerConnection in this process gets a native connection using the same ICE credentials,
   certificate and mids. Audio tracks carry Opus: mic PCM is encoded natively, received audio is decoded
   and played through SDL or read back for Web Audio. Network events are delivered in rtc_pump. */
#include "js_int.h"
#include "../rtc/rtc_core.h"
#include <cstring>
#include <map>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <string>
#include <vector>

using FCI = v8::FunctionCallbackInfo<v8::Value>;

namespace {

std::map<JsCtx *, v8::Global<v8::Function>> g_cbs;

bool make_cert(std::string &crt, std::string &key, std::string &fp) {
    EVP_PKEY *pk = EVP_EC_gen("P-256");
    X509 *x = X509_new();
    bool ok = false;
    if (pk && x) {
        unsigned char serial[8];
        RAND_bytes(serial, sizeof serial); serial[0] &= 0x7f;
        BIGNUM *bn = BN_bin2bn(serial, sizeof serial, nullptr);
        BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(x)); BN_free(bn);
        X509_set_version(x, 2);
        X509_gmtime_adj(X509_getm_notBefore(x), -86400);
        X509_gmtime_adj(X509_getm_notAfter(x), 30L * 86400);
        X509_set_pubkey(x, pk);
        X509_NAME *nm = X509_get_subject_name(x);
        X509_NAME_add_entry_by_txt(nm, "CN", MBSTRING_ASC, (const unsigned char *)"WebRTC", -1, -1, 0);
        X509_set_issuer_name(x, nm);
        if (X509_sign(x, pk, EVP_sha256()) > 0) {
            BIO *b1 = BIO_new(BIO_s_mem()), *b2 = BIO_new(BIO_s_mem());
            PEM_write_bio_X509(b1, x); PEM_write_bio_PrivateKey(b2, pk, nullptr, nullptr, 0, nullptr, nullptr);
            char *p; long n = BIO_get_mem_data(b1, &p); crt.assign(p, (size_t)n);
            n = BIO_get_mem_data(b2, &p); key.assign(p, (size_t)n);
            BIO_free(b1); BIO_free(b2);
            unsigned char md[32]; unsigned mdn = 0;
            X509_digest(x, EVP_sha256(), md, &mdn);
            static const char hx[] = "0123456789ABCDEF";
            fp.clear();
            for (unsigned i = 0; i < mdn; i++) { if (i) fp += ':'; fp += hx[md[i] >> 4]; fp += hx[md[i] & 15]; }
            ok = true;
        }
    }
    X509_free(x); EVP_PKEY_free(pk);
    return ok;
}

#define CTX                                                \
    JsCtx *c = jctx(a.GetIsolate());                       \
    v8::Isolate *iso = c->iso;                             \
    v8::Local<v8::Context> ctx = iso->GetCurrentContext(); \
    (void)ctx
#define S(i) jcstr(iso, a[i])
#define NUM(i) a[i]->NumberValue(ctx).FromMaybe(0)
#define BOOL(i) a[i]->BooleanValue(iso)
#define ERR(e) a.GetReturnValue().Set(jstr(iso, (e)))

/* rtcCert() -> [certificate PEM, key PEM, SHA-256 fingerprint] */
void n_rtcCert(const FCI &a) {
    CTX;
    std::string crt, key, fp;
    if (!make_cert(crt, key, fp)) return;
    v8::Local<v8::Value> v[3] = { jstr(iso, crt.c_str()), jstr(iso, key.c_str()), jstr(iso, fp.c_str()) };
    a.GetReturnValue().Set(v8::Array::New(iso, v, 3));
}
/* rtcInit(callback(pc, type, handle, a, b, n)) once per context */
void n_rtcInit(const FCI &a) {
    CTX;
    if (a[0]->IsFunction()) g_cbs[c].Reset(iso, a[0].As<v8::Function>());
}
/* rtcNew(iceServers [[url, user, credential]...], relayOnly, certPem, keyPem) -> pc id */
void n_rtcNew(const FCI &a) {
    CTX;
    std::vector<std::string> strs;
    std::vector<int> idx;   /* per server: url, user, credential index into strs or -1 */
    if (a[0]->IsArray()) {
        v8::Local<v8::Array> list = a[0].As<v8::Array>();
        for (uint32_t i = 0; i < list->Length(); i++) {
            v8::Local<v8::Value> e;
            if (!list->Get(ctx, i).ToLocal(&e) || !e->IsArray()) continue;
            v8::Local<v8::Array> s = e.As<v8::Array>();
            for (uint32_t k = 0; k < 3; k++) {
                v8::Local<v8::Value> v;
                if (s->Get(ctx, k).ToLocal(&v) && v->IsString()) { idx.push_back((int)strs.size()); strs.push_back(jcstr(iso, v)); }
                else idx.push_back(-1);
            }
        }
    }
    int n = (int)idx.size() / 3;
    std::vector<const char *> urls(n), users(n), creds(n);
    for (int i = 0; i < n; i++) {
        urls[i] = idx[i * 3] < 0 ? "" : strs[idx[i * 3]].c_str();
        users[i] = idx[i * 3 + 1] < 0 ? nullptr : strs[idx[i * 3 + 1]].c_str();
        creds[i] = idx[i * 3 + 2] < 0 ? nullptr : strs[idx[i * 3 + 2]].c_str();
    }
    std::string crt = S(2), key = S(3);
    int id = rtcc_new(c, urls.data(), users.data(), creds.data(), n, BOOL(1), crt.c_str(), key.c_str());
    if (id) a.GetReturnValue().Set((double)id);
}
/* rtcAddTrack(pc, mid, kind, direction, ssrc, cname, msid, trackId) -> track handle (before the native offer) */
void n_rtcAddTrack(const FCI &a) {
    CTX;
    std::string mid = S(1), kind = S(2), dir = S(3), cname = S(5), msid = S(6), tid = S(7);
    int h = rtcc_add_track((int)NUM(0), mid.c_str(), kind.c_str(), dir.c_str(), (uint32_t)NUM(4), cname.c_str(), msid.c_str(), tid.c_str());
    if (h) a.GetReturnValue().Set((double)h);
}
/* rtcTrackSetup(track, ssrc, cname): RTP packetizing, sender reports, NACK and receiver reports for an audio track */
void n_rtcTrackSetup(const FCI &a) {
    CTX;
    rtcc_track_setup((int)NUM(0), (uint32_t)NUM(1), S(2).c_str());
}
void n_rtcLocal(const FCI &a) {   /* rtcLocal(pc, type, ufrag, pwd) -> '' or an error */
    CTX;
    const char *e = rtcc_local((int)NUM(0), S(1).c_str(), S(2).c_str(), S(3).c_str());
    ERR(e ? e : "");
}
void n_rtcRemote(const FCI &a) {   /* rtcRemote(pc, sdp, type) -> '' or an error */
    CTX;
    const char *e = rtcc_remote((int)NUM(0), S(1).c_str(), S(2).c_str());
    ERR(e ? e : "");
}
void n_rtcCand(const FCI &a) {   /* rtcCand(pc, candidate, mid) */
    CTX;
    rtcc_cand((int)NUM(0), S(1).c_str(), S(2).c_str());
}
/* rtcDc(pc, label, protocol, unordered, maxRetransmits|-1, maxPacketLifeTime|-1, negotiated, id|-1) -> channel handle */
void n_rtcDc(const FCI &a) {
    CTX;
    int h = rtcc_dc((int)NUM(0), S(1).c_str(), S(2).c_str(), BOOL(3), (int)NUM(4), (int)NUM(5), BOOL(6), (int)NUM(7));
    if (h) a.GetReturnValue().Set((double)h);
}
void n_rtcDcSend(const FCI &a) {   /* rtcDcSend(channel, data, binary) -> buffered amount */
    CTX;
    double r;
    if (BOOL(2)) {
        const char *p = nullptr; size_t n = 0;
        if (a[1]->IsArrayBuffer()) { auto ab = a[1].As<v8::ArrayBuffer>(); p = (const char *)ab->Data(); n = ab->ByteLength(); }
        else if (a[1]->IsArrayBufferView()) { auto vw = a[1].As<v8::ArrayBufferView>(); p = (const char *)vw->Buffer()->Data() + vw->ByteOffset(); n = vw->ByteLength(); }
        r = rtcc_dc_send((int)NUM(0), p, n, true);
    } else {
        std::string s = S(1);
        r = rtcc_dc_send((int)NUM(0), s.data(), s.size(), false);
    }
    if (r >= 0) a.GetReturnValue().Set(r);
}
void n_rtcDcInfo(const FCI &a) {   /* rtcDcInfo(channel, lowThreshold|-1) -> [bufferedAmount, stream id|-1] */
    CTX;
    double buffered; int stream;
    if (!rtcc_dc_info((int)NUM(0), NUM(1), &buffered, &stream)) return;
    v8::Local<v8::Value> v[2] = { v8::Number::New(iso, buffered), v8::Number::New(iso, stream) };
    a.GetReturnValue().Set(v8::Array::New(iso, v, 2));
}
void n_rtcDcClose(const FCI &a) {
    CTX;
    rtcc_dc_close((int)NUM(0));
}
/* rtcSendPCM(track, Float32Array mono 48 kHz, enabled): 20 ms Opus frames; a disabled track sends silence */
void n_rtcSendPCM(const FCI &a) {
    CTX;
    if (!a[1]->IsFloat32Array()) return;
    auto f = a[1].As<v8::Float32Array>();
    rtcc_send_pcm((int)NUM(0), (const float *)((const char *)f->Buffer()->Data() + f->ByteOffset()), f->Length(), BOOL(2));
}
/* rtcPlay(track, on, volume): play received audio on the default output */
void n_rtcPlay(const FCI &a) {
    CTX;
    rtcc_play((int)NUM(0), BOOL(1), (float)NUM(2));
}
/* rtcRead(track) -> Float32Array of received PCM since the last read, or null; rtcRead(track, false) turns the tap off */
void n_rtcRead(const FCI &a) {
    CTX;
    if (a.Length() > 1 && !BOOL(1)) { rtcc_read((int)NUM(0), false, nullptr, 0); return; }
    static thread_local std::vector<float> buf(48000 + 5760);
    long n = rtcc_read((int)NUM(0), true, buf.data(), buf.size());
    if (n <= 0) { if (n == 0) a.GetReturnValue().Set(v8::Null(iso)); return; }
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, (size_t)n * sizeof(float));
    memcpy(ab->Data(), buf.data(), (size_t)n * sizeof(float));
    a.GetReturnValue().Set(v8::Float32Array::New(ab, 0, (size_t)n));
}
/* rtcStats(pc) -> [bytesSent, bytesReceived, rtt ms|-1, local address, remote address] */
void n_rtcStats(const FCI &a) {
    CTX;
    double sent, recvd, rtt;
    char l[256], r[256];
    if (!rtcc_stats((int)NUM(0), &sent, &recvd, &rtt, l, sizeof l, r, sizeof r)) return;
    v8::Local<v8::Value> v[5] = { v8::Number::New(iso, sent), v8::Number::New(iso, recvd), v8::Number::New(iso, rtt), jstr(iso, l), jstr(iso, r) };
    a.GetReturnValue().Set(v8::Array::New(iso, v, 5));
}
void n_rtcClose(const FCI &a) {
    CTX;
    rtcc_close((int)NUM(0));
}

}  // namespace

void js_install_rtc(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(rtcCert); REG(rtcInit); REG(rtcNew); REG(rtcAddTrack); REG(rtcTrackSetup); REG(rtcLocal); REG(rtcRemote); REG(rtcCand);
    REG(rtcDc); REG(rtcDcSend); REG(rtcDcInfo); REG(rtcDcClose); REG(rtcSendPCM); REG(rtcPlay); REG(rtcRead); REG(rtcStats); REG(rtcClose);
#undef REG
}

bool rtc_pending(JsCtx *c) { return rtcc_pending(c) > 0; }
bool rtc_active(JsCtx *c) { return g_cbs.count(c) > 0; }

void rtc_pump(JsCtx *c) {
    size_t n = rtcc_pending(c);   /* those queued now; later ones wait for the next turn */
    if (!n || !g_cbs.count(c)) return;
    JS_ENTER(c);
    v8::Local<v8::Function> cb = g_cbs[c].Get(iso);
    RtccEvent e;
    while (n-- && rtcc_take(c, &e)) {
        bool msg = !strcmp(e.type, "msg");
        v8::Local<v8::Value> argv[6] = {
            v8::Number::New(iso, e.pc), jstr(iso, e.type), v8::Number::New(iso, e.h),
            v8::Undefined(iso), jstr(iso, e.b), v8::Number::New(iso, e.n),
        };
        if (msg && e.bin) {
            v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, e.alen);
            if (e.alen) memcpy(ab->Data(), e.a, e.alen);
            argv[3] = ab;
        } else argv[3] = jstr(iso, e.a, (int)e.alen);
        if (!strcmp(e.type, "dc")) argv[4] = v8::Array::New(iso, std::vector<v8::Local<v8::Value>>{ jstr(iso, e.b), v8::Boolean::New(iso, e.bin) }.data(), 2);
        (void)jcall(c, cb, v8::Undefined(iso), 6, argv);
    }
}

void rtc_ctx_free(JsCtx *c) {
    rtcc_owner_free(c);
    auto f = g_cbs.find(c);
    if (f != g_cbs.end()) { f->second.Reset(); g_cbs.erase(f); }
}
