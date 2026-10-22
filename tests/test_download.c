#include "../src/net/download.h"
#include "../src/base/url.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
enum { BODY = 12 << 20, CHUNK = 64 << 10 };
static unsigned char body_at(long long i) { return (unsigned char)(i * 31 + i / 7); }

/* ---------- a loopback server that serves byte ranges ---------- */
static int g_listener;
static atomic_int g_active, g_max_active, g_requests, g_ranged, g_without_cookie, g_bad_referer;
static char g_referer[256];
static const char *header(const char *req, const char *name, char *out, size_t n) {
    size_t k = strlen(name);
    for (const char *p = strstr(req, "\r\n"); p; p = strstr(p + 2, "\r\n")) {
        if (!strncasecmp(p + 2, name, k) && p[2 + k] == ':') {
            const char *v = p + 3 + k; while (*v == ' ') v++;
            size_t l = strcspn(v, "\r\n"); if (l >= n) l = n - 1;
            memcpy(out, v, l); out[l] = 0; return out;
        }
    }
    return NULL;
}
static void send_all(int fd, const void *p, size_t n) {
    const char *c = p;
    while (n) { ssize_t k = send(fd, c, n, MSG_NOSIGNAL); if (k <= 0) return; c += k; n -= (size_t)k; }
}
static void *handle(void *arg) {
    int fd = (int)(intptr_t)arg;
    char req[8192]; size_t n = 0;
    while (n < sizeof req - 1) {
        ssize_t k = recv(fd, req + n, sizeof req - n - 1, 0);
        if (k <= 0) { close(fd); return NULL; }
        n += (size_t)k; req[n] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    atomic_fetch_add(&g_requests, 1);
    char v[512];
    if (!header(req, "Cookie", v, sizeof v) || !strstr(v, "sid=abc")) atomic_fetch_add(&g_without_cookie, 1);
    if (!header(req, "Referer", v, sizeof v) || strcmp(v, g_referer)) atomic_fetch_add(&g_bad_referer, 1);
    long long a = 0, b = BODY - 1; bool ranged = false;
    if (header(req, "Range", v, sizeof v) && sscanf(v, "bytes=%lld-%lld", &a, &b) >= 1) {
        ranged = true; if (b >= BODY) b = BODY - 1;
        atomic_fetch_add(&g_ranged, 1);
    }
    char head[512];
    int hn = ranged
        ? snprintf(head, sizeof head, "HTTP/1.1 206 Partial Content\r\nContent-Type: application/octet-stream\r\nContent-Range: bytes %lld-%lld/%d\r\nContent-Length: %lld\r\nETag: \"v1\"\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n", a, b, BODY, b - a + 1)
        : snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=\"report.bin\"\r\nContent-Length: %d\r\nETag: \"v1\"\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n", BODY);
    send_all(fd, head, (size_t)hn);
    int now = atomic_fetch_add(&g_active, 1) + 1, seen = atomic_load(&g_max_active);
    while (now > seen && !atomic_compare_exchange_weak(&g_max_active, &seen, now)) {}
    unsigned char chunk[CHUNK];
    for (long long pos = a; pos <= b;) {
        size_t k = (size_t)(b - pos + 1 < CHUNK ? b - pos + 1 : CHUNK);
        for (size_t i = 0; i < k; i++) chunk[i] = body_at(pos + (long long)i);
        ssize_t w = send(fd, chunk, k, MSG_NOSIGNAL);
        if (w <= 0) break;
        pos += w;
        usleep(2000);   /* slow enough that the ranges overlap in time */
    }
    atomic_fetch_sub(&g_active, 1);
    close(fd);
    return NULL;
}
static void *serve(void *arg) {
    (void)arg;
    for (;;) {
        int fd = accept(g_listener, NULL, NULL);
        if (fd < 0) return NULL;
        pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        CHECK(pthread_create(&t, &at, handle, (void *)(intptr_t)fd) == 0);
        pthread_attr_destroy(&at);
    }
}

/* ---------- a store in a temporary directory ---------- */
static char g_dir[256];
static int store_create(const char *name, const char *mime, char *where, size_t where_n, char *final_name, size_t final_n) {
    (void)mime;
    snprintf(where, where_n, "%s/%s", g_dir, name); snprintf(final_name, final_n, "%s", name);
    return open(where, O_RDWR | O_CREAT | O_TRUNC, 0644);
}
static void store_finish(const char *where, int fd, bool keep) { if (fd >= 0) close(fd); if (!keep) unlink(where); }
static void store_open(const char *where, const char *mime) { (void)where; (void)mime; }

/* the UI polls the list while workers finish (a lock-order inversion here once froze the app) */
static atomic_bool g_polling;
static atomic_int g_polls;
static void *poll_list(void *arg) {
    (void)arg;
    DlInfo l[8];
    while (atomic_load(&g_polling)) { dl_list(l, 8); dl_active(); atomic_fetch_add(&g_polls, 1); }
    return NULL;
}
static DlInfo wait_done(uint64_t id) {
    DlInfo d;
    for (int i = 0; i < 3000; i++) {
        CHECK(dl_get(id, &d));
        if (d.state != DL_RUNNING) return d;
        usleep(10000);
    }
    CHECK(!"download did not finish");
    return d;
}

