#ifndef LUMEN_PAINT_PROFILE_H
#define LUMEN_PAINT_PROFILE_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    PAINT_PREPARE, PAINT_LAYOUT_DL, PAINT_SIGNATURES,
    PAINT_RASTER, PAINT_COMPOSE, PAINT_PRESENT, PAINT_PHASE_COUNT
} PaintPhase;

typedef struct {
    double phase_ms[PAINT_PHASE_COUNT], last_ms;
    double cpu_phase_ms[PAINT_PHASE_COUNT], cpu_last_ms;
    bool blit, cpu_valid;
} PaintFrame;

typedef struct {
    bool enabled, started;
    unsigned frames, partial, blits, cpu_frames;
    double start_ms, phase_ms[PAINT_PHASE_COUNT];
    double cpu_phase_ms[PAINT_PHASE_COUNT];
} PaintProfile;

static inline bool paint_profile_record(PaintProfile *profile, const PaintFrame *frame,
                                       double start_ms, bool partial) {
    if (!profile->enabled) return false;
    if (!profile->started) {
        profile->started = true;
        profile->start_ms = start_ms;
    }
    profile->frames++;
    profile->partial += partial;
    profile->blits += frame->blit;
    for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++) profile->phase_ms[phase] += frame->phase_ms[phase];
    if (frame->cpu_valid) {
        profile->cpu_frames++;
        for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++) profile->cpu_phase_ms[phase] += frame->cpu_phase_ms[phase];
    }
    return frame->last_ms - profile->start_ms >= 1000;
}

static inline int paint_profile_format(const PaintProfile *profile, double end_ms,
                                      char *text, size_t capacity) {
    return snprintf(text, capacity,
        "lumen: paint wall interval_ms=%.1f frames=%u partial=%u full=%u blits=%u "
        "prepare_ms=%.3f layout_dl_ms=%.3f signatures_ms=%.3f raster_ms=%.3f compose_ms=%.3f present_ms=%.3f",
        end_ms - profile->start_ms, profile->frames, profile->partial,
        profile->frames - profile->partial, profile->blits,
        profile->phase_ms[PAINT_PREPARE], profile->phase_ms[PAINT_LAYOUT_DL],
        profile->phase_ms[PAINT_SIGNATURES], profile->phase_ms[PAINT_RASTER],
        profile->phase_ms[PAINT_COMPOSE], profile->phase_ms[PAINT_PRESENT]);
}

static inline int paint_profile_cpu_format(const PaintProfile *profile, double end_ms,
                                          char *text, size_t capacity) {
    return snprintf(text, capacity,
        "lumen: paint thread_cpu interval_ms=%.1f frames=%u measured_frames=%u unavailable_frames=%u "
        "prepare_ms=%.3f layout_dl_ms=%.3f signatures_ms=%.3f raster_ms=%.3f compose_ms=%.3f present_ms=%.3f",
        end_ms - profile->start_ms, profile->frames, profile->cpu_frames,
        profile->frames - profile->cpu_frames,
        profile->cpu_phase_ms[PAINT_PREPARE], profile->cpu_phase_ms[PAINT_LAYOUT_DL],
        profile->cpu_phase_ms[PAINT_SIGNATURES], profile->cpu_phase_ms[PAINT_RASTER],
        profile->cpu_phase_ms[PAINT_COMPOSE], profile->cpu_phase_ms[PAINT_PRESENT]);
}

static inline void paint_profile_reset(PaintProfile *profile, double end_ms) {
    bool enabled = profile->enabled;
    memset(profile, 0, sizeof *profile);
    profile->enabled = enabled;
    profile->started = true;
    profile->start_ms = end_ms;
}

#endif
