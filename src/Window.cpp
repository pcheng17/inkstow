#include "Window.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

Window::Window(TrashFunction trash)
    : mSession(std::move(trash))
    , mWatcher(new QFileSystemWatcher(this))
    , mSyncTimer(new QTimer(this))
    , mFolder(new QLineEdit)
    , mNotes(new QListWidget)
    , mEditor(new QPlainTextEdit)
    , mNewPath(new QLineEdit)
    , mDeleteBar(new QWidget)
    , mDeleteLabel(new QLabel)
    , mConflictBar(new QWidget)
    , mConflictLabel(new QLabel)
{
    setWindowTitle(QStringLiteral("Inkstow"));
    resize(1100, 750);
    auto *central = new QWidget(this);
    auto *page = new QVBoxLayout(central);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Inkstow"));
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(14);
    title->setFont(titleFont);
    mFolder->setObjectName(QStringLiteral("vaultFolder"));
    mFolder->setPlaceholderText(QStringLiteral("/path/to/vault"));
    auto *open = new QPushButton(QStringLiteral("Open"));
    open->setObjectName(QStringLiteral("openVault"));
    header->addWidget(title);
    header->addSpacing(16);
    header->addWidget(new QLabel(QStringLiteral("Vault folder")));
    header->addWidget(mFolder, 1);
    header->addWidget(open);
    page->addLayout(header);

    auto *splitter = new QSplitter;
    mNotes->setObjectName(QStringLiteral("notes"));
    mEditor->setObjectName(QStringLiteral("editor"));
    mEditor->setPlaceholderText(QStringLiteral("Select a note"));
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    auto *toolbar = new QHBoxLayout;
    mFilename = new QLabel(QStringLiteral("Select a note"));
    mSave = new QPushButton(QStringLiteral("Save"));
    mSave->setObjectName(QStringLiteral("saveNote"));
    mDelete = new QPushButton(QStringLiteral("Delete"));
    mDelete->setObjectName(QStringLiteral("deleteNote"));
    toolbar->addWidget(mFilename, 1);
    toolbar->addWidget(mDelete);
    toolbar->addWidget(mSave);
    rightLayout->addLayout(toolbar);
    mConflictBar->setObjectName(QStringLiteral("conflictBar"));
    auto *conflictLayout = new QHBoxLayout(mConflictBar);
    conflictLayout->setContentsMargins(0, 0, 0, 0);
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
    deleteLayout->setContentsMargins(0, 0, 0, 0);
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
    leftLayout->setContentsMargins(0, 0, 0, 0);
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
    splitter->setSizes({240, 860});
    page->addWidget(splitter, 1);
    mStatus = new QLabel(QStringLiteral("Open a vault folder to begin."));
    mStatus->setObjectName(QStringLiteral("status"));
    page->addWidget(mStatus);
    setCentralWidget(central);
    // Waits for a burst of writes to settle so a half-written note is not loaded.
    mSyncTimer->setSingleShot(true);
    mSyncTimer->setInterval(150);
    Refresh();

    connect(open, &QPushButton::clicked, this, [this] { OpenVault(); });
    connect(mFolder, &QLineEdit::returnPressed, this, [this] { OpenVault(); });
    connect(mSave, &QPushButton::clicked, this, [this] { SaveNote(); });
    connect(mReload, &QPushButton::clicked, this, [this] { ReloadFromDisk(); });
    connect(mKeepMine, &QPushButton::clicked, this, [this] { KeepMine(); });
    connect(mMerge, &QPushButton::clicked, this, [this] { MergeOutsideChanges(); });
    connect(mSaveAsNew, &QPushButton::clicked, this, [this] { SaveAsNewNote(); });
    connect(mDiscard, &QPushButton::clicked, this, [this] { DiscardEdits(); });
    connect(mCreate, &QPushButton::clicked, this, [this] { CreateNote(); });
    connect(mNewPath, &QLineEdit::returnPressed, this, [this] { CreateNote(); });
    connect(mDelete, &QPushButton::clicked, this, [this] {
        mDeletePath = mSession.Path();
        mDeleteLabel->setText(QStringLiteral("Move %1 to the trash?").arg(mDeletePath));
        mDeleteBar->show();
    });
    connect(confirmDelete, &QPushButton::clicked, this, [this] { TrashNote(); });
    connect(cancelDelete, &QPushButton::clicked, mDeleteBar, &QWidget::hide);
    connect(mEditor, &QPlainTextEdit::textChanged, this, [this] {
        mSession.SetContent(mEditor->toPlainText());
        Refresh();
    });
    // Watch vault folders for list changes and atomic replacements, which can
    // drop the selected file from the watcher.
    connect(mWatcher, &QFileSystemWatcher::fileChanged, mSyncTimer, qOverload<>(&QTimer::start));
    connect(
        mWatcher,
        &QFileSystemWatcher::directoryChanged,
        mSyncTimer,
        qOverload<>(&QTimer::start));
    connect(mSyncTimer, &QTimer::timeout, this, [this] { SyncWithDisk(); });
    connect(mNotes, &QListWidget::currentTextChanged, this, [this](const QString &path) {
        if (!path.isEmpty() && path != mSession.Path()) SelectNote(path);
    });
}

