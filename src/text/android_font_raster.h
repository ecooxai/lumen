#pragma once
#include "android_fonts.h"
#include "font.h"

bool android_font_raster(const AndroidFontMatch *match, uint32_t glyph_id, float pixels, Glyph *result);
