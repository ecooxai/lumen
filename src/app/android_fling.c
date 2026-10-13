#include "android_fling.h"
#include <math.h>
#include <string.h>

void android_fling_reset(AndroidFling *fling) {
    memset(fling, 0, sizeof *fling);
}

void android_fling_begin(AndroidFling *fling, double timestamp) {
    android_fling_reset(fling);
    if (!isfinite(timestamp) || timestamp < 0) return;
    fling->tracking = true;
    fling->sample_ms = timestamp;
}

static float filtered_velocity(float previous, double current, bool sampled) {
    float bounded = (float)fmax(-4500, fmin(4500, current));
    if (!sampled || previous * bounded < 0) return bounded;
    return previous * 0.65f + bounded * 0.35f;
}

void android_fling_sample(AndroidFling *fling, float dx, float dy, double timestamp) {
    if (!fling->tracking) return;
    double elapsed = timestamp - fling->sample_ms;
    if (!isfinite(timestamp) || !isfinite(dx) || !isfinite(dy) || elapsed <= 0) {
        android_fling_reset(fling);
        return;
    }
    fling->sample_ms = timestamp;
    if (elapsed > 100) {
        fling->velocity_x = fling->velocity_y = 0;
        fling->sampled = false;
        return;
    }
    fling->velocity_x = filtered_velocity(fling->velocity_x, (double)dx * 1000 / elapsed, fling->sampled);
    fling->velocity_y = filtered_velocity(fling->velocity_y, (double)dy * 1000 / elapsed, fling->sampled);
    float speed = hypotf(fling->velocity_x, fling->velocity_y);
    if (speed > 4500) {
        fling->velocity_x *= 4500 / speed;
        fling->velocity_y *= 4500 / speed;
    }
    fling->sampled = true;
}

void android_fling_release(AndroidFling *fling, double timestamp, double now) {
    double elapsed = timestamp - fling->sample_ms;
    if (!fling->tracking || !fling->sampled || !isfinite(timestamp) || !isfinite(now) || now < 0 || elapsed < 0 || elapsed > 80 || hypotf(fling->velocity_x, fling->velocity_y) < 80) {
        android_fling_reset(fling);
        return;
    }
    fling->tracking = false;
    fling->active = true;
    fling->release_ms = fling->frame_ms = now;
}

int android_fling_wait_ms(const AndroidFling *fling, double now, int timeout) {
    if (!fling->active) return timeout;
    double elapsed = now - fling->frame_ms;
    if (!isfinite(elapsed) || elapsed < 0 || elapsed >= 16) return 0;
    int remaining = (int)ceil(16 - elapsed);
    return timeout < 0 || remaining < timeout ? remaining : timeout;
}

bool android_fling_step(AndroidFling *fling, double now, float scale, float *dx, float *dy) {
    *dx = *dy = 0;
    if (!fling->active) return false;
    double elapsed = now - fling->frame_ms;
    if (!isfinite(now) || !isfinite(scale) || scale < 1 || scale > 3 || elapsed < 0 || elapsed > 250 || now - fling->release_ms > 1500) {
        android_fling_reset(fling);
        return false;
    }
    if (elapsed == 0) return false;
    double decay = exp(-6 * elapsed / 1000);
    float movement_x = (float)(fling->velocity_x * (1 - decay) / 6) + fling->remainder_x;
    float movement_y = (float)(fling->velocity_y * (1 - decay) / 6) + fling->remainder_y;
    *dx = truncf(movement_x * scale) / scale;
    *dy = truncf(movement_y * scale) / scale;
    fling->remainder_x = movement_x - *dx;
    fling->remainder_y = movement_y - *dy;
    fling->velocity_x *= (float)decay;
    fling->velocity_y *= (float)decay;
    fling->frame_ms = now;
    if (hypotf(fling->velocity_x, fling->velocity_y) < 12) android_fling_reset(fling);
    return *dx != 0 || *dy != 0;
}
