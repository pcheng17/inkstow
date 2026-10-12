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

static void resolvesFixtureLinks()
{
    QString error;
    const auto vault = Vault::open(QString::fromUtf8(LINKS_VAULT), &error);
    CHECK(vault.has_value());
    const QString source = QStringLiteral("A/Source.md");
    const auto check = [&](const QString &link, const QString &expected) {
        CHECK(vault->resolveWikilink(link, source) == std::optional<QString>(expected));
    };
    check(QStringLiteral("Twin"), QStringLiteral("A/Twin.md"));
    check(QStringLiteral("B/Twin"), QStringLiteral("B/Twin.md"));
    check(QStringLiteral("b/twin"), QStringLiteral("B/Twin.md"));
    check(QStringLiteral("/b/twin"), QStringLiteral("B/Twin.md"));
    check(QStringLiteral("/B/Twin"), QStringLiteral("B/Twin.md"));
    check(QStringLiteral("/Twin"), QStringLiteral("Twin.md"));
    check(QStringLiteral("tWiN"), QStringLiteral("A/Twin.md"));
    check(QStringLiteral("Twin.md"), QStringLiteral("A/Twin.md"));
    check(QStringLiteral("Twin#Section|Display"), QStringLiteral("A/Twin.md"));
    check(QStringLiteral("photo.png"), QStringLiteral("assets/photo.png"));
    check(QStringLiteral("LOWER"), QStringLiteral("Sub/lower.md"));
    CHECK(!vault->resolveWikilink(QStringLiteral("Absent"), source));
    CHECK(!vault->resolveWikilink(QStringLiteral("/Absent"), source));
    CHECK(!vault->resolveWikilink(QStringLiteral("Wrong/Twin"), source));
    CHECK(!vault->resolveWikilink(QStringLiteral("wrong/twin"), source));
    check(QStringLiteral("/a/twin"), QStringLiteral("A/Twin.md"));
    CHECK(!vault->resolveWikilink(QStringLiteral("../Twin"), source));
    CHECK(!vault->resolveWikilink(QStringLiteral(".obsidian/private"), source));
    CHECK(!vault->resolveWikilink(QStringLiteral("Twin"), QStringLiteral("missing.md")));
    CHECK(
        vault->attachmentPath(QStringLiteral("assets/photo.png")) ==
        QFileInfo(QString::fromUtf8(LINKS_VAULT) + QStringLiteral("/assets/photo.png"))
            .canonicalFilePath());
    CHECK(!vault->attachmentPath(QStringLiteral("../photo.png")));
    CHECK(!vault->attachmentPath(QStringLiteral("A/Twin.md")));

#ifdef Q_OS_UNIX
    QTemporaryDir outside;
    CHECK(outside.isValid());
    writeFile(outside.filePath(QStringLiteral("secret.md")), "outside");
    QTemporaryDir safe;
    CHECK(safe.isValid());
    writeFile(safe.filePath(QStringLiteral("source.md")), "links");
    CHECK(QFile::link(outside.path(), safe.filePath(QStringLiteral("linked"))));
    CHECK(
        QFile::link(
            outside.filePath(QStringLiteral("secret.md")),
            safe.filePath(QStringLiteral("shortcut.md"))));
    const auto isolated = Vault::open(safe.path(), &error);
    CHECK(isolated.has_value());
    CHECK(!isolated->resolveWikilink(QStringLiteral("secret"), QStringLiteral("source.md")));
    CHECK(!isolated->resolveWikilink(QStringLiteral("shortcut"), QStringLiteral("source.md")));
    CHECK(!isolated->attachmentPath(QStringLiteral("linked/secret.md")));
    CHECK(!isolated->attachmentPath(QStringLiteral("shortcut.md")));
#endif
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
    struct stat before{};
    struct stat after{};
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

    // A rejected or failed create leaves no folders behind.
    QTemporaryDir empty;
    CHECK(empty.isValid());
    const auto fresh = Vault::open(empty.path(), &error);
    CHECK(fresh.has_value());
    for (const QString &path :
         {QStringLiteral("x.md/.md"),
          QStringLiteral("Folder/.md"),
          QStringLiteral("A/B//c.md"),
          QStringLiteral("New/.hidden.md"),
          QStringLiteral("P/../q.md"),
          QStringLiteral("Long/Deeper/") + QString(300, u'a') + QStringLiteral(".md")})
    {
        CHECK(!fresh->createNote(path, &error));
        CHECK(QDir(empty.path()).isEmpty());
    }

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

static void mergeOutsideEdits()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("note.md"));
    QString error;
    Session session;
    writeFile(path, "one\ntwo\nthree\nfour\n");
    CHECK(session.openVault(dir.path(), &error));
    CHECK(session.selectNote(QStringLiteral("note.md"), &error));
    session.setContent(QStringLiteral("ONE\ntwo\nthree\nfour\n"));
    writeFile(path, "one\ntwo\nthree\nFOUR\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.content() == QStringLiteral("ONE\ntwo\nthree\nFOUR\n"));
    CHECK(session.dirty());
    CHECK(readFile(path) == "one\ntwo\nthree\nFOUR\n");
    writeFile(path, "newer\n");
    CHECK(!session.save(&error));
    CHECK(session.content() == QStringLiteral("ONE\ntwo\nthree\nFOUR\n"));
    CHECK(readFile(path) == "newer\n");
    writeFile(path, "one\ntwo\nthree\nFOUR\n");
    CHECK(session.save(&error));
    CHECK(readFile(path) == "ONE\ntwo\nthree\nFOUR\n");

    writeFile(path, "one\ntwo\nthree\nfour\n");
    CHECK(session.reloadFromDisk(&error));
    session.setContent(QStringLiteral("one\nlocal\nthree\nfour\n"));
    writeFile(path, "one\noutside\nthree\nfour\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    writeFile(path, "one\nnewer\nthree\nfour\n");
    CHECK(!session.mergeOutsideChanges(&error));
    CHECK(session.content() == QStringLiteral("one\nlocal\nthree\nfour\n"));
    writeFile(path, "one\noutside\nthree\nfour\n");
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.content().contains(
        QStringLiteral("<<<<<<< mine\nlocal\n=======\noutside\n>>>>>>> outside")));
    CHECK(readFile(path) == "one\noutside\nthree\nfour\n");
    CHECK(session.save(&error)); // Markers are only written when explicitly saved.

    writeFile(path, "one\ntwo\nthree\n");
    CHECK(session.reloadFromDisk(&error));
    session.setContent(QStringLiteral("one\nthree\n"));
    writeFile(path, "one\nchanged\nthree\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.content().contains(QStringLiteral("<<<<<<< mine")));

    // An outside insertion inside a locally deleted region is a conflict:
    // applying it separately would silently restore deleted lines.
    writeFile(path, "a\nb\nc\nd\ne");
    CHECK(session.reloadFromDisk(&error));
    session.setContent(QStringLiteral("a\nX\ne"));
    writeFile(path, "a\nb\nc\nY\nd\ne");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.content().contains(QStringLiteral("<<<<<<< mine")));
    CHECK(session.content().contains(QStringLiteral("a\n<<<<<<< mine\nX\n=======\nb\nc\nY\nd")));
    CHECK(readFile(path) == "a\nb\nc\nY\nd\ne");

    // Large notes fail safely rather than allocating a quadratic diff table.
    QByteArray lines;
    for (int i = 0; i < 1500; ++i)
        lines += "x\n";
    writeFile(path, lines);
    CHECK(session.reloadFromDisk(&error));
    session.setContent(session.content() + QStringLiteral("mine\n"));
    writeFile(path, lines + "outside\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    const QString edits = session.content();
    CHECK(!session.mergeOutsideChanges(&error));
    CHECK(error.contains(QStringLiteral("too large")));
    CHECK(session.content() == edits);
    CHECK(readFile(path) == lines + "outside\n");
}

static void saveMissingAsNewNote()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    CHECK(dir.isValid() && outside.isValid());
    const QString path = dir.filePath(QStringLiteral("note.md"));
    writeFile(path, "old");
    QString error;
    Session session;
    CHECK(session.openVault(dir.path(), &error));
    CHECK(session.selectNote(QStringLiteral("note.md"), &error));
    session.setContent(QStringLiteral("recovered"));
    CHECK(QFile::remove(path));
    CHECK(session.syncWithDisk(&error) == DiskChange::Missing);
    writeFile(path, "collision");
    CHECK(!session.saveAsNewNote(&error));
    CHECK(readFile(path) == "collision");
    CHECK(session.content() == QStringLiteral("recovered"));
    CHECK(QFile::remove(path));
#ifdef Q_OS_UNIX
    const QString target = outside.filePath(QStringLiteral("target.md"));
    CHECK(QFile::link(target, path));
    CHECK(!session.saveAsNewNote(&error));
    CHECK(!QFile::exists(target));
    CHECK(QFile::remove(path));
#endif
    CHECK(session.saveAsNewNote(&error));
    CHECK(readFile(path) == "recovered");
    CHECK(!session.dirty());
    CHECK(session.notes().contains(QStringLiteral("note.md")));
    const auto vault = Vault::open(dir.path(), &error);
    CHECK(vault.has_value());
    CHECK(!vault->createNoteWithContent(QStringLiteral(".hidden.md"), "bad", &error));
    CHECK(!vault->createNoteWithContent(QStringLiteral("sub/../bad.md"), "bad", &error));
    CHECK(!vault->createNoteWithContent(QStringLiteral("note.md"), "bad", &error));
    CHECK(readFile(path) == "recovered");
#ifdef Q_OS_UNIX
    CHECK(QFile::link(outside.path(), dir.filePath(QStringLiteral("linked"))));
    CHECK(!vault->createNoteWithContent(QStringLiteral("linked/new.md"), "bad", &error));
    CHECK(QDir(outside.path()).isEmpty());
#endif
    CHECK(vault->createNoteWithContent(QStringLiteral("fresh/actual.md"), "new body", &error));
    CHECK(readFile(dir.filePath(QStringLiteral("fresh/actual.md"))) == "new body");
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

    const TrashFunction failing = [](const QString &, QString *trashError) {
        *trashError = QStringLiteral("No trash here.");
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
    CHECK(session.content().isEmpty());
    CHECK(!session.dirty());
    CHECK(session.notes().contains(QStringLiteral("Ideas.md")));
    CHECK(session.createNote(QStringLiteral("Folder/Plan.MD"), &error));
    CHECK(session.path() == QStringLiteral("Folder/Plan.MD"));
    // On case-insensitive file systems, an existing folder keeps its spelling.
    if (QFileInfo::exists(dir.filePath(QStringLiteral("folder")))) {
        CHECK(session.createNote(QStringLiteral("folder/Later"), &error));
        CHECK(session.path() == QStringLiteral("Folder/Later.md"));
    }
    // Decomposed input (as pasted from Finder) is stored and listed composed.
    CHECK(session.createNote(QStringLiteral("Cafe\u0301"), &error));
    CHECK(session.path() == QStringLiteral("Caf\u00e9.md"));
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

#ifdef Q_OS_UNIX
    // A trashed note leaves the session even if the vault can't be relisted.
    CHECK(session.selectNote(QStringLiteral("Ideas.md"), &error));
    const QByteArray locked = QFile::encodeName(dir.filePath(QStringLiteral("Folder")));
    CHECK(::chmod(locked.constData(), 0) == 0);
    const bool trashed = session.trashNote(&error);
    CHECK(::chmod(locked.constData(), 0755) == 0);
    CHECK(trashed);
    CHECK(session.path().isEmpty());
    CHECK(!session.dirty());
    CHECK(!session.notes().contains(QStringLiteral("Ideas.md")));
#endif
}

static void renamesUniqueAndQualifiedLinks()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QDir dir(temp.path());
    CHECK(dir.mkdir(QStringLiteral("sub")));
    writeFile(dir.filePath(QStringLiteral("sub/Target.md")), "content");
    writeFile(dir.filePath(QStringLiteral("Source.md")), "[[Target]] [[sub/Target]]\n");
    QString error;
    Session session;
    CHECK(session.openVault(temp.path(), &error));
    CHECK(session.selectNote(QStringLiteral("sub/Target.md"), &error));
    writeFile(dir.filePath(QStringLiteral("sub/New")), "attachment");
    CHECK(!session.moveNote(QStringLiteral("sub/New"), true, &error));
    CHECK(readFile(dir.filePath(QStringLiteral("Source.md"))) == "[[Target]] [[sub/Target]]\n");
    CHECK(QFile::exists(dir.filePath(QStringLiteral("sub/Target.md"))));
    CHECK(QFile::remove(dir.filePath(QStringLiteral("sub/New"))));
    writeFile(dir.filePath(QStringLiteral("sub/NEW")), "case-folded attachment");
    CHECK(!session.moveNote(QStringLiteral("sub/New"), true, &error));
    CHECK(readFile(dir.filePath(QStringLiteral("Source.md"))) == "[[Target]] [[sub/Target]]\n");
    CHECK(QFile::remove(dir.filePath(QStringLiteral("sub/NEW"))));
    CHECK(session.moveNote(QStringLiteral("sub/New"), true, &error));
    CHECK(readFile(dir.filePath(QStringLiteral("Source.md"))) == "[[/sub/New]] [[/sub/New]]\n");
    CHECK(session.moveNote(QStringLiteral("sub/Again"), true, &error));
    CHECK(readFile(dir.filePath(QStringLiteral("Source.md"))) == "[[/sub/Again]] [[/sub/Again]]\n");
}

