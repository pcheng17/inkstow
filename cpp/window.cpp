#include "window.h"

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

Window::Window() {
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
  toolbar->addWidget(filename_, 1);
  toolbar->addWidget(save_);
  rightLayout->addLayout(toolbar);
  rightLayout->addWidget(editor_, 1);
  splitter->addWidget(notes_);
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
  connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
    session_.setContent(editor_->toPlainText());
    refresh();
  });
  // The folder is watched too because atomic replacements by other programs
  // can drop the file from the watcher.
  connect(watcher_, &QFileSystemWatcher::fileChanged, syncTimer_,
          qOverload<>(&QTimer::start));
  connect(watcher_, &QFileSystemWatcher::directoryChanged, syncTimer_,
          qOverload<>(&QTimer::start));
  connect(syncTimer_, &QTimer::timeout, this, [this] { syncWithDisk(); });
  connect(notes_, &QListWidget::currentTextChanged, this,
          [this](const QString &path) {
            if (!path.isEmpty() && path != session_.path())
              selectNote(path);
          });
}

void Window::refresh() {
  const bool selected = !session_.path().isEmpty();
  filename_->setText(selected ? session_.path() + (session_.dirty()
                                                       ? QStringLiteral(" *")
                                                       : QString())
                              : QStringLiteral("Select a note"));
  save_->setEnabled(selected && session_.dirty());
  editor_->setEnabled(selected);
}

void Window::replaceEditor(const QString &text) {
  const QSignalBlocker block(editor_);
  editor_->setPlainText(text);
  refresh();
}

void Window::openVault() {
  QString error;
  if (!session_.openVault(folder_->text().trimmed(), &error)) {
    status_->setText(QStringLiteral("Error: ") + error);
    return;
  }
  {
    const QSignalBlocker block(notes_);
    notes_->clear();
    notes_->addItems(session_.notes());
  }
  replaceEditor(QString());
  watchNote();
  status_->setText(
      QStringLiteral("Found %1 notes").arg(session_.notes().size()));
}

void Window::selectNote(const QString &path) {
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

void Window::saveNote() {
  QString error;
  if (!session_.save(&error)) {
    status_->setText(QStringLiteral("Error: ") + error);
  } else {
    status_->setText(QStringLiteral("Saved"));
  }
  refresh();
}

void Window::watchNote() {
  syncTimer_->stop();
  const QStringList watched = watcher_->files() + watcher_->directories();
  if (!watched.isEmpty())
    watcher_->removePaths(watched);
  const QString path = session_.notePath();
  if (!path.isEmpty())
    watcher_->addPaths({path, QFileInfo(path).absolutePath()});
}

void Window::syncWithDisk() {
  QString error;
  const int position = editor_->textCursor().position();
  const int scroll = editor_->verticalScrollBar()->value();
  switch (session_.syncWithDisk(&error)) {
  case DiskChange::None:
    break;
  case DiskChange::Reloaded: {
    replaceEditor(session_.content());
    QTextCursor cursor = editor_->textCursor();
    cursor.setPosition(
        std::min(position, editor_->document()->characterCount() - 1));
    editor_->setTextCursor(cursor);
    editor_->verticalScrollBar()->setValue(scroll);
    status_->setText(QStringLiteral("Reloaded %1 after an outside change")
                         .arg(session_.path()));
    break;
  }
  case DiskChange::Conflict:
  case DiskChange::Missing:
    status_->setText(QStringLiteral("Warning: ") + error);
    break;
  }
  // Replacing or recreating the note or its folder drops it from the watcher.
  const QString path = session_.notePath();
  if (path.isEmpty())
    return;
  const QString folder = QFileInfo(path).absolutePath();
  if (!watcher_->directories().contains(folder) && QFileInfo::exists(folder))
    watcher_->addPath(folder);
  if (!watcher_->files().contains(path) && QFileInfo::exists(path))
    watcher_->addPath(path);
}

void Window::changeEvent(QEvent *event) {
  // Catches changes the watcher missed while the app was in the background.
  if (event->type() == QEvent::ActivationChange && isActiveWindow())
    syncWithDisk();
  QMainWindow::changeEvent(event);
}

void Window::closeEvent(QCloseEvent *event) {
  if (session_.dirty()) {
    QMessageBox::warning(
        this, QStringLiteral("Unsaved changes"),
        QStringLiteral("Save the current note before closing."));
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
}
