/* Camera + microphone capture (SDL3: V4L2/PipeWire on Linux, AVFoundation on macOS) and per-origin permission prompts. */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../paint/cvbuf.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define LUMEN_REPLAYKIT 1
int ios_rp_start(void); void ios_rp_stop(void); bool ios_rp_push(void (*store)(const uint32_t *px, int w, int h, int stride));
#endif

typedef struct { SDL_Camera *cam; Node *node; Image img[2]; int cur, w, h, fps; SDL_Thread *th; SDL_AtomicInt run, ready; bool screen, rp; Uint64 last; unsigned seq; } Cam;
#define NCAM 8
static Cam g_cam[NCAM + 1];
static SDL_AudioStream *g_mic[NCAM + 1];

static bool cap_init(Uint32 f) { return (SDL_WasInit(f) & f) == f || SDL_InitSubSystem(f); }
int cap_cameras(char (*names)[128], int max) {
    if (!cap_init(SDL_INIT_CAMERA)) return 0;
    int n = 0; SDL_CameraID *ids = SDL_GetCameras(&n); if (!ids) return 0;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) { const char *s = SDL_GetCameraName(ids[i]); snprintf(names[i], 128, "%s", s ? s : "Camera"); }
    SDL_free(ids); return n;
}
static int cam_thread(void *p) {
    Cam *c = p;
    while (SDL_GetAtomicInt(&c->run)) {
        if (SDL_GetCameraPermissionState(c->cam) <= 0) { SDL_Delay(50); continue; }
        Uint64 ts; SDL_Surface *f = SDL_AcquireCameraFrame(c->cam, &ts);
        if (!f) { SDL_Delay(4); continue; }
        SDL_Surface *s = SDL_ConvertSurface(f, SDL_PIXELFORMAT_ARGB8888);
        SDL_ReleaseCameraFrame(c->cam, f);
        if (!s) continue;
        int b = c->cur ^ 1; Image *im = &c->img[b];
        if (im->w != s->w || im->h != s->h) { free(im->px); im->px = malloc((size_t)s->w * s->h * 4); im->w = s->w; im->h = s->h; im->refs = 1; im->scale = 1; }
        for (int y = 0; y < s->h; y++) { uint32_t *d = im->px + (size_t)y * s->w; const uint32_t *r = (const uint32_t *)((const uint8_t *)s->pixels + (size_t)y * s->pitch); for (int x = 0; x < s->w; x++) d[x] = r[x] | 0xff000000u; }
        c->w = s->w; c->h = s->h; SDL_DestroySurface(s);
        c->cur = b; c->seq++; SDL_SetAtomicInt(&c->ready, 1); cvbuf_changed = 1;
    }
    return 0;
}
int cap_cam_open(int idx, int w, int h, int fps) {
    if (!cap_init(SDL_INIT_CAMERA)) return 0;
    int n = 0; SDL_CameraID *ids = SDL_GetCameras(&n); if (!ids || idx < 0 || idx >= n) { SDL_free(ids); return 0; }
    SDL_CameraSpec spec = { 0 }, *want = NULL;
    if (w > 0 || h > 0 || fps > 0) { /* closest supported format */
        int nf = 0; SDL_CameraSpec **fs = SDL_GetCameraSupportedFormats(ids[idx], &nf); long best = -1;
        for (int i = 0; i < nf; i++) { long d = labs((long)fs[i]->width - (w > 0 ? w : 640)) + labs((long)fs[i]->height - (h > 0 ? h : 480));
            if (best < 0 || d < best) { best = d; spec = *fs[i]; want = &spec; } }
        SDL_free(fs);
    }
    int slot = 0; for (int i = 1; i <= NCAM; i++) if (!g_cam[i].cam && !g_cam[i].screen) { slot = i; break; }
    if (!slot) { SDL_free(ids); return 0; }
    Cam *c = &g_cam[slot]; memset(c, 0, sizeof *c);
    c->cam = SDL_OpenCamera(ids[idx], want); SDL_free(ids);
    if (!c->cam) { fprintf(stderr, "camera: %s\n", SDL_GetError()); return 0; }
    if (SDL_GetCameraPermissionState(c->cam) < 0) { SDL_CloseCamera(c->cam); c->cam = NULL; return -1; }
    SDL_CameraSpec cs; if (SDL_GetCameraFormat(c->cam, &cs)) { c->w = cs.width; c->h = cs.height; c->fps = cs.framerate_denominator ? cs.framerate_numerator / cs.framerate_denominator : 30; }
    SDL_SetAtomicInt(&c->run, 1); c->th = SDL_CreateThread(cam_thread, "camera", c);
    if (!c->th) { SDL_SetAtomicInt(&c->run, 0); SDL_CloseCamera(c->cam); memset(c, 0, sizeof *c); return 0; }
    return slot;
}
/* -2 unavailable, -1 OS denied, 0 OS pending, 1 authorized, 2 first frame ready.
 * JS polls without blocking the main event loop while an OS prompt is open. */
