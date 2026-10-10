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

static void WriteFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
}

static QByteArray ReadFile(const QString &path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// Stands in for the system trash so tests don't fill the real one.
static TrashFunction TrashInto(const QString &folder)
{
    return [folder](const QString &path, QString *error) {
        if (QFile::rename(path, QDir(folder).filePath(QFileInfo(path).fileName()))) return true;
        *error = QStringLiteral("Fake trash failed.");
        return false;
    };
}

static void FixtureDiscovery()
{
    QString error;
    const auto vault = Vault::Open(QString::fromUtf8(SAMPLE_VAULT), &error);
    CHECK(vault.has_value());
    QStringList notes;
    CHECK(vault->ListNotes(&notes, &error));
    CHECK(
        notes ==
        (QStringList{QStringLiteral("Folder/Second note.md"), QStringLiteral("Welcome.md")}));
    QByteArray hidden;
    CHECK(!vault->ReadNote(QStringLiteral(".obsidian/app.json"), &hidden, &error));
}

static void SavesAndRejectsConflicts()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("note.md"));
    WriteFile(path, "original\n");
    QString error;
    const auto vault = Vault::Open(dir.path(), &error);
    CHECK(vault.has_value());
    QByteArray original;
    CHECK(vault->ReadNote(QStringLiteral("note.md"), &original, &error));
#ifdef Q_OS_UNIX
    struct stat before{};
    struct stat after{};
    CHECK(::chmod(QFile::encodeName(path).constData(), 0640) == 0);
    CHECK(::stat(QFile::encodeName(path).constData(), &before) == 0);
#endif
    CHECK(vault->SaveNote(QStringLiteral("note.md"), original, "saved\n", &error));
    CHECK(ReadFile(path) == "saved\n");
#ifdef Q_OS_UNIX
    CHECK(::stat(QFile::encodeName(path).constData(), &after) == 0);
    CHECK(before.st_ino != after.st_ino);
    CHECK((after.st_mode & 0777) == 0640);
#endif
    WriteFile(path, "external\n");
    CHECK(!vault->SaveNote(QStringLiteral("note.md"), "saved\n", "local\n", &error));
    CHECK(error.contains(QStringLiteral("Save conflict")));
    CHECK(ReadFile(path) == "external\n");
    CHECK(
        QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size() == 1);
    CHECK(QFile::remove(path));
    CHECK(!vault->SaveNote(QStringLiteral("note.md"), "external\n", "local\n", &error));
    CHECK(!QFile::exists(path));
}

static void RejectsEscapesAndSymlinks()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    CHECK(dir.isValid() && outside.isValid());
    WriteFile(dir.filePath(QStringLiteral("note.md")), "safe");
    WriteFile(outside.filePath(QStringLiteral("secret.md")), "outside");
    QString error;
    auto vault = Vault::Open(dir.path(), &error);
    CHECK(vault.has_value());
    for (const QString &path :
         {QStringLiteral("../note.md"),
          QStringLiteral("/tmp/note.md"),
          QStringLiteral("note.md/.."),
          QStringLiteral(""),
          QStringLiteral("photo.png")})
    {
        CHECK(!vault->SaveNote(path, "safe", "unsafe", &error));
    }
#ifdef Q_OS_UNIX
    CHECK(
        QFile::link(
            outside.filePath(QStringLiteral("secret.md")),
            dir.filePath(QStringLiteral("shortcut.md"))));
    CHECK(QFile::link(outside.path(), dir.filePath(QStringLiteral("external"))));
    QByteArray bytes;
    CHECK(!vault->ReadNote(QStringLiteral("shortcut.md"), &bytes, &error));
    CHECK(!vault->SaveNote(QStringLiteral("external/secret.md"), "outside", "bad", &error));
    CHECK(ReadFile(outside.filePath(QStringLiteral("secret.md"))) == "outside");
#endif
}

static void PreservesUtf8BomWhenEditing()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("bom.md"));
    WriteFile(note, QByteArray::fromHex("efbbbf") + "original");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("bom.md"), &error));
    session.SetContent(QStringLiteral("updated"));
    CHECK(session.Save(&error));
    CHECK(ReadFile(note) == QByteArray::fromHex("efbbbf") + "updated");
}

