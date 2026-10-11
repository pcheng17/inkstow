#include "Vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegularExpression>
#include <QStringDecoder>

#include <vector>

#include <algorithm>

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

bool Vault::listNotes(QStringList *notes, QString *error, QStringList *folders) const
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

std::optional<QString> Vault::notePath(const QString &relative, QString *error) const
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

std::optional<QString> Vault::resolveWikilink(const QString &link, const QString &source) const
{
    QString error;
    if (!notePath(source, &error)) return std::nullopt;
    QString path = link.section(u'|', 0, 0).section(u'#', 0, 0);
    const bool rootOnly = path.startsWith(u'/');
    if (rootOnly) path.remove(0, 1);
    if (path.isEmpty() || path.contains(u'\\')) return std::nullopt;
    for (const QString &part : path.split(u'/')) {
        if (part.isEmpty() || part.startsWith(u'.') || part == QStringLiteral(".."))
            return std::nullopt;
    }

    // Discover only ordinary visible files. Never traverse symlinked folders or
    // return linked files, even if they point back into this vault.
    QStringList files;
    QStringList pending{mRoot};
    while (!pending.isEmpty()) {
        QDir dir(pending.takeLast());
        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().startsWith(u'.') || entry.isSymLink()) continue;
            if (entry.isDir())
                pending.append(entry.filePath());
            else if (entry.isFile())
                files.append(QDir(mRoot).relativeFilePath(entry.filePath()).replace(u'\\', u'/'));
        }
    }
    const QStringList spellings = path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
                                      ? QStringList{path}
                                      : QStringList{path, path + QStringLiteral(".md")};
    const QString sourceFolder =
        source.contains(u'/') ? source.left(source.lastIndexOf(u'/')) + u'/' : QString();
    for (const QString &spelling : spellings) {
        if (!rootOnly && files.contains(sourceFolder + spelling)) return sourceFolder + spelling;
        if (files.contains(spelling)) return spelling;
    }
    // A qualified path must match in full: never send a miss in one folder
    // to an unrelated file with the same basename elsewhere.
    if (path.contains(u'/')) {
        for (const QString &spelling : spellings) {
            if (!rootOnly) {
                for (const QString &file : files)
                    if (file.compare(sourceFolder + spelling, Qt::CaseInsensitive) == 0)
                        return file;
            }
            for (const QString &file : files)
                if (file.compare(spelling, Qt::CaseInsensitive) == 0) return file;
        }
        return std::nullopt;
    }
    if (rootOnly) return std::nullopt;

    // Basename lookup is case-insensitive. Prefer nearby files, then shorter
    // paths, then a stable lexical order for equally ranked duplicates.
    for (const QString &spelling : spellings) {
        const QString name = spelling.section(u'/', -1);
        QStringList matches;
        for (const QString &file : files) {
            if (file.section(u'/', -1).compare(name, Qt::CaseInsensitive) == 0)
                matches.append(file);
        }
        if (matches.isEmpty()) continue;
        std::sort(
            matches.begin(),
            matches.end(),
            [&sourceFolder](const QString &a, const QString &b) {
                const bool aNear = a.startsWith(sourceFolder);
                const bool bNear = b.startsWith(sourceFolder);
                if (aNear != bNear) return aNear;
                if (a.size() != b.size()) return a.size() < b.size();
                return a < b;
            });
        return matches.first();
    }
    return std::nullopt;
}

std::optional<QString> Vault::attachmentPath(const QString &relative) const
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(u'\\') ||
        relative.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        return std::nullopt;
    QString walked = mRoot;
    for (const QString &segment : relative.split(u'/')) {
        if (segment.isEmpty() || segment.startsWith(u'.')) return std::nullopt;
        walked = QDir(walked).filePath(segment);
        if (QFileInfo(walked).isSymLink()) return std::nullopt;
    }
    const QFileInfo info(walked);
    const QString prefix = mRoot.endsWith(u'/') ? mRoot : mRoot + u'/';
    QString canonical = info.canonicalFilePath();
    if (!info.isFile() || !canonical.startsWith(prefix)) return std::nullopt;
    return canonical;
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
    return createNoteWithContent(relative, QByteArray(), error);
}

