#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace inkstow::portable::detail
{

bool validUtf8(const std::string &text);
enum class ReplaceResult : std::uint8_t
{
    Success,
    Retryable,
    Failure
};
// Internal seam: tests simulate Windows lock outcomes on any platform. The
// callback only replaces the staged file; every retry rechecks disk bytes.
using ReplaceAttempt = std::function<ReplaceResult(
    const std::filesystem::path &, const std::filesystem::path &, std::string *)>;
bool guardedReplace(
    const std::filesystem::path &path,
    const std::string &expected,
    const std::string &bytes,
    const std::function<bool(std::string *)> &revalidate,
    const ReplaceAttempt &replace,
    int maxAttempts,
    std::string *error);
ReplaceResult replaceFile(
    const std::filesystem::path &staged,
    const std::filesystem::path &destination,
    std::string *error);

} // namespace inkstow::portable::detail
