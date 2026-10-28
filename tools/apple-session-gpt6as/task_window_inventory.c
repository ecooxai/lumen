/* Enumerate windows belonging only to one verified task process. Never request
 * screen-recording permission or enumerate other window titles in output. */
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: task-window-inventory PID\n"); return 2; }
    char *end = NULL; errno = 0; long owner = strtol(argv[1], &end, 10);
    if (errno || !end || *end || owner < 2 || owner > 2147483647L) return 2;
    bool permission = CGPreflightScreenCaptureAccess();
    printf("{\"pid\":%ld,\"screenCapturePermission\":%s,\"windows\":[", owner, permission ? "true" : "false");
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    int emitted = 0;
    if (windows) for (CFIndex i = 0; i < CFArrayGetCount(windows); i++) {
        CFDictionaryRef window = CFArrayGetValueAtIndex(windows, i);
        CFNumberRef owner_value = CFDictionaryGetValue(window, kCGWindowOwnerPID);
        int pid = 0, number = 0, layer = -1; double alpha = 0;
        if (!owner_value || !CFNumberGetValue(owner_value, kCFNumberIntType, &pid) || pid != owner) continue;
        CFNumberRef number_value = CFDictionaryGetValue(window, kCGWindowNumber);
        CFNumberRef layer_value = CFDictionaryGetValue(window, kCGWindowLayer);
        CFNumberRef alpha_value = CFDictionaryGetValue(window, kCGWindowAlpha);
        CFDictionaryRef bounds_value = CFDictionaryGetValue(window, kCGWindowBounds);
        CGRect bounds = CGRectZero;
        if (!number_value || !CFNumberGetValue(number_value, kCFNumberIntType, &number) ||
            !layer_value || !CFNumberGetValue(layer_value, kCFNumberIntType, &layer) ||
            !bounds_value || !CGRectMakeWithDictionaryRepresentation(bounds_value, &bounds)) continue;
        if (alpha_value) CFNumberGetValue(alpha_value, kCFNumberDoubleType, &alpha);
        if (layer != 0 || alpha <= 0 || bounds.size.width < 200 || bounds.size.height < 100) continue;
        printf("%s{\"id\":%d,\"layer\":%d,\"alpha\":%.6f,\"width\":%.3f,\"height\":%.3f}", emitted++ ? "," : "", number, layer, alpha, bounds.size.width, bounds.size.height);
    }
    if (windows) CFRelease(windows);
    puts("]}");
    return 0;
}
