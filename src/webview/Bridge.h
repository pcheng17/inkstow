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
    portable::Session mSession;
    portable::DiskChange mChange = portable::DiskChange::None;
};

} // namespace inkstow::webview_shell
