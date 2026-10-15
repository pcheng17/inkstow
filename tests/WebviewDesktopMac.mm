#include "DesktopWindow.h"

#import <AppKit/AppKit.h>

#include <stdexcept>
#include <string>

void activateMacMenu(webview::webview& window, const char* action)
{
    @autoreleasepool {
        const std::string name(action);
        NSString* key = name == "open" ? @"o" : name == "save" ? @"s" : @"q";
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
        [native makeKeyAndOrderFront:nil];
        [native performClose:nil];
    }
}

void activateMacApplicationQuit()
{
    @autoreleasepool {
        [NSApp terminate:nil];
    }
}
