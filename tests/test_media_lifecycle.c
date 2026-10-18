/* Resource lifecycle regression, intentionally including the implementation so the
 * actual SDL device's pause state can be checked without a production debug API.
 * Link like jsrun, omitting build/src/media/media.c.o. Use the dummy audio driver.
 */
#include "../src/media/media.c"

static int failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) failures++; \
} while (0)

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
    SDL_Quit();
    printf("RESULTS media-lifecycle %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
