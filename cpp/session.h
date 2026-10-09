#pragma once

#include "vault.h"

#include <optional>

// UI-independent state, including the last disk bytes used for conflict checks.
class Session {
public:
  bool openVault(const QString &folder, QString *error);
  bool selectNote(const QString &path, QString *error);
  bool save(QString *error);
  void setContent(const QString &content) { content_ = content; }

  bool dirty() const { return content_ != savedContent_; }
  const QStringList &notes() const { return notes_; }
  const QString &path() const { return path_; }
  const QString &content() const { return content_; }

private:
  std::optional<Vault> vault_;
  QStringList notes_;
  QString path_;
  QString content_;
  QString savedContent_;
  QByteArray savedBytes_;
  bool hasBom_ = false;
};
