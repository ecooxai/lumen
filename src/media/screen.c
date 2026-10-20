/* Screen capture for getDisplayMedia. Android mirrors the screen through MediaProjection
   (ScreenCapture.java): its RGBA images arrive on a Java thread, which converts them and leaves the
   newest in a mailbox; the main thread moves it to a live source (media.c), since Image reference
   counts are not atomic. With audio wanted, the projection's playback capture (Android 10+, the
   "system audio" of a desktop browser) streams mono float PCM at 48 kHz into a ring the JS thread
   drains (screen_audio_read), like a microphone. Other platforms have none yet. One capture at a
   time, like the system's.
   On Android this file is linked into libmain itself (CMakeLists.txt), so its JNI entry points stay
   visible to Java. */
#include "media/media.h"
#include "paint/paint.h"
#include "base/util.h"
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#endif
#include <string.h>

#define SCR_ACAP 131072   /* audio ring samples (2.7 s), a power of two */
/* state: 0 asking, 1 capturing, -1 refused or ended; audio: 1 while playback capture runs;
   ask_audio: the record permission is being asked before the consent dialog */
static struct {
    const void *owner; int src, maxw, maxh, fps; bool ask_audio;
    SDL_AtomicInt state, w, h, audio, aw, ar; SDL_Mutex *mu; Image *next;
    float ring[SCR_ACAP];
} g_scr;

static Image *screen_take(void) {
    if (!g_scr.mu) return NULL;
    SDL_LockMutex(g_scr.mu); Image *im = g_scr.next; g_scr.next = NULL; SDL_UnlockMutex(g_scr.mu);
    return im;
}

#ifdef __ANDROID__
static bool screen_java(const char *name, const char *sig, int a, int b, int c, int d) {
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    jobject activity = env ? SDL_GetAndroidActivity() : NULL;
    if (!activity) return false;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID m = cls ? (*env)->GetStaticMethodID(env, cls, name, sig) : NULL;
    if (m) { if (sig[1] == ')') (*env)->CallStaticVoidMethod(env, cls, m); else (*env)->CallStaticVoidMethod(env, cls, m, (jint)a, (jint)b, (jint)c, (jboolean)(d != 0)); }
    bool ok = m && !(*env)->ExceptionCheck(env);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}
static void screen_start_java(bool audio) {
    if (!screen_java("screenStart", "(IIIZ)V", g_scr.maxw, g_scr.maxh, g_scr.fps, audio)) SDL_SetAtomicInt(&g_scr.state, -1);
}

/* state: 1 capturing (width, height, audio: playback capture runs too), -1 refused or failed, 0 ended */
JNIEXPORT void JNICALL Java_org_lumen_browser_ScreenCapture_nativeState(JNIEnv *env, jclass cls, jint state, jint w, jint h, jboolean audio) {
    (void)env; (void)cls;
    if (state > 0) { SDL_SetAtomicInt(&g_scr.w, w); SDL_SetAtomicInt(&g_scr.h, h); SDL_SetAtomicInt(&g_scr.audio, audio ? 1 : 0); }
    else SDL_SetAtomicInt(&g_scr.audio, 0);
    SDL_SetAtomicInt(&g_scr.state, state > 0 ? 1 : -1);
    SDL_Log("lumen: screen capture %s%s", state > 0 ? "started" : state < 0 ? "refused" : "ended", state > 0 && audio ? " with audio" : "");
    if (media_wakeup) media_wakeup();
}

JNIEXPORT void JNICALL Java_org_lumen_browser_ScreenCapture_nativeFrame(JNIEnv *env, jclass cls, jobject buf, jint w, jint h, jint stride) {
    (void)cls;
    int src = g_scr.src;
    const uint8_t *p = (*env)->GetDirectBufferAddress(env, buf);
    if (!p || !g_scr.mu || src <= 0 || w <= 0 || h <= 0 || stride < w * 4 || SDL_GetAtomicInt(&g_scr.state) != 1) return;
    Image *im = xcalloc(1, sizeof *im); im->w = w; im->h = h; im->refs = 1;
    im->px = xmalloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {   /* RGBA bytes to 0xAARRGGBB, opaque */
        const uint32_t *s = (const uint32_t *)(p + (size_t)y * stride);
        uint32_t *o = im->px + (size_t)y * w;
        for (int x = 0; x < w; x++) { uint32_t v = s[x]; o[x] = 0xff000000u | (v & 0x0000ff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu); }
    }
    SDL_SetAtomicInt(&g_scr.w, w); SDL_SetAtomicInt(&g_scr.h, h);
    SDL_LockMutex(g_scr.mu); Image *old = g_scr.next; g_scr.next = im; SDL_UnlockMutex(g_scr.mu);
    image_unref(old);   /* never seen by anyone else */
    if (media_wakeup) media_wakeup();
}

