#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

// Moves the file at an absolute path to a trash; on failure, sets error and
// leaves the file in place.
using TrashFunction = std::function<bool(const QString &path, QString *error)>;
bool MoveToSystemTrash(const QString &path, QString *error);

// Filesystem-only vault operations. Paths are relative to the vault and use
// '/'.
class Vault
{
public:
    static std::optional<Vault> Open(const QString &folder, QString *error);

    const QString &Root() const { return mRoot; }
    // Optionally returns every discoverable folder, including empty folders and the root.
    bool ListNotes(QStringList *notes, QString *error, QStringList *folders = nullptr) const;
    bool ReadNote(const QString &relative, QByteArray *content, QString *error) const;
    // The disk bytes must still equal expected. The replacement is staged in the
    // same directory and renamed into place; other programs can race the final
    // check.
    bool SaveNote(
        const QString &relative,
        const QByteArray &expected,
        const QByteArray &content,
        QString *error) const;
    // Creates an empty note and any missing folders. Refuses existing files,
    // links, and dot-prefixed names, which discovery would hide.
    bool CreateNote(const QString &relative, QString *error) const;
    // Creates a new note with bytes in one non-overwriting operation.
    bool CreateNoteWithContent(
        const QString &relative, const QByteArray &content, QString *error) const;
    bool TrashNote(const QString &relative, const TrashFunction &trash, QString *error) const;

private:
    explicit Vault(QString root)
        : mRoot(std::move(root))
    {
    }
    std::optional<QString> NotePath(const QString &relative, QString *error) const;

    QString mRoot;
};
