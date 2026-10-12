#include "MarkdownHighlighter.h"
#include "NoteTree.h"
#include "SettingsStore.h"
#include "Theme.h"
#include "UiTestSupport.h"
#include "Window.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDialog>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QThread>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <utility>

// Stands in for settings that can't be written, such as a read-only config folder.
class FailingSettings : public SettingsStore
{
public:
    QString lastVault() const override { return {}; }
    bool setLastVault(const QString & /*path*/) override { return false; }
};

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
    auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
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

static void testUnrememberedVaultIsReported()
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
    auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(open && notes && status);
    open->click();
    CHECK(notePaths(notes).size() == 1); // The vault still opens.
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
    auto *notes = second.findChild<QTreeWidget *>(QStringLiteral("notes"));
    CHECK(notes);
    CHECK(notePaths(notes) == QStringList{QStringLiteral("Welcome.md")});
    CHECK(second.windowTitle() == title);
}

// Opens a vault holding one note.md with `text` and selects it.
static void openSingleNote(Window &window, const QTemporaryDir &dir, const QByteArray &text)
{
    writeFile(QDir(dir.path()).filePath(QStringLiteral("note.md")), text);
    auto *open = window.findChild<QPushButton *>(QStringLiteral("openVault"));
    auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
    CHECK(open && notes);
    open->click();
    openNote(notes, QStringLiteral("note.md"));
}

// Distance from the top of one paragraph to the top of the next.
static qreal linePitch(const QTextEdit *editor, int paragraph)
{
    const QTextBlock block = editor->document()->findBlockByNumber(paragraph);
    CHECK(block.isValid() && block.next().isValid());
    QAbstractTextDocumentLayout *layout = editor->document()->documentLayout();
    return layout->blockBoundingRect(block.next()).top() - layout->blockBoundingRect(block).top();
}

// Text is shown at a readable size with roomy line spacing, including lines
// typed or pasted later, and none of it changes the saved bytes.
static void testReadableEditorText()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<MemorySettings>());
    window.show(); // Text is laid out only once it is shown.
    openSingleNote(window, dir, "first\nsecond");
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    auto *save = window.findChild<QAction *>(QStringLiteral("saveAction"));
    CHECK(editor && save);
    CHECK(editor->font().pointSize() == 13);
    const qreal roomy = 1.4 * QFontMetricsF(editor->font()).height();
    CHECK(linePitch(editor, 0) >= roomy);

    QTextCursor end = editor->textCursor();
    end.movePosition(QTextCursor::End);
    editor->setTextCursor(end);
    editor->insertPlainText(QStringLiteral("\nthird"));
    CHECK(linePitch(editor, 1) >= roomy);
    auto *pasted = new QMimeData;
    pasted->setHtml(QStringLiteral("<h1>fourth</h1><p><b>fifth</b></p>"));
    pasted->setText(QStringLiteral("\nfourth\nfifth"));
    QApplication::clipboard()->setMimeData(pasted);
    editor->paste();
    CHECK(linePitch(editor, 2) >= roomy);
    CHECK(linePitch(editor, 3) >= roomy);
    save->trigger();
    CHECK(
        readFile(QDir(dir.path()).filePath(QStringLiteral("note.md"))) ==
        "first\nsecond\nthird\nfourth\nfifth");
}

// The area the editor's characters occupy, in the editor's own coordinates.
static QRect textBounds(const QTextEdit *editor)
{
    QRect bounds;
    QTextCursor cursor(editor->document());
    for (int position = 0; position < editor->document()->characterCount(); ++position) {
        cursor.setPosition(position);
        bounds |= editor->cursorRect(cursor).translated(editor->viewport()->pos());
    }
    return bounds;
}

