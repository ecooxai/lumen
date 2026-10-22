#ifndef LUMEN_WEBGPU_H
#define LUMEN_WEBGPU_H
/* WebGPU for pages (src/js/webgpu.cc). Pages share the compositor's device when it has the adapter's features
   and limits (gpu_shared): a presented canvas texture then goes to the canvas's media player as a GPU frame the
   compositor samples in place. Otherwise, and for see-through or captured canvases, each presented texture is
   copied into a mapped buffer and webgpu_tick hands the newest finished one over as pixels, like a WebGL frame. */
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
   canvas's player and its captureStream() source (capture > 0); true if the frame has alpha below 1. */
enum { WGF_BGRA8, WGF_RGBA8, WGF_RGBA16F };
typedef struct WgFrames WgFrames;
WgFrames *wgf_new(void);
void wgf_free(WgFrames *f);
bool wgf_push(WgFrames *f, struct MediaPlayer *m, int capture, const uint8_t *rows, size_t stride, int w, int h, int fmt, bool opaque);
/* A frame left on the GPU: a WebGPU canvas texture the presenter samples in place (Image.gpu). view and dev are
   a WGPUTextureView and its WGPUDevice, flags GPU_VIEW_* (gpu/gpu.h). read fills px (w * h premultiplied
   0xAARRGGBB words) from the GPU, on the main thread only; release drops the Image's reference. */
typedef struct ImgGpu { void *view, *dev; int flags; bool (*read)(struct ImgGpu *g, uint32_t *px, int w, int h); void (*release)(struct ImgGpu *g); } ImgGpu;
/* a frame of g (w x h, opaque) to the player; takes one reference of g */
void wgf_push_gpu(struct MediaPlayer *m, ImgGpu *g, int w, int h);
/* the conversion wgf_push does, into px (w * h words): true if some pixel's alpha is below 1 */
bool wgf_convert(uint32_t *px, const uint8_t *rows, size_t stride, int w, int h, int fmt, bool opaque);
#ifdef __cplusplus
}
#endif
#endif
