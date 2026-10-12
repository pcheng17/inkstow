#include "SettingsStore.h"
#include "Theme.h"
#include "Window.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>

#include <algorithm>
#include <optional>
#include <stdexcept>

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

// Lets the file watcher deliver its notifications.
template<typename Done>
static bool waitFor(Done done)
{
    QDeadlineTimer deadline(5000);
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return done();
}

// Remembers settings for one test instead of touching the user's real settings.
class MemorySettings : public SettingsStore
{
public:
    QString lastVault() const override { return mLastVault; }
    bool setLastVault(const QString &path) override
    {
        mLastVault = path;
        return true;
    }

private:
    QString mLastVault;
};

// Stands in for settings that can't be written, such as a read-only config folder.
class FailingSettings : public SettingsStore
{
public:
    QString lastVault() const override { return {}; }
    bool setLastVault(const QString & /*path*/) override { return false; }
};

static bool refuseTrash(const QString & /*path*/, QString * /*error*/)
{
    return false;
}

static std::optional<QString> failIfPicked(QWidget * /*parent*/)
{
    throw std::runtime_error("The folder picker was shown unexpectedly");
}

// A last vault that can't be reopened leaves the empty state up with the reason.
static void checkLastVaultNotReopened(const QString &lastVault)
{
    const auto settings = std::make_shared<MemorySettings>();
    settings->setLastVault(lastVault);
    Window window(refuseTrash, {}, failIfPicked, settings);
    auto *emptyState = window.findChild<QWidget *>(QStringLiteral("emptyState"));
    auto *message = window.findChild<QLabel *>(QStringLiteral("emptyStateMessage"));
    auto *notes = window.findChild<QListWidget *>(QStringLiteral("notes"));
    CHECK(emptyState && message && notes);
    CHECK(emptyState->isVisibleTo(&window));
    CHECK(!notes->isVisibleTo(&window));
    CHECK(message->text().contains(lastVault));
    CHECK(window.windowTitle() == QStringLiteral("Inkstow"));
}

static void testMissingLastVault()
{
    QTemporaryDir parent;
    CHECK(parent.isValid());
    checkLastVaultNotReopened(QDir(parent.path()).filePath(QStringLiteral("Gone")));
    const QString file = QDir(parent.path()).filePath(QStringLiteral("not-a-folder.md"));
    writeFile(file, "note");
    checkLastVaultNotReopened(file);
}

static void testUnsavedLastVaultIsReported()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    writeFile(QDir(dir.path()).filePath(QStringLiteral("Welcome.md")), "welcome");
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<FailingSettings>());
    auto *open = window.findChild<QPushButton *>(QStringLiteral("openVault"));
    auto *notes = window.findChild<QListWidget *>(QStringLiteral("notes"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(open && notes && status);
    open->click();
    CHECK(notes->count() == 1); // The vault still opens.
    CHECK(status->text().contains(QStringLiteral("Couldn't remember this vault")));
}

static void testReopeningLastVault()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    writeFile(QDir(dir.path()).filePath(QStringLiteral("Welcome.md")), "welcome");
    const QString title = QStringLiteral("Inkstow - ") + QDir(dir.path()).dirName();
    const auto settings = std::make_shared<MemorySettings>();
    {
        std::optional<QString> picked;
        Window first(refuseTrash, {}, [&](QWidget *) { return picked; }, settings);
        CHECK(first.windowTitle() == QStringLiteral("Inkstow"));
        auto *open = first.findChild<QPushButton *>(QStringLiteral("openVault"));
        auto *emptyState = first.findChild<QWidget *>(QStringLiteral("emptyState"));
        CHECK(open && emptyState);
        CHECK(emptyState->isVisibleTo(&first));
        open->click(); // Cancelled.
        CHECK(emptyState->isVisibleTo(&first));
        CHECK(settings->lastVault().isEmpty());
        CHECK(first.windowTitle() == QStringLiteral("Inkstow"));
        picked = dir.path();
        open->click();
        CHECK(!emptyState->isVisibleTo(&first));
        CHECK(settings->lastVault() == dir.path());
        CHECK(first.windowTitle() == title);
    }
    Window second(refuseTrash, {}, failIfPicked, settings);
    auto *notes = second.findChild<QListWidget *>(QStringLiteral("notes"));
    CHECK(notes);
    CHECK(notes->count() == 1);
    CHECK(notes->item(0)->text() == QStringLiteral("Welcome.md"));
    CHECK(second.windowTitle() == title);
}

