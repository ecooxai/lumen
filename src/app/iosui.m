/* UIKit implementation of the macui.h platform hooks for iOS / iPadOS (and the same build on Apple Silicon Macs). */
#import <UIKit/UIKit.h>
#include "macui.h"

static Uint32 g_menu_ev;
static UIViewController *root_vc(SDL_Window *w) {
    UIWindow *win = w ? (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL) : nil;
    UIViewController *vc = win.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}
static SDL_Window *g_win;

/* Runs the UIKit run loop until the presented controller reports a result (SDL's loop is on this thread). */
static int run_modal(UIViewController *vc, UIAlertController *ac, float x, float y, volatile int *done) {
    if (!vc) return 0;
    UIPopoverPresentationController *pop = ac.popoverPresentationController;
    if (pop) { pop.sourceView = vc.view; pop.sourceRect = CGRectMake(x, y, 1, 1); }
    [vc presentViewController:ac animated:YES completion:nil];
    while (!*done) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    return *done;
}

void mac_style_window(SDL_Window *w) { g_win = w; }
void mac_install_menu(Uint32 ev) { g_menu_ev = ev; }

bool mac_prompt(const char *title, const char *init, char *out, size_t n) {
    UIViewController *vc = root_vc(g_win);
    __block volatile int done = 0; __block NSString *val = nil;
    UIAlertController *ac = [UIAlertController alertControllerWithTitle:[NSString stringWithUTF8String:title] message:nil preferredStyle:UIAlertControllerStyleAlert];
    [ac addTextFieldWithConfigurationHandler:^(UITextField *tf) { tf.text = [NSString stringWithUTF8String:init]; tf.clearButtonMode = UITextFieldViewModeWhileEditing; }];
    UIAlertController *__weak wac = ac;
    [ac addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction *a) { (void)a; done = 2; }]];
    [ac addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) { (void)a; val = wac.textFields.firstObject.text; done = 1; }]];
    if (run_modal(vc, ac, 0, 0, &done) != 1 || !val) return false;
    snprintf(out, n, "%s", val.UTF8String);
    return out[0] != 0;
}

static const char *icon_names[MAC_NICONS] = { "briefcase.fill", "gamecontroller.fill", "house.fill", "star.fill", "heart.fill", "book.fill",
    "chevron.left.forwardslash.chevron.right", "music.note", "cart.fill", "globe", "graduationcap.fill", "airplane", "camera.fill", "bolt.fill", "leaf.fill", "paintbrush.fill" };
static NSString *icon_titles[MAC_NICONS] = { @"Work", @"Game", @"Home", @"Star", @"Heart", @"Reading", @"Code", @"Music", @"Shopping", @"Web",
    @"School", @"Travel", @"Photos", @"Energy", @"Nature", @"Art" };

typedef struct { NSString *title; int tag; bool on; UIImage *img; } SheetItem;
static int sheet(SDL_Window *w, float x, float y, NSString *title, const SheetItem *it, int n) {
    UIViewController *vc = root_vc(w);
    __block volatile int done = 0; __block int pick = 0;
    UIAlertController *ac = [UIAlertController alertControllerWithTitle:title message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    for (int i = 0; i < n; i++) {
        int tag = it[i].tag;
        UIAlertAction *a = [UIAlertAction actionWithTitle:it[i].on ? [@"\u2713 " stringByAppendingString:it[i].title] : it[i].title
                                                    style:UIAlertActionStyleDefault handler:^(UIAlertAction *x_) { (void)x_; pick = tag; done = 1; }];
        if (it[i].img) [a setValue:it[i].img forKey:@"image"];
        [ac addAction:a];
    }
    [ac addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction *x_) { (void)x_; done = 2; }]];
    run_modal(vc, ac, x, y, &done);
    return pick;
}

static UIImage *icon_image(int i) { return [UIImage systemImageNamed:[NSString stringWithUTF8String:icon_names[i]]]; }

int mac_ws_menu(SDL_Window *w, float x, float y) {
    enum { CHANGE_ICON = -5 };
    SheetItem m[] = { { @"Rename…", MENU_WS_RENAME, false, nil }, { @"Change Icon…", CHANGE_ICON, false, nil },
        { @"Refresh All Tabs", MENU_WS_REFRESH, false, nil }, { @"New Workspace", MENU_WS_NEW, false, nil },
        { @"Show/Hide Workspace Bar", MENU_WS_SIDEBAR, false, nil }, { @"Close Workspace", MENU_WS_CLOSE, false, nil } };
    int c = sheet(w, x, y, @"Workspace", m, (int)(sizeof m / sizeof *m));
    if (c != CHANGE_ICON) return c;
    SheetItem ic[MAC_NICONS + 1];
    for (int i = 0; i < MAC_NICONS; i++) ic[i] = (SheetItem){ icon_titles[i], MENU_WS_ICON + i, false, icon_image(i) };
    ic[MAC_NICONS] = (SheetItem){ @"Letters", MENU_WS_ICON + MAC_NICONS, false, nil };
    return sheet(w, x, y, @"Change Icon", ic, MAC_NICONS + 1);
}