int cap_cam_status(int id) {
    if (id <= 0 || id > NCAM || !g_cam[id].cam) return -2;
    Cam *c = &g_cam[id]; int permission = SDL_GetCameraPermissionState(c->cam);
    if (permission <= 0) return permission < 0 ? -1 : 0;
    return SDL_GetAtomicInt(&c->ready) ? 2 : 1;
}
void cap_cam_info(int id, int *w, int *h, int *fps) { Cam *c = id > 0 && id <= NCAM ? &g_cam[id] : NULL; *w = c ? c->w : 0; *h = c ? c->h : 0; *fps = c ? c->fps : 0; }
void cap_cam_bind(int id, Node *n) {
    for (int i = 1; i <= NCAM; i++) if (g_cam[i].node == n) g_cam[i].node = NULL;
    if (id > 0 && id <= NCAM && (g_cam[id].cam || g_cam[id].screen)) g_cam[id].node = n;
    cvbuf_changed = 1;
}
void cap_cam_close(int id) {
    if (id <= 0 || id > NCAM || !(g_cam[id].cam || g_cam[id].screen)) return; Cam *c = &g_cam[id];
#ifdef LUMEN_REPLAYKIT
    if (c->rp) ios_rp_stop();
#endif
    if (c->cam) { SDL_SetAtomicInt(&c->run, 0); SDL_WaitThread(c->th, NULL); SDL_CloseCamera(c->cam); }
    free(c->img[0].px); free(c->img[1].px); memset(c, 0, sizeof *c); cvbuf_changed = 1;
}
Image *cap_frame_for(Node *n) {
    for (int i = 1; i <= NCAM; i++) if ((g_cam[i].cam || g_cam[i].screen) && g_cam[i].node == n && g_cam[i].img[g_cam[i].cur].px) return &g_cam[i].img[g_cam[i].cur];
    return NULL;
}

/* getDisplayMedia: a capture source fed with Lumen's own presented page pixels (same on macOS, iPadOS, Linux, Android) */
int cap_screen_open(void) {
    for (int i = 1; i <= NCAM; i++) if (!g_cam[i].cam && !g_cam[i].screen) { Cam *c = &g_cam[i]; memset(c, 0, sizeof *c); c->screen = true; c->fps = 30;
#ifdef LUMEN_REPLAYKIT
        c->rp = ios_rp_start();
#endif
        return i; }
    return 0;
}
bool cap_screen_active(void) { for (int i = 1; i <= NCAM; i++) if (g_cam[i].screen) return true; return false; }
static void screen_store(const uint32_t *px, int w, int h, int stride) {
    Uint64 now = SDL_GetTicks();
    for (int i = 1; i <= NCAM; i++) {
        Cam *c = &g_cam[i];
        if (!c->screen || w <= 0 || h <= 0 || (c->last && now - c->last < 1000u / (unsigned)c->fps)) continue;
        int b = c->cur ^ 1; Image *im = &c->img[b];
        if (im->w != w || im->h != h) { free(im->px); im->px = malloc((size_t)w * h * 4); im->w = w; im->h = h; im->refs = 1; im->scale = 1; }
        for (int y = 0; y < h; y++) memcpy(im->px + (size_t)y * w, px + (size_t)y * stride, (size_t)w * 4);
        c->w = w; c->h = h; c->cur = b; c->last = now; c->seq++;
        if (c->node) cvbuf_changed = 1;
    }
}
void cap_screen_push(const uint32_t *px, int w, int h, int stride) {
#ifdef LUMEN_REPLAYKIT
    for (int i = 1; i <= NCAM; i++) if (g_cam[i].rp && ios_rp_push(screen_store)) return;
#endif
    screen_store(px, w, h, stride);
}
int cap_mics(char (*names)[128], int max) {
    if (!cap_init(SDL_INIT_AUDIO)) return 0;
    int n = 0; SDL_AudioDeviceID *ids = SDL_GetAudioRecordingDevices(&n); if (!ids) return 0;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) { const char *s = SDL_GetAudioDeviceName(ids[i]); snprintf(names[i], 128, "%s", s ? s : "Microphone"); }
    SDL_free(ids); return n;
}
int cap_mic_open(int rate, int ch) {
    if (!cap_init(SDL_INIT_AUDIO)) return 0;
    int slot = 0; for (int i = 1; i <= NCAM; i++) if (!g_mic[i]) { slot = i; break; }
    if (!slot) return 0;
    SDL_AudioSpec sp = { SDL_AUDIO_F32, ch > 0 ? ch : 1, rate > 0 ? rate : 48000 };
    SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &sp, NULL, NULL);
    if (!s) { fprintf(stderr, "microphone: %s\n", SDL_GetError()); return 0; }
    SDL_ResumeAudioStreamDevice(s); g_mic[slot] = s; return slot;
}
int cap_mic_read(int id, float *out, int max) { if (id <= 0 || id > NCAM || !g_mic[id]) return -1; int n = SDL_GetAudioStreamData(g_mic[id], out, max * 4); return n > 0 ? n / 4 : 0; }
void cap_mic_close(int id) { if (id > 0 && id <= NCAM && g_mic[id]) { SDL_DestroyAudioStream(g_mic[id]); g_mic[id] = NULL; } }

