#include "Vault.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
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
            IFileOperation* operation = nullptr;
            IShellItem* item = nullptr;
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (SUCCEEDED(initialized)) {
                HRESULT result = CoCreateInstance(
                    CLSID_FileOperation,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&operation));
                if (SUCCEEDED(result))
                    result = operation->SetOperationFlags(
                        FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE |
                        FOFX_EARLYFAILURE);
                if (SUCCEEDED(result))
                    result =
                        SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
                if (SUCCEEDED(result)) result = operation->DeleteItem(item, nullptr);
                if (SUCCEEDED(result)) result = operation->PerformOperations();
                BOOL aborted = TRUE;
                if (SUCCEEDED(result)) result = operation->GetAnyOperationsAborted(&aborted);
                if (item) item->Release();
                if (operation) operation->Release();
                CoUninitialize();
                if (SUCCEEDED(result) && !aborted) return true;
            }
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
