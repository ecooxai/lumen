#ifndef LUMEN_URL_H
#define LUMEN_URL_H
#include "util.h"
typedef struct {
    char *scheme;   /* lower-case, no ':' */
    char *host;     /* lower-case */
    int port;       /* explicit or default; 0 if none */
    char *path;     /* starts with '/' for hierarchical urls */
    char *query;    /* without '?', NULL if none */
    char *fragment; /* without '#', NULL if none */
    char *userinfo;
    bool opaque;    /* data:, about:, javascript:, blob: ... */
} URL;
bool url_parse(const char *s, URL *u);
bool url_resolve(const URL *base, const char *rel, URL *out);
char *url_to_string(const URL *u);           /* full href */
char *url_origin(const URL *u);              /* scheme://host[:port] */
char *url_path_query(const URL *u);          /* path?query for request line */
char *url_join(const char *base, const char *rel); /* convenience, returns href or NULL */
void url_free(URL *u);
int url_default_port(const char *scheme);
char *url_decode(const char *s, size_t n);
char *url_encode_component(const char *s);
#endif
