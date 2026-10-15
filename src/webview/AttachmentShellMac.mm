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

bool openExternal(const std::string& url, std::string* error)
{
    @autoreleasepool {
        NSString* address = [NSString stringWithUTF8String:url.c_str()];
        if (address && [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:address]])
            return true;
        *error = "Could not open URL in the default browser.";
        return false;
    }
}
} // namespace inkstow::webview_shell
