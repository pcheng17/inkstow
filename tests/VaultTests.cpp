#include "Session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <stdexcept>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value))                                                                              \
            throw std::runtime_error("Failed: " #value " at line " + std::to_string(__LINE__));    \
    } while (false)

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// Stands in for the system trash so tests don't fill the real one.
static TrashFunction trashInto(const QString &folder)
{
    return [folder](const QString &path, QString *error) {
        if (QFile::rename(path, QDir(folder).filePath(QFileInfo(path).fileName()))) return true;
        *error = QStringLiteral("Fake trash failed.");
        return false;
    };
}

static void fixtureDiscovery()
{
    QString error;
    const auto vault = Vault::open(QString::fromUtf8(SAMPLE_VAULT), &error);
    CHECK(vault.has_value());
    QStringList notes;
    CHECK(vault->listNotes(&notes, &error));
    CHECK(
        notes ==
        (QStringList{QStringLiteral("Folder/Second note.md"), QStringLiteral("Welcome.md")}));
    QByteArray hidden;
    CHECK(!vault->readNote(QStringLiteral(".obsidian/app.json"), &hidden, &error));
}

static void savesAndRejectsConflicts()
{
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
    CHECK(vault->saveNote(QStringLiteral("note.md"), original, "saved\n", &error));
    CHECK(readFile(path) == "saved\n");
#ifdef Q_OS_UNIX
    CHECK(::stat(QFile::encodeName(path).constData(), &after) == 0);
    CHECK(before.st_ino != after.st_ino);
    CHECK((after.st_mode & 0777) == 0640);
#endif
    writeFile(path, "external\n");
    CHECK(!vault->saveNote(QStringLiteral("note.md"), "saved\n", "local\n", &error));
    CHECK(error.contains(QStringLiteral("Save conflict")));
    CHECK(readFile(path) == "external\n");
    CHECK(
        QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size() == 1);
    CHECK(QFile::remove(path));
    CHECK(!vault->saveNote(QStringLiteral("note.md"), "external\n", "local\n", &error));
    CHECK(!QFile::exists(path));
}

static void rejectsEscapesAndSymlinks()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    CHECK(dir.isValid() && outside.isValid());
    writeFile(dir.filePath(QStringLiteral("note.md")), "safe");
    writeFile(outside.filePath(QStringLiteral("secret.md")), "outside");
    QString error;
    auto vault = Vault::open(dir.path(), &error);
    CHECK(vault.has_value());
    for (const QString &path :
         {QStringLiteral("../note.md"),
          QStringLiteral("/tmp/note.md"),
          QStringLiteral("note.md/.."),
          QStringLiteral(""),
          QStringLiteral("photo.png")})
    {
        CHECK(!vault->saveNote(path, "safe", "unsafe", &error));
    }
#ifdef Q_OS_UNIX
    CHECK(
        QFile::link(
            outside.filePath(QStringLiteral("secret.md")),
            dir.filePath(QStringLiteral("shortcut.md"))));
    CHECK(QFile::link(outside.path(), dir.filePath(QStringLiteral("external"))));
    QByteArray bytes;
    CHECK(!vault->readNote(QStringLiteral("shortcut.md"), &bytes, &error));
    CHECK(!vault->saveNote(QStringLiteral("external/secret.md"), "outside", "bad", &error));
    CHECK(readFile(outside.filePath(QStringLiteral("secret.md"))) == "outside");
#endif
}

