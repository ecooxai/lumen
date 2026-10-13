#include "android_touch.h"
#include <limits.h>
#include <math.h>
#include <string.h>

void android_touch_reset(AndroidTouch *touch) {
    memset(touch, 0, sizeof *touch);
}

AndroidTouchResult android_touch_update(AndroidTouch *touch, AndroidTouchPhase phase,
        uint64_t device, uint64_t finger, float pointer_x, float pointer_y, bool can_scroll) {
    AndroidTouchResult result = {0};
    if (phase == ANDROID_TOUCH_CANCEL || !isfinite(pointer_x) || !isfinite(pointer_y)) {
        android_touch_reset(touch);
        return result;
    }
    if (!device || !finger) return result;
    unsigned slot = ANDROID_TOUCH_CONTACTS, free_slot = ANDROID_TOUCH_CONTACTS;
    for (unsigned index = 0; index < ANDROID_TOUCH_CONTACTS; index++) {
        if (touch->contacts[index].device == device && touch->contacts[index].finger == finger) slot = index;
        if (!touch->contacts[index].device && free_slot == ANDROID_TOUCH_CONTACTS) free_slot = index;
    }
    if (phase == ANDROID_TOUCH_DOWN) {
        if (slot != ANDROID_TOUCH_CONTACTS) return result;
        if (free_slot == ANDROID_TOUCH_CONTACTS) {
            touch->blocked = true;
            if (touch->overflow < UINT_MAX) touch->overflow++;
            return result;
        }
        if (!touch->count && !touch->overflow) {
            touch->origin_x = touch->last_x = pointer_x;
            touch->origin_y = touch->last_y = pointer_y;
            touch->can_scroll = can_scroll;
        } else touch->blocked = true;
        touch->contacts[free_slot].device = device;
        touch->contacts[free_slot].finger = finger;
        touch->count++;
        return result;
    }
    if (slot == ANDROID_TOUCH_CONTACTS) {
        if (phase == ANDROID_TOUCH_UP && touch->overflow) {
            touch->overflow--;
            if (!touch->count && !touch->overflow) android_touch_reset(touch);
        }
        return result;
    }
    if (!touch->blocked && (phase == ANDROID_TOUCH_MOVE || phase == ANDROID_TOUCH_UP)) {
        float distance_x = pointer_x - touch->origin_x, distance_y = pointer_y - touch->origin_y;
        float delta_x = touch->last_x - pointer_x, delta_y = touch->last_y - pointer_y;
        if (!isfinite(distance_x) || !isfinite(distance_y) || !isfinite(delta_x) || !isfinite(delta_y)) {
            android_touch_reset(touch);
            return result;
        }
        if (distance_x * distance_x + distance_y * distance_y >= 64) touch->dragged = true;
        result.anchor_x = touch->origin_x; result.anchor_y = touch->origin_y;
        if (touch->dragged) {
            if (touch->can_scroll && (delta_x || delta_y)) {
                result.action = ANDROID_TOUCH_SCROLL;
                result.dx = delta_x; result.dy = delta_y;
            }
            touch->last_x = pointer_x; touch->last_y = pointer_y;
        } else if (phase == ANDROID_TOUCH_UP) result.action = ANDROID_TOUCH_TAP;
    }
    if (phase == ANDROID_TOUCH_UP) {
        touch->contacts[slot].device = touch->contacts[slot].finger = 0;
        touch->count--;
        if (!touch->count && !touch->overflow) android_touch_reset(touch);
    }
    return result;
}
