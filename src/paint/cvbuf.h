#ifndef LUMEN_CVBUF_H
#define LUMEN_CVBUF_H
#include "paint.h"
/* Backing store of a <canvas> element (Node.ext). Drawing goes to `px`; painting shows img[cur].
   Each commit copies into the other Image so the display-list hash changes and the rect is repainted. */
typedef struct CanvasBuf { Image img[2]; int cur, w, h; uint32_t *px; bool dirty; } CanvasBuf;
CanvasBuf *cvbuf_for(Node *n, bool create);
void cvbuf_resize(CanvasBuf *c, int w, int h);
void cvbuf_touch(CanvasBuf *c);
Image *cvbuf_image(Node *n);   /* commits pending drawing; NULL if the canvas was never drawn */
extern volatile int cvbuf_changed;
#endif