static bool draw_rgba(UIImage *im, int px, uint32_t rgb, uint32_t *out) {
    if (!im) return false;
    UIColor *col = [UIColor colorWithRed:((rgb >> 16) & 255) / 255.0 green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:1];
    im = [im imageWithTintColor:col renderingMode:UIImageRenderingModeAlwaysOriginal];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(out, (size_t)px, (size_t)px, 8, (size_t)px * 4, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (!ctx) return false;
    CGContextTranslateCTM(ctx, 0, px); CGContextScaleCTM(ctx, 1, -1);   /* UIKit drawing is top-left origin */
    UIGraphicsPushContext(ctx);
    CGSize sz = im.size; CGFloat sc = MIN(px / sz.width, px / sz.height);
    [im drawInRect:CGRectMake((px - sz.width * sc) / 2, (px - sz.height * sc) / 2, sz.width * sc, sz.height * sc)];
    UIGraphicsPopContext();
    CGContextRelease(ctx);
    return true;
}

bool mac_icon_rgba(int idx, int px, uint32_t rgb, uint32_t *out) {
    return idx >= 0 && idx < MAC_NICONS && draw_rgba(icon_image(idx), px, rgb, out);
}

bool mac_symbol_rgba(const char *name, int px, uint32_t rgb, uint32_t *out) {
    UIImage *im = [UIImage systemImageNamed:[NSString stringWithUTF8String:name]
                          withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:px weight:UIImageSymbolWeightSemibold]];
    return draw_rgba(im, px, rgb, out);
}

int mac_tab_menu(SDL_Window *w, float x, float y, int mode, int lim, bool vctl, bool lite, const char *deflabel) {
    SheetItem m[] = { { [NSString stringWithUTF8String:deflabel], MENU_TAB_CPU_DEFAULT, mode == 0, nil },
        { @"Never limit", MENU_TAB_CPU_NEVER, mode == 1, nil },
        { @"Limit to 20%", MENU_TAB_CPU_LIM + 20, mode == 2 && lim == 20, nil },
        { @"Limit to 40%", MENU_TAB_CPU_LIM + 40, mode == 2 && lim == 40, nil },
        { @"Limit to 60%", MENU_TAB_CPU_LIM + 60, mode == 2 && lim == 60, nil },
        { @"Always show video controls", MENU_TAB_VCTL, vctl, nil },
        { @"Lite mode (release off-screen media to save RAM)", MENU_TAB_LITE, lite, nil } };
    return sheet(w, x, y, @"CPU limit for this tab", m, (int)(sizeof m / sizeof *m));
}

/* SDL's UIKit SDL_WaitEventTimeout polls the run loop every ms; block in CFRunLoop instead and
   wake it through a signalled source whenever any thread pushes an SDL event. */
static CFRunLoopSourceRef g_wake_src;
static void wake_perform(void *info) { (void)info; }
static bool SDLCALL wake_watch(void *ud, SDL_Event *e) {
    (void)ud; (void)e;
    CFRunLoopSourceSignal(g_wake_src);
    CFRunLoopWakeUp(CFRunLoopGetMain());
    return true;
}
void ios_wait_init(void) {
    CFRunLoopSourceContext c = {0};
    c.perform = wake_perform;
    g_wake_src = CFRunLoopSourceCreate(NULL, 0, &c);
    CFRunLoopAddSource(CFRunLoopGetMain(), g_wake_src, kCFRunLoopCommonModes);
    SDL_AddEventWatch(wake_watch, NULL);
}
bool ios_wait_event(SDL_Event *ev, int to_ms) {
    if (SDL_PollEvent(ev)) return true;
    @autoreleasepool {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, to_ms < 0 ? 3600.0 : to_ms / 1000.0, true);
    }
    return SDL_PollEvent(ev);
}

/* getDisplayMedia on iOS/iPadOS/Catalyst: ReplayKit in-app capture (system prompt + recording indicator).
   Frames arrive on a ReplayKit queue and are staged here for the main loop's capture source. */
#import <ReplayKit/ReplayKit.h>
#import <Accelerate/Accelerate.h>
#include <pthread.h>
static pthread_mutex_t g_rp_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t *g_rp_px;
static int g_rp_w, g_rp_h;
static bool g_rp_on;
static vImage_YpCbCrToARGB g_rp_conv;
static OSType g_rp_conv_fmt;

