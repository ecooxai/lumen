#include <jni.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

int font_smoke_run(void);

JNIEXPORT jint JNICALL Java_org_lumen_codex_fonttest_FontTestActivity_runTests(
    JNIEnv *environment, jclass activity, jstring report_path) {
    (void)activity;
    const char *path = (*environment)->GetStringUTFChars(environment, report_path, NULL);
    if (!path) return 2;
    FILE *output = freopen(path, "w", stdout);
    bool errors = output && dup2(fileno(stdout), STDERR_FILENO) >= 0;
    (*environment)->ReleaseStringUTFChars(environment, report_path, path);
    if (!output || !errors) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    int result = font_smoke_run();
    printf("FONT_TEST_EXIT=%d\n", result);
    return result;
}
