#include "../src/app/android_back.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static atomic_uint wakes;
static void wake(void) { atomic_fetch_add(&wakes, 1); }
static void reentrant_wake(void) { CHECK(android_back_take()); }
static void *producer(void *unused) {
    (void)unused;
    for (unsigned index = 0; index < 1000; index++) {
        while (!android_back_submit()) sched_yield();
    }
    return NULL;
}

int main(void) {
    AndroidTextInputState text_state = {0};
    CHECK(!android_text_input_start(&text_state, 0, 1, false));
    CHECK(android_text_input_start(&text_state, 100, 1, false));
    CHECK(!android_text_input_start(&text_state, 100, 1, false));
    CHECK(android_text_input_start(&text_state, 200, 1, false));
    CHECK(!android_text_input_start(&text_state, 200, 1, true));
    CHECK(!android_text_input_start(&text_state, 200, 1, false));
    CHECK(!android_text_input_start(&text_state, 0, 1, false));
    CHECK(android_text_input_start(&text_state, 200, 1, false));
    CHECK(android_text_input_start(&text_state, 200, 2, false));
    CHECK(!android_text_input_start(&text_state, 300, 2, true));
    CHECK(!android_text_input_start(&text_state, 300, 2, false));
    for (unsigned mask = 0; mask < 16; mask++) {
        AndroidBackAction expected = mask & 1 ? ANDROID_BACK_DISMISS_POPUP
            : mask & 2 ? ANDROID_BACK_CANCEL_ADDRESS
            : mask & 4 ? ANDROID_BACK_CLEAR_FOCUS
            : mask & 8 ? ANDROID_BACK_HISTORY : ANDROID_BACK_BACKGROUND;
        CHECK(android_back_action(mask & 1, mask & 2, mask & 4, mask & 8) == expected);
    }
    CHECK(!android_back_take());
    CHECK(!android_back_submit());
    android_back_set_wakeup(wake);
    for (unsigned index = 0; index < 32; index++) CHECK(android_back_submit());
    CHECK(!android_back_submit());
    CHECK(atomic_load(&wakes) == 32);
    for (unsigned index = 0; index < 32; index++) CHECK(android_back_take());
    CHECK(!android_back_take());
    android_back_set_wakeup(reentrant_wake);
    CHECK(android_back_submit());
    CHECK(!android_back_take());
    android_back_set_wakeup(wake);
    CHECK(android_back_submit());
    android_back_set_wakeup(NULL);
    CHECK(!android_back_take());
    CHECK(!android_back_submit());
    android_back_set_wakeup(wake);
    atomic_store(&wakes, 0);
    pthread_t producers[4];
    for (unsigned index = 0; index < 4; index++) CHECK(!pthread_create(&producers[index], NULL, producer, NULL));
    for (unsigned consumed = 0; consumed < 4000;) {
        if (android_back_take()) consumed++;
        else sched_yield();
    }
    for (unsigned index = 0; index < 4; index++) CHECK(!pthread_join(producers[index], NULL));
    CHECK(atomic_load(&wakes) == 4000);
    CHECK(!android_back_take());
    android_back_set_wakeup(NULL);
    puts("Android Back policy and bounded concurrent dispatch PASS");
    return 0;
}