// Long lines wrap at a readable width centred in a wide window, even with a
// scroll bar, and padding keeps text off the editor's edges in a narrow one.
static void testEditorTextWidth()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<MemorySettings>());
    window.show();
    openSingleNote(window, dir, QByteArray("a ").repeated(4000)); // Short words wrap evenly.
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    CHECK(editor);
    const int word = QFontMetrics(editor->font()).horizontalAdvance(QStringLiteral("a "));

    window.resize(1800, 750);
    QCoreApplication::processEvents();
    CHECK(editor->verticalScrollBar()->isVisible());
    QRect text = textBounds(editor);
    CHECK(text.width() <= 720);
    CHECK(text.width() > 720 - word); // Wraps at the cap, not short of it.
    CHECK(std::abs(text.left() - (editor->width() - text.right())) <= word);
    const QPoint margin(text.left() / 2, editor->height() / 2);
    CHECK(editor->grab().toImage().pixelColor(margin) == editor->palette().color(QPalette::Base));

    window.resize(700, 750);
    QCoreApplication::processEvents();
    text = textBounds(editor);
    CHECK(text.left() >= 24);
    CHECK(text.top() >= 24);
    CHECK(editor->width() - text.right() >= 24);
}

// The editor highlights Markdown as it is typed, and saving writes exactly the
// typed text, with none of the formatting.
static void testEditorHighlightsMarkdown()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<MemorySettings>());
    openSingleNote(window, dir, "# Title\n**bold** and [[link]]");
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    auto *save = window.findChild<QAction *>(QStringLiteral("saveAction"));
    CHECK(editor && save);
    CHECK(
        dynamic_cast<MarkdownHighlighter *>(editor->document()->findChild<QSyntaxHighlighter *>(
            QStringLiteral("markdownHighlighter"))));
    const auto kindsAt = [&](int block, int position) {
        const auto ranges = editor->document()->findBlockByNumber(block).layout()->formats();
        for (const QTextLayout::FormatRange &range : ranges) {
            if (position >= range.start && position < range.start + range.length)
                return range.format.intProperty(theme::kMarkdownKindsProperty);
        }
        return 0;
    };
    CHECK(kindsAt(0, 2) == static_cast<int>(theme::MarkdownKind::Heading));
    CHECK(kindsAt(1, 2) == static_cast<int>(theme::MarkdownKind::Bold));

    QTextCursor end = editor->textCursor();
    end.movePosition(QTextCursor::End);
    editor->setTextCursor(end);
    editor->insertPlainText(QStringLiteral("\n```\n# not a heading"));
    CHECK(kindsAt(3, 2) == static_cast<int>(theme::MarkdownKind::CodeBlock));
    save->trigger();
    CHECK(
        readFile(QDir(dir.path()).filePath(QStringLiteral("note.md"))) ==
        "# Title\n**bold** and [[link]]\n```\n# not a heading");
}

// Typing in a long, highlighted note stays quick. HighlighterTests checks that
// edits re-highlight incrementally; this bounds the cost in the real editor.
static void testLongNoteStaysResponsive()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<MemorySettings>());
    window.show();
    QByteArray text;
    for (int line = 0; line < 2000; ++line) {
        text += "## Section " + QByteArray::number(line) + "\n";
        text += "Some **bold**, *italic*, `code` and [[Note " + QByteArray::number(line) + "]].\n";
        text += "```\nfenced " + QByteArray::number(line) + "\n```\n";
    }
    openSingleNote(window, dir, text);
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    CHECK(editor);
    CHECK(editor->document()->blockCount() > 10000);
    QTextCursor middle(editor->document()->findBlockByNumber(5001));
    QElapsedTimer timer;
    timer.start();
    middle.insertText(QStringLiteral("x"));
    QCoreApplication::processEvents();
    CHECK(timer.elapsed() < 500);
}

// Reloading a note changed outside Inkstow keeps the cursor and scroll position.
static void testReloadKeepsPlace()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<MemorySettings>());
    window.show();
    window.activateWindow(); // Activation is how this test asks for a sync.
    CHECK(waitFor([&] { return window.isActiveWindow(); }));
    QByteArray text;
    for (int line = 0; line < 200; ++line)
        text += "line " + QByteArray::number(line) + '\n';
    openSingleNote(window, dir, text);
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    CHECK(editor);
    QScrollBar *scroll = editor->verticalScrollBar();
    CHECK(scroll->maximum() > 0);
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(static_cast<int>(text.indexOf("line 150")));
    editor->setTextCursor(cursor);
    scroll->setValue(scroll->maximum() / 2);
    const int position = editor->textCursor().position();
    const int scrolled = scroll->value();

    writeFile(QDir(dir.path()).filePath(QStringLiteral("note.md")), "LINE" + text.mid(4));
    QEvent activation(QEvent::ActivationChange);
    QCoreApplication::sendEvent(&window, &activation);
    CHECK(waitFor([&] { return editor->toPlainText().startsWith(QStringLiteral("LINE 0")); }));
    CHECK(editor->textCursor().position() == position);
    CHECK(scroll->value() == scrolled);
}

