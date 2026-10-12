#include "Window.h"

#include "Theme.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
std::optional<QString> pickFolderWithDialog(QWidget *parent)
{
    QString folder = QFileDialog::getExistingDirectory(parent, QStringLiteral("Open vault"));
    if (folder.isEmpty()) return std::nullopt;
    return folder;
}
} // namespace

Window::Window(
    TrashFunction trash,
    std::function<bool(const QUrl &)> openAttachment,
    FolderPicker pickFolder,
    std::shared_ptr<SettingsStore> settings)
    : mSession(std::move(trash))
    , mOpenAttachment(openAttachment ? std::move(openAttachment) : QDesktopServices::openUrl)
    , mPickFolder(pickFolder ? std::move(pickFolder) : pickFolderWithDialog)
    , mSettings(settings ? std::move(settings) : std::make_shared<AppSettingsStore>())
    , mWatcher(new QFileSystemWatcher(this))
    , mSyncTimer(new QTimer(this))
    , mNotes(new QListWidget)
    , mEditor(new QPlainTextEdit)
    , mNewPath(new QLineEdit)
    , mDeleteBar(new QWidget)
    , mDeleteLabel(new QLabel)
    , mConflictBar(new QWidget)
    , mConflictLabel(new QLabel)
    , mOpenVault(new QAction(QStringLiteral("&Open vault…"), this))
    , mPages(new QStackedWidget)
    , mEmptyMessage(new QLabel(QStringLiteral("Open a folder of Markdown notes to begin.")))
    , mStatus(new QLabel)
{
    setWindowTitle(QStringLiteral("Inkstow"));
    mOpenVault->setObjectName(QStringLiteral("openVaultAction"));
    mOpenVault->setShortcut(QKeySequence::Open);
    menuBar()->addMenu(QStringLiteral("&File"))->addAction(mOpenVault);
    resize(theme::kWindowWidth, theme::kWindowHeight);
    auto *central = new QWidget(this);
    auto *page = new QVBoxLayout(central);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Inkstow"));
    title->setObjectName(QStringLiteral("appTitle"));
    title->setFont(theme::titleFont(title->font()));
    header->addWidget(title);
    page->addLayout(header);

    auto *splitter = new QSplitter;
    mNotes->setObjectName(QStringLiteral("notes"));
    mEditor->setObjectName(QStringLiteral("editor"));
    mEditor->setPlaceholderText(QStringLiteral("Select a note"));
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    auto *toolbar = new QHBoxLayout;
    mFilename = new QLabel(QStringLiteral("Select a note"));
    mFollow = new QPushButton(QStringLiteral("Follow link"));
    mFollow->setObjectName(QStringLiteral("followLink"));
    mSave = new QPushButton(QStringLiteral("Save"));
    mSave->setObjectName(QStringLiteral("saveNote"));
    mMove = new QPushButton(QStringLiteral("Rename / move"));
    mMove->setObjectName(QStringLiteral("moveNote"));
    mDelete = new QPushButton(QStringLiteral("Delete"));
    mDelete->setObjectName(QStringLiteral("deleteNote"));
    toolbar->addWidget(mFilename, 1);
    toolbar->addWidget(mFollow);
    toolbar->addWidget(mMove);
    toolbar->addWidget(mDelete);
    toolbar->addWidget(mSave);
    rightLayout->addLayout(toolbar);
    mConflictBar->setObjectName(QStringLiteral("conflictBar"));
    auto *conflictLayout = new QHBoxLayout(mConflictBar);
    conflictLayout->setContentsMargins(theme::kFlushMargins);
    mReload = new QPushButton(QStringLiteral("Reload from disk"));
    mReload->setObjectName(QStringLiteral("reloadFromDisk"));
    mKeepMine = new QPushButton(QStringLiteral("Keep mine"));
    mKeepMine->setObjectName(QStringLiteral("keepMine"));
    mMerge = new QPushButton(QStringLiteral("Merge outside changes"));
    mMerge->setObjectName(QStringLiteral("mergeOutsideChanges"));
    mSaveAsNew = new QPushButton(QStringLiteral("Save as new note"));
    mSaveAsNew->setObjectName(QStringLiteral("saveAsNewNote"));
    mDiscard = new QPushButton(QStringLiteral("Discard edits"));
    mDiscard->setObjectName(QStringLiteral("discardEdits"));
    conflictLayout->addWidget(mConflictLabel, 1);
    conflictLayout->addWidget(mReload);
    conflictLayout->addWidget(mKeepMine);
    conflictLayout->addWidget(mMerge);
    conflictLayout->addWidget(mSaveAsNew);
    conflictLayout->addWidget(mDiscard);
    mConflictBar->hide();
    rightLayout->addWidget(mConflictBar);
    mDeleteBar->setObjectName(QStringLiteral("deleteBar"));
    auto *deleteLayout = new QHBoxLayout(mDeleteBar);
    deleteLayout->setContentsMargins(theme::kFlushMargins);
    auto *confirmDelete = new QPushButton(QStringLiteral("Move to trash"));
    confirmDelete->setObjectName(QStringLiteral("confirmDelete"));
    auto *cancelDelete = new QPushButton(QStringLiteral("Cancel"));
    cancelDelete->setObjectName(QStringLiteral("cancelDelete"));
    deleteLayout->addWidget(mDeleteLabel, 1);
    deleteLayout->addWidget(confirmDelete);
    deleteLayout->addWidget(cancelDelete);
    mDeleteBar->hide();
    rightLayout->addWidget(mDeleteBar);
    rightLayout->addWidget(mEditor, 1);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(theme::kFlushMargins);
    auto *newNote = new QHBoxLayout;
    mNewPath->setObjectName(QStringLiteral("newNotePath"));
    mNewPath->setPlaceholderText(QStringLiteral("Folder/New note"));
    mCreate = new QPushButton(QStringLiteral("Create"));
    mCreate->setObjectName(QStringLiteral("createNote"));
    newNote->addWidget(mNewPath, 1);
    newNote->addWidget(mCreate);
    leftLayout->addWidget(mNotes, 1);
    leftLayout->addLayout(newNote);
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({theme::kSidebarWidth, theme::kEditorWidth});
    mVaultView = splitter;
    mPages->addWidget(createEmptyState());
    mPages->addWidget(mVaultView);
    page->addWidget(mPages, 1);
    mStatus->setObjectName(QStringLiteral("status"));
    page->addWidget(mStatus);
    setCentralWidget(central);
    // Waits for a burst of writes to settle so a half-written note is not loaded.
    mSyncTimer->setSingleShot(true);
    mSyncTimer->setInterval(150);
    refresh();

    connect(mOpenVault, &QAction::triggered, this, [this] { openVault(); });
    connect(mSave, &QPushButton::clicked, this, [this] { saveNote(); });
    connect(mFollow, &QPushButton::clicked, this, [this] { followLink(); });
    connect(mReload, &QPushButton::clicked, this, [this] { reloadFromDisk(); });
    connect(mKeepMine, &QPushButton::clicked, this, [this] { keepMine(); });
    connect(mMerge, &QPushButton::clicked, this, [this] { mergeOutsideChanges(); });
    connect(mSaveAsNew, &QPushButton::clicked, this, [this] { saveAsNewNote(); });
    connect(mDiscard, &QPushButton::clicked, this, [this] { discardEdits(); });
    connect(mCreate, &QPushButton::clicked, this, [this] { createNote(); });
    connect(mMove, &QPushButton::clicked, this, [this] { moveNote(); });
    connect(mNewPath, &QLineEdit::returnPressed, this, [this] { createNote(); });
    connect(mDelete, &QPushButton::clicked, this, [this] {
        mDeletePath = mSession.path();
        mDeleteLabel->setText(QStringLiteral("Move %1 to the trash?").arg(mDeletePath));
        mDeleteBar->show();
    });
    connect(confirmDelete, &QPushButton::clicked, this, [this] { trashNote(); });
    connect(cancelDelete, &QPushButton::clicked, mDeleteBar, &QWidget::hide);
    connect(mEditor, &QPlainTextEdit::textChanged, this, [this] {
        mSession.setContent(mEditor->toPlainText());
        refresh();
    });
    // Watch vault folders for list changes and atomic replacements, which can
    // drop the selected file from the watcher.
    connect(mWatcher, &QFileSystemWatcher::fileChanged, mSyncTimer, qOverload<>(&QTimer::start));
    connect(
        mWatcher,
        &QFileSystemWatcher::directoryChanged,
        mSyncTimer,
        qOverload<>(&QTimer::start));
    connect(mSyncTimer, &QTimer::timeout, this, [this] { syncWithDisk(); });
    connect(mNotes, &QListWidget::currentTextChanged, this, [this](const QString &path) {
        if (!path.isEmpty() && path != mSession.path()) selectNote(path);
    });
    reopenLastVault();
}

