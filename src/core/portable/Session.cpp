#include "Session.h"

#include "GuardedReplace.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace inkstow::portable
{
namespace
{
constexpr std::string_view kBom = "\xEF\xBB\xBF";

bool markdownExtension(const std::string& path)
{
    if (path.size() < 3) return false;
    const auto suffix = path.substr(path.size() - 3);
    return std::equal(suffix.begin(), suffix.end(), ".md", [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

using Lines = std::vector<std::string>;
struct Change
{
    std::size_t mStart;
    std::size_t mEnd;
    Lines mLines;
};

Lines splitLines(std::string_view text)
{
    Lines result;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find('\n', start);
        result.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) return result;
        start = end + 1;
    }
}

std::vector<Change> changes(const Lines& base, const Lines& other)
{
    const auto n = base.size();
    const auto m = other.size();
    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1));
    for (auto i = n; i-- > 0;)
        for (auto j = m; j-- > 0;)
            lcs[i][j] = base[i] == other[j] ? 1 + lcs[i + 1][j + 1]
                                            : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    std::vector<Change> result;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && base[i] == other[j]) {
            ++i;
            ++j;
            continue;
        }
        const auto start = i;
        Lines replacement;
        while (i < n || j < m) {
            if (i < n && j < m && base[i] == other[j]) break;
            if (j < m && (i == n || lcs[i][j + 1] >= lcs[i + 1][j]))
                replacement.push_back(other[j++]);
            else
                ++i;
        }
        result.push_back({start, i, std::move(replacement)});
    }
    return result;
}

bool overlaps(const Change& a, const Change& b)
{
    if (a.mStart == a.mEnd && b.mStart == b.mEnd) return a.mStart == b.mStart;
    if (a.mStart == a.mEnd) return b.mStart <= a.mStart && a.mStart < b.mEnd;
    if (b.mStart == b.mEnd) return a.mStart <= b.mStart && b.mStart < a.mEnd;
    return a.mStart < b.mEnd && b.mStart < a.mEnd;
}

Lines apply(const Lines& base, std::size_t start, std::size_t end, const std::vector<Change>& hunks)
{
    Lines result;
    auto pos = start;
    for (const Change& hunk : hunks) {
        for (; pos < hunk.mStart; ++pos)
            result.push_back(base[pos]);
        result.insert(result.end(), hunk.mLines.begin(), hunk.mLines.end());
        pos = hunk.mEnd;
    }
    for (; pos < end; ++pos)
        result.push_back(base[pos]);
    return result;
}

std::string threeWayMerge(
    const std::string& baseText, const std::string& mineText, const std::string& outsideText)
{
    const Lines base = splitLines(baseText);
    const auto mine = changes(base, splitLines(mineText));
    const auto outside = changes(base, splitLines(outsideText));
    Lines result;
    std::size_t a = 0;
    std::size_t b = 0;
    std::size_t pos = 0;
    while (a < mine.size() || b < outside.size()) {
        const auto start = std::min(
            a < mine.size() ? mine[a].mStart : base.size(),
            b < outside.size() ? outside[b].mStart : base.size());
        for (; pos < start; ++pos)
            result.push_back(base[pos]);
        std::vector<Change> left;
        std::vector<Change> right;
        if (a < mine.size() && (b == outside.size() || mine[a].mStart <= outside[b].mStart))
            left.push_back(mine[a++]);
        else
            right.push_back(outside[b++]);
        bool expanded = true;
        while (expanded) {
            expanded = false;
            if (a < mine.size() && std::any_of(
                                       right.begin(),
                                       right.end(),
                                       [&](const Change& c) { return overlaps(mine[a], c); }))
            {
                left.push_back(mine[a++]);
                expanded = true;
            }
            if (b < outside.size() && std::any_of(
                                          left.begin(),
                                          left.end(),
                                          [&](const Change& c) { return overlaps(outside[b], c); }))
            {
                right.push_back(outside[b++]);
                expanded = true;
            }
        }
        auto end = start;
        for (const Change& c : left)
            end = std::max(end, c.mEnd);
        for (const Change& c : right)
            end = std::max(end, c.mEnd);
        const Lines l = apply(base, start, end, left);
        const Lines r = apply(base, start, end, right);
        if (left.empty())
            result.insert(result.end(), r.begin(), r.end());
        else if (right.empty() || l == r)
            result.insert(result.end(), l.begin(), l.end());
        else {
            result.push_back("<<<<<<< mine");
            result.insert(result.end(), l.begin(), l.end());
            result.push_back("=======");
            result.insert(result.end(), r.begin(), r.end());
            result.push_back(">>>>>>> outside");
        }
        pos = end;
    }
    for (; pos < base.size(); ++pos)
        result.push_back(base[pos]);
    std::string merged;
    for (std::size_t i = 0; i < result.size(); ++i) {
        if (i != 0) merged += '\n';
        merged += result[i];
    }
    return merged;
}
} // namespace

