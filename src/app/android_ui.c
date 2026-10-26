#include "android_ui.h"
#ifdef __ANDROID__
#include "../net/net.h"
#include "../js/js.h"
#include <SDL3/SDL.h>
#include <jni.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void (*android_ui_action)(int code, int value, char *s1, char *s2);

JNIEXPORT void JNICALL Java_org_lumen_browser_LumenActivity_nativeUiAction(
    JNIEnv *env, jclass activity_class, jint code, jint value, jbyteArray a, jbyteArray b) {
    (void)activity_class;
    char *s[2] = { NULL, NULL }; jbyteArray in[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        if (!in[i]) continue;
        jsize n = (*env)->GetArrayLength(env, in[i]);
        if (n < 0 || n > (1 << 20) || !(s[i] = malloc((size_t)n + 1))) continue;
        (*env)->GetByteArrayRegion(env, in[i], 0, n, (jbyte *)s[i]);
        s[i][n] = 0;
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (android_ui_action) android_ui_action(code, value, s[0], s[1]);
    else { free(s[0]); free(s[1]); }
}

JNIEXPORT jstring JNICALL Java_org_lumen_browser_LumenActivity_nativeUiIcon(JNIEnv *env, jclass activity_class, jint code) {
    (void)activity_class;
    const char *p = android_ui_icon(code);
    return p ? (*env)->NewStringUTF(env, p) : NULL;
}

/* site info: how many cookies the site of a URL has (on the UI thread; the jar has its own lock) */
JNIEXPORT jint JNICALL Java_org_lumen_browser_LumenActivity_nativeSiteCookies(JNIEnv *env, jclass activity_class, jbyteArray url) {
    (void)activity_class;
    jsize n = url ? (*env)->GetArrayLength(env, url) : 0;
    if (n <= 0 || n > 8192) return 0;
    char text[8193]; (*env)->GetByteArrayRegion(env, url, 0, n, (jbyte *)text); text[n] = 0;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    URL u; int count = 0;
    if (!url_parse(text, &u)) return 0;
    if (u.host && *u.host) { char site[256]; net_site_of(u.host, site, sizeof site); count = cookies_site(site, false); }
    url_free(&u);
    return count;
}

/* site info: the site whose data "Delete data" removes, NULL for a page without a host */
JNIEXPORT jstring JNICALL Java_org_lumen_browser_LumenActivity_nativeSite(JNIEnv *env, jclass activity_class, jbyteArray url) {
    (void)activity_class;
    jsize n = url ? (*env)->GetArrayLength(env, url) : 0;
    if (n <= 0 || n > 8192) return NULL;
    char text[8193]; (*env)->GetByteArrayRegion(env, url, 0, n, (jbyte *)text); text[n] = 0;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return NULL; }
    URL u; char site[256] = "";
    if (!url_parse(text, &u)) return NULL;
    if (u.host && *u.host) net_site_of(u.host, site, sizeof site);
    url_free(&u);
    return *site ? (*env)->NewStringUTF(env, site) : NULL;
}

