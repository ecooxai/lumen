/* <video>/<audio> playback. Each source (progressive file or MSE SourceBuffer) gets a decoder
 * thread that demuxes through a custom AVIOContext fed from in-memory segments. Video frames
 * queue as BGRA Images; audio is resampled to f32 stereo 48 kHz and pulled by an SDL3 audio
 * stream callback whose position is the master clock (wall clock when there is no audio). */
#include "media/media.h"
#include "paint/paint.h"
#include "base/util.h"
#include "net/net.h"
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VQ_MAX 6
#define AQ_MAX_SEC 1.0
#define OUT_RATE 48000
#define IO_BUF 65536
#define MAX_W 1920

typedef struct Blob { int refs; uint8_t *p; size_t n; } Blob;
typedef struct { double t0, t1; uint8_t *p; size_t n; Blob *init; } Seg;
typedef struct { double pts; Image *im; } VFrame;
typedef struct AChunk { double pts; float *s; int frames, off; struct AChunk *next; } AChunk;

typedef struct Stream {
    MediaPlayer *m;
    char *mime;
    bool progressive, webm;
    Blob *init;                            /* latest init segment, or the whole file when progressive */
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
    float volume; bool muted;
    double aclock, base_t, base_wall;
    int w, h;
    VFrame vq[VQ_MAX]; int vqn;
    AChunk *ah, *at; double abuf;
    Image *cur, *grave[3]; int ngrave;
};

void (*media_wakeup)(void);
static SDL_Mutex *g_mu;
static MediaPlayer **g_pl; static int g_np, g_cap;

