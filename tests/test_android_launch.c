#include "../src/app/android_launch.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static int wakes;
static void wake(void) { wakes++; }
static void *producer(void *unused) {
    (void)unused;
    for (int index = 0; index < 1000; index++) {
        while (!android_launch_submit("https://example.org/queued")) sched_yield();
    }
    return NULL;
}

int main(void) {
    CHECK(!android_launch_take());
    CHECK(!android_launch_submit(NULL));
    CHECK(!android_launch_submit("file:///private/data"));
    CHECK(!android_launch_submit("javascript:alert(1)"));
    CHECK(!android_launch_submit("https://example.org/\nspoof"));
    CHECK(!android_launch_submit_file("relative/page.html", false));
    CHECK(!android_launch_submit_file("/sdcard/../data/x", true));
    CHECK(!android_launch_submit_file("/sdcard/a\nb.html", false));
    char *oversized = malloc(65538);
    CHECK(oversized);
    memset(oversized, 'a', 65537);
    memcpy(oversized, "https://", 8);
    oversized[65537] = 0;
    CHECK(!android_launch_submit(oversized));
    free(oversized);
    CHECK(android_launch_submit("https://example.org/first"));
    CHECK(android_launch_submit("http://example.org/latest"));
    char *url = android_launch_take();
    CHECK(url && !strcmp(url, "https://example.org/first"));
    free(url);
    url = android_launch_take();
    CHECK(url && !strcmp(url, "http://example.org/latest"));
    free(url);
    CHECK(!android_launch_take());
    CHECK(android_launch_submit_file("/sdcard/My page #1.html", false));
    CHECK(android_launch_submit_file("/sdcard/a b&c.png", true));
    url = android_launch_take();
    CHECK(url && !strcmp(url, "file:///sdcard/My%20page%20%231.html"));
    free(url);
    url = android_launch_take();
    CHECK(url && !strcmp(url, "lumen://newtab?s=downloads&view=%2Fsdcard%2Fa%20b%26c.png"));
    free(url);
    CHECK(!android_launch_take());
    for (int index = 0; index < 32; index++) CHECK(android_launch_submit("https://example.org/bounded"));
    CHECK(!android_launch_submit("https://example.org/overflow"));
    for (int index = 0; index < 32; index++) {
        url = android_launch_take();
        CHECK(url && !strcmp(url, "https://example.org/bounded"));
        free(url);
    }
    android_launch_set_wakeup(wake);
    CHECK(android_launch_submit("https://example.org/wake"));
    CHECK(wakes == 1);
    url = android_launch_take();
    CHECK(url);
    free(url);
    android_launch_set_wakeup(NULL);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, producer, NULL) == 0);
    for (int index = 0; index < 1000;) {
        url = android_launch_take();
        if (url) {
            CHECK(!strcmp(url, "https://example.org/queued"));
            index++;
        } else sched_yield();
        free(url);
    }
    CHECK(pthread_join(thread, NULL) == 0);
    free(android_launch_take());
    CHECK(!android_launch_take());
    CHECK(wakes == 1);
    puts("Android launch queue tests ok");
    return 0;
}
