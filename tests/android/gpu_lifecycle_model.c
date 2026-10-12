#include <assert.h>
#include <android/native_window.h>
#include "../../src/gpu/gpu.c"

static int created, configured, unconfigured, released, capabilities_freed;
static bool fail_create, fail_caps, incompatible;
static WGPUSurface live_surface;
static ANativeWindow *live_window;
static WGPUTextureFormat formats[] = {WGPUTextureFormat_BGRA8Unorm};

void ANativeWindow_acquire(ANativeWindow *window) {
    assert(window && window->references == 1);
    window->references++;
}

void ANativeWindow_release(ANativeWindow *window) {
    assert(window && window->references == 2);
    assert(!live_surface);
    window->references--;
}

WGPUSurface wgpuInstanceCreateSurface(WGPUInstance instance, const WGPUSurfaceDescriptor *descriptor) {
    assert(instance && descriptor && descriptor->nextInChain);
    assert(descriptor->nextInChain->sType == WGPUSType_SurfaceSourceAndroidNativeWindow);
    live_window = ((const WGPUSurfaceSourceAndroidNativeWindow *)descriptor->nextInChain)->window;
    assert(live_window->references == 2);
    assert(!live_surface);
    created++;
    if (fail_create) return NULL;
    live_surface = (WGPUSurface)(uintptr_t)(32 + created);
    return live_surface;
}

WGPUStatus wgpuSurfaceGetCapabilities(WGPUSurface surface, WGPUAdapter adapter, WGPUSurfaceCapabilities *caps) {
    assert(surface == live_surface && adapter);
    if (fail_caps) return WGPUStatus_Error;
    formats[0] = incompatible ? WGPUTextureFormat_RGBA8Unorm : WGPUTextureFormat_BGRA8Unorm;
    caps->formatCount = 1;
    caps->formats = formats;
    return WGPUStatus_Success;
}

void wgpuSurfaceCapabilitiesFreeMembers(WGPUSurfaceCapabilities caps) {
    (void)caps;
    capabilities_freed++;
}

void wgpuSurfaceConfigure(WGPUSurface surface, const WGPUSurfaceConfiguration *config) {
    assert(surface && surface == live_surface);
    assert(config->device && config->width && config->height);
    assert(config->format == WGPUTextureFormat_BGRA8Unorm);
    assert(live_window->references == 2);
    configured++;
}

void wgpuSurfaceUnconfigure(WGPUSurface surface) {
    assert(surface && surface == live_surface);
    assert(live_window->references == 2);
    unconfigured++;
}

void wgpuSurfaceRelease(WGPUSurface surface) {
    assert(surface && surface == live_surface);
    assert(live_window->references == 2);
    released++;
    live_surface = NULL;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    int scenario = atoi(argv[1]);
    ANativeWindow first = {1}, second = {1};
    Gpu state = {.inst = (WGPUInstance)(uintptr_t)1, .adapter = (WGPUAdapter)(uintptr_t)2,
                 .dev = (WGPUDevice)(uintptr_t)3, .tex = (WGPUTexture)(uintptr_t)4,
                 .fmt = WGPUTextureFormat_BGRA8Unorm, .tw = 200, .th = 300};
    GpuSurfSrc source = {.kind = GPU_SURF_ANDROID, .a = &first};
    if (scenario == 1) {
        gpu_resize(&state, 100, 200);
        assert(configured == 0 && !state.configured);
    }
    if (scenario == 2) fail_create = true;
    if (scenario == 3) fail_caps = true;
    if (scenario == 4) incompatible = true;
    if (scenario == 5) {
        assert(!gpu_resume(&state, &source, 0, 200));
        assert(!gpu_resume(&state, NULL, 100, 200));
        source.a = NULL;
        assert(!gpu_resume(&state, &source, 100, 200));
        assert(created == 0 && first.references == 1);
        gpu_suspend(NULL);
        assert(!gpu_resume(NULL, &source, 100, 200));
        return 0;
    }
    bool resumed = gpu_resume(&state, &source, 100, 200);
    if (scenario >= 2 && scenario <= 4) {
        assert(!resumed && !state.configured && !state.surf);
        assert(first.references == 1 && configured == 0);
        assert(released == (scenario == 2 ? 0 : 1));
        assert(capabilities_freed == (scenario == 2 ? 0 : 1));
        gpu_suspend(&state);
        return 0;
    }
    assert(resumed && state.configured && state.surf == live_surface);
    assert(state.w == 100 && state.h == 200 && first.references == 2);
    if (scenario == 6) {
        source.a = &second;
        assert(gpu_resume(&state, &source, 200, 100));
        assert(first.references == 1 && second.references == 2);
        for (int count = 0; count < 10; count++) {
            gpu_suspend(&state);
            assert(gpu_resume(&state, &source, 100, 200));
        }
    }
    if (scenario == 7) {
        gpu_resize(&state, 0, 0);
        assert(!state.configured && unconfigured == 1);
        gpu_resize(&state, 200, 100);
        assert(state.configured && configured == 2);
    }
    gpu_suspend(&state);
    assert(!state.configured && !state.surf && !live_surface);
    assert(first.references == 1 && second.references == 1);
    assert(created == released && configured == unconfigured);
    int release_count = released;
    gpu_suspend(&state);
    gpu_resize(&state, 300, 400);
    assert(released == release_count && !state.configured);
    assert(state.dev == (WGPUDevice)(uintptr_t)3 && state.tex == (WGPUTexture)(uintptr_t)4);
    assert(state.tw == 200 && state.th == 300);
    puts("PASS native surface lifecycle and retained device/cache");
    return 0;
}
