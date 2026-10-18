#ifndef LUMEN_PRESENT_RETRY_H
#define LUMEN_PRESENT_RETRY_H
#include <stdbool.h>
#include <math.h>

/* A missing native drawable gets a prompt retry, then bounded backoff. No
   allocation, extra timer thread, or permanent high-frequency wakeup. */
typedef struct { double at; int delay_ms; } PresentRetry;

static inline void present_retry_clear(PresentRetry *r) { *r = (PresentRetry){0}; }
static inline void present_retry_fail(PresentRetry *r, double now) {
    r->delay_ms = r->delay_ms <= 0 ? 16 : r->delay_ms >= 500 ? 1000 : r->delay_ms * 2;
    r->at = now + r->delay_ms;
}
static inline bool present_retry_due(const PresentRetry *r, double now) { return r->delay_ms > 0 && now >= r->at; }
static inline int present_retry_wait(const PresentRetry *r, double now, int timeout) {
    if (!r->delay_ms) return timeout;
    double left = r->at - now;
    if (left <= 0) return 0;
    return left >= timeout ? timeout : (int)ceil(left);
}
#endif