bool Session::openVault(const std::string& folder, std::string* error)
{
    if (isDirty()) {
        *error = "Save the current note before opening another vault.";
        return false;
    }
    auto next = Vault::open(folder, error);
    if (!next) return false;
    std::vector<std::string> notes;
    if (!next->listNotes(&notes, error)) return false;
    mVault = std::move(next);
    mNotes = std::move(notes);
    mPath.clear();
    mContent.clear();
    mSavedContent.clear();
    mSavedBytes.clear();
    mConflictBytes.reset();
    mHasBom = false;
    return true;
}

bool Session::selectNote(const std::string& relative, std::string* error)
{
    if (isDirty()) {
        *error = "Save the current note before switching.";
        return false;
    }
    if (!mVault || std::find(mNotes.begin(), mNotes.end(), relative) == mNotes.end()) {
        *error = "Note is not in the current vault.";
        return false;
    }
    std::string bytes;
    if (!mVault->readNote(relative, &bytes, error)) return false;
    return load(relative, std::move(bytes), error);
}

bool Session::load(const std::string& path, std::string bytes, std::string* error)
{
    const bool hasBom = bytes.compare(0, kBom.size(), kBom.data(), kBom.size()) == 0;
    const std::string text = bytes.substr(hasBom ? 3 : 0);
    if (!detail::validUtf8(text)) {
        *error = "Markdown must be valid UTF-8.";
        return false;
    }
    mPath = path;
    mSavedBytes = std::move(bytes);
    mConflictBytes.reset();
    mSavedContent = text;
    mContent = text;
    mHasBom = hasBom;
    return true;
}

DiskChange Session::syncWithDisk(std::string* error)
{
    if (!mVault || mPath.empty()) return DiskChange::None;
    std::string bytes;
    if (!mVault->readNote(mPath, &bytes, error)) return DiskChange::Missing;
    if (bytes == mSavedBytes) return DiskChange::None;
    mConflictBytes = bytes;
    if (isDirty()) {
        *error = "Note changed on disk while you have unsaved edits; reload from disk or keep your "
                 "version.";
        return DiskChange::Conflict;
    }
    return load(mPath, std::move(bytes), error) ? DiskChange::Reloaded : DiskChange::Conflict;
}

bool Session::reloadFromDisk(std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "No note selected.";
        return false;
    }
    std::string bytes;
    return mVault->readNote(mPath, &bytes, error) && load(mPath, std::move(bytes), error);
}

bool Session::mergeOutsideChanges(std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "No note selected.";
        return false;
    }
    if (!mConflictBytes || !isDirty()) {
        *error = "No unsaved edits and reported outside change to merge.";
        return false;
    }
    std::string current;
    if (!mVault->readNote(mPath, &current, error)) return false;
    const bool outsideBom = current.compare(0, kBom.size(), kBom.data(), kBom.size()) == 0;
    const std::string outside = current.substr(outsideBom ? kBom.size() : 0);
    if (!detail::validUtf8(outside)) {
        *error = "Markdown must be valid UTF-8.";
        return false;
    }
    if (current != *mConflictBytes) {
        *error = "Outside note changed again; refresh before merging.";
        return false;
    }
    if (!detail::validUtf8(mContent)) {
        *error = "Markdown must be valid UTF-8.";
        return false;
    }
    const auto baseLines = splitLines(mSavedContent).size();
    const auto mineLines = splitLines(mContent).size();
    const auto outsideLines = splitLines(outside).size();
    // The LCS table grows quadratically. Refuse oversized notes before allocating it.
    constexpr std::size_t kMaxCells = 2000000;
    if (baseLines > kMaxCells / mineLines || baseLines > kMaxCells / outsideLines) {
        *error = "Note is too large for the line merge; edits are unchanged.";
        return false;
    }
    const std::string merged = threeWayMerge(mSavedContent, mContent, outside);
    mSavedBytes = std::move(current);
    mSavedContent = outside;
    mHasBom = outsideBom;
    mContent = merged;
    mConflictBytes.reset();
    return true;
}

