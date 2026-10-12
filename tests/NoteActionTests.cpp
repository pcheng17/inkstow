#include "NoteTree.h"
#include "Theme.h"
#include "UiTestSupport.h"
#include "Window.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
using NoteFiles = std::vector<std::pair<QString, QByteArray>>;

// A temporary vault open in a shown, active window. Its trash moves notes into
// another temporary folder, and its folder picker answers this vault only once.
class TestVault
{
public:
    explicit TestVault(const NoteFiles &notes)
    {
        CHECK(mDir.isValid() && mTrash.isValid());
        for (const auto &[path, text] : notes) {
            CHECK(QFileInfo(filePath(path)).dir().mkpath(QStringLiteral(".")));
            writeFile(filePath(path), text);
        }
        mWindow = std::make_unique<Window>(
            [this](const QString &path, QString * /*error*/) {
                return QFile::rename(
                    path,
                    QDir(mTrash.path()).filePath(QFileInfo(path).fileName()));
            },
            [](const QUrl & /*url*/) { return true; },
            [this](QWidget * /*parent*/) {
                ++mPicks;
                return mPicks == 1 ? std::optional(mDir.path()) : std::nullopt;
            },
            std::make_shared<MemorySettings>());
        mWindow->show();
        activate();
        action(QStringLiteral("openVaultAction"))->trigger();
    }

    // Shortcuts and focus need an active window.
    void activate()
    {
        mWindow->activateWindow();
        CHECK(waitFor([&] { return mWindow->isActiveWindow(); }));
    }
    Window &window() { return *mWindow; }
    QString filePath(const QString &relative) const { return QDir(mDir.path()).filePath(relative); }
    bool trashed(const QString &name) const
    {
        return QFile::exists(QDir(mTrash.path()).filePath(name));
    }
    int picks() const { return mPicks; }

    template<typename T>
    T *find(const QString &name)
    {
        auto *found = mWindow->findChild<T *>(name);
        CHECK(found);
        return found;
    }
    QAction *action(const QString &name) { return find<QAction>(name); }
    QTreeWidget *tree() { return find<QTreeWidget>(QStringLiteral("notes")); }
    QTextEdit *editor() { return find<QTextEdit>(QStringLiteral("editor")); }

private:
    QTemporaryDir mDir;
    QTemporaryDir mTrash;
    int mPicks = 0;
    std::unique_ptr<Window> mWindow; // Destroyed before the folders it uses.
};

// Presses a shortcut in widget as a user would, so the window's shortcuts see
// it before the widget does.
void pressShortcut(QWidget *widget, const QKeySequence &shortcut)
{
    CHECK(shortcut.count() == 1);
    const QKeyCombination key = shortcut[0];
    QTest::keyClick(widget, key.key(), key.keyboardModifiers());
}

// Answers the next Rename / move prompts with destination and the default
// choice, Update links.
void answerMovePrompts(const QString &destination)
{
    QTimer::singleShot(0, [destination] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        CHECK(dialog);
        auto *input = dialog->findChild<QLineEdit *>();
        CHECK(input);
        input->setText(destination);
        dialog->accept();
        QTimer::singleShot(0, [] {
            auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            CHECK(confirmation && confirmation->defaultButton());
            confirmation->defaultButton()->click();
        });
    });
}

void moveCursorTo(QTextEdit *editor, int position)
{
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(position);
    editor->setTextCursor(cursor);
}

bool inMenuBar(QMainWindow &window, QAction *action)
{
    const auto menus = window.menuBar()->actions();
    return std::any_of(menus.begin(), menus.end(), [&](const QAction *menu) {
        return menu->menu() && menu->menu()->actions().contains(action);
    });
}

