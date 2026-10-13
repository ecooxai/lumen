#pragma once

struct JsViewportState {
    enum { RESIZE = 1, MEDIA = 2 };
    unsigned update(float width, float height, float dpr) {
        unsigned changes = 0;
        if (initialized) {
            if (width != last_width || height != last_height) changes |= RESIZE;
            if (dpr != last_dpr) changes |= MEDIA;
        }
        last_width = width;
        last_height = height;
        last_dpr = dpr;
        initialized = true;
        return changes;
    }

private:
    float last_width = 0, last_height = 0, last_dpr = 1;
    bool initialized = false;
};