// With no note open, the editor area explains itself instead of showing a
// blank, disabled editor.
static void testNoNoteEmptyState()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<MemorySettings>());
    writeFile(QDir(dir.path()).filePath(QStringLiteral("note.md")), "text");
    auto *open = window.findChild<QPushButton *>(QStringLiteral("openVault"));
    auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    auto *empty = window.findChild<QWidget *>(QStringLiteral("noNoteState"));
    auto *message = window.findChild<QLabel *>(QStringLiteral("noNoteMessage"));
    CHECK(open && notes && editor && empty && message);
    CHECK(editor->accessibleName() == QStringLiteral("Note text"));
    window.show();
    window.activateWindow(); // So the editor can take focus.
    CHECK(waitFor([&] { return window.isActiveWindow(); }));
    open->click();
    CHECK(empty->isVisible());
    CHECK(!editor->isVisible());
    CHECK(message->text().contains(QStringLiteral("Select a note")));
    openNote(notes, QStringLiteral("note.md"));
    CHECK(editor->isVisible());
    CHECK(!empty->isVisible());
    CHECK(editor->hasFocus());
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

// What the injected folder picker answers, and how often the window asked it.
struct PickerScript
{
    std::optional<QString> mAnswer;
    int mCalls = 0;
};

// Triggers File > Open vault with the picker answering `answer` this once, then
// checks the open vault's notes and window title were left as they were.
static void openVaultExpectingNoChange(
    Window &window, PickerScript &picker, std::optional<QString> answer)
{
    auto *switchVault = window.findChild<QAction *>(QStringLiteral("openVaultAction"));
    auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
    CHECK(switchVault && notes);
    const QStringList listed = notePaths(notes);
    const QString title = window.windowTitle();
    const std::optional<QString> usual = std::exchange(picker.mAnswer, std::move(answer));
    switchVault->trigger();
    picker.mAnswer = usual;
    CHECK(notePaths(notes) == listed);
    CHECK(window.windowTitle() == title);
}

