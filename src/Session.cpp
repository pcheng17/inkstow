#include "Session.h"

#include <QDir>
#include <QStringDecoder>

bool Session::openVault(const QString &folder, QString *error) {
  if (dirty()) {
    *error =
        QStringLiteral("Save the current note before opening another vault.");
    return false;
  }
  auto next = Vault::open(folder, error);
  if (!next)
    return false;
  QStringList notes;
  if (!next->listNotes(&notes, error))
    return false;
  vault_ = std::move(next);
  notes_ = notes;
  path_.clear();
  content_.clear();
  savedContent_.clear();
  savedBytes_.clear();
  hasBom_ = false;
  return true;
}

bool Session::selectNote(const QString &path, QString *error) {
  if (dirty()) {
    *error = QStringLiteral("Save the current note before switching.");
    return false;
  }
  if (!vault_ || !notes_.contains(path)) {
    *error = QStringLiteral("Note is not in the current vault.");
    return false;
  }
  QByteArray bytes;
  if (!vault_->readNote(path, &bytes, error))
    return false;
  QString text;
  if (!decode(bytes, &text, error))
    return false;
  load(path, bytes, text);
  return true;
}

bool Session::save(QString *error) { return write(savedBytes_, error); }

bool Session::write(const QByteArray &expected, QString *error) {
  if (!requireNote(error))
    return false;
  const QByteArray bytes =
      (hasBom_ ? QByteArray::fromHex("efbbbf") : QByteArray()) +
      content_.toUtf8();
  if (!vault_->saveNote(path_, expected, bytes, error))
    return false;
  savedContent_ = content_;
  savedBytes_ = bytes;
  conflictBytes_.reset();
  return true;
}

DiskChange Session::syncWithDisk(QString *error) {
  if (!vault_ || path_.isEmpty())
    return DiskChange::None;
  QByteArray bytes;
  if (!vault_->readNote(path_, &bytes, error))
    return DiskChange::Missing;
  if (bytes == savedBytes_)
    return DiskChange::None;
  conflictBytes_ = bytes;
  if (dirty()) {
    *error = QStringLiteral("Note changed on disk while you have unsaved edits; "
                            "reload from disk or keep your version.");
    return DiskChange::Conflict;
  }
  QString text;
  if (!decode(bytes, &text, error))
    return DiskChange::Conflict;
  load(path_, bytes, text);
  return DiskChange::Reloaded;
}

bool Session::reloadFromDisk(QString *error) {
  if (!requireNote(error))
    return false;
  QByteArray bytes;
  QString text;
  if (!vault_->readNote(path_, &bytes, error) || !decode(bytes, &text, error))
    return false;
  load(path_, bytes, text);
  return true;
}

bool Session::overwriteDisk(QString *error) {
  if (!requireNote(error))
    return false;
  if (!conflictBytes_) {
    *error = QStringLiteral("No outside change to replace.");
    return false;
  }
  return write(*conflictBytes_, error);
}

bool Session::requireNote(QString *error) const {
  if (vault_ && !path_.isEmpty())
    return true;
  *error = QStringLiteral("Select a note first.");
  return false;
}

QString Session::notePath() const {
  if (!vault_ || path_.isEmpty())
    return QString();
  return QDir(vault_->root()).filePath(path_);
}

bool Session::decode(const QByteArray &bytes, QString *text,
                     QString *error) const {
  QStringDecoder decoder(QStringDecoder::Utf8);
  *text = decoder.decode(bytes);
  if (decoder.hasError()) {
    *error = QStringLiteral(
        "Note is not valid UTF-8; refusing to risk changing its bytes.");
    return false;
  }
  return true;
}

void Session::load(const QString &path, const QByteArray &bytes,
                   const QString &text) {
  path_ = path;
  hasBom_ = bytes.startsWith("\xEF\xBB\xBF");
  content_ = text;
  savedContent_ = text;
  savedBytes_ = bytes;
  conflictBytes_.reset();
}
