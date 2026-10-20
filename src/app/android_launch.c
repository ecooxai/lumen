#include "android_launch.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static char *queue[32];
static unsigned head, count;
static void (*wakeup)(void);

static bool push(char *copy);

bool android_launch_submit(const char *url) {
    if (!url || (strncmp(url, "https://", 8) && strncmp(url, "http://", 7))) return false;
    size_t length = 0;
    while (length <= 65536 && url[length]) length++;
    if (length > 65536) return false;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)url[index];
        if (character <= 32 || character == 127) return false;
    }
    char *copy = malloc(length + 1);
    if (!copy) return false;
    memcpy(copy, url, length + 1);
    return push(copy);
}

bool android_launch_submit_file(const char *path, bool viewer) {
    if (!path || path[0] != '/' || strstr(path, "/../") || strstr(path, "/./")) return false;
    size_t length = strlen(path);
    if (length > 16384 || (length >= 3 && !strcmp(path + length - 3, "/.."))) return false;
    const char *prefix = viewer ? "lumen://newtab?s=downloads&view=" : "file://";
    size_t start = strlen(prefix);
    char *copy = malloc(start + length * 3 + 1);
    if (!copy) return false;
    memcpy(copy, prefix, start);
    char *out = copy + start;
    for (const unsigned char *cursor = (const unsigned char *)path; *cursor; cursor++) {
        unsigned char character = *cursor;
        if (character < 32 || character == 127) { free(copy); return false; }
        bool plain = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || strchr("-._~", character) || (character == '/' && !viewer);
        if (plain) *out++ = (char)character;
        else { static const char hex[] = "0123456789ABCDEF"; *out++ = '%'; *out++ = hex[character >> 4]; *out++ = hex[character & 15]; }
    }
    *out = 0;
    return push(copy);
}

static bool push(char *copy) {
    pthread_mutex_lock(&mutex);
    if (count == 32) {
        pthread_mutex_unlock(&mutex);
        free(copy);
        return false;
    }
    queue[(head + count) % 32] = copy;
    count++;
    void (*callback)(void) = wakeup;
    pthread_mutex_unlock(&mutex);
    if (callback) callback();
    return true;
}

char *android_launch_take(void) {
    pthread_mutex_lock(&mutex);
    char *url = NULL;
    if (count) {
        url = queue[head];
        queue[head] = NULL;
        head = (head + 1) % 32;
        count--;
    }
    pthread_mutex_unlock(&mutex);
    return url;
}

void android_launch_set_wakeup(void (*callback)(void)) {
    pthread_mutex_lock(&mutex);
    wakeup = callback;
    pthread_mutex_unlock(&mutex);
}
