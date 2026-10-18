/* Loopback regressions for cancellation during synchronous and asynchronous
 * network I/O. A stalled peer is always released after the bounded assertion,
 * so old code fails without waiting for its 30-second socket timeout.
 * For a pre-API RED run only, compile this file with NET_CANCEL_BASELINE: it
 * uses the old atomic cancellation flag, not an alternate network client. */
#include "../src/net/net.h"
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(name, expr) do { checks++; if (!(expr)) { \
    printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); failures++; \
} } while (0)

static struct timespec deadline_ms(int ms) {
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ms / 1000; t.tv_nsec += (long)(ms % 1000) * 1000000;
    if (t.tv_nsec >= 1000000000) { t.tv_sec++; t.tv_nsec -= 1000000000; }
    return t;
}
static void cancel_request(NetRequest *r) {
#ifdef NET_CANCEL_BASELINE
    __atomic_store_n(&r->cancelled, true, __ATOMIC_RELAXED);
#else
    net_request_cancel(r);
#endif
}
static bool send_all(int fd, const char *s) {
    size_t left = strlen(s);
    while (left) { ssize_t n = send(fd, s, left, 0); if (n <= 0) return false; s += n; left -= (size_t)n; }
    return true;
}
static bool read_head(int fd) {
    char buf[8192]; size_t used = 0;
    while (used + 1 < sizeof buf) {
        ssize_t n = recv(fd, buf + used, sizeof buf - used - 1, 0);
        if (n <= 0) return false;
        used += (size_t)n; buf[used] = 0;
        if (strstr(buf, "\r\n\r\n")) return true;
    }
    return false;
}
static int accept_ready(int listener) {
    struct pollfd p = { listener, POLLIN, 0 };
    if (poll(&p, 1, 3000) <= 0) return -1;
    int fd = accept(listener, NULL, NULL);
    if (fd >= 0) {
        struct timeval tv = { 3, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
    return fd;
}

enum { STALL_HEAD, STALL_FIXED, STALL_CHUNK, STALL_EOF, STALL_TLS, REUSE };
typedef struct {
    int listener, port, mode, accepts;
    pthread_t thread; pthread_mutex_t mu; pthread_cond_t cv;
    bool ready, release, ok;
} Server;
static const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nOK";
static void *serve(void *arg) {
    Server *s = arg; int fd = accept_ready(s->listener), old = -1;
    bool ok = fd >= 0;
    if (ok) {
        s->accepts++;
        if (s->mode == STALL_TLS) { char hello[4096]; ok = recv(fd, hello, sizeof hello, 0) > 0; }
        else ok = read_head(fd);
    }
    if (ok && s->mode == REUSE) {
        ok = send_all(fd, reply);
        struct pollfd p[2] = { { fd, POLLIN, 0 }, { s->listener, POLLIN, 0 } };
        if (ok && poll(p, 2, 3000) > 0) {
            if (p[1].revents & POLLIN) { old = fd; fd = accept_ready(s->listener); s->accepts++; }
            ok = fd >= 0 && read_head(fd);
        } else ok = false;
    } else if (ok && s->mode == STALL_FIXED)
        ok = send_all(fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nO");
    else if (ok && s->mode == STALL_CHUNK)
        ok = send_all(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nO\r\n");
    else if (ok && s->mode == STALL_EOF)
        ok = send_all(fd, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nO");
    pthread_mutex_lock(&s->mu);
    s->ok = ok; s->ready = true; pthread_cond_broadcast(&s->cv);
    struct timespec until = deadline_ms(3000);
    while (!s->release && pthread_cond_timedwait(&s->cv, &s->mu, &until) != ETIMEDOUT) {}
    pthread_mutex_unlock(&s->mu);
    if (ok) {
        if (s->mode == STALL_HEAD || s->mode == REUSE) (void)send_all(fd, reply);
        else if (s->mode == STALL_FIXED || s->mode == STALL_EOF) (void)send_all(fd, "K");
        else if (s->mode == STALL_CHUNK) (void)send_all(fd, "1\r\nK\r\n0\r\n\r\n");
    }
    if (fd >= 0) close(fd);
    if (old >= 0) close(old);
    return NULL;
}
static bool server_start(Server *s, int mode) {
    memset(s, 0, sizeof *s); s->mode = mode;
    pthread_mutex_init(&s->mu, NULL); pthread_cond_init(&s->cv, NULL);
    s->listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s->listener < 0 || bind(s->listener, (struct sockaddr *)&a, sizeof a) || listen(s->listener, 2)) return false;
    socklen_t n = sizeof a;
    if (getsockname(s->listener, (struct sockaddr *)&a, &n)) return false;
    s->port = ntohs(a.sin_port);
    return pthread_create(&s->thread, NULL, serve, s) == 0;
}
static bool server_ready(Server *s) {
    pthread_mutex_lock(&s->mu); struct timespec until = deadline_ms(3500);
    while (!s->ready && pthread_cond_timedwait(&s->cv, &s->mu, &until) != ETIMEDOUT) {}
    bool ok = s->ready && s->ok; pthread_mutex_unlock(&s->mu); return ok;
}
static void server_release(Server *s) {
    pthread_mutex_lock(&s->mu); s->release = true; pthread_cond_broadcast(&s->cv); pthread_mutex_unlock(&s->mu);
}
static void server_finish(Server *s) {
    pthread_join(s->thread, NULL); close(s->listener);
    pthread_cond_destroy(&s->cv); pthread_mutex_destroy(&s->mu);
}
static NetRequest *request_for(Server *s) {
    char u[100]; snprintf(u, sizeof u, "%s://127.0.0.1:%d/cancel", s->mode == STALL_TLS ? "https" : "http", s->port);
    return net_request_new("GET", u);
}
typedef struct {
    NetRequest *req; NetResponse *resp;
    pthread_t thread; pthread_mutex_t mu; pthread_cond_t cv; bool done;
} Fetch;
static void *fetch_thread(void *arg) {
    Fetch *f = arg; NetResponse *r = net_fetch_sync(f->req);
    pthread_mutex_lock(&f->mu); f->resp = r; f->done = true; pthread_cond_broadcast(&f->cv); pthread_mutex_unlock(&f->mu);
    return NULL;
}
static bool fetch_start(Fetch *f, NetRequest *r) {
    memset(f, 0, sizeof *f); f->req = r;
    pthread_mutex_init(&f->mu, NULL); pthread_cond_init(&f->cv, NULL);
    return pthread_create(&f->thread, NULL, fetch_thread, f) == 0;
}
static bool fetch_done(Fetch *f, int ms) {
    pthread_mutex_lock(&f->mu); struct timespec until = deadline_ms(ms);
    while (!f->done && pthread_cond_timedwait(&f->cv, &f->mu, &until) != ETIMEDOUT) {}
    bool done = f->done; pthread_mutex_unlock(&f->mu); return done;
}
static void fetch_join(Fetch *f) {
    pthread_join(f->thread, NULL); pthread_cond_destroy(&f->cv); pthread_mutex_destroy(&f->mu);
}
static bool cancelled_response(NetResponse *r) {
    return r && r->status == 0 && r->error && !strcmp(r->error, "cancelled") && r->body_len == 0;
}
static void stalled(int mode, const char *name) {
    Server s; bool start = server_start(&s, mode); CHECK("loopback server starts", start); if (!start) return;
    NetRequest *rq = request_for(&s); Fetch f;
    if (!fetch_start(&f, rq)) { CHECK("sync fetch thread starts", false); server_release(&s); server_finish(&s); net_request_free(rq); return; }
    CHECK("request reaches the intended I/O stall", server_ready(&s));
    double begin = now_ms(); cancel_request(rq); cancel_request(rq);
    bool prompt = fetch_done(&f, 750);
    printf("CANCEL %s prompt=%d elapsed_ms=%.1f\n", name, prompt, now_ms() - begin);
    CHECK("cancellation interrupts blocked I/O within 750 ms", prompt);
    server_release(&s); fetch_join(&f); server_finish(&s);
    CHECK("cancelled request returns a network error without a partial body", cancelled_response(f.resp));
    net_response_free(f.resp); net_request_free(rq);
}
static void pooled_isolation(void) {
    Server s; bool start = server_start(&s, REUSE); CHECK("reuse server starts", start); if (!start) return;
    NetRequest *a = request_for(&s); NetResponse *ar = net_fetch_sync(a);
    CHECK("first request completes before its connection is reused", ar && ar->status == 200 && ar->body_len == 2);
    NetRequest *b = request_for(&s); Fetch f;
    if (!fetch_start(&f, b)) { CHECK("second sync fetch starts", false); server_release(&s); server_finish(&s); net_response_free(ar); net_request_free(a); net_request_free(b); return; }
    CHECK("second request reaches the same peer", server_ready(&s));
    cancel_request(a); net_request_free(a); net_response_free(ar);
    CHECK("cancelling a completed request does not interrupt its next connection owner", !fetch_done(&f, 150));
    server_release(&s); fetch_join(&f); server_finish(&s);
    CHECK("the connection really was reused", s.accepts == 1);
    CHECK("the new owner receives its complete response", f.resp && f.resp->status == 200 && !f.resp->error && f.resp->body_len == 2 && !memcmp(f.resp->body, "OK", 2));
    net_response_free(f.resp); net_request_free(b);
}
static void async_done(NetRequest *rq, NetResponse *r, void *ud) { (void)rq; (void)r; (*(int *)ud)++; }
static bool drain(int ms) {
    double end = now_ms() + ms;
    do {
        net_poll(); if (!net_pending()) return true;
        struct timespec wait = { 0, 10000000 }; nanosleep(&wait, NULL);
    } while (now_ms() < end);
    return false;
}
static void async_cancel(void) {
    Server s; bool start = server_start(&s, STALL_HEAD); CHECK("async server starts", start); if (!start) return;
    int callbacks = 0; NetRequest *rq = request_for(&s); rq->done = async_done; rq->ud = &callbacks;
    uint64_t id = net_fetch(rq);
    CHECK("async request reaches the peer", server_ready(&s));
    net_cancel(id);
    CHECK("net_cancel releases blocked async I/O promptly", drain(750));
    CHECK("cancelled callback stays suppressed", callbacks == 0);
    server_release(&s); server_finish(&s);
    CHECK("all cancelled async events are reclaimed", drain(1500));
}
int main(void) {
    alarm(25); net_init(2);
    NetRequest *rq = net_request_new("GET", "data:text/plain,never-loaded"); cancel_request(rq);
    NetResponse *r = net_fetch_sync(rq);
    CHECK("a request cancelled before starting does not load data URLs", cancelled_response(r));
    net_response_free(r); net_request_free(rq);
    stalled(STALL_HEAD, "response-head"); stalled(STALL_FIXED, "fixed-body");
    stalled(STALL_CHUNK, "chunked-body"); stalled(STALL_EOF, "close-delimited-body");
    stalled(STALL_TLS, "TLS-handshake"); pooled_isolation(); async_cancel();
    net_shutdown(); alarm(0);
    printf("net cancellation tests: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
