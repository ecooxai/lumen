#ifndef LUMEN_JSGLUE_H
#define LUMEN_JSGLUE_H
#include "../dom/dom.h"
#ifdef __cplusplus
extern "C" {
#endif
int jsg_query(Node *root, const char *sel, bool all, Node ***out); /* *out malloc'd; -1: invalid selector */
bool jsg_matches(Node *el, const char *sel, Node *scope, bool *ok);
void jsg_install_hooks(void);
void js_anim_event(Node *n, const char *type, const char *name, double delay_ms, double elapsed, bool anim);
bool js_anim_cancel(Node *n, const char *name);
void js_transition_start(Node *n, const char *prop, const char *from, const char *to);
void js_animation_names(Node *n, const char *names);
char *jsg_computed(Node *el, const char *prop);
void jsg_set_anim_style(Node *el, const char *css);
uint64_t jsg_layout_generation(void);
uint64_t jsg_visual_generation(void);
void jsg_visual_changed(void);
uint64_t jsg_activity_generation(void);
bool jsg_rect(Node *n, float r[4]);
bool jsg_vrect(Node *n, float r[4], float scroll_x, float scroll_y);
bool jsg_vclip(Node *n, Node *stop, float c[4], float scroll_x, float scroll_y);   /* clipping ancestors' viewport rect: x0, y0, x1, y1 */
int jsg_text_rects(Node *n, int s, int e, float *out, int max);   /* UTF-16 range of a text node: x y w h each, document coordinates */
int jsg_ctl_rects(Node *el, int s, int e, float *out, int max);   /* codepoint range of a text control's value */
bool jsg_scroll(Node *n, float r[4]);   /* scroll x, y, width, height of a scroll container */
bool jsg_set_scroll(Node *n, float x, float y);  /* NAN keeps an axis; true if it moved */  /* border box, document coordinates */
bool jsg_media(void *media, const char *q);
bool jsg_supports(const char *cond);
char *css_selector_text(const char *src);
bool jsg_img_size(Node *n, float *w, float *h);
/* PNG, JPEG, WebP or GIF (first frame) bytes to malloc'd straight-alpha RGBA rows; any thread */
uint8_t *jsg_decode_rgba(const uint8_t *d, size_t n, int *w, int *h);
bool jsg_classic_script(Node *s);
bool jsg_module_script(Node *s);
#ifdef __cplusplus
}
#endif
#endif
