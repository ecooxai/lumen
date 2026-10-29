#include "media_icon.h"
#include <stddef.h>

bool media_icon_rgba(bool paused, int size, uint32_t rgb, uint32_t *pixels) {
    if (!pixels || size < 8 || size > 512) return false;
    for (int row = 0; row < size; row++) for (int column = 0; column < size; column++) {
        unsigned covered = 0;
        for (int sample_y = 0; sample_y < 4; sample_y++) for (int sample_x = 0; sample_x < 4; sample_x++) {
            float horizontal = (column + (sample_x + .5f) / 4) / size;
            float vertical = (row + (sample_y + .5f) / 4) / size;
            if (paused) {
                float distance = vertical > .5f ? vertical - .5f : .5f - vertical;
                covered += horizontal >= .18f && horizontal <= .82f && distance <= (.82f - horizontal) * .35f / .64f;
            } else {
                covered += vertical >= .15f && vertical <= .85f &&
                    ((horizontal >= .20f && horizontal <= .40f) || (horizontal >= .60f && horizontal <= .80f));
            }
        }
        uint32_t alpha = (covered * 255 + 8) / 16;
        pixels[(size_t)row * size + column] = alpha << 24 |
            (((rgb >> 16) & 255) * alpha / 255) << 16 |
            (((rgb >> 8) & 255) * alpha / 255) << 8 |
            (rgb & 255) * alpha / 255;
    }
    return true;
}
