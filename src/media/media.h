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
    uint64_t frame_serial, presented_frames, dropped_frames;
    double frame_pts, frame_wall;
    int frame_w, frame_h;
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
uint64_t media_frame_version(void); /* main-thread presentation generation; no timer polling */
uint64_t media_state_version(void); /* main-thread metadata/playback-state generation */
extern bool media_yuv;                  /* presenter can draw NV12 frames (Image.yuv) */
int media_timeout_ms(void);              /* -1 when nothing is playing */
Image *media_frame_for(Node *element);
const void *media_owner_of(const Image *im);   /* player showing im, or NULL */
uint64_t media_image_serial(const Image *im); /* main thread; global frame identity, 0 for unknown/hidden images */
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
/* microphone (mono float at 48 kHz) and MediaRecorder encoding, per owning JS context */
int mic_open(const void *owner);                 /* -1 without audio; Android asks for permission first */
int mic_permission(void);                        /* 1 granted, 0 not asked yet, -1 denied */
int mic_state(const void *owner, int id);        /* 0 waiting for permission, 1 capturing, -1 denied or failed */
int mic_read(const void *owner, int id, float *out, int max);
int mic_available(const void *owner, int id);
void mic_close(const void *owner, int id);
void mic_close_owner(const void *owner);
bool mic_active(void);
int rec_kind(const char *mime);                  /* 1 webm/opus, 2 ogg/opus, 3 mp4/aac, 0 unsupported */
int rec_open(const void *owner, const char *mime, int bits_per_second);
void rec_push(const void *owner, int id, const float *pcm, int frames);
void rec_finish(const void *owner, int id);      /* encoder flush and container trailer */
uint8_t *rec_take(const void *owner, int id, size_t *n);   /* muxed bytes since the last take (malloc), NULL if none */
void rec_close(const void *owner, int id);
void rec_close_owner(const void *owner);
float *media_decode_pcm(const uint8_t *p, size_t n, int *frames);
