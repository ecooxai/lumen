/* getUserMedia capture: camera frames and microphone PCM through SDL3, plus the webm/opus encoder behind
 * MediaRecorder. Fake devices (LUMEN_FAKE_MEDIA=1, or no permission hook as in jsrun) give a moving test
 * pattern and a 440 Hz tone, like Chrome's --use-fake-device-for-media-stream. All calls are main-thread. */
#include "media/media.h"
#include "paint/paint.h"
#include "base/util.h"
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP_MAX 32
#define MIC_RATE 48000
typedef struct Cap {
    int kind;                       /* 0 free, CAP_AUDIO, CAP_VIDEO */
    const void *owner;
    bool fake;
    int state;                      /* 0 waiting for OS permission, 1 live, -1 denied or lost */
    SDL_Camera *cam; SDL_AudioStream *mic;
    double t0, last; uint64_t done; /* fake devices: start time, last frame, mic frames delivered */
    int w, h;
    Image *cur, *grave[3]; int ngrave;
    Node *el[8]; int nel;
    char label[128];
} Cap;
static Cap g_caps[CAP_MAX];
bool (*capture_permit)(const char *origin, int kinds);
int (*capture_permission)(const char *origin, int kind);

static bool fake_media(void) { const char *e = getenv("LUMEN_FAKE_MEDIA"); return e && *e ? *e != '0' : !capture_permit; }
static double cap_now(void) { return (double)SDL_GetTicksNS() / 1e6; }
static Cap *get(const void *owner, int id) { return id >= 0 && id < CAP_MAX && g_caps[id].kind && g_caps[id].owner == owner ? &g_caps[id] : NULL; }
static bool sub(Uint32 f) { return (SDL_WasInit(f) & f) || SDL_InitSubSystem(f); }

bool cap_permit(const char *origin, int kinds) { return capture_permit ? capture_permit(origin, kinds) : true; }
int cap_permission(const char *origin, int kind) { return capture_permission ? capture_permission(origin, kind) : 1; }

int cap_list(int kind, char *out, size_t n) {
    size_t o = 0; int cnt = 0; out[0] = 0;
#define ADD(s) do { int k = snprintf(out + o, n - o, "%s%s", cnt ? "\n" : "", s); if (k > 0 && (size_t)k < n - o) { o += (size_t)k; cnt++; } } while (0)
    if (fake_media()) { ADD(kind == CAP_VIDEO ? "Fake Camera" : "Fake Microphone"); return cnt; }
    if (kind == CAP_VIDEO) {
        if (!sub(SDL_INIT_CAMERA)) return 0;
        int c = 0; SDL_CameraID *ids = SDL_GetCameras(&c);
        for (int i = 0; i < c; i++) { const char *nm = SDL_GetCameraName(ids[i]); ADD(nm && *nm ? nm : "Camera"); }
        SDL_free(ids);
    } else {
        if (!sub(SDL_INIT_AUDIO)) return 0;
        int c = 0; SDL_AudioDeviceID *ids = SDL_GetAudioRecordingDevices(&c);
        for (int i = 0; i < c; i++) { const char *nm = SDL_GetAudioDeviceName(ids[i]); ADD(nm && *nm ? nm : "Microphone"); }
        SDL_free(ids);
    }
#undef ADD
    return cnt;
}

static SDL_CameraSpec *pick_spec(SDL_CameraID id, int w, int h, SDL_CameraSpec *out) {
    int c = 0; SDL_CameraSpec **fs = SDL_GetCameraSupportedFormats(id, &c);
    if (!fs || !c) { SDL_free(fs); return NULL; }
    int best = -1; double bs = 1e18;
    for (int i = 0; i < c; i++) {   /* closest size to the request, prefer >= 15 fps */
        double d = fabs((double)fs[i]->width - w) + fabs((double)fs[i]->height - h);
        double fps = fs[i]->framerate_denominator ? (double)fs[i]->framerate_numerator / fs[i]->framerate_denominator : 30;
        if (fps < 15) d += 1e6;
        if (d < bs) { bs = d; best = i; }
    }
    *out = *fs[best]; SDL_free(fs);
    return out;
}

