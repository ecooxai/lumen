/* Where the download manager writes: Android's shared Downloads collection (MediaStore, no
 * storage permission needed), or ~/Downloads elsewhere (written as name.part, renamed at the end). */
#include "download_store.h"
#include "../net/download.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <jni.h>

static jbyteArray jbytes(JNIEnv *env, const char *s) {
    jsize n = (jsize)strlen(s);
    jbyteArray b = (*env)->NewByteArray(env, n);
    if (b) (*env)->SetByteArrayRegion(env, b, 0, n, (const jbyte *)s);
    return b;
}
static jclass activity_class(JNIEnv *env) {
    jobject activity = SDL_GetAndroidActivity();
    if (!activity) return NULL;
    jclass c = (*env)->GetObjectClass(env, activity);
    (*env)->DeleteLocalRef(env, activity);
    return c;
}
/* LumenActivity.downloadCreate(name, mime) returns { uri, display name, fd } as UTF-8 strings */
static int store_create(const char *name, const char *mime, char *where, size_t where_n, char *final_name, size_t final_n) {
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    if (!env || (*env)->PushLocalFrame(env, 16) < 0) return -1;
    int fd = -1;
    jclass c = activity_class(env);
    jmethodID m = c ? (*env)->GetStaticMethodID(env, c, "downloadCreate", "([B[B)[Ljava/lang/String;") : NULL;
    jobjectArray r = m ? (*env)->CallStaticObjectMethod(env, c, m, jbytes(env, name), jbytes(env, mime)) : NULL;
    if (!(*env)->ExceptionCheck(env) && r && (*env)->GetArrayLength(env, r) == 3) {
        const char *v[3] = {0}; jstring s[3];
        for (int i = 0; i < 3; i++) { s[i] = (*env)->GetObjectArrayElement(env, r, i); v[i] = s[i] ? (*env)->GetStringUTFChars(env, s[i], NULL) : NULL; }
        if (v[0] && v[1] && v[2]) { snprintf(where, where_n, "%s", v[0]); snprintf(final_name, final_n, "%s", v[1]); fd = atoi(v[2]); }
        for (int i = 0; i < 3; i++) if (v[i]) (*env)->ReleaseStringUTFChars(env, s[i], v[i]);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->PopLocalFrame(env, NULL);
    return fd > 0 ? fd : -1;
}
static void call_uri(const char *method, const char *where, const char *extra, bool flag, bool with_flag) {
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    if (!env || (*env)->PushLocalFrame(env, 8) < 0) return;
    jclass c = activity_class(env);
    jmethodID m = c ? (*env)->GetStaticMethodID(env, c, method, with_flag ? "([BZ)V" : "([B[B)V") : NULL;
    if (m) {
        if (with_flag) (*env)->CallStaticVoidMethod(env, c, m, jbytes(env, where), flag ? JNI_TRUE : JNI_FALSE);
        else (*env)->CallStaticVoidMethod(env, c, m, jbytes(env, where), jbytes(env, extra ? extra : ""));
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->PopLocalFrame(env, NULL);
}
static void store_finish(const char *where, int fd, bool keep) {
    if (fd >= 0) { fsync(fd); close(fd); }
    call_uri("downloadFinish", where, NULL, keep, true);
}
static void store_open(const char *where, const char *mime) { call_uri("downloadOpen", where, mime, false, false); }
#else
static char g_dir[1024];
static int store_create(const char *name, const char *mime, char *where, size_t where_n, char *final_name, size_t final_n) {
    (void)mime;
    if (!*g_dir) return -1;
    mkdir(g_dir, 0755);
    char stem[256], ext[64] = "";
    snprintf(stem, sizeof stem, "%s", name);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem && strlen(dot) < sizeof ext) { snprintf(ext, sizeof ext, "%s", dot); *dot = 0; }
    for (int i = 0; i < 1000; i++) {
        char nm[320], path[1400], part[1410];
        if (i) snprintf(nm, sizeof nm, "%s (%d)%s", stem, i, ext); else snprintf(nm, sizeof nm, "%s%s", stem, ext);
        snprintf(path, sizeof path, "%s/%s", g_dir, nm);
        int hold = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);   /* reserves the final name */
        if (hold < 0) { if (errno == EEXIST) continue; return -1; }
        close(hold);
        snprintf(part, sizeof part, "%s.part", path);
        int fd = open(part, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { unlink(path); return -1; }
        snprintf(where, where_n, "%s", path); snprintf(final_name, final_n, "%s", nm);
        return fd;
    }
    return -1;
}
static void store_finish(const char *where, int fd, bool keep) {
    if (fd >= 0) close(fd);
    char part[1100]; snprintf(part, sizeof part, "%s.part", where);
    if (keep) rename(part, where); else { unlink(part); unlink(where); }
}
static void store_open(const char *where, const char *mime) {
    (void)mime;
    char *u = SDL_malloc(strlen(where) + 8);
    if (!u) return;
    strcpy(u, "file://"); strcat(u, where);
    SDL_OpenURL(u); SDL_free(u);
}
#endif

void download_store_init(const char *data_dir) {
#ifndef __ANDROID__
    const char *xdg = getenv("XDG_DOWNLOAD_DIR"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(g_dir, sizeof g_dir, "%s", xdg);
    else if (home && *home) snprintf(g_dir, sizeof g_dir, "%s/Downloads", home);
#endif
    static const DlStore store = { store_create, store_finish, store_open };
    dl_set_store(&store);
    char path[1100]; snprintf(path, sizeof path, "%s/downloads.txt", data_dir ? data_dir : ".");
    dl_init(path);
}
