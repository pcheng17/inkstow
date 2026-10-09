#include "session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <stdexcept>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

#define CHECK(value)                                                           \
  do {                                                                         \
    if (!(value))                                                              \
      throw std::runtime_error("Failed: " #value " at line " +                 \
                               std::to_string(__LINE__));                      \
  } while (false)

static void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(bytes) == bytes.size());
}

static QByteArray readFile(const QString &path) {
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

static void fixtureDiscovery() {
  QString error;
  const auto vault = Vault::open(QString::fromUtf8(SAMPLE_VAULT), &error);
  CHECK(vault.has_value());
  QStringList notes;
  CHECK(vault->listNotes(&notes, &error));
  CHECK(notes == (QStringList{QStringLiteral("Folder/Second note.md"),
                              QStringLiteral("Welcome.md")}));
  QByteArray hidden;
  CHECK(
      !vault->readNote(QStringLiteral(".obsidian/app.json"), &hidden, &error));
}

static void savesAndRejectsConflicts() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("note.md"));
  writeFile(path, "original\n");
  QString error;
  const auto vault = Vault::open(dir.path(), &error);
  CHECK(vault.has_value());
  QByteArray original;
  CHECK(vault->readNote(QStringLiteral("note.md"), &original, &error));
#ifdef Q_OS_UNIX
  struct stat before{}, after{};
  CHECK(::chmod(QFile::encodeName(path).constData(), 0640) == 0);
  CHECK(::stat(QFile::encodeName(path).constData(), &before) == 0);
#endif
  CHECK(
      vault->saveNote(QStringLiteral("note.md"), original, "saved\n", &error));
  CHECK(readFile(path) == "saved\n");
#ifdef Q_OS_UNIX
  CHECK(::stat(QFile::encodeName(path).constData(), &after) == 0);
  CHECK(before.st_ino != after.st_ino);
  CHECK((after.st_mode & 0777) == 0640);
#endif
  writeFile(path, "external\n");
  CHECK(!vault->saveNote(QStringLiteral("note.md"), "saved\n", "local\n",
                         &error));
  CHECK(error.contains(QStringLiteral("Save conflict")));
  CHECK(readFile(path) == "external\n");
  CHECK(QDir(dir.path())
            .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
            .size() == 1);
  CHECK(QFile::remove(path));
  CHECK(!vault->saveNote(QStringLiteral("note.md"), "external\n", "local\n",
                         &error));
  CHECK(!QFile::exists(path));
}

static void rejectsEscapesAndSymlinks() {
  QTemporaryDir dir;
  QTemporaryDir outside;
  CHECK(dir.isValid() && outside.isValid());
  writeFile(dir.filePath(QStringLiteral("note.md")), "safe");
  writeFile(outside.filePath(QStringLiteral("secret.md")), "outside");
  QString error;
  auto vault = Vault::open(dir.path(), &error);
  CHECK(vault.has_value());
  for (const QString &path :
       {QStringLiteral("../note.md"), QStringLiteral("/tmp/note.md"),
        QStringLiteral("note.md/.."), QStringLiteral(""),
        QStringLiteral("photo.png")}) {
    CHECK(!vault->saveNote(path, "safe", "unsafe", &error));
  }
#ifdef Q_OS_UNIX
  CHECK(QFile::link(outside.filePath(QStringLiteral("secret.md")),
                    dir.filePath(QStringLiteral("shortcut.md"))));
  CHECK(QFile::link(outside.path(), dir.filePath(QStringLiteral("external"))));
  QByteArray bytes;
  CHECK(!vault->readNote(QStringLiteral("shortcut.md"), &bytes, &error));
  CHECK(!vault->saveNote(QStringLiteral("external/secret.md"), "outside", "bad",
                         &error));
  CHECK(readFile(outside.filePath(QStringLiteral("secret.md"))) == "outside");
#endif
}

static void preservesUtf8BomWhenEditing() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const QString note = dir.filePath(QStringLiteral("bom.md"));
  writeFile(note, QByteArray::fromHex("efbbbf") + "original");
  QString error;
  Session session;
  CHECK(session.openVault(dir.path(), &error));
  CHECK(session.selectNote(QStringLiteral("bom.md"), &error));
  session.setContent(QStringLiteral("updated"));
  CHECK(session.save(&error));
  CHECK(readFile(note) == QByteArray::fromHex("efbbbf") + "updated");
}

static void rejectsInvalidUtf8AndKeepsPriorVaultOnFailure() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  writeFile(dir.filePath(QStringLiteral("bad.md")), QByteArray::fromHex("ff"));
  writeFile(dir.filePath(QStringLiteral("good.md")), "good");
  QString error;
  Session session;
  CHECK(session.openVault(dir.path(), &error));
  CHECK(!session.selectNote(QStringLiteral("bad.md"), &error));
  CHECK(error.contains(QStringLiteral("UTF-8")));
  CHECK(session.path().isEmpty());
  CHECK(session.selectNote(QStringLiteral("good.md"), &error));
  CHECK(!session.openVault(dir.filePath(QStringLiteral("missing")), &error));
  CHECK(session.path() == QStringLiteral("good.md"));
  CHECK(session.content() == QStringLiteral("good"));
}

static void sessionRetainsLocalEditsOnConflict() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const QString note = dir.filePath(QStringLiteral("note.md"));
  writeFile(note, "original");
  writeFile(dir.filePath(QStringLiteral("other.md")), "second");
  QString error;
  Session session;
  CHECK(session.openVault(dir.path(), &error));
  CHECK(session.selectNote(QStringLiteral("note.md"), &error));
  session.setContent(QStringLiteral("first edit"));
  CHECK(session.dirty());
  CHECK(!session.selectNote(QStringLiteral("other.md"), &error));
  CHECK(session.save(&error));
  CHECK(!session.dirty());
  CHECK(readFile(note) == "first edit");
  session.setContent(QStringLiteral("local edit"));
  writeFile(note, "external edit");
  CHECK(!session.save(&error));
  CHECK(error.contains(QStringLiteral("Save conflict")));
  CHECK(session.dirty());
  CHECK(session.content() == QStringLiteral("local edit"));
  CHECK(readFile(note) == "external edit");
  CHECK(!session.selectNote(QStringLiteral("other.md"), &error));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    fixtureDiscovery();
    savesAndRejectsConflicts();
    rejectsEscapesAndSymlinks();
    preservesUtf8BomWhenEditing();
    rejectsInvalidUtf8AndKeepsPriorVaultOnFailure();
    sessionRetainsLocalEditsOnConflict();
  } catch (const std::exception &error) {
    qCritical("%s", error.what());
    return 1;
  }
  return 0;
}
