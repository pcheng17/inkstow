#include "Theme.h"
#include "Window.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>

#include <stdexcept>

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

// Lets the file watcher deliver its notifications.
template<typename Done>
static bool WaitFor(Done done)
{
    QDeadlineTimer deadline(5000);
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return done();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        ConfigureLightTheme(app);
        CHECK(app.palette().color(QPalette::Window) == QColor(QStringLiteral("#faf9fc")));
        QTemporaryDir probeDir;
        CHECK(probeDir.isValid());
        QFileSystemWatcher probe;
        CHECK(probe.addPath(probeDir.path()));
        bool notified = false;
        QObject::connect(&probe, &QFileSystemWatcher::directoryChanged, &app, [&] {
            notified = true;
        });
        WriteFile(probeDir.filePath(QStringLiteral("probe.md")), "probe");
        const bool watcherWorks = WaitFor([&] { return notified; }) &&
                                  !qEnvironmentVariableIsSet("INKSTOW_TEST_NO_EVENTS");
        if (!watcherWorks)
            qInfo(
                "File system events unavailable or disabled; skipping watcher-only checks; using "
                "activation "
                "for sync checks.");

        QTemporaryDir dir;
        CHECK(dir.isValid());
        const QString note = dir.filePath(QStringLiteral("note.md"));
        WriteFile(note, "original");
        WriteFile(dir.filePath(QStringLiteral("other.md")), "other");
        WriteFile(dir.filePath(QStringLiteral(".hidden.md")), "hidden");

        QTemporaryDir trash;
        CHECK(trash.isValid());
        Window window([&](const QString &path, QString *) {
            return QFile::rename(path, QDir(trash.path()).filePath(QFileInfo(path).fileName()));
        });
        auto *folder = window.findChild<QLineEdit *>(QStringLiteral("vaultFolder"));
        auto *open = window.findChild<QPushButton *>(QStringLiteral("openVault"));
        auto *notes = window.findChild<QListWidget *>(QStringLiteral("notes"));
        auto *editor = window.findChild<QPlainTextEdit *>(QStringLiteral("editor"));
        auto *save = window.findChild<QPushButton *>(QStringLiteral("saveNote"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
        auto *conflictBar = window.findChild<QWidget *>(QStringLiteral("conflictBar"));
        auto *keepMine = window.findChild<QPushButton *>(QStringLiteral("keepMine"));
        auto *reload = window.findChild<QPushButton *>(QStringLiteral("reloadFromDisk"));
        auto *discard = window.findChild<QPushButton *>(QStringLiteral("discardEdits"));
        auto *merge = window.findChild<QPushButton *>(QStringLiteral("mergeOutsideChanges"));
        auto *saveAsNew = window.findChild<QPushButton *>(QStringLiteral("saveAsNewNote"));
        auto *newPath = window.findChild<QLineEdit *>(QStringLiteral("newNotePath"));
        auto *create = window.findChild<QPushButton *>(QStringLiteral("createNote"));
        auto *remove = window.findChild<QPushButton *>(QStringLiteral("deleteNote"));
        auto *deleteBar = window.findChild<QWidget *>(QStringLiteral("deleteBar"));
        auto *confirmDelete = window.findChild<QPushButton *>(QStringLiteral("confirmDelete"));
        auto *cancelDelete = window.findChild<QPushButton *>(QStringLiteral("cancelDelete"));
        CHECK(
            folder && open && notes && editor && save && status && conflictBar && keepMine &&
            reload && discard && merge && saveAsNew && newPath && create && remove && deleteBar &&
            confirmDelete && cancelDelete);
        CHECK(!create->isEnabled()); // No vault yet.
        CHECK(!remove->isEnabled());
        CHECK(conflictBar->isHidden());
        window.show();
        window.activateWindow();
        const auto syncWithoutWatcher = [&] {
            if (!watcherWorks) {
                QEvent activation(QEvent::ActivationChange);
                QCoreApplication::sendEvent(&window, &activation);
            }
        };
        folder->setText(dir.path());
        open->click();
        CHECK(notes->count() == 2);
        CHECK(notes->item(0)->text() == QStringLiteral("note.md"));
        notes->setCurrentRow(0);
        CHECK(editor->toPlainText() == QStringLiteral("original"));
        editor->setPlainText(QStringLiteral("first edit"));
        CHECK(save->isEnabled());
        notes->setCurrentRow(1);
        CHECK(notes->currentRow() == 0); // Unsaved note cannot be switched away.
        save->click();
        CHECK(ReadFile(note) == "first edit");
        CHECK(!save->isEnabled());

        // External additions, renames and deletions refresh the list without
        // replacing the open note or exposing hidden entries.
        const QString nested = dir.filePath(QStringLiteral("nested"));
        CHECK(QDir().mkdir(nested));
        WriteFile(QDir(nested).filePath(QStringLiteral("new.md")), "new");
        syncWithoutWatcher();
        CHECK(WaitFor([&] {
            return notes->findItems(QStringLiteral("nested/new.md"), Qt::MatchExactly).size() == 1;
        }));
        CHECK(notes->currentItem()->text() == QStringLiteral("note.md"));
        CHECK(editor->toPlainText() == QStringLiteral("first edit"));
        WriteFile(QDir(nested).filePath(QStringLiteral("later.md")), "later");
        syncWithoutWatcher();
        CHECK(WaitFor([&] {
            return notes->findItems(QStringLiteral("nested/later.md"), Qt::MatchExactly).size() ==
                   1;
        }));
        CHECK(
            QFile::rename(
                QDir(nested).filePath(QStringLiteral("new.md")),
                QDir(nested).filePath(QStringLiteral("renamed.md"))));
        syncWithoutWatcher();
        CHECK(WaitFor([&] {
            return notes->findItems(QStringLiteral("nested/renamed.md"), Qt::MatchExactly).size() ==
                       1 &&
                   notes->findItems(QStringLiteral("nested/new.md"), Qt::MatchExactly).isEmpty();
        }));
        CHECK(QFile::remove(QDir(nested).filePath(QStringLiteral("renamed.md"))));
        CHECK(QFile::remove(QDir(nested).filePath(QStringLiteral("later.md"))));
        CHECK(QDir().rmdir(nested));
        syncWithoutWatcher();
        CHECK(WaitFor([&] {
            return notes->findItems(QStringLiteral("nested/later.md"), Qt::MatchExactly).isEmpty();
        }));

        // Losing and recreating the selected note's folder must not strand
        // the watcher: a later write must still reach the editor.
        if (watcherWorks) {
            CHECK(QDir().mkdir(nested));
            const QString subnote = QDir(nested).filePath(QStringLiteral("note.md"));
            WriteFile(subnote, "sub original");
            CHECK(WaitFor([&] {
                return !notes->findItems(QStringLiteral("nested/note.md"), Qt::MatchExactly)
                            .isEmpty();
            }));
            notes->setCurrentItem(
                notes->findItems(QStringLiteral("nested/note.md"), Qt::MatchExactly).first());
            CHECK(editor->toPlainText() == QStringLiteral("sub original"));
            CHECK(QFile::remove(subnote));
            CHECK(QDir().rmdir(nested));
            CHECK(WaitFor([&] {
                return notes->findItems(QStringLiteral("nested/note.md"), Qt::MatchExactly)
                    .isEmpty();
            }));
            CHECK(QDir().mkdir(nested));
            WriteFile(subnote, "sub recreated");
            CHECK(
                WaitFor([&] { return editor->toPlainText() == QStringLiteral("sub recreated"); }));
            WriteFile(subnote, "sub changed again");
            CHECK(WaitFor(
                [&] { return editor->toPlainText() == QStringLiteral("sub changed again"); }));
            notes->setCurrentItem(
                notes->findItems(QStringLiteral("note.md"), Qt::MatchExactly).first());
        }

        WriteFile(note, "outside edit");
        syncWithoutWatcher();
        CHECK(WaitFor([&] { return editor->toPlainText() == QStringLiteral("outside edit"); }));
        CHECK(!save->isEnabled());
        CHECK(status->text().contains(QStringLiteral("Reloaded")));

        // Atomic replacement, as many editors save, drops the file from the
        // watcher; later changes must still be noticed.
        if (watcherWorks)
            for (const QByteArray &bytes :
                 {QByteArray("renamed once"), QByteArray("renamed twice")})
            {
                QSaveFile replacement(note);
                CHECK(replacement.open(QIODevice::WriteOnly));
                CHECK(replacement.write(bytes) == bytes.size());
                CHECK(replacement.commit());
                CHECK(WaitFor([&] { return editor->toPlainText() == QString::fromUtf8(bytes); }));
            }

        editor->setPlainText(QStringLiteral("local edit"));
        WriteFile(note, "external edit");
        syncWithoutWatcher();
        CHECK(WaitFor([&] { return status->text().contains(QStringLiteral("changed on disk")); }));
        CHECK(editor->toPlainText() == QStringLiteral("local edit"));
        save->click();
        CHECK(status->text().contains(QStringLiteral("Save conflict")));
        CHECK(editor->toPlainText() == QStringLiteral("local edit"));
        CHECK(ReadFile(note) == "external edit");
        CHECK(save->isEnabled());
        CHECK(!conflictBar->isHidden());

        CHECK(!merge->isHidden());
        CHECK(saveAsNew->isHidden());
        merge->click();
        CHECK(editor->toPlainText().contains(QStringLiteral("<<<<<<< mine")));
        CHECK(ReadFile(note) == "external edit");
        CHECK(conflictBar->isHidden());
        editor->setPlainText(QStringLiteral("local edit"));
        save->click();
        CHECK(ReadFile(note) == "local edit");

        editor->setPlainText(QStringLiteral("another local"));
        WriteFile(note, "another outside");
        save->click();
        keepMine->click();
        CHECK(ReadFile(note) == "another local");
        CHECK(conflictBar->isHidden());
        CHECK(!save->isEnabled());

        editor->setPlainText(QStringLiteral("mine again"));
        WriteFile(note, "theirs");
        syncWithoutWatcher();
        CHECK(WaitFor([&] { return !conflictBar->isHidden(); }));
        reload->click();
        CHECK(editor->toPlainText() == QStringLiteral("theirs"));
        CHECK(conflictBar->isHidden());
        CHECK(!save->isEnabled());
        editor->undo(); // Reloading can be undone, bringing the edits back.
        CHECK(editor->toPlainText() == QStringLiteral("mine again"));
        CHECK(save->isEnabled());
        CHECK(conflictBar->isHidden()); // The outside version has been seen.
        save->click();
        CHECK(ReadFile(note) == "mine again");

        // Save alone, before the watcher reports anything, also shows the bar.
        editor->setPlainText(QStringLiteral("mine 3"));
        WriteFile(note, "theirs 3");
        save->click();
        CHECK(!conflictBar->isHidden());
        CHECK(!keepMine->isHidden());
        CHECK(discard->isHidden());
        reload->click();
        CHECK(editor->toPlainText() == QStringLiteral("theirs 3"));

        // A deleted note with unsaved edits can be discarded to move on.
        editor->setPlainText(QStringLiteral("orphaned"));
        CHECK(QFile::remove(note));
        syncWithoutWatcher();
        CHECK(WaitFor([&] { return !conflictBar->isHidden(); }));
        CHECK(!discard->isHidden());
        CHECK(keepMine->isHidden());
        CHECK(merge->isHidden());
        CHECK(!saveAsNew->isHidden());
        CHECK(reload->isHidden());
        WriteFile(note, "collision");
        saveAsNew->click();
        CHECK(status->text().startsWith(QStringLiteral("Error")));
        CHECK(editor->toPlainText() == QStringLiteral("orphaned"));
        CHECK(ReadFile(note) == "collision");
        CHECK(QFile::remove(note));
        saveAsNew->click();
        CHECK(ReadFile(note) == "orphaned");
        CHECK(conflictBar->isHidden());
        CHECK(!save->isEnabled());
        editor->setPlainText(QStringLiteral("orphaned again"));
        CHECK(QFile::remove(note));
        syncWithoutWatcher();
        CHECK(WaitFor([&] { return !conflictBar->isHidden(); }));
        discard->click();
        CHECK(conflictBar->isHidden());
        CHECK(!save->isEnabled());
        notes->setCurrentItem(
            notes->findItems(QStringLiteral("other.md"), Qt::MatchExactly).first());
        CHECK(notes->currentItem()->text() == QStringLiteral("other.md"));

        // Creating a note adds it to the list and opens it.
        newPath->setText(QStringLiteral("Ideas"));
        create->click();
        CHECK(QFile::exists(dir.filePath(QStringLiteral("Ideas.md"))));
        CHECK(notes->currentItem() && notes->currentItem()->text() == QStringLiteral("Ideas.md"));
        CHECK(notes->findItems(QStringLiteral("note.md"), Qt::MatchExactly).isEmpty());
        CHECK(editor->isEnabled() && editor->toPlainText().isEmpty());
        CHECK(newPath->text().isEmpty());
        newPath->setText(QStringLiteral("../escape"));
        create->click();
        CHECK(status->text().startsWith(QStringLiteral("Error")));
        CHECK(newPath->text() == QStringLiteral("../escape"));

        // Unsaved edits disable both, and a confirmation never outlives its note.
        editor->setPlainText(QStringLiteral("draft"));
        CHECK(!create->isEnabled());
        CHECK(!remove->isEnabled());
        editor->setPlainText(QString());
        CHECK(create->isEnabled() && remove->isEnabled());
        remove->click();
        CHECK(!deleteBar->isHidden());
        notes->setCurrentItem(
            notes->findItems(QStringLiteral("other.md"), Qt::MatchExactly).first());
        CHECK(deleteBar->isHidden());
        CHECK(QFile::exists(dir.filePath(QStringLiteral("other.md"))));
        notes->setCurrentItem(
            notes->findItems(QStringLiteral("Ideas.md"), Qt::MatchExactly).first());
        CHECK(deleteBar->isHidden());

        // Deleting asks first, and Cancel leaves the note alone.
        CHECK(remove->isEnabled() && deleteBar->isHidden());
        remove->click();
        CHECK(!deleteBar->isHidden());
        cancelDelete->click();
        CHECK(deleteBar->isHidden());
        CHECK(QFile::exists(dir.filePath(QStringLiteral("Ideas.md"))));
        remove->click();
        confirmDelete->click();
        CHECK(deleteBar->isHidden());
        CHECK(!QFile::exists(dir.filePath(QStringLiteral("Ideas.md"))));
        CHECK(QFile::exists(QDir(trash.path()).filePath(QStringLiteral("Ideas.md"))));
        CHECK(notes->findItems(QStringLiteral("Ideas.md"), Qt::MatchExactly).isEmpty());
        CHECK(!editor->isEnabled());
        CHECK(!remove->isEnabled());
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
