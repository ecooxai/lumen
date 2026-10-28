/* Native loopback regression for opt-in exact test DNS overrides. */
#include "net/net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(name, condition) do { checks++; if (!(condition)) { failures++; fprintf(stderr, "FAIL: %s\n", name); } } while (0)

typedef struct {
    int listener, port, hits;
    bool stop;
    pthread_t thread;
    pthread_mutex_t mutex;
    char last_request[4096];
} Server;

static void timed_out(int sig) {
    (void)sig;
    static const char message[] = "host-map tests exceeded the 25-second watchdog; system DNS is not cancellable\n";
    (void)write(STDERR_FILENO, message, sizeof message - 1);
    _exit(124);
}

static void *serve(void *argument) {
    Server *s = argument;
    for (;;) {
        pthread_mutex_lock(&s->mutex);
        bool stop = s->stop;
        pthread_mutex_unlock(&s->mutex);
        if (stop) break;
        struct pollfd waiting = { .fd = s->listener, .events = POLLIN };
        if (poll(&waiting, 1, 100) <= 0) continue;
        int peer = accept(s->listener, NULL, NULL);
        if (peer < 0) continue;
        char request[4096] = {0}; size_t used = 0;
        while (used < sizeof request - 1 && !strstr(request, "\r\n\r\n")) {
            struct pollfd reading = { .fd = peer, .events = POLLIN };
            if (poll(&reading, 1, 500) <= 0) break;
            ssize_t got = recv(peer, request + used, sizeof request - used - 1, 0);
            if (got <= 0) break;
            used += (size_t)got; request[used] = 0;
        }
        pthread_mutex_lock(&s->mutex);
        s->hits++;
        memcpy(s->last_request, request, used + 1);
        pthread_mutex_unlock(&s->mutex);
        static const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
        size_t sent = 0;
        while (sent < sizeof response - 1) {
            ssize_t count = send(peer, response + sent, sizeof response - 1 - sent, 0);
            if (count <= 0) break;
            sent += (size_t)count;
        }
        close(peer);
    }
    return NULL;
}

static bool server_start(Server *s) {
    memset(s, 0, sizeof *s);
    s->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listener < 0) return false;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s->listener, (struct sockaddr *)&address, sizeof address) || listen(s->listener, 4)) {
        close(s->listener); return false;
    }
    socklen_t length = sizeof address;
    if (getsockname(s->listener, (struct sockaddr *)&address, &length)) {
        close(s->listener); return false;
    }
    s->port = ntohs(address.sin_port);
    if (pthread_mutex_init(&s->mutex, NULL)) { close(s->listener); return false; }
    if (pthread_create(&s->thread, NULL, serve, s)) {
        pthread_mutex_destroy(&s->mutex); close(s->listener); return false;
    }
    return true;
}

static void server_finish(Server *s) {
    pthread_mutex_lock(&s->mutex); s->stop = true; pthread_mutex_unlock(&s->mutex);
    pthread_join(s->thread, NULL);
    close(s->listener); pthread_mutex_destroy(&s->mutex);
}

static bool has_host_header(const char *request, const char *host, int port) {
    char expected[256]; snprintf(expected, sizeof expected, "%s:%d", host, port);
    for (const char *line = request; line && *line;) {
        const char *end = strstr(line, "\r\n"); if (!end) break;
        if ((size_t)(end - line) >= 5 && !strncasecmp(line, "Host:", 5)) {
            const char *value = line + 5;
            while (value < end && (*value == ' ' || *value == '\t')) value++;
            return (size_t)(end - value) == strlen(expected) && !strncasecmp(value, expected, (size_t)(end - value));
        }
        line = end + 2;
    }
    return false;
}

static void fetch_case(Server *s, const char *host, const char *name, bool reachable) {
    int before;
    pthread_mutex_lock(&s->mutex); before = s->hits; pthread_mutex_unlock(&s->mutex);
    char url[512]; snprintf(url, sizeof url, "http://%s:%d/host-map-%d", host, s->port, checks);
    NetRequest *request = net_request_new("GET", url);
    request->no_cookies = true;
    NetResponse *response = net_fetch_sync(request);
    pthread_mutex_lock(&s->mutex);
    int reached = s->hits - before;
    bool origin_preserved = has_host_header(s->last_request, host, s->port);
    pthread_mutex_unlock(&s->mutex);
    bool ok = reachable
        ? response && response->status == 200 && !response->error && response->body_len == 2 && !memcmp(response->body, "OK", 2)
        : response && response->status == 0 && response->error;
    CHECK(name, ok);
    CHECK(reachable ? "mapped hostname reaches only the owned loopback server" : "unlisted hostname never reaches the mapped server", reached == (reachable ? 1 : 0));
    if (reachable) CHECK("DNS override preserves the original HTTP Host authority", origin_preserved);
    net_response_free(response); net_request_free(request);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN); signal(SIGALRM, timed_out); alarm(25);
    const char *value = getenv("LUMEN_HOST_MAP"); char *old_map = value ? strdup(value) : NULL;
    value = getenv("LUMEN_HOST_MAP_MODE"); char *old_mode = value ? strdup(value) : NULL;
    Server server;
    if (!server_start(&server)) { fprintf(stderr, "FAIL: cannot start owned loopback server\n"); free(old_map); free(old_mode); return 1; }
    net_init(1);
    setenv("LUMEN_HOST_MAP", "LUMEN-MAP-20261022.invalid=127.0.0.1,explicit.lumen-map-20261022.invalid=127.0.0.1", 1);
    unsetenv("LUMEN_HOST_MAP_MODE");
    fetch_case(&server, "legacy.lumen-map-20261022.invalid", "legacy default still maps a suffix subdomain", true);
    setenv("LUMEN_HOST_MAP_MODE", "exact", 1);
    fetch_case(&server, "lumen-map-20261022.invalid", "exact mode maps an explicitly listed hostname case-insensitively", true);
    fetch_case(&server, "explicit.lumen-map-20261022.invalid", "exact mode maps an explicitly listed subdomain", true);
    fetch_case(&server, "missing.lumen-map-20261022.invalid", "exact mode preserves a nonexistent unlisted subdomain", false);
    fetch_case(&server, "evillumen-map-20261022.invalid", "an unrelated suffix without a dot boundary is never mapped", false);
    net_shutdown(); server_finish(&server);
    if (old_map) setenv("LUMEN_HOST_MAP", old_map, 1); else unsetenv("LUMEN_HOST_MAP");
    if (old_mode) setenv("LUMEN_HOST_MAP_MODE", old_mode, 1); else unsetenv("LUMEN_HOST_MAP_MODE");
    free(old_map); free(old_mode); alarm(0);
    printf("host-map tests: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
