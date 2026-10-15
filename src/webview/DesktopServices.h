#pragma once

#include "Bridge.h"
#include "VaultWatch.h"

#include <filesystem>

namespace inkstow::webview_shell
{
class DesktopServices final : public ShellServices
{
public:
    std::string lastVault() override;
    void setLastVault(const std::string& path) override;
    std::string pickVault() override;
    bool openAttachment(const std::filesystem::path& path) override;
    void watchVault(const std::string& folder) override;
    void watchNote(const std::string& note) override;
    void setWatcher(VaultWatch* watcher) { mWatcher = watcher; }

private:
    static std::filesystem::path settingsFile();
    VaultWatch* mWatcher = nullptr;
};
} // namespace inkstow::webview_shell
