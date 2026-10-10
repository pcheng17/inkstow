#include "Vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace
{
void Conflict(QString *error)
{
    *error = QStringLiteral(
        "Save conflict: note changed on disk. Your edits remain "
        "in the editor.");
}

bool ReadBytes(const QString &path, QByteArray *content, QString *error)
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

bool MoveToSystemTrash(const QString &path, QString *error)
{
    if (QFile::moveToTrash(path)) return true;
    *error = QStringLiteral("Could not move the note to the trash, so it was not deleted.");
    return false;
}

std::optional<Vault> Vault::Open(const QString &folder, QString *error)
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

bool Vault::ListNotes(QStringList *notes, QString *error, QStringList *folders) const
{
    QStringList found;
    QStringList discoveredFolders;
    QStringList pending{mRoot};
    while (!pending.isEmpty()) {
        QDir dir(pending.takeLast());
        if (!dir.exists() || !QFileInfo(dir.path()).isReadable()) {
            *error = QStringLiteral("Cannot list folder: %1").arg(dir.path());
            return false;
        }
        discoveredFolders.append(dir.path());
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
                found.append(QDir(mRoot).relativeFilePath(entry.filePath()).replace(u'\\', u'/'));
            }
        }
    }
    found.sort();
    *notes = found;
    if (folders) *folders = discoveredFolders;
    return true;
}

std::optional<QString> Vault::NotePath(const QString &relative, QString *error) const
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(u'\\') ||
        !relative.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
    {
        *error = QStringLiteral("Expected a relative Markdown path.");
        return std::nullopt;
    }
    QString walked = mRoot;
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
    QFileInfo info(QDir(mRoot).filePath(relative));
    QString resolved = info.canonicalFilePath();
    const QString prefix = mRoot.endsWith(u'/') ? mRoot : mRoot + u'/';
    if (!info.isFile() || !resolved.startsWith(prefix) ||
        resolved.startsWith(prefix + QStringLiteral(".obsidian/")))
    {
        *error = QStringLiteral("Note is missing or outside the vault.");
        return std::nullopt;
    }
    return resolved;
}

bool Vault::ReadNote(const QString &relative, QByteArray *content, QString *error) const
{
    const auto path = NotePath(relative, error);
    return path && ReadBytes(*path, content, error);
}

bool Vault::SaveNote(
    const QString &relative,
    const QByteArray &expected,
    const QByteArray &content,
    QString *error) const
{
    const auto path = NotePath(relative, error);
    if (!path) return false;
    QByteArray current;
    if (!ReadBytes(*path, &current, error)) return false;
    if (current != expected) {
        Conflict(error);
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
    const auto stillThere = NotePath(relative, error);
    if (!stillThere || *stillThere != *path || !ReadBytes(*path, &current, error) ||
        current != expected)
    {
        Conflict(error);
        replacement.cancelWriting();
        return false;
    }
    if (!replacement.commit()) {
        *error = replacement.errorString();
        return false;
    }
    return true;
}

bool Vault::CreateNote(const QString &relative, QString *error) const
{
    return CreateNoteWithContent(relative, QByteArray(), error);
}

bool Vault::CreateNoteWithContent(
    const QString &relative, const QByteArray &content, QString *error) const
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
    QString walked = mRoot;
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
    if (file.write(content) != content.size() || !file.flush()) {
        // Leave a partial note in place. Removing it by path after close could
        // delete another program's replacement; the editor still holds the text.
        return fail(
            QStringLiteral("Could not finish writing new note (a partial file may remain): %1")
                .arg(file.errorString()));
    }
    return true;
}

bool Vault::TrashNote(const QString &relative, const TrashFunction &trash, QString *error) const
{
    const auto path = NotePath(relative, error);
    return path && trash(*path, error);
}
