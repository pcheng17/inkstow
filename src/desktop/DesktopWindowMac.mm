#include "DesktopWindow.h"

#import <AppKit/AppKit.h>


using inkstow::webview_shell::DesktopWindow;

@interface InkstowWindowDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate> {
@public
    DesktopWindow* shell;
    id previousWindow;
    id previousApp;
}
- (void)openVault:(id)sender;
- (void)save:(id)sender;
- (void)quit:(id)sender;
- (void)undo:(id)sender;
- (void)follow:(id)sender;
@end

@implementation InkstowWindowDelegate
- (BOOL)windowShouldClose:(id)sender { shell->invoke("inkstowAskQuit()"); return NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    shell->invoke("inkstowAskQuit()");
    return NSTerminateCancel;
}
- (void)openVault:(id)sender { shell->invoke("inkstowOpenPicker()"); }
- (void)save:(id)sender { shell->invoke("inkstowSave()"); }
- (void)quit:(id)sender { shell->invoke("inkstowAskQuit()"); }
- (void)undo:(id)sender { shell->invoke("inkstowUndo()"); }
- (void)follow:(id)sender { shell->invoke("inkstowFollow()"); }
- (BOOL)respondsToSelector:(SEL)selector
{
    return [super respondsToSelector:selector] || [previousWindow respondsToSelector:selector] ||
           [previousApp respondsToSelector:selector];
}
- (id)forwardingTargetForSelector:(SEL)selector
{
    if ([previousWindow respondsToSelector:selector]) return previousWindow;
    return previousApp;
}
@end

namespace inkstow::webview_shell
{
namespace
{
void addItem(NSMenu* menu, NSString* name, SEL action, NSString* shortcut, id target)
{
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:name action:action keyEquivalent:shortcut];
    [item setTarget:target];
    [menu addItem:item];
    [item release];
}
} // namespace

DesktopWindow::DesktopWindow(webview::webview& window) : mWindow(window)
{
    @autoreleasepool {
        NSWindow* cocoa = static_cast<NSWindow*>(window.window().value());
        mHandle = cocoa;
        InkstowWindowDelegate* delegate = [[InkstowWindowDelegate alloc] init];
        delegate->shell = this;
        delegate->previousWindow = [[cocoa delegate] retain];
        delegate->previousApp = [[NSApp delegate] retain];
        mDelegate = delegate;
        [cocoa setDelegate:delegate];
        [NSApp setDelegate:delegate];

        NSMenu* bar = [[NSMenu alloc] init];
        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Inkstow"];
        addItem(appMenu, @"Quit Inkstow", @selector(quit:), @"q", delegate);
        [appItem setSubmenu:appMenu];
        [bar addItem:appItem];
        NSMenuItem* fileItem = [[NSMenuItem alloc] init];
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        addItem(fileMenu, @"Open vault…", @selector(openVault:), @"o", delegate);
        addItem(fileMenu, @"Save", @selector(save:), @"s", delegate);
        [fileItem setSubmenu:fileMenu];
        [bar addItem:fileItem];
        NSMenuItem* noteItem = [[NSMenuItem alloc] init];
        NSMenu* noteMenu = [[NSMenu alloc] initWithTitle:@"Note"];
        addItem(noteMenu, @"Undo", @selector(undo:), @"z", delegate);
        addItem(noteMenu, @"Follow link", @selector(follow:), @"\r", delegate);
        [[noteMenu itemAtIndex:1] setKeyEquivalentModifierMask:NSEventModifierFlagOption];
        [noteItem setSubmenu:noteMenu];
        [bar addItem:noteItem];
        [NSApp setMainMenu:bar];
        [bar release]; [appItem release]; [appMenu release];
        [fileItem release]; [fileMenu release]; [noteItem release]; [noteMenu release];
    }
}

DesktopWindow::~DesktopWindow()
{
    @autoreleasepool {
        auto* delegate = static_cast<InkstowWindowDelegate*>(mDelegate);
        if (!delegate) return;
        [static_cast<NSWindow*>(mHandle) setDelegate:delegate->previousWindow];
        [NSApp setDelegate:delegate->previousApp];
        [delegate->previousWindow release];
        [delegate->previousApp release];
        [delegate release];
    }
}
void DesktopWindow::invoke(const char* script) { mWindow.eval(script); }
} // namespace inkstow::webview_shell
