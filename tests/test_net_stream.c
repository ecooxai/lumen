#include "../src/net/net.h"
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
enum { CHUNKS = 100, CHUNK_BYTES = 4096 };
static pthread_mutex_t wake_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake_cv = PTHREAD_COND_INITIALIZER;
static int wakes;
static void wake(void) {
    pthread_mutex_lock(&wake_mu); wakes++; pthread_cond_signal(&wake_cv); pthread_mutex_unlock(&wake_mu);
}
static void await_wakes(int count) {
    struct timespec limit; clock_gettime(CLOCK_REALTIME, &limit); limit.tv_sec += 5;
    pthread_mutex_lock(&wake_mu);
    while (wakes < count) CHECK(pthread_cond_timedwait(&wake_cv, &wake_mu, &limit) == 0);
    pthread_mutex_unlock(&wake_mu);
}
static void send_all(int fd, const void *bytes, size_t n) {
    const char *p = bytes;
    while (n) { ssize_t k = send(fd, p, n, 0); CHECK(k > 0); p += k; n -= (size_t)k; }
}
typedef struct { int listener; bool truncated, paced; } Server;
static void *serve(void *arg) {
    Server *s = arg;
    int fd = accept(s->listener, NULL, NULL); CHECK(fd >= 0);
    char request[4096]; size_t n = 0;
    do { ssize_t k = recv(fd, request + n, sizeof request - n - 1, 0); CHECK(k > 0); n += (size_t)k; request[n] = 0; }
    while (!strstr(request, "\r\n\r\n"));
    const char *head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    send_all(fd, head, strlen(head));
    char chunk[CHUNK_BYTES];
    for (int i = 0; i < CHUNKS; i++) {
        memset(chunk, 'a' + i % 26, sizeof chunk);
        send_all(fd, "1000\r\n", 6); send_all(fd, chunk, sizeof chunk); send_all(fd, "\r\n", 2);
        if (s->paced) usleep(2000);
    }
    if (!s->truncated) send_all(fd, "0\r\n\r\n", 5);
    close(fd); return NULL;
}
typedef struct { int heads, chunks, dones; size_t bytes; bool truncated; } Result;
static void head(NetRequest *q, NetResponse *r, void *arg) {
    (void)q; Result *out = arg; CHECK(out->heads++ == 0 && out->chunks == 0 && out->dones == 0); CHECK(r->status == 200);
}
static void chunk(NetRequest *q, const char *bytes, size_t n, void *arg) {
    (void)q; Result *out = arg; CHECK(out->heads == 1 && out->dones == 0); CHECK(n && n <= 256 * 1024);
    for (size_t i = 0; i < n; i++) CHECK(bytes[i] == 'a' + ((out->bytes + i) / CHUNK_BYTES) % 26);
    out->bytes += n; out->chunks++;
}
static void done(NetRequest *q, NetResponse *r, void *arg) {
    (void)q; Result *out = arg; CHECK(out->heads == 1 && out->dones++ == 0);
    CHECK(out->bytes == CHUNKS * CHUNK_BYTES); CHECK((r->error != NULL) == out->truncated);
}
static void run(bool truncated, bool paced) {
    pthread_mutex_lock(&wake_mu); wakes = 0; pthread_mutex_unlock(&wake_mu);
    Server server = {.listener = socket(AF_INET, SOCK_STREAM, 0), .truncated = truncated, .paced = paced};
    CHECK(server.listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(server.listener, (struct sockaddr *)&address, sizeof address) == 0); CHECK(listen(server.listener, 1) == 0);
    socklen_t size = sizeof address; CHECK(getsockname(server.listener, (struct sockaddr *)&address, &size) == 0);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, serve, &server) == 0);
    char url[128]; snprintf(url, sizeof url, "http://127.0.0.1:%u/stream", ntohs(address.sin_port));
    Result result = {.truncated = truncated};
    NetRequest *request = net_request_new("GET", url);
    request->no_cookies = true; request->head = head; request->chunk = chunk; request->done = done; request->ud = &result;
    net_fetch(request);
    if (!paced) {
        /* Simulate a UI busy until the whole response has reached the queue. */
        await_wakes(CHUNKS + 2);
        CHECK(net_poll() == 1); CHECK(result.chunks == 2);
    } else {
        while (!result.dones) { net_poll(); usleep(1000); }
        CHECK(result.chunks > 2); /* Fresh data is not delayed to form a batch. */
    }
    CHECK(result.dones == 1 && net_pending() == 0);
    CHECK(pthread_join(thread, NULL) == 0); close(server.listener);
}
int main(void) {
    alarm(15); net_wakeup = wake; net_init(1);
    run(false, false); run(true, false); run(false, true);
    net_shutdown(); puts("stream byte ordering, bounded batching, live delivery and truncation passed");
    return 0;
}
