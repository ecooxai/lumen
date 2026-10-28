#ifndef LUMEN_URL_H
#define LUMEN_URL_H
#include "util.h"
#include "encoding.h"
typedef struct {
    char *scheme;   /* lower-case, no ':' */
    char *host;     /* lower-case */
    int port;       /* explicit or default; 0 if none */
    char *path;     /* starts with '/' for hierarchical urls */
    char *query;    /* without '?', NULL if none */
    char *fragment; /* without '#', NULL if none */
    char *userinfo;
    bool opaque;    /* opaque path: data:, about:, javascript:, blob: ... */
    bool has_port;  /* explicit non-default port */
} URL;
/* Values match the protocol-through-hash fields shared by JS URL bindings. */
typedef enum {
    URL_SET_PROTOCOL = 1, URL_SET_USERNAME = 2, URL_SET_PASSWORD = 3,
    URL_SET_HOST = 4, URL_SET_HOSTNAME = 5, URL_SET_PORT = 6,
    URL_SET_PATHNAME = 7, URL_SET_SEARCH = 8, URL_SET_HASH = 9
} URLComponent;
/* Apply a basic URL parser state override to a valid, owned URL record.
 * Input is byte-counted and may alias a record field. A false return does not
 * imply an unchanged record: a valid host can commit before its port fails.
 * Web API callers suppress parser failures and serialize the resulting record. */
bool url_set_component_n(URL *u, URLComponent component, const char *input, size_t length);

/* Byte-counted entry points preserve embedded U+0000 in WebIDL strings. */
bool url_parse_n(const char *s, size_t length, URL *u);
bool url_resolve_n(const URL *base, const char *rel, size_t length, URL *out);
bool url_parse(const char *s, URL *u);
bool url_resolve(const URL *base, const char *rel, URL *out);
char *url_to_string(const URL *u);           /* full href */
char *url_origin(const URL *u);              /* serialized origin; "null" if opaque */
char *url_path_query(const URL *u);          /* path?query for request line */
char *url_join_n(const char *base, const char *rel, size_t length);
char *url_join(const char *base, const char *rel); /* convenience, returns href or NULL */
void url_free(URL *u);
int url_default_port(const char *scheme);
char *url_decode(const char *s, size_t n);
char *url_encode_component(const char *s);
/* HTML URL parsing alone supplies a document encoding. Only the query uses
 * that encoding; path, fragment, credentials and modern URL APIs remain UTF-8.
 * Input for a legacy query must be scalar-value UTF-8. Invalid encoding/input
 * contracts fail without publishing a partial parse. Hyperlink component
 * setters also retain their existing UTF-8 parser default.
 */
bool url_parse_encoding_n(const char *s, size_t length, WebEncoding encoding, URL *u);
bool url_resolve_encoding_n(const URL *base, const char *rel, size_t length,
                           WebEncoding encoding, URL *out);
char *url_join_encoding_n(const char *base, const char *rel, size_t length,
                          WebEncoding encoding);
#endif
