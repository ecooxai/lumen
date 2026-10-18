/* Targeted native input for an explicitly selected Lumen test process.
 * clang -fobjc-arc -framework Cocoa -framework ApplicationServices \
 *   tools/macos_input_probe.m -o out/apple-compat/gpt-6-astra-chatgpt-cgevent-input
 * The helper never requests accessibility permission or sends Return/Enter.
 * Use LUMEN_PRE_FILE=tests/probes/chatgpt-native-input.js to observe the result.
 */
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool number(const char *text, double *out) {
    char *end; errno = 0; *out = strtod(text, &end);
    return !errno && end != text && !*end && isfinite(*out);
}
static bool window_bounds(pid_t pid, CGRect *rect, CGWindowID *wid) {
    NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    double largest = 0;
    for (NSDictionary *window in windows) {
        if ([window[(id)kCGWindowOwnerPID] intValue] != pid || [window[(id)kCGWindowLayer] intValue] != 0) continue;
        CGRect r;
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)window[(id)kCGWindowBounds], &r)) continue;
        double area = r.size.width * r.size.height;
        if (r.size.width > 0 && r.size.height > 0 && area > largest) {
            largest = area; *rect = r; *wid = [window[(id)kCGWindowNumber] unsignedIntValue];
        }
    }
    return largest > 0;
}
static bool post(pid_t pid, CGEventRef event) {
    if (!event) return false;
    CGEventPostToPid(pid, event); CFRelease(event);
    usleep(25000); /* let Cocoa/SDL process each press and release */
    return true;
}
static bool key(pid_t pid, CGEventSourceRef source, CGKeyCode code, CGEventFlags flags, const UniChar *letter) {
    for (int down = 1; down >= 0; down--) {
        CGEventRef event = CGEventCreateKeyboardEvent(source, code, down != 0);
        if (!event) return false;
        CGEventSetFlags(event, flags);
        if (letter) CGEventKeyboardSetUnicodeString(event, 1, letter);
        if (!post(pid, event)) return false;
    }
    return true;
}
static bool modifier(pid_t pid, CGEventSourceRef source, bool down) {
    CGEventRef event = CGEventCreateKeyboardEvent(source, 55, down); /* Command */
    if (!event) return false;
    CGEventSetType(event, kCGEventFlagsChanged);
    CGEventSetFlags(event, down ? kCGEventFlagMaskCommand : 0);
    return post(pid, event);
}
static bool text(pid_t pid, CGEventSourceRef source, const char *value) {
    static const CGKeyCode letters[26] = {0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    static const CGKeyCode digits[10] = {29,18,19,20,21,23,22,26,28,25};
    size_t len = strlen(value);
    if (len > 128) { fprintf(stderr, "text exceeds 128-character test limit\n"); return false; }
    /* Validate the entire input before posting any event. No control characters,
       shortcuts, line breaks, or unrecognized keyboard-layout mappings. */
    for (size_t i = 0; i < len; i++) if (!((value[i] >= 'a' && value[i] <= 'z') ||
            (value[i] >= 'A' && value[i] <= 'Z') || (value[i] >= '0' && value[i] <= '9') || value[i] == ' ')) {
        fprintf(stderr, "text accepts ASCII letters, digits, and spaces only\n"); return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i]; bool upper = c >= 'A' && c <= 'Z';
        unsigned char lower = upper ? (unsigned char)(c + 'a' - 'A') : c;
        CGKeyCode code = lower >= 'a' && lower <= 'z' ? letters[lower - 'a'] :
            lower >= '0' && lower <= '9' ? digits[lower - '0'] : 49;
        UniChar letter = c;
        if (!key(pid, source, code, upper ? kCGEventFlagMaskShift : 0, &letter)) return false;
    }
    return true;
}
int main(int argc, char **argv) { @autoreleasepool {
    bool permitted = CGPreflightPostEventAccess();
    if (argc == 2 && !strcmp(argv[1], "--check")) {
        printf("{\"post_event_access\":%s}\n", permitted ? "true" : "false"); return permitted ? 0 : 77;
    }
    if (argc < 4 || strcmp(argv[1], "--pid")) {
        fprintf(stderr, "usage: input-probe --check | --pid PID bounds|activate|click X Y|text VALUE|key left|right|home|end|backspace|delete|select-all|escape\n"); return 64;
    }
    char *end; errno = 0; long parsed = strtol(argv[2], &end, 10);
    if (errno || end == argv[2] || *end || parsed <= 0 || parsed > INT_MAX) return 64;
    pid_t pid = (pid_t)parsed;
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (!app || app.terminated || ![app.executableURL.lastPathComponent.lowercaseString isEqualToString:@"lumen"]) {
        fprintf(stderr, "target must be a running Lumen executable\n"); return 65;
    }
    const char *operation = argv[3];
    bool bounds_only = !strcmp(operation, "bounds");
    if (!bounds_only && !permitted) {
        fprintf(stderr, "CGPreflightPostEventAccess=false; no activation or events performed\n"); return 77;
    }
    if (!bounds_only) {
        [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        for (int attempt = 0; !app.active && attempt < 40; attempt++) usleep(25000);
        if (!app.active) { fprintf(stderr, "Lumen did not become active; no input posted\n"); return 75; }
    }
    CGRect bounds = CGRectZero; CGWindowID wid = 0;
    if (!window_bounds(pid, &bounds, &wid)) { fprintf(stderr, "no visible Lumen window\n"); return 75; }
    if (bounds_only || !strcmp(operation, "activate")) {
        printf("{\"pid\":%d,\"window_id\":%u,\"x\":%.3f,\"y\":%.3f,\"width\":%.3f,\"height\":%.3f,\"active\":%s}\n",
            pid, wid, bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height, app.active ? "true" : "false");
        return 0;
    }
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!source) return 70;
    bool ok = false;
    if (!strcmp(operation, "click") && argc == 6) {
        double x, y;
        if (number(argv[4], &x) && number(argv[5], &y) && CGRectContainsPoint(bounds, CGPointMake(x, y))) {
            CGPoint at = CGPointMake(x, y); ok = true;
            CGEventType types[] = { kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseUp };
            for (unsigned i = 0; i < sizeof types / sizeof *types && ok; i++) {
                CGEventRef event = CGEventCreateMouseEvent(source, types[i], at, kCGMouseButtonLeft);
                if (event) { CGEventSetFlags(event, 0); CGEventSetIntegerValueField(event, kCGMouseEventClickState, 1); }
                ok = post(pid, event);
            }
        } else fprintf(stderr, "click must be finite and inside the selected Lumen window\n");
    } else if (!strcmp(operation, "text") && argc == 5) ok = text(pid, source, argv[4]);
    else if (!strcmp(operation, "key") && argc == 5) {
        const char *name = argv[4];
        if (!strcmp(name, "select-all")) {
            bool pressed = modifier(pid, source, true);
            bool sent = pressed && key(pid, source, 0, kCGEventFlagMaskCommand, NULL);
            bool released = modifier(pid, source, false);
            ok = sent && released;
        } else {
            static const struct { const char *name; CGKeyCode code; } keys[] = {
                {"left",123}, {"right",124}, {"home",115}, {"end",119},
                {"backspace",51}, {"delete",117}, {"escape",53}
            };
            for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) if (!strcmp(name, keys[i].name)) ok = key(pid, source, keys[i].code, 0, NULL);
        }
    }
    CFRelease(source);
    if (!ok) { fprintf(stderr, "operation failed or unsupported; Enter/Return is intentionally unavailable\n"); return 64; }
    printf("{\"pid\":%d,\"operation\":\"%s\",\"posted\":true}\n", pid, operation);
    return 0;
} }
