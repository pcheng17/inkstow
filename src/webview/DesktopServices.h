#pragma once

#include "Bridge.h"

#include <filesystem>

namespace inkstow::webview_shell
{
class DesktopServices final : public ShellServices
{
public:
    std::string lastVault() override;
    void setLastVault(const std::string& path) override;
    std::string pickVault() override;

private:
    static std::filesystem::path settingsFile();
};
} // namespace inkstow::webview_shell
