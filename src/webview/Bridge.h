#pragma once

#include "Session.h"

#include <filesystem>
#include <string>

namespace inkstow::webview_shell
{

// Native UI and local settings are supplied by the shell, not the vault core.
class ShellServices
{
public:
    ShellServices() = default;
    ShellServices(const ShellServices&) = delete;
    ShellServices& operator=(const ShellServices&) = delete;
    ShellServices(ShellServices&&) = delete;
    ShellServices& operator=(ShellServices&&) = delete;
    virtual ~ShellServices() = default;
    virtual std::string lastVault() = 0;
    virtual void setLastVault(const std::string& path) = 0;
    // Empty means the picker was cancelled.
    virtual std::string pickVault() = 0;
    virtual bool openAttachment(const std::filesystem::path& path) = 0;
    virtual void watchVault(const std::string& folder) = 0;
    virtual void watchNote(const std::string& note) = 0;
};

// Accepts the JSON argument array supplied by webview_bind, returns a JSON object.
class Bridge
{
public:
    explicit Bridge(ShellServices* services = nullptr)
        : mServices(services)
    {
    }
    std::string handle(const std::string& arguments);

private:
    void syncWithDisk(std::string* error);
    ShellServices* mServices;
    portable::Session mSession;
    portable::DiskChange mChange = portable::DiskChange::None;
    // A dismissed missing-note decision stays hidden on focus refresh until new edits or disk
    // change.
    bool mDismissedMissing = false;
};

} // namespace inkstow::webview_shell
