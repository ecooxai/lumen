#ifndef LUMEN_XML_STYLESHEET_H
#define LUMEN_XML_STYLESHEET_H
#include "../dom/dom.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline bool node_is_stylesheet_pi(const Node *n) {
    return n && n->type == NODE_PI && n->tag && !strcmp(n->tag, "xml-stylesheet");
}
/* PI-only sparse state is branded by its ext_free callback. Clones begin fresh. */
void xml_stylesheet_touch(Node *node);
uint64_t xml_stylesheet_version(const Node *node);
/* Output begins empty; successful results are freed with hm_free(attributes, free). */
bool xml_pseudo_attributes(const char *text, size_t length, HMap *attributes);
bool xml_stylesheet_info(const Node *node, HMap *attributes);
#ifdef __cplusplus
}
#endif
#endif
