#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "paint_profile.h"

#ifdef NDEBUG
#define TEST_RELEASE 1
#undef NDEBUG
#endif
#include <assert.h>
#ifdef TEST_RELEASE
#define NDEBUG 1
#endif

static PaintProfile g_paint_profile;
static char g_pref[16] = "./";
static int clock_reads, log_count;
static double clock_ms;
static char last_log[1024], last_wall_log[1024];

static double now_ms(void) { clock_reads++; return clock_ms; }

#if defined(__ANDROID__) && !defined(NDEBUG)
static void pref_path(char *path, size_t capacity, const char *name) {
    snprintf(path, capacity, "%s%s", g_pref, name);
}
#endif

#ifdef __ANDROID__
static void SDL_Log(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(last_log, sizeof last_log, format, arguments);
    va_end(arguments);
    if (strstr(last_log, "lumen: paint wall")) snprintf(last_wall_log, sizeof last_wall_log, "%s", last_log);
    log_count++;
}
#endif

#include "profile_functions.h"

static void marker(const char *value) {
    FILE *file = fopen("paint-profile.enabled", "w");
    assert(file);
    assert(fputs(value, file) >= 0);
    assert(fclose(file) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(unsetenv("LUMEN_DEBUG_PAINT") == 0);
    if (!strcmp(argv[1], "configure")) {
        paint_profile_configure();
        assert(!g_paint_profile.enabled);
        const char *invalid[] = {"", "0", "true", "1\n", "11"};
        for (size_t index = 0; index < sizeof invalid / sizeof *invalid; index++) {
            marker(invalid[index]);
            paint_profile_configure();
            assert(!g_paint_profile.enabled);
        }
        marker("1");
        paint_profile_configure();
#if defined(__ANDROID__) && !defined(NDEBUG)
        assert(g_paint_profile.enabled);
#else
        assert(!g_paint_profile.enabled);
#endif
        assert(remove("paint-profile.enabled") == 0);
        for (size_t index = 0; index < sizeof invalid / sizeof *invalid; index++) {
            assert(setenv("LUMEN_DEBUG_PAINT", invalid[index], 1) == 0);
            paint_profile_configure();
            assert(!g_paint_profile.enabled);
        }
        assert(setenv("LUMEN_DEBUG_PAINT", "1", 1) == 0);
        paint_profile_configure();
        assert(g_paint_profile.enabled);
        assert(unsetenv("LUMEN_DEBUG_PAINT") == 0);
        g_pref[0] = 0;
        marker("1");
        paint_profile_configure();
        assert(!g_paint_profile.enabled);
    } else if (!strcmp(argv[1], "stage")) {
        PaintFrame frame = { .last_ms = 50 };
        clock_ms = 100;
        paint_profile_stage(&frame, PAINT_RASTER);
        assert(clock_reads == 0 && frame.last_ms == 50 && frame.phase_ms[PAINT_RASTER] == 0);
        g_paint_profile.enabled = true;
        paint_profile_stage(&frame, PAINT_RASTER);
        assert(clock_reads == 1 && frame.last_ms == 100 && frame.phase_ms[PAINT_RASTER] == 50);
        clock_ms = 105;
        paint_profile_stage(&frame, PAINT_COMPOSE);
        assert(clock_reads == 2 && frame.last_ms == 105 && frame.phase_ms[PAINT_COMPOSE] == 5);
    } else if (!strcmp(argv[1], "report")) {
        PaintFrame frame = { .last_ms = 100, .blit = true };
        paint_profile_report(&frame, 99, true);
        assert(!g_paint_profile.frames && log_count == 0);
        g_paint_profile.enabled = true;
        paint_profile_report(&frame, 99, true);
        assert(g_paint_profile.frames == 1 && log_count == 0);
        frame.last_ms = 1099;
        frame.phase_ms[PAINT_PRESENT] = 9;
        paint_profile_report(&frame, 1090, false);
        assert(log_count == 2 && strstr(last_wall_log, "frames=2 partial=1 full=1 blits=2"));
        assert(strstr(last_wall_log, "present_ms=9.000"));
        assert(strstr(last_log, "paint thread_cpu") && strstr(last_log, "measured_frames=0 unavailable_frames=2"));
        assert(g_paint_profile.frames == 0 && g_paint_profile.start_ms == 1099);
        assert(clock_reads == 0);
    } else {
        return 2;
    }
    return 0;
}