QWidget *Window::createEmptyState()
{
    auto *emptyState = new QWidget;
    emptyState->setObjectName(QStringLiteral("emptyState"));
    auto *layout = new QVBoxLayout(emptyState);
    layout->setSpacing(theme::kEmptyStateSpacing);
    auto *heading = new QLabel(QStringLiteral("No vault open"));
    heading->setFont(theme::titleFont(heading->font()));
    mEmptyMessage->setObjectName(QStringLiteral("emptyStateMessage"));
    mEmptyMessage->setWordWrap(true);
    auto *open = new QPushButton(QStringLiteral("Open vault…"));
    open->setObjectName(QStringLiteral("openVault"));
    open->setAccessibleName(QStringLiteral("Open vault"));
    open->setDefault(true);
    connect(open, &QPushButton::clicked, mOpenVault, &QAction::trigger);
    layout->addStretch(1);
    layout->addWidget(heading, 0, Qt::AlignHCenter);
    layout->addWidget(mEmptyMessage, 0, Qt::AlignHCenter);
    layout->addWidget(open, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    return emptyState;
}

void Window::refresh()
{
    mPages->setCurrentIndex(mSession.hasVault() ? mPages->indexOf(mVaultView) : 0);
    const bool selected = !mSession.path().isEmpty();
    mFilename->setText(
        selected ? mSession.path() + (mSession.dirty() ? QStringLiteral(" *") : QString())
                 : QStringLiteral("Select a note"));
    mSave->setEnabled(selected && mSession.dirty());
    mFollow->setEnabled(selected);
    if (!mSession.dirty()) mConflictBar->hide();
    if (mSession.path() != mDeletePath) mDeleteBar->hide();
    mDelete->setEnabled(selected && !mSession.dirty());
    mMove->setEnabled(selected && !mSession.dirty());
    mCreate->setEnabled(mSession.hasVault() && !mSession.dirty());
    mEditor->setEnabled(selected);
}

void Window::replaceEditor(const QString &text)
{
    const QSignalBlocker block(mEditor);
    mEditor->setPlainText(text);
    refresh();
}

void Window::openVault()
{
    const auto folder = mPickFolder(this);
    if (!folder) return;
    QString error;
    if (!openVaultFolder(*folder, &error)) mStatus->setText(QStringLiteral("Error: ") + error);
}

bool Window::openVaultFolder(const QString &folder, QString *error)
{
    if (!mSession.openVault(folder, error)) return false;
    mSettings->setLastVault(folder);
    setWindowTitle(QStringLiteral("Inkstow - ") + QDir(folder).dirName());
    showNotes();
    replaceEditor(QString());
    watchNote();
    mStatus->setText(QStringLiteral("Found %1 notes").arg(mSession.notes().size()));
    return true;
}

void Window::reopenLastVault()
{
    const QString folder = mSettings->lastVault();
    if (folder.isEmpty()) return;
    QString error;
    if (!openVaultFolder(folder, &error))
        mEmptyMessage->setText(QStringLiteral("Could not reopen %1: %2").arg(folder, error));
}

void Window::selectNote(const QString &path)
{
    QString error;
    if (!mSession.selectNote(path, &error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        const QSignalBlocker block(mNotes);
        const auto matches = mNotes->findItems(mSession.path(), Qt::MatchExactly);
        mNotes->setCurrentItem(matches.isEmpty() ? nullptr : matches.first());
        return;
    }
    replaceEditor(mSession.content());
    watchNote();
    mEditor->setFocus();
    mStatus->setText(QStringLiteral("Opened ") + path);
}

void Window::followLink()
{
    if (mSession.path().isEmpty()) return;
    const int position = mEditor->textCursor().position();
    const QString text = mEditor->toPlainText();
    const QRegularExpression links(QStringLiteral(R"(\[\[([^\[\]\n]+)\]\])"));
    auto match = links.globalMatch(text);
    while (match.hasNext()) {
        const auto current = match.next();
        if (position < current.capturedStart() || position >= current.capturedEnd()) continue;
        const auto target = mSession.resolveWikilink(current.captured(1));
        if (!target) {
            mStatus->setText(QStringLiteral("Unresolved link"));
        } else if (!target->endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
            if (mSession.dirty()) {
                mStatus->setText(QStringLiteral("Error: Save the current note before switching."));
                return;
            }
            const auto path = mSession.attachmentPath(*target);
            if (!path || !mOpenAttachment(QUrl::fromLocalFile(*path))) {
                mStatus->setText(QStringLiteral("Error: Could not open attachment: ") + *target);
                return;
            }
            mStatus->setText(QStringLiteral("Opened attachment: ") + *target);
        } else {
            selectNote(*target); // Session refuses to abandon unsaved changes.
            if (mSession.path() == *target) {
                const QSignalBlocker block(mNotes);
                const auto items = mNotes->findItems(*target, Qt::MatchExactly);
                mNotes->setCurrentItem(items.isEmpty() ? nullptr : items.first());
            }
        }
        return;
    }
    mStatus->setText(QStringLiteral("Place the cursor inside a [[link]] to follow it."));
}

void Window::saveNote()
{
    QString error;
    if (!mSession.save(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        QString ignored;
        showConflictBar(mSession.syncWithDisk(&ignored));
    } else {
        mStatus->setText(QStringLiteral("Saved"));
    }
    refresh();
}

void Window::watchNote()
{
    mSyncTimer->stop();
    updateWatches();
}

void Window::updateWatches()
{
    QStringList folders = mSession.folders();
    const QString path = mSession.notePath();
    if (!path.isEmpty()) {
        QString folder = QFileInfo(path).absolutePath();
        // If the note's folder disappeared, an existing ancestor can report
        // its return even when the file and folder watches were dropped.
        while ((!QFileInfo(folder).isDir() || QFileInfo(folder).isSymLink()) &&
               QFileInfo(folder).absolutePath() != folder)
            folder = QFileInfo(folder).absolutePath();
        if (QFileInfo(folder).isDir() && !QFileInfo(folder).isSymLink() &&
            !folders.contains(folder))
            folders.append(folder);
    }
    for (const QString &watched : mWatcher->directories()) {
        if (!folders.contains(watched)) mWatcher->removePath(watched);
    }
    for (const QString &folder : folders) {
        if (QFileInfo(folder).isDir() && !QFileInfo(folder).isSymLink() &&
            !mWatcher->directories().contains(folder))
            mWatcher->addPath(folder);
    }
    for (const QString &watched : mWatcher->files()) {
        if (watched != path) mWatcher->removePath(watched);
    }
    if (!path.isEmpty() && QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() &&
        !mWatcher->files().contains(path))
        mWatcher->addPath(path);
}

void Window::syncWithDisk()
{
    QString error;
    const int position = mEditor->textCursor().position();
    const int scroll = mEditor->verticalScrollBar()->value();
    const DiskChange change = mSession.syncWithDisk(&error);
    switch (change) {
    case DiskChange::None:
        mConflictBar->hide(); // An outside change may have been reverted.
        break;
    case DiskChange::Reloaded: {
        replaceEditor(mSession.content());
        QTextCursor cursor = mEditor->textCursor();
        cursor.setPosition(std::min(position, mEditor->document()->characterCount() - 1));
        mEditor->setTextCursor(cursor);
        mEditor->verticalScrollBar()->setValue(scroll);
        mStatus->setText(
            QStringLiteral("Reloaded %1 after an outside change").arg(mSession.path()));
        break;
    }
    case DiskChange::Conflict:
    case DiskChange::Missing:
        mStatus->setText(QStringLiteral("Warning: ") + error);
        showConflictBar(change);
        break;
    }
    if (mSession.hasVault()) {
        const QStringList previous = mSession.notes();
        QString listError;
        if (mSession.reloadNotes(&listError) && previous != mSession.notes()) showNotes();
    }
    // File replacements and directory removal drop watches; reconcile after
    // every rescan, including when the note itself has disappeared.
    updateWatches();
}

// Only unsaved edits can be in conflict; a deleted note can only be let go.
void Window::showConflictBar(DiskChange change)
{
    const bool conflict = change == DiskChange::Conflict;
    const bool missing = change == DiskChange::Missing;
    if (!mSession.dirty() || !(conflict || missing)) {
        mConflictBar->hide();
        return;
    }
    mConflictLabel->setText(
        missing ? QStringLiteral(
                      "This note was deleted or moved outside "
                      "Inkstow. Copy any text you want to keep.")
                : QStringLiteral(
                      "This note changed outside Inkstow while you "
                      "had unsaved edits."));
    mReload->setHidden(missing);
    mKeepMine->setHidden(missing);
    mMerge->setHidden(missing);
    mSaveAsNew->setHidden(!missing);
    mDiscard->setHidden(!missing);
    mConflictBar->show();
}

void Window::reloadFromDisk()
{
    QString error;
    if (!mSession.reloadFromDisk(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    {
        // Replaced through a cursor, not setPlainText, so Undo restores the edits.
        const QSignalBlocker block(mEditor);
        QTextCursor cursor(mEditor->document());
        cursor.select(QTextCursor::Document);
        cursor.insertText(mSession.content());
    }
    mStatus->setText(QStringLiteral("Reloaded from disk"));
    refresh();
}

void Window::keepMine()
{
    QString error;
    if (!mSession.overwriteDisk(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        syncWithDisk(); // Report whatever is on disk now.
        return;
    }
    mStatus->setText(QStringLiteral("Saved your version"));
    refresh();
}

void Window::mergeOutsideChanges()
{
    QString error;
    if (!mSession.mergeOutsideChanges(&error)) {
        syncWithDisk(); // Refresh the conflict if the outside note changed again.
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    mConflictBar->hide();
    replaceEditor(mSession.content());
    mStatus->setText(QStringLiteral("Merged outside changes; review and Save."));
}

void Window::saveAsNewNote()
{
    QString error;
    if (!mSession.saveAsNewNote(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    showNotes();
    watchNote();
    refresh();
    mStatus->setText(QStringLiteral("Saved as new note"));
}

void Window::discardEdits()
{
    mSession.discardEdits();
    replaceEditor(mSession.content());
    mStatus->setText(QStringLiteral("Discarded unsaved edits"));
}

void Window::showNotes()
{
    const QSignalBlocker block(mNotes);
    mNotes->clear();
    mNotes->addItems(mSession.notes());
    const auto matches = mNotes->findItems(mSession.path(), Qt::MatchExactly);
    mNotes->setCurrentItem(matches.isEmpty() ? nullptr : matches.first());
}

void Window::createNote()
{
    QString error;
    if (!mSession.createNote(mNewPath->text(), &error)) {
        showNotes(); // The note may exist even if selecting it failed.
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    mNewPath->clear();
    showNotes();
    replaceEditor(mSession.content());
    watchNote();
    mEditor->setFocus();
    mStatus->setText(QStringLiteral("Created ") + mSession.path());
}

void Window::moveNote()
{
    if (mSession.path().isEmpty() || mSession.dirty()) return;
    bool accepted = false;
    const QString path = QInputDialog::getText(
        this,
        QStringLiteral("Rename / move note"),
        QStringLiteral("New vault-relative path (existing folders only):"),
        QLineEdit::Normal,
        mSession.path(),
        &accepted);
    if (!accepted || path == mSession.path()) return;
    QMessageBox confirmation(this);
    confirmation.setWindowTitle(QStringLiteral("Update wikilinks?"));
    confirmation.setText(QStringLiteral("Update links when moving this note?"));
    confirmation.setInformativeText(QStringLiteral(
        "Updating is safest. Don't update can leave links pointing to the old path."));
    auto *update = confirmation.addButton(QStringLiteral("Update links"), QMessageBox::AcceptRole);
    confirmation.addButton(
        QStringLiteral("Don't update (links may break)"),
        QMessageBox::DestructiveRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.setDefaultButton(qobject_cast<QPushButton *>(update));
    confirmation.exec();
    if (!confirmation.clickedButton() ||
        confirmation.clickedButton() == confirmation.button(QMessageBox::Cancel))
        return;
    const bool updateLinks = confirmation.clickedButton() == update;
    QString error;
    const bool moved = mSession.moveNote(path, updateLinks, &error);
    if (moved || error.startsWith(QStringLiteral("Move partially completed"))) {
        showNotes();
        replaceEditor(mSession.content());
        watchNote();
    }
    mStatus->setText(
        moved ? QStringLiteral("Moved to ") + mSession.path() : QStringLiteral("Error: ") + error);
}

void Window::trashNote()
{
    const QString path = mSession.path();
    QString error;
    mDeleteBar->hide();
    if (path != mDeletePath) {
        mStatus->setText(QStringLiteral("Error: the selected note changed; press Delete again."));
        return;
    }
    if (!mSession.trashNote(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    showNotes();
    replaceEditor(QString());
    watchNote();
    mStatus->setText(QStringLiteral("Moved %1 to the trash").arg(path));
}

void Window::changeEvent(QEvent *event)
{
    // Catches changes the watcher missed while the app was in the background.
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) syncWithDisk();
    QMainWindow::changeEvent(event);
}

void Window::closeEvent(QCloseEvent *event)
{
    if (mSession.dirty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Unsaved changes"),
            QStringLiteral("Save the current note before closing."));
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
