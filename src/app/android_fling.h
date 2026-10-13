#pragma once
#include <stdbool.h>

typedef struct {
    float velocity_x, velocity_y, remainder_x, remainder_y;
    double sample_ms, frame_ms, release_ms;
    bool tracking, sampled, active;
} AndroidFling;

void android_fling_reset(AndroidFling *fling);
void android_fling_begin(AndroidFling *fling, double timestamp);
void android_fling_sample(AndroidFling *fling, float dx, float dy, double timestamp);
void android_fling_release(AndroidFling *fling, double timestamp, double now);
bool android_fling_step(AndroidFling *fling, double now, float scale, float *dx, float *dy);