/* MediaRecorder: encode a capture source (screen or camera) to fragmented MP4 in memory, like Safari's video/mp4 */
typedef struct { int src, w, h, fps, bps; bool paused; AVFormatContext *fc; AVCodecContext *cc; AVStream *st; struct SwsContext *sws; AVFrame *fr; AVPacket *pk;
    Uint64 t0, last, pause_t, paused_ms; unsigned seq; } Rec;
#define LCLAMPI(v, a, b) ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))
static Rec *g_rec[NCAM + 1];
static void rec_drain(Rec *r) {
    while (avcodec_receive_packet(r->cc, r->pk) == 0) {
        av_packet_rescale_ts(r->pk, r->cc->time_base, r->st->time_base); r->pk->stream_index = r->st->index;
        av_interleaved_write_frame(r->fc, r->pk);
    }
}
static void rec_free(Rec *r) {
    if (r->fc && r->fc->pb) { uint8_t *b; avio_close_dyn_buf(r->fc->pb, &b); av_free(b); r->fc->pb = NULL; }
    avformat_free_context(r->fc); avcodec_free_context(&r->cc); sws_freeContext(r->sws); av_frame_free(&r->fr); av_packet_free(&r->pk); free(r);
}
static bool rec_open(Rec *r, int w, int h) {
    r->w = w & ~1; r->h = h & ~1;
    static const char *const enc[] = { "h264_videotoolbox", "libx264", "h264_mediacodec", "mpeg4" };
    if (avformat_alloc_output_context2(&r->fc, NULL, "mp4", NULL) < 0 || avio_open_dyn_buf(&r->fc->pb) < 0) return false;
    for (size_t i = 0; i < sizeof enc / sizeof enc[0] && !r->cc; i++) {
        const AVCodec *co = avcodec_find_encoder_by_name(enc[i]); if (!co) continue;
        AVCodecContext *cc = avcodec_alloc_context3(co);
        cc->width = r->w; cc->height = r->h; cc->pix_fmt = AV_PIX_FMT_YUV420P; cc->time_base = (AVRational){ 1, 1000 }; cc->framerate = (AVRational){ r->fps, 1 };
        cc->gop_size = r->fps * 2; cc->max_b_frames = 0; cc->bit_rate = r->bps > 0 ? r->bps : LCLAMPI(r->w * r->h * r->fps / 8, 500000, 12000000);
        if (r->fc->oformat->flags & AVFMT_GLOBALHEADER) cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (!strcmp(enc[i], "h264_videotoolbox")) av_opt_set_int(cc->priv_data, "realtime", 1, 0);
        if (avcodec_open2(cc, co, NULL) < 0) { avcodec_free_context(&cc); continue; }
        r->cc = cc;
    }
    if (!r->cc) { fprintf(stderr, "recorder: no video encoder\n"); return false; }
    r->st = avformat_new_stream(r->fc, NULL); avcodec_parameters_from_context(r->st->codecpar, r->cc); r->st->time_base = r->cc->time_base;
    AVDictionary *o = NULL; av_dict_set(&o, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    int e = avformat_write_header(r->fc, &o); av_dict_free(&o);
    if (e < 0) { return false; }
    r->fr = av_frame_alloc(); r->fr->format = AV_PIX_FMT_YUV420P; r->fr->width = r->w; r->fr->height = r->h; av_frame_get_buffer(r->fr, 0);
    fprintf(stderr, "recorder: %dx%d@%d with %s\n", r->w, r->h, r->fps, r->cc->codec->name);
    return true;
}
int cap_rec_start(int src, int bps) {
    if (src <= 0 || src > NCAM || !(g_cam[src].cam || g_cam[src].screen)) return 0;
    int slot = 0; for (int i = 1; i <= NCAM; i++) if (!g_rec[i]) { slot = i; break; }
    if (!slot) return 0;
    Cam *c = &g_cam[src]; Rec *r = calloc(1, sizeof *r); r->src = src; r->fps = c->fps > 0 && c->fps <= 60 ? c->fps : 30;
    r->bps = bps;
    r->pk = av_packet_alloc(); r->t0 = SDL_GetTicks(); r->seq = c->seq - 1;
    if (c->w > 0 && !rec_open(r, c->w, c->h)) { rec_free(r); return 0; }
    g_rec[slot] = r; return slot;
}
void cap_rec_tick(void) {
    Uint64 now = SDL_GetTicks();
    for (int i = 1; i <= NCAM; i++) {
        Rec *r = g_rec[i]; if (!r || r->paused) continue;
        Cam *c = &g_cam[r->src]; Image *im = &c->img[c->cur];
        if (!(c->cam || c->screen) || !im->px) continue;
        if (!r->cc && !rec_open(r, im->w, im->h)) { g_rec[i] = NULL; rec_free(r); continue; }
        if (r->last && (now - r->last < 1000u / (unsigned)r->fps || (c->seq == r->seq && now - r->last < 1000))) continue;
        r->sws = sws_getCachedContext(r->sws, im->w, im->h, AV_PIX_FMT_BGRA, r->w, r->h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
        if (!r->sws || av_frame_make_writable(r->fr) < 0) continue;
        const uint8_t *sp[1] = { (const uint8_t *)im->px }; int sl[1] = { im->w * 4 };
        sws_scale(r->sws, sp, sl, 0, im->h, r->fr->data, r->fr->linesize);
        r->fr->pts = (int64_t)(now - r->t0 - r->paused_ms); r->last = now; r->seq = c->seq;
        if (avcodec_send_frame(r->cc, r->fr) == 0) rec_drain(r);
    }
}
void cap_rec_pause(int id, bool on) {
    Rec *r = id > 0 && id <= NCAM ? g_rec[id] : NULL; if (!r || r->paused == on) return;
    Uint64 now = SDL_GetTicks(); if (on) r->pause_t = now; else r->paused_ms += now - r->pause_t; r->paused = on;
}
uint8_t *cap_rec_stop(int id, int *len) {
    Rec *r = id > 0 && id <= NCAM ? g_rec[id] : NULL; *len = 0; if (!r) return NULL;
    g_rec[id] = NULL;
    if (!r->cc) { rec_free(r); return NULL; }
    avcodec_send_frame(r->cc, NULL); rec_drain(r); av_write_trailer(r->fc);
    uint8_t *b = NULL; int n = avio_close_dyn_buf(r->fc->pb, &b); r->fc->pb = NULL;
    const char *dump = getenv("LUMEN_REC_DUMP"); FILE *df = dump && n > 0 ? fopen(dump, "wb") : NULL; if (df) { fwrite(b, 1, (size_t)n, df); fclose(df); }
    uint8_t *out = n > 0 ? malloc((size_t)n) : NULL; if (out) { memcpy(out, b, (size_t)n); *len = n; }
    av_free(b); rec_free(r); return out;
}

/* permissions: LUMEN_PERMISSIONS=grant|deny overrides; otherwise ask once per origin+kind per session */
typedef struct { char key[300]; int v; } Perm;
static Perm g_perm[128]; static int g_nperm;
int cap_perm_state(const char *kind, const char *origin) {
    const char *e = getenv("LUMEN_PERMISSIONS");
    if (e && !strcmp(e, "grant")) return 1; if (e && !strcmp(e, "deny")) return -1;
    if (!strcmp(kind, "screen")) return 0; /* screen selection is never persisted */
    char k[300]; snprintf(k, sizeof k, "%s|%s", kind, origin);
    for (int i = 0; i < g_nperm; i++) if (!strcmp(g_perm[i].key, k)) return g_perm[i].v;
    return 0;
}
int cap_ask(const char *kind, const char *origin) {
    int s = cap_perm_state(kind, origin); if (s) return s > 0;
    char msg[512]; snprintf(msg, sizeof msg, "%s wants to use your %s.", origin, kind);
    const SDL_MessageBoxButtonData bt[] = { { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Block" }, { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Allow" } };
    SDL_MessageBoxData d = { SDL_MESSAGEBOX_INFORMATION, NULL, "Permission request", msg, 2, bt, NULL };
    int r = 0; if (!SDL_ShowMessageBox(&d, &r)) r = 0;
    if (strcmp(kind, "screen") && g_nperm < 128) { snprintf(g_perm[g_nperm].key, sizeof g_perm[0].key, "%s|%s", kind, origin); g_perm[g_nperm++].v = r ? 1 : -1; }
    return r == 1;
}
