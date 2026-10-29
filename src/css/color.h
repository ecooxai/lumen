/* CSS Color 4/5 values in double precision: parsing (relative colours, color-mix(), calc() in channels, none),
   conversion between colour spaces and computed-value serialization. css_parse_color (style.c) reduces these to
   the 8-bit Color used for painting. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    CSPACE_SRGB, CSPACE_SRGB_LINEAR, CSPACE_P3, CSPACE_A98, CSPACE_PROPHOTO, CSPACE_REC2020, CSPACE_XYZ50, CSPACE_XYZ65,
    CSPACE_LAB, CSPACE_LCH, CSPACE_OKLAB, CSPACE_OKLCH, CSPACE_HSL, CSPACE_HWB, CSPACE_P3_LINEAR, CSPACE_COUNT
};
/* c: the space's channels (rgb 0..1, hsl/hwb hue in degrees and percentages 0..100, lab L 0..100);
   none: bit i for a missing channel i, bit 3 for missing alpha; legacy: an absolute hex/named/rgb()/hsl()/hwb()
   colour, which serializes as rgb()/rgba() */
typedef struct { uint8_t space, none; bool legacy; double c[3], a; } CssColor;

bool css_color_parse(const char *s, CssColor *out, const CssColor *current);
void css_color_convert(const CssColor *in, int space, CssColor *out);
/* 8-bit sRGB, gamut-clipped */
uint32_t css_color_rgba8(const CssColor *c);
/* the computed-value serialization into buf (rgb()/rgba() for legacy colours) */
void css_color_serialize(const CssColor *c, char *buf, int cap);
#ifdef __cplusplus
}
#endif
