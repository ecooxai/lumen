#include "../src/net/net.h"
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <zstd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL setup line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static size_t total, passed;
static void check(bool ok, const char *name, const char *part, int mode) {
    total++; if (ok) passed++;
    else fprintf(stderr, "FAIL zstd %s mode%d: %s\n", name, mode, part);
}
typedef struct {
    const char *name, *method;
    const void *encoded, *expected;
    size_t length, expected_length;
    bool bad, chunked;
    int status;
} Case;
typedef struct {
    int listener, mode; const Case *test;
    pthread_mutex_t mu; pthread_cond_t cv; bool head_seen;
} Server;
static void send_all(int fd, const void *bytes, size_t n) {
    const char *p = bytes;
    while (n) { ssize_t k = send(fd, p, n, MSG_NOSIGNAL); CHECK(k > 0); p += k; n -= (size_t)k; }
}
static void *serve(void *arg) {
    Server *s = arg; const Case *t = s->test;
    int fd = accept(s->listener, NULL, NULL); CHECK(fd >= 0);
    struct timeval timeout = {3, 0}; CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);
    char request[4096]; size_t n = 0;
    do { CHECK(n + 1 < sizeof request); ssize_t k = recv(fd, request + n, sizeof request - n - 1, 0); CHECK(k > 0); n += (size_t)k; request[n] = 0; }
    while (!strstr(request, "\r\n\r\n"));
    char header[512];
    int hn = snprintf(header, sizeof header, "HTTP/1.1 %d OK\r\nContent-Encoding: zstd\r\nCache-Control: no-store\r\nConnection: close\r\n", t->status);
    CHECK(hn > 0 && (size_t)hn < sizeof header); send_all(fd, header, (size_t)hn);
    if (t->chunked) send_all(fd, "Transfer-Encoding: chunked\r\n\r\n", 30);
    else { hn = snprintf(header, sizeof header, "Content-Length: %zu\r\n\r\n", t->length); send_all(fd, header, (size_t)hn); }
    if (s->mode != 0) {
        /* Require the native head callback before the first encoded byte. */
        struct timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += 3;
        pthread_mutex_lock(&s->mu);
        while (!s->head_seen) CHECK(pthread_cond_timedwait(&s->cv, &s->mu, &deadline) == 0);
        pthread_mutex_unlock(&s->mu);
    }
    bool no_body = !strcmp(t->method, "HEAD") || t->status == 204 || t->status == 304;
    if (!no_body) {
        for (size_t at = 0; at < t->length; ) {
            size_t size = t->length - at; if (size > 17) size = 17;
            if (t->chunked) { hn = snprintf(header, sizeof header, "%zx\r\n", size); send_all(fd, header, (size_t)hn); }
            send_all(fd, (const char *)t->encoded + at, size);
            if (t->chunked) send_all(fd, "\r\n", 2);
            at += size;
        }
        if (t->chunked) send_all(fd, "0\r\n\r\n", 5);
    }
    close(fd); return NULL;
}
typedef struct { Server *server; SB bytes; int heads, dones, status; bool failed, head_error; } Result;
static void head(NetRequest *q, NetResponse *r, void *arg) {
    (void)q; Result *o = arg; o->heads++; o->head_error = r->error != NULL; o->status = r->status;
    pthread_mutex_lock(&o->server->mu); o->server->head_seen = true;
    pthread_cond_signal(&o->server->cv); pthread_mutex_unlock(&o->server->mu);
}
static void chunk(NetRequest *q, const char *bytes, size_t n, void *arg) {
    (void)q; Result *o = arg; CHECK(o->heads == 1 && o->dones == 0); sb_put(&o->bytes, bytes, n);
}
static bool sink_head(NetRequest *q, const NetResponse *r, void *arg) { head(q, (NetResponse *)r, arg); return true; }
static bool sink(NetRequest *q, const char *bytes, size_t n, void *arg) { chunk(q, bytes, n, arg); return true; }
static void done(NetRequest *q, NetResponse *r, void *arg) {
    (void)q; Result *o = arg; o->dones++; o->failed = r->error != NULL; o->status = r->status;
}
static void run(const Case *test, int mode) {
    /* HEAD deliberately does not invoke the download sink; its sync path is covered. */
    if (mode == 2 && !strcmp(test->method, "HEAD")) return;
    Server server = {.listener = socket(AF_INET, SOCK_STREAM, 0), .mode = mode, .test = test,
                     .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};
    CHECK(server.listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(server.listener, (struct sockaddr *)&address, sizeof address) == 0); CHECK(listen(server.listener, 1) == 0);
    socklen_t size = sizeof address; CHECK(getsockname(server.listener, (struct sockaddr *)&address, &size) == 0);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, serve, &server) == 0);
    char url[128]; snprintf(url, sizeof url, "http://127.0.0.1:%u/zstd", ntohs(address.sin_port));
    NetRequest *q = net_request_new(test->method, url); q->no_cookies = true;
    Result result = {.server = &server}; sb_init(&result.bytes);
    if (!mode) {
        NetResponse *r = net_fetch_sync(q); CHECK(r != NULL);
        result.failed = r->error != NULL; result.status = r->status; result.dones = 1;
        sb_put(&result.bytes, r->body, r->body_len); net_response_free(r);
    } else {
        q->ud = &result; q->done = done;
        if (mode == 1) { q->head = head; q->chunk = chunk; }
        else { q->on_head = sink_head; q->sink = sink; q->sink_ud = &result; }
        net_fetch(q);
        while (!result.dones) { net_poll(); usleep(1000); }
    }
    CHECK(pthread_join(thread, NULL) == 0); close(server.listener);
    pthread_cond_destroy(&server.cv); pthread_mutex_destroy(&server.mu);
    check(result.status == test->status && result.dones == 1, test->name, "HTTP status and completion", mode);
    check(!mode || (result.heads == 1 && !result.head_error && server.head_seen), test->name, "head resolved before body", mode);
    check(result.failed == test->bad, test->name, "final decoding outcome", mode);
    if (!test->bad) check(result.bytes.n == test->expected_length && (!result.bytes.n || !memcmp(result.bytes.s, test->expected, result.bytes.n)), test->name, "decoded bytes", mode);
    if (test->bad && mode && test->expected) check(result.bytes.n >= test->expected_length && !memcmp(result.bytes.s, test->expected, test->expected_length), test->name, "valid prefix delivered before later frame error", mode);
    check(net_pending() == 0, test->name, "request drained", mode);
    sb_free(&result.bytes);
}
static SB compress_frame(const void *bytes, size_t length, bool checksum) {
    SB out; sb_init(&out); size_t bound = ZSTD_compressBound(length);
    char *buffer = xmalloc(bound); ZSTD_CCtx *c = ZSTD_createCCtx(); CHECK(c != NULL);
    CHECK(!ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, checksum)));
    size_t n = ZSTD_compress2(c, buffer, bound, bytes, length); CHECK(!ZSTD_isError(n));
    sb_put(&out, buffer, n); ZSTD_freeCCtx(c); free(buffer); return out;
}
int main(void) {
    alarm(45); signal(SIGPIPE, SIG_IGN); net_init(1); net_set_proxy(NULL);
    static const char payload[] = "native zstd frame";
    SB frame = compress_frame(payload, sizeof payload - 1, false), empty = compress_frame("", 0, false);
    SB checked = compress_frame(payload, sizeof payload - 1, true); checked.s[checked.n - 1] ^= 1;
    SB joined; sb_init(&joined); sb_put(&joined, frame.s, frame.n); sb_put(&joined, frame.s, frame.n);
    static const unsigned char skip[] = {0x50,0x2a,0x4d,0x18,3,0,0,0,'a','b','c'};
    SB skipped; sb_init(&skipped); sb_put(&skipped, (const char *)skip, sizeof skip); sb_put(&skipped, frame.s, frame.n); sb_put(&skipped, (const char *)skip, sizeof skip);
    SB junk; sb_init(&junk); sb_put(&junk, frame.s, frame.n); sb_put(&junk, "junk", 4);
    static const unsigned char window8[] = {0x28,0xb5,0x2f,0xfd,0,0x68,1,0,0};
    static const unsigned char window16[] = {0x28,0xb5,0x2f,0xfd,0,0x70,1,0,0};
    char binary[131073]; for (size_t i = 0; i < sizeof binary; i++) binary[i] = (char)(i * 37);
    SB large = compress_frame(binary, sizeof binary, false);
    Case cases[] = {
        {"valid fragmented", "GET", frame.s, payload, frame.n, sizeof payload - 1, false, true, 200},
        {"valid empty frame", "GET", empty.s, "", empty.n, 0, false, false, 200},
        {"valid binary output boundary", "GET", large.s, binary, large.n, sizeof binary, false, true, 200},
        {"concatenated frames", "GET", joined.s, "native zstd framenative zstd frame", joined.n, 2 * (sizeof payload - 1), false, true, 200},
        {"skippable frames", "GET", skipped.s, payload, skipped.n, sizeof payload - 1, false, true, 200},
        {"skippable only", "GET", skip, "", sizeof skip, 0, false, false, 200},
        {"bad magic", "GET", "not actually zstd", NULL, 17, 0, true, false, 200},
        {"truncated frame", "GET", frame.s, NULL, frame.n - 1, 0, true, false, 200},
        {"truncated header", "GET", frame.s, NULL, 3, 0, true, true, 200},
        {"bad checksum", "GET", checked.s, NULL, checked.n, 0, true, true, 200},
        {"trailing junk", "GET", junk.s, NULL, junk.n, 0, true, false, 200},
        {"truncated second frame", "GET", joined.s, payload, joined.n - 1, sizeof payload - 1, true, true, 200},
        {"truncated skip frame", "GET", skip, NULL, sizeof skip - 1, 0, true, false, 200},
        {"eight MiB window boundary", "GET", window8, "", sizeof window8, 0, false, false, 200},
        {"oversized sixteen MiB window", "GET", window16, NULL, sizeof window16, 0, true, false, 200},
        {"empty HTTP body", "GET", "", "", 0, 0, false, false, 200},
        {"HEAD has no body", "HEAD", frame.s, "", frame.n, 0, false, false, 200},
        {"204 has no body", "GET", "", "", 0, 0, false, false, 204},
        {"304 has no body", "GET", "", "", 0, 0, false, false, 304}
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) for (int mode = 0; mode < 3; mode++) run(&cases[i], mode);
    sb_free(&frame); sb_free(&empty); sb_free(&checked); sb_free(&joined); sb_free(&skipped); sb_free(&junk); sb_free(&large);
    net_shutdown(); printf("HTTP zstd controls: %zu/%zu PASS across sync, stream and sink paths\n", passed, total);
    return passed == total ? 0 : 1;
}
