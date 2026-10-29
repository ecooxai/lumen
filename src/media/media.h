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
void mp_open_bytes(MediaPlayer *m, const uint8_t *p, size_t n);   /* a whole file in memory: a Blob's object URL */
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
bool media_state_for(Node *el, MpState *s);
/* Live players show pushed frames instead of decoding: those of a live source (src > 0) or mp_push's. */
void mp_live(MediaPlayer *m, int src);
void mp_push(MediaPlayer *m, Image *im);   /* takes the reference; any thread */
/* Live sources (camera, screen capture): the newest frame, shown by every player attached to it. Ids from 1. */
int live_open(void);
void live_push(int id, Image *im);          /* takes the reference; any thread */
void live_close(int id);
Image *live_frame(int id, uint64_t *serial); /* main thread; borrowed until the next live_push */   /* state of the player attached to <video>/<audio> el */

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
int mic_open(const void *owner, bool voice);     /* -1 without audio; Android asks for permission first; voice: echo cancellation */
int mic_permission(void);                        /* 1 granted, 0 not asked yet, -1 denied */
bool mic_permission_request(void);               /* Android: asks for the record permission unless granted; true while an answer is due */
int mic_state(const void *owner, int id);        /* 0 waiting for permission, 1 capturing, -1 denied or failed */
int mic_read(const void *owner, int id, float *out, int max);
int mic_available(const void *owner, int id);
void mic_close(const void *owner, int id);
void mic_close_owner(const void *owner);
bool mic_active(void);
int rec_kind(const char *mime);                  /* >0 if MediaRecorder can record this mime type here, 0 unsupported */
int rec_open(const void *owner, const char *mime, int bits_per_second, int video_bps, int video_src, int w, int h, int fps, bool audio);   /* video_src: a live source or 0 */
void rec_pause(const void *owner, int id, bool paused);
void rec_tick(void);                               /* main thread: video frames to the encoders */
int rec_timeout_ms(void);
void rec_push(const void *owner, int id, const float *pcm, int frames);
void rec_finish(const void *owner, int id);      /* encoder flush and container trailer */
uint8_t *rec_take(const void *owner, int id, size_t *n);   /* muxed bytes since the last take (malloc), NULL if none */
const char *rec_mime(const void *owner, int id);   /* the mime type with the codecs it records, as Chrome reports it */
/* the Downloads viewer: a local media file's duration, bitrates, codecs, picture size, frame and sample rates as JSON members */
bool media_probe_file(const char *path, char *out, size_t n);
void rec_close(const void *owner, int id);
void rec_close_owner(const void *owner);
/* camera for getUserMedia video (SDL3 camera; Camera2 on Android); frames go to a live source. Main thread.
   facing: 0 any, 1 user (front), 2 environment (back). */
int cam_open(const void *owner, int w, int h, int fps, int facing);
int cam_state(const void *owner, int id);        /* 0 waiting for permission, 1 capturing, -1 denied or failed */
int cam_source(const void *owner, int id);       /* its live source */
void cam_info(const void *owner, int id, int *w, int *h, int *fps, int *facing);
void cam_close(const void *owner, int id);
void cam_close_owner(const void *owner);
int cam_count(void);                             /* cameras present */
int cam_permission(void);                        /* 1 granted, 0 not asked yet, -1 denied */
void cam_tick(void);                             /* media_tick: takes new frames */
int cam_timeout_ms(void);                        /* -1 without an open camera */
/* screen capture for getDisplayMedia (Android: MediaProjection); one at a time, frames go to a live source */
bool screen_supported(void);
int screen_open(const void *owner, int maxw, int maxh, int fps, bool audio);   /* -1 if busy or unsupported; Android asks first; audio: the playback too */
int screen_state(const void *owner, int id);     /* 0 asking, 1 capturing, -1 refused or ended */
int screen_source(const void *owner, int id);
void screen_info(const void *owner, int id, int *w, int *h, bool *audio);   /* audio: its playback capture runs */
int screen_audio_available(const void *owner, int id);
int screen_audio_read(const void *owner, int id, float *out, int max);   /* mono float at 48 kHz */
bool screen_waiting(void);                       /* asking for the record permission before the consent dialog */
void screen_close(const void *owner, int id);
void screen_close_owner(const void *owner);
void screen_tick(void);                          /* main thread: the newest frame to its live source */
float *media_decode_pcm(const uint8_t *p, size_t n, int *frames);