static void wake(void) { if (media_wakeup) media_wakeup(); }
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
    Stream *s = opaque; int64_t n = s->init ? (int64_t)s->init->n : 0;
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
static bool emit_video(Stream *s, DecCtx *d, AVFrame *f, AVRational tb) {
    MediaPlayer *m = s->m;
    int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    double pts = ts == AV_NOPTS_VALUE ? 0 : ts * av_q2d(tb);
    if (pts < d->skip - 1e-3) return true;
    Image *im = to_image(&d->sws, f);
    if (!im) return true;
    SDL_LockMutex(m->mu);
    while (m->vqn == VQ_MAX && !m->quit && m->epoch == d->epoch) SDL_WaitCondition(m->cv, m->mu);
    bool ok = !m->quit && m->epoch == d->epoch;
    if (ok) { m->vq[m->vqn++] = (VFrame){ pts, im }; if (m->w != f->width || m->h != f->height) { m->w = f->width; m->h = f->height; } }
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
static AVCodecContext *open_dec(AVStream *st) {
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return NULL;
    AVCodecContext *cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0) { avcodec_free_context(&cc); return NULL; }
    cc->thread_count = 0; cc->pkt_timebase = st->time_base;
    if (avcodec_open2(cc, codec, NULL) < 0) { avcodec_free_context(&cc); return NULL; }
    return cc;
}
static bool decode_pkt(Stream *s, DecCtx *d, AVCodecContext *cc, AVPacket *pkt, AVFrame *f, bool video, AVRational tb) {
    if (avcodec_send_packet(cc, pkt) < 0 && pkt) return true;
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
    while ((r = av_read_frame(fc, pkt)) >= 0) {
        bool ok = true;
        if (pkt->stream_index == vi) ok = decode_pkt(s, d, vc, pkt, f, true, fc->streams[vi]->time_base);
        else if (pkt->stream_index == ai) ok = decode_pkt(s, d, ac, pkt, f, false, fc->streams[ai]->time_base);
        av_packet_unref(pkt);
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
    if (s->progressive && !s->init) {
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
    SDL_BroadcastCondition(m->cv);
    SDL_UnlockMutex(m->mu);
}
static void open_audio(MediaPlayer *m) {
    if (m->dev || m->audio_failed) return;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) { m->audio_failed = true; return; }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUT_RATE };
    m->dev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, m);
    if (!m->dev) { m->audio_failed = true; return; }
    SDL_ResumeAudioStreamDevice(m->dev);
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
    return m;
}
static Stream *new_stream(MediaPlayer *m, const char *mime, bool progressive) {
    if (m->nst == 4) return NULL;
    Stream *s = xcalloc(1, sizeof *s); s->m = m; s->mime = xstrdup(mime); s->progressive = progressive;
    s->webm = strstr(mime, "webm") != NULL;
    s->rd_epoch = m->epoch;
    m->st[m->nst++] = s;
    s->th = SDL_CreateThread(dec_thread, "lumen-media", s);
    return s;
}
void mp_open_url(MediaPlayer *m, const char *url) {
    SDL_LockMutex(m->mu); new_stream(m, url, true); SDL_UnlockMutex(m->mu);
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
    else if (m->nst && m->st[0]->progressive) { if (m->st[0]->init && !isnan(m->duration) && max) { r[0] = 0; r[1] = m->duration; n = 1; } }
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
void mp_end_of_stream(MediaPlayer *m) { SDL_LockMutex(m->mu); m->eos = true; SDL_BroadcastCondition(m->cv); SDL_UnlockMutex(m->mu); }
void mp_set_duration(MediaPlayer *m, double d) { SDL_LockMutex(m->mu); m->duration = d; SDL_UnlockMutex(m->mu); }
static double clock_of(MediaPlayer *m) {
    if (m->expect_audio && m->dev) {
        double lat = (double)SDL_GetAudioStreamQueued(m->dev) / 8.0 / OUT_RATE;
        double t = m->aclock - lat; return t < 0 ? 0 : t;
    }
    double t = m->base_t;
    if (m->playing && !m->waiting && !m->seeking) t += (now_ms() - m->base_wall) / 1000.0;
    if (!isnan(m->duration) && t > m->duration) t = m->duration;
    return t;
}
void mp_play(MediaPlayer *m) {
    SDL_LockMutex(m->mu);
    if (m->ended) { m->ended = false; SDL_UnlockMutex(m->mu); mp_seek(m, 0); SDL_LockMutex(m->mu); }
    if (!m->playing) { m->base_wall = now_ms(); m->playing = true; }
    if (m->expect_audio) open_audio(m);
    SDL_UnlockMutex(m->mu);
}
void mp_pause(MediaPlayer *m) {
    SDL_LockMutex(m->mu);
    if (m->playing) { m->base_t = clock_of(m); m->playing = false; }
    SDL_UnlockMutex(m->mu);
}
void mp_seek(MediaPlayer *m, double t) {
    SDL_LockMutex(m->mu);
    if (!isnan(m->duration) && t > m->duration) t = m->duration;
    if (t < 0) t = 0;
    m->epoch++; m->target = t; m->seeking = true; m->ended = false;
    for (int i = 0; i < m->vqn; i++) image_unref(m->vq[i].im);
    m->vqn = 0;
    while (m->ah) { AChunk *c = m->ah; m->ah = c->next; free(c->s); free(c); }
    m->at = NULL; m->abuf = 0; m->aclock = m->base_t = t; m->base_wall = now_ms();
    if (m->dev) SDL_ClearAudioStream(m->dev);
    SDL_BroadcastCondition(m->cv);
    SDL_UnlockMutex(m->mu);
}
void mp_set_volume(MediaPlayer *m, float v, bool muted) { SDL_LockMutex(m->mu); m->volume = v; m->muted = muted; SDL_UnlockMutex(m->mu); }
void mp_state(MediaPlayer *m, MpState *o) {
    SDL_LockMutex(m->mu);
    bool meta = false, vid = false;
    for (int i = 0; i < m->nst; i++) { if (m->st[i]->opened) meta = true; if (m->st[i]->has_video) vid = true; }
    o->ready = m->error ? 0 : !meta ? 0 : (m->cur || m->vqn || (!vid && m->abuf > 0)) && !m->seeking ? 4 : 1;
    o->paused = !m->playing; o->ended = m->ended; o->seeking = m->seeking; o->waiting = m->waiting && m->playing;
    o->error = m->error; o->errmsg = m->err;
    o->time = m->seeking ? m->target : clock_of(m); o->duration = m->duration; o->w = m->w; o->h = m->h;
    SDL_UnlockMutex(m->mu);
}
void mp_free(MediaPlayer *m) {
    if (!m) return;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if (g_pl[i] == m) { g_pl[i] = g_pl[--g_np]; break; }
    SDL_UnlockMutex(g_mu);
    if (m->dev) SDL_DestroyAudioStream(m->dev);
    SDL_LockMutex(m->mu); m->quit = true; SDL_BroadcastCondition(m->cv); SDL_UnlockMutex(m->mu);
    for (int i = 0; i < m->nst; i++) {
        Stream *s = m->st[i];
        SDL_WaitThread(s->th, NULL);
        for (int k = 0; k < s->nsegs; k++) { free(s->segs[k].p); blob_unref(s->segs[k].init); }
        free(s->segs); free(s->pend); free(s->moof); blob_unref(s->init); blob_unref(s->rd_init); free(s->mime); free(s);
    }
    for (int i = 0; i < m->vqn; i++) image_unref(m->vq[i].im);
    while (m->ah) { AChunk *c = m->ah; m->ah = c->next; free(c->s); free(c); }
    image_unref(m->cur);
    for (int i = 0; i < m->ngrave; i++) image_unref(m->grave[i]);
    SDL_DestroyCondition(m->cv); SDL_DestroyMutex(m->mu); free(m);
}

static void present(MediaPlayer *m, Image *im) {
    if (m->cur) { if (m->ngrave == 3) { image_unref(m->grave[0]); memmove(m->grave, m->grave + 1, sizeof m->grave[0] * 2); m->ngrave--; } m->grave[m->ngrave++] = m->cur; }
    m->cur = im;
}
int media_tick(void) {
    if (!g_mu) return 0;
    int out = 0;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) {
        MediaPlayer *m = g_pl[i];
        SDL_LockMutex(m->mu);
        bool vid = false, all_eof = m->nst > 0, aud_eof = true; int ow = m->cur ? m->cur->w : 0, oh = m->cur ? m->cur->h : 0;
        for (int k = 0; k < m->nst; k++) {
            Stream *s = m->st[k];
            if (s->has_video) vid = true;
            if (!s->eof) all_eof = false;
            if (s->has_audio && !s->eof) aud_eof = false;
        }
        bool popped = false;
        if (m->seeking && (m->vqn || (!vid && m->abuf > 0) || all_eof)) {
            m->seeking = false;
            if (m->vqn) { present(m, m->vq[0].im); memmove(m->vq, m->vq + 1, sizeof m->vq[0] * (size_t)--m->vqn); popped = true; }
            m->base_t = m->aclock = m->target; m->base_wall = now_ms();
        }
        if (!m->cur && m->vqn) { present(m, m->vq[0].im); memmove(m->vq, m->vq + 1, sizeof m->vq[0] * (size_t)--m->vqn); popped = true; }
        if (m->expect_audio && m->dev && aud_eof && !m->ah) { m->base_t = m->aclock; m->base_wall = now_ms(); m->expect_audio = false; }
        if (m->playing && !m->seeking) {
            bool starve = (m->expect_audio && m->dev) ? (!m->ah && !aud_eof) : (vid && !m->vqn && !all_eof);
            if (starve && !m->waiting) { m->base_t = clock_of(m); m->waiting = true; }
            else if (!starve && m->waiting) { m->waiting = false; m->base_wall = now_ms(); }
            double t = clock_of(m);
            while (m->vqn && m->vq[0].pts <= t + 0.008) { present(m, m->vq[0].im); memmove(m->vq, m->vq + 1, sizeof m->vq[0] * (size_t)--m->vqn); popped = true; }
            if (all_eof && !m->vqn && !m->ah && (m->eos || (m->nst && m->st[0]->progressive))) {
                m->base_t = isnan(m->duration) ? t : m->duration; m->playing = false; m->ended = true;
            }
        }
        if (popped) { SDL_BroadcastCondition(m->cv); out |= 1; if (m->cur && (m->cur->w != ow || m->cur->h != oh)) out |= 2; }
        SDL_UnlockMutex(m->mu);
    }
    SDL_UnlockMutex(g_mu);
    return out;
}
int media_timeout_ms(void) {
    if (!g_mu) return -1;
    int to = -1;
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
Image *media_frame_for(Node *el) {
    if (!g_mu) return NULL;
    Image *im = NULL;
    SDL_LockMutex(g_mu);
    for (int i = 0; i < g_np; i++) if (g_pl[i]->node == el) { im = g_pl[i]->cur; break; }
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
