/* Microphone capture for getUserMedia (mono float at 48 kHz; SDL recording, AAudio on Android) and
   MediaRecorder encoding: Opus (libopus) in WebM or Ogg and AAC in fragmented MP4, muxed by FFmpeg,
   with VP8/VP9 or H.264 video from a live source (camera, screen). Everything here runs on the JS
   thread, except the Android permission answer and each recorder's video encoder thread. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "media/media.h"
#include "paint/paint.h"
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#include <jni.h>
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <math.h>
#include <opus/opus.h>
#include <stdio.h>
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
#define AMIC_CAP 262144   /* ring samples (5.5 s), a power of two: a busy page may poll late */
/* fed: callback frames, dropped: frames lost to a full ring, last: SDL_GetTicks of the last callback */
typedef struct { AAudioStream *st; SDL_AtomicInt w, r, lost, fed, dropped, last; uint64_t opened; float ring[AMIC_CAP]; } MicDev;
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
    SDL_AddAtomicInt(&m->fed, n);
    SDL_SetAtomicInt(&m->last, (int)SDL_GetTicks());
    if ((unsigned)n > room) { SDL_AddAtomicInt(&m->dropped, n - (int32_t)room); n = (int32_t)room; }   /* the reader fell behind: drop the newest */
    for (int32_t i = 0; i < n; i++) m->ring[(w + i) & (AMIC_CAP - 1)] = in[i];
    SDL_MemoryBarrierRelease();
    SDL_SetAtomicInt(&m->w, (int)(w + n));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void mic_error(AAudioStream *st, void *ud, aaudio_result_t e) { (void)st; SDL_Log("lumen: microphone stream error %d", (int)e); SDL_SetAtomicInt(&((MicDev *)ud)->lost, 1); }
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
    m->opened = SDL_GetTicks();
    if (r == AAUDIO_OK) SDL_Log("lumen: microphone opened (%s, burst %d, capacity %d, sharing %d, performance %d)", voice ? "voice" : "camcorder",
        AAudioStream_getFramesPerBurst(m->st), AAudioStream_getBufferCapacityInFrames(m->st), AAudioStream_getSharingMode(m->st), AAudioStream_getPerformanceMode(m->st));
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
bool mic_permission_request(void) {
#ifdef __ANDROID__
    mic_check_granted();
    int p = SDL_GetAtomicInt(&g_mic_perm);
    if (p == 2) return false;
    if (p == 1) return true;
    SDL_SetAtomicInt(&g_mic_perm, 1);
    if (SDL_RequestAndroidPermission("android.permission.RECORD_AUDIO", mic_permission_answer, NULL)) return true;
    SDL_SetAtomicInt(&g_mic_perm, -1);
#endif
    return false;
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
/* LUMEN_MIC_STATS (debug builds): every 2 s, frames captured, read and dropped, and the level read,
   to tell a stalled or starved device from a slow reader; no audio leaves the process */
static struct { uint64_t next; int fed, dropped, read; double sq; float peak; } g_mic_stats[MIC_MAX];
static void mic_stats(int id, uint64_t now) {
#ifdef NDEBUG
    (void)id; (void)now;
#else
    if (!g_mic_stats[id].next) { g_mic_stats[id].next = now + 2000; return; }
    if (now < g_mic_stats[id].next) return;
    MicDev *m = g_mic[id].s;
    int fed = SDL_GetAtomicInt(&m->fed), dropped = SDL_GetAtomicInt(&m->dropped), read = g_mic_stats[id].read;
    SDL_Log("lumen: mic stats id=%d fed=%d dropped=%d read=%d rms=%.4f peak=%.3f ring=%d", id, fed - g_mic_stats[id].fed, dropped - g_mic_stats[id].dropped,
        read, read ? sqrt(g_mic_stats[id].sq / read) : 0.0, g_mic_stats[id].peak, dev_available(m));
    g_mic_stats[id].fed = fed; g_mic_stats[id].dropped = dropped; g_mic_stats[id].read = 0; g_mic_stats[id].sq = 0; g_mic_stats[id].peak = 0;
    g_mic_stats[id].next = now + 2000;
#endif
}
static void mic_stats_read(int id, const float *p, int n) {
#ifndef NDEBUG
    double sq = 0; float peak = g_mic_stats[id].peak;
    for (int i = 0; i < n; i++) { sq += (double)p[i] * p[i]; float a = fabsf(p[i]); if (a > peak) peak = a; }
    g_mic_stats[id].sq += sq; g_mic_stats[id].peak = peak; g_mic_stats[id].read += n;
#else
    (void)id; (void)p; (void)n;
#endif
}
/* a stream Android disconnected (a route change) is replaced, retried twice a second until it opens */
static void mic_heal(int id) {
    uint64_t now = SDL_GetTicks();
    MicDev *m = g_mic[id].s;
    if (g_mic[id].state == 1 && m) mic_stats(id, now);
    /* a stream whose callbacks stopped without an error (a route or mode change on some devices) is lost too */
    if (m && !SDL_GetAtomicInt(&m->lost) && now > m->opened + 1000 && (uint32_t)now - (uint32_t)SDL_GetAtomicInt(&m->last) > 600 && AAudioStream_getState(m->st) != AAUDIO_STREAM_STATE_STARTING) {
        SDL_Log("lumen: microphone stalled (state %d), reopening", (int)AAudioStream_getState(m->st));
        SDL_SetAtomicInt(&m->lost, 1);
    }
    if (g_mic[id].state != 1 || (m && !SDL_GetAtomicInt(&m->lost))) return;
    if (now < g_mic[id].retry) return;
    if (g_mic[id].s) {
        /* the counters move to the new stream, so the stats keep adding up */
        g_mic_stats[id].fed -= SDL_GetAtomicInt(&g_mic[id].s->fed); g_mic_stats[id].dropped -= SDL_GetAtomicInt(&g_mic[id].s->dropped);
        mic_release(g_mic[id].s, false);
    }
    g_mic[id].s = dev_open(g_mic[id].voice);
    g_mic[id].retry = now + 500;
    SDL_Log("lumen: microphone %s", g_mic[id].s ? "reopened" : "lost, retrying");
}
#else
static void mic_heal(int id) { (void)id; }
static void mic_stats_read(int id, const float *p, int n) { (void)id; (void)p; (void)n; }
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
    int n = g_mic[id].s ? dev_read(g_mic[id].s, out, max) : 0;
    mic_stats_read(id, out, n);
    return n;
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
#ifdef __ANDROID__
    memset(&g_mic_stats[id], 0, sizeof g_mic_stats[id]);
#endif
    mic_release(s, call);
}
void mic_close_owner(const void *owner) { for (int i = 0; i < MIC_MAX; i++) if (g_mic[i].owner == owner) mic_close(owner, i); }
bool mic_active(void) { for (int i = 0; i < MIC_MAX; i++) if (g_mic[i].state == 1) return true; return false; }

