#include "../src/app/android_touch.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static AndroidTouchResult send(AndroidTouch *touch, AndroidTouchPhase phase,
        uint64_t device, uint64_t finger, float pointer_x, float pointer_y, bool scroll) {
    return android_touch_update(touch, phase, device, finger, pointer_x, pointer_y, scroll);
}

static void tap(AndroidTouch *touch) {
    CHECK(send(touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
    AndroidTouchResult result = send(touch, ANDROID_TOUCH_UP, 1, 1, 202, 201, true);
    CHECK(result.action == ANDROID_TOUCH_TAP && result.anchor_x == 200 && result.anchor_y == 200);
    CHECK(send(touch, ANDROID_TOUCH_UP, 1, 1, 202, 201, true).action == ANDROID_TOUCH_NONE);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    AndroidTouch touch = {0};
    AndroidTouchResult result;
    if (!strcmp(argv[1], "tap")) {
        tap(&touch);
    } else if (!strcmp(argv[1], "drag")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        result = send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 200, 175, true);
        CHECK(result.action == ANDROID_TOUCH_SCROLL && result.dx == 0 && result.dy == 25);
        CHECK(result.anchor_x == 200 && result.anchor_y == 200);
        result = send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 180, 160, true);
        CHECK(result.action == ANDROID_TOUCH_SCROLL && result.dx == 20 && result.dy == 15);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 180, 160, true).action == ANDROID_TOUCH_NONE);
    } else if (!strcmp(argv[1], "slop")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 203, 204, true).action == ANDROID_TOUCH_NONE);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 203, 204, true).action == ANDROID_TOUCH_TAP);
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 208, 200, true).action == ANDROID_TOUCH_SCROLL);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 208, 200, true).action == ANDROID_TOUCH_NONE);
    } else if (!strcmp(argv[1], "cancel")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        CHECK(send(&touch, ANDROID_TOUCH_CANCEL, 1, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
        tap(&touch);
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        android_touch_reset(&touch);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
    } else if (!strcmp(argv[1], "chrome")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 20, 20, false);
        CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 200, 300, true).action == ANDROID_TOUCH_NONE);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 300, true).action == ANDROID_TOUCH_NONE);
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 20, 20, false);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 20, 20, false).action == ANDROID_TOUCH_TAP);
    } else if (!strcmp(argv[1], "multitouch")) {
        for (unsigned reverse = 0; reverse < 2; reverse++) {
            send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
            send(&touch, ANDROID_TOUCH_DOWN, 1, 2, 250, 200, true);
            CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 200, 100, true).action == ANDROID_TOUCH_NONE);
            CHECK(send(&touch, ANDROID_TOUCH_UP, 1, reverse ? 1 : 2, 200, 100, true).action == ANDROID_TOUCH_NONE);
            CHECK(send(&touch, ANDROID_TOUCH_UP, 1, reverse ? 2 : 1, 200, 100, true).action == ANDROID_TOUCH_NONE);
            tap(&touch);
        }
    } else if (!strcmp(argv[1], "identity")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 900, 900, true);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 2, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
        CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 2, 200, 100, true).action == ANDROID_TOUCH_NONE);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 200, true).action == ANDROID_TOUCH_TAP);
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        send(&touch, ANDROID_TOUCH_DOWN, 2, 1, 250, 200, true);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 2, 1, 250, 200, true).action == ANDROID_TOUCH_NONE);
        tap(&touch);
    } else if (!strcmp(argv[1], "invalid")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 1, NAN, 0, true).action == ANDROID_TOUCH_NONE);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
        tap(&touch);
    } else if (!strcmp(argv[1], "release_delta")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        result = send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 170, true);
        CHECK(result.action == ANDROID_TOUCH_SCROLL && result.dy == 30);
        tap(&touch);
    } else if (!strcmp(argv[1], "capacity")) {
        for (unsigned finger = 1; finger <= ANDROID_TOUCH_CONTACTS + 1; finger++)
            CHECK(send(&touch, ANDROID_TOUCH_DOWN, 1, finger, 200, 200, true).action == ANDROID_TOUCH_NONE);
        for (unsigned finger = 1; finger <= ANDROID_TOUCH_CONTACTS + 1; finger++)
            CHECK(send(&touch, ANDROID_TOUCH_UP, 1, finger, 200, 200, true).action == ANDROID_TOUCH_NONE);
        tap(&touch);
    } else if (!strcmp(argv[1], "reverse")) {
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 200, 180, true).dy == 20);
        CHECK(send(&touch, ANDROID_TOUCH_MOVE, 1, 1, 200, 200, true).dy == -20);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 200, true).action == ANDROID_TOUCH_NONE);
    } else if (!strcmp(argv[1], "independent")) {
        AndroidTouch other = {0};
        send(&touch, ANDROID_TOUCH_DOWN, 1, 1, 200, 200, true);
        tap(&other);
        CHECK(send(&touch, ANDROID_TOUCH_UP, 1, 1, 200, 200, true).action == ANDROID_TOUCH_TAP);
    } else {
        CHECK(false);
    }
    puts("Android touch scenario PASS");
    return 0;
}