void Window::Refresh()
{
    const bool selected = !mSession.Path().isEmpty();
    mFilename->setText(
        selected ? mSession.Path() + (mSession.Dirty() ? QStringLiteral(" *") : QString())
                 : QStringLiteral("Select a note"));
    mSave->setEnabled(selected && mSession.Dirty());
    if (!mSession.Dirty()) mConflictBar->hide();
    if (mSession.Path() != mDeletePath) mDeleteBar->hide();
    mDelete->setEnabled(selected && !mSession.Dirty());
    mCreate->setEnabled(mSession.HasVault() && !mSession.Dirty());
    mEditor->setEnabled(selected);
}

void Window::ReplaceEditor(const QString &text)
{
    const QSignalBlocker block(mEditor);
    mEditor->setPlainText(text);
    Refresh();
}

void Window::OpenVault()
{
    QString error;
    if (!mSession.OpenVault(mFolder->text().trimmed(), &error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    ShowNotes();
    ReplaceEditor(QString());
    WatchNote();
    mStatus->setText(QStringLiteral("Found %1 notes").arg(mSession.Notes().size()));
}

void Window::SelectNote(const QString &path)
{
    QString error;
    if (!mSession.SelectNote(path, &error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        const QSignalBlocker block(mNotes);
        const auto matches = mNotes->findItems(mSession.Path(), Qt::MatchExactly);
        mNotes->setCurrentItem(matches.isEmpty() ? nullptr : matches.first());
        return;
    }
    ReplaceEditor(mSession.Content());
    WatchNote();
    mEditor->setFocus();
    mStatus->setText(QStringLiteral("Opened ") + path);
}

void Window::SaveNote()
{
    QString error;
    if (!mSession.Save(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        QString ignored;
        ShowConflictBar(mSession.SyncWithDisk(&ignored));
    } else {
        mStatus->setText(QStringLiteral("Saved"));
    }
    Refresh();
}

void Window::WatchNote()
{
    mSyncTimer->stop();
    UpdateWatches();
}

void Window::UpdateWatches()
{
    QStringList folders = mSession.Folders();
    const QString path = mSession.NotePath();
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

void Window::SyncWithDisk()
{
    QString error;
    const int position = mEditor->textCursor().position();
    const int scroll = mEditor->verticalScrollBar()->value();
    const DiskChange change = mSession.SyncWithDisk(&error);
    switch (change) {
    case DiskChange::None:
        mConflictBar->hide(); // An outside change may have been reverted.
        break;
    case DiskChange::Reloaded: {
        ReplaceEditor(mSession.Content());
        QTextCursor cursor = mEditor->textCursor();
        cursor.setPosition(std::min(position, mEditor->document()->characterCount() - 1));
        mEditor->setTextCursor(cursor);
        mEditor->verticalScrollBar()->setValue(scroll);
        mStatus->setText(
            QStringLiteral("Reloaded %1 after an outside change").arg(mSession.Path()));
        break;
    }
    case DiskChange::Conflict:
    case DiskChange::Missing:
        mStatus->setText(QStringLiteral("Warning: ") + error);
        ShowConflictBar(change);
        break;
    }
    if (mSession.HasVault()) {
        const QStringList previous = mSession.Notes();
        QString listError;
        if (mSession.ReloadNotes(&listError) && previous != mSession.Notes()) ShowNotes();
    }
    // File replacements and directory removal drop watches; reconcile after
    // every rescan, including when the note itself has disappeared.
    UpdateWatches();
}

// Only unsaved edits can be in conflict; a deleted note can only be let go.
void Window::ShowConflictBar(DiskChange change)
{
    const bool conflict = change == DiskChange::Conflict;
    const bool missing = change == DiskChange::Missing;
    if (!mSession.Dirty() || !(conflict || missing)) {
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

void Window::ReloadFromDisk()
{
    QString error;
    if (!mSession.ReloadFromDisk(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    {
        // Replaced through a cursor, not setPlainText, so Undo restores the edits.
        const QSignalBlocker block(mEditor);
        QTextCursor cursor(mEditor->document());
        cursor.select(QTextCursor::Document);
        cursor.insertText(mSession.Content());
    }
    mStatus->setText(QStringLiteral("Reloaded from disk"));
    Refresh();
}

void Window::KeepMine()
{
    QString error;
    if (!mSession.OverwriteDisk(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        SyncWithDisk(); // Report whatever is on disk now.
        return;
    }
    mStatus->setText(QStringLiteral("Saved your version"));
    Refresh();
}

void Window::MergeOutsideChanges()
{
    QString error;
    if (!mSession.MergeOutsideChanges(&error)) {
        SyncWithDisk(); // Refresh the conflict if the outside note changed again.
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    mConflictBar->hide();
    ReplaceEditor(mSession.Content());
    mStatus->setText(QStringLiteral("Merged outside changes; review and Save."));
}

void Window::SaveAsNewNote()
{
    QString error;
    if (!mSession.SaveAsNewNote(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    ShowNotes();
    WatchNote();
    Refresh();
    mStatus->setText(QStringLiteral("Saved as new note"));
}

void Window::DiscardEdits()
{
    mSession.DiscardEdits();
    ReplaceEditor(mSession.Content());
    mStatus->setText(QStringLiteral("Discarded unsaved edits"));
}

void Window::ShowNotes()
{
    const QSignalBlocker block(mNotes);
    mNotes->clear();
    mNotes->addItems(mSession.Notes());
    const auto matches = mNotes->findItems(mSession.Path(), Qt::MatchExactly);
    mNotes->setCurrentItem(matches.isEmpty() ? nullptr : matches.first());
}

void Window::CreateNote()
{
    QString error;
    if (!mSession.CreateNote(mNewPath->text(), &error)) {
        ShowNotes(); // The note may exist even if selecting it failed.
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    mNewPath->clear();
    ShowNotes();
    ReplaceEditor(mSession.Content());
    WatchNote();
    mEditor->setFocus();
    mStatus->setText(QStringLiteral("Created ") + mSession.Path());
}

void Window::TrashNote()
{
    const QString path = mSession.Path();
    QString error;
    mDeleteBar->hide();
    if (path != mDeletePath) {
        mStatus->setText(QStringLiteral("Error: the selected note changed; press Delete again."));
        return;
    }
    if (!mSession.TrashNote(&error)) {
        mStatus->setText(QStringLiteral("Error: ") + error);
        return;
    }
    ShowNotes();
    ReplaceEditor(QString());
    WatchNote();
    mStatus->setText(QStringLiteral("Moved %1 to the trash").arg(path));
}

void Window::changeEvent(QEvent *event)
{
    // Catches changes the watcher missed while the app was in the background.
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) SyncWithDisk();
    QMainWindow::changeEvent(event);
}

void Window::closeEvent(QCloseEvent *event)
{
    if (mSession.Dirty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Unsaved changes"),
            QStringLiteral("Save the current note before closing."));
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
