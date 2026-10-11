/* Android platform adapter: JNI calls into LumenActivity for things SDL doesn't cover. */
#include <jni.h>
#include <string.h>
#include <SDL3/SDL.h>

static jmethodID method(JNIEnv *env, jobject act, const char *name, const char *sig) {
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, name, sig);
    (*env)->DeleteLocalRef(env, cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return NULL; }
    return m;
}

void android_publish_download(const char *path, const char *name, const char *mime) {
    JNIEnv *env = SDL_GetAndroidJNIEnv(); jobject act = SDL_GetAndroidActivity();
    if (!env || !act) return;
    jmethodID m = method(env, act, "publishDownload", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    if (m) {
        jstring a = (*env)->NewStringUTF(env, path), b = (*env)->NewStringUTF(env, name), c = (*env)->NewStringUTF(env, mime ? mime : "");
        (*env)->CallVoidMethod(env, act, m, a, b, c);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, a); (*env)->DeleteLocalRef(env, b); (*env)->DeleteLocalRef(env, c);
    }
    (*env)->DeleteLocalRef(env, act);
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../js/js.h"

static const char *perm_name(const char *what) {
    if (!strcmp(what, "geolocation")) return "android.permission.ACCESS_FINE_LOCATION";
    if (!strcmp(what, "camera")) return "android.permission.CAMERA";
    if (!strcmp(what, "microphone")) return "android.permission.RECORD_AUDIO";
    return NULL;
}

static bool has_perm(const char *perm) {
    JNIEnv *env = SDL_GetAndroidJNIEnv(); jobject act = SDL_GetAndroidActivity();
    if (!env || !act) return false;
    jmethodID m = method(env, act, "hasPermission", "(Ljava/lang/String;)Z");
    jstring s = (*env)->NewStringUTF(env, perm);
    bool r = m && (*env)->CallBooleanMethod(env, act, m, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); r = false; }
    (*env)->DeleteLocalRef(env, s); (*env)->DeleteLocalRef(env, act);
    return r;
}

typedef struct { JsCtx *c; unsigned id; } PermReq;
static void SDLCALL perm_done(void *ud, const char *perm, bool granted) {
    PermReq *r = ud; (void)perm;
    js_host_reply(r->c, r->id, granted ? "\"granted\"" : "\"denied\"");
    free(r);
}

static void reply_location(JsCtx *c, unsigned id, bool high) {
    JNIEnv *env = SDL_GetAndroidJNIEnv(); jobject act = SDL_GetAndroidActivity();
    jmethodID m = env && act ? method(env, act, "getLocation", "(Z)[D") : NULL;
    jdoubleArray arr = m ? (*env)->CallObjectMethod(env, act, m, (jboolean)high) : NULL;
    if (env && (*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); arr = NULL; }
    if (!arr || (*env)->GetArrayLength(env, arr) < 7) { js_host_reply(c, id, "null"); if (act) (*env)->DeleteLocalRef(env, act); return; }
    double v[7]; (*env)->GetDoubleArrayRegion(env, arr, 0, 7, v);
    char num[4][32];
    for (int i = 0; i < 3; i++) { if (isnan(v[3 + i])) snprintf(num[i], 32, "null"); else snprintf(num[i], 32, "%.17g", v[3 + i]); }
    char js[512];
    snprintf(js, sizeof js, "{\"latitude\":%.17g,\"longitude\":%.17g,\"accuracy\":%.17g,\"altitude\":%s,\"heading\":%s,\"speed\":%s,\"timestamp\":%.0f}",
             v[0], v[1], v[2], num[0], num[1], num[2], v[6]);
    js_host_reply(c, id, js);
    (*env)->DeleteLocalRef(env, arr); (*env)->DeleteLocalRef(env, act);
}

void android_host_request(JsCtx *c, unsigned id, const char *kind, const char *arg) {
    const char *perm = perm_name(arg);
    if (!strcmp(kind, "permission-state")) js_host_reply(c, id, !perm ? "\"granted\"" : has_perm(perm) ? "\"granted\"" : "\"prompt\"");
    else if (!strcmp(kind, "permission")) {
        if (!perm || has_perm(perm)) { js_host_reply(c, id, "\"granted\""); return; }
        PermReq *r = malloc(sizeof *r); r->c = c; r->id = id;
        if (!SDL_RequestAndroidPermission(perm, perm_done, r)) { free(r); js_host_reply(c, id, "\"denied\""); }
    } else if (!strcmp(kind, "geolocation")) reply_location(c, id, !strcmp(arg, "high"));
    else js_host_reply(c, id, "null");
}

char *android_import_content(const char *uri) {
    JNIEnv *env = SDL_GetAndroidJNIEnv(); jobject act = SDL_GetAndroidActivity();
    if (!env || !act) return NULL;
    jmethodID m = method(env, act, "importContent", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring s = (*env)->NewStringUTF(env, uri), r = m ? (*env)->CallObjectMethod(env, act, m, s) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); r = NULL; }
    char *out = NULL;
    if (r) { const char *u = (*env)->GetStringUTFChars(env, r, NULL); out = strdup(u); (*env)->ReleaseStringUTFChars(env, r, u); (*env)->DeleteLocalRef(env, r); }
    (*env)->DeleteLocalRef(env, s); (*env)->DeleteLocalRef(env, act);
    return out;
}
