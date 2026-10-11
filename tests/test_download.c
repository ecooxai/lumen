#include "../src/net/download.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define EQ(a, b) do { if (strcmp((a), (b))) { fprintf(stderr, "FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__, (a), (b)); fails++; } } while (0)
#define OK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    char n[256];
    download_filename("https://x.com/a/b/report%20v2.pdf?x=1#f", NULL, NULL, n, sizeof n); EQ(n, "report v2.pdf");
    download_filename("https://x.com/dl", "attachment; filename=\"q\\\"1.zip\"", NULL, n, sizeof n); EQ(n, "q_1.zip");
    download_filename("https://x.com/dl", "attachment; filename=a.txt; filename*=UTF-8''%E2%82%AC%20rates.csv", NULL, n, sizeof n); EQ(n, "\xe2\x82\xac rates.csv");
    download_filename("https://x.com/", NULL, NULL, n, sizeof n); EQ(n, "download");
    download_filename("blob:https://x.com/1", NULL, "../../etc/passwd", n, sizeof n); EQ(n, "_.._etc_passwd");
    download_filename("data:text/plain,hi", NULL, "", n, sizeof n); EQ(n, "download");
    OK(download_wanted("application/octet-stream", NULL));
    OK(download_wanted("application/zip", NULL));
    OK(download_wanted("application/pdf", NULL));
    OK(download_wanted("text/html", "attachment"));
    OK(!download_wanted("text/html; charset=utf-8", NULL));
    OK(!download_wanted("image/png", "inline"));
    OK(!download_wanted("application/json", NULL));
    OK(!download_wanted(NULL, NULL));
    char dir[] = "/tmp/lumen-dl-XXXXXX"; OK(mkdtemp(dir) != NULL);
    setenv("LUMEN_DOWNLOAD_DIR", dir, 1);
    char p1[1024], p2[1024], want[1100];
    OK(download_save("https://e.com/f.txt", NULL, "text/plain", NULL, "hello", 5, p1, sizeof p1));
    OK(download_save("https://e.com/f.txt", NULL, "text/plain", NULL, "again", 5, p2, sizeof p2));
    snprintf(want, sizeof want, "%s/f.txt", dir); EQ(p1, want);
    snprintf(want, sizeof want, "%s/f (1).txt", dir); EQ(p2, want);
    FILE *f = fopen(p2, "rb"); char b[8] = {0}; OK(f && fread(b, 1, 8, f) == 5); if (f) fclose(f); EQ(b, "again");
    unlink(p1); unlink(p2); rmdir(dir);
    printf(fails ? "test_download: %d failures\n" : "test_download: all passed\n", fails);
    return fails != 0;
}