// Opening a vault from the File menu and cancelling the picker changes nothing.
static void testCancellingOpenVault(Window &window, PickerScript &picker)
{
    auto *switchVault = window.findChild<QAction *>(QStringLiteral("openVaultAction"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(switchVault && status);
    CHECK(switchVault->shortcut() == QKeySequence(QKeySequence::Open));
    const auto menus = window.menuBar()->actions();
    CHECK(std::any_of(menus.begin(), menus.end(), [&](QAction *menu) {
        return menu->menu() && menu->menu()->actions().contains(switchVault);
    }));
    const QString openedStatus = status->text();
    openVaultExpectingNoChange(window, picker, std::nullopt);
    CHECK(status->text() == openedStatus);
}

// Unsaved edits block switching vaults, the same way they block switching notes.
static void testVaultSwitchRefusedWhileUnsaved(Window &window, PickerScript &picker)
{
    auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(notes && editor && status);
    QTemporaryDir otherVault;
    CHECK(otherVault.isValid());
    writeFile(QDir(otherVault.path()).filePath(QStringLiteral("elsewhere.md")), "elsewhere");
    const QString text = editor->toPlainText();
    const int callsBefore = picker.mCalls;
    openVaultExpectingNoChange(window, picker, otherVault.path());
    CHECK(picker.mCalls == callsBefore); // Refused before asking for a folder.
    CHECK(status->text().contains(QStringLiteral("Save the current note")));
    CHECK(treeItem(notes, QStringLiteral("elsewhere.md")) == nullptr);
    CHECK(editor->toPlainText() == text);
}

static void testFollowingLinks(
    Window &window, const QDir &dir, const QStringList &openedAttachments)
{
    auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    auto *follow = window.findChild<QAction *>(QStringLiteral("followLinkAction"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(notes && editor && follow && status);
    writeFile(
        dir.filePath(QStringLiteral("other.md")),
        "[[note#Part|Alias]] [[missing]] [[asset.png]]");
    writeFile(dir.filePath(QStringLiteral("asset.png")), "attachment");
    openNote(notes, QStringLiteral("other.md"));
    const auto cursorAt = [&](const QString &needle) {
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(static_cast<int>(editor->toPlainText().indexOf(needle) + 2));
        editor->setTextCursor(cursor);
    };
    cursorAt(QStringLiteral("[[missing]]"));
    follow->trigger();
    CHECK(status->text() == QStringLiteral("Unresolved link"));
    cursorAt(QStringLiteral("[[asset.png]]"));
    follow->trigger();
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
    follow->trigger();
    CHECK(status->text().contains(QStringLiteral("cursor inside")));
    CHECK(openedAttachments.size() == 1);
    // Make sure unsaved edits cannot be abandoned through Follow link.
    editor->setPlainText(QStringLiteral("[[note#Part|Alias]] [[asset.png]] draft"));
    cursorAt(QStringLiteral("[[note#Part|Alias]]"));
    follow->trigger();
    CHECK(currentPath(notes) == QStringLiteral("other.md"));
    CHECK(status->text().contains(QStringLiteral("Save the current note")));
    cursorAt(QStringLiteral("[[asset.png]]"));
    follow->trigger();
    CHECK(status->text().contains(QStringLiteral("Save the current note")));
    CHECK(openedAttachments.size() == 1);
    CHECK(
        readFile(dir.filePath(QStringLiteral("other.md"))) ==
        "[[note#Part|Alias]] [[missing]] [[asset.png]]");
    editor->setPlainText(QStringLiteral("[[note#Part|Alias]] [[missing]] [[asset.png]]"));
    cursorAt(QStringLiteral("[[note#Part|Alias]]"));
    follow->trigger();
    CHECK(currentPath(notes) == QStringLiteral("note.md"));
    CHECK(editor->toPlainText() == QStringLiteral("original"));
    CHECK(
        readFile(dir.filePath(QStringLiteral("other.md"))) ==
        "[[note#Part|Alias]] [[missing]] [[asset.png]]");
}

// Shows the tree's items under parent (the top level when null) as "kind:name".
static QStringList treeRows(const QTreeWidget *tree, const QTreeWidgetItem *parent = nullptr)
{
    QStringList rows;
    const int count = parent ? parent->childCount() : tree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        const QTreeWidgetItem *item = parent ? parent->child(i) : tree->topLevelItem(i);
        const bool folder = item->type() == NoteTree::kFolderItem;
        rows.append((folder ? QStringLiteral("folder:") : QStringLiteral("note:")) + item->text(0));
    }
    return rows;
}

static void testNoteTree()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QDir root(dir.path());
    CHECK(root.mkpath(QStringLiteral("Zeta")));
    CHECK(root.mkpath(QStringLiteral("alpha/deep")));
    CHECK(root.mkpath(QStringLiteral("Beta")));
    CHECK(root.mkpath(QStringLiteral(".obsidian")));
    writeFile(root.filePath(QStringLiteral("b.md")), "b");
    writeFile(root.filePath(QStringLiteral("A note.md")), "[[inner]]");
    writeFile(root.filePath(QStringLiteral(".dot.md")), "hidden");
    writeFile(root.filePath(QStringLiteral("alpha/deep/inner.md")), "inner");
    writeFile(root.filePath(QStringLiteral("alpha/c.md")), "c");
    writeFile(root.filePath(QStringLiteral("Zeta/z.md")), "z");
    writeFile(root.filePath(QStringLiteral(".obsidian/app.md")), "config");
    Window window(
        refuseTrash,
        {},
        [&](QWidget *) { return std::optional(dir.path()); },
        std::make_shared<MemorySettings>());
    window.show();
    window.activateWindow();
    auto *open = window.findChild<QPushButton *>(QStringLiteral("openVault"));
    auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
    auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    CHECK(open && tree && editor && status);
    CHECK(!tree->accessibleName().isEmpty());
    CHECK(tree->indentation() == 12);
    open->click();

    // Folders first, then notes, each by name; no ".md"; empty folders shown; dot entries hidden.
    CHECK(
        treeRows(tree) == (QStringList{
                              QStringLiteral("folder:alpha"),
                              QStringLiteral("folder:Beta"),
                              QStringLiteral("folder:Zeta"),
                              QStringLiteral("note:A note"),
                              QStringLiteral("note:b")}));
    QTreeWidgetItem *alpha = treeItem(tree, QStringLiteral("alpha"));
    CHECK(alpha);
    CHECK(
        treeRows(tree, alpha) ==
        (QStringList{QStringLiteral("folder:deep"), QStringLiteral("note:c")}));
    CHECK(treeItem(tree, QStringLiteral("alpha/deep/inner.md")));
    CHECK(!alpha->isExpanded());

    // Arrow keys move without opening; Enter opens the note.
    tree->setFocus();
    tree->setCurrentItem(tree->topLevelItem(0));
    pressKey(tree, Qt::Key_Right); // Expands alpha.
    CHECK(alpha->isExpanded());
    pressKey(tree, Qt::Key_Down);
    pressKey(tree, Qt::Key_Down);
    CHECK(currentPath(tree) == QStringLiteral("alpha/c.md"));
    CHECK(editor->toPlainText().isEmpty());
    pressKey(tree, Qt::Key_Return);
    CHECK(editor->toPlainText() == QStringLiteral("c"));
    CHECK(status->text() == QStringLiteral("Opened alpha/c.md"));

    // A mouse click opens a note too.
    clickItem(tree, treeItem(tree, QStringLiteral("A note.md")));
    CHECK(editor->toPlainText() == QStringLiteral("[[inner]]"));

    // Following a link selects the target and reveals its folders.
    alpha->setExpanded(false);
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(3);
    editor->setTextCursor(cursor);
    window.findChild<QAction *>(QStringLiteral("followLinkAction"))->trigger();
    CHECK(currentPath(tree) == QStringLiteral("alpha/deep/inner.md"));
    CHECK(alpha->isExpanded() && treeItem(tree, QStringLiteral("alpha/deep"))->isExpanded());

    // Unsaved edits refuse another note and keep the selection on the open note.
    editor->setPlainText(QStringLiteral("draft"));
    openNote(tree, QStringLiteral("b.md"));
    CHECK(status->text().contains(QStringLiteral("Save the current note")));
    CHECK(currentPath(tree) == QStringLiteral("alpha/deep/inner.md"));
    CHECK(editor->toPlainText() == QStringLiteral("draft"));
    clickItem(tree, treeItem(tree, QStringLiteral("b.md")));
    CHECK(currentPath(tree) == QStringLiteral("alpha/deep/inner.md"));
    CHECK(editor->toPlainText() == QStringLiteral("draft"));
    editor->setPlainText(QStringLiteral("inner"));

    // A rescan after outside changes keeps expanded folders and the selection,
    // and shows a new folder even when no note changed.
    treeItem(tree, QStringLiteral("Zeta"))->setExpanded(true);
    CHECK(waitFor([&] { return window.isActiveWindow(); }));
    QEvent activation(QEvent::ActivationChange);
    CHECK(root.mkpath(QStringLiteral("Zeta/Later")));
    QCoreApplication::sendEvent(&window, &activation);
    CHECK(treeItem(tree, QStringLiteral("Zeta/Later")));
    writeFile(root.filePath(QStringLiteral("alpha/added.md")), "added");
    QCoreApplication::sendEvent(&window, &activation);
    CHECK(treeItem(tree, QStringLiteral("alpha/added.md")));
    CHECK(treeItem(tree, QStringLiteral("alpha"))->isExpanded());
    CHECK(treeItem(tree, QStringLiteral("alpha/deep"))->isExpanded());
    CHECK(treeItem(tree, QStringLiteral("Zeta"))->isExpanded());
    CHECK(currentPath(tree) == QStringLiteral("alpha/deep/inner.md"));

    // Opening a vault starts with folders collapsed and nothing selected.
    window.findChild<QAction *>(QStringLiteral("openVaultAction"))->trigger();
    CHECK(!treeItem(tree, QStringLiteral("alpha"))->isExpanded());
    CHECK(!treeItem(tree, QStringLiteral("Zeta"))->isExpanded());
    CHECK(currentPath(tree).isEmpty());
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
        PickerScript picker{dir.path()};
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
                ++picker.mCalls;
                return picker.mAnswer;
            },
            std::make_shared<MemorySettings>());
        testCurrentStyleKept(window);
        testReopeningLastVault();
        testMissingLastVault();
        testUnrememberedVaultIsReported();
        testNoteTree();
        testReadableEditorText();
        testEditorTextWidth();
        testNoNoteEmptyState();
        testReloadKeepsPlace();
        testEditorHighlightsMarkdown();
        testLongNoteStaysResponsive();
        runNoteActionTests();
        auto *open = window.findChild<QPushButton *>(QStringLiteral("openVault"));
        auto *notes = window.findChild<QTreeWidget *>(QStringLiteral("notes"));
        auto *editor = window.findChild<QTextEdit *>(QStringLiteral("editor"));
        auto *save = window.findChild<QAction *>(QStringLiteral("saveAction"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
        auto *conflictBar = window.findChild<QWidget *>(QStringLiteral("conflictBar"));
        auto *keepMine = window.findChild<QPushButton *>(QStringLiteral("keepMine"));
        auto *reload = window.findChild<QPushButton *>(QStringLiteral("reloadFromDisk"));
        auto *discard = window.findChild<QPushButton *>(QStringLiteral("discardEdits"));
        auto *merge = window.findChild<QPushButton *>(QStringLiteral("mergeOutsideChanges"));
        auto *saveAsNew = window.findChild<QPushButton *>(QStringLiteral("saveAsNewNote"));
        auto *newPath = window.findChild<QLineEdit *>(QStringLiteral("newNotePath"));
        auto *create = window.findChild<QPushButton *>(QStringLiteral("createNote"));
        auto *remove = window.findChild<QAction *>(QStringLiteral("deleteNoteAction"));
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
        CHECK(
            notePaths(notes) ==
            (QStringList{QStringLiteral("note.md"), QStringLiteral("other.md")}));
        testCancellingOpenVault(window, picker);
        testFollowingLinks(window, QDir(dir.path()), openedAttachments);
        editor->setPlainText(QStringLiteral("first edit"));
        CHECK(save->isEnabled());
        openNote(notes, QStringLiteral("other.md"));
        CHECK(
            currentPath(notes) ==
            QStringLiteral("note.md"));                     // Unsaved note cannot be switched away.
        testVaultSwitchRefusedWhileUnsaved(window, picker); // Nor can its vault.
        save->trigger();
        CHECK(readFile(note) == "first edit");
        CHECK(!save->isEnabled());

        // External additions, renames and deletions refresh the list without
        // replacing the open note or exposing hidden entries.
        const QString nested = dir.filePath(QStringLiteral("nested"));
        CHECK(QDir().mkdir(nested));
        writeFile(QDir(nested).filePath(QStringLiteral("new.md")), "new");
        syncWithoutWatcher();
        CHECK(waitFor([&] { return treeItem(notes, QStringLiteral("nested/new.md")) != nullptr; }));
        CHECK(currentPath(notes) == QStringLiteral("note.md"));
        CHECK(editor->toPlainText() == QStringLiteral("first edit"));
        writeFile(QDir(nested).filePath(QStringLiteral("later.md")), "later");
        syncWithoutWatcher();
        CHECK(
            waitFor([&] { return treeItem(notes, QStringLiteral("nested/later.md")) != nullptr; }));
        CHECK(
            QFile::rename(
                QDir(nested).filePath(QStringLiteral("new.md")),
                QDir(nested).filePath(QStringLiteral("renamed.md"))));
        syncWithoutWatcher();
        CHECK(waitFor([&] {
            return treeItem(notes, QStringLiteral("nested/renamed.md")) != nullptr &&
                   treeItem(notes, QStringLiteral("nested/new.md")) == nullptr;
        }));
        CHECK(QFile::remove(QDir(nested).filePath(QStringLiteral("renamed.md"))));
        CHECK(QFile::remove(QDir(nested).filePath(QStringLiteral("later.md"))));
        CHECK(QDir().rmdir(nested));
        syncWithoutWatcher();
        CHECK(
            waitFor([&] { return treeItem(notes, QStringLiteral("nested/later.md")) == nullptr; }));

        // Losing and recreating the selected note's folder must not strand
        // the watcher: a later write must still reach the editor.
        if (watcherWorks) {
            CHECK(QDir().mkdir(nested));
            const QString subnote = QDir(nested).filePath(QStringLiteral("note.md"));
            writeFile(subnote, "sub original");
            CHECK(waitFor(
                [&] { return treeItem(notes, QStringLiteral("nested/note.md")) != nullptr; }));
            openNote(notes, QStringLiteral("nested/note.md"));
            CHECK(editor->toPlainText() == QStringLiteral("sub original"));
            CHECK(QFile::remove(subnote));
            CHECK(QDir().rmdir(nested));
            CHECK(waitFor(
                [&] { return treeItem(notes, QStringLiteral("nested/note.md")) == nullptr; }));
            CHECK(QDir().mkdir(nested));
            writeFile(subnote, "sub recreated");
            CHECK(
                waitFor([&] { return editor->toPlainText() == QStringLiteral("sub recreated"); }));
            writeFile(subnote, "sub changed again");
            CHECK(waitFor(
                [&] { return editor->toPlainText() == QStringLiteral("sub changed again"); }));
            openNote(notes, QStringLiteral("note.md"));
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
        save->trigger();
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
        save->trigger();
        CHECK(readFile(note) == "local edit");

        editor->setPlainText(QStringLiteral("another local"));
        writeFile(note, "another outside");
        save->trigger();
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
        save->trigger();
        CHECK(readFile(note) == "mine again");

        // Save alone, before the watcher reports anything, also shows the bar.
        editor->setPlainText(QStringLiteral("mine 3"));
        writeFile(note, "theirs 3");
        save->trigger();
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
        openNote(notes, QStringLiteral("other.md"));
        CHECK(currentPath(notes) == QStringLiteral("other.md"));

        // Creating a note adds it to the list and opens it.
        newPath->setText(QStringLiteral("Ideas"));
        create->click();
        CHECK(QFile::exists(dir.filePath(QStringLiteral("Ideas.md"))));
        CHECK(currentPath(notes) == QStringLiteral("Ideas.md"));
        CHECK(treeItem(notes, QStringLiteral("note.md")) == nullptr);
        CHECK(editor->isEnabled() && editor->toPlainText().isEmpty());
        CHECK(newPath->text().isEmpty());
        newPath->setText(QStringLiteral("../escape"));
        create->click();
        CHECK(status->text().startsWith(QStringLiteral("Error")));
        CHECK(newPath->text() == QStringLiteral("../escape"));

        // Rename/move asks for a destination and explicitly confirms link updates.
        auto *move = window.findChild<QAction *>(QStringLiteral("moveNoteAction"));
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
        move->trigger();
        CHECK(QFile::exists(dir.filePath(QStringLiteral("Moved.md"))));
        CHECK(currentPath(notes) == QStringLiteral("Moved.md"));
        CHECK(!QFile::exists(dir.filePath(QStringLiteral("Ideas.md"))));

        // Unsaved edits disable both, and a confirmation never outlives its note.
        editor->setPlainText(QStringLiteral("draft"));
        CHECK(!create->isEnabled());
        CHECK(!remove->isEnabled());
        CHECK(!move->isEnabled());
        editor->setPlainText(QString());
        CHECK(create->isEnabled() && remove->isEnabled());
        remove->trigger();
        CHECK(!deleteBar->isHidden());
        openNote(notes, QStringLiteral("other.md"));
        CHECK(deleteBar->isHidden());
        CHECK(QFile::exists(dir.filePath(QStringLiteral("other.md"))));
        openNote(notes, QStringLiteral("Moved.md"));
        CHECK(deleteBar->isHidden());

        // Deleting asks first, and Cancel leaves the note alone.
        CHECK(remove->isEnabled() && deleteBar->isHidden());
        remove->trigger();
        CHECK(!deleteBar->isHidden());
        cancelDelete->click();
        CHECK(deleteBar->isHidden());
        CHECK(QFile::exists(dir.filePath(QStringLiteral("Moved.md"))));
        remove->trigger();
        confirmDelete->click();
        CHECK(deleteBar->isHidden());
        CHECK(!QFile::exists(dir.filePath(QStringLiteral("Moved.md"))));
        CHECK(QFile::exists(QDir(trash.path()).filePath(QStringLiteral("Moved.md"))));
        CHECK(treeItem(notes, QStringLiteral("Moved.md")) == nullptr);
        CHECK(!editor->isEnabled());
        CHECK(window.findChild<QWidget *>(QStringLiteral("noNoteState"))->isVisible());
        CHECK(!remove->isEnabled());
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
