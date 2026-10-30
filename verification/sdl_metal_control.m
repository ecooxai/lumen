// Neutral independent SDL3/Metal presentation control. No browser engine code.
// Only this app's public window/layer metadata is logged. No input is posted.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <unistd.h>

static void emit(NSString *kind, NSDictionary *value) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingSortedKeys error:nil];
    if (data) {
        printf("LUME_SDL_%s %.*s\n", kind.UTF8String, (int)data.length, (const char *)data.bytes);
        fflush(stdout);
    }
}

static NSView *viewForLayer(NSView *view, CALayer *layer) {
    if (!view || !layer) return nil;
    if (view.layer == layer) return view;
    for (NSView *child in view.subviews) {
        NSView *found = viewForLayer(child, layer);
        if (found) return found;
    }
    return nil;
}

static NSArray *rectArray(CGRect value) {
    return @[@(value.origin.x), @(value.origin.y), @(value.size.width), @(value.size.height)];
}

static void state(SDL_Window *window, SDL_Renderer *renderer, double elapsed, NSString *phase) {
    NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    CAMetalLayer *layer = (__bridge CAMetalLayer *)SDL_GetRenderMetalLayer(renderer);
    NSView *view = viewForLayer(native.contentView, layer);
    NSRunningApplication *app = NSRunningApplication.currentApplication;
    NSString *rendererName = [NSString stringWithUTF8String:SDL_GetRendererName(renderer) ?: ""];
    NSDictionary *value = @{
        @"elapsedSeconds": @(elapsed), @"phase": phase, @"pid": @(getpid()),
        @"mainThread": @(NSThread.isMainThread), @"sdlVersion": @(SDL_GetVersion()),
        @"renderer": rendererName, @"applicationActive": @(NSApp.isActive),
        @"applicationRunning": @(NSApp.isRunning), @"applicationHidden": @(NSApp.isHidden),
        @"activationPolicy": @(NSApp.activationPolicy), @"runningApplicationActive": @(app.isActive),
        @"frontmostMatchesSelf": @(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == getpid()),
        @"nativeWindowFound": @(native != nil), @"windowVisible": @(native.isVisible),
        @"windowMiniaturized": @(native.isMiniaturized), @"windowKey": @(native.isKeyWindow),
        @"windowMain": @(native.isMainWindow), @"windowOnActiveSpace": @(native.isOnActiveSpace),
        @"windowOcclusionState": @(native.occlusionState),
        @"windowOcclusionVisible": @((native.occlusionState & NSWindowOcclusionStateVisible) != 0),
        @"windowFrame": rectArray(native.frame), @"windowBackingScale": @(native.backingScaleFactor),
        @"sdlWindowFlags": @(SDL_GetWindowFlags(window)),
        @"metalLayerFound": @(layer != nil), @"metalLayerDevicePresent": @(layer.device != nil),
        @"metalLayerHidden": @(layer.hidden), @"metalLayerOpacity": @(layer.opacity),
        @"metalLayerFrame": rectArray(layer.frame), @"metalLayerBounds": rectArray(layer.bounds),
        @"metalLayerDrawableSize": @[@(layer.drawableSize.width), @(layer.drawableSize.height)],
        @"metalLayerHasSuperlayer": @(layer.superlayer != nil),
        @"metalViewFoundInContentHierarchy": @(view != nil),
        @"metalViewWindowMatchesNative": @(view != nil && view.window == native),
        @"metalViewHiddenByAncestor": @(view != nil && view.isHiddenOrHasHiddenAncestor),
        @"inputEventsPosted": @NO, @"permissionsChanged": @NO
    };
    emit(@"STATE", value);
}

