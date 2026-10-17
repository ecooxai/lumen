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

    /* without validators or freshness nothing is kept */
    CHECK(has(get("GET", "/plain", NULL, OK() "\r\nF1", &hits), "F1") && hits == 1);
    CHECK(has(get("GET", "/plain", NULL, OK() "\r\nF2", &hits), "F2") && hits == 1);

    puts("http cache: ok");
    return 0;
}
