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
const char *gpu_backend_name(Gpu *g);
void gpu_destroy(Gpu *g);
#endif
