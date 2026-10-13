#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if __has_include("app/android_fling.h")
#include "app/android_fling.h"
#else
typedef struct { bool active; } AndroidFling;
static void android_fling_reset(AndroidFling *fling) { fling->active = false; }
static void android_fling_begin(AndroidFling *fling, double timestamp) { (void)timestamp; android_fling_reset(fling); }
static void android_fling_sample(AndroidFling *fling, float dx, float dy, double timestamp) { (void)fling; (void)dx; (void)dy; (void)timestamp; }
static void android_fling_release(AndroidFling *fling, double timestamp, double now) { (void)fling; (void)timestamp; (void)now; }
static bool android_fling_step(AndroidFling *fling, double now, float scale, float *dx, float *dy) { (void)fling; (void)now; (void)scale; *dx = *dy = 0; return false; }
#endif

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
static void flick(AndroidFling *fling, float delta) {
    android_fling_begin(fling, 1000);
    android_fling_sample(fling, 0, delta, 1016);
    android_fling_sample(fling, 0, delta, 1032);
    android_fling_release(fling, 1033, 5000);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    AndroidFling fling = {0};
    float dx, dy;
    if (!strcmp(argv[1], "release")) {
        flick(&fling, 24);
        CHECK(fling.active);
        CHECK(android_fling_step(&fling, 5016, 2, &dx, &dy));
        CHECK(dx == 0 && dy > 10 && dy < 30);
    } else if (!strcmp(argv[1], "slow")) {
        flick(&fling, 0.25f);
        CHECK(!fling.active);
    } else if (!strcmp(argv[1], "hold")) {
        android_fling_begin(&fling, 1000);
        android_fling_sample(&fling, 0, 24, 1016);
        android_fling_release(&fling, 1200, 5000);
        CHECK(!fling.active);
    } else if (!strcmp(argv[1], "cancel")) {
        flick(&fling, 24); CHECK(fling.active);
        android_fling_reset(&fling);
        CHECK(!fling.active && !android_fling_step(&fling, 5016, 2, &dx, &dy));
    } else if (!strcmp(argv[1], "invalid")) {
        const double bad[] = {NAN, INFINITY, -1, 900};
        for (int index = 0; index < 4; index++) {
            android_fling_begin(&fling, 1000);
            android_fling_sample(&fling, 0, 24, bad[index]);
            android_fling_release(&fling, 1033, 5000);
            CHECK(!fling.active);
        }
        flick(&fling, NAN); CHECK(!fling.active);
        flick(&fling, 24); CHECK(fling.active);
        CHECK(!android_fling_step(&fling, 5016, 0, &dx, &dy) && !fling.active);
    } else if (!strcmp(argv[1], "stall")) {
        flick(&fling, 24); CHECK(fling.active);
        CHECK(!android_fling_step(&fling, 6000, 2, &dx, &dy) && !fling.active);
    } else if (!strcmp(argv[1], "pixels")) {
        const float scales[] = {1, 1.5f, 2, 2.5f, 3};
        for (int index = 0; index < 5; index++) {
            flick(&fling, 24); CHECK(fling.active);
            for (int frame = 1; frame < 20; frame++) {
                android_fling_step(&fling, 5000 + frame * 16, scales[index], &dx, &dy);
                CHECK(fabsf(dy * scales[index] - roundf(dy * scales[index])) < 0.001f);
            }
        }
    } else if (!strcmp(argv[1], "cadence")) {
        float totals[2] = {0};
        for (int cadence = 0; cadence < 2; cadence++) {
            flick(&fling, 24); CHECK(fling.active);
            int interval = cadence ? 8 : 16;
            for (int elapsed = interval; elapsed <= 800; elapsed += interval) {
                android_fling_step(&fling, 5000 + elapsed, 2, &dx, &dy); totals[cadence] += dy;
            }
        }
        CHECK(fabsf(totals[0] - totals[1]) <= 0.5f);
    } else if (!strcmp(argv[1], "stop")) {
        flick(&fling, 24); CHECK(fling.active);
        for (int elapsed = 16; elapsed < 2000; elapsed += 16) android_fling_step(&fling, 5000 + elapsed, 2, &dx, &dy);
        CHECK(!fling.active);
        CHECK(!android_fling_step(&fling, 8000, 2, &dx, &dy) && dx == 0 && dy == 0);
    } else if (!strcmp(argv[1], "reverse")) {
        android_fling_begin(&fling, 1000);
        android_fling_sample(&fling, 0, 24, 1016);
        android_fling_sample(&fling, 0, -24, 1032);
        android_fling_release(&fling, 1033, 5000);
        CHECK(fling.active && android_fling_step(&fling, 5016, 2, &dx, &dy) && dy < 0);
    } else if (!strcmp(argv[1], "bounded")) {
        flick(&fling, 1e20f); CHECK(fling.active);
        CHECK(android_fling_step(&fling, 5016, 2, &dx, &dy));
        CHECK(isfinite(dy) && dy > 0 && dy <= 100);
    } else if (!strcmp(argv[1], "independent")) {
        AndroidFling other = {0};
        flick(&fling, 24); flick(&other, -24);
        CHECK(fling.active && other.active);
        android_fling_reset(&fling);
        CHECK(other.active && android_fling_step(&other, 5016, 2, &dx, &dy) && dy < 0);
    } else CHECK(false);
    return 0;
}
