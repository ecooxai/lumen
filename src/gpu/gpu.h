#ifndef LUMEN_GPU_H
#define LUMEN_GPU_H
#include <stdbool.h>
#include <stdint.h>

typedef struct Gpu Gpu;
typedef enum { GPU_SURF_METAL, GPU_SURF_XLIB, GPU_SURF_WAYLAND, GPU_SURF_ANDROID } GpuSurfKind;
typedef struct { GpuSurfKind kind; void *a; void *b; uint64_t win; } GpuSurfSrc;

/* backend: user choice from Settings; adapter: index into gpu_adapters() for that backend, -1 = best available */
typedef enum { GPU_BACKEND_AUTO, GPU_BACKEND_VULKAN, GPU_BACKEND_GL, GPU_BACKEND_CPU, GPU_BACKEND_METAL } GpuBackend;
typedef struct { int backend, adapter; bool allow_cpu; } GpuOpts;
Gpu *gpu_create(const GpuSurfSrc *src, int w, int h, const GpuOpts *o);
/* Hardware adapters usable with `backend` ("name (vulkan, discrete)"); returns the count written. */
int gpu_adapters(int backend, char (*names)[128], int max);
const char *gpu_adapter_name(Gpu *g);
void gpu_resize(Gpu *g, int w, int h);
bool gpu_present(Gpu *g, const uint32_t *px, int w, int h, int stride);
/* Like gpu_present but only re-uploads rows [y0,y1); the rest of the texture keeps its previous contents. */
bool gpu_present_rows(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1);
/* A video frame drawn by the GPU under the page texture at device rect (x0,y0)-(x1,y1); the page shows it through alpha-0 holes. */
/* yuv, when set, replaces px: NV12 planes (Y, then interleaved UV); mat bit 1 = BT.709, bit 2 = full range. */
typedef struct { const uint32_t *px; int w, h, stride; float x0, y0, x1, y1; const uint8_t *yuv; int mat; } GpuVideo;
bool gpu_yuv_ok(Gpu *g);
bool gpu_present_frame(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1, const GpuVideo *v);
const char *gpu_backend_name(Gpu *g);
void gpu_destroy(Gpu *g);
#endif
