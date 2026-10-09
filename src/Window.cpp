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
    : session_(std::move(trash))
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
    folder_ = new QLineEdit;
    folder_->setObjectName(QStringLiteral("vaultFolder"));
    folder_->setPlaceholderText(QStringLiteral("/path/to/vault"));
    auto *open = new QPushButton(QStringLiteral("Open"));
    open->setObjectName(QStringLiteral("openVault"));
    header->addWidget(title);
    header->addSpacing(16);
    header->addWidget(new QLabel(QStringLiteral("Vault folder")));
    header->addWidget(folder_, 1);
    header->addWidget(open);
    page->addLayout(header);

    auto *splitter = new QSplitter;
    notes_ = new QListWidget;
    notes_->setObjectName(QStringLiteral("notes"));
    editor_ = new QPlainTextEdit;
    editor_->setObjectName(QStringLiteral("editor"));
    editor_->setPlaceholderText(QStringLiteral("Select a note"));
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    auto *toolbar = new QHBoxLayout;
    filename_ = new QLabel(QStringLiteral("Select a note"));
    save_ = new QPushButton(QStringLiteral("Save"));
    save_->setObjectName(QStringLiteral("saveNote"));
    delete_ = new QPushButton(QStringLiteral("Delete"));
    delete_->setObjectName(QStringLiteral("deleteNote"));
    toolbar->addWidget(filename_, 1);
    toolbar->addWidget(delete_);
    toolbar->addWidget(save_);
    rightLayout->addLayout(toolbar);
    conflictBar_ = new QWidget;
    conflictBar_->setObjectName(QStringLiteral("conflictBar"));
    auto *conflictLayout = new QHBoxLayout(conflictBar_);
    conflictLayout->setContentsMargins(0, 0, 0, 0);
    conflictLabel_ = new QLabel;
    reload_ = new QPushButton(QStringLiteral("Reload from disk"));
    reload_->setObjectName(QStringLiteral("reloadFromDisk"));
    keepMine_ = new QPushButton(QStringLiteral("Keep mine"));
    keepMine_->setObjectName(QStringLiteral("keepMine"));
    discard_ = new QPushButton(QStringLiteral("Discard edits"));
    discard_->setObjectName(QStringLiteral("discardEdits"));
    conflictLayout->addWidget(conflictLabel_, 1);
    conflictLayout->addWidget(reload_);
    conflictLayout->addWidget(keepMine_);
    conflictLayout->addWidget(discard_);
    conflictBar_->hide();
    rightLayout->addWidget(conflictBar_);
    deleteBar_ = new QWidget;
    deleteBar_->setObjectName(QStringLiteral("deleteBar"));
    auto *deleteLayout = new QHBoxLayout(deleteBar_);
    deleteLayout->setContentsMargins(0, 0, 0, 0);
    deleteLabel_ = new QLabel;
    auto *confirmDelete = new QPushButton(QStringLiteral("Move to trash"));
    confirmDelete->setObjectName(QStringLiteral("confirmDelete"));
    auto *cancelDelete = new QPushButton(QStringLiteral("Cancel"));
    cancelDelete->setObjectName(QStringLiteral("cancelDelete"));
    deleteLayout->addWidget(deleteLabel_, 1);
    deleteLayout->addWidget(confirmDelete);
    deleteLayout->addWidget(cancelDelete);
    deleteBar_->hide();
    rightLayout->addWidget(deleteBar_);
    rightLayout->addWidget(editor_, 1);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto *newNote = new QHBoxLayout;
    newPath_ = new QLineEdit;
    newPath_->setObjectName(QStringLiteral("newNotePath"));
    newPath_->setPlaceholderText(QStringLiteral("Folder/New note"));
    create_ = new QPushButton(QStringLiteral("Create"));
    create_->setObjectName(QStringLiteral("createNote"));
    newNote->addWidget(newPath_, 1);
    newNote->addWidget(create_);
    leftLayout->addWidget(notes_, 1);
    leftLayout->addLayout(newNote);
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 860});
    page->addWidget(splitter, 1);
    status_ = new QLabel(QStringLiteral("Open a vault folder to begin."));
    status_->setObjectName(QStringLiteral("status"));
    page->addWidget(status_);
    setCentralWidget(central);
    watcher_ = new QFileSystemWatcher(this);
    // Waits for a burst of writes to settle so a half-written note is not loaded.
    syncTimer_ = new QTimer(this);
    syncTimer_->setSingleShot(true);
    syncTimer_->setInterval(150);
    refresh();

    connect(open, &QPushButton::clicked, this, [this] { openVault(); });
    connect(folder_, &QLineEdit::returnPressed, this, [this] { openVault(); });
    connect(save_, &QPushButton::clicked, this, [this] { saveNote(); });
    connect(reload_, &QPushButton::clicked, this, [this] { reloadFromDisk(); });
    connect(keepMine_, &QPushButton::clicked, this, [this] { keepMine(); });
    connect(discard_, &QPushButton::clicked, this, [this] { discardEdits(); });
    connect(create_, &QPushButton::clicked, this, [this] { createNote(); });
    connect(newPath_, &QLineEdit::returnPressed, this, [this] { createNote(); });
    connect(delete_, &QPushButton::clicked, this, [this] {
        deletePath_ = session_.path();
        deleteLabel_->setText(QStringLiteral("Move %1 to the trash?").arg(deletePath_));
        deleteBar_->show();
    });
    connect(confirmDelete, &QPushButton::clicked, this, [this] { trashNote(); });
    connect(cancelDelete, &QPushButton::clicked, deleteBar_, &QWidget::hide);
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        session_.setContent(editor_->toPlainText());
        refresh();
    });
    // Watch vault folders for list changes and atomic replacements, which can
    // drop the selected file from the watcher.
    connect(watcher_, &QFileSystemWatcher::fileChanged, syncTimer_, qOverload<>(&QTimer::start));
    connect(
        watcher_,
        &QFileSystemWatcher::directoryChanged,
        syncTimer_,
        qOverload<>(&QTimer::start));
    connect(syncTimer_, &QTimer::timeout, this, [this] { syncWithDisk(); });
    connect(notes_, &QListWidget::currentTextChanged, this, [this](const QString &path) {
        if (!path.isEmpty() && path != session_.path()) selectNote(path);
    });
}

