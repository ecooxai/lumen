/* Cached native display/input capabilities shared by the embedder and JS. */
#ifndef LUMEN_DEVICE_H
#define LUMEN_DEVICE_H
#include <stdbool.h>

enum DeviceOrientation {
    DEVICE_PORTRAIT_PRIMARY, DEVICE_PORTRAIT_SECONDARY,
    DEVICE_LANDSCAPE_PRIMARY, DEVICE_LANDSCAPE_SECONDARY
};
typedef struct DeviceInfo {
    int screen_w, screen_h, avail_w, avail_h; /* CSS pixels, independent of page/iframe size */
    int outer_w, outer_h;
    int orientation, angle;                  /* angle clockwise from the display's natural orientation */
    int max_touch_points;
    bool any_fine;                          /* accessory pointer on a touch device */
} DeviceInfo;

static inline void device_apple_input(DeviceInfo *d, bool phone_or_pad, bool app_on_mac, bool pointer_connected) {
    /* A UIKit IPA on macOS can retain the iPad idiom. It has no direct-touch screen. */
    bool touch = phone_or_pad && !app_on_mac;
    d->max_touch_points = touch ? 5 : 0;
    d->any_fine = !touch || pointer_connected;
}
#endif
