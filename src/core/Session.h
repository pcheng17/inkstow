#pragma once

#include "Vault.h"

#include <optional>
#include <stdint.h>

// What syncWithDisk found when comparing the note on disk with the session.
enum class DiskChange : uint8_t
{
    None,     // Disk still holds the last loaded or saved bytes.
    Reloaded, // The editor was clean, so it now holds the new disk text.
    Conflict, // Disk changed but could not be loaded; editor text is kept.
    Missing,  // The note can no longer be read; editor text is kept.
};

// UI-independent state, including the last disk bytes used for conflict checks.
class Session
{
public:
    explicit Session(TrashFunction trash = moveToSystemTrash)
        : mTrash(std::move(trash))
    {
    }

    // Refuses while the selected note has unsaved changes.
    bool canOpenVault(QString *error) const;
    bool openVault(const QString &folder, QString *error);
    bool selectNote(const QString &path, QString *error);
    bool save(QString *error);
    // Creates and selects a note at a vault-relative path, adding ".md" when
    // missing. Moves the selected note to the trash. Both refuse while the
    // editor has unsaved edits.
    bool createNote(const QString &input, QString *error);
    bool trashNote(QString *error);
    bool moveNote(const QString &input, bool updateLinks, QString *error);
    bool reloadNotes(QString *error);
    // Reloads a clean note changed by another program. Never discards edits.
    DiskChange syncWithDisk(QString *error);
    // Conflict resolution: discard the edits for the disk version, or replace
    // the disk version with the edits. Both fail if the note is gone, and
    // overwriteDisk only replaces the version syncWithDisk last reported.
    bool reloadFromDisk(QString *error);
    bool overwriteDisk(QString *error);
    // Merges the last saved text, editor edits and the reported disk version.
    // Conflict markers remain editable; no disk write happens until save.
    bool mergeOutsideChanges(QString *error);
    // Recreates a missing selected note without replacing any existing path.
    bool saveAsNewNote(QString *error);
    // Drops unsaved edits, e.g. after the note was deleted outside Inkstow.
    void discardEdits() { mContent = mSavedContent; }
    void setContent(const QString &content) { mContent = content; }

    bool hasVault() const { return mVault.has_value(); }
    bool dirty() const { return mContent != mSavedContent; }
    const QStringList &notes() const { return mNotes; }
    // Vault-relative folders below the root, including empty ones.
    const QStringList &folders() const { return mFolders; }
    // Absolute vault folder, or empty when no vault is open.
    QString vaultRoot() const { return mVault ? mVault->root() : QString(); }
    const QString &path() const { return mPath; }
    const QString &content() const { return mContent; }
    std::optional<QString> resolveWikilink(const QString &link) const
    {
        return mVault && !mPath.isEmpty() ? mVault->resolveWikilink(link, mPath) : std::nullopt;
    }
    std::optional<QString> attachmentPath(const QString &relative) const
    {
        return mVault ? mVault->attachmentPath(relative) : std::nullopt;
    }
    // Absolute path of the selected note, or empty when none is selected.
    QString notePath() const;

private:
    const Vault *requireNote(QString *error) const;
    bool requireClean(QString *error) const;
    void clearNote();
    QString discoveredPath(const QString &requested) const;
    bool write(const QByteArray &expected, QString *error);
    static bool decode(const QByteArray &bytes, QString *text, QString *error);
    void load(const QString &path, const QByteArray &bytes, const QString &text);
    QByteArray encodedContent() const;

    TrashFunction mTrash;
    std::optional<Vault> mVault;
    QStringList mNotes;
    QStringList mFolders;
    QString mPath;
    QString mContent;
    QString mSavedContent;
    QByteArray mSavedBytes;
    std::optional<QByteArray> mConflictBytes;
    bool mHasBom = false;
    QString mLineEnding = QStringLiteral("\n");
};
