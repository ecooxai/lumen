#ifndef LUMEN_TEST_ANDROID_LOG_H
#define LUMEN_TEST_ANDROID_LOG_H

#include <stdarg.h>

#define ANDROID_LOG_WARN 5

int __android_log_vprint(int priority, const char *tag, const char *format, va_list arguments);

#endif
