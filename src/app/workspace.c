#include "workspace.h"
#include <stddef.h>

bool workspace_icon_rgba(int size, uint32_t rgb, uint32_t *pixels) {
    if (!pixels || size < 8 || size > 512) return false;
    int edge = size / 9, gap_start = size * 4 / 9, gap_end = size * 5 / 9;
    for (int row = 0; row < size; row++) for (int column = 0; column < size; column++) {
        bool inside = column >= edge && column < size - edge && row >= edge && row < size - edge;
        bool gap = (column >= gap_start && column <= gap_end) || (row >= gap_start && row <= gap_end);
        pixels[(size_t)row * size + column] = inside && !gap ? 0xff000000u | (rgb & 0xffffffu) : 0;
    }
    return true;
}