void Window::refresh()
{
    const bool selected = !session_.path().isEmpty();
    filename_->setText(
        selected ? session_.path() + (session_.dirty() ? QStringLiteral(" *") : QString())
                 : QStringLiteral("Select a note"));
    save_->setEnabled(selected && session_.dirty());
    if (!session_.dirty()) conflictBar_->hide();
    if (session_.path() != deletePath_) deleteBar_->hide();
    delete_->setEnabled(selected && !session_.dirty());
    create_->setEnabled(session_.hasVault() && !session_.dirty());
    editor_->setEnabled(selected);
}

void Window::replaceEditor(const QString &text)
{
    const QSignalBlocker block(editor_);
    editor_->setPlainText(text);
    refresh();
}

void Window::openVault()
{
    QString error;
    if (!session_.openVault(folder_->text().trimmed(), &error)) {
        status_->setText(QStringLiteral("Error: ") + error);
        return;
    }
    showNotes();
    replaceEditor(QString());
    watchNote();
    status_->setText(QStringLiteral("Found %1 notes").arg(session_.notes().size()));
}

void Window::selectNote(const QString &path)
{
    QString error;
    if (!session_.selectNote(path, &error)) {
        status_->setText(QStringLiteral("Error: ") + error);
        const QSignalBlocker block(notes_);
        const auto matches = notes_->findItems(session_.path(), Qt::MatchExactly);
        notes_->setCurrentItem(matches.isEmpty() ? nullptr : matches.first());
        return;
    }
    replaceEditor(session_.content());
    watchNote();
    editor_->setFocus();
    status_->setText(QStringLiteral("Opened ") + path);
}

void Window::saveNote()
{
    QString error;
    if (!session_.save(&error)) {
        status_->setText(QStringLiteral("Error: ") + error);
        QString ignored;
        showConflictBar(session_.syncWithDisk(&ignored));
    } else {
        status_->setText(QStringLiteral("Saved"));
    }
    refresh();
}

void Window::watchNote()
{
    syncTimer_->stop();
    updateWatches();
}

void Window::updateWatches()
{
    QStringList folders = session_.folders();
    const QString path = session_.notePath();
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
    for (const QString &watched : watcher_->directories()) {
        if (!folders.contains(watched)) watcher_->removePath(watched);
    }
    for (const QString &folder : folders) {
        if (QFileInfo(folder).isDir() && !QFileInfo(folder).isSymLink() &&
            !watcher_->directories().contains(folder))
            watcher_->addPath(folder);
    }
    for (const QString &watched : watcher_->files()) {
        if (watched != path) watcher_->removePath(watched);
    }
    if (!path.isEmpty() && QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() &&
        !watcher_->files().contains(path))
        watcher_->addPath(path);
}

