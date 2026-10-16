#pragma once

#include <filesystem>
#include <string>

namespace inkstow::webview_shell
{
// Open a core-validated local attachment with the operating system's default application.
bool openAttachment(const std::filesystem::path& path, std::string* error);
// Open a bridge-validated HTTP(S) URL through the operating system's default browser.
bool openExternal(const std::string& url, std::string* error);
} // namespace inkstow::webview_shell
