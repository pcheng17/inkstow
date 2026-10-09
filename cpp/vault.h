#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

// Filesystem-only vault operations. Paths are relative to the vault and use
// '/'.
class Vault {
public:
  static std::optional<Vault> open(const QString &folder, QString *error);

  bool listNotes(QStringList *notes, QString *error) const;
  bool readNote(const QString &relative, QByteArray *content,
                QString *error) const;
  // The disk bytes must still equal expected. The replacement is staged in the
  // same directory and renamed into place; other programs can race the final
  // check.
  bool saveNote(const QString &relative, const QByteArray &expected,
                const QByteArray &content, QString *error) const;

private:
  explicit Vault(QString root) : root_(std::move(root)) {}
  std::optional<QString> notePath(const QString &relative,
                                  QString *error) const;

  QString root_;
};