static void preservesUtf8BomWhenEditing()
{
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

static void rejectsInvalidUtf8AndKeepsPriorVaultOnFailure()
{
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

static void sessionRetainsLocalEditsOnConflict()
{
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

static void sessionSyncsExternalEdits()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    writeFile(note, "original");
    QString error;
    Session session;
    CHECK(session.syncWithDisk(&error) == DiskChange::None); // No note yet.
    CHECK(session.openVault(dir.path(), &error));
    CHECK(session.selectNote(QStringLiteral("note.md"), &error));
    CHECK(session.notePath() == QFileInfo(note).canonicalFilePath());
    CHECK(session.syncWithDisk(&error) == DiskChange::None);

    // A clean editor follows the disk, including a newly added BOM.
    writeFile(note, QByteArray::fromHex("efbbbf") + "external");
    CHECK(session.syncWithDisk(&error) == DiskChange::Reloaded);
    CHECK(session.content() == QStringLiteral("external"));
    CHECK(!session.dirty());
    session.setContent(QStringLiteral("saved"));
    CHECK(session.save(&error));
    CHECK(readFile(note) == QByteArray::fromHex("efbbbf") + "saved");
    CHECK(session.syncWithDisk(&error) == DiskChange::None); // Our own save.
    writeFile(note, "no bom");
    CHECK(session.syncWithDisk(&error) == DiskChange::Reloaded);
    session.setContent(QStringLiteral("still no bom"));
    CHECK(session.save(&error));
    CHECK(readFile(note) == "still no bom");

    // Unsaved edits are kept, and saving still refuses to overwrite.
    session.setContent(QStringLiteral("unsaved"));
    writeFile(note, "external again");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(error.contains(QStringLiteral("changed on disk")));
    CHECK(session.content() == QStringLiteral("unsaved"));
    CHECK(session.dirty());
    CHECK(!session.save(&error));
    CHECK(readFile(note) == "external again");
}

static void sessionKeepsTextWhenDiskCannotBeLoaded()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    writeFile(note, "original");
    QString error;
    Session session;
    CHECK(session.openVault(dir.path(), &error));
    CHECK(session.selectNote(QStringLiteral("note.md"), &error));
    writeFile(note, QByteArray::fromHex("ff"));
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(error.contains(QStringLiteral("UTF-8")));
    CHECK(session.content() == QStringLiteral("original"));
    CHECK(QFile::remove(note));
    CHECK(session.syncWithDisk(&error) == DiskChange::Missing);
    CHECK(session.content() == QStringLiteral("original"));
    CHECK(session.path() == QStringLiteral("note.md"));
}

static void sessionResolvesConflicts()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    writeFile(note, QByteArray::fromHex("efbbbf") + "original");
    QString error;
    Session session;
    CHECK(session.openVault(dir.path(), &error));
    CHECK(session.selectNote(QStringLiteral("note.md"), &error));

    // Keep mine: replaces the outside version and leaves the session clean.
    session.setContent(QStringLiteral("mine"));
    writeFile(note, "theirs");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.overwriteDisk(&error));
    CHECK(readFile(note) == QByteArray::fromHex("efbbbf") + "mine");
    CHECK(!session.dirty());
    CHECK(session.syncWithDisk(&error) == DiskChange::None);

    // Reload from disk: discards the edits for the outside version.
    session.setContent(QStringLiteral("mine again"));
    writeFile(note, "theirs again");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.reloadFromDisk(&error));
    CHECK(session.content() == QStringLiteral("theirs again"));
    CHECK(!session.dirty());
    CHECK(readFile(note) == "theirs again");

    // Neither choice can act on a note that is gone; the text is kept.
    session.setContent(QStringLiteral("orphaned"));
    CHECK(QFile::remove(note));
    CHECK(!session.reloadFromDisk(&error));
    CHECK(!session.overwriteDisk(&error));
    CHECK(session.content() == QStringLiteral("orphaned"));
    CHECK(!QFile::exists(note));
}

static void keepMineRefusesUnseenAndSwappedVersions()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    const QString other = dir.filePath(QStringLiteral("other.md"));
    writeFile(note, "original");
    writeFile(other, "other");
    QString error;
    Session session;
    CHECK(session.openVault(dir.path(), &error));
    CHECK(session.selectNote(QStringLiteral("note.md"), &error));
    session.setContent(QStringLiteral("mine"));
    CHECK(!session.overwriteDisk(&error)); // No conflict has been seen yet.
    CHECK(readFile(note) == "original");

    // A second outside change after the conflict was reported is not
    // overwritten; the next sync reports it instead.
    writeFile(note, "theirs A");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    writeFile(note, "theirs B");
    CHECK(!session.overwriteDisk(&error));
    CHECK(readFile(note) == "theirs B");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.overwriteDisk(&error));
    CHECK(readFile(note) == "mine");

