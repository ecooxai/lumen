#include "../src/text/font.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(void) {
    font_init();
    Font *font = font_get("Arial, sans-serif", 400, false, 16);
    CHECK(font && font->face);
    const char *text = "hi 你好 😀 ▸ ok";
    for (int pass = 0; pass < 2; pass++) {
        ShapedRun run;
        text_shape(font, text, strlen(text), 0, &run);
        CHECK(run.n > 0 && run.width > 0);
        for (int index = 0; index < run.n; index++) CHECK(run.g[index].gid != 0);
        shaped_free(&run);
    }
    puts("font fallback tests ok");
    return 0;
}
