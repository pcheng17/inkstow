#include "DesktopWindow.h"
#include "DesktopServices.h"

#include <doctest/doctest.h>

#import <AppKit/AppKit.h>

#include <stdexcept>
#include <string>

@interface PickerProbe : NSObject {
@public
    BOOL shown;
    NSOpenPanel* panel;
}
- (void)cancel:(NSTimer*)timer;
@end

@implementation PickerProbe
- (void)cancel:(NSTimer*)timer
{
    shown = [panel isVisible];
    [panel cancel:nil];
}
@end

TEST_CASE("WebviewWindow macOS folder picker presents a panel")
{
    @autoreleasepool {
        webview::webview window(false, nullptr);
        inkstow::webview_shell::DesktopWindow desktop(window);
        inkstow::webview_shell::DesktopServices services;
        PickerProbe* probe = [[PickerProbe alloc] init];
        probe->shown = NO;
        probe->panel = [NSOpenPanel openPanel];
        NSTimer* timer = [NSTimer timerWithTimeInterval:1.0
                                               target:probe
                                             selector:@selector(cancel:)
                                             userInfo:nil
                                              repeats:NO];
        [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
        const auto selected = services.pickVault();
        [timer invalidate];
        const bool visible = probe->shown == YES;
        [probe release];
        CHECK(selected.empty());
        CHECK_MESSAGE(visible, "Native folder panel did not become visible before picker returned");
    }
}

void activateMacMenu(webview::webview& window, const char* action)
{
    @autoreleasepool {
        const std::string name(action);
        NSString* key = name == "open" ? @"o" : name == "save" ? @"s" : name == "undo" ? @"z" : @"q";
        if (name == "undo") {
            NSMenuItem* edit = [[NSApp mainMenu] itemAtIndex:2];
            if (![[[edit submenu] title] isEqualToString:@"Edit"] ||
                [[edit submenu] numberOfItems] != 1 ||
                ![[[[edit submenu] itemAtIndex:0] title] isEqualToString:@"Undo"])
                throw std::runtime_error("Native Edit menu must contain only Undo");
        }
        auto* native = static_cast<NSWindow*>(window.window().value());
        NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                         location:NSZeroPoint
                                    modifierFlags:NSEventModifierFlagCommand
                                        timestamp:0
                                     windowNumber:[native windowNumber]
                                          context:nil
                                       characters:key
                      charactersIgnoringModifiers:key
                                        isARepeat:NO
                                          keyCode:0];
        if (![[NSApp mainMenu] performKeyEquivalent:event])
            throw std::runtime_error("Native macOS menu shortcut not registered");
    }
}

void activateMacClose(webview::webview& window)
{
    @autoreleasepool {
        auto* native = static_cast<NSWindow*>(window.window().value());
        // The headless test window may not have a working close button; exercise the
        // delegate callback that AppKit calls for a real native close request.
        if ([[native delegate] windowShouldClose:native])
            throw std::runtime_error("Native close was not guarded");
    }
}

void activateMacApplicationQuit()
{
    @autoreleasepool {
        [NSApp terminate:nil];
    }
}
