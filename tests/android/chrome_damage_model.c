#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define LMAX(left, right) ((left) > (right) ? (left) : (right))
#define LMIN(left, right) ((left) < (right) ? (left) : (right))
#define RGBA(red, green, blue, alpha) UINT32_MAX

struct DSig { uint64_t h; float b[4]; };
typedef struct { uint64_t hash; } DisplayList;
typedef struct { float scale; int stride; uint32_t *px; void *punch; bool punched; } Canvas;
typedef struct { float sy; bool loading; } Tab;
typedef struct {
    Canvas page;
    DisplayList cdl, hdl, pdl;
    uint64_t chash;
    bool osig_ok, gvid_ok;
    float last_sy;
    Tab *t;
    struct DSig *osig, *nsig;
    int nosig;
} App;

static int full_calls, rect_calls, bounds[4];
static double g_tx;
static double now_ms(void) { return 0; }
static void build_chrome(App *app) { (void)app; }
static void chrome_tip(App *app) { (void)app; }
static uint64_t dl_hash(const DisplayList *list) { return list->hash; }
static void bunion(float *dest, const float *source) {
    dest[0] = LMIN(dest[0], source[0]); dest[1] = LMIN(dest[1], source[1]);
    dest[2] = LMAX(dest[2], source[2]); dest[3] = LMAX(dest[3], source[3]);
}
static void raster(Canvas *canvas, const DisplayList *list, uint32_t color) {
    (void)canvas; (void)list; (void)color; full_calls++;
}
static void raster_rect(Canvas *canvas, const DisplayList *list, uint32_t color,
                        int left, int top, int right, int bottom) {
    (void)canvas; (void)list; (void)color; rect_calls++;
    bounds[0] = left; bounds[1] = top; bounds[2] = right; bounds[3] = bottom;
}
static void canvas_init(Canvas *canvas, int width, int height, float scale) {
    canvas->scale = scale; canvas->stride = width;
    canvas->px = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
}
static void canvas_free(Canvas *canvas) { free(canvas->px); }

int main(int argc, char **argv) {
    assert(argc == 2);
    int scenario = atoi(argv[1]);
    Tab tab = {0};
    struct DSig old_sig = {1, {10, 20, 30, 40}}, new_sig = old_sig;
    App state = {.page = {.scale = 1}, .cdl = {1}, .chash = 31,
                 .osig_ok = true, .t = &tab, .osig = &old_sig, .nsig = &new_sig, .nosig = 1};
    App *a = &state;
    int why = 1, nv = 0, nn = 1, pwp = 100, ph = 100;
    int rx0 = 100, ry0 = 100, rx1 = 0, ry1 = 0;
    bool part = false, gskip = false, gv = false;
    struct { void *img; } video = {0}, *vit = &video;
    if (scenario == 0 || scenario == 2) state.cdl.hash = 2;
    if (scenario == 2) { new_sig.h = 2; new_sig.b[2] = 35; }
    if (scenario == 3) { new_sig.h = 2; new_sig.b[2] = new_sig.b[3] = 100; }
    if (scenario == 4) nn = 0;
    if (scenario == 5) { nn = 0; state.nosig = 0; }
    if (scenario == 6) {
        new_sig.h = 2;
        for (int index = 0; index < 4; index++) old_sig.b[index] = new_sig.b[index] = 1e30f;
    }
    if (scenario == 7) state.osig_ok = false;

#include "chrome_damage_function.h"

    switch (scenario) {
        case 0:
            assert(full_calls == 0 && rect_calls == 0);
            assert(!part && !gskip);
            break;
        case 1: case 5: case 6:
            assert(full_calls == 0 && rect_calls == 0);
            assert(part && gskip);
            break;
        case 2: case 4:
            assert(full_calls == 0 && rect_calls == 1);
            assert(bounds[0] == 10 && bounds[1] == 20 && bounds[3] == 40);
            assert(bounds[2] == (scenario == 2 ? 35 : 30));
            assert(rx0 == 10 && ry0 == 20 && rx1 == bounds[2] && ry1 == 40);
            assert(!gskip && part == (scenario == 4));
            break;
        case 3: case 7:
            assert(full_calls == 1 && rect_calls == 0);
            assert(!part && !gskip);
            break;
        default: abort();
    }
    puts("PASS page-cache damage policy");
    return 0;
}
