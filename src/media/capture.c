/* Camera + microphone capture (SDL3: V4L2/PipeWire on Linux, AVFoundation on macOS) and per-origin permission prompts. */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../paint/cvbuf.h"

typedef struct { SDL_Camera *cam; Node *node; Image img[2]; int cur, w, h, fps; SDL_Thread *th; SDL_AtomicInt run; } Cam;
#define NCAM 8
static Cam g_cam[NCAM + 1];
static SDL_AudioStream *g_mic[NCAM + 1];

static bool cap_init(Uint32 f) { return (SDL_WasInit(f) & f) == f || SDL_InitSubSystem(f); }
int cap_cameras(char (*names)[128], int max) {
    if (!cap_init(SDL_INIT_CAMERA)) return 0;
    int n = 0; SDL_CameraID *ids = SDL_GetCameras(&n); if (!ids) return 0;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) { const char *s = SDL_GetCameraName(ids[i]); snprintf(names[i], 128, "%s", s ? s : "Camera"); }
    SDL_free(ids); return n;
}
static int cam_thread(void *p) {
    Cam *c = p;
    while (SDL_GetAtomicInt(&c->run)) {
        Uint64 ts; SDL_Surface *f = SDL_AcquireCameraFrame(c->cam, &ts);
        if (!f) { SDL_Delay(4); continue; }
        SDL_Surface *s = SDL_ConvertSurface(f, SDL_PIXELFORMAT_ARGB8888);
        SDL_ReleaseCameraFrame(c->cam, f);
        if (!s) continue;
        int b = c->cur ^ 1; Image *im = &c->img[b];
        if (im->w != s->w || im->h != s->h) { free(im->px); im->px = malloc((size_t)s->w * s->h * 4); im->w = s->w; im->h = s->h; im->refs = 1; im->scale = 1; }
        for (int y = 0; y < s->h; y++) { uint32_t *d = im->px + (size_t)y * s->w; const uint32_t *r = (const uint32_t *)((const uint8_t *)s->pixels + (size_t)y * s->pitch); for (int x = 0; x < s->w; x++) d[x] = r[x] | 0xff000000u; }
        c->w = s->w; c->h = s->h; SDL_DestroySurface(s);
        c->cur = b; cvbuf_changed = 1;
    }
    return 0;
}
int cap_cam_open(int idx, int w, int h, int fps) {
    if (!cap_init(SDL_INIT_CAMERA)) return 0;
    int n = 0; SDL_CameraID *ids = SDL_GetCameras(&n); if (!ids || idx < 0 || idx >= n) { SDL_free(ids); return 0; }
    SDL_CameraSpec spec = { 0 }, *want = NULL;
    if (w > 0 || h > 0 || fps > 0) { /* closest supported format */
        int nf = 0; SDL_CameraSpec **fs = SDL_GetCameraSupportedFormats(ids[idx], &nf); long best = -1;
        for (int i = 0; i < nf; i++) { long d = labs((long)fs[i]->width - (w > 0 ? w : 640)) + labs((long)fs[i]->height - (h > 0 ? h : 480));
            if (best < 0 || d < best) { best = d; spec = *fs[i]; want = &spec; } }
        SDL_free(fs);
    }
    int slot = 0; for (int i = 1; i <= NCAM; i++) if (!g_cam[i].cam) { slot = i; break; }
    if (!slot) { SDL_free(ids); return 0; }
    Cam *c = &g_cam[slot]; memset(c, 0, sizeof *c);
    c->cam = SDL_OpenCamera(ids[idx], want); SDL_free(ids);
    if (!c->cam) { fprintf(stderr, "camera: %s\n", SDL_GetError()); return 0; }
    for (int i = 0; i < 200 && SDL_GetCameraPermissionState(c->cam) == 0; i++) { SDL_PumpEvents(); SDL_Delay(10); }
    if (SDL_GetCameraPermissionState(c->cam) < 0) { SDL_CloseCamera(c->cam); c->cam = NULL; return -1; }
    SDL_CameraSpec cs; if (SDL_GetCameraFormat(c->cam, &cs)) { c->w = cs.width; c->h = cs.height; c->fps = cs.framerate_denominator ? cs.framerate_numerator / cs.framerate_denominator : 30; }
    SDL_SetAtomicInt(&c->run, 1); c->th = SDL_CreateThread(cam_thread, "camera", c);
    return slot;
}
void cap_cam_info(int id, int *w, int *h, int *fps) { Cam *c = id > 0 && id <= NCAM ? &g_cam[id] : NULL; *w = c ? c->w : 0; *h = c ? c->h : 0; *fps = c ? c->fps : 0; }
void cap_cam_bind(int id, Node *n) {
    for (int i = 1; i <= NCAM; i++) if (g_cam[i].node == n) g_cam[i].node = NULL;
    if (id > 0 && id <= NCAM && g_cam[id].cam) g_cam[id].node = n;
    cvbuf_changed = 1;
}
void cap_cam_close(int id) {
    if (id <= 0 || id > NCAM || !g_cam[id].cam) return; Cam *c = &g_cam[id];
    SDL_SetAtomicInt(&c->run, 0); SDL_WaitThread(c->th, NULL); SDL_CloseCamera(c->cam);
    free(c->img[0].px); free(c->img[1].px); memset(c, 0, sizeof *c); cvbuf_changed = 1;
}
Image *cap_frame_for(Node *n) {
    for (int i = 1; i <= NCAM; i++) if (g_cam[i].cam && g_cam[i].node == n && g_cam[i].img[g_cam[i].cur].px) return &g_cam[i].img[g_cam[i].cur];
    return NULL;
}
int cap_mics(char (*names)[128], int max) {
    if (!cap_init(SDL_INIT_AUDIO)) return 0;
    int n = 0; SDL_AudioDeviceID *ids = SDL_GetAudioRecordingDevices(&n); if (!ids) return 0;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) { const char *s = SDL_GetAudioDeviceName(ids[i]); snprintf(names[i], 128, "%s", s ? s : "Microphone"); }
    SDL_free(ids); return n;
}
int cap_mic_open(int rate, int ch) {
    if (!cap_init(SDL_INIT_AUDIO)) return 0;
    int slot = 0; for (int i = 1; i <= NCAM; i++) if (!g_mic[i]) { slot = i; break; }
    if (!slot) return 0;
    SDL_AudioSpec sp = { SDL_AUDIO_F32, ch > 0 ? ch : 1, rate > 0 ? rate : 48000 };
    SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &sp, NULL, NULL);
    if (!s) { fprintf(stderr, "microphone: %s\n", SDL_GetError()); return 0; }
    SDL_ResumeAudioStreamDevice(s); g_mic[slot] = s; return slot;
}
int cap_mic_read(int id, float *out, int max) { if (id <= 0 || id > NCAM || !g_mic[id]) return -1; int n = SDL_GetAudioStreamData(g_mic[id], out, max * 4); return n > 0 ? n / 4 : 0; }
void cap_mic_close(int id) { if (id > 0 && id <= NCAM && g_mic[id]) { SDL_DestroyAudioStream(g_mic[id]); g_mic[id] = NULL; } }