int cap_open(const void *owner, int kind, int dev, int w, int h) {
    int id = -1;
    for (int i = 0; i < CAP_MAX; i++) if (!g_caps[i].kind) { id = i; break; }
    if (id < 0) return -1;
    Cap *c = &g_caps[id]; memset(c, 0, sizeof *c);
    c->owner = owner; c->fake = fake_media(); c->t0 = cap_now();
    c->w = w > 0 ? w : 640; c->h = h > 0 ? h : 480;
    if (c->fake) {
        snprintf(c->label, sizeof c->label, "%s", kind == CAP_VIDEO ? "Fake Camera" : "Fake Microphone");
        c->state = 1; c->kind = kind; return id;
    }
    if (kind == CAP_VIDEO) {
        if (!sub(SDL_INIT_CAMERA)) return -1;
        int n = 0; SDL_CameraID *ids = SDL_GetCameras(&n);
        if (!ids || n == 0) { SDL_free(ids); return -1; }
        SDL_CameraID cid = ids[dev >= 0 && dev < n ? dev : 0]; SDL_free(ids);
        SDL_CameraSpec sp, *spec = pick_spec(cid, c->w, c->h, &sp);
        c->cam = SDL_OpenCamera(cid, spec);
        if (!c->cam) { fprintf(stderr, "lumen: camera: %s\n", SDL_GetError()); return -1; }
        const char *nm = SDL_GetCameraName(cid); snprintf(c->label, sizeof c->label, "%s", nm && *nm ? nm : "Camera");
        SDL_CameraSpec got; if (SDL_GetCameraFormat(c->cam, &got)) { c->w = got.width; c->h = got.height; }
        c->state = SDL_GetCameraPermissionState(c->cam) > 0 ? 1 : 0;
    } else {
        if (!sub(SDL_INIT_AUDIO)) return -1;
        int n = 0; SDL_AudioDeviceID *ids = SDL_GetAudioRecordingDevices(&n);
        if (!ids || n == 0) { SDL_free(ids); return -1; }
        SDL_AudioDeviceID did = dev >= 0 && dev < n ? ids[dev] : SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
        const char *nm = SDL_GetAudioDeviceName(dev >= 0 && dev < n ? ids[dev] : ids[0]);
        snprintf(c->label, sizeof c->label, "%s", nm && *nm ? nm : "Microphone");
        SDL_free(ids);
        SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, MIC_RATE };
        c->mic = SDL_OpenAudioDeviceStream(did, &spec, NULL, NULL);
        if (!c->mic) { fprintf(stderr, "lumen: microphone: %s\n", SDL_GetError()); return -1; }
        SDL_ResumeAudioStreamDevice(c->mic);
        c->state = 1;
    }
    c->kind = kind;
    return id;
}

