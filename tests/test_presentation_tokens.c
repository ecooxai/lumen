#include "../src/paint/presentation.h"
#include <stdio.h>
static int checks, failures;
#define CHECK(name, condition) do { int ok = (condition); checks++; printf("%s %s\n", ok ? "PASS" : "FAIL", name); failures += !ok; } while (0)
int main(void) {
    PaintFrames child = {0}, parent = {0};
    paint_frames_add(&child, (PaintFrame){0, 1});
    paint_frames_add(&child, (PaintFrame){1, 0});
    CHECK("non-media identities do not allocate tokens", !child.n && !child.v);
    paint_frames_add(&child, (PaintFrame){1, 1});
    paint_frames_add(&child, (PaintFrame){1, 1});
    CHECK("repeated draws retain one token per owner", child.n == 1);
    for (uint64_t frame = 2; frame <= 50000; frame++) paint_frames_add(&child, (PaintFrame){1, frame});
    CHECK("fifty thousand frames use one retained slot", child.n == 1 && child.cap == 4 && child.v[0].frame == 50000);
    paint_frames_add(&child, (PaintFrame){1, 3});
    CHECK("an older cache cannot replace a newer frame token", child.v[0].frame == 50000);
    paint_frames_add(&child, (PaintFrame){2, 1});
    paint_frames_merge(&parent, &child); paint_frames_merge(&parent, &child);
    CHECK("nested surface merges deduplicate media owners", parent.n == 2);
    paint_frames_merge(&parent, &parent);
    CHECK("self-merge does not grow or corrupt the set", parent.n == 2);
    parent.n = 0;
    paint_frames_add(&parent, (PaintFrame){3, 1});
    CHECK("full surface replacement reuses bounded storage", parent.n == 1 && parent.cap == 4 && parent.v[0].owner == 3);
    paint_frames_free(&parent); paint_frames_free(&child);
    CHECK("surface lifetime releases every token allocation", !child.v && !parent.v && !child.n && !parent.cap);
    printf("RESULTS presentation-tokens %d/%d (%d failures)\n", checks - failures, checks, failures);
    return failures ? 1 : 0;
}
