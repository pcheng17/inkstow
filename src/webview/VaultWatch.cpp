#include "VaultWatch.h"

#include <chrono>
#include <map>
#include <string>
#include <system_error>

namespace inkstow::webview_shell
{
namespace
{
using namespace std::chrono_literals;
using Stamp = std::pair<std::filesystem::file_time_type, std::uintmax_t>;
using Snapshot = std::map<std::filesystem::path, Stamp>;

Snapshot observe(const std::filesystem::path& root, const std::string& note)
{
    namespace fs = std::filesystem;
    Snapshot result;
    std::error_code error;
    auto record = [&](const fs::path& path, bool file) {
        const auto modified = fs::last_write_time(path, error);
        if (error) {
            error.clear();
            return;
        }
        std::uintmax_t size = 0;
        if (file) {
            size = fs::file_size(path, error);
            if (error) {
                error.clear();
                return;
            }
        }
        result.emplace(path, Stamp{modified, size});
    };
    record(root, false);
    fs::recursive_directory_iterator iterator(
        root,
        fs::directory_options::skip_permission_denied,
        error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const bool directory = iterator->is_directory(error);
        if (error) error.clear();
        if (directory) {
            const auto name = iterator->path().filename().u8string();
            if ((!name.empty() && name.front() == u8'.') || iterator->is_symlink(error)) {
                iterator.disable_recursion_pending();
            } else {
                record(iterator->path(), false);
            }
        }
        if (error) error.clear();
        iterator.increment(error);
    }
    if (!note.empty()) {
        const auto path = fs::path(std::u8string(note.begin(), note.end()));
        // The session has already validated this vault-relative selection. Never traverse links.
        record(root / path, true);
    }
    return result;
}
} // namespace

VaultWatch::VaultWatch(std::function<void()> changed)
    : mChanged(std::move(changed)), mThread([this] { run(); })
{
}
VaultWatch::~VaultWatch()
{
    mStop.store(true);
    if (mThread.joinable()) mThread.join();
}
void VaultWatch::open(const std::filesystem::path& root, const std::string& note)
{
    std::lock_guard lock(mMutex);
    mRoot = root;
    mNote = note;
    ++mGeneration;
}
void VaultWatch::select(const std::string& note)
{
    std::lock_guard lock(mMutex);
    mNote = note;
    ++mGeneration;
}
void VaultWatch::run()
{
    Snapshot previous;
    std::size_t generation = 0;
    bool initialized = false;
    while (!mStop.load()) {
        std::filesystem::path root;
        std::string note;
        std::size_t current = 0;
        {
            std::lock_guard lock(mMutex);
            root = mRoot;
            note = mNote;
            current = mGeneration;
        }
        if (!root.empty()) {
            const auto snapshot = observe(root, note);
            {
                std::lock_guard lock(mMutex);
                if (current == mGeneration) {
                    if (initialized && generation == current && snapshot != previous) mChanged();
                    previous = snapshot;
                    generation = current;
                    initialized = true;
                }
            }
        }
        std::this_thread::sleep_for(400ms);
    }
}
} // namespace inkstow::webview_shell
