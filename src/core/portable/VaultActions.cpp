#include "Vault.h"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif

namespace fs = std::filesystem;
namespace inkstow::portable
{
namespace
{
bool linkedPath(const fs::path& path, const fs::file_status& status)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    (void)path;
    return fs::is_symlink(status);
#endif
}
std::string bytes(const fs::path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
fs::path pathOf(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}
std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
bool markdown(const std::string& path)
{
    return path.size() >= 3 && lower(path.substr(path.size() - 3)) == ".md";
}
bool safeRelative(const std::string& path)
{
    if (path.empty() || path[0] == '/' || path.find_first_of("\\:") != std::string::npos ||
        path.find('\0') != std::string::npos || !detail::validUtf8(path))
        return false;
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find('/', start);
        if (path[start] == '.' || end == start) return false;
        if (end == std::string::npos) return true;
        start = end + 1;
    }
    return false;
}
std::string basename(const std::string& path)
{
    return path.substr(
        path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
}
std::vector<std::string> files(const fs::path& root)
{
    std::vector<std::string> found;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::none, ec);
    while (!ec && it != fs::recursive_directory_iterator{}) {
        const auto status = it->symlink_status(ec);
        if (ec) break;
        const auto name = bytes(it->path().filename());
        if (name.empty() || name[0] == '.' || linkedPath(it->path(), status))
            it.disable_recursion_pending();
        else if (fs::is_regular_file(status))
            found.push_back(bytes(it->path().lexically_relative(root)));
        it.increment(ec);
    }
    if (ec) return {};
    std::sort(found.begin(), found.end());
    return found;
}
std::optional<std::string> resolve(
    const std::string& link, const std::string& source, const std::vector<std::string>& found)
{
    std::string path = link.substr(0, link.find_first_of("|#"));
    const bool rootOnly = !path.empty() && path[0] == '/';
    if (rootOnly) path.erase(0, 1);
    if (!safeRelative(path)) return std::nullopt;
    const std::string folder =
        source.find('/') == std::string::npos ? "" : source.substr(0, source.find_last_of('/') + 1);
    const std::vector<std::string> spellings = markdown(path)
                                                   ? std::vector<std::string>{path}
                                                   : std::vector<std::string>{path, path + ".md"};
    for (const auto& spelling : spellings) {
        if (!rootOnly && std::ranges::find(found, folder + spelling) != found.end())
            return folder + spelling;
        if (std::ranges::find(found, spelling) != found.end()) return spelling;
    }
    for (const auto& spelling : spellings) {
        if (path.find('/') != std::string::npos) {
            if (!rootOnly)
                for (const auto& file : found)
                    if (lower(file) == lower(folder + spelling)) return file;
            for (const auto& file : found)
                if (lower(file) == lower(spelling)) return file;
            continue;
        }
        if (rootOnly) continue;
        std::vector<std::string> matches;
        for (const auto& file : found)
            if (lower(basename(file)) == lower(spelling)) matches.push_back(file);
        if (matches.empty()) continue;
        std::sort(matches.begin(), matches.end(), [&](const auto& a, const auto& b) {
            const bool nearA = a.starts_with(folder);
            const bool nearB = b.starts_with(folder);
            if (nearA != nearB) return nearA;
            if (a.size() != b.size()) return a.size() < b.size();
            return a < b;
        });
        return matches.front();
    }
    return std::nullopt;
}
bool safeLink(const std::string& path)
{
    return path.find_first_of("#|[]\n\r") == std::string::npos;
}

// Discovery omits hidden and linked entries; either could shadow a rewritten link.
bool unshadowedLinkPath(
    const fs::path& root, const std::string& linkPath, const std::string& note, std::string* error)
{
    const auto shadow = root / pathOf(linkPath);
    std::error_code ec;
    fs::directory_iterator siblings(shadow.parent_path(), ec);
    if (ec) {
        *error = "Cannot inspect link target in " + note;
        return false;
    }
    const fs::directory_iterator end;
    while (!ec && siblings != end) {
        if (lower(bytes(siblings->path().filename())) == lower(bytes(shadow.filename()))) {
            *error = "Cannot write an unambiguous extensionless link in " + note;
            return false;
        }
        siblings.increment(ec);
    }
    if (ec) {
        *error = "Cannot inspect link target in " + note;
        return false;
    }
    return true;
}
} // namespace

