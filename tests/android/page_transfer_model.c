#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LMIN(left, right) ((left) < (right) ? (left) : (right))
#define RGBA(red, green, blue, alpha) 0xffffffffu
#define SDL_PIXELFORMAT_ARGB8888 1
#define SDL_SCALEMODE_LINEAR 1
#define PAINT_COMPOSE 1

typedef struct { int w, h, stride; uint32_t *px; float scale; } Canvas;
typedef struct { struct { int n; } items; } DisplayList;
typedef struct { int unused; } GpuVideo;
typedef struct { bool blit; } PaintFrame;
typedef struct {
    Canvas frame, page;
    void *gpu, *win;
    DisplayList hdl;
    bool frame_page_stale;
} App;
typedef struct { const uint32_t *pixels; int width, height, stride; } SDL_Surface;

static uint32_t gpu_pixels[32], software_pixels[32];
static unsigned uploads, presents, software_presents, copy_calls;
static size_t copied_bytes;
static bool upload_allowed = true, present_allowed = true, surface_available = true;
static bool shot, recreated;
static bool scroll_allowed;
static unsigned scroll_uploads;
int scroll_delta = 2, scroll_fixed_first = 1, scroll_fixed_last = 3;
static float g_pop[4] = {0, 3, 1, 4};

static void *counted_memcpy(void *destination, const void *source, size_t size) {
    copy_calls++;
    copied_bytes += size;
    return memcpy(destination, source, size);
}

#ifdef __ANDROID__
static const char *test_getenv(const char *name) {
    assert(!strcmp(name, "LUMEN_SHOT"));
    return shot ? "test.png" : NULL;
}
#endif

bool gpu_scroll_page(void *gpu, const uint32_t *pixels, int width, int height,
                     int stride, int toolbar, int delta, int first, int last) {
    assert(gpu && width == 4 && height <= 8 && toolbar == 2);
    assert(delta == 2 && first == 1 && last == 3);
    scroll_uploads++;
    if (!scroll_allowed) return false;
    for (int row = 0; row < height - toolbar; row++)
        memcpy(gpu_pixels + (row + toolbar) * width, pixels + row * stride, (size_t)width * 4);
    return true;
}

static bool gpu_upload_page(void *gpu, const uint32_t *pixels, int width, int height,
                            int stride, int toolbar, int first, int last) {
    assert(gpu && width == 4 && height <= 8 && toolbar == 2);
    uploads++;
    if (!upload_allowed) return false;
    for (int row = first; row < last; row++)
        memcpy(gpu_pixels + (row + toolbar) * width, pixels + row * stride, (size_t)width * 4);
    return true;
}

static bool gpu_present_frame(void *gpu, const uint32_t *pixels, int width, int height,
                              int stride, int first, int last, const GpuVideo *video) {
    assert(gpu && width == 4 && height <= 8);
    (void)video;
    presents++;
    if (recreated) { first = 0; last = height; }
    for (int row = first; row < last; row++)
        memcpy(gpu_pixels + row * width, pixels + row * stride, (size_t)width * 4);
    return present_allowed;
}

static void raster_rect(Canvas *canvas, const DisplayList *list, uint32_t background,
                        int left, int top, int right, int bottom) {
    assert(list->items.n == 1 && background == 0xffffffffu);
    assert(left == 0 && top == 3 && right == 1 && bottom == 4);
    canvas->px[top * canvas->stride + left] = 0xff123456u;
}

static void paint_profile_stage(PaintFrame *frame, int stage) {
    assert(frame && stage == PAINT_COMPOSE);
}

static SDL_Surface *SDL_GetWindowSurface(void *window) {
    static SDL_Surface surface;
    assert(window);
    return surface_available ? &surface : NULL;
}

static SDL_Surface *SDL_CreateSurfaceFrom(int width, int height, int format,
                                          const void *pixels, int pitch) {
    static SDL_Surface surface;
    assert(format == SDL_PIXELFORMAT_ARGB8888);
    surface = (SDL_Surface){pixels, width, height, pitch / 4};
    return &surface;
}

static void SDL_BlitSurfaceScaled(SDL_Surface *source, const void *source_rect,
                                  SDL_Surface *destination, const void *destination_rect, int mode) {
    assert(!source_rect && !destination_rect && destination && mode == SDL_SCALEMODE_LINEAR);
    for (int row = 0; row < source->height; row++)
        memcpy(software_pixels + row * source->width, source->pixels + row * source->stride,
               (size_t)source->width * 4);
    software_presents++;
}

static void SDL_DestroySurface(SDL_Surface *surface) { assert(surface); }
static void SDL_UpdateWindowSurface(void *window) { assert(window); }

#define memcpy counted_memcpy
#include "compose_page_function.h"
static void transfer(App *app, bool part, bool gskip, int bar_px, int side_px,
                     int ph, int ry0, int ry1, const GpuVideo *gvp, PaintFrame profile_frame) {
#define getenv test_getenv
#include "page_transfer_function.h"
#undef getenv
}
#undef memcpy