/* the playback capture thread: n mono samples; a reader that fell behind loses the newest */
JNIEXPORT void JNICALL Java_org_lumen_browser_ScreenCapture_nativeAudio(JNIEnv *env, jclass cls, jfloatArray pcm, jint n) {
    (void)cls;
    if (n <= 0 || !SDL_GetAtomicInt(&g_scr.audio) || SDL_GetAtomicInt(&g_scr.state) != 1) return;
    float tmp[2048];
    if (n > (jint)(sizeof tmp / sizeof *tmp)) n = (jint)(sizeof tmp / sizeof *tmp);
    (*env)->GetFloatArrayRegion(env, pcm, 0, n, tmp);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }
    unsigned w = (unsigned)SDL_GetAtomicInt(&g_scr.aw), r = (unsigned)SDL_GetAtomicInt(&g_scr.ar), room = SCR_ACAP - (w - r);
    if ((unsigned)n > room) n = (jint)room;
    for (jint i = 0; i < n; i++) g_scr.ring[(w + i) & (SCR_ACAP - 1)] = tmp[i];
    SDL_MemoryBarrierRelease();
    SDL_SetAtomicInt(&g_scr.aw, (int)(w + n));
}
#endif

bool screen_supported(void) {
#ifdef __ANDROID__
    return true;
#else
    return false;
#endif
}

int screen_open(const void *owner, int maxw, int maxh, int fps, bool audio) {
#ifdef __ANDROID__
    if (g_scr.owner) return -1;
    if (!g_scr.mu && !(g_scr.mu = SDL_CreateMutex())) return -1;
    image_unref(screen_take());
    int src = live_open();
    if (src < 0) return -1;
    g_scr.owner = owner; g_scr.src = src;
    g_scr.maxw = maxw > 0 ? maxw : 1920; g_scr.maxh = maxh > 0 ? maxh : 1920; g_scr.fps = fps > 0 ? fps : 30;
    SDL_SetAtomicInt(&g_scr.state, 0); SDL_SetAtomicInt(&g_scr.w, 0); SDL_SetAtomicInt(&g_scr.h, 0);
    SDL_SetAtomicInt(&g_scr.audio, 0); SDL_SetAtomicInt(&g_scr.ar, SDL_GetAtomicInt(&g_scr.aw));
    /* playback capture records audio: the permission comes first, then the consent dialog (screen_tick) */
    g_scr.ask_audio = audio && mic_permission() != 1 && mic_permission_request();
    if (!g_scr.ask_audio) screen_start_java(audio && mic_permission() == 1);
    return 0;
#else
    (void)owner; (void)maxw; (void)maxh; (void)fps; (void)audio;
    return -1;
#endif
}

static bool screen_ok(const void *owner, int id) { return id == 0 && owner && g_scr.owner == owner; }
int screen_state(const void *owner, int id) { return screen_ok(owner, id) ? SDL_GetAtomicInt(&g_scr.state) : -1; }
int screen_source(const void *owner, int id) { return screen_ok(owner, id) ? g_scr.src : -1; }
void screen_info(const void *owner, int id, int *w, int *h, bool *audio) {
    bool ok = screen_ok(owner, id);
    *w = ok ? SDL_GetAtomicInt(&g_scr.w) : 0; *h = ok ? SDL_GetAtomicInt(&g_scr.h) : 0;
    *audio = ok && SDL_GetAtomicInt(&g_scr.audio);
}
int screen_audio_available(const void *owner, int id) {
    if (!screen_ok(owner, id)) return 0;
    return (int)((unsigned)SDL_GetAtomicInt(&g_scr.aw) - (unsigned)SDL_GetAtomicInt(&g_scr.ar));
}
int screen_audio_read(const void *owner, int id, float *out, int max) {
    if (!screen_ok(owner, id) || max <= 0) return 0;
    unsigned r = (unsigned)SDL_GetAtomicInt(&g_scr.ar), w = (unsigned)SDL_GetAtomicInt(&g_scr.aw);
    SDL_MemoryBarrierAcquire();
    int n = (int)(w - r) < max ? (int)(w - r) : max;
    for (int i = 0; i < n; i++) out[i] = g_scr.ring[(r + i) & (SCR_ACAP - 1)];
    SDL_SetAtomicInt(&g_scr.ar, (int)(r + n));
    return n;
}
void screen_close(const void *owner, int id) {
    if (!screen_ok(owner, id)) return;
    int src = g_scr.src;
    bool asked = !g_scr.ask_audio;
    SDL_SetAtomicInt(&g_scr.state, -1); SDL_SetAtomicInt(&g_scr.audio, 0);
#ifdef __ANDROID__
    if (asked) screen_java("screenStop", "()V", 0, 0, 0, 0);
#else
    (void)asked;
#endif
    g_scr.src = 0; g_scr.owner = NULL; g_scr.ask_audio = false;
    image_unref(screen_take());
    live_close(src);
}
/* main thread, from media_tick */
void screen_tick(void) {
    if (!g_scr.owner) return;
#ifdef __ANDROID__
    if (g_scr.ask_audio) {
        int p = mic_permission();
        if (p) { g_scr.ask_audio = false; screen_start_java(p > 0); }   /* refused: the screen without its audio */
    }
#endif
    Image *im = screen_take();
    if (im && g_scr.src > 0 && SDL_GetAtomicInt(&g_scr.state) == 1) live_push(g_scr.src, im);
    else image_unref(im);
}
bool screen_waiting(void) { return g_scr.owner && g_scr.ask_audio; }
void screen_close_owner(const void *owner) { if (owner && g_scr.owner == owner) screen_close(owner, 0); }
