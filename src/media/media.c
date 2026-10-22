/* <video>/<audio> playback. Each source (progressive file or MSE SourceBuffer) gets a decoder
 * thread that demuxes through a custom AVIOContext fed from in-memory segments. Video frames
 * queue as BGRA Images; audio is resampled to f32 stereo 48 kHz and pulled by an SDL3 audio
 * stream callback whose position is the master clock (wall clock when there is no audio). */
#include "media/media.h"
#include <time.h>
#include "paint/paint.h"
#include "base/util.h"
#include "net/net.h"
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include "gpu/webgl.h"
#include "gpu/webgpu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "base/url.h"
#ifdef __APPLE__
#include <CoreVideo/CoreVideo.h>
#endif

#define VQ_MAX 3
#define AQ_MAX_SEC 1.0
#define OUT_RATE 48000
#define IO_BUF 65536
#define MAX_W 1920

typedef struct Blob { int refs; uint8_t *p; size_t n; } Blob;
typedef struct { double t0, t1; uint8_t *p; size_t n; Blob *init; } Seg;
typedef struct { double pts; Image *im; int w, h; bool hidden; } VFrame;
typedef struct AChunk { double pts; float *s; int frames, off; struct AChunk *next; } AChunk;

typedef struct Stream {
    MediaPlayer *m;
    char *mime;
    bool progressive, webm;
    Blob *init;                            /* latest init segment, or the whole file when progressive */
    int fd; int64_t fsize;                 /* a progressive file:// source, read in place (fd -1 otherwise) */
    Seg *segs; int nsegs, capsegs;         /* sorted by t0 */
    uint8_t *pend; size_t npend, cappend;
    uint8_t *moof; size_t nmoof;
    struct { uint32_t id, timescale, defdur; } trk[8]; int ntrk;
    /* reader state, owned by the decoder thread (guarded by m->mu) */
    uint64_t rd_epoch; Blob *rd_init; bool rd_in_init; size_t rd_off;
    bool rd_have_seg; double rd_t0, rd_pos; bool reopen; double reopen_pos;
    bool opened, eof, has_video, has_audio;
    SDL_Thread *th;
} Stream;

struct MediaPlayer {
    SDL_Mutex *mu; SDL_Condition *cv;
    Node *node;
    Stream *st[4]; int nst;
    bool quit, eos;
    double duration;
    uint64_t epoch; double target; bool seeking;
    bool playing, ended, waiting, error; char err[160];
    bool expect_audio, audio_failed; SDL_AudioStream *dev;
    SDL_AudioDeviceID audio_device;       /* main-thread owned; closed while paused */
    float volume; bool muted;
    double aclock, base_t, base_wall;
    double audio_queued, audio_wall;       /* callback snapshot; never lock the SDL stream under mu */
    int w, h;
    VFrame vq[VQ_MAX]; int vqn;
    AChunk *ah, *at; double abuf;
    Image *cur, *grave[3]; int ngrave; double painted;
    uint64_t frame_serial, presented_frames, dropped_frames;
    double frame_pts, frame_wall; int frame_w, frame_h; bool frame_hidden;
    MpState notified_state; bool notified;
    float limit; double cpu_ms;            /* tab CPU limiter: max share of a core, decoder-thread CPU used */
    bool live; int live_src; uint64_t live_seen; Image *live_next;   /* pushed frames: a live source (camera, screen) or mp_push (canvas) */
};

void (*media_wakeup)(void);
static SDL_Mutex *g_mu;
static uint64_t g_frame_version;
uint64_t media_frame_version(void) { return g_frame_version; }
static uint64_t g_state_version;
uint64_t media_state_version(void) { return g_state_version; }
static MediaPlayer **g_pl; static int g_np, g_cap;
/* live sources (camera, screen capture): the newest frame of each, shared by the players showing it */
#define LIVE_MAX 16
static struct { bool used; Image *im; uint64_t serial; } g_src[LIVE_MAX];

static void wake(void) { if (media_wakeup) media_wakeup(); }
static double thread_cpu_ms(void) { struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6; }
double media_cpu_ms(struct Document *d) {
    double t = 0;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) { MediaPlayer *m = g_pl[i]; if (m->node && m->node->doc == d) { SDL_LockMutex(m->mu); t += m->cpu_ms; SDL_UnlockMutex(m->mu); } }
    SDL_UnlockMutex(g_mu);
    return t;
}
void media_set_limit(struct Document *d, float lim) {
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) { MediaPlayer *m = g_pl[i]; if (m->node && m->node->doc == d) { SDL_LockMutex(m->mu); m->limit = lim; SDL_UnlockMutex(m->mu); } }
    SDL_UnlockMutex(g_mu);
}
size_t media_mem_bytes(size_t *frames) {
    size_t seg = 0, fr = 0;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) {
        MediaPlayer *m = g_pl[i]; SDL_LockMutex(m->mu);
        for (int j = 0; j < m->nst; j++) { Stream *s = m->st[j]; for (int k = 0; k < s->nsegs; k++) seg += s->segs[k].n; seg += s->cappend; if (s->init) seg += s->init->n; }
        for (int k = 0; k < m->vqn; k++) if (m->vq[k].im) {
            const Image *im = m->vq[k].im;
            size_t pixels = (size_t)im->w * (size_t)im->h;
            if (im->px) fr += pixels * 4;
            if (im->yuv) fr += pixels + (size_t)((im->w + 1) / 2) * ((im->h + 1) / 2) * 2;
        }
        SDL_UnlockMutex(m->mu);
    }
    SDL_UnlockMutex(g_mu);
    if (frames) *frames = fr;
    return seg;
}
static void blob_unref(Blob *b) { if (b && --b->refs <= 0) { free(b->p); free(b); } }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) << 32 | rd32(p + 4); }
#define FCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* ---------------- fragmented MP4 box parsing ---------------- */
static bool box_at(const uint8_t *p, const uint8_t *e, uint64_t *sz, uint32_t *type, size_t *hdr) {
    if (e - p < 8) return false;
    uint64_t s = rd32(p); *type = rd32(p + 4); *hdr = 8;
    if (s == 1) { if (e - p < 16) return false; s = rd64(p + 8); *hdr = 16; }
    else if (s == 0) s = (uint64_t)(e - p);
    if (s < *hdr) { *sz = 0; return true; }
    *sz = s; return true;
}
static const uint8_t *child(const uint8_t *p, const uint8_t *e, uint32_t t, const uint8_t **ce) {
    uint64_t sz; uint32_t ty; size_t h;
    while (box_at(p, e, &sz, &ty, &h) && sz && sz <= (uint64_t)(e - p)) {
        if (ty == t) { *ce = p + sz; return p + h; }
        p += sz;
    }
    return NULL;
}
static void parse_moov(Stream *s, const uint8_t *p, const uint8_t *e) {
    uint64_t sz; uint32_t ty; size_t h;
    s->ntrk = 0;
    for (const uint8_t *b = p; box_at(b, e, &sz, &ty, &h) && sz && sz <= (uint64_t)(e - b); b += sz) {
        if (ty != FCC('t', 'r', 'a', 'k') || s->ntrk == 8) continue;
        const uint8_t *te = b + sz, *ke, *me, *de, *ee;
        const uint8_t *tk = child(b + h, te, FCC('t', 'k', 'h', 'd'), &ke);
        const uint8_t *md = child(b + h, te, FCC('m', 'd', 'i', 'a'), &me);
        const uint8_t *mh = md ? child(md, me, FCC('m', 'd', 'h', 'd'), &de) : NULL;
        if (!tk || !mh || ke - tk < 24 || de - mh < 24) continue;
        s->trk[s->ntrk].id = tk[0] == 1 ? rd32(tk + 20) : rd32(tk + 12);
        s->trk[s->ntrk].timescale = mh[0] == 1 ? rd32(mh + 20) : rd32(mh + 12);
        s->trk[s->ntrk].defdur = 0;
        (void)ee;
        s->ntrk++;
    }
    const uint8_t *mve, *mv = child(p, e, FCC('m', 'v', 'e', 'x'), &mve);
    for (const uint8_t *b = mv; b && box_at(b, mve, &sz, &ty, &h) && sz && sz <= (uint64_t)(mve - b); b += sz)
        if (ty == FCC('t', 'r', 'e', 'x') && sz >= h + 20)
            for (int i = 0; i < s->ntrk; i++) if (s->trk[i].id == rd32(b + h + 4)) s->trk[i].defdur = rd32(b + h + 12);
}
static bool parse_moof(Stream *s, const uint8_t *p, const uint8_t *e, double *t0, double *t1) {
    uint64_t sz; uint32_t ty; size_t h; bool any = false;
    for (const uint8_t *b = p; box_at(b, e, &sz, &ty, &h) && sz && sz <= (uint64_t)(e - b); b += sz) {
        if (ty != FCC('t', 'r', 'a', 'f')) continue;
        const uint8_t *fe = b + sz, *he, *de;
        const uint8_t *hd = child(b + h, fe, FCC('t', 'f', 'h', 'd'), &he);
        if (!hd || he - hd < 8) continue;
        uint32_t fl = rd32(hd) & 0xffffff, id = rd32(hd + 4), defdur = 0, ts = 0; const uint8_t *q = hd + 8;
        for (int i = 0; i < s->ntrk; i++) if (s->trk[i].id == id) { ts = s->trk[i].timescale; defdur = s->trk[i].defdur; }
        if (!ts) continue;
        if (fl & 1) q += 8;
        if (fl & 2) q += 4;
        if ((fl & 8) && q + 4 <= he) defdur = rd32(q);
        const uint8_t *dt = child(b + h, fe, FCC('t', 'f', 'd', 't'), &de);
        uint64_t base = dt ? (dt[0] == 1 ? rd64(dt + 4) : rd32(dt + 4)) : 0;
        uint64_t dur = 0;
        for (const uint8_t *r = b + h; box_at(r, fe, &sz, &ty, &h) && sz && sz <= (uint64_t)(fe - r); r += sz) {
            if (ty != FCC('t', 'r', 'u', 'n')) continue;
            const uint8_t *x = r + h, *xe = r + sz;
            if (xe - x < 8) continue;
            uint32_t tf = rd32(x) & 0xffffff, cnt = rd32(x + 4); x += 8;
            if (tf & 1) x += 4;
            if (tf & 4) x += 4;
            for (uint32_t i = 0; i < cnt && x <= xe; i++) {
                uint32_t d = defdur;
                if (tf & 0x100) { if (x + 4 > xe) break; d = rd32(x); x += 4; }
                if (tf & 0x200) x += 4;
                if (tf & 0x400) x += 4;
                if (tf & 0x800) x += 4;
                dur += d;
            }
        }
        double a = (double)base / ts, z = (double)(base + dur) / ts;
        if (!any || a < *t0) *t0 = a;
        if (!any || z > *t1) *t1 = z;
        any = true;
        h = 8;
    }
    return any;
}
static void add_seg(Stream *s, uint8_t *p, size_t n, double t0, double t1) {
    for (int i = 0; i < s->nsegs;) {   /* replace overlapping data */
        Seg *g = &s->segs[i];
        if (g->t0 < t1 - 1e-3 && g->t1 > t0 + 1e-3 && fabs(g->t0 - t0) < 0.5 * (t1 - t0) + 1e-3) {
            free(g->p); blob_unref(g->init);
            memmove(g, g + 1, sizeof *g * (size_t)(s->nsegs - i - 1)); s->nsegs--;
        } else i++;
    }
    if (s->nsegs == s->capsegs) { s->capsegs = s->capsegs ? s->capsegs * 2 : 32; s->segs = xrealloc(s->segs, sizeof *s->segs * (size_t)s->capsegs); }
    int k = s->nsegs; while (k > 0 && s->segs[k - 1].t0 > t0) k--;
    memmove(&s->segs[k + 1], &s->segs[k], sizeof *s->segs * (size_t)(s->nsegs - k));
    s->segs[k] = (Seg){ t0, t1, p, n, s->init }; if (s->init) s->init->refs++;
    s->nsegs++;
}

