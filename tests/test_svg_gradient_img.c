/* An SVG <img> resolves url(#id) paint servers inside its own detached document */
#include "../src/paint/paint.h"
#include "../src/css/css.h"
#include "../src/dom/dom.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char ring[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"110\" height=\"110\" viewBox=\"0 0 110 110\" fill=\"none\">"
    "<path d=\"M55.691 1.504C84.92 1.874 108.5 25.684 108.5 55c0 29.547-23.953 53.5-53.5 53.5S1.5 84.547 1.5 55l.004-.691C1.874 25.08 25.684 1.5 55 1.5l.691.004z\" "
    "stroke=\"url(#a)\" stroke-width=\"3\" vector-effect=\"non-scaling-stroke\"/>"
    "<defs><linearGradient id=\"a\" x1=\"25.533\" y1=\"97.756\" x2=\"86.778\" y2=\"12.822\" gradientUnits=\"userSpaceOnUse\">"
    "<stop offset=\".85\" stop-color=\"#3186FF\"/><stop offset=\".996\" stop-color=\"#A9A8FF\"/></linearGradient></defs></svg>";

static uint32_t at(const Image *im, float cx, float cy) {
    float k = (float)im->w / 110;
    return im->px[(int)(cy * k) * im->w + (int)(cx * k)];
}

int main(void) {
    dom_init();
    Image *im = image_decode((const uint8_t *)ring, sizeof ring - 1);
    assert(im && im->w >= 110);
    uint32_t left = at(im, 1.6f, 55), mid = at(im, 55, 55);
    printf("ring left %08x centre %08x\n", left, mid);
    assert(COLOR_A(left) > 128);                         /* the stroke is painted */
    assert(COLOR_B(left) > 0xc0 && COLOR_R(left) < 0x60);   /* in the first stop's blue, not black */
    assert(COLOR_A(mid) == 0);                           /* fill="none" */
    puts("SVG image gradients passed");
}
