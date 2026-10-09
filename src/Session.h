#pragma once

#include "Vault.h"

#include <optional>

// What syncWithDisk found when comparing the note on disk with the session.
enum class DiskChange {
  None,     // Disk still holds the last loaded or saved bytes.
  Reloaded, // The editor was clean, so it now holds the new disk text.
  Conflict, // Disk changed but could not be loaded; editor text is kept.
  Missing,  // The note can no longer be read; editor text is kept.
};

// UI-independent state, including the last disk bytes used for conflict checks.
class Session {
public:
  bool openVault(const QString &folder, QString *error);
  bool selectNote(const QString &path, QString *error);
  bool save(QString *error);
  // Reloads a clean note changed by another program. Never discards edits.
  DiskChange syncWithDisk(QString *error);
  void setContent(const QString &content) { content_ = content; }

  bool dirty() const { return content_ != savedContent_; }
  const QStringList &notes() const { return notes_; }
  const QString &path() const { return path_; }
  const QString &content() const { return content_; }
  // Absolute path of the selected note, or empty when none is selected.
  QString notePath() const;

private:
  bool decode(const QByteArray &bytes, QString *text, QString *error) const;
  void load(const QString &path, const QByteArray &bytes, const QString &text);

  std::optional<Vault> vault_;
  QStringList notes_;
  QString path_;
  QString content_;
  QString savedContent_;
  QByteArray savedBytes_;
  bool hasBom_ = false;
};
