#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SDL_PLATFORM_ANDROID 1
#define SDL_assert assert
#define SDL_EVENT_FIRST 0
#define SDL_EVENT_LAST 65535
#define SDL_EVENT_POLL_SENTINEL 32512
#define SDL_GETEVENT 1
#define SDL_PEEKEVENT 2
typedef uint64_t Uint64;
typedef int64_t Sint64;
typedef struct { unsigned type; } SDL_Event;

static int SDL_sentinel_pending;
static unsigned queued_event, pump_count;
static bool lifecycle_wake, deliver_event, deliver_lifecycle;
static Uint64 ticks, scheduled_at;

static int SDL_GetAtomicInt(int *value) { return *value; }
static Uint64 SDL_GetTicksNS(void) { return ++ticks; }

static void Android_PumpEvents(Sint64 timeout) {
    if (lifecycle_wake) { lifecycle_wake = false; return; }
    if (!timeout) return;
    Uint64 until = timeout < 0 ? UINT64_MAX : ticks + timeout;
    if ((deliver_event || deliver_lifecycle) && scheduled_at <= until) {
        ticks = scheduled_at;
        if (deliver_event) queued_event = 42;
        deliver_event = false;
        if (deliver_lifecycle) {
            deliver_lifecycle = false;
            scheduled_at += 100;
            deliver_event = true;
        }
    } else {
        assert(timeout > 0);
        ticks = until;
    }
}

static void SDL_PumpEventsInternal(bool sentinel) {
    pump_count++;
    assert(pump_count < 2000000);
    Android_PumpEvents(0);
    if (sentinel) {
        SDL_sentinel_pending = 1;
        lifecycle_wake = true;
    }
}

static int SDL_PeepEventsInternal(SDL_Event *event, int count, int action,
    unsigned minimum, unsigned maximum, bool include_sentinel) {
    (void)count;
    if (queued_event && queued_event >= minimum && queued_event <= maximum) {
        if (event) {
            event->type = queued_event;
            if (action == SDL_GETEVENT) queued_event = 0;
        }
        return 1;
    }
    if (SDL_sentinel_pending) {
        if (event && action == SDL_GETEVENT) SDL_sentinel_pending = 0;
        if (include_sentinel) {
            if (event) event->type = SDL_EVENT_POLL_SENTINEL;
            return 1;
        }
    }
    return 0;
}

static int SDL_PeepEvents(SDL_Event *event, int count, int action,
    unsigned minimum, unsigned maximum) {
    return SDL_PeepEventsInternal(event, count, action, minimum, maximum, false);
}

#include "sdl_wait_function.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    int scenario = atoi(argv[1]);
    SDL_Event event = {0};
    bool result;
    switch (scenario) {
    case 0:
        result = SDL_WaitEventTimeoutNS(&event, 1000000);
        assert(!result && ticks >= 1000000);
        break;
    case 1:
    case 2:
        deliver_event = true;
        scheduled_at = 10000;
        result = SDL_WaitEventTimeoutNS(&event, scenario == 1 ? 1000000 : -1);
        assert(result && event.type == 42 && ticks < 11000);
        break;
    case 3:
        queued_event = 42;
        assert(SDL_WaitEventTimeoutNS(&event, 1000000) && event.type == 42);
        assert(ticks < 10);
        break;
    case 4:
        assert(!SDL_WaitEventTimeoutNS(&event, 0));
        assert(ticks == 0 && !SDL_sentinel_pending);
        break;
    case 5:
        assert(!SDL_WaitEventTimeoutNS(NULL, 0));
        assert(ticks == 0 && !SDL_sentinel_pending);
        break;
    case 6:
        deliver_lifecycle = true;
        scheduled_at = 10000;
        assert(SDL_WaitEventTimeoutNS(&event, 1000000) && event.type == 42);
        assert(ticks < 11000);
        break;
    case 7:
        queued_event = 42;
        assert(SDL_WaitEventTimeoutNS(&event, 0) && event.type == 42);
        queued_event = 43;
        assert(SDL_WaitEventTimeoutNS(&event, 0) && event.type == 43);
        assert(!SDL_WaitEventTimeoutNS(&event, 0));
        assert(ticks == 0);
        break;
    default: return 2;
    }
    printf("scenario=%d pumps=%u ticks=%llu\n", scenario, pump_count, (unsigned long long)ticks);
    if (pump_count > 8) return 1;
    return 0;
}
