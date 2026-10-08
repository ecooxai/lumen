#ifndef LUMEN_JSGLUE_H
#define LUMEN_JSGLUE_H
#include "../dom/dom.h"
#ifdef __cplusplus
extern "C" {
#endif
int jsg_query(Node *root, const char *sel, bool all, Node ***out); /* *out malloc'd */
bool jsg_matches(Node *el, const char *sel, bool *ok);
char *jsg_computed(Node *el, const char *prop);
bool jsg_rect(Node *n, float r[4]);  /* border box, document coordinates */
bool jsg_media(void *media, const char *q);
bool jsg_valid_selector(const char *sel);
bool jsg_supports(const char *cond);
bool jsg_img_size(Node *n, float *w, float *h);
bool jsg_classic_script(Node *s);
#ifdef __cplusplus
}
#endif
#endif