bool Session::requireClean(std::string* error) const
{
    if (!isDirty()) return true;
    *error = "Save or discard your edits first.";
    return false;
}

bool Session::refreshNotes(std::string* error)
{
    if (!mVault) {
        *error = "Open a vault first.";
        return false;
    }
    std::vector<std::string> notes;
    if (!mVault->listNotes(&notes, error)) return false;
    mNotes = std::move(notes);
    return true;
}

bool Session::createNote(const std::string& relative, std::string* error)
{
    if (!requireClean(error)) return false;
    if (!mVault) {
        *error = "Open a vault first.";
        return false;
    }
    const std::string path = markdownExtension(relative) ? relative : relative + ".md";
    if (!mVault->createNote(path, error)) return false;
    if (!refreshNotes(error)) return false;
    return selectNote(path, error);
}

bool Session::moveNote(const std::string& to, bool updateLinks, std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "Select a note first.";
        return false;
    }
    if (!requireClean(error)) return false;
    const std::string path = markdownExtension(to) ? to : to + ".md";
    const bool moved = mVault->moveNote(mPath, path, mSavedBytes, updateLinks, error);
    const bool partial = !moved && error->starts_with("Move partially completed");
    if (!moved && !partial) return false;
    const std::string failure = *error;
    mPath = path; // The rename succeeded, even if reloading a later rewrite fails.
    std::string ignored;
    refreshNotes(&ignored);
    std::string current;
    if (!mVault->readNote(path, &current, error) || !load(path, std::move(current), error)) {
        *error = "Move partially completed; check the moved note: " + *error;
        return false;
    }
    if (partial) *error = failure;
    return moved;
}

bool Session::trashNote(const Vault::TrashFunction& trash, std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "Select a note first.";
        return false;
    }
    if (!requireClean(error) || !mVault->trashNote(mPath, trash, error)) return false;
    finishTrash();
    return true;
}

bool Session::trashNote(std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "Select a note first.";
        return false;
    }
    if (!requireClean(error) || !mVault->trashNote(mPath, error)) return false;
    finishTrash();
    return true;
}

void Session::finishTrash()
{
    std::string ignored;
    if (!refreshNotes(&ignored)) std::erase(mNotes, mPath);
    mPath.clear();
    mContent.clear();
    mSavedContent.clear();
    mSavedBytes.clear();
    mConflictBytes.reset();
    mHasBom = false;
}

bool Session::followWikilink(const std::string& link, std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "Select a note first.";
        return false;
    }
    const auto target = mVault->resolveWikilink(link, mPath);
    if (!target || !markdownExtension(*target)) {
        *error = "Link does not point to a supported Markdown note.";
        return false;
    }
    if (!requireClean(error)) return false;
    if (!refreshNotes(error)) return false;
    return selectNote(*target, error);
}

std::optional<std::filesystem::path> Session::attachmentPath(const std::string& relative) const
{
    if (!mVault || mPath.empty() || isDirty()) return std::nullopt;
    return mVault->attachmentPath(relative);
}

bool Session::saveAsNewNote(std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "No note selected.";
        return false;
    }
    if (!detail::validUtf8(mContent)) {
        *error = "Markdown must be valid UTF-8.";
        return false;
    }
    const std::string bytes = (mHasBom ? std::string(kBom) : std::string()) + mContent;
    if (!mVault->createNoteWithContent(mPath, bytes, error)) return false;
    mSavedBytes = bytes;
    mSavedContent = mContent;
    mConflictBytes.reset();
    return true;
}

bool Session::overwriteDisk(std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "No note selected.";
        return false;
    }
    if (!mConflictBytes) {
        *error = "No outside change to replace.";
        return false;
    }
    return write(*mConflictBytes, error);
}

bool Session::save(std::string* error)
{
    return write(mSavedBytes, error);
}

bool Session::write(const std::string& expected, std::string* error)
{
    if (!mVault || mPath.empty()) {
        *error = "No note selected.";
        return false;
    }
    if (!detail::validUtf8(mContent)) {
        *error = "Markdown must be valid UTF-8.";
        return false;
    }
    const std::string bytes = (mHasBom ? std::string(kBom) : std::string()) + mContent;
    if (!mVault->saveNote(mPath, expected, bytes, error)) return false;
    mSavedBytes = bytes;
    mSavedContent = mContent;
    mConflictBytes.reset();
    return true;
}

} // namespace inkstow::portable
