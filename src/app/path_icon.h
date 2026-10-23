#ifndef LUMEN_PATH_ICON_H
#define LUMEN_PATH_ICON_H
#include <stdbool.h>
#include <stdint.h>
/* Strokes an outline icon given as android_ui_icon's path text (a 24 unit grid: "M x y", "L x y", "Z",
   "O cx cy r", "A cx cy r start sweep") into size x size premultiplied ARGB pixels; stroke in grid units. */
bool path_icon_rgba(const char *path, int size, float stroke, uint32_t rgb, uint32_t *pixels);
#endif