static void verify_frame(const App *app, int sidebar, bool popup, bool software) {
    for (int row = 0; row < app->frame.h; row++) for (int column = 0; column < app->frame.w; column++) {
        uint32_t expected = 0x12000000u + (uint32_t)(row * 4 + column);
        if (row >= 2 && column >= sidebar)
            expected = app->page.px[(row - 2) * app->page.stride + column - sidebar];
        if (popup && row == 3 && column == 0) expected = 0xff123456u;
        assert(app->frame.px[row * app->frame.stride + column] == expected);
        assert((software ? software_pixels : gpu_pixels)[row * 4 + column] == expected);
    }
    assert(!app->frame_page_stale);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    uint32_t frame_pixels[32], page_pixels[24];
    for (size_t index = 0; index < 32; index++) frame_pixels[index] = gpu_pixels[index] = 0x12000000u + (uint32_t)index;
    for (size_t index = 0; index < 24; index++) page_pixels[index] = 0x81000000u + (uint32_t)index;
    App state = {.frame = {4, 8, 4, frame_pixels, 1}, .page = {4, 6, 4, page_pixels, 1},
                 .gpu = (void *)(uintptr_t)1, .win = (void *)(uintptr_t)2};
    PaintFrame profile = {.blit = true};
    const char *scenario = argv[1];
    if (!strcmp(scenario, "ring") || !strcmp(scenario, "ring-failure")) {
        scroll_allowed = true;
        present_allowed = !strcmp(scenario, "ring");
        transfer(&state, true, false, 2, 0, 6, 0, 6, NULL, profile);
        assert(scroll_uploads == 1 && uploads == 0);
        if (present_allowed) {
            assert(copy_calls == 0 && state.frame_page_stale);
            assert(memcmp(gpu_pixels + 8, page_pixels, sizeof page_pixels) == 0);
        } else {
            assert(copy_calls == 1 && software_presents == 1);
            verify_frame(&state, 0, false, true);
        }
        return 0;
    }
    if (!strcmp(scenario, "sidebar")) {
        state.page.w = 3;
        transfer(&state, true, false, 2, 1, 6, 0, 6, NULL, profile);
        assert(uploads == 0 && copied_bytes == 3 * 6 * 4);
        verify_frame(&state, 1, false, false);
        return 0;
    }
    if (!strcmp(scenario, "other")) {
        transfer(&state, true, false, 2, 0, 6, 0, 6, NULL, profile);
        assert(uploads == 0 && copy_calls == 1);
        verify_frame(&state, 0, false, false);
        return 0;
    }
    transfer(&state, true, false, 2, 0, 6, 0, 6, NULL, profile);
    assert(uploads == 1 && copy_calls == 0 && copied_bytes == 0 && state.frame_page_stale);
    for (size_t index = 0; index < 32; index++) assert(frame_pixels[index] == 0x12000000u + (uint32_t)index);
    assert(memcmp(gpu_pixels + 8, page_pixels, sizeof page_pixels) == 0);
    if (!strcmp(scenario, "direct")) return 0;
    for (size_t index = 0; index < 24; index++) page_pixels[index] += 100;
    bool partial = true, skip = false;
    int first = 0, last = 6;
    GpuVideo video = {0};
    const GpuVideo *video_ptr = NULL;
    if (!strcmp(scenario, "repeat")) {}
    else if (!strcmp(scenario, "damage")) { profile.blit = false; first = 2; last = 3; }
    else if (!strcmp(scenario, "skipped")) { skip = true; recreated = true; }
    else if (!strcmp(scenario, "full")) partial = false;
    else if (!strcmp(scenario, "popup")) state.hdl.items.n = 1;
    else if (!strcmp(scenario, "video")) video_ptr = &video;
    else if (!strcmp(scenario, "screenshot")) shot = true;
    else if (!strcmp(scenario, "declined")) upload_allowed = false;
    else if (!strcmp(scenario, "present-failure")) present_allowed = false;
    else if (!strcmp(scenario, "no-surface")) { present_allowed = false; surface_available = false; }
    else if (!strcmp(scenario, "software")) state.gpu = NULL;
    else if (!strcmp(scenario, "resize")) { partial = false; state.frame.h = 7; state.page.h = last = 5; recreated = true; }
    else assert(false);
    transfer(&state, partial, skip, 2, 0, state.page.h, first, last, video_ptr, profile);
    if (!strcmp(scenario, "repeat")) {
        assert(uploads == 2 && copy_calls == 0 && state.frame_page_stale);
        assert(memcmp(gpu_pixels + 8, page_pixels, sizeof page_pixels) == 0);
    } else {
        bool software = !present_allowed || !state.gpu;
        assert(copy_calls == 1 && copied_bytes == (size_t)state.page.w * (size_t)state.page.h * 4);
        if (!surface_available) {
            assert(!state.frame_page_stale && software_presents == 0);
            assert(memcmp(frame_pixels + 8, page_pixels, sizeof page_pixels) == 0);
        } else verify_frame(&state, 0, state.hdl.items.n != 0, software);
        if (software && surface_available) assert(software_presents == 1);
    }
    return 0;
}
