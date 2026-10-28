/* Native keepalive quota and context-independent Beacon delivery. */
#ifndef LUMEN_BEACON_H
#define LUMEN_BEACON_H
#include "net.h"

typedef struct NetKeepaliveGroup NetKeepaliveGroup;
NetKeepaliveGroup *net_keepalive_group_new(void);
void net_keepalive_group_release(NetKeepaliveGroup *group);
/* A successful reservation retains the group until its matching release.
   All keepalive requests in a document must share this 64 KiB body quota. */
bool net_keepalive_reserve(NetKeepaliveGroup *group, size_t body_bytes);
void net_keepalive_unreserve(NetKeepaliveGroup *group, size_t body_bytes);
/* Schemeful HTTP(S) site comparison. Invalid/opaque origins are never equal;
   registrable-domain matching uses the existing cookie jar's suffix table. */
bool net_beacon_same_site(const char *first, const char *second);

typedef struct {
    const char *origin;           /* immutable serialized document principal */
    const char *document_url;
    const char *top_origin;       /* top document's cookie origin, including inherited origins */
    const char *referrer_policy;
    const char *csp;
    bool coep_require_corp;
    bool force_cross_site;       /* an inclusive ancestor differs from the top cookie site */
} NetBeaconOptions;

/* Copies all input. True means queued, not delivered. No V8 or document
   pointer is retained. Completion and quota release run through net_poll. */
bool net_beacon_queue(NetKeepaliveGroup *group, const NetBeaconOptions *options,
                      const char *url, const char *content_type,
                      const void *body, size_t body_bytes);
#endif