/* ---------------- MediaRecorder ---------------- */
#define REC_MAX 8
typedef struct {
    const void *owner;
    AVFormatContext *fc; AVCodecContext *cc; AVStream *st; AVFrame *fr; AVPacket *pkt;
    OpusEncoder *oe; int skip;     /* Opus: libopus (FFmpeg's own encoder fell silent after about 4 s), its lookahead */
    float *pend; int npend, fs;    /* PCM waiting for a whole codec frame of fs samples */
    int64_t pts;
    int vcodec;                    /* VC_* */
    char mime[80];                 /* the type with the codecs it records */
    uint8_t *out; size_t nout, cap;   /* muxed bytes not yet taken */
    bool done;
    /* video: the main thread converts the source's frames to NV12 (rec_tick) and leaves the newest in
       vnext for the encoder thread; mu guards vnext, the muxer and out */
    SDL_Mutex *mu; SDL_Condition *cv; SDL_Thread *th;
    AVCodecContext *vc; AVStream *vs; AVPacket *vpkt; struct SwsContext *sws;
    AVFrame *vnext; bool vstop, paused;
    int vsrc; uint64_t vseen;
    Uint64 t0, pause_ns, paused_at, vlast_ns, interval_ns; int64_t vpts;
} Rec;
static Rec *g_rec[REC_MAX];

/* MediaRecorder formats, as Chrome takes them: a container (webm, x-matroska, mp4, ogg) and the codecs
   its mime type names, each defaulting to the container's usual one. WebM with H.264, HEVC or PCM is
   written as Matroska, like Chrome's (FFmpeg's WebM muxer refuses them). */
enum { RF_WEBM = 1, RF_OGG, RF_MP4, RF_MKV };
enum { VC_NONE, VC_VP8, VC_VP9, VC_AV1, VC_H264, VC_HEVC, VC_MPEG4 };
enum { AC_NONE, AC_OPUS, AC_AAC, AC_PCM };
typedef struct { int fmt; bool video; int vc, ac; } RecFmt;   /* vc, ac: 0 when the mime type names none */

