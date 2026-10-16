#include "Vault.h"

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: vault-inspect <vault-folder>\n";
        return 1;
    }
    std::string folder = argv[1];
#ifdef _WIN32
    // Windows narrow argv uses the system code page, but Vault::open expects UTF-8.
    int argumentCount = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments || argumentCount != 2) {
        if (arguments) LocalFree(arguments);
        std::cerr << "Cannot read vault folder argument.\n";
        return 1;
    }
    const int bytes = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        arguments[1],
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);
    int converted = 0;
    if (bytes > 0) {
        folder.resize(static_cast<std::size_t>(bytes));
        converted = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            arguments[1],
            -1,
            folder.data(),
            bytes,
            nullptr,
            nullptr);
        if (converted > 0) folder.pop_back(); // Conversion includes the terminating NUL.
    }
    LocalFree(arguments);
    if (converted == 0) {
        std::cerr << "Cannot convert vault folder argument to UTF-8.\n";
        return 1;
    }
#endif
    std::string error;
    const auto vault = inkstow::portable::Vault::open(folder, &error);
    std::vector<std::string> notes;
    if (!vault || !vault->listNotes(&notes, &error)) {
        std::cerr << "Vault error: " << error << '\n';
        return 1;
    }
    for (const auto& note : notes)
        std::cout << note << '\n';
    return 0;
}
