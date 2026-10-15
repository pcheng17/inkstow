#pragma once

#include "Session.h"

#include <functional>
#include <filesystem>
#include <string>

namespace inkstow::webview_shell
{

// Accepts the JSON argument array supplied by webview_bind, returns a JSON object.
class Bridge
{
public:
    using OpenAttachment = std::function<bool(const std::filesystem::path&, std::string*)>;
    using OpenExternal = std::function<bool(const std::string&, std::string*)>;
    explicit Bridge(
        OpenAttachment openAttachment = {},
        portable::Vault::TrashFunction trash = {},
        OpenExternal openExternal = {});
    std::string handle(const std::string& arguments);

private:
    void syncWithDisk(std::string* error);
    portable::Session mSession;
    OpenAttachment mOpenAttachment;
    OpenExternal mOpenExternal;
    portable::Vault::TrashFunction mTrash;
    portable::DiskChange mChange = portable::DiskChange::None;
    // A dismissed missing-note decision stays hidden on focus refresh until new edits or disk
    // change.
    bool mDismissedMissing = false;
};

} // namespace inkstow::webview_shell
