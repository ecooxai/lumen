#include "../src/net/net.h"
#include <netinet/in.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <zlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL setup line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static size_t passed, total;
static const char payload[] = "native range negotiation";
static void check(bool ok, const char *name, const char *part) {
    total++;
    if (ok) passed++;
    else fprintf(stderr, "FAIL HTTP Range: %s (%s)\n", name, part);
}
typedef struct { int listener; bool gzip; unsigned encodings; char encoding[256]; } Server;
static void send_all(int fd, const void *bytes, size_t length) {
    const char *p = bytes;
    while (length) { ssize_t n = send(fd, p, length, 0); CHECK(n > 0); p += n; length -= (size_t)n; }
}
static void *serve(void *arg) {
    Server *server = arg;
    int fd = accept(server->listener, NULL, NULL); CHECK(fd >= 0);
    struct timeval timeout = {5, 0};
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);
    char request[8192]; size_t used = 0;
    while (used + 1 < sizeof request) {
        ssize_t n = recv(fd, request + used, sizeof request - used - 1, 0);
        CHECK(n > 0); used += (size_t)n; request[used] = 0;
        if (strstr(request, "\r\n\r\n")) break;
    }
    CHECK(strstr(request, "\r\n\r\n") != NULL);
    for (const char *line = strstr(request, "\r\n"); line && line[2]; ) {
        line += 2;
        const char *end = strstr(line, "\r\n"); if (!end || end == line) break;
        const char *colon = memchr(line, ':', (size_t)(end - line));
        if (colon && colon - line == 15 && !strncasecmp(line, "Accept-Encoding", 15)) {
            const char *value = colon + 1; while (value < end && (*value == ' ' || *value == '\t')) value++;
            while (end > value && (end[-1] == ' ' || end[-1] == '\t')) end--;
            size_t previous = strlen(server->encoding), length = (size_t)(end - value);
            CHECK(previous + (server->encodings ? 2 : 0) + length < sizeof server->encoding);
            if (server->encodings) { memcpy(server->encoding + previous, ", ", 2); previous += 2; }
            memcpy(server->encoding + previous, value, length); server->encoding[previous + length] = 0; server->encodings++;
        }
        line = strstr(line, "\r\n");
    }
    unsigned char compressed[256];
    const void *body = payload; size_t length = sizeof payload - 1;
    if (server->gzip) {
        z_stream z = {0};
        CHECK(deflateInit2(&z, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK);
        z.next_in = (Bytef *)payload; z.avail_in = sizeof payload - 1;
        z.next_out = compressed; z.avail_out = sizeof compressed;
        CHECK(deflate(&z, Z_FINISH) == Z_STREAM_END);
        length = sizeof compressed - z.avail_out; body = compressed; CHECK(deflateEnd(&z) == Z_OK);
    }
    char header[512];
    int hn = snprintf(header, sizeof header, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n%sConnection: close\r\n\r\n",
                      length, server->gzip ? "Content-Encoding: gzip\r\n" : "");
    CHECK(hn > 0 && (size_t)hn < sizeof header);
    send_all(fd, header, (size_t)hn); send_all(fd, body, length);
    close(fd); return NULL;
}
typedef struct {
    const char *name, *method, *range_name, *range_value, *encoding_name, *encoding_value, *expected;
    bool gzip;
} Case;
static const Case cases[] = {
    {"ordinary compressed response", "GET", NULL, NULL, NULL, NULL, NULL, true},
    {"byte range", "GET", "Range", "bytes=0-10", NULL, NULL, "identity", false},
    {"mixed-case range name", "GET", "rAnGe", "bytes=0-10", NULL, NULL, "identity", false},
    {"open-ended byte range", "GET", "Range", "bytes=0-", NULL, NULL, "identity", false},
    {"unknown range unit", "GET", "Range", "foo=0-10", NULL, NULL, "identity", false},
    {"unparsed range value", "GET", "Range", "foo", NULL, NULL, "identity", false},
    {"empty range value", "GET", "Range", "", NULL, NULL, "identity", false},
    {"different header prefix", "GET", "X-Range", "bytes=0-10", NULL, NULL, NULL, true},
    {"different header suffix", "GET", "Range-Extra", "bytes=0-10", NULL, NULL, NULL, true},
    {"explicit internal gzip preserved", "GET", "Range", "bytes=0-10", "Accept-Encoding", "gzip", "gzip, identity", true},
    {"mixed-case internal encoding preserved", "GET", "Range", "bytes=0-10", "aCcEpT-EnCoDiNg", "br", "br, identity", false},
    {"explicit internal identity preserved", "GET", NULL, NULL, "Accept-Encoding", "identity", "identity", false},
    {"explicit empty internal encoding preserved", "GET", "Range", "bytes=0-10", "Accept-Encoding", "", ", identity", false},
    {"range on another method", "POST", "Range", "bytes=0-10", NULL, NULL, "identity", false}
};
static void run(const Case *test) {
    Server server = {.listener = socket(AF_INET, SOCK_STREAM, 0), .gzip = test->gzip};
    CHECK(server.listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(server.listener, (struct sockaddr *)&address, sizeof address) == 0);
    CHECK(listen(server.listener, 1) == 0);
    socklen_t size = sizeof address;
    CHECK(getsockname(server.listener, (struct sockaddr *)&address, &size) == 0);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, serve, &server) == 0);
    char url[128]; snprintf(url, sizeof url, "http://127.0.0.1:%u/range", ntohs(address.sin_port));
    NetRequest *request = net_request_new(test->method, url); request->no_cookies = true;
    if (test->range_name) headers_add(&request->headers, test->range_name, test->range_value);
    if (test->encoding_name) headers_add(&request->headers, test->encoding_name, test->encoding_value);
    NetResponse *response = net_fetch_sync(request);
    CHECK(pthread_join(thread, NULL) == 0); close(server.listener);
    check(response && response->status == 200 && !response->error, test->name, "response completion");
    bool encoding_ok = test->expected ? !strcmp(server.encoding, test->expected)
        : strstr(server.encoding, "gzip") != NULL && strcmp(server.encoding, "identity");
    check(server.encodings == (test->range_name && test->encoding_name ? 2u : 1u) && encoding_ok, test->name, "wire Accept-Encoding");
    check(response && response->body_len == sizeof payload - 1 && !memcmp(response->body, payload, sizeof payload - 1),
          test->name, "decoded response bytes");
    net_response_free(response);
}
int main(void) {
    alarm(15);
    net_set_proxy(NULL);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) run(&cases[i]);
    printf("HTTP Range controls: %zu/%zu PASS across %zu wire requests\n", passed, total, sizeof cases / sizeof cases[0]);
    return passed == total ? 0 : 1;
}