/* ---------------- AVIO reader ---------------- */
static Seg *rd_find(Stream *s) {
    if (s->rd_have_seg) for (int i = 0; i < s->nsegs; i++) if (fabs(s->segs[i].t0 - s->rd_t0) < 1e-6) return &s->segs[i];
    s->rd_have_seg = false; s->rd_off = 0;
    for (int i = 0; i < s->nsegs; i++) if (s->segs[i].t1 > s->rd_pos + 1e-3) return &s->segs[i];
    return NULL;
}
static int read_cb(void *opaque, uint8_t *buf, int size) {
    Stream *s = opaque; MediaPlayer *m = s->m; int out = AVERROR_EOF;
    if (s->fd >= 0) {   /* a local file: read straight from it, outside the lock */
        SDL_LockMutex(m->mu); bool stop = m->quit || s->rd_epoch != m->epoch; SDL_UnlockMutex(m->mu);
        if (stop) return AVERROR_EXIT;
        ssize_t k;
        do k = pread(s->fd, buf, (size_t)size, (off_t)s->rd_off); while (k < 0 && errno == EINTR);
        if (k <= 0) return AVERROR_EOF;
        s->rd_off += (size_t)k;
        return (int)k;
    }
    SDL_LockMutex(m->mu);
    for (;;) {
        if (m->quit || s->rd_epoch != m->epoch) { out = AVERROR_EXIT; break; }
        if (s->progressive) {
            if (!s->init) { SDL_WaitCondition(m->cv, m->mu); continue; }
            if (s->rd_off >= s->init->n) break;
            size_t k = s->init->n - s->rd_off; if (k > (size_t)size) k = (size_t)size;
            memcpy(buf, s->init->p + s->rd_off, k); s->rd_off += k; out = (int)k; break;
        }
        if (s->rd_in_init && s->rd_init) {
            if (s->rd_off < s->rd_init->n) {
                size_t k = s->rd_init->n - s->rd_off; if (k > (size_t)size) k = (size_t)size;
                memcpy(buf, s->rd_init->p + s->rd_off, k); s->rd_off += k; out = (int)k; break;
            }
            s->rd_in_init = false; s->rd_off = 0;
        }
        Seg *g = rd_find(s);
        if (!g) { if (m->eos) break; SDL_WaitCondition(m->cv, m->mu); continue; }
        if (!s->rd_init) { s->rd_init = g->init; if (g->init) g->init->refs++; s->rd_in_init = true; s->rd_off = 0; continue; }
        if (g->init != s->rd_init && !s->rd_have_seg) { s->reopen = true; s->reopen_pos = g->t0; break; }  /* new init: reopen demuxer */
        s->rd_have_seg = true; s->rd_t0 = g->t0;
        size_t k = g->n - s->rd_off; if (k > (size_t)size) k = (size_t)size;
        memcpy(buf, g->p + s->rd_off, k); s->rd_off += k; out = (int)k;
        if (s->rd_off >= g->n) { s->rd_pos = g->t1; s->rd_have_seg = false; s->rd_off = 0; }
        break;
    }
    SDL_UnlockMutex(m->mu);
    return out;
}
static int64_t seek_cb(void *opaque, int64_t off, int whence) {
    Stream *s = opaque; int64_t n = s->fd >= 0 ? s->fsize : s->init ? (int64_t)s->init->n : 0;
    if (whence == AVSEEK_SIZE) return n;
    whence &= ~AVSEEK_FORCE;
    int64_t pos = whence == SEEK_SET ? off : whence == SEEK_CUR ? (int64_t)s->rd_off + off : n + off;
    if (pos < 0 || pos > n) return -1;
    s->rd_off = (size_t)pos; return pos;
}