/* permissions: LUMEN_PERMISSIONS=grant|deny overrides; otherwise ask once per origin+kind per session */
typedef struct { char key[300]; int v; } Perm;
static Perm g_perm[128]; static int g_nperm;
int cap_perm_state(const char *kind, const char *origin) {
    const char *e = getenv("LUMEN_PERMISSIONS");
    if (e && !strcmp(e, "grant")) return 1; if (e && !strcmp(e, "deny")) return -1;
    char k[300]; snprintf(k, sizeof k, "%s|%s", kind, origin);
    for (int i = 0; i < g_nperm; i++) if (!strcmp(g_perm[i].key, k)) return g_perm[i].v;
    return 0;
}
int cap_ask(const char *kind, const char *origin) {
    int s = cap_perm_state(kind, origin); if (s) return s > 0;
    char msg[512]; snprintf(msg, sizeof msg, "%s wants to use your %s.", origin, kind);
    const SDL_MessageBoxButtonData bt[] = { { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Block" }, { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Allow" } };
    SDL_MessageBoxData d = { SDL_MESSAGEBOX_INFORMATION, NULL, "Permission request", msg, 2, bt, NULL };
    int r = 0; if (!SDL_ShowMessageBox(&d, &r)) r = 0;
    if (g_nperm < 128) { snprintf(g_perm[g_nperm].key, sizeof g_perm[0].key, "%s|%s", kind, origin); g_perm[g_nperm++].v = r ? 1 : -1; }
    return r == 1;
}