static void movesNotesAndGuardsLinks()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QDir dir(temp.path());
    CHECK(dir.mkdir(QStringLiteral("A")));
    CHECK(dir.mkdir(QStringLiteral("B")));
    CHECK(dir.mkdir(QStringLiteral("C")));
    writeFile(dir.filePath(QStringLiteral("A/Target.md")), "\xef\xbb\xbf[[Target]]\n");
    writeFile(dir.filePath(QStringLiteral("B/Target.md")), "other");
    writeFile(dir.filePath(QStringLiteral("A/Source.md")), "[[Target]] [[B/Target.md]]");
    writeFile(dir.filePath(QStringLiteral("B/Source.md")), "[[Target]]");
    QString error;
    Session session;
    CHECK(session.openVault(temp.path(), &error));
    CHECK(session.selectNote(QStringLiteral("A/Target.md"), &error));
    CHECK(!session.moveNote(QStringLiteral("../escape"), true, &error));
    CHECK(!session.moveNote(QStringLiteral("B/Target"), true, &error));
    CHECK(!session.moveNote(QStringLiteral("missing/New"), true, &error));
    session.setContent(QStringLiteral("unsaved"));
    CHECK(!session.moveNote(QStringLiteral("C/Target"), true, &error));
    session.discardEdits();
    writeFile(dir.filePath(QStringLiteral("A/Target.md")), "external");
    CHECK(!session.moveNote(QStringLiteral("C/Target"), true, &error));
    CHECK(readFile(dir.filePath(QStringLiteral("A/Source.md"))) == "[[Target]] [[B/Target.md]]");
    writeFile(dir.filePath(QStringLiteral("A/Target.md")), "\xef\xbb\xbf[[Target]]\n");
    CHECK(session.moveNote(QStringLiteral("C/Target"), true, &error));
    CHECK(session.path() == QStringLiteral("C/Target.md"));
    CHECK(!session.dirty());
    CHECK(readFile(dir.filePath(QStringLiteral("C/Target.md"))) == "\xef\xbb\xbf[[/C/Target]]\n");
    CHECK(readFile(dir.filePath(QStringLiteral("A/Source.md"))) == "[[/C/Target]] [[/B/Target]]");
    CHECK(readFile(dir.filePath(QStringLiteral("B/Source.md"))) == "[[/B/Target]]");
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("A/Target.md"))));
    CHECK(session.save(&error));
    CHECK(readFile(dir.filePath(QStringLiteral("C/Target.md"))).startsWith("\xef\xbb\xbf"));
    writeFile(dir.filePath(QStringLiteral("A/Source.md")), "[[C/Target|alias]]");
    CHECK(!session.moveNote(QStringLiteral("C/New"), true, &error));
    CHECK(QFile::exists(dir.filePath(QStringLiteral("C/Target.md"))));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("C/New.md"))));
    CHECK(session.moveNote(QStringLiteral("C/New"), false, &error));
    CHECK(readFile(dir.filePath(QStringLiteral("A/Source.md"))) == "[[C/Target|alias]]");
}

