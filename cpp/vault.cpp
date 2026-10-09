#include "vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {
void conflict(QString *error) {
  *error =
      QStringLiteral("Save conflict: note changed on disk. Your edits remain "
                     "in the editor; copy them before reopening the note.");
}

bool readBytes(const QString &path, QByteArray *content, QString *error) {
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

std::optional<Vault> Vault::open(const QString &folder, QString *error) {
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

bool Vault::listNotes(QStringList *notes, QString *error) const {
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
      if (entry.fileName().startsWith(u'.') || entry.isSymLink())
        continue;
      if (entry.isDir()) {
        pending.append(entry.filePath());
      } else if (entry.isFile() &&
                 entry.suffix().compare(QStringLiteral("md"),
                                        Qt::CaseInsensitive) == 0) {
        found.append(QDir(root_)
                         .relativeFilePath(entry.filePath())
                         .replace(u'\\', u'/'));
      }
    }
  }
  found.sort();
  *notes = found;
  return true;
}

std::optional<QString> Vault::notePath(const QString &relative,
                                       QString *error) const {
  if (relative.isEmpty() || QDir::isAbsolutePath(relative) ||
      relative.contains(u'\\') ||
      !relative.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
    *error = QStringLiteral("Expected a relative Markdown path.");
    return std::nullopt;
  }
  for (const QString &segment : relative.split(u'/')) {
    if (segment.isEmpty() || segment == QStringLiteral(".") ||
        segment == QStringLiteral("..")) {
      *error = QStringLiteral("Expected a relative Markdown path.");
      return std::nullopt;
    }
  }
  QFileInfo info(QDir(root_).filePath(relative));
  const QString resolved = info.canonicalFilePath();
  const QString prefix = root_.endsWith(u'/') ? root_ : root_ + u'/';
  if (!info.isFile() || !resolved.startsWith(prefix) ||
      resolved.startsWith(prefix + QStringLiteral(".obsidian/"))) {
    *error = QStringLiteral("Note is missing or outside the vault.");
    return std::nullopt;
  }
  return resolved;
}

bool Vault::readNote(const QString &relative, QByteArray *content,
                     QString *error) const {
  const auto path = notePath(relative, error);
  return path && readBytes(*path, content, error);
}

bool Vault::saveNote(const QString &relative, const QByteArray &expected,
                     const QByteArray &content, QString *error) const {
  const auto path = notePath(relative, error);
  if (!path)
    return false;
  QByteArray current;
  if (!readBytes(*path, &current, error))
    return false;
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
      replacement.write(content) != content.size()) {
    *error = replacement.errorString();
    replacement.cancelWriting();
    return false;
  }
  const auto stillThere = notePath(relative, error);
  if (!stillThere || *stillThere != *path ||
      !readBytes(*path, &current, error) || current != expected) {
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