#ifdef Q_OS_UNIX
    // A note swapped for a link to another note is never written through.
    session.setContent(QStringLiteral("mine again"));
    CHECK(QFile::remove(note));
    CHECK(QFile::link(other, note));
    CHECK(session.syncWithDisk(&error) == DiskChange::Missing);
    CHECK(!session.overwriteDisk(&error));
    CHECK(!session.save(&error));
    CHECK(readFile(other) == "other");
#endif
}

static void discardEditsLeavesDeletedNote()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    writeFile(note, "original");
    writeFile(dir.filePath(QStringLiteral("other.md")), "other");
    QString error;
    Session session;
    CHECK(session.openVault(dir.path(), &error));
    CHECK(session.selectNote(QStringLiteral("note.md"), &error));
    session.setContent(QStringLiteral("unsaved"));
    CHECK(QFile::remove(note));
    CHECK(session.syncWithDisk(&error) == DiskChange::Missing);
    session.discardEdits();
    CHECK(!session.dirty());
    CHECK(session.content() == QStringLiteral("original"));
    CHECK(session.selectNote(QStringLiteral("other.md"), &error));
    CHECK(!QFile::exists(note));
}

static void createsNotesSafely()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    CHECK(dir.isValid() && outside.isValid());
    QString error;
    const auto vault = Vault::open(dir.path(), &error);
    CHECK(vault.has_value());

    CHECK(vault->createNote(QStringLiteral("Folder/Sub/New.md"), &error));
    CHECK(readFile(dir.filePath(QStringLiteral("Folder/Sub/New.md"))).isEmpty());
    QStringList notes;
    CHECK(vault->listNotes(&notes, &error));
    CHECK(notes == QStringList{QStringLiteral("Folder/Sub/New.md")});

    // Never overwrites an existing file.
    writeFile(dir.filePath(QStringLiteral("Folder/Sub/New.md")), "kept");
    CHECK(!vault->createNote(QStringLiteral("Folder/Sub/New.md"), &error));
    CHECK(error.contains(QStringLiteral("exists")));
    CHECK(readFile(dir.filePath(QStringLiteral("Folder/Sub/New.md"))) == "kept");

    for (const QString &path :
         {QStringLiteral("../escape.md"),
          QStringLiteral("/tmp/absolute.md"),
          QStringLiteral(".hidden.md"),
          QStringLiteral(".hidden/note.md"),
          QStringLiteral("Folder/.draft.md"),
          QStringLiteral(".obsidian/note.md"),
          QStringLiteral("Folder//note.md"),
          QStringLiteral("Folder\\note.md"),
          QStringLiteral("note.txt"),
          QStringLiteral("")})
    {
        CHECK(!vault->createNote(path, &error));
    }
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("../escape.md"))));
    CHECK(!QDir(dir.filePath(QStringLiteral(".hidden"))).exists());

#ifdef Q_OS_UNIX
    // Neither a linked folder nor a dangling link is written through.
    CHECK(QFile::link(outside.path(), dir.filePath(QStringLiteral("linked"))));
    CHECK(!vault->createNote(QStringLiteral("linked/note.md"), &error));
    CHECK(!vault->createNote(QStringLiteral("linked/Sub/note.md"), &error));
    const QString target = outside.filePath(QStringLiteral("target.md"));
    CHECK(QFile::link(target, dir.filePath(QStringLiteral("dangling.md"))));
    CHECK(!vault->createNote(QStringLiteral("dangling.md"), &error));
    CHECK(!QFile::exists(target));
    CHECK(QDir(outside.path()).isEmpty());
#endif
}

