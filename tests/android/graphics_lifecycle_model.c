#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../../src/gpu/gpu.h"

#define SDLCALL
#define SDL_EVENT_WILL_ENTER_BACKGROUND 1
#define SDL_EVENT_DID_ENTER_FOREGROUND 2
#define SDL_LOG_CATEGORY_APPLICATION 0
#define SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER "native-window"
typedef struct { int type; } SDL_Event;
typedef struct {
    void *win;
    Gpu *gpu;
    bool android_suspended, dirty, vonly, gvid_ok;
} App;
static App app;
static int suspends, resumes, errors;
static bool resume_ok = true;
static void *native_window = (void *)(uintptr_t)20;

static unsigned SDL_GetWindowProperties(void *window) {
    assert(window == app.win);
    return 7;
}
static void *SDL_GetPointerProperty(unsigned properties, const char *name, void *fallback) {
    assert(properties == 7 && name && !fallback);
    return native_window;
}
static bool SDL_GetWindowSizeInPixels(void *window, int *width, int *height) {
    assert(window == app.win);
    *width = 200; *height = 300;
    return true;
}
static void SDL_LogError(int category, const char *message, ...) {
    assert(category == SDL_LOG_CATEGORY_APPLICATION && message);
    errors++;
}
void gpu_suspend(Gpu *gpu) {
    assert(gpu == app.gpu && app.android_suspended);
    suspends++;
}
bool gpu_resume(Gpu *gpu, const GpuSurfSrc *source, int width, int height) {
    assert(gpu == app.gpu && app.android_suspended);
    assert(source->kind == GPU_SURF_ANDROID && source->a == native_window);
    assert(width == 200 && height == 300);
    resumes++;
    return resume_ok;
}

#include "android_graphics_lifecycle.h"

int main(void) {
    app.win = (void *)(uintptr_t)1;
    app.gpu = (Gpu *)(uintptr_t)2;
    app.vonly = app.gvid_ok = true;
    SDL_Event event = {99};
    assert(android_graphics_lifecycle(&app, &event));
    assert(suspends == 0 && resumes == 0 && !app.android_suspended);
    event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
    assert(android_graphics_lifecycle(&app, &event));
    assert(resumes == 0);
    for (int count = 0; count < 3; count++) {
        event.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        assert(android_graphics_lifecycle(&app, &event));
        assert(app.android_suspended);
        native_window = (void *)(uintptr_t)(30 + count);
        event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        assert(android_graphics_lifecycle(&app, &event));
        assert(!app.android_suspended && app.dirty && !app.vonly && !app.gvid_ok);
        assert(suspends == count + 1 && resumes == count + 1);
    }
    event.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
    assert(android_graphics_lifecycle(&app, &event));
    resume_ok = false;
    event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
    assert(android_graphics_lifecycle(&app, &event));
    assert(app.android_suspended && errors == 1);
    resume_ok = true;
    assert(android_graphics_lifecycle(&app, &event));
    assert(!app.android_suspended && resumes == 5);
    app.gpu = NULL;
    event.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
    assert(android_graphics_lifecycle(&app, &event));
    event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
    assert(android_graphics_lifecycle(&app, &event));
    assert(!app.android_suspended && resumes == 5);
    puts("PASS synchronous SDL lifecycle, fresh native window and safe failure");
    return 0;
}
