#include "VaultWatch.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
struct TempVault
{
    std::filesystem::path mRoot;
    TempVault()
    {
        std::mt19937_64 generator(std::random_device{}());
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                                   ("inkstow-webview-watch-" + std::to_string(generator()));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                mRoot = candidate;
                return;
            }
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error(
                    "Could not create test vault",
                    candidate,
                    error);
        }
        throw std::runtime_error("Could not reserve unique test vault");
    }
    ~TempVault()
    {
        if (mRoot.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(mRoot, ignored);
    }
    TempVault(const TempVault&) = delete;
    TempVault& operator=(const TempVault&) = delete;
    TempVault(TempVault&&) = delete;
    TempVault& operator=(TempVault&&) = delete;
};
} // namespace

TEST_CASE("VaultWatch reports selected note and folder changes")
{
    namespace fs = std::filesystem;
    TempVault vault;
    const auto& root = vault.mRoot;
    fs::create_directories(root / "Folder");
    const auto note = root / "Folder" / "Note.md";
    {
        std::ofstream file(note);
        file << "one";
    }
    std::atomic<int> events = 0;
    {
        inkstow::webview_shell::VaultWatch watch([&] { ++events; });
        watch.open(root, "Folder/Note.md");
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        {
            std::ofstream file(note);
            file << "a different outside note";
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (events.load() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        REQUIRE(events.load() > 0);
        const int previous = events.load();
        {
            std::ofstream file(root / "Folder" / "Added.md");
            file << "new";
        }
        while (events.load() == previous && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        REQUIRE(events.load() > previous);
    }
}
