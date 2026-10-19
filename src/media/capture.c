/* Microphone capture for getUserMedia (mono float at 48 kHz; SDL recording, AAudio on Android) and
   MediaRecorder encoding with FFmpeg: Opus in WebM or Ogg, AAC in fragmented MP4.
   Everything here runs on the JS thread, except the Android permission answer. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "media/media.h"
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#include <jni.h>
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MIC_RATE 48000
#define MIC_MAX 8
#ifndef __ANDROID__
static int g_mic_granted;   /* desktop: no prompt, so "granted" once a device opened */
#endif
#ifdef __ANDROID__
/* Android records with AAudio itself: entering communication mode reroutes the input and
   disconnects a stream opened just before, and SDL keeps default recordings on such a lost
   device for good, so they would stay silent. A lost stream is reopened instead (mic_heal). */
#define AMIC_CAP 65536   /* ring samples (1.4 s), a power of two */
typedef struct { AAudioStream *st; SDL_AtomicInt w, r, lost; float ring[AMIC_CAP]; } MicDev;
#else
typedef SDL_AudioStream MicDev;
#endif
static struct { const void *owner; MicDev *s; int state; bool voice, call; uint64_t retry; } g_mic[MIC_MAX];   /* state: 0 asking, 1 capturing, -1 failed */
#ifdef __ANDROID__
static SDL_AtomicInt g_mic_perm;   /* 0 unknown, 1 asking, 2 granted, -1 denied (the next mic_open asks again) */
static void SDLCALL mic_permission_answer(void *ud, const char *permission, bool granted) {
    (void)ud; (void)permission;
    SDL_SetAtomicInt(&g_mic_perm, granted ? 2 : -1);
}
/* the persistent grant survives app restarts: ask Android once instead of reporting "prompt" */
static void mic_check_granted(void) {
    if (SDL_GetAtomicInt(&g_mic_perm)) return;
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    jobject activity = env ? SDL_GetAndroidActivity() : NULL;
    if (!activity) return;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID check = cls ? (*env)->GetMethodID(env, cls, "checkSelfPermission", "(Ljava/lang/String;)I") : NULL;
    jstring name = check ? (*env)->NewStringUTF(env, "android.permission.RECORD_AUDIO") : NULL;
    if (name && (*env)->CallIntMethod(env, activity, check, name) == 0) SDL_CompareAndSwapAtomicInt(&g_mic_perm, 0, 2);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (name) (*env)->DeleteLocalRef(env, name);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}
/* open voice microphones; while there are any, the activity keeps Android's communication mode
   (LumenActivity.setVoiceCall), which pairs the echo canceller with the call's playback; a closing
   microphone leaves it on its closer thread, after the stream is gone */
static int g_voice_calls;
static SDL_Mutex *g_voice_mu;
static void voice_call_java(bool on);
static void voice_call(bool on) {
    static SDL_InitState once;
    if (SDL_ShouldInit(&once)) { g_voice_mu = SDL_CreateMutex(); SDL_SetInitialized(&once, true); }
    SDL_LockMutex(g_voice_mu);
    if (!(on ? g_voice_calls++ : --g_voice_calls)) voice_call_java(on);
    SDL_UnlockMutex(g_voice_mu);
}
static void voice_call_java(bool on) {
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    jobject activity = env ? SDL_GetAndroidActivity() : NULL;
    if (!activity) return;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID m = cls ? (*env)->GetStaticMethodID(env, cls, "setVoiceCall", "(Z)V") : NULL;
    if (m) (*env)->CallStaticVoidMethod(env, cls, m, (jboolean)on);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}
#else
static void voice_call(bool on) { (void)on; }
#endif

