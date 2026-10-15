#pragma once

#include "Vault.h"

#include <span>
#include <cstdint>

namespace inkstow::portable
{

enum class DiskChange : std::uint8_t
{
    None,
    Reloaded,
    Conflict,
    Missing
};

// UI-independent editor snapshot; callers retain edits after any failed save.
class Session
{
public:
    bool openVault(const std::string& folder, std::string* error);
    bool selectNote(const std::string& relative, std::string* error);
    bool save(std::string* error);
    DiskChange syncWithDisk(std::string* error);
    bool refreshNotes(std::string* error);
    bool overwriteDisk(std::string* error);
    bool reloadFromDisk(std::string* error);
    bool mergeOutsideChanges(std::string* error);
    bool saveAsNewNote(std::string* error);
    bool createNote(const std::string& relative, std::string* error);
    bool moveNote(const std::string& to, bool updateLinks, std::string* error);
    bool trashNote(std::string* error);
    bool trashNote(const Vault::TrashFunction& trash, std::string* error);
    bool followWikilink(const std::string& link, std::string* error);
    // Resolve a rendered link once; select a note or return a validated attachment for the shell.
    bool followLink(
        const std::string& link,
        std::optional<std::filesystem::path>* attachment,
        std::string* error);
    // Resolve and revalidate an attachment link before the shell opens it.
    std::optional<std::filesystem::path> followAttachment(
        const std::string& link, std::string* error) const;
    std::optional<std::filesystem::path> attachmentPath(const std::string& relative) const;
    void discardEdits() { mContent = mSavedContent; }
    void setContent(std::string content) { mContent = std::move(content); }
    bool isDirty() const { return mContent != mSavedContent; }
    bool hasVault() const { return mVault.has_value(); }
    const std::string& getContent() const { return mContent; }
    const std::string& getPath() const { return mPath; }
    // Borrowed view; valid until the session opens another vault or is destroyed.
    std::span<const std::string> getNotes() const { return mNotes; }

private:
    bool load(const std::string& path, std::string bytes, std::string* error);
    bool write(const std::string& expected, std::string* error);
    bool requireClean(std::string* error) const;
    void finishTrash();
    std::optional<Vault> mVault;
    std::vector<std::string> mNotes;
    std::string mPath;
    std::string mContent;
    std::string mSavedContent;
    std::string mSavedBytes;
    std::optional<std::string> mConflictBytes;
    bool mHasBom = false;
};

} // namespace inkstow::portable
