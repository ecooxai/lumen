#ifdef __ANDROID__
#include "android_platform.h"
#include "android_launch.h"
#include "android_back.h"
#include "../net/trust.h"
#include <SDL3/SDL.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>

JNIEXPORT jboolean JNICALL Java_org_lumen_browser_LumenActivity_nativeRequestBack(
    JNIEnv *environment, jclass activity_class) {
    (void)environment;
    (void)activity_class;
    return android_back_submit() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_lumen_browser_LumenActivity_nativeOpenUrl(
    JNIEnv *environment, jclass activity_class, jbyteArray encoded) {
    (void)activity_class;
    if (!encoded) return JNI_FALSE;
    jsize length = (*environment)->GetArrayLength(environment, encoded);
    if (length <= 0 || length > 65536) return JNI_FALSE;
    char *url = malloc((size_t)length + 1);
    if (!url) return JNI_FALSE;
    (*environment)->GetByteArrayRegion(environment, encoded, 0, length, (jbyte *)url);
    if ((*environment)->ExceptionCheck(environment)) {
        free(url);
        return JNI_FALSE;
    }
    url[length] = 0;
    bool accepted = !memchr(url, 0, (size_t)length) && android_launch_submit(url);
    free(url);
    return accepted ? JNI_TRUE : JNI_FALSE;
}

/* a file another app opened or shared with Lumen, at a path Lumen can read */
JNIEXPORT jboolean JNICALL Java_org_lumen_browser_LumenActivity_nativeOpenFile(
    JNIEnv *environment, jclass activity_class, jbyteArray encoded, jboolean viewer) {
    (void)activity_class;
    if (!encoded) return JNI_FALSE;
    jsize length = (*environment)->GetArrayLength(environment, encoded);
    if (length <= 0 || length > 16384) return JNI_FALSE;
    char *path = malloc((size_t)length + 1);
    if (!path) return JNI_FALSE;
    (*environment)->GetByteArrayRegion(environment, encoded, 0, length, (jbyte *)path);
    if ((*environment)->ExceptionCheck(environment)) {
        free(path);
        return JNI_FALSE;
    }
    path[length] = 0;
    bool accepted = !memchr(path, 0, (size_t)length) && android_launch_submit_file(path, viewer);
    free(path);
    return accepted ? JNI_TRUE : JNI_FALSE;
}

void android_share_text(const char *text) {
    JNIEnv *environment = SDL_GetAndroidJNIEnv();
    if (!environment || !text) return;
    if ((*environment)->PushLocalFrame(environment, 8) < 0) {
        (*environment)->ExceptionClear(environment);
        return;
    }
    jobject activity = SDL_GetAndroidActivity();
    jclass activity_class = activity ? (*environment)->GetObjectClass(environment, activity) : NULL;
    jmethodID method = activity_class ? (*environment)->GetStaticMethodID(environment, activity_class, "shareText", "([B)V") : NULL;
    jsize length = (jsize)strlen(text);
    jbyteArray bytes = method ? (*environment)->NewByteArray(environment, length) : NULL;
    if (bytes) {
        (*environment)->SetByteArrayRegion(environment, bytes, 0, length, (const jbyte *)text);
        (*environment)->CallStaticVoidMethod(environment, activity_class, method, bytes);
    }
    if ((*environment)->ExceptionCheck(environment)) (*environment)->ExceptionClear(environment);
    (*environment)->PopLocalFrame(environment, NULL);
}

static bool android_trust_roots(SSL_CTX *context) {
    JNIEnv *environment = SDL_GetAndroidJNIEnv();
    if (!environment) return false;
    if ((*environment)->PushLocalFrame(environment, 16) < 0) {
        if ((*environment)->ExceptionCheck(environment)) (*environment)->ExceptionClear(environment);
        return false;
    }
    bool loaded = false;
    SSL_CTX *staging = NULL;
    jobject activity = SDL_GetAndroidActivity();
    if (!activity) goto done;
    jclass activity_class = (*environment)->GetObjectClass(environment, activity);
    if (!activity_class) goto done;
    jmethodID method = (*environment)->GetStaticMethodID(environment, activity_class, "getTrustRoots", "()[[B");
    if (!method) goto done;
    jobjectArray certificates = (*environment)->CallStaticObjectMethod(environment, activity_class, method);
    if ((*environment)->ExceptionCheck(environment) || !certificates) goto done;
    jsize count = (*environment)->GetArrayLength(environment, certificates);
    if (count <= 0 || count > 4096) goto done;
    staging = SSL_CTX_new(TLS_client_method());
    if (!staging) goto done;
    for (jsize index = 0; index < count; index++) {
        jbyteArray certificate = (*environment)->GetObjectArrayElement(environment, certificates, index);
        if (!certificate) goto done;
        jsize length = (*environment)->GetArrayLength(environment, certificate);
        if (length <= 0 || length > 1024 * 1024) goto done;
        jbyte *encoded = (*environment)->GetByteArrayElements(environment, certificate, NULL);
        if (!encoded) goto done;
        bool accepted = net_trust_add_der(staging, (const unsigned char *)encoded, (size_t)length);
        (*environment)->ReleaseByteArrayElements(environment, certificate, encoded, JNI_ABORT);
        (*environment)->DeleteLocalRef(environment, certificate);
        if (!accepted) goto done;
    }
    X509_STORE *store = SSL_CTX_get_cert_store(staging);
    if (X509_STORE_up_ref(store) != 1) goto done;
    SSL_CTX_set_cert_store(context, store);
    loaded = true;
done:
    SSL_CTX_free(staging);
    if ((*environment)->ExceptionCheck(environment)) (*environment)->ExceptionClear(environment);
    (*environment)->PopLocalFrame(environment, NULL);
    return loaded;
}

void android_platform_init(void) {
    net_trust_roots_loader = android_trust_roots;
}
#endif
