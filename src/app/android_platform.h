#ifndef LUMEN_ANDROID_PLATFORM_H
#define LUMEN_ANDROID_PLATFORM_H
#ifdef __ANDROID__
#include <stddef.h>
void android_platform_init(void);
/* the system share sheet for UTF-8 text */
void android_share_text(const char *text);
/* the primary clip's image (malloc'd bytes, *mime malloc'd), else NULL */
unsigned char *android_clip_image(char **mime, size_t *len);
#endif
#endif