#ifdef __ANDROID__
static aaudio_data_callback_result_t mic_data(AAudioStream *st, void *ud, void *data, int32_t n) {
    (void)st;
    MicDev *m = ud; const float *in = data;
    unsigned w = (unsigned)SDL_GetAtomicInt(&m->w), r = (unsigned)SDL_GetAtomicInt(&m->r);
    unsigned room = AMIC_CAP - (w - r);
    if ((unsigned)n > room) n = (int32_t)room;   /* the reader fell behind: drop the newest */
    for (int32_t i = 0; i < n; i++) m->ring[(w + i) & (AMIC_CAP - 1)] = in[i];
    SDL_MemoryBarrierRelease();
    SDL_SetAtomicInt(&m->w, (int)(w + n));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void mic_error(AAudioStream *st, void *ud, aaudio_result_t e) { (void)st; (void)e; SDL_SetAtomicInt(&((MicDev *)ud)->lost, 1); }
static MicDev *dev_open(bool voice) {
    MicDev *m = calloc(1, sizeof *m);
    AAudioStreamBuilder *b = NULL;
    if (!m || AAudio_createStreamBuilder(&b) != AAUDIO_OK) { free(m); return NULL; }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 1);
    AAudioStreamBuilder_setSampleRate(b, MIC_RATE);
    /* VOICE_COMMUNICATION brings the platform echo canceller, noise suppressor and gain control */
    AAudioStreamBuilder_setInputPreset(b, voice ? AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION : AAUDIO_INPUT_PRESET_CAMCORDER);
    AAudioStreamBuilder_setDataCallback(b, mic_data, m);
    AAudioStreamBuilder_setErrorCallback(b, mic_error, m);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &m->st);
    AAudioStreamBuilder_delete(b);
    if (r == AAUDIO_OK && (AAudioStream_getSampleRate(m->st) != MIC_RATE || AAudioStream_getChannelCount(m->st) != 1 || AAudioStream_getFormat(m->st) != AAUDIO_FORMAT_PCM_FLOAT))
        r = AAUDIO_ERROR_INVALID_FORMAT;
    if (r == AAUDIO_OK) r = AAudioStream_requestStart(m->st);
    if (r != AAUDIO_OK) {
        SDL_SetError("%s", AAudio_convertResultToText(r));
        if (m->st) AAudioStream_close(m->st);
        free(m);
        return NULL;
    }
    return m;
}
static void dev_close(MicDev *m) { AAudioStream_requestStop(m->st); AAudioStream_close(m->st); free(m); }
static int dev_read(MicDev *m, float *out, int max) {
    unsigned r = (unsigned)SDL_GetAtomicInt(&m->r), w = (unsigned)SDL_GetAtomicInt(&m->w);
    SDL_MemoryBarrierAcquire();
    int n = (int)(w - r) < max ? (int)(w - r) : max;
    for (int i = 0; i < n; i++) out[i] = m->ring[(r + i) & (AMIC_CAP - 1)];
    SDL_SetAtomicInt(&m->r, (int)(r + n));
    return n;
}
static int dev_available(MicDev *m) { return (int)((unsigned)SDL_GetAtomicInt(&m->w) - (unsigned)SDL_GetAtomicInt(&m->r)); }
#else
static MicDev *dev_open(bool voice) {
    (void)voice;
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, MIC_RATE };
    SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL);
    if (s) SDL_ResumeAudioStreamDevice(s);
    return s;
}
static void dev_close(MicDev *s) { SDL_DestroyAudioStream(s); }
static int dev_read(MicDev *s, float *out, int max) { int n = SDL_GetAudioStreamData(s, out, max * (int)sizeof(float)); return n > 0 ? n / (int)sizeof(float) : 0; }
static int dev_available(MicDev *s) { int n = SDL_GetAudioStreamAvailable(s); return n > 0 ? n / (int)sizeof(float) : 0; }
#endif

/* Closing an Android voice input waits for AAudio to stop it, seconds on some devices, so streams
   close on their own thread; the slot is free (and mic_read sees no stream) at once */