static void RejectsInvalidUtf8AndKeepsPriorVaultOnFailure()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    WriteFile(dir.filePath(QStringLiteral("bad.md")), QByteArray::fromHex("ff"));
    WriteFile(dir.filePath(QStringLiteral("good.md")), "good");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(!session.SelectNote(QStringLiteral("bad.md"), &error));
    CHECK(error.contains(QStringLiteral("UTF-8")));
    CHECK(session.Path().isEmpty());
    CHECK(session.SelectNote(QStringLiteral("good.md"), &error));
    CHECK(!session.OpenVault(dir.filePath(QStringLiteral("missing")), &error));
    CHECK(session.Path() == QStringLiteral("good.md"));
    CHECK(session.Content() == QStringLiteral("good"));
}

static void SessionRetainsLocalEditsOnConflict()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    WriteFile(note, "original");
    WriteFile(dir.filePath(QStringLiteral("other.md")), "second");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));
    session.SetContent(QStringLiteral("first edit"));
    CHECK(session.Dirty());
    CHECK(!session.SelectNote(QStringLiteral("other.md"), &error));
    CHECK(session.Save(&error));
    CHECK(!session.Dirty());
    CHECK(ReadFile(note) == "first edit");
    session.SetContent(QStringLiteral("local edit"));
    WriteFile(note, "external edit");
    CHECK(!session.Save(&error));
    CHECK(error.contains(QStringLiteral("Save conflict")));
    CHECK(session.Dirty());
    CHECK(session.Content() == QStringLiteral("local edit"));
    CHECK(ReadFile(note) == "external edit");
    CHECK(!session.SelectNote(QStringLiteral("other.md"), &error));
}

static void SessionSyncsExternalEdits()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    WriteFile(note, "original");
    QString error;
    Session session;
    CHECK(session.SyncWithDisk(&error) == DiskChange::None); // No note yet.
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));
    CHECK(session.NotePath() == QFileInfo(note).canonicalFilePath());
    CHECK(session.SyncWithDisk(&error) == DiskChange::None);

    // A clean editor follows the disk, including a newly added BOM.
    WriteFile(note, QByteArray::fromHex("efbbbf") + "external");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Reloaded);
    CHECK(session.Content() == QStringLiteral("external"));
    CHECK(!session.Dirty());
    session.SetContent(QStringLiteral("saved"));
    CHECK(session.Save(&error));
    CHECK(ReadFile(note) == QByteArray::fromHex("efbbbf") + "saved");
    CHECK(session.SyncWithDisk(&error) == DiskChange::None); // Our own save.
    WriteFile(note, "no bom");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Reloaded);
    session.SetContent(QStringLiteral("still no bom"));
    CHECK(session.Save(&error));
    CHECK(ReadFile(note) == "still no bom");

    // Unsaved edits are kept, and saving still refuses to overwrite.
    session.SetContent(QStringLiteral("unsaved"));
    WriteFile(note, "external again");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(error.contains(QStringLiteral("changed on disk")));
    CHECK(session.Content() == QStringLiteral("unsaved"));
    CHECK(session.Dirty());
    CHECK(!session.Save(&error));
    CHECK(ReadFile(note) == "external again");
}

static void SessionKeepsTextWhenDiskCannotBeLoaded()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    WriteFile(note, "original");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));
    WriteFile(note, QByteArray::fromHex("ff"));
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(error.contains(QStringLiteral("UTF-8")));
    CHECK(session.Content() == QStringLiteral("original"));
    CHECK(QFile::remove(note));
    CHECK(session.SyncWithDisk(&error) == DiskChange::Missing);
    CHECK(session.Content() == QStringLiteral("original"));
    CHECK(session.Path() == QStringLiteral("note.md"));
}