/* ---------------- decoder thread ---------------- */
static void set_error(MediaPlayer *m, const char *msg) {
    SDL_LockMutex(m->mu); m->error = true; snprintf(m->err, sizeof m->err, "%s", msg); SDL_UnlockMutex(m->mu); wake();
}
static Image *to_image(struct SwsContext **sws, AVFrame *f) {
    int ow = f->width, oh = f->height;
    if (ow > MAX_W) { oh = (int)((double)oh * MAX_W / ow); ow = MAX_W; }
    if (ow <= 0 || oh <= 0) return NULL;
    *sws = sws_getCachedContext(*sws, f->width, f->height, (enum AVPixelFormat)f->format, ow, oh, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!*sws) return NULL;
    Image *im = xcalloc(1, sizeof *im); im->w = ow; im->h = oh; im->refs = 1;
    im->px = xmalloc((size_t)ow * (size_t)oh * 4);
    uint8_t *dst[4] = { (uint8_t *)im->px }; int ls[4] = { ow * 4 };
    sws_scale(*sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, dst, ls);
    return im;
}
typedef struct { struct SwsContext *sws; SwrContext *swr; double skip; uint64_t epoch; } DecCtx;
bool media_lowmem;
bool media_yuv;
static Image *yuv_copy(int w, int h, const uint8_t *p0, size_t s0, const uint8_t *p1, size_t s1, int mat) {
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    Image *im = xcalloc(1, sizeof *im); im->w = w; im->h = h; im->refs = 1; im->yuv_mat = mat;
    im->yuv = xmalloc((size_t)w * (size_t)h + (size_t)cw * 2 * (size_t)ch);
    for (int y = 0; y < h; y++) memcpy(im->yuv + (size_t)y * (size_t)w, p0 + (size_t)y * s0, (size_t)w);
    uint8_t *uv = im->yuv + (size_t)w * (size_t)h;
    for (int y = 0; y < ch; y++) memcpy(uv + (size_t)y * (size_t)cw * 2, p1 + (size_t)y * s1, (size_t)cw * 2);
    return im;
}
static int yuv_mat(const AVFrame *f, bool full) { return (f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height >= 720) ? 1 : 0) | (full ? 2 : 0); }
#ifdef __APPLE__
static Image *vt_image(const AVFrame *f) {   /* copy the decoder surface's NV12 planes once; colour conversion happens on the GPU */
    CVPixelBufferRef pb = (CVPixelBufferRef)f->data[3];
    OSType ft = CVPixelBufferGetPixelFormatType(pb);
    if (ft != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange && ft != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) return NULL;
    if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return NULL;
    const uint8_t *p0 = CVPixelBufferGetBaseAddressOfPlane(pb, 0), *p1 = CVPixelBufferGetBaseAddressOfPlane(pb, 1);
    Image *im = NULL;
    if (p0 && p1 && (int)CVPixelBufferGetWidthOfPlane(pb, 0) >= f->width && (int)CVPixelBufferGetHeightOfPlane(pb, 0) >= f->height)
        im = yuv_copy(f->width, f->height, p0, CVPixelBufferGetBytesPerRowOfPlane(pb, 0), p1, CVPixelBufferGetBytesPerRowOfPlane(pb, 1), yuv_mat(f, ft == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange));
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    return im;
}
#endif
static Image *tiny_image(void) { Image *im = xcalloc(1, sizeof *im); im->w = im->h = 1; im->refs = 1; im->px = xcalloc(1, 4); return im; }
static bool emit_video(Stream *s, DecCtx *d, AVFrame *f, AVRational tb) {
    MediaPlayer *m = s->m;
    int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    double pts = ts == AV_NOPTS_VALUE ? 0 : ts * av_q2d(tb);
    if (pts < d->skip - 1e-3) return true;
    bool hidden = media_lowmem && (double)SDL_GetTicks() - m->painted > 2000;
    AVFrame *sw = NULL; Image *im = NULL;
#ifdef __APPLE__
    if (!hidden && media_yuv && f->format == AV_PIX_FMT_VIDEOTOOLBOX && f->width <= MAX_W) im = vt_image(f);
#endif
    if (!hidden && media_yuv && f->format == AV_PIX_FMT_NV12 && f->width <= MAX_W) im = yuv_copy(f->width, f->height, f->data[0], (size_t)f->linesize[0], f->data[1], (size_t)f->linesize[1], yuv_mat(f, f->color_range == AVCOL_RANGE_JPEG));
    if (!hidden && media_yuv && f->width <= MAX_W &&
        (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P) &&
        (f->colorspace == AVCOL_SPC_UNSPECIFIED || f->colorspace == AVCOL_SPC_BT709 ||
         f->colorspace == AVCOL_SPC_BT470BG || f->colorspace == AVCOL_SPC_SMPTE170M)) {
        const uint8_t *planes[3] = { f->data[0], f->data[1], f->data[2] };
        im = image_yuv420p(f->width, f->height, planes, f->linesize,
                          yuv_mat(f, f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P));
    }
    if (!im && !hidden && f->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        sw = av_frame_alloc();
        if (!sw || av_hwframe_transfer_data(sw, f, 0) < 0) { av_frame_free(&sw); return true; }
    }
    if (!im) im = hidden ? tiny_image() : to_image(&d->sws, sw ? sw : f);
    av_frame_free(&sw);
    if (!im) return true;
    SDL_LockMutex(m->mu);
    while (m->vqn == VQ_MAX && !m->quit && m->epoch == d->epoch) SDL_WaitCondition(m->cv, m->mu);
    bool ok = !m->quit && m->epoch == d->epoch;
    if (ok) { m->vq[m->vqn++] = (VFrame){ pts, im, f->width, f->height, hidden }; if (m->w != f->width || m->h != f->height) { m->w = f->width; m->h = f->height; } }
    SDL_UnlockMutex(m->mu);
    if (!ok) image_unref(im); else wake();
    return ok;
}
static bool emit_audio(Stream *s, DecCtx *d, AVFrame *f, AVRational tb) {
    MediaPlayer *m = s->m;
    int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    double pts = ts == AV_NOPTS_VALUE ? 0 : ts * av_q2d(tb);
    if (pts + (double)f->nb_samples / f->sample_rate < d->skip) return true;
    if (!d->swr) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&d->swr, &stereo, AV_SAMPLE_FMT_FLT, OUT_RATE, &f->ch_layout, (enum AVSampleFormat)f->format, f->sample_rate, 0, NULL) < 0 || swr_init(d->swr) < 0) return true;
    }
    int cap = swr_get_out_samples(d->swr, f->nb_samples);
    if (cap <= 0) return true;
    AChunk *c = xcalloc(1, sizeof *c); c->s = xmalloc(sizeof(float) * 2 * (size_t)cap);
    uint8_t *o = (uint8_t *)c->s;
    c->frames = swr_convert(d->swr, &o, cap, (const uint8_t **)f->extended_data, f->nb_samples);
    c->pts = pts;
    if (c->frames <= 0) { free(c->s); free(c); return true; }
    SDL_LockMutex(m->mu);
    while (m->abuf > AQ_MAX_SEC && !m->quit && m->epoch == d->epoch) SDL_WaitCondition(m->cv, m->mu);
    bool ok = !m->quit && m->epoch == d->epoch;
    if (ok) { if (m->at) m->at->next = c; else m->ah = c; m->at = c; m->abuf += (double)c->frames / OUT_RATE; }
    SDL_UnlockMutex(m->mu);
    if (!ok) { free(c->s); free(c); } else wake();
    return ok;
}
static enum AVPixelFormat pick_hw(AVCodecContext *cc, const enum AVPixelFormat *fmts) {
    (void)cc;
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) if (*p == AV_PIX_FMT_VIDEOTOOLBOX) { if (getenv("LUMEN_MEDIA_DEBUG")) fprintf(stderr, "lumen-media: using VideoToolbox for %s\n", cc->codec->name); return *p; }
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) { const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(*p); if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p; }
    return fmts[0];
}
static bool hwdec_enabled(void) { const char *e = getenv("LUMEN_HWDEC"); return !e || strcmp(e, "0") != 0; }
#ifdef __ANDROID__
/* the phone's hardware video decoder through FFmpeg's MediaCodec wrapper (NDK API, as no JVM is set);
   frames come back as NV12/I420 buffers like software ones. Software decoding stays the fallback,
   also for 10-bit streams (YouTube 1080p H.264 cost ~180% CPU in libavcodec on the MI 8) */