typedef struct { MicDev *s; bool call; } MicClosing;
static int mic_closer(void *p) {
    MicClosing *c = p;
    dev_close(c->s);
    if (c->call) voice_call(false);
    free(c);
    return 0;
}
static void mic_release(MicDev *s, bool call) {
    MicClosing *c = s ? malloc(sizeof *c) : NULL;
    SDL_Thread *t = NULL;
    if (c) { c->s = s; c->call = call; t = SDL_CreateThread(mic_closer, "lumen-mic-close", c); }
    if (t) { SDL_DetachThread(t); return; }
    free(c);
    if (s) dev_close(s);
    if (call) voice_call(false);
}
int mic_permission(void) {
#ifdef __ANDROID__
    mic_check_granted();
    int p = SDL_GetAtomicInt(&g_mic_perm);
    return p == 2 ? 1 : p < 0 ? -1 : 0;
#else
    return g_mic_granted;
#endif
}
static void mic_start(int i) {
    if (g_mic[i].voice) { voice_call(true); g_mic[i].call = true; }
    g_mic[i].s = dev_open(g_mic[i].voice);
    if (!g_mic[i].s) {
        SDL_Log("lumen: microphone unavailable: %s", SDL_GetError()); g_mic[i].state = -1;
        if (g_mic[i].call) { g_mic[i].call = false; voice_call(false); }
        return;
    }
    g_mic[i].state = 1;
#ifndef __ANDROID__
    g_mic_granted = 1;
#endif
}
#ifdef __ANDROID__
/* a stream Android disconnected (a route change) is replaced, retried twice a second until it opens */
static void mic_heal(int id) {
    if (g_mic[id].state != 1 || (g_mic[id].s && !SDL_GetAtomicInt(&g_mic[id].s->lost))) return;
    uint64_t now = SDL_GetTicks();
    if (now < g_mic[id].retry) return;
    if (g_mic[id].s) mic_release(g_mic[id].s, false);
    g_mic[id].s = dev_open(g_mic[id].voice);
    g_mic[id].retry = now + 500;
    SDL_Log("lumen: microphone %s", g_mic[id].s ? "reopened" : "lost, retrying");
}
#else
static void mic_heal(int id) { (void)id; }
#endif
int mic_open(const void *owner, bool voice) {
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
    for (int i = 0; i < MIC_MAX; i++) if (!g_mic[i].owner) {
        g_mic[i].owner = owner; g_mic[i].s = NULL; g_mic[i].state = 0; g_mic[i].voice = voice;
#ifdef __ANDROID__
        /* ask without blocking; the device opens on a later mic_state once granted */
        mic_check_granted();
        int p = SDL_GetAtomicInt(&g_mic_perm);
        if (p == 2) mic_start(i);
        else if (p != 1) {
            SDL_SetAtomicInt(&g_mic_perm, 1);
            if (!SDL_RequestAndroidPermission("android.permission.RECORD_AUDIO", mic_permission_answer, NULL)) SDL_SetAtomicInt(&g_mic_perm, -1);
        }
#else
        mic_start(i);
#endif
        return i;
    }
    return -1;
}
static bool mic_ok(const void *owner, int id) { return id >= 0 && id < MIC_MAX && g_mic[id].owner && g_mic[id].owner == owner; }
int mic_state(const void *owner, int id) {
    if (!mic_ok(owner, id)) return -1;
#ifdef __ANDROID__
    if (!g_mic[id].state) {
        int p = SDL_GetAtomicInt(&g_mic_perm);
        if (p == 2) mic_start(id);
        else if (p < 0) g_mic[id].state = -1;
    }
#endif
    return g_mic[id].state;
}
int mic_read(const void *owner, int id, float *out, int max) {
    if (!mic_ok(owner, id) || max <= 0) return 0;
    mic_heal(id);
    return g_mic[id].s ? dev_read(g_mic[id].s, out, max) : 0;
}
int mic_available(const void *owner, int id) {
    if (!mic_ok(owner, id)) return 0;
    mic_heal(id);
    return g_mic[id].s ? dev_available(g_mic[id].s) : 0;
}
void mic_close(const void *owner, int id) {
    if (!mic_ok(owner, id)) return;
    MicDev *s = g_mic[id].s;
    bool call = g_mic[id].call;
    memset(&g_mic[id], 0, sizeof g_mic[id]);
    mic_release(s, call);
}
void mic_close_owner(const void *owner) { for (int i = 0; i < MIC_MAX; i++) if (g_mic[i].owner == owner) mic_close(owner, i); }
bool mic_active(void) { for (int i = 0; i < MIC_MAX; i++) if (g_mic[i].state == 1) return true; return false; }

