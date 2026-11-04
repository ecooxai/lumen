/* CSSOM selector serialization shared by the C parser and C++ bindings. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct CssNamespace { const char *prefix, *uri; } CssNamespace;
char *css_selector_text_ns(const char *src, const CssNamespace *namespaces, int count);

#ifdef __cplusplus
}
#endif
