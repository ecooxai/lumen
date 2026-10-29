#include "../src/net/net.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef LUMEN_FETCH_METADATA_FULL_PSL
#define SUBDOMAIN_SITE "same-site"
#define SITE_TEST_SCOPE "pinned full PSL"
#else
/* This is an explicit conservative fallback contract, not a normative
   same-site result or a substitute for full-PSL conformance coverage. */
#define SUBDOMAIN_SITE "cross-site"
#define SITE_TEST_SCOPE "conservative no-PSL fallback"
#endif
static _Atomic unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void header(NetRequest *r, const char *name, const char *value) {
    const char *actual = headers_get(&r->headers, name);
    CHECK(value ? actual && !strcmp(actual, value) : actual == NULL);
}
static void update(NetRequest *r, const char *url, const char *site, const char *mode) {
    URL u; CHECK(url_parse(url, &u));
    net_request_update_fetch_metadata(r, &u); url_free(&u);
    header(r, "Sec-Fetch-Dest", site ? "style" : NULL);
    header(r, "Sec-Fetch-Mode", site ? mode : NULL);
    header(r, "Sec-Fetch-Site", site);
    header(r, "Sec-Fetch-User", NULL);
}
static void isolated(const char *origin, const char *target, const char *site) {
    NetRequest r = {0};
    net_request_set_fetch_metadata(&r, origin, "style", "no-cors");
    update(&r, target, site, "no-cors");
    headers_free(&r.headers); net_request_free_fetch_metadata(&r);
}