static bool rec_parse(const char *mime, RecFmt *f) {
    memset(f, 0, sizeof *f);
    if (!mime) return false;
    while (*mime == ' ') mime++;
    size_t n = strcspn(mime, ";");
    while (n && mime[n - 1] == ' ') n--;
    static const struct { const char *t; int fmt; bool video; } types[] = {
        { "video/webm", RF_WEBM, true }, { "audio/webm", RF_WEBM, false }, { "video/mp4", RF_MP4, true }, { "audio/mp4", RF_MP4, false },
        { "video/x-matroska", RF_MKV, true }, { "audio/x-matroska", RF_MKV, false }, { "audio/ogg", RF_OGG, false },
    };
    for (size_t i = 0; i < sizeof types / sizeof *types && !f->fmt; i++)
        if (strlen(types[i].t) == n && !strncasecmp(mime, types[i].t, n)) { f->fmt = types[i].fmt; f->video = types[i].video; }
    if (!f->fmt) return false;
    const char *c = strcasestr(mime + n, "codecs=");
    if (c) {
        c += 7;
        if (*c == '"' || *c == '\'') c++;
        while (*c && *c != ';' && *c != '"' && *c != '\'') {
            while (*c == ' ' || *c == ',') c++;
            size_t k = strcspn(c, ",;\"' ");
            if (!k) break;
            char tok[32];
            if (k >= sizeof tok) return false;
            memcpy(tok, c, k); tok[k] = 0; c += k;
            while (*c == ' ') c++;
            int v = VC_NONE, a = AC_NONE;
            if (!strcasecmp(tok, "vp8") || !strncasecmp(tok, "vp08", 4) || !strcasecmp(tok, "vp8.0")) v = VC_VP8;
            else if (!strcasecmp(tok, "vp9") || !strncasecmp(tok, "vp09", 4) || !strcasecmp(tok, "vp9.0")) v = VC_VP9;
            else if (!strcasecmp(tok, "av1") || !strncasecmp(tok, "av01", 4)) v = VC_AV1;
            else if (!strncasecmp(tok, "avc1", 4) || !strncasecmp(tok, "avc3", 4) || !strcasecmp(tok, "h264")) v = VC_H264;
            else if (!strncasecmp(tok, "hvc1", 4) || !strncasecmp(tok, "hev1", 4) || !strcasecmp(tok, "h265") || !strcasecmp(tok, "hevc")) v = VC_HEVC;
            else if (!strcasecmp(tok, "opus")) a = AC_OPUS;
            else if (!strncasecmp(tok, "mp4a", 4) || !strcasecmp(tok, "aac")) a = AC_AAC;
            else if (!strcasecmp(tok, "pcm")) a = AC_PCM;
            else return false;
            if (v && (!f->video || f->vc)) return false;
            if (a && f->ac) return false;
            if (v) f->vc = v; else f->ac = a;
        }
    }
    switch (f->fmt) {
    case RF_WEBM: return f->vc != VC_HEVC && (f->ac == AC_NONE || f->ac == AC_OPUS || f->ac == AC_PCM);
    case RF_MP4: return f->vc != VC_VP8 && f->ac != AC_PCM;
    case RF_OGG: return f->ac == AC_NONE || f->ac == AC_OPUS;
    default: return true;
    }
}
/* the video codecs to try when the mime type names none, best first */
static const int *rec_vdefaults(int fmt) {
    static const int webm[] = { VC_VP8, VC_VP9, VC_H264, 0 }, mp4[] = { VC_H264, VC_HEVC, VC_MPEG4, 0 }, mkv[] = { VC_H264, VC_VP8, VC_VP9, 0 };
    return fmt == RF_MP4 ? mp4 : fmt == RF_MKV ? mkv : webm;
}
static int rec_adefault(int fmt) { return fmt == RF_MP4 ? AC_AAC : AC_OPUS; }
/* each video codec's encoders, best first (hardware, then software) */
static const char *const *rec_vnames(int vc) {
    static const char *const vp8[] = { "vp8_mediacodec", "libvpx", NULL }, *const vp9[] = { "vp9_mediacodec", "libvpx-vp9", NULL };
    static const char *const av1[] = { "av1_mediacodec", "libsvtav1", "libaom-av1", NULL }, *const avc[] = { "h264_mediacodec", "libx264", "libopenh264", NULL };
    static const char *const hevc[] = { "hevc_mediacodec", "libx265", NULL }, *const mpeg4[] = { "mpeg4", NULL }, *const none[] = { NULL };
    switch (vc) {
    case VC_VP8: return vp8;
    case VC_VP9: return vp9;
    case VC_AV1: return av1;
    case VC_H264: return avc;
    case VC_HEVC: return hevc;
    case VC_MPEG4: return mpeg4;
    default: return none;
    }
}
static AVCodecContext *rec_venc(const AVCodec *codec, const char *name, int w, int h, int fps, int bps, bool global_header) {
    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) return NULL;
    c->width = w; c->height = h;   /* NV12 for the hardware encoders and x264, I420 for the others */
    c->pix_fmt = strstr(name, "mediacodec") || !strcmp(name, "libx264") ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    c->time_base = (AVRational){ 1, 1000 }; c->framerate = (AVRational){ fps, 1 };
    c->gop_size = fps * 2; c->max_b_frames = 0;
    c->bit_rate = bps > 0 ? (bps < 100000 ? 100000 : bps > 20000000 ? 20000000 : bps) : 2500000;
    c->color_range = AVCOL_RANGE_MPEG; c->colorspace = AVCOL_SPC_BT470BG;
    if (global_header) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary *o = NULL;   /* real time for the software encoders; each takes only its own option values */
    if (!strncmp(name, "libvpx", 6)) { av_dict_set(&o, "deadline", "realtime", 0); av_dict_set(&o, "cpu-used", "8", 0); av_dict_set(&o, "lag-in-frames", "0", 0); }
    else if (!strcmp(name, "libx264")) { av_dict_set(&o, "preset", "ultrafast", 0); av_dict_set(&o, "tune", "zerolatency", 0); }
    else if (!strcmp(name, "libsvtav1")) av_dict_set(&o, "preset", "12", 0);
    else if (!strcmp(name, "libaom-av1")) { av_dict_set(&o, "cpu-used", "8", 0); av_dict_set(&o, "usage", "realtime", 0); }
    int e = avcodec_open2(c, codec, &o);
    av_dict_free(&o);
    if (e < 0) avcodec_free_context(&c);
    return c;
}
/* whether an encoder works here: a hardware one is built in but the device may have no such codec, so
   it is opened once, small, and the answer kept */