static BOOL paint(SDL_Renderer *renderer, int color, double elapsed) {
    const Uint8 colors[2][3] = {{40,183,72},{207,52,167}};
    bool set = SDL_SetRenderDrawColor(renderer, colors[color][0], colors[color][1], colors[color][2], 255);
    bool clear = set && SDL_RenderClear(renderer);
    bool present = clear && SDL_RenderPresent(renderer);
    emit(@"DRAW", @{@"elapsedSeconds": @(elapsed), @"colorIndex": @(color),
        @"expectedRGB": @[@(colors[color][0]), @(colors[color][1]), @(colors[color][2])],
        @"setColorReturnedTrue": @(set), @"clearReturnedTrue": @(clear),
        @"presentReturnedTrue": @(present), @"compositorVerified": @NO,
        @"error": present ? @"" : [NSString stringWithUTF8String:SDL_GetError()]});
    return present;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            emit(@"ERROR", @{@"stage": @"SDL_Init", @"error": [NSString stringWithUTF8String:SDL_GetError()]});
            return 2;
        }
        SDL_Window *window = SDL_CreateWindow("Lume verification: independent SDL Metal control", 1024, 724, SDL_WINDOW_HIGH_PIXEL_DENSITY);
        SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, "metal") : NULL;
        if (!window || !renderer) {
            emit(@"ERROR", @{@"stage": @"SDL_CreateWindow/Renderer", @"error": [NSString stringWithUTF8String:SDL_GetError()]});
            if (window) SDL_DestroyWindow(window);
            SDL_Quit(); return 3;
        }
        const Uint64 started = SDL_GetTicks();
        Uint64 nextReport = 0;
        int phase = -1, color = 0;
        BOOL captureInitial = NO, captureRaised = NO, captureActivated = NO;
        BOOL quit = NO;
        while (!quit && SDL_GetTicks() - started < 25000) {
            @autoreleasepool {
                Uint64 elapsed = SDL_GetTicks() - started;
                int nextPhase = elapsed < 5000 ? 0 : elapsed < 12000 ? 1 : 2;
                BOOL changed = nextPhase != phase;
                if (changed) {
                    phase = nextPhase;
                    color = phase == 1 ? 1 : 0;
                    if (phase == 1) {
                        bool accepted = SDL_RaiseWindow(window);
                        emit(@"ACTION", @{@"elapsedSeconds": @(elapsed / 1000.0),
                            @"operation": @"SDL_RaiseWindow", @"accepted": @(accepted)});
                    } else if (phase == 2) {
                        NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(
                            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
                        BOOL policy = [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                        [native makeKeyAndOrderFront:nil];
                        BOOL accepted = [NSRunningApplication.currentApplication activateWithOptions:0];
                        emit(@"ACTION", @{@"elapsedSeconds": @(elapsed / 1000.0),
                            @"operation": @"normal own-app activation and makeKeyAndOrderFront",
                            @"regularPolicyAccepted": @(policy), @"activationAccepted": @(accepted),
                            @"inputEventsPosted": @NO, @"permissionsChanged": @NO});
                    }
                    paint(renderer, color, elapsed / 1000.0);
                }
                if (elapsed >= nextReport) {
                    // A single repaint each second makes delayed exposure observable without a render spin.
                    if (!changed) paint(renderer, color, elapsed / 1000.0);
                    state(window, renderer, elapsed / 1000.0,
                          phase == 0 ? @"normal-launch" : phase == 1 ? @"sdl-raise" : @"normal-own-app-activation");
                    if (!captureInitial && elapsed >= 2000) { captureInitial = YES; puts("LUME_SDL_CAPTURE initial"); fflush(stdout); }
                    if (!captureRaised && elapsed >= 8000) { captureRaised = YES; puts("LUME_SDL_CAPTURE sdl-raised"); fflush(stdout); }
                    if (!captureActivated && elapsed >= 16000) { captureActivated = YES; puts("LUME_SDL_CAPTURE app-activated"); fflush(stdout); }
                    nextReport = elapsed + 1000;
                }
                SDL_Event event;
                if (SDL_WaitEventTimeout(&event, 100)) {
                    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit = YES;
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit = YES;
                    }
                }
            }
        }
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        emit(@"CLEANUP", @{@"rendererDestroyed": @YES, @"windowDestroyed": @YES, @"sdlQuit": @YES});
        return 0;
    }
}
