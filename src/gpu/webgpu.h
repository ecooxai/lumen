#ifndef LUMEN_WEBGPU_H
#define LUMEN_WEBGPU_H
/* WebGPU for pages (src/js/webgpu.cc): a wgpu-native instance of its own, separate from the compositor's.
   Canvases configured for WebGPU copy each presented texture into a mapped buffer; webgpu_tick hands the
   newest finished one to the canvas's media player, like a WebGL frame. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct MediaPlayer;
void webgpu_tick(void);                    /* main thread, from media_tick: polls devices, finishes frames */
int webgpu_timeout_ms(void);               /* -1 when nothing is in flight */
void webgpu_close_owner(const void *owner);

/* Canvas frames (src/gpu/webgpu_frame.c): mapped rows of a presented texture to a pooled frame for the
   canvas's player and its captureStream() source (capture > 0). */
enum { WGF_BGRA8, WGF_RGBA8, WGF_RGBA16F };
typedef struct WgFrames WgFrames;
WgFrames *wgf_new(void);
void wgf_free(WgFrames *f);
void wgf_push(WgFrames *f, struct MediaPlayer *m, int capture, const uint8_t *rows, size_t stride, int w, int h, int fmt, bool opaque);
#ifdef __cplusplus
}
#endif
#endif