volatile float android_ui_stats[4];
volatile int android_media_playing;
JNIEXPORT jboolean JNICALL Java_org_lumen_browser_LumenActivity_nativeMediaPlaying(JNIEnv *env, jclass activity_class) {
    (void)env; (void)activity_class;
    return android_media_playing ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jfloatArray JNICALL Java_org_lumen_browser_LumenActivity_nativeTabStats(JNIEnv *env, jclass activity_class) {
    (void)activity_class;
    jfloat v[4]; for (int i = 0; i < 4; i++) v[i] = android_ui_stats[i];
    jfloatArray a = (*env)->NewFloatArray(env, 4);
    if (a) (*env)->SetFloatArrayRegion(env, a, 0, 4, v);
    return a;
}

static jbyteArray jbytes(JNIEnv *env, const char *s) {
    if (!s) s = "";
    jsize n = (jsize)strlen(s);
    jbyteArray b = (*env)->NewByteArray(env, n);
    if (b) (*env)->SetByteArrayRegion(env, b, 0, n, (const jbyte *)s);
    return b;
}
static jobjectArray jbytes_array(JNIEnv *env, const char *const *v, int n) {
    jclass bc = (*env)->FindClass(env, "[B");
    jobjectArray r = bc ? (*env)->NewObjectArray(env, n, bc, NULL) : NULL;
    for (int i = 0; r && i < n; i++) { jbyteArray e = jbytes(env, v[i]); (*env)->SetObjectArrayElement(env, r, i, e); (*env)->DeleteLocalRef(env, e); }
    return r;
}
/* runs LumenActivity.<name>(sig) with the prepared arguments inside a local frame */
static JNIEnv *begin(jclass *c, const char *name, const char *sig, jmethodID *m) {
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    if (!env || (*env)->PushLocalFrame(env, 64) < 0) return NULL;
    jobject activity = SDL_GetAndroidActivity();
    *c = activity ? (*env)->GetObjectClass(env, activity) : NULL;
    *m = *c ? (*env)->GetStaticMethodID(env, *c, name, sig) : NULL;
    if (!*m) { if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env); (*env)->PopLocalFrame(env, NULL); return NULL; }
    return env;
}
static void end(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->PopLocalFrame(env, NULL);
}

