#ifndef LUMEN_TEST_NATIVE_WINDOW_H
#define LUMEN_TEST_NATIVE_WINDOW_H
typedef struct ANativeWindow { int references; } ANativeWindow;
void ANativeWindow_acquire(ANativeWindow *window);
void ANativeWindow_release(ANativeWindow *window);
#endif
