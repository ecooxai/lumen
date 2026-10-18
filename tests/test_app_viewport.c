#include <math.h>
#include <stdio.h>
#include "../src/app/viewport.h"

static int checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); failures++; } } while (0)
#define NEAR(a, b) CHECK(fabsf((a) - (b)) < 0.001f)

int main(void) {
    UiViewport v = ui_viewport(1280, 840, 2560, 1680, 0, 0, 1280, 840);
    CHECK(v.px_x == 0 && v.px_y == 0 && v.px_w == 2560 && v.px_h == 1680);
    NEAR(v.scale, 2); NEAR(v.w, 1280); NEAR(v.h, 840);

    /* iPhone portrait: controls clear the camera and bottom home indicator. */
    v = ui_viewport(393, 852, 1179, 2556, 0, 59, 393, 759);
    CHECK(v.px_x == 0 && v.px_y == 177 && v.px_w == 1179 && v.px_h == 2277);
    NEAR(v.x, 0); NEAR(v.y, 59); NEAR(v.w, 393); NEAR(v.h, 759);
    float x, y;
    CHECK(ui_viewport_point(&v, 20, 119, &x, &y)); NEAR(x, 20); NEAR(y, 60);
    CHECK(!ui_viewport_point(&v, 20, 58, &x, &y));
    CHECK(!ui_viewport_point(&v, 20, 818, &x, &y));
    CHECK(!ui_viewport_point(&v, 393, 100, &x, &y));
    CHECK(ui_viewport_point(&v, 0, 59, &x, &y)); NEAR(x, 0); NEAR(y, 0);

    /* Rotation moves the exclusion to the sides without accumulating offsets. */
    v = ui_viewport(852, 393, 2556, 1179, 59, 0, 734, 372);
    CHECK(v.px_x == 177 && v.px_y == 0 && v.px_w == 2202 && v.px_h == 1116);
    CHECK(ui_viewport_point(&v, 79, 60, &x, &y)); NEAR(x, 20); NEAR(y, 60);
    CHECK(!ui_viewport_point(&v, 58, 60, &x, &y));
    CHECK(!ui_viewport_point(&v, 793, 60, &x, &y));
    CHECK(!ui_viewport_point(&v, 80, 372, &x, &y));

    /* Fractional desktop scale: crop inward so content never crosses a safe edge. */
    v = ui_viewport(800, 600, 1000, 750, 3, 5, 792, 587);
    CHECK(v.px_x == 4 && v.px_y == 7 && v.px_w == 989 && v.px_h == 733);
    NEAR(v.x * v.scale, 4); NEAR(v.y * v.scale, 7);
    NEAR(v.w * v.scale, 989); NEAR(v.h * v.scale, 733);

    /* Stale/invalid safe rectangles during lifecycle changes remain bounded. */
    v = ui_viewport(300, 200, 600, 400, -10, 20, 500, 400);
    CHECK(v.px_x == 0 && v.px_y == 40 && v.px_w == 600 && v.px_h == 360);
    v = ui_viewport(300, 200, 600, 400, 400, 0, 10, 20);
    CHECK(v.px_x == 0 && v.px_y == 0 && v.px_w == 600 && v.px_h == 400);
    v = ui_viewport(300, 200, 600, 400, 10, 10, 0, 20);
    CHECK(v.px_x == 0 && v.px_y == 0 && v.px_w == 600 && v.px_h == 400);
    v = ui_viewport(0, 0, 0, 0, 0, 0, 0, 0);
    CHECK(v.px_w == 1 && v.px_h == 1 && isfinite(v.scale));

    printf("app viewport: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
