#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ANDROID_BACK_DISMISS_POPUP,
    ANDROID_BACK_CANCEL_ADDRESS,
    ANDROID_BACK_CLEAR_FOCUS,
    ANDROID_BACK_HISTORY,
    ANDROID_BACK_BACKGROUND
} AndroidBackAction;

typedef struct {
    uintptr_t focus;
    uint64_t generation;
} AndroidTextInputState;

AndroidBackAction android_back_action(bool popup, bool editing, bool focused, bool history);
bool android_text_input_start(AndroidTextInputState *state, uintptr_t focus, uint64_t generation, bool editing);
bool android_back_submit(void);
bool android_back_take(void);
void android_back_set_wakeup(void (*callback)(void));
