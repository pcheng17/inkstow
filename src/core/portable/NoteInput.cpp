#include <climits>
#include <cstring>
#include <optional>
#include <string>

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
std::optional<std::string> normalizeNoteInput(const std::string& input)
{
    if (input.find('\0') != std::string::npos) return std::nullopt;
#ifdef _WIN32
    if (input.size() > static_cast<size_t>(INT_MAX)) return std::nullopt;
    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        input.data(),
        static_cast<int>(input.size()),
        nullptr,
        0);
    if (!length) return std::nullopt;
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (!MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            input.data(),
            static_cast<int>(input.size()),
            wide.data(),
            length))
        return std::nullopt;
    const int size = NormalizeString(NormalizationC, wide.data(), length, nullptr, 0);
    if (size <= 0) return std::nullopt;
    std::wstring normalized(static_cast<size_t>(size), L'\0');
    if (NormalizeString(NormalizationC, wide.data(), length, normalized.data(), size) <= 0)
        return std::nullopt;
    // NormalizeString can include a terminating NUL in its reported length.
    if (!normalized.empty() && normalized.back() == L'\0') normalized.pop_back();
    const auto space = [](wchar_t value) {
        WORD kind = 0;
        return GetStringTypeW(CT_CTYPE1, &value, 1, &kind) && (kind & C1_SPACE);
    };
    // GetStringTypeW covers non-ASCII whitespace as well as ASCII.
    size_t first = 0;
    size_t last = normalized.size();
    while (first < last && space(normalized[first]))
        ++first;
    while (last > first && space(normalized[last - 1]))
        --last;
    const int count = WideCharToMultiByte(
        CP_UTF8,
        0,
        normalized.data() + first,
        static_cast<int>(last - first),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (!count) {
        if (last == first) return std::string();
        return std::nullopt;
    }
    std::string result(static_cast<size_t>(count), '\0');
    if (!WideCharToMultiByte(
            CP_UTF8,
            0,
            normalized.data() + first,
            static_cast<int>(last - first),
            result.data(),
            count,
            nullptr,
            nullptr))
        return std::nullopt;
    return result;
#elif defined(__APPLE__)
    auto* text = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(input.data()),
        static_cast<CFIndex>(input.size()),
        kCFStringEncodingUTF8,
        false);
    if (!text) return std::nullopt;
    auto* mutableText = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, text);
    CFRelease(text);
    if (!mutableText) return std::nullopt;
    CFStringNormalize(mutableText, kCFStringNormalizationFormC);
    CFStringTrimWhitespace(mutableText);
    const auto size =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(mutableText), kCFStringEncodingUTF8) +
        1;
    std::string result(static_cast<size_t>(size), '\0');
    const bool ok = CFStringGetCString(mutableText, result.data(), size, kCFStringEncodingUTF8);
    CFRelease(mutableText);
    if (!ok) return std::nullopt;
    result.resize(std::char_traits<char>::length(result.c_str()));
    return result;
#else
    if (!g_utf8_validate(input.data(), static_cast<gssize>(input.size()), nullptr))
        return std::nullopt;
    gchar* normalized = g_utf8_normalize(
        input.c_str(),
        static_cast<gssize>(input.size()),
        G_NORMALIZE_DEFAULT_COMPOSE);
    if (!normalized) return std::nullopt;
    const char* first = normalized;
    const char* last = normalized + std::strlen(normalized);
    while (first < last && g_unichar_isspace(g_utf8_get_char(first)))
        first = g_utf8_next_char(first);
    while (last > first) {
        const char* previous = g_utf8_find_prev_char(normalized, last);
        if (!g_unichar_isspace(g_utf8_get_char(previous))) break;
        last = previous;
    }
    std::string result(first, last);
    g_free(normalized);
    return result;
#endif
}
} // namespace inkstow::portable
