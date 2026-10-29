#include "../src/base/platform.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(void) {
    PlatformStats before = {0}, after = {0};
    CHECK(platform_stats(&before));
    CHECK(before.rss_bytes > 0);
    CHECK(before.cpu_us > 0);
    volatile unsigned char *memory = malloc(8 * 1024 * 1024);
    CHECK(memory != NULL);
    for (size_t offset = 0; offset < 8 * 1024 * 1024; offset += 4096) memory[offset] = 1;
    CHECK(platform_stats(&after));
    CHECK(after.cpu_us >= before.cpu_us);
    CHECK(after.rss_bytes >= before.rss_bytes);
    CHECK(!platform_stats(NULL));
    free((void *)memory);
    printf("platform tests ok: RSS=%llu bytes, CPU=%llu us\n", (unsigned long long)after.rss_bytes, (unsigned long long)after.cpu_us);
    return 0;
}
