#include "android_back.h"
#include <pthread.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned pending;
static void (*wakeup)(void);

AndroidBackAction android_back_action(bool popup, bool editing, bool focused, bool history) {
    if (popup) return ANDROID_BACK_DISMISS_POPUP;
    if (editing) return ANDROID_BACK_CANCEL_ADDRESS;
    if (focused) return ANDROID_BACK_CLEAR_FOCUS;
    return history ? ANDROID_BACK_HISTORY : ANDROID_BACK_BACKGROUND;
}

bool android_text_input_start(AndroidTextInputState *state, uintptr_t focus, uint64_t generation, bool editing) {
    bool changed = focus != state->focus || generation != state->generation;
    state->focus = focus;
    state->generation = generation;
    return focus && changed && !editing;
}

bool android_back_submit(void) {
    pthread_mutex_lock(&mutex);
    if (!wakeup || pending == 32) {
        pthread_mutex_unlock(&mutex);
        return false;
    }
    pending++;
    void (*callback)(void) = wakeup;
    pthread_mutex_unlock(&mutex);
    callback();
    return true;
}

bool android_back_take(void) {
    pthread_mutex_lock(&mutex);
    bool available = pending != 0;
    if (available) pending--;
    pthread_mutex_unlock(&mutex);
    return available;
}

void android_back_set_wakeup(void (*callback)(void)) {
    pthread_mutex_lock(&mutex);
    wakeup = callback;
    if (!callback) pending = 0;
    pthread_mutex_unlock(&mutex);
}