// Pins the current light look so moving style values into the theme changes nothing.
static void testCurrentStyleKept(const Window &window)
{
    const QPalette palette = QApplication::palette();
    const QColor ink(QStringLiteral("#24212b"));
    const QColor paper(QStringLiteral("#faf9fc"));
    CHECK(palette.color(QPalette::Window) == paper);
    CHECK(palette.color(QPalette::WindowText) == ink);
    CHECK(palette.color(QPalette::Base) == QColor(Qt::white));
    CHECK(palette.color(QPalette::AlternateBase) == paper);
    CHECK(palette.color(QPalette::Text) == ink);
    CHECK(palette.color(QPalette::Button) == QColor(Qt::white));
    CHECK(palette.color(QPalette::ButtonText) == ink);
    CHECK(palette.color(QPalette::Highlight) == QColor(QStringLiteral("#e7def4")));
    CHECK(palette.color(QPalette::HighlightedText) == ink);
    auto *title = window.findChild<QLabel *>(QStringLiteral("appTitle"));
    CHECK(title);
    CHECK(title->text() == QStringLiteral("Inkstow"));
    CHECK(title->font().bold());
    CHECK(title->font().pointSize() == 14);
    CHECK(window.size() == QSize(1100, 750));
}

// Opening a vault from the File menu and cancelling the picker changes nothing.
static void testCancellingOpenVault(Window &window, std::optional<QString> &picked)
{
    auto *switchVault = window.findChild<QAction *>(QStringLiteral("openVaultAction"));
    auto *notes = window.findChild<QListWidget *>(QStringLiteral("notes"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(switchVault && notes && status);
    CHECK(switchVault->shortcut() == QKeySequence(QKeySequence::Open));
    const auto menus = window.menuBar()->actions();
    CHECK(std::any_of(menus.begin(), menus.end(), [&](QAction *menu) {
        return menu->menu() && menu->menu()->actions().contains(switchVault);
    }));
    const int listed = notes->count();
    const QString openedStatus = status->text();
    const QString title = window.windowTitle();
    const std::optional<QString> vault = picked;
    picked.reset();
    switchVault->trigger();
    CHECK(notes->count() == listed);
    CHECK(status->text() == openedStatus);
    CHECK(window.windowTitle() == title);
    picked = vault;
}

// Unsaved edits block switching vaults, the same way they block switching notes.
// pickerCalls counts how often the window asked for a folder.
static void testVaultSwitchRefusedWhileUnsaved(
    Window &window, std::optional<QString> &picked, const int &pickerCalls)
{
    auto *switchVault = window.findChild<QAction *>(QStringLiteral("openVaultAction"));
    auto *notes = window.findChild<QListWidget *>(QStringLiteral("notes"));
    auto *editor = window.findChild<QPlainTextEdit *>(QStringLiteral("editor"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(switchVault && notes && editor && status);
    QTemporaryDir otherVault;
    CHECK(otherVault.isValid());
    writeFile(QDir(otherVault.path()).filePath(QStringLiteral("elsewhere.md")), "elsewhere");
    const int listed = notes->count();
    const QString text = editor->toPlainText();
    const QString title = window.windowTitle();
    const std::optional<QString> vault = picked;
    picked = otherVault.path();
    const int callsBefore = pickerCalls;
    switchVault->trigger();
    CHECK(pickerCalls == callsBefore); // Refused before asking for a folder.
    CHECK(status->text().contains(QStringLiteral("Save the current note")));
    CHECK(notes->count() == listed);
    CHECK(notes->findItems(QStringLiteral("elsewhere.md"), Qt::MatchExactly).isEmpty());
    CHECK(editor->toPlainText() == text);
    CHECK(window.windowTitle() == title);
    picked = vault;
}

static void testFollowingLinks(
    Window &window, const QDir &dir, const QStringList &openedAttachments)
{
    auto *notes = window.findChild<QListWidget *>(QStringLiteral("notes"));
    auto *editor = window.findChild<QPlainTextEdit *>(QStringLiteral("editor"));
    auto *follow = window.findChild<QPushButton *>(QStringLiteral("followLink"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(notes && editor && follow && status);
    writeFile(
        dir.filePath(QStringLiteral("other.md")),
        "[[note#Part|Alias]] [[missing]] [[asset.png]]");
    writeFile(dir.filePath(QStringLiteral("asset.png")), "attachment");
    notes->setCurrentRow(1);
    const auto cursorAt = [&](const QString &needle) {
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(static_cast<int>(editor->toPlainText().indexOf(needle) + 2));
        editor->setTextCursor(cursor);
    };
    cursorAt(QStringLiteral("[[missing]]"));
    follow->click();
    CHECK(status->text() == QStringLiteral("Unresolved link"));
    cursorAt(QStringLiteral("[[asset.png]]"));
    follow->click();
    CHECK(status->text() == QStringLiteral("Opened attachment: asset.png"));
    CHECK(
        openedAttachments ==
        QStringList{QFileInfo(dir.filePath(QStringLiteral("asset.png"))).canonicalFilePath()});
    CHECK(editor->toPlainText() == QStringLiteral("[[note#Part|Alias]] [[missing]] [[asset.png]]"));
    CHECK(readFile(dir.filePath(QStringLiteral("asset.png"))) == "attachment");
    // The cursor at the exclusive end of ]] must not select this link.
    QTextCursor after = editor->textCursor();
    after.setPosition(
        static_cast<int>(
            editor->toPlainText().indexOf(QStringLiteral("[[asset.png]]")) +
            QStringLiteral("[[asset.png]]").size()));
    editor->setTextCursor(after);
    follow->click();
    CHECK(status->text().contains(QStringLiteral("cursor inside")));
    CHECK(openedAttachments.size() == 1);
    // Make sure unsaved edits cannot be abandoned through Follow link.
    editor->setPlainText(QStringLiteral("[[note#Part|Alias]] [[asset.png]] draft"));
    cursorAt(QStringLiteral("[[note#Part|Alias]]"));
    follow->click();
    CHECK(notes->currentItem()->text() == QStringLiteral("other.md"));
    CHECK(status->text().contains(QStringLiteral("Save the current note")));
    cursorAt(QStringLiteral("[[asset.png]]"));
    follow->click();
    CHECK(status->text().contains(QStringLiteral("Save the current note")));
    CHECK(openedAttachments.size() == 1);
    CHECK(
        readFile(dir.filePath(QStringLiteral("other.md"))) ==
        "[[note#Part|Alias]] [[missing]] [[asset.png]]");
    editor->setPlainText(QStringLiteral("[[note#Part|Alias]] [[missing]] [[asset.png]]"));
    cursorAt(QStringLiteral("[[note#Part|Alias]]"));
    follow->click();
    CHECK(notes->currentItem()->text() == QStringLiteral("note.md"));
    CHECK(editor->toPlainText() == QStringLiteral("original"));
    CHECK(
        readFile(dir.filePath(QStringLiteral("other.md"))) ==
        "[[note#Part|Alias]] [[missing]] [[asset.png]]");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        configureLightTheme(app);
        CHECK(app.palette().color(QPalette::Window) == QColor(QStringLiteral("#faf9fc")));
        QTemporaryDir probeDir;
        CHECK(probeDir.isValid());
        QFileSystemWatcher probe;
        CHECK(probe.addPath(probeDir.path()));
        bool notified = false;
        QObject::connect(&probe, &QFileSystemWatcher::directoryChanged, &app, [&] {
            notified = true;
        });
        writeFile(probeDir.filePath(QStringLiteral("probe.md")), "probe");
        const bool watcherWorks = waitFor([&] { return notified; }) &&
                                  !qEnvironmentVariableIsSet("INKSTOW_TEST_NO_EVENTS");
        if (!watcherWorks)
            qInfo(
                "File system events unavailable or disabled; skipping watcher-only checks; using "
                "activation "
                "for sync checks.");

        QTemporaryDir dir;
        CHECK(dir.isValid());
        const QString note = dir.filePath(QStringLiteral("note.md"));
        writeFile(note, "original");
        writeFile(dir.filePath(QStringLiteral("other.md")), "other");
        writeFile(dir.filePath(QStringLiteral(".hidden.md")), "hidden");

        QTemporaryDir trash;
        CHECK(trash.isValid());
        QStringList openedAttachments;
        std::optional<QString> picked = dir.path();
        int pickerCalls = 0;
        Window window(
            [&](const QString &path, QString *) {
                return QFile::rename(path, QDir(trash.path()).filePath(QFileInfo(path).fileName()));
            },
            [&](const QUrl &url) {
                CHECK(url.isLocalFile());
                openedAttachments.append(url.toLocalFile());
                return true;
            },
            [&](QWidget *) {
                ++pickerCalls;
                return picked;
            },
            std::make_shared<MemorySettings>());
        testCurrentStyleKept(window);
        testReopeningLastVault();
        testMissingLastVault();
        testUnsavedLastVaultIsReported();
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
            open && notes && editor && save && status && conflictBar && keepMine && reload &&
            discard && merge && saveAsNew && newPath && create && remove && deleteBar &&
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
        open->click();
        CHECK(notes->count() == 2);
        CHECK(notes->item(0)->text() == QStringLiteral("note.md"));
        testCancellingOpenVault(window, picked);
        testFollowingLinks(window, QDir(dir.path()), openedAttachments);
        editor->setPlainText(QStringLiteral("first edit"));
        CHECK(save->isEnabled());
        notes->setCurrentRow(1);
        CHECK(notes->currentRow() == 0); // Unsaved note cannot be switched away.
        testVaultSwitchRefusedWhileUnsaved(window, picked, pickerCalls); // Nor can its vault.
        save->click();
        CHECK(readFile(note) == "first edit");
        CHECK(!save->isEnabled());

        // External additions, renames and deletions refresh the list without
        // replacing the open note or exposing hidden entries.
        const QString nested = dir.filePath(QStringLiteral("nested"));
        CHECK(QDir().mkdir(nested));
        writeFile(QDir(nested).filePath(QStringLiteral("new.md")), "new");
        syncWithoutWatcher();
        CHECK(waitFor([&] {
            return notes->findItems(QStringLiteral("nested/new.md"), Qt::MatchExactly).size() == 1;
        }));
        CHECK(notes->currentItem()->text() == QStringLiteral("note.md"));
        CHECK(editor->toPlainText() == QStringLiteral("first edit"));
        writeFile(QDir(nested).filePath(QStringLiteral("later.md")), "later");
        syncWithoutWatcher();
        CHECK(waitFor([&] {
            return notes->findItems(QStringLiteral("nested/later.md"), Qt::MatchExactly).size() ==
                   1;
        }));
        CHECK(
            QFile::rename(
                QDir(nested).filePath(QStringLiteral("new.md")),
                QDir(nested).filePath(QStringLiteral("renamed.md"))));
        syncWithoutWatcher();
        CHECK(waitFor([&] {
            return notes->findItems(QStringLiteral("nested/renamed.md"), Qt::MatchExactly).size() ==
                       1 &&
                   notes->findItems(QStringLiteral("nested/new.md"), Qt::MatchExactly).isEmpty();
        }));
        CHECK(QFile::remove(QDir(nested).filePath(QStringLiteral("renamed.md"))));
        CHECK(QFile::remove(QDir(nested).filePath(QStringLiteral("later.md"))));
        CHECK(QDir().rmdir(nested));
        syncWithoutWatcher();
        CHECK(waitFor([&] {
            return notes->findItems(QStringLiteral("nested/later.md"), Qt::MatchExactly).isEmpty();
        }));

        // Losing and recreating the selected note's folder must not strand
        // the watcher: a later write must still reach the editor.
        if (watcherWorks) {
            CHECK(QDir().mkdir(nested));
            const QString subnote = QDir(nested).filePath(QStringLiteral("note.md"));
            writeFile(subnote, "sub original");
            CHECK(waitFor([&] {
                return !notes->findItems(QStringLiteral("nested/note.md"), Qt::MatchExactly)
                            .isEmpty();
            }));
            notes->setCurrentItem(
                notes->findItems(QStringLiteral("nested/note.md"), Qt::MatchExactly).first());
            CHECK(editor->toPlainText() == QStringLiteral("sub original"));
            CHECK(QFile::remove(subnote));
            CHECK(QDir().rmdir(nested));
            CHECK(waitFor([&] {
                return notes->findItems(QStringLiteral("nested/note.md"), Qt::MatchExactly)
                    .isEmpty();
            }));
            CHECK(QDir().mkdir(nested));
            writeFile(subnote, "sub recreated");
            CHECK(
                waitFor([&] { return editor->toPlainText() == QStringLiteral("sub recreated"); }));
            writeFile(subnote, "sub changed again");
            CHECK(waitFor(
                [&] { return editor->toPlainText() == QStringLiteral("sub changed again"); }));
            notes->setCurrentItem(
                notes->findItems(QStringLiteral("note.md"), Qt::MatchExactly).first());
        }

        writeFile(note, "outside edit");
        syncWithoutWatcher();
        CHECK(waitFor([&] { return editor->toPlainText() == QStringLiteral("outside edit"); }));
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
                CHECK(waitFor([&] { return editor->toPlainText() == QString::fromUtf8(bytes); }));
            }

        editor->setPlainText(QStringLiteral("local edit"));
        writeFile(note, "external edit");
        syncWithoutWatcher();
        CHECK(waitFor([&] { return status->text().contains(QStringLiteral("changed on disk")); }));
        CHECK(editor->toPlainText() == QStringLiteral("local edit"));
        save->click();
        CHECK(status->text().contains(QStringLiteral("Save conflict")));
        CHECK(editor->toPlainText() == QStringLiteral("local edit"));
        CHECK(readFile(note) == "external edit");
        CHECK(save->isEnabled());
        CHECK(!conflictBar->isHidden());

        CHECK(!merge->isHidden());
        CHECK(saveAsNew->isHidden());
        merge->click();
        CHECK(editor->toPlainText().contains(QStringLiteral("<<<<<<< mine")));
        CHECK(readFile(note) == "external edit");
        CHECK(conflictBar->isHidden());
        editor->setPlainText(QStringLiteral("local edit"));
        save->click();
        CHECK(readFile(note) == "local edit");

        editor->setPlainText(QStringLiteral("another local"));
        writeFile(note, "another outside");
        save->click();
        keepMine->click();
        CHECK(readFile(note) == "another local");
        CHECK(conflictBar->isHidden());
        CHECK(!save->isEnabled());

        editor->setPlainText(QStringLiteral("mine again"));
        writeFile(note, "theirs");
        syncWithoutWatcher();
        CHECK(waitFor([&] { return !conflictBar->isHidden(); }));
        reload->click();
        CHECK(editor->toPlainText() == QStringLiteral("theirs"));
        CHECK(conflictBar->isHidden());
        CHECK(!save->isEnabled());
        editor->undo(); // Reloading can be undone, bringing the edits back.
        CHECK(editor->toPlainText() == QStringLiteral("mine again"));
        CHECK(save->isEnabled());
        CHECK(conflictBar->isHidden()); // The outside version has been seen.
        save->click();
        CHECK(readFile(note) == "mine again");

        // Save alone, before the watcher reports anything, also shows the bar.
        editor->setPlainText(QStringLiteral("mine 3"));
        writeFile(note, "theirs 3");
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
        CHECK(waitFor([&] { return !conflictBar->isHidden(); }));
        CHECK(!discard->isHidden());
        CHECK(keepMine->isHidden());
        CHECK(merge->isHidden());
        CHECK(!saveAsNew->isHidden());
        CHECK(reload->isHidden());
        writeFile(note, "collision");
        saveAsNew->click();
        CHECK(status->text().startsWith(QStringLiteral("Error")));
        CHECK(editor->toPlainText() == QStringLiteral("orphaned"));
        CHECK(readFile(note) == "collision");
        CHECK(QFile::remove(note));
        saveAsNew->click();
        CHECK(readFile(note) == "orphaned");
        CHECK(conflictBar->isHidden());
        CHECK(!save->isEnabled());
        editor->setPlainText(QStringLiteral("orphaned again"));
        CHECK(QFile::remove(note));
        syncWithoutWatcher();
        CHECK(waitFor([&] { return !conflictBar->isHidden(); }));
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

        // Rename/move asks for a destination and explicitly confirms link updates.
        auto *move = window.findChild<QPushButton *>(QStringLiteral("moveNote"));
        CHECK(move && move->isEnabled());
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            CHECK(dialog);
            auto *input = dialog->findChild<QLineEdit *>();
            CHECK(input);
            input->setText(QStringLiteral("Moved"));
            dialog->accept();
            QTimer::singleShot(0, [&] {
                auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                CHECK(confirmation);
                CHECK(confirmation->defaultButton());
                confirmation->defaultButton()->click();
            });
        });
        move->click();
        CHECK(QFile::exists(dir.filePath(QStringLiteral("Moved.md"))));
        CHECK(notes->currentItem()->text() == QStringLiteral("Moved.md"));
        CHECK(!QFile::exists(dir.filePath(QStringLiteral("Ideas.md"))));

        // Unsaved edits disable both, and a confirmation never outlives its note.
        editor->setPlainText(QStringLiteral("draft"));
        CHECK(!create->isEnabled());
        CHECK(!remove->isEnabled());
        CHECK(!move->isEnabled());
        editor->setPlainText(QString());
        CHECK(create->isEnabled() && remove->isEnabled());
        remove->click();
        CHECK(!deleteBar->isHidden());
        notes->setCurrentItem(
            notes->findItems(QStringLiteral("other.md"), Qt::MatchExactly).first());
        CHECK(deleteBar->isHidden());
        CHECK(QFile::exists(dir.filePath(QStringLiteral("other.md"))));
        notes->setCurrentItem(
            notes->findItems(QStringLiteral("Moved.md"), Qt::MatchExactly).first());
        CHECK(deleteBar->isHidden());

        // Deleting asks first, and Cancel leaves the note alone.
        CHECK(remove->isEnabled() && deleteBar->isHidden());
        remove->click();
        CHECK(!deleteBar->isHidden());
        cancelDelete->click();
        CHECK(deleteBar->isHidden());
        CHECK(QFile::exists(dir.filePath(QStringLiteral("Moved.md"))));
        remove->click();
        confirmDelete->click();
        CHECK(deleteBar->isHidden());
        CHECK(!QFile::exists(dir.filePath(QStringLiteral("Moved.md"))));
        CHECK(QFile::exists(QDir(trash.path()).filePath(QStringLiteral("Moved.md"))));
        CHECK(notes->findItems(QStringLiteral("Moved.md"), Qt::MatchExactly).isEmpty());
        CHECK(!editor->isEnabled());
        CHECK(!remove->isEnabled());
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
