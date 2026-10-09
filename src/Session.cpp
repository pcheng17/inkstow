#include "Session.h"

#include <QDir>
#include <QStringDecoder>

bool Session::openVault(const QString &folder, QString *error)
{
    if (dirty()) {
        *error = QStringLiteral("Save the current note before opening another vault.");
        return false;
    }
    auto next = Vault::open(folder, error);
    if (!next) return false;
    QStringList notes;
    QStringList folders;
    if (!next->listNotes(&notes, error, &folders)) return false;
    vault_ = std::move(next);
    notes_ = notes;
    folders_ = folders;
    clearNote();
    return true;
}

bool Session::selectNote(const QString &path, QString *error)
{
    if (dirty()) {
        *error = QStringLiteral("Save the current note before switching.");
        return false;
    }
    if (!vault_ || !notes_.contains(path)) {
        *error = QStringLiteral("Note is not in the current vault.");
        return false;
    }
    QByteArray bytes;
    if (!vault_->readNote(path, &bytes, error)) return false;
    QString text;
    if (!decode(bytes, &text, error)) return false;
    load(path, bytes, text);
    return true;
}

bool Session::save(QString *error)
{
    return write(savedBytes_, error);
}

bool Session::write(const QByteArray &expected, QString *error)
{
    if (!requireNote(error)) return false;
    const QByteArray bytes =
        (hasBom_ ? QByteArray::fromHex("efbbbf") : QByteArray()) + content_.toUtf8();
    if (!vault_->saveNote(path_, expected, bytes, error)) return false;
    savedContent_ = content_;
    savedBytes_ = bytes;
    conflictBytes_.reset();
    return true;
}

DiskChange Session::syncWithDisk(QString *error)
{
    if (!vault_ || path_.isEmpty()) return DiskChange::None;
    QByteArray bytes;
    if (!vault_->readNote(path_, &bytes, error)) return DiskChange::Missing;
    if (bytes == savedBytes_) return DiskChange::None;
    conflictBytes_ = bytes;
    if (dirty()) {
        *error = QStringLiteral(
            "Note changed on disk while you have unsaved edits; "
            "reload from disk or keep your version.");
        return DiskChange::Conflict;
    }
    QString text;
    if (!decode(bytes, &text, error)) return DiskChange::Conflict;
    load(path_, bytes, text);
    return DiskChange::Reloaded;
}

bool Session::reloadFromDisk(QString *error)
{
    if (!requireNote(error)) return false;
    QByteArray bytes;
    QString text;
    if (!vault_->readNote(path_, &bytes, error) || !decode(bytes, &text, error)) return false;
    load(path_, bytes, text);
    return true;
}

bool Session::overwriteDisk(QString *error)
{
    if (!requireNote(error)) return false;
    if (!conflictBytes_) {
        *error = QStringLiteral("No outside change to replace.");
        return false;
    }
    return write(*conflictBytes_, error);
}

bool Session::createNote(const QString &input, QString *error)
{
    if (!requireClean(error)) return false;
    if (!vault_) {
        *error = QStringLiteral("Open a vault first.");
        return false;
    }
    // macOS lists names composed, so decomposed input would not match the list.
    QString path = input.trimmed().normalized(QString::NormalizationForm_C);
    if (!path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) path += QStringLiteral(".md");
    if (!vault_->createNote(path, error) || !reloadNotes(error)) return false;
    // A case-insensitive file system may have kept an existing folder's spelling.
    if (!notes_.contains(path)) {
        for (const QString &note : notes_) {
            if (note.compare(path, Qt::CaseInsensitive) == 0) {
                path = note;
                break;
            }
        }
    }
    return selectNote(path, error);
}

bool Session::trashNote(QString *error)
{
    if (!requireNote(error) || !requireClean(error) || !vault_->trashNote(path_, trash_, error))
        return false;
    // The note is gone either way; a failed relist only drops it from the list.
    QString ignored;
    if (!reloadNotes(&ignored)) notes_.removeAll(path_);
    clearNote();
    return true;
}

bool Session::reloadNotes(QString *error)
{
    if (!vault_) return false;
    QStringList notes;
    QStringList folders;
    if (!vault_->listNotes(&notes, error, &folders)) return false;
    notes_ = notes;
    folders_ = folders;
    return true;
}

void Session::clearNote()
{
    path_.clear();
    content_.clear();
    savedContent_.clear();
    savedBytes_.clear();
    conflictBytes_.reset();
    hasBom_ = false;
}

bool Session::requireClean(QString *error) const
{
    if (!dirty()) return true;
    *error = QStringLiteral("Save or discard your edits first.");
    return false;
}

bool Session::requireNote(QString *error) const
{
    if (vault_ && !path_.isEmpty()) return true;
    *error = QStringLiteral("Select a note first.");
    return false;
}

QString Session::notePath() const
{
    if (!vault_ || path_.isEmpty()) return QString();
    return QDir(vault_->root()).filePath(path_);
}

bool Session::decode(const QByteArray &bytes, QString *text, QString *error) const
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    *text = decoder.decode(bytes);
    if (decoder.hasError()) {
        *error = QStringLiteral("Note is not valid UTF-8; refusing to risk changing its bytes.");
        return false;
    }
    return true;
}

void Session::load(const QString &path, const QByteArray &bytes, const QString &text)
{
    path_ = path;
    hasBom_ = bytes.startsWith("\xEF\xBB\xBF");
    content_ = text;
    savedContent_ = text;
    savedBytes_ = bytes;
    conflictBytes_.reset();
}
