#ifndef LUMEN_MEDIA_ICON_H
#define LUMEN_MEDIA_ICON_H
#include <stdbool.h>
#include <stdint.h>

bool media_icon_rgba(bool paused, int size, uint32_t rgb, uint32_t *pixels);

#endif