static void rp_frame(CVPixelBufferRef pb) {
    OSType f = CVPixelBufferGetPixelFormatType(pb);
    bool full = f == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
    if (f != kCVPixelFormatType_32BGRA && !full && f != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) return;
    int w = (int)CVPixelBufferGetWidth(pb), h = (int)CVPixelBufferGetHeight(pb);
    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    pthread_mutex_lock(&g_rp_mu);
    if (g_rp_on && w > 0 && h > 0) {
        if (w != g_rp_w || h != g_rp_h) { fprintf(stderr, "lumen: ReplayKit frames %dx%d\n", w, h); free(g_rp_px); g_rp_px = malloc((size_t)w * h * 4); g_rp_w = w; g_rp_h = h; }
        if (f == kCVPixelFormatType_32BGRA) {
            const uint8_t *b = CVPixelBufferGetBaseAddress(pb); size_t bpr = CVPixelBufferGetBytesPerRow(pb);
            for (int y = 0; y < h; y++) memcpy(g_rp_px + (size_t)y * w, b + (size_t)y * bpr, (size_t)w * 4);
        } else {
            if (g_rp_conv_fmt != f) {
                vImage_YpCbCrPixelRange r = full ? (vImage_YpCbCrPixelRange){ 0, 128, 255, 255, 255, 1, 255, 0 }
                                                 : (vImage_YpCbCrPixelRange){ 16, 128, 235, 240, 255, 0, 255, 1 };
                g_rp_conv_fmt = vImageConvert_YpCbCrToARGB_GenerateConversion(kvImage_YpCbCrToARGBMatrix_ITU_R_709_2, &r, &g_rp_conv,
                                    kvImage420Yp8_CbCr8, kvImageARGB8888, kvImageNoFlags) == kvImageNoError ? f : 0;
            }
            if (g_rp_conv_fmt == f) {
                vImage_Buffer yb = { CVPixelBufferGetBaseAddressOfPlane(pb, 0), (vImagePixelCount)h, (vImagePixelCount)w, CVPixelBufferGetBytesPerRowOfPlane(pb, 0) };
                vImage_Buffer cb = { CVPixelBufferGetBaseAddressOfPlane(pb, 1), (vImagePixelCount)(h / 2), (vImagePixelCount)(w / 2), CVPixelBufferGetBytesPerRowOfPlane(pb, 1) };
                vImage_Buffer db = { g_rp_px, (vImagePixelCount)h, (vImagePixelCount)w, (size_t)w * 4 };
                const uint8_t bgra[4] = { 3, 2, 1, 0 };   /* Lumen frames are ARGB8888 words = B,G,R,A bytes */
                vImageConvert_420Yp8_CbCr8ToARGB8888(&yb, &cb, &db, &g_rp_conv, bgra, 255, kvImageNoFlags);
            }
        }
    }
    pthread_mutex_unlock(&g_rp_mu);
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
}

int ios_rp_start(void) {
    RPScreenRecorder *r = RPScreenRecorder.sharedRecorder;
    if (getenv("LUMEN_NO_REPLAYKIT") || !r.available) return 0;
    pthread_mutex_lock(&g_rp_mu); g_rp_on = true; pthread_mutex_unlock(&g_rp_mu);
    [r startCaptureWithHandler:^(CMSampleBufferRef sb, RPSampleBufferType t, NSError *e) {
        CVPixelBufferRef pb = e ? NULL : CMSampleBufferGetImageBuffer(sb);
        static int logged;
        if (logged++ < 3) {
            OSType f = pb ? CVPixelBufferGetPixelFormatType(pb) : 0;
            fprintf(stderr, "lumen: ReplayKit sample type=%d fmt=%c%c%c%c err=%s\n", (int)t, (int)(f >> 24) & 255, (int)(f >> 16) & 255,
                    (int)(f >> 8) & 255, (int)f & 255, e ? e.localizedDescription.UTF8String : "none");
        }
        if (e || t != RPSampleBufferTypeVideo) return;
        if (pb) rp_frame(pb);
    } completionHandler:^(NSError *e) {
        if (!e) { fprintf(stderr, "lumen: ReplayKit capture started\n"); return; }
        fprintf(stderr, "lumen: ReplayKit capture unavailable (%s), recording page pixels\n", e.localizedDescription.UTF8String);
        pthread_mutex_lock(&g_rp_mu); g_rp_on = false; pthread_mutex_unlock(&g_rp_mu);
    }];
    return 1;
}
void ios_rp_stop(void) {
    pthread_mutex_lock(&g_rp_mu);
    g_rp_on = false; free(g_rp_px); g_rp_px = NULL; g_rp_w = g_rp_h = 0;
    pthread_mutex_unlock(&g_rp_mu);
    [RPScreenRecorder.sharedRecorder stopCaptureWithHandler:nil];
}
/* hands the newest ReplayKit frame to `store`; false until ReplayKit has delivered one */
bool ios_rp_push(void (*store)(const uint32_t *px, int w, int h, int stride)) {
    pthread_mutex_lock(&g_rp_mu);
    bool ok = g_rp_on && g_rp_px;
    if (ok) store(g_rp_px, g_rp_w, g_rp_h, g_rp_w);
    pthread_mutex_unlock(&g_rp_mu);
    return ok;
}
