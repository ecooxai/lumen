#include "app/android_fling.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double clock_ms;
static double now_ms(void) { return clock_ms; }

static int wait_timeout(AndroidFling fling, double timestamp, int timeout) {
    struct { AndroidFling fling; } state = { .fling = fling };
    int to = timeout;
    clock_ms = timestamp;
    (void)now_ms;
#include "fling_wait_block.h"
    return to;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    AndroidFling fling = { .active = true, .frame_ms = 100, .release_ms = 100,
                           .velocity_y = 1000 };
    if (!strcmp(argv[1], "inactive")) {
        fling.active = false;
        assert(wait_timeout(fling, 110, 1000) == 1000);
        assert(wait_timeout(fling, 110, 3) == 3);
        assert(wait_timeout(fling, NAN, 0) == 0);
    } else if (!strcmp(argv[1], "remaining")) {
        assert(wait_timeout(fling, 100, 1000) == 16);
        assert(wait_timeout(fling, 108, 1000) == 8);
        assert(wait_timeout(fling, 115, 1000) == 1);
    } else if (!strcmp(argv[1], "late")) {
        assert(wait_timeout(fling, 116, 1000) == 0);
        assert(wait_timeout(fling, 130, 1000) == 0);
        assert(wait_timeout(fling, 1e100, 1000) == 0);
    } else if (!strcmp(argv[1], "earlier")) {
        assert(wait_timeout(fling, 108, 0) == 0);
        assert(wait_timeout(fling, 108, 3) == 3);
        assert(wait_timeout(fling, 108, 8) == 8);
        assert(wait_timeout(fling, 108, 12) == 8);
    } else if (!strcmp(argv[1], "fractional")) {
        assert(wait_timeout(fling, 100.25, 1000) == 16);
        assert(wait_timeout(fling, 110.25, 1000) == 6);
        assert(wait_timeout(fling, 115.999, 1000) == 1);
    } else if (!strcmp(argv[1], "invalid")) {
        assert(wait_timeout(fling, NAN, 1000) == 0);
        assert(wait_timeout(fling, INFINITY, 1000) == 0);
        assert(wait_timeout(fling, -INFINITY, 1000) == 0);
        assert(wait_timeout(fling, 99, 1000) == 0);
        fling.frame_ms = NAN;
        assert(wait_timeout(fling, 100, 1000) == 0);
    } else if (!strcmp(argv[1], "cadence") || !strcmp(argv[1], "slow")) {
        double work_ms = !strcmp(argv[1], "cadence") ? 8 : 25;
        double interval_ms = fmax(16, work_ms);
        double timestamp = 100;
        for (int frame = 0; frame < 10; frame++) {
            timestamp += work_ms;
            timestamp += wait_timeout(fling, timestamp, 1000);
            assert(fabs(timestamp - (100 + (frame + 1) * interval_ms)) < 0.001);
            float delta_x, delta_y;
            assert(android_fling_step(&fling, timestamp, 2, &delta_x, &delta_y));
            assert(fling.active && fling.frame_ms == timestamp);
        }
    } else if (!strcmp(argv[1], "cancel")) {
        android_fling_reset(&fling);
        assert(wait_timeout(fling, 110, 1000) == 1000);
    } else {
        assert(!"unknown scenario");
    }
    puts("PASS Android fling frame pacing");
    return 0;
}
