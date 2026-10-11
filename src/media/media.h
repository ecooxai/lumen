/* Media playback for <video>/<audio>: progressive files and Media Source Extensions
 * (fragmented MP4) decoded with FFmpeg, audio through SDL3. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct Node Node;
typedef struct Image Image;

typedef struct MediaPlayer MediaPlayer;

typedef struct MpState {
    int ready;            /* HTMLMediaElement.readyState (0..4) */
    bool paused, ended, seeking, waiting, error;
    double time, duration;
    int w, h;
    const char *errmsg;
} MpState;

MediaPlayer *mp_new(Node *element);
void mp_free(MediaPlayer *m);
void mp_open_url(MediaPlayer *m, const char *url);
int mp_add_buffer(MediaPlayer *m, const char *mime);   /* SourceBuffer index, -1 if unsupported */
bool mp_append(MediaPlayer *m, int sb, const uint8_t *p, size_t n);
void mp_remove(MediaPlayer *m, int sb, double start, double end);
int mp_buffered(MediaPlayer *m, int sb, double *ranges, int max);  /* sb < 0: whole element */
void mp_end_of_stream(MediaPlayer *m);
void mp_set_duration(MediaPlayer *m, double d);
void mp_play(MediaPlayer *m);
void mp_pause(MediaPlayer *m);
void mp_seek(MediaPlayer *m, double t);
void mp_set_volume(MediaPlayer *m, float volume, bool muted);
void mp_state(MediaPlayer *m, MpState *s);
bool media_state_for(Node *el, MpState *s);   /* state of the player attached to <video>/<audio> el */

/* 0 = no, 1 = maybe, 2 = probably */
int media_can_play(const char *mime, bool mse);

/* Main-thread pump: advances clocks and presents frames. Bit 0: a frame changed, bit 1: a video size changed. */
int media_tick(void);
extern bool media_yuv;                  /* presenter can draw NV12 frames (Image.yuv) */
int media_timeout_ms(void);              /* -1 when nothing is playing */
Image *media_frame_for(Node *element);
const void *media_owner_of(const Image *im);   /* player showing im, or NULL */
Image *media_owner_frame(const void *owner);   /* current frame of that player if it still exists */
bool media_is_frame(const Image *im);   /* borrowed; valid until a later media_tick */
extern void (*media_wakeup)(void);
size_t media_mem_bytes(size_t *frames);
/* Tab CPU limiter: decoder-thread CPU (ms) of players in `d`, and cap them to `lim` of a core (0 = none). */
struct Document;
double media_cpu_ms(struct Document *d);
void media_set_limit(struct Document *d, float lim);
extern bool media_lowmem;   /* hidden players skip RGBA conversion */
void media_mark_visible(bool (*vis)(Node *, void *), void *ud);


/* Web Audio output: one SDL stream per AudioContext, fed interleaved stereo float at 48 kHz; owner = JS context. */
int wa_open(const void *owner);            /* -1 when no audio device */
void wa_push(const void *owner, int id, const float *s, int frames);
int wa_queued(const void *owner, int id);  /* frames not yet played */
void wa_pause(const void *owner, int id, bool paused);
void wa_close(const void *owner, int id);
void wa_close_owner(const void *owner);
/* decodeAudioData: whole file -> malloc'd interleaved stereo float at 48 kHz, NULL if undecodable */
float *media_decode_pcm(const uint8_t *p, size_t n, int *frames);

/* getUserMedia capture (capture.c). kinds: bit CAP_AUDIO | CAP_VIDEO */
enum { CAP_AUDIO = 1, CAP_VIDEO = 2 };
extern bool (*capture_permit)(const char *origin, int kinds);   /* app prompt; NULL = allow with fake devices */
extern int (*capture_permission)(const char *origin, int kind);  /* 1 granted, 0 ask, -1 denied */
bool cap_permit(const char *origin, int kinds);
int cap_permission(const char *origin, int kind);
int cap_list(int kind, char *out, size_t n);                     /* device names, '\n'-separated; returns count */
int cap_open(const void *owner, int kind, int dev, int w, int h); /* -1 when no device */
void cap_close(const void *owner, int id);
void cap_close_owner(const void *owner);
int cap_state(const void *owner, int id, int *w, int *h, const char **label);   /* 0 pending, 1 live, -1 ended */
int cap_read(const void *owner, int id, float *out, int max);    /* mono float 48 kHz */
void cap_attach(const void *owner, int id, Node *video);
void cap_detach(Node *video);
int cap_tick(void);
int cap_timeout_ms(void);
Image *cap_frame_for(Node *el);
const void *cap_owner_of(const Image *im);
Image *cap_owner_frame(const void *owner);
bool cap_is(const void *owner);
int rec_new(const void *owner, int bitrate);                     /* WebM/Opus encoder for MediaRecorder */
void rec_push(const void *owner, int id, const float *s, int n);
uint8_t *rec_take(const void *owner, int id, bool fin, size_t *n);
void rec_close_owner(const void *owner);
