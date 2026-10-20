/* Camera capture for getUserMedia video through the SDL3 camera API (Camera2 NDK on Android, V4L2 or
   PipeWire elsewhere). NV12 frames stay NV12 (the compositor converts them on the GPU); other formats
   become ARGB. Each open camera feeds a live source (media.c) that <video> elements show.
   Everything here runs on the main thread; SDL asks Android for the camera permission itself. */
#include "media/media.h"
#include "paint/paint.h"
#include "base/util.h"
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#endif
#include <stdlib.h>
#include <string.h>

#define CAM_MAX 4
static struct {
    const void *owner;
    SDL_Camera *cam;
    int src, state;          /* state: 0 asking, 1 capturing, -1 denied or failed */
    int w, h, fps, facing;   /* the opened format; facing 1 user, 2 environment */
    Uint64 last;             /* timestamp of the newest frame taken */
} g_cam[CAM_MAX];
static bool g_init_failed;
static int g_perm;   /* what the last request answered: 1 granted, -1 denied */

int cam_permission(void) {
#ifdef __ANDROID__
    /* the persistent grant survives app restarts */
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    jobject activity = env ? SDL_GetAndroidActivity() : NULL;
    if (activity) {
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID check = cls ? (*env)->GetMethodID(env, cls, "checkSelfPermission", "(Ljava/lang/String;)I") : NULL;
        jstring name = check ? (*env)->NewStringUTF(env, "android.permission.CAMERA") : NULL;
        if (name && (*env)->CallIntMethod(env, activity, check, name) == 0) g_perm = 1;
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        if (name) (*env)->DeleteLocalRef(env, name);
        if (cls) (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
    }
#endif
    return g_perm;
}

static bool cam_init(void) {
    if (SDL_WasInit(SDL_INIT_CAMERA) & SDL_INIT_CAMERA) return true;
    if (g_init_failed) return false;
    if (!SDL_InitSubSystem(SDL_INIT_CAMERA)) { SDL_Log("lumen: camera unavailable: %s", SDL_GetError()); g_init_failed = true; return false; }
    return true;
}

int cam_count(void) {
    if (!cam_init()) return 0;
    int n = 0; SDL_CameraID *ids = SDL_GetCameras(&n);
    SDL_free(ids);
    return n;
}

static int facing_of(SDL_CameraID id) {
    SDL_CameraPosition p = SDL_GetCameraPosition(id);
    return p == SDL_CAMERA_POSITION_FRONT_FACING ? 1 : p == SDL_CAMERA_POSITION_BACK_FACING ? 2 : 0;
}

/* the format closest to the wanted size and rate; formats the compositor takes directly score best */
static long spec_cost(const SDL_CameraSpec *s, int w, int h, int fps) {
    if (s->format == SDL_PIXELFORMAT_MJPG || s->width <= 0 || s->height <= 0) return -1;
    long cost = labs((long)s->width * s->height - (long)w * h) / 64 + labs((long)s->width - w) + labs((long)s->height - h);
    if (s->width > 1920 || s->height > 1920) cost += 1000000;   /* bigger frames cost every copy */
    int rate = s->framerate_denominator > 0 ? s->framerate_numerator / s->framerate_denominator : 30;
    if (rate < fps) cost += (long)(fps - rate) * 2000;
    if (s->format != SDL_PIXELFORMAT_NV12) cost += 4000;
    return cost;
}

int cam_open(const void *owner, int w, int h, int fps, int facing) {
    if (!cam_init()) return -1;
    if (w <= 0) w = 640;
    if (h <= 0) h = 480;
    if (fps <= 0) fps = 30;
    int slot = -1;
    for (int i = 0; i < CAM_MAX && slot < 0; i++) if (!g_cam[i].owner) slot = i;
    if (slot < 0) return -1;
    int n = 0; SDL_CameraID *ids = SDL_GetCameras(&n);
    if (!ids || n <= 0) { SDL_free(ids); return -1; }
    SDL_CameraID pick = ids[0];
    for (int i = 0; i < n; i++) if (facing_of(ids[i]) == (facing ? facing : 1)) { pick = ids[i]; break; }
    SDL_free(ids);
    SDL_CameraSpec spec, *want = NULL;
    int ns = 0; SDL_CameraSpec **specs = SDL_GetCameraSupportedFormats(pick, &ns);
    long best = -1;
    for (int i = 0; specs && i < ns; i++) {
        long c = spec_cost(specs[i], w, h, fps);
        if (c >= 0 && (best < 0 || c < best)) { best = c; spec = *specs[i]; want = &spec; }
    }
    SDL_free(specs);
    if (want && want->framerate_denominator > 0 && want->framerate_numerator / want->framerate_denominator > fps * 2) {
        want->framerate_numerator = fps; want->framerate_denominator = 1;
    }
    SDL_Camera *cam = SDL_OpenCamera(pick, want);
    if (!cam) { SDL_Log("lumen: camera open failed: %s", SDL_GetError()); return -1; }
    int src = live_open();
    if (src < 0) { SDL_CloseCamera(cam); return -1; }
    memset(&g_cam[slot], 0, sizeof g_cam[slot]);
    g_cam[slot].owner = owner; g_cam[slot].cam = cam; g_cam[slot].src = src;
    g_cam[slot].w = want ? want->width : w; g_cam[slot].h = want ? want->height : h;
    g_cam[slot].fps = want && want->framerate_denominator > 0 ? want->framerate_numerator / want->framerate_denominator : fps;
    g_cam[slot].facing = facing_of(pick);
    SDL_Log("lumen: camera opened %dx%d@%d facing=%d", g_cam[slot].w, g_cam[slot].h, g_cam[slot].fps, g_cam[slot].facing);
    return slot;
}

static bool cam_ok(const void *owner, int id) { return id >= 0 && id < CAM_MAX && g_cam[id].owner && g_cam[id].owner == owner; }

static void cam_update_state(int id) {
    if (g_cam[id].state) return;
    SDL_CameraPermissionState p = SDL_GetCameraPermissionState(g_cam[id].cam);
    if (p == SDL_CAMERA_PERMISSION_STATE_APPROVED) {
        g_cam[id].state = 1; g_perm = 1;
        SDL_CameraSpec s;
        if (SDL_GetCameraFormat(g_cam[id].cam, &s) && s.width > 0) { g_cam[id].w = s.width; g_cam[id].h = s.height; }
    } else if (p == SDL_CAMERA_PERMISSION_STATE_DENIED) { g_cam[id].state = -1; g_perm = -1; }
}

int cam_state(const void *owner, int id) {
    if (!cam_ok(owner, id)) return -1;
    cam_update_state(id);
    return g_cam[id].state;
}
int cam_source(const void *owner, int id) { return cam_ok(owner, id) ? g_cam[id].src : -1; }
void cam_info(const void *owner, int id, int *w, int *h, int *fps, int *facing) {
    bool ok = cam_ok(owner, id);
    *w = ok ? g_cam[id].w : 0; *h = ok ? g_cam[id].h : 0; *fps = ok ? g_cam[id].fps : 0; *facing = ok ? g_cam[id].facing : 0;
}

void cam_close(const void *owner, int id) {
    if (!cam_ok(owner, id)) return;
    SDL_CloseCamera(g_cam[id].cam);
    live_close(g_cam[id].src);
    memset(&g_cam[id], 0, sizeof g_cam[id]);
}
void cam_close_owner(const void *owner) { for (int i = 0; i < CAM_MAX; i++) if (g_cam[i].owner == owner) cam_close(owner, i); }

/* NV12 rotated clockwise by deg (90, 180 or 270): planes of w x h into a new Image */
static Image *nv12_image(const Uint8 *y, int ys, const Uint8 *uv, int uvs, int w, int h, int deg, int mat) {
    w &= ~1; h &= ~1;
    if (w <= 0 || h <= 0) return NULL;
    bool swap = deg == 90 || deg == 270;
    int ow = swap ? h : w, oh = swap ? w : h, cw = w / 2, ch = h / 2, ocw = ow / 2;
    Image *im = xcalloc(1, sizeof *im); im->w = ow; im->h = oh; im->refs = 1; im->yuv_mat = mat;
    im->yuv = xmalloc((size_t)ow * oh + (size_t)ow * (oh / 2));
    Uint8 *dy = im->yuv, *duv = im->yuv + (size_t)ow * oh;
    if (deg == 0) {
        for (int r = 0; r < h; r++) memcpy(dy + (size_t)r * ow, y + (size_t)r * ys, (size_t)w);
        for (int r = 0; r < ch; r++) memcpy(duv + (size_t)r * ow, uv + (size_t)r * uvs, (size_t)w);
        return im;
    }
    /* destination row by row: (dx, dr) comes from source (sx, sr) */
    for (int dr = 0; dr < oh; dr++) {
        Uint8 *o = dy + (size_t)dr * ow;
        if (deg == 180) { const Uint8 *s = y + (size_t)(h - 1 - dr) * ys; for (int dx = 0; dx < ow; dx++) o[dx] = s[w - 1 - dx]; }
        else if (deg == 90) for (int dx = 0; dx < ow; dx++) o[dx] = y[(size_t)(h - 1 - dx) * ys + dr];
        else for (int dx = 0; dx < ow; dx++) o[dx] = y[(size_t)dx * ys + (w - 1 - dr)];
    }
    for (int dr = 0; dr < oh / 2; dr++) {
        Uint16 *o = (Uint16 *)(duv + (size_t)dr * ow);
        if (deg == 180) { const Uint16 *s = (const Uint16 *)(uv + (size_t)(ch - 1 - dr) * uvs); for (int dx = 0; dx < ocw; dx++) o[dx] = s[cw - 1 - dx]; }
        else if (deg == 90) for (int dx = 0; dx < ocw; dx++) memcpy(&o[dx], uv + (size_t)(ch - 1 - dx) * uvs + (size_t)dr * 2, 2);
        else for (int dx = 0; dx < ocw; dx++) memcpy(&o[dx], uv + (size_t)dx * uvs + (size_t)(cw - 1 - dr) * 2, 2);
    }
    return im;
}

static Image *frame_image(SDL_Surface *f) {
    float rot = SDL_GetFloatProperty(SDL_GetSurfaceProperties(f), SDL_PROP_SURFACE_ROTATION_FLOAT, 0.0f);
    int deg = (((int)(rot + (rot < 0 ? -0.5f : 0.5f)) % 360) + 360) % 360;
    deg = deg < 45 || deg >= 315 ? 0 : deg < 135 ? 90 : deg < 225 ? 180 : 270;
    if (f->format == SDL_PIXELFORMAT_NV12 && f->pixels) {
        SDL_Colorspace cs = SDL_GetSurfaceColorspace(f);
        int mat = (SDL_ISCOLORSPACE_MATRIX_BT709(cs) ? 1 : 0) | (SDL_ISCOLORSPACE_FULL_RANGE(cs) ? 2 : 0);
        const Uint8 *y = f->pixels, *uv = y + (size_t)f->pitch * f->h;
        return nv12_image(y, f->pitch, uv, f->pitch, f->w, f->h, deg, mat);
    }
    SDL_Surface *c = SDL_ConvertSurface(f, SDL_PIXELFORMAT_ARGB8888);
    if (!c) return NULL;
    SDL_Surface *r = deg ? SDL_RotateSurface(c, (float)deg) : NULL;
    SDL_Surface *s = r ? r : c;
    Image *im = xcalloc(1, sizeof *im); im->w = s->w; im->h = s->h; im->refs = 1;
    im->px = xmalloc((size_t)s->w * s->h * 4);
    for (int i = 0; i < s->h; i++) {
        uint32_t *o = im->px + (size_t)i * s->w;
        memcpy(o, (const Uint8 *)s->pixels + (size_t)i * s->pitch, (size_t)s->w * 4);
        for (int x = 0; x < s->w; x++) o[x] |= 0xff000000u;
    }
    SDL_DestroySurface(r); SDL_DestroySurface(c);
    return im;
}

void cam_tick(void) {
    for (int i = 0; i < CAM_MAX; i++) {
        if (!g_cam[i].owner) continue;
        cam_update_state(i);
        if (g_cam[i].state != 1) continue;
        /* only the newest queued frame is shown */
        SDL_Surface *keep = NULL; Uint64 ts = 0, t;
        for (SDL_Surface *f; (f = SDL_AcquireCameraFrame(g_cam[i].cam, &t)) != NULL;) {
            if (keep) SDL_ReleaseCameraFrame(g_cam[i].cam, keep);
            keep = f; ts = t;
        }
        if (!keep) continue;
        Image *im = frame_image(keep);
        SDL_ReleaseCameraFrame(g_cam[i].cam, keep);
        g_cam[i].last = ts;
        if (im) { g_cam[i].w = im->w; g_cam[i].h = im->h; live_push(g_cam[i].src, im); }
    }
}

/* frames are queued by SDL's camera thread without waking us: poll at twice the frame rate */
int cam_timeout_ms(void) {
    int to = -1;
    for (int i = 0; i < CAM_MAX; i++) if (g_cam[i].owner && g_cam[i].state >= 0) {
        int t = g_cam[i].state == 0 ? 50 : 500 / (g_cam[i].fps > 0 ? g_cam[i].fps : 30);
        if (t < 4) t = 4;
        if (to < 0 || t < to) to = t;
    }
    return to;
}