static void trashesNotes()
{
    QTemporaryDir dir;
    QTemporaryDir trash;
    CHECK(dir.isValid() && trash.isValid());
    writeFile(dir.filePath(QStringLiteral("note.md")), "note");
    writeFile(dir.filePath(QStringLiteral("other.md")), "other");
    QString error;
    const auto vault = Vault::open(dir.path(), &error);
    CHECK(vault.has_value());

    CHECK(vault->trashNote(QStringLiteral("note.md"), trashInto(trash.path()), &error));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("note.md"))));
    CHECK(readFile(trash.filePath(QStringLiteral("note.md"))) == "note");

    CHECK(!vault->trashNote(QStringLiteral("note.md"), trashInto(trash.path()), &error));
    CHECK(!vault->trashNote(QStringLiteral("../x.md"), trashInto(trash.path()), &error));

    const TrashFunction failing = [](const QString &, QString *error) {
        *error = QStringLiteral("No trash here.");
        return false;
    };
    CHECK(!vault->trashNote(QStringLiteral("other.md"), failing, &error));
    CHECK(error == QStringLiteral("No trash here."));
    CHECK(readFile(dir.filePath(QStringLiteral("other.md"))) == "other");

#ifdef Q_OS_UNIX
    CHECK(
        QFile::link(
            dir.filePath(QStringLiteral("other.md")),
            dir.filePath(QStringLiteral("link.md"))));
    CHECK(!vault->trashNote(QStringLiteral("link.md"), trashInto(trash.path()), &error));
    CHECK(QFileInfo(dir.filePath(QStringLiteral("link.md"))).isSymLink());
#endif
}

static void sessionCreatesAndTrashesNotes()
{
    QTemporaryDir dir;
    QTemporaryDir trash;
    CHECK(dir.isValid() && trash.isValid());
    writeFile(dir.filePath(QStringLiteral("note.md")), "note");
    QString error;
    Session session(trashInto(trash.path()));
    CHECK(!session.trashNote(&error)); // Nothing open yet.
    CHECK(session.openVault(dir.path(), &error));

    // The extension is added when missing and the new note is selected.
    CHECK(session.createNote(QStringLiteral(" Ideas "), &error));
    CHECK(session.path() == QStringLiteral("Ideas.md"));
    CHECK(session.content().isEmpty() && !session.dirty());
    CHECK(session.notes().contains(QStringLiteral("Ideas.md")));
    CHECK(session.createNote(QStringLiteral("Folder/Plan.MD"), &error));
    CHECK(session.path() == QStringLiteral("Folder/Plan.MD"));
    // On case-insensitive file systems, an existing folder keeps its spelling.
    if (QFileInfo::exists(dir.filePath(QStringLiteral("folder")))) {
        CHECK(session.createNote(QStringLiteral("folder/Later"), &error));
        CHECK(session.path() == QStringLiteral("Folder/Later.md"));
    }
    CHECK(session.createNote(QStringLiteral("v1.2"), &error));
    CHECK(session.path() == QStringLiteral("v1.2.md"));

    // An existing note is not replaced, and the selection stays.
    CHECK(!session.createNote(QStringLiteral("note"), &error));
    CHECK(session.path() == QStringLiteral("v1.2.md"));
    CHECK(readFile(dir.filePath(QStringLiteral("note.md"))) == "note");

    // Unsaved edits block both, so nothing is lost silently.
    session.setContent(QStringLiteral("unsaved"));
    CHECK(!session.createNote(QStringLiteral("Another"), &error));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("Another.md"))));
    CHECK(!session.trashNote(&error));
    CHECK(QFile::exists(dir.filePath(QStringLiteral("v1.2.md"))));
    session.discardEdits();

    CHECK(session.trashNote(&error));
    CHECK(session.path().isEmpty() && session.content().isEmpty());
    CHECK(!session.notes().contains(QStringLiteral("v1.2.md")));
    CHECK(QFile::exists(trash.filePath(QStringLiteral("v1.2.md"))));
    CHECK(!session.trashNote(&error));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        fixtureDiscovery();
        savesAndRejectsConflicts();
        rejectsEscapesAndSymlinks();
        preservesUtf8BomWhenEditing();
        rejectsInvalidUtf8AndKeepsPriorVaultOnFailure();
        sessionRetainsLocalEditsOnConflict();
        sessionSyncsExternalEdits();
        sessionKeepsTextWhenDiskCannotBeLoaded();
        sessionResolvesConflicts();
        keepMineRefusesUnseenAndSwappedVersions();
        discardEditsLeavesDeletedNote();
        createsNotesSafely();
        trashesNotes();
        sessionCreatesAndTrashesNotes();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
