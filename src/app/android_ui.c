#include "android_ui.h"
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <jni.h>
#include <stdlib.h>
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
    jint v[7] = { st->can_forward, st->desktop, st->can_desktop, st->can_zoom, st->zoom_pct, st->bookmarked, st->can_bookmark };
    jintArray a = (*env)->NewIntArray(env, 7);
    if (a) { (*env)->SetIntArrayRegion(env, a, 0, 7, v); (*env)->CallStaticVoidMethod(env, c, m, a); }
    end(env);
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
#endif