// Each action is defined once, labelled, in the menu bar with its shortcut,
// and the old buttons above the editor are gone.
void testActionsInMenuBar()
{
    TestVault vault({
        {QStringLiteral("note.md"), "text"}
    });
    const std::vector<std::pair<QString, QKeySequence>> expected{
        { QStringLiteral("openVaultAction"),       QKeySequence(QKeySequence::Open)},
        {      QStringLiteral("saveAction"),       QKeySequence(QKeySequence::Save)},
        {QStringLiteral("followLinkAction"), QKeySequence(Qt::ALT | Qt::Key_Return)},
        {  QStringLiteral("moveNoteAction"),               QKeySequence(Qt::Key_F2)},
        {QStringLiteral("deleteNoteAction"),                         QKeySequence()},
    };
    for (const auto &[name, shortcut] : expected) {
        CHECK(vault.window().findChildren<QAction *>(name).size() == 1);
        QAction *action = vault.action(name);
        CHECK(!action->text().isEmpty());
        CHECK(!action->toolTip().isEmpty());
        CHECK(action->shortcut() == shortcut);
        CHECK(inMenuBar(vault.window(), action));
    }
    for (const QString &button :
         {QStringLiteral("saveNote"),
          QStringLiteral("followLink"),
          QStringLiteral("moveNote"),
          QStringLiteral("deleteNote")})
        CHECK(vault.window().findChild<QPushButton *>(button) == nullptr);
}

// Shortcuts work from the editor and the tree with the same results as the actions.
void testShortcuts()
{
    TestVault vault({
        {QStringLiteral("a.md"), "see [[b]]"},
        {QStringLiteral("b.md"),         "b"}
    });
    QTreeWidget *tree = vault.tree();
    QTextEdit *editor = vault.editor();
    openNote(tree, QStringLiteral("a.md"));
    CHECK(editor->hasFocus());
    moveCursorTo(editor, static_cast<int>(editor->toPlainText().size()));
    QTest::keyClicks(editor, QStringLiteral(" more"));
    pressShortcut(editor, QKeySequence::Save);
    CHECK(readFile(vault.filePath(QStringLiteral("a.md"))) == "see [[b]] more");
    CHECK(!vault.action(QStringLiteral("saveAction"))->isEnabled());

    moveCursorTo(editor, 6);
    pressShortcut(editor, QKeySequence(Qt::ALT | Qt::Key_Return));
    CHECK(currentPath(tree) == QStringLiteral("b.md"));
    CHECK(editor->toPlainText() == QStringLiteral("b"));

    tree->setFocus();
    answerMovePrompts(QStringLiteral("Renamed"));
    pressShortcut(tree, QKeySequence(Qt::Key_F2));
    CHECK(QFile::exists(vault.filePath(QStringLiteral("Renamed.md"))));
    CHECK(!QFile::exists(vault.filePath(QStringLiteral("b.md"))));
    CHECK(readFile(vault.filePath(QStringLiteral("a.md"))) == "see [[/Renamed]] more");
    CHECK(currentPath(tree) == QStringLiteral("Renamed.md"));

    const QStringList listed = notePaths(tree);
    vault.activate(); // The offscreen platform leaves the window inactive after the prompts.
    editor->setFocus();
    pressShortcut(editor, QKeySequence::Open); // The picker is cancelled this time.
    CHECK(vault.picks() == 2);
    CHECK(notePaths(tree) == listed);
}

// Ctrl+click (Cmd+click on macOS) follows the link under the pointer; a plain
// click only moves the cursor.
void testCtrlClickFollowsLink()
{
    TestVault vault({
        {QStringLiteral("a.md"), "see [[b]] here"},
        {QStringLiteral("b.md"),              "b"}
    });
    QTreeWidget *tree = vault.tree();
    QTextEdit *editor = vault.editor();
    auto *status = vault.find<QLabel>(QStringLiteral("status"));
    openNote(tree, QStringLiteral("a.md"));
    const auto pointAt = [&](int position) {
        QTextCursor cursor(editor->document());
        cursor.setPosition(position);
        return QPointF(editor->cursorRect(cursor).center());
    };
    clickAt(editor->viewport(), pointAt(7));
    CHECK(currentPath(tree) == QStringLiteral("a.md"));
    CHECK(editor->toPlainText() == QStringLiteral("see [[b]] here"));
    clickAt(editor->viewport(), pointAt(1), Qt::ControlModifier);
    CHECK(currentPath(tree) == QStringLiteral("a.md"));
    CHECK(status->text().contains(QStringLiteral("cursor inside")));
    clickAt(editor->viewport(), pointAt(7), Qt::ControlModifier);
    CHECK(currentPath(tree) == QStringLiteral("b.md"));
    CHECK(editor->toPlainText() == QStringLiteral("b"));
}

