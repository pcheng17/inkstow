#include "Vault.h"

#include "GuardedReplace.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace inkstow::portable
{
namespace detail
{
bool validUtf8(const std::string &text)
{
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            ++i;
            continue;
        }
        size_t length = 0;
        if (first >= 0xC2 && first <= 0xDF) length = 2;
        if (first >= 0xE0 && first <= 0xEF) length = 3;
        if (first >= 0xF0 && first <= 0xF4) length = 4;
        if (length == 0 || length > text.size() - i) return false;
        for (size_t j = 1; j < length; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) return false;
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90))
            return false;
        i += length;
    }
    return true;
}
} // namespace detail

namespace
{
bool markdownPath(const std::string &relative)
{
    if (!detail::validUtf8(relative) || relative.empty() || relative[0] == '/' ||
        relative.find('\\') != std::string::npos || relative.find(':') != std::string::npos ||
        relative.find('\0') != std::string::npos || relative.size() < 3)
        return false;
    std::string extension = relative.substr(relative.size() - 3);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension != ".md") return false;
    size_t start = 0;
    while (start < relative.size()) {
        const size_t end = relative.find('/', start);
        const std::string segment = relative.substr(start, end - start);
        if (segment.empty() || segment[0] == '.') return false;
        if (end == std::string::npos) break;
        start = end + 1;
        if (start == relative.size()) return false;
    }
    return true;
}

bool inside(const fs::path &root, const fs::path &path)
{
    auto a = root.begin();
    auto b = path.begin();
    for (; a != root.end(); ++a, ++b)
        if (b == path.end() || *a != *b) return false;
    return b != path.end();
}

bool readBytes(const fs::path &path, std::string *bytes, std::string *error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "Cannot read note.";
        return false;
    }
    std::string result(std::istreambuf_iterator<char>{in}, {});
    if (in.bad()) {
        *error = "Cannot finish reading note.";
        return false;
    }
    *bytes = std::move(result);
    return true;
}

bool stage(const fs::path &path, const std::string &bytes, fs::path *staged, std::string *error)
{
    std::error_code ec;
    const auto permissions = fs::status(path, ec).permissions();
    if (ec) {
        *error = ec.message();
        return false;
    }
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
#ifdef _WIN32
        const fs::path temporary = path.parent_path() / (path.filename().wstring() + L".inkstow-" +
                                                         std::to_wstring(random()));
        HANDLE file = CreateFileW(
            temporary.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) continue;
            *error = "Cannot stage replacement.";
            return false;
        }
        bool ok = true;
        size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            const DWORD count =
                static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1U << 20));
            if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || !written) {
                ok = false;
                break;
            }
            offset += written;
        }
        if (!FlushFileBuffers(file)) ok = false;
        if (!CloseHandle(file)) ok = false;
#else
        const fs::path temporary = path.parent_path() / (path.filename().string() + ".inkstow-" +
                                                         std::to_string(random()));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) -- exclusive POSIX create
        const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file < 0) {
            if (errno == EEXIST) continue;
            *error = "Cannot stage replacement.";
            return false;
        }
        bool ok = true;
        size_t offset = 0;
        while (offset < bytes.size()) {
            const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
            if (written <= 0) {
                ok = false;
                break;
            }
            offset += static_cast<size_t>(written);
        }
        if (::close(file) != 0) ok = false;
#endif
        fs::permissions(temporary, permissions, ec);
        if (ec) ok = false;
        if (!ok) {
            fs::remove(temporary, ec);
            *error = "Cannot finish staging replacement.";
            return false;
        }
        *staged = temporary;
        return true;
    }
    *error = "Cannot allocate replacement filename.";
    return false;
}
} // namespace

std::optional<Vault> Vault::open(const std::string &folder, std::string *error)
{
    if (!detail::validUtf8(folder) || folder.find('\0') != std::string::npos) {
        *error = "Expected a UTF-8 vault folder.";
        return std::nullopt;
    }
    std::error_code ec;
    const fs::path root = fs::canonical(fs::u8path(folder), ec);
    if (ec || !fs::is_directory(root)) {
        *error = "Vault must be a readable directory.";
        return std::nullopt;
    }
    return Vault(root);
}

