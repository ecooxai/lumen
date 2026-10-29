#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "paint_profile.h"

static PaintProfile g_paint_profile;
static int cpu_reads, wall_reads, log_count, cpu_status;
static time_t cpu_seconds;
static long cpu_nanoseconds = 100000000;
static double wall_ms;
static char logs[2][1024];

static double now_ms(void) { wall_reads++; return wall_ms; }

#ifdef __ANDROID__
static int model_clock_gettime(clockid_t clock, struct timespec *value) {
    assert(clock == CLOCK_THREAD_CPUTIME_ID);
    cpu_reads++;
    if (cpu_status) return -1;
    value->tv_sec = cpu_seconds;
    value->tv_nsec = cpu_nanoseconds;
    return 0;
}
#define clock_gettime model_clock_gettime

static void SDL_Log(const char *format, ...) {
    assert(log_count < 2);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(logs[log_count++], sizeof logs[0], format, arguments);
    va_end(arguments);
}
#endif

#include "paint_cpu_functions.h"

static void finish(PaintFrame *frame, double wall, long cpu) {
    wall_ms = wall;
    cpu_nanoseconds = cpu;
    paint_profile_stage(frame, PAINT_PRESENT);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
    if (!strcmp(scenario, "disabled")) {
        PaintFrame frame = paint_profile_begin(10);
        finish(&frame, 1010, 110000000);
        paint_profile_report(&frame, 10, true);
        assert(!cpu_reads && !wall_reads && !log_count && !g_paint_profile.frames);
        assert(frame.last_ms == 10 && !frame.cpu_valid);
        return 0;
    }
    g_paint_profile.enabled = true;
    if (!strcmp(scenario, "initial-error")) cpu_status = 1;
    PaintFrame frame = paint_profile_begin(10);
    if (!strcmp(scenario, "unavailable")) {
        finish(&frame, 20, 103000000);
        assert(!frame.cpu_valid && cpu_reads == 0 && wall_reads == 1);
        assert(frame.phase_ms[PAINT_PRESENT] == 10);
    } else if (!strcmp(scenario, "initial-error")) {
        finish(&frame, 20, 103000000);
        assert(!frame.cpu_valid && cpu_reads == 1);
    } else if (!strcmp(scenario, "invalid")) {
        cpu_seconds = 3; cpu_nanoseconds = 250000000;
        assert(paint_profile_cpu_ms() == 3250);
        cpu_seconds = -1;
        assert(paint_profile_cpu_ms() < 0);
        cpu_seconds = 0; cpu_nanoseconds = -1;
        assert(paint_profile_cpu_ms() < 0);
        cpu_nanoseconds = 1000000000;
        assert(paint_profile_cpu_ms() < 0);
        return 0;
    } else {
        assert(frame.cpu_valid && frame.cpu_last_ms == 100 && cpu_reads == 1);
        wall_ms = 110; cpu_nanoseconds = 103000000;
        paint_profile_stage(&frame, PAINT_PREPARE);
        assert(frame.phase_ms[PAINT_PREPARE] == 100 && frame.cpu_phase_ms[PAINT_PREPARE] == 3);
        if (!strcmp(scenario, "mid-error")) cpu_status = 1;
        finish(&frame, 1010, !strcmp(scenario, "backward") ? 99000000 : 107000000);
        if (!strcmp(scenario, "mid-error") || !strcmp(scenario, "backward")) {
            assert(!frame.cpu_valid && cpu_reads == 3);
            finish(&frame, 1020, 110000000);
            assert(cpu_reads == 3);
        } else {
            assert(frame.cpu_valid && frame.cpu_phase_ms[PAINT_PRESENT] == 4);
            assert(frame.phase_ms[PAINT_PRESENT] == 900);
        }
    }
    if (!strcmp(scenario, "report")) {
        paint_profile_report(&frame, 10, true);
        assert(log_count == 2);
        assert(strstr(logs[0], "lumen: paint wall interval_ms=1000.0 frames=1"));
        assert(strstr(logs[0], "present_ms=900.000"));
        assert(strstr(logs[1], "lumen: paint thread_cpu interval_ms=1000.0 frames=1 measured_frames=1 unavailable_frames=0"));
        assert(strstr(logs[1], "prepare_ms=3.000") && strstr(logs[1], "present_ms=4.000"));
        assert(!g_paint_profile.frames && !g_paint_profile.cpu_frames);
        return 0;
    }
    paint_profile_record(&g_paint_profile, &frame, 10, true);
    assert(g_paint_profile.frames == 1 && g_paint_profile.cpu_frames == (unsigned)frame.cpu_valid);
    if (!frame.cpu_valid) for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++)
        assert(g_paint_profile.cpu_phase_ms[phase] == 0);
    if (!strcmp(scenario, "counts")) {
        frame.cpu_valid = false;
        paint_profile_record(&g_paint_profile, &frame, 10, false);
        assert(g_paint_profile.frames == 2 && g_paint_profile.cpu_frames == 1);
        assert(g_paint_profile.cpu_phase_ms[PAINT_PRESENT] == 4);
    }
    char text[1024];
    paint_profile_cpu_format(&g_paint_profile, frame.last_ms, text, sizeof text);
    assert(strstr(text, "lumen: paint thread_cpu"));
    assert(!strstr(text, "fps") && !strstr(text, "gpu"));
    if (!frame.cpu_valid) assert(strstr(text, "unavailable_frames=1"));
    if (!strcmp(scenario, "reset")) {
        paint_profile_reset(&g_paint_profile, frame.last_ms);
        assert(g_paint_profile.enabled && !g_paint_profile.cpu_frames && !g_paint_profile.frames);
        for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++) assert(!g_paint_profile.cpu_phase_ms[phase]);
    }
    puts("PASS actual render-thread CPU profiling");
    return 0;
}