static void SessionResolvesConflicts()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    WriteFile(note, QByteArray::fromHex("efbbbf") + "original");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));

    // Keep mine: replaces the outside version and leaves the session clean.
    session.SetContent(QStringLiteral("mine"));
    WriteFile(note, "theirs");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.OverwriteDisk(&error));
    CHECK(ReadFile(note) == QByteArray::fromHex("efbbbf") + "mine");
    CHECK(!session.Dirty());
    CHECK(session.SyncWithDisk(&error) == DiskChange::None);

    // Reload from disk: discards the edits for the outside version.
    session.SetContent(QStringLiteral("mine again"));
    WriteFile(note, "theirs again");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.ReloadFromDisk(&error));
    CHECK(session.Content() == QStringLiteral("theirs again"));
    CHECK(!session.Dirty());
    CHECK(ReadFile(note) == "theirs again");

    // Neither choice can act on a note that is gone; the text is kept.
    session.SetContent(QStringLiteral("orphaned"));
    CHECK(QFile::remove(note));
    CHECK(!session.ReloadFromDisk(&error));
    CHECK(!session.OverwriteDisk(&error));
    CHECK(session.Content() == QStringLiteral("orphaned"));
    CHECK(!QFile::exists(note));
}

static void KeepMineRefusesUnseenAndSwappedVersions()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    const QString other = dir.filePath(QStringLiteral("other.md"));
    WriteFile(note, "original");
    WriteFile(other, "other");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));
    session.SetContent(QStringLiteral("mine"));
    CHECK(!session.OverwriteDisk(&error)); // No conflict has been seen yet.
    CHECK(ReadFile(note) == "original");

    // A second outside change after the conflict was reported is not
    // overwritten; the next sync reports it instead.
    WriteFile(note, "theirs A");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    WriteFile(note, "theirs B");
    CHECK(!session.OverwriteDisk(&error));
    CHECK(ReadFile(note) == "theirs B");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.OverwriteDisk(&error));
    CHECK(ReadFile(note) == "mine");

#ifdef Q_OS_UNIX
    // A note swapped for a link to another note is never written through.
    session.SetContent(QStringLiteral("mine again"));
    CHECK(QFile::remove(note));
    CHECK(QFile::link(other, note));
    CHECK(session.SyncWithDisk(&error) == DiskChange::Missing);
    CHECK(!session.OverwriteDisk(&error));
    CHECK(!session.Save(&error));
    CHECK(ReadFile(other) == "other");
#endif
}

static void DiscardEditsLeavesDeletedNote()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    WriteFile(note, "original");
    WriteFile(dir.filePath(QStringLiteral("other.md")), "other");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));
    session.SetContent(QStringLiteral("unsaved"));
    CHECK(QFile::remove(note));
    CHECK(session.SyncWithDisk(&error) == DiskChange::Missing);
    session.DiscardEdits();
    CHECK(!session.Dirty());
    CHECK(session.Content() == QStringLiteral("original"));
    CHECK(session.SelectNote(QStringLiteral("other.md"), &error));
    CHECK(!QFile::exists(note));
}

static void CreatesNotesSafely()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    CHECK(dir.isValid() && outside.isValid());
    QString error;
    const auto vault = Vault::Open(dir.path(), &error);
    CHECK(vault.has_value());

    CHECK(vault->CreateNote(QStringLiteral("Folder/Sub/New.md"), &error));
    CHECK(ReadFile(dir.filePath(QStringLiteral("Folder/Sub/New.md"))).isEmpty());
    QStringList notes;
    CHECK(vault->ListNotes(&notes, &error));
    CHECK(notes == QStringList{QStringLiteral("Folder/Sub/New.md")});

    // Never overwrites an existing file.
    WriteFile(dir.filePath(QStringLiteral("Folder/Sub/New.md")), "kept");
    CHECK(!vault->CreateNote(QStringLiteral("Folder/Sub/New.md"), &error));
    CHECK(error.contains(QStringLiteral("exists")));
    CHECK(ReadFile(dir.filePath(QStringLiteral("Folder/Sub/New.md"))) == "kept");

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
        CHECK(!vault->CreateNote(path, &error));
    }
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("../escape.md"))));
    CHECK(!QDir(dir.filePath(QStringLiteral(".hidden"))).exists());

    // A rejected or failed create leaves no folders behind.
    QTemporaryDir empty;
    CHECK(empty.isValid());
    const auto fresh = Vault::Open(empty.path(), &error);
    CHECK(fresh.has_value());
    for (const QString &path :
         {QStringLiteral("x.md/.md"),
          QStringLiteral("Folder/.md"),
          QStringLiteral("A/B//c.md"),
          QStringLiteral("New/.hidden.md"),
          QStringLiteral("P/../q.md"),
          QStringLiteral("Long/Deeper/") + QString(300, u'a') + QStringLiteral(".md")})
    {
        CHECK(!fresh->CreateNote(path, &error));
        CHECK(QDir(empty.path()).isEmpty());
    }