bool Vault::listNotes(std::vector<std::string> *notes, std::string *error) const
{
    std::vector<std::string> found;
    std::error_code ec;
    fs::recursive_directory_iterator it(mRoot, fs::directory_options::none, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        const auto &entry = *it;
        const std::string name = entry.path().filename().u8string();
        const auto status = entry.symlink_status(ec);
        if (ec) break;
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            *error = "Cannot inspect vault entry.";
            return false;
        }
        const bool linked = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        const bool linked = fs::is_symlink(status);
#endif
        if (name.empty() || name[0] == '.' || linked) {
            it.disable_recursion_pending();
        } else if (fs::is_regular_file(status)) {
            const auto relative = entry.path().lexically_relative(mRoot).generic_u8string();
            if (markdownPath(relative)) found.push_back(relative);
        }
        it.increment(ec);
    }
    if (ec) {
        *error = "Cannot list vault: " + ec.message();
        return false;
    }
    std::sort(found.begin(), found.end());
    *notes = std::move(found);
    return true;
}

std::optional<fs::path> Vault::notePath(const std::string &relative, std::string *error) const
{
    if (!markdownPath(relative)) {
        *error = "Expected a visible relative Markdown path.";
        return std::nullopt;
    }
    fs::path walked = mRoot;
    std::error_code ec;
    for (const auto &segment : fs::u8path(relative)) {
        walked /= segment;
        const auto status = fs::symlink_status(walked, ec);
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(walked.c_str());
        const bool linked = attributes != INVALID_FILE_ATTRIBUTES &&
                            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        const bool linked = fs::is_symlink(status);
#endif
        if (ec || linked) {
            *error = "Note is missing or outside the vault.";
            return std::nullopt;
        }
    }
    auto resolved = fs::canonical(walked, ec);
    if (ec || !inside(mRoot, resolved) || !fs::is_regular_file(resolved)) {
        *error = "Note is missing or outside the vault.";
        return std::nullopt;
    }
    return resolved;
}

bool Vault::readNote(const std::string &relative, std::string *bytes, std::string *error) const
{
    const auto path = notePath(relative, error);
    return path && readBytes(*path, bytes, error);
}

bool Vault::saveNote(
    const std::string &relative,
    const std::string &expected,
    const std::string &bytes,
    std::string *error) const
{
    const auto path = notePath(relative, error);
    if (!path) return false;
    const auto revalidate = [this, &relative, &path](std::string *reason) {
        return notePath(relative, reason) == path;
    };
#ifdef _WIN32
    constexpr int kAttempts = 7;
#else
    constexpr int kAttempts = 1;
#endif
    return detail::guardedReplace(
        *path,
        expected,
        bytes,
        revalidate,
        detail::replaceFile,
        kAttempts,
        error);
}

namespace detail
{
ReplaceResult replaceFile(const fs::path &staged, const fs::path &destination, std::string *error)
{
#ifdef _WIN32
    if (MoveFileExW(
            staged.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return ReplaceResult::Success;
    const DWORD code = GetLastError();
    *error = "Cannot replace note (Windows error " + std::to_string(code) + ").";
    return code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION ||
                   code == ERROR_ACCESS_DENIED
               ? ReplaceResult::Retryable
               : ReplaceResult::Failure;
#else
    std::error_code ec;
    fs::rename(staged, destination, ec);
    if (!ec) return ReplaceResult::Success;
    *error = ec.message();
    return ReplaceResult::Failure;
#endif
}

bool guardedReplace(
    const fs::path &path,
    const std::string &expected,
    const std::string &bytes,
    const std::function<bool(std::string *)> &revalidate,
    const ReplaceAttempt &replace,
    int maxAttempts,
    std::string *error)
{
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        std::string current;
        if (!revalidate(error) || !readBytes(path, &current, error)) return false;
        if (current != expected) {
            *error = "Save conflict: note changed on disk. Your edits remain unsaved.";
            return false;
        }
        fs::path staged;
        if (!stage(path, bytes, &staged, error)) return false;
        // An external write can still race this final comparison and rename.
        const bool safe = revalidate(error) && readBytes(path, &current, error);
        if (!safe || current != expected) {
            std::error_code ignored;
            fs::remove(staged, ignored);
            *error = "Save conflict: note changed on disk. Your edits remain unsaved.";
            return false;
        }
        const auto outcome = replace(staged, path, error);
        if (outcome == ReplaceResult::Success) return true;
        std::error_code ignored;
        fs::remove(staged, ignored);
        if (outcome != ReplaceResult::Retryable || attempt + 1 == maxAttempts) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10 << attempt));
    }
    return false;
}
} // namespace detail
} // namespace inkstow::portable
