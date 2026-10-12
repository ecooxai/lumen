#include "../src/app/media_icon.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(void) {
    uint32_t pixels[32 * 32];
    CHECK(!media_icon_rgba(true, 0, 0xffffff, pixels));
    CHECK(!media_icon_rgba(true, 16, 0xffffff, NULL));
    for (int size = 16; size <= 32; size += 16) {
        for (int paused = 0; paused <= 1; paused++) {
            CHECK(media_icon_rgba(paused, size, 0xffffff, pixels));
            int filled = 0;
            for (int offset = 0; offset < size * size; offset++) {
                uint32_t pixel = pixels[offset], alpha = pixel >> 24;
                CHECK((pixel & 255) == alpha && ((pixel >> 8) & 255) == alpha && ((pixel >> 16) & 255) == alpha);
                filled += alpha > 0;
            }
            CHECK(filled > size * size / 8 && filled < size * size / 2);
            CHECK(pixels[0] == 0 && pixels[size * size - 1] == 0);
            CHECK((pixels[size * (size / 2) + size / 2] != 0) == (paused != 0));
        }
    }
    puts("media icon tests ok");
    return 0;
}