static AVCodecContext *open_mediacodec(AVStream *st) {
    const AVCodecParameters *p = st->codecpar;
    const char *name = p->codec_id == AV_CODEC_ID_H264 ? (p->profile == AV_PROFILE_H264_HIGH_10 || p->profile >= AV_PROFILE_H264_HIGH_422 ? NULL : "h264_mediacodec")
        : p->codec_id == AV_CODEC_ID_HEVC ? (p->profile == AV_PROFILE_HEVC_MAIN ? "hevc_mediacodec" : NULL)
        : p->codec_id == AV_CODEC_ID_VP9 ? (p->profile <= 0 ? "vp9_mediacodec" : NULL)
        : p->codec_id == AV_CODEC_ID_VP8 ? "vp8_mediacodec" : NULL;
    const AVCodec *codec = name ? avcodec_find_decoder_by_name(name) : NULL;
    if (!codec || !hwdec_enabled()) return NULL;
    AVCodecContext *cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, p) < 0) { avcodec_free_context(&cc); return NULL; }
    cc->pkt_timebase = st->time_base;
    if (avcodec_open2(cc, codec, NULL) < 0) { avcodec_free_context(&cc); return NULL; }
    static bool said;
    if (!said) { said = true; SDL_Log("lumen-media: hardware decoder %s %dx%d", name, p->width, p->height); }
    return cc;
}
#endif
static AVCodecContext *open_dec(AVStream *st) {
#ifdef __ANDROID__
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { AVCodecContext *hw = open_mediacodec(st); if (hw) return hw; }
#endif
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return NULL;
    AVCodecContext *cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0) { avcodec_free_context(&cc); return NULL; }
    { const char *dt = getenv("LUMEN_DEC_THREADS"); cc->thread_count = dt ? atoi(dt) : 4; } cc->pkt_timebase = st->time_base;
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && hwdec_enabled() &&
        !getenv("LUMEN_NO_HWDEC") && av_hwdevice_ctx_create(&cc->hw_device_ctx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0) == 0) {
        cc->get_format = pick_hw;
        if (getenv("LUMEN_MEDIA_DEBUG")) fprintf(stderr, "lumen-media: hwdec device ok for %s\n", codec->name);
        if (!getenv("LUMEN_DEC_THREADS")) cc->thread_count = 1;
    }
    if (avcodec_open2(cc, codec, NULL) < 0) { avcodec_free_context(&cc); return NULL; }
    return cc;
}
static bool decode_pkt(Stream *s, DecCtx *d, AVCodecContext *cc, AVPacket *pkt, AVFrame *f, bool video, AVRational tb) {
    /* a hardware decoder refuses input (EAGAIN) while its queue is full: take its frames and retry, for up
       to a second, rather than drop the packet */
    for (int tries = 0;; tries++) {
        int e = avcodec_send_packet(cc, pkt);
        if (e != AVERROR(EAGAIN) || tries >= 500) { if (e < 0 && pkt) return true; break; }
        bool got = false;
        while (avcodec_receive_frame(cc, f) == 0) {
            got = true;
            bool ok = video ? emit_video(s, d, f, tb) : emit_audio(s, d, f, tb);
            av_frame_unref(f);
            if (!ok) return false;
        }
        if (!got) SDL_Delay(2);
    }
    while (avcodec_receive_frame(cc, f) == 0) {
        bool ok = video ? emit_video(s, d, f, tb) : emit_audio(s, d, f, tb);
        av_frame_unref(f);
        if (!ok) return false;
    }
    return true;
}
/* returns 0: epoch change/quit, 1: eof, 2: reopen with a new init segment, -1: error */
static int run_demux(Stream *s, DecCtx *d, double start) {
    MediaPlayer *m = s->m; int ret = -1;
    AVFormatContext *fc = avformat_alloc_context();
    AVIOContext *io = avio_alloc_context(av_malloc(IO_BUF), IO_BUF, 0, s, read_cb, NULL, s->progressive ? seek_cb : NULL);
    AVCodecContext *vc = NULL, *ac = NULL; AVPacket *pkt = av_packet_alloc(); AVFrame *f = av_frame_alloc();
    fc->pb = io; fc->flags |= AVFMT_FLAG_CUSTOM_IO;
    const AVInputFormat *fmt = s->progressive ? NULL : av_find_input_format(s->webm ? "matroska" : "mp4");
    if (avformat_open_input(&fc, NULL, fmt, NULL) < 0) goto done;
    if (s->progressive && avformat_find_stream_info(fc, NULL) < 0) goto done;
    int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    int ai = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (vi >= 0 && !(vc = open_dec(fc->streams[vi]))) vi = -1;
    if (ai >= 0 && !(ac = open_dec(fc->streams[ai]))) ai = -1;
    if (vi < 0 && ai < 0) goto done;
    SDL_LockMutex(m->mu);
    s->has_video = vi >= 0; s->has_audio = ai >= 0; s->opened = true;
    if (s->has_audio && s->progressive) m->expect_audio = true;
    if (vi >= 0 && !m->w) { m->w = fc->streams[vi]->codecpar->width; m->h = fc->streams[vi]->codecpar->height; }
    if (s->progressive && isnan(m->duration) && fc->duration > 0) m->duration = (double)fc->duration / AV_TIME_BASE;
    SDL_UnlockMutex(m->mu); wake();
    if (s->progressive && start > 0) av_seek_frame(fc, -1, (int64_t)(start * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
    int r;
    ret = 0;
    double wc0 = (double)SDL_GetTicks(), cc0 = thread_cpu_ms(), cl = cc0;
    while ((r = av_read_frame(fc, pkt)) >= 0) {
        bool ok = true;
        if (pkt->stream_index == vi) ok = decode_pkt(s, d, vc, pkt, f, true, fc->streams[vi]->time_base);
        else if (pkt->stream_index == ai) ok = decode_pkt(s, d, ac, pkt, f, false, fc->streams[ai]->time_base);
        av_packet_unref(pkt);
        double cn = thread_cpu_ms(); SDL_LockMutex(m->mu); m->cpu_ms += cn - cl; float lim = m->limit; SDL_UnlockMutex(m->mu); cl = cn;
        if (lim > 0) {
            double wn = (double)SDL_GetTicks(), need = (cn - cc0) / lim - (wn - wc0);
            if (need > 1) SDL_Delay((Uint32)fmin(need, 200));
            if (wn - wc0 > 2000) { wc0 = (double)SDL_GetTicks(); cc0 = thread_cpu_ms(); }
        }
        if (!ok) goto done;
    }
    if (r == AVERROR_EXIT) goto done;
    if (vc && !decode_pkt(s, d, vc, NULL, f, true, fc->streams[vi]->time_base)) goto done;
    if (ac && !decode_pkt(s, d, ac, NULL, f, false, fc->streams[ai]->time_base)) goto done;
    SDL_LockMutex(m->mu); ret = s->reopen ? 2 : m->epoch != d->epoch || m->quit ? 0 : 1; SDL_UnlockMutex(m->mu);
done:
    if (ret == -1) { SDL_LockMutex(m->mu); if (m->quit || m->epoch != d->epoch) ret = 0; else if (s->reopen) ret = 2; SDL_UnlockMutex(m->mu); }
    av_frame_free(&f); av_packet_free(&pkt);
    avcodec_free_context(&vc); avcodec_free_context(&ac);
    avformat_close_input(&fc);
    if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    return ret;
}
static int dec_thread(void *arg) {
    Stream *s = arg; MediaPlayer *m = s->m;
    if (s->progressive && !s->init && !strncmp(s->mime, "file:///", 8)) {   /* a local file, however large: no copy in memory */
        const char *p = s->mime + 7;
        char *path = url_decode(p, strcspn(p, "?#"));
        int fd = open(path, O_RDONLY | O_CLOEXEC); struct stat st;
        free(path);
        if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode)) { if (fd >= 0) close(fd); set_error(m, "file not found"); return 0; }
        SDL_LockMutex(m->mu); s->fd = fd; s->fsize = (int64_t)st.st_size; SDL_BroadcastCondition(m->cv); SDL_UnlockMutex(m->mu);
    } else if (s->progressive && !s->init) {
        NetResponse *r = net_fetch_sync(net_request_new("GET", s->mime));
        if (!r || r->status < 200 || r->status >= 300 || !r->body) { set_error(m, "network error"); if (r) net_response_free(r); return 0; }
        Blob *b = xcalloc(1, sizeof *b); b->refs = 1; b->n = r->body_len; b->p = xmalloc(r->body_len); memcpy(b->p, r->body, r->body_len);
        net_response_free(r);
        SDL_LockMutex(m->mu); s->init = b; SDL_BroadcastCondition(m->cv); SDL_UnlockMutex(m->mu);
    }
    DecCtx d = { 0 }; double start = 0, skip = 0; int fails = 0;
    for (;;) {
        SDL_LockMutex(m->mu);
        if (m->quit) { SDL_UnlockMutex(m->mu); break; }
        if (s->rd_epoch != m->epoch) { start = skip = m->target; }
        s->rd_epoch = d.epoch = m->epoch; d.skip = skip;
        blob_unref(s->rd_init); s->rd_init = NULL; s->rd_in_init = true; s->rd_off = 0;
        s->rd_have_seg = false; s->rd_pos = start; s->reopen = false; s->eof = false;
        SDL_UnlockMutex(m->mu);
        if (d.swr) swr_free(&d.swr);
        int r = run_demux(s, &d, start);
        SDL_LockMutex(m->mu);
        /* A seek can arrive while run_demux tears down its codecs. Do not
           publish the old EOF or wait on the new epoch before decoding it. */
        if (d.epoch != m->epoch) r = 0;
        if (r == 2) { start = s->reopen_pos; skip = 0; }
        else if (r == 1 || r == -1) {
            if (r == -1 && ++fails > 3) { m->error = true; snprintf(m->err, sizeof m->err, "decode error"); }
            s->eof = true; SDL_BroadcastCondition(m->cv); wake();
            uint64_t ep = m->epoch;
            while (!m->quit && m->epoch == ep && !(r == -1 && !m->error)) SDL_WaitCondition(m->cv, m->mu);
            if (r == -1 && !m->error && m->epoch == ep) { start = s->rd_pos; skip = 0; }
        }
        SDL_UnlockMutex(m->mu);
    }
    sws_freeContext(d.sws); swr_free(&d.swr);
    return 0;
}

/* ---------------- audio output ---------------- */
static void SDLCALL audio_cb(void *ud, SDL_AudioStream *as, int additional, int total) {
    (void)total; MediaPlayer *m = ud; int frames = additional / 8;
    float buf[2048];
    SDL_LockMutex(m->mu);
    float g = m->muted ? 0 : m->volume;
    while (frames > 0) {
        int k = frames < 1024 ? frames : 1024, w = 0;
        memset(buf, 0, sizeof(float) * 2 * (size_t)k);
        if (m->playing && !m->seeking) while (w < k && m->ah) {
            AChunk *c = m->ah; int n = c->frames - c->off; if (n > k - w) n = k - w;
            for (int i = 0; i < n * 2; i++) buf[w * 2 + i] = c->s[c->off * 2 + i] * g;
            c->off += n; w += n; m->abuf -= (double)n / OUT_RATE;
            m->aclock = c->pts + (double)c->off / OUT_RATE;
            if (c->off >= c->frames) { m->ah = c->next; if (!m->ah) m->at = NULL; free(c->s); free(c); }
        }
        SDL_PutAudioStreamData(as, buf, k * 8);
        frames -= k;
    }
    /* SDL already owns the stream lock before invoking this callback. Readers
       must use this snapshot, not take that lock while holding the player lock. */
    m->audio_queued = fmax(0, (double)SDL_GetAudioStreamQueued(as)) / 8.0 / OUT_RATE;
    m->audio_wall = now_ms();
    SDL_BroadcastCondition(m->cv);
    SDL_UnlockMutex(m->mu);
}
static void open_audio(MediaPlayer *m) {
    if (m->dev || m->audio_failed) return;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) { m->audio_failed = true; return; }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUT_RATE };
    m->dev = SDL_CreateAudioStream(&spec, &spec);
    if (!m->dev) { m->audio_failed = true; return; }
    SDL_SetAudioStreamGetCallback(m->dev, audio_cb, m);
}
/* Main-thread only, outside the player mutex: SDL device/stream locks must never
   nest inside that mutex. Keep the stream and its queued samples across pause,
   but release the physical output rather than continuously generating silence. */