static void present(Cap *c, Image *im) {
    if (c->cur) { if (c->ngrave == 3) { image_unref(c->grave[0]); memmove(c->grave, c->grave + 1, sizeof c->grave[0] * 2); c->ngrave--; } c->grave[c->ngrave++] = c->cur; }
    c->cur = im;
}
void cap_close(const void *owner, int id) {
    Cap *c = get(owner, id); if (!c) return;
    if (c->cam) SDL_CloseCamera(c->cam);
    if (c->mic) SDL_DestroyAudioStream(c->mic);
    if (c->cur) image_unref(c->cur);
    for (int i = 0; i < c->ngrave; i++) image_unref(c->grave[i]);
    memset(c, 0, sizeof *c);
}
void cap_close_owner(const void *owner) { for (int i = 0; i < CAP_MAX; i++) if (g_caps[i].kind && g_caps[i].owner == owner) cap_close(owner, i); }
int cap_state(const void *owner, int id, int *w, int *h, const char **label) {
    Cap *c = get(owner, id); if (!c) return -1;
    if (w) *w = c->w; if (h) *h = c->h; if (label) *label = c->label;
    return c->state;
}
int cap_read(const void *owner, int id, float *out, int max) {
    Cap *c = get(owner, id); if (!c || c->kind != CAP_AUDIO || c->state != 1) return 0;
    if (c->fake) {
        uint64_t due = (uint64_t)((cap_now() - c->t0) * MIC_RATE / 1000);
        int n = due > c->done ? (int)(due - c->done) : 0; if (n > max) { c->done = due - (uint64_t)max; n = max; }
        for (int i = 0; i < n; i++) out[i] = 0.3f * sinf((float)(2 * M_PI * 440.0 * (double)(c->done + (uint64_t)i) / MIC_RATE));
        c->done += (uint64_t)n;
        return n;
    }
    int got = SDL_GetAudioStreamData(c->mic, out, max * 4);
    return got > 0 ? got / 4 : 0;
}
void cap_attach(const void *owner, int id, Node *el) {
    cap_detach(el);
    Cap *c = get(owner, id); if (!c || c->kind != CAP_VIDEO || c->nel == 8) return;
    c->el[c->nel++] = el;
}
void cap_detach(Node *el) {
    for (int i = 0; i < CAP_MAX; i++) for (int k = 0; k < g_caps[i].nel; k++)
        if (g_caps[i].el[k] == el) { memmove(g_caps[i].el + k, g_caps[i].el + k + 1, sizeof(Node *) * (size_t)(g_caps[i].nel - k - 1)); g_caps[i].nel--; k--; }
}