// The header names the open note without ".md" and shows unsaved edits, which
// also disable Rename / move and Delete.
void testHeader()
{
    TestVault vault({
        {QStringLiteral("Folder/Second note.md"), "second"},
        {           QStringLiteral("Welcome.md"),     "hi"}
    });
    Window &window = vault.window();
    auto *header = vault.find<QWidget>(QStringLiteral("noteHeader"));
    auto *title = vault.find<QLabel>(QStringLiteral("noteTitle"));
    auto *unsaved = vault.find<QLabel>(QStringLiteral("unsavedIndicator"));
    QAction *save = vault.action(QStringLiteral("saveAction"));
    QAction *move = vault.action(QStringLiteral("moveNoteAction"));
    QAction *remove = vault.action(QStringLiteral("deleteNoteAction"));
    QTextEdit *editor = vault.editor();
    // Whether Save, Rename / move and Delete are enabled.
    const auto enabled = [&] {
        return std::tuple(save->isEnabled(), move->isEnabled(), remove->isEnabled());
    };
    CHECK(unsaved->accessibleName() == QStringLiteral("Unsaved changes"));
    CHECK(unsaved->palette().color(QPalette::WindowText) == QColor(theme::kUnsavedInk));
    CHECK(!header->isVisibleTo(&window)); // No note open.
    CHECK(enabled() == std::tuple(false, false, false));

    openNote(vault.tree(), QStringLiteral("Folder/Second note.md"));
    CHECK(header->isVisibleTo(&window));
    CHECK(title->text() == QStringLiteral("Second note"));
    CHECK(title->toolTip() == QStringLiteral("Folder/Second note.md"));
    CHECK(!unsaved->isVisibleTo(&window));
    CHECK(enabled() == std::tuple(false, true, true));

    editor->insertPlainText(QStringLiteral("x"));
    CHECK(unsaved->isVisibleTo(&window));
    CHECK(enabled() == std::tuple(true, false, false));
    save->trigger();
    CHECK(!unsaved->isVisibleTo(&window));
    CHECK(enabled() == std::tuple(false, true, true));
    editor->insertPlainText(QStringLiteral("y"));
    CHECK(unsaved->isVisibleTo(&window));
    editor->textCursor().deletePreviousChar(); // Back to the saved text.
    CHECK(!unsaved->isVisibleTo(&window));

    openNote(vault.tree(), QStringLiteral("Welcome.md"));
    CHECK(title->text() == QStringLiteral("Welcome"));
}

QPoint itemPoint(QTreeWidget *tree, const QString &path)
{
    QTreeWidgetItem *item = treeItem(tree, path);
    CHECK(item);
    tree->scrollToItem(item);
    return tree->visualItemRect(item).center();
}

// Right-clicks at point in the tree's viewport and returns the actions of the
// menu shown, closing it.
QList<QAction *> rightClick(QTreeWidget *tree, const QPoint &point)
{
    QList<QAction *> shown;
    QTimer::singleShot(0, [&] {
        auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup) return;
        shown = popup->actions();
        popup->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, tree->viewport()->mapToGlobal(point));
    QCoreApplication::sendEvent(tree->viewport(), &event);
    return shown;
}

