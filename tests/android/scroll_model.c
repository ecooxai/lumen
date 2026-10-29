#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#define BAR 44.f
#define LCLAMP(value, low, high) fminf(fmaxf(value, low), high)
typedef struct { bool internal_ui; } Page;
typedef struct { float sy; bool asleep, info; Page *cur; int zoom_pct; } Tab;
typedef struct { Tab *t; float side, scale; bool dirty, vonly; } App;
static int g_page_zoom_pct=100;
static bool nested;
static unsigned calls;
static float anchor_x, anchor_y, delta_x, delta_y;
static float max_scroll(App *app) { (void)app; return 1000; }
static bool wheel_scroll(App *app, float x, float y, float dx, float dy) {
    calls++;
    anchor_x = x; anchor_y = y; delta_x = dx; delta_y = dy;
    if (nested) app->dirty = true;
    return nested;
}
#include "scroll_functions.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    Page page={0}; Tab tab = {.cur = &page};
    App app = {.t = &tab, .side = 0, .scale = 2};
    if (!strcmp(argv[1], "clamp")) {
        tab.sy = 990;
        CHECK(android_scroll_apply(&app, 200, 300, 0, 100));
        CHECK(tab.sy == 1000 && app.dirty && app.vonly);
        CHECK(android_scroll_apply(&app, 200, 300, 0, -2000));
        CHECK(tab.sy == 0);
        app.dirty = true; app.vonly = false;
        CHECK(android_scroll_apply(&app, 200, 300, 0, 10));
        CHECK(!app.vonly);
    } else if (!strcmp(argv[1], "boundary")) {
        CHECK(!android_scroll_apply(&app, 200, 300, 0, -20));
        CHECK(!app.dirty);
        tab.sy = 1000;
        CHECK(!android_scroll_apply(&app, 200, 300, 0, 20));
        CHECK(!app.dirty);
        CHECK(!android_scroll_apply(&app, 200, 300, 0, 0));
        CHECK(!app.dirty);
    } else if (!strcmp(argv[1], "nested")) {
        nested = true;
        CHECK(android_scroll_apply(&app, 200, 300, 15, 25));
        CHECK(calls == 1 && anchor_x == 200 && anchor_y == 256);
        CHECK(delta_x == 15 && delta_y == 25 && tab.sy == 0 && app.dirty);
    } else if (!strcmp(argv[1], "pixels")) {
        app.scale = 1.5f;
        CHECK(android_scroll_apply(&app, 200, 300, 0, 10.2f));
        CHECK(fabsf(tab.sy * app.scale - roundf(tab.sy * app.scale)) < .001f);
    } else if (!strcmp(argv[1], "zoom")) {
        tab.zoom_pct=200;app.side=10;
        CHECK(android_scroll_apply(&app,210,300,0,40));
        CHECK(tab.sy==20 && tab.sy*page_scale(&app)==80);
        nested=true;
        CHECK(android_scroll_apply(&app,210,300,20,40));
        CHECK(anchor_x==100 && anchor_y==128 && delta_x==10 && delta_y==20);
        tab.zoom_pct=125;app.scale=1.5f;nested=false;
        CHECK(android_scroll_apply(&app,210,300,0,10.2f));
        CHECK(fabsf(tab.sy*page_scale(&app)-roundf(tab.sy*page_scale(&app)))<.001f);
        page.internal_ui=true;CHECK(tab_zoom(&tab)==1);
        page.internal_ui=false;tab.zoom_pct=0;g_page_zoom_pct=150;
        CHECK(tab_zoom(&tab)==1.5f);
    } else if (!strcmp(argv[1], "invalid")) {
        CHECK(!android_scroll_apply(&app, 200, 300, NAN, 20));
        CHECK(!app.dirty && !calls);
        CHECK(!android_scroll_apply(&app, 200, 20, 0, 20));
        tab.asleep = true;
        CHECK(!android_scroll_apply(&app, 200, 300, 0, 20));
        tab.asleep = false; tab.info = true;
        CHECK(!android_scroll_apply(&app, 200, 300, 0, 20));
        tab.info = false; tab.cur = NULL;
        CHECK(!android_scroll_apply(&app, 200, 300, 0, 20));
        CHECK(!app.dirty && !calls);
    } else CHECK(false);
    puts("Android scroll scenario PASS");
    return 0;
}
