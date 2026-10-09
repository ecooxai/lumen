#ifndef LUMEN_JSGLUE_H
#define LUMEN_JSGLUE_H
#include "../dom/dom.h"
#ifdef __cplusplus
extern "C" {
#endif
int jsg_query(Node *root, const char *sel, bool all, Node ***out); /* *out malloc'd */
bool jsg_matches(Node *el, const char *sel, bool *ok);
void jsg_install_hooks(void);
void js_anim_event(Node *n, const char *type, const char *name, double delay_ms, double elapsed, bool anim);
bool js_anim_cancel(Node *n, const char *name);
char *jsg_computed(Node *el, const char *prop);
bool jsg_rect(Node *n, float r[4]);
bool jsg_vrect(Node *n, float r[4]);
bool jsg_scroll(Node *n, float r[4]);   /* scroll x, y, width, height of a scroll container */
bool jsg_set_scroll(Node *n, float x, float y);  /* NAN keeps an axis; true if it moved */  /* border box, document coordinates */
bool jsg_media(void *media, const char *q);
bool jsg_valid_selector(const char *sel);
bool jsg_supports(const char *cond);
char *css_selector_text(const char *src);
bool jsg_img_size(Node *n, float *w, float *h);
bool jsg_classic_script(Node *s);
bool jsg_module_script(Node *s);
#ifdef __cplusplus
}
#endif
#endif
