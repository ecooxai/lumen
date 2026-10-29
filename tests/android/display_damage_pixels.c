#include "../../src/paint/paint.h"
#include <assert.h>
#include <math.h>

#include "display_signatures.h"

typedef struct { float sy; bool loading; } Tab;
typedef struct {
    Canvas page;
    DisplayList cdl, hdl, pdl;
    uint64_t chash;
    bool osig_ok, gvid_ok;
    float last_sy;
    Tab *t;
    struct DSig *osig, *nsig;
    int nosig;
} App;

static double g_tx;
static void build_chrome(App *app) { (void)app; }
static void chrome_tip(App *app) { (void)app; }

const Glyph *font_glyph(Font *font, uint32_t glyph, float scale) {
    (void)font; (void)glyph; (void)scale;
    abort();
}

void image_yuv_materialize(Image *image) { (void)image; abort(); }

static DItem rectangle(float left, float top, float width, float height, Color color) {
    return (DItem){.op = DO_RECT, .x = left, .y = top, .w = width, .h = height,
                   .color = color};
}

static void transition(DisplayList previous, DisplayList current, float scale, int expected) {
    Tab tab = {0};
    App state = {.osig_ok = true, .t = &tab, .pdl = current};
    App *a = &state;
    int old_capacity = 0, new_capacity = 0;
    state.nosig = dl_sigs(&previous, &state.osig, &old_capacity);
    int nn = dl_sigs(&current, &state.nsig, &new_capacity);
    int why = 1, nv = 0, pwp = (int)(100 * scale), ph = pwp;
    int rx0 = pwp, ry0 = ph, rx1 = 0, ry1 = 0;
    bool part = false, gskip = false, gv = false;
    struct { void *img; } video = {0}, *vit = &video;
    canvas_init(&state.page, pwp, ph, scale);
    raster(&state.page, &previous, RGBA(255, 255, 255, 255));

#include "display_damage.h"

    Canvas reference;
    canvas_init(&reference, pwp, ph, scale);
    raster(&reference, &current, RGBA(255, 255, 255, 255));
    for (int row = 0; row < ph; row++) {
        for (int column = 0; column < pwp; column++) {
            size_t offset = (size_t)row * (size_t)pwp + (size_t)column;
            if (state.page.px[offset] != reference.px[offset]) {
                fprintf(stderr, "pixel mismatch scale=%g coordinate=%d,%d\n", scale, column, row);
                abort();
            }
        }
    }
    if (expected == 1) {
        assert(part && !gskip);
        assert(rx0 < rx1 && ry0 < ry1);
        assert((rx1 - rx0) * (ry1 - ry0) < pwp * ph / 2);
    }
    if (expected == 2) assert(part && gskip);
    canvas_free(&state.page);
    canvas_free(&reference);
    free(state.osig);
    free(state.nsig);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    bool hidden = !strcmp(argv[1], "hidden");
    bool clip = !strcmp(argv[1], "clip");
    bool layer = !strcmp(argv[1], "layer");
    assert(hidden || clip || layer || !strcmp(argv[1], "visible"));
    float scales[] = {0.5f, 1, 1.5f, 2, 3};
    int transitions = 0;
    for (size_t scale_index = 0; scale_index < sizeof scales / sizeof *scales; scale_index++) {
        for (int variant = 0; variant < 4; variant++) {
            DItem background = rectangle(0, 0, 100, 100, RGBA(210, 230, 255, 255));
            DItem content = rectangle(10.25f, 20.5f, 50, 45, RGBA(160, 80, 30, 190));
            DItem caret = rectangle(30.25f, hidden ? 120 : 25.5f, 1.5f, 20, RGBA(10, 20, 30, 255));
            DItem header = rectangle(0, 0, 100, 10, RGBA(20, 80, 130, 255));
            DItem footer = rectangle(0, 90, 100, 10, RGBA(30, 90, 120, 255));
            DItem previous_items[8] = {background, content, header, footer};
            DItem current_items[8] = {background, content, caret, header, footer};
            int previous_count = 4, current_count = 5;
            if (clip || layer) {
                DItem push = {.op = clip ? DO_PUSH_CLIP : DO_PUSH_LAYER,
                              .x = 20, .y = 30, .w = 30, .h = 25, .alpha = 0.4f};
                DItem pop = {.op = clip ? DO_POP_CLIP : DO_POP_LAYER};
                previous_items[1] = push;
                previous_items[2] = content;
                previous_items[3] = pop;
                previous_items[4] = header;
                previous_items[5] = footer;
                previous_count = 6;
                current_items[1] = push;
                current_items[2] = content;
                current_items[3] = caret;
                current_items[4] = pop;
                current_items[5] = header;
                current_items[6] = footer;
                current_count = 7;
                if (variant == 1) {
                    current_items[1].x = 35;
                    current_items[1].alpha = 0.8f;
                }
                if (variant == 2) {
                    current_items[1] = content;
                    current_items[2] = push;
                }
                if (variant == 3) {
                    current_items[3] = pop;
                    current_items[4] = caret;
                }
            } else {
                current_items[2].x += variant * 0.25f;
            }
            DisplayList previous = {.items = {.v = previous_items, .n = previous_count}};
            DisplayList current = {.items = {.v = current_items, .n = current_count}};
            int expected = clip || layer ? 0 : hidden ? 2 : 1;
            transition(previous, current, scales[scale_index], expected);
            transition(current, previous, scales[scale_index], expected);
            transitions += 2;
        }
    }
    printf("PASS %d actual-raster transitions with exact full-frame pixel comparison\n", transitions);
    return 0;
}
