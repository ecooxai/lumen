/* Disk cache: fresh hits skip the network, stale entries revalidate, and nothing
 * private (URLs, Set-Cookie, Vary-selected request values) is written to disk. */
#include "../src/net/net.h"
#include <dirent.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static int g_listener, g_hits;
static const char *g_response;
static char g_request[8192];

static void *serve(void *opaque) {
    (void)opaque;
    for (;;) {
        int client = accept(g_listener, NULL, NULL);
        if (client < 0) return NULL;
        size_t used = 0;
        while (used + 1 < sizeof g_request) {
            ssize_t count = recv(client, g_request + used, sizeof g_request - used - 1, 0);
            if (count <= 0) break;
            used += (size_t)count;
            g_request[used] = 0;
            if (strstr(g_request, "\r\n\r\n")) break;
        }
        __atomic_add_fetch(&g_hits, 1, __ATOMIC_SEQ_CST);
        size_t length = strlen(g_response), sent = 0;
        while (sent < length) {
            ssize_t count = send(client, g_response + sent, length - sent, 0);
            if (count <= 0) break;
            sent += (size_t)count;
        }
        close(client);
    }
}

static char g_base[64];
/* Fetches path, answering with response if the server is asked; returns the body and counts server hits. */
static char *get(const char *method, const char *path, const char *header, const char *response, int *hits) {
    char url[128]; snprintf(url, sizeof url, "%s%s", g_base, path);
    g_response = response; g_request[0] = 0;
    int before = __atomic_load_n(&g_hits, __ATOMIC_SEQ_CST);
    NetRequest *request = net_request_new(method, url);
    request->no_cookies = true;
    if (header) { char name[64]; const char *colon = strchr(header, ':'); snprintf(name, sizeof name, "%.*s", (int)(colon - header), header); headers_add(&request->headers, name, colon + 2); }
    if (strcmp(method, "GET")) { request->body = xstrdup("x"); request->body_len = 1; }
    NetResponse *r = net_fetch_sync(request);
    CHECK(r->status == 200);
    *hits = __atomic_load_n(&g_hits, __ATOMIC_SEQ_CST) - before;
    char *body = xstrdup(r->body);
    net_response_free(r);
    return body;
}
#define OK(body) "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n"
static bool has(char *body, const char *want) { bool ok = !strcmp(body, want); free(body); return ok; }

/* No file under dir contains needle. */
static bool disk_free_of(const char *dir, const char *needle) {
    DIR *d = opendir(dir); CHECK(d);
    bool clean = true;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        FILE *f = fopen(p, "rb"); CHECK(f);
        char buf[65536]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
        if (memmem(buf, n, needle, strlen(needle))) clean = false;
    }
    closedir(d);
    return clean;
}


/* Independent Fetch policy probes use the existing loopback hit counter.
 * Returning the whole response also verifies persisted redirect metadata. */
static NetResponse *policy_get(const char *path, NetCacheMode mode, const char *header,
                               const char *response, int redirects, int *hits) {
    char url[128]; snprintf(url, sizeof url, "%s%s", g_base, path);
    g_response = response; g_request[0] = 0;
    int before = __atomic_load_n(&g_hits, __ATOMIC_SEQ_CST);
    NetRequest *request = net_request_new("GET", url);
    request->no_cookies = true; request->cache_mode = mode; request->max_redirects = redirects;
    if (header) {
        const char *colon = strchr(header, ':'); char name[64]; CHECK(colon);
        snprintf(name, sizeof name, "%.*s", (int)(colon - header), header);
        headers_add(&request->headers, name, colon + 2);
    }
    NetResponse *r = net_fetch_sync(request); CHECK(r);
    *hits = __atomic_load_n(&g_hits, __ATOMIC_SEQ_CST) - before;
    return r;
}
static bool response_is(NetResponse *r, int status, const char *reason, const char *body) {
    bool ok = r && !r->error && r->status == status && r->status_text &&
              !strcmp(r->status_text, reason) && r->body && !strcmp(r->body, body);
    net_response_free(r); return ok;
}
static bool cache_miss_is(NetResponse *r) {
    bool ok = r && r->error && r->status == 0;
    net_response_free(r); return ok;
}
/* Convert one newly written200 entry to the documented legacy1 layout.
 * The URL hash and header/body data are preserved; no internal key is assumed. */
