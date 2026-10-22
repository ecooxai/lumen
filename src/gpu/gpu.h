#ifndef LUMEN_GPU_H
#define LUMEN_GPU_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Gpu Gpu;
typedef enum { GPU_BACKEND_AUTO, GPU_BACKEND_VULKAN, GPU_BACKEND_GL, GPU_BACKEND_CPU } GpuBackend;
typedef enum { GPU_SURF_METAL, GPU_SURF_XLIB, GPU_SURF_WAYLAND, GPU_SURF_ANDROID } GpuSurfKind;
typedef struct { GpuSurfKind kind; void *a; void *b; uint64_t win; } GpuSurfSrc;

Gpu *gpu_create(const GpuSurfSrc *src, int w, int h, bool prefer_vulkan);
Gpu *gpu_create_backend(const GpuSurfSrc *src, int w, int h, GpuBackend backend);
void gpu_suspend(Gpu *gpu);
bool gpu_resume(Gpu *gpu, const GpuSurfSrc *source, int width, int height);
void gpu_resize(Gpu *g, int w, int h);
bool gpu_present(Gpu *g, const uint32_t *px, int w, int h, int stride);
/* Like gpu_present but only re-uploads rows [y0,y1); the rest of the texture keeps its previous contents. */
bool gpu_present_rows(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1);
/* A video frame drawn by the GPU under the page texture at device rect (x0,y0)-(x1,y1); the page shows it through alpha-0 holes. */
/* yuv, when set, replaces px: NV12 planes (Y, then interleaved UV); mat bit 1 = BT.709, bit 2 = full range. */
/* A nonzero serial identifies immutable frame pixels across all video owners.
   Zero keeps unconditional uploads for callers without frame identity. */
/* view, when set, replaces px and yuv: a WGPUTextureView on the device of view_dev (a WebGPU canvas texture, sampled
   in place); it is drawn only when that is the presenter's device. view_flags: GPU_VIEW_* */
typedef struct { const uint32_t *px; int w, h, stride; float x0, y0, x1, y1; const uint8_t *yuv; int mat; uint64_t serial;
                 const void *view, *view_dev; int view_flags; } GpuVideo;
#define GPU_VIEW_OPAQUE 1   /* alpha reads as 1 (a canvas whose alphaMode is opaque) */
bool gpu_yuv_ok(Gpu *g);
bool gpu_upload_page(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                     int toolbar, int first_row, int last_row);
bool gpu_scroll_page(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                     int toolbar, int delta, int fixed_first, int fixed_last);
/* source_offset maps logical row zero into a circular CPU page allocation. */
bool gpu_upload_page_ring(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                          int toolbar, int first_row, int last_row, int source_offset);
bool gpu_scroll_page_ring(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                          int toolbar, int delta, int fixed_first, int fixed_last, int source_offset);
/* columns x0..x1 of the rows (a fixed side column after a scroll) */
bool gpu_upload_page_rect_ring(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                               int toolbar, int x0, int x1, int first_row, int last_row, int source_offset);
bool gpu_present_frame(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1, const GpuVideo *v);
/* Up to GPU_EXTRA_LAYERS more RGBA frames (px required, yuv ignored) drawn like v by the next gpu_present_frame:
   several canvases or videos on one page stay GPU layers instead of being rastered each frame. */
#define GPU_EXTRA_LAYERS 3
void gpu_set_extra_layers(Gpu *g, const GpuVideo *v, int n);
const char *gpu_backend_name(Gpu *g);
/* The presenter's wgpu instance, adapter, device and queue (unowned), for WebGPU pages to share so their canvases
   are sampled in place. False without a presenter, or when its device lacks the adapter's features and limits. */
typedef struct { void *inst, *adapter, *dev, *queue; } GpuShared;
bool gpu_shared(GpuShared *out);
void gpu_log(const char *format, ...);   /* logcat LumenGPU on Android, else stderr */
/* errors on the presenter's device go here first; true when it took them (WebGPU calls made for a page) */
extern bool (*gpu_error_hook)(int type, const char *msg, size_t len);
void gpu_destroy(Gpu *g);
#endif
