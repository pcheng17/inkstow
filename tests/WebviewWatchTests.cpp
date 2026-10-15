#include "VaultWatch.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

int run()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "inkstow-webview-watch-test";
    fs::remove_all(root);
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
        if (events.load() == 0) throw std::runtime_error("Selected note change was not reported");
        const int previous = events.load();
        {
            std::ofstream file(root / "Folder" / "Added.md");
            file << "new";
        }
        while (events.load() == previous && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (events.load() == previous) throw std::runtime_error("Folder change was not reported");
    }
    fs::remove_all(root);
    return 0;
}

int main()
{
    try {
        return run();
    } catch (const std::exception&) {
        return 1;
    }
}