bool Vault::createNote(const std::string& relative, std::string* error) const
{
    if (!safeRelative(relative) || !markdown(relative)) {
        *error = "Expected a visible relative Markdown path.";
        return false;
    }
    fs::path walked = mRoot;
    std::vector<fs::path> created;
    std::error_code ec;
    bool parentsReady = true;
    for (const auto& part : pathOf(relative).parent_path()) {
        walked /= part;
        const auto status = fs::symlink_status(walked, ec);
        if (status.type() == fs::file_type::not_found && ec == std::errc::no_such_file_or_directory)
        {
            ec.clear();
            if (fs::create_directory(walked, ec)) {
                created.push_back(walked);
                continue;
            }
        } else if (!ec && fs::is_directory(status) && !linkedPath(walked, status))
            continue;
        parentsReady = false;
        *error = "Cannot create note parent folder (missing, linked or inaccessible).";
        break;
    }
    const bool ok = parentsReady && createNoteWithContent(relative, "", error);
    if (!ok) {
        for (auto it = created.rbegin(); it != created.rend(); ++it) {
            std::error_code ignored;
            fs::remove(*it, ignored);
        }
    }
    return ok;
}

std::optional<std::string> Vault::resolveWikilink(
    const std::string& link, const std::string& source) const
{
    std::string error;
    if (!notePath(source, &error)) return std::nullopt;
    return resolve(link, source, files(mRoot));
}

std::optional<fs::path> Vault::attachmentPath(const std::string& relative) const
{
    if (!safeRelative(relative) || markdown(relative)) return std::nullopt;
    fs::path walked = mRoot;
    std::error_code ec;
    for (const auto& part : pathOf(relative)) {
        walked /= part;
        const auto status = fs::symlink_status(walked, ec);
        if (ec || linkedPath(walked, status)) return std::nullopt;
    }
    auto canonical = fs::canonical(walked, ec);
    if (ec || !fs::is_regular_file(canonical)) return std::nullopt;
    const auto rel = canonical.lexically_relative(mRoot);
    if (rel.empty() || *rel.begin() == "..") return std::nullopt;
    return canonical;
}