#ifdef Q_OS_UNIX
    // Neither a linked folder nor a dangling link is written through.
    CHECK(QFile::link(outside.path(), dir.filePath(QStringLiteral("linked"))));
    CHECK(!vault->CreateNote(QStringLiteral("linked/note.md"), &error));
    CHECK(!vault->CreateNote(QStringLiteral("linked/Sub/note.md"), &error));
    const QString target = outside.filePath(QStringLiteral("target.md"));
    CHECK(QFile::link(target, dir.filePath(QStringLiteral("dangling.md"))));
    CHECK(!vault->CreateNote(QStringLiteral("dangling.md"), &error));
    CHECK(!QFile::exists(target));
    CHECK(QDir(outside.path()).isEmpty());
#endif
}

static void MergeOutsideEdits()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("note.md"));
    QString error;
    Session session;
    WriteFile(path, "one\ntwo\nthree\nfour\n");
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));
    session.SetContent(QStringLiteral("ONE\ntwo\nthree\nfour\n"));
    WriteFile(path, "one\ntwo\nthree\nFOUR\n");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.MergeOutsideChanges(&error));
    CHECK(session.Content() == QStringLiteral("ONE\ntwo\nthree\nFOUR\n"));
    CHECK(session.Dirty());
    CHECK(ReadFile(path) == "one\ntwo\nthree\nFOUR\n");
    WriteFile(path, "newer\n");
    CHECK(!session.Save(&error));
    CHECK(session.Content() == QStringLiteral("ONE\ntwo\nthree\nFOUR\n"));
    CHECK(ReadFile(path) == "newer\n");
    WriteFile(path, "one\ntwo\nthree\nFOUR\n");
    CHECK(session.Save(&error));
    CHECK(ReadFile(path) == "ONE\ntwo\nthree\nFOUR\n");

    WriteFile(path, "one\ntwo\nthree\nfour\n");
    CHECK(session.ReloadFromDisk(&error));
    session.SetContent(QStringLiteral("one\nlocal\nthree\nfour\n"));
    WriteFile(path, "one\noutside\nthree\nfour\n");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    WriteFile(path, "one\nnewer\nthree\nfour\n");
    CHECK(!session.MergeOutsideChanges(&error));
    CHECK(session.Content() == QStringLiteral("one\nlocal\nthree\nfour\n"));
    WriteFile(path, "one\noutside\nthree\nfour\n");
    CHECK(session.MergeOutsideChanges(&error));
    CHECK(session.Content().contains(
        QStringLiteral("<<<<<<< mine\nlocal\n=======\noutside\n>>>>>>> outside")));
    CHECK(ReadFile(path) == "one\noutside\nthree\nfour\n");
    CHECK(session.Save(&error)); // Markers are only written when explicitly saved.

    WriteFile(path, "one\ntwo\nthree\n");
    CHECK(session.ReloadFromDisk(&error));
    session.SetContent(QStringLiteral("one\nthree\n"));
    WriteFile(path, "one\nchanged\nthree\n");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.MergeOutsideChanges(&error));
    CHECK(session.Content().contains(QStringLiteral("<<<<<<< mine")));

    // An outside insertion inside a locally deleted region is a conflict:
    // applying it separately would silently restore deleted lines.
    WriteFile(path, "a\nb\nc\nd\ne");
    CHECK(session.ReloadFromDisk(&error));
    session.SetContent(QStringLiteral("a\nX\ne"));
    WriteFile(path, "a\nb\nc\nY\nd\ne");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.MergeOutsideChanges(&error));
    CHECK(session.Content().contains(QStringLiteral("<<<<<<< mine")));
    CHECK(session.Content().contains(QStringLiteral("a\n<<<<<<< mine\nX\n=======\nb\nc\nY\nd")));
    CHECK(ReadFile(path) == "a\nb\nc\nY\nd\ne");

    // Large notes fail safely rather than allocating a quadratic diff table.
    QByteArray lines;
    for (int i = 0; i < 1500; ++i)
        lines += "x\n";
    WriteFile(path, lines);
    CHECK(session.ReloadFromDisk(&error));
    session.SetContent(session.Content() + QStringLiteral("mine\n"));
    WriteFile(path, lines + "outside\n");
    CHECK(session.SyncWithDisk(&error) == DiskChange::Conflict);
    const QString edits = session.Content();
    CHECK(!session.MergeOutsideChanges(&error));
    CHECK(error.contains(QStringLiteral("too large")));
    CHECK(session.Content() == edits);
    CHECK(ReadFile(path) == lines + "outside\n");
}