static void moveResolvesSharedDiscoveryAcrossSources()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QDir dir(temp.path());
    CHECK(dir.mkdir(QStringLiteral("A")));
    CHECK(dir.mkdir(QStringLiteral("B")));
    CHECK(dir.mkdir(QStringLiteral("C")));
    writeFile(dir.filePath(QStringLiteral("A/Old.md")), "target\n");
    writeFile(
        dir.filePath(QStringLiteral("A/Source.md")),
        QByteArray::fromHex("efbbbf") + "near [[Old]] [[old.md]] [[/a/old]]\n");
    writeFile(dir.filePath(QStringLiteral("B/Old.md")), "other\n");
    writeFile(dir.filePath(QStringLiteral("Old.md")), "root\n");
    writeFile(dir.filePath(QStringLiteral("B/Source.md")), "[[Old]] [[A/Old]] [[/A/Old.md]]\n");
    writeFile(dir.filePath(QStringLiteral("C/Source.md")), "[[Old]] [[a/old]] [[/Old]]\n");
    QString error;
    const auto vault = Vault::open(temp.path(), &error);
    CHECK(vault.has_value());
    CHECK(vault->moveNote(
        QStringLiteral("A/Old.md"),
        QStringLiteral("C/New.md"),
        "target\n",
        true,
        &error));
    CHECK(
        readFile(dir.filePath(QStringLiteral("A/Source.md"))) ==
        QByteArray::fromHex("efbbbf") + "near [[/C/New]] [[/C/New]] [[/C/New]]\n");
    CHECK(
        readFile(dir.filePath(QStringLiteral("B/Source.md"))) ==
        "[[/B/Old]] [[/C/New]] [[/C/New]]\n");
    CHECK(
        readFile(dir.filePath(QStringLiteral("C/Source.md"))) == "[[/Old]] [[/C/New]] [[/Old]]\n");
    CHECK(readFile(dir.filePath(QStringLiteral("C/New.md"))) == "target\n");
    CHECK(readFile(dir.filePath(QStringLiteral("B/Old.md"))) == "other\n");
    CHECK(readFile(dir.filePath(QStringLiteral("Old.md"))) == "root\n");
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("A/Old.md"))));
}

