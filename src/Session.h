#pragma once

#include "Vault.h"

#include <cstdint>
#include <optional>

// What syncWithDisk found when comparing the note on disk with the session.
enum class DiskChange : std::uint8_t
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
        : trash_(std::move(trash))
    {
    }

    bool openVault(const QString &folder, QString *error);
    bool selectNote(const QString &path, QString *error);
    bool save(QString *error);
    // Creates and selects a note at a vault-relative path, adding ".md" when
    // missing. Moves the selected note to the trash. Both refuse while the
    // editor has unsaved edits.
    bool createNote(const QString &input, QString *error);
    bool trashNote(QString *error);
    bool reloadNotes(QString *error);
    // Reloads a clean note changed by another program. Never discards edits.
    DiskChange syncWithDisk(QString *error);
    // Conflict resolution: discard the edits for the disk version, or replace
    // the disk version with the edits. Both fail if the note is gone, and
    // overwriteDisk only replaces the version syncWithDisk last reported.
    bool reloadFromDisk(QString *error);
    bool overwriteDisk(QString *error);
    // Drops unsaved edits, e.g. after the note was deleted outside Inkstow.
    void discardEdits() { content_ = savedContent_; }
    void setContent(const QString &content) { content_ = content; }

    bool hasVault() const { return vault_.has_value(); }
    bool dirty() const { return content_ != savedContent_; }
    const QStringList &notes() const { return notes_; }
    const QStringList &folders() const { return folders_; }
    const QString &path() const { return path_; }
    const QString &content() const { return content_; }
    // Absolute path of the selected note, or empty when none is selected.
    QString notePath() const;

private:
    const Vault *requireNote(QString *error) const;
    bool requireClean(QString *error) const;
    void clearNote();
    bool write(const QByteArray &expected, QString *error);
    static bool decode(const QByteArray &bytes, QString *text, QString *error);
    void load(const QString &path, const QByteArray &bytes, const QString &text);

    TrashFunction trash_;
    std::optional<Vault> vault_;
    QStringList notes_;
    QStringList folders_;
    QString path_;
    QString content_;
    QString savedContent_;
    QByteArray savedBytes_;
    std::optional<QByteArray> conflictBytes_;
    bool hasBom_ = false;
};
