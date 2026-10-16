#pragma once

struct JsViewportState {
    enum { RESIZE = 1, MEDIA = 2, SCROLL = 4 };
    unsigned changes(float width, float height, float dpr, float scroll_x = 0, float scroll_y = 0) const {
        unsigned changes = 0;
        if (initialized) {
            if (width != last_width || height != last_height) changes |= RESIZE;
            if (scroll_x != last_scroll_x || scroll_y != last_scroll_y) changes |= SCROLL;
            if (dpr != last_dpr) changes |= MEDIA;
        }
        return changes;
    }
    unsigned update(float width, float height, float dpr, float scroll_x = 0, float scroll_y = 0) {
        unsigned changed = changes(width, height, dpr, scroll_x, scroll_y);
        last_width = width;
        last_height = height;
        last_dpr = dpr;
        last_scroll_x = scroll_x; last_scroll_y = scroll_y;
        initialized = true;
        return changed;
    }

private:
    float last_width = 0, last_height = 0, last_dpr = 1;
    float last_scroll_x = 0, last_scroll_y = 0;
    bool initialized = false;
};