typedef struct { int fd, count; unsigned port, other_port; bool first; } Server;
static void *serve(void *opaque) {
    Server *s = opaque;
    for (int i = 0; i < s->count; i++) {
        int client = accept(s->fd, NULL, NULL); CHECK(client >= 0);
        struct timeval limit = {4, 0};
        CHECK(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit) == 0);
        char request[8192] = {0}; size_t used = 0;
        while (!strstr(request, "\r\n\r\n")) {
            ssize_t n = recv(client, request + used, sizeof request - used - 1, 0);
            CHECK(n > 0); used += (size_t)n; request[used] = 0; CHECK(used + 1 < sizeof request);
        }
        CHECK(strstr(request, "\r\nSec-Fetch-Dest: style\r\n") != NULL);
        CHECK(strstr(request, "\r\nSec-Fetch-Mode: cors\r\n") != NULL);
        CHECK(strstr(request, s->first && i == 0 ? "\r\nSec-Fetch-Site: same-origin\r\n" : "\r\nSec-Fetch-Site: cross-site\r\n") != NULL);
        CHECK(strcasestr(request, "\r\nSec-Fetch-User:") == NULL);
        char response[1024];
        int n = s->first && i == 1
            ? snprintf(response, sizeof response, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok")
            : snprintf(response, sizeof response, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.%d:%u/%s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                s->first ? 2 : 1, s->other_port, s->first ? "middle" : "last");
        CHECK(n > 0 && (size_t)n < sizeof response);
        size_t done = 0;
        while (done < (size_t)n) { ssize_t sent = send(client, response + done, (size_t)n - done, 0); CHECK(sent > 0); done += (size_t)sent; }
        close(client);
    }
    return NULL;
}
static void listen_loopback(Server *s, unsigned ip) {
    s->fd = socket(AF_INET, SOCK_STREAM, 0); CHECK(s->fd >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(ip)};
    CHECK(bind(s->fd, (struct sockaddr *)&address, sizeof address) == 0);
    CHECK(listen(s->fd, 3) == 0); socklen_t n = sizeof address;
    CHECK(getsockname(s->fd, (struct sockaddr *)&address, &n) == 0); s->port = ntohs(address.sin_port);
}
static void wire_redirects(void) {
    Server a = {.count = 2, .first = true}, b = {.count = 1};
    listen_loopback(&a, 0x7f000001u); listen_loopback(&b, 0x7f000002u);
    a.other_port = b.port; b.other_port = a.port;
    pthread_t ta, tb; CHECK(pthread_create(&ta, NULL, serve, &a) == 0); CHECK(pthread_create(&tb, NULL, serve, &b) == 0);
    char origin[100], url[120]; snprintf(origin, sizeof origin, "http://127.0.0.1:%u", a.port); snprintf(url, sizeof url, "%s/first", origin);
    net_set_proxy(NULL);
    NetRequest *r = net_request_new("GET", url); r->no_cookies = true;
    net_request_set_fetch_metadata(r, origin, "style", "cors");
    NetResponse *response = net_fetch_sync(r);
    CHECK(response && response->status == 200 && response->body_len == 2 && !memcmp(response->body, "ok", 2));
    net_response_free(response);
    CHECK(pthread_join(ta, NULL) == 0); CHECK(pthread_join(tb, NULL) == 0);
    close(a.fd); close(b.fd);
}
int main(void) {
    alarm(15);
    isolated("https://example.com/page", "https://example.com/s.css", "same-origin");
    isolated("https://example.com:443/page", "https://example.com/s.css", "same-origin");
    isolated("https://example.com:8443/page", "https://example.com/s.css", "same-site");
    isolated("https://a.example.com/", "https://b.example.com/s.css", SUBDOMAIN_SITE);
    isolated("https://a.example.co.uk/", "https://b.example.co.uk/s.css", SUBDOMAIN_SITE);
    isolated("https://a.co.uk/", "https://b.co.uk/s.css", "cross-site");
    isolated("https://a.github.io/", "https://b.github.io/s.css", "cross-site");
    isolated("https://x.a.github.io/", "https://y.a.github.io/s.css", SUBDOMAIN_SITE);
    isolated("https://a.appspot.com/", "https://b.appspot.com/s.css", "cross-site");
    isolated("https://a.example.com/", "https://example.com.evil/s.css", "cross-site");
    isolated("http://example.com/", "https://example.com/s.css", "cross-site");
    isolated("https://example.com/", "https://example.com./s.css", "cross-site");
    isolated("https://www.example.com./", "https://a.example.com./s.css", SUBDOMAIN_SITE);
    isolated("https://127.0.0.1/", "https://127.0.0.2/s.css", "cross-site");
    isolated("http://127.0.0.1:8000/", "http://127.0.0.1:9000/s.css", "same-site");
    isolated("http://[::1]:8000/", "http://[::1]:9000/s.css", "same-site");
    isolated("https://example.com/", "http://127.255.1.2/s.css", "cross-site");
    isolated("https://example.com/", "http://128.0.0.1/s.css", NULL);
    isolated("https://example.com/", "http://localhost.evil/s.css", NULL);
    isolated("https://example.com/", "http://example.com/s.css", NULL);
    isolated("null", "https://example.com/s.css", "cross-site");
    isolated("data:text/html,hello", "https://example.com/s.css", "cross-site");
    isolated(NULL, "https://example.com/s.css", "cross-site");
    NetRequest r = {0};
    net_request_set_fetch_metadata(&r, "https://a.example.com", "style", "cors");
    update(&r, "https://a.example.com/first", "same-origin", "cors");
    update(&r, "https://b.example.com/middle", SUBDOMAIN_SITE, "cors");
    update(&r, "https://a.example.com/last", SUBDOMAIN_SITE, "cors");
    update(&r, "https://different.test/middle", "cross-site", "cors");
    update(&r, "https://a.example.com/last", "cross-site", "cors");
    update(&r, "http://a.example.com/insecure", NULL, "cors");
    update(&r, "https://a.example.com/secure", "cross-site", "cors");
    headers_free(&r.headers); net_request_free_fetch_metadata(&r);
    /* Ordinary requests and script fetch metadata are not rewritten. */
    headers_add(&r.headers, "Sec-Fetch-Dest", "empty");
    URL u; CHECK(url_parse("https://example.com", &u)); net_request_update_fetch_metadata(&r, &u); url_free(&u);
    header(&r, "Sec-Fetch-Dest", "empty"); headers_free(&r.headers);
    wire_redirects();
    printf("fetch-metadata: %u/%u checks passed (%s)\n", checks, checks, SITE_TEST_SCOPE);
    return 0;
}
