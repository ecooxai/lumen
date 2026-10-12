#include "../src/app/workspace.h"
#include "../src/app/menu.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(void) {
    CHECK(WORKSPACE_DEFAULT_ICON >= 0 && WORKSPACE_DEFAULT_ICON < WORKSPACE_ICON_COUNT);
    uint32_t pixels[36 * 36];
    CHECK(!workspace_icon_rgba(0, 0x141414, pixels));
    CHECK(!workspace_icon_rgba(18, 0x141414, NULL));
    for (int size = 18; size <= 36; size += 18) {
        CHECK(workspace_icon_rgba(size, 0x141414, pixels));
        int filled = 0;
        for (int offset = 0; offset < size * size; offset++) {
            CHECK(pixels[offset] == 0 || pixels[offset] == 0xff141414);
            filled += pixels[offset] != 0;
        }
        CHECK(filled > size * size / 3 && filled < size * size * 3 / 4);
        CHECK(pixels[0] == 0);
        CHECK(pixels[size * (size / 2) + size / 2] == 0);
    }
    puts("workspace icon tests ok");
    return 0;
}
