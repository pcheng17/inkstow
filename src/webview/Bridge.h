#pragma once

#include "Session.h"

#include <string>

namespace inkstow::webview_shell
{

// Accepts the JSON argument array supplied by webview_bind, returns a JSON object.
class Bridge
{
public:
    std::string handle(const std::string& arguments);

private:
    void syncWithDisk(std::string* error);
    portable::Session mSession;
    portable::DiskChange mChange = portable::DiskChange::None;
    // A dismissed missing-note decision stays hidden on focus refresh until new edits or disk
    // change.
    bool mDismissedMissing = false;
};

} // namespace inkstow::webview_shell