bool Vault::createNoteWithContent(
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

bool Vault::moveNote(
    const QString &from,
    const QString &to,
    const QByteArray &expected,
    bool updateLinks,
    QString *error) const
{
    const auto source = notePath(from, error);
    if (!source) return false;
    const QStringList parts = to.split(u'/');
    if (to.isEmpty() || QDir::isAbsolutePath(to) || to.contains(u'\\') ||
        !to.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ||
        std::any_of(
            parts.begin(),
            parts.end(),
            [](const QString &part) { return part.isEmpty() || part.startsWith(u'.'); }) ||
        from == to)
    {
        *error = QStringLiteral("Expected a different visible relative Markdown path.");
        return false;
    }
    QString parent = mRoot;
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        parent = QDir(parent).filePath(parts[i]);
        if (QFileInfo(parent).isSymLink() || !QFileInfo(parent).isDir()) {
            *error = QStringLiteral("Destination folder must already exist and not be a link.");
            return false;
        }
    }
    const QString destination = QDir(parent).filePath(parts.last());
    if (QFileInfo(destination).exists() || QFileInfo(destination).isSymLink()) {
        *error = QStringLiteral("Destination already exists.");
        return false;
    }
    QByteArray current;
    if (!readBytes(*source, &current, error)) return false;
    if (current != expected) {
        conflict(error);
        return false;
    }
    struct Rewrite
    {
        QString mPath;
        QByteArray mBefore;
        QByteArray mAfter;
    };
    std::vector<Rewrite> rewrites;
    if (updateLinks) {
        const auto safeLinkPath = [](const QString &path) {
            return !path.contains(u'#') && !path.contains(u'|') && !path.contains(u'[') &&
                   !path.contains(u']') && !path.contains(u'\n') && !path.contains(u'\r');
        };
        if (!safeLinkPath(to) ||
            to.left(to.size() - 3).endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        {
            *error =
                QStringLiteral("Destination cannot be represented by an extensionless wikilink.");
            return false;
        }
        QStringList notes;
        if (!listNotes(&notes, error)) return false;
        const QRegularExpression links(QStringLiteral(R"(\[\[([^\[\]\n]+)\]\])"));
        const QString oldName = QFileInfo(from).completeBaseName();
        const QString newName = QFileInfo(to).completeBaseName();
        for (const QString &note : notes) {
            QByteArray bytes;
            if (!readNote(note, &bytes, error)) return false;
            QStringDecoder decoder(QStringDecoder::Utf8);
            const bool bom = bytes.startsWith("\xEF\xBB\xBF");
            QString text = decoder.decode(bom ? bytes.mid(3) : bytes);
            if (decoder.hasError()) {
                *error = QStringLiteral("Cannot safely update links in invalid UTF-8 note: %1")
                             .arg(note);
                return false;
            }
            QString output;
            qsizetype offset = 0;
            if (text.count(QStringLiteral("[[")) != text.count(QStringLiteral("]]"))) {
                *error = QStringLiteral("Unsupported wikilink syntax in %1.").arg(note);
                return false;
            }
            auto matches = links.globalMatch(text);
            qsizetype linkCount = 0;
            while (matches.hasNext()) {
                ++linkCount;
                const auto match = matches.next();
                const QString link = match.captured(1);
                const QString bare = link.section(u'/', -1).section(u'#', 0, 0).section(u'|', 0, 0);
                const bool relevant =
                    note == from || bare.compare(oldName, Qt::CaseInsensitive) == 0 ||
                    bare.compare(newName, Qt::CaseInsensitive) == 0 ||
                    bare.compare(oldName + QStringLiteral(".md"), Qt::CaseInsensitive) == 0 ||
                    bare.compare(newName + QStringLiteral(".md"), Qt::CaseInsensitive) == 0;
                if (!relevant) continue;
                if (link.contains(u'|') || link.contains(u'#') || link.startsWith(u'.') ||
                    (match.capturedStart() > 0 && text[match.capturedStart() - 1] == u'!'))
                {
                    *error = QStringLiteral("Complex wikilink syntax needs manual review in %1.")
                                 .arg(note);
                    return false;
                }
                const auto target = resolveWikilink(link, note);
                if (!target || !target->endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
                    *error =
                        QStringLiteral("Unsupported or unresolved link needs manual review in %1.")
                            .arg(note);
                    return false;
                }
                output += text.mid(offset, match.capturedStart() - offset);
                const QString mapped = *target == from ? to : *target;
                if (!safeLinkPath(mapped)) {
                    *error =
                        QStringLiteral("Link target cannot be safely serialized in %1.").arg(note);
                    return false;
                }
                const QString linkPath = mapped.left(mapped.size() - 3);
                const QFileInfo shadow(QDir(mRoot).filePath(linkPath));
                // The resolver also falls back to a case-insensitive exact
                // spelling before it considers adding .md.
                bool shadowed = shadow.isFile() || shadow.isSymLink();
                const QFileInfoList siblings =
                    QDir(shadow.absolutePath())
                        .entryInfoList(
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                for (const QFileInfo &sibling : siblings) {
                    if (sibling.fileName().compare(shadow.fileName(), Qt::CaseInsensitive) == 0 &&
                        (sibling.isFile() || sibling.isSymLink()))
                    {
                        shadowed = true;
                        break;
                    }
                }
                if (linkPath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) || shadowed) {

                    *error = QStringLiteral(
                                 "Cannot write an unambiguous extensionless link to %1 in %2.")
                                 .arg(mapped, note);
                    return false;
                }
                output += QStringLiteral("[[/") + linkPath + QStringLiteral("]]");
                offset = match.capturedEnd();
            }
            if (linkCount != text.count(QStringLiteral("[["))) {
                *error = QStringLiteral("Unsupported wikilink syntax in %1.").arg(note);
                return false;
            }
            output += text.mid(offset);
            const QByteArray after =
                (bom ? QByteArray::fromHex("efbbbf") : QByteArray()) + output.toUtf8();
            if (after != bytes) rewrites.push_back({note, bytes, after});
        }
    }
    // Recheck every file slated for modification before the first mutation.
    const bool unchanged =
        std::all_of(rewrites.begin(), rewrites.end(), [&](const Rewrite &rewrite) {
            QByteArray bytes;
            return readNote(rewrite.mPath, &bytes, error) && bytes == rewrite.mBefore;
        });
    if (!unchanged) {
        conflict(error);
        return false;
    }
    if (!readNote(from, &current, error) || current != expected) {
        conflict(error);
        return false;
    }
    if (QFileInfo(destination).exists() || QFileInfo(destination).isSymLink() ||
        !QFile::rename(*source, destination))
    {
        *error = QStringLiteral("Could not move note (destination may have appeared).");
        return false;
    }
    const bool saved = std::all_of(rewrites.begin(), rewrites.end(), [&](const Rewrite &rewrite) {
        const QString path = rewrite.mPath == from ? to : rewrite.mPath;
        return saveNote(path, rewrite.mBefore, rewrite.mAfter, error);
    });
    if (!saved) {
        *error = QStringLiteral("Move partially completed; some links may need repair: ") + *error;
    }
    return saved;
}

bool Vault::trashNote(const QString &relative, const TrashFunction &trash, QString *error) const
{
    const auto path = notePath(relative, error);
    return path && trash(*path, error);
}