/* ---------------- MediaRecorder ---------------- */
#define REC_MAX 8
typedef struct {
    const void *owner;
    AVFormatContext *fc; AVCodecContext *cc; AVStream *st; AVFrame *fr; AVPacket *pkt;
    float *pend; int npend;        /* PCM waiting for a whole codec frame */
    int64_t pts;
    uint8_t *out; size_t nout, cap;   /* muxed bytes not yet taken */
    bool done;
} Rec;
static Rec *g_rec[REC_MAX];

/* the container and codec for a MediaRecorder mime type: 1 webm/opus, 2 ogg/opus, 3 mp4/aac; 0 unsupported */
int rec_kind(const char *mime) {
    if (!mime || !*mime) return 1;
    char t[64]; size_t n = strcspn(mime, ";"); if (n >= sizeof t) return 0;
    memcpy(t, mime, n); t[n] = 0;
    while (n && t[n - 1] == ' ') t[--n] = 0;
    const char *codecs = strstr(mime, "codecs=");
    bool opus = !codecs || strcasestr(codecs, "opus"), aac = !codecs || strcasestr(codecs, "mp4a") || strcasestr(codecs, "aac");
    if (codecs && (strcasestr(codecs, "vp") || strcasestr(codecs, "avc"))) return 0;   /* audio only */
    if (!strcasecmp(t, "audio/webm") || !strcasecmp(t, "video/webm")) return opus ? 1 : 0;
    if (!strcasecmp(t, "audio/ogg")) return opus ? 2 : 0;
    if (!strcasecmp(t, "audio/mp4") || !strcasecmp(t, "video/mp4")) return aac ? 3 : 0;
    return 0;
}
static int rec_write(void *o, const uint8_t *buf, int n) {
    Rec *r = o;
    if (r->nout + (size_t)n > r->cap) {
        size_t cap = r->cap ? r->cap : 16384; while (cap < r->nout + (size_t)n) cap *= 2;
        uint8_t *p = realloc(r->out, cap); if (!p) return AVERROR(ENOMEM);
        r->out = p; r->cap = cap;
    }
    memcpy(r->out + r->nout, buf, (size_t)n); r->nout += (size_t)n;
    return n;
}
static void rec_free(Rec *r) {
    if (!r) return;
    if (r->fc) { if (r->fc->pb) { av_freep(&r->fc->pb->buffer); avio_context_free(&r->fc->pb); } avformat_free_context(r->fc); }
    avcodec_free_context(&r->cc); av_frame_free(&r->fr); av_packet_free(&r->pkt);
    free(r->pend); free(r->out); free(r);
}
int rec_open(const void *owner, const char *mime, int bps) {
    int kind = rec_kind(mime), slot = -1;
    for (int i = 0; i < REC_MAX && slot < 0; i++) if (!g_rec[i]) slot = i;
    if (!kind || slot < 0) return -1;
    Rec *r = calloc(1, sizeof *r); if (!r) return -1;
    r->owner = owner;
    const char *fmt = kind == 1 ? "webm" : kind == 2 ? "ogg" : "mp4";
    const AVCodec *codec = avcodec_find_encoder(kind == 3 ? AV_CODEC_ID_AAC : AV_CODEC_ID_OPUS);
    if (!codec || avformat_alloc_output_context2(&r->fc, NULL, fmt, NULL) < 0 || !r->fc) goto fail;
    uint8_t *iobuf = av_malloc(4096);
    if (!iobuf) goto fail;
    r->fc->pb = avio_alloc_context(iobuf, 4096, 1, r, NULL, rec_write, NULL);
    if (!r->fc->pb) { av_free(iobuf); goto fail; }
    r->fc->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_FLUSH_PACKETS;
    r->cc = avcodec_alloc_context3(codec);
    if (!r->cc) goto fail;
    r->cc->sample_rate = MIC_RATE; r->cc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&r->cc->ch_layout, 1);
    r->cc->bit_rate = bps > 0 ? bps : kind == 3 ? 64000 : 32000;
    r->cc->time_base = (AVRational){ 1, MIC_RATE };
    r->cc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;   /* FFmpeg's own Opus encoder */
    if (r->fc->oformat->flags & AVFMT_GLOBALHEADER) r->cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(r->cc, codec, NULL) < 0) goto fail;
    r->st = avformat_new_stream(r->fc, NULL);
    if (!r->st || avcodec_parameters_from_context(r->st->codecpar, r->cc) < 0) goto fail;
    r->st->time_base = r->cc->time_base;
    AVDictionary *opt = NULL;
    if (kind == 1) { av_dict_set(&opt, "live", "1", 0); av_dict_set(&opt, "cluster_time_limit", "1000", 0); }
    if (kind == 3) { av_dict_set(&opt, "movflags", "empty_moov+default_base_moof", 0); av_dict_set(&opt, "frag_duration", "1000000", 0); }
    int hr = avformat_write_header(r->fc, &opt);
    av_dict_free(&opt);
    if (hr < 0) goto fail;
    avio_flush(r->fc->pb);
    r->fr = av_frame_alloc(); r->pkt = av_packet_alloc();
    int fs = r->cc->frame_size > 0 ? r->cc->frame_size : 960;
    r->pend = malloc(sizeof(float) * (size_t)fs);
    if (!r->fr || !r->pkt || !r->pend) goto fail;
    g_rec[slot] = r;
    return slot;