static void sync_audio_device(MediaPlayer *m, bool run) {
    if (!run && m->audio_device) {
        SDL_CloseAudioDevice(m->audio_device); m->audio_device = 0;
    } else if (run && m->dev && !m->audio_device) {
        SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUT_RATE };
        SDL_AudioDeviceID dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (!dev || !SDL_PauseAudioDevice(dev) || !SDL_BindAudioStream(dev, m->dev)) {
            if (dev) SDL_CloseAudioDevice(dev);
            SDL_AudioStream *stream = m->dev;
            SDL_LockMutex(m->mu); m->dev = NULL; m->audio_failed = true; SDL_UnlockMutex(m->mu);
            SDL_DestroyAudioStream(stream);
            return;
        }
        double queued = fmax(0, (double)SDL_GetAudioStreamQueued(m->dev)) / 8.0 / OUT_RATE;
        SDL_LockMutex(m->mu);
        m->audio_queued = queued; m->audio_wall = now_ms();
        SDL_UnlockMutex(m->mu);
        m->audio_device = dev;
        SDL_ResumeAudioDevice(dev);
    }
}

/* ---------------- Web Audio: pages push interleaved stereo float at OUT_RATE ---------------- */
#define WA_MAX 32
static struct { SDL_AudioStream *s; const void *owner; } g_wa[WA_MAX];
int wa_open(const void *owner) {
    uint64_t t0 = SDL_GetTicksNS();
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
    for (int i = 0; i < WA_MAX; i++) if (!g_wa[i].s) {
        SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUT_RATE };
        SDL_AudioStream *st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (!st) return -1;
        SDL_ResumeAudioStreamDevice(st); g_wa[i].s = st; g_wa[i].owner = owner;
        SDL_Log("lumen: web audio output %d opened in %.0f ms", i, (SDL_GetTicksNS() - t0) / 1e6);
        return i;
    }
    return -1;
}
static SDL_AudioStream *wa_get(const void *owner, int id) { return id >= 0 && id < WA_MAX && g_wa[id].owner == owner ? g_wa[id].s : NULL; }
void wa_push(const void *owner, int id, const float *s, int frames) { SDL_AudioStream *st = wa_get(owner, id); if (st && frames > 0) SDL_PutAudioStreamData(st, s, frames * 8); }
int wa_queued(const void *owner, int id) { SDL_AudioStream *st = wa_get(owner, id); return st ? SDL_GetAudioStreamQueued(st) / 8 : 0; }
void wa_pause(const void *owner, int id, bool p) {
    SDL_AudioStream *st = wa_get(owner, id); if (!st) return;
    if (p) SDL_PauseAudioStreamDevice(st); else SDL_ResumeAudioStreamDevice(st);
}
void wa_close(const void *owner, int id) {
    if (!wa_get(owner, id)) return;
    SDL_DestroyAudioStream(g_wa[id].s); g_wa[id].s = NULL; g_wa[id].owner = NULL;
}
void wa_close_owner(const void *owner) { for (int i = 0; i < WA_MAX; i++) if (g_wa[i].s && g_wa[i].owner == owner) wa_close(owner, i); }

typedef struct { const uint8_t *p; size_t n, pos; } MemIO;
static int mem_read(void *o, uint8_t *buf, int sz) {
    MemIO *m = o; size_t k = m->n - m->pos;
    if (!k) return AVERROR_EOF;
    if (k > (size_t)sz) k = (size_t)sz;
    memcpy(buf, m->p + m->pos, k); m->pos += k;
    return (int)k;
}
static int64_t mem_seek(void *o, int64_t off, int wh) {
    MemIO *m = o;
    if (wh == AVSEEK_SIZE) return (int64_t)m->n;
    wh &= ~AVSEEK_FORCE;
    int64_t b = (wh == SEEK_CUR ? (int64_t)m->pos : wh == SEEK_END ? (int64_t)m->n : 0) + off;
    if (b < 0 || b > (int64_t)m->n) return -1;
    m->pos = (size_t)b;
    return b;
}
typedef struct { SwrContext *swr; float *out; size_t cap, len; } Pcm;
static bool pcm_add(Pcm *pc, AVFrame *f) {
    if (!pc->swr) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (!f || swr_alloc_set_opts2(&pc->swr, &stereo, AV_SAMPLE_FMT_FLT, OUT_RATE, &f->ch_layout, (enum AVSampleFormat)f->format, f->sample_rate, 0, NULL) < 0 || swr_init(pc->swr) < 0) return false;
    }
    int want = swr_get_out_samples(pc->swr, f ? f->nb_samples : 0);
    if (want <= 0) return true;
    if (pc->len + (size_t)want > pc->cap) { pc->cap = (pc->len + (size_t)want) * 2; pc->out = xrealloc(pc->out, pc->cap * 8); }
    uint8_t *o = (uint8_t *)(pc->out + pc->len * 2);
    int got = swr_convert(pc->swr, &o, want, f ? (const uint8_t **)f->extended_data : NULL, f ? f->nb_samples : 0);
    if (got > 0) pc->len += (size_t)got;
    return pc->len < (size_t)OUT_RATE * 600;
}
float *media_decode_pcm(const uint8_t *p, size_t n, int *frames) {
    *frames = 0;
    MemIO mio = { p, n, 0 };
    AVFormatContext *fc = avformat_alloc_context();
    AVIOContext *io = avio_alloc_context(av_malloc(32768), 32768, 0, &mio, mem_read, NULL, mem_seek);
    fc->pb = io; fc->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVCodecContext *cc = NULL; AVPacket *pk = av_packet_alloc(); AVFrame *f = av_frame_alloc();
    Pcm pc = { 0 };
    int si = -1;
    if (avformat_open_input(&fc, NULL, NULL, NULL) < 0) fc = NULL;
    if (fc && avformat_find_stream_info(fc, NULL) >= 0) si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    const AVCodec *codec = si >= 0 ? avcodec_find_decoder(fc->streams[si]->codecpar->codec_id) : NULL;
    if (codec && (cc = avcodec_alloc_context3(codec)) && avcodec_parameters_to_context(cc, fc->streams[si]->codecpar) >= 0 && avcodec_open2(cc, codec, NULL) >= 0) {
        bool ok = true, eof = false;
        while (ok && !eof) {
            int r = av_read_frame(fc, pk);
            if (r < 0) { eof = true; avcodec_send_packet(cc, NULL); }
            else if (pk->stream_index != si) { av_packet_unref(pk); continue; }
            for (;;) {
                int sr = eof ? 0 : avcodec_send_packet(cc, pk);
                while (ok && avcodec_receive_frame(cc, f) == 0) { ok = pcm_add(&pc, f); av_frame_unref(f); }
                if (sr != AVERROR(EAGAIN)) break;
            }
            if (!eof) av_packet_unref(pk);
        }
        if (pc.swr) pcm_add(&pc, NULL);
    }
    swr_free(&pc.swr); avcodec_free_context(&cc); av_frame_free(&f); av_packet_free(&pk);
    avformat_close_input(&fc);
    if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    if (!pc.len) { free(pc.out); return NULL; }
    *frames = (int)pc.len;
    return pc.out;
}

