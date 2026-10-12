#ifndef LUMEN_PLATFORM_H
#define LUMEN_PLATFORM_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t cpu_us, rss_bytes, host_busy_ticks, host_total_ticks, host_used_bytes;
} PlatformStats;

bool platform_stats(PlatformStats *stats);
uint64_t platform_heap_bytes(void);
#endif
