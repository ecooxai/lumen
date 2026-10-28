/* Test-process acquisition refusal for Linux SDL3. This does not supply media,
 * grant permission, or change Web API results to success. Load only through the
 * verified runner handshake; production browser binaries remain byte-identical. */
#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static SDL_AudioDeviceID (*real_open_device)(SDL_AudioDeviceID, const SDL_AudioSpec *);
static SDL_AudioStream *(*real_open_stream)(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *);
static bool (*real_is_playback)(SDL_AudioDeviceID);
static unsigned blocked_categories;

static void guard_exit(const char *reason) {
    dprintf(STDERR_FILENO, "LUMEN_CAPTURE_GUARD_FATAL %s\n", reason);
    _exit(126);
}
static int guard_fd(const char *name) {
    const char *v = getenv(name); char *end = NULL;
    if (!v || !*v) guard_exit("missing handshake descriptor");
    errno = 0; long n = strtol(v, &end, 10);
    if (errno || !end || *end || n < 3 || n > INT_MAX) guard_exit("invalid handshake descriptor");
    return (int)n;
}
static void refuse(const char *entry, unsigned category) {
    SDL_SetError("Capture acquisition is unavailable in this conformance test environment");
    unsigned previous = __atomic_fetch_or(&blocked_categories, category, __ATOMIC_RELAXED);
    if (!(previous & category)) dprintf(STDERR_FILENO, "LUMEN_CAPTURE_GUARD_BLOCK %s\n", entry);
}
static bool playback(SDL_AudioDeviceID id) {
    if (id == SDL_AUDIO_DEVICE_DEFAULT_RECORDING) return false;
    if (id == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) return true;
    /* Unknown IDs fail closed. Classification only queries SDL metadata; it
     * cannot open a device and never substitutes a default recording device. */
    return real_is_playback && real_is_playback(id);
}
SDL_Camera *SDLCALL SDL_OpenCamera(SDL_CameraID id, const SDL_CameraSpec *spec) {
    (void)id; (void)spec; refuse("camera", 1); return NULL;
}
SDL_AudioDeviceID SDLCALL SDL_OpenAudioDevice(SDL_AudioDeviceID id, const SDL_AudioSpec *spec) {
    if (!playback(id)) { refuse("recording-device-or-unknown", 2); return 0; }
    if (!real_open_device) guard_exit("uninitialized playback entry");
    return real_open_device(id, spec);
}
SDL_AudioStream *SDLCALL SDL_OpenAudioDeviceStream(SDL_AudioDeviceID id, const SDL_AudioSpec *spec, SDL_AudioStreamCallback cb, void *ud) {
    if (!playback(id)) { refuse("recording-stream-or-unknown", 4); return NULL; }
    if (!real_open_stream) guard_exit("uninitialized playback stream entry");
    return real_open_stream(id, spec, cb, ud);
}
const char *lumen_capture_guard_version(void) { return "lumen-capture-refusal-v1"; }

__attribute__((constructor)) static void guard_start(void) {
    /* The frozen executables import these versioned SDL3 symbols dynamically.
     * Refuse startup if another preload wins resolution or SDL is unavailable. */
    Dl_info own, found;
    if (!dladdr((void *)guard_start, &own)) guard_exit("cannot identify guard library");
    const char *entries[] = { "SDL_OpenCamera", "SDL_OpenAudioDevice", "SDL_OpenAudioDeviceStream" };
    for (unsigned i = 0; i < sizeof entries / sizeof *entries; i++) {
        void *entry = dlsym(RTLD_DEFAULT, entries[i]);
        if (!entry || !dladdr(entry, &found) || found.dli_fbase != own.dli_fbase)
            guard_exit("capture symbol interposition failed");
    }
    real_open_device = dlsym(RTLD_NEXT, "SDL_OpenAudioDevice");
    real_open_stream = dlsym(RTLD_NEXT, "SDL_OpenAudioDeviceStream");
    real_is_playback = dlsym(RTLD_NEXT, "SDL_IsAudioDevicePlayback");
    if (!real_open_device || !real_open_stream || !real_is_playback) guard_exit("missing SDL3 playback symbols");
    const char *nonce = getenv("LUMEN_CAPTURE_GUARD_NONCE");
    if (!nonce || strlen(nonce) != 32 || strspn(nonce, "0123456789abcdef") != 32) guard_exit("invalid handshake nonce");
    int out = guard_fd("LUMEN_CAPTURE_GUARD_FD"), ack = guard_fd("LUMEN_CAPTURE_GUARD_ACK_FD");
    char line[128]; int n = snprintf(line, sizeof line, "LUMEN_CAPTURE_GUARD_V1 %ld %s\n", (long)getpid(), nonce);
    if (n <= 0 || (size_t)n >= sizeof line || write(out, line, (size_t)n) != n) guard_exit("handshake write failed");
    close(out);
    char byte = 0; ssize_t got;
    do got = read(ack, &byte, 1); while (got < 0 && errno == EINTR);
    close(ack);
    if (got != 1 || byte != 'A') guard_exit("runner did not acknowledge acquisition refusal");
    dprintf(STDERR_FILENO, "LUMEN_CAPTURE_GUARD_READY %ld %s\n", (long)getpid(), nonce);
}
