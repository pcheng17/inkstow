#pragma once

#include "Vault.h"

#include <optional>
#include <stdint.h>

// What SyncWithDisk found when comparing the note on disk with the session.
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
    explicit Session(TrashFunction trash = MoveToSystemTrash)
        : mTrash(std::move(trash))
    {
    }

    bool OpenVault(const QString &folder, QString *error);
    bool SelectNote(const QString &path, QString *error);
    bool Save(QString *error);
    // Creates and selects a note at a vault-relative path, adding ".md" when
    // missing. Moves the selected note to the trash. Both refuse while the
    // editor has unsaved edits.
    bool CreateNote(const QString &input, QString *error);
    bool TrashNote(QString *error);
    bool ReloadNotes(QString *error);
    // Reloads a clean note changed by another program. Never discards edits.
    DiskChange SyncWithDisk(QString *error);
    // Conflict resolution: discard the edits for the disk version, or replace
    // the disk version with the edits. Both fail if the note is gone, and
    // OverwriteDisk only replaces the version SyncWithDisk last reported.
    bool ReloadFromDisk(QString *error);
    bool OverwriteDisk(QString *error);
    // Merges the last saved text, editor edits and the reported disk version.
    // Conflict markers remain editable; no disk write happens until Save.
    bool MergeOutsideChanges(QString *error);
    // Recreates a missing selected note without replacing any existing path.
    bool SaveAsNewNote(QString *error);
    // Drops unsaved edits, e.g. after the note was deleted outside Inkstow.
    void DiscardEdits() { mContent = mSavedContent; }
    void SetContent(const QString &content) { mContent = content; }

    bool HasVault() const { return mVault.has_value(); }
    bool Dirty() const { return mContent != mSavedContent; }
    const QStringList &Notes() const { return mNotes; }
    const QStringList &Folders() const { return mFolders; }
    const QString &Path() const { return mPath; }
    const QString &Content() const { return mContent; }
    // Absolute path of the selected note, or empty when none is selected.
    QString NotePath() const;

private:
    const Vault *RequireNote(QString *error) const;
    bool RequireClean(QString *error) const;
    void ClearNote();
    bool Write(const QByteArray &expected, QString *error);
    static bool Decode(const QByteArray &bytes, QString *text, QString *error);
    void Load(const QString &path, const QByteArray &bytes, const QString &text);

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
};
