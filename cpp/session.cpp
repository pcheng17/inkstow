#include "session.h"

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
  QStringDecoder decoder(QStringDecoder::Utf8);
  const QString text = decoder.decode(bytes);
  if (decoder.hasError()) {
    *error = QStringLiteral(
        "Note is not valid UTF-8; refusing to risk changing its bytes.");
    return false;
  }
  path_ = path;
  hasBom_ = bytes.startsWith("\xEF\xBB\xBF");
  content_ = text;
  savedContent_ = text;
  savedBytes_ = bytes;
  return true;
}

bool Session::save(QString *error) {
  if (!vault_ || path_.isEmpty()) {
    *error = QStringLiteral("Select a note first.");
    return false;
  }
  const QByteArray bytes =
      (hasBom_ ? QByteArray::fromHex("efbbbf") : QByteArray()) +
      content_.toUtf8();
  if (!vault_->saveNote(path_, savedBytes_, bytes, error))
    return false;
  savedContent_ = content_;
  savedBytes_ = bytes;
  return true;
}
