#include "../../src/layout/layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

#include "layout_hit_functions.h"

static void append(Box *parent, Box *child) {
    child->parent = parent;
    child->prev = parent->last;
    if (parent->last) parent->last->next = child;
    else parent->first = child;
    parent->last = child;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    Node nodes[6] = {0};
    ComputedStyle normal = {.position = P_STATIC, .visibility = VIS_VISIBLE,
        .pointer_events = true, .opacity = 1};
    ComputedStyle fixed = normal;
    fixed.position = P_FIXED;
    fixed.z_index = 5;
    ComputedStyle button_style = normal;
    Box root = {.node = &nodes[0], .st = &normal, .w = 540, .h = 5000};
    Box content = {.node = &nodes[1], .st = &normal, .w = 540, .h = 5000};
    Box header = {.node = &nodes[2], .st = &fixed, .fixed = true, .abs = true, .w = 540, .h = 100};
    Box button = {.node = &nodes[3], .st = &button_style, .x = 4, .y = 40, .w = 130, .h = 44};
    Box footer = {.node = &nodes[4], .st = &fixed, .fixed = true, .abs = true,
        .x = 400, .y = 980, .w = 140, .h = 32};
    Box scroller = {.node = &nodes[5], .st = &normal, .scroller = true,
        .x = 0, .y = 20, .w = 200, .h = 70};
    append(&root, &content);
    append(&root, &header);
    append(&root, &footer);
    const char *scenario = argv[1];
    if (!strcmp(scenario, "nested")) {
        append(&header, &scroller);
        append(&scroller, &button);
        nodes[5].scroll_y = 30;
        button.y = 60;
    } else append(&header, &button);
    Layout layout = {.root = &root};
    if (!strcmp(scenario, "top")) {
        CHECK(actual_hit(&layout, 14, 60, 0, 0) == &button);
    } else if (!strcmp(scenario, "scrolled")) {
        CHECK(actual_hit(&layout, 14, 1260, 0, 1200) == &button);
    } else if (!strcmp(scenario, "footer")) {
        CHECK(actual_hit(&layout, 420, 2190, 0, 1200) == &footer);
    } else if (!strcmp(scenario, "horizontal")) {
        CHECK(actual_hit(&layout, 214, 1260, 200, 1200) == &button);
    } else if (!strcmp(scenario, "reverse")) {
        float offsets[] = {0, 600, 1200, 800, 50, 0, 3200};
        for (unsigned index = 0; index < sizeof offsets / sizeof *offsets; index++)
            CHECK(actual_hit(&layout, 14, offsets[index] + 60, 0, offsets[index]) == &button);
    } else if (!strcmp(scenario, "content")) {
        CHECK(actual_hit(&layout, 14, 1500, 0, 1200) == &content);
    } else if (!strcmp(scenario, "nested")) {
        CHECK(actual_hit(&layout, 14, 1240, 0, 1200) == &button);
    } else if (!strcmp(scenario, "pointer-none")) {
        button_style.pointer_events = false;
        CHECK(actual_hit(&layout, 14, 1260, 0, 1200) == &header);
    } else if (!strcmp(scenario, "empty")) {
        layout.root = NULL;
        CHECK(actual_hit(&layout, 14, 1260, 0, 1200) == NULL);
    } else CHECK(false);
    return 0;
}
