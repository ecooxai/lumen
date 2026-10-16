#include "../../src/app/android_touch.h"
#include "../../src/app/android_fling.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#define BAR 44.f
#define SDL_TOUCH_MOUSEID UINT32_MAX
#define SDL_MOUSE_TOUCHID UINT64_MAX
#define SDL_BUTTON_LEFT 1
enum { SDL_EVENT_MOUSE_MOTION = 1, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
    SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_UP, SDL_EVENT_FINGER_MOTION, SDL_EVENT_FINGER_CANCELED,
    SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WILL_ENTER_BACKGROUND, SDL_EVENT_KEY_DOWN,
    SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED };
typedef uint32_t SDL_WindowID;
typedef union {
    uint32_t type;
    struct { uint32_t type; uint64_t timestamp, touchID, fingerID; float x, y; SDL_WindowID windowID; } tfinger;
    struct { uint32_t type; uint64_t timestamp; SDL_WindowID windowID; uint32_t which; float x, y; bool down; unsigned button, clicks; } button;
    struct { uint32_t type; uint64_t timestamp; SDL_WindowID windowID; uint32_t which; float x, y; } motion;
    struct { uint32_t type; uint64_t timestamp; SDL_WindowID windowID; } window;
} SDL_Event;
typedef struct { uint64_t lgen; bool info; } Tab;
typedef struct {
    AndroidTouch touch;
    AndroidFling fling;
    float fling_x, fling_y;
    Tab *touch_tab, *t;
    uint64_t touch_generation;
    unsigned touch_dom_flags;
    void *win;
    float vw, vh, side;
    double last_input;
} App;
static bool g_hud_pop;
static float g_sm_dx, g_sm_dy;
static unsigned wheel_calls;
static float wheel_x, wheel_y, wheel_dx, wheel_dy;
static unsigned dom_flags, dom_scrolls, dom_resets;
static unsigned controls_taps;
static float controls_x, controls_y;
static float page_x(const App *a, float x) { return x - a->side; }
static float page_y(const App *a, float y) { (void)a; return y - BAR; }
static void page_touch_controls(App *a, float x, float y) {
    (void)a; controls_taps++; controls_x=x; controls_y=y;
}
static unsigned android_dom_touch(App *app, const char *phase, const SDL_Event *event) {
    (void)app; (void)event;
    if (!strcmp(phase, "scroll")) dom_scrolls++;
    if (!strcmp(phase, "reset")) dom_resets++;
    return dom_flags;
}
static SDL_WindowID SDL_GetWindowID(void *window) { (void)window; return 1; }
static double now_ms(void) { return 123; }
static bool android_scroll_apply(App *app, float anchor_x, float anchor_y, float dx, float dy) {
    (void)app;
    wheel_calls++;
    wheel_x = anchor_x; wheel_y = anchor_y; wheel_dx = dx; wheel_dy = dy;
    return true;
}

#include "touch_input_function.h"

