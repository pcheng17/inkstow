#include "AttachmentShell.h"

#import <AppKit/AppKit.h>

namespace inkstow::webview_shell
{
bool openAttachment(const std::filesystem::path& path, std::string* error)
{
    const auto utf8 = path.u8string();
    NSString* name = [[NSString alloc] initWithBytes:utf8.data() length:utf8.size()
                                           encoding:NSUTF8StringEncoding];
    if (name && [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:name]]) return true;
    *error = "Could not open attachment in the default application.";
    return false;
}
} // namespace inkstow::webview_shell