/* ---------------- public API ---------------- */
MediaPlayer *mp_new(Node *el) {
    if (!g_mu) g_mu = SDL_CreateMutex();
    MediaPlayer *m = xcalloc(1, sizeof *m);
    m->mu = SDL_CreateMutex(); m->cv = SDL_CreateCondition(); m->node = el;
    m->duration = NAN; m->volume = 1; m->base_wall = now_ms();
    SDL_LockMutex(g_mu);
    if (g_np == g_cap) { g_cap = g_cap ? g_cap * 2 : 8; g_pl = xrealloc(g_pl, sizeof *g_pl * (size_t)g_cap); }
    g_pl[g_np++] = m;
    SDL_UnlockMutex(g_mu);
    wake();
    return m;
}
static Stream *new_stream(MediaPlayer *m, const char *mime, bool progressive) {
    if (m->nst == 4) return NULL;
    Stream *s = xcalloc(1, sizeof *s); s->m = m; s->mime = xstrdup(mime); s->progressive = progressive; s->fd = -1;
    s->webm = strstr(mime, "webm") != NULL;
    s->rd_epoch = m->epoch;
    m->st[m->nst++] = s;
    s->th = SDL_CreateThread(dec_thread, "lumen-media", s);
    return s;
}
void mp_open_url(MediaPlayer *m, const char *url) {
    SDL_LockMutex(m->mu); new_stream(m, url, true); SDL_UnlockMutex(m->mu);
}
void mp_open_bytes(MediaPlayer *m, const uint8_t *p, size_t n) {
    Blob *b = xcalloc(1, sizeof *b); b->refs = 1; b->n = n; b->p = xmalloc(n ? n : 1); memcpy(b->p, p, n);
    SDL_LockMutex(m->mu);
    Stream *s = new_stream(m, "blob:", true);
    if (s) s->init = b; else blob_unref(b);
    SDL_BroadcastCondition(m->cv); SDL_UnlockMutex(m->mu);
}
int mp_add_buffer(MediaPlayer *m, const char *mime) {
    if (media_can_play(mime, true) == 0) return -1;
    SDL_LockMutex(m->mu);
    Stream *s = new_stream(m, mime, false);
    if (s && strncmp(mime, "audio/", 6) == 0) m->expect_audio = true;
    int r = s ? m->nst - 1 : -1;
    SDL_UnlockMutex(m->mu);
    return r;
}
bool mp_append(MediaPlayer *m, int sb, const uint8_t *p, size_t n) {
    if (sb < 0 || sb >= m->nst) return false;
    SDL_LockMutex(m->mu);
    Stream *s = m->st[sb]; bool ok = true;
    if (s->npend + n > s->cappend) { s->cappend = (s->npend + n) * 2; s->pend = xrealloc(s->pend, s->cappend); }
    memcpy(s->pend + s->npend, p, n); s->npend += n;
    size_t off = 0;
    for (;;) {
        uint64_t sz; uint32_t ty; size_t h;
        const uint8_t *b = s->pend + off, *e = s->pend + s->npend;
        if (!box_at(b, e, &sz, &ty, &h)) break;
        if (!sz) { ok = false; off = s->npend; break; }
        if (sz > (uint64_t)(e - b)) break;
        if (ty == FCC('f', 't', 'y', 'p')) {
            Blob *nb = xcalloc(1, sizeof *nb); nb->refs = 1; nb->n = sz; nb->p = xmalloc(sz); memcpy(nb->p, b, sz);
            blob_unref(s->init); s->init = nb;
        } else if (ty == FCC('m', 'o', 'o', 'v')) {
            if (!s->init) { s->init = xcalloc(1, sizeof *s->init); s->init->refs = 1; }
            else if (s->init->refs > 1 || (s->init->n >= 8 && rd32(s->init->p + 4) == FCC('m', 'o', 'o', 'v'))) {
                Blob *nb = xcalloc(1, sizeof *nb); nb->refs = 1; blob_unref(s->init); s->init = nb;
            }
            s->init->p = xrealloc(s->init->p, s->init->n + sz); memcpy(s->init->p + s->init->n, b, sz); s->init->n += sz;
            parse_moov(s, b + h, b + sz);
        } else if (ty == FCC('m', 'o', 'o', 'f')) {
            free(s->moof); s->moof = xmalloc(sz); memcpy(s->moof, b, sz); s->nmoof = sz;
        } else if (ty == FCC('m', 'd', 'a', 't') && s->moof) {
            double t0 = 0, t1 = 0;
            if (parse_moof(s, s->moof + 8, s->moof + s->nmoof, &t0, &t1)) {
                uint8_t *seg = xmalloc(s->nmoof + sz); memcpy(seg, s->moof, s->nmoof); memcpy(seg + s->nmoof, b, sz);
                add_seg(s, seg, s->nmoof + sz, t0, t1);
            }
            free(s->moof); s->moof = NULL;
        }
        off += sz;
    }
    memmove(s->pend, s->pend + off, s->npend - off); s->npend -= off;
    SDL_BroadcastCondition(m->cv);
    SDL_UnlockMutex(m->mu);
    return ok;
}
void mp_remove(MediaPlayer *m, int sb, double a, double z) {
    if (sb < 0 || sb >= m->nst) return;
    SDL_LockMutex(m->mu);
    Stream *s = m->st[sb];
    for (int i = 0; i < s->nsegs;) {
        Seg *g = &s->segs[i];
        if (g->t0 >= a - 1e-3 && g->t1 <= z + 1e-3) { free(g->p); blob_unref(g->init); memmove(g, g + 1, sizeof *g * (size_t)(s->nsegs - i - 1)); s->nsegs--; }
        else i++;
    }
    SDL_UnlockMutex(m->mu);
}
static int stream_ranges(Stream *s, double *r, int max) {
    int n = 0;
    for (int i = 0; i < s->nsegs; i++) {
        if (n && s->segs[i].t0 <= r[2 * n - 1] + 0.15) { if (s->segs[i].t1 > r[2 * n - 1]) r[2 * n - 1] = s->segs[i].t1; continue; }
        if (n == max) break;
        r[2 * n] = s->segs[i].t0; r[2 * n + 1] = s->segs[i].t1; n++;
    }
    return n;
}
int mp_buffered(MediaPlayer *m, int sb, double *r, int max) {
    SDL_LockMutex(m->mu);
    int n = 0;
    if (sb >= 0 && sb < m->nst) n = stream_ranges(m->st[sb], r, max);
    else if (m->nst && m->st[0]->progressive) { if ((m->st[0]->init || m->st[0]->fd >= 0) && !isnan(m->duration) && max) { r[0] = 0; r[1] = m->duration; n = 1; } }
    else if (m->nst) {
        n = stream_ranges(m->st[0], r, max);
        double *t = xmalloc(sizeof(double) * 2 * (size_t)(max ? max : 1)), *o = xmalloc(sizeof(double) * 2 * (size_t)(max ? max : 1));
        for (int k = 1; k < m->nst; k++) {
            int tn = stream_ranges(m->st[k], t, max), on = 0;
            for (int i = 0; i < n; i++) for (int j = 0; j < tn && on < max; j++) {
                double a = fmax(r[2 * i], t[2 * j]), z = fmin(r[2 * i + 1], t[2 * j + 1]);
                if (z > a) { o[2 * on] = a; o[2 * on + 1] = z; on++; }
            }
            memcpy(r, o, sizeof(double) * 2 * (size_t)on); n = on;
        }
        free(t); free(o);
    }
    SDL_UnlockMutex(m->mu);
    return n;
}
void mp_end_of_stream(MediaPlayer *m) { SDL_LockMutex(m->mu); m->eos = true; SDL_BroadcastCondition(m->cv); SDL_UnlockMutex(m->mu); wake(); }
void mp_set_duration(MediaPlayer *m, double d) { SDL_LockMutex(m->mu); m->duration = d; SDL_UnlockMutex(m->mu); wake(); }
static double clock_of(MediaPlayer *m) {
    if (m->expect_audio && m->dev) {
        if (!m->playing || m->seeking) return m->base_t;
        double lat = fmax(0, m->audio_queued - (now_ms() - m->audio_wall) / 1000.0);
        return fmax(m->base_t, m->aclock - lat);
    }
    double t = m->base_t;
    if (m->playing && !m->waiting && !m->seeking) t += (now_ms() - m->base_wall) / 1000.0;
    if (!isnan(m->duration) && t > m->duration) t = m->duration;
    return t;
}
void mp_play(MediaPlayer *m) {
    SDL_LockMutex(m->mu);
    if (m->ended) { m->ended = false; SDL_UnlockMutex(m->mu); mp_seek(m, 0); SDL_LockMutex(m->mu); }
    if (!m->playing) { m->audio_wall = m->base_wall = now_ms(); m->playing = true; }
    SDL_UnlockMutex(m->mu);
    wake();
}
void mp_pause(MediaPlayer *m) {
    SDL_LockMutex(m->mu);
    if (m->playing) { m->base_t = clock_of(m); m->playing = false; }
    SDL_UnlockMutex(m->mu);
    wake();
}
void mp_seek(MediaPlayer *m, double t) {
    SDL_LockMutex(m->mu);
    if (!isnan(m->duration) && t > m->duration) t = m->duration;
    if (t < 0) t = 0;
    m->epoch++; m->target = t; m->seeking = true; m->ended = false;
    m->notified = false; /* Seek may complete in one pump while already paused. */
    for (int i = 0; i < m->vqn; i++) image_unref(m->vq[i].im);
    m->vqn = 0;
    while (m->ah) { AChunk *c = m->ah; m->ah = c->next; free(c->s); free(c); }
    m->at = NULL; m->abuf = 0; m->aclock = m->base_t = t; m->base_wall = now_ms();
    m->audio_queued = 0; m->audio_wall = m->base_wall;
    SDL_BroadcastCondition(m->cv);
    SDL_UnlockMutex(m->mu);
    /* seeking remains true until the main-thread media pump sees new data, so
       a concurrent callback can only supply silence while the queue is reset. */
    if (m->dev) SDL_ClearAudioStream(m->dev);
    wake();
}
void mp_set_volume(MediaPlayer *m, float v, bool muted) { SDL_LockMutex(m->mu); m->volume = v; m->muted = muted; SDL_UnlockMutex(m->mu); }
static void state_locked(MediaPlayer *m, MpState *o) {
    bool meta = false, vid = false;
    for (int i = 0; i < m->nst; i++) { if (m->st[i]->opened) meta = true; if (m->st[i]->has_video) vid = true; }
    o->ready = m->error ? 0 : m->live ? (m->cur ? 4 : 0) : !meta ? 0 : (m->cur || m->vqn || (!vid && m->abuf > 0)) && !m->seeking ? 4 : 1;
    o->paused = !m->playing; o->ended = m->ended; o->seeking = m->seeking; o->waiting = m->waiting && m->playing;
    o->error = m->error; o->errmsg = m->err;
    o->time = m->seeking ? m->target : clock_of(m); o->duration = m->duration; o->w = m->w; o->h = m->h;
    o->frame_serial = m->frame_hidden ? 0 : m->frame_serial; o->presented_frames = m->presented_frames; o->dropped_frames = m->dropped_frames;
    o->frame_pts = m->frame_pts; o->frame_wall = m->frame_wall; o->frame_w = m->frame_w; o->frame_h = m->frame_h;
}
void mp_state(MediaPlayer *m, MpState *o) {
    SDL_LockMutex(m->mu);
    state_locked(m, o);
    SDL_UnlockMutex(m->mu);
}

/* Publish on the main thread after decoding/seeking/presentation has settled.
   Playback position is sampled by JS only while playing; a paused element
   needs no repeating timer to discover metadata, duration, errors or seek end. */
