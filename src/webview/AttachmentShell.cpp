#include "AttachmentShell.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <gio/gio.h>
#endif

namespace inkstow::webview_shell
{
bool openAttachment(const std::filesystem::path& path, std::string* error)
{
#ifdef _WIN32
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32) return true;
#else
    GFile* file = g_file_new_for_path(path.c_str());
    char* uri = g_file_get_uri(file);
    GError* failure = nullptr;
    const bool launched = g_app_info_launch_default_for_uri(uri, nullptr, &failure);
    if (failure) g_error_free(failure);
    g_free(uri);
    g_object_unref(file);
    if (launched) return true;
#endif
    *error = "Could not open attachment in the default application.";
    return false;
}
} // namespace inkstow::webview_shell