static void SaveMissingAsNewNote()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    CHECK(dir.isValid() && outside.isValid());
    const QString path = dir.filePath(QStringLiteral("note.md"));
    WriteFile(path, "old");
    QString error;
    Session session;
    CHECK(session.OpenVault(dir.path(), &error));
    CHECK(session.SelectNote(QStringLiteral("note.md"), &error));
    session.SetContent(QStringLiteral("recovered"));
    CHECK(QFile::remove(path));
    CHECK(session.SyncWithDisk(&error) == DiskChange::Missing);
    WriteFile(path, "collision");
    CHECK(!session.SaveAsNewNote(&error));
    CHECK(ReadFile(path) == "collision");
    CHECK(session.Content() == QStringLiteral("recovered"));
    CHECK(QFile::remove(path));
#ifdef Q_OS_UNIX
    const QString target = outside.filePath(QStringLiteral("target.md"));
    CHECK(QFile::link(target, path));
    CHECK(!session.SaveAsNewNote(&error));
    CHECK(!QFile::exists(target));
    CHECK(QFile::remove(path));
#endif
    CHECK(session.SaveAsNewNote(&error));
    CHECK(ReadFile(path) == "recovered");
    CHECK(!session.Dirty());
    CHECK(session.Notes().contains(QStringLiteral("note.md")));
    const auto vault = Vault::Open(dir.path(), &error);
    CHECK(vault.has_value());
    CHECK(!vault->CreateNoteWithContent(QStringLiteral(".hidden.md"), "bad", &error));
    CHECK(!vault->CreateNoteWithContent(QStringLiteral("sub/../bad.md"), "bad", &error));
    CHECK(!vault->CreateNoteWithContent(QStringLiteral("note.md"), "bad", &error));
    CHECK(ReadFile(path) == "recovered");
#ifdef Q_OS_UNIX
    CHECK(QFile::link(outside.path(), dir.filePath(QStringLiteral("linked"))));
    CHECK(!vault->CreateNoteWithContent(QStringLiteral("linked/new.md"), "bad", &error));
    CHECK(QDir(outside.path()).isEmpty());
#endif
    CHECK(vault->CreateNoteWithContent(QStringLiteral("fresh/actual.md"), "new body", &error));
    CHECK(ReadFile(dir.filePath(QStringLiteral("fresh/actual.md"))) == "new body");
}

static void TrashesNotes()
{
    QTemporaryDir dir;
    QTemporaryDir trash;
    CHECK(dir.isValid() && trash.isValid());
    WriteFile(dir.filePath(QStringLiteral("note.md")), "note");
    WriteFile(dir.filePath(QStringLiteral("other.md")), "other");
    QString error;
    const auto vault = Vault::Open(dir.path(), &error);
    CHECK(vault.has_value());

    CHECK(vault->TrashNote(QStringLiteral("note.md"), TrashInto(trash.path()), &error));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("note.md"))));
    CHECK(ReadFile(trash.filePath(QStringLiteral("note.md"))) == "note");

    CHECK(!vault->TrashNote(QStringLiteral("note.md"), TrashInto(trash.path()), &error));
    CHECK(!vault->TrashNote(QStringLiteral("../x.md"), TrashInto(trash.path()), &error));

    const TrashFunction failing = [](const QString &, QString *trashError) {
        *trashError = QStringLiteral("No trash here.");
        return false;
    };
    CHECK(!vault->TrashNote(QStringLiteral("other.md"), failing, &error));
    CHECK(error == QStringLiteral("No trash here."));
    CHECK(ReadFile(dir.filePath(QStringLiteral("other.md"))) == "other");

#ifdef Q_OS_UNIX
    CHECK(
        QFile::link(
            dir.filePath(QStringLiteral("other.md")),
            dir.filePath(QStringLiteral("link.md"))));
    CHECK(!vault->TrashNote(QStringLiteral("link.md"), TrashInto(trash.path()), &error));
    CHECK(QFileInfo(dir.filePath(QStringLiteral("link.md"))).isSymLink());