void android_ui_toast(const char *text) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "showToast", "([B)V", &m);
    if (!env) return;
    (*env)->CallStaticVoidMethod(env, c, m, jbytes(env, text));
    end(env);
}
void android_ui_menu(const AndroidMenuState *st) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "showMenu", "([I)V", &m);
    if (!env) return;
    jint v[9] = { st->can_forward, st->desktop, st->can_desktop, st->can_zoom, st->zoom_pct, st->bookmarked, st->can_bookmark, st->can_back, st->fullscreen };
    jintArray a = (*env)->NewIntArray(env, 9);
    if (a) { (*env)->SetIntArrayRegion(env, a, 0, 9, v); (*env)->CallStaticVoidMethod(env, c, m, a); }
    end(env);
}
void android_ui_toolbar(const AndroidBarState *st, const char *url) {
    static AndroidBarState last; static char last_url[4096]; static bool sent;
    if (!url) url = "";
    if (sent && !memcmp(&last, st, sizeof last) && !strncmp(last_url, url, sizeof last_url - 1)) return;
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "updateToolbar", "([I[B)V", &m);
    if (!env) return;
    const AndroidMenuState *mn = &st->menu;
    /* left, top, width, height, loading, slot count, 4 x (action, enabled, lit), then showMenu's 9,
       then the CPU readout and touch feedback settings: on, colour, opacity, size, hide delay */
    jint v[33] = { st->left, st->top, st->width, st->height, st->loading, st->nslot };
    for (int i = 0; i < 4; i++) { v[6 + 3 * i] = st->slot[i]; v[7 + 3 * i] = st->slot_enabled[i]; v[8 + 3 * i] = st->slot_lit[i]; }
    jint menu[9] = { mn->can_forward, mn->desktop, mn->can_desktop, mn->can_zoom, mn->zoom_pct, mn->bookmarked, mn->can_bookmark, mn->can_back, mn->fullscreen };
    memcpy(v + 18, menu, sizeof menu);
    v[27] = st->hud; v[28] = st->touch_feedback;
    v[29] = st->touch_rgb; v[30] = st->touch_alpha; v[31] = st->touch_size; v[32] = st->touch_ms;
    jintArray a = (*env)->NewIntArray(env, 33);
    if (a) {
        (*env)->SetIntArrayRegion(env, a, 0, 33, v);
        (*env)->CallStaticVoidMethod(env, c, m, a, jbytes(env, url));
        if (!(*env)->ExceptionCheck(env)) { last = *st; snprintf(last_url, sizeof last_url, "%s", url); sent = true; }
    }
    end(env);
}
void android_ui_pick_files(const char *accept, bool multiple) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "pickFiles", "([BZ)V", &m);
    if (!env) { js_files_picked(NULL, -1); return; }
    (*env)->CallStaticVoidMethod(env, c, m, jbytes(env, accept ? accept : ""), (jboolean)multiple);
    if ((*env)->ExceptionCheck(env)) js_files_picked(NULL, -1);
    end(env);
}
/* pickFiles' answer on the UI thread: the paths, one per line; null when nothing was chosen */
JNIEXPORT void JNICALL Java_org_lumen_browser_LumenActivity_nativeFilesPicked(JNIEnv *env, jclass activity_class, jbyteArray list) {
    (void)activity_class;
    jsize n = list ? (*env)->GetArrayLength(env, list) : 0;
    char *s = n > 0 && n < (1 << 20) ? malloc((size_t)n + 1) : NULL;
    if (!s) { js_files_picked(NULL, -1); return; }
    (*env)->GetByteArrayRegion(env, list, 0, n, (jbyte *)s); s[n] = 0;
    const char *paths[64]; int k = 0;
    for (char *sv = NULL, *line = strtok_r(s, "\n", &sv); line && k < 64; line = strtok_r(NULL, "\n", &sv)) paths[k++] = line;
    js_files_picked(paths, k ? k : -1);
    free(s);
}
void android_ui_fullscreen(bool on) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "setFullscreen", "(Z)V", &m);
    if (!env) return;
    (*env)->CallStaticVoidMethod(env, c, m, (jboolean)on);
    end(env);
}
const char *android_ui_icon(int code) {
    switch (code) {
    case UI_BACK: return "M20 12 L4.5 12 M10.5 6 L4.5 12 L10.5 18";
    case UI_FORWARD: return "M4 12 L19.5 12 M13.5 6 L19.5 12 L13.5 18";
    case UI_RELOAD: return "A 12 12 7.5 -40 -280 M18.6 2.8 L18.2 7.3 L13.7 7";
    case UI_NEW_TAB: return "M12 4.5 L12 19.5 M4.5 12 L19.5 12";
    case UI_BM_TOGGLE: return "M12.0 3.6 L14.3 9.4 L20.6 9.8 L15.7 13.8 L17.3 19.9 L12.0 16.5 L6.7 19.9 L8.3 13.8 L3.4 9.8 L9.7 9.4 Z";
    case UI_BOOKMARKS: return "M6 3.5 L18 3.5 L18 20.5 L12 15.5 L6 20.5 Z";
    case UI_HISTORY: return "O 12 12 8.5 M12 7 L12 12 L15.5 14";
    case UI_DOWNLOADS: return "M12 4 L12 15.5 M7 10.5 L12 15.5 L17 10.5 M5 20 L19 20";
    case UI_WORKSPACES: return "M4 4 L10.5 4 L10.5 10.5 L4 10.5 Z M13.5 4 L20 4 L20 10.5 L13.5 10.5 Z M4 13.5 L10.5 13.5 L10.5 20 L4 20 Z M13.5 13.5 L20 13.5 L20 20 L13.5 20 Z";
    case UI_DESKTOP: return "M3 5 L21 5 L21 16 L3 16 Z M12 16 L12 20 M8 20 L16 20";
    case UI_FULLSCREEN: return "M4 9 L4 4 L9 4 M15 4 L20 4 L20 9 M20 15 L20 20 L15 20 M9 20 L4 20 L4 15";
    case UI_KEYBOARD: return "M3 6 L21 6 L21 18 L3 18 Z M6.5 9.5 L7.5 9.5 M10 9.5 L11 9.5 M13 9.5 L14 9.5 M16.5 9.5 L17.5 9.5 M6.5 12.5 L7.5 12.5 M10 12.5 L11 12.5 M13 12.5 L14 12.5 M16.5 12.5 L17.5 12.5 M8 15.3 L16 15.3";
    case UI_COPY: return "M9 8 L19 8 L19 20.5 L9 20.5 Z M6 16.5 L5 16.5 L5 3.5 L15 3.5 L15 5";
    case UI_PASTE: return "M6 5 L18 5 L18 21 L6 21 Z M9 3 L15 3 L15 7 L9 7 Z";
    case UI_UNDO: return "M9 5 L4 10 L9 15 M4 10 L14 10 A 14 15 5 -90 180 M14 20 L9 20";
    case UI_REDO: return "M15 5 L20 10 L15 15 M20 10 L10 10 A 10 15 5 -90 -180 M10 20 L15 20";
    case UI_EDIT_MORE: return "O 5 12 1.2 O 12 12 1.2 O 19 12 1.2";
    case UI_ZOOM: return "O 10.5 10.5 6.5 M15.2 15.2 L20 20 M7.5 10.5 L13.5 10.5 M10.5 7.5 L10.5 13.5";
    case UI_SETTINGS: return "O 12 12 3 O 12 12 6.5 M18.0 14.5 L20.5 15.5 M14.5 18.0 L15.5 20.5 M9.5 18.0 L8.5 20.5 M6.0 14.5 L3.5 15.5 "
                             "M6.0 9.5 L3.5 8.5 M9.5 6.0 L8.5 3.5 M14.5 6.0 L15.5 3.5 M18.0 9.5 L20.5 8.5";
    default: return NULL;
    }
}
void android_ui_bookmarks(const char *const *titles, const char *const *urls, int n, const char *page_title, const char *page_url) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "showBookmarks", "([[B[[B[B[B)V", &m);
    if (!env) return;
    jobjectArray t = jbytes_array(env, titles, n), u = jbytes_array(env, urls, n);
    if (t && u) (*env)->CallStaticVoidMethod(env, c, m, t, u, page_title ? jbytes(env, page_title) : NULL, page_url ? jbytes(env, page_url) : NULL);
    end(env);
}
void android_ui_workspaces(const char *const *names, const int *tabs, int n, int current) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "showWorkspaces", "([[B[II)V", &m);
    if (!env) return;
    jobjectArray v = jbytes_array(env, names, n);
    jintArray k = (*env)->NewIntArray(env, n);
    if (v && k) { (*env)->SetIntArrayRegion(env, k, 0, n, (const jint *)tabs); (*env)->CallStaticVoidMethod(env, c, m, v, k, current); }
    end(env);
}
void android_ui_tab_menu(const char *title, const char *const *items, const bool *on, int n) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "showTabMenu", "([B[[B[Z)V", &m);
    if (!env) return;
    jbyteArray t = jbytes(env, title);
    jobjectArray v = jbytes_array(env, items, n);
    jbooleanArray k = (*env)->NewBooleanArray(env, n);
    if (t && v && k) {
        for (int i = 0; i < n; i++) { jboolean b = on[i]; (*env)->SetBooleanArrayRegion(env, k, i, 1, &b); }
        (*env)->CallStaticVoidMethod(env, c, m, t, v, k);
    }
    end(env);
}
void android_ui_list_menu(const char *title, const char *const *items, int n) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, "showListMenu", "([B[[B)V", &m);
    if (!env) return;
    jbyteArray t = jbytes(env, title); jobjectArray v = jbytes_array(env, items, n);
    if (t && v) (*env)->CallStaticVoidMethod(env, c, m, t, v);
    end(env);
}
static void path_call(const char *name, const char *path) {
    jclass c; jmethodID m; JNIEnv *env = begin(&c, name, "([B)V", &m);
    if (!env) return;
    jbyteArray b = jbytes(env, path);
    if (b) (*env)->CallStaticVoidMethod(env, c, m, b);
    end(env);
}
void android_ui_clip_image_file(const char *path) { path_call("clipImageFile", path); }
void android_ui_share_image_file(const char *path) { path_call("shareImageFile", path); }
#endif