static bool rec_encoder_works(const char *name) {
    static struct { const char *name; int ok; } seen[24];
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (!codec) return false;
    if (!strstr(name, "mediacodec")) return true;
    for (size_t i = 0; i < sizeof seen / sizeof *seen; i++) {
        if (seen[i].name == name) return seen[i].ok > 0;
        if (!seen[i].name) {
            AVCodecContext *c = rec_venc(codec, name, 320, 240, 30, 500000, false);
            seen[i].name = name; seen[i].ok = c ? 1 : -1;
            avcodec_free_context(&c);
            return seen[i].ok > 0;
        }
    }
    return false;
}
static bool rec_vcodec_works(int vc) { for (const char *const *n = rec_vnames(vc); *n; n++) if (rec_encoder_works(*n)) return true; return false; }
static bool rec_acodec_works(int ac) {
    return ac == AC_OPUS || (ac == AC_AAC && avcodec_find_encoder(AV_CODEC_ID_AAC)) || (ac == AC_PCM && avcodec_find_encoder(AV_CODEC_ID_PCM_F32LE));
}

int rec_kind(const char *mime) {
    if (!mime || !*mime) return 1;
    RecFmt f;
    if (!rec_parse(mime, &f)) return 0;
    if (f.vc && !rec_vcodec_works(f.vc)) return 0;
    if (f.ac && !rec_acodec_works(f.ac)) return 0;
    return f.fmt;
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
/* one packet to the muxer: interleaved when there is video, as the encoder thread writes too */
static void rec_mux(Rec *r, AVPacket *pkt) {
    SDL_LockMutex(r->mu);
    if (r->vs) av_interleaved_write_frame(r->fc, pkt); else av_write_frame(r->fc, pkt);
    avio_flush(r->fc->pb);
    SDL_UnlockMutex(r->mu);
}
static void rec_vdrain(Rec *r) {
    while (avcodec_receive_packet(r->vc, r->vpkt) == 0) {
        av_packet_rescale_ts(r->vpkt, r->vc->time_base, r->vs->time_base);
        r->vpkt->stream_index = r->vs->index;
        rec_mux(r, r->vpkt);
        av_packet_unref(r->vpkt);
    }
}
static int rec_vthread(void *o) {
    Rec *r = o;
    for (;;) {
        SDL_LockMutex(r->mu);
        while (!r->vnext && !r->vstop) SDL_WaitCondition(r->cv, r->mu);
        AVFrame *f = r->vnext; r->vnext = NULL;
        SDL_UnlockMutex(r->mu);
        if (!f) break;
        if (avcodec_send_frame(r->vc, f) == 0) rec_vdrain(r);
        av_frame_free(&f);
    }
    if (avcodec_send_frame(r->vc, NULL) == 0) rec_vdrain(r);
    return 0;
}
static void rec_vjoin(Rec *r) {
    if (!r->th) return;
    SDL_LockMutex(r->mu); r->vstop = true; SDL_SignalCondition(r->cv); SDL_UnlockMutex(r->mu);
    SDL_WaitThread(r->th, NULL); r->th = NULL;
}
static void rec_free(Rec *r) {
    if (!r) return;
    rec_vjoin(r);
    avcodec_free_context(&r->vc); av_packet_free(&r->vpkt); sws_freeContext(r->sws); av_frame_free(&r->vnext);
    if (r->cv) SDL_DestroyCondition(r->cv);
    if (r->mu) SDL_DestroyMutex(r->mu);
    if (r->fc) { if (r->fc->pb) { av_freep(&r->fc->pb->buffer); avio_context_free(&r->fc->pb); } avformat_free_context(r->fc); }
    if (r->oe) opus_encoder_destroy(r->oe);
    avcodec_free_context(&r->cc); av_frame_free(&r->fr); av_packet_free(&r->pkt);
    free(r->pend); free(r->out); free(r);
}
/* the mime type a recorder reports, with its codecs as Chrome names them */
static void rec_name(Rec *r, const RecFmt *f, int ac) {
    static const char *const vn[] = { "", "vp8", "vp9", "av1", "avc1", "hvc1", "mp4v" }, *const an[] = { "", "opus", "mp4a.40.2", "pcm" };
    const char *type = f->fmt == RF_MKV ? "x-matroska" : f->fmt == RF_OGG ? "ogg" : f->fmt == RF_MP4 ? "mp4" : "webm";
    const char *v = r->vs ? vn[r->vcodec] : "", *a = ac ? an[ac] : "";
    snprintf(r->mime, sizeof r->mime, "%s/%s;codecs=%s%s%s", r->vs ? "video" : "audio", type, v, *v && *a ? "," : "", a);
}
/* the first video codec of the list whose encoder opens; its codec in vc */
static bool rec_open_video(Rec *r, const int *vcs, int bps, int w, int h, int fps) {
    /* hardware encoders want 16-aligned planes; the long side at most 1920 */
    if (w <= 0 || h <= 0) { w = 640; h = 480; }
    double k = (w > h ? w : h) > 1920 ? 1920.0 / (w > h ? w : h) : 1.0;
    w = (int)(w * k) & ~15; h = (int)(h * k) & ~15;
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    fps = fps > 0 ? (fps > 60 ? 60 : fps) : 30;
    for (; *vcs; vcs++) for (const char *const *n = rec_vnames(*vcs); *n; n++) {
        const AVCodec *codec = avcodec_find_encoder_by_name(*n);
        AVCodecContext *c = codec ? rec_venc(codec, *n, w, h, fps, bps, r->fc->oformat->flags & AVFMT_GLOBALHEADER) : NULL;
        if (!c) continue;
        r->vc = c; r->vcodec = *vcs;
        r->interval_ns = SDL_NS_PER_SECOND / (Uint64)fps;
        SDL_Log("lumen: MediaRecorder video %s %dx%d@%d", *n, w, h, fps);
        return true;
    }
    return false;
}
int rec_open(const void *owner, const char *mime, int bps, int vbps, int vsrc, int vw, int vh, int vfps, bool audio) {
    RecFmt f;
    int slot = -1;
    for (int i = 0; i < REC_MAX && slot < 0; i++) if (!g_rec[i]) slot = i;
    if (!mime || !*mime) mime = vsrc > 0 ? "video/webm" : "audio/webm";
    if (!rec_kind(mime) || !rec_parse(mime, &f) || slot < 0) return -1;
    if (!f.video) vsrc = 0;   /* an audio type records the audio only */
    if (!audio && vsrc <= 0) return -1;
    int ac = audio ? (f.ac ? f.ac : rec_adefault(f.fmt)) : AC_NONE;
    if (ac && !rec_acodec_works(ac)) return -1;
    Rec *r = calloc(1, sizeof *r); if (!r) return -1;
    r->owner = owner;
    if (!(r->mu = SDL_CreateMutex()) || !(r->cv = SDL_CreateCondition())) { rec_free(r); return -1; }
    bool mkv = f.fmt == RF_MKV || (f.fmt == RF_WEBM && (f.vc == VC_H264 || f.vc == VC_HEVC || ac == AC_PCM));
    const char *fmt = mkv ? "matroska" : f.fmt == RF_WEBM ? "webm" : f.fmt == RF_OGG ? "ogg" : "mp4";
    const AVCodec *codec = ac == AC_AAC ? avcodec_find_encoder(AV_CODEC_ID_AAC) : ac == AC_PCM ? avcodec_find_encoder(AV_CODEC_ID_PCM_F32LE) : NULL;
    if (avformat_alloc_output_context2(&r->fc, NULL, fmt, NULL) < 0 || !r->fc) goto fail;
    uint8_t *iobuf = av_malloc(4096);
    if (!iobuf) goto fail;
    r->fc->pb = avio_alloc_context(iobuf, 4096, 1, r, NULL, rec_write, NULL);
    if (!r->fc->pb) { av_free(iobuf); goto fail; }
    r->fc->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_FLUSH_PACKETS;
    if (vsrc > 0) {
        const int named[] = { f.vc, 0 };
        if (!rec_open_video(r, f.vc ? named : rec_vdefaults(f.fmt), vbps, vw, vh, vfps) || !(r->vs = avformat_new_stream(r->fc, NULL)) || !(r->vpkt = av_packet_alloc())) goto fail;
        if (avcodec_parameters_from_context(r->vs->codecpar, r->vc) < 0) goto fail;
        if (r->vcodec == VC_HEVC && f.fmt == RF_MP4) r->vs->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');   /* as Chrome and Safari play it */
        r->vs->time_base = r->vc->time_base;
        r->vsrc = vsrc;
    }
    if (!ac) {
    } else if (codec) {
        r->cc = avcodec_alloc_context3(codec);
        if (!r->cc) goto fail;
        r->cc->sample_rate = MIC_RATE; r->cc->sample_fmt = ac == AC_PCM ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP;   /* the same layout for mono */
        av_channel_layout_default(&r->cc->ch_layout, 1);
        r->cc->bit_rate = ac == AC_PCM ? 0 : bps > 0 ? bps : 64000;
        r->cc->time_base = (AVRational){ 1, MIC_RATE };
        if (r->fc->oformat->flags & AVFMT_GLOBALHEADER) r->cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(r->cc, codec, NULL) < 0) goto fail;
        r->fs = r->cc->frame_size > 0 ? r->cc->frame_size : 960;
    } else {
        int e = 0;
        r->oe = opus_encoder_create(MIC_RATE, 1, OPUS_APPLICATION_AUDIO, &e);
        if (!r->oe || e != OPUS_OK) goto fail;
        opus_encoder_ctl(r->oe, OPUS_SET_BITRATE(bps > 0 ? (bps < 6000 ? 6000 : bps > 256000 ? 256000 : bps) : 64000));
        opus_encoder_ctl(r->oe, OPUS_SET_COMPLEXITY(5));   /* half of libopus' default cost, still clean for speech */
        opus_encoder_ctl(r->oe, OPUS_GET_LOOKAHEAD(&r->skip));
        r->fs = 960;   /* 20 ms */
    }
    if (ac && !(r->st = avformat_new_stream(r->fc, NULL))) goto fail;
    if (!ac) {
    } else if (r->cc) { if (avcodec_parameters_from_context(r->st->codecpar, r->cc) < 0) goto fail; }
    else {
        /* the OpusHead the containers carry: version 1, mono, pre-skip, 48 kHz input, no gain, mapping 0 */
        AVCodecParameters *cp = r->st->codecpar;
        cp->codec_type = AVMEDIA_TYPE_AUDIO; cp->codec_id = AV_CODEC_ID_OPUS; cp->sample_rate = MIC_RATE;
        av_channel_layout_default(&cp->ch_layout, 1);
        cp->initial_padding = r->skip; cp->seek_preroll = 3840; cp->frame_size = r->fs;
        if (!(cp->extradata = av_mallocz(19 + AV_INPUT_BUFFER_PADDING_SIZE))) goto fail;
        uint8_t *h = cp->extradata; cp->extradata_size = 19;
        memcpy(h, "OpusHead", 8); h[8] = 1; h[9] = 1;
        h[10] = (uint8_t)r->skip; h[11] = (uint8_t)(r->skip >> 8);
        h[12] = MIC_RATE & 0xff; h[13] = (MIC_RATE >> 8) & 0xff; h[14] = (MIC_RATE >> 16) & 0xff; h[15] = 0;
    }
    if (r->st) r->st->time_base = (AVRational){ 1, MIC_RATE };
    AVDictionary *opt = NULL;
    if (f.fmt == RF_WEBM || f.fmt == RF_MKV) { av_dict_set(&opt, "live", "1", 0); av_dict_set(&opt, "cluster_time_limit", "1000", 0); }
    if (f.fmt == RF_MP4) { av_dict_set(&opt, "movflags", r->vs ? "frag_keyframe+empty_moov+default_base_moof" : "empty_moov+default_base_moof", 0); av_dict_set(&opt, "frag_duration", "1000000", 0); }
    int hr = avformat_write_header(r->fc, &opt);
    av_dict_free(&opt);
    if (hr < 0) goto fail;
    avio_flush(r->fc->pb);
    r->fr = av_frame_alloc(); r->pkt = av_packet_alloc();
    r->pend = malloc(sizeof(float) * (size_t)(r->fs > 0 ? r->fs : 1));
    if (!r->fr || !r->pkt || !r->pend) goto fail;
    rec_name(r, &f, ac);
    if (r->vs) {
        r->t0 = SDL_GetTicksNS(); r->vpts = -1;
        if (!(r->th = SDL_CreateThread(rec_vthread, "lumen-recvideo", r))) goto fail;
    }
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
        rec_mux(r, r->pkt);
        av_packet_unref(r->pkt);
    }
}
static void rec_frame(Rec *r, int n) {
    int fs = r->fs;
    if (n < fs) memset(r->pend + n, 0, sizeof(float) * (size_t)(fs - n));
    if (r->oe) {
        uint8_t buf[1500];
        int k = opus_encode_float(r->oe, r->pend, fs, buf, (opus_int32)sizeof buf);
        if (k > 0 && av_new_packet(r->pkt, k) == 0) {
            memcpy(r->pkt->data, buf, (size_t)k);
            r->pkt->pts = r->pkt->dts = r->pts - r->skip; r->pkt->duration = fs;
            av_packet_rescale_ts(r->pkt, (AVRational){ 1, MIC_RATE }, r->st->time_base);
            r->pkt->stream_index = r->st->index;
            rec_mux(r, r->pkt);
            av_packet_unref(r->pkt);
        }
        r->pts += fs;
        return;
    }
    AVFrame *f = r->fr;
    av_frame_unref(f);
    f->nb_samples = fs; f->format = r->cc->sample_fmt; f->sample_rate = MIC_RATE;
    av_channel_layout_copy(&f->ch_layout, &r->cc->ch_layout);
    if (av_frame_get_buffer(f, 0) < 0) return;
    memcpy(f->data[0], r->pend, sizeof(float) * (size_t)fs);
    f->pts = r->pts; r->pts += fs;
    if (avcodec_send_frame(r->cc, f) == 0) rec_drain(r);
}
void rec_push(const void *owner, int id, const float *pcm, int frames) {
    Rec *r = rec_get(owner, id); if (!r || r->done || !r->st) return;
    int fs = r->fs;
    while (frames > 0) {
        int k = fs - r->npend < frames ? fs - r->npend : frames;
        memcpy(r->pend + r->npend, pcm, sizeof(float) * (size_t)k);
        r->npend += k; pcm += k; frames -= k;
        if (r->npend == fs) { rec_frame(r, fs); r->npend = 0; }
    }
}
void rec_finish(const void *owner, int id) {
    Rec *r = rec_get(owner, id); if (!r || r->done) return;
    rec_vjoin(r);   /* the encoder thread flushes its last frames */
    if (!r->st) {
    } else if (r->npend) { rec_frame(r, r->npend); r->npend = 0; }
    if (!r->st) {
    } else if (r->oe) rec_frame(r, 0);   /* the lookahead's last samples */
    else if (avcodec_send_frame(r->cc, NULL) == 0) rec_drain(r);
    av_write_trailer(r->fc);
    avio_flush(r->fc->pb);
    r->done = true;
}
uint8_t *rec_take(const void *owner, int id, size_t *n) {
    Rec *r = rec_get(owner, id); *n = 0;
    if (!r) return NULL;
    SDL_LockMutex(r->mu);
    uint8_t *p = r->nout ? r->out : NULL; *n = r->nout;
    if (p) { r->out = NULL; r->nout = r->cap = 0; }
    SDL_UnlockMutex(r->mu);
    return p;
}
void rec_pause(const void *owner, int id, bool paused) {
    Rec *r = rec_get(owner, id); if (!r || r->paused == paused) return;
    Uint64 now = SDL_GetTicksNS();
    if (paused) r->paused_at = now; else r->pause_ns += now - r->paused_at;
    r->paused = paused;
}

