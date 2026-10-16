#pragma once

#include <optional>
#include <string>

namespace inkstow::portable
{
// Unicode case folding for vault path lookup, without trimming or changing accents.
std::optional<std::string> caseFoldUtf8(const std::string& text);
} // namespace inkstow::portable