static SDL_Event finger(uint32_t type, float pointer_x, float pointer_y) {
    SDL_Event event = {0};
    event.tfinger.type = type;
    event.tfinger.windowID = 1;
    event.tfinger.touchID = event.tfinger.fingerID = 1;
    event.tfinger.timestamp = 42;
    event.tfinger.x = pointer_x; event.tfinger.y = pointer_y;
    return event;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    Tab tab = {.lgen = 1};
    App app = {.t = &tab, .vw = 400, .vh = 600, .side = 84};
    SDL_Event event;
    if (!strcmp(argv[1], "tap")) {
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        CHECK(android_touch_input(&app, &event));
        event = finger(SDL_EVENT_FINGER_UP, .5f, .5f);
        CHECK(!android_touch_input(&app, &event));
        CHECK(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.down);
        CHECK(event.button.x == 200 && event.button.y == 300);
        CHECK(event.button.timestamp == 42 && event.button.windowID == 1);
        CHECK(event.button.clicks == 1 && event.button.button == SDL_BUTTON_LEFT);
        CHECK(event.button.which == SDL_TOUCH_MOUSEID && app.last_input == 123);
        CHECK(controls_taps == 0); /* Normal tap hold runs after compatibility click. */
    } else if (!strcmp(argv[1], "scroll")) {
        g_sm_dx = g_sm_dy = 100;
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        CHECK(android_touch_input(&app, &event));
        CHECK(g_sm_dx == 0 && g_sm_dy == 0);
        event = finger(SDL_EVENT_FINGER_MOTION, .45f, .4f);
        CHECK(android_touch_input(&app, &event));
        CHECK(wheel_calls == 1 && wheel_x == 200 && wheel_y == 300);
        CHECK(fabsf(wheel_dx - 20) < .001f && fabsf(wheel_dy - 60) < .001f);
        event = finger(SDL_EVENT_FINGER_UP, .45f, .4f);
        CHECK(android_touch_input(&app, &event) && wheel_calls == 1);
        CHECK(controls_taps == 0);
    } else if (!strcmp(argv[1], "synthetic")) {
        event = (SDL_Event){.button = {.type = SDL_EVENT_MOUSE_BUTTON_DOWN, .which = SDL_TOUCH_MOUSEID}};
        CHECK(android_touch_input(&app, &event));
        event.button.which = 7;
        CHECK(!android_touch_input(&app, &event));
        event = (SDL_Event){.motion = {.type = SDL_EVENT_MOUSE_MOTION, .which = SDL_TOUCH_MOUSEID}};
        CHECK(android_touch_input(&app, &event));
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        event.tfinger.touchID = SDL_MOUSE_TOUCHID;
        CHECK(android_touch_input(&app, &event) && app.touch.count == 0);
    } else if (!strcmp(argv[1], "cancel")) {
        const uint32_t types[] = {SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WILL_ENTER_BACKGROUND,
            SDL_EVENT_KEY_DOWN, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
            SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED};
        for (unsigned index = 0; index < sizeof types / sizeof types[0]; index++) {
            event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
            android_touch_input(&app, &event);
            event = (SDL_Event){.type = types[index]};
            CHECK(!android_touch_input(&app, &event));
            event = finger(SDL_EVENT_FINGER_UP, .5f, .5f);
            CHECK(android_touch_input(&app, &event));
        }
    } else if (!strcmp(argv[1], "navigation")) {
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        android_touch_input(&app, &event);
        tab.lgen++;
        event = finger(SDL_EVENT_FINGER_UP, .5f, .5f);
        CHECK(android_touch_input(&app, &event));
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        android_touch_input(&app, &event);
        Tab other = {.lgen = tab.lgen};
        app.t = &other;
        event = finger(SDL_EVENT_FINGER_UP, .5f, .5f);
        CHECK(android_touch_input(&app, &event));
    } else if (!strcmp(argv[1], "chrome")) {
        event = finger(SDL_EVENT_FINGER_DOWN, .1f, .05f);
        android_touch_input(&app, &event);
        event = finger(SDL_EVENT_FINGER_MOTION, .5f, .5f);
        CHECK(android_touch_input(&app, &event) && wheel_calls == 0);
        event = finger(SDL_EVENT_FINGER_UP, .5f, .5f);
        CHECK(android_touch_input(&app, &event));
        event = finger(SDL_EVENT_FINGER_DOWN, .1f, .05f);
        android_touch_input(&app, &event);
        event = finger(SDL_EVENT_FINGER_UP, .1f, .05f);
        CHECK(!android_touch_input(&app, &event));
    } else if (!strcmp(argv[1], "bounds")) {
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        event.tfinger.windowID = 2;
        CHECK(android_touch_input(&app, &event) && app.touch.count == 0);
        event = finger(SDL_EVENT_FINGER_DOWN, -.1f, .5f);
        CHECK(android_touch_input(&app, &event) && app.touch.count == 0);
    } else if (!strcmp(argv[1], "dom_cancel")) {
        dom_flags = 15;
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        CHECK(android_touch_input(&app, &event));
        event = finger(SDL_EVENT_FINGER_MOTION, .5f, .3f);
        CHECK(android_touch_input(&app, &event) && !wheel_calls && !dom_scrolls);
        event = finger(SDL_EVENT_FINGER_UP, .5f, .3f);
        CHECK(android_touch_input(&app, &event));
        CHECK(controls_taps == 0);
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        android_touch_input(&app, &event);
        event = finger(SDL_EVENT_FINGER_UP, .5f, .5f);
        CHECK(android_touch_input(&app, &event));
        CHECK(controls_taps == 1); /* A suppressed compatibility click is still a tap. */
    } else if (!strcmp(argv[1], "dom_pointer")) {
        dom_flags = 10; /* pointerdown cancellation suppresses mouse, not scroll/click */
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        android_touch_input(&app, &event);
        event = finger(SDL_EVENT_FINGER_UP, .5f, .5f);
        CHECK(!android_touch_input(&app, &event) && app.touch_dom_flags == 10);
        event = finger(SDL_EVENT_FINGER_DOWN, .5f, .5f);
        android_touch_input(&app, &event);
        event = finger(SDL_EVENT_FINGER_MOTION, .5f, .3f);
        CHECK(android_touch_input(&app, &event) && wheel_calls == 1 && dom_scrolls == 1);
    } else {
        CHECK(false);
    }
    puts("Android app touch integration scenario PASS");
    return 0;
}
