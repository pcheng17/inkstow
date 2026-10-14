#pragma once

#include "Vault.h"

#include <span>

namespace inkstow::portable
{

// UI-independent editor snapshot; callers retain edits after any failed save.
class Session
{
public:
    bool openVault(const std::string& folder, std::string* error);
    bool selectNote(const std::string& relative, std::string* error);
    bool save(std::string* error);
    void setContent(std::string content) { mContent = std::move(content); }
    bool isDirty() const { return mContent != mSavedContent; }
    const std::string& getContent() const { return mContent; }
    const std::string& getPath() const { return mPath; }
    // Borrowed view; valid until the session opens another vault or is destroyed.
    std::span<const std::string> getNotes() const { return mNotes; }

private:
    std::optional<Vault> mVault;
    std::vector<std::string> mNotes;
    std::string mPath;
    std::string mContent;
    std::string mSavedContent;
    std::string mSavedBytes;
    bool mHasBom = false;
};

} // namespace inkstow::portable