// Right-clicking a note opens it and offers Rename / move and Delete for it;
// folders and empty space offer no note actions.
void testTreeContextMenu()
{
    TestVault vault({
        {       QStringLiteral("a.md"), "a"},
        {       QStringLiteral("b.md"), "b"},
        {QStringLiteral("Folder/c.md"), "c"}
    });
    auto *tree = dynamic_cast<NoteTree *>(vault.tree());
    CHECK(tree);
    QTextEdit *editor = vault.editor();
    QAction *move = vault.action(QStringLiteral("moveNoteAction"));
    QAction *remove = vault.action(QStringLiteral("deleteNoteAction"));
    const QList<QAction *> noteActions{move, remove};
    openNote(tree, QStringLiteral("a.md"));

    std::unique_ptr<QMenu> menu = tree->contextMenuAt(itemPoint(tree, QStringLiteral("b.md")));
    CHECK(menu && menu->actions() == noteActions);
    CHECK(!menu->objectName().isEmpty());
    CHECK(!menu->accessibleName().isEmpty());
    CHECK(currentPath(tree) == QStringLiteral("b.md"));
    CHECK(editor->toPlainText() == QStringLiteral("b"));
    menu->actions().at(1)->trigger();
    CHECK(!vault.find<QWidget>(QStringLiteral("deleteBar"))->isHidden());
    vault.find<QPushButton>(QStringLiteral("confirmDelete"))->click();
    CHECK(vault.trashed(QStringLiteral("b.md")));
    CHECK(treeItem(tree, QStringLiteral("b.md")) == nullptr);

    menu = tree->contextMenuAt(itemPoint(tree, QStringLiteral("a.md")));
    CHECK(menu && currentPath(tree) == QStringLiteral("a.md"));
    answerMovePrompts(QStringLiteral("Folder/a moved"));
    menu->actions().at(0)->trigger();
    CHECK(QFile::exists(vault.filePath(QStringLiteral("Folder/a moved.md"))));
    CHECK(currentPath(tree) == QStringLiteral("Folder/a moved.md"));

    CHECK(!tree->contextMenuAt(itemPoint(tree, QStringLiteral("Folder"))));
    CHECK(!tree->contextMenuAt(QPoint(1, tree->viewport()->height() - 1)));

    // A real right-click shows the same menu.
    CHECK(rightClick(tree, itemPoint(tree, QStringLiteral("Folder/c.md"))) == noteActions);
    CHECK(currentPath(tree) == QStringLiteral("Folder/c.md"));

    // Unsaved edits keep the open note, so the clicked note gets no menu either.
    editor->insertPlainText(QStringLiteral("draft"));
    CHECK(!tree->contextMenuAt(itemPoint(tree, QStringLiteral("Folder/a moved.md"))));
    CHECK(currentPath(tree) == QStringLiteral("Folder/c.md"));
    CHECK(!move->isEnabled());
    CHECK(!remove->isEnabled());
}

// A note that fails to open offers no menu, so the open note's actions can't
// be mistaken for the clicked note's.
void testTreeContextMenuForUnopenableNote()
{
    TestVault vault({
        {  QStringLiteral("a.md"),            "a"},
        {QStringLiteral("bad.md"), "\xff\xfe bad"}
    });
    auto *tree = dynamic_cast<NoteTree *>(vault.tree());
    CHECK(tree);
    openNote(tree, QStringLiteral("a.md"));
    CHECK(!tree->contextMenuAt(itemPoint(tree, QStringLiteral("bad.md"))));
    CHECK(currentPath(tree) == QStringLiteral("a.md"));
}

// Closing with unsaved edits warns and keeps the window open.
void testCloseBlockedWhileUnsaved()
{
    TestVault vault({
        {QStringLiteral("a.md"), "a"}
    });
    openNote(vault.tree(), QStringLiteral("a.md"));
    vault.editor()->insertPlainText(QStringLiteral("x"));
    bool warned = false;
    QTimer::singleShot(0, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        warned = box != nullptr;
        if (box) box->accept();
    });
    CHECK(!vault.window().close());
    CHECK(warned && vault.window().isVisible());
    vault.action(QStringLiteral("saveAction"))->trigger();
    CHECK(vault.window().close());
}
} // namespace

void runNoteActionTests()
{
    testActionsInMenuBar();
    testShortcuts();
    testCtrlClickFollowsLink();
    testHeader();
    testTreeContextMenu();
    testTreeContextMenuForUnopenableNote();
    testCloseBlockedWhileUnsaved();
}
