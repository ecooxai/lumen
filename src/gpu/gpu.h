#ifndef LUMEN_GPU_H
#define LUMEN_GPU_H
#include <stdbool.h>
#include <stdint.h>

typedef struct Gpu Gpu;
typedef enum { GPU_SURF_METAL, GPU_SURF_XLIB, GPU_SURF_WAYLAND, GPU_SURF_ANDROID } GpuSurfKind;
typedef struct { GpuSurfKind kind; void *a; void *b; uint64_t win; } GpuSurfSrc;

Gpu *gpu_create(const GpuSurfSrc *src, int w, int h, bool prefer_vulkan);
void gpu_resize(Gpu *g, int w, int h);
bool gpu_present(Gpu *g, const uint32_t *px, int w, int h, int stride);
/* Like gpu_present but only re-uploads rows [y0,y1); the rest of the texture keeps its previous contents. */
bool gpu_present_rows(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1);
/* A video frame drawn by the GPU under the page texture at device rect (x0,y0)-(x1,y1); the page shows it through alpha-0 holes. */
typedef struct { const uint32_t *px; int w, h, stride; float x0, y0, x1, y1; } GpuVideo;
bool gpu_present_frame(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1, const GpuVideo *v);
const char *gpu_backend_name(Gpu *g);
void gpu_destroy(Gpu *g);
#endif
