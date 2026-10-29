/* Private HTTP cache on disk (RFC 9111 subset): complete GET responses and cacheable redirects,
 * one variant per URL, keyed by hashes (URLs are not stored), revalidated with
 * ETag / Last-Modified when stale. */
#ifndef LUMEN_NET_CACHE_H
#define LUMEN_NET_CACHE_H
#include "net.h"

bool http_cache_enabled(void);
/* Cached response for a GET, or NULL. *fresh: usable without contacting the server.
 * When stale, cond receives a copy of req plus the validators to send. */
NetResponse *http_cache_lookup(const char *url, const Headers *req, bool *fresh, Headers *cond);
/* Fetch modes can explicitly permit stale reuse or require validation. */
NetResponse *http_cache_lookup_mode(const char *url, const Headers *req, bool allow_stale,
                                  bool force_validate, bool *fresh, Headers *cond);
/* Stores a complete response if its headers allow it. */
void http_cache_store(const char *url, const Headers *req, const NetResponse *r);
/* Merges a 304 into the cached response (consumed) and returns it as the result. */
NetResponse *http_cache_revalidated(const char *url, const Headers *req, NetResponse *cached, const NetResponse *r304);
/* A successful unsafe request drops the stored response (RFC 9111 4.4). */
void http_cache_invalidate(const char *url);
#endif
