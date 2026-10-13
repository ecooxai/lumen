#include "gpu/gpu.h"
#include "app/android_touch.h"
#include "app/android_fling.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#define SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER "native-window"
#define SDL_LOG_CATEGORY_APPLICATION 0
typedef struct {
    void *win;
    Gpu *gpu;
    int pw, ph;
    AndroidTouch touch;
    AndroidFling fling;
    bool vonly, sdirty, gvid_ok, osig_ok, dirty;
} App;
static GpuBackend g_renderer = GPU_BACKEND_VULKAN;
static const char *renderer_key(GpuBackend backend) { (void)backend; return "test"; }
static bool media_yuv, fail_create;
static float g_sm_dx, g_sm_dy;
static unsigned sequence, destroyed, surface_destroyed, created;
static int old_gpu, next_gpu, native_window;
void gpu_destroy(Gpu *gpu) { CHECK(gpu == (Gpu *)&old_gpu || !gpu); destroyed = ++sequence; }
static bool SDL_DestroyWindowSurface(void *window) { (void)window; surface_destroyed = ++sequence; return true; }
static void *SDL_GetWindowProperties(void *window) { return window; }
static void *SDL_GetPointerProperty(void *props, const char *key, void *fallback) { (void)props; (void)key; (void)fallback; return &native_window; }
static void SDL_Log(const char *format, ...) { (void)format; }
Gpu *gpu_create_backend(const GpuSurfSrc *source, int width, int height, GpuBackend backend) {
    CHECK(destroyed && surface_destroyed > destroyed);
    CHECK(source->kind == GPU_SURF_ANDROID && source->a == &native_window);
    CHECK(width == 1080 && height == 2112 && backend == g_renderer);
    created = ++sequence;
    return fail_create || backend == GPU_BACKEND_CPU ? NULL : (Gpu *)&next_gpu;
}
bool gpu_yuv_ok(Gpu *gpu) { return gpu != NULL; }
const char *gpu_backend_name(Gpu *gpu) { return gpu ? "wgpu/vulkan" : "cpu"; }
void android_touch_reset(AndroidTouch *touch) { touch->count = 0; }
void android_fling_reset(AndroidFling *fling) { fling->active = false; }
#include "renderer_switch_function.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    App app = {.gpu = (Gpu *)&old_gpu, .pw = 1080, .ph = 2112,
        .vonly = true, .sdirty = true, .gvid_ok = true, .osig_ok = true};
    app.touch.count = 1; app.fling.active = true; g_sm_dx = g_sm_dy = 100;
    if (argv[1][0] == 's') {
        renderer_apply(&app, GPU_BACKEND_GL);
        CHECK(created && app.gpu == (Gpu *)&next_gpu && g_renderer == GPU_BACKEND_GL && media_yuv);
    } else {
        fail_create = true;
        renderer_apply(&app, GPU_BACKEND_VULKAN);
        CHECK(created && !app.gpu && !media_yuv);
        renderer_apply(&app, GPU_BACKEND_CPU);
        CHECK(!app.gpu && !media_yuv && g_renderer == GPU_BACKEND_CPU);
    }
    CHECK(app.dirty && !app.vonly && !app.sdirty && !app.gvid_ok && !app.osig_ok);
    CHECK(!app.touch.count && !app.fling.active && !g_sm_dx && !g_sm_dy);
    puts("Renderer switch scenario PASS");
    return 0;
}