/* the source's frame as an NV12 (or I420) frame of the encoder's size */
static AVFrame *rec_convert(Rec *r, const Image *im) {
    AVFrame *f = av_frame_alloc();
    if (!f) return NULL;
    f->format = r->vc->pix_fmt; f->width = r->vc->width; f->height = r->vc->height;
    f->color_range = AVCOL_RANGE_MPEG; f->colorspace = AVCOL_SPC_BT470BG;
    if (av_frame_get_buffer(f, 0) < 0) { av_frame_free(&f); return NULL; }
    const uint8_t *src[4] = { 0 }; int ls[4] = { 0 }; enum AVPixelFormat sf;
    if (im->yuv) { src[0] = im->yuv; src[1] = im->yuv + (size_t)im->w * im->h; ls[0] = im->w; ls[1] = (im->w + 1) / 2 * 2; sf = AV_PIX_FMT_NV12; }
    else if (im->px) { src[0] = (const uint8_t *)im->px; ls[0] = im->w * 4; sf = AV_PIX_FMT_BGRA; }
    else { av_frame_free(&f); return NULL; }
    if (sf == f->format && im->w == f->width && im->h == f->height) {
        av_image_copy2(f->data, f->linesize, (uint8_t *const *)src, ls, sf, f->width, f->height);
        return f;
    }
    r->sws = sws_getCachedContext(r->sws, im->w, im->h, sf, f->width, f->height, (enum AVPixelFormat)f->format, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!r->sws) { av_frame_free(&f); return NULL; }
    sws_scale(r->sws, src, ls, 0, im->h, f->data, f->linesize);
    return f;
}
/* main thread, from media_tick: each video recorder takes its source's newest frame at most fps times a
   second, and repeats the last one each second while the source is still (a static screen) */
