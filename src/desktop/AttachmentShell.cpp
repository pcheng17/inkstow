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

bool openExternal(const std::string& url, std::string* error)
{
#ifdef _WIN32
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url.c_str(), -1, nullptr, 0);
    if (length > 0) {
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                url.c_str(),
                -1,
                wide.data(),
                length) > 0)
        {
            const auto result = reinterpret_cast<std::intptr_t>(
                ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            if (result > 32) return true;
        }
    }
#else
    GError* failure = nullptr;
    const bool launched = g_app_info_launch_default_for_uri(url.c_str(), nullptr, &failure);
    if (failure) g_error_free(failure);
    if (launched) return true;
#endif
    *error = "Could not open URL in the default browser.";
    return false;
}
} // namespace inkstow::webview_shell
