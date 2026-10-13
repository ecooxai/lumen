#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
#define NEAR(actual, expected) (fabsf((actual) - (expected)) < 0.001f)

typedef struct { bool relayout; } Tab;
typedef struct {
    void *win, *gpu;
    Tab *tabs[2], *t;
    int ntabs, pw, ph, screen_w, screen_h;
    float vw, vh, scale, input_scale;
    bool dirty, vonly, sdirty, osig_ok, gvid_ok;
} App;
enum { SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP, SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_KEY_DOWN };
typedef union {
    int type;
    struct { int type; float x, y, xrel, yrel; } motion;
    struct { int type; float x, y; } button;
    struct { int type; float x, y, mouse_x, mouse_y; } wheel;
} SDL_Event;
static int window_width = 1080, window_height = 2200, pixel_width = 1080, pixel_height = 2200;
static float display_scale = 2.75f;
static int g_pixel_scale_pct = 200;
typedef struct { int w, h; float pixel_density; } SDL_DisplayMode;
static SDL_DisplayMode display_mode = {1080, 2248, 1};
static int SDL_GetDisplayForWindow(void *window) { (void)window; return 1; }
static const SDL_DisplayMode *SDL_GetCurrentDisplayMode(int display) { (void)display; return &display_mode; }
static int resized_width, resized_height;
static bool SDL_GetWindowSize(void *window, int *width, int *height) { (void)window; *width = window_width; *height = window_height; return true; }
static bool SDL_GetWindowSizeInPixels(void *window, int *width, int *height) { (void)window; *width = pixel_width; *height = pixel_height; return true; }
static float SDL_GetWindowDisplayScale(void *window) { (void)window; return display_scale; }
static void gpu_resize(void *gpu, int width, int height) { (void)gpu; resized_width = width; resized_height = height; }

#include "density_functions.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    Tab first = {0}, second = {0};
    App app = {.tabs = {&first, &second}, .t = &first, .ntabs = 2, .gpu = &first};
    const char *scenario = argv[1];
    if (!strcmp(scenario, "landscape")) {
        window_width = pixel_width = 2160;
        window_height = pixel_height = 976;
    }
    update_size(&app);
    if (!strcmp(scenario, "portrait") || !strcmp(scenario, "landscape")) {
        CHECK(NEAR(app.vw, window_width / 2.f));
        CHECK(NEAR(app.vh, window_height / 2.f));
        CHECK(NEAR(app.scale, 2));
        CHECK(NEAR(app.input_scale, 0.5f));
        CHECK(resized_width == pixel_width && resized_height == pixel_height);
        CHECK(first.relayout && second.relayout && app.dirty);
    } else if (!strcmp(scenario, "keyboard")) {
        window_height = pixel_height = 1261;
        update_size(&app);
        CHECK(NEAR(app.vh, 1261 / 2.f));
        CHECK(NEAR(app.scale, 2));
        window_height = pixel_height = 2200;
        update_size(&app);
        CHECK(NEAR(app.vh, 1100));
    } else if (!strcmp(scenario, "invalid")) {
        float invalid[] = {0, -1, NAN, INFINITY};
        for (int index = 0; index < 4; index++) {
            display_scale = invalid[index];
            update_size(&app);
            CHECK(NEAR(app.scale, 2) && NEAR(app.input_scale, 0.5f));
            CHECK(NEAR(app.vw, 540));
        }
    } else if (!strcmp(scenario, "change")) {
        app.vonly = app.sdirty = app.osig_ok = app.gvid_ok = true;
        g_pixel_scale_pct = 300;
        update_size(&app);
        CHECK(NEAR(app.scale, 3) && NEAR(app.vw, 360));
        CHECK(!app.vonly && !app.sdirty && !app.osig_ok && !app.gvid_ok);
    } else if (!strcmp(scenario, "physical")) {
        g_pixel_scale_pct = 100;
        window_width = 540; window_height = 1100;
        update_size(&app);
        CHECK(NEAR(app.scale, 1) && NEAR(app.input_scale, 2));
        CHECK(NEAR(app.vw, 1080) && NEAR(app.vh, 2200));
        CHECK(resized_width == 1080 && resized_height == 2200);
    } else if (!strcmp(scenario, "resolution")) {
        CHECK(app.screen_w == 1080 && app.screen_h == 2248);
        CHECK(app.pw == 1080 && app.ph == 2200);
    } else if (!strcmp(scenario, "desktop")) {
        window_width = 540; window_height = 1100;
        update_size(&app);
        CHECK(NEAR(app.scale, 2) && NEAR(app.input_scale, 1));
        CHECK(NEAR(app.vw, 540) && NEAR(app.vh, 1100));
    }
#ifdef HAVE_INPUT_ADAPTER
    else if (!strcmp(scenario, "pointer")) {
        SDL_Event event = {.motion = {SDL_EVENT_MOUSE_MOTION, 200, 400, -20, 40}};
        input_coordinates(&app, &event);
        CHECK(NEAR(event.motion.x, 100) && NEAR(event.motion.y, 200));
        CHECK(NEAR(event.motion.xrel, -10) && NEAR(event.motion.yrel, 20));
        for (int type = SDL_EVENT_MOUSE_BUTTON_DOWN; type <= SDL_EVENT_MOUSE_BUTTON_UP; type++) {
            event = (SDL_Event){.button = {type, 200, 400}};
            input_coordinates(&app, &event);
            CHECK(NEAR(event.button.x, 100) && NEAR(event.button.y, 200));
        }
    } else if (!strcmp(scenario, "wheel")) {
        SDL_Event event = {.wheel = {SDL_EVENT_MOUSE_WHEEL, 1, -0.5f, 200, 400}};
        input_coordinates(&app, &event);
        CHECK(NEAR(event.wheel.mouse_x, 100) && NEAR(event.wheel.mouse_y, 200));
        CHECK(event.wheel.x == 1 && event.wheel.y == -0.5f);
    } else if (!strcmp(scenario, "unrelated")) {
        SDL_Event event = {.motion = {SDL_EVENT_KEY_DOWN, 275, 550, -27.5f, 55}}, before = event;
        input_coordinates(&app, &event);
        CHECK(!memcmp(&event, &before, sizeof event));
    }
#endif
    else { CHECK(false); }
    return 0;
}
