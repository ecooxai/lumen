#import <Cocoa/Cocoa.h>
#include "macui.h"

static Uint32 g_menu_ev;

@interface LumenMenuTarget : NSObject
@end
@implementation LumenMenuTarget
- (void)act:(NSMenuItem *)it {
    SDL_Event e; SDL_zero(e); e.type = g_menu_ev; e.user.code = (Sint32)it.tag; SDL_PushEvent(&e);
}
@end
static LumenMenuTarget *g_target;

static void add_item(NSMenu *m, NSString *title, NSString *key, NSEventModifierFlags mods, int tag) {
    NSMenuItem *it = [m addItemWithTitle:title action:@selector(act:) keyEquivalent:key];
    it.keyEquivalentModifierMask = mods; it.target = g_target; it.tag = tag;
}

void mac_style_window(SDL_Window *w) {
    NSWindow *win = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!win) return;
    win.styleMask |= NSWindowStyleMaskFullSizeContentView;
    win.titlebarAppearsTransparent = YES;
    win.titleVisibility = NSWindowTitleHidden;
}

void mac_install_menu(Uint32 ev) {
    g_menu_ev = ev; g_target = [LumenMenuTarget new];
    NSMenu *bar = NSApp.mainMenu;
    if (!bar) { bar = [NSMenu new]; NSApp.mainMenu = bar; }
    NSMenu *m = [[NSMenu alloc] initWithTitle:@"Workspace"];
    NSEventModifierFlags cs = NSEventModifierFlagCommand | NSEventModifierFlagShift, cc = NSEventModifierFlagCommand | NSEventModifierFlagControl;
    add_item(m, @"New Workspace…", @"n", cs, MENU_WS_NEW);
    add_item(m, @"Rename Workspace…", @"", 0, MENU_WS_RENAME);
    add_item(m, @"Close Workspace", @"", 0, MENU_WS_CLOSE);
    [m addItem:[NSMenuItem separatorItem]];
    add_item(m, @"Next Workspace", @"]", cc, MENU_WS_NEXT);
    add_item(m, @"Previous Workspace", @"[", cc, MENU_WS_PREV);
    [m addItem:[NSMenuItem separatorItem]];
    add_item(m, @"Show/Hide Workspace Bar", @"s", cc, MENU_WS_SIDEBAR);
    NSMenuItem *top = [[NSMenuItem alloc] initWithTitle:@"Workspace" action:nil keyEquivalent:@""];
    top.submenu = m;
    NSInteger idx = [bar indexOfItemWithTitle:@"Window"];
    [bar insertItem:top atIndex:idx < 0 ? bar.numberOfItems : idx];
}

bool mac_prompt(const char *title, const char *init, char *out, size_t n) {
    NSAlert *al = [NSAlert new];
    al.messageText = [NSString stringWithUTF8String:title];
    [al addButtonWithTitle:@"OK"]; [al addButtonWithTitle:@"Cancel"];
    NSTextField *tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
    tf.stringValue = [NSString stringWithUTF8String:init];
    al.accessoryView = tf; al.window.initialFirstResponder = tf;
    if ([al runModal] != NSAlertFirstButtonReturn) return false;
    snprintf(out, n, "%s", tf.stringValue.UTF8String);
    return out[0] != 0;
}

static const char *icon_names[MAC_NICONS] = { "briefcase.fill", "gamecontroller.fill", "house.fill", "star.fill", "heart.fill", "book.fill",
    "chevron.left.forwardslash.chevron.right", "music.note", "cart.fill", "globe", "graduationcap.fill", "airplane", "camera.fill", "bolt.fill", "leaf.fill", "paintbrush.fill" };
static NSString *icon_titles[MAC_NICONS] = { @"Work", @"Game", @"Home", @"Star", @"Heart", @"Reading", @"Code", @"Music", @"Shopping", @"Web",
    @"School", @"Travel", @"Photos", @"Energy", @"Nature", @"Art" };
static int g_pick;
@interface LumenPick : NSObject
@end
@implementation LumenPick
- (void)pick:(NSMenuItem *)it { g_pick = (int)it.tag; }
@end

static NSImage *icon_image(int i) {
    return [NSImage imageWithSystemSymbolName:[NSString stringWithUTF8String:icon_names[i]] accessibilityDescription:icon_titles[i]];
}

int mac_ws_menu(SDL_Window *w, float x, float y) {
    NSWindow *win = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!win) return 0;
    static LumenPick *pk; if (!pk) pk = [LumenPick new];
    NSMenuItem *(^add)(NSMenu *, NSString *, int) = ^(NSMenu *mm, NSString *t, int tag) {
        NSMenuItem *it = [mm addItemWithTitle:t action:@selector(pick:) keyEquivalent:@""]; it.target = pk; it.tag = tag; return it;
    };
    NSMenu *m = [NSMenu new], *sub = [NSMenu new];
    add(m, @"Rename…", MENU_WS_RENAME);
    for (int i = 0; i < MAC_NICONS; i++) add(sub, icon_titles[i], MENU_WS_ICON + i).image = icon_image(i);
    [sub addItem:[NSMenuItem separatorItem]];
    add(sub, @"Letters", MENU_WS_ICON + MAC_NICONS);
    [m addItemWithTitle:@"Change Icon" action:nil keyEquivalent:@""].submenu = sub;
    add(m, @"Refresh All Tabs", MENU_WS_REFRESH);
    [m addItem:[NSMenuItem separatorItem]];
    add(m, @"New Workspace…", MENU_WS_NEW);
    add(m, @"Close Workspace", MENU_WS_CLOSE);
    g_pick = 0;
    NSView *v = win.contentView;
    [m popUpMenuPositioningItem:nil atLocation:(v.isFlipped ? NSMakePoint(x, y) : NSMakePoint(x, v.bounds.size.height - y)) inView:v];
    return g_pick;
}

bool mac_icon_rgba(int idx, int px, uint32_t rgb, uint32_t *out) {
    if (idx < 0 || idx >= MAC_NICONS) return false;
    NSImage *im = icon_image(idx);
    if (!im) return false;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(out, (size_t)px, (size_t)px, 8, (size_t)px * 4, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (!ctx) return false;
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
    NSSize sz = im.size; CGFloat sc = MIN(px / sz.width, px / sz.height);
    [im drawInRect:NSMakeRect((px - sz.width * sc) / 2, (px - sz.height * sc) / 2, sz.width * sc, sz.height * sc)];
    [[NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.0 green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:1] set];
    NSRectFillUsingOperation(NSMakeRect(0, 0, px, px), NSCompositingOperationSourceAtop);
    [NSGraphicsContext restoreGraphicsState];
    CGContextRelease(ctx);
    return true;
}
