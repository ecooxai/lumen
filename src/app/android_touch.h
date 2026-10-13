#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ANDROID_TOUCH_CONTACTS 16

typedef enum {
    ANDROID_TOUCH_DOWN, ANDROID_TOUCH_MOVE, ANDROID_TOUCH_UP, ANDROID_TOUCH_CANCEL
} AndroidTouchPhase;

typedef enum {
    ANDROID_TOUCH_NONE, ANDROID_TOUCH_TAP, ANDROID_TOUCH_SCROLL
} AndroidTouchAction;

typedef struct {
    AndroidTouchAction action;
    float anchor_x, anchor_y, dx, dy;
} AndroidTouchResult;

typedef struct {
    struct { uint64_t device, finger; } contacts[ANDROID_TOUCH_CONTACTS];
    unsigned count, overflow;
    bool blocked, dragged, can_scroll;
    float origin_x, origin_y, last_x, last_y;
} AndroidTouch;

void android_touch_reset(AndroidTouch *touch);
AndroidTouchResult android_touch_update(AndroidTouch *touch, AndroidTouchPhase phase,
    uint64_t device, uint64_t finger, float pointer_x, float pointer_y, bool can_scroll);
