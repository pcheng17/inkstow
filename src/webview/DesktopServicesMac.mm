#include "DesktopServices.h"

#import <AppKit/AppKit.h>

namespace inkstow::webview_shell
{
bool DesktopServices::openAttachment(const std::filesystem::path& path)
{
    @autoreleasepool {
        const auto bytes = path.u8string();
        const std::string text(bytes.begin(), bytes.end());
        NSString* name = [NSString stringWithUTF8String:text.c_str()];
        if (!name) return false;
        return [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:name]] == YES;
    }
}

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