#endif
}

static void SessionCreatesAndTrashesNotes()
{
    QTemporaryDir dir;
    QTemporaryDir trash;
    CHECK(dir.isValid() && trash.isValid());
    WriteFile(dir.filePath(QStringLiteral("note.md")), "note");
    QString error;
    Session session(TrashInto(trash.path()));
    CHECK(!session.TrashNote(&error)); // Nothing open yet.
    CHECK(session.OpenVault(dir.path(), &error));

    // The extension is added when missing and the new note is selected.
    CHECK(session.CreateNote(QStringLiteral(" Ideas "), &error));
    CHECK(session.Path() == QStringLiteral("Ideas.md"));
    CHECK(session.Content().isEmpty());
    CHECK(!session.Dirty());
    CHECK(session.Notes().contains(QStringLiteral("Ideas.md")));
    CHECK(session.CreateNote(QStringLiteral("Folder/Plan.MD"), &error));
    CHECK(session.Path() == QStringLiteral("Folder/Plan.MD"));
    // On case-insensitive file systems, an existing folder keeps its spelling.
    if (QFileInfo::exists(dir.filePath(QStringLiteral("folder")))) {
        CHECK(session.CreateNote(QStringLiteral("folder/Later"), &error));
        CHECK(session.Path() == QStringLiteral("Folder/Later.md"));
    }
    // Decomposed input (as pasted from Finder) is stored and listed composed.
    CHECK(session.CreateNote(QStringLiteral("Cafe\u0301"), &error));
    CHECK(session.Path() == QStringLiteral("Caf\u00e9.md"));
    CHECK(session.CreateNote(QStringLiteral("v1.2"), &error));
    CHECK(session.Path() == QStringLiteral("v1.2.md"));

    // An existing note is not replaced, and the selection stays.
    CHECK(!session.CreateNote(QStringLiteral("note"), &error));
    CHECK(session.Path() == QStringLiteral("v1.2.md"));
    CHECK(ReadFile(dir.filePath(QStringLiteral("note.md"))) == "note");

    // Unsaved edits block both, so nothing is lost silently.
    session.SetContent(QStringLiteral("unsaved"));
    CHECK(!session.CreateNote(QStringLiteral("Another"), &error));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("Another.md"))));
    CHECK(!session.TrashNote(&error));
    CHECK(QFile::exists(dir.filePath(QStringLiteral("v1.2.md"))));
    session.DiscardEdits();

    CHECK(session.TrashNote(&error));
    CHECK(session.Path().isEmpty() && session.Content().isEmpty());
    CHECK(!session.Notes().contains(QStringLiteral("v1.2.md")));
    CHECK(QFile::exists(trash.filePath(QStringLiteral("v1.2.md"))));
    CHECK(!session.TrashNote(&error));

#ifdef Q_OS_UNIX
    // A trashed note leaves the session even if the vault can't be relisted.
    CHECK(session.SelectNote(QStringLiteral("Ideas.md"), &error));
    const QByteArray locked = QFile::encodeName(dir.filePath(QStringLiteral("Folder")));
    CHECK(::chmod(locked.constData(), 0) == 0);
    const bool trashed = session.TrashNote(&error);
    CHECK(::chmod(locked.constData(), 0755) == 0);
    CHECK(trashed);
    CHECK(session.Path().isEmpty());
    CHECK(!session.Dirty());
    CHECK(!session.Notes().contains(QStringLiteral("Ideas.md")));
#endif
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        FixtureDiscovery();
        SavesAndRejectsConflicts();
        RejectsEscapesAndSymlinks();
        PreservesUtf8BomWhenEditing();
        RejectsInvalidUtf8AndKeepsPriorVaultOnFailure();
        SessionRetainsLocalEditsOnConflict();
        SessionSyncsExternalEdits();
        SessionKeepsTextWhenDiskCannotBeLoaded();
        SessionResolvesConflicts();
        KeepMineRefusesUnseenAndSwappedVersions();
        DiscardEditsLeavesDeletedNote();
        CreatesNotesSafely();
        MergeOutsideEdits();
        SaveMissingAsNewNote();
        TrashesNotes();
        SessionCreatesAndTrashesNotes();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
