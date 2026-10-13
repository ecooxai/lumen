#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LMIN(left, right) ((left) < (right) ? (left) : (right))

typedef struct { uint32_t *px; int stride, w, h; } Canvas;
typedef struct { Canvas frame, page; } App;

static size_t copy_calls, copy_bytes;

static void *counted_copy(void *destination, const void *source, size_t bytes) {
    copy_calls++;
    copy_bytes += bytes;
    return memcpy(destination, source, bytes);
}

#define memcpy counted_copy
#include "page_compose_function.h"
#undef memcpy

static void check(int width, int height, int toolbar, int sidebar, int page_padding,
                  int frame_padding, int frame_height, bool partial, int first, int last,
                  bool skip, bool check_calls) {
    int page_stride = width + page_padding;
    int frame_stride = width + sidebar + frame_padding;
    size_t page_pixels = (size_t)page_stride * (size_t)height;
    size_t frame_pixels = (size_t)frame_stride * (size_t)frame_height;
    uint32_t *page = malloc(page_pixels * sizeof(*page));
    uint32_t *frame = malloc(frame_pixels * sizeof(*frame));
    uint32_t *expected = malloc(frame_pixels * sizeof(*expected));
    assert(page && frame && expected);
    for (size_t index = 0; index < page_pixels; index++) page[index] = 0x81000000u + (uint32_t)index;
    for (size_t index = 0; index < frame_pixels; index++) frame[index] = 0x12000000u + (uint32_t)index;
    memcpy(expected, frame, frame_pixels * sizeof(*frame));
    size_t rows = 0;
    if (!skip) for (int row = partial ? first : 0; row < (partial ? last : height) && row + toolbar < frame_height; row++) {
        memcpy(expected + (size_t)(row + toolbar) * (size_t)frame_stride + sidebar,
               page + (size_t)row * (size_t)page_stride, (size_t)width * sizeof(*page));
        rows++;
    }
    App state = {.frame = {frame, frame_stride, width + sidebar, frame_height},
                 .page = {page, page_stride, width, height}};
    copy_calls = copy_bytes = 0;
    if (!skip) compose_page(&state, partial ? first : 0, partial ? last : height, toolbar, sidebar);
    assert(memcmp(frame, expected, frame_pixels * sizeof(*frame)) == 0);
    for (size_t index = 0; index < page_pixels; index++) assert(page[index] == 0x81000000u + (uint32_t)index);
    assert(copy_bytes == rows * (size_t)width * sizeof(*page));
    if (check_calls) {
        bool contiguous = !sidebar && !page_padding && !frame_padding;
        assert(copy_calls == (contiguous && rows ? 1 : rows));
    }
    free(expected);
    free(frame);
    free(page);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
    if (!strcmp(scenario, "matrix")) {
        for (int width = 1; width <= 17; width += 4)
            for (int height = 1; height <= 12; height++)
                for (int first = 0; first <= height; first++)
                    for (int last = 0; last <= height; last++)
                        for (int padding = 0; padding <= 2; padding++)
                            check(width, height, 3, padding == 2, padding == 1, padding,
                                  height + 2, true, first, last, false, false);
    } else {
        int toolbar = 3, sidebar = 0, page_padding = 0, frame_padding = 0, frame_height = 15;
        int first = 2, last = 8;
        bool partial = true, skip = false;
        if (!strcmp(scenario, "full")) partial = false;
        else if (!strcmp(scenario, "partial")) {}
        else if (!strcmp(scenario, "clipped")) frame_height = 7;
        else if (!strcmp(scenario, "empty")) last = first;
        else if (!strcmp(scenario, "reversed")) last = 1;
        else if (!strcmp(scenario, "skip")) skip = true;
        else if (!strcmp(scenario, "sidebar")) sidebar = 4;
        else if (!strcmp(scenario, "padded")) page_padding = frame_padding = 3;
        else if (!strcmp(scenario, "page-padding")) page_padding = 3;
        else if (!strcmp(scenario, "frame-padding")) frame_padding = 3;
        else if (!strcmp(scenario, "single")) last = first + 1;
        else if (!strcmp(scenario, "toolbar")) toolbar = frame_height;
        else abort();
        check(17, 12, toolbar, sidebar, page_padding, frame_padding, frame_height,
              partial, first, last, skip, true);
    }
    puts("PASS actual page composition pixels and copy policy");
    return 0;
}
