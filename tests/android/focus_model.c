#include "../../src/layout/layout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#define NEAR(actual, expected) (fabsf((actual) - (expected)) < 0.001f)

#include "focus_functions.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    Node nodes[4] = {0};
    ComputedStyle normal = {.visibility = VIS_VISIBLE, .opacity = 1};
    ComputedStyle scrolling = normal;
    scrolling.overflow_x = scrolling.overflow_y = OV_AUTO;
    Box root = {.node = &nodes[0], .st = &normal, .w = 540, .h = 5000};
    Box outer = {.node = &nodes[1], .st = &normal, .parent = &root, .w = 540, .h = 5000};
    Box scroller = {.node = &nodes[2], .st = &scrolling, .parent = &outer,
        .scroller = true, .x = 20, .y = 100, .w = 400, .h = 200, .scroll_w = 400, .scroll_h = 800};
    Box input = {.node = &nodes[3], .st = &normal, .parent = &outer,
        .x = 30, .y = 900, .w = 400, .h = 40};
    Layout layout = {.root = &root, .vw = 540, .vh = 586.5f, .doc_h = 5000};
    float scroll = 0;
    const char *scenario = argv[1];
    if (!strcmp(scenario, "keyboard")) {
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(scroll, 361.5f));
    } else if (!strcmp(scenario, "visible")) {
        input.y = 200;
        CHECK(!android_focus_scroll(&layout, &input, &scroll));
        CHECK(scroll == 0);
    } else if (!strcmp(scenario, "above")) {
        scroll = 1200;
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(scroll, 892));
    } else if (!strcmp(scenario, "end")) {
        input.y = 4960;
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(scroll, 4413.5f));
    } else if (!strcmp(scenario, "nested") || !strcmp(scenario, "nested-root") ||
               !strcmp(scenario, "fixed-nested")) {
        input.parent = &scroller; input.y = 700; input.w = 200;
        if (!strcmp(scenario, "nested-root")) {
            scroller.y = 700; input.y = 1460;
        } else if (!strcmp(scenario, "fixed-nested")) outer.fixed = true;
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(nodes[2].scroll_y, !strcmp(scenario, "nested-root") ? 600 : 448));
        CHECK(NEAR(scroll, !strcmp(scenario, "nested-root") ? 321.5f : 0));
    } else if (!strcmp(scenario, "reverse")) {
        scrolling.display = D_FLEX; scrolling.flex_direction = FD_COLUMN_REVERSE;
        input.parent = &scroller; input.y = 120; input.w = 200;
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(nodes[2].scroll_y, -588));
        CHECK(scroll == 0);
    } else if (!strcmp(scenario, "fixed")) {
        outer.fixed = true; scroll = 1200;
        CHECK(!android_focus_scroll(&layout, &input, &scroll));
        CHECK(scroll == 1200);
    } else if (!strcmp(scenario, "absolute")) {
        input.parent = &scroller; input.abs = true; input.cb = &root;
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(scroll, 361.5f));
        CHECK(nodes[2].scroll_y == 0);
    } else if (!strcmp(scenario, "horizontal")) {
        input.parent = &scroller; input.x = 600; input.y = 150; input.w = 40;
        scroller.scroll_w = 1000;
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(nodes[2].scroll_x, 228));
        CHECK(nodes[2].scroll_y == 0 && scroll == 0);
    } else if (!strcmp(scenario, "oversized")) {
        input.y = 10; input.h = 1200; scroll = 400;
        CHECK(!android_focus_scroll(&layout, &input, &scroll));
        CHECK(scroll == 400);
    } else if (!strcmp(scenario, "translation")) {
        ComputedStyle translated = normal;
        translated.has_transform = true; translated.transform[0] = translated.transform[3] = 1;
        translated.transform[5] = 30;
        input.st = &translated; input.parent = &scroller; input.y = 700; input.w = 200;
        scroller.b[2] = scroller.b[0] = 4;
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(nodes[2].scroll_y, 482));
        CHECK(scroll == 0);
    } else if (!strcmp(scenario, "invalid")) {
        CHECK(!android_focus_scroll(NULL, &input, &scroll));
        CHECK(!android_focus_scroll(&layout, NULL, &scroll));
        CHECK(!android_focus_scroll(&layout, &input, NULL));
        input.y = NAN;
        CHECK(!android_focus_scroll(&layout, &input, &scroll));
        CHECK(scroll == 0);
    } else if (!strcmp(scenario, "repeat")) {
        CHECK(android_focus_scroll(&layout, &input, &scroll));
        CHECK(!android_focus_scroll(&layout, &input, &scroll));
        layout.vh = 1012;
        CHECK(!android_focus_scroll(&layout, &input, &scroll));
        CHECK(NEAR(scroll, 361.5f));
    } else CHECK(false);
    return 0;
}
