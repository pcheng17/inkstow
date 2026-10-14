#include "Vault.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#elif !defined(__APPLE__)
#include <gio/gio.h>
#endif

namespace inkstow::portable
{
bool Vault::trashNote(const std::string& relative, std::string* error) const
{
    return trashNote(
        relative,
        [](const std::filesystem::path& path, std::string* reason) {
#ifdef _WIN32
            // SHFileOperation expects a double-NUL-terminated absolute path.
            std::wstring name = path.wstring();
            name.push_back(L'\0');
            name.push_back(L'\0');
            SHFILEOPSTRUCTW operation{};
            operation.wFunc = FO_DELETE;
            operation.pFrom = name.c_str();
            operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
            const int result = SHFileOperationW(&operation);
            if (result == 0 && !operation.fAnyOperationsAborted) return true;
#elif defined(__APPLE__)
            extern bool trashMac(const std::filesystem::path&, std::string*);
            if (trashMac(path, reason)) return true;
#else
            GFile* file = g_file_new_for_path(path.c_str());
            GError* failure = nullptr;
            const gboolean moved = g_file_trash(file, nullptr, &failure);
            if (failure) g_error_free(failure);
            g_object_unref(file);
            if (moved) return true;
#endif
            *reason = "Could not move the note to the system trash, so it was not deleted.";
            return false;
        },
        error);
}
} // namespace inkstow::portable
