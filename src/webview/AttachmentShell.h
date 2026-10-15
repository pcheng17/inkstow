#pragma once

#include <filesystem>
#include <string>

namespace inkstow::webview_shell
{
// Open a core-validated local attachment with the operating system's default application.
bool openAttachment(const std::filesystem::path& path, std::string* error);
} // namespace inkstow::webview_shell
