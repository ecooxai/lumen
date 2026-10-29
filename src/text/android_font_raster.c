#include "android_font_raster.h"
#ifdef __ANDROID__
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t raster_mutex = PTHREAD_MUTEX_INITIALIZER;
static JavaVM *raster_vm;
static jclass raster_class;
static jmethodID raster_method;

JNIEXPORT jboolean JNICALL Java_org_lumen_browser_AndroidFontRaster_nativeInitialize(
    JNIEnv *environment, jclass renderer) {
    pthread_mutex_lock(&raster_mutex);
    if (!raster_vm) {
        JavaVM *vm = NULL;
        if ((*environment)->GetJavaVM(environment, &vm) == JNI_OK) {
            jmethodID method = (*environment)->GetStaticMethodID(environment, renderer, "raster", "(Ljava/lang/String;I[I[FIF)[I");
            jclass reference = method ? (*environment)->NewGlobalRef(environment, renderer) : NULL;
            if (reference) {
                raster_class = reference;
                raster_method = method;
                raster_vm = vm;
            }
        }
        if ((*environment)->ExceptionCheck(environment)) (*environment)->ExceptionClear(environment);
    }
    bool ready = raster_vm != NULL;
    pthread_mutex_unlock(&raster_mutex);
    return ready ? JNI_TRUE : JNI_FALSE;
}

bool android_font_raster(const AndroidFontMatch *match, uint32_t glyph_id, float pixels, Glyph *result) {
    if (!match || !result || !glyph_id || glyph_id > 65535 || !isfinite(pixels) || pixels <= 0 || pixels > 2048
        || match->collection_index < 0 || match->axis_count > ANDROID_FONT_MAX_AXES
        || match->path[0] != '/' || !memchr(match->path, 0, sizeof match->path)) return false;
    jint tags[ANDROID_FONT_MAX_AXES];
    jfloat values[ANDROID_FONT_MAX_AXES];
    for (unsigned axis = 0; axis < match->axis_count; axis++) {
        if (!match->axes[axis].tag || !isfinite(match->axes[axis].value)) return false;
        for (unsigned previous = 0; previous < axis; previous++) {
            if (match->axes[previous].tag == match->axes[axis].tag) return false;
        }
        tags[axis] = (jint)match->axes[axis].tag;
        values[axis] = match->axes[axis].value;
    }
    pthread_mutex_lock(&raster_mutex);
    JavaVM *vm = raster_vm;
    jclass renderer = raster_class;
    jmethodID method = raster_method;
    pthread_mutex_unlock(&raster_mutex);
    if (!vm) return false;
    JNIEnv *environment = NULL;
    bool attached = false;
    jint status = (*vm)->GetEnv(vm, (void **)&environment, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &environment, NULL) != JNI_OK) return false;
        attached = true;
    } else if (status != JNI_OK) return false;
    if ((*environment)->ExceptionCheck(environment)) {
        if (attached) (*vm)->DetachCurrentThread(vm);
        return false;
    }
    if ((*environment)->PushLocalFrame(environment, 8) < 0) {
        if ((*environment)->ExceptionCheck(environment)) (*environment)->ExceptionClear(environment);
        if (attached) (*vm)->DetachCurrentThread(vm);
        return false;
    }
    bool rendered = false;
    uint32_t *buffer = NULL;
    jstring path = (*environment)->NewStringUTF(environment, match->path);
    if (!path) goto done;
    jintArray axis_tags = (*environment)->NewIntArray(environment, (jsize)match->axis_count);
    if (!axis_tags) goto done;
    jfloatArray axis_values = (*environment)->NewFloatArray(environment, (jsize)match->axis_count);
    if (!axis_values) goto done;
    (*environment)->SetIntArrayRegion(environment, axis_tags, 0, (jsize)match->axis_count, tags);
    if ((*environment)->ExceptionCheck(environment)) goto done;
    (*environment)->SetFloatArrayRegion(environment, axis_values, 0, (jsize)match->axis_count, values);
    if ((*environment)->ExceptionCheck(environment)) goto done;
    jintArray packed = (*environment)->CallStaticObjectMethod(environment, renderer, method,
        path, (jint)match->collection_index, axis_tags, axis_values, (jint)glyph_id, (jfloat)pixels);
    if ((*environment)->ExceptionCheck(environment) || !packed) goto done;
    jsize length = (*environment)->GetArrayLength(environment, packed);
    if (length < 5 || length > 1048580) goto done;
    jint header[4];
    (*environment)->GetIntArrayRegion(environment, packed, 0, 4, header);
    if ((*environment)->ExceptionCheck(environment)) goto done;
    if (header[0] <= 0 || header[0] > 2048 || header[1] <= 0 || header[1] > 2048
        || header[2] < -4096 || header[2] > 4096 || header[3] < -4096 || header[3] > 4096) goto done;
    size_t count = (size_t)header[0] * (size_t)header[1];
    if (count > 1048576 || count != (size_t)length - 4) goto done;
    buffer = malloc(count * sizeof *buffer);
    if (!buffer) goto done;
    (*environment)->GetIntArrayRegion(environment, packed, 4, (jsize)count, (jint *)buffer);
    if ((*environment)->ExceptionCheck(environment)) goto done;
    bool visible = false;
    for (size_t index = 0; index < count; index++) {
        uint32_t pixel = buffer[index], alpha = pixel >> 24;
        uint32_t red = (((pixel >> 16) & 255) * alpha + 127) / 255;
        uint32_t green = (((pixel >> 8) & 255) * alpha + 127) / 255;
        uint32_t blue = ((pixel & 255) * alpha + 127) / 255;
        buffer[index] = (alpha << 24) | (red << 16) | (green << 8) | blue;
        visible |= alpha != 0;
    }
    if (!visible) goto done;
    result->w = header[0];
    result->h = header[1];
    result->left = header[2];
    result->top = header[3];
    result->color = true;
    result->rgba = buffer;
    result->alpha = NULL;
    buffer = NULL;
    rendered = true;
done:
    free(buffer);
    if ((*environment)->ExceptionCheck(environment)) (*environment)->ExceptionClear(environment);
    (*environment)->PopLocalFrame(environment, NULL);
    if (attached) (*vm)->DetachCurrentThread(vm);
    return rendered;
}
#else
bool android_font_raster(const AndroidFontMatch *match, uint32_t glyph_id, float pixels, Glyph *result) {
    (void)match;
    (void)glyph_id;
    (void)pixels;
    (void)result;
    return false;
}
#endif
