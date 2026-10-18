#ifndef LUMEN_ANDROID_PLATFORM_H
#define LUMEN_ANDROID_PLATFORM_H
#ifdef __ANDROID__
void android_platform_init(void);
/* the system share sheet for UTF-8 text */
void android_share_text(const char *text);
#endif
#endif
