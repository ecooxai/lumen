/* Request ownership and reusable per-document keepalive budget contracts. */
#include "../src/net/beacon.h"
#include <stdio.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(name, condition) do { ++checks; bool pass = !!(condition); \
    fprintf(pass ? stdout : stderr, "%s %s\n", pass ? "PASS" : "FAIL", name); \
    if (!pass) ++failures; } while (0)
typedef struct { int completed, released; } Counts;
static void completed(NetRequest *request, NetResponse *response, void *ud) {
    (void)request; (void)response; ((Counts *)ud)->completed++;
}
static void released(void *ud) { ((Counts *)ud)->released++; }
static bool drain(void) {
    double deadline = now_ms() + 2000;
    while (now_ms() < deadline && net_pending()) { net_poll(); usleep(1000); }
    net_poll(); return net_pending() == 0;
}
int main(void) {
    net_init(2);
    CHECK("schemeful site comparison ignores a port difference", net_beacon_same_site("http://example.test:80/a", "http://example.test:9000/b"));
    CHECK("HTTP and HTTPS on one host are different sites", !net_beacon_same_site("http://example.test", "https://example.test"));
    CHECK("same-scheme sibling subdomains share the existing registrable site", net_beacon_same_site("https://a.example.test", "https://b.example.test"));
    CHECK("different IP addresses are different sites", !net_beacon_same_site("http://127.0.0.1", "http://127.0.0.2"));
    CHECK("opaque origins cannot compare equal", !net_beacon_same_site("null", "null"));
    CHECK("missing or invalid origins cannot compare equal", !net_beacon_same_site(NULL, NULL) && !net_beacon_same_site("http://[", "http://["));
    NetKeepaliveGroup *a = net_keepalive_group_new(), *b = net_keepalive_group_new();
    CHECK("a body larger than the group quota is refused", !net_keepalive_reserve(a, 65537));
    CHECK("exactly 64 KiB can be reserved", net_keepalive_reserve(a, 65536));
    CHECK("an in-flight byte cannot exceed the group quota", !net_keepalive_reserve(a, 1));
    CHECK("different documents have independent body quotas", net_keepalive_reserve(b, 65536));
    net_keepalive_unreserve(a, 65536);
    CHECK("released body quota can be reused", net_keepalive_reserve(a, 1));
    net_keepalive_group_release(a); /* the outstanding byte owns the remaining reference */
    CHECK("a retained group remains usable after its document reference is released", net_keepalive_reserve(a, 65535));
    net_keepalive_unreserve(a, 1); net_keepalive_unreserve(a, 65535);
    net_keepalive_group_release(b); net_keepalive_unreserve(b, 65536);

    Counts direct = {0};
    NetRequest *request = net_request_new("GET", "data:text/plain,unused");
    request->ud = &direct; request->done = completed; request->release = released;
    net_request_free(request);
    CHECK("a never-queued request releases its optional owner exactly once", direct.released == 1 && direct.completed == 0);

    Counts success = {0};
    request = net_request_new("GET", "data:text/plain,complete");
    request->ud = &success; request->done = completed; request->release = released;
    net_fetch(request);
    CHECK("normal async request completes and finalizes once", drain() && success.completed == 1 && success.released == 1);

    Counts cancelled = {0};
    request = net_request_new("GET", "data:text/plain,not-loaded");
    request->ud = &cancelled; request->done = completed; request->release = released;
    net_request_cancel(request); net_fetch(request);
    CHECK("cancellation suppresses response callbacks but still finalizes ownership", drain() && cancelled.completed == 0 && cancelled.released == 1);

    Counts expired = {0};
    request = net_request_new("GET", "data:text/plain,expired");
    request->ud = &expired; request->done = completed; request->release = released;
    request->deadline_ms = now_ms() - 1;
    net_fetch(request);
    CHECK("an expired optional deadline still reclaims the queued request", drain() && expired.completed == 0 && expired.released == 1);
    net_shutdown();
    printf("net-keepalive: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