int main(void) {
    alarm(60);
    /* what a navigation response becomes */
    CHECK(dl_classify(200, "text/html; charset=utf-8", NULL) == 0);
    CHECK(dl_classify(200, "text/html", "attachment; filename=a.html") == 1);
    CHECK(dl_classify(200, "application/pdf", NULL) == 1);
    CHECK(dl_classify(200, "application/zip", "inline") == 1);
    CHECK(dl_classify(200, "audio/mpeg", NULL) == 2);
    CHECK(dl_classify(200, "video/mp4", NULL) == 2);
    CHECK(dl_classify(200, "image/png", NULL) == 2);
    CHECK(dl_classify(200, "image/svg+xml", NULL) == 0);
    CHECK(dl_classify(200, "application/json", NULL) == 0);
    CHECK(dl_classify(404, "application/zip", "attachment") == 0);
    CHECK(dl_classify(204, NULL, NULL) == 0);
    CHECK(dl_classify(200, NULL, NULL) == 0);

    /* file names */
    char name[256];
    dl_file_name("attachment; filename=\"report 2024.pdf\"", NULL, "https://x.test/dl?id=1", "application/pdf", name, sizeof name);
    CHECK(!strcmp(name, "report 2024.pdf"));
    dl_file_name("attachment; filename*=UTF-8''na%C3%AFve%20file.txt", NULL, "https://x.test/a", NULL, name, sizeof name);
    CHECK(!strcmp(name, "na\xC3\xAFve file.txt"));
    dl_file_name("attachment; filename=\"../../etc/passwd\"", NULL, "https://x.test/a", NULL, name, sizeof name);
    CHECK(!strcmp(name, "passwd"));
    dl_file_name(NULL, "song", "https://x.test/a", "audio/mpeg", name, sizeof name);
    CHECK(!strcmp(name, "song.mp3"));
    dl_file_name(NULL, NULL, "https://x.test/files/My%20Doc.docx?x=1#y", NULL, name, sizeof name);
    CHECK(!strcmp(name, "My Doc.docx"));
    dl_file_name(NULL, NULL, "https://x.test/", "application/zip", name, sizeof name);
    CHECK(!strcmp(name, "download.zip"));
    dl_file_name(NULL, "a:b*c?.txt", NULL, NULL, name, sizeof name);
    CHECK(!strchr(name, ':') && !strchr(name, '*') && !strchr(name, '?'));

    snprintf(g_dir, sizeof g_dir, "/tmp/lumen-dl-XXXXXX"); CHECK(mkdtemp(g_dir));
    static const DlStore store = { store_create, store_finish, store_open };
    dl_set_store(&store);
    char list[300]; snprintf(list, sizeof list, "%s/downloads.txt", g_dir);
    dl_init(list);
    net_init(8);

    /* page bytes (blob: and data: links) */
    uint64_t id = dl_save_bytes("note", "text/plain", "hello", 5);
    DlInfo d = wait_done(id);
    CHECK(d.state == DL_DONE && !strcmp(d.name, "note.txt") && d.total == 5);
    char path[600]; snprintf(path, sizeof path, "%s/note.txt", g_dir);
    size_t n; char *got = read_file(path, &n); CHECK(got && n == 5 && !memcmp(got, "hello", 5)); free(got);

    /* a large file over several connections, each with the page's cookie and referrer */
    g_listener = socket(AF_INET, SOCK_STREAM, 0); CHECK(g_listener >= 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    CHECK(bind(g_listener, (struct sockaddr *)&addr, sizeof addr) == 0); CHECK(listen(g_listener, 16) == 0);
    socklen_t al = sizeof addr; CHECK(getsockname(g_listener, (struct sockaddr *)&addr, &al) == 0);
    pthread_t server; CHECK(pthread_create(&server, NULL, serve, NULL) == 0);
    char url[128], page[160];
    snprintf(url, sizeof url, "http://127.0.0.1:%u/files/get?id=7", ntohs(addr.sin_port));
    snprintf(page, sizeof page, "http://127.0.0.1:%u/page.html#section", ntohs(addr.sin_port));
    snprintf(g_referer, sizeof g_referer, "http://127.0.0.1:%u/page.html", ntohs(addr.sin_port));
    URL cu; CHECK(url_parse(url, &cu)); cookies_set_from_header(&cu, "sid=abc; Path=/"); url_free(&cu);
    atomic_store(&g_polling, true);
    pthread_t poller; CHECK(pthread_create(&poller, NULL, poll_list, NULL) == 0);
    id = dl_start(url, page, NULL);
    CHECK(id);
    d = wait_done(id);
    atomic_store(&g_polling, false); pthread_join(poller, NULL);
    CHECK(atomic_load(&g_polls) > 0);
    if (d.state != DL_DONE) fprintf(stderr, "state %d error %s\n", d.state, d.error);
    CHECK(d.state == DL_DONE);
    CHECK(!strcmp(d.name, "report.bin"));
    CHECK(d.total == BODY && d.done == BODY);
    snprintf(path, sizeof path, "%s/report.bin", g_dir);
    got = read_file(path, &n); CHECK(got && n == BODY);
    for (size_t i = 0; i < n; i++) if ((unsigned char)got[i] != body_at((long long)i)) { fprintf(stderr, "byte %zu differs\n", i); CHECK(0); }
    free(got);
    fprintf(stderr, "requests %d ranged %d max parallel %d\n", atomic_load(&g_requests), atomic_load(&g_ranged), atomic_load(&g_max_active));
    CHECK(atomic_load(&g_ranged) >= 2);
    CHECK(atomic_load(&g_max_active) >= 3);
    CHECK(atomic_load(&g_without_cookie) == 0);
    CHECK(atomic_load(&g_bad_referer) == 0);

    /* the finished list survives a restart */
    char *saved = read_file(list, &n); CHECK(saved && strstr(saved, "report.bin") && strstr(saved, "note.txt")); free(saved);
    /* the 12 MB report must not pile up in /tmp across runs */
    unlink(path); snprintf(path, sizeof path, "%s/note.txt", g_dir); unlink(path); unlink(list); rmdir(g_dir);

    puts("download classification, names, page bytes and parallel ranged downloads with cookies passed");
    return 0;
}
