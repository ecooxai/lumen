#pragma once
#include <stddef.h>
#include <string.h>

static const int page_zoom_levels[] = {50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300};
static inline int page_zoom_valid(int percent) {
    for (size_t i = 0; i < sizeof page_zoom_levels / sizeof *page_zoom_levels; i++)
        if (page_zoom_levels[i] == percent) return percent;
    return 0;
}
static inline int page_zoom_parse(const char *text) {
    if (!text || !*text) return 0;
    int value = 0;
    for (size_t i = 0; text[i]; i++) {
        if (i >= 3 || text[i] < '0' || text[i] > '9') return 0;
        value = value * 10 + text[i] - '0';
    }
    return page_zoom_valid(value);
}
static inline int page_zoom_step(int percent, int direction) {
    if (direction > 0) {
        for (size_t i = 0; i < sizeof page_zoom_levels / sizeof *page_zoom_levels; i++)
            if (page_zoom_levels[i] > percent) return page_zoom_levels[i];
        return 300;
    }
    for (size_t i = sizeof page_zoom_levels / sizeof *page_zoom_levels; i > 0; i--)
        if (page_zoom_levels[i - 1] < percent) return page_zoom_levels[i - 1];
    return 50;
}
