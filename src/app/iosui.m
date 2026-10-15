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