fail:
    SDL_Log("lumen: MediaRecorder %s unavailable", fmt);
    rec_free(r);
    return -1;
}
static Rec *rec_get(const void *owner, int id) { return id >= 0 && id < REC_MAX && g_rec[id] && g_rec[id]->owner == owner ? g_rec[id] : NULL; }
static void rec_drain(Rec *r) {
    while (avcodec_receive_packet(r->cc, r->pkt) == 0) {
        av_packet_rescale_ts(r->pkt, r->cc->time_base, r->st->time_base);
        r->pkt->stream_index = r->st->index;
        av_write_frame(r->fc, r->pkt);   /* takes the packet's reference */
    }
    avio_flush(r->fc->pb);
}
static void rec_frame(Rec *r, int n) {
    AVFrame *f = r->fr; int fs = r->cc->frame_size > 0 ? r->cc->frame_size : n;
    av_frame_unref(f);
    f->nb_samples = fs; f->format = r->cc->sample_fmt; f->sample_rate = MIC_RATE;
    av_channel_layout_copy(&f->ch_layout, &r->cc->ch_layout);
    if (av_frame_get_buffer(f, 0) < 0) return;
    memcpy(f->data[0], r->pend, sizeof(float) * (size_t)n);
    if (n < fs) memset((float *)f->data[0] + n, 0, sizeof(float) * (size_t)(fs - n));
    f->pts = r->pts; r->pts += fs;
    if (avcodec_send_frame(r->cc, f) == 0) rec_drain(r);
}
void rec_push(const void *owner, int id, const float *pcm, int frames) {
    Rec *r = rec_get(owner, id); if (!r || r->done) return;
    int fs = r->cc->frame_size > 0 ? r->cc->frame_size : 960;
    while (frames > 0) {
        int k = fs - r->npend < frames ? fs - r->npend : frames;
        memcpy(r->pend + r->npend, pcm, sizeof(float) * (size_t)k);
        r->npend += k; pcm += k; frames -= k;
        if (r->npend == fs) { rec_frame(r, fs); r->npend = 0; }
    }
}
void rec_finish(const void *owner, int id) {
    Rec *r = rec_get(owner, id); if (!r || r->done) return;
    if (r->npend) { rec_frame(r, r->npend); r->npend = 0; }
    if (avcodec_send_frame(r->cc, NULL) == 0) rec_drain(r);
    av_write_trailer(r->fc);
    avio_flush(r->fc->pb);
    r->done = true;
}
uint8_t *rec_take(const void *owner, int id, size_t *n) {
    Rec *r = rec_get(owner, id); *n = 0;
    if (!r || !r->nout) return NULL;
    uint8_t *p = r->out; *n = r->nout;
    r->out = NULL; r->nout = r->cap = 0;
    return p;
}
void rec_close(const void *owner, int id) { Rec *r = rec_get(owner, id); if (r) { rec_free(r); g_rec[id] = NULL; } }
void rec_close_owner(const void *owner) { for (int i = 0; i < REC_MAX; i++) if (g_rec[i] && g_rec[i]->owner == owner) rec_close(owner, i); }
