#pragma once
#include <stdbool.h>

bool android_launch_submit(const char *url);
/* a file another app opened or shared: the page itself (file: URL), or the Downloads viewer for it */
bool android_launch_submit_file(const char *path, bool viewer);
char *android_launch_take(void);
void android_launch_set_wakeup(void (*callback)(void));
