/* Resource lifecycle regression, intentionally including the implementation so the
 * actual SDL device's pause state can be checked without a production debug API.
 * Link like jsrun, omitting build/src/media/media.c.o. Use the dummy audio driver.
 */
#include "../src/media/media.c"
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) failures++; \
} while (0)

/* Hold a real progressive HTTP response open. The server's deadline only
 * releases broken implementations, so a RED run fails in two seconds instead
 * of waiting for the client's 30-second socket timeout. */
typedef struct {
    int listener, port;
    bool body, ready, ok, release, expired;
    SDL_Mutex *mu; SDL_Condition *cv; SDL_Thread *thread;
} HeldResponse;
static int hold_response(void *arg) {
    HeldResponse *s = arg;
    struct pollfd p = { s->listener, POLLIN, 0 };
    int fd = poll(&p, 1, 2000) > 0 ? accept(s->listener, NULL, NULL) : -1;
    bool ok = fd >= 0;
    if (ok) {
        struct timeval tv = { 2, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        char head[8192]; size_t used = 0;
        ok = false;
        while (used + 1 < sizeof head) {
            ssize_t n = recv(fd, head + used, sizeof head - used - 1, 0);
            if (n <= 0) break;
            used += (size_t)n; head[used] = 0;
            if (strstr(head, "\r\n\r\n")) { ok = true; break; }
        }
    }
    if (ok && s->body) {
        const char *reply = "HTTP/1.1 200 OK\r\nContent-Length: 4096\r\nConnection: close\r\n\r\nRIFF";
        size_t left = strlen(reply);
        while (left) {
            ssize_t n = send(fd, reply, left, 0);
            if (n <= 0) { ok = false; break; }
            reply += n; left -= (size_t)n;
        }
    }
    SDL_LockMutex(s->mu);
    s->ok = ok; s->ready = true; SDL_BroadcastCondition(s->cv);
    double until = now_ms() + 2000;
    while (!s->release) {
        double left = until - now_ms(); if (left <= 0) break;
        SDL_WaitConditionTimeout(s->cv, s->mu, (Sint32)ceil(left));
    }
    s->expired = !s->release;
    SDL_UnlockMutex(s->mu);
    if (fd >= 0) { shutdown(fd, SHUT_RDWR); close(fd); }
    return 0;
}
static bool hold_start(HeldResponse *s, bool body) {
    memset(s, 0, sizeof *s); s->body = body;
    s->mu = SDL_CreateMutex(); s->cv = SDL_CreateCondition();
    s->listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (!s->mu || !s->cv || s->listener < 0 || bind(s->listener, (struct sockaddr *)&addr, sizeof addr) || listen(s->listener, 1)) return false;
    socklen_t n = sizeof addr;
    if (getsockname(s->listener, (struct sockaddr *)&addr, &n)) return false;
    s->port = ntohs(addr.sin_port);
    s->thread = SDL_CreateThread(hold_response, "held-media-response", s);
    return s->thread != NULL;
}
static bool hold_ready(HeldResponse *s) {
    SDL_LockMutex(s->mu);
    double until = now_ms() + 3000;
    while (!s->ready) {
        double left = until - now_ms(); if (left <= 0) break;
        SDL_WaitConditionTimeout(s->cv, s->mu, (Sint32)ceil(left));
    }
    bool ok = s->ready && s->ok;
    SDL_UnlockMutex(s->mu);
    return ok;
}
static void hold_finish(HeldResponse *s) {
    if (s->mu) {
        SDL_LockMutex(s->mu); s->release = true;
        if (s->cv) SDL_BroadcastCondition(s->cv);
        SDL_UnlockMutex(s->mu);
    }
    if (s->thread) SDL_WaitThread(s->thread, NULL);
    if (s->listener >= 0) close(s->listener);
    SDL_DestroyCondition(s->cv); SDL_DestroyMutex(s->mu);
}
static void close_loading_media(bool body) {
    HeldResponse s;
    bool started = hold_start(&s, body);
    CHECK("progressive media loopback server starts", started);
    if (!started) { hold_finish(&s); return; }
    char url[128]; snprintf(url, sizeof url, "http://127.0.0.1:%d/held-media", s.port);
    MediaPlayer *m = mp_new(NULL);
    mp_open_url(m, url);
    CHECK("progressive media reaches the held response", hold_ready(&s));
    double start = now_ms(); mp_free(m); double elapsed = now_ms() - start;
    SDL_LockMutex(s.mu); bool held = s.ready && s.ok && !s.expired; SDL_UnlockMutex(s.mu);
    printf("MEDIA-CLOSE %s elapsed_ms=%.1f\n", body ? "body" : "headers", elapsed);
    CHECK("free cancels the download and joins its decoder within 750 ms", elapsed < 750);
    CHECK("free finishes before the server releases its response", held);
    CHECK("free removes the loading player", g_np == 0);
    hold_finish(&s);
}

int main(void) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "audio init failed: %s\n", SDL_GetError());
        return 1;
    }
    MediaPlayer *m = mp_new(NULL);
    /* Provide real PCM to the real SDL callback, independent of codec/network
     * timing. Half a second is enough to cover pause/resume and clock checks. */
    AChunk *c = xcalloc(1, sizeof *c);
    c->frames = OUT_RATE / 2;
    c->s = xcalloc((size_t)c->frames * 2, sizeof(float));
    m->ah = m->at = c; m->abuf = 0.5; m->expect_audio = true;
    mp_play(m);
    CHECK("audio device opens and plays", m->dev && !SDL_AudioStreamDevicePaused(m->dev));
    if (!m->dev) { mp_free(m); SDL_Quit(); return 1; }

    SDL_Delay(150);
    MpState before, paused, later, resumed;
    mp_state(m, &before);
    CHECK("PCM advances the media clock", before.time > 0.02);
    mp_pause(m);
    mp_state(m, &paused);
    CHECK("pause suspends the SDL device", SDL_AudioStreamDevicePaused(m->dev));
    SDL_Delay(150);
    mp_state(m, &later);
    CHECK("paused clock is stable", paused.paused && later.paused && fabs(later.time - paused.time) < 0.001);

    mp_play(m);
    CHECK("play resumes an existing SDL device", !SDL_AudioStreamDevicePaused(m->dev));
    SDL_Delay(100);
    mp_state(m, &resumed);
    CHECK("resumed PCM advances the clock", !resumed.paused && resumed.time > later.time + 0.02);
    mp_pause(m);
    CHECK("a repeated pause suspends the device", SDL_AudioStreamDevicePaused(m->dev));

    /* The source has already decoded all its PCM, but some is still queued in
     * the real SDL stream. Natural end must drain this tail and then stop both
     * the device callback and the native media deadline. */
    Stream exhausted = {0};
    exhausted.m = m; exhausted.progressive = exhausted.opened = exhausted.eof = exhausted.has_audio = true;
    m->st[0] = &exhausted; m->nst = 1; m->duration = 0.5;
    mp_play(m);
    double end = now_ms() + 2000;
    MpState finished;
    do {
        media_tick(); mp_state(m, &finished);
        if (finished.ended && SDL_AudioStreamDevicePaused(m->dev)) break;
        SDL_Delay(10);
    } while (now_ms() < end);
    CHECK("natural end is reported", finished.ended && finished.paused);
    CHECK("natural end drains the queued PCM", SDL_GetAudioStreamQueued(m->dev) == 0);
    CHECK("natural end suspends the SDL device", SDL_AudioStreamDevicePaused(m->dev));
    CHECK("ended media has no periodic native deadline", media_timeout_ms() == -1);
    m->nst = 0; m->st[0] = NULL;   /* the exhausted source marker is test-owned */
    mp_free(m);
    CHECK("free removes the player", g_np == 0);

    net_init(0);   /* These requests run on the media decoder, not the async pool. */
    close_loading_media(false);
    close_loading_media(true);
    /* Closing before the decoder starts must not issue a late request or
     * turn normal teardown into a media error. */
    m = mp_new(NULL); m->quit = true;
    Stream cancelled = {0}; cancelled.m = m; cancelled.progressive = true;
    cancelled.mime = "file:///lumen-test-cancelled-before-start-does-not-exist";
    dec_thread(&cancelled);
    CHECK("a decoder closed before startup does not publish an error", !m->error && !cancelled.init);
    mp_free(m);
    net_shutdown();
    SDL_Quit();
    printf("RESULTS media-lifecycle %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