static void renameLinksRemainResolvable()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QDir dir(temp.path());
    writeFile(dir.filePath(QStringLiteral("Old.md")), "target");
    writeFile(dir.filePath(QStringLiteral("New")), "attachment");
    writeFile(dir.filePath(QStringLiteral("Source.md")), "[[Old]]");
    QString error;
    Session session;
    CHECK(session.openVault(temp.path(), &error));
    CHECK(session.selectNote(QStringLiteral("Old.md"), &error));
    CHECK(!session.moveNote(QStringLiteral("New#Part"), true, &error));
    CHECK(!session.moveNote(QStringLiteral("New|Alias"), true, &error));
    CHECK(!session.moveNote(QStringLiteral("New[Part]"), true, &error));
    CHECK(!session.moveNote(QStringLiteral("New.md.md"), true, &error));
    CHECK(!session.moveNote(QStringLiteral("New.md"), true, &error));
    CHECK(QFile::exists(dir.filePath(QStringLiteral("Old.md"))));
    CHECK(readFile(dir.filePath(QStringLiteral("Source.md"))) == "[[Old]]");
    CHECK(QFile::remove(dir.filePath(QStringLiteral("New"))));
    CHECK(session.moveNote(QStringLiteral("New.md"), true, &error));
    auto vault = Vault::open(temp.path(), &error);
    CHECK(vault.has_value());
    CHECK(readFile(dir.filePath(QStringLiteral("Source.md"))) == "[[/New]]");
    CHECK(
        vault->resolveWikilink(QStringLiteral("/New"), QStringLiteral("Source.md")) ==
        QStringLiteral("New.md"));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("Old.md"))));

    // A destination ending in .md.md cannot be serialized without an explicit
    // extension, even when there are no backlinks to rewrite yet.
    writeFile(dir.filePath(QStringLiteral("Unlinked.md")), "standalone");
    CHECK(session.reloadNotes(&error));
    CHECK(session.selectNote(QStringLiteral("Unlinked.md"), &error));
    CHECK(!session.moveNote(QStringLiteral("Double.md.md"), true, &error));
    CHECK(QFile::exists(dir.filePath(QStringLiteral("Unlinked.md"))));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("Double.md.md"))));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        fixtureDiscovery();
        resolvesFixtureLinks();
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
        mergeOutsideEdits();
        saveMissingAsNewNote();
        trashesNotes();
        sessionCreatesAndTrashesNotes();
        renamesUniqueAndQualifiedLinks();
        movesNotesAndGuardsLinks();
        moveResolvesSharedDiscoveryAcrossSources();
        renameLinksRemainResolvable();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
