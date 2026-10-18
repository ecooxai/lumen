#ifndef LUMEN_LOCAL_VIEWER_H
#define LUMEN_LOCAL_VIEWER_H

#include <stdbool.h>
/* A viewer page for a media URL (file:, or http(s) when remote) with an image/audio/video type, else NULL. */
char *local_viewer_html(const char *url, const char *type, bool remote);

#endif
