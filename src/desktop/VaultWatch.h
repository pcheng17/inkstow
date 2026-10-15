#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace inkstow::webview_shell
{
// Shell-side best-effort observation. Focus refresh catches changes a filesystem timestamp misses.
class VaultWatch
{
public:
    explicit VaultWatch(std::function<void()> changed);
    VaultWatch(const VaultWatch&) = delete;
    VaultWatch& operator=(const VaultWatch&) = delete;
    VaultWatch(VaultWatch&&) = delete;
    VaultWatch& operator=(VaultWatch&&) = delete;
    ~VaultWatch();

    void open(const std::filesystem::path& root, const std::string& note);
    void select(const std::string& note);

private:
    void run();
    std::mutex mMutex;
    std::filesystem::path mRoot;
    std::string mNote;
    std::size_t mGeneration = 0;
    std::function<void()> mChanged;
    std::atomic_bool mStop{false};
    std::thread mThread;
};
} // namespace inkstow::webview_shell
