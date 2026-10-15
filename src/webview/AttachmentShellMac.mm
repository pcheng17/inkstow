#include "AttachmentShell.h"

#import <AppKit/AppKit.h>

namespace inkstow::webview_shell
{
bool openAttachment(const std::filesystem::path& path, std::string* error)
{
    @autoreleasepool {
        NSString* name = [NSString stringWithUTF8String:path.c_str()];
        if (name && [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:name]])
            return true;
        *error = "Could not open attachment in the default application.";
        return false;
    }
}
} // namespace inkstow::webview_shell