static bool notify_state(MediaPlayer *m) {
    MpState s; state_locked(m, &s);
    const MpState *o = &m->notified_state;
    bool changed = !m->notified || s.ready != o->ready || s.paused != o->paused ||
        s.ended != o->ended || s.seeking != o->seeking || s.waiting != o->waiting ||
        s.error != o->error || s.w != o->w || s.h != o->h ||
        (s.duration != o->duration && !(isnan(s.duration) && isnan(o->duration)));
    m->notified_state = s; m->notified = true;
    return changed;
}
void mp_free(MediaPlayer *m) {
    if (!m) return;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if (g_pl[i] == m) { g_pl[i] = g_pl[--g_np]; break; }
    SDL_UnlockMutex(g_mu);
    if (m->audio_device) SDL_CloseAudioDevice(m->audio_device);
    if (m->dev) SDL_DestroyAudioStream(m->dev);
    SDL_LockMutex(m->mu); m->quit = true; SDL_BroadcastCondition(m->cv); SDL_UnlockMutex(m->mu);
    for (int i = 0; i < m->nst; i++) {
        Stream *s = m->st[i];
        SDL_WaitThread(s->th, NULL);
        for (int k = 0; k < s->nsegs; k++) { free(s->segs[k].p); blob_unref(s->segs[k].init); }
        if (s->fd >= 0) close(s->fd);
        free(s->segs); free(s->pend); free(s->moof); blob_unref(s->init); blob_unref(s->rd_init); free(s->mime); free(s);
    }
    for (int i = 0; i < m->vqn; i++) image_unref(m->vq[i].im);
    while (m->ah) { AChunk *c = m->ah; m->ah = c->next; free(c->s); free(c); }
    image_unref(m->cur); image_unref(m->live_next);
    for (int i = 0; i < m->ngrave; i++) image_unref(m->grave[i]);
    SDL_DestroyCondition(m->cv); SDL_DestroyMutex(m->mu); free(m);
}

static void present(MediaPlayer *m, const VFrame *frame) {
    Image *im = frame->im;
    m->frame_pts = frame->pts; m->frame_w = frame->w; m->frame_h = frame->h; m->frame_hidden = frame->hidden;
    if (m->cur) { if (m->ngrave == 3) { image_unref(m->grave[0]); memmove(m->grave, m->grave + 1, sizeof m->grave[0] * 2); m->ngrave--; } m->grave[m->ngrave++] = m->cur; }
    m->cur = im;
}
/* a live player shows its source's newest frame (or the one pushed to it); called with g_mu and m->mu held */
static int live_tick(MediaPlayer *m) {
    Image *im = NULL;
    if (m->live_src > 0) {
        int k = m->live_src - 1;
        if (!m->playing && m->cur) return 0;   /* a paused element keeps its frame */
        if (g_src[k].used && g_src[k].im && g_src[k].serial != m->live_seen) { im = g_src[k].im; im->refs++; m->live_seen = g_src[k].serial; }
    } else { im = m->live_next; m->live_next = NULL; }
    if (!im) return 0;
    int ow = m->cur ? m->cur->w : 0, oh = m->cur ? m->cur->h : 0;
    VFrame f = { .pts = (now_ms() - m->base_wall) / 1000.0, .im = im, .w = im->w, .h = im->h };
    present(m, &f);
    m->w = im->w; m->h = im->h;
    m->presented_frames++; m->frame_serial = ++g_frame_version; m->frame_wall = now_ms();
    return 1 | (im->w != ow || im->h != oh ? 2 : 0);
}
int media_tick(void) {
    if (!g_mu) return 0;
    cam_tick(); screen_tick(); rec_tick(); gl_tick(); webgpu_tick();
    int out = 0;
    bool state_changed = false;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) {
        MediaPlayer *m = g_pl[i];
        SDL_LockMutex(m->mu);
        if (m->live) { out |= live_tick(m); state_changed |= notify_state(m); SDL_UnlockMutex(m->mu); continue; }
        if (m->playing && m->expect_audio) open_audio(m);
        bool vid = false, all_eof = m->nst > 0, aud_eof = true; int ow = m->cur ? m->cur->w : 0, oh = m->cur ? m->cur->h : 0;
        for (int k = 0; k < m->nst; k++) {
            Stream *s = m->st[k];
            if (s->has_video) vid = true;
            bool eof = s->eof && s->rd_epoch == m->epoch;
            if (!eof) all_eof = false;
            if (!eof && (s->has_audio || (!s->opened && !s->progressive &&
                strncmp(s->mime, "audio/", 6) == 0))) aud_eof = false;
        }
        unsigned popped = 0;
        if (getenv("LUMEN_MEDIA_DEBUG")) { static double last; if (now_ms() - last > 500) { last = now_ms(); fprintf(stderr, "mp %p play=%d seek=%d wait=%d eos=%d vqn=%d ah=%p abuf=%.2f clk=%.2f dev=%p ea=%d", (void *)m, m->playing, m->seeking, m->waiting, m->eos, m->vqn, (void *)m->ah, m->abuf, clock_of(m), (void *)m->dev, m->expect_audio); for (int k = 0; k < m->nst; k++) fprintf(stderr, " s%d[eof=%d v=%d a=%d]", k, m->st[k]->eof, m->st[k]->has_video, m->st[k]->has_audio); fprintf(stderr, "\n"); } }
        if (m->seeking && (m->vqn || (!vid && m->abuf > 0) || all_eof)) {
            m->seeking = false;
            if (m->vqn) { present(m, &m->vq[0]); memmove(m->vq, m->vq + 1, sizeof m->vq[0] * (size_t)--m->vqn); popped++; }
            m->base_t = m->aclock = m->target; m->base_wall = now_ms();
        }
        if (!m->cur && m->vqn) { present(m, &m->vq[0]); memmove(m->vq, m->vq + 1, sizeof m->vq[0] * (size_t)--m->vqn); popped++; }
        if (m->expect_audio && m->dev && aud_eof && !m->ah) { m->base_t = m->aclock; m->base_wall = now_ms(); m->expect_audio = false; }
        if (m->playing && !m->seeking) {
            bool starve = (m->expect_audio && m->dev) ? (!m->ah && !aud_eof) : (vid && !m->vqn && !all_eof);
            if (starve && !m->waiting) { m->base_t = clock_of(m); m->waiting = true; }
            else if (!starve && m->waiting) { m->waiting = false; m->base_wall = now_ms(); }
            double t = clock_of(m);
            if (!(m->expect_audio && m->dev)) while (m->ah && m->ah->pts + (double)m->ah->frames / OUT_RATE <= t) {   /* no audio device: drain by clock */
                AChunk *c = m->ah; m->ah = c->next; if (!m->ah) m->at = NULL;
                m->abuf -= (double)(c->frames - c->off) / OUT_RATE; if (m->abuf < 0) m->abuf = 0;
                free(c->s); free(c); SDL_BroadcastCondition(m->cv);
            }
            if (all_eof && !(m->expect_audio && m->dev) && !isnan(m->duration) && t >= m->duration - 0.01) while (m->ah) {
                AChunk *c = m->ah; m->ah = c->next; if (!m->ah) m->at = NULL; free(c->s); free(c); m->abuf = 0;
            }
            if (all_eof && !isnan(m->duration) && t >= m->duration - 0.01) while (m->vqn) { present(m, &m->vq[0]); memmove(m->vq, m->vq + 1, sizeof m->vq[0] * (size_t)--m->vqn); popped++; }
            while (m->vqn && m->vq[0].pts <= t + 0.008) { present(m, &m->vq[0]); memmove(m->vq, m->vq + 1, sizeof m->vq[0] * (size_t)--m->vqn); popped++; }
            if (all_eof && !m->vqn && !m->ah && (m->eos || (m->nst && m->st[0]->progressive))) {
                m->base_t = isnan(m->duration) ? t : m->duration; m->playing = false; m->ended = true;
            }
        }
        if (popped) {
            /* Only the final selected frame reaches composition. Earlier due
               frames and low-memory hidden placeholders are dropped frames. */
            m->dropped_frames += popped - (m->frame_hidden ? 0 : 1);
            if (!m->frame_hidden) { m->presented_frames++; m->frame_serial = ++g_frame_version; m->frame_wall = now_ms(); }
        }
        if (popped) { SDL_BroadcastCondition(m->cv); out |= 1; if (m->cur && (m->cur->w != ow || m->cur->h != oh)) { out |= 2; if (getenv("LUMEN_DEBUG_PAINT")) fprintf(stderr, "media: size %p %dx%d -> %dx%d\n", (void *)m, ow, oh, m->cur->w, m->cur->h); } }
        bool run = m->playing;
        state_changed |= notify_state(m);
        SDL_UnlockMutex(m->mu);
        sync_audio_device(m, run);
    }
    SDL_UnlockMutex(g_mu);
    if (state_changed) { ++g_state_version; wake(); }
    return out;
}
int media_timeout_ms(void) {
    if (!g_mu) return -1;
    int to = cam_timeout_ms(), rt = rec_timeout_ms(), gt = gl_timeout_ms(), wt = webgpu_timeout_ms();
    if (rt >= 0 && (to < 0 || rt < to)) to = rt;
    if (gt >= 0 && (to < 0 || gt < to)) to = gt;
    if (wt >= 0 && (to < 0 || wt < to)) to = wt;
    if (screen_waiting() && (to < 0 || to > 100)) to = 100;   /* polls the record permission's answer */
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) {
        MediaPlayer *m = g_pl[i];
        SDL_LockMutex(m->mu);
        if (m->playing || m->seeking) {
            int t = 20;
            if (m->vqn && !m->seeking) { double d = (m->vq[0].pts - clock_of(m)) * 1000; t = d < 1 ? 1 : d > 20 ? 20 : (int)d; }
            if (to < 0 || t < to) to = t;
        }
        SDL_UnlockMutex(m->mu);
    }
    SDL_UnlockMutex(g_mu);
    return to;
}
bool media_is_frame(const Image *im) {
    if (!g_mu || !im) return false;
    bool r = false;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np && !r; i++) r = g_pl[i]->cur == im;
    SDL_UnlockMutex(g_mu);
    return r;
}
const void *media_owner_of(const Image *im) {
    if (!g_mu || !im) return NULL;
    const void *o = NULL;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np && !o; i++) if (g_pl[i]->cur == im) o = g_pl[i];
    SDL_UnlockMutex(g_mu);
    return o;
}
uint64_t media_image_serial(const Image *im) {
    if (!g_mu || !im) return 0;
    uint64_t serial = 0;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if (g_pl[i]->cur == im) {
        if (!g_pl[i]->frame_hidden) serial = g_pl[i]->frame_serial;
        break;
    }
    SDL_UnlockMutex(g_mu);
    return serial;
}
Image *media_owner_frame(const void *owner) {
    if (!g_mu || !owner) return NULL;
    Image *im = NULL;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if ((const void *)g_pl[i] == owner) { im = g_pl[i]->cur; break; }
    SDL_UnlockMutex(g_mu);
    return im;
}
Image *media_frame_for(Node *el) {
    if (!g_mu) return NULL;
    Image *im = NULL;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if (g_pl[i]->node == el) { im = g_pl[i]->cur; break; }
    SDL_UnlockMutex(g_mu);
    return im;
}

