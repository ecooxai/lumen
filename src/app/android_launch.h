#pragma once
#include <stdbool.h>

bool android_launch_submit(const char *url);
char *android_launch_take(void);
void android_launch_set_wakeup(void (*callback)(void));