void rec_tick(void) {
    Uint64 now = SDL_GetTicksNS();
    for (int i = 0; i < REC_MAX; i++) {
        Rec *r = g_rec[i];
        if (!r || !r->th || r->done || r->paused) continue;
        uint64_t serial; Image *im = live_frame(r->vsrc, &serial);
        if (!im) continue;
        bool fresh = serial != r->vseen;
        if (r->vlast_ns && now - r->vlast_ns < (fresh ? r->interval_ns * 4 / 5 : SDL_NS_PER_SECOND)) continue;
        AVFrame *f = rec_convert(r, im);
        if (!f) continue;
        int64_t pts = (int64_t)((now - r->t0 - r->pause_ns) / SDL_NS_PER_MS);
        if (pts <= r->vpts) pts = r->vpts + 1;
        f->pts = r->vpts = pts;
        r->vseen = serial; r->vlast_ns = now;
        SDL_LockMutex(r->mu);
        AVFrame *old = r->vnext; r->vnext = f;
        SDL_SignalCondition(r->cv);
        SDL_UnlockMutex(r->mu);
        av_frame_free(&old);   /* the encoder fell behind: keep the newest */
    }
}
int rec_timeout_ms(void) {
    int to = -1; Uint64 now = SDL_GetTicksNS();
    for (int i = 0; i < REC_MAX; i++) {
        Rec *r = g_rec[i];
        if (!r || !r->th || r->done || r->paused) continue;
        uint64_t serial; if (!live_frame(r->vsrc, &serial)) continue;
        Uint64 due = r->vlast_ns + (serial != r->vseen ? r->interval_ns * 4 / 5 : SDL_NS_PER_SECOND);
        int t = due > now ? (int)((due - now) / SDL_NS_PER_MS) + 1 : 0;
        if (to < 0 || t < to) to = t;
    }
    return to;
}
const char *rec_mime(const void *owner, int id) { Rec *r = rec_get(owner, id); return r ? r->mime : NULL; }
void rec_close(const void *owner, int id) { Rec *r = rec_get(owner, id); if (r) { rec_free(r); g_rec[id] = NULL; } }
void rec_close_owner(const void *owner) { for (int i = 0; i < REC_MAX; i++) if (g_rec[i] && g_rec[i]->owner == owner) rec_close(owner, i); }