bool media_state_for(Node *el, MpState *s) {
    if (!g_mu) return false;
    MediaPlayer *m = NULL;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if (g_pl[i]->node == el) { m = g_pl[i]; break; }
    SDL_UnlockMutex(g_mu);
    if (m) mp_state(m, s);
    return m != NULL;
}

void mp_live(MediaPlayer *m, int src) {
    SDL_LockMutex(g_mu); SDL_LockMutex(m->mu);
    m->live = true; m->live_src = src; m->live_seen = 0; m->duration = INFINITY;
    SDL_UnlockMutex(m->mu); SDL_UnlockMutex(g_mu);
    wake();
}
void mp_push(MediaPlayer *m, Image *im) {
    SDL_LockMutex(m->mu);
    m->live = true;
    image_unref(m->live_next); m->live_next = im;
    SDL_UnlockMutex(m->mu);
    wake();
}
int live_open(void) {
    if (!g_mu) g_mu = SDL_CreateMutex();
    int id = -1;
    SDL_LockMutex(g_mu);
    for (int k = 0; k < LIVE_MAX && id < 0; k++) if (!g_src[k].used) { g_src[k].used = true; g_src[k].im = NULL; g_src[k].serial = 0; id = k + 1; }
    SDL_UnlockMutex(g_mu);
    return id;
}
void live_push(int id, Image *im) {
    if (id < 1 || id > LIVE_MAX) { image_unref(im); return; }
    SDL_LockMutex(g_mu);
    Image *old = g_src[id - 1].im;
    if (g_src[id - 1].used) { g_src[id - 1].im = im; g_src[id - 1].serial++; } else { old = im; }
    SDL_UnlockMutex(g_mu);
    image_unref(old);
    wake();
}
void live_close(int id) {
    if (id < 1 || id > LIVE_MAX) return;
    SDL_LockMutex(g_mu);
    Image *old = g_src[id - 1].im;
    g_src[id - 1].used = false; g_src[id - 1].im = NULL;
    for (int i = 0; i < g_np; i++) if (g_pl[i]->live_src == id) g_pl[i]->live_src = -1;   /* keep their last frame; a reused id is another source */
    SDL_UnlockMutex(g_mu);
    image_unref(old);
}
Image *live_frame(int id, uint64_t *serial) {
    if (id < 1 || id > LIVE_MAX || !g_mu) return NULL;
    SDL_LockMutex(g_mu);
    Image *im = g_src[id - 1].used ? g_src[id - 1].im : NULL;
    if (serial) *serial = g_src[id - 1].serial;
    SDL_UnlockMutex(g_mu);
    return im;
}

static bool has_param_over(const char *mime, const char *key, double lim) {
    const char *p = strstr(mime, key);
    return p && atof(p + strlen(key)) > lim;
}
int media_can_play(const char *mime, bool mse) {
    if (!mime) return 0;
    char t[64]; size_t i = 0;
    while (mime[i] && mime[i] != ';' && i < sizeof t - 1) { t[i] = (char)(mime[i] >= 'A' && mime[i] <= 'Z' ? mime[i] + 32 : mime[i]); i++; }
    t[i] = 0; while (i && t[i - 1] == ' ') t[--i] = 0;
    bool mp4 = !strcmp(t, "video/mp4") || !strcmp(t, "audio/mp4");
    bool other = !strcmp(t, "video/webm") || !strcmp(t, "audio/webm") || !strcmp(t, "audio/mpeg") || !strcmp(t, "audio/ogg") || !strcmp(t, "video/ogg") || !strcmp(t, "audio/wav") || !strcmp(t, "audio/aac") || !strcmp(t, "audio/flac") || !strcmp(t, "video/quicktime");
    if (mse ? !mp4 : !(mp4 || other)) return 0;
    if (has_param_over(mime, "width=", 4096) || has_param_over(mime, "height=", 2160) || has_param_over(mime, "framerate=", 120) || has_param_over(mime, "channels=", 8)) return 0;
    if (strstr(mime, "eotf=smpte2084") || strstr(mime, "eotf=arib-std-b67")) return 0;
    const char *c = strstr(mime, "codecs=");
    if (!c) return mse ? 2 : 1;
    c += 7;
    static const char *const ok[] = { "avc1", "avc3", "mp4a", "opus", "vp09", "vp9", "vp8", "vorbis", "av01", "flac", "mp3", "hev1", "hvc1", NULL };
    static const char *const ok_mse[] = { "avc1", "avc3", "mp4a", NULL };
    while (*c) {
        while (*c == '"' || *c == '\'' || *c == ' ' || *c == ',') c++;
        if (!*c || *c == ';') break;
        bool good = false;
        for (const char *const *k = mse ? ok_mse : ok; *k; k++) if (!strncmp(c, *k, strlen(*k))) good = true;
        if (!good) return 0;
        while (*c && *c != ',' && *c != '"' && *c != '\'' && *c != ';') c++;
    }
    return 2;
}

void media_mark_visible(bool (*vis)(Node *, void *), void *ud) {
    double now = (double)SDL_GetTicks();
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if (vis(g_pl[i]->node, ud)) g_pl[i]->painted = now;
    SDL_UnlockMutex(g_mu);
}

/* facts about a local media file for the Downloads viewer, as JSON object members: "dur" (s) and "br" (bits/s)
   of the whole file; "vcodec", "w", "h", "fps", "vbr" of its picture; "acodec", "rate", "ch", "abr" of its
   sound. False if FFmpeg cannot read it. Main thread; reads only the headers and the first seconds. */
bool media_probe_file(const char *path, char *out, size_t n) {
    AVFormatContext *fc = NULL; AVDictionary *opt = NULL;
    av_dict_set(&opt, "probesize", "4000000", 0); av_dict_set(&opt, "analyzeduration", "3000000", 0);
    int r = avformat_open_input(&fc, path, NULL, &opt);
    av_dict_free(&opt);
    if (r < 0) return false;
    if (avformat_find_stream_info(fc, NULL) < 0) { avformat_close_input(&fc); return false; }
    int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0), ai = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    SB b; sb_init(&b);
    sb_printf(&b, "\"dur\":%.3f,\"br\":%lld", fc->duration > 0 ? (double)fc->duration / AV_TIME_BASE : 0, (long long)fc->bit_rate);
    if (vi >= 0 && !(fc->streams[vi]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {   /* not an audio file's cover */
        const AVStream *st = fc->streams[vi]; const AVCodecParameters *cp = st->codecpar;
        AVRational fr = st->avg_frame_rate.num && st->avg_frame_rate.den ? st->avg_frame_rate : st->r_frame_rate;
        sb_printf(&b, ",\"vcodec\":\"%s\",\"w\":%d,\"h\":%d,\"fps\":%.2f,\"vbr\":%lld", avcodec_get_name(cp->codec_id), cp->width, cp->height,
            fr.num && fr.den ? av_q2d(fr) : 0, (long long)cp->bit_rate);
    }
    if (ai >= 0) {
        const AVCodecParameters *cp = fc->streams[ai]->codecpar;
        sb_printf(&b, ",\"acodec\":\"%s\",\"rate\":%d,\"ch\":%d,\"abr\":%lld", avcodec_get_name(cp->codec_id), cp->sample_rate, cp->ch_layout.nb_channels, (long long)cp->bit_rate);
    }
    avformat_close_input(&fc);
    bool ok = b.n < n;
    if (ok) memcpy(out, b.s, b.n + 1);
    sb_free(&b);
    return ok;
}
