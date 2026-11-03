#ifndef LUMEN_CSS_EASING_H
#define LUMEN_CSS_EASING_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private declaration parser boundary. Successful parse results are owned
 * UTF-8 strings; callers free them. NULL denotes invalid grammar. */
char *css_easing_parse(const char *value);
bool css_easing_valid(const char *property, const char *value);

#ifdef __cplusplus
}
#endif

#endif
