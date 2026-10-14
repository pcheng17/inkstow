#include "Session.h"

#include "GuardedReplace.h"

#include <algorithm>
#include <string_view>

namespace inkstow::portable
{
namespace
{
constexpr std::string_view kBom = "\xEF\xBB\xBF";
} // namespace

bool Session::openVault(const std::string &folder, std::string *error)
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
    mHasBom = false;
    return true;
}

bool Session::selectNote(const std::string &relative, std::string *error)
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
    const bool hasBom = bytes.compare(0, kBom.size(), kBom.data(), kBom.size()) == 0;
    const std::string text = bytes.substr(hasBom ? 3 : 0);
    if (!detail::validUtf8(text)) {
        *error = "Markdown must be valid UTF-8.";
        return false;
    }
    mPath = relative;
    mSavedBytes = std::move(bytes);
    mSavedContent = text;
    mContent = text;
    mHasBom = hasBom;
    return true;
}

bool Session::save(std::string *error)
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
    if (!mVault->saveNote(mPath, mSavedBytes, bytes, error)) return false;
    mSavedBytes = bytes;
    mSavedContent = mContent;
    return true;
}

} // namespace inkstow::portable