static Image *fake_frame(Cap *c) {
    static const uint32_t bars[8] = { 0xffc0c0c0, 0xffc0c000, 0xff00c0c0, 0xff00c000, 0xffc000c0, 0xffc00000, 0xff0000c0, 0xff101010 };
    int w = c->w, h = c->h;
    Image *im = xcalloc(1, sizeof *im); im->w = w; im->h = h; im->refs = 1; im->px = xmalloc((size_t)w * (size_t)h * 4);
    double t = (cap_now() - c->t0) / 1000;
    int bx = (int)fmod(t * 160, (double)(w + h / 4)) - h / 4, by = h / 2 - h / 8, bs = h / 4;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t p = bars[x * 8 / w];
        if (x >= bx && x < bx + bs && y >= by && y < by + bs) p = 0xffffffff;
        im->px[(size_t)y * (size_t)w + (size_t)x] = p;
    }
    return im;
}
static Image *surface_image(SDL_Surface *f) {
    SDL_Surface *s = f->format == SDL_PIXELFORMAT_ARGB8888 ? f : SDL_ConvertSurface(f, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    Image *im = xcalloc(1, sizeof *im); im->w = s->w; im->h = s->h; im->refs = 1; im->px = xmalloc((size_t)s->w * (size_t)s->h * 4);
    for (int y = 0; y < s->h; y++) memcpy(im->px + (size_t)y * (size_t)s->w, (const uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch, (size_t)s->w * 4);
    for (size_t i = 0, n = (size_t)s->w * (size_t)s->h; i < n; i++) im->px[i] |= 0xff000000u;
    if (s != f) SDL_DestroySurface(s);
    return im;
}
int cap_tick(void) {
    int out = 0;
    for (int i = 0; i < CAP_MAX; i++) {
        Cap *c = &g_caps[i];
        if (c->kind == CAP_AUDIO && c->mic && SDL_GetAudioStreamAvailable(c->mic) > MIC_RATE * 4 * 2) {   /* nobody reading: keep 1 s */
            float junk[4096]; int extra = SDL_GetAudioStreamAvailable(c->mic) - MIC_RATE * 4;
            while (extra > 0) { int k = SDL_GetAudioStreamData(c->mic, junk, extra < (int)sizeof junk ? extra : (int)sizeof junk); if (k <= 0) break; extra -= k; }
        }
        if (c->kind != CAP_VIDEO) continue;
        if (c->cam && c->state == 0) { int p = SDL_GetCameraPermissionState(c->cam); if (p) c->state = p > 0 ? 1 : -1; }
        if (c->state != 1 || !c->nel) continue;
        Image *im = NULL;
        if (c->fake) { if (cap_now() - c->last >= 33) { c->last = cap_now(); im = fake_frame(c); } }
        else {
            SDL_Surface *f, *keep = NULL; Uint64 ts;
            while ((f = SDL_AcquireCameraFrame(c->cam, &ts))) { if (keep) SDL_ReleaseCameraFrame(c->cam, keep); keep = f; }
            if (keep) { im = surface_image(keep); SDL_ReleaseCameraFrame(c->cam, keep); }
        }
        if (!im) continue;
        int ow = c->cur ? c->cur->w : 0, oh = c->cur ? c->cur->h : 0;
        present(c, im); c->w = im->w; c->h = im->h;
        out |= 1; if (im->w != ow || im->h != oh) out |= 2;
    }
    return out;
}
int cap_timeout_ms(void) {
    for (int i = 0; i < CAP_MAX; i++) if (g_caps[i].kind == CAP_VIDEO && g_caps[i].nel && g_caps[i].state >= 0) return g_caps[i].state ? 15 : 100;
    return -1;
}
Image *cap_frame_for(Node *el) {
    for (int i = 0; i < CAP_MAX; i++) for (int k = 0; k < g_caps[i].nel; k++) if (g_caps[i].el[k] == el) return g_caps[i].cur;
    return NULL;
}
const void *cap_owner_of(const Image *im) {
    for (int i = 0; i < CAP_MAX; i++) if (g_caps[i].kind && g_caps[i].cur == im) return &g_caps[i];
    return NULL;
}
Image *cap_owner_frame(const void *owner) {
    const Cap *c = owner; return c >= g_caps && c < g_caps + CAP_MAX ? c->cur : NULL;
}
bool cap_is(const void *owner) { const Cap *c = owner; return c >= g_caps && c < g_caps + CAP_MAX; }

/* ---------------- MediaRecorder: mono float 48 kHz -> Opus in WebM ---------------- */
#define REC_MAX 16
typedef struct Rec {
    const void *owner;
    AVFormatContext *fc; AVCodecContext *cc; AVStream *st; AVFrame *fr; AVPacket *pk;
    uint8_t *buf; size_t n, cap;
    float *pend; int npend; int64_t pts;
} Rec;
static Rec *g_rec[REC_MAX];
static int rec_write(void *op, const uint8_t *b, int n) {
    Rec *r = op;
    if (r->n + (size_t)n > r->cap) { r->cap = (r->n + (size_t)n) * 2; r->buf = xrealloc(r->buf, r->cap); }
    memcpy(r->buf + r->n, b, (size_t)n); r->n += (size_t)n;
    return n;
}
static void rec_free(Rec *r) {
    if (r->fc) { if (r->fc->pb) { av_freep(&r->fc->pb->buffer); avio_context_free(&r->fc->pb); } avformat_free_context(r->fc); }
    avcodec_free_context(&r->cc); av_frame_free(&r->fr); av_packet_free(&r->pk);
    free(r->buf); free(r->pend); free(r);
}
int rec_new(const void *owner, int bitrate) {
    int id = -1;
    for (int i = 0; i < REC_MAX; i++) if (!g_rec[i]) { id = i; break; }
    if (id < 0) return -1;
    const AVCodec *codec = avcodec_find_encoder_by_name("libopus");
    if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    if (!codec) return -1;
    Rec *r = xcalloc(1, sizeof *r); r->owner = owner;
    if (avformat_alloc_output_context2(&r->fc, NULL, "webm", NULL) < 0 || !r->fc) { rec_free(r); return -1; }
    r->cc = avcodec_alloc_context3(codec);
    r->cc->sample_rate = MIC_RATE; r->cc->sample_fmt = AV_SAMPLE_FMT_FLT; r->cc->bit_rate = bitrate > 0 ? bitrate : 64000;
    av_channel_layout_default(&r->cc->ch_layout, 1);
    r->cc->time_base = (AVRational){ 1, MIC_RATE };
    if (r->fc->oformat->flags & AVFMT_GLOBALHEADER) r->cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(r->cc, codec, NULL) < 0) { rec_free(r); return -1; }
    r->st = avformat_new_stream(r->fc, NULL);
    avcodec_parameters_from_context(r->st->codecpar, r->cc);
    r->st->time_base = r->cc->time_base;
    r->fc->pb = avio_alloc_context(av_malloc(4096), 4096, 1, r, NULL, rec_write, NULL);
    r->fc->pb->seekable = 0;
    AVDictionary *o = NULL; av_dict_set(&o, "live", "1", 0);
    int ok = avformat_write_header(r->fc, &o); av_dict_free(&o);
    if (ok < 0) { rec_free(r); return -1; }
    r->fr = av_frame_alloc(); r->pk = av_packet_alloc();
    r->pend = xcalloc((size_t)r->cc->frame_size, sizeof(float));
    g_rec[id] = r;
    return id;
}
static void rec_drain(Rec *r) {
    while (avcodec_receive_packet(r->cc, r->pk) == 0) {
        av_packet_rescale_ts(r->pk, r->cc->time_base, r->st->time_base); r->pk->stream_index = 0;
        av_write_frame(r->fc, r->pk);
    }
}
static void rec_send(Rec *r) {
    AVFrame *f = r->fr; av_frame_unref(f);
    f->nb_samples = r->cc->frame_size; f->format = AV_SAMPLE_FMT_FLT; f->sample_rate = MIC_RATE;
    av_channel_layout_copy(&f->ch_layout, &r->cc->ch_layout);
    if (av_frame_get_buffer(f, 0) < 0) return;
    memcpy(f->data[0], r->pend, sizeof(float) * (size_t)r->cc->frame_size);
    f->pts = r->pts; r->pts += r->cc->frame_size;
    if (avcodec_send_frame(r->cc, f) == 0) rec_drain(r);
    r->npend = 0;
}
void rec_push(const void *owner, int id, const float *s, int n) {
    Rec *r = id >= 0 && id < REC_MAX && g_rec[id] && g_rec[id]->owner == owner ? g_rec[id] : NULL; if (!r) return;
    while (n > 0) {
        int k = r->cc->frame_size - r->npend; if (k > n) k = n;
        memcpy(r->pend + r->npend, s, sizeof(float) * (size_t)k); r->npend += k; s += k; n -= k;
        if (r->npend == r->cc->frame_size) rec_send(r);
    }
}
/* bytes muxed so far (caller frees); fin = flush the encoder, finish the file and free the recorder */
uint8_t *rec_take(const void *owner, int id, bool fin, size_t *n) {
    Rec *r = id >= 0 && id < REC_MAX && g_rec[id] && g_rec[id]->owner == owner ? g_rec[id] : NULL;
    *n = 0; if (!r) return NULL;
    if (fin) {
        if (r->npend) { memset(r->pend + r->npend, 0, sizeof(float) * (size_t)(r->cc->frame_size - r->npend)); rec_send(r); }
        avcodec_send_frame(r->cc, NULL); rec_drain(r);
        av_write_trailer(r->fc);
    } else av_write_frame(r->fc, NULL);
    avio_flush(r->fc->pb);
    uint8_t *out = r->buf; *n = r->n;
    r->buf = NULL; r->n = r->cap = 0;
    if (fin) { rec_free(r); g_rec[id] = NULL; }
    return out;
}
void rec_close_owner(const void *owner) {
    for (int i = 0; i < REC_MAX; i++) if (g_rec[i] && g_rec[i]->owner == owner) { rec_free(g_rec[i]); g_rec[i] = NULL; }
}