static bool make_legacy_one(const char *dir, const char *body_marker) {
    DIR *d = opendir(dir); CHECK(d); int rewritten = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char path[512]; snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb"); CHECK(f);
        char buf[65536]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
        size_t marker_len = strlen(body_marker);
        if (n < marker_len || memcmp(buf + n - marker_len, body_marker, marker_len)) continue;
        CHECK(!strncmp(buf, "LUMENCACHE 2\n", strlen("LUMENCACHE 2\n")));
        char *stored = strchr(buf, '\n') + 1;
        char *status = strchr(stored, '\n'); CHECK(status); ++status;
        char *reason = strchr(status, '\n'); CHECK(reason); ++reason;
        char *after_reason = strchr(reason, '\n'); CHECK(after_reason); ++after_reason;
        size_t removed = (size_t)(after_reason - reason);
        memmove(reason, after_reason, n - (size_t)(after_reason - buf));
        n -= removed; *(strchr(buf, ' ') + 1) = '1';
        f = fopen(path, "wb"); CHECK(f); CHECK(fwrite(buf, 1, n, f) == n); CHECK(fclose(f) == 0);
        ++rewritten;
    }
    closedir(d); return rewritten == 1;
}

int main(void) {
    alarm(20);
    char dir[] = "/tmp/lumen-http-cache-XXXXXX";
    CHECK(mkdtemp(dir));
    net_cache_init(dir, 1 << 20);
    g_listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(g_listener, (struct sockaddr *)&address, sizeof address) == 0 && listen(g_listener, 8) == 0);
    socklen_t size = sizeof address;
    CHECK(getsockname(g_listener, (struct sockaddr *)&address, &size) == 0);
    snprintf(g_base, sizeof g_base, "http://127.0.0.1:%u", ntohs(address.sin_port));
    pthread_t thread; CHECK(pthread_create(&thread, NULL, serve, NULL) == 0);
    int hits;

    /* max-age: the second load never reaches the server */
    const char *fresh = OK() "Cache-Control: public, max-age=600\r\nSet-Cookie: session=secret-cookie\r\n\r\nA1";
    CHECK(has(get("GET", "/asset.js?token=secret-query", NULL, fresh, &hits), "A1") && hits == 1);
    CHECK(has(get("GET", "/asset.js?token=secret-query", NULL, "HTTP/1.1 500 X\r\nConnection: close\r\n\r\n", &hits), "A1") && hits == 0);
    CHECK(disk_free_of(dir, "secret-query") && disk_free_of(dir, "asset.js") && disk_free_of(dir, "secret-cookie"));

    /* no-cache + ETag: revalidated with If-None-Match; a 304 granting max-age makes it fresh */
    const char *etag = OK() "Cache-Control: no-cache\r\nETag: \"v1\"\r\n\r\nB1";
    CHECK(has(get("GET", "/etag", NULL, etag, &hits), "B1") && hits == 1);
    CHECK(has(get("GET", "/etag", NULL, "HTTP/1.1 304 Not Modified\r\nCache-Control: max-age=600\r\nConnection: close\r\n\r\n", &hits), "B1") && hits == 1);
    CHECK(strstr(g_request, "If-None-Match: \"v1\""));
    CHECK(has(get("GET", "/etag", NULL, "HTTP/1.1 500 X\r\nConnection: close\r\n\r\n", &hits), "B1") && hits == 0);
    /* a changed resource replaces the entry */
    CHECK(has(get("GET", "/etag2", NULL, OK() "ETag: \"a\"\r\n\r\nC1", &hits), "C1") && hits == 1);
    CHECK(has(get("GET", "/etag2", NULL, OK() "ETag: \"b\"\r\n\r\nC2", &hits), "C2") && hits == 1 && strstr(g_request, "If-None-Match: \"a\""));

    /* no-store and Authorization are never cached */
    const char *nostore = OK() "Cache-Control: no-store, max-age=600\r\n\r\nD1";
    CHECK(has(get("GET", "/nostore", NULL, nostore, &hits), "D1") && hits == 1);
    CHECK(has(get("GET", "/nostore", NULL, nostore, &hits), "D1") && hits == 1);
    CHECK(has(get("GET", "/auth", "Authorization: Bearer secret-token", fresh, &hits), "A1") && hits == 1);
    CHECK(has(get("GET", "/auth", "Authorization: Bearer secret-token", fresh, &hits), "A1") && hits == 1);

    /* Vary selects on a request header whose value is stored only as a hash */
    const char *vary = OK() "Cache-Control: max-age=600\r\nVary: X-Secret\r\n\r\nE1";
    CHECK(has(get("GET", "/vary", "X-Secret: vary-value", vary, &hits), "E1") && hits == 1);
    CHECK(has(get("GET", "/vary", "X-Secret: vary-value", vary, &hits), "E1") && hits == 0);
    CHECK(has(get("GET", "/vary", "X-Secret: other", vary, &hits), "E1") && hits == 1);
    CHECK(disk_free_of(dir, "vary-value"));

    /* a successful POST invalidates the stored GET */
    CHECK(has(get("GET", "/post", NULL, fresh, &hits), "A1") && hits == 1);
    CHECK(has(get("POST", "/post", NULL, OK() "\r\nP1", &hits), "P1") && hits == 1);
    CHECK(has(get("GET", "/post", NULL, fresh, &hits), "A1") && hits == 1);

    /* default mode does not reuse an entry without validators or freshness */
    CHECK(has(get("GET", "/plain", NULL, OK() "\r\nF1", &hits), "F1") && hits == 1);
    CHECK(has(get("GET", "/plain", NULL, OK() "\r\nF2", &hits), "F2") && hits == 1);


    /* An only-if-cached miss must not contact even an available server. */
    const char *offline = "HTTP/1.1 500 Must Not Reach Network\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    CHECK(cache_miss_is(policy_get("/policy-miss", NET_CACHE_ONLY, NULL, offline, -1, &hits)) && hits == 0);

    /* Explicit cache modes may reuse a stale entry without validators. */
    const char *stale_plain = OK() "Cache-Control: max-age=0\r\n\r\nS1";
    CHECK(response_is(policy_get("/policy-stale", NET_CACHE_DEFAULT, NULL, stale_plain, -1, &hits), 200, "OK", "S1") && hits == 1);
    CHECK(response_is(policy_get("/policy-stale", NET_CACHE_ONLY, NULL, offline, -1, &hits), 200, "OK", "S1") && hits == 0);
    CHECK(response_is(policy_get("/policy-stale", NET_CACHE_FORCE, NULL, offline, -1, &hits), 200, "OK", "S1") && hits == 0);
    CHECK(response_is(policy_get("/policy-stale", NET_CACHE_DEFAULT, NULL, OK() "Cache-Control: max-age=0\r\n\r\nS2", -1, &hits), 200, "OK", "S2") && hits == 1);

    /* no-store bypasses an old entry and leaves it intact; a new response is not stored. */
    CHECK(response_is(policy_get("/policy-no-store-old", NET_CACHE_DEFAULT, NULL, fresh, -1, &hits), 200, "OK", "A1") && hits == 1);
    CHECK(response_is(policy_get("/policy-no-store-old", NET_CACHE_NO_STORE, NULL, OK() "Cache-Control: max-age=600\r\n\r\nN1", -1, &hits), 200, "OK", "N1") && hits == 1);
    CHECK(strstr(g_request, "Cache-Control: no-cache") && strstr(g_request, "Pragma: no-cache"));
    CHECK(response_is(policy_get("/policy-no-store-old", NET_CACHE_ONLY, NULL, offline, -1, &hits), 200, "OK", "A1") && hits == 0);
    CHECK(response_is(policy_get("/policy-no-store-new", NET_CACHE_NO_STORE, NULL, fresh, -1, &hits), 200, "OK", "A1") && hits == 1);
    CHECK(cache_miss_is(policy_get("/policy-no-store-new", NET_CACHE_ONLY, NULL, offline, -1, &hits)) && hits == 0);

    /* reload bypasses lookup but replaces the stored response. */
    CHECK(response_is(policy_get("/policy-reload", NET_CACHE_DEFAULT, NULL, fresh, -1, &hits), 200, "OK", "A1") && hits == 1);
    CHECK(response_is(policy_get("/policy-reload", NET_CACHE_RELOAD, NULL, OK() "Cache-Control: max-age=600\r\n\r\nR2", -1, &hits), 200, "OK", "R2") && hits == 1);
    CHECK(response_is(policy_get("/policy-reload", NET_CACHE_ONLY, NULL, offline, -1, &hits), 200, "OK", "R2") && hits == 0);

    /* no-cache validates even a fresh entry and returns the merged200 body. */
    const char *fresh_etag = OK() "Cache-Control: max-age=600\r\nETag: \"policy-v1\"\r\n\r\nV1";
    CHECK(response_is(policy_get("/policy-validate", NET_CACHE_DEFAULT, NULL, fresh_etag, -1, &hits), 200, "OK", "V1") && hits == 1);
    CHECK(response_is(policy_get("/policy-validate", NET_CACHE_NO_CACHE, NULL, "HTTP/1.1 304 Not Modified\r\nCache-Control: max-age=600\r\nConnection: close\r\n\r\n", -1, &hits), 200, "OK", "V1") && hits == 1);
    CHECK(strstr(g_request, "If-None-Match: \"policy-v1\"") && strstr(g_request, "Cache-Control: max-age=0"));
    CHECK(response_is(policy_get("/policy-validate", NET_CACHE_ONLY, NULL, offline, -1, &hits), 200, "OK", "V1") && hits == 0);

    /* Version1 files remain readable after the version2 writer is introduced. */
    CHECK(response_is(policy_get("/policy-legacy", NET_CACHE_DEFAULT, NULL, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\nCache-Control: max-age=600\r\nConnection: close\r\n\r\nLEGACY1", -1, &hits), 200, "OK", "LEGACY1") && hits == 1);
    CHECK(make_legacy_one(dir, "LEGACY1"));
    CHECK(response_is(policy_get("/policy-legacy", NET_CACHE_ONLY, NULL, offline, -1, &hits), 200, "OK", "LEGACY1") && hits == 0);

    /* Version2 preserves a cached redirect's reason, Location and body. */
    const char *redirect = "HTTP/1.1 302 QA Redirect\r\nContent-Length: 2\r\nCache-Control: max-age=600\r\nLocation: /policy-target\r\nConnection: close\r\n\r\nD2";
    CHECK(response_is(policy_get("/policy-target", NET_CACHE_DEFAULT, NULL, OK() "Cache-Control: max-age=600\r\n\r\nT1", -1, &hits), 200, "OK", "T1") && hits == 1);
    CHECK(response_is(policy_get("/policy-redirect", NET_CACHE_DEFAULT, NULL, redirect, -1, &hits), 302, "QA Redirect", "D2") && hits == 1);
    NetResponse *cached_redirect = policy_get("/policy-redirect", NET_CACHE_ONLY, NULL, offline, -1, &hits);
    CHECK(hits == 0 && headers_get(&cached_redirect->headers, "location") && !strcmp(headers_get(&cached_redirect->headers, "location"), "/policy-target"));
    CHECK(response_is(cached_redirect, 302, "QA Redirect", "D2"));
    CHECK(response_is(policy_get("/policy-redirect", NET_CACHE_ONLY, NULL, offline, 2, &hits), 200, "OK", "T1") && hits == 0);
    const char *redirect_miss = "HTTP/1.1 302 QA Missing Target\r\nContent-Length: 2\r\nCache-Control: max-age=600\r\nLocation: /policy-never-cached\r\nConnection: close\r\n\r\nD3";
    CHECK(response_is(policy_get("/policy-redirect-miss", NET_CACHE_DEFAULT, NULL, redirect_miss, -1, &hits), 302, "QA Missing Target", "D3") && hits == 1);
    CHECK(cache_miss_is(policy_get("/policy-redirect-miss", NET_CACHE_ONLY, NULL, offline, 2, &hits)) && hits == 0);

    /* Cache-only does not bypass the existing Authorization or Vary isolation. */
    CHECK(cache_miss_is(policy_get("/asset.js?token=secret-query", NET_CACHE_ONLY, "Authorization: Bearer secret-token", offline, -1, &hits)) && hits == 0);
    CHECK(cache_miss_is(policy_get("/vary", NET_CACHE_ONLY, "X-Secret: never-selected", offline, -1, &hits)) && hits == 0);
    CHECK(disk_free_of(dir, "secret-query") && disk_free_of(dir, "secret-cookie") &&
          disk_free_of(dir, "secret-token") && disk_free_of(dir, "vary-value"));

    puts("http cache: ok");
    return 0;
}
