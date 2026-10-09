#include "Vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace
{
void conflict(QString *error)
{
    *error = QStringLiteral(
        "Save conflict: note changed on disk. Your edits remain "
        "in the editor.");
}

bool readBytes(const QString &path, QByteArray *content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    *content = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        *error = file.errorString();
        return false;
    }
    return true;
}
} // namespace

bool moveToSystemTrash(const QString &path, QString *error)
{
    if (QFile::moveToTrash(path)) return true;
    *error = QStringLiteral("Could not move the note to the trash, so it was not deleted.");
    return false;
}

std::optional<Vault> Vault::open(const QString &folder, QString *error)
{
    QFileInfo info(folder);
    if (!info.isDir()) {
        *error = QStringLiteral("Vault must be a directory.");
        return std::nullopt;
    }
    const QString root = info.canonicalFilePath();
    if (root.isEmpty()) {
        *error = QStringLiteral("Cannot resolve vault directory.");
        return std::nullopt;
    }
    return Vault(root);
}

bool Vault::listNotes(QStringList *notes, QString *error) const
{
    QStringList found;
    QStringList pending{root_};
    while (!pending.isEmpty()) {
        QDir dir(pending.takeLast());
        if (!dir.exists() || !QFileInfo(dir.path()).isReadable()) {
            *error = QStringLiteral("Cannot list folder: %1").arg(dir.path());
            return false;
        }
        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().startsWith(u'.') || entry.isSymLink()) continue;
            if (entry.isDir()) {
                pending.append(entry.filePath());
            } else if (
                entry.isFile() &&
                entry.suffix().compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0)
            {
                found.append(QDir(root_).relativeFilePath(entry.filePath()).replace(u'\\', u'/'));
            }
        }
    }
    found.sort();
    *notes = found;
    return true;
}

std::optional<QString> Vault::notePath(const QString &relative, QString *error) const
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(u'\\') ||
        !relative.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
    {
        *error = QStringLiteral("Expected a relative Markdown path.");
        return std::nullopt;
    }
    QString walked = root_;
    for (const QString &segment : relative.split(u'/')) {
        if (segment.isEmpty() || segment == QStringLiteral(".") || segment == QStringLiteral(".."))
        {
            *error = QStringLiteral("Expected a relative Markdown path.");
            return std::nullopt;
        }
        // Discovery skips links, so a link here means the note was swapped.
        walked = QDir(walked).filePath(segment);
        if (QFileInfo(walked).isSymLink()) {
            *error = QStringLiteral("Note is missing or outside the vault.");
            return std::nullopt;
        }
    }
    QFileInfo info(QDir(root_).filePath(relative));
    const QString resolved = info.canonicalFilePath();
    const QString prefix = root_.endsWith(u'/') ? root_ : root_ + u'/';
    if (!info.isFile() || !resolved.startsWith(prefix) ||
        resolved.startsWith(prefix + QStringLiteral(".obsidian/")))
    {
        *error = QStringLiteral("Note is missing or outside the vault.");
        return std::nullopt;
    }
    return resolved;
}

bool Vault::readNote(const QString &relative, QByteArray *content, QString *error) const
{
    const auto path = notePath(relative, error);
    return path && readBytes(*path, content, error);
}

bool Vault::saveNote(
    const QString &relative,
    const QByteArray &expected,
    const QByteArray &content,
    QString *error) const
{
    const auto path = notePath(relative, error);
    if (!path) return false;
    QByteArray current;
    if (!readBytes(*path, &current, error)) return false;
    if (current != expected) {
        conflict(error);
        return false;
    }

    QSaveFile replacement(*path);
    replacement.setDirectWriteFallback(false);
    if (!replacement.open(QIODevice::WriteOnly)) {
        *error = replacement.errorString();
        return false;
    }
    if (!replacement.setPermissions(QFileInfo(*path).permissions()) ||
        replacement.write(content) != content.size())
    {
        *error = replacement.errorString();
        replacement.cancelWriting();
        return false;
    }
    const auto stillThere = notePath(relative, error);
    if (!stillThere || *stillThere != *path || !readBytes(*path, &current, error) ||
        current != expected)
    {
        conflict(error);
        replacement.cancelWriting();
        return false;
    }
    if (!replacement.commit()) {
        *error = replacement.errorString();
        return false;
    }
    return true;
}

bool Vault::createNote(const QString &relative, QString *error) const
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(u'\\') ||
        !relative.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
    {
        *error = QStringLiteral("Expected a relative Markdown path.");
        return false;
    }
    const QStringList segments = relative.split(u'/');
    // Checked up front so a rejected path creates no folders. Covers "." and "..".
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment.startsWith(u'.')) {
            *error = QStringLiteral("Note and folder names can't be empty or start with '.'.");
            return false;
        }
    }
    QString walked = root_;
    QStringList created;
    const auto fail = [&created, error](const QString &message) {
        for (auto folder = created.crbegin(); folder != created.crend(); ++folder)
            QDir().rmdir(*folder);
        *error = message;
        return false;
    };
    for (qsizetype i = 0; i < segments.size(); ++i) {
        walked = QDir(walked).filePath(segments[i]);
        const QFileInfo info(walked);
        if (info.isSymLink())
            return fail(QStringLiteral("Cannot create a note through a link: %1").arg(relative));
        if (i + 1 == segments.size() || (info.exists() && info.isDir())) continue;
        if (info.exists() || !QDir().mkdir(walked))
            return fail(QStringLiteral("Cannot create folder: %1").arg(walked));
        created.append(walked);
    }
    // NewOnly fails instead of replacing a file created since the checks above.
    QFile file(walked);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return fail(
            QFileInfo::exists(walked) ? QStringLiteral("A file already exists at %1.").arg(relative)
                                      : file.errorString());
    }
    return true;
}

bool Vault::trashNote(const QString &relative, const TrashFunction &trash, QString *error) const
{
    const auto path = notePath(relative, error);
    return path && trash(*path, error);
}
