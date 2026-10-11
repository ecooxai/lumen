#ifndef LUMEN_DOWNLOAD_H
#define LUMEN_DOWNLOAD_H
#include <stdbool.h>
#include <stddef.h>

/* Directory downloads are written to ($LUMEN_DOWNLOAD_DIR, ~/Downloads, or the app's external Download dir on Android). */
const char *download_dir(void);
/* True when a top-level response should be saved instead of rendered. */
bool download_wanted(const char *ctype, const char *disposition);
/* Suggested file name from Content-Disposition, an explicit hint, or the URL; sanitized, never empty. */
void download_filename(const char *url, const char *disposition, const char *hint, char *out, size_t n);
/* Saves data under a unique name in download_dir(); writes the final path to out. */
bool download_save(const char *url, const char *disposition, const char *ctype, const char *hint, const void *data, size_t len, char *out, size_t n);
/* Optional: called after each successful save (any thread). */
extern void (*download_notify)(const char *path);
#endif
