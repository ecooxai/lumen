#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LMAX(left, right) ((left) > (right) ? (left) : (right))
#define LMIN(left, right) ((left) < (right) ? (left) : (right))

typedef struct {
    struct { float fix[4]; bool has_fixed; } pdl;
    struct { float scale; } page;
} App;

typedef struct { bool active; int left, top, right, bottom; } Bounds;

static Bounds actual(App *app, int pwp, int ph, int blit) {
#include "scroll_fixed_bounds_function.h"
    return (Bounds){fixed_area, fx0, fy0, fx1, fy1};
}

static int clipped(double value, int limit) {
    if (value <= 0) return 0;
    if (value >= limit) return limit;
    return (int)value;
}

static void check(App *app, int shift) {
    Bounds result = actual(app, 100, 100, shift);
    if (!app->pdl.has_fixed) { assert(!result.active); return; }
    int left = clipped(floor((double)app->pdl.fix[0] * app->page.scale), 100);
    int top = clipped(floor((double)app->pdl.fix[1] * app->page.scale) - LMAX(shift, 0), 100);
    int right = clipped(ceil((double)app->pdl.fix[2] * app->page.scale), 100);
    int bottom = clipped(ceil((double)app->pdl.fix[3] * app->page.scale) - LMIN(shift, 0), 100);
    assert(result.active == (right > left && bottom > top));
    if (result.active) {
        assert(result.left == left && result.top == top);
        assert(result.right == right && result.bottom == bottom);
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
    App app = {.pdl = {{10.25f, 20.25f, 70.25f, 80.25f}, true}, .page = {1}};
    int shift = 0;
    if (!strcmp(scenario, "matrix")) {
        for (int percent = 100; percent <= 300; percent += 50)
            for (int offset = -150; offset <= 150; offset += 3)
                for (int delta = -30; delta <= 30; delta += 3) {
                    app.page.scale = percent / 100.f;
                    app.pdl.fix[0] = app.pdl.fix[1] = offset + 0.25f;
                    app.pdl.fix[2] = app.pdl.fix[3] = offset + 50.75f;
                    check(&app, delta);
                }
    } else {
        if (!strcmp(scenario, "empty-250") || !strcmp(scenario, "empty-300")) {
            app.page.scale = !strcmp(scenario, "empty-250") ? 2.5f : 3;
            app.pdl.has_fixed = false;
            app.pdl.fix[0] = app.pdl.fix[1] = 1e9f;
            app.pdl.fix[2] = app.pdl.fix[3] = -1e9f;
        } else if (!strcmp(scenario, "positive") || !strcmp(scenario, "negative")) {
            app.page.scale = 3;
            float value = !strcmp(scenario, "positive") ? FLT_MAX : -FLT_MAX;
            for (int index = 0; index < 4; index++) app.pdl.fix[index] = value;
        } else if (!strcmp(scenario, "spanning")) {
            app.page.scale = 3;
            app.pdl.fix[0] = app.pdl.fix[1] = -FLT_MAX;
            app.pdl.fix[2] = app.pdl.fix[3] = FLT_MAX;
        } else if (!strcmp(scenario, "up")) shift = 7;
        else if (!strcmp(scenario, "down")) shift = -7;
        else if (!strcmp(scenario, "below")) {
            app.pdl.fix[1] = 103; app.pdl.fix[3] = 120; shift = 7;
        } else if (!strcmp(scenario, "above")) {
            app.pdl.fix[1] = -12; app.pdl.fix[3] = -3; shift = -7;
        } else if (!strcmp(scenario, "empty-box")) app.pdl.fix[0] = app.pdl.fix[2] = 20;
        else abort();
        check(&app, shift);
    }
    puts("PASS actual fixed scroll damage bounds");
    return 0;
}
