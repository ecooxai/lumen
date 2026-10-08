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

/* 0 = no, 1 = maybe, 2 = probably */
int media_can_play(const char *mime, bool mse);

/* Main-thread pump: advances clocks and presents frames. Bit 0: a frame changed, bit 1: a video size changed. */
int media_tick(void);
int media_timeout_ms(void);              /* -1 when nothing is playing */
Image *media_frame_for(Node *element);
bool media_is_frame(const Image *im);   /* borrowed; valid until a later media_tick */
extern void (*media_wakeup)(void);
size_t media_mem_bytes(size_t *frames);
extern bool media_lowmem;   /* hidden players skip RGBA conversion */
void media_mark_visible(bool (*vis)(Node *, void *), void *ud);

