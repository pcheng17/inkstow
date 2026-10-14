#include "UnicodeCase.h"

#include <climits>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <glib.h>
#endif

namespace inkstow::portable
{
std::optional<std::string> caseFoldUtf8(const std::string& text)
{
    if (text.find('\0') != std::string::npos) return std::nullopt;
    if (text.empty()) return std::string();
#ifdef _WIN32
    if (text.size() > static_cast<size_t>(INT_MAX)) return std::nullopt;
    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (!length) return std::nullopt;
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (!MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            length))
        return std::nullopt;
    const int foldedLength = LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_LOWERCASE,
        wide.data(),
        length,
        nullptr,
        0,
        nullptr,
        nullptr,
        0);
    if (!foldedLength) return std::nullopt;
    std::wstring folded(static_cast<size_t>(foldedLength), L'\0');
    if (!LCMapStringEx(
            LOCALE_NAME_INVARIANT,
            LCMAP_LOWERCASE,
            wide.data(),
            length,
            folded.data(),
            foldedLength,
            nullptr,
            nullptr,
            0))
        return std::nullopt;
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, folded.data(), foldedLength, nullptr, 0, nullptr, nullptr);
    if (!size) return std::nullopt;
    std::string result(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(
            CP_UTF8,
            0,
            folded.data(),
            foldedLength,
            result.data(),
            size,
            nullptr,
            nullptr))
        return std::nullopt;
    return result;
#elif defined(__APPLE__)
    auto* value = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()),
        kCFStringEncodingUTF8,
        false);
    if (!value) return std::nullopt;
    auto* folded = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, value);
    CFRelease(value);
    if (!folded) return std::nullopt;
    CFStringFold(folded, kCFCompareCaseInsensitive, nullptr);
    const auto size =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(folded), kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<size_t>(size), '\0');
    const bool ok = CFStringGetCString(folded, result.data(), size, kCFStringEncodingUTF8);
    CFRelease(folded);
    if (!ok) return std::nullopt;
    result.resize(std::strlen(result.c_str()));
    return result;
#else
    if (!g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr))
        return std::nullopt;
    gchar* folded = g_utf8_casefold(text.c_str(), static_cast<gssize>(text.size()));
    if (!folded) return std::nullopt;
    std::string result(folded);
    g_free(folded);
    return result;
#endif
}
} // namespace inkstow::portable
