#include "DesktopServices.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <shlobj.h>
#include <windows.h>
#elif defined(__APPLE__)
// The picker is implemented in DesktopServicesMac.mm.
#else
#include <gtk/gtk.h>
#endif

namespace inkstow::webview_shell
{
#if defined(_WIN32)
namespace
{
std::string utf8FromWide(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
} // namespace
#endif

std::filesystem::path DesktopServices::settingsFile()
{
#if defined(_WIN32)
    PWSTR directory = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &directory))) return {};
    const std::filesystem::path root(directory);
    CoTaskMemFree(directory);
    return root / "Inkstow" / "last-vault";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) / "Library/Application Support/Inkstow/last-vault"
                : std::filesystem::path{};
#else
    const char* config = std::getenv("XDG_CONFIG_HOME");
    if (config && *config) return std::filesystem::path(config) / "inkstow/last-vault";
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) / ".config/inkstow/last-vault"
                : std::filesystem::path{};
#endif
}

std::string DesktopServices::lastVault()
{
    const auto file = settingsFile();
    if (file.empty()) return {};
    std::ifstream input(file, std::ios::binary);
    std::string folder(std::istreambuf_iterator<char>{input}, {});
    if (folder.size() > 4096) return {};
    return folder;
}

void DesktopServices::setLastVault(const std::string& path)
{
    const auto file = settingsFile();
    if (file.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) return;
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output << path;
}

#if defined(_WIN32)
std::string DesktopServices::pickVault()
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog))))
        return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    std::string result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR folder = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &folder))) {
                result = utf8FromWide(folder);
                CoTaskMemFree(folder);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}
#elif !defined(__APPLE__)
std::string DesktopServices::pickVault()
{
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new(
        "Open vault",
        nullptr,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Open",
        "Cancel");
    std::string result;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            result = folder;
            g_free(folder);
        }
    }
    g_object_unref(dialog);
    return result;
}
#endif
} // namespace inkstow::webview_shell