void Window::syncWithDisk()
{
    QString error;
    const int position = editor_->textCursor().position();
    const int scroll = editor_->verticalScrollBar()->value();
    const DiskChange change = session_.syncWithDisk(&error);
    switch (change) {
    case DiskChange::None:
        conflictBar_->hide(); // An outside change may have been reverted.
        break;
    case DiskChange::Reloaded: {
        replaceEditor(session_.content());
        QTextCursor cursor = editor_->textCursor();
        cursor.setPosition(std::min(position, editor_->document()->characterCount() - 1));
        editor_->setTextCursor(cursor);
        editor_->verticalScrollBar()->setValue(scroll);
        status_->setText(
            QStringLiteral("Reloaded %1 after an outside change").arg(session_.path()));
        break;
    }
    case DiskChange::Conflict:
    case DiskChange::Missing:
        status_->setText(QStringLiteral("Warning: ") + error);
        showConflictBar(change);
        break;
    }
    if (session_.hasVault()) {
        const QStringList previous = session_.notes();
        QString listError;
        if (session_.reloadNotes(&listError) && previous != session_.notes()) showNotes();
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
    if (!session_.dirty() || !(conflict || missing)) {
        conflictBar_->hide();
        return;
    }
    conflictLabel_->setText(
        missing ? QStringLiteral(
                      "This note was deleted or moved outside "
                      "Inkstow. Copy any text you want to keep.")
                : QStringLiteral(
                      "This note changed outside Inkstow while you "
                      "had unsaved edits."));
    reload_->setHidden(missing);
    keepMine_->setHidden(missing);
    discard_->setHidden(!missing);
    conflictBar_->show();
}

void Window::reloadFromDisk()
{
    QString error;
    if (!session_.reloadFromDisk(&error)) {
        status_->setText(QStringLiteral("Error: ") + error);
        return;
    }
    {
        // Replaced through a cursor, not setPlainText, so Undo restores the edits.
        const QSignalBlocker block(editor_);
        QTextCursor cursor(editor_->document());
        cursor.select(QTextCursor::Document);
        cursor.insertText(session_.content());
    }
    status_->setText(QStringLiteral("Reloaded from disk"));
    refresh();
}

void Window::keepMine()
{
    QString error;
    if (!session_.overwriteDisk(&error)) {
        status_->setText(QStringLiteral("Error: ") + error);
        syncWithDisk(); // Report whatever is on disk now.
        return;
    }
    status_->setText(QStringLiteral("Saved your version"));
    refresh();
}

void Window::discardEdits()
{
    session_.discardEdits();
    replaceEditor(session_.content());
    status_->setText(QStringLiteral("Discarded unsaved edits"));
}

void Window::showNotes()
{
    const QSignalBlocker block(notes_);
    notes_->clear();
    notes_->addItems(session_.notes());
    const auto matches = notes_->findItems(session_.path(), Qt::MatchExactly);
    notes_->setCurrentItem(matches.isEmpty() ? nullptr : matches.first());
}

void Window::createNote()
{
    QString error;
    if (!session_.createNote(newPath_->text(), &error)) {
        showNotes(); // The note may exist even if selecting it failed.
        status_->setText(QStringLiteral("Error: ") + error);
        return;
    }
    newPath_->clear();
    showNotes();
    replaceEditor(session_.content());
    watchNote();
    editor_->setFocus();
    status_->setText(QStringLiteral("Created ") + session_.path());
}

void Window::trashNote()
{
    const QString path = session_.path();
    QString error;
    deleteBar_->hide();
    if (path != deletePath_) {
        status_->setText(QStringLiteral("Error: the selected note changed; press Delete again."));
        return;
    }
    if (!session_.trashNote(&error)) {
        status_->setText(QStringLiteral("Error: ") + error);
        return;
    }
    showNotes();
    replaceEditor(QString());
    watchNote();
    status_->setText(QStringLiteral("Moved %1 to the trash").arg(path));
}

void Window::changeEvent(QEvent *event)
{
    // Catches changes the watcher missed while the app was in the background.
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) syncWithDisk();
    QMainWindow::changeEvent(event);
}

void Window::closeEvent(QCloseEvent *event)
{
    if (session_.dirty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Unsaved changes"),
            QStringLiteral("Save the current note before closing."));
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
