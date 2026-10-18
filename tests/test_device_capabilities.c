#include "../src/base/device.h"
#include "../src/css/css.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(name, expr) do { ++checks; if (!(expr)) { fprintf(stderr, "FAIL %s\n", name); ++failures; } } while (0)
static void query(const MediaCtx *m, const char *q, bool expected) {
    ++checks;
    if (css_media_matches(q, m) != expected) { fprintf(stderr, "FAIL %s expected %d (touch=%d fine=%d)\n", q, expected, m->touch, m->any_fine); ++failures; }
}
static void cached_queries(void) {
    MediaCtx m = {402, 734, 2, false}; m.touch = true;
    ContainerCond hover = {.query = "(any-hover: hover)"};
    ContainerCond coarse = {.query = "(pointer: coarse)"};
    ContainerCond density = {.query = "(resolution: 2dppx)"};
    CHECK("cached touch initially has no hover", !css_media_chain(&hover, &m));
    CHECK("cached touch initially has a coarse pointer", css_media_chain(&coarse, &m));
    CHECK("cached density starts at2", css_media_chain(&density, &m));
    m.any_fine = true;
    CHECK("cached hover updates when a trackpad attaches without resize", css_media_chain(&hover, &m));
    m.any_fine = false;
    CHECK("cached hover updates when a trackpad detaches without resize", !css_media_chain(&hover, &m));
    m.touch = false; m.any_fine = true;
    CHECK("same-size Mac capabilities clear cached coarse pointer", !css_media_chain(&coarse, &m));
    m.touch = true; m.any_fine = false;
    CHECK("cached coarse pointer returns for a touchscreen", css_media_chain(&coarse, &m));
    m.dpr = 3;
    CHECK("cached resolution updates when only DPR changes", !css_media_chain(&density, &m));
    m.dpr = 2;
    CHECK("cached resolution returns when DPR returns", css_media_chain(&density, &m));
    ContainerCond nested = {.query = "(min-width: 300px)", .outer = &hover};
    CHECK("nested cached query follows unavailable hover", !css_media_chain(&nested, &m));
    m.any_fine = true;
    CHECK("nested cached query follows restored hover", css_media_chain(&nested, &m));
    ContainerCond axis = {.query = "(min-width: 100vi)"};
    CHECK("logical viewport units cache horizontal root basis", css_media_chain(&axis, &m));
    m.root_vertical = true;
    CHECK("logical viewport units update their cached writing-mode basis", !css_media_chain(&axis, &m));
}
int main(void) {
    cached_queries();
    DeviceInfo d = {0};
    MediaCtx m = {402, 734, 3, false};
    query(&m, "(pointer: fine)", true); query(&m, "(hover: hover)", true);
    query(&m, "(any-pointer: fine)", true); query(&m, "(any-hover: hover)", true);
    query(&m, "(pointer: coarse)", false); query(&m, "(any-pointer: coarse)", false);

    device_apple_input(&d, true, false, false);
    CHECK("iPhone/iPad has five supported direct contacts", d.max_touch_points == 5 && !d.any_fine);
    m.touch = d.max_touch_points > 0; m.any_fine = d.any_fine;
    query(&m, "(pointer: coarse)", true); query(&m, "(pointer: fine)", false);
    query(&m, "(hover: none)", true); query(&m, "(hover: hover)", false);
    query(&m, "(hover)", false); query(&m, "(pointer)", true);
    query(&m, "(any-pointer: coarse)", true); query(&m, "(any-pointer: fine)", false);
    query(&m, "(any-hover: none)", true); query(&m, "(any-hover: hover)", false);
    query(&m, "(any-hover)", false); query(&m, "(any-pointer)", true);
    query(&m, "(pointer: none)", false); query(&m, "(any-pointer: none)", false);
    query(&m, "(orientation: portrait)", true); query(&m, "(orientation: landscape)", false);
    query(&m, "(resolution: 3dppx)", true); query(&m, "(resolution: 2dppx)", false);

    device_apple_input(&d, true, false, true); /* tablet retains touch while a trackpad is attached */
    CHECK("accessory pointer does not remove touch", d.max_touch_points == 5 && d.any_fine);
    m.any_fine = d.any_fine;
    query(&m, "(pointer: coarse)", true); query(&m, "(hover: none)", true);
    query(&m, "(any-pointer: coarse)", true); query(&m, "(any-pointer: fine)", true);
    query(&m, "(any-hover: hover)", true); query(&m, "(any-hover: none)", false);
    query(&m, "(any-hover)", true); query(&m, "(hover)", false);
    m.vw = 750; m.vh = 338;
    query(&m, "(orientation: landscape)", true); query(&m, "(orientation: portrait)", false);
    m.vw = m.vh = 600;
    query(&m, "(orientation: portrait)", true); query(&m, "(orientation: landscape)", false);

    device_apple_input(&d, true, true, false); /* same UIKit IPA, iPad idiom, actually on a Mac */
    CHECK("iPad idiom on Mac never invents touch contacts", d.max_touch_points == 0 && d.any_fine);
    m.touch = d.max_touch_points > 0; m.any_fine = d.any_fine;
    query(&m, "(pointer: fine)", true); query(&m, "(hover: hover)", true);
    query(&m, "(any-pointer: coarse)", false); query(&m, "(any-hover: hover)", true);
    device_apple_input(&d, false, true, true);
    CHECK("Mac Catalyst keeps desktop input", d.max_touch_points == 0 && d.any_fine);
    device_apple_input(&d, false, false, false);
    CHECK("unknown non-touch idiom does not acquire phone contacts", d.max_touch_points == 0);
    printf("RESULTS device-capabilities %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
