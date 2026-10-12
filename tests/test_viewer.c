#include "../src/app/local_viewer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(int argc, char **argv) {
    CHECK(local_viewer_html("https://example.com/image.png", "image/png") == NULL);
    CHECK(local_viewer_html("file:///index.html", "text/html") == NULL);
    CHECK(local_viewer_html("file:///file.bin", "application/octet-stream") == NULL);
    CHECK(local_viewer_html("file:///a.png", NULL) == NULL);
    const char *types[] = {"image/png", "video/mp4", "audio/mpeg"};
    const char *elements[] = {"<img ", "<video ", "<audio "};
    for (size_t index = 0; index < 3; index++) {
        char *html = local_viewer_html("file:///media\"<&.png", types[index]);
        CHECK(html != NULL);
        CHECK(strstr(html, elements[index]));
        CHECK(strstr(html, "file:///media&quot;&lt;&amp;.png"));
        CHECK(!strstr(html, "src=\"file:///media\"<"));
        CHECK(!strstr(html, "setInterval"));
        if (index != 2) {
            CHECK(strstr(html, "[10,25,50,75,100,150,200,300]"));
            CHECK(strstr(html, "devicePixelRatio"));
        }
        free(html);
    }
    if (argc == 2) {
        char *html = local_viewer_html("file:///test.png", "image/png");
        FILE *fixture = fopen(argv[1], "wb");
        CHECK(fixture != NULL);
        CHECK(fwrite(html, 1, strlen(html), fixture) == strlen(html));
        CHECK(fclose(fixture) == 0);
        free(html);
    }
    puts("local viewer tests ok");
    return 0;
}
