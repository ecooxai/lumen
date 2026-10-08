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