bool Vault::moveNote(
    const std::string& from,
    const std::string& to,
    const std::string& expected,
    bool updateLinks,
    std::string* error) const
{
    const auto source = notePath(from, error);
    if (!source) return false;
    if (!safeRelative(to) || !markdown(to) || to == from) {
        *error = "Expected a different visible relative Markdown path.";
        return false;
    }
    fs::path parent = mRoot;
    std::error_code ec;
    for (const auto& part : pathOf(to).parent_path()) {
        parent /= part;
        const auto status = fs::symlink_status(parent, ec);
        if (ec || !fs::is_directory(status) || linkedPath(parent, status)) {
            *error = "Destination folder must already exist and not be a link.";
            return false;
        }
    }
    const auto destination = parent / pathOf(to).filename();
    if (fs::exists(destination, ec) || fs::is_symlink(fs::symlink_status(destination, ec))) {
        *error = "Destination already exists.";
        return false;
    }
    std::string current;
    if (!readNote(from, &current, error)) return false;
    if (current != expected) {
        *error = "Save conflict: note changed on disk.";
        return false;
    }
    struct Rewrite
    {
        std::string mPath;
        std::string mBefore;
        std::string mAfter;
    };
    std::vector<Rewrite> rewrites;
    std::vector<std::string> snapshot;
    if (updateLinks) {
        if (!safeLink(to) || markdown(to.substr(0, to.size() - 3))) {
            *error = "Destination cannot be represented by an extensionless wikilink.";
            return false;
        }
        snapshot = files(mRoot);
        std::vector<std::string> notes;
        if (!listNotes(&notes, error)) return false;
        for (const auto& note : notes) {
            std::string text;
            if (!readNote(note, &text, error)) return false;
            const std::string before = text;
            // UTF-8 validation also rejects invalid bytes in notes without affected links.
            // readNote is byte-oriented, so validate using the existing editor's strict decoder.
            if (!detail::validUtf8(text)) {
                *error = "Cannot safely update links in invalid UTF-8 note: " + note;
                return false;
            }
            const std::string oldName = basename(from).substr(0, basename(from).size() - 3);
            const std::string newName = basename(to).substr(0, basename(to).size() - 3);
            size_t cursor = 0;
            size_t count = 0;
            std::string output;
            size_t start = text.find("[[", cursor);
            while (start != std::string::npos) {
                const size_t end = text.find("]]", start + 2);
                if (end == std::string::npos ||
                    text.substr(start + 2, end - start - 2).find_first_of("[]") !=
                        std::string::npos)
                {
                    *error = "Unsupported wikilink syntax in " + note;
                    return false;
                }
                const std::string link = text.substr(start + 2, end - start - 2);
                if (link.empty() || link.find_first_of("\n\r") != std::string::npos) {
                    *error = "Unsupported wikilink syntax in " + note;
                    return false;
                }
                const auto bare = basename(link.substr(0, link.find_first_of("|#")));
                const bool relevant = note == from || lower(bare) == lower(oldName) ||
                                      lower(bare) == lower(newName) ||
                                      lower(bare) == lower(oldName + ".md") ||
                                      lower(bare) == lower(newName + ".md");
                output += text.substr(cursor, start - cursor);
                if (relevant) {
                    if (link.find_first_of("|#") != std::string::npos || link.starts_with('.') ||
                        (start > 0 && text[start - 1] == '!'))
                    {
                        *error = "Complex wikilink syntax needs manual review in " + note;
                        return false;
                    }
                    const auto target = resolve(link, note, snapshot);
                    if (!target || !markdown(*target)) {
                        *error = "Unsupported or unresolved link needs manual review in " + note;
                        return false;
                    }
                    const auto mapped = *target == from ? to : *target;
                    const auto linkPath = mapped.substr(0, mapped.size() - 3);
                    if (!safeLink(mapped) || markdown(linkPath)) {
                        *error = "Cannot write an unambiguous extensionless link in " + note;
                        return false;
                    }
                    if (!unshadowedLinkPath(mRoot, linkPath, note, error)) return false;
                    output += "[[/" + linkPath + "]]";
                } else
                    output += text.substr(start, end + 2 - start);
                cursor = end + 2;
                ++count;
                start = text.find("[[", cursor);
            }
            size_t opens = 0;
            for (size_t pos = text.find("[["); pos != std::string::npos;
                 pos = text.find("[[", pos + 2))
                ++opens;
            size_t closes = 0;
            for (size_t pos = text.find("]]"); pos != std::string::npos;
                 pos = text.find("]]", pos + 2))
                ++closes;
            if (count != opens || count != closes) {
                *error = "Unsupported wikilink syntax in " + note;
                return false;
            }
            output += text.substr(cursor);
            if (output != before) rewrites.push_back({note, before, output});
        }
    }
    const bool unchanged = std::ranges::all_of(rewrites, [&](const Rewrite& rewrite) {
        return readNote(rewrite.mPath, &current, error) && current == rewrite.mBefore;
    });
    if (!unchanged) {
        *error = "Save conflict: note changed on disk.";
        return false;
    }
    if (!readNote(from, &current, error) || current != expected ||
        (updateLinks && files(mRoot) != snapshot))
    {
        *error = "Vault files or note changed while preparing link updates; retry the move.";
        return false;
    }
    // rename must not overwrite a newly appeared destination.
    if (fs::exists(destination, ec) || fs::is_symlink(fs::symlink_status(destination, ec))) {
        *error = "Destination already exists.";
        return false;
    }
#ifdef _WIN32
    const bool moved = MoveFileW(source->c_str(), destination.c_str()) != 0;
#elif defined(__linux__)
    // renameat2 refuses a destination created after the preflight check.
    const bool moved =
        ::renameat2(AT_FDCWD, source->c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) ==
        0;
#elif defined(__APPLE__)
    const bool moved = renamex_np(source->c_str(), destination.c_str(), RENAME_EXCL) == 0;
#endif
    if (!moved) {
        *error = "Could not move note (destination may have appeared).";
        return false;
    }
    const bool saved = std::ranges::all_of(rewrites, [&](const Rewrite& rewrite) {
        const auto& path = rewrite.mPath == from ? to : rewrite.mPath;
        return saveNote(path, rewrite.mBefore, rewrite.mAfter, error);
    });
    if (!saved) *error = "Move partially completed; some links may need repair: " + *error;
    return saved;
}

bool Vault::trashNote(
    const std::string& relative, const TrashFunction& trash, std::string* error) const
{
    const auto path = notePath(relative, error);
    return path && trash(*path, error);
}
} // namespace inkstow::portable
