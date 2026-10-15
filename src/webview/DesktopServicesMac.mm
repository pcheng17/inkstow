#include "DesktopServices.h"

#import <AppKit/AppKit.h>

namespace inkstow::webview_shell
{
std::string DesktopServices::pickVault()
{
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseDirectories:YES];
        [panel setCanChooseFiles:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] != NSModalResponseOK) return {};
        const char* path = [[[panel URLs] firstObject] fileSystemRepresentation];
        return path ? std::string(path) : std::string{};
    }
}
} // namespace inkstow::webview_shell
