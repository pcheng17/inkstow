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
bool moveToSystemTrash(const QString &path, QString *error);

// Filesystem-only vault operations. Paths are relative to the vault and use
// '/'.
class Vault
{
public:
    static std::optional<Vault> open(const QString &folder, QString *error);

    const QString &root() const { return root_; }
    bool listNotes(QStringList *notes, QString *error) const;
    bool readNote(const QString &relative, QByteArray *content, QString *error) const;
    // The disk bytes must still equal expected. The replacement is staged in the
    // same directory and renamed into place; other programs can race the final
    // check.
    bool saveNote(
        const QString &relative,
        const QByteArray &expected,
        const QByteArray &content,
        QString *error) const;
    // Creates an empty note and any missing folders. Refuses existing files,
    // links, and dot-prefixed names, which discovery would hide.
    bool createNote(const QString &relative, QString *error) const;
    bool trashNote(const QString &relative, const TrashFunction &trash, QString *error) const;

private:
    explicit Vault(QString root)
        : root_(std::move(root))
    {
    }
    std::optional<QString> notePath(const QString &relative, QString *error) const;

    QString root_;
};
