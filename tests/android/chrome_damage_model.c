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
typedef enum { DO_RECT, DO_PUSH_CLIP, DO_POP_CLIP, DO_PUSH_LAYER, DO_POP_LAYER } DrawOp;
typedef struct { DrawOp op; uint64_t hash; float bounds[4]; } DItem;
typedef struct { uint64_t hash; struct { DItem *v; int n; } items; } DisplayList;
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
static void *xrealloc(void *memory, size_t size) {
    void *result = realloc(memory, size);
    assert(result);
    return result;
}
#include "chrome_damage_hash.h"

static uint64_t ditem_hash(const DItem *item) {
    return hmix((uint64_t)item->op, &item->hash, sizeof item->hash);
}
static void ditem_box(const DItem *item, float *result) {
    for (int index = 0; index < 4; index++) result[index] = item->bounds[index];
}

#include "chrome_damage_signatures.h"

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
    App state = {.page = {.scale = 1}, .cdl = {.hash = 1}, .chash = 31,
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
    struct DSig *old_items = NULL, *new_items = NULL;
    if (scenario >= 8) {
        DItem background = {DO_RECT, 10, {0, 0, 100, 100}};
        DItem text = {DO_RECT, 11, {10, 20, 30, 40}};
        DItem caret = {DO_RECT, 12, {30, 20, 32, 40}};
        DItem header = {DO_RECT, 13, {0, 0, 100, 10}};
        DItem footer = {DO_RECT, 14, {0, 90, 100, 100}};
        DItem old_draws[8] = {background, text, header, footer};
        DItem new_draws[8] = {background, text, caret, header, footer};
        int old_count = 4, new_count = 5;
        if (scenario == 10 || scenario == 11) {
            new_draws[2].bounds[1] = 120;
            new_draws[2].bounds[3] = 140;
        }
        if (scenario == 12) {
            new_draws[1].hash = 21;
            new_draws[1].bounds[0] = 5;
        }
        if (scenario == 13) {
            old_draws[2] = caret;
            old_draws[3] = header;
            old_draws[4] = footer;
            old_count = 5;
            new_draws[1] = caret;
            new_draws[2] = text;
        }
        if (scenario == 14) {
            old_draws[0] = (DItem){DO_PUSH_LAYER, 30, {1e30f, 1e30f, -1e30f, -1e30f}};
            old_draws[4] = (DItem){DO_POP_LAYER, 31, {1e30f, 1e30f, -1e30f, -1e30f}};
            old_count = 5;
            new_draws[0] = old_draws[0];
            new_draws[0].hash = 32;
            new_draws[5] = old_draws[4];
            new_count = 6;
        }
        if (scenario == 15) {
            old_draws[2] = text;
            new_draws[3] = text;
        }
        if (scenario == 16 || scenario == 18) {
            old_draws[1] = background;
            old_count = 2;
            new_draws[1] = (DItem){DO_PUSH_CLIP, 40, {20, 20, 40, 40}};
            if (scenario == 18) new_draws[1].op = DO_PUSH_LAYER;
            new_draws[2] = background;
            new_count = 3;
        }
        if (scenario == 17 || scenario == 19) {
            old_draws[0] = (DItem){DO_PUSH_CLIP, 40, {20, 20, 40, 40}};
            old_draws[1] = (DItem){DO_POP_CLIP, 41, {1e30f, 1e30f, -1e30f, -1e30f}};
            if (scenario == 19) {
                old_draws[0].op = DO_PUSH_LAYER;
                old_draws[1].op = DO_POP_LAYER;
            }
            old_draws[2] = background;
            old_count = 3;
            new_draws[0] = old_draws[0];
            new_draws[1] = background;
            new_count = 2;
        }
        DisplayList previous = {.items = {old_draws, old_count}};
        DisplayList current = {.items = {new_draws, new_count}};
        if (scenario == 9 || scenario == 11) {
            DisplayList swap = previous;
            previous = current;
            current = swap;
        }
        int old_capacity = 0, new_capacity = 0;
        state.nosig = dl_sigs(&previous, &old_items, &old_capacity);
        nn = dl_sigs(&current, &new_items, &new_capacity);
        state.osig = old_items;
        state.nsig = new_items;
    }

#include "chrome_damage_function.h"

    switch (scenario) {
        case 0:
            assert(full_calls == 0 && rect_calls == 0);
            assert(!part && !gskip);
            break;
        case 1: case 5: case 6: case 10: case 11:
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
        case 3: case 7: case 14: case 16: case 17: case 18: case 19:
            assert(full_calls == 1 && rect_calls == 0);
            assert(!part && !gskip);
            break;
        case 8: case 9: case 12: case 13: case 15:
            assert(full_calls == 0 && rect_calls == 1);
            assert(part && !gskip);
            assert(bounds[0] == (scenario == 12 ? 5 : scenario == 13 ? 10 : 30));
            assert(bounds[1] == 20 && bounds[2] == 32 && bounds[3] == 40);
            break;
        default: abort();
    }
    free(old_items);
    free(new_items);
    puts("PASS page-cache damage policy");
    return 0;
}
