/* HTTP proxy: plain http uses absolute-form targets with proxy credentials, https tunnels
 * through CONNECT, and loopback / NO_PROXY hosts bypass the proxy. */
#include "../src/net/net.h"
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
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
        send(client, g_response, strlen(g_response), 0);
        close(client);
    }
}

static const char k_ok[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";

/* Fetches url, answering with response if the proxy is asked; returns the response. */
static NetResponse *get(const char *url, const char *response) {
    g_response = response; g_request[0] = 0;
    NetRequest *request = net_request_new("GET", url);
    request->no_cookies = true;
    NetResponse *r = net_fetch_sync(request);
    return r;
}

int main(void) {
    setenv("NO_PROXY", "skip.invalid, .also.invalid", 1);
    unsetenv("LUMEN_PROXY"); unsetenv("https_proxy"); unsetenv("HTTPS_PROXY"); unsetenv("http_proxy"); unsetenv("HTTP_PROXY");
    net_init(1);
    g_listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    CHECK(bind(g_listener, (struct sockaddr *)&address, sizeof address) == 0 && listen(g_listener, 8) == 0);
    socklen_t size = sizeof address;
    CHECK(getsockname(g_listener, (struct sockaddr *)&address, &size) == 0);
    int port = ntohs(address.sin_port);
    pthread_t thread; pthread_create(&thread, NULL, serve, NULL);
    char spec[128];

    snprintf(spec, sizeof spec, "127.0.0.1:%d", port); net_set_proxy(spec);
    NetResponse *r = get("http://origin.invalid/a/b?c=1#frag", k_ok);
    CHECK(r->status == 200 && r->body_len == 2 && !memcmp(r->body, "ok", 2));
    CHECK(!strncmp(g_request, "GET http://origin.invalid/a/b?c=1 HTTP/1.1\r\n", 44));
    CHECK(strstr(g_request, "\r\nHost: origin.invalid\r\n") && !strstr(g_request, "Proxy-Authorization"));
    net_response_free(r);

    r = get("http://origin.invalid:8081/x", k_ok);
    CHECK(r->status == 200 && !strncmp(g_request, "GET http://origin.invalid:8081/x HTTP/1.1\r\n", 43));
    net_response_free(r);

    snprintf(spec, sizeof spec, "http://user:pw@127.0.0.1:%d/", port); net_set_proxy(spec);
    r = get("http://origin.invalid/", k_ok);
    CHECK(r->status == 200 && strstr(g_request, "\r\nProxy-Authorization: Basic dXNlcjpwdw==\r\n"));
    net_response_free(r);

    int before = __atomic_load_n(&g_hits, __ATOMIC_SEQ_CST);
    r = get("https://secure.invalid/path", "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
    CHECK(__atomic_load_n(&g_hits, __ATOMIC_SEQ_CST) == before + 1);
    CHECK(!strncmp(g_request, "CONNECT secure.invalid:443 HTTP/1.1\r\nHost: secure.invalid:443\r\n", 63));
    CHECK(strstr(g_request, "\r\nProxy-Authorization: Basic dXNlcjpwdw==\r\n") && !strstr(g_request, "/path"));
    CHECK(r->status == 0 && r->error && strstr(r->error, "403"));
    net_response_free(r);

    /* loopback goes direct: the listener sees an origin-form request */
    char url[128]; snprintf(url, sizeof url, "http://localhost:%d/direct", port);
    r = get(url, k_ok);
    CHECK(r->status == 200 && !strncmp(g_request, "GET /direct HTTP/1.1\r\n", 22));
    net_response_free(r);

    /* NO_PROXY hosts go direct (and fail to resolve) without asking the proxy */
    before = __atomic_load_n(&g_hits, __ATOMIC_SEQ_CST);
    r = get("http://skip.invalid/", k_ok); CHECK(r->status == 0); net_response_free(r);
    r = get("http://x.also.invalid/", k_ok); CHECK(r->status == 0); net_response_free(r);
    CHECK(__atomic_load_n(&g_hits, __ATOMIC_SEQ_CST) == before);

    net_set_proxy(NULL);
    r = get("http://origin.invalid/", k_ok); CHECK(r->status == 0); net_response_free(r);
    CHECK(__atomic_load_n(&g_hits, __ATOMIC_SEQ_CST) == before);
    puts("proxy ok");
    return 0;
}
