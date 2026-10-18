#ifndef LUMEN_PAINT_CONTROL_H
#define LUMEN_PAINT_CONTROL_H
#include "paint.h"
#include "../dom/control.h"

/* Coordinates are unscrolled layout coordinates. Selection indices are UTF-16;
 * shaped glyph clusters index the prepared (possibly masked) UTF-8 text. */
typedef struct { float x, y, h; } ControlCaret;
size_t control_paint_byte(const Box *control, uint32_t index);
uint32_t control_paint_index(const Box *control, size_t byte);
bool control_paint_caret(const Box *control, uint32_t index, ControlCaret *caret);
uint32_t control_paint_hit(const Box *control, float x, float y);
#endif
